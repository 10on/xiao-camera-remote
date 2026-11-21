#include "phone_device.h"

#include <Preferences.h>
#include <string.h>

#include "../battery.h"
#include "../dvh_link.h"
#include "../theme.h"

PhoneDevice phoneDevice(0);
PhoneDevice phoneDevice2(1);
PhoneDevice phoneDevice3(2);
PhoneDevice phoneDevice4(3);

namespace {

constexpr uint8_t kReportId = 1;
constexpr uint16_t kConsumerVolumeIncrement = 0x00E9;
constexpr uint16_t kNone = 0xFFFF;

// One 16-bit Consumer Control usage per report. Volume Increment is what
// commodity BLE camera shutters send; native iOS/Android camera apps map it
// to the photo shutter or video start/stop action.
uint8_t kConsumerReportMap[] = {
	0x05, 0x0C,       // Usage Page (Consumer)
	0x09, 0x01,       // Usage (Consumer Control)
	0xA1, 0x01,       // Collection (Application)
	0x85, kReportId,  //   Report ID
	0x15, 0x00,       //   Logical Minimum (0)
	0x26, 0xFF, 0x03, //   Logical Maximum (1023)
	0x19, 0x00,       //   Usage Minimum (0)
	0x2A, 0xFF, 0x03, //   Usage Maximum (1023)
	0x75, 0x10,       //   Report Size (16)
	0x95, 0x01,       //   Report Count (1)
	0x81, 0x00,       //   Input (Data, Array, Absolute)
	0xC0,             // End Collection
};

const char *kNames[PhoneHub::kSlots] = {"Phone 1", "Phone 2", "Phone 3", "Phone 4"};
const char *kNvsNamespace = "phones";
const char *kSlotKeys[PhoneHub::kSlots] = {"s0", "s1", "s2", "s3"};
const char *kMacKey = "mac";

struct Slot {
	bool bound = false;
	ble_addr_t addr = {};
	uint16_t conn = kNone;
};

struct Pending {
	bool used = false;
	uint16_t conn = kNone;
	ble_addr_t id = {};
	uint32_t atMs = 0;
};

PhoneDevice *const kDevices[PhoneHub::kSlots] = {&phoneDevice, &phoneDevice2, &phoneDevice3,
                                                 &phoneDevice4};

// Shared between the loop task and the NimBLE host task (callbacks).
SemaphoreHandle_t g_mutex = nullptr; // PhoneHub::begin(), before BLE starts
struct Lock {
	Lock() { xSemaphoreTake(g_mutex, portMAX_DELAY); }
	~Lock() { xSemaphoreGive(g_mutex); }
};
Slot g_slots[PhoneHub::kSlots];
Pending g_pending[6];
bool g_macKnown = false;
ble_addr_t g_mac = {};
bool g_dirty = false; // bindings changed -> persist from tick()

NimBLEServer *g_server = nullptr;
NimBLEHIDDevice *g_hid = nullptr;
NimBLECharacteristic *g_consumerInput = nullptr;
uint32_t g_lastBatteryMs = 0;

bool sameAddr(const ble_addr_t &a, const ble_addr_t &b) {
	return a.type == b.type && memcmp(a.val, b.val, sizeof(a.val)) == 0;
}

int slotOfAddr(const ble_addr_t &a) {
	for (int i = 0; i < PhoneHub::kSlots; i++)
		if (g_slots[i].bound && sameAddr(g_slots[i].addr, a)) return i;
	return -1;
}

void forgetConn(uint16_t h) {
	for (auto &p : g_pending)
		if (p.used && p.conn == h) p.used = false;
	for (auto &s : g_slots)
		if (s.conn == h) s.conn = kNone;
}

void saveBindings() {
	Preferences prefs;
	prefs.begin(kNvsNamespace, false);
	for (int i = 0; i < PhoneHub::kSlots; i++) {
		if (g_slots[i].bound) prefs.putBytes(kSlotKeys[i], &g_slots[i].addr, sizeof(ble_addr_t));
		else if (prefs.isKey(kSlotKeys[i])) prefs.remove(kSlotKeys[i]);
	}
	if (g_macKnown) prefs.putBytes(kMacKey, &g_mac, sizeof(ble_addr_t));
	prefs.end();
}

// Decide one encrypted peer (loop task). Returns the handle to disconnect,
// or kNone to keep the link.
uint16_t decide(const Pending &p) {
	int s = slotOfAddr(p.id);
	if (s < 0) {
		// New phone: first free slot the current rig wants, else any free one.
		for (int i = 0; i < PhoneHub::kSlots && s < 0; i++)
			if (!g_slots[i].bound && kDevices[i]->isActive()) s = i;
		for (int i = 0; i < PhoneHub::kSlots && s < 0; i++)
			if (!g_slots[i].bound) s = i;
		if (s < 0) {
			Serial.printf("[phone] conn %u: all %d slots taken\n", p.conn, PhoneHub::kSlots);
			return p.conn;
		}
		g_slots[s].bound = true;
		g_slots[s].addr = p.id;
		g_dirty = true;
		Serial.printf("[phone] new phone -> %s\n", kNames[s]);
	}
	if (g_slots[s].conn != kNone && g_slots[s].conn != p.conn) {
		Serial.printf("[phone] %s conn %u: replaces conn %u\n", kNames[s], p.conn, g_slots[s].conn);
	}
	g_slots[s].conn = p.conn;
	Serial.printf("[phone] %s ready conn %u%s\n", kNames[s], p.conn,
	              kDevices[s]->isActive() ? "" : " (not in this rig)");
	return kNone;
}

} // namespace

// ===========================================================================
// PhoneHub
// ===========================================================================

PhoneDevice *PhoneHub::at(int slot) {
	return slot >= 0 && slot < kSlots ? kDevices[slot] : nullptr;
}

void PhoneHub::begin() {
	if (!g_mutex) g_mutex = xSemaphoreCreateMutex();
	Preferences prefs;
	prefs.begin(kNvsNamespace, false);
	for (int i = 0; i < kSlots; i++) {
		ble_addr_t a;
		if (prefs.getBytes(kSlotKeys[i], &a, sizeof(a)) == sizeof(a)) {
			g_slots[i].bound = true;
			g_slots[i].addr = a;
		}
	}
	if (prefs.getBytes(kMacKey, &g_mac, sizeof(g_mac)) == sizeof(g_mac)) g_macKnown = true;
	prefs.end();
}

void PhoneHub::setupHid(NimBLEServer *server) {
	g_server = server;
	// The Mac links through its own advertising set without pairing (see
	// BleServer). Drop its old keyboard bond so macOS can't resume the HID
	// link on the phone set.
	if (g_macKnown && NimBLEDevice::isBonded(NimBLEAddress(g_mac))) {
		bool ok = NimBLEDevice::deleteBond(NimBLEAddress(g_mac));
		Serial.printf("[phone] Mac bond %s deleted: %d\n", NimBLEAddress(g_mac).toString().c_str(), ok);
	}
	g_hid = new NimBLEHIDDevice(server);
	g_hid->setManufacturer("XIAO Remote");
	g_hid->setPnp(0x02, 0x303A, 0x4001, 0x0100); // USB-IF source, Espressif VID
	g_hid->setHidInfo(0x00, 0x02);                // normally-connectable HID
	g_hid->setReportMap(kConsumerReportMap, sizeof(kConsumerReportMap));
	g_consumerInput = g_hid->getInputReport(kReportId);
	g_hid->setBatteryLevel(battery.percent());
}

void PhoneHub::tick() {
	uint32_t now = millis();
	uint16_t drop[6];
	int nDrop = 0;

	bool dirty;
	{
	Lock lock;
	for (auto &p : g_pending) {
		if (!p.used) continue;
		bool known = slotOfAddr(p.id) >= 0;
		if (!known && now - p.atMs < kDecideDelayMs) continue; // give the Mac time to subscribe
		uint16_t h = decide(p);
		p.used = false;
		if (h != kNone) drop[nDrop++] = h;
	}
	dirty = g_dirty;
	g_dirty = false;
	}

	for (int i = 0; i < nDrop; i++)
		if (g_server) g_server->disconnect(drop[i]);
	if (dirty) saveBindings();

	if (g_hid && now - g_lastBatteryMs >= 30000) {
		bool any = false;
		for (int i = 0; i < kSlots; i++) any = any || connected(i);
		g_hid->setBatteryLevel(battery.percent(), any);
		g_lastBatteryMs = now;
	}
}

// Every bonded phone stays linked, in the rig or not (iOS stops
// reconnecting on its own after the remote drops it a few times); plus a
// rig slot still waiting for its first phone.
bool PhoneHub::wantsAdvertising() {
	for (int i = 0; i < kSlots; i++)
		// Only the rig's slots: advertising for a bound phone that is away
		// (16 Pro not on the set) kept the single radio busy and the Mac's
		// 2 s supervision timeout ran out. Phones outside the rig link up
		// while we advertise for the Mac or once their slot is in a rig.
		if (kDevices[i]->isActive() && !connected(i)) return true;
	return false;
}

void PhoneHub::onEncrypted(const NimBLEConnInfo &connInfo) {
	uint16_t h = connInfo.getConnHandle();
	if (dvhLink.isMac(h)) return;
	ble_addr_t id = *connInfo.getIdAddress().getBase();

	Lock lock;
	bool isMac = g_macKnown && sameAddr(g_mac, id);
	bool queued = false;
	for (auto &p : g_pending) queued = queued || (p.used && p.conn == h);
	for (auto &s : g_slots) queued = queued || s.conn == h; // re-encryption of a known link
	if (!isMac && !queued) {
		for (auto &p : g_pending) {
			if (p.used) continue;
			p.used = true;
			p.conn = h;
			p.id = id;
			p.atMs = millis();
			break;
		}
	}
}

void PhoneHub::onPeerDisconnected(uint16_t h) {
	Lock lock;
	forgetConn(h);
}

void PhoneHub::onMacSubscribed(const NimBLEConnInfo &connInfo) {
	uint16_t h = connInfo.getConnHandle();
	ble_addr_t id = *connInfo.getIdAddress().getBase();
	Lock lock;
	forgetConn(h);
	if (!g_macKnown || !sameAddr(g_mac, id)) {
		g_mac = id;
		g_macKnown = true;
		g_dirty = true;
	}
	int s = slotOfAddr(id); // taken for a phone before it subscribed
	if (s >= 0) {
		g_slots[s].bound = false;
		g_dirty = true;
	}
}

bool PhoneHub::connected(int slot) {
	return slot >= 0 && slot < kSlots && g_slots[slot].conn != kNone;
}

bool PhoneHub::bound(int slot) { return slot >= 0 && slot < kSlots && g_slots[slot].bound; }

void PhoneHub::dropSlot(int slot) {
	if (slot < 0 || slot >= kSlots) return;
	uint16_t h;
	{
		Lock lock;
		h = g_slots[slot].conn;
		g_slots[slot].conn = kNone;
	}
	if (h != kNone && g_server) g_server->disconnect(h);
}

void PhoneHub::forgetSlot(int slot) {
	if (slot < 0 || slot >= kSlots) return;
	dropSlot(slot);
	bool was;
	ble_addr_t a;
	{
		Lock lock;
		was = g_slots[slot].bound;
		a = g_slots[slot].addr;
		g_slots[slot].bound = false;
	}
	if (!was) return;
	NimBLEDevice::deleteBond(NimBLEAddress(a));
	saveBindings();
	Serial.printf("[phone] %s forgotten\n", kNames[slot]);
}

void PhoneHub::notify(int slot, uint16_t usage) {
	if (!g_consumerInput || !connected(slot)) return;
	g_consumerInput->notify(reinterpret_cast<uint8_t *>(&usage), sizeof(usage), g_slots[slot].conn);
}

bool PhoneHub::addressOf(int slot, NimBLEAddress &out) {
	if (!bound(slot)) return false;
	out = NimBLEAddress(g_slots[slot].addr);
	return true;
}

// ===========================================================================
// PhoneDevice
// ===========================================================================

const char *PhoneDevice::name() const { return kNames[_slot]; }

uint16_t PhoneDevice::identityColor565() const { return theme::kPhoneFill; }
uint16_t PhoneDevice::identityTextColor565() const { return theme::kPhoneText; }

// Phone body outline + speaker slot (mock icon set).
void PhoneDevice::drawGlyph(Adafruit_GFX &g, int16_t x, int16_t y, int16_t s, uint16_t c) const {
	int16_t iw = s * 10 / 26, ih = s * 15 / 26;
	if (iw < 8) iw = 8;
	int16_t ix = x + (s - iw) / 2, iy = y + (s - ih) / 2;
	g.drawRoundRect(ix, iy, iw, ih, 2, c);
	g.drawRoundRect(ix + 1, iy + 1, iw - 2, ih - 2, 2, c); // ~2px stroke
	g.fillRect(ix + iw / 2 - 1, iy + 3, 3, 1, c);          // speaker
}

bool PhoneDevice::isConnected() const { return PhoneHub::connected(_slot); }

// The link stays: a phone outside the rig just gets no clicks.
void PhoneDevice::deactivate() {
	_active = false;
	_releasePending = false;
}

void PhoneDevice::tick() {
	if (_slot == 0) PhoneHub::tick(); // the hub ticks once, with the first slot
	if (_releasePending && millis() - _pressedAtMs >= 12) {
		PhoneHub::notify(_slot, 0);
		_releasePending = false;
	}
}

// A HID key is an edge, not a persistent state. The release is sent from
// tick() after a short hold so the stack can't coalesce them.
void PhoneDevice::handleCommand(Command cmd) {
	if (cmd != Command::Record && cmd != Command::StopRecord) return;
	if (!isConnected()) return;
	PhoneHub::notify(_slot, kConsumerVolumeIncrement);
	_pressedAtMs = millis();
	_releasePending = true;
}

void PhoneDevice::runServiceAction(int) { PhoneHub::forgetSlot(_slot); }

bool PhoneDevice::serviceInfo(int i, const char *&key, char *val, size_t valLen) const {
	if (i != 0) return false;
	key = "PHONE";
	NimBLEAddress a;
	if (PhoneHub::addressOf(_slot, a)) snprintf(val, valLen, "%s", a.toString().c_str());
	else snprintf(val, valLen, "not paired");
	return true;
}

void PhoneDevice::renderStatusLine(Adafruit_GFX &tft, int16_t y) {
	tft.setCursor(theme::kPadH, y);
	tft.setTextSize(theme::kSizeBody);
	if (isConnected()) {
		tft.setTextColor(theme::kTextPrimary);
		tft.printf("%s: ready", name());
	} else {
		tft.setTextColor(_active ? theme::kWarnFill : theme::kTextInactive);
		tft.printf("%s: %s", name(), _active ? "waiting..." : "off");
	}
}

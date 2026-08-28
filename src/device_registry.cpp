#include "device_registry.h"

#include <Preferences.h>
#include <string.h>

#include "device_table.h"
#include "rig_store.h"

DeviceRegistry deviceRegistry;

namespace {
const char *kNamespace = "devreg";
const char *kKeyAlias[] = {"a0", "a1", "a2", "a3"};
} // namespace

void DeviceRegistry::begin() {
	Preferences prefs;
	prefs.begin(kNamespace, /*readOnly=*/false); // RW so a fresh namespace isn't a NOT_FOUND log
	for (int i = 0; i < deviceCount() && i < DeviceRegistry::kMaxDevices; i++) {
		if (!prefs.isKey(kKeyAlias[i])) continue; // no alias set -> keep Device::name() fallback
		String s = prefs.getString(kKeyAlias[i], "");
		strncpy(_alias[i], s.c_str(), kAliasMax);
		_alias[i][kAliasMax] = '\0';
	}
	prefs.end();
}

void DeviceRegistry::tick() {
	for (int i = 0; i < deviceCount() && i < DeviceRegistry::kMaxDevices; i++) {
		Device *d = deviceAt(i);
		if (!d) continue;
		if (d->isConnected()) _seen[i] = SeenState::Connected;
		else if (_seen[i] == SeenState::Connected) _seen[i] = SeenState::EarlierThisBoot;
	}
}

const char *DeviceRegistry::alias(int deviceIndex) const {
	if (deviceIndex >= 0 && deviceIndex < kMaxDevices && _alias[deviceIndex][0] != '\0')
		return _alias[deviceIndex];
	Device *d = deviceAt(deviceIndex);
	return d ? d->name() : "";
}

void DeviceRegistry::setAlias(int deviceIndex, const char *s) {
	if (deviceIndex < 0 || deviceIndex >= kMaxDevices) return;
	strncpy(_alias[deviceIndex], s ? s : "", kAliasMax);
	_alias[deviceIndex][kAliasMax] = '\0';
	Preferences prefs;
	prefs.begin(kNamespace, false);
	prefs.putString(kKeyAlias[deviceIndex], _alias[deviceIndex]);
	prefs.end();
}

SeenState DeviceRegistry::seen(int deviceIndex) const {
	if (deviceIndex < 0 || deviceIndex >= kMaxDevices) return SeenState::NeverThisBoot;
	return _seen[deviceIndex];
}

int DeviceRegistry::rigMembership(int deviceIndex) const {
	int n = 0;
	for (int i = 0; i < rigStore.count(); i++)
		if (rigStore.at(i).references(deviceIndex)) n++;
	return n;
}

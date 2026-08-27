#include "rig_store.h"

#include <Preferences.h>
#include <initializer_list>
#include <string.h>

RigStore rigStore;

namespace {
const char *kNamespace = "rigs";
const char *kKeyBlob = "blob";
const char *kKeyCount = "count";
// Bump when the Rig layout changes so a stale blob is discarded, not
// misread. Stored as the first 4 bytes of the blob.
const uint32_t kLayoutVersion = 0x52494702; // 'R','I','G',2 — reseed for slider program takeMode
} // namespace

// Device indices into menu.cpp kDevices: sliderDevice=0, phoneDevice=1, dollyDevice=2.
void RigStore::seedDefaults() {
	auto mk = [](const char *name, int8_t main, std::initializer_list<int8_t> sec,
	             TakeMode tm) -> Rig {
		Rig r{};
		strncpy(r.name, name, kRigNameMax);
		r.name[kRigNameMax] = '\0';
		r.mainIndex = main;
		r.secondaryCount = 0;
		for (int8_t s : sec) {
			if (r.secondaryCount < kMaxSecondaryPerRig) r.secondary[r.secondaryCount++] = s;
		}
		r.takeMode = tm;
		return r;
	};

	// Slider rigs default to RecordAndMoveMain: with the program API the
	// slider owns a safe, explicit trajectory (Ping-Pong), so "OK = REC +
	// run the shot" is the natural, clearly-labelled default. Dolly keeps
	// RecordOnly (raw jog, no program) — motion stays on the arrows.
	_count = 0;
	_rigs[_count++] = mk("Slider + Phone", 0, {1}, TakeMode::RecordAndMoveMain);
	_rigs[_count++] = mk("Dolly + Phone", 2, {1}, TakeMode::RecordOnly);
	_rigs[_count++] = mk("Slider", 0, {}, TakeMode::RecordAndMoveMain);
	_rigs[_count++] = mk("Dolly", 2, {}, TakeMode::RecordOnly);
	_rigs[_count++] = mk("Phones", -1, {1}, TakeMode::RecordOnly);
}

void RigStore::begin() {
	Preferences prefs;
	prefs.begin(kNamespace, /*readOnly=*/true);
	uint32_t version = 0;
	size_t got = prefs.getBytes("ver", &version, sizeof(version));
	int count = prefs.getInt(kKeyCount, 0);
	bool ok = false;
	if (got == sizeof(version) && version == kLayoutVersion && count > 0 && count <= kMaxRigs) {
		size_t want = (size_t)count * sizeof(Rig);
		if (prefs.getBytesLength(kKeyBlob) == want) {
			prefs.getBytes(kKeyBlob, _rigs, want);
			_count = count;
			ok = true;
		}
	}
	prefs.end();

	if (!ok) {
		seedDefaults();
		save();
	}
}

void RigStore::save() const {
	Preferences prefs;
	prefs.begin(kNamespace, false);
	uint32_t version = kLayoutVersion;
	prefs.putBytes("ver", &version, sizeof(version));
	prefs.putInt(kKeyCount, _count);
	prefs.putBytes(kKeyBlob, _rigs, (size_t)_count * sizeof(Rig));
	prefs.end();
}

Rig RigStore::blank() {
	Rig r{};
	strncpy(r.name, "New rig", kRigNameMax);
	r.mainIndex = 0; // Slider
	r.secondaryCount = 0;
	r.takeMode = TakeMode::RecordOnly;
	return r;
}

int RigStore::add(const Rig &r) {
	if (_count >= kMaxRigs) return -1;
	_rigs[_count] = r;
	int idx = _count++;
	save();
	return idx;
}

void RigStore::replace(int i, const Rig &r) {
	if (i < 0 || i >= _count) return;
	_rigs[i] = r;
	save();
}

void RigStore::remove(int i) {
	if (i < 0 || i >= _count) return;
	for (int j = i; j < _count - 1; j++) _rigs[j] = _rigs[j + 1];
	_count--;
	save();
}

int RigStore::duplicate(int i) {
	if (i < 0 || i >= _count || _count >= kMaxRigs) return -1;
	Rig copy = _rigs[i];
	size_t len = strlen(copy.name);
	if (len + 2 <= kRigNameMax) {
		copy.name[len] = ' ';
		copy.name[len + 1] = '2';
		copy.name[len + 2] = '\0';
	}
	return add(copy);
}

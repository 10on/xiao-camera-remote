#pragma once

#include <stddef.h>
#include <stdint.h>

// Shooting-rig model — authoritative UI/UX spec:
// docs/design/ux-redesign.md + the pixel mock in
// docs/design/ux-redesign-mock/new-model.dc.html.
//
// A Rig is a named set of roles: one optional Main (a Motion device that
// drives the control screen) plus zero or more Secondary devices (phones —
// the REC block). Launching a rig auto-connects exactly its participants
// (see Menu::launchRig). Rigs are user-editable and persisted — see
// rig_store.h.

// How Ok behaves on the control screen during a take (ux-redesign.md §8,
// mock screen 13).
enum class TakeMode : uint8_t {
	RecordOnly = 0,        // "Запись": Ok drives phones only; Main motion stays on the arrows
	RecordAndMoveMain = 1, // "Запись + движение": Ok also starts/stops a preset Main move
};

static const int kMaxSecondaryPerRig = 4;
static const int kRigNameMax = 20; // visible chars; buffer is +1 for the NUL

struct Rig {
	char name[kRigNameMax + 1];
	int8_t mainIndex;                       // index into menu.cpp kDevices, or -1
	int8_t secondary[kMaxSecondaryPerRig];  // device indices
	int8_t secondaryCount;
	TakeMode takeMode;

	bool hasSecondary(int deviceIndex) const;
	bool references(int deviceIndex) const; // Main or any Secondary slot
};

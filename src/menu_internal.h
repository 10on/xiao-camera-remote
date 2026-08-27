#pragma once

#include "command.h"
#include "device_table.h"
#include "rig.h"
#include "rig_store.h"

// Shared, session-agnostic helpers used by both menu.cpp (input/lifecycle)
// and menu_render.cpp (drawing). All take an explicit rig index — the live
// session's index lives on the Menu instance.

// How long Connecting waits before swapping its hint to a retry prompt
// (ux-redesign.md §6). The scan keeps running regardless.
static const uint32_t kConnectTimeoutMs = 25000;

inline const Rig *rigAt(int index) {
	if (index < 0 || index >= rigStore.count()) return nullptr;
	return &rigStore.at(index);
}

inline Device *rigMainOf(int rigIndex) {
	const Rig *r = rigAt(rigIndex);
	if (!r) return nullptr;
	return deviceAt(r->mainIndex); // deviceAt(-1) -> nullptr
}

// (ready, total) cameras in a rig's Secondary list. A single phone slot can
// represent several connected phones (Device::cameraReady/cameraTotal).
inline void rigPhoneCounts(int rigIndex, int &ready, int &total) {
	ready = total = 0;
	const Rig *r = rigAt(rigIndex);
	if (!r) return;
	for (int i = 0; i < r->secondaryCount; i++) {
		Device *d = deviceAt(r->secondary[i]);
		if (!d || d->kind() != DeviceKind::Camera) continue;
		ready += d->cameraReady();
		total += d->cameraTotal();
	}
}

inline void fanOutAll(Command cmd) {
	for (int i = 0; i < deviceCount(); i++)
		if (deviceAt(i)->isActive()) deviceAt(i)->handleCommand(cmd);
}

inline void fanToPhones(int rigIndex, Command cmd) {
	const Rig *r = rigAt(rigIndex);
	if (!r) return;
	for (int i = 0; i < r->secondaryCount; i++) {
		Device *d = deviceAt(r->secondary[i]);
		if (d && d->kind() == DeviceKind::Camera) d->handleCommand(cmd);
	}
}

inline void fanToMain(int rigIndex, Command cmd) {
	Device *m = rigMainOf(rigIndex);
	if (m) m->handleCommand(cmd);
}

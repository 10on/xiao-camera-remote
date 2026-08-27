#pragma once

#include "devices/device.h"

// The registered device set. Indices here are the stable identity used by
// Rig::mainIndex / Rig::secondary (rig.h) and by device_registry.h. Add a
// new driver in device_table.cpp; it also needs registering with
// bleManager in Menu::begin().
Device *deviceAt(int i);
int deviceCount();
int deviceIndexOf(const Device *d); // -1 if not registered

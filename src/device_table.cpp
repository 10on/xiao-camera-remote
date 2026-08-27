#include "device_table.h"

#include "devices/dolly_device.h"
#include "devices/phone_device.h"
#include "devices/slider_device.h"

// Order is the on-wire identity — do not reorder without a rig-store
// layout bump (rig_store.cpp kLayoutVersion).
static Device *const kDevices[] = {
	&sliderDevice, // 0
	&phoneDevice,  // 1
	&dollyDevice,  // 2
};
static const int kCount = sizeof(kDevices) / sizeof(kDevices[0]);

Device *deviceAt(int i) {
	if (i < 0 || i >= kCount) return nullptr;
	return kDevices[i];
}

int deviceCount() { return kCount; }

int deviceIndexOf(const Device *d) {
	for (int i = 0; i < kCount; i++)
		if (kDevices[i] == d) return i;
	return -1;
}

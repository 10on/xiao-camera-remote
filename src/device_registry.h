#pragma once

#include <stdint.h>

// Lightweight per-device metadata for the Devices registry screens (mock
// 15 / 21): a user-editable alias and a coarse "last seen" marker. The
// device set itself is still the fixed kDevices[] table in menu.cpp — this
// does not add BLE discovery (mock 20's scan is a separate future step).
//
// "Last seen" has no RTC/NTP to lean on, so it's coarse: NeverThisBoot /
// EarlierThisBoot / Connected.
enum class SeenState : uint8_t { NeverThisBoot, EarlierThisBoot, Connected };

class DeviceRegistry {
public:
	void begin(); // load aliases from NVS
	void tick();  // call each loop: refreshes SeenState from live link state

	const char *alias(int deviceIndex) const;      // falls back to Device::name()
	void setAlias(int deviceIndex, const char *s); // persists
	SeenState seen(int deviceIndex) const;
	int rigMembership(int deviceIndex) const;       // # of stored rigs referencing it

	static const int kAliasMax = 18;
	static const int kMaxDevices = 4; // keep >= device_table.cpp kDevices[] count

private:
	char _alias[kMaxDevices][kAliasMax + 1] = {{0}, {0}, {0}, {0}}; // one per kDevices entry
	SeenState _seen[kMaxDevices] = {SeenState::NeverThisBoot, SeenState::NeverThisBoot,
	                                SeenState::NeverThisBoot, SeenState::NeverThisBoot};
};

extern DeviceRegistry deviceRegistry;

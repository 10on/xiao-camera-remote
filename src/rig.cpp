#include "rig.h"

bool Rig::hasSecondary(int deviceIndex) const {
	for (int i = 0; i < secondaryCount; i++)
		if (secondary[i] == deviceIndex) return true;
	return false;
}

bool Rig::references(int deviceIndex) const {
	return mainIndex == deviceIndex || hasSecondary(deviceIndex);
}

#pragma once

#include "rig.h"

// User-editable list of shooting rigs, persisted to NVS as one blob.
// Seeds a default set on first boot (mirrors ux-redesign.md §5). The rig
// editor (Menu) mutates a working copy and calls save() to commit.
static const int kMaxRigs = 8;

class RigStore {
public:
	void begin(); // load from NVS; seed defaults if empty

	int count() const { return _count; }
	const Rig &at(int i) const { return _rigs[i]; }

	// Editor helpers — all persist immediately.
	int add(const Rig &r);          // -> new index, or -1 if full
	void replace(int i, const Rig &r);
	void remove(int i);
	int duplicate(int i);           // -> new index, or -1 if full

	// A blank rig pre-filled with sensible defaults, for "new rig".
	static Rig blank();

private:
	void seedDefaults();
	void save() const;

	Rig _rigs[kMaxRigs] = {};
	int _count = 0;
};

extern RigStore rigStore;

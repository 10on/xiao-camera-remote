#include "menu.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>

#include "menu_internal.h"
#include "ble_manager.h"
#include "device_registry.h"
#include "devices/phone_device.h"
#include "display.h"
#include "ota.h"
#include "led.h"
#include "battery.h"
#include "settings.h"

Menu menu;

// Alphabet for the 5-button name editor (mock 19). Latin placeholder.
static const char *kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 +-";
static const int kAlphabetLen = 38;

// ===========================================================================
// Session lifecycle
// ===========================================================================

void Menu::launchRig(int rigIndex) {
	const Rig *r = rigAt(rigIndex);
	if (!r) return;

	for (int i = 0; i < deviceCount(); i++) {
		bool member = r->references(i);
		if (member && !deviceAt(i)->isActive()) deviceAt(i)->activate();
		else if (!member && deviceAt(i)->isActive()) deviceAt(i)->deactivate();
	}

	_activeRig = rigIndex;
	_connectStartedMs = millis();
	_mainEverReady = false;
	_takeActive = false;
	_recordStartedAtMs = 0;
	_mainMotion = MainMotion::Stopped;
	_lostSinceMs = 0;
	_screen = Screen::Connecting;
}

void Menu::endSession() {
	if (_takeActive) {
		fanToPhones(_activeRig, Command::StopRecord);
		fanToMain(_activeRig, Command::EmergencyStop);
	}
	for (int i = 0; i < deviceCount(); i++)
		if (deviceAt(i)->isActive()) deviceAt(i)->deactivate();

	_activeRig = -1;
	_takeActive = false;
	_recordStartedAtMs = 0;
	_mainMotion = MainMotion::Stopped;
	_mainEverReady = false;
	_lostSinceMs = 0;
	_screen = Screen::Rigs;
}

void Menu::startTake() {
	_takeActive = true;
	_recordStartedAtMs = millis();
	phoneDevice.markTakeStart();
	fanToPhones(_activeRig, Command::Record);
	const Rig *r = rigAt(_activeRig);
	Device *m = rigMainOf(_activeRig);
	if (r && r->takeMode == TakeMode::RecordAndMoveMain && m) {
		// Program-capable Main (slider): the device owns the trajectory —
		// send START, never a fake F/B. Legacy motion device: jog forward.
		if (m->programName()) {
			fanToMain(_activeRig, Command::StartProgram);
		} else {
			fanToMain(_activeRig, Command::MoveForward);
			_mainMotion = MainMotion::Forward;
		}
	}
}

void Menu::stopTake() {
	_takeActive = false;
	_recordStartedAtMs = 0;
	phoneDevice.markTakeEnd();
	fanToPhones(_activeRig, Command::StopRecord);
	const Rig *r = rigAt(_activeRig);
	Device *m = rigMainOf(_activeRig);
	if (r && r->takeMode == TakeMode::RecordAndMoveMain && m) {
		if (m->programName()) fanToMain(_activeRig, Command::StopProgram);
		else {
			fanToMain(_activeRig, Command::StopMove);
			_mainMotion = MainMotion::Stopped;
		}
	}
}

bool Menu::mainLostTakeoverActive() const {
	if (_screen != Screen::Control) return false;
	Device *m = rigMainOf(_activeRig);
	return m && _mainEverReady && !m->isConnected() && !_takeActive;
}

// ===========================================================================
// Editor / text entry
// ===========================================================================

void Menu::openEditor(int rigIndex) {
	_editRigIndex = rigIndex;
	_editRig = rigIndex < 0 ? RigStore::blank() : rigStore.at(rigIndex);
	_editStep = 0;
	_editCursor = 0;
	openTextEntry(TextReturn::EditorName, _editRig.name);
}

void Menu::saveEditor() {
	if (_editRigIndex < 0) {
		int idx = rigStore.add(_editRig);
		if (idx >= 0) {
			settings.setRigIndex(idx);
			_rigCursor = idx;
		}
	} else {
		rigStore.replace(_editRigIndex, _editRig);
		_rigCursor = _editRigIndex;
	}
	_screen = Screen::Rigs;
}

void Menu::openTextEntry(TextReturn ret, const char *initial) {
	_textReturn = ret;
	memset(_textBuf, 0, sizeof(_textBuf));
	strncpy(_textBuf, initial ? initial : "", kRigNameMax);
	_textPos = (int)strlen(_textBuf);
	if (_textPos >= kRigNameMax) _textPos = kRigNameMax - 1;
	_textAlpha = 0;
	_screen = Screen::TextEntry;
}

void Menu::commitTextEntry() {
	// Trim trailing spaces.
	for (int i = (int)strlen(_textBuf) - 1; i >= 0 && _textBuf[i] == ' '; i--) _textBuf[i] = '\0';
	if (_textBuf[0] == '\0') strcpy(_textBuf, "Rig");

	switch (_textReturn) {
	case TextReturn::EditorName:
		strncpy(_editRig.name, _textBuf, kRigNameMax);
		_editRig.name[kRigNameMax] = '\0';
		_editStep = 1;
		_editCursor = 0;
		_screen = Screen::Editor;
		break;
	case TextReturn::RigRename: {
		Rig r = rigStore.at(_rigCursor);
		strncpy(r.name, _textBuf, kRigNameMax);
		r.name[kRigNameMax] = '\0';
		rigStore.replace(_rigCursor, r);
		_screen = Screen::Rigs;
		break;
	}
	case TextReturn::DeviceRename:
		deviceRegistry.setAlias(_cardDevice, _textBuf);
		_screen = Screen::DeviceCard;
		break;
	}
}

// ===========================================================================
// BLE scan (mock 20) — one-shot, only while no session is live
// ===========================================================================

void Menu::beginScan() {
	_scanCount = 0;
	_scanCursor = 0;
	for (int i = 0; i < deviceCount(); i++)
		if (deviceAt(i)->isActive()) return; // guarded by the screen; belt and braces

	NimBLEScan *scan = NimBLEDevice::getScan();
	scan->setActiveScan(true);
	scan->clearResults();
	scan->start(6000, false, false); // non-blocking; pollScan() reads results
	_scanning = true;
	_scanStartedMs = millis();
}

void Menu::pollScan() {
	if (!_scanning) return;
	NimBLEScan *scan = NimBLEDevice::getScan();
	if (scan->isScanning() && (millis() - _scanStartedMs) < 7000) return;

	NimBLEScanResults res = scan->getResults();
	_scanCount = 0;
	for (int i = 0; i < (int)res.getCount() && _scanCount < kMaxScan; i++) {
		const NimBLEAdvertisedDevice *d = res.getDevice(i);
		if (!d->haveName() || d->getName().empty()) continue;
		strncpy(_scan[_scanCount].name, d->getName().c_str(), sizeof(_scan[0].name) - 1);
		_scan[_scanCount].name[sizeof(_scan[0].name) - 1] = '\0';
		_scan[_scanCount].rssi = d->getRSSI();
		_scanCount++;
	}
	scan->clearResults();
	_scanning = false;
}

// ===========================================================================
// begin / update
// ===========================================================================

void Menu::begin() {
	for (int i = 0; i < deviceCount(); i++) bleManager.registerDevice(deviceAt(i));

	int last = settings.rigIndex();
	if (last < 0 || last >= rigStore.count()) last = 0;
	_rigCursor = last;

	if (settings.autostartLastRig() && rigStore.count() > 0) launchRig(last);
	else _screen = Screen::Rigs;
}

bool Menu::update() {
	if (_scanning) pollScan();

	bool needRender = false;
	Device *m = rigMainOf(_activeRig);
	bool mainNow = m && m->isConnected();
	if (mainNow) _mainEverReady = true;

	if (_screen == Screen::Connecting) {
		int ready, total;
		rigPhoneCounts(_activeRig, ready, total);
		bool goControl = m ? mainNow : (ready > 0);
		if (goControl) {
			_screen = Screen::Control;
			needRender = true;
		}
	}

	// Main lost mid-take -> immediate broadcast stop + end the take (§4).
	if (_screen == Screen::Control && m && _mainEverReady && !mainNow && _takeActive) {
		fanToMain(_activeRig, Command::EmergencyStop);
		fanToPhones(_activeRig, Command::StopRecord);
		_takeActive = false;
		_recordStartedAtMs = 0;
		_mainMotion = MainMotion::Stopped;
		phoneDevice.markTakeEnd();
		needRender = true;
	}

	bool lost = mainLostTakeoverActive();
	if (lost != _shownMainLost) needRender = true;
	if (!lost) _lostSinceMs = 0;
	else if (_lostSinceMs == 0) _lostSinceMs = millis();

	return needRender;
}

// ===========================================================================
// Button routing
// ===========================================================================

void Menu::handleButton(ButtonId id, ButtonEvent ev) {
	if (ev == ButtonEvent::None) return;

	// OTA is exclusive: only Ok, to leave the mode.
	if (ota.state() != Ota::State::Idle) {
		if (ev == ButtonEvent::Press && id == ButtonId::Ok) {
			ota.cancel();
			_screen = Screen::System;
		}
		return;
	}

	if (mainLostTakeoverActive()) {
		if (ev == ButtonEvent::Press && id == ButtonId::Ok) _lostSinceMs = millis();
		if (ev == ButtonEvent::LongPress && id == ButtonId::Left) endSession();
		return;
	}

	switch (_screen) {
	case Screen::Rigs:         handleRigsButton(id, ev); break;
	case Screen::RigMenu:      handleRigMenuButton(id, ev); break;
	case Screen::Editor:       handleEditorButton(id, ev); break;
	case Screen::TextEntry:    handleTextEntryButton(id, ev); break;
	case Screen::Connecting:   handleConnectingButton(id, ev); break;
	case Screen::Control:      handleControlButton(id, ev); break;
	case Screen::Settings:     handleSettingsButton(id, ev); break;
	case Screen::Devices:      handleDevicesButton(id, ev); break;
	case Screen::DeviceCard:   handleDeviceCardButton(id, ev); break;
	case Screen::Scan:         handleScanButton(id, ev); break;
	case Screen::ControlPrefs: handleControlPrefsButton(id, ev); break;
	case Screen::ScreenPrefs:  handleScreenPrefsButton(id, ev); break;
	case Screen::System:       handleSystemButton(id, ev); break;
	}
}

// --- Rigs (home) --------------------------------------------------------

void Menu::handleRigsButton(ButtonId id, ButtonEvent ev) {
	int n = rigStore.count();
	if (ev == ButtonEvent::Press) {
		switch (id) {
		case ButtonId::Up:   if (n) _rigCursor = (_rigCursor + n - 1) % n; break;
		case ButtonId::Down: if (n) _rigCursor = (_rigCursor + 1) % n; break;
		case ButtonId::Ok:
			if (n == 0) { openEditor(-1); }
			else { settings.setRigIndex(_rigCursor); launchRig(_rigCursor); }
			break;
		case ButtonId::Right:
			if (n) { _rigMenuCursor = 0; _screen = Screen::RigMenu; }
			break;
		default: break;
		}
	} else if (ev == ButtonEvent::LongPress && id == ButtonId::Left) {
		_settingsCursor = 0;
		_screen = Screen::Settings;
	}
}

// --- Rig menu (mock 18): Edit / Duplicate / Rename / Delete -------------

void Menu::handleRigMenuButton(ButtonId id, ButtonEvent ev) {
	if (ev == ButtonEvent::Press) {
		switch (id) {
		case ButtonId::Up:   _rigMenuCursor = (_rigMenuCursor + 3) % 4; break;
		case ButtonId::Down: _rigMenuCursor = (_rigMenuCursor + 1) % 4; break;
		case ButtonId::Left: _screen = Screen::Rigs; break;
		case ButtonId::Ok:
			switch (_rigMenuCursor) {
			case 0: openEditor(_rigCursor); break;
			case 1: {
				int idx = rigStore.duplicate(_rigCursor);
				if (idx >= 0) _rigCursor = idx;
				_screen = Screen::Rigs;
				break;
			}
			case 2: openTextEntry(TextReturn::RigRename, rigStore.at(_rigCursor).name); break;
			case 3:
				rigStore.remove(_rigCursor);
				if (_rigCursor >= rigStore.count()) _rigCursor = rigStore.count() - 1;
				if (_rigCursor < 0) _rigCursor = 0;
				_screen = Screen::Rigs;
				break;
			}
			break;
		default: break;
		}
	} else if (ev == ButtonEvent::LongPress && id == ButtonId::Left) {
		_screen = Screen::Rigs;
	}
}

// --- Editor (mock 11-13): step 1 Main, 2 Secondary, 3 TakeMode ---------
// Step 0 (name) is handled by TextEntry, which advances to step 1.

void Menu::handleEditorButton(ButtonId id, ButtonEvent ev) {
	// Motion devices are Main candidates; index -1 == "None" is the last row.
	auto motionCount = []() {
		int n = 0;
		for (int i = 0; i < deviceCount(); i++)
			if (deviceAt(i)->kind() == DeviceKind::Motion) n++;
		return n;
	};
	auto motionAt = [](int k) -> int {
		int n = 0;
		for (int i = 0; i < deviceCount(); i++)
			if (deviceAt(i)->kind() == DeviceKind::Motion) {
				if (n == k) return i;
				n++;
			}
		return -1;
	};

	if (ev == ButtonEvent::LongPress && id == ButtonId::Left) {
		if (_editStep == 1) { openTextEntry(TextReturn::EditorName, _editRig.name); }
		else if (_editStep > 1) { _editStep--; _editCursor = 0; }
		else { _screen = Screen::Rigs; }
		return;
	}
	if (ev != ButtonEvent::Press) return;

	if (_editStep == 1) { // Main: [motion devices..., None]
		int rows = motionCount() + 1;
		if (id == ButtonId::Up) _editCursor = (_editCursor + rows - 1) % rows;
		else if (id == ButtonId::Down) _editCursor = (_editCursor + 1) % rows;
		else if (id == ButtonId::Ok) {
			int mainIdx = _editCursor < motionCount() ? motionAt(_editCursor) : -1;
			_editRig.mainIndex = (int8_t)mainIdx;
			// Drop any Secondary slot that now collides with Main.
			int w = 0;
			for (int i = 0; i < _editRig.secondaryCount; i++)
				if (_editRig.secondary[i] != mainIdx) _editRig.secondary[w++] = _editRig.secondary[i];
			_editRig.secondaryCount = (int8_t)w;
			_editStep = 2;
			_editCursor = 0;
		}
	} else if (_editStep == 2) { // Secondary: checkbox list of Camera devices
		int rows = 0, cams[8] = {0};
		for (int i = 0; i < deviceCount(); i++)
			if (deviceAt(i)->kind() == DeviceKind::Camera) cams[rows++] = i;
		if (rows == 0) { if (id == ButtonId::Ok) { _editStep = 3; _editCursor = 0; } return; }
		if (id == ButtonId::Up) _editCursor = (_editCursor + rows - 1) % rows;
		else if (id == ButtonId::Down) _editCursor = (_editCursor + 1) % rows;
		else if (id == ButtonId::Right) {
			int dev = cams[_editCursor];
			if (_editRig.hasSecondary(dev)) {
				int w = 0;
				for (int i = 0; i < _editRig.secondaryCount; i++)
					if (_editRig.secondary[i] != dev) _editRig.secondary[w++] = _editRig.secondary[i];
				_editRig.secondaryCount = (int8_t)w;
			} else if (_editRig.secondaryCount < kMaxSecondaryPerRig) {
				_editRig.secondary[_editRig.secondaryCount++] = (int8_t)dev;
			}
		} else if (id == ButtonId::Ok) {
			_editStep = 3;
			_editCursor = 0;
		}
	} else if (_editStep == 3) { // TakeMode
		if (id == ButtonId::Up || id == ButtonId::Down) _editCursor ^= 1;
		else if (id == ButtonId::Ok) {
			_editRig.takeMode = _editCursor == 0 ? TakeMode::RecordOnly : TakeMode::RecordAndMoveMain;
			saveEditor();
		}
	}
}

// --- Text entry (mock 19) ---------------------------------------------

void Menu::handleTextEntryButton(ButtonId id, ButtonEvent ev) {
	int len = (int)strlen(_textBuf);

	if (ev == ButtonEvent::LongPress) {
		if (id == ButtonId::Right) { // backspace
			if (len > 0) {
				_textBuf[len - 1] = '\0';
				if (_textPos > 0) _textPos--;
			}
		} else if (id == ButtonId::Left) {
			if (_textReturn == TextReturn::EditorName && _editRigIndex < 0 && _editStep == 0)
				_screen = Screen::Rigs;
			else if (_textReturn == TextReturn::EditorName)
				_screen = Screen::Rigs;
			else if (_textReturn == TextReturn::RigRename)
				_screen = Screen::RigMenu;
			else
				_screen = Screen::DeviceCard;
		}
		return;
	}
	if (ev != ButtonEvent::Press) return;

	// Ensure the buffer covers _textPos so we can edit "past the end".
	while ((int)strlen(_textBuf) <= _textPos && (int)strlen(_textBuf) < kRigNameMax) {
		int l = (int)strlen(_textBuf);
		_textBuf[l] = ' ';
		_textBuf[l + 1] = '\0';
	}
	char cur = _textBuf[_textPos];
	int ai = 0;
	for (int i = 0; i < kAlphabetLen; i++)
		if (kAlphabet[i] == cur) { ai = i; break; }

	switch (id) {
	case ButtonId::Up:
		ai = (ai + 1) % kAlphabetLen;
		_textBuf[_textPos] = kAlphabet[ai];
		break;
	case ButtonId::Down:
		ai = (ai + kAlphabetLen - 1) % kAlphabetLen;
		_textBuf[_textPos] = kAlphabet[ai];
		break;
	case ButtonId::Left:
		if (_textPos > 0) _textPos--;
		break;
	case ButtonId::Right:
		if (_textPos < kRigNameMax - 1) _textPos++;
		break;
	case ButtonId::Ok:
		commitTextEntry();
		break;
	default:
		break;
	}
}

// --- Connecting --------------------------------------------------------

void Menu::handleConnectingButton(ButtonId id, ButtonEvent ev) {
	if (ev == ButtonEvent::Press && id == ButtonId::Ok) _connectStartedMs = millis();
	else if (ev == ButtonEvent::LongPress && id == ButtonId::Left) endSession();
}

// --- Control ----------------------------------------------------------

void Menu::handleControlButton(ButtonId id, ButtonEvent ev) {
	Device *m = rigMainOf(_activeRig);
	int ready, total;
	rigPhoneCounts(_activeRig, ready, total);

	if (ev == ButtonEvent::LongPress) {
		if (id == ButtonId::Ok) {
			// In a fault state, long-Ok clears the error instead of E-Stopping.
			if (m && m->inFault() && !_takeActive) {
				m->handleCommand(Command::ResetFault);
				return;
			}
			_takeActive = false;
			_recordStartedAtMs = 0;
			_mainMotion = MainMotion::Stopped;
			phoneDevice.markTakeEnd();
			fanToMain(_activeRig, Command::EmergencyStop);
			fanToPhones(_activeRig, Command::StopRecord);
			return;
		}
		if (id == ButtonId::Left) { endSession(); return; }
		if (id == ButtonId::Right && m && m->supportsHome() && !_takeActive) {
			m->handleCommand(Command::Home);
			_mainMotion = MainMotion::Stopped;
			return;
		}
		return;
	}
	if (ev != ButtonEvent::Press) return;

	if (id == ButtonId::Ok) {
		if (_takeActive) stopTake();
		else if (ready > 0) startTake();
		else if (m && m->programName()) {
			// Phone-less program device: Ok is the program's START / STOP.
			if (m->programRunning()) m->handleCommand(Command::StopProgram);
			else if (!m->inFault() && m->canExecuteCommand(Command::StartProgram))
				m->handleCommand(Command::StartProgram);
		} else if (m) { // phone-less plain motion device: Ok is a plain STOP
			m->handleCommand(Command::StopMove);
			_mainMotion = MainMotion::Stopped;
		}
		return;
	}

	if (!m) return; // camera-only rig: arrows idle

	// One fixed, obvious mapping for every motion device: up/down = speed,
	// left/right = drive that way (press again or Ok = stop, opposite =
	// reverse). No axis-binding choice, no manual-vs-program split on the
	// arrows — a program device just ignores a jog it can't accept.
	const int cap = settings.maxSpeedPercent();
	auto speed = [&](bool up) {
		if (up) {
			if (m->speedPercent() == 0 || m->speedPercent() < cap) m->handleCommand(Command::SpeedUp);
		} else {
			m->handleCommand(Command::SpeedDown);
		}
	};
	auto drive = [&](MainMotion dir, Command go) {
		if (_mainMotion == dir) {
			m->handleCommand(Command::StopMove);
			_mainMotion = MainMotion::Stopped;
		} else if (m->canExecuteCommand(go)) {
			m->handleCommand(go);
			_mainMotion = dir;
		}
	};

	if (_takeActive) {
		// Mid-take: arrows only trim speed; the motion is the take's own.
		if (id == ButtonId::Up) speed(true);
		else if (id == ButtonId::Down) speed(false);
		return;
	}

	// Per-device L/R inversion (Devices card) — 'F'/'B' are motor-relative.
	const bool inv = deviceRegistry.invertDir(deviceIndexOf(m));
	const MainMotion fwdM = inv ? MainMotion::Backward : MainMotion::Forward;
	const MainMotion bwdM = inv ? MainMotion::Forward : MainMotion::Backward;
	const Command fwdC = inv ? Command::MoveBackward : Command::MoveForward;
	const Command bwdC = inv ? Command::MoveForward : Command::MoveBackward;

	switch (id) {
	case ButtonId::Up:    speed(true); break;
	case ButtonId::Down:  speed(false); break;
	case ButtonId::Left:  drive(bwdM, bwdC); break;
	case ButtonId::Right: drive(fwdM, fwdC); break;
	default: break;
	}
}

// --- Settings (mock 14) ----------------------------------------------

void Menu::handleSettingsButton(ButtonId id, ButtonEvent ev) {
	if (ev == ButtonEvent::Press) {
		switch (id) {
		case ButtonId::Up:   _settingsCursor = (_settingsCursor + 4) % 5; break;
		case ButtonId::Down: _settingsCursor = (_settingsCursor + 1) % 5; break;
		case ButtonId::Left: _screen = Screen::Rigs; break;
		case ButtonId::Ok:
			switch (_settingsCursor) {
			case 0: _screen = Screen::Rigs; break;
			case 1: _devicesCursor = 0; _screen = Screen::Devices; break;
			case 2: _ctrlPrefCursor = 0; _screen = Screen::ControlPrefs; break;
			case 3: _pendingBrightness = settings.brightness(); _screen = Screen::ScreenPrefs; break;
			case 4: _sysCursor = 0; _sysConfirm = SysConfirm::None; _screen = Screen::System; break;
			}
			break;
		default: break;
		}
	} else if (ev == ButtonEvent::LongPress && id == ButtonId::Left) {
		_screen = Screen::Rigs;
	}
}

// --- Devices registry (mock 15) ------------------------------------

void Menu::handleDevicesButton(ButtonId id, ButtonEvent ev) {
	int n = deviceCount();
	if (ev == ButtonEvent::Press) {
		switch (id) {
		case ButtonId::Up:   _devicesCursor = (_devicesCursor + n - 1) % n; break;
		case ButtonId::Down: _devicesCursor = (_devicesCursor + 1) % n; break;
		case ButtonId::Left: _screen = Screen::Settings; break;
		case ButtonId::Right:
			_cardDevice = _devicesCursor;
			_cardCursor = 0;
			_screen = Screen::DeviceCard;
			break;
		case ButtonId::Ok: // "add" -> the scan screen
			_scanCount = 0;
			_scanCursor = 0;
			_screen = Screen::Scan;
			break;
		default: break;
		}
	} else if (ev == ButtonEvent::LongPress && id == ButtonId::Left) {
		for (int i = 0; i < deviceCount(); i++)
			if (deviceAt(i)->isActive()) deviceAt(i)->deactivate();
		_screen = Screen::Settings;
	}
}

// --- Device card (mock 21) ---------------------------------------

void Menu::handleDeviceCardButton(ButtonId id, ButtonEvent ev) {
	Device *d = deviceAt(_cardDevice);
	// 0 test link, 1 rename, [2 flip L/R] for motion devices.
	const bool canFlip = d && d->kind() == DeviceKind::Motion;
	const int acts = canFlip ? 3 : 2;

	if (ev == ButtonEvent::Press) {
		switch (id) {
		case ButtonId::Up:   _cardCursor = (_cardCursor + acts - 1) % acts; break;
		case ButtonId::Down: _cardCursor = (_cardCursor + 1) % acts; break;
		case ButtonId::Left:
			if (d && d->isActive()) d->deactivate();
			_screen = Screen::Devices;
			break;
		case ButtonId::Ok:
			if (_cardCursor == 0) {
				if (d) d->isActive() ? d->deactivate() : d->activate();
			} else if (_cardCursor == 1) {
				openTextEntry(TextReturn::DeviceRename, deviceRegistry.alias(_cardDevice));
			} else {
				deviceRegistry.toggleInvertDir(_cardDevice);
			}
			break;
		default:
			break;
		}
	} else if (ev == ButtonEvent::LongPress && id == ButtonId::Left) {
		if (d && d->isActive()) d->deactivate();
		_screen = Screen::Devices;
	}
}

// --- Scan (mock 20) ---------------------------------------------

void Menu::handleScanButton(ButtonId id, ButtonEvent ev) {
	if (ev == ButtonEvent::Press) {
		switch (id) {
		case ButtonId::Up:   if (_scanCount) _scanCursor = (_scanCursor + _scanCount - 1) % _scanCount; break;
		case ButtonId::Down: if (_scanCount) _scanCursor = (_scanCursor + 1) % _scanCount; break;
		case ButtonId::Left: _screen = Screen::Devices; break;
		case ButtonId::Ok:
			if (!_scanning && _scanCount == 0) beginScan();
			// Registering a scanned peer needs a matching built-in driver
			// (dynamic drivers are a later phase) — no-op otherwise.
			break;
		default: break;
		}
	} else if (ev == ButtonEvent::LongPress && id == ButtonId::Left) {
		_screen = Screen::Devices;
	}
}

// --- Control prefs (mock 22) ----------------------------------

void Menu::handleControlPrefsButton(ButtonId id, ButtonEvent ev) {
	// Rows: 0 Max speed, 1 Autostart last, 2 Key sound.
	if (ev == ButtonEvent::Press) {
		switch (id) {
		case ButtonId::Up:   _ctrlPrefCursor = (_ctrlPrefCursor + 2) % 3; break;
		case ButtonId::Down: _ctrlPrefCursor = (_ctrlPrefCursor + 1) % 3; break;
		case ButtonId::Left:
		case ButtonId::Right: {
			int dir = id == ButtonId::Right ? 1 : -1;
			if (_ctrlPrefCursor == 0)
				settings.setMaxSpeedPercent((uint8_t)(settings.maxSpeedPercent() + dir * 10));
			else if (_ctrlPrefCursor == 1)
				settings.setAutostartLastRig(!settings.autostartLastRig());
			else
				settings.setButtonSound(!settings.buttonSound());
			break;
		}
		case ButtonId::Ok:
			if (_ctrlPrefCursor == 1)
				settings.setAutostartLastRig(!settings.autostartLastRig());
			else if (_ctrlPrefCursor == 2)
				settings.setButtonSound(!settings.buttonSound());
			break;
		default:
			break;
		}
	} else if (ev == ButtonEvent::LongPress && id == ButtonId::Left) {
		_screen = Screen::Settings;
	}
}

// --- Screen prefs (brightness) ------------------------------

void Menu::handleScreenPrefsButton(ButtonId id, ButtonEvent ev) {
	if (ev == ButtonEvent::Press) {
		const int kStep = 16, kMin = 10;
		if (id == ButtonId::Left) {
			_pendingBrightness = max(kMin, _pendingBrightness - kStep);
			display.setBrightness((uint8_t)_pendingBrightness);
		} else if (id == ButtonId::Right) {
			_pendingBrightness = min(255, _pendingBrightness + kStep);
			display.setBrightness((uint8_t)_pendingBrightness);
		} else if (id == ButtonId::Ok) {
			settings.setBrightness((uint8_t)_pendingBrightness);
			_screen = Screen::Settings;
		}
	} else if (ev == ButtonEvent::LongPress && id == ButtonId::Left) {
		display.setBrightness(settings.brightness());
		_screen = Screen::Settings;
	}
}

// --- System (mock 23) --------------------------------------

void Menu::handleSystemButton(ButtonId id, ButtonEvent ev) {
	if (_sysConfirm != SysConfirm::None) {
		if (ev == ButtonEvent::Press) {
			if (id == ButtonId::Left || id == ButtonId::Up) _sysConfirmSel = 0;
			else if (id == ButtonId::Right || id == ButtonId::Down) _sysConfirmSel = 1;
			else if (id == ButtonId::Ok) {
				bool yes = _sysConfirmSel == 0;
				SysConfirm which = _sysConfirm;
				_sysConfirm = SysConfirm::None;
				if (yes && which == SysConfirm::Ota) ota.begin();
				else if (yes && which == SysConfirm::Reset) {
					settings.factoryReset();
					ESP.restart();
				}
			}
		} else if (ev == ButtonEvent::LongPress && id == ButtonId::Left) {
			_sysConfirm = SysConfirm::None;
		}
		return;
	}

	if (ev == ButtonEvent::Press) {
		switch (id) {
		case ButtonId::Up:   _sysCursor = (_sysCursor + 3) % 4; break;
		case ButtonId::Down: _sysCursor = (_sysCursor + 1) % 4; break;
		case ButtonId::Left: _screen = Screen::Settings; break;
		case ButtonId::Ok:
			if (_sysCursor == 2) { _sysConfirmSel = 1; _sysConfirm = SysConfirm::Ota; }
			else if (_sysCursor == 3) { _sysConfirmSel = 1; _sysConfirm = SysConfirm::Reset; }
			break;
		default: break;
		}
	} else if (ev == ButtonEvent::LongPress && id == ButtonId::Left) {
		_screen = Screen::Settings;
	}
}

// ===========================================================================
// Status LED
// ===========================================================================

void Menu::updateStatusLed() {
	if (ota.state() == Ota::State::WaitingForUpload || ota.state() == Ota::State::Uploading) {
		statusLed.setActivity(true, false);
	} else {
		bool anyConnected = false, anyConnecting = false;
		for (int i = 0; i < deviceCount(); i++) {
			if (deviceAt(i)->isConnected()) anyConnected = true;
			else if (deviceAt(i)->isActive()) anyConnecting = true;
		}
		statusLed.setActivity(anyConnected || anyConnecting, !anyConnected && anyConnecting);
	}
	uint8_t pct = battery.percent();
	statusLed.setPower(pct <= 20, pct <= 10);
}

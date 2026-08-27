#pragma once

#include "buttons.h"
#include "rig.h"

// UI state machine — authoritative UX model: docs/design/ux-redesign.md
// plus the pixel mock docs/design/ux-redesign-mock/new-model.dc.html
// (screen numbers below refer to that mock). Copy is English placeholder;
// the mock is Russian and a Cyrillic GFXfont is a separate follow-up.
//
//  Daily path:  Rigs (1) -> Connecting (2/3) -> Control (4-9) ; a lost Main
//               outside a take draws MainLost (10) over Control.
//  Editing:     Rigs -> RigMenu (18) -> Editor (11-13) / TextEntry (19).
//  Settings:    Settings (14) -> Devices (15) -> DeviceCard (21) / Scan (20)
//                              -> ControlPrefs (22) / ScreenPrefs / System (23)
//
// Emergency stop (long-Ok) and the mid-take auto-E-Stop on Main loss are
// handled in handleControlButton / update().
class Menu {
public:
	void begin();
	bool update(); // session transitions; returns true if a full redraw is needed
	void handleButton(ButtonId id, ButtonEvent ev);
	void render();
	void renderDynamic();
	void updateStatusLed();
	bool takeActive() const { return _takeActive; } // suppresses stage-2 deep sleep

private:
	enum class Screen {
		Rigs, RigMenu, Editor, TextEntry, Connecting, Control,
		Settings, Devices, DeviceCard, Scan, ControlPrefs, ScreenPrefs, System,
	};
	enum class MainMotion { Stopped, Forward, Backward };
	enum class TextReturn { EditorName, RigRename, DeviceRename };
	enum class SysConfirm { None, Ota, Reset };

	// --- render (menu_render.cpp) ---
	void renderRigs();
	void renderRigMenu();
	void renderEditor();
	void renderTextEntry();
	void renderConnecting();
	void renderControl();
	void renderControlMotion(const Rig &r);
	void renderControlCamerasOnly(const Rig &r);
	void renderMainLost();
	void renderSettings();
	void renderDevices();
	void renderDeviceCard();
	void renderScan();
	void renderControlPrefs();
	void renderScreenPrefs();
	void renderSystem();
	void renderOtaActive();

	// --- input (menu.cpp) ---
	void handleRigsButton(ButtonId id, ButtonEvent ev);
	void handleRigMenuButton(ButtonId id, ButtonEvent ev);
	void handleEditorButton(ButtonId id, ButtonEvent ev);
	void handleTextEntryButton(ButtonId id, ButtonEvent ev);
	void handleConnectingButton(ButtonId id, ButtonEvent ev);
	void handleControlButton(ButtonId id, ButtonEvent ev);
	void handleSettingsButton(ButtonId id, ButtonEvent ev);
	void handleDevicesButton(ButtonId id, ButtonEvent ev);
	void handleDeviceCardButton(ButtonId id, ButtonEvent ev);
	void handleScanButton(ButtonId id, ButtonEvent ev);
	void handleControlPrefsButton(ButtonId id, ButtonEvent ev);
	void handleScreenPrefsButton(ButtonId id, ButtonEvent ev);
	void handleSystemButton(ButtonId id, ButtonEvent ev);

	// --- helpers ---
	void launchRig(int rigIndex);
	void endSession();
	void startTake();
	void stopTake();
	bool mainLostTakeoverActive() const;
	void openEditor(int rigIndex); // -1 = new
	void saveEditor();
	void openTextEntry(TextReturn ret, const char *initial);
	void commitTextEntry();
	void beginScan();
	void pollScan();

	Screen _screen = Screen::Rigs;
	Screen _shownScreen = Screen::Rigs;
	bool _shownTake = false;
	bool _shownMainLost = false;

	// Rigs home
	int _rigCursor = 0;

	// Live session
	int _activeRig = -1;
	uint32_t _connectStartedMs = 0;
	bool _mainEverReady = false;
	bool _takeActive = false;
	uint32_t _recordStartedAtMs = 0;
	MainMotion _mainMotion = MainMotion::Stopped;
	uint32_t _lostSinceMs = 0;

	// Rig menu / editor
	int _rigMenuCursor = 0;
	Rig _editRig{};
	int _editRigIndex = -1;
	int _editStep = 0;
	int _editCursor = 0;

	// Text entry
	char _textBuf[kRigNameMax + 1] = {0};
	int _textPos = 0;
	int _textAlpha = 0;
	TextReturn _textReturn = TextReturn::EditorName;

	// Settings tree
	int _settingsCursor = 0;
	int _devicesCursor = 0;
	int _cardDevice = 0;
	int _cardCursor = 0;
	int _ctrlPrefCursor = 0;
	int _pendingBrightness = 0;
	int _sysCursor = 0;
	SysConfirm _sysConfirm = SysConfirm::None;
	int _sysConfirmSel = 1;

	// Scan (mock 20)
	static const int kMaxScan = 8;
	struct ScanHit { char name[22]; int rssi; };
	ScanHit _scan[kMaxScan] = {};
	int _scanCount = 0;
	int _scanCursor = 0;
	bool _scanning = false;
	uint32_t _scanStartedMs = 0;
};

extern Menu menu;

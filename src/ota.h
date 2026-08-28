#pragma once

#include <Arduino.h>

class WebServer;

// WiFi OTA firmware update, entered manually from the Settings screen.
// Mirrors ~/projects/slider/firmware/.../wifi.cpp: STA connect to a known
// network (see wifi_env.h.example), then a minimal WebServer with a
// /update upload form (Update.h), reboot on success.
//
// WiFi shares the ESP32's radio with BLE and can disrupt active BLE
// central connections, so it's off unless explicitly requested from
// Settings, and gives up after OTA_WAIT_MS with nothing uploaded.
class Ota {
public:
	enum class State { Idle, Connecting, WaitingForUpload, Uploading, Failed, NoWifi };

	// Whether any WiFi credentials are compiled in (wifi_env.h present).
	static bool configured();

	void begin();  // enter OTA mode: connect WiFi, start server
	void cancel(); // leave OTA mode, WiFi off
	void update(); // call every loop()

	State state() const { return _state; }
	const char *ip() const { return _ip.c_str(); }

private:
	void startServer();
	void stopServer();

	State _state = State::Idle;
	String _ip;
	uint32_t _stateEnteredMs = 0;
	WebServer *_http = nullptr;
};

extern Ota ota;

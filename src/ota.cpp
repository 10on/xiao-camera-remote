#include "ota.h"

#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>

// wifi_env.h is gitignored (real credentials) — see wifi_env.h.example.
// Pattern mirrors ~/projects/slider's wifi_env.h.
#if __has_include("wifi_env.h")
#include "wifi_env.h"
#else
struct WifiCred {
	const char *ssid;
	const char *pass;
};
static const WifiCred WIFI_CREDENTIALS[] = {};
static const size_t WIFI_CREDENTIALS_COUNT = 0;
#endif

Ota ota;

static const uint32_t kWifiConnectTimeoutMs = 12000;
static const uint32_t kUploadWaitTimeoutMs = 60000; // "ждём прошивку в течение минуты"

bool Ota::configured() { return WIFI_CREDENTIALS_COUNT > 0; }

void Ota::begin() {
	if (_state == State::Connecting || _state == State::WaitingForUpload ||
	    _state == State::Uploading) {
		return; // already in progress
	}
	if (WIFI_CREDENTIALS_COUNT == 0) {
		_state = State::NoWifi;
		return;
	}

	WiFi.mode(WIFI_STA);
	WiFi.begin(WIFI_CREDENTIALS[0].ssid, WIFI_CREDENTIALS[0].pass);
	_state = State::Connecting;
	_stateEnteredMs = millis();
}

void Ota::cancel() {
	stopServer();
	WiFi.disconnect(true);
	WiFi.mode(WIFI_OFF);
	_state = State::Idle;
	_ip = String();
}

void Ota::stopServer() {
	if (_http) {
		_http->stop();
		delete _http;
		_http = nullptr;
	}
}

void Ota::startServer() {
	_http = new WebServer(80);

	_http->on("/", HTTP_GET, [this]() {
		_http->send(200, "text/plain", "XIAO Remote OTA. POST firmware to /update.\n");
	});

	_http->on("/update", HTTP_GET, [this]() {
		String html = "<!doctype html><html><body>"
		              "<h3>Firmware Update</h3>"
		              "<form method='POST' action='/update' enctype='multipart/form-data'>"
		              "<input type='file' name='firmware'>"
		              "<input type='submit' value='Update'></form>"
		              "</body></html>";
		_http->send(200, "text/html", html);
	});

	_http->on(
		"/update", HTTP_POST,
		[this]() {
			bool ok = !Update.hasError();
			_http->send(200, "text/plain", ok ? "Update OK, rebooting..." : "Update failed");
			if (ok) {
				delay(500);
				ESP.restart();
			} else {
				_state = State::Failed;
			}
		},
		[this]() {
			HTTPUpload &up = _http->upload();
			if (up.status == UPLOAD_FILE_START) {
				_state = State::Uploading;
				Update.begin(UPDATE_SIZE_UNKNOWN);
			} else if (up.status == UPLOAD_FILE_WRITE) {
				Update.write(up.buf, up.currentSize);
			} else if (up.status == UPLOAD_FILE_END) {
				Update.end(true);
			}
		});

	_http->begin();
}

void Ota::update() {
	if (_http) _http->handleClient();

	switch (_state) {
	case State::Idle:
	case State::Failed:
	case State::NoWifi:
		return;

	case State::Connecting:
		if (WiFi.status() == WL_CONNECTED) {
			_ip = WiFi.localIP().toString();
			startServer();
			_state = State::WaitingForUpload;
			_stateEnteredMs = millis();
		} else if (millis() - _stateEnteredMs > kWifiConnectTimeoutMs) {
			WiFi.mode(WIFI_OFF);
			_state = State::Failed;
		}
		break;

	case State::WaitingForUpload:
		if (millis() - _stateEnteredMs > kUploadWaitTimeoutMs) {
			stopServer();
			WiFi.disconnect(true);
			WiFi.mode(WIFI_OFF);
			_state = State::Failed;
		}
		break;

	case State::Uploading:
		// Handled by the /update handlers above — they either reboot on
		// success or fall back to State::Failed on error. No idle
		// timeout while a transfer is actually in progress.
		break;
	}
}

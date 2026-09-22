// ESP32-C3 radio co-processor. One control protocol everywhere: MIDI bytes.
//   BLE-MIDI peripheral  <->  UART1 (to Daisy USART1, 115200)  <->  WebSocket clients (JSON <-> MIDI)
// BLE is always on. The WiFi AP starts on a request from the Daisy (TAP held 3 s sends
// SysEx 7D 20) or at boot if WIFI_AT_BOOT is defined. Web UI lives in LittleFS (/index.html,
// built from ../../web). OTA: ArduinoOTA on the AP (hostname "pedal").
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoOTA.h>
#include <BLEMidi.h>

static const int PIN_TX = 21, PIN_RX = 20;     // C3 SuperMini UART1
static HardwareSerial daisy(1);
static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static bool wifiUp = false;
void startWifi();

// ---- MIDI <-> JSON for the web UI ----
// {"cc":8,"v":64}  {"cc":8,"v14":8192}  {"on":2,"v":1}  {"order":[3,0,1,2]}  {"hp":{"slot":0,"hz":80}}  {"lp":..}  {"tap":1}
// {"preset":{"save":true,"slot":2}}  {"tempo_ms":500}  {"req":"state"}
static void sendToDaisy(const uint8_t* b, size_t n) { daisy.write(b, n); }

static void jsonToMidi(const String& s) {
    // tiny hand parser: enough for the fixed message set above, no ArduinoJson dependency
    auto num = [&](const char* key, long def = -1) -> long { int i = s.indexOf(String("\"") + key + "\""); if (i < 0) return def; i = s.indexOf(':', i); return s.substring(i + 1).toInt(); };
    uint8_t b[16];
    if (s.indexOf("\"v14\"") >= 0) { const int cc = num("cc"), v = num("v14"); b[0] = 0xB0; b[1] = cc; b[2] = (v >> 7) & 0x7F; b[3] = 0xB0; b[4] = cc + 32; b[5] = v & 0x7F; sendToDaisy(b, 6); }
    else if (s.indexOf("\"cc\"") >= 0) { b[0] = 0xB0; b[1] = num("cc"); b[2] = num("v") & 0x7F; sendToDaisy(b, 3); }
    else if (s.indexOf("\"on\"") >= 0) { b[0] = 0xB0; b[1] = 64 + num("on"); b[2] = num("v") ? 127 : 0; sendToDaisy(b, 3); }
    else if (s.indexOf("\"order\"") >= 0) { int i = s.indexOf('['); b[0] = 0xF0; b[1] = 0x7D; b[2] = 0x01; for (int k = 0; k < 4; ++k) { b[3 + k] = s.substring(i + 1).toInt(); i = s.indexOf(',', i + 1); } b[7] = 0xF7; sendToDaisy(b, 8); }
    else if (s.indexOf("\"hp\"") >= 0 || s.indexOf("\"lp\"") >= 0) { const int hz = num("hz"); b[0] = 0xF0; b[1] = 0x7D; b[2] = s.indexOf("\"lp\"") >= 0 ? 0x05 : 0x04; b[3] = num("slot"); b[4] = (hz >> 7) & 0x7F; b[5] = hz & 0x7F; b[6] = 0xF7; sendToDaisy(b, 7); }
    else if (s.indexOf("\"tap\"") >= 0) { b[0] = 0xB0; b[1] = 72; b[2] = 127; sendToDaisy(b, 3); }
    else if (s.indexOf("\"tempo_ms\"") >= 0) { const int ms = num("tempo_ms"); b[0] = 0xF0; b[1] = 0x7D; b[2] = 0x06; b[3] = (ms >> 7) & 0x7F; b[4] = ms & 0x7F; b[5] = 0xF7; sendToDaisy(b, 6); }
    else if (s.indexOf("\"preset\"") >= 0) { b[0] = 0xF0; b[1] = 0x7D; b[2] = s.indexOf("true") >= 0 ? 0x02 : 0x03; b[3] = num("slot"); b[4] = 0xF7; sendToDaisy(b, 5); }
    else if (s.indexOf("\"req\"") >= 0) { b[0] = 0xF0; b[1] = 0x7D; b[2] = 0x07; b[3] = 0xF7; sendToDaisy(b, 4); }
}

// Daisy -> clients: forward raw MIDI to BLE and as JSON to WebSocket
static uint8_t rxStatus = 0; static int rxD1 = -1; static bool inSx = false; static uint8_t sx[64]; static int sxLen = 0;
static void midiFromDaisy(uint8_t c) {
    if (c == 0xF0) { inSx = true; sxLen = 0; return; }
    if (c == 0xF7) { inSx = false; if (sxLen >= 2 && sx[0] == 0x7D) {
            if (sx[1] == 0x01 && sxLen >= 6) ws.textAll(String("{\"order\":[") + sx[2] + "," + sx[3] + "," + sx[4] + "," + sx[5] + "]}");
            else if ((sx[1] == 0x04 || sx[1] == 0x05) && sxLen >= 5) ws.textAll(String("{\"") + (sx[1] == 0x05 ? "lp" : "hp") + "\":{\"slot\":" + sx[2] + ",\"hz\":" + ((sx[3] << 7) | sx[4]) + "}}");
            else if (sx[1] == 0x06 && sxLen >= 4) ws.textAll(String("{\"tempo_ms\":") + ((sx[2] << 7) | sx[3]) + "}");
            else if (sx[1] == 0x20) { startWifi(); }
        }
        return; }
    if (inSx) { if (sxLen < 64) sx[sxLen++] = c; return; }
    if (c & 0x80) { rxStatus = c; rxD1 = -1; return; }
    if (rxD1 < 0) { rxD1 = c; return; }
    if ((rxStatus & 0xF0) == 0xB0) {
        const uint8_t cc = rxD1, v = c; rxD1 = -1;
        BLEMidiServer.controlChange(0, cc, v);
        if (cc < 64) ws.textAll(String("{\"cc\":") + cc + ",\"v\":" + v + "}");
        else if (cc < 68) ws.textAll(String("{\"on\":") + (cc - 64) + ",\"v\":" + (v >= 64) + "}");
        else ws.textAll(String("{\"cc\":") + cc + ",\"v\":" + v + "}");
    } else rxD1 = -1;
}

void startWifi() {
    if (wifiUp) return;
    WiFi.mode(WIFI_AP); WiFi.softAP("pedal", "guitarfuzz");
    ArduinoOTA.setHostname("pedal"); ArduinoOTA.begin();
    server.begin(); wifiUp = true;
}

void setup() {
    Serial.begin(115200);
    daisy.begin(115200, SERIAL_8N1, PIN_RX, PIN_TX);
    LittleFS.begin(true);
    BLEMidiServer.begin("Pedal");
    BLEMidiServer.setControlChangeCallback([](uint8_t, uint8_t cc, uint8_t v, uint16_t) { uint8_t b[3] = { 0xB0, cc, v }; sendToDaisy(b, 3); });
    BLEMidiServer.setProgramChangeCallback([](uint8_t, uint8_t p, uint16_t) { uint8_t b[2] = { 0xC0, p }; sendToDaisy(b, 2); });
    ws.onEvent([](AsyncWebSocket*, AsyncWebSocketClient* c, AwsEventType t, void*, uint8_t* data, size_t len) {
        if (t == WS_EVT_CONNECT) { uint8_t b[4] = { 0xF0, 0x7D, 0x07, 0xF7 }; sendToDaisy(b, 4); }
        else if (t == WS_EVT_DATA) jsonToMidi(String(reinterpret_cast<char*>(data), len));
    });
    server.addHandler(&ws);
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    server.onNotFound([](AsyncWebServerRequest* r) { r->redirect("/"); });   // captive-portal style
#ifdef WIFI_AT_BOOT
    startWifi();
#endif
}

void loop() {
    while (daisy.available()) midiFromDaisy(daisy.read());
    if (wifiUp) ArduinoOTA.handle();
    ws.cleanupClients();
}

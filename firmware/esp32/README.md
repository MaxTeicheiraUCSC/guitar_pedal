# firmware/esp32 — radio co-processor (unverified)

ESP32-C3 SuperMini. BLE-MIDI peripheral + WiFi AP (`pedal` / `guitarfuzz`) serving
`../../web/index.html` over HTTP with a WebSocket at `/ws`; everything is translated
to the MIDI map in `docs/midi-map.md` and forwarded to the Daisy on UART1.

Status: **not built** here (no PlatformIO on the dev machine). Library names in
`platformio.ini` are the current ones on the registry; pin numbers are the C3
SuperMini's UART1 defaults — check against your board.

Wiring: C3 TX (GPIO21) → Seed3 D14 (USART1 RX); C3 RX (GPIO20) ← Seed3 D13 (USART1 TX); common GND. Both 3.3 V logic.

Web UI: `cp ../../web/index.html data/ && pio run -t uploadfs` (`data/` is a copy made at upload time; `web/` is the source).
OTA: `pio run -t upload --upload-port pedal.local` once connected to the AP.

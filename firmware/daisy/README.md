# firmware/daisy — Seed3 wrapper (unverified)

`main.cpp` binds the portable core in `../dsp` to the Daisy Seed3: audio callback,
footswitch gestures, knob bank with soft take-over, global HP/LP toggles, LEDs,
UART-MIDI link to the ESP32-C3, QSPI presets, CPU load logging, DFU gesture.

Status: **not compiled** in this repo yet — there is no `arm-none-eabi-gcc` on the
dev machine. Expect API-name fixes against the libDaisy version you check out
(`UartHandler` FIFO receive, `PersistentStorage`, `Switch::TimeHeldMs`).

Build: `git clone --recursive https://github.com/electro-smith/libDaisy third_party/libDaisy && make -C third_party/libDaisy && make`
Flash: hold fx1 + fx4 for 2 s (or BOOT+RESET) then `make program-dfu`. If you switch to
`APP_TYPE=BOOT_SRAM` (bigger images), flash the Daisy bootloader once with `make program-boot`;
the fx1+fx4 gesture then drops into that bootloader instead of the ROM DFU.

First hardware measurements to take (docs/design.md §12): `cpu avg/max` from the
serial log with all four effects on, fuzz at 4×; if max > 75 % drop the fuzz to 2×
(`Oversample` param) before touching anything else.

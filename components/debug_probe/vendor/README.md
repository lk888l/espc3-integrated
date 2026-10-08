Arm CMSIS-DAP reference command engine, Apache-2.0.
Upstream: https://github.com/ARM-software/CMSIS-DAP
Pinned upstream commit: 12636590eec66fae2d1bba4518749426ad5a4595

DAP.c, DAP.h and SW_DP.c were copied locally from the user's m5_sticks3
component, which already adapted the pinned engine for bounded cancellation,
unsigned decoding, pin wait masks and initialized transfer data.
All source and the upstream LICENSE are included; no build-time download.
This project's adapter uses ESP32-C3 RISC-V cycle counting and C3 GPIO LL,
with a checked 64-byte packet parser and a single Wi-Fi TCP owner.
SWD only: JTAG, USB bulk, BLE, SWO, UART and atomic commands are disabled.
GPIO LL is intentionally confined to this adapter and input sampling.
Validate it when upgrading beyond the tested ESP-IDF 5.5.4.

Additional local changes: ignore a parameter in a compiled-out JTAG handler;
check cancellation inside long sequences and idle clocks (at most 16 clocks
between checks). A cancelled sequence returns DAP_ERROR from the wrapper.
DAP.h includes DAP_config.h itself so adapter delay declarations do not
depend on the include order chosen by a formatter.

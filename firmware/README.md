# Firmware (nRF Connect SDK v3.4.1)

| App | Board | Role |
|---|---|---|
| `stick/` | `xiao_ble/nrf52840/sense` | drumstick: IMU, detection, ESB transmitter |
| `receiver/` | `xiao_ble/nrf52840` | USB receiver: ESB receiver, USB CDC to the host |
| `common/` | - | `protocol.h`, shared with the host and the Python tools |

Phase 0: each stick streams its IMU (1.66 kHz, RAW packets of 8 samples, STATUS every second)
to the receiver, which relays every packet to the host over USB CDC.
- Stick LEDs: blue blinks while packets go out, red while samples or packets are lost.
- Receiver LEDs: green blinks slowly when idle, faster with traffic; red when frames are dropped.

## Build
`west` runs through the pinned toolchain (see `docs/HARDWARE.md` for the installation):
```
export ZEPHYR_BASE=C:/ncs/v3.4.1/zephyr        # macOS: ~/ncs/v3.4.1/zephyr
nrfutil toolchain-manager launch --ncs-version v3.4.1 -- west build -b xiao_ble/nrf52840/sense firmware/stick -d firmware/stick/build/left
nrfutil toolchain-manager launch --ncs-version v3.4.1 -- west build -b xiao_ble/nrf52840/sense firmware/stick -d firmware/stick/build/right -- -DCONFIG_VDRUM_STICK_ID=1
nrfutil toolchain-manager launch --ncs-version v3.4.1 -- west build -b xiao_ble/nrf52840 firmware/receiver -d firmware/receiver/build/receiver
```
Always build inside a `build/` folder (ignored by git). Output:
`firmware/<app>/build/<name>/<app>/zephyr/zephyr.uf2` (application at 0x27000, the UF2
bootloader and its SoftDevice area are preserved).

**Paths must not contain spaces** (Zephyr's Kconfig/sysbuild fail on them). If the repository
lives in such a path, map it to a drive letter first (Windows):
```
subst V: "C:\path with spacesirtual-drum"
cd /d V:```

CI builds the left stick, the right stick and the receiver on every firmware change and
publishes the three UF2 files as artifacts
(`.github/workflows/firmware.yml`).

## Flash (UF2, no probe needed)
Double-tap the reset button: a USB drive appears. Copy `zephyr.uf2` onto it; the board
reboots into the new firmware.

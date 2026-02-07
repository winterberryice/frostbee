# frostbee

SHT40 temperature and humidity sensor application for the nRF52840 Dongle.

Built with Zephyr RTOS / nRF Connect SDK, using the Sensirion SHT4X driver
and ZBOSS Zigbee stack (Sleepy End Device).

## Hardware

- **Board:** nRF52840 Dongle (PCA10059)
- **Sensor:** Sensirion SHT40-AD1B (I2C address 0x44)
- **Pins:** SDA = P0.24, SCL = P1.00 (100 kHz)

## Build & Flash

### Development build (default — safe for UF2 bootloader)

```
west build -b nrf52840dongle_nrf52840 app
```

Copy the `.uf2` from `build/zephyr/` to the dongle in bootloader mode
(double-tap RESET, dongle mounts as USB drive).

Logging is enabled; RAM power-down is **off** so the bootloader's
double-tap reset keeps working even after a crash.

### Release build (battery-optimized — use only when Zigbee is validated)

```
west build -b nrf52840dongle_nrf52840 app -- -DOVERLAY_CONFIG=prj_release.conf
```

Disables logging/serial and enables RAM power-down for lower idle current.

> **Warning:** `CONFIG_RAM_POWER_DOWN_LIBRARY` can prevent the UF2 bootloader
> from detecting double-tap reset.  Only flash release builds when you have
> SWD/J-Link access for recovery, or when you are confident the firmware
> is stable.

## Flash Partitioning

`app/pm_static.yml` defines a safe flash layout that keeps ZBOSS NVRAM
away from the bootloader region.  The reserved bootloader area (160 KB)
is deliberately oversized to be safe regardless of bootloader variant:

```
0x000000 - 0x001000  MBR              (  4 KB)
0x001000 - 0x0cc000  Application      (812 KB)
0x0cc000 - 0x0d4000  ZBOSS NVRAM      ( 32 KB)
0x0d4000 - 0x0d8000  ZBOSS product cfg( 16 KB)
0x0d8000 - 0x100000  Bootloader       (160 KB)  ← protected
```

After SWD recovery, you can check the actual bootloader start address
and shrink the reserved region to reclaim flash:

```
nrfjprog --memrd 0x10001014   # reads UICR.BOOTLOADERADDR
```

## Recovery (bricked dongle)

If double-tap reset no longer enters bootloader mode:

1. Connect a J-Link, ST-Link, or other SWD debugger to the dongle's
   SWD pads (SWDIO, SWDCLK, GND).
2. Re-flash the UF2 bootloader hex with `nrfjprog` or OpenOCD:
   ```
   nrfjprog --program <bootloader.hex> --chiperase --verify --reset
   ```
3. After recovery, use the **development build** (no release overlay)
   until the firmware is validated.

## Serial Output

115200 baud, USB CDC (development builds only):

```
[00:00:00.000,000] <inf> frostbee: Frostbee starting - Zigbee SHT40 sensor
[00:00:00.100,000] <inf> frostbee: SHT40 sensor ready
```

## License

MIT

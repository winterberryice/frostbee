# frostbee

SHT40 temperature and humidity sensor application for the nRF52840 Dongle.

Built with Zephyr RTOS / nRF Connect SDK, using the Sensirion SHT4X driver
and ZBOSS Zigbee stack (Sleepy End Device).

## Hardware

- **Board:** nRF52840 Dongle (PCA10059)
- **Sensor:** Sensirion SHT40-AD1B (I2C address 0x44)

### Pin Assignment

| Function | Pin | Description |
|----------|-----|-------------|
| **I2C SDA** | P0.24 | SHT40 sensor data |
| **I2C SCL** | P1.00 | SHT40 sensor clock (100 kHz) |
| **Battery ADC** | P0.29 (AIN5) | Battery voltage measurement |
| **Battery Enable** | P0.02 | Voltage divider control (active LOW = GND) |
| **Reset Button** | P0.31 | Factory reset / force read |
| **Status LED** | P0.15 | Blue LED (active LOW) |

### Battery Voltage Measurement

The device measures battery voltage (3× AA in series, 3.0V - 4.5V) using a voltage divider with GPIO control for power saving:

```
BAT+ ─── R1 (10kΩ) ─── [P0.29/ADC] ─┬─── R2 (10kΩ) ─── [P0.02/GPIO] ─── GND
                                     │
                                     └─── C (0.1µF) ─── GND
```

**How it works:**
- **P0.02** is configured as **INPUT** (high-Z) when not measuring → 0µA power consumption
- During measurement (every 10s), **P0.02** is set to **OUTPUT LOW** → connects divider to GND
- ADC reads voltage on **P0.29**, then **P0.02** returns to INPUT mode
- Measurement duration: ~2ms per reading

**Components:**
- R1, R2: 10kΩ (voltage divider 1:2, scales 4.5V → 2.25V for ADC)
- C1: 0.1µF (noise filtering, RC time constant = 1ms)
- Power consumption: 150µA for ~2ms = 0.0008 mAh/day (negligible)

**Voltage ranges:**
- 3× AA fresh: 4.5V → 2.25V at ADC → 100% battery
- 3× AA depleted: 3.0V → 1.5V at ADC → 0% battery
- Low battery alarm: 3.0V (1.0V per cell)

## Build & Flash

### Development build (default — safe for UF2 bootloader)

```
west build -b nrf52840dongle_nrf52840 app
```

Copy the `.uf2` from `build/zephyr/` to the dongle in bootloader mode
(double-tap RESET, dongle mounts as USB drive).

ZBOSS NVRAM partitions are **not included** in the dev flash layout, so
the Zigbee stack cannot write to flash at all.  The device rejoins the
network on every boot.  Logging is enabled; RAM power-down is **off**.

### Release build (battery-optimized — use only when Zigbee is validated)

```
west build -b nrf52840dongle_nrf52840 app -- \
  -DOVERLAY_CONFIG=prj_release.conf \
  -DPM_STATIC_YML_FILE=pm_static_release.yml
```

Adds ZBOSS NVRAM partitions (in a safe flash region), disables
logging/serial, and enables RAM power-down for lower idle current.
Change `ERASE_PERSISTENT_CONFIG` to `ZB_FALSE` in `main.c` to persist
network state across reboots.

> **Warning:** `CONFIG_RAM_POWER_DOWN_LIBRARY` can prevent the UF2 bootloader
> from detecting double-tap reset.  Only flash release builds when you have
> SWD/J-Link access for recovery, or when you are confident the firmware
> is stable.

## Flash Partitioning

Two flash layouts are provided:

| File | ZBOSS NVRAM | Use case |
|---|---|---|
| `pm_static.yml` | **None** — zero flash writes | Development (default) |
| `pm_static_release.yml` | 32 KB + 16 KB in safe region | Production |

**Development layout** (`pm_static.yml`):
```
0x000000 - 0x001000  MBR              (  4 KB)
0x001000 - 0x0d8000  Application      (860 KB)
0x0d8000 - 0x100000  Bootloader       (160 KB)  ← protected
```

**Release layout** (`pm_static_release.yml`):
```
0x000000 - 0x001000  MBR              (  4 KB)
0x001000 - 0x0cc000  Application      (812 KB)
0x0cc000 - 0x0d4000  ZBOSS NVRAM      ( 32 KB)
0x0d4000 - 0x0d8000  ZBOSS product cfg( 16 KB)
0x0d8000 - 0x100000  Bootloader       (160 KB)  ← protected
```

The 160 KB bootloader reservation is deliberately oversized.  After SWD
recovery you can check the actual start address and reclaim flash:

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

## Home Assistant Integration

Frostbee works with both Zigbee2MQTT and ZHA:

| Integration | Setup | Folder |
|---|---|---|
| **Zigbee2MQTT** | External converter (copy + config edit) | [`zigbee2mqtt/`](zigbee2mqtt/) |
| **ZHA** | Works out of the box; optional quirk for device naming | [`zha/`](zha/) |

See the README in each folder for step-by-step instructions.

## License

MIT

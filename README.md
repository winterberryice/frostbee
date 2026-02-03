# frostbee

SHT40 temperature and humidity sensor application for the nRF52840 Dongle.

Built with Zephyr RTOS using the Sensirion SHT4X driver.

## Hardware

- **Board:** nRF52840 Dongle (PCA10059)
- **Sensor:** Sensirion SHT40-AD1B (I2C address 0x44)
- **Pins:** SDA = P0.24, SCL = P1.00 (100 kHz)

## Build & Flash

```
west build -b nrf52840dongle_nrf52840 app
```

Copy the `.uf2` from `build/zephyr/` to the dongle in bootloader mode.

## Serial Output

115200 baud, USB CDC:

```
[00:00:00.000,000] <inf> frostbee: Frostbee started - SHT40 sensor ready
[00:00:03.000,000] <inf> frostbee: T: 23.45 C  H: 48.12 %RH
```

## License

MIT

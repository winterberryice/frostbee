# Zigbee2MQTT Integration

External converter for Frostbee sensor to make it a "supported" device in Zigbee2MQTT.

## Installation

1. Copy `frostbee.js` to your Zigbee2MQTT data folder (where `configuration.yaml` is located)

2. Add to `configuration.yaml`:
   ```yaml
   external_converters:
     - frostbee.js
   ```

3. Restart Zigbee2MQTT

4. Remove and re-pair the device (or just restart Z2M if already paired)

## Docker

If running Z2M in Docker, the data folder is typically mounted as a volume. Copy the file there:

```bash
# Example for typical Docker setup
cp frostbee.js /path/to/zigbee2mqtt/data/
```

## Home Assistant Add-on

If using the Home Assistant add-on:

1. Access the Z2M data folder via Samba/SSH (usually `/config/zigbee2mqtt/`)
2. Copy `frostbee.js` there
3. Edit `configuration.yaml` in the same folder
4. Restart the add-on

## What this does

- Registers Frostbee as a supported device (no more "Unsupported" warning)
- Proper device icon and naming
- Configures standard temperature, humidity, and battery clusters

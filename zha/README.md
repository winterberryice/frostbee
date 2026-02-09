# ZHA Integration

Frostbee uses standard Zigbee HA clusters (Temperature Measurement, Relative
Humidity, Power Configuration), so ZHA discovers it automatically. All three
entities (temperature, humidity, battery) should appear without any extra
configuration.

The optional quirk below registers Frostbee as a "known" device so it shows
with its proper name instead of a generic model string.

## Quick start (no quirk)

1. Put your ZHA coordinator into pairing mode
2. Power on the Frostbee dongle
3. The device joins and ZHA creates temperature, humidity, and battery entities

That's it. If the entities appear correctly you don't need the quirk.

## Optional: install the custom quirk

1. Create a `custom_zha_quirks` folder inside your Home Assistant config
   directory (next to `configuration.yaml`):

   ```
   config/
     configuration.yaml
     custom_zha_quirks/
       frostbee.py        <-- copy here
   ```

2. Copy `frostbee.py` from this folder into `custom_zha_quirks/`

3. Tell ZHA to load custom quirks. Go to:

   **Settings > Devices & Services > ZHA > Configure**

   Set **Custom quirks path** to `/config/custom_zha_quirks`

4. Restart Home Assistant

5. If the device was already paired, remove and re-pair it (or restart HA)

## Home Assistant OS / Container

The config directory is typically `/config`. If you use SSH or Samba add-on
the path is the same.

## What the quirk does

- Registers `Frostbee FBE_TH_1` as a recognized device (removes "unknown"
  label)
- Maps all five server clusters: Basic, Identify, Power Configuration,
  Temperature Measurement, Relative Humidity
- No custom cluster overrides -- all clusters use standard ZCL behavior

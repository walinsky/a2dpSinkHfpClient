# A2DP-SINK & HFP CLIENT
## phonebook and avrc support
===========================================================================

## Turn your ESP32 into a bluetooth speaker and handsfree kit!

## How to use

### Hardware Required

Besides a ESP32 you'll need a I2S DAC (I used a PCM5102) and a mems microphone (I used a INMP441)
![mic](./img/INMP441-MEMS.jpg "INMP441 MEMS microphone")
![DAC](./img/dac.jpg "PCM5102 DAC")
![ESP32](./img/esp32.jpg "ESP32 with mic and dac")

### Build and Flash

After connecting your microphone and dac, set the pins accordingly in your main.c
In the examples folder you'll find projects that you can build straightaway.
For example:
```
cd examples/minimal
idf.py build

```
When creating your own project using this component, just make sure you (only) include `a2dpSinkHfpHf.h`.
Don't forget to set your config values before calling `a2dpSinkHfpHf_init`.

### What Gets Configured Automatically

On first build, the following files are copied from this component to your project root:

- `sdkconfig.defaults` - Bluetooth and system configuration defaults
- `partitions.csv` - Flash partition table for 4MB ESP32

However; for some reason sdkconfig.defaults is not picked up on first try.
Resulting in compiler errors missing header files.
Simply delete sdkconfig from your project root and build again.

## Usage
After the program is started, smart phones can discover and connect to a device named "ESP_SPEAKER".
You should be able to stream music from your phone now; and make/recieve phone calls.
If/when you allow to sync your phonebook, it will be stored locally on the ESP32.

## Notes
* A2DP is default 44.1kHz 16-bit stereo
* HFP client (only) supports msbc codec (highest quality)
* ESP IDF version >= 5.5.0 (!). IDF moves sbc coding to an external codec. I haven't tested if this component works on previous IDF versions.

## Issues
I'm not your helpdesk.
Everything is here: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/

## Pull requests
More than welcome.

## todo
* add volume control to a2dp and hfp
* expose hfp controls from a2dpSinkHfpHf
* expose phonebook controls from a2dpSinkHfpHf


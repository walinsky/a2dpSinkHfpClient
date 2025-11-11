# a2dpSinkHfpClient

**ESP-IDF Component for Bluetooth Classic Audio & Hands-Free**

[![ESP-IDF Version](https://img.shields.io/badge/ESP--IDF-v5.0+-blue.svg)](https://github.com/espressif/esp-idf)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

Turn your ESP32 into a fully-featured Bluetooth speaker and hands-free kit with support for music streaming, phone calls, contact sync, and playback control.

## Features

### 🎵 A2DP Sink (Audio Streaming)
- High-quality music streaming from phones/tablets
- SBC codec support (16kHz - 48kHz sample rates)
- Automatic sample rate detection
- Low-latency audio output via I2S

### 📞 HFP Hands-Free Client
- Make and receive phone calls
- Full-duplex audio (speaker + microphone)
- mSBC wideband codec for HD voice
- Call controls (answer, reject, hang up)
- Voice recognition support

### 🎛️ AVRCP Controller
- Playback control (play, pause, next, previous)
- Track metadata (title, artist, album, genre)
- Playback status monitoring
- Volume synchronization

### 📇 Phonebook Access (PBAP)
- Automatic contact synchronization
- Persistent storage in SPIFFS
- Caller ID with contact name lookup
- E.164 phone number normalization
- Search contacts by name/number

## Hardware Requirements

- **ESP32 module** (ESP32, ESP32-S3, or ESP32-C3)
- **I2S DAC/Amplifier** (e.g., PCM5102, MAX98357A, ES8388)
- **I2S Microphone** (e.g., INMP441, SPH0645) - optional for calls
- **Speakers** or headphones

### Example Hardware

<table>
<tr>
<td><img src="./img/INMP441-MEMS.jpg" width="200" alt="INMP441 MEMS Microphone"/><br/><b>INMP441 Microphone</b></td>
<td><img src="./img/dac.jpg" width="200" alt="PCM5102 DAC"/><br/><b>PCM5102 DAC</b></td>
<td><img src="./img/esp32.jpg" width="200" alt="ESP32 Setup"/><br/><b>Complete Setup</b></td>
</tr>
</table>

## Quick Start

### Installation

**From ESP Component Registry:**
```bash
idf.py add-dependency "a2dpSinkHfpClient"
```

**Manual Installation:**
```bash
cd your_project/components
git clone https://github.com/yourusername/a2dpSinkHfpClient.git
```

### Minimal Example

```c
#include "a2dpSinkHfpHf.h"
#include "nvs_flash.h"

void app_main(void) {
    // Initialize NVS
    nvs_flash_init();

    // Configure pins and device name
    a2dpSinkHfpHf_config_t config = {
        .device_name = "ESP32-Speaker",
        .i2s_tx_bck = 26,    // I2S BCK (speaker)
        .i2s_tx_ws = 25,     // I2S WS (speaker)
        .i2s_tx_dout = 22,   // I2S DOUT (speaker)
        .i2s_rx_bck = 32,    // I2S BCK (mic)
        .i2s_rx_ws = 33,     // I2S WS (mic)
        .i2s_rx_din = 34     // I2S DIN (mic)
    };

    // Initialize component
    a2dpSinkHfpHf_init(&config);
}
```

That's it! Your ESP32 is now discoverable and ready to pair.

## Examples

This repository includes two ready-to-use examples:

### 1. Minimal Example (`examples/minimal/`)

Basic setup with default configuration - perfect for getting started quickly.

**Features:**
- Simple initialization
- Default PIN code (1234)
- Basic A2DP audio streaming
- HFP call support

**Build & Flash:**
```bash
cd examples/minimal
idf.py build flash monitor
```

### 2. Full-Featured Example (`examples/avrc/`)

Complete demonstration with all features enabled and custom callbacks.

**Features:**
- Custom PIN code configuration
- AVRC metadata callbacks (display track info)
- Playback status monitoring
- Volume change callbacks
- Country code configuration for phonebook
- Comprehensive logging

**Build & Flash:**
```bash
cd examples/avrc
idf.py build flash monitor
```

## Configuration

### Required menuconfig Settings

Run `idf.py menuconfig` and enable:

```
Component config → Bluetooth → Bluedroid Options
    [*] A2DP
    [*] Hands Free Profile
        [*] Audio (SCO) data path in controller → HCI
        [*] Use external codec for mSBC
    [*] Phone Book Access Profile
    [*] AVRCP
```

### Custom PIN Code

```c
// Set before initialization
a2dpSinkHfpHf_set_pin("5678", 4);
a2dpSinkHfpHf_init(&config);
```

### Country Code (for Phonebook)

```c
// Set for proper phone number formatting
a2dpSinkHfpHf_set_country_code("1");   // USA
a2dpSinkHfpHf_set_country_code("31");  // Netherlands
a2dpSinkHfpHf_set_country_code("44");  // UK
```

### SPIFFS Partition (for Phonebook Storage)

Add to your `partitions.csv`:
```csv
# Name,   Type, SubType, Offset,  Size
nvs,      data, nvs,     0x9000,  0x6000
phy_init, data, phy,     0xf000,  0x1000
factory,  app,  factory, 0x10000, 1536K
storage,  data, spiffs,  ,        1M
```

## API Reference

### Initialization

```c
esp_err_t a2dpSinkHfpHf_init(const a2dpSinkHfpHf_config_t *config);
esp_err_t a2dpSinkHfpHf_deinit(void);
```

### Configuration

```c
esp_err_t a2dpSinkHfpHf_set_pin(const char *pin_code, uint8_t pin_len);
esp_err_t a2dpSinkHfpHf_set_country_code(const char *country_code);
```

### AVRC Control

```c
bool a2dpSinkHfpHf_avrc_play(void);
bool a2dpSinkHfpHf_avrc_pause(void);
bool a2dpSinkHfpHf_avrc_next(void);
bool a2dpSinkHfpHf_avrc_prev(void);
```

### Callbacks

```c
void a2dpSinkHfpHf_register_avrc_metadata_callback(bt_avrc_metadata_cb_t callback);
void a2dpSinkHfpHf_register_avrc_playback_callback(bt_avrc_playback_status_cb_t callback);
void a2dpSinkHfpHf_register_avrc_volume_callback(bt_avrc_volume_cb_t callback);
```

### Status

```c
bool a2dpSinkHfpHf_is_connected(void);
bool a2dpSinkHfpHf_is_avrc_connected(void);
const bt_avrc_metadata_t* a2dpSinkHfpHf_get_avrc_metadata(void);
```

## Advanced Usage

### Display Track Metadata

```c
void metadata_callback(const bt_avrc_metadata_t *metadata) {
    if (metadata && metadata->valid) {
        printf("Now Playing: %s - %s\n", metadata->artist, metadata->title);
        printf("Album: %s\n", metadata->album);
    }
}

a2dpSinkHfpHf_register_avrc_metadata_callback(metadata_callback);
```

### Monitor Playback Status

```c
void playback_callback(const bt_avrc_playback_status_t *status) {
    switch (status->status) {
        case ESP_AVRC_PLAYBACK_PLAYING:
            printf("Playing\n");
            break;
        case ESP_AVRC_PLAYBACK_PAUSED:
            printf("Paused\n");
            break;
    }
}

a2dpSinkHfpHf_register_avrc_playback_callback(playback_callback);
```

### Caller ID

Caller ID is handled automatically when phonebook is synced. The component will:
1. Download contacts when phone connects
2. Store in SPIFFS persistently
3. Display contact name for incoming calls

## Documentation

Comprehensive documentation is available in the [Wiki](../../wiki):

- [Getting Started](../../wiki/Getting-Started) - Installation and setup guide
- [API Reference](../../wiki/API-Reference) - Complete API documentation
- [AVRC Control](../../wiki/AVRC-Control) - Music control and metadata
- [Phonebook](../../wiki/Phonebook) - Contact sync and caller ID
- [Configuration](../../wiki/Configuration) - Advanced configuration options
- [Examples](../../wiki/Examples) - More code examples
- [Troubleshooting](../../wiki/Troubleshooting) - Common issues and solutions

## Pin Configuration

Configure I2S pins in your `main.c`:

```c
a2dpSinkHfpHf_config_t config = {
    .device_name = "ESP32-Audio",
    // Speaker output (DAC/Amplifier)
    .i2s_tx_bck = GPIO_NUM_26,   // Bit clock
    .i2s_tx_ws = GPIO_NUM_25,    // Word select
    .i2s_tx_dout = GPIO_NUM_22,  // Data out
    // Microphone input (for calls)
    .i2s_rx_bck = GPIO_NUM_32,   // Bit clock
    .i2s_rx_ws = GPIO_NUM_33,    // Word select
    .i2s_rx_din = GPIO_NUM_34    // Data in
};
```

**Note:** For music-only applications (no calls), set RX pins to `-1`:
```c
.i2s_rx_bck = -1,
.i2s_rx_ws = -1,
.i2s_rx_din = -1
```

## Supported ESP-IDF Versions

- ESP-IDF v5.0 and newer
- Tested on ESP-IDF v5.1, v5.2, v5.3

## Supported Hardware

- **ESP32** (all variants)
- **ESP32-S3**
- **ESP32-C3**

## Troubleshooting

### No Audio Output
- Verify I2S pin connections
- Check DAC power supply
- Enable debug logs: `esp_log_level_set("BT_I2S", ESP_LOG_DEBUG)`

### Cannot Pair
- Verify PIN code is set before `a2dpSinkHfpHf_init()`
- Clear paired devices: `nvs_flash_erase()` and restart
- Check Bluetooth is enabled in menuconfig

### Contacts Not Syncing
- Add SPIFFS partition to `partitions.csv`
- Grant contact sharing permission on phone
- Set correct country code: `a2dpSinkHfpHf_set_country_code("1")`

For more troubleshooting help, see the [Troubleshooting Guide](../../wiki/Troubleshooting).

## Contributing

Contributions are welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Submit a pull request

## License

Copyright (c) 2025 walinsky

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Acknowledgments

- Based on [ESP-IDF Bluetooth examples](https://github.com/espressif/esp-idf/tree/master/examples/bluetooth)
- Built with [ESP-IDF](https://github.com/espressif/esp-idf)
- Inspired by the ESP32 community

## Support

- **Issues:** [GitHub Issues](../../issues)
- **Discussions:** [GitHub Discussions](../../discussions)
- **ESP32 Forum:** [esp32.com](https://esp32.com/)

## Related Projects

- [ESP-IDF](https://github.com/espressif/esp-idf) - Official ESP32 development framework
- [ESP-ADF](https://github.com/espressif/esp-adf) - Audio development framework

---

**Made with ❤️ for the ESP32 community**

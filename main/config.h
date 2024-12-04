#ifndef GLOBALS_H
#define GLOBALS_H

// #include "driver/i2s_types.h"
#include "esp_gap_bt_api.h"
#include "driver/i2s_std.h"

namespace configuration
{
    /* set default parameters for Legacy Pairing (use fixed pin code 1234) */
    inline constexpr esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
    inline constexpr esp_bt_pin_code_t pin_code = {1, 2, 3, 4};

    // we will show up as a bluetooth device under this name
    inline constexpr char deviceName[] = "ESP_SPEAKER"; //(max 12 characters)
 
    // define our i2s ports
    inline constexpr i2s_port_t TX_I2S_PORT = I2S_NUM_0; // sound out at i2s port 0
    inline constexpr i2s_port_t RX_I2S_PORT = I2S_NUM_1; // sound in at i2s port 1

    // our i2s config for our mic
    // This is our INMP441 mems microphone. left channel, so pin is low
    inline constexpr i2s_std_config_t rx_i2s_config = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,    // we don't use mclock
            .bclk = GPIO_NUM_16,        // this is our bclk gpio pin
            .ws   = GPIO_NUM_27,        // this is our ws gpio pin
            .dout = I2S_GPIO_UNUSED,    // there is no data out on a rx channel
            .din  = GPIO_NUM_14,        // this is our din gpio pin
            .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = true,
                    .ws_inv   = false,
            },
        },
    };
    //this is our base tx config (sound out)
    inline constexpr i2s_std_config_t tx_i2s_config = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,    // we don't use mclock
            .bclk = GPIO_NUM_26,        // this is our bclk gpio pin
            .ws   = GPIO_NUM_17,        // this is our ws gpio pin
            .dout = GPIO_NUM_25,        // this is our dout gpio pin
            .din  = I2S_GPIO_UNUSED,    // there is no data in on a tx channel
            .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv   = false,
            },
        },
    };
} // namespace configuration
#endif
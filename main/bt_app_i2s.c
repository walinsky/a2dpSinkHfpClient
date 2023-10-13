#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"

#include "bt_app_core.h"
#include "bt_app_i2s.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"


#include "sys/lock.h"

#define BT_APP_HFP_SAMPLE_RATE          16000
#define BT_APP_HFP_I2S_DATA_BIT_WIDTH   I2S_DATA_BIT_WIDTH_32BIT
#define BT_APP_ADP_STANDARD_SAMPLE_RATE 44100
#define BT_APP_ADP_I2S_DATA_BIT_WIDTH   I2S_DATA_BIT_WIDTH_16BIT
int BT_APP_A2DP_SAMPLE_RATE =           BT_APP_ADP_STANDARD_SAMPLE_RATE; // this might be changed by a avrc event
int BT_APP_A2DP_CH_COUNT =              I2S_SLOT_MODE_STEREO;
bool tx_chan_running =                  false;
bool rx_chan_running =                  false;
// our channel handles
i2s_chan_handle_t tx_chan = NULL;
i2s_chan_handle_t rx_chan = NULL;
// semaphore handle for allowing a2dp to writing to i2s
extern SemaphoreHandle_t s_i2s_tx_mode_semaphore;
/*  
    I2S pins we use for our BT audio
    int bck, int ws, int dout, int din;
    we initialize with default values here
*/
I2S_pin_config i2sTxPinConfig = { 26, 17, 25, 0 };
I2S_pin_config i2sRxPinConfig = { 16, 27, 0, 14 };

i2s_chan_config_t bt_i2s_get_tx_chan_config(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    return chan_cfg;
}

i2s_std_clk_config_t bt_i2s_get_hfp_clk_cfg(void)
{
    i2s_std_clk_config_t hfp_clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(BT_APP_HFP_SAMPLE_RATE);
    ESP_LOGI(BT_APP_I2S_TAG, "reconfiguring hfp clock to sample rate:  %d", BT_APP_HFP_SAMPLE_RATE);
    return hfp_clk_cfg;
}

i2s_std_slot_config_t bt_i2s_get_hfp_tx_slot_cfg(void)
{
    i2s_std_slot_config_t hfp_slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(BT_APP_HFP_I2S_DATA_BIT_WIDTH, I2S_SLOT_MODE_MONO);
    hfp_slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    ESP_LOGI(BT_APP_I2S_TAG, "reconfiguring hfp tx slot to data bit width:  %d", BT_APP_HFP_I2S_DATA_BIT_WIDTH);
    return hfp_slot_cfg;
}

i2s_std_clk_config_t bt_i2s_get_adp_clk_cfg(void)
{
    i2s_std_clk_config_t adp_clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(BT_APP_A2DP_SAMPLE_RATE);
    ESP_LOGI(BT_APP_I2S_TAG, "reconfiguring adp clock to sample rate:  %d", BT_APP_A2DP_SAMPLE_RATE);
    return adp_clk_cfg;
}

i2s_std_slot_config_t bt_i2s_get_adp_slot_cfg(void)
{
    i2s_std_slot_config_t adp_slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(BT_APP_ADP_I2S_DATA_BIT_WIDTH, I2S_SLOT_MODE_STEREO);
    ESP_LOGI(BT_APP_I2S_TAG, "reconfiguring adp slot to data bit width:  %d", BT_APP_ADP_I2S_DATA_BIT_WIDTH);
    return adp_slot_cfg;
}

// Functions to set our I2S pins
void bt_i2s_set_tx_I2S_pins(int bckPin, int wsPin, int doPin, int diPin) {
    i2sTxPinConfig.bck = bckPin;
    i2sTxPinConfig.ws = wsPin;
    i2sTxPinConfig.dout = doPin;
    i2sTxPinConfig.din = diPin;
    ESP_LOGI(BT_APP_I2S_TAG, "setting tx GPIO Pins: BCK: %d WS: %d DOUT: %d DIN: %d ", i2sTxPinConfig.bck, i2sTxPinConfig.ws, i2sTxPinConfig.dout, i2sTxPinConfig.din);
}

void bt_i2s_set_rx_I2S_pins(int bckPin, int wsPin, int doPin, int diPin) {
    i2sRxPinConfig.bck = bckPin;
    i2sRxPinConfig.ws = wsPin;
    i2sRxPinConfig.dout = doPin;
    i2sRxPinConfig.din = diPin;
    ESP_LOGI(BT_APP_I2S_TAG, "setting rx GPIO Pins: BCK: %d WS: %d DOUT: %d DIN: %d ", i2sRxPinConfig.bck, i2sRxPinConfig.ws, i2sRxPinConfig.dout, i2sRxPinConfig.din);
}

// This is our tx channel (used for both ad2p sink and hfp tx)
void bt_i2s_init_tx_chan()
{
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_new_channel(&tx_chan_cfg, &tx_chan, NULL);
    i2s_std_config_t std_tx_cfg = {
        .clk_cfg = bt_i2s_get_adp_clk_cfg(),
        .slot_cfg = bt_i2s_get_adp_slot_cfg(),
        .gpio_cfg = {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = i2sTxPinConfig.bck,
                .ws   = i2sTxPinConfig.ws,
                .dout = i2sTxPinConfig.dout,
                .din  = I2S_GPIO_UNUSED,
                .invert_flags = {
                        .mclk_inv = false,
                        .bclk_inv = false,
                        .ws_inv   = false,
                },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &std_tx_cfg));
}

// This is our INMP441 mems microphone. left channel, so pin is low.
void bt_i2s_init_rx_chan()
{
    /* RX channel will be registered on our second I2S */
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan);
    i2s_std_config_t std_rx_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = i2sRxPinConfig.bck,
                .ws   = i2sRxPinConfig.ws,
                .dout = I2S_GPIO_UNUSED,
                .din  = i2sRxPinConfig.din,
                .invert_flags = {
                        .mclk_inv = false,
                        .bclk_inv = true,
                        .ws_inv   = false,
                },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &std_rx_cfg));
}

void bt_i2s_driver_install(void)
{
    ESP_LOGI(BT_APP_I2S_TAG, "%s", __func__);
    bt_i2s_init_tx_chan();
    bt_i2s_init_rx_chan();
}

void bt_i2s_driver_uninstall(void)
{
    if (tx_chan_running)
        {
        bt_i2s_tx_channel_disable();
        ESP_ERROR_CHECK(i2s_del_channel(tx_chan));
        }
    if (rx_chan_running)
        {
        bt_i2s_rx_channel_disable();
        ESP_ERROR_CHECK(i2s_del_channel(rx_chan));
        }
}

void bt_i2s_channels_disable(void)
{
    bt_i2s_tx_channel_disable();
    bt_i2s_rx_channel_disable();
}

void bt_i2s_tx_channel_enable(void)
{
    ESP_LOGI(BT_APP_I2S_TAG, "%s", __func__);
    if (!tx_chan_running)
     {
        ESP_LOGI(BT_APP_I2S_TAG, " -- not running; enabling now");
        ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
     }
    tx_chan_running = true;
}

void bt_i2s_tx_channel_disable(void)
{
    ESP_LOGI(BT_APP_I2S_TAG, "%s", __func__);
    if (tx_chan_running)
     {
        ESP_LOGI(BT_APP_I2S_TAG, " -- running; disabling now");
        ESP_ERROR_CHECK(i2s_channel_disable(tx_chan));
     }
    tx_chan_running = false;
}

void bt_i2s_rx_channel_enable(void)
{
    ESP_LOGI(BT_APP_I2S_TAG, "%s", __func__);
    if (!rx_chan_running)
     {
        ESP_LOGI(BT_APP_I2S_TAG, " -- not running; enabling now");
        ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
     }
    rx_chan_running = true;
}

void bt_i2s_rx_channel_disable(void)
{
    ESP_LOGI(BT_APP_I2S_TAG, "%s", __func__);
    if (rx_chan_running)
     {
        ESP_LOGI(BT_APP_I2S_TAG, " -- running; disabling now");
        ESP_ERROR_CHECK(i2s_channel_disable(rx_chan));
     }
    rx_chan_running = false;
}

void bt_i2s_tx_channel_reconfig_clock_slot(int sample_rate, int ch_count)
{
    BT_APP_A2DP_SAMPLE_RATE = sample_rate;
    BT_APP_A2DP_CH_COUNT = ch_count;
    bt_i2s_channels_config_adp();
}

void bt_i2s_audio_enable_adp(void)
{
    bt_i2s_channels_config_adp();
    bt_i2s_tx_channel_enable();
}

void bt_i2s_audio_disable_adp(void)
{
    bt_i2s_tx_channel_disable();
}

void bt_i2s_channels_config_adp(void)
{
    bool _isrunning = tx_chan_running; 
    i2s_std_clk_config_t clk_cfg = bt_i2s_get_adp_clk_cfg();
    i2s_std_slot_config_t slot_cfg = bt_i2s_get_adp_slot_cfg();
    bt_i2s_tx_channel_disable();
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_clock(tx_chan, &clk_cfg));
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_slot(tx_chan, &slot_cfg));
    if (_isrunning) {
        bt_i2s_tx_channel_enable();
    }
}

void bt_i2s_channels_config_hfp(void)
{
    bool _tx_is_running = tx_chan_running;

    i2s_std_clk_config_t clk_cfg = bt_i2s_get_hfp_clk_cfg();
    i2s_std_slot_config_t slot_cfg = bt_i2s_get_hfp_tx_slot_cfg();
    bt_i2s_tx_channel_disable();
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_clock(tx_chan, &clk_cfg));
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_slot(tx_chan, &slot_cfg));
    if (_tx_is_running) {
        bt_i2s_tx_channel_enable();
    }
}
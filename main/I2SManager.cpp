#include "config.h"
#include "I2SManager.h"
// #include "I2SConfig.h"
#include <esp_log.h>

#define TAG "I2SManager"

i2s_chan_handle_t tx_handle;
i2s_chan_handle_t rx_handle;
I2SManager::I2SManager() : initialized(false) {}

I2SManager::~I2SManager() {
    deinitialize();
}

bool I2SManager::initialize() {
    if (initialized) {
        ESP_LOGW(TAG, "I2S already initialized.");
        return true;
    }
    init_tx_chan();
    init_rx_chan();
    initialized = true;
    return true;
}

void I2SManager::configureForA2DP() {
    if (!initialized) initialize();
// do your i2s configure foo
    ESP_LOGI(TAG, "I2S configured for A2DP.");
}

void I2SManager::configureForHFP() {
    if (!initialized) initialize();
// do your i2s configure foo
    ESP_LOGI(TAG, "I2S configured for HFP.");
}

void I2SManager::deinitialize() {
    if (initialized) {
        deinitializeChannel(tx_handle);
        deinitializeChannel(rx_handle);
        ESP_LOGI(TAG, "I2S deinitialized.");
    }
}
void I2SManager::deinitializeChannel(i2s_chan_handle_t handle) {
    if (initialized) {
        /* Have to stop the channel before deleting it */
        i2s_channel_disable(handle);
        /* If the handle is not needed any more, delete it to release the channel resources */
        i2s_del_channel(handle);
        initialized = false;
        ESP_LOGI(TAG, "I2S channel deinitialized.");
    }
}

// This is our tx channel (used for both ad2p sink and hfp tx)
void init_tx_chan()
{
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(configuration::TX_I2S_PORT, I2S_ROLE_MASTER);
    i2s_new_channel(&tx_chan_cfg, &tx_handle, NULL);
    i2s_std_config_t std_tx_cfg = configuration::tx_i2s_config;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_tx_cfg));
}

// This is our rx channel (our mic)
void init_rx_chan()
{
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(configuration::RX_I2S_PORT, I2S_ROLE_MASTER);
    i2s_new_channel(&rx_chan_cfg, &rx_handle, NULL);
    i2s_std_config_t std_rx_cfg = configuration::rx_i2s_config;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_rx_cfg));
}

/* 
// Before writing data, start the TX channel first
i2s_channel_enable(tx_handle);
i2s_channel_write(tx_handle, src_buf, bytes_to_write, bytes_written, ticks_to_wait);

// If the configurations of slot or clock need to be updated,
// stop the channel first and then update it
i2s_channel_disable(tx_handle);
std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO; // Default is stereo
i2s_channel_reconfig_std_slot(tx_handle, &std_cfg.slot_cfg);
std_cfg.clk_cfg.sample_rate_hz = 96000;
i2s_channel_reconfig_std_clock(tx_handle, &std_cfg.clk_cfg);

// Have to stop the channel before deleting it
i2s_channel_disable(tx_handle);
// If the handle is not needed any more, delete it to release the channel resources
i2s_del_channel(tx_handle);
*/
/*
 * ESP32 A2DP Sink + HFP + AVRC Example - main.c
 * 
 * This example demonstrates how to use the a2dpSinkHfpHf component
 * with custom PIN code configuration at compile time.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "a2dpSinkHfpHf.h"

#define TAG "MAIN"

// ============================================================================
// COMPILE-TIME CONFIGURATION
// ============================================================================

// Set your custom PIN code here (4-16 digits)
#define BT_PIN_CODE "5678"
#define BT_PIN_LENGTH 4

// Bluetooth Device Name
#define BT_DEVICE_NAME      "ESP32-boo"

// country code for phonebook
#define COUNTRY_CODE        "31" // Netherlands

// ============================================================================
// AVRC CALLBACK EXAMPLES (Optional)
// ============================================================================

/**
 * @brief AVRC connection state callback
 */
void avrc_conn_callback(bool connected)
{
    if (connected) {
        ESP_LOGI(TAG, "AVRC Connected - Remote control active");
    } else {
        ESP_LOGI(TAG, "AVRC Disconnected");
    }
}

/**
 * @brief AVRC metadata callback (track info)
 */
void avrc_metadata_callback(const bt_avrc_metadata_t *metadata)
{
    if (metadata && metadata->valid) {
        ESP_LOGI(TAG, "═══════════════════════════════════════");
        ESP_LOGI(TAG, "Now Playing:");
        ESP_LOGI(TAG, "  Title:  %s", metadata->title);
        ESP_LOGI(TAG, "  Artist: %s", metadata->artist);
        ESP_LOGI(TAG, "  Album:  %s", metadata->album);
        if (metadata->track_num > 0) {
            ESP_LOGI(TAG, "  Track:  %d/%d", metadata->track_num, metadata->total_tracks);
        }
        ESP_LOGI(TAG, "═══════════════════════════════════════");
    }
}

/**
 * @brief AVRC playback status callback
 */
void avrc_playback_callback(const bt_avrc_playback_status_t *status)
{
    const char *status_str[] = {"Stopped", "Playing", "Paused", "Forward Seek", "Reverse Seek", "Error"};
    
    if (status->status < 6) {
        ESP_LOGI(TAG, "Playback Status: %s", status_str[status->status]);
    }
    
    if (status->song_len_ms > 0) {
        int pos_sec = status->song_pos_ms / 1000;
        int len_sec = status->song_len_ms / 1000;
        ESP_LOGI(TAG, "  Position: %d:%02d / %d:%02d", 
                 pos_sec / 60, pos_sec % 60,
                 len_sec / 60, len_sec % 60);
    }
}

/**
 * @brief AVRC volume callback
 */
void avrc_volume_callback(uint8_t volume)
{
    // Volume range: 0-127
    int percent = (volume * 100) / 127;
    ESP_LOGI(TAG, "Volume: %d%% (%d/127)", percent, volume);
}

// ============================================================================
// MAIN APPLICATION
// ============================================================================

void app_main(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "ESP32 A2DP Sink + HFP + AVRC Example");
    ESP_LOGI(TAG, "========================================");

    // ===== STEP 1: Initialize NVS (Required for Bluetooth) =====
    ESP_LOGI(TAG, "Initializing NVS...");
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs to be erased");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "✓ NVS initialized");

    // ===== STEP 2: Configure PIN Code (BEFORE init) =====
    ESP_LOGI(TAG, "Setting Bluetooth PIN code...");
    ret = a2dpSinkHfpHf_set_pin(BT_PIN_CODE, BT_PIN_LENGTH);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✓ PIN code set to: %s", BT_PIN_CODE);
        ESP_LOGW(TAG, "⚠️  Use this PIN when pairing with your phone!");
    } else {
        ESP_LOGE(TAG, "Failed to set PIN code: %s", esp_err_to_name(ret));
        return;
    }

    ret = a2dpSinkHfpHf_set_country_code(COUNTRY_CODE);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✓ country code set to: %s", COUNTRY_CODE);
        ESP_LOGW(TAG, "this is used when parsing the phonebook from your phone!");
    } else {
        ESP_LOGE(TAG, "Failed to set country code: %s", esp_err_to_name(ret));
        return;
    }
    // ===== STEP 3: Register AVRC Callbacks (Optional) =====
    ESP_LOGI(TAG, "Registering AVRC callbacks...");
    a2dpSinkHfpHf_register_avrc_conn_callback(avrc_conn_callback);
    a2dpSinkHfpHf_register_avrc_metadata_callback(avrc_metadata_callback);
    a2dpSinkHfpHf_register_avrc_playback_callback(avrc_playback_callback);
    a2dpSinkHfpHf_register_avrc_volume_callback(avrc_volume_callback);
    ESP_LOGI(TAG, "✓ AVRC callbacks registered");

    // ===== STEP 4: Configure Component =====
    // Using direct pin numbers (your preferred method)
    a2dpSinkHfpHf_config_t config = {
        .device_name = BT_DEVICE_NAME,
        .i2s_tx_bck = 26,   // TX BCK
        .i2s_tx_ws = 17,    // TX WS
        .i2s_tx_dout = 25,  // TX DOUT
        .i2s_rx_bck = 16,   // RX BCK
        .i2s_rx_ws = 27,    // RX WS
        .i2s_rx_din = 14    // RX DIN
    };

    // ===== STEP 5: Initialize Bluetooth Component =====
    ESP_LOGI(TAG, "Initializing Bluetooth component...");
    ret = a2dpSinkHfpHf_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize component: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "✓ System Ready!");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Device Name: %s", BT_DEVICE_NAME);
    ESP_LOGI(TAG, "PIN Code:    %s", BT_PIN_CODE);
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "I2S Configuration:");
    ESP_LOGI(TAG, "  TX: BCK=26, WS=17, DOUT=25");
    ESP_LOGI(TAG, "  RX: BCK=16, WS=27, DIN=14");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Instructions:");
    ESP_LOGI(TAG, "1. Scan for Bluetooth devices on your phone");
    ESP_LOGI(TAG, "2. Look for '%s'", BT_DEVICE_NAME);
    ESP_LOGI(TAG, "3. When prompted, enter PIN: %s", BT_PIN_CODE);
    ESP_LOGI(TAG, "4. Play music or make a call");
    ESP_LOGI(TAG, "========================================");

    // ===== STEP 6: Main Loop (Optional - Control Commands) =====
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        // Example: Check connection status
        if (a2dpSinkHfpHf_is_connected()) {
            ESP_LOGD(TAG, "Device connected");
            
            // Example: Get current track metadata
            const bt_avrc_metadata_t *metadata = a2dpSinkHfpHf_get_avrc_metadata();
            if (metadata && metadata->valid) {
                ESP_LOGD(TAG, "Current track: %s - %s", metadata->artist, metadata->title);
            }
        }
    }
}

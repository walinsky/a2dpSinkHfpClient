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

// just run menuconfig
// or if you insist. config here, and do your config calls from main
// before calling ESP_ERROR_CHECK(a2dpSinkHfpHf_init(&config));


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

    // ===== STEP 2: Register AVRC Callbacks (Optional) =====
    ESP_LOGI(TAG, "Registering AVRC callbacks...");
    a2dpSinkHfpHf_register_avrc_conn_callback(avrc_conn_callback);
    a2dpSinkHfpHf_register_avrc_metadata_callback(avrc_metadata_callback);
    a2dpSinkHfpHf_register_avrc_playback_callback(avrc_playback_callback);
    a2dpSinkHfpHf_register_avrc_volume_callback(avrc_volume_callback);
    ESP_LOGI(TAG, "✓ AVRC callbacks registered");

    // ===== STEP 3: Initialize Bluetooth Component =====
    ESP_LOGI(TAG, "Initializing Bluetooth component...");
    ret = a2dpSinkHfpHf_init(NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize component: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "✓ System Ready!");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Instructions:");
    ESP_LOGI(TAG, "1. Pair your phone");
    ESP_LOGI(TAG, "2. Play music or make a call");
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

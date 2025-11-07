/*
 * A2DP Sink + HFP Hands-Free Example with AVRC Event Handling
 * 
 * This example demonstrates how to:
 * - Initialize the Bluetooth component
 * - Register AVRC callbacks for metadata, playback status, and volume
 * - Control playback (play/pause/next/prev)
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "a2dpSinkHfpHf.h"

#define MAIN_TAG "MAIN"

// ============================================================================
// Device Configuration
// ============================================================================
#define DEVICE_NAME "ESP32-Audio"
#define COUNTRY_CODE "31"  // Netherlands

// ============================================================================
// PIN CONFIGURATION - CUSTOMIZE FOR YOUR HARDWARE
// ============================================================================
// I2S TX (audio output to speaker/DAC)
#define I2S_TX_BCK      26    // Bit clock
#define I2S_TX_WS       17    // Word select (LRCK)
#define I2S_TX_DOUT     25    // Data out

// I2S RX (audio input from microphone)
#define I2S_RX_BCK      16    // Bit clock
#define I2S_RX_WS       27    // Word select (LRCK)
#define I2S_RX_DIN      14    // Data in

// ============================================================================
// AVRC Event Callbacks
// ============================================================================

/**
 * @brief Called when AVRC connection state changes
 */
static void avrc_connection_callback(bool connected)
{
    if (connected) {
        ESP_LOGI(MAIN_TAG, "🔗 AVRC Connected");
    } else {
        ESP_LOGI(MAIN_TAG, "🔗 AVRC Disconnected");
    }
}

/**
 * @brief Called when track metadata changes (track name, artist, album, etc.)
 */
static void avrc_metadata_callback(const bt_avrc_metadata_t *metadata)
{
    if (metadata && metadata->valid) {
        ESP_LOGI(MAIN_TAG, "🎵 Now Playing:");
        
        if (metadata->title[0] != '\0') {
            ESP_LOGI(MAIN_TAG, "   Track:  %s", metadata->title);
        }
        
        if (metadata->artist[0] != '\0') {
            ESP_LOGI(MAIN_TAG, "   Artist: %s", metadata->artist);
        }
        
        if (metadata->album[0] != '\0') {
            ESP_LOGI(MAIN_TAG, "   Album:  %s", metadata->album);
        }
        
        if (metadata->genre[0] != '\0') {
            ESP_LOGI(MAIN_TAG, "   Genre:  %s", metadata->genre);
        }
        
        if (metadata->track_num > 0) {
            ESP_LOGI(MAIN_TAG, "   Track:  %d/%d", metadata->track_num, metadata->total_tracks);
        }
        
        if (metadata->playing_time_ms > 0) {
            int minutes = metadata->playing_time_ms / 60000;
            int seconds = (metadata->playing_time_ms % 60000) / 1000;
            ESP_LOGI(MAIN_TAG, "   Length: %d:%02d", minutes, seconds);
        }
    }
}

/**
 * @brief Called when playback status changes (playing, paused, stopped, etc.)
 */
static void avrc_playback_callback(const bt_avrc_playback_status_t *status)
{
    if (!status) {
        return;
    }
    
    const char *status_str[] = {
        "⏹️  Stopped",
        "▶️  Playing",
        "⏸️  Paused",
        "⏩ Forward Seeking",
        "⏪ Reverse Seeking"
    };
    
    if (status->status <= 4) {
        ESP_LOGI(MAIN_TAG, "%s", status_str[status->status]);
        
        // Display position if available
        if (status->song_len_ms > 0) {
            int pos_min = status->song_pos_ms / 60000;
            int pos_sec = (status->song_pos_ms % 60000) / 1000;
            int len_min = status->song_len_ms / 60000;
            int len_sec = (status->song_len_ms % 60000) / 1000;
            
            ESP_LOGI(MAIN_TAG, "   Position: %d:%02d / %d:%02d", 
                     pos_min, pos_sec, len_min, len_sec);
        }
    }
}

/**
 * @brief Called when volume changes
 */
static void avrc_volume_callback(uint8_t volume)
{
    // Volume is 0-127, convert to percentage
    int volume_percent = (volume * 100) / 127;
    ESP_LOGI(MAIN_TAG, "🔊 Volume: %d%%", volume_percent);
}

// ============================================================================
// Playback Control Demo Task (Optional)
// ============================================================================

/**
 * @brief Demo task that shows how to control playback
 * This is just an example - remove or modify as needed
 */
static void playback_control_demo_task(void *arg)
{
    ESP_LOGI(MAIN_TAG, "Playback control demo task started");
    ESP_LOGI(MAIN_TAG, "Waiting 30 seconds before sending commands...");
    
    vTaskDelay(pdMS_TO_TICKS(30000));  // Wait 30 seconds
    
    while (1) {
        if (a2dpSinkHfpHf_is_avrc_connected()) {
            ESP_LOGI(MAIN_TAG, "⏸️  Sending PAUSE command");
            a2dpSinkHfpHf_avrc_pause();
            vTaskDelay(pdMS_TO_TICKS(10000));  // Wait 10 seconds
            
            ESP_LOGI(MAIN_TAG, "▶️  Sending PLAY command");
            a2dpSinkHfpHf_avrc_play();
            vTaskDelay(pdMS_TO_TICKS(10000));  // Wait 10 seconds
            
            ESP_LOGI(MAIN_TAG, "⏭️  Sending NEXT command");
            a2dpSinkHfpHf_avrc_next();
            vTaskDelay(pdMS_TO_TICKS(15000));  // Wait 15 seconds
            
            ESP_LOGI(MAIN_TAG, "⏮️  Sending PREV command");
            a2dpSinkHfpHf_avrc_prev();
            vTaskDelay(pdMS_TO_TICKS(15000));  // Wait 15 seconds
        } else {
            ESP_LOGW(MAIN_TAG, "AVRC not connected, skipping playback control demo");
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
}

// ============================================================================
// APPLICATION ENTRY POINT
// ============================================================================

void app_main(void)
{
    ESP_LOGI(MAIN_TAG, "Starting A2DP Sink + HFP with AVRC Event Handling");

    // ===== Step 1: Initialize NVS =====
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // ===== Step 2: Register AVRC Callbacks BEFORE Initialization =====
    ESP_LOGI(MAIN_TAG, "Registering AVRC event callbacks...");
    
    a2dpSinkHfpHf_register_avrc_conn_callback(avrc_connection_callback);
    a2dpSinkHfpHf_register_avrc_metadata_callback(avrc_metadata_callback);
    a2dpSinkHfpHf_register_avrc_playback_callback(avrc_playback_callback);
    a2dpSinkHfpHf_register_avrc_volume_callback(avrc_volume_callback);
    
    ESP_LOGI(MAIN_TAG, "✓ AVRC callbacks registered");

    // ===== Step 3: Configure & Initialize Component =====
    a2dpSinkHfpHf_set_country_code(COUNTRY_CODE);

    a2dpSinkHfpHf_config_t config = {
        .device_name = DEVICE_NAME,
        .i2s_tx_bck = I2S_TX_BCK,
        .i2s_tx_ws = I2S_TX_WS,
        .i2s_tx_dout = I2S_TX_DOUT,
        .i2s_rx_bck = I2S_RX_BCK,
        .i2s_rx_ws = I2S_RX_WS,
        .i2s_rx_din = I2S_RX_DIN,
    };

    ESP_ERROR_CHECK(a2dpSinkHfpHf_init(&config));

    // ===== Ready =====
    ESP_LOGI(MAIN_TAG, "");
    ESP_LOGI(MAIN_TAG, "========================================");
    ESP_LOGI(MAIN_TAG, "✓ A2DP Sink + HFP Ready with AVRC!");
    ESP_LOGI(MAIN_TAG, "Device Name: %s", a2dpSinkHfpHf_get_device_name());
    ESP_LOGI(MAIN_TAG, "========================================");
    ESP_LOGI(MAIN_TAG, "");
    ESP_LOGI(MAIN_TAG, "AVRC Events Registered:");
    ESP_LOGI(MAIN_TAG, "  ✓ Connection state");
    ESP_LOGI(MAIN_TAG, "  ✓ Metadata (track, artist, album)");
    ESP_LOGI(MAIN_TAG, "  ✓ Playback status");
    ESP_LOGI(MAIN_TAG, "  ✓ Volume changes");
    ESP_LOGI(MAIN_TAG, "");
    ESP_LOGI(MAIN_TAG, "Waiting for incoming connections...");
    ESP_LOGI(MAIN_TAG, "");

    // ===== Optional: Start playback control demo task =====
    // Uncomment the line below to enable automatic playback control demo
    // xTaskCreate(playback_control_demo_task, "playback_demo", 4096, NULL, 5, NULL);

    // Keep application running
    vTaskDelay(portMAX_DELAY);
}

/*
 * Minimal A2DP Sink + HFP Hands-Free Example
 * This is the simplest possible setup to get audio working.
 * Users customize the pin numbers for their hardware.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "a2dpSinkHfpHf.h"

#define MAIN_TAG "MAIN"

// ============================================================================
// Device name - This is how your device shows up when searching bluetooth
// ============================================================================
#define DEVICE_NAME "ESP32-Audio"
// ============================================================================
// Country code - Your country code; used in your phonebook
// ============================================================================
#define COUNTRY_CODE "31"

// ============================================================================
// PIN CONFIGURATION - CUSTOMIZE FOR YOUR HARDWARE
// ============================================================================

// I2S TX (audio output to speaker/DAC)
#define I2S_TX_BCK  26  // Bit clock
#define I2S_TX_WS   17  // Word select (LRCK)
#define I2S_TX_DOUT 25  // Data out

// I2S RX (audio input from microphone)
#define I2S_RX_BCK  16  // Bit clock
#define I2S_RX_WS   27  // Word select (LRCK)
#define I2S_RX_DIN  14  // Data in

// ============================================================================
// APPLICATION ENTRY POINT
// ============================================================================

#define HEAP_MONITOR_PERIOD_MS 5000  // Report every 5 seconds

static void heap_monitor_task(void *arg)
{
    while (1) {
        // Get heap information
        size_t free_heap = esp_get_free_heap_size();
        size_t min_free_heap = esp_get_minimum_free_heap_size();
        size_t largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        
        // Get internal DRAM info
        size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t total_internal = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
        size_t used_internal = total_internal - free_internal;
        
        ESP_LOGI("HEAP_MONITOR", 
                "Free: %d bytes | Min Free: %d bytes | Largest Block: %d bytes | Used: %d/%d (%.1f%%)",
                free_heap,
                min_free_heap,
                largest_free_block,
                used_internal,
                total_internal,
                (used_internal * 100.0f) / total_internal);
        
        vTaskDelay(pdMS_TO_TICKS(HEAP_MONITOR_PERIOD_MS));
    }
}

void app_main(void)
{
    ESP_LOGI(MAIN_TAG, "Starting A2DP Sink + HFP Hands-Free application");

    // ===== Step 1: Initialize NVS =====
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    xTaskCreate(heap_monitor_task, "heap_mon", 3072, NULL, 1, NULL);

    // ===== Step 2: Configure & Initialize Component =====
    
    // Set country code BEFORE initialization
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
    ESP_LOGI(MAIN_TAG, "A2DP Sink + HFP Ready!");
    ESP_LOGI(MAIN_TAG, "Device Name: %s", a2dpSinkHfpHf_get_device_name());
    ESP_LOGI(MAIN_TAG, "========================================");
    ESP_LOGI(MAIN_TAG, "");
    ESP_LOGI(MAIN_TAG, "Waiting for incoming connections...");
    ESP_LOGI(MAIN_TAG, "");

    // Keep application running
    vTaskDelay(portMAX_DELAY);
}

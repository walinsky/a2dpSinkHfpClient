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
#define BT_DEVICE_NAME      "ESP32-audio"

// country code for phonebook
#define COUNTRY_CODE        "31" // Netherlands

// I2S PINS
#define I2S_TX_BCK  26
#define I2S_TX_WS   17
#define I2S_TX_DOUT 25
#define I2S_RX_BCK  16
#define I2S_RX_WS   27
#define I2S_RX_DIN  14

// ============================================================================
// MAIN APPLICATION
// ============================================================================

void app_main(void)
{
    esp_err_t ret;

    // Initialize NVS (Required for Bluetooth)
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs to be erased");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    // set our bt pincode for pairing
    a2dpSinkHfpHf_set_pin(BT_PIN_CODE, BT_PIN_LENGTH);
    // set our country code (used by phonebook for parsing phone numbers)
    a2dpSinkHfpHf_set_country_code(COUNTRY_CODE);
    // set i2s pins (tx: dac, rx: microphone)
    a2dpSinkHfpHf_config_t config = {
        .device_name = BT_DEVICE_NAME,
        .i2s_tx_bck = I2S_TX_BCK,
        .i2s_tx_ws = I2S_TX_WS,
        .i2s_tx_dout = I2S_TX_DOUT,
        .i2s_rx_bck = I2S_RX_BCK,
        .i2s_rx_ws = I2S_RX_WS,
        .i2s_rx_din = I2S_RX_DIN
    };


    ret = a2dpSinkHfpHf_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize component: %s", esp_err_to_name(ret));
        return;
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

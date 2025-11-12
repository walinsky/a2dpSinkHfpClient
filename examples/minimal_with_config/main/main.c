#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "a2dpSinkHfpHf.h"

#define TAG "MAIN"

void app_main(void)
{
    esp_err_t ret;
    
    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Optional: Override specific settings at runtime
    a2dpSinkHfpHf_set_pin("1234", 4);
    a2dpSinkHfpHf_set_country_code("1");

    // Or provide full custom config
    a2dpSinkHfpHf_config_t custom_config = {
        .device_name = "My-Custom-Device",
        .i2s_tx_bck = 22,
        .i2s_tx_ws = 23,
        .i2s_tx_dout = 21,
        .i2s_rx_bck = 19,
        .i2s_rx_ws = 18,
        .i2s_rx_din = 5
    };
    
    ret = a2dpSinkHfpHf_init(&custom_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize: %s", esp_err_to_name(ret));
        return;
    }

    // Application code...
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

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
    
    // Initialize NVS (Required for Bluetooth)
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs to be erased");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize with Kconfig defaults (no arguments needed!)
    ret = a2dpSinkHfpHf_init(NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize component: %s", esp_err_to_name(ret));
        return;
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
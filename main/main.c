#include "nvs.h"
#include "nvs_flash.h"
#include "bt_app_core.h"

/*******************************
 * MAIN ENTRY POINT
 ******************************/

void app_main(void)
{
    /* initialize NVS — it is used to store PHY calibration data */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* optionally set our GPIO pins for TX (DAC) and RX (MEMS microphone) */
    bt_i2s_set_tx_I2S_pins(GPIO_NUM_26, GPIO_NUM_17, GPIO_NUM_25, GPIO_NUM_NC);
    bt_i2s_set_rx_I2S_pins(GPIO_NUM_16, GPIO_NUM_27, GPIO_NUM_NC, GPIO_NUM_14);

    /* optionally set our bt pin code (4 digits) here*/
    bt_app_set_pin_code("0000", 4);
    
    /* optionally set our bt name here (max 12 characters)*/
    bt_app_set_device_name("ESP_SPEAKER");

    bt_app_bt_init();
}

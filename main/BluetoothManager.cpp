#include "BluetoothManager.h"
#include "config.h"
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_log.h>

#define TAG "BluetoothManager"

BluetoothManager::BluetoothManager() : initialized(false) {}

BluetoothManager::~BluetoothManager() {
    deinitialize();
}

BluetoothManager& BluetoothManager::getInstance() {
    static BluetoothManager instance;
    return instance;
}

bool BluetoothManager::initialize() {
    if (initialized) {
        ESP_LOGW(TAG, "Bluetooth already initialized.");
        return true;
    }
    /*
     * We only use the functions of Classical Bluetooth.
     * So release the controller memory for Bluetooth Low Energy.
     */
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller init failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller enable failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return false;
    }

    // ret = esp_bt_dev_set_device_name(configuration::deviceName);
    //     if (ret != ESP_OK) {
    //     ESP_LOGE(TAG, "Could not set device name: %s", esp_err_to_name(ret));
    //     return false;
    // }
    initialized = true;
    ESP_LOGI(TAG, "Bluetooth initialized with device name: %s", configuration::deviceName);
    return true;
}

void BluetoothManager::deinitialize() {
    if (initialized) {
        esp_bluedroid_disable();
        esp_bluedroid_deinit();
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        initialized = false;
        ESP_LOGI(TAG, "Bluetooth deinitialized.");
    }
}

#include "HFPClient.h"
#include <esp_log.h>
#include <esp_bt_defs.h>
#include <cstring> // For memset and memcpy

#define TAG "HFPClient"

esp_bd_addr_t remote_bda; //remote bluetooth device address

HFPClient::HFPClient() : initialized(false), connected(false) {}

HFPClient::~HFPClient() {
    if (initialized) {
        esp_hf_client_deinit();
    }
}

bool HFPClient::initialize() {
    esp_err_t ret = esp_hf_client_register_callback(eventCallback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register HFP client callback: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_hf_client_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize HFP client: %s", esp_err_to_name(ret));
        return false;
    }

    initialized = true;
    ESP_LOGI(TAG, "HFP Client initialized.");
    return true;
}

void HFPClient::connect(const std::string& remoteAddress) {
    if (!initialized) {
        ESP_LOGE(TAG, "HFP Client not initialized.");
        return;
    }
}

void HFPClient::disconnect() {
    if (!connected) {
        ESP_LOGW(TAG, "HFP Client not connected.");
        return;
    }
    // esp_err_t esp_hf_client_disconnect(esp_bd_addr_t remote_bda)
}

void HFPClient::eventCallback(esp_hf_client_cb_event_t event, esp_hf_client_cb_param_t* param) {
    switch (event) {
        case ESP_HF_CLIENT_CONNECTION_STATE_EVT: {
            if (param->conn_stat.state == ESP_HF_CLIENT_CONNECTION_STATE_CONNECTED) {
                ESP_LOGI(TAG, "HFP connected.");
                // connected = true;
            } else if (param->conn_stat.state == ESP_HF_CLIENT_CONNECTION_STATE_DISCONNECTED) {
                ESP_LOGI(TAG, "HFP disconnected.");
                // connected = false;
            } else {
                ESP_LOGI(TAG, "HFP connection state changed: %d", param->conn_stat.state);
            }
            break;
        }
        case ESP_HF_CLIENT_AUDIO_STATE_EVT: {
            ESP_LOGI(TAG, "HFP audio state changed: %d", param->audio_stat.state);
            break;
        }
        default:
            ESP_LOGW(TAG, "Unhandled HFP event: %d", event);
            break;
    }
}

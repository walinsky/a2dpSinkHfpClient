#include "A2DPSink.h"
#include "BluetoothManager.h"
#include <esp_log.h>
#include <esp_bt.h>
#include <esp_bt_main.h>

#define TAG "A2DPSink"

A2DPSink::A2DPSink() : initialized(false) {}

A2DPSink::~A2DPSink() {
    stop();
}



bool A2DPSink::initialize() {
    // Use BluetoothManager to handle initialization
    BluetoothManager& btManager = BluetoothManager::getInstance();

    esp_a2d_register_callback(eventCallback);
    esp_a2d_sink_register_data_callback(audioDataCallback);
    esp_a2d_sink_init();

    initialized = true;
    ESP_LOGI(TAG, "A2DP Sink initialized.");
    return true;
}

void A2DPSink::stop() {
    if (initialized) {
        esp_a2d_sink_deinit();
        initialized = false;
    }
}


void A2DPSink::eventCallback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t* param) {
    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT:
            ESP_LOGI(TAG, "A2DP connection state: %d", param->conn_stat.state);
            break;
        case ESP_A2D_AUDIO_STATE_EVT:
            ESP_LOGI(TAG, "A2DP audio state: %d", param->audio_stat.state);
            break;
        default:
            ESP_LOGW(TAG, "Unhandled A2DP event: %d", event);
            break;
    }
}

void A2DPSink::audioDataCallback(const uint8_t* data, uint32_t len) {
    // Handle incoming audio data for playback
    ESP_LOGI(TAG, "Audio data received, length: %lu", len);
}

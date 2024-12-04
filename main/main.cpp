#include <esp_log.h>
// #include "config.h"//this is our pin/board config in the 'constants' namespace
#include "A2DPSink.h"
#include "HFPClient.h"
#include "BluetoothManager.h"
#include "I2SManager.h"

#include "driver/i2s_std.h"


extern "C" void app_main() {
    BluetoothManager& btManager = BluetoothManager::getInstance();
    btManager.initialize();

    A2DPSink a2dpSink;
    if (a2dpSink.initialize()) {
        // A2DP is ready
    }

    HFPClient hfpClient;
    if (hfpClient.initialize()) {
        // HFP is ready
    }

    // Initialize I2S with the custom pins for I2S1
    
    ESP_LOGI("Main", "I2S1 configured for INMP441 microphone.");
    
    // Application logic...
}

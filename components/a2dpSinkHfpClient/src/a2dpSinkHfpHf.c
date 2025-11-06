/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <string.h>
#include "esp_log.h"
#include "esp_bt_main.h"
#include "esp_hf_client_api.h"
#include "esp_bt.h"
#include "esp_bt_defs.h"
#include "esp_gap_bt_api.h"
#include "a2dpSinkHfpHf.h"
#include "a2dpSink.h"
#include "bt_gap.h"
#include "bt_app_hf.h"
#include "bt_i2s.h"
#include "codec.h"
#include "bt_app_pbac.h"
#include "phonebook.h"

#define A2DP_SINK_HFP_HF_TAG "A2DP_SINK_HFP_HF"

// Component state
static bool s_component_initialized = false;
static a2dpSinkHfpHf_config_t s_current_config = {0};
static char s_country_code[4] = DEFAULT_COUNTRY_CODE;

// ============================================================================
// COMPONENT INITIALIZATION
// ============================================================================

/**
 * @brief Initialize all BT subsystems in sequence
 * 
 * Steps:
 * 1. I2S audio interface configuration and initialization
 * 2. Audio codec (mSBC for HFP/A2DP)
 * 3. GAP layer (device discovery, pairing, authentication)
 * 4. HFP Hands-Free profile
 * 5. A2DP Sink profile
 */
esp_err_t a2dpSinkHfpHf_init(const a2dpSinkHfpHf_config_t *config)
{
    if (!config) {
        ESP_LOGE(A2DP_SINK_HFP_HF_TAG, "Config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_component_initialized) {
        ESP_LOGW(A2DP_SINK_HFP_HF_TAG, "Component already initialized");
        return ESP_OK;
    }

    // Store configuration
    memcpy(&s_current_config, config, sizeof(a2dpSinkHfpHf_config_t));

    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "========================================");
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "Initializing A2DP Sink + HFP Hands-Free");
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "Device: %s", config->device_name);
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "========================================");

    esp_err_t ret;

    // ===== STEP 0: Initialize Bluetooth Controller & Bluedroid =====
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "[0/5] Initializing Bluetooth Controller");

    // Release BLE memory (Classic BT only)
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    // Initialize BT Controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    bt_cfg.mode = ESP_BT_MODE_CLASSIC_BT;
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(A2DP_SINK_HFP_HF_TAG, "Failed to init BT controller: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(A2DP_SINK_HFP_HF_TAG, "Failed to enable BT controller: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, " ✓ BT Controller initialized");

    // Initialize Bluedroid with SSP disabled
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "[0/5] Initializing Bluedroid Stack");
    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    bluedroid_cfg.ssp_en = false;  // Disable SSP - use classic PIN pairing
    ret = esp_bluedroid_init_with_cfg(&bluedroid_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(A2DP_SINK_HFP_HF_TAG, "Failed to init Bluedroid: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(A2DP_SINK_HFP_HF_TAG, "Failed to enable Bluedroid: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, " ✓ Bluedroid initialized (SSP disabled)");

    // ===== STEP 1: Initialize I2S interface =====
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "[1/5] Initializing I2S interface");
    bt_i2s_set_tx_I2S_pins(config->i2s_tx_bck, config->i2s_tx_ws, config->i2s_tx_dout, 0);
    bt_i2s_set_rx_I2S_pins(config->i2s_rx_bck, config->i2s_rx_ws, 0, config->i2s_rx_din);
    bt_i2s_init();
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, " ✓ I2S interface initialized");

    // ===== STEP 2: Initialize audio codec =====
    /* ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "[2/5] Initializing audio codec (mSBC)");
    msbc_enc_open();
    msbc_dec_open();
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, " ✓ Audio codec initialized"); */

    // ===== STEP 3: Initialize GAP layer =====
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "[3/5] Initializing GAP layer");
    ret = bt_gap_init();
    if (ret != ESP_OK) {
        ESP_LOGE(A2DP_SINK_HFP_HF_TAG, "Failed to initialize GAP: %d", ret);
        goto err_cleanup;
    }

    ret = bt_gap_set_device_name(config->device_name);
    if (ret != ESP_OK) {
        ESP_LOGE(A2DP_SINK_HFP_HF_TAG, "Failed to set device name: %d", ret);
        goto err_cleanup;
    }

    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, " ✓ GAP layer initialized");

    // ===== STEP 4: Initialize HFP Hands-Free profile =====
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "[4/5] Initializing HFP Hands-Free profile");
    phonebook_init();
    phonebook_set_country_code(s_country_code);  // Netherlands - change as needed

    // Start phonebook processing task BEFORE Bluetooth
    bt_app_pbac_task_start();

    ret = esp_hf_client_register_callback(bt_app_hf_client_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(A2DP_SINK_HFP_HF_TAG, "Failed to register HFP callback: %d", ret);
        goto err_cleanup;
    }

    ret = esp_hf_client_init();
    if (ret != ESP_OK) {
        ESP_LOGE(A2DP_SINK_HFP_HF_TAG, "Failed to initialize HFP client: %d", ret);
        goto err_cleanup;
    }

    esp_pbac_register_callback(bt_app_pbac_cb);
    esp_pbac_init();

    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, " ✓ HFP Hands-Free profile initialized");

    // ===== STEP 5: Initialize A2DP Sink profile =====
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "[5/5] Initializing A2DP Sink profile");
    a2dp_sink_init();
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, " ✓ A2DP Sink profile initialized");

    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "========================================");
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "✓ Component initialized successfully!");
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "========================================");

    // ===== Set device as discoverable and connectable =====
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "Setting device as discoverable and connectable...");
    ret = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    if (ret != ESP_OK) {
        ESP_LOGE(A2DP_SINK_HFP_HF_TAG, "Failed to set scan mode: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "✓ Device is now discoverable and connectable");

    s_component_initialized = true;
    return ESP_OK;

err_cleanup:
    ESP_LOGE(A2DP_SINK_HFP_HF_TAG, "Component initialization failed, cleaning up");
    a2dpSinkHfpHf_deinit();
    return ret;
}

/**
 * @brief Deinitialize all BT subsystems
 * 
 * Reverses initialization in opposite order:
 * 1. A2DP Sink profile
 * 2. HFP Hands-Free profile
 * 3. GAP layer
 * 4. Audio codec
 * 5. I2S interface
 */
esp_err_t a2dpSinkHfpHf_deinit(void)
{
    if (!s_component_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "========================================");
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "Deinitializing A2DP Sink + HFP Hands-Free");
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "========================================");

    // Deinitialize A2DP Sink
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "Deinitializing A2DP Sink");
    a2dp_sink_deinit();

    // Deinitialize HFP
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "Deinitializing HFP");
    esp_hf_client_deinit();

    // Deinitialize GAP
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "Deinitializing GAP");
    bt_gap_deinit();

    // Close codec
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "Closing codec");
    msbc_enc_close();
    msbc_dec_close();

    // Deinitialize I2S
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "Deinitializing I2S");
    bt_i2s_driver_uninstall();

    s_component_initialized = false;

    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "========================================");
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "✓ Component deinitialized");
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "========================================");

    return ESP_OK;
}

// ============================================================================
// PUBLIC API FUNCTIONS
// ============================================================================

esp_err_t a2dpSinkHfpHf_start_discovery(void)
{
    if (!s_component_initialized) {
        ESP_LOGE(A2DP_SINK_HFP_HF_TAG, "Component not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    return bt_gap_start_discovery();
}

esp_err_t a2dpSinkHfpHf_cancel_discovery(void)
{
    if (!s_component_initialized) {
        ESP_LOGE(A2DP_SINK_HFP_HF_TAG, "Component not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    return bt_gap_cancel_discovery();
}

const char *a2dpSinkHfpHf_get_device_name(void)
{
    return s_current_config.device_name;
}

bool a2dpSinkHfpHf_is_connected(void)
{
    // Device is connected if either HFP or A2DP is connected
    return a2dp_sink_is_connected();
}

/**
 * @brief Set the country code for phonebook international number formatting
 * Must be called BEFORE a2dpSinkHfpHf_init()
 * 
 * @param country_code Two or three digit country code (e.g., "31" for Netherlands)
 * @return ESP_OK on success
 */
esp_err_t a2dpSinkHfpHf_set_country_code(const char *country_code)
{
    if (s_component_initialized) {
        ESP_LOGW(A2DP_SINK_HFP_HF_TAG, "Cannot change country code after initialization");
        return ESP_ERR_INVALID_STATE;
    }

    if (country_code == NULL || strlen(country_code) > 3) {
        ESP_LOGE(A2DP_SINK_HFP_HF_TAG, "Invalid country code");
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(s_country_code, country_code, sizeof(s_country_code) - 1);
    s_country_code[sizeof(s_country_code) - 1] = '\0';

    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "Country code set to: %s", s_country_code);
    return ESP_OK;
}

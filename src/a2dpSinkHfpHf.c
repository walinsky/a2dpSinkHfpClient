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
#include "sdkconfig.h"

#include "a2dpSinkHfpHf.h"
#include "a2dpSink.h"
#include "bt_app_avrc.h"
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
 * @brief Set custom configuration (optional)
 * This function allows runtime configuration override.
 * Must be called BEFORE a2dpSinkHfpHf_init()
 * 
 * @param config Pointer to configuration structure
 * @return ESP_OK on success
 */
esp_err_t a2dpSinkHfpHf_config(const a2dpSinkHfpHf_config_t *config)
{
    if (s_component_initialized) {
        ESP_LOGE(A2DP_SINK_HFP_HF_TAG, "Cannot configure after initialization");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!config) {
        ESP_LOGE(A2DP_SINK_HFP_HF_TAG, "Config is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Store the custom configuration for use during init
    memcpy(&s_current_config, config, sizeof(a2dpSinkHfpHf_config_t));
    
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "Custom configuration stored");
    return ESP_OK;
}

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
    if (s_component_initialized) {
        ESP_LOGW(A2DP_SINK_HFP_HF_TAG, "Component already initialized");
        return ESP_OK;
    }

    // Create default config from Kconfig if not provided
    a2dpSinkHfpHf_config_t default_config;
    if (!config) {
        default_config = (a2dpSinkHfpHf_config_t){
            .device_name = CONFIG_A2DPSINK_HFPHF_DEVICE_NAME,
            .i2s_tx_bck = CONFIG_A2DPSINK_HFPHF_I2S_TX_BCK,
            .i2s_tx_ws = CONFIG_A2DPSINK_HFPHF_I2S_TX_WS,
            .i2s_tx_dout = CONFIG_A2DPSINK_HFPHF_I2S_TX_DOUT,
            .i2s_rx_bck = CONFIG_A2DPSINK_HFPHF_I2S_RX_BCK,
            .i2s_rx_ws = CONFIG_A2DPSINK_HFPHF_I2S_RX_WS,
            .i2s_rx_din = CONFIG_A2DPSINK_HFPHF_I2S_RX_DIN
        };
        config = &default_config;
        
        // Also apply Kconfig PIN and country code
        bt_gap_set_pin(CONFIG_A2DPSINK_HFPHF_PIN_CODE, strlen(CONFIG_A2DPSINK_HFPHF_PIN_CODE));
        strncpy(s_country_code, CONFIG_A2DPSINK_HFPHF_COUNTRY_CODE, sizeof(s_country_code) - 1);
        s_country_code[sizeof(s_country_code) - 1] = '\0';
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

    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "  ✓ BT Controller initialized");

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

    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "  ✓ Bluedroid initialized (SSP disabled)");

    // ===== STEP 1: Initialize I2S interface =====
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "[1/5] Initializing I2S interface");
    bt_i2s_set_tx_I2S_pins(config->i2s_tx_bck, config->i2s_tx_ws, config->i2s_tx_dout, 0);
    bt_i2s_set_rx_I2S_pins(config->i2s_rx_bck, config->i2s_rx_ws, 0, config->i2s_rx_din);
    bt_i2s_init();
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "  ✓ I2S interface initialized");

    // ===== STEP 2: Initialize audio codec =====
    /* ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "[2/5] Initializing audio codec (mSBC)");
    msbc_enc_open();
    msbc_dec_open();
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "  ✓ Audio codec initialized"); */

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

    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "  ✓ GAP layer initialized");

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
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "  ✓ HFP Hands-Free profile initialized");

    // ===== STEP 5: Initialize A2DP Sink profile =====
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "[5/5] Initializing A2DP Sink profile");
    
    // Initialize AVRCP before A2DP
    bt_app_avrc_init();
    a2dp_sink_init();
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "  ✓ A2DP Sink profile initialized");

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
    bt_app_avrc_deinit();

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
// GAP API
// ============================================================================

esp_err_t a2dpSinkHfpHf_register_gap_callback(bt_gap_event_cb_t callback)
{
    if (!s_component_initialized) {
        ESP_LOGE(A2DP_SINK_HFP_HF_TAG, "Component not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    return bt_gap_register_event_callback(callback);
}

esp_err_t a2dpSinkHfpHf_unregister_gap_callback(bt_gap_event_cb_t callback)
{
    if (!s_component_initialized) {
        ESP_LOGE(A2DP_SINK_HFP_HF_TAG, "Component not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    return bt_gap_unregister_event_callback(callback);
}

// ============================================================================
// PHONEBOOK API WRAPPERS
// ============================================================================

a2dpSinkHfpHf_phonebook_handle_t a2dpSinkHfpHf_get_phonebook(void)
{
    if (!s_component_initialized) {
        return NULL;
    }
    return (a2dpSinkHfpHf_phonebook_handle_t)bt_app_pbac_get_current_phonebook();
}

uint16_t a2dpSinkHfpHf_phonebook_get_count(a2dpSinkHfpHf_phonebook_handle_t pb)
{
    if (!pb) {
        return 0;
    }
    return phonebook_get_count((phonebook_t*)pb);
}

a2dpSinkHfpHf_contact_t* a2dpSinkHfpHf_phonebook_search_by_letter(
    a2dpSinkHfpHf_phonebook_handle_t pb,
    char letter,
    uint16_t *count)
{
    if (!pb || !count) {
        return NULL;
    }
    
    // Cast internal types to public types (they're binary compatible)
    contact_t *internal_contacts = phonebook_search_by_letter((phonebook_t*)pb, letter, count);
    return (a2dpSinkHfpHf_contact_t*)internal_contacts;
}

a2dpSinkHfpHf_contact_t* a2dpSinkHfpHf_phonebook_search_by_name(
    a2dpSinkHfpHf_phonebook_handle_t pb,
    const char *name,
    uint16_t *count)
{
    if (!pb || !name || !count) {
        return NULL;
    }
    
    contact_t *internal_contacts = phonebook_search_by_name((phonebook_t*)pb, name, count);
    return (a2dpSinkHfpHf_contact_t*)internal_contacts;
}

a2dpSinkHfpHf_contact_t* a2dpSinkHfpHf_phonebook_search_by_number(
    a2dpSinkHfpHf_phonebook_handle_t pb,
    const char *number)
{
    if (!pb || !number) {
        return NULL;
    }
    
    contact_t *internal_contact = phonebook_search_by_number((phonebook_t*)pb, number);
    return (a2dpSinkHfpHf_contact_t*)internal_contact;
}

a2dpSinkHfpHf_phone_number_t* a2dpSinkHfpHf_phonebook_get_numbers(
    a2dpSinkHfpHf_phonebook_handle_t pb,
    const char *full_name,
    uint8_t *count)
{
    if (!pb || !full_name || !count) {
        return NULL;
    }
    
    phone_number_t *internal_numbers = phonebook_get_numbers((phonebook_t*)pb, full_name, count);
    return (a2dpSinkHfpHf_phone_number_t*)internal_numbers;
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

// ============================================================================
// PIN CODE API (Wrapper to bt_gap.c)
// ============================================================================

/**
 * @brief Set Bluetooth pairing PIN code
 * Must be called BEFORE a2dpSinkHfpHf_init() to take effect
 */
esp_err_t a2dpSinkHfpHf_set_pin(const char *pin_code, uint8_t pin_len)
{
    if (s_component_initialized) {
        ESP_LOGW(A2DP_SINK_HFP_HF_TAG, "Cannot change PIN after initialization");
        return ESP_ERR_INVALID_STATE;
    }
    
    return bt_gap_set_pin(pin_code, pin_len);
}

/**
 * @brief Get current PIN code configuration
 */
esp_err_t a2dpSinkHfpHf_get_pin(char *pin_code, uint8_t *pin_len)
{
    return bt_gap_get_pin(pin_code, pin_len);
}

// ============================================================================
// AVRC PUBLIC API (Wrappers to bt_app_avrc.c)
// ============================================================================

esp_err_t a2dpSinkHfpHf_set_avrc_metadata_mask(uint8_t attr_mask)
{
    if (s_component_initialized) {
        ESP_LOGE(A2DP_SINK_HFP_HF_TAG, "Cannot change AVRC metadata mask after initialization");
        return ESP_ERR_INVALID_STATE;
    }

    if (!bt_app_avrc_set_metadata_mask(attr_mask)) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

void a2dpSinkHfpHf_register_avrc_conn_callback(bt_avrc_conn_state_cb_t callback)
{
    bt_app_avrc_register_conn_callback(callback);
}

void a2dpSinkHfpHf_register_avrc_metadata_callback(bt_avrc_metadata_cb_t callback)
{
    bt_app_avrc_register_metadata_callback(callback);
}

void a2dpSinkHfpHf_register_avrc_playback_callback(bt_avrc_playback_status_cb_t callback)
{
    bt_app_avrc_register_playback_status_callback(callback);
}

void a2dpSinkHfpHf_register_avrc_volume_callback(bt_avrc_volume_cb_t callback)
{
    bt_app_avrc_register_volume_callback(callback);
}

const bt_avrc_metadata_t* a2dpSinkHfpHf_get_avrc_metadata(void)
{
    return bt_app_avrc_get_metadata();
}

bool a2dpSinkHfpHf_is_avrc_connected(void)
{
    return bt_app_avrc_is_connected();
}

bool a2dpSinkHfpHf_avrc_play(void)
{
    if (!s_component_initialized) {
        ESP_LOGE(A2DP_SINK_HFP_HF_TAG, "Component not initialized");
        return false;
    }
    
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "AVRC: Play");
    return bt_app_avrc_cmd_play();
}

bool a2dpSinkHfpHf_avrc_pause(void)
{
    if (!s_component_initialized) {
        ESP_LOGE(A2DP_SINK_HFP_HF_TAG, "Component not initialized");
        return false;
    }
    
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "AVRC: Pause");
    return bt_app_avrc_cmd_pause();
}

bool a2dpSinkHfpHf_avrc_next(void)
{
    if (!s_component_initialized) {
        ESP_LOGE(A2DP_SINK_HFP_HF_TAG, "Component not initialized");
        return false;
    }
    
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "AVRC: Next track");
    return bt_app_avrc_cmd_next();
}

bool a2dpSinkHfpHf_avrc_prev(void)
{
    if (!s_component_initialized) {
        ESP_LOGE(A2DP_SINK_HFP_HF_TAG, "Component not initialized");
        return false;
    }
    
    ESP_LOGI(A2DP_SINK_HFP_HF_TAG, "AVRC: Previous track");
    return bt_app_avrc_cmd_prev();
}


// ============================================================================
// HFP HANDS-FREE CONTROL FUNCTIONS
// ============================================================================

esp_err_t a2dpSinkHfpHf_answer_call(void) {
    return esp_hf_client_answer_call();
}

esp_err_t a2dpSinkHfpHf_reject_call(void) {
    return esp_hf_client_reject_call();
}

esp_err_t a2dpSinkHfpHf_hangup_call(void) {
    return esp_hf_client_reject_call();  // Same as reject
}

esp_err_t a2dpSinkHfpHf_dial_number(const char *number) {
    if (number == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_hf_client_dial(number);
}

esp_err_t a2dpSinkHfpHf_redial(void) {
    return esp_hf_client_dial(NULL);  // NULL triggers redial
}

esp_err_t a2dpSinkHfpHf_dial_memory(int location) {
    return esp_hf_client_dial_memory(location);
}

// ============================================================================
// Voice Recognition
// ============================================================================

esp_err_t a2dpSinkHfpHf_start_voice_recognition(void) {
    return esp_hf_client_start_voice_recognition();
}

esp_err_t a2dpSinkHfpHf_stop_voice_recognition(void) {
    return esp_hf_client_stop_voice_recognition();
}

// ============================================================================
// Volume Control
// ============================================================================

esp_err_t a2dpSinkHfpHf_volume_update(const char *target, int volume) {
    if (target == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (volume < 0 || volume > 15) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_hf_volume_control_target_t vol_target;
    if (strcmp(target, "spk") == 0) {
        vol_target = ESP_HF_VOLUME_CONTROL_TARGET_SPK;
    } else if (strcmp(target, "mic") == 0) {
        vol_target = ESP_HF_VOLUME_CONTROL_TARGET_MIC;
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    return esp_hf_client_volume_update(vol_target, volume);
}

// ============================================================================
// Query Functions
// ============================================================================

esp_err_t a2dpSinkHfpHf_query_operator(void) {
    return esp_hf_client_query_current_operator_name();
}

esp_err_t a2dpSinkHfpHf_query_current_calls(void) {
    return esp_hf_client_query_current_calls();
}

esp_err_t a2dpSinkHfpHf_retrieve_subscriber_info(void) {
    return esp_hf_client_retrieve_subscriber_info();
}

// ============================================================================
// Advanced Features
// ============================================================================

esp_err_t a2dpSinkHfpHf_send_btrh(int btrh) {
    if (btrh < 0 || btrh > 2) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_hf_client_send_btrh_cmd((esp_hf_btrh_cmd_t)btrh);
}

esp_err_t a2dpSinkHfpHf_send_xapl(const char *features) {
    if (features == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Parse features string format: "ProductName-Version,Features"
    // e.g., "iPhone-6.3.0,2"
    // For now, we'll assume caller provides proper format

    // The API signature is: esp_err_t esp_hf_client_send_xapl(char *information, uint32_t features)
    // We need to split the features string

    char info_copy[64];
    strncpy(info_copy, features, sizeof(info_copy) - 1);
    info_copy[sizeof(info_copy) - 1] = '\0';

    // Find comma separator
    char *comma = strchr(info_copy, ',');
    if (comma == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *comma = '\0';  // Split string
    char *info_part = info_copy;
    uint32_t features_part = (uint32_t)atoi(comma + 1);

    return esp_hf_client_send_xapl(info_part, features_part);
}

esp_err_t a2dpSinkHfpHf_send_iphoneaccev(int bat_level, int docked) {
    // Validate parameters
    if (bat_level < -1 || bat_level > 9) {
        return ESP_ERR_INVALID_ARG;
    }
    if (docked < -1 || docked > 1) {
        return ESP_ERR_INVALID_ARG;
    }

    // At least one parameter must be valid
    if (bat_level < 0 && docked < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // API signature: esp_err_t esp_hf_client_send_iphoneaccev(uint32_t bat_level, bool docked)
    // If bat_level is -1, send 0 (it won't be used if we only send docked state)
    uint32_t battery = (bat_level >= 0) ? (uint32_t)bat_level : 0;
    bool is_docked = (docked > 0);

    return esp_hf_client_send_iphoneaccev(battery, is_docked);
}

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_gap_bt_api.h"
#include "esp_bt_device.h"
#include "bt_gap.h"
#include "esp_hf_client_api.h"

#define BT_GAP_TAG "BT_GAP"
#define MAX_EVENT_CALLBACKS 5

/* Global peer address - populated when target device is found */
esp_bd_addr_t peer_addr = {0};

/* Cached device name */
static char s_device_name[250] = {0};

/* Target remote device name to search for */
static const char *remote_device_name = "ESP_HF_SERVER";

static char peer_bdname[ESP_BT_GAP_MAX_BDNAME_LEN + 1] = {0};
static uint8_t peer_bdname_len = 0;

/* Configurable PIN code for pairing */
static char s_pin_code[ESP_BT_PIN_CODE_LEN] = {'1', '2', '3', '4'}; // Default PIN
static uint8_t s_pin_len = 4; // Default PIN length

// Callback registry
static bt_gap_event_cb_t s_event_callbacks[MAX_EVENT_CALLBACKS] = {NULL};
static uint8_t s_callback_count = 0;

/**
 * @brief Convert Bluetooth address to string format
 * @param bda Bluetooth device address (6 bytes)
 * @param str Output buffer for string
 * @param size Size of output buffer (minimum 18 bytes)
 * @return Pointer to string or NULL on error
 */
static char *bda2str(const esp_bd_addr_t bda, char *str, size_t size)
{
    if (bda == NULL || str == NULL || size < 18) {
        return NULL;
    }
    const uint8_t *p = bda;
    snprintf(str, size, "%02x:%02x:%02x:%02x:%02x:%02x",
             p[0], p[1], p[2], p[3], p[4], p[5]);
    return str;
}

/**
 * @brief Extract device name from EIR (Extended Inquiry Response) data
 * @param eir EIR data buffer
 * @param bdname Output buffer for device name
 * @param bdname_len Pointer to store name length
 * @return true if name found, false otherwise
 */
static bool get_name_from_eir(uint8_t *eir, char *bdname, uint8_t *bdname_len)
{
    uint8_t *rmt_bdname = NULL;
    uint8_t rmt_bdname_len = 0;

    if (!eir) {
        return false;
    }

    /* Try to get complete local name first */
    rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &rmt_bdname_len);
    if (!rmt_bdname) {
        /* Fall back to shortened local name */
        rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &rmt_bdname_len);
    }

    if (rmt_bdname) {
        if (rmt_bdname_len > ESP_BT_GAP_MAX_BDNAME_LEN) {
            rmt_bdname_len = ESP_BT_GAP_MAX_BDNAME_LEN;
        }

        if (bdname) {
            memcpy(bdname, rmt_bdname, rmt_bdname_len);
            bdname[rmt_bdname_len] = '\0';
        }

        if (bdname_len) {
            *bdname_len = rmt_bdname_len;
        }

        return true;
    }

    return false;
}

// ============================================================================
// CALLBACK MANAGEMENT
// ============================================================================

esp_err_t bt_gap_register_event_callback(bt_gap_event_cb_t callback)
{
    if (callback == NULL) {
        ESP_LOGE(BT_GAP_TAG, "Callback cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Check if already registered
    for (int i = 0; i < s_callback_count; i++) {
        if (s_event_callbacks[i] == callback) {
            ESP_LOGW(BT_GAP_TAG, "Callback already registered");
            return ESP_OK;
        }
    }
    
    // Check if we have space
    if (s_callback_count >= MAX_EVENT_CALLBACKS) {
        ESP_LOGE(BT_GAP_TAG, "Maximum number of callbacks reached (%d)", MAX_EVENT_CALLBACKS);
        return ESP_ERR_NO_MEM;
    }
    
    // Register callback
    s_event_callbacks[s_callback_count] = callback;
    s_callback_count++;
    
    ESP_LOGI(BT_GAP_TAG, "Event callback registered (total: %d)", s_callback_count);
    
    return ESP_OK;
}

esp_err_t bt_gap_unregister_event_callback(bt_gap_event_cb_t callback)
{
    if (callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Find and remove callback
    for (int i = 0; i < s_callback_count; i++) {
        if (s_event_callbacks[i] == callback) {
            // Shift remaining callbacks down
            for (int j = i; j < s_callback_count - 1; j++) {
                s_event_callbacks[j] = s_event_callbacks[j + 1];
            }
            s_event_callbacks[s_callback_count - 1] = NULL;
            s_callback_count--;
            
            ESP_LOGI(BT_GAP_TAG, "Event callback unregistered (remaining: %d)", s_callback_count);
            return ESP_OK;
        }
    }
    
    ESP_LOGW(BT_GAP_TAG, "Callback not found in registry");
    return ESP_ERR_NOT_FOUND;
}

// Helper function to dispatch events to all registered callbacks
static void dispatch_gap_event(bt_gap_event_type_t event_type, bt_gap_event_data_t *data)
{
    for (int i = 0; i < s_callback_count; i++) {
        if (s_event_callbacks[i]) {
            s_event_callbacks[i](event_type, data);
        }
    }
}

// ============================================================================
// GAP CALLBACK - dispatches to subscribers
// ============================================================================

void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    char bda_str[18] = {0};
    
    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT: {
            /* Device discovery result */
            for (int i = 0; i < param->disc_res.num_prop; i++) {
                if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_EIR &&
                    get_name_from_eir(param->disc_res.prop[i].val, peer_bdname, &peer_bdname_len)) {
                    
                    ESP_LOGI(BT_GAP_TAG, "Found device: %s [%s]", 
                            peer_bdname, bda2str(param->disc_res.bda, bda_str, sizeof(bda_str)));
                    
                    // Dispatch discovery event
                    bt_gap_event_data_t evt_data = {0};
                    memcpy(evt_data.discovery.bda, param->disc_res.bda, ESP_BD_ADDR_LEN);
                    strncpy(evt_data.discovery.name, peer_bdname, sizeof(evt_data.discovery.name) - 1);
                    dispatch_gap_event(BT_GAP_EVT_DEVICE_DISCOVERED, &evt_data);
                    
                    /* Check if this is the target device */
                    if (strcmp(peer_bdname, remote_device_name) == 0) {
                        memcpy(peer_addr, param->disc_res.bda, ESP_BD_ADDR_LEN);
                        ESP_LOGI(BT_GAP_TAG, "Found target device: %s", peer_bdname);
                        esp_bt_gap_cancel_discovery();
                    }
                    
                    break;
                }
            }
            break;
        }
        
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
            if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
                ESP_LOGI(BT_GAP_TAG, "Discovery started");
            } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
                ESP_LOGI(BT_GAP_TAG, "Discovery stopped");
            }
            break;
            
        case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT: {
            // ACL Connection Complete - NEW EVENT HANDLING
            bda2str(param->acl_conn_cmpl_stat.bda, bda_str, sizeof(bda_str));
            
            if (param->acl_conn_cmpl_stat.stat == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(BT_GAP_TAG, "ACL connection complete: %s", bda_str);
                
                // Store peer address
                memcpy(peer_addr, param->acl_conn_cmpl_stat.bda, ESP_BD_ADDR_LEN);
                
                // Dispatch connection event to subscribers
                bt_gap_event_data_t evt_data = {0};
                memcpy(evt_data.connection.bda, param->acl_conn_cmpl_stat.bda, ESP_BD_ADDR_LEN);
                evt_data.connection.success = true;
                dispatch_gap_event(BT_GAP_EVT_DEVICE_CONNECTED, &evt_data);
            } else {
                ESP_LOGE(BT_GAP_TAG, "ACL connection failed: %s (status: 0x%x)", 
                        bda_str, param->acl_conn_cmpl_stat.stat);
                
                // Dispatch failed connection event
                bt_gap_event_data_t evt_data = {0};
                memcpy(evt_data.connection.bda, param->acl_conn_cmpl_stat.bda, ESP_BD_ADDR_LEN);
                evt_data.connection.success = false;
                dispatch_gap_event(BT_GAP_EVT_DEVICE_CONNECTED, &evt_data);
            }
            break;
        }
        
        case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT: {
            // ACL Disconnection Complete - NEW EVENT HANDLING
            bda2str(param->acl_disconn_cmpl_stat.bda, bda_str, sizeof(bda_str));
            ESP_LOGI(BT_GAP_TAG, "ACL disconnection complete: %s (reason: 0x%x)", 
                    bda_str, param->acl_disconn_cmpl_stat.reason);
            
            // Clear peer address if it matches
            if (memcmp(peer_addr, param->acl_disconn_cmpl_stat.bda, ESP_BD_ADDR_LEN) == 0) {
                memset(peer_addr, 0, ESP_BD_ADDR_LEN);
            }
            
            // Dispatch disconnection event to subscribers
            bt_gap_event_data_t evt_data = {0};
            memcpy(evt_data.disconnection.bda, param->acl_disconn_cmpl_stat.bda, ESP_BD_ADDR_LEN);
            dispatch_gap_event(BT_GAP_EVT_DEVICE_DISCONNECTED, &evt_data);
            break;
        }
        
        case ESP_BT_GAP_RMT_SRVC_REC_EVT:
            ESP_LOGI(BT_GAP_TAG, "Remote services resolved");
            break;
            
        case ESP_BT_GAP_AUTH_CMPL_EVT: {
            bda2str(param->auth_cmpl.bda, bda_str, sizeof(bda_str));
            
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(BT_GAP_TAG, "Authentication success: %s", bda_str);
                
                // Store peer address
                memcpy(peer_addr, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
                
                // Dispatch auth success event
                bt_gap_event_data_t evt_data = {0};
                memcpy(evt_data.auth.bda, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
                evt_data.auth.success = true;
                dispatch_gap_event(BT_GAP_EVT_AUTH_COMPLETE, &evt_data);
                
                // Auto-connect HFP after successful authentication
                ESP_LOGI(BT_GAP_TAG, "Initiating HFP connection to: %s", bda_str);
                esp_hf_client_connect(param->auth_cmpl.bda);
            } else {
                ESP_LOGE(BT_GAP_TAG, "Authentication failed: %s (status: 0x%x)", 
                        bda_str, param->auth_cmpl.stat);
                
                // Dispatch auth failure event
                bt_gap_event_data_t evt_data = {0};
                memcpy(evt_data.auth.bda, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
                evt_data.auth.success = false;
                dispatch_gap_event(BT_GAP_EVT_AUTH_COMPLETE, &evt_data);
            }
            break;
        }
        
        case ESP_BT_GAP_PIN_REQ_EVT: {
            ESP_LOGI(BT_GAP_TAG, "PIN request (min_16_digit: %d)", param->pin_req.min_16_digit);
            esp_bt_pin_code_t pin_code;
            
            if (param->pin_req.min_16_digit) {
                ESP_LOGI(BT_GAP_TAG, "Input pin code: 0000 0000 0000 0000");
                memset(pin_code, '0', 16);
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin_code);
            } else {
                memcpy(pin_code, s_pin_code, s_pin_len);
                char pin_str[17] = {0};
                memcpy(pin_str, s_pin_code, s_pin_len);
                pin_str[s_pin_len] = '\0';
                ESP_LOGI(BT_GAP_TAG, "Input pin code: %s", pin_str);
                esp_bt_gap_pin_reply(param->pin_req.bda, true, s_pin_len, pin_code);
            }
            break;
        }
        
        case ESP_BT_GAP_CFM_REQ_EVT:
            ESP_LOGI(BT_GAP_TAG, "SSP Confirmation request");
            esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
            break;
            
        case ESP_BT_GAP_MODE_CHG_EVT: {
            bda2str(param->mode_chg.bda, bda_str, sizeof(bda_str));
            ESP_LOGI(BT_GAP_TAG, "Mode change: %s, mode: %d", bda_str, param->mode_chg.mode);
            
            // Dispatch mode change event
            bt_gap_event_data_t evt_data = {0};
            memcpy(evt_data.mode_change.bda, param->mode_chg.bda, ESP_BD_ADDR_LEN);
            evt_data.mode_change.mode = param->mode_chg.mode;
            dispatch_gap_event(BT_GAP_EVT_MODE_CHANGE, &evt_data);
            break;
        }
        
        case ESP_BT_GAP_GET_DEV_NAME_CMPL_EVT:
            if (param->get_dev_name_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(BT_GAP_TAG, "Get device name complete");
            } else {
                ESP_LOGW(BT_GAP_TAG, "Failed to retrieve device name");
            }
            break;
            
        default:
            ESP_LOGD(BT_GAP_TAG, "Unhandled GAP event: %d", event);
            break;
    }
}

/**
 * @brief Initialize Bluetooth GAP
 */
esp_err_t bt_gap_init(void)
{
    esp_err_t ret = esp_bt_gap_register_callback(bt_app_gap_cb);
    if (ret == ESP_OK) {
        ESP_LOGI(BT_GAP_TAG, "GAP callback registered successfully");
    } else {
        ESP_LOGE(BT_GAP_TAG, "Failed to register GAP callback: %d", ret);
    }

    return ret;
}

/**
 * @brief Deinitialize Bluetooth GAP
 */
esp_err_t bt_gap_deinit(void)
{
    esp_err_t ret = esp_bt_gap_register_callback(NULL);
    if (ret == ESP_OK) {
        ESP_LOGI(BT_GAP_TAG, "GAP callback unregistered");
    }

    return ret;
}

/**
 * @brief Set the local Bluetooth device name
 */
esp_err_t bt_gap_set_device_name(const char *name)
{
    if (name == NULL) {
        ESP_LOGE(BT_GAP_TAG, "Device name cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    /* Cache the name locally */
    strncpy(s_device_name, name, sizeof(s_device_name) - 1);
    s_device_name[sizeof(s_device_name) - 1] = '\0';

    /* Set name in Bluetooth stack */
    esp_err_t ret = esp_bt_gap_set_device_name(name);
    if (ret == ESP_OK) {
        ESP_LOGI(BT_GAP_TAG, "Device name set to: %s", name);
    } else {
        ESP_LOGE(BT_GAP_TAG, "Failed to set device name: %d", ret);
    }

    return ret;
}

/**
 * @brief Get the local Bluetooth device name
 */
const char *bt_gap_get_device_name(void)
{
    /* Trigger async retrieval */
    esp_bt_gap_get_device_name();

    /* Return cached name or "Unknown" */
    return s_device_name[0] != 0 ? s_device_name : "Unknown";
}

/**
 * @brief Get the local Bluetooth device address
 */
const uint8_t *bt_gap_get_local_bd_addr(void)
{
    const uint8_t *addr = esp_bt_dev_get_address();
    if (addr) {
        char bda_str[18] = {0};
        bda2str(addr, bda_str, sizeof(bda_str));
        ESP_LOGD(BT_GAP_TAG, "Local BD Address: %s", bda_str);
    }

    return addr;
}

/**
 * @brief Start Bluetooth device discovery
 */
esp_err_t bt_gap_start_discovery(void)
{
    ESP_LOGI(BT_GAP_TAG, "Starting device discovery...");

    /* Start general inquiry for 10 seconds, max 10 results */
    esp_err_t ret = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 10);
    if (ret == ESP_OK) {
        ESP_LOGI(BT_GAP_TAG, "Device discovery started");
    } else {
        ESP_LOGE(BT_GAP_TAG, "Failed to start device discovery: %d", ret);
    }

    return ret;
}

/**
 * @brief Cancel ongoing Bluetooth device discovery
 */
esp_err_t bt_gap_cancel_discovery(void)
{
    esp_err_t ret = esp_bt_gap_cancel_discovery();
    if (ret == ESP_OK) {
        ESP_LOGI(BT_GAP_TAG, "Device discovery cancelled");
    } else {
        ESP_LOGE(BT_GAP_TAG, "Failed to cancel device discovery: %d", ret);
    }

    return ret;
}

/**
 * @brief Set the PIN code for Bluetooth pairing
 */
esp_err_t bt_gap_set_pin(const char *pin_code, uint8_t pin_len)
{
    if (pin_code == NULL) {
        ESP_LOGE(BT_GAP_TAG, "PIN code cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (pin_len < 4 || pin_len > 16) {
        ESP_LOGE(BT_GAP_TAG, "PIN length must be 4-16 digits (got %d)", pin_len);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Validate that PIN contains only digits
    for (uint8_t i = 0; i < pin_len; i++) {
        if (pin_code[i] < '0' || pin_code[i] > '9') {
            ESP_LOGE(BT_GAP_TAG, "PIN must contain only digits (0-9)");
            return ESP_ERR_INVALID_ARG;
        }
    }
    
    // Store PIN code
    memcpy(s_pin_code, pin_code, pin_len);
    s_pin_len = pin_len;
    
    ESP_LOGI(BT_GAP_TAG, "PIN code set (length: %d)", pin_len);
    return ESP_OK;
}

/**
 * @brief Get the current PIN code
 */
esp_err_t bt_gap_get_pin(char *pin_code, uint8_t *pin_len)
{
    if (pin_code == NULL || pin_len == NULL) {
        ESP_LOGE(BT_GAP_TAG, "Output buffers cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(pin_code, s_pin_code, s_pin_len);
    pin_code[s_pin_len] = '\0'; // Null-terminate
    *pin_len = s_pin_len;
    
    return ESP_OK;
}

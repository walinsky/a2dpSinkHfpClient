#ifndef __BT_GAP_H__
#define __BT_GAP_H__

#include "esp_err.h"
#include "esp_gap_bt_api.h"
#include "esp_bt_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

// GAP event types that can be subscribed to
typedef enum {
    BT_GAP_EVT_DEVICE_CONNECTED,      // Device ACL connected
    BT_GAP_EVT_DEVICE_DISCONNECTED,   // Device ACL disconnected
    BT_GAP_EVT_DEVICE_DISCOVERED,     // Device found during discovery
    BT_GAP_EVT_AUTH_COMPLETE,         // Authentication completed
    BT_GAP_EVT_MODE_CHANGE            // Power mode changed
} bt_gap_event_type_t;

// Event data structures
typedef struct {
    esp_bd_addr_t bda;
    bool success;
} bt_gap_connection_evt_t;

typedef struct {
    esp_bd_addr_t bda;
} bt_gap_disconnection_evt_t;

typedef struct {
    esp_bd_addr_t bda;
    char name[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
} bt_gap_discovery_evt_t;

typedef struct {
    esp_bd_addr_t bda;
    bool success;
} bt_gap_auth_evt_t;

typedef struct {
    esp_bd_addr_t bda;
    esp_bt_pm_mode_t mode;
} bt_gap_mode_change_evt_t;

// Union of all event data
typedef union {
    bt_gap_connection_evt_t connection;
    bt_gap_disconnection_evt_t disconnection;
    bt_gap_discovery_evt_t discovery;
    bt_gap_auth_evt_t auth;
    bt_gap_mode_change_evt_t mode_change;
} bt_gap_event_data_t;

// GAP event callback type
typedef void (*bt_gap_event_cb_t)(bt_gap_event_type_t event, bt_gap_event_data_t *data);

/**
 * @brief Initialize Bluetooth GAP
 */
esp_err_t bt_gap_init(void);

/**
 * @brief Deinitialize Bluetooth GAP
 */
esp_err_t bt_gap_deinit(void);

/**
 * @brief Register a callback for GAP events
 * Multiple callbacks can be registered
 * @param callback Callback function to register
 * @return ESP_OK on success
 */
esp_err_t bt_gap_register_event_callback(bt_gap_event_cb_t callback);

/**
 * @brief Unregister a GAP event callback
 * @param callback Callback function to unregister
 * @return ESP_OK on success
 */
esp_err_t bt_gap_unregister_event_callback(bt_gap_event_cb_t callback);

/**
 * @brief Set the local Bluetooth device name
 */
esp_err_t bt_gap_set_device_name(const char *name);

/**
 * @brief Get the local Bluetooth device name
 */
const char *bt_gap_get_device_name(void);

/**
 * @brief Get the local Bluetooth device address
 */
const uint8_t *bt_gap_get_local_bd_addr(void);

/**
 * @brief Start Bluetooth device discovery
 */
esp_err_t bt_gap_start_discovery(void);

/**
 * @brief Cancel ongoing Bluetooth device discovery
 */
esp_err_t bt_gap_cancel_discovery(void);

/**
 * @brief Set the PIN code for Bluetooth pairing
 */
esp_err_t bt_gap_set_pin(const char *pin_code, uint8_t pin_len);

/**
 * @brief Get the current PIN code
 */
esp_err_t bt_gap_get_pin(char *pin_code, uint8_t *pin_len);

#ifdef __cplusplus
}
#endif

#endif // __BT_GAP_H__

/**
 * @file bt_app_avrc.h
 * @brief AVRCP Controller and Target implementation for ESP32 Bluetooth Audio Receiver
 * 
 * Provides Audio/Video Remote Control Profile functionality including:
 * - Track metadata retrieval (title, artist, album)
 * - Playback control commands (play, pause, next, prev)
 * - Playback status monitoring
 * - Volume control
 * - Event notifications
 */

#ifndef BT_APP_AVRC_H
#define BT_APP_AVRC_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_avrc_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum length for metadata text strings
 */
#define BT_AVRC_META_TEXT_MAX_LEN   256

/**
 * @brief Transaction label counter (0-15)
 */
#define BT_AVRC_TRANS_LABEL_MAX     15

/**
 * @brief Track metadata structure
 */
typedef struct {
    char title[BT_AVRC_META_TEXT_MAX_LEN];
    char artist[BT_AVRC_META_TEXT_MAX_LEN];
    char album[BT_AVRC_META_TEXT_MAX_LEN];
    char genre[BT_AVRC_META_TEXT_MAX_LEN];
    uint32_t track_num;
    uint32_t total_tracks;
    uint32_t playing_time_ms;
    bool valid;
} bt_avrc_metadata_t;

/**
 * @brief Playback status information
 */
typedef struct {
    esp_avrc_playback_stat_t status;
    uint32_t song_len_ms;
    uint32_t song_pos_ms;
} bt_avrc_playback_status_t;

/**
 * @brief AVRCP connection state
 */
typedef enum {
    BT_AVRC_STATE_DISCONNECTED = 0,
    BT_AVRC_STATE_CONNECTING,
    BT_AVRC_STATE_CONNECTED,
    BT_AVRC_STATE_DISCONNECTING
} bt_avrc_conn_state_t;

/**
 * @brief Set custom AVRC metadata attribute mask (optional)
 * This overrides the Kconfig default. Must be called before bt_app_avrc_init()
 * 
 * @param attr_mask Bitmask of ESP_AVRC_MD_ATTR_* values
 * @return true on success, false if already initialized
 */
bool bt_app_avrc_set_metadata_mask(uint8_t attr_mask);

/**
 * @brief Initialize AVRCP controller and target
 * 
 * Must be called after esp_bluedroid_enable() and before A2DP initialization
 * 
 * @return true on success, false on failure
 */
bool bt_app_avrc_init(void);

/**
 * @brief Deinitialize AVRCP controller and target
 * 
 * Should be called before A2DP deinitialization
 */
void bt_app_avrc_deinit(void);

/**
 * @brief Get current AVRCP connection state
 * 
 * @return Current connection state
 */
bt_avrc_conn_state_t bt_app_avrc_get_connection_state(void);

/**
 * @brief Check if AVRCP is connected
 * 
 * @return true if connected, false otherwise
 */
bool bt_app_avrc_is_connected(void);

/* ============================================
 * Metadata Retrieval
 * ============================================ */

/**
 * @brief Request track metadata from remote device
 * 
 * Metadata will be received asynchronously via callback.
 * Use bt_app_avrc_get_metadata() to retrieve once available.
 * 
 * @return true if request sent successfully
 */
bool bt_app_avrc_request_metadata(void);

/**
 * @brief Get cached track metadata
 * 
 * @return Pointer to metadata structure (read-only), NULL if not available
 */
const bt_avrc_metadata_t* bt_app_avrc_get_metadata(void);

/* ============================================
 * Playback Control Commands
 * ============================================ */

/**
 * @brief Send play command to remote device
 * @return true if command sent successfully
 */
bool bt_app_avrc_cmd_play(void);

/**
 * @brief Send pause command to remote device
 * @return true if command sent successfully
 */
bool bt_app_avrc_cmd_pause(void);

/**
 * @brief Send stop command to remote device
 * @return true if command sent successfully
 */
bool bt_app_avrc_cmd_stop(void);

/**
 * @brief Send next track command to remote device
 * @return true if command sent successfully
 */
bool bt_app_avrc_cmd_next(void);

/**
 * @brief Send previous track command to remote device
 * @return true if command sent successfully
 */
bool bt_app_avrc_cmd_prev(void);

/**
 * @brief Send fast forward command to remote device
 * @return true if command sent successfully
 */
bool bt_app_avrc_cmd_fast_forward(void);

/**
 * @brief Send rewind command to remote device
 * @return true if command sent successfully
 */
bool bt_app_avrc_cmd_rewind(void);

/* ============================================
 * Playback Status
 * ============================================ */

/**
 * @brief Request current playback status from remote device
 * 
 * Status will be received asynchronously.
 * Use bt_app_avrc_get_playback_status() to retrieve.
 * 
 * @return true if request sent successfully
 */
bool bt_app_avrc_request_playback_status(void);

/**
 * @brief Get cached playback status
 * 
 * @return Pointer to playback status structure (read-only), NULL if not available
 */
const bt_avrc_playback_status_t* bt_app_avrc_get_playback_status(void);

/* ============================================
 * Volume Control
 * ============================================ */

/**
 * @brief Set absolute volume on remote device
 * 
 * @param volume Volume level (0-127, where 127 = 100%)
 * @return true if command sent successfully
 */
bool bt_app_avrc_set_volume(uint8_t volume);

/**
 * @brief Get last known volume level
 * 
 * @return Volume level (0-127), or 0xFF if unknown
 */
uint8_t bt_app_avrc_get_volume(void);

/* ============================================
 * Event Callbacks (Optional)
 * ============================================ */

/**
 * @brief Callback function type for connection state changes
 * 
 * @param connected true if connected, false if disconnected
 */
typedef void (*bt_avrc_conn_state_cb_t)(bool connected);

/**
 * @brief Callback function type for metadata updates
 * 
 * @param metadata Pointer to updated metadata structure
 */
typedef void (*bt_avrc_metadata_cb_t)(const bt_avrc_metadata_t *metadata);

/**
 * @brief Callback function type for playback status updates
 * 
 * @param status Pointer to updated playback status structure
 */
typedef void (*bt_avrc_playback_status_cb_t)(const bt_avrc_playback_status_t *status);

/**
 * @brief Callback function type for volume changes
 * 
 * @param volume New volume level (0-127)
 */
typedef void (*bt_avrc_volume_cb_t)(uint8_t volume);

/**
 * @brief Register connection state callback
 * 
 * @param callback Callback function (NULL to unregister)
 */
void bt_app_avrc_register_conn_callback(bt_avrc_conn_state_cb_t callback);

/**
 * @brief Register metadata update callback
 * 
 * @param callback Callback function (NULL to unregister)
 */
void bt_app_avrc_register_metadata_callback(bt_avrc_metadata_cb_t callback);

/**
 * @brief Register playback status callback
 * 
 * @param callback Callback function (NULL to unregister)
 */
void bt_app_avrc_register_playback_status_callback(bt_avrc_playback_status_cb_t callback);

/**
 * @brief Register volume change callback
 * 
 * @param callback Callback function (NULL to unregister)
 */
void bt_app_avrc_register_volume_callback(bt_avrc_volume_cb_t callback);

#ifdef __cplusplus
}
#endif

#endif /* BT_APP_AVRC_H */
/**
 * @file bt_volume_control.h
 * @brief Bluetooth Volume Control for Car Stereo Application
 * 
 * Application-level volume control wrapper for a2dpSinkHfpHf component.
 * Manages volume state, provides unified interface for A2DP/HFP volume control.
 */

#ifndef BT_VOLUME_CONTROL_H
#define BT_VOLUME_CONTROL_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Volume control targets
 */
typedef enum {
    BT_VOLUME_TARGET_A2DP,          /**< A2DP music streaming (informational only) */
    BT_VOLUME_TARGET_HFP_SPEAKER,   /**< HFP hands-free speaker (call audio output) */
    BT_VOLUME_TARGET_HFP_MIC,       /**< HFP hands-free microphone (call audio input) */
    BT_VOLUME_TARGET_CALL_BOTH      /**< Both HFP speaker and mic together */
} bt_volume_target_t;

/**
 * Volume change callback
 * Called when volume changes (user control or remote device)
 * 
 * @param target Volume target that changed
 * @param new_volume New volume level (0-15)
 */
typedef void (*bt_volume_change_cb_t)(bt_volume_target_t target, uint8_t new_volume);

/**
 * Volume control configuration
 */
typedef struct {
    uint8_t default_a2dp_volume;         /**< Default A2DP volume (0-15) */
    uint8_t default_hfp_speaker_volume;  /**< Default HFP speaker volume (0-15) */
    uint8_t default_hfp_mic_volume;      /**< Default HFP microphone volume (0-15) */
    bt_volume_change_cb_t on_volume_change; /**< Optional callback for volume changes */
} bt_volume_config_t;

/**
 * Initialize Bluetooth volume control
 * 
 * @param config Configuration structure (required)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_volume_control_init(const bt_volume_config_t *config);

/**
 * Set volume for specific target
 * 
 * For A2DP: Tracked locally only (phone controls absolute volume via AVRCP)
 * For HFP: Sent to connected device via esp_hf_client_volume_update()
 * 
 * @param target Volume target (A2DP, HFP speaker, HFP mic, or both)
 * @param volume Volume level (0-15, clamped automatically)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_volume_control_set(bt_volume_target_t target, uint8_t volume);

/**
 * Get current volume for specific target
 * 
 * @param target Volume target
 * @return Current volume level (0-15), or 0 if invalid target
 */
uint8_t bt_volume_control_get(bt_volume_target_t target);

/**
 * Increase volume by specified amount
 * 
 * @param target Volume target
 * @param amount Amount to increase (will clamp at max)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_volume_control_increase(bt_volume_target_t target, uint8_t amount);

/**
 * Decrease volume by specified amount
 * 
 * @param target Volume target
 * @param amount Amount to decrease (will clamp at min)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_volume_control_decrease(bt_volume_target_t target, uint8_t amount);

/**
 * Mute or unmute specific volume target
 * 
 * @param target Volume target to mute/unmute
 * @param mute True to mute, false to unmute
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_volume_control_mute(bt_volume_target_t target, bool mute);

/**
 * Check if specific target is currently muted
 * 
 * @param target Volume target to check
 * @return True if muted, false otherwise
 */
bool bt_volume_control_is_muted(bt_volume_target_t target);

/**
 * Get volume limits (per HFP specification)
 * 
 * @param min Pointer to store minimum volume (can be NULL)
 * @param max Pointer to store maximum volume (can be NULL)
 */
void bt_volume_control_get_limits(uint8_t *min, uint8_t *max);

/**
 * Reset all volumes to default configuration values
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_volume_control_reset_defaults(void);

/**
 * Deinitialize volume control module
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_volume_control_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // BT_VOLUME_CONTROL_H

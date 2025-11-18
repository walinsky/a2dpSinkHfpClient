/**
 * @file bt_volume_control.c
 * @brief Bluetooth Volume Control Implementation
 * 
 * Application-level volume control that wraps a2dpSinkHfpHf component APIs.
 * No direct dependency on car_stereo_state - integration happens via callbacks.
 */

#include "bt_volume_control.h"
#include "a2dpSinkHfpHf.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#define TAG "BT_VOL_CTRL"

// Volume limits (0-15 per HFP spec)
#define VOLUME_MIN 0
#define VOLUME_MAX 15

// Global state
static bt_volume_config_t g_config = {0};
static SemaphoreHandle_t g_mutex = NULL;
static bool g_initialized = false;

// Current volume state
static uint8_t g_current_a2dp_volume = 10;
static uint8_t g_current_hfp_speaker_volume = 12;
static uint8_t g_current_hfp_mic_volume = 10;

// Mute state per target
static bool g_a2dp_muted = false;
static bool g_hfp_speaker_muted = false;
static bool g_hfp_mic_muted = false;

// Volume before mute (for restore)
static uint8_t g_a2dp_volume_before_mute = 0;
static uint8_t g_hfp_speaker_volume_before_mute = 0;
static uint8_t g_hfp_mic_volume_before_mute = 0;

/**
 * Internal: Apply HFP speaker volume
 */
static esp_err_t apply_hfp_speaker_volume(uint8_t volume)
{
    esp_err_t ret = a2dpSinkHfpHf_volume_update("spk", volume);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set HFP speaker volume: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "HFP speaker volume set to %d", volume);
    g_current_hfp_speaker_volume = volume;
    
    // Notify callback if registered
    if (g_config.on_volume_change) {
        g_config.on_volume_change(BT_VOLUME_TARGET_HFP_SPEAKER, volume);
    }
    
    return ESP_OK;
}

/**
 * Internal: Apply HFP microphone volume
 */
static esp_err_t apply_hfp_mic_volume(uint8_t volume)
{
    esp_err_t ret = a2dpSinkHfpHf_volume_update("mic", volume);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set HFP mic volume: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "HFP microphone volume set to %d", volume);
    g_current_hfp_mic_volume = volume;
    
    // Notify callback if registered
    if (g_config.on_volume_change) {
        g_config.on_volume_change(BT_VOLUME_TARGET_HFP_MIC, volume);
    }
    
    return ESP_OK;
}

/**
 * Internal: Track A2DP volume locally
 * Note: A2DP sink volume is controlled by the phone via AVRCP absolute volume.
 * We track it for UI purposes but don't directly control it.
 */
static esp_err_t track_a2dp_volume(uint8_t volume)
{
    ESP_LOGI(TAG, "A2DP volume tracked: %d (controlled by phone via AVRCP)", volume);
    g_current_a2dp_volume = volume;
    
    // Notify callback if registered
    if (g_config.on_volume_change) {
        g_config.on_volume_change(BT_VOLUME_TARGET_A2DP, volume);
    }
    
    return ESP_OK;
}

/**
 * Initialize Bluetooth volume control
 */
esp_err_t bt_volume_control_init(const bt_volume_config_t *config)
{
    if (g_initialized) {
        ESP_LOGW(TAG, "Volume control already initialized");
        return ESP_OK;
    }
    
    if (!config) {
        ESP_LOGE(TAG, "Invalid config");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Copy configuration
    memcpy(&g_config, config, sizeof(bt_volume_config_t));
    
    // Create mutex for thread safety
    g_mutex = xSemaphoreCreateMutex();
    if (!g_mutex) {
        ESP_LOGE(TAG, "Failed to create volume mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Set default volumes (clamped to valid range)
    g_current_a2dp_volume = (config->default_a2dp_volume > VOLUME_MAX) ? 
                             VOLUME_MAX : config->default_a2dp_volume;
    g_current_hfp_speaker_volume = (config->default_hfp_speaker_volume > VOLUME_MAX) ? 
                                    VOLUME_MAX : config->default_hfp_speaker_volume;
    g_current_hfp_mic_volume = (config->default_hfp_mic_volume > VOLUME_MAX) ? 
                                VOLUME_MAX : config->default_hfp_mic_volume;
    
    g_initialized = true;
    ESP_LOGI(TAG, "Bluetooth volume control initialized");
    ESP_LOGI(TAG, "  A2DP default: %d", g_current_a2dp_volume);
    ESP_LOGI(TAG, "  HFP speaker default: %d", g_current_hfp_speaker_volume);
    ESP_LOGI(TAG, "  HFP mic default: %d", g_current_hfp_mic_volume);
    
    return ESP_OK;
}

/**
 * Set volume for specific target
 */
esp_err_t bt_volume_control_set(bt_volume_target_t target, uint8_t volume)
{
    if (!g_initialized) {
        ESP_LOGE(TAG, "Volume control not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Clamp volume to valid range
    if (volume > VOLUME_MAX) {
        volume = VOLUME_MAX;
    }
    
    // Take mutex
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire volume mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    esp_err_t ret = ESP_OK;
    
    switch (target) {
        case BT_VOLUME_TARGET_A2DP:
            // A2DP volume is controlled by phone via AVRCP - just track locally
            ret = track_a2dp_volume(volume);
            break;
            
        case BT_VOLUME_TARGET_HFP_SPEAKER:
            ret = apply_hfp_speaker_volume(volume);
            break;
            
        case BT_VOLUME_TARGET_HFP_MIC:
            ret = apply_hfp_mic_volume(volume);
            break;
            
        case BT_VOLUME_TARGET_CALL_BOTH:
            // Set both speaker and mic to same volume
            ret = apply_hfp_speaker_volume(volume);
            if (ret == ESP_OK) {
                ret = apply_hfp_mic_volume(volume);
            }
            break;
            
        default:
            ESP_LOGE(TAG, "Invalid volume target: %d", target);
            ret = ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreGive(g_mutex);
    return ret;
}

/**
 * Get current volume for specific target
 */
uint8_t bt_volume_control_get(bt_volume_target_t target)
{
    if (!g_initialized) {
        ESP_LOGW(TAG, "Volume control not initialized");
        return 0;
    }
    
    switch (target) {
        case BT_VOLUME_TARGET_A2DP:
            return g_current_a2dp_volume;
        case BT_VOLUME_TARGET_HFP_SPEAKER:
            return g_current_hfp_speaker_volume;
        case BT_VOLUME_TARGET_HFP_MIC:
            return g_current_hfp_mic_volume;
        case BT_VOLUME_TARGET_CALL_BOTH:
            // Return speaker volume as representative
            return g_current_hfp_speaker_volume;
        default:
            return 0;
    }
}

/**
 * Increase volume by specified amount
 */
esp_err_t bt_volume_control_increase(bt_volume_target_t target, uint8_t amount)
{
    if (!g_initialized) {
        ESP_LOGE(TAG, "Volume control not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    uint8_t current = bt_volume_control_get(target);
    uint8_t new_volume = current + amount;
    
    if (new_volume > VOLUME_MAX) {
        new_volume = VOLUME_MAX;
    }
    
    return bt_volume_control_set(target, new_volume);
}

/**
 * Decrease volume by specified amount
 */
esp_err_t bt_volume_control_decrease(bt_volume_target_t target, uint8_t amount)
{
    if (!g_initialized) {
        ESP_LOGE(TAG, "Volume control not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    uint8_t current = bt_volume_control_get(target);
    int16_t new_volume = (int16_t)current - (int16_t)amount;
    
    if (new_volume < VOLUME_MIN) {
        new_volume = VOLUME_MIN;
    }
    
    return bt_volume_control_set(target, (uint8_t)new_volume);
}

/**
 * Mute or unmute specific volume target
 */
esp_err_t bt_volume_control_mute(bt_volume_target_t target, bool mute)
{
    if (!g_initialized) {
        ESP_LOGE(TAG, "Volume control not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire volume mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    esp_err_t ret = ESP_OK;
    
    switch (target) {
        case BT_VOLUME_TARGET_A2DP:
            if (mute && !g_a2dp_muted) {
                g_a2dp_volume_before_mute = g_current_a2dp_volume;
                ret = track_a2dp_volume(0);
                g_a2dp_muted = true;
                ESP_LOGI(TAG, "A2DP muted");
            } else if (!mute && g_a2dp_muted) {
                ret = track_a2dp_volume(g_a2dp_volume_before_mute);
                g_a2dp_muted = false;
                ESP_LOGI(TAG, "A2DP unmuted (restored to %d)", g_a2dp_volume_before_mute);
            }
            break;
            
        case BT_VOLUME_TARGET_HFP_SPEAKER:
            if (mute && !g_hfp_speaker_muted) {
                g_hfp_speaker_volume_before_mute = g_current_hfp_speaker_volume;
                ret = apply_hfp_speaker_volume(0);
                g_hfp_speaker_muted = true;
                ESP_LOGI(TAG, "HFP speaker muted");
            } else if (!mute && g_hfp_speaker_muted) {
                ret = apply_hfp_speaker_volume(g_hfp_speaker_volume_before_mute);
                g_hfp_speaker_muted = false;
                ESP_LOGI(TAG, "HFP speaker unmuted (restored to %d)", g_hfp_speaker_volume_before_mute);
            }
            break;
            
        case BT_VOLUME_TARGET_HFP_MIC:
            if (mute && !g_hfp_mic_muted) {
                g_hfp_mic_volume_before_mute = g_current_hfp_mic_volume;
                ret = apply_hfp_mic_volume(0);
                g_hfp_mic_muted = true;
                ESP_LOGI(TAG, "HFP mic muted");
            } else if (!mute && g_hfp_mic_muted) {
                ret = apply_hfp_mic_volume(g_hfp_mic_volume_before_mute);
                g_hfp_mic_muted = false;
                ESP_LOGI(TAG, "HFP mic unmuted (restored to %d)", g_hfp_mic_volume_before_mute);
            }
            break;
            
        case BT_VOLUME_TARGET_CALL_BOTH:
            // Mute/unmute both speaker and mic
            if (mute) {
                if (!g_hfp_speaker_muted) {
                    g_hfp_speaker_volume_before_mute = g_current_hfp_speaker_volume;
                    ret = apply_hfp_speaker_volume(0);
                    g_hfp_speaker_muted = true;
                }
                if (ret == ESP_OK && !g_hfp_mic_muted) {
                    g_hfp_mic_volume_before_mute = g_current_hfp_mic_volume;
                    ret = apply_hfp_mic_volume(0);
                    g_hfp_mic_muted = true;
                }
                ESP_LOGI(TAG, "Call audio muted (both speaker and mic)");
            } else {
                if (g_hfp_speaker_muted) {
                    ret = apply_hfp_speaker_volume(g_hfp_speaker_volume_before_mute);
                    g_hfp_speaker_muted = false;
                }
                if (ret == ESP_OK && g_hfp_mic_muted) {
                    ret = apply_hfp_mic_volume(g_hfp_mic_volume_before_mute);
                    g_hfp_mic_muted = false;
                }
                ESP_LOGI(TAG, "Call audio unmuted");
            }
            break;
            
        default:
            ESP_LOGE(TAG, "Invalid volume target: %d", target);
            ret = ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreGive(g_mutex);
    return ret;
}

/**
 * Check if specific target is currently muted
 */
bool bt_volume_control_is_muted(bt_volume_target_t target)
{
    switch (target) {
        case BT_VOLUME_TARGET_A2DP:
            return g_a2dp_muted;
        case BT_VOLUME_TARGET_HFP_SPEAKER:
            return g_hfp_speaker_muted;
        case BT_VOLUME_TARGET_HFP_MIC:
            return g_hfp_mic_muted;
        case BT_VOLUME_TARGET_CALL_BOTH:
            return g_hfp_speaker_muted && g_hfp_mic_muted;
        default:
            return false;
    }
}

/**
 * Get volume limits
 */
void bt_volume_control_get_limits(uint8_t *min, uint8_t *max)
{
    if (min) *min = VOLUME_MIN;
    if (max) *max = VOLUME_MAX;
}

/**
 * Reset to default volumes
 */
esp_err_t bt_volume_control_reset_defaults(void)
{
    if (!g_initialized) {
        ESP_LOGE(TAG, "Volume control not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Resetting to default volumes");
    
    bt_volume_control_set(BT_VOLUME_TARGET_A2DP, g_config.default_a2dp_volume);
    bt_volume_control_set(BT_VOLUME_TARGET_HFP_SPEAKER, g_config.default_hfp_speaker_volume);
    bt_volume_control_set(BT_VOLUME_TARGET_HFP_MIC, g_config.default_hfp_mic_volume);
    
    // Clear mute states
    g_a2dp_muted = false;
    g_hfp_speaker_muted = false;
    g_hfp_mic_muted = false;
    
    return ESP_OK;
}

/**
 * Deinitialize volume control
 */
esp_err_t bt_volume_control_deinit(void)
{
    if (!g_initialized) {
        return ESP_OK;
    }
    
    if (g_mutex) {
        vSemaphoreDelete(g_mutex);
        g_mutex = NULL;
    }
    
    g_initialized = false;
    ESP_LOGI(TAG, "Bluetooth volume control deinitialized");
    
    return ESP_OK;
}

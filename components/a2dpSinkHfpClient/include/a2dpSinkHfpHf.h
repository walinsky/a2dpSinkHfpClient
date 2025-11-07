/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#ifndef __A2DP_SINK_HFP_HF_H__
#define __A2DP_SINK_HFP_HF_H__

#include <stdbool.h>
#include "esp_err.h"
#include "bt_app_avrc.h" // Include for AVRC types

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration structure for a2dpSinkHfpHf component
 */
typedef struct {
    const char *device_name;        ///< Bluetooth device name (visible when scanning)
    
    // I2S TX (speaker output) pin configuration
    int i2s_tx_bck;                 ///< I2S TX bit clock pin
    int i2s_tx_ws;                  ///< I2S TX word select (LRCK) pin
    int i2s_tx_dout;                ///< I2S TX data out (speaker/DAC) pin
    
    // I2S RX (microphone input) pin configuration
    int i2s_rx_bck;                 ///< I2S RX bit clock pin
    int i2s_rx_ws;                  ///< I2S RX word select (LRCK) pin
    int i2s_rx_din;                 ///< I2S RX data in (microphone) pin
} a2dpSinkHfpHf_config_t;

/**
 * @brief Initialize the A2DP Sink + HFP Hands-Free component
 * 
 * This must be called after Bluedroid stack is initialized (esp_bluedroid_enable()).
 * It initializes:
 * - BT core task dispatcher
 * - I2S audio interface with configured pins
 * - Audio codec (mSBC for HFP)
 * - GAP layer (device discovery and pairing)
 * - HFP Hands-Free profile
 * - A2DP Sink profile
 * 
 * @param config Configuration structure with pin and device settings
 * @return ESP_OK on success, ESP_ERR_* on error
 */
esp_err_t a2dpSinkHfpHf_init(const a2dpSinkHfpHf_config_t *config);

/**
 * @brief Deinitialize the component
 * 
 * Cleanly shuts down all profiles, stops audio, and releases resources.
 * 
 * @return ESP_OK on success
 */
esp_err_t a2dpSinkHfpHf_deinit(void);

/**
 * @brief Start Bluetooth device discovery (make device discoverable)
 * 
 * Makes the device visible to other Bluetooth devices during scanning.
 * 
 * @return ESP_OK on success, ESP_ERR_* on error
 */
esp_err_t a2dpSinkHfpHf_start_discovery(void);

/**
 * @brief Stop Bluetooth device discovery
 * 
 * @return ESP_OK on success, ESP_ERR_* on error
 */
esp_err_t a2dpSinkHfpHf_cancel_discovery(void);

/**
 * @brief Get the current device name
 * 
 * @return Device name string, or NULL if not set
 */
const char *a2dpSinkHfpHf_get_device_name(void);

/**
 * @brief Check if device is connected (HFP or A2DP)
 * 
 * @return true if connected, false otherwise
 */
bool a2dpSinkHfpHf_is_connected(void);

/**
 * @brief Set country code for phonebook before initialization
 */
esp_err_t a2dpSinkHfpHf_set_country_code(const char *country_code);

/**
 * @brief Set Bluetooth pairing PIN code (compile-time configuration)
 * 
 * This function MUST be called BEFORE a2dpSinkHfpHf_init() to take effect.
 * Use this to configure the PIN code at compile time from main.c.
 * 
 * @param pin_code PIN code string (4-16 digits, ASCII '0'-'9')
 * @param pin_len Length of PIN code (must be 4-16)
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if pin_code is NULL or invalid
 * @return ESP_ERR_INVALID_STATE if called after initialization
 * 
 * @example
 *   // In main.c, before calling a2dpSinkHfpHf_init()
 *   a2dpSinkHfpHf_set_pin("5678", 4);
 */
esp_err_t a2dpSinkHfpHf_set_pin(const char *pin_code, uint8_t pin_len);

/**
 * @brief Get current PIN code configuration
 * 
 * @param pin_code Buffer to receive PIN (minimum 17 bytes)
 * @param pin_len Pointer to receive PIN length
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if buffers are NULL
 */
esp_err_t a2dpSinkHfpHf_get_pin(char *pin_code, uint8_t *pin_len);

/* ============================================
 * AVRC (Audio/Video Remote Control) API
 * ============================================ */

/**
 * @brief Register callback for AVRC connection state changes
 * @param callback Function to call when connection state changes (connected/disconnected)
 */
void a2dpSinkHfpHf_register_avrc_conn_callback(bt_avrc_conn_state_cb_t callback);

/**
 * @brief Register callback for metadata updates (track, artist, album, etc.)
 * @param callback Function to call when metadata changes
 */
void a2dpSinkHfpHf_register_avrc_metadata_callback(bt_avrc_metadata_cb_t callback);

/**
 * @brief Register callback for playback status changes (playing, paused, stopped, etc.)
 * @param callback Function to call when playback status changes
 */
void a2dpSinkHfpHf_register_avrc_playback_callback(bt_avrc_playback_status_cb_t callback);

/**
 * @brief Register callback for volume changes
 * @param callback Function to call when volume changes (0-127)
 */
void a2dpSinkHfpHf_register_avrc_volume_callback(bt_avrc_volume_cb_t callback);

/**
 * @brief Get current AVRC metadata (if available)
 * @return Pointer to metadata struct, or NULL if not available
 */
const bt_avrc_metadata_t* a2dpSinkHfpHf_get_avrc_metadata(void);

/**
 * @brief Check if AVRC is connected
 * @return true if connected, false otherwise
 */
bool a2dpSinkHfpHf_is_avrc_connected(void);

/**
 * @brief Send AVRC play command
 * @return true if sent successfully, false if not connected
 */
bool a2dpSinkHfpHf_avrc_play(void);

/**
 * @brief Send AVRC pause command
 * @return true if sent successfully, false if not connected
 */
bool a2dpSinkHfpHf_avrc_pause(void);

/**
 * @brief Send AVRC next track command
 * @return true if sent successfully, false if not connected
 */
bool a2dpSinkHfpHf_avrc_next(void);

/**
 * @brief Send AVRC previous track command
 * @return true if sent successfully, false if not connected
 */
bool a2dpSinkHfpHf_avrc_prev(void);

#ifdef __cplusplus
}
#endif

#endif /* __A2DP_SINK_HFP_HF_H__ */

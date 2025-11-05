/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#ifndef __A2DP_SINK_HFP_HF_H__
#define __A2DP_SINK_HFP_HF_H__

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration structure for a2dpSinkHfpHf component
 */
typedef struct {
    const char *device_name;     ///< Bluetooth device name (visible when scanning)

    // I2S TX (speaker output) pin configuration
    int i2s_tx_bck;              ///< I2S TX bit clock pin
    int i2s_tx_ws;               ///< I2S TX word select (LRCK) pin
    int i2s_tx_dout;             ///< I2S TX data out (speaker/DAC) pin

    // I2S RX (microphone input) pin configuration
    int i2s_rx_bck;              ///< I2S RX bit clock pin
    int i2s_rx_ws;               ///< I2S RX word select (LRCK) pin
    int i2s_rx_din;              ///< I2S RX data in (microphone) pin
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
 * @brief Get the configured device name
 * @return Device name string
 */
const char *a2dpSinkHfpHf_get_device_name(void);

/**
 * @brief Set country code for phonebook before initialization
 */
esp_err_t a2dpSinkHfpHf_set_country_code(const char *country_code);


#ifdef __cplusplus
}
#endif

#endif /* __A2DP_SINK_HFP_HF_H__ */

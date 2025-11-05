/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#ifndef BT_GAP_H
#define BT_GAP_H

#include "esp_bt_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Global peer address - updated when target device found/connected */
extern esp_bd_addr_t peer_addr;

/**
 * @brief Initialize Bluetooth GAP
 */
esp_err_t bt_gap_init(void);

/**
 * @brief Deinitialize Bluetooth GAP
 */
esp_err_t bt_gap_deinit(void);

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
 * Searches for devices with a specific name
 */
esp_err_t bt_gap_start_discovery(void);

/**
 * @brief Cancel ongoing Bluetooth device discovery
 */
esp_err_t bt_gap_cancel_discovery(void);

#ifdef __cplusplus
}
#endif

#endif /* BT_GAP_H */

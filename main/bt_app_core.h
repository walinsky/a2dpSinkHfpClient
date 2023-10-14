/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#ifndef __BT_APP_CORE_H__
#define __BT_APP_CORE_H__

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "bt_app_i2s.h"

/* log tag */
#define BT_APP_CORE_TAG    "BT_APP_CORE"

/* signal for `bt_app_work_dispatch` */
#define BT_APP_SIG_WORK_DISPATCH    (0x01)

/**
 * @brief  handler for the dispatched work
 *
 * @param [in] event  event id
 * @param [in] param  handler parameter
 */
typedef void (* bt_app_cb_t) (uint16_t event, void *param);

/* message to be sent */
typedef struct {
    uint16_t       sig;      /*!< signal to bt_app_task */
    uint16_t       event;    /*!< message event id */
    bt_app_cb_t    cb;       /*!< context switch callback */
    void           *param;   /*!< parameter area needs to be last */
} bt_app_msg_t;

/**
 * @brief  parameter deep-copy function to be customized
 *
 * @param [out] p_dest  pointer to destination data
 * @param [in]  p_src   pointer to source data
 * @param [in]  len     data length in byte
 */
typedef void (* bt_app_copy_cb_t) (void *p_dest, void *p_src, int len);

/**
 * @brief  work dispatcher for the application task
 *
 * @param [in] p_cback       callback function
 * @param [in] event         event id
 * @param [in] p_params      callback paramters
 * @param [in] param_len     parameter length in byte
 * @param [in] p_copy_cback  parameter deep-copy function
 *
 * @return  true if work dispatch successfully, false otherwise
 */
bool bt_app_work_dispatch(bt_app_cb_t p_cback, uint16_t event, void *p_params, int param_len, bt_app_copy_cb_t p_copy_cback);

/**
 * @brief  initialize our classic bluetooth stack
 */
void bt_app_bt_init(void);
/**
 * @brief  start up the application task
 */
void bt_app_task_start_up(void);

/**
 * @brief  shut down the application task
 */
void bt_app_task_shut_down(void);

/**
 * @brief  initialize the a2dp I2S tx task
 */
void bt_i2s_a2dp_task_init(void);
/**
 * @brief  deinitialize the a2dp I2S tx task
 */
void bt_i2s_a2dp_task_deinit(void);
/**
 * @brief  start up the a2dp I2S tx task
 */
void bt_i2s_a2dp_task_start_up(void);

/**
 * @brief  shut down the a2dp I2S tx task
 */
void bt_i2s_a2dp_task_shut_down(void);

/**
 * @brief  start up the hfp I2S rx task
 */
void bt_i2s_hfp_task_start_up(void);

/**
 * @brief  shut down the hfp I2S rx task
 */
void bt_i2s_hfp_task_shut_down(void);

void setup_i2s_rx_timer();
void start_i2s_rx_timer();
void stop_i2s_rx_timer();

/**
 * @brief  write a2dp data to ringbuffer
 *
 * @param [in] data  pointer to data stream
 * @param [in] size  data length in byte
 *
 * @return size if writteen ringbuffer successfully, 0 others
 */
size_t write_ringbuf(const uint8_t *data, uint32_t size);
size_t write_rx_ringbuf(char *data, uint32_t size);
uint32_t read_ringbuf(uint8_t *p_buf, uint32_t sz);
void bt_app_hf_client_tx_data_cb(const uint8_t *buf, uint32_t len);
void bt_app_set_pin_code(const char *pin, uint8_t pin_code_len);
void bt_app_set_device_name(char *name);
#endif /* __BT_APP_CORE_H__ */

/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/xtensa_api.h"
#include "freertos/FreeRTOSConfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bt_app_core.h"
#include "bt_app_volume_control.h"
#include "bt_app_i2s.h"
#include "driver/i2s_std.h"
#include "freertos/ringbuf.h"
#include "bt_app_hf.h"
#include "esp_timer.h"
#include "osi/allocator.h"

#define RINGBUF_HIGHEST_WATER_LEVEL    (32 * 1024)
#define RINGBUF_PREFETCH_WATER_LEVEL   (20 * 1024)

enum {
    RINGBUFFER_MODE_PROCESSING,    /* ringbuffer is buffering incoming audio data, I2S is working */
    RINGBUFFER_MODE_PREFETCHING,   /* ringbuffer is buffering incoming audio data, I2S is waiting */
    RINGBUFFER_MODE_DROPPING       /* ringbuffer is not buffering (dropping) incoming audio data, I2S is working */
};

enum {
    I2S_TX_MODE_NONE,   /* i2s tx isn't being used by a2dp or hfp */
    I2S_TX_MODE_A2DP,   /* i2s tx is being used by a2dp */
    I2S_TX_MODE_HFP     /* i2s tx is being used by hfp */
};

// 7500 microseconds(=12 slots) is aligned to 1 msbc frame duration, and is multiple of common Tesco for eSCO link with EV3 or 2-EV3 packet type
#define PCM_BLOCK_DURATION_US        (7500)

/*******************************
 * STATIC FUNCTION DECLARATIONS
 ******************************/

/* handler for application task */
static void bt_app_task_handler(void *arg);
/* handler for I2S task */
static void bt_i2s_task_handler(void *arg);
/* message sender */
static bool bt_app_send_msg(bt_app_msg_t *msg);
/* handle dispatched messages */
static void bt_app_work_dispatched(bt_app_msg_t *msg);
/* handler for I2S rx task */
static void bt_i2s_rx_task_handler(void *arg);

/*******************************
 * STATIC VARIABLE DEFINITIONS
 ******************************/

static QueueHandle_t s_bt_app_task_queue = NULL;            /* handle of work queue */
static TaskHandle_t s_bt_app_task_handle = NULL;            /* handle of application task  */
static TaskHandle_t s_bt_i2s_task_handle = NULL;            /* handle of I2S task */
static RingbufHandle_t s_ringbuf_i2s = NULL;                /* handle of ringbuffer for I2S */
static SemaphoreHandle_t s_i2s_write_semaphore = NULL;      /* handle of semaphore for a2dp I2S */
static TaskHandle_t s_bt_i2s_rx_task_handle = NULL;         /* handle of I2S rx task */
static RingbufHandle_t s_ringbuf_i2s_rx = NULL;             /* handle of ringbuffer for I2S rx*/
static SemaphoreHandle_t s_i2s_rx_write_semaphore = NULL;   /* handle of semaphore for hfp I2S rx */
static TaskHandle_t s_bt_i2s_tx_task_handle = NULL;         /* handle of I2S tx task */
static RingbufHandle_t s_ringbuf_i2s_tx = NULL;             /* handle of ringbuffer for I2S tx*/
static SemaphoreHandle_t s_i2s_tx_write_semaphore = NULL;   /* handle of semaphore for hfp I2S tx */

// static SemaphoreHandle_t s_i2s_rx_write_callback_semaphore = NULL;
static uint16_t ringbuffer_mode = RINGBUFFER_MODE_PROCESSING;
static uint16_t rx_ringbuffer_mode = RINGBUFFER_MODE_PROCESSING;
static uint16_t s_i2s_tx_mode = I2S_TX_MODE_NONE;
static SemaphoreHandle_t s_i2s_tx_mode_semaphore = NULL;    /* handle of semaphore for I2S tx mode */
static esp_timer_handle_t s_i2s_rx_timer = NULL;
/*********************************
 * EXTERNAL FUNCTION DECLARATIONS
 ********************************/
extern i2s_chan_handle_t tx_chan;
extern i2s_chan_handle_t rx_chan;


/*******************************
 * STATIC FUNCTION DEFINITIONS
 ******************************/
static void i2s_rx_timer_callback(void* arg);

static bool bt_app_send_msg(bt_app_msg_t *msg)
{
    if (msg == NULL) {
        return false;
    }

    /* send the message to work queue */
    if (xQueueSend(s_bt_app_task_queue, msg, 10 / portTICK_PERIOD_MS) != pdTRUE) {
        ESP_LOGE(BT_APP_CORE_TAG, "%s xQueue send failed", __func__);
        return false;
    }
    return true;
}

static void bt_app_work_dispatched(bt_app_msg_t *msg)
{
    if (msg->cb) {
        msg->cb(msg->event, msg->param);
    }
}

static void bt_app_task_handler(void *arg)
{
    bt_app_msg_t msg;
    for (;;) {
        /* receive message from work queue and handle it */
        if (pdTRUE == xQueueReceive(s_bt_app_task_queue, &msg, (TickType_t)portMAX_DELAY)) {
            ESP_LOGD(BT_APP_CORE_TAG, "%s, signal: 0x%x, event: 0x%x", __func__, msg.sig, msg.event);

            switch (msg.sig) {
            case BT_APP_SIG_WORK_DISPATCH:
                bt_app_work_dispatched(&msg);
                break;
            default:
                ESP_LOGW(BT_APP_CORE_TAG, "%s, unhandled signal: %d", __func__, msg.sig);
                break;
            } /* switch (msg.sig) */

            if (msg.param) {
                free(msg.param);
            }
        }
    }
}

static void bt_i2s_task_handler(void *arg)
{
    uint8_t *data = NULL;
    size_t item_size = 0;
    /**
     * The total length of DMA buffer of I2S is:
     * `dma_frame_num * dma_desc_num * i2s_channel_num * i2s_data_bit_width / 8`.
     * Transmit `dma_frame_num * dma_desc_num` bytes to DMA is trade-off.
     */
    const size_t item_size_upto = 240 * 6;
    size_t bytes_written = 0;
    int last_i2s_tx_mode = I2S_TX_MODE_NONE;
    for (;;) {
        if (pdTRUE == xSemaphoreTake(s_i2s_write_semaphore, portMAX_DELAY)) {
            for (;;) {
                item_size = 0;
                /* receive data from ringbuffer and write it to I2S DMA transmit buffer */
                /* note: write_ringbuf always writes 4096 bytes to our ringbuffer */
                data = (uint8_t *)xRingbufferReceiveUpTo(s_ringbuf_i2s, &item_size, 0, item_size_upto);
                if (item_size == 0) {
                    ESP_LOGI(BT_APP_CORE_TAG, "%s - tx ringbuffer underflowed! mode changed: RINGBUFFER_MODE_PREFETCHING", __func__);
                    ringbuffer_mode = RINGBUFFER_MODE_PREFETCHING;
                    break;
                }
                if (s_i2s_tx_mode == I2S_TX_MODE_A2DP)
                {
                    if (last_i2s_tx_mode != I2S_TX_MODE_A2DP)
                    {
                        //  ESP_LOGI(BT_APP_CORE_TAG, "%s - delaying for we just changed to a2dp mode", __func__);
                        //  vTaskDelay(50);
                        // bt_i2s_a2dp_task_start_up();
                    }
                    i2s_channel_write(tx_chan, data, item_size, &bytes_written, portMAX_DELAY);
                }
                vRingbufferReturnItem(s_ringbuf_i2s, (void *)data);
                last_i2s_tx_mode = s_i2s_tx_mode;
            }
        }
    }
}

static void bt_i2s_tx_task_handler(void *arg)
{
    const size_t item_size_upto = 240;
    size_t item_size = 0;
    while (true) {
        if (xSemaphoreTake(s_i2s_tx_write_semaphore, (TickType_t)portMAX_DELAY)) {
            uint8_t *data = xRingbufferReceiveUpTo(s_ringbuf_i2s_tx, &item_size, 0, item_size_upto);
            if (item_size == 0) {
                ESP_LOGI(BT_APP_CORE_TAG, "%s - tx ringbuffer underflowed!", __func__);
                break;
            }
            i2s_channel_write(tx_chan, data, item_size, NULL, 0);
            vRingbufferReturnItem(s_ringbuf_i2s_tx, data);
        }
        // taskYIELD();
    }
}

static void bt_i2s_rx_task_handler(void *arg)
{
    // Codec (code & decode) for HFP is mSBC with specific parameters.
    // It transfers 240 Bytes every 7.5 ms. So we need to create a 240 Byte (120 16bit mono samples) output buffer every 7.5ms.
    // Our mems microphone on I2S is 32bit; we get both left and right channels. This totals 64 bits, or 8 bytes, per sample.
    // So for every 64bit (8 byte) input sample we get a 16bit (2 byte) output sample
    const size_t item_size_upto = 120 * 8; // input sample bytes
    char *i2s_rx_buff = calloc(item_size_upto, sizeof(char));
    char *i2s_tx_buff = calloc(item_size_upto / 4, sizeof(char)); // every 8 input bytes(64 bit) give us 2 output bytes (16bits)

    size_t bytes_read;
    uint32_t bytes_written;
    while (true) {
        // todo: do the osi alloc here, initialize bytes_read/bytes_written here
        if (xSemaphoreTake(s_i2s_rx_write_semaphore, (TickType_t)portMAX_DELAY)) {
            char *buf_ptr_read = i2s_rx_buff;
            char *buf_ptr_write = i2s_tx_buff;
            // Read the RAW samples from the microphone
            if (i2s_channel_read(rx_chan, i2s_rx_buff, item_size_upto, &bytes_read, 0) == ESP_OK) { // we wait 0 ticks! the semaphore gives us enough headroom
                int raw_samples_read = bytes_read / 2 / (I2S_DATA_BIT_WIDTH_32BIT / 8); // raw samples = bytes read / 2 channels / 4 bytes per channel
                for (int i = 0; i < raw_samples_read; i++)
                {
                    // left channel (mono)
                    buf_ptr_write[0] = buf_ptr_read[2]; // mid
                    buf_ptr_write[1] = buf_ptr_read[3]; // high
                    buf_ptr_write += 1 * (I2S_DATA_BIT_WIDTH_16BIT / 8); // 1 channel mono; step our pointer 1 x 2 bytes
                    buf_ptr_read += 2 * (I2S_DATA_BIT_WIDTH_32BIT / 8); // 2 channel stereo; step our pointer 2 * 4 bytes
                }
                bytes_written = raw_samples_read * 1 * (I2S_DATA_BIT_WIDTH_16BIT / 8); // raw samples * 1 channel mono * 2 bytes per sample (output)
                write_rx_ringbuf((char *)i2s_tx_buff, bytes_written);
                if (rx_ringbuffer_mode != RINGBUFFER_MODE_PREFETCHING) {
                    esp_hf_client_outgoing_data_ready(); // After this function is called, lower layer will invoke esp_hf_client_outgoing_data_cb_t to fetch data.
                }
            } else {
                ESP_LOGI(BT_APP_CORE_TAG, "Read Failed!");
            }
        }
        // todo: free (osi) tx and rx buff here
        taskYIELD();
    }
}

void setup_i2s_rx_timer()
{
    const esp_timer_create_args_t periodic_timer_args = {
            .callback = &i2s_rx_timer_callback,
            /* name is optional, but may help identify the timer when debugging */
            .name = "i2s_rx_periodic"
    };

    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &s_i2s_rx_timer));
    /* The timer has been created but is not running yet */
    
}

void start_i2s_rx_timer()
{
    s_i2s_rx_write_semaphore = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_i2s_rx_timer, PCM_BLOCK_DURATION_US));
}

void stop_i2s_rx_timer()
{
    ESP_ERROR_CHECK(esp_timer_stop(s_i2s_rx_timer));
    if (s_i2s_rx_write_semaphore) {
        vSemaphoreDelete(s_i2s_rx_write_semaphore);
        s_i2s_rx_write_semaphore = NULL;
    }
}

 static void i2s_rx_timer_callback(void* arg)
{
    if (s_i2s_rx_write_semaphore != NULL) {
        xSemaphoreGive(s_i2s_rx_write_semaphore);
    }
}
/********************************
 * EXTERNAL FUNCTION DEFINITIONS
 *******************************/

bool bt_app_work_dispatch(bt_app_cb_t p_cback, uint16_t event, void *p_params, int param_len, bt_app_copy_cb_t p_copy_cback)
{
    ESP_LOGD(BT_APP_CORE_TAG, "%s event: 0x%x, param len: %d", __func__, event, param_len);

    bt_app_msg_t msg;
    memset(&msg, 0, sizeof(bt_app_msg_t));

    msg.sig = BT_APP_SIG_WORK_DISPATCH;
    msg.event = event;
    msg.cb = p_cback;

    if (param_len == 0) {
        return bt_app_send_msg(&msg);
    } else if (p_params && param_len > 0) {
        if ((msg.param = malloc(param_len)) != NULL) {
            memcpy(msg.param, p_params, param_len);
            /* check if caller has provided a copy callback to do the deep copy */
            if (p_copy_cback) {
                p_copy_cback(msg.param, p_params, param_len);
            }
            return bt_app_send_msg(&msg);
        }
    }

    return false;
}

void bt_app_task_start_up(void)
{
    s_bt_app_task_queue = xQueueCreate(10, sizeof(bt_app_msg_t));
    xTaskCreate(bt_app_task_handler, "BtAppTask", 3072, NULL, 10, &s_bt_app_task_handle);
}

void bt_app_task_shut_down(void)
{
    if (s_bt_app_task_handle) {
        vTaskDelete(s_bt_app_task_handle);
        s_bt_app_task_handle = NULL;
    }
    if (s_bt_app_task_queue) {
        vQueueDelete(s_bt_app_task_queue);
        s_bt_app_task_queue = NULL;
    }
}

/* 
    initialize our ringbuffer and write semaphore for a2dp tx
 */
void bt_i2s_a2dp_task_init(void)
{
    // s_i2s_tx_mode_semaphore should this be here? we need to init it somewhere
    // if ((s_i2s_tx_mode_semaphore = xSemaphoreCreateBinary()) == NULL) {
    //     ESP_LOGE(BT_APP_CORE_TAG, "%s, s_i2s_tx_mode_semaphore Semaphore create failed", __func__);
    //     return;
    // }
    // bt_app_set_i2s_tx_mode_none();
    ESP_LOGI(BT_APP_CORE_TAG, "ringbuffer data empty! mode changed: RINGBUFFER_MODE_PREFETCHING");
    ringbuffer_mode = RINGBUFFER_MODE_PREFETCHING;
    if ((s_i2s_write_semaphore = xSemaphoreCreateBinary()) == NULL) {
        ESP_LOGE(BT_APP_CORE_TAG, "%s, s_i2s_write_semaphore Semaphore create failed", __func__);
        return;
    }
    if ((s_ringbuf_i2s = xRingbufferCreate(RINGBUF_HIGHEST_WATER_LEVEL, RINGBUF_TYPE_BYTEBUF)) == NULL) {
        ESP_LOGE(BT_APP_CORE_TAG, "%s, ringbuffer create failed", __func__);
        return;
    }
    xTaskCreate(bt_i2s_task_handler, "BtI2STask", 2048, NULL, configMAX_PRIORITIES - 4, &s_bt_i2s_task_handle);
}

void bt_i2s_a2dp_task_deinit(void)
{
    if (s_bt_i2s_task_handle) {
        vTaskDelete(s_bt_i2s_task_handle);
        s_bt_i2s_task_handle = NULL;
    }
    if (s_ringbuf_i2s) {
        vRingbufferDelete(s_ringbuf_i2s);
        s_ringbuf_i2s = NULL;
    }
    if (s_i2s_write_semaphore) {
        vSemaphoreDelete(s_i2s_write_semaphore);
        s_i2s_write_semaphore = NULL;
    }
    if (s_i2s_tx_mode_semaphore) {
        vSemaphoreDelete(s_i2s_tx_mode_semaphore);
        s_i2s_tx_mode_semaphore = NULL;
    }
}

/* 
    start our a2dp tx task.
    this should be called after bt_i2s_task_init
 */
void bt_i2s_a2dp_task_start_up(void)
{
    
    bt_i2s_channels_config_adp();
    bt_i2s_tx_channel_enable();
    s_i2s_tx_mode = I2S_TX_MODE_A2DP;
}

/* 
    stop our a2dp tx task.
    this should be called before bt_i2s_task_deinit
 */
void bt_i2s_a2dp_task_shut_down(void)
{
    // if (s_bt_i2s_task_handle) {
    //     vTaskDelete(s_bt_i2s_task_handle);
    //     s_bt_i2s_task_handle = NULL;
    // }
    s_i2s_tx_mode = I2S_TX_MODE_NONE;
    bt_i2s_tx_channel_disable();
}

// todo: send this as msg to the app queue
void bt_i2s_hfp_task_start_up(void)
{
    // bt_app_set_i2s_tx_mode_hfp(); // s_i2s_tx_mode = I2S_TX_MODE_HFP;
    bt_i2s_channels_config_hfp();
    bt_i2s_tx_channel_enable();
    bt_i2s_rx_channel_enable();
    if ((s_i2s_rx_write_semaphore = xSemaphoreCreateBinary()) == NULL) {
        ESP_LOGE(BT_APP_CORE_TAG, "%s, rx Semaphore create failed", __func__);
        return;
    }
    if ((s_i2s_tx_write_semaphore = xSemaphoreCreateBinary()) == NULL) {
        ESP_LOGE(BT_APP_CORE_TAG, "%s, rx Semaphore create failed", __func__);
        return;
    }
    if ((s_ringbuf_i2s_rx = xRingbufferCreate(RINGBUF_HIGHEST_WATER_LEVEL, RINGBUF_TYPE_BYTEBUF)) == NULL) {
        ESP_LOGE(BT_APP_CORE_TAG, "%s, rx ringbuffer create failed", __func__);
        return;
    }
    if ((s_ringbuf_i2s_tx = xRingbufferCreate(RINGBUF_HIGHEST_WATER_LEVEL, RINGBUF_TYPE_BYTEBUF)) == NULL) {
        ESP_LOGE(BT_APP_CORE_TAG, "%s, tx ringbuffer create failed", __func__);
        return;
    }
    start_i2s_rx_timer();
    xTaskCreate(bt_i2s_rx_task_handler, "BtI2SRxTask", 2048, NULL, configMAX_PRIORITIES - 3, &s_bt_i2s_rx_task_handle);
    xTaskCreate(bt_i2s_tx_task_handler, "BtI2SRxTask", 2048, NULL, configMAX_PRIORITIES - 3, &s_bt_i2s_tx_task_handle);
}

void bt_i2s_hfp_task_shut_down(void)
{
    if (s_bt_i2s_rx_task_handle) {
        vTaskDelete(s_bt_i2s_rx_task_handle);
        s_bt_i2s_rx_task_handle = NULL;
    }
    if (s_bt_i2s_tx_task_handle) {
        vTaskDelete(s_bt_i2s_tx_task_handle);
        s_bt_i2s_tx_task_handle = NULL;
    }
    if (s_ringbuf_i2s_rx) {
        vRingbufferDelete(s_ringbuf_i2s_rx);
        s_ringbuf_i2s_rx = NULL;
    }
    if (s_ringbuf_i2s_tx) {
        vRingbufferDelete(s_ringbuf_i2s_tx);
        s_ringbuf_i2s_tx = NULL;
    }
    stop_i2s_rx_timer();
    if (s_i2s_rx_write_semaphore) {
        vSemaphoreDelete(s_i2s_rx_write_semaphore);
        s_i2s_rx_write_semaphore = NULL;
    }
    if (s_i2s_tx_write_semaphore) {
        vSemaphoreDelete(s_i2s_tx_write_semaphore);
        s_i2s_tx_write_semaphore = NULL;
    }
    bt_i2s_tx_channel_disable();
    bt_i2s_rx_channel_disable();
    bt_i2s_channels_config_adp();// i2s task for adp is a loose cannon. once we give our semaphore it'll start sending data
}

size_t write_ringbuf(const uint8_t *data, uint32_t size)
{
    size_t item_size = 0;
    BaseType_t done = pdFALSE;

    if (ringbuffer_mode == RINGBUFFER_MODE_DROPPING) {
        ESP_LOGW(BT_APP_CORE_TAG, "%s - ringbuffer is full, drop this packet!", __func__);
        vRingbufferGetInfo(s_ringbuf_i2s, NULL, NULL, NULL, NULL, &item_size);
        if (item_size <= RINGBUF_PREFETCH_WATER_LEVEL) {
            ESP_LOGI(BT_APP_CORE_TAG, "%s - ringbuffer data decreased! mode changed: RINGBUFFER_MODE_PROCESSING", __func__);
            ringbuffer_mode = RINGBUFFER_MODE_PROCESSING;
        }
        return 0;
    }
    // ESP_LOGI(BT_APP_CORE_TAG, "%s - sending tx size %lu to the ringbuffer", __func__, size);
    done = xRingbufferSend(s_ringbuf_i2s, (void *)data, size, (TickType_t)0);

    if (!done) {
        ESP_LOGW(BT_APP_CORE_TAG, "%s - ringbuffer overflowed, ready to decrease data! mode changed: RINGBUFFER_MODE_DROPPING", __func__);
        ringbuffer_mode = RINGBUFFER_MODE_DROPPING;
    }

    if (ringbuffer_mode == RINGBUFFER_MODE_PREFETCHING) {
        vRingbufferGetInfo(s_ringbuf_i2s, NULL, NULL, NULL, NULL, &item_size);
        if (item_size >= RINGBUF_PREFETCH_WATER_LEVEL) {
            ESP_LOGI(BT_APP_CORE_TAG, "%s - ringbuffer data increased! mode changed: RINGBUFFER_MODE_PROCESSING", __func__);
            ringbuffer_mode = RINGBUFFER_MODE_PROCESSING;
            if (pdFALSE == xSemaphoreGive(s_i2s_write_semaphore)) {
                ESP_LOGE(BT_APP_CORE_TAG, "%s - semphore give failed", __func__);
            }
        }
    }

    return done ? size : 0;
}

size_t write_rx_ringbuf(char *data, uint32_t size)
{
    size_t item_size = 0;
    BaseType_t done = pdFALSE;

    if (rx_ringbuffer_mode == RINGBUFFER_MODE_DROPPING) {
        ESP_LOGW(BT_APP_CORE_TAG, "%s - ringbuffer is full, drop this packet!", __func__);
        vRingbufferGetInfo(s_ringbuf_i2s_rx, NULL, NULL, NULL, NULL, &item_size);
        if (item_size <= RINGBUF_PREFETCH_WATER_LEVEL) {
            ESP_LOGI(BT_APP_CORE_TAG, "%s - ringbuffer data decreased! mode changed: RINGBUFFER_MODE_PROCESSING", __func__);
            rx_ringbuffer_mode = RINGBUFFER_MODE_PROCESSING;
        }
        return 0;
    }
    // ESP_LOGI(BT_APP_CORE_TAG, "sending rx size %lu to the ringbuffer", size);
    done = xRingbufferSend(s_ringbuf_i2s_rx, (void *)data, size, (TickType_t)0);

    if (!done) {
        ESP_LOGW(BT_APP_CORE_TAG, "%s - ringbuffer overflowed, ready to decrease data! mode changed: RINGBUFFER_MODE_DROPPING", __func__);
        rx_ringbuffer_mode = RINGBUFFER_MODE_DROPPING;
    }

    if (rx_ringbuffer_mode == RINGBUFFER_MODE_PREFETCHING) {
        vRingbufferGetInfo(s_ringbuf_i2s_rx, NULL, NULL, NULL, NULL, &item_size);
        if (item_size >= RINGBUF_PREFETCH_WATER_LEVEL) {
            ESP_LOGI(BT_APP_CORE_TAG, "%s - ringbuffer data increased! mode changed: RINGBUFFER_MODE_PROCESSING", __func__);
            rx_ringbuffer_mode = RINGBUFFER_MODE_PROCESSING;
            if (pdFALSE == xSemaphoreGive(s_i2s_rx_write_semaphore)) {
                ESP_LOGE(BT_APP_CORE_TAG, "%s - semphore give failed", __func__);
            }
        }
    }

    return done ? size : 0;
}

uint32_t read_ringbuf(uint8_t *p_buf, uint32_t sz)
{
    // ESP_LOGE(BT_APP_CORE_TAG, "%s", __func__);
    if (!s_ringbuf_i2s_rx) {
        return 0;
    }
    size_t item_size = 0;
    uint8_t *data = xRingbufferReceiveUpTo(s_ringbuf_i2s_rx, &item_size, 0, sz);
    // ESP_LOGE(BT_APP_CORE_TAG, "requested size is %lu size is %zu", sz, item_size);
    if (item_size == sz) {
        memcpy(p_buf, data, item_size);
        vRingbufferReturnItem(s_ringbuf_i2s_rx, data);
        return sz;
    } else if (0 < item_size) {
        vRingbufferReturnItem(s_ringbuf_i2s_rx, data);
        return 0;
    } else {
        // data not enough, do not read
        return 0;
    }
}

// typedef void (* esp_hf_client_incoming_data_cb_t)(const uint8_t *buf, uint32_t len);
// this is our mono data we receive from hfp ag and forward to our i2s tx channel
// this is a bitch, for we need to swap bytes
// https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/i2s.html#std-tx-mode
// for now we just set i2s to 32 bit and we're all good
void bt_app_hf_client_tx_data_cb(const uint8_t *buf, uint32_t len)
{
    // ESP_LOGI(BT_HF_TAG, "%s got: %lu", __func__, len);
    BaseType_t done = pdFALSE;
    // u_int8_t i2s_tx_buff[len];
    // for (int i = 0; i < len; i++)
    // {
    //     i2s_tx_buff[i] = buf[i];
    // }
    // int written = write_ringbuf(i2s_tx_buff, len);
    done = xRingbufferSend(s_ringbuf_i2s_tx, buf, len, (TickType_t)0);
    if (!done) {
        ESP_LOGW(BT_APP_CORE_TAG, "%s - ringbuffer overflowed", __func__);
    }
    if (pdFALSE == xSemaphoreGive(s_i2s_tx_write_semaphore)) {
        ESP_LOGE(BT_APP_CORE_TAG, "%s - semphore give failed", __func__);
    }
}
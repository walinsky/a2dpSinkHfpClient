/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"
#include <xtensa/hal.h>
#include "freertos/FreeRTOSConfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "sys/lock.h"
#include "driver/i2s_std.h"
#include "bt_i2s.h"
#include "bt_app_hf.h"
#include "codec.h"
#include "esp_timer.h"

#define BT_I2S_TAG "BT_I2S"

// Sample rates and bit widths
#define HFP_SAMPLE_RATE 16000
#define HFP_I2S_DATA_BIT_WIDTH I2S_DATA_BIT_WIDTH_16BIT
#define A2DP_STANDARD_SAMPLE_RATE 44100
#define A2DP_I2S_DATA_BIT_WIDTH I2S_DATA_BIT_WIDTH_16BIT

// A2DP ringbuffer watermarks
#define RINGBUF_HIGHEST_WATER_LEVEL (32 * 1024)
#define RINGBUF_PREFETCH_WATER_LEVEL (20 * 1024)

// HFP ringbuffer watermarks
#define RINGBUF_HFP_TX_HIGHEST_WATER_LEVEL (32 * MSBC_FRAME_SAMPLES * 2)
#define RINGBUF_HFP_TX_PREFETCH_WATER_LEVEL (20 * MSBC_FRAME_SAMPLES * 2)
#define RINGBUF_HFP_RX_HIGHEST_WATER_LEVEL (32 * ESP_HF_MSBC_ENCODED_FRAME_SIZE)
#define RINGBUF_HFP_RX_PREFETCH_WATER_LEVEL (20 * ESP_HF_MSBC_ENCODED_FRAME_SIZE)

// Mode switch timeout
#define I2S_MODE_SWITCH_TIMEOUT_MS 2000

// Ringbuffer modes
enum {
    RINGBUFFER_MODE_PROCESSING,  /* ringbuffer is buffering incoming audio data, I2S is working */
    RINGBUFFER_MODE_PREFETCHING, /* ringbuffer is buffering incoming audio data, I2S is waiting */
    RINGBUFFER_MODE_DROPPING     /* ringbuffer is not buffering (dropping) incoming audio data, I2S is working */
};

// I2S RX modes
enum {
    I2S_RX_MODE_NONE, /* i2s rx isn't being used by hfp */
    I2S_RX_MODE_HFP   /* i2s rx is being used by hfp */
};

/*******************************
 * STATIC VARIABLE DEFINITIONS
 ******************************/

// A2DP TX task and ringbuffer
static TaskHandle_t s_bt_i2s_a2dp_tx_task_handle = NULL;
static RingbufHandle_t s_i2s_a2dp_tx_ringbuf = NULL;
static uint16_t s_i2s_a2dp_tx_ringbuffer_mode = RINGBUFFER_MODE_PREFETCHING;
static volatile bool s_bt_i2s_a2dp_tx_task_running = false;

// HFP RX task and ringbuffer (microphone)
static TaskHandle_t s_bt_i2s_hfp_rx_task_handle = NULL;
static bool s_bt_i2s_hfp_rx_task_running = false;
static RingbufHandle_t s_i2s_hfp_rx_ringbuf = NULL;
static SemaphoreHandle_t s_i2s_hfp_rx_ringbuf_delete = NULL;
static uint16_t s_i2s_hfp_rx_ringbuffer_mode = RINGBUFFER_MODE_PREFETCHING;

// HFP TX task and ringbuffer (speaker)
static TaskHandle_t s_bt_i2s_hfp_tx_task_handle = NULL;
static bool s_bt_i2s_hfp_tx_task_running = false;
static RingbufHandle_t s_i2s_hfp_tx_ringbuf = NULL;
static SemaphoreHandle_t s_i2s_hfp_tx_ringbuf_delete = NULL;
static uint16_t s_i2s_hfp_tx_ringbuffer_mode = RINGBUFFER_MODE_PREFETCHING;

// I2S mode management
static i2s_tx_mode_t s_i2s_tx_mode = I2S_TX_MODE_NONE;
static SemaphoreHandle_t s_i2s_tx_semaphore = NULL;
static SemaphoreHandle_t s_i2s_rx_semaphore = NULL;
static SemaphoreHandle_t s_i2s_mode_mutex = NULL;
static SemaphoreHandle_t s_i2s_mode_idle_sem = NULL;

// A2DP SBC decoding pipeline
static RingbufHandle_t s_a2dp_sbc_encoded_ringbuf = NULL;
static TaskHandle_t s_bt_i2s_a2dp_decode_task_hdl = NULL;
static SemaphoreHandle_t s_a2dp_sbc_packet_ready_sem = NULL;
static volatile bool s_bt_i2s_a2dp_decode_task_running = false;

// A2DP SBC packet configuration (set once after audio config)
static uint16_t s_a2dp_sbc_packet_size = 0;
static uint8_t s_a2dp_sbc_frames_per_packet = 0;
static SemaphoreHandle_t s_a2dp_params_ready_sem = NULL;

// Cleanup semaphores for tasks
static SemaphoreHandle_t s_a2dp_decode_task_exit_sem = NULL;
static SemaphoreHandle_t s_a2dp_tx_task_exit_sem = NULL;

// I2S configuration
static int A2DP_SAMPLE_RATE = A2DP_STANDARD_SAMPLE_RATE;
static int A2DP_CH_COUNT = I2S_SLOT_MODE_STEREO;
static bool tx_chan_running = false;
static bool rx_chan_running = false;

// Pin configurations
static I2S_pin_config i2sTxPinConfig = { 26, 17, 25, 0 };
static I2S_pin_config i2sRxPinConfig = { 16, 27, 0, 14 };

// Channel handles
static i2s_chan_handle_t tx_chan = NULL;
static i2s_chan_handle_t rx_chan = NULL;

// HFP RX ringbuffer statistics
static size_t i2s_hfp_rx_ringbuffer_total = 0;
static size_t i2s_hfp_rx_ringbuffer_dropped = 0;
static size_t i2s_hfp_rx_ringbuffer_sent = 0;

// ============================================================================
// FORWARD DECLARATIONS (INTERNAL FUNCTIONS)
// ============================================================================

// I2S low-level control
static void bt_i2s_init_tx_chan(void);
static void bt_i2s_init_rx_chan(void);
static void bt_i2s_tx_channel_enable(void);
static void bt_i2s_tx_channel_disable(void);
static void bt_i2s_rx_channel_enable(void);
static void bt_i2s_rx_channel_disable(void);

// I2S configuration helpers
static i2s_std_clk_config_t bt_i2s_get_hfp_clk_cfg(void);
static i2s_std_slot_config_t bt_i2s_get_hfp_tx_slot_cfg(void);
static i2s_std_clk_config_t bt_i2s_get_adp_clk_cfg(void);
static i2s_std_slot_config_t bt_i2s_get_adp_slot_cfg(void);
static void bt_i2s_channels_config_adp(void);
static void bt_i2s_channels_config_hfp(void);

// Task handlers
static void bt_i2s_a2dp_tx_task_handler(void *arg);
static void bt_i2s_a2dp_decode_task_handler(void *arg);
static void bt_i2s_hfp_tx_task_handler(void *arg);
static void bt_i2s_hfp_rx_task_handler(void *arg);

// Internal data writes
static void bt_i2s_a2dp_write_tx_ringbuf(const uint8_t *data, uint32_t size);
static void bt_i2s_hfp_write_rx_ringbuf(unsigned char *data, uint32_t size);

// HFP task management
static void bt_i2s_hfp_task_init(void);
static void bt_i2s_hfp_task_deinit(void);
static void bt_i2s_hfp_start_internal(void);

// ============================================================================
// PUBLIC API: INITIALIZATION & PIN CONFIGURATION
// ============================================================================

/**
 * @brief Configure TX I2S GPIO pins
 */
void bt_i2s_set_tx_I2S_pins(int bckPin, int wsPin, int doPin, int diPin) {
    i2sTxPinConfig.bck = bckPin;
    i2sTxPinConfig.ws = wsPin;
    i2sTxPinConfig.dout = doPin;
    i2sTxPinConfig.din = diPin;
    ESP_LOGI(BT_I2S_TAG, "setting tx GPIO Pins: BCK: %d WS: %d DOUT: %d DIN: %d", 
             i2sTxPinConfig.bck, i2sTxPinConfig.ws, i2sTxPinConfig.dout, i2sTxPinConfig.din);
}

/**
 * @brief Configure RX I2S GPIO pins
 */
void bt_i2s_set_rx_I2S_pins(int bckPin, int wsPin, int doPin, int diPin) {
    i2sRxPinConfig.bck = bckPin;
    i2sRxPinConfig.ws = wsPin;
    i2sRxPinConfig.dout = doPin;
    i2sRxPinConfig.din = diPin;
    ESP_LOGI(BT_I2S_TAG, "setting rx GPIO Pins: BCK: %d WS: %d DOUT: %d DIN: %d", 
             i2sRxPinConfig.bck, i2sRxPinConfig.ws, i2sRxPinConfig.dout, i2sRxPinConfig.din);
}

/**
 * @brief Initialize I2S driver and create synchronization primitives
 */
void bt_i2s_init() {
    ESP_LOGI(BT_I2S_TAG, "%s", __func__);
    
    // Create mode management primitives
    if (s_i2s_mode_mutex == NULL) {
        s_i2s_mode_mutex = xSemaphoreCreateMutex();
        if (s_i2s_mode_mutex == NULL) {
            ESP_LOGE(BT_I2S_TAG, "Failed to create mode mutex");
        }
    }
    
    if (s_i2s_mode_idle_sem == NULL) {
        s_i2s_mode_idle_sem = xSemaphoreCreateBinary();
        if (s_i2s_mode_idle_sem == NULL) {
            ESP_LOGE(BT_I2S_TAG, "Failed to create idle semaphore");
        } else {
            xSemaphoreGive(s_i2s_mode_idle_sem); // Initially idle
        }
    }
    
    if ((s_i2s_tx_semaphore = xSemaphoreCreateBinary()) == NULL) {
        ESP_LOGE(BT_I2S_TAG, "%s, s_i2s_write_semaphore Semaphore create failed", __func__);
        return;
    }
    
    if ((s_i2s_rx_semaphore = xSemaphoreCreateBinary()) == NULL) {
        ESP_LOGE(BT_I2S_TAG, "%s, s_i2s_read_semaphore Semaphore create failed", __func__);
        return;
    }
    
    if ((s_i2s_hfp_tx_ringbuf_delete = xSemaphoreCreateBinary()) == NULL) {
        ESP_LOGE(BT_I2S_TAG, "%s, s_i2s_hfp_tx_ringbuf_delete Semaphore create failed", __func__);
        return;
    }
    
    if ((s_i2s_hfp_rx_ringbuf_delete = xSemaphoreCreateBinary()) == NULL) {
        ESP_LOGE(BT_I2S_TAG, "%s, s_i2s_hfp_rx_ringbuf_delete Semaphore create failed", __func__);
        return;
    }
    
    // Create cleanup semaphores
    s_a2dp_decode_task_exit_sem = xSemaphoreCreateBinary();
    s_a2dp_tx_task_exit_sem = xSemaphoreCreateBinary();
    
    // Initialize to "not given" (binary sems are 1-count initially)
    xSemaphoreTake(s_a2dp_decode_task_exit_sem, 0);
    xSemaphoreTake(s_a2dp_tx_task_exit_sem, 0);
    
    s_i2s_tx_mode = I2S_TX_MODE_NONE;
    
    bt_i2s_init_tx_chan();
    bt_i2s_init_rx_chan();
}

/**
 * @brief Uninstall I2S driver and free all resources
 */
void bt_i2s_driver_uninstall(void) {
    ESP_LOGI(BT_I2S_TAG, "%s", __func__);
    
    if (tx_chan_running) {
        bt_i2s_tx_channel_disable();
        ESP_ERROR_CHECK(i2s_del_channel(tx_chan));
        ESP_LOGI(BT_I2S_TAG, "tx_chan pointer: %p", tx_chan);
    }
    
    if (rx_chan_running) {
        bt_i2s_rx_channel_disable();
        ESP_ERROR_CHECK(i2s_del_channel(rx_chan));
        ESP_LOGI(BT_I2S_TAG, "rx_chan pointer: %p", rx_chan);
    }
}

// ============================================================================
// INTERNAL: I2S LOW-LEVEL CONFIGURATION
// ============================================================================

/**
 * @brief Get HFP clock configuration
 */
static i2s_std_clk_config_t bt_i2s_get_hfp_clk_cfg(void) {
    i2s_std_clk_config_t hfp_clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(HFP_SAMPLE_RATE);
    ESP_LOGI(BT_I2S_TAG, "reconfiguring hfp clock to sample rate: %d", HFP_SAMPLE_RATE);
    return hfp_clk_cfg;
}

/**
 * @brief Get HFP TX slot configuration
 */
static i2s_std_slot_config_t bt_i2s_get_hfp_tx_slot_cfg(void) {
    i2s_std_slot_config_t hfp_slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(HFP_I2S_DATA_BIT_WIDTH, I2S_SLOT_MODE_MONO);
    hfp_slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    ESP_LOGI(BT_I2S_TAG, "reconfiguring hfp tx slot to data bit width: %d", HFP_I2S_DATA_BIT_WIDTH);
    return hfp_slot_cfg;
}

/**
 * @brief Get A2DP clock configuration
 */
static i2s_std_clk_config_t bt_i2s_get_adp_clk_cfg(void) {
    i2s_std_clk_config_t adp_clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(A2DP_SAMPLE_RATE);
    ESP_LOGI(BT_I2S_TAG, "reconfiguring adp clock to sample rate: %d", A2DP_SAMPLE_RATE);
    return adp_clk_cfg;
}

/**
 * @brief Get A2DP slot configuration
 */
static i2s_std_slot_config_t bt_i2s_get_adp_slot_cfg(void) {
    i2s_std_slot_config_t adp_slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(A2DP_I2S_DATA_BIT_WIDTH, I2S_SLOT_MODE_STEREO);
    ESP_LOGI(BT_I2S_TAG, "reconfiguring adp slot to data bit width: %d", A2DP_I2S_DATA_BIT_WIDTH);
    return adp_slot_cfg;
}

/**
 * @brief Initialize TX I2S channel (for A2DP and HFP output)
 */
static void bt_i2s_init_tx_chan() {
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_new_channel(&tx_chan_cfg, &tx_chan, NULL);
    
    i2s_std_config_t std_tx_cfg = {
        .clk_cfg = bt_i2s_get_adp_clk_cfg(),
        .slot_cfg = bt_i2s_get_adp_slot_cfg(),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = i2sTxPinConfig.bck,
            .ws = i2sTxPinConfig.ws,
            .dout = i2sTxPinConfig.dout,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &std_tx_cfg));
}

/**
 * @brief Initialize RX I2S channel (for HFP microphone input)
 */
static void bt_i2s_init_rx_chan() {
    /* RX channel will be registered on our second I2S */
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan);
    
    // PHILIPS mode with MONO and 32-bit
    i2s_std_config_t std_rx_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(HFP_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = i2sRxPinConfig.bck,
            .ws = i2sRxPinConfig.ws,
            .dout = I2S_GPIO_UNUSED,
            .din = i2sRxPinConfig.din,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &std_rx_cfg));
}

/**
 * @brief Enable TX I2S channel
 */
static void bt_i2s_tx_channel_enable(void) {
    ESP_LOGI(BT_I2S_TAG, "%s", __func__);
    if (!tx_chan_running) {
        ESP_LOGI(BT_I2S_TAG, " -- not running; enabling now");
        ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
    }
    tx_chan_running = true;
}

/**
 * @brief Disable TX I2S channel
 */
static void bt_i2s_tx_channel_disable(void) {
    ESP_LOGI(BT_I2S_TAG, "%s", __func__);
    if (tx_chan_running) {
        ESP_LOGI(BT_I2S_TAG, " -- bt_i2s_tx_channel running; disabling now");
        ESP_ERROR_CHECK(i2s_channel_disable(tx_chan));
    }
    tx_chan_running = false;
}

/**
 * @brief Enable RX I2S channel
 */
static void bt_i2s_rx_channel_enable(void) {
    ESP_LOGI(BT_I2S_TAG, "%s", __func__);
    if (!rx_chan_running) {
        ESP_LOGI(BT_I2S_TAG, " -- not running; enabling now");
        ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
    }
    rx_chan_running = true;
}

/**
 * @brief Disable RX I2S channel
 */
static void bt_i2s_rx_channel_disable(void) {
    ESP_LOGI(BT_I2S_TAG, "%s", __func__);
    if (rx_chan_running) {
        ESP_LOGI(BT_I2S_TAG, " -- bt_i2s_rx_channel running; disabling now");
        ESP_ERROR_CHECK(i2s_channel_disable(rx_chan));
    }
    rx_chan_running = false;
}

/**
 * @brief Reconfigure I2S channels for A2DP mode (44.1kHz stereo)
 */
static void bt_i2s_channels_config_adp(void) {
    bool _isrunning = tx_chan_running;
    i2s_std_clk_config_t clk_cfg = bt_i2s_get_adp_clk_cfg();
    i2s_std_slot_config_t slot_cfg = bt_i2s_get_adp_slot_cfg();
    
    bt_i2s_tx_channel_disable();
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_clock(tx_chan, &clk_cfg));
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_slot(tx_chan, &slot_cfg));
    
    if (_isrunning) {
        bt_i2s_tx_channel_enable();
    }
}

/**
 * @brief Reconfigure I2S channels for HFP mode (16kHz mono)
 */
static void bt_i2s_channels_config_hfp(void) {
    bool _tx_is_running = tx_chan_running;
    i2s_std_clk_config_t clk_cfg = bt_i2s_get_hfp_clk_cfg();
    i2s_std_slot_config_t slot_cfg = bt_i2s_get_hfp_tx_slot_cfg();
    
    bt_i2s_tx_channel_disable();
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_clock(tx_chan, &clk_cfg));
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_slot(tx_chan, &slot_cfg));
    
    if (_tx_is_running) {
        bt_i2s_tx_channel_enable();
    }
}

// ============================================================================
// PUBLIC API: A2DP MODE CONTROL
// ============================================================================

/**
 * @brief Start A2DP audio streaming mode
 */
void bt_i2s_a2dp_start(void) {
    ESP_LOGI(BT_I2S_TAG, "Starting A2DP mode");
    
    // Take mutex to ensure exclusive access
    if (xSemaphoreTake(s_i2s_mode_mutex, pdMS_TO_TICKS(I2S_MODE_SWITCH_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(BT_I2S_TAG, "Failed to acquire mode mutex for A2DP start");
        return;
    }
    
    // Wait until I2S is idle
    if (xSemaphoreTake(s_i2s_mode_idle_sem, pdMS_TO_TICKS(I2S_MODE_SWITCH_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(BT_I2S_TAG, "Failed to wait for idle state for A2DP");
        xSemaphoreGive(s_i2s_mode_mutex);
        return;
    }
    
    /* Create params and packets ready semaphores */
    if (s_a2dp_params_ready_sem == NULL) {
        s_a2dp_params_ready_sem = xSemaphoreCreateBinary();
    }
    
    if (s_a2dp_sbc_packet_ready_sem == NULL) {
        s_a2dp_sbc_packet_ready_sem = xSemaphoreCreateBinary();
    }
    
    /* CRITICAL: Reset exit semaphores to "not given" state */
    xSemaphoreTake(s_a2dp_decode_task_exit_sem, 0);
    xSemaphoreTake(s_a2dp_tx_task_exit_sem, 0);
    
    /* Start decode task handler */
    s_a2dp_sbc_encoded_ringbuf = xRingbufferCreate(8192, RINGBUF_TYPE_BYTEBUF);
    s_bt_i2s_a2dp_decode_task_running = true;
    xTaskCreate(bt_i2s_a2dp_decode_task_handler, "BtI2SA2DPDec", 8192, NULL, configMAX_PRIORITIES - 3, &s_bt_i2s_a2dp_decode_task_hdl);
    ESP_LOGI(BT_I2S_TAG, "✓ A2DP SBC decoder started");
    
    /* start tx task handler */
    s_i2s_a2dp_tx_ringbuffer_mode = RINGBUFFER_MODE_PREFETCHING;
    if ((s_i2s_a2dp_tx_ringbuf = xRingbufferCreate(RINGBUF_HIGHEST_WATER_LEVEL, RINGBUF_TYPE_BYTEBUF)) == NULL) {
        ESP_LOGE(BT_I2S_TAG, "%s, ringbuffer create failed", __func__);
        return;
    }
    
    s_bt_i2s_a2dp_tx_task_running = true;
    xTaskCreate(bt_i2s_a2dp_tx_task_handler, "BtI2Sa2dpTask", 6144, NULL, configMAX_PRIORITIES - 4, &s_bt_i2s_a2dp_tx_task_handle);
    ESP_LOGI(BT_I2S_TAG, "✓ A2DP tx handler started");
    
    // Configure I2S for A2DP
    bt_i2s_channels_config_adp();
    bt_i2s_tx_channel_enable();
    
    // Set mode FIRST
    s_i2s_tx_mode = I2S_TX_MODE_A2DP;
    
    // Release mutex
    xSemaphoreGive(s_i2s_mode_mutex);
    ESP_LOGI(BT_I2S_TAG, "A2DP mode started");
}

/**
 * @brief Stop A2DP audio streaming mode
 */
void bt_i2s_a2dp_stop(void) {
    ESP_LOGI(BT_I2S_TAG, "Stopping A2DP mode");
    
    // Take mutex to ensure exclusive access
    if (xSemaphoreTake(s_i2s_mode_mutex, pdMS_TO_TICKS(I2S_MODE_SWITCH_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(BT_I2S_TAG, "Failed to acquire mode mutex for A2DP stop");
        return;
    }
    
    // This tells TX task to stop and prevents new data
    s_i2s_tx_mode = I2S_TX_MODE_NONE;
    
    // Give it the semaphore to unblock
    if (s_i2s_tx_semaphore != NULL) {
        xSemaphoreGive(s_i2s_tx_semaphore);
    }
    
    // Signal both tasks to exit
    s_bt_i2s_a2dp_decode_task_running = false;
    s_bt_i2s_a2dp_tx_task_running = false;
    
    // Wake both tasks from any blocking calls
    if (s_a2dp_params_ready_sem) {
        xSemaphoreGive(s_a2dp_params_ready_sem);
    }
    
    if (s_a2dp_sbc_packet_ready_sem) {
        xSemaphoreGive(s_a2dp_sbc_packet_ready_sem);
    }
    
    // Wait for both tasks to exit
    if (xSemaphoreTake(s_a2dp_decode_task_exit_sem, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(BT_I2S_TAG, "Failed to acquire a2dp decode task exit semaphore");
    }
    
    if (xSemaphoreTake(s_a2dp_tx_task_exit_sem, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(BT_I2S_TAG, "Failed to acquire a2dp tx task exit semaphore");
    }
    
    /* stop our decoding task, and delete its buffer */
    if (s_bt_i2s_a2dp_decode_task_hdl) {
        s_bt_i2s_a2dp_decode_task_hdl = NULL;
    }
    
    if (s_a2dp_sbc_encoded_ringbuf) {
        vRingbufferDelete(s_a2dp_sbc_encoded_ringbuf);
        s_a2dp_sbc_encoded_ringbuf = NULL;
    }
    
    /* stop our tx task, and delete its buffer */
    if (s_bt_i2s_a2dp_tx_task_handle) {
        s_bt_i2s_a2dp_tx_task_handle = NULL;
    }
    
    if (s_i2s_a2dp_tx_ringbuf) {
        vRingbufferDelete(s_i2s_a2dp_tx_ringbuf);
        s_i2s_a2dp_tx_ringbuf = NULL;
    }
    
    s_i2s_tx_mode = I2S_TX_MODE_NONE;
    bt_i2s_tx_channel_disable();
    
    /* Delete semaphores */
    if (s_a2dp_params_ready_sem != NULL) {
        vSemaphoreDelete(s_a2dp_params_ready_sem);
        s_a2dp_params_ready_sem = NULL;
    }
    
    if (s_a2dp_sbc_packet_ready_sem != NULL) {
        vSemaphoreDelete(s_a2dp_sbc_packet_ready_sem);
        s_a2dp_sbc_packet_ready_sem = NULL;
    }
    
    /* Reset packet params for next A2DP session */
    s_a2dp_sbc_packet_size = 0;
    s_a2dp_sbc_frames_per_packet = 0;
    
    // Signal idle state
    xSemaphoreGive(s_i2s_mode_idle_sem);
    xSemaphoreGive(s_i2s_mode_mutex);
    ESP_LOGI(BT_I2S_TAG, "A2DP mode stopped");
}

/**
 * @brief Set A2DP audio configuration (sample rate and channel count)
 */
void bt_i2s_a2dp_set_audio_config(int sample_rate, int ch_count) {
    A2DP_SAMPLE_RATE = sample_rate;
    A2DP_CH_COUNT = ch_count;
    ESP_LOGI(BT_I2S_TAG, "A2DP audio config set: sample_rate=%d, ch_count=%d", sample_rate, ch_count);
}

/**
 * @brief Set A2DP SBC packet parameters (called once after audio config)
 */
void bt_i2s_a2dp_set_packet_params(uint16_t packet_size, uint8_t frames_per_packet) {
    s_a2dp_sbc_packet_size = packet_size;
    s_a2dp_sbc_frames_per_packet = frames_per_packet;
    ESP_LOGI(BT_I2S_TAG, "A2DP packet params set: size=%d, frames=%d", packet_size, frames_per_packet);
    
    /* Signal that params are ready - decoder can now allocate buffer */
    if (s_a2dp_params_ready_sem != NULL) {
        xSemaphoreGive(s_a2dp_params_ready_sem);
    }
}

/**
 * @brief Write raw SBC encoded data to A2DP decode ringbuffer
 */
void bt_i2s_a2dp_write_sbc_encoded_ringbuf(const uint8_t *data, uint32_t len) {
    if (data == NULL || len == 0 || s_a2dp_sbc_encoded_ringbuf == NULL) {
        return;
    }
    
    /* FAST: Just copy raw SBC frame - BTC callback exits immediately */
    xRingbufferSend(s_a2dp_sbc_encoded_ringbuf, (void *)data, len, 0);
    
    if (s_a2dp_sbc_packet_size > 0 && len == s_a2dp_sbc_packet_size) {
        xSemaphoreGive(s_a2dp_sbc_packet_ready_sem);
    }
}

// ============================================================================
// INTERNAL: A2DP TASKS
// ============================================================================

/**
 * @brief A2DP SBC decoding task - decodes SBC frames and feeds decoded PCM to tx_ringbuffer
 */
static void bt_i2s_a2dp_decode_task_handler(void *arg) {
    ESP_LOGI(BT_I2S_TAG, "A2DP SBC decode task started - waiting for params...");
    
    /* Wait for packet params to be set by first audio callback */
    if (s_a2dp_params_ready_sem == NULL) {
        ESP_LOGE(BT_I2S_TAG, "Params semaphore not created!");
        vTaskDelete(NULL);
        return;
    }
    
    if (xSemaphoreTake(s_a2dp_params_ready_sem, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(BT_I2S_TAG, "Failed to wait for params");
        vTaskDelete(NULL);
        return;
    }
    
    uint8_t *sbc_buffer = (uint8_t *)malloc(s_a2dp_sbc_packet_size);
    if (sbc_buffer == NULL) {
        ESP_LOGE(BT_I2S_TAG, "Failed to allocate SBC buffer");
        vTaskDelete(NULL);
        return;
    }
    
    size_t sbc_buffer_fill = 0;
    uint8_t *sbc_data = NULL;
    size_t sbc_data_len = 0;
    bool decoder_opened = false;
    
    ESP_LOGI(BT_I2S_TAG, "A2DP SBC decode task ready (packet_size=%d)", s_a2dp_sbc_packet_size);
    
    while (s_bt_i2s_a2dp_decode_task_running) {
        if (xSemaphoreTake(s_a2dp_sbc_packet_ready_sem, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        
        sbc_buffer_fill = 0;
        while (sbc_buffer_fill < s_a2dp_sbc_packet_size) {
            /* Try to get remaining bytes - xRingbufferReceiveUpTo handles wrapping */
            sbc_data = xRingbufferReceiveUpTo(s_a2dp_sbc_encoded_ringbuf, &sbc_data_len,
                                               pdMS_TO_TICKS(10), /* SHORT timeout for fragments */
                                               s_a2dp_sbc_packet_size - sbc_buffer_fill);
            
            if (sbc_data == NULL || sbc_data_len == 0) {
                if (sbc_buffer_fill > 0) {
                    /* We got partial data but timed out - keep waiting */
                    continue;
                }
                break; /* No data at all */
            }
            
            /* Copy what we got */
            memcpy(&sbc_buffer[sbc_buffer_fill], sbc_data, sbc_data_len);
            sbc_buffer_fill += sbc_data_len;
            vRingbufferReturnItem(s_a2dp_sbc_encoded_ringbuf, sbc_data);
        }
        
        if (!decoder_opened) {
            if (a2dp_sbc_dec_open(A2DP_SAMPLE_RATE, A2DP_CH_COUNT) == 0) {
                decoder_opened = true;
                ESP_LOGI(BT_I2S_TAG, "✓ A2DP SBC decoder opened");
            } else {
                continue;
            }
        }
        
        /* Decode packet */
        size_t offset = 0;
        while (offset < s_a2dp_sbc_packet_size) {
            uint8_t decoded_pcm[2048];
            size_t decoded_len = 0;
            size_t consumed = 0;
            
            int ret = a2dp_sbc_dec_data(&sbc_buffer[offset],
                                         s_a2dp_sbc_packet_size - offset,
                                         decoded_pcm, &decoded_len, &consumed);
            
            if (ret == 0 && decoded_len > 0) {
                bt_i2s_a2dp_write_tx_ringbuf(decoded_pcm, decoded_len);
            }
            
            if (consumed == 0) break;
            offset += consumed;
        }
    }
    
    if (decoder_opened) {
        a2dp_sbc_dec_close();
    }
    
    free(sbc_buffer);
    xSemaphoreGive(s_a2dp_decode_task_exit_sem);
    ESP_LOGI(BT_I2S_TAG, "%s - exiting gracefully", __func__);
    vTaskDelete(NULL);
}

/**
 * @brief A2DP TX task - fetches decoded PCM from ringbuffer and writes to I2S
 */
static void bt_i2s_a2dp_tx_task_handler(void *arg) {
    uint8_t *data = NULL;
    size_t item_size = 0;
    const size_t item_size_upto = 240 * 6;
    size_t bytes_written = 0;
    
    while (s_bt_i2s_a2dp_tx_task_running) {
        if (pdTRUE == xSemaphoreTake(s_i2s_tx_semaphore, portMAX_DELAY)) {
            for (;;) {
                item_size = 0;
                data = (uint8_t *)xRingbufferReceiveUpTo(s_i2s_a2dp_tx_ringbuf, &item_size, 0, item_size_upto);
                
                if (item_size == 0) {
                    ESP_LOGI(BT_I2S_TAG, "%s - tx ringbuffer underflowed! mode changed: RINGBUFFER_MODE_PREFETCHING", __func__);
                    s_i2s_a2dp_tx_ringbuffer_mode = RINGBUFFER_MODE_PREFETCHING;
                    break;
                }
                
                if (s_i2s_tx_mode == I2S_TX_MODE_A2DP) {
                    i2s_channel_write(tx_chan, data, item_size, &bytes_written, portMAX_DELAY);
                }
                
                vRingbufferReturnItem(s_i2s_a2dp_tx_ringbuf, (void *)data);
            }
        }
    }
    
    xSemaphoreGive(s_a2dp_tx_task_exit_sem);
    ESP_LOGI(BT_I2S_TAG, "%s - exiting gracefully", __func__);
    vTaskDelete(NULL);
}

/**
 * @brief Write decoded A2DP PCM data to TX ringbuffer
 */
static void bt_i2s_a2dp_write_tx_ringbuf(const uint8_t *data, uint32_t size) {
    if (data == NULL || size == 0) {
        return;
    }
    
    size_t item_size = 0;
    if (s_i2s_a2dp_tx_ringbuffer_mode == RINGBUFFER_MODE_DROPPING) {
        ESP_LOGW(BT_I2S_TAG, "%s - ringbuffer is full, drop this packet!", __func__);
        vRingbufferGetInfo(s_i2s_a2dp_tx_ringbuf, NULL, NULL, NULL, NULL, &item_size);
        if (item_size <= RINGBUF_PREFETCH_WATER_LEVEL) {
            ESP_LOGI(BT_I2S_TAG, "%s - ringbuffer data decreased! mode changed: RINGBUFFER_MODE_PROCESSING", __func__);
            s_i2s_a2dp_tx_ringbuffer_mode = RINGBUFFER_MODE_PROCESSING;
        }
        return;
    }
    
    xRingbufferSend(s_i2s_a2dp_tx_ringbuf, (void *)data, size, (TickType_t)0);
    
    if (s_i2s_a2dp_tx_ringbuffer_mode == RINGBUFFER_MODE_PREFETCHING) {
        vRingbufferGetInfo(s_i2s_a2dp_tx_ringbuf, NULL, NULL, NULL, NULL, &item_size);
        if (item_size >= RINGBUF_PREFETCH_WATER_LEVEL) {
            ESP_LOGI(BT_I2S_TAG, "%s - ringbuffer data increased! mode changed: RINGBUFFER_MODE_PROCESSING", __func__);
            s_i2s_a2dp_tx_ringbuffer_mode = RINGBUFFER_MODE_PROCESSING;
            if (pdFALSE == xSemaphoreGive(s_i2s_tx_semaphore)) {
                ESP_LOGE(BT_I2S_TAG, "%s - semphore give failed", __func__);
            }
        }
    }
}

// ============================================================================
// PUBLIC API: HFP MODE CONTROL
// ============================================================================

/**
 * @brief Start HFP audio streaming mode
 */
void bt_i2s_hfp_start(void) {
    if (s_i2s_mode_mutex == NULL) {
        ESP_LOGW(BT_I2S_TAG, "Mode mutex not initialized, starting HFP without synchronization");
        bt_i2s_hfp_start_internal();
        return;
    }
    
    xSemaphoreTake(s_i2s_mode_mutex, portMAX_DELAY);
    
    // Wait for A2DP to stop if active
    if (s_i2s_tx_mode == I2S_TX_MODE_A2DP) {
        ESP_LOGI(BT_I2S_TAG, "HFP start: waiting for A2DP to stop...");
        xSemaphoreGive(s_i2s_mode_mutex);
        
        if (xSemaphoreTake(s_i2s_mode_idle_sem, pdMS_TO_TICKS(I2S_MODE_SWITCH_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGE(BT_I2S_TAG, "Timeout waiting for A2DP to stop");
            return;
        }
        
        xSemaphoreTake(s_i2s_mode_mutex, portMAX_DELAY);
    }
    
    if (s_i2s_tx_mode == I2S_TX_MODE_HFP) {
        ESP_LOGW(BT_I2S_TAG, "HFP already active");
        xSemaphoreGive(s_i2s_mode_mutex);
        return;
    }
    
    ESP_LOGI(BT_I2S_TAG, "Starting HFP mode");
    bt_i2s_hfp_start_internal();
    xSemaphoreGive(s_i2s_mode_mutex);
}

/**
 * @brief Stop HFP audio streaming mode
 */
void bt_i2s_hfp_stop(void) {
    if (s_i2s_mode_mutex == NULL) {
        bt_i2s_hfp_task_deinit();
        return;
    }
    
    xSemaphoreTake(s_i2s_mode_mutex, portMAX_DELAY);
    
    if (s_i2s_tx_mode != I2S_TX_MODE_HFP) {
        ESP_LOGW(BT_I2S_TAG, "HFP not active");
        xSemaphoreGive(s_i2s_mode_mutex);
        return;
    }
    
    ESP_LOGI(BT_I2S_TAG, "Stopping HFP mode");
    bt_i2s_hfp_task_deinit();
    
    // Signal idle
    xSemaphoreGive(s_i2s_mode_idle_sem);
    xSemaphoreGive(s_i2s_mode_mutex);
}

/**
 * @brief Write decoded HFP audio data to TX ringbuffer (speaker output)
 */
void bt_i2s_hfp_write_tx_ringbuf(const uint8_t *data, uint32_t size) {
    if (s_i2s_hfp_tx_ringbuf == NULL) {
        return;
    }
    
    size_t item_size = 0;
    BaseType_t done = pdFALSE;
    
    if (s_i2s_hfp_tx_ringbuffer_mode == RINGBUFFER_MODE_DROPPING) {
        ESP_LOGW(BT_I2S_TAG, "%s - hfp tx ringbuffer is full, drop this packet!", __func__);
        vRingbufferGetInfo(s_i2s_hfp_tx_ringbuf, NULL, NULL, NULL, NULL, &item_size);
        if (item_size <= RINGBUF_HFP_TX_PREFETCH_WATER_LEVEL) {
            ESP_LOGI(BT_I2S_TAG, "%s - hfp tx ringbuffer data decreased! (%d) mode changed: RINGBUFFER_MODE_PROCESSING", __func__, item_size);
            s_i2s_hfp_tx_ringbuffer_mode = RINGBUFFER_MODE_PROCESSING;
        }
        return;
    }
    
    done = xRingbufferSend(s_i2s_hfp_tx_ringbuf, (void *)data, size, (TickType_t)0);
    
    if (!done) {
        ESP_LOGW(BT_I2S_TAG, "%s - hfp tx ringbuffer overflowed, ready to decrease data! mode changed: RINGBUFFER_MODE_DROPPING", __func__);
        s_i2s_hfp_tx_ringbuffer_mode = RINGBUFFER_MODE_DROPPING;
    }
    
    if (s_i2s_hfp_tx_ringbuffer_mode == RINGBUFFER_MODE_PREFETCHING) {
        vRingbufferGetInfo(s_i2s_hfp_tx_ringbuf, NULL, NULL, NULL, NULL, &item_size);
        if (item_size >= RINGBUF_HFP_TX_PREFETCH_WATER_LEVEL) {
            ESP_LOGI(BT_I2S_TAG, "%s - hfp tx ringbuffer data increased! (%d) mode changed: RINGBUFFER_MODE_PROCESSING", __func__, item_size);
            s_i2s_hfp_tx_ringbuffer_mode = RINGBUFFER_MODE_PROCESSING;
        }
    }
}

/**
 * @brief Read encoded HFP audio data from RX ringbuffer (microphone input)
 */
size_t bt_i2s_hfp_read_rx_ringbuf(uint8_t *mic_data) {
    if (!s_i2s_hfp_rx_ringbuf) {
        return 0;
    }
    
    size_t item_size = 0;
    if (s_i2s_hfp_rx_ringbuffer_mode != RINGBUFFER_MODE_PREFETCHING) {
        uint8_t *ringbuf_data = xRingbufferReceiveUpTo(s_i2s_hfp_rx_ringbuf, &item_size, 10000, ESP_HF_MSBC_ENCODED_FRAME_SIZE);
        memcpy(mic_data, ringbuf_data, item_size);
        vRingbufferReturnItem(s_i2s_hfp_rx_ringbuf, (void *)ringbuf_data);
    }
    
    return item_size;
}

// ============================================================================
// INTERNAL: HFP TASK MANAGEMENT
// ============================================================================

/**
 * @brief Start HFP mode internal - opens codec and starts tasks
 */
static void bt_i2s_hfp_start_internal(void) {
    msbc_dec_open();
    if (msbc_enc_open() != 0) {
        ESP_LOGE(BT_I2S_TAG, "Failed to initialize encoder");
    }
    
    bt_i2s_channels_config_hfp();
    bt_i2s_tx_channel_enable();
    bt_i2s_rx_channel_enable();
    bt_i2s_hfp_task_init();
}

/**
 * @brief Initialize HFP tasks and ringbuffers
 */
static void bt_i2s_hfp_task_init(void) {
    s_i2s_hfp_tx_ringbuffer_mode = RINGBUFFER_MODE_PREFETCHING;
    s_i2s_tx_mode = I2S_TX_MODE_HFP;
    
    if ((s_i2s_hfp_tx_ringbuf = xRingbufferCreate(RINGBUF_HFP_TX_HIGHEST_WATER_LEVEL, RINGBUF_TYPE_BYTEBUF)) == NULL) {
        ESP_LOGE(BT_I2S_TAG, "%s, hfp tx ringbuffer create failed", __func__);
        return;
    }
    
    s_bt_i2s_hfp_tx_task_running = true;
    xTaskCreate(bt_i2s_hfp_tx_task_handler, "BtI2ShfpTxTask", 4096, NULL, configMAX_PRIORITIES - 4, &s_bt_i2s_hfp_tx_task_handle);
    
    s_i2s_hfp_rx_ringbuffer_mode = RINGBUFFER_MODE_PREFETCHING;
    
    if ((s_i2s_hfp_rx_ringbuf = xRingbufferCreate(RINGBUF_HFP_RX_HIGHEST_WATER_LEVEL, RINGBUF_TYPE_BYTEBUF)) == NULL) {
        ESP_LOGE(BT_I2S_TAG, "%s, hfp rx ringbuffer create failed", __func__);
        return;
    }
    
    s_bt_i2s_hfp_rx_task_running = true;
    xTaskCreate(bt_i2s_hfp_rx_task_handler, "BtI2ShfpRxTask", 4096, NULL, configMAX_PRIORITIES - 4, &s_bt_i2s_hfp_rx_task_handle);
}

/**
 * @brief Deinitialize HFP tasks and ringbuffers
 */
static void bt_i2s_hfp_task_deinit(void) {
    ESP_LOGI(BT_I2S_TAG, "%s", __func__);
    
    // STEP 1: Unregister audio callback FIRST (prevents new data from arriving)
    esp_hf_client_register_audio_data_callback(NULL);
    
    // STEP 2: Set mode to NONE and stop flags IMMEDIATELY (signals tasks to exit)
    s_i2s_tx_mode = I2S_TX_MODE_NONE;
    s_bt_i2s_hfp_tx_task_running = false;
    s_bt_i2s_hfp_rx_task_running = false;
    
    // STEP 3: Wait briefly for tasks to see the stop flag and exit loops
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // STEP 4: Close codecs (NOW safe - tasks have stopped)
    msbc_dec_close();
    msbc_enc_close();
    
    // STEP 5: Clean up TX task
    if (s_bt_i2s_hfp_tx_task_handle) {
        // Wake up task by sending dummy data to ringbuffer (in case it's blocked)
        if (s_i2s_hfp_tx_ringbuf) {
            uint8_t dummy[1] = {0};
            xRingbufferSend(s_i2s_hfp_tx_ringbuf, dummy, 1, 0);
        }
        
        // Wait for task to exit
        if (pdTRUE == xSemaphoreTake(s_i2s_hfp_tx_ringbuf_delete, pdMS_TO_TICKS(500))) {
            if (s_i2s_hfp_tx_ringbuf) {
                vRingbufferDelete(s_i2s_hfp_tx_ringbuf);
                s_i2s_hfp_tx_ringbuf = NULL;
            }
            s_bt_i2s_hfp_tx_task_handle = NULL;
        } else {
            ESP_LOGW(BT_I2S_TAG, "TX task did not stop in time");
        }
    }
    
    // STEP 6: Clean up RX task
    if (s_bt_i2s_hfp_rx_task_handle) {
        // Wait for task to exit
        if (pdTRUE == xSemaphoreTake(s_i2s_hfp_rx_ringbuf_delete, pdMS_TO_TICKS(500))) {
            if (s_i2s_hfp_rx_ringbuf) {
                vRingbufferDelete(s_i2s_hfp_rx_ringbuf);
                s_i2s_hfp_rx_ringbuf = NULL;
            }
            s_bt_i2s_hfp_rx_task_handle = NULL;
        } else {
            ESP_LOGW(BT_I2S_TAG, "RX task did not stop in time");
        }
    }
    
    // STEP 7: Disable I2S channels
    bt_i2s_tx_channel_disable();
    bt_i2s_rx_channel_disable();
    
    ESP_LOGI(BT_I2S_TAG, "HFP task deinitialized");
}

// ============================================================================
// INTERNAL: HFP TASKS
// ============================================================================

/**
 * @brief HFP TX task - fetches decoded audio from ringbuffer and writes to I2S
 */
static void bt_i2s_hfp_tx_task_handler(void *arg) {
    uint8_t *data = NULL;
    size_t item_size = 0;
    const size_t item_size_upto = MSBC_FRAME_SAMPLES * 2;
    size_t bytes_written = 0;
    
    ESP_LOGI(BT_I2S_TAG, "%s starting", __func__);
    
    while (s_bt_i2s_hfp_tx_task_running && s_i2s_tx_mode == I2S_TX_MODE_HFP) {
        if (s_i2s_hfp_tx_ringbuffer_mode != RINGBUFFER_MODE_PREFETCHING) {
            item_size = 0;
            data = (uint8_t *)xRingbufferReceiveUpTo(s_i2s_hfp_tx_ringbuf, &item_size,
                                                       pdMS_TO_TICKS(100), item_size_upto);
            
            if (item_size == 0 || data == NULL) {
                if (!s_bt_i2s_hfp_tx_task_running || s_i2s_tx_mode != I2S_TX_MODE_HFP) {
                    ESP_LOGI(BT_I2S_TAG, "%s - exiting (no data, task stopping)", __func__);
                    break;
                }
                
                ESP_LOGI(BT_I2S_TAG, "%s - tx ringbuffer underflowed! mode changed: RINGBUFFER_MODE_PREFETCHING", __func__);
                s_i2s_hfp_tx_ringbuffer_mode = RINGBUFFER_MODE_PREFETCHING;
                vTaskDelay(pdMS_TO_TICKS(40));
                continue;
            }
            
            if (!s_bt_i2s_hfp_tx_task_running || s_i2s_tx_mode != I2S_TX_MODE_HFP) {
                vRingbufferReturnItem(s_i2s_hfp_tx_ringbuf, (void *)data);
                ESP_LOGI(BT_I2S_TAG, "%s - exiting (task stopped while processing)", __func__);
                break;
            }
            
            // Byte-swap inline (no temp buffer needed)
            int16_t *send_data = (int16_t *)data;
            for (int i = 0; i < (int)(item_size / 2); i += 2) {
                int16_t temp = send_data[i];
                send_data[i] = send_data[i + 1];
                send_data[i + 1] = temp;
            }
            
            esp_err_t write_ret = i2s_channel_write(tx_chan, send_data, item_size,
                                                      &bytes_written, portMAX_DELAY);
            if (write_ret != ESP_OK) {
                ESP_LOGW(BT_I2S_TAG, "%s - I2S write failed: %d", __func__, write_ret);
            }
            
            vRingbufferReturnItem(s_i2s_hfp_tx_ringbuf, (void *)data);
        } else {
            for (int i = 0; i < 4; i++) {
                if (!s_bt_i2s_hfp_tx_task_running || s_i2s_tx_mode != I2S_TX_MODE_HFP) {
                    ESP_LOGI(BT_I2S_TAG, "%s - exiting (prefetch interrupted)", __func__);
                    goto exit_task;
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
    }
    
exit_task:
    ESP_LOGI(BT_I2S_TAG, "%s - task exiting, giving delete semaphore", __func__);
    xSemaphoreGive(s_i2s_hfp_tx_ringbuf_delete);
    vTaskDelete(NULL);
}

/**
 * @brief HFP RX task - reads microphone data from I2S and encodes to ringbuffer
 */
static void bt_i2s_hfp_rx_task_handler(void *arg) {
    int32_t *i2s_buffer = malloc(MSBC_FRAME_SAMPLES * sizeof(int32_t));
    uint8_t *pcm_buffer = malloc(MSBC_FRAME_SAMPLES * 2);
    uint8_t *encoded_buffer = malloc(ESP_HF_MSBC_ENCODED_FRAME_SIZE);
    
    if (!i2s_buffer || !pcm_buffer || !encoded_buffer) {
        ESP_LOGE(BT_I2S_TAG, "Failed to allocate buffers");
        if (i2s_buffer) free(i2s_buffer);
        if (pcm_buffer) free(pcm_buffer);
        if (encoded_buffer) free(encoded_buffer);
        xSemaphoreGive(s_i2s_hfp_rx_ringbuf_delete);
        vTaskDelete(NULL);
        return;
    }
    
    size_t bytes_read;
    
    while (s_bt_i2s_hfp_rx_task_running) {
        esp_err_t ret = i2s_channel_read(rx_chan, i2s_buffer,
                                           MSBC_FRAME_SAMPLES * sizeof(int32_t),
                                           &bytes_read, portMAX_DELAY);
        
        if (ret != ESP_OK || bytes_read == 0) {
            if (!s_bt_i2s_hfp_rx_task_running) {
                break;
            }
            continue;
        }
        
        // Convert I2S 32-bit to 16-bit PCM
        i2s_32bit_to_16bit_pcm(i2s_buffer, pcm_buffer, MSBC_FRAME_SAMPLES);
        
        // Encode the PCM data
        size_t encoded_len;
        if (msbc_enc_data(pcm_buffer, MSBC_FRAME_SAMPLES * 2,
                          encoded_buffer, &encoded_len) == 0) {
            bt_i2s_hfp_write_rx_ringbuf(encoded_buffer, ESP_HF_MSBC_ENCODED_FRAME_SIZE);
        }
    }
    
    free(i2s_buffer);
    free(pcm_buffer);
    free(encoded_buffer);
    
    xSemaphoreGive(s_i2s_hfp_rx_ringbuf_delete);
    ESP_LOGI(BT_I2S_TAG, "%s, deleting myself", __func__);
    vTaskDelete(NULL);
}

/**
 * @brief Write encoded HFP audio data to RX ringbuffer (internal)
 */
static void bt_i2s_hfp_write_rx_ringbuf(unsigned char *data, uint32_t size) {
    if (!s_i2s_hfp_rx_ringbuf) {
        return;
    }
    
    i2s_hfp_rx_ringbuffer_total += 1;
    
    size_t item_size = 0;
    BaseType_t done = pdFALSE;
    
    if (s_i2s_hfp_rx_ringbuffer_mode == RINGBUFFER_MODE_DROPPING) {
        vRingbufferGetInfo(s_i2s_hfp_rx_ringbuf, NULL, NULL, NULL, NULL, &item_size);
        if (item_size <= RINGBUF_HFP_RX_HIGHEST_WATER_LEVEL) {
            s_i2s_hfp_rx_ringbuffer_mode = RINGBUFFER_MODE_PROCESSING;
        }
        i2s_hfp_rx_ringbuffer_dropped += 1;
        return;
    }
    
    done = xRingbufferSend(s_i2s_hfp_rx_ringbuf, (const char*)data, size, (TickType_t)0);
    
    if (!done) {
        s_i2s_hfp_rx_ringbuffer_mode = RINGBUFFER_MODE_DROPPING;
        i2s_hfp_rx_ringbuffer_dropped += 1;
    } else {
        i2s_hfp_rx_ringbuffer_sent += 1;
    }
    
    if (s_i2s_hfp_rx_ringbuffer_mode == RINGBUFFER_MODE_PREFETCHING) {
        vRingbufferGetInfo(s_i2s_hfp_rx_ringbuf, NULL, NULL, NULL, NULL, &item_size);
        if (item_size >= RINGBUF_HFP_RX_PREFETCH_WATER_LEVEL) {
            s_i2s_hfp_rx_ringbuffer_mode = RINGBUFFER_MODE_PROCESSING;
        }
    }
    
    // Log every 1000 calls
    if (i2s_hfp_rx_ringbuffer_total % 1000 == 0) {
        ESP_LOGI(BT_I2S_TAG, "%s - calls: %d sent: %d dropped: %d",
                 __func__, i2s_hfp_rx_ringbuffer_total, i2s_hfp_rx_ringbuffer_sent, i2s_hfp_rx_ringbuffer_dropped);
    }
}

// ============================================================================
// PUBLIC API: MODE QUERY FUNCTIONS
// ============================================================================

/**
 * @brief Get current I2S TX mode
 */
i2s_tx_mode_t bt_i2s_get_tx_mode(void) {
    return s_i2s_tx_mode;
}

/**
 * @brief Check if HFP mode is currently active
 */
bool bt_i2s_is_hfp_mode(void) {
    return (s_i2s_tx_mode == I2S_TX_MODE_HFP);
}

/**
 * @brief Check if A2DP mode is currently active
 */
bool bt_i2s_is_a2dp_mode(void) {
    return (s_i2s_tx_mode == I2S_TX_MODE_A2DP);
}

/**
 * @brief Get TX I2S channel handle
 */
i2s_chan_handle_t bt_i2s_get_tx_chan(void) {
    return tx_chan;
}

/**
 * @brief Get RX I2S channel handle
 */
i2s_chan_handle_t bt_i2s_get_rx_chan(void) {
    return rx_chan;
}

/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include "esp_log.h"
#include "esp_a2dp_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "a2dpSink.h"
#include "bt_i2s.h"
#include "codec.h"

#define A2DP_SINK_TAG "A2DP_SINK"

/* Local state tracking */
static bool s_a2dp_connected = false;
static bool s_audio_stream_active = false;
static bool s_audio_data_params_set = false;

static const char *s_a2d_conn_state_str[] = {
    "Disconnected", "Connecting", "Connected", "Disconnecting"
};

static const char *s_a2d_audio_state_str[] = {
    "Suspended", "Started"
};

/**
 * @brief Handle A2DP connection state changes
 */
static void bt_app_a2dp_conn_state_handler(esp_a2d_cb_param_t *param)
{
    uint8_t *bda = param->conn_stat.remote_bda;
    
    ESP_LOGI(A2DP_SINK_TAG, "A2DP connection state: %s, [%02x:%02x:%02x:%02x:%02x:%02x]",
             s_a2d_conn_state_str[param->conn_stat.state],
             bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);

    switch (param->conn_stat.state) {
    case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
        s_a2dp_connected = false;
        s_audio_stream_active = false;
        break;

    case ESP_A2D_CONNECTION_STATE_CONNECTING:
        ESP_LOGI(A2DP_SINK_TAG, "A2DP connecting...");
        break;

    case ESP_A2D_CONNECTION_STATE_CONNECTED:
        s_a2dp_connected = true;
        // bt_i2s_a2dp_task_init();
        ESP_LOGI(A2DP_SINK_TAG, "✓ A2DP connected from: %02x:%02x:%02x:%02x:%02x:%02x",
                 bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
        break;

    case ESP_A2D_CONNECTION_STATE_DISCONNECTING:
        ESP_LOGI(A2DP_SINK_TAG, "A2DP disconnecting...");
        break;

    default:
        break;
    }
}

/**
 * @brief Handle A2DP audio codec configuration
 */
static void bt_app_a2dp_audio_cfg_handler(esp_a2d_cb_param_t *param)
{
    esp_a2d_mcc_t *p_mcc = &param->audio_cfg.mcc;

    ESP_LOGI(A2DP_SINK_TAG, "A2DP audio stream configuration, codec type: %d", p_mcc->type);

    /* For now only SBC stream is supported */
    if (p_mcc->type == ESP_A2D_MCT_SBC) {
        int sample_rate = 16000;
        int ch_count = 2;

        /* Parse sample rate */
        if (p_mcc->cie.sbc_info.samp_freq & ESP_A2D_SBC_CIE_SF_32K) {
            sample_rate = 32000;
        } else if (p_mcc->cie.sbc_info.samp_freq & ESP_A2D_SBC_CIE_SF_44K) {
            sample_rate = 44100;
        } else if (p_mcc->cie.sbc_info.samp_freq & ESP_A2D_SBC_CIE_SF_48K) {
            sample_rate = 48000;
        } else if (p_mcc->cie.sbc_info.samp_freq & ESP_A2D_SBC_CIE_SF_16K) {
            sample_rate = 16000;
        }

        /* Parse channel count */
        if (p_mcc->cie.sbc_info.ch_mode & ESP_A2D_SBC_CIE_CH_MODE_MONO) {
            ch_count = 1;
        }

        bt_i2s_tx_channel_reconfig_clock_slot(sample_rate, ch_count);
        
        ESP_LOGI(A2DP_SINK_TAG, "Audio codec configured:");
        ESP_LOGI(A2DP_SINK_TAG, "  Sample rate: %d Hz", sample_rate);
        ESP_LOGI(A2DP_SINK_TAG, "  Channels: %d", ch_count);
        ESP_LOGI(A2DP_SINK_TAG, "  Block len: %d", p_mcc->cie.sbc_info.block_len);
        ESP_LOGI(A2DP_SINK_TAG, "  Subbands: %d", p_mcc->cie.sbc_info.num_subbands);
        ESP_LOGI(A2DP_SINK_TAG, "  Bitpool: %d-%d", 
                 p_mcc->cie.sbc_info.min_bitpool,
                 p_mcc->cie.sbc_info.max_bitpool);
    }
}

/**
 * @brief Handle A2DP audio state changes - open decoder when stream starts
 */
static void bt_app_a2dp_audio_state_handler(esp_a2d_cb_param_t *param)
{
    ESP_LOGI(A2DP_SINK_TAG, "A2DP audio state: %s",
             s_a2d_audio_state_str[param->audio_stat.state]);

    if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED) {
        s_audio_stream_active = true;
        s_audio_data_params_set = false;
        bt_i2s_a2dp_start();
        ESP_LOGI(A2DP_SINK_TAG, "✓ A2DP audio stream started");
        
    } else if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_SUSPEND) {
        s_audio_stream_active = false;
        bt_i2s_a2dp_stop();
        ESP_LOGI(A2DP_SINK_TAG, "A2DP audio stream stopped");
    }
}


void bt_app_a2d_audio_data_cb(uint16_t conn_hdl, esp_a2d_audio_buff_t *audio_buf)
{
    if (audio_buf == NULL || !s_audio_stream_active || 
        audio_buf->data == NULL || audio_buf->data_len == 0) {
        if (audio_buf) esp_a2d_audio_buff_free(audio_buf);
        return;
    }

    /* Set packet params from FIRST audio buffer (auto-detect) */
    if (!s_audio_data_params_set) {
        bt_i2s_a2dp_set_packet_params(audio_buf->data_len, audio_buf->number_frame);
        s_audio_data_params_set = true;
    }

    bt_i2s_a2dp_write_sbc_encoded_ringbuf(audio_buf->data, audio_buf->data_len);
    esp_a2d_audio_buff_free(audio_buf);
}

/**
 * @brief Main A2DP event callback (NO audio data handling here)
 */
void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    if (param == NULL) {
        ESP_LOGE(A2DP_SINK_TAG, "A2DP callback: NULL param");
        return;
    }

    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
        bt_app_a2dp_conn_state_handler(param);
        break;

    case ESP_A2D_AUDIO_STATE_EVT:
        bt_app_a2dp_audio_state_handler(param);
        break;

    case ESP_A2D_AUDIO_CFG_EVT:
        bt_app_a2dp_audio_cfg_handler(param);
        break;

    case ESP_A2D_PROF_STATE_EVT:
        if (ESP_A2D_INIT_SUCCESS == param->a2d_prof_stat.init_state) {
            ESP_LOGI(A2DP_SINK_TAG, "A2DP PROF STATE: Init Complete");
        } else {
            ESP_LOGI(A2DP_SINK_TAG, "A2DP PROF STATE: Deinit Complete");
        }
        break;

    case ESP_A2D_SEP_REG_STATE_EVT:
        if (param->a2d_sep_reg_stat.reg_state == ESP_A2D_SEP_REG_SUCCESS) {
            ESP_LOGI(A2DP_SINK_TAG, "A2DP register SEP success, seid: %d",
                     param->a2d_sep_reg_stat.seid);
        } else {
            ESP_LOGE(A2DP_SINK_TAG, "A2DP register SEP fail, seid: %d, state: %d",
                     param->a2d_sep_reg_stat.seid, param->a2d_sep_reg_stat.reg_state);
        }
        break;

    case ESP_A2D_SNK_PSC_CFG_EVT:
        ESP_LOGI(A2DP_SINK_TAG, "A2DP PSC configured: 0x%x", param->a2d_psc_cfg_stat.psc_mask);
        break;

    default:
        ESP_LOGD(A2DP_SINK_TAG, "Unhandled A2DP event: %d", event);
        break;
    }
}

/**
 * @brief Initialize A2DP Sink
 */
esp_err_t a2dp_sink_init(void)
{
    esp_err_t ret;

    /* Initialize A2DP sink */
    ret = esp_a2d_sink_init();
    if (ret != ESP_OK) {
        ESP_LOGE(A2DP_SINK_TAG, "Failed to init A2DP sink: %d", ret);
        return ret;
    }

    /* Register SEP (Stream Endpoint) BEFORE callback - this may be critical */
    esp_a2d_mcc_t mcc = {0};
    mcc.type = ESP_A2D_MCT_SBC;
    mcc.cie.sbc_info.samp_freq = ESP_A2D_SBC_CIE_SF_44K | ESP_A2D_SBC_CIE_SF_48K | 
                                  ESP_A2D_SBC_CIE_SF_32K | ESP_A2D_SBC_CIE_SF_16K;
    mcc.cie.sbc_info.ch_mode = ESP_A2D_SBC_CIE_CH_MODE_MONO | ESP_A2D_SBC_CIE_CH_MODE_STEREO |
                               ESP_A2D_SBC_CIE_CH_MODE_DUAL_CHANNEL | ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO;
    mcc.cie.sbc_info.block_len = ESP_A2D_SBC_CIE_BLOCK_LEN_4 | ESP_A2D_SBC_CIE_BLOCK_LEN_8 | 
                                 ESP_A2D_SBC_CIE_BLOCK_LEN_12 | ESP_A2D_SBC_CIE_BLOCK_LEN_16;
    mcc.cie.sbc_info.num_subbands = ESP_A2D_SBC_CIE_NUM_SUBBANDS_4 | ESP_A2D_SBC_CIE_NUM_SUBBANDS_8;
    mcc.cie.sbc_info.alloc_mthd = ESP_A2D_SBC_CIE_ALLOC_MTHD_SRN | ESP_A2D_SBC_CIE_ALLOC_MTHD_LOUDNESS;
    mcc.cie.sbc_info.min_bitpool = 2;
    mcc.cie.sbc_info.max_bitpool = 53;
    
    ret = esp_a2d_sink_register_stream_endpoint(0, &mcc);
    if (ret != ESP_OK) {
        ESP_LOGE(A2DP_SINK_TAG, "Failed to register SEP: %d", ret);
        return ret;
    }
    ESP_LOGI(A2DP_SINK_TAG, "A2DP SBC SEP registered");

    /* Register audio data callback - AFTER SEP */
    ret = esp_a2d_sink_register_audio_data_callback(&bt_app_a2d_audio_data_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(A2DP_SINK_TAG, "Failed to register audio data callback: %d", ret);
        return ret;
    }
    ESP_LOGI(A2DP_SINK_TAG, "Audio data callback registered");

    /* Register A2DP event callback - LAST */
    ret = esp_a2d_register_callback(&bt_app_a2d_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(A2DP_SINK_TAG, "Failed to register A2DP callback: %d", ret);
        return ret;
    }

    ESP_LOGI(A2DP_SINK_TAG, "A2DP sink initialized successfully");
    return ESP_OK;
}

 /**
 * @brief Deinitialize A2DP Sink
 */
esp_err_t a2dp_sink_deinit(void)
{
    return esp_a2d_sink_deinit();
}

/* ============================================
 * AVRC (Audio/Video Remote Control) Support
 * (Placeholders for future implementation)
 * ============================================ */

void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    ESP_LOGD(A2DP_SINK_TAG, "AVRC CT event: %d", event);
    /* TODO: Implement AVRC Controller support */
}

void bt_app_rc_tg_cb(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param)
{
    ESP_LOGD(A2DP_SINK_TAG, "AVRC TG event: %d", event);
    /* TODO: Implement AVRC Target support */
}

/**
 * @brief Check if A2DP is connected
 */
bool a2dp_sink_is_connected(void)
{
    return s_a2dp_connected;
}
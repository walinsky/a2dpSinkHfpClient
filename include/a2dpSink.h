#ifndef __A2DP_SINK_H__
#define __A2DP_SINK_H__

#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A2DP initialization */
esp_err_t a2dp_sink_init(void);
esp_err_t a2dp_sink_deinit(void);

/* A2DP callback handlers */
void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);

/* Audio data callbacks - one or the other based on CONFIG_EXAMPLE_A2DP_SINK_USE_EXTERNAL_CODEC */
void bt_app_a2d_data_cb(const uint8_t *data, uint32_t len);
void bt_app_a2d_audio_data_cb(esp_a2d_conn_hdl_t conn_hdl, esp_a2d_audio_buff_t *audio_buf);

/* AVRC support (for later) */
void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param);
void bt_app_rc_tg_cb(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param);

/* Connection state check */
bool a2dp_sink_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* __A2DP_SINK_H__ */

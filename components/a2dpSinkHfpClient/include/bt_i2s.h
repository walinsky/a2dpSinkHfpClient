#ifndef __BT_I2S_H__
#define __BT_I2S_H__


#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "driver/i2s_std.h"
#include "esp_audio_dec.h"
#include "esp_audio_enc.h"
#include "esp_hf_defs.h"


typedef struct I2S_pin_config
{
    int bck; // GPIO number to use for I2S BCK Driver.
    int ws;   // GPIO number to use for I2S LRCK(WS) Driver.
    int dout; // GPIO number to use for I2S Data out.
    int din;  // GPIO number to use for I2S Data in.
} I2S_pin_config;

// I2S TX mode enumeration
typedef enum {
    I2S_TX_MODE_NONE = 0,
    I2S_TX_MODE_A2DP,
    I2S_TX_MODE_HFP,
} i2s_tx_mode_t;

void bt_i2s_init();
void bt_i2s_set_tx_I2S_pins(int bckPin, int wsPin, int doPin, int diPin);
void bt_i2s_set_rx_I2S_pins(int bckPin, int wsPin, int doPin, int diPin);

void bt_i2s_init_tx_chan();
void bt_i2s_init_rx_chan();
// void bt_i2s_driver_install(void);
void bt_i2s_driver_uninstall(void);
void bt_i2s_channels_disable(void);
void bt_i2s_tx_channel_enable(void);
void bt_i2s_tx_channel_disable(void);
void bt_i2s_rx_channel_enable(void);
void bt_i2s_rx_channel_disable(void);
void bt_i2s_tx_channel_reconfig_clock_slot(int sample_rate, int ch_count);
void bt_i2s_tx_channel_reconfig_clock_slot_default(void);
void bt_i2s_channels_config_adp(void);
void bt_i2s_channels_config_hfp(void);

void bt_i2s_a2dp_tx_task_handler(void *arg);
void bt_i2s_a2dp_task_init(void);
void bt_i2s_a2dp_task_deinit(void);
void bt_i2s_a2dp_task_start_up(void);
void bt_i2s_a2dp_task_shut_down(void);
void bt_i2s_a2dp_set_packet_params(uint16_t packet_size, uint8_t frames_per_packet);
void bt_i2s_a2dp_write_sbc_encoded_ringbuf(const uint8_t *data, uint32_t len);
void bt_i2s_a2dp_write_tx_ringbuf(const uint8_t *data, uint32_t size);

void bt_i2s_hfp_tx_task_handler(void *arg);
void bt_i2s_hfp_rx_task_handler(void *arg);
void bt_i2s_hfp_write_tx_ringbuf(const uint8_t *data, uint32_t size);
void bt_i2s_hfp_write_rx_ringbuf(unsigned char *data, uint32_t size);
// void bt_i2s_hfp_read_rx_ringbuf(esp_hf_audio_buff_t *mic_data);
size_t bt_i2s_hfp_read_rx_ringbuf(uint8_t *mic_data);
// Mode management wrappers
void bt_i2s_a2dp_start(void);
void bt_i2s_a2dp_stop(void);
void bt_i2s_hfp_start(void);
void bt_i2s_hfp_stop(void);

// Mode checking
i2s_tx_mode_t bt_i2s_get_tx_mode(void);
bool bt_i2s_is_hfp_mode(void);
bool bt_i2s_is_a2dp_mode(void);


#ifdef __cplusplus
}
#endif

#endif /* __BT_I2S_H__*/
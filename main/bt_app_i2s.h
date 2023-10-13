#ifndef __BT_APP_I2S_H__
#define __BT_APP_I2S_H__

#include <stdint.h>
#include "bt_app_core.h"
// #include "bt_app_i2s.h"
#include "driver/i2s_std.h"

#define BT_APP_I2S_TAG "BT_I2S"

typedef struct I2S_pin_config
{
    int bck; // GPIO number to use for I2S BCK Driver.
    int ws;   // GPIO number to use for I2S LRCK(WS) Driver.
    int dout; // GPIO number to use for I2S Data out.
    int din;  // GPIO number to use for I2S Data in.
} I2S_pin_config;

void bt_i2s_set_tx_I2S_pins(int bckPin, int wsPin, int doPin, int diPin);
void bt_i2s_set_rx_I2S_pins(int bckPin, int wsPin, int doPin, int diPin);


void bt_i2s_driver_install(void);
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

#endif /* __BT_APP_I2S_H__*/
#include "BluetoothA2DPSink.h"
#include "bt_i2s.h"
#include "bt_hfp.h"

BluetoothA2DPSink a2dp_sink;

void setup(){
    bt_i2s_set_tx_I2S_pins( 26, 17, 25, 0 );
    bt_i2s_set_rx_I2S_pins( 16, 27, 0, 14 );
    bt_i2s_init();
    // set our GAP to disable SSP(secure simple pairing) enabling legacy pairing for Classic Bluetooth
    a2dp_sink.set_bluedroid_config_t( {.ssp_en = false,});
    a2dp_sink.set_stream_reader(bt_i2s_a2dp_write_tx_ringbuf, false); // false for we are disabling I2S 
    a2dp_sink.set_avrc_metadata_attribute_mask(ESP_AVRC_MD_ATTR_TITLE);
    a2dp_sink.start("MyMusic");  
}

void loop(){
   a2dp_sink.delay_ms( 500 ); // or use vTaskDelay()
}


extern "C" void app_main(void){
    setup();
    while(true){
        loop();
    }
}
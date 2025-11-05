idf_component_register(
    SRCS "src/a2dpSinkHfpHf.c"
         "src/a2dpSink.c"
         "src/bt_gap.c"
         "src/phonebook.c"
         "src/codec.c"
         "src/ringtone.c"
         "src/bt_i2s.c"
         "src/app_hf_msg_set.c"
         "src/bt_app_core.c"
         "src/bt_app_hf.c"
         "src/bt_app_pbac.c"

    INCLUDE_DIRS "include"

    PRIV_REQUIRES bt 
                  nvs_flash 
                  esp_driver_gpio 
                  esp_driver_i2s
                  console 
                  esp_ringbuf  
                  spiffs 
                  vfs
                  esp_audio_codec
)

# Ensure proper linking with encoder
target_link_libraries(${COMPONENT_LIB} PUBLIC esp_audio_codec)

# Debug: Check what flags are being used
message(STATUS "Component compile flags: ${COMPONENT_COMPILE_FLAGS}")
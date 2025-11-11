idf_component_register(
     SRCS 
          "src/a2dpSinkHfpHf.c"
          "src/a2dpSink.c"
          "src/bt_app_avrc.c" 
          "src/bt_gap.c"
          "src/phonebook.c"
          "src/codec.c"
          "src/ringtone.c"
          "src/bt_i2s.c"
          "src/app_hf_msg_set.c"
          "src/bt_app_hf.c"
          "src/bt_app_pbac.c"

     INCLUDE_DIRS
          "include"

     PRIV_REQUIRES
          bt 
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

message(STATUS "cmake source dir: ${CMAKE_SOURCE_DIR} cmake current source dir ${CMAKE_CURRENT_SOURCE_DIR}")

# Copy component configuration files to project root
# This allows the build system to find them without requiring users to move them
# Copy sdkconfig.defaults from component to project root if it doesn't exist
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/sdkconfig.defaults")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults")
        file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults" DESTINATION "${CMAKE_SOURCE_DIR}")
        message(STATUS "Copied sdkconfig.defaults from component")
    endif()
endif()

# Copy partitions.csv from component to project root if it doesn't exist
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/partitions.csv")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/partitions.csv")
        file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/partitions.csv" DESTINATION "${CMAKE_SOURCE_DIR}")
        message(STATUS "Copied partitions.csv from component")
    endif()
endif()
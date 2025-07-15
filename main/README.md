ESP32-A2DP is included as a component.
If you ever decide to use a newer version; no problem.
Just remember to edit it's CMakeLists.txt so it has no references to arduino-audio-tools like so
```
# -- CMAKE for Espressif IDF
# -- author Phil Schatzmann
# -- copyright GPLv3

idf_component_register(
    SRC_DIRS src
    INCLUDE_DIRS src 
    REQUIRES bt esp_common freertos hal log nvs_flash driver
)

target_compile_options(${COMPONENT_LIB} PUBLIC -DA2DP_LEGACY_I2S_SUPPORT=0 -DA2DP_I2S_AUDIOTOOLS=0 -DESP32_CMAKE=1 -Wno-error -Wno-format -fpermissive)
```
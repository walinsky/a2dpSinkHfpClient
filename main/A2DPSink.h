#ifndef A2DPSINK_H
#define A2DPSINK_H

#include <esp_a2dp_api.h>
#include <string>

class A2DPSink {
public:
    A2DPSink();
    ~A2DPSink();

    bool initialize();
    void stop();

private:
    static void eventCallback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t* param);
    static void audioDataCallback(const uint8_t* data, uint32_t len);

    std::string deviceName;
    bool initialized;
};

#endif

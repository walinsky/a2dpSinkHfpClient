#ifndef HFPCLIENT_H
#define HFPCLIENT_H

#include <esp_hf_client_api.h>
#include <string>

class HFPClient {
public:
    HFPClient();
    ~HFPClient();

    bool initialize();
    void connect(const std::string& remoteAddress);
    void disconnect();

private:
    static void eventCallback(esp_hf_client_cb_event_t event, esp_hf_client_cb_param_t* param);

    bool initialized;
    bool connected;
};

#endif // HFPCLIENT_H

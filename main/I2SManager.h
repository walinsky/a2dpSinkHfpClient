#ifndef I2SMANAGER_H
#define I2SMANAGER_H

#include "driver/i2s_std.h"

class I2SManager {
public:
    I2SManager();
    ~I2SManager();
    bool initialize();
    void configureForA2DP();
    void configureForHFP();
    void deinitialize();

private:
    bool initialized;
    void deinitializeChannel(i2s_chan_handle_t handle);
    void init_tx_chan();
    void init_rx_chan();
};

#endif // I2SMANAGER_H

#ifndef BLUETOOTHMANAGER_H
#define BLUETOOTHMANAGER_H

#include <string>

class BluetoothManager {
public:
    static BluetoothManager& getInstance(); // Singleton

    bool initialize();
    void deinitialize();

private:
    BluetoothManager();
    ~BluetoothManager();
    BluetoothManager(const BluetoothManager&) = delete;
    BluetoothManager& operator=(const BluetoothManager&) = delete;

    bool initialized;
};

#endif

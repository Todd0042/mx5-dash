#pragma once

#include "ObdSource.h"
#include "VehicleData.h"
#include <mutex>
#include <cstring>

class AndroidObdSource : public ObdSource {
public:
    AndroidObdSource() {
        memset(&data_, 0, sizeof(data_));
        data_.gear = 'N';
        data_.connected = false;
    }

    void snapshot(VehicleData& out) override {
        std::lock_guard<std::mutex> lock(mutex_);
        out = data_;
    }

    bool connected() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_.connected;
    }

    void update(const VehicleData& in) {
        std::lock_guard<std::mutex> lock(mutex_);
        data_ = in;
    }

    VehicleData& getDirect() {
        return data_;
    }

    std::mutex& getMutex() {
        return mutex_;
    }

private:
    VehicleData data_;
    mutable std::mutex mutex_;
};

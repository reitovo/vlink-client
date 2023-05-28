//
// Created by reito on 2023/3/21.
//

#include "speed.h"
#include "util.h"

void SpeedStat::update(size_t value) {
    int64_t time = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

    if (lastTime == 0) {
        lastTime = time;
        bytesPerSecond = 0;
        return;
    }

    auto deltaTime = time - lastTime;
    lastTime = time;

    if (deltaTime == 0 || value < lastValue) {
        lastValue = value;
        bytesPerSecond = 0;
        return;
    }

    auto deltaValue = value - lastValue;
    lastValue = value;

    bytesPerSecond = (size_t) ((double) deltaValue / ((double) deltaTime / 1000000.0));
}

QString SpeedStat::speed() {
    return QString("%1/s").arg(humanizeBytes(bytesPerSecond));
}

size_t SpeedStat::speedBytes() {
    return bytesPerSecond;
}

//
// Created by reito on 2023/3/21.
//

#ifndef VTSLINK_SPEED_H
#define VTSLINK_SPEED_H

#include "QString"

class Speed {
    size_t lastValue = 0;
    int64_t lastTime = 0;
    size_t bytesPerSecond = 0;

public:
    void update(size_t value);
    QString speed() const;
};

#endif //VTSLINK_SPEED_H

#ifndef BLDC_DRIVER_H
#define BLDC_DRIVER_H

#include <Arduino.h>

/**
 * @class BldcDriver
 * @brief Quản lý tín hiệu PWM cho từng động cơ riêng biệt.
 * Chỉ quan tâm độ lớn (Speed), không quan tâm chiều quay (do RoverSide quản lý).
 */
class BldcDriver {
private:
    uint8_t _pwmPin;    // Chân GPIO xuất xung
    uint8_t _channel;   // Kênh LEDC (0-15) của ESP32

public:
    BldcDriver(uint8_t pwmPin, uint8_t channel);
    void begin();
    
    // Xuất xung PWM (0-255) ra mạch lọc RC -> 0-5V Analog
    void setThrottle(uint8_t duty);
};

#endif
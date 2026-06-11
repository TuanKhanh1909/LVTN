#ifndef BLDC_DRIVER_H
#define BLDC_DRIVER_H

#include <Arduino.h>

/**
 * @class BldcDriver
 * @brief Quản lý tín hiệu PWM cho từng động cơ riêng biệt.
 * Chỉ quan tâm độ lớn (Speed), không quan tâm chiều quay (do RoverSide quản lý).
 */
class BldcDriver
{
private:
    uint8_t _pwmPin;  // Chân GPIO xuất xung
    uint8_t _channel; // Kênh LEDC (0-15) của ESP32
    float _trim = 1.0; // Hệ số hiệu chỉnh (Trim) cho từng động cơ nếu cần
public:
    BldcDriver(uint8_t pwmPin, uint8_t channel);
    void begin();

    void setTrim(float trim); // Cho phép điều chỉnh Trim nếu cần
    float getTrim();          // Lấy hệ số hiện tại
    void setThrottle(int pwm);// Nhận trực tiếp PWM từ PID tính ra

};

#endif
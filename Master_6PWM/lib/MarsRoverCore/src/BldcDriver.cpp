#include "BldcDriver.h"

BldcDriver::BldcDriver(uint8_t pwmPin, uint8_t channel)
{
    _pwmPin = pwmPin;
    _channel = channel;
    _trim = 1.0; // Mặc định là 100%
}

void BldcDriver::setTrim(float trimValue)
{
    // Giới hạn trong khoảng 0.0 đến 1.0 để an toàn
    if (trimValue < 0)
        _trim = 0;
    else if (trimValue > 1.0)
        _trim = 1.0;
    else
        _trim = trimValue;
}

void BldcDriver::begin()
{
    // Cấu hình PWM: Tần số 5KHz, Độ phân giải 8-bit (0-255)
    // Tần số này phù hợp với mạch lọc RC R=15k, C=220nF trong báo cáo
    ledcSetup(_channel, 5000, 8);
    ledcAttachPin(_pwmPin, _channel);
    setThrottle(0); // Khởi động ở trạng thái dừng
}

void BldcDriver::setThrottle(int pwm)
{
    // Nhận PWM với hệ số Trim trước khi xuất ra
    // Ví dụ: Lệnh 200, trim 0.9 -> chỉ xuất 180
    int adjustedPwm = pwm * _trim;

    int finalPwm = 255 - abs(adjustedPwm); // Logic ngược cho NPN

    finalPwm = constrain(finalPwm, 0, 255);
    ledcWrite(_channel, finalPwm);
}
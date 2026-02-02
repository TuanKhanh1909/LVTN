#include "BldcDriver.h"

BldcDriver::BldcDriver(uint8_t pwmPin, uint8_t channel) 
    : _pwmPin(pwmPin), _channel(channel) {}

void BldcDriver::begin() {
    // Cấu hình PWM: Tần số 5KHz, Độ phân giải 8-bit (0-255)
    // Tần số này phù hợp với mạch lọc RC R=15k, C=220nF trong báo cáo
    ledcSetup(_channel, 5000, 8); 
    ledcAttachPin(_pwmPin, _channel);
    setThrottle(0); // Khởi động ở trạng thái dừng
}

void BldcDriver::setThrottle(uint8_t duty) {
    // Giới hạn an toàn (Clamp) để tránh lỗi phần cứng
    if (duty > 255) duty = 255;
    ledcWrite(_channel, duty);
}
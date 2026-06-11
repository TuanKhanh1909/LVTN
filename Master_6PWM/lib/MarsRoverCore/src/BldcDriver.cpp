#include "BldcDriver.h"

BldcDriver::BldcDriver(uint8_t pwmPin, uint8_t channel)
{
    _pwmPin = pwmPin;
    _channel = channel;
    _trim = 1.0f; // Mặc định không hiệu chỉnh
}

void BldcDriver::begin()
{
    #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcAttach(_pwmPin, 5000, 8); // Thư viện mới dùng Pin
#else
    ledcSetup(_channel, 10000, 8); // Thư viện cũ dùng Channel
    ledcAttachPin(_pwmPin, _channel);
#endif
    setThrottle(0);
}

void BldcDriver::setTrim(float trim)
{
    _trim = constrain(trim, 0.0f, 1.0f); // Giới hạn Trim từ 0.0 đến 1.0
}
float BldcDriver::getTrim()
{
    return _trim;
}
void BldcDriver::setThrottle(int pwm)
{
    // Áp dụng hệ số Trim để giảm công suất nếu cần
    int trimmedPwm = (int)(abs(pwm) * _trim);   
    int finalPwm = 255 - abs(trimmedPwm); // Logic ngược cho NPN
    finalPwm = constrain(finalPwm, 0, 255);
   
   // ledcWrite(_channel, finalPwm);

    #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcWrite(_pwmPin, finalPwm); // <--- LỖI NẰM Ở ĐÂY ĐÃ ĐƯỢC FIX
#else
    ledcWrite(_channel, finalPwm); 
#endif

}
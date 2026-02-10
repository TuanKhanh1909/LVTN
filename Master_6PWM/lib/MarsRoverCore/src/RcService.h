#ifndef RC_SERVICE_H
#define RC_SERVICE_H

#include <Arduino.h>
#include "InputManager.h" // Cần truy cập InputManager để đẩy dữ liệu vào
class RcService {
private:
    uint8_t _throttlePin;
    uint8_t _steeringPin;
    InputManager* _inputMgr; // Con trỏ để đẩy dữ liệu

    // Các biến lưu giá trị xung (cần volatile vì dùng trong ISR)
    // Để static để hàm ngắt (ISR) có thể truy cập được
    static volatile unsigned long _startThr;
    static volatile uint16_t _valThr;
    
    static volatile unsigned long _startStr;
    static volatile uint16_t _valStr;

    // Biến lưu trạng thái cũ để kiểm tra thay đổi
    static uint16_t _lastThr;
    static uint16_t _lastStr;
    static RcService* _instance; // Con trỏ tĩnh để ISR gọi được hàm thành viên (nếu cần)

public:
    // Constructor nhận thêm InputManager
    RcService(InputManager* inputMgr,uint8_t throttlePin, uint8_t steeringPin);
    
    void begin();
    
    // Hàm này sẽ được gọi trong loop hoặc task để kiểm tra và đẩy dữ liệu
    // (Vì ISR không nên gọi hàm phức tạp của InputManager trực tiếp)
    void update();

private:
    // Hàm ngắt phải là static để gắn vào attachInterrupt
    static void IRAM_ATTR isrThrottle();
    static void IRAM_ATTR isrSteering();
};

#endif
#include "RcService.h"

// Khởi tạo các biến tĩnh
volatile unsigned long RcService::_startThr = 0;
volatile uint16_t RcService::_valThr = 0;
volatile unsigned long RcService::_startStr = 0;
volatile uint16_t RcService::_valStr = 0;
uint16_t RcService::_lastThr = 1500;
uint16_t RcService::_lastStr = 1500;

// Các con trỏ pin để ISR biết chân nào (nhưng ISR static không truy cập được biến non-static)
// Cách đơn giản nhất cho ESP32 là hardcode chân trong define hoặc dùng thủ thuật instance

static uint8_t PIN_THR = 0;
static uint8_t PIN_STR = 0;

RcService::RcService(InputManager* inputMgr, uint8_t throttlePin, uint8_t steeringPin) {
    _inputMgr = inputMgr;
    _throttlePin = throttlePin;
    _steeringPin = steeringPin;
    PIN_THR = throttlePin;
    PIN_STR = steeringPin;
}

void RcService::begin() {
    pinMode(_throttlePin, INPUT);
    pinMode(_steeringPin, INPUT);
    
    attachInterrupt(digitalPinToInterrupt(_throttlePin), RcService::isrThrottle, CHANGE);
    attachInterrupt(digitalPinToInterrupt(_steeringPin), RcService::isrSteering, CHANGE);
}

// Hàm này em sẽ gọi trong TaskControl (giống như network.update() trong TaskNetwork)
void RcService::update() {
    uint16_t thr, str;
    unsigned long lastThrTime;
    // Đọc an toàn từ ISR
    noInterrupts();
    thr = _valThr;
    str = _valStr;
    lastThrTime = _startThr;
    interrupts();

    
    // BÍ KÍP Ở ĐÂY: Nếu quá 200ms (200,000 micro-giây) mà chân RC không nháy lên HIGH lần nào
    // -> Tay cầm RC đã tắt hoặc chưa cắm điện -> KHÔNG BƠM DỮ LIỆU RÁC VÀO NỮA!
    if (micros() - lastThrTime > 200000) {
        return; // Lặng lẽ thoát ra, để InputManager tự chuyển quyền cho Web
    }
    // Chỉ đẩy vào InputManager nếu giá trị thay đổi đáng kể (tránh spam)
    // Hoặc cứ đẩy liên tục cũng được vì InputManager xử lý rất nhanh
    _inputMgr->updateRC(thr, str);
}

// --- HÀM NGẮT TỐC ĐỘ CAO ---
void IRAM_ATTR RcService::isrThrottle() {
    unsigned long now = micros();
    if (digitalRead(PIN_THR) == HIGH) {
        _startThr = now;
    } else {
        _valThr = (uint16_t)(now - _startThr);
    }
}

void IRAM_ATTR RcService::isrSteering() {
    unsigned long now = micros();
    if (digitalRead(PIN_STR) == HIGH) {
        _startStr = now;
    } else {
        _valStr = (uint16_t)(now - _startStr);
    }
}

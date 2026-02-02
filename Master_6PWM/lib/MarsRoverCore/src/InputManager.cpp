#include "InputManager.h"

InputManager::InputManager() {
    _activeSource = SOURCE_NONE;
    // Giá trị an toàn mặc định (1500 = Đứng yên)
    _dataRC = {1500, 1500, false};
    _dataEspNow = {1500, 1500, false};
    _dataWeb = {1500, 1500, false};
}

void InputManager::begin() {}

// --- THUẬT TOÁN TRỘN XUNG (MIXING ALGORITHM) ---
// Chuyển đổi tọa độ Web (0-4095) thành xung điều khiển (1000-2000)
// Hỗ trợ đi chéo (vừa tiến vừa rẽ) và xoay tại chỗ.
void InputManager::calculateWebMixing(int x, int y, int pot, uint16_t &outL, uint16_t &outR) {
    float speed_factor = map(pot, 0, 4095, 0, 100) / 100.0;
    const int RANGE = 500; 
    
    // Ngưỡng Deadzone của Joystick ảo
    int low = 1648, high = 2448; 
    
    int tempL = 1500;
    int tempR = 1500;

    if (y < low) { // Đang đẩy TIẾN
        if (x < low) { // Nghiêng TRÁI -> Giảm bánh trái, giữ bánh phải
            tempR = 1500 + (RANGE * speed_factor);
            tempL = 1500 + (RANGE * speed_factor * 0.4); // Bánh trái chạy chậm lại để rẽ
        } else if (x > high) { // Nghiêng PHẢI
            tempL = 1500 + (RANGE * speed_factor);
            tempR = 1500 + (RANGE * speed_factor * 0.4);
        } else { // Đi THẲNG
            tempL = 1500 + (RANGE * speed_factor);
            tempR = 1500 + (RANGE * speed_factor);
        }
    } 
    else if (y > high) { // Đang đẩy LÙI
        if (x < low) { // Lùi TRÁI
            tempR = 1500 - (RANGE * speed_factor);
            tempL = 1500 - (RANGE * speed_factor * 0.4);
        } else if (x > high) { // Lùi PHẢI
            tempL = 1500 - (RANGE * speed_factor);
            tempR = 1500 - (RANGE * speed_factor * 0.4);
        } else { // Lùi THẲNG
            tempL = 1500 - (RANGE * speed_factor);
            tempR = 1500 - (RANGE * speed_factor);
        }
    } 
    else { // Đứng yên tại chỗ (Y ở giữa)
        if (x < low) { // Xoay TRÁI tại chỗ (Zero Turn)
            tempL = 1500 - (RANGE * speed_factor); // Trái lùi
            tempR = 1500 + (RANGE * speed_factor); // Phải tiến
        } else if (x > high) { // Xoay PHẢI tại chỗ
            tempL = 1500 + (RANGE * speed_factor); // Trái tiến
            tempR = 1500 - (RANGE * speed_factor); // Phải lùi
        }
    }

    outL = constrain(tempL, 1000, 2000);
    outR = constrain(tempR, 1000, 2000);
}

void InputManager::updateWeb(int x, int y, int pot) {
    uint16_t pL, pR;
    calculateWebMixing(x, y, pot, pL, pR); // Tính toán ngay lập tức
    _dataWeb.pulseL = pL;
    _dataWeb.pulseR = pR;
    _dataWeb.connected = true;
    _lastTimeWeb = millis();
}

void InputManager::updateEspNow(uint16_t pL, uint16_t pR) {
    _dataEspNow.pulseL = pL;
    _dataEspNow.pulseR = pR;
    _dataEspNow.connected = true;
    _lastTimeEspNow = millis();
}

void InputManager::updateRC(uint16_t pL, uint16_t pR, bool isSwitchOn) {
    if (isSwitchOn) {
        _dataRC.pulseL = pL;
        _dataRC.pulseR = pR;
        _dataRC.connected = true;
        _lastTimeRC = millis();
    } else {
        _dataRC.connected = false;
    }
}

bool InputManager::isSourceValid(unsigned long lastTime) {
    // Kiểm tra xem tín hiệu có còn mới không (trong vòng 2 giây)
    return (millis() - lastTime < SIGNAL_TIMEOUT_MS);
}

ControlCommand InputManager::getCommand() {
    ControlCommand finalCmd;
    // Mặc định an toàn
    finalCmd.pulseL = 1500; 
    finalCmd.pulseR = 1500;
    finalCmd.connected = false;
    _activeSource = SOURCE_NONE;

    // --- LOGIC ƯU TIÊN ---
    // 1. Tay cầm RC (Cao nhất)
    if (_dataRC.connected && isSourceValid(_lastTimeRC)) {
        _activeSource = SOURCE_RC;
        return _dataRC;
    }
    // 2. Tay cầm ESP-NOW
    if (isSourceValid(_lastTimeEspNow)) {
        _activeSource = SOURCE_ESP_NOW;
        return _dataEspNow;
    }
    // 3. Web
    if (isSourceValid(_lastTimeWeb)) {
        _activeSource = SOURCE_WEB;
        return _dataWeb;
    }

    return finalCmd; // Không có nguồn nào -> Dừng xe
}

InputSource InputManager::getActiveSource() {
    return _activeSource;
}
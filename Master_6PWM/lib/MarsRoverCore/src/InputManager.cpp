#include "InputManager.h"

InputManager::InputManager()
{
    _activeSource = SOURCE_NONE;
    _targetMode = SOURCE_NONE; // Khởi động lên MẶC ĐỊNH là RC
   
   // Bắt buộc người dùng phải vào Web để mở khóa
    _isFailsafeLatched = true;
   
    // Giá trị an toàn mặc định (1500 = Đứng yên)
    _dataRC = {1500, 1500, false};
    _dataEspNow = {1500, 1500, false};
    _dataWeb = {1500, 1500, false};

    // ---> THÊM 3 DÒNG NÀY VÀO ĐỂ DỌN SẠCH RÁC BỘ NHỚ <---
    _lastTimeRC = 0;
    _lastTimeEspNow = 0;
    _lastTimeWeb = 0;
}

void InputManager::begin() {}

// --->  HÀM NÀY VÀO BÊN DƯỚI HÀM KHỞI TẠO <---
void InputManager::setControlMode(InputSource mode)
{
    _targetMode = mode;

    // BÍ KÍP ADMIN: Cứ mỗi lần Web chọn lại chế độ -> Xóa cờ Failsafe (Mở khóa xe)
    if (_isFailsafeLatched) {
        Serial.println("[ADMIN] Da xac nhan an toan! MO KHOA FAILSAFE!");
        _isFailsafeLatched = false; 
    }
    _lastTimeWeb = millis();
    _lastTimeRC = millis();
    _lastTimeEspNow = millis();
}

// --- THUẬT TOÁN TRỘN XUNG (MIXING ALGORITHM) ---
// Chuyển đổi tọa độ Web (0-4095) thành xung điều khiển (1000-2000)
// Hỗ trợ đi chéo (vừa tiến vừa rẽ) và xoay tại chỗ.
void InputManager::calculateWebMixing(int x, int y, int pot, uint16_t &outL, uint16_t &outR)
{
    float speed_factor = map(pot, 0, 4095, 0, 100) / 100.0;
    const int RANGE = 500;

    // Ngưỡng Deadzone của Joystick ảo
    int low = 1648, high = 2448;

    int tempL = 1500;
    int tempR = 1500;

    if (y < low)
    { // Đang đẩy TIẾN
        if (x < low)
        { // Nghiêng TRÁI -> Giảm bánh trái, giữ bánh phải
            tempR = 1500 + (RANGE * speed_factor);
            tempL = 1500 + (RANGE * speed_factor * 0.4); // Bánh trái chạy chậm lại để rẽ
        }
        else if (x > high)
        { // Nghiêng PHẢI
            tempL = 1500 + (RANGE * speed_factor);
            tempR = 1500 + (RANGE * speed_factor * 0.4);
        }
        else
        { // Đi THẲNG
            tempL = 1500 + (RANGE * speed_factor);
            tempR = 1500 + (RANGE * speed_factor);
        }
    }
    else if (y > high)
    { // Đang đẩy LÙI
        if (x < low)
        { // Lùi TRÁI
            tempR = 1500 - (RANGE * speed_factor);
            tempL = 1500 - (RANGE * speed_factor * 0.4);
        }
        else if (x > high)
        { // Lùi PHẢI
            tempL = 1500 - (RANGE * speed_factor);
            tempR = 1500 - (RANGE * speed_factor * 0.4);
        }
        else
        { // Lùi THẲNG
            tempL = 1500 - (RANGE * speed_factor);
            tempR = 1500 - (RANGE * speed_factor);
        }
    }
    else
    { // Đứng yên tại chỗ (Y ở giữa)
        if (x < low)
        {                                          // Xoay TRÁI tại chỗ (Zero Turn)
            tempL = 1500 - (RANGE * speed_factor); // Trái lùi
            tempR = 1500 + (RANGE * speed_factor); // Phải tiến
        }
        else if (x > high)
        {                                          // Xoay PHẢI tại chỗ
            tempL = 1500 + (RANGE * speed_factor); // Trái tiến
            tempR = 1500 - (RANGE * speed_factor); // Phải lùi
        }
    }

    outL = constrain(tempL, 1000, 2000);
    outR = constrain(tempR, 1000, 2000);
}

void InputManager::updateWeb(int x, int y, int pot)
{
    // 1. LỚP PHÒNG THỦ: Xác thực dữ liệu (Input Validation)
    // Giới hạn hợp lệ của Web Joystick và Slider là từ 0 đến 4095
    if (x < 0 || x > 4095 || y < 0 || y > 4095 || pot < 0 || pot > 4095)
    {
        // Nếu phát hiện dữ liệu rác / hack -> Đưa xe về Failsafe (Đứng im)
        _dataWeb.pulseL = 1500;
        _dataWeb.pulseR = 1500;
        _dataWeb.connected = false; // Tạm ngắt kết nối để nhường quyền cho bộ khác
        return;                     // Thoát ngay lập tức, không cho phép làm toán
    }
    // 2. Dữ liệu sạch -> Cho phép tính toán Mixing bình thường
    uint16_t pL, pR;
    calculateWebMixing(x, y, pot, pL, pR); // Tính toán ngay lập tức
    _dataWeb.pulseL = pL;
    _dataWeb.pulseR = pR;
    _dataWeb.connected = true;
    _lastTimeWeb = millis();
}

void InputManager::updateEspNow(uint16_t pL, uint16_t pR)
{
    _dataEspNow.pulseL = pL;
    _dataEspNow.pulseR = pR;
    _dataEspNow.connected = true;
    _lastTimeEspNow = millis();
}
// Hàm trộn cho kênh RC
void InputManager::calculateRCMixing(uint16_t Throttle, uint16_t Steering, uint16_t &outL, uint16_t &outR)
{
    // throttle: 1000(Lùi) - 1500(Dừng) - 2000(Tiến)
    // steering: 1000(Trái) - 1500(Thẳng) - 2000(Phải)

    // 1. Chuyển về tọa độ (-500, 500)
    long y = (long)Throttle - 1500;
    long x = (long)Steering - 1500;

    // Deadzone (Vùng chết) để chống rung tay
    if (abs(y) < 30)
        y = 0;
    if (abs(x) < 30)
        x = 0;

    long left = 0;
    long right = 0;

    // 2. Thuật toán trộn xe tự hành (Smooth Skid-Steer Mixing)
    if (y == 0)
    {
        // TRƯỜNG HỢP 1: Xe đang không ga -> Đứng yên xoay tại chỗ (Zero-Turn)
        left = x;
        right = -x;
    }
    else
    {
        // TRƯỜNG HỢP 2: Xe đang chạy tới hoặc lùi -> Ôm cua mềm mại
        if (x > 0)
        { // Đánh lái sang PHẢI
            // Bánh Trái (ngoài) giữ nguyên tốc độ, Bánh Phải (trong) giảm tốc độ
            left = y;
            right = y - (y * x / 500);
        }
        else if (x < 0)
        { // Đánh lái sang TRÁI
            // Bánh Phải (ngoài) giữ nguyên tốc độ, Bánh Trái (trong) giảm tốc độ
            left = y - (y * abs(x) / 500);
            right = y;
        }
        else
        { // Đi thẳng (x = 0)
            left = y;
            right = y;
        }
    }

    // 3. Đưa về dải 1000-2000
    outL = constrain(left + 1500, 1000, 2000);
    outR = constrain(right + 1500, 1000, 2000);
}
void InputManager::updateRC(uint16_t Throttle, uint16_t Steering)
{
    // Lọc nhiễu cơ bản
    if (Throttle < 800 || Throttle > 2200)
        return;
    if (Steering < 800 || Steering > 2200)
        return;

    uint16_t pL, pR;
    calculateRCMixing(Throttle, Steering, pL, pR);

    _dataRC.pulseL = pL;
    _dataRC.pulseR = pR;
    _dataRC.connected = true; // Đánh dấu là có kết nối RC
    _lastTimeRC = millis();
}

bool InputManager::isSourceValid(unsigned long lastTime,unsigned long timeoutMs)
{
    // Kiểm tra xem tín hiệu có còn mới không (trong vòng 200 miligiây)
    return (millis() - lastTime < timeoutMs);
}

ControlCommand InputManager::getCommand()   
{
    ControlCommand finalCmd = {1500, 1500, false};
    
    _activeSource = SOURCE_NONE;

    // -----------------------------------------------------------------
    // TẦNG 1: BẢO VỆ TỐI CAO (LATCHED FAILSAFE)
    // Nếu cờ này đã bật, từ chối mọi tín hiệu cho đến khi Admin mở khóa
    // -----------------------------------------------------------------
    if (_isFailsafeLatched) {
        return finalCmd; 
    }

    // -----------------------------------------------------------------
    // TẦNG 2: KIỂM TRA NGUỒN ĐANG ĐƯỢC CHỌN (MUTEX CONTROL)
    // -----------------------------------------------------------------
    
    // ---> KỊCH BẢN A: ADMIN CHO PHÉP WEB LÁI
    if (_targetMode == SOURCE_WEB) {
        if (_dataWeb.connected) { 
            // FIX: Cho phép WiFi lag tối đa 1000ms (1 giây)
            if (isSourceValid(_lastTimeWeb, 1000)) { 
                _activeSource = SOURCE_WEB;
                return _dataWeb;
            } else {
                Serial.println("[FAILSAFE] Mat song WEB! KHOA CUNG HE THONG!");
                _isFailsafeLatched = true;
            }
        }
    }

    // ---> KỊCH BẢN B: ADMIN CHO PHÉP ESP-NOW LÁI
    else if (_targetMode == SOURCE_ESP_NOW) {
        if (_dataEspNow.connected) {
            // FIX: Cho phép sóng ESP-NOW lag tối đa 1000ms
            if (isSourceValid(_lastTimeEspNow, 1000)) {
                _activeSource = SOURCE_ESP_NOW;
                return _dataEspNow;
            } else {
                Serial.println("[FAILSAFE] Mat song ESP-NOW! KHOA CUNG HE THONG!");
                _isFailsafeLatched = true;
            }
        }
    }

    // ---> KỊCH BẢN C: ADMIN CHO PHÉP RC LÁI (HOẶC VỪA KHỞI ĐỘNG LÊN)
    else if (_targetMode == SOURCE_RC) {
        if (_dataRC.connected) {
            // GIỮ NGUYÊN: RC vật lý bắt buộc phải nhanh, 200ms là chốt khóa!
            if (isSourceValid(_lastTimeRC, 200)) {
                _activeSource = SOURCE_RC;
                return _dataRC;
            } else {
                Serial.println("[FAILSAFE] Mat song Tay cam RC! KHOA CUNG HE THONG!");
                _isFailsafeLatched = true;
            }
        }
    }

    // Nếu chưa nhận được dữ liệu (mới bật nguồn chưa kịp kết nối) -> Đứng im an toàn
    return finalCmd;
}

InputSource InputManager::getActiveSource()
{
    return _activeSource;
}
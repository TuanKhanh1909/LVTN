#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <Arduino.h>
#include "RoverTypes.h"

#define SIGNAL_TIMEOUT_MS 200 // 200 miligiây không có tín hiệu -> Tự dừng (Failsafe)

/**
 * @class InputManager
 * @brief Bộ đa hợp tín hiệu (Multiplexer). 
 * Nhận tín hiệu từ Web, RC, ESP-NOW, chuẩn hóa về dạng Pulse và chọn nguồn ưu tiên.
 */
class InputManager {
private:
    ControlCommand _dataRC;
    ControlCommand _dataEspNow;
    ControlCommand _dataWeb;

    unsigned long _lastTimeRC;
    unsigned long _lastTimeEspNow;
    unsigned long _lastTimeWeb;

    InputSource _activeSource;
    InputSource _targetMode; //Biến lưu chế độ đang được cấp phép

    // --->Cờ Khóa An Toàn (Latched Failsafe)
    bool _isFailsafeLatched;
public:
    InputManager();
    void begin();

    // ---> THÊM DÒNG NÀY: Lấy trạng thái cờ Failsafe
    bool isFailsafeLatched() { return _isFailsafeLatched; }
    
    //Hàm để NetworkService cập nhật chế độ từ Web
    void setControlMode(InputSource mode);

    // 1. Cập nhật từ Web: Nhận X, Y, Pot -> Tính toán Mixing ngay lập tức
    void updateWeb(int x, int y, int pot);
    
    // 2. Cập nhật từ Tay cầm cũ: Đã là dạng Pulse -> Lưu trực tiếp
    void updateEspNow(uint16_t pL, uint16_t pR);
    
    // 3. Cập nhật từ RC: Tín hiệu PWM từ tay cầm RC -> Tính toán mixing
    void updateRC(uint16_t Throttle, uint16_t Steering); 

    // Lấy lệnh điều khiển cuối cùng sau khi xét độ ưu tiên
    ControlCommand getCommand();
    
    // Lấy nguồn đang điều khiển (để hiển thị LED báo hiệu)
    InputSource getActiveSource();

private:
    bool isSourceValid(unsigned long lastTime, unsigned long timeoutMs);
    void calculateWebMixing(int x, int y, int pot, uint16_t &outL, uint16_t &outR);
    void calculateRCMixing(uint16_t Throttle, uint16_t Steering, uint16_t &outL, uint16_t &outR);
};

#endif
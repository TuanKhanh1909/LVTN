#ifndef ROVER_SIDE_H
#define ROVER_SIDE_H

#include <Arduino.h>
#include <vector>
#include "BldcDriver.h"

/**
 * @class RoverSide
 * @brief Quản lý nhóm 3 bánh xe một bên (Trái hoặc Phải).
 * Chịu trách nhiệm đồng bộ tốc độ PWM cho 3 bánh và điều khiển Transistor đảo chiều/phanh.
 */
class RoverSide {
private:
    uint8_t _dirPin;   // Chân kích Transistor đảo chiều
    uint8_t _brakePin; // Chân kích Transistor phanh
    
    std::vector<BldcDriver*> _motors; // Danh sách 3 động cơ con
    
    bool _reverseLogic; // Cấu hình: True nếu mạch yêu cầu mức HIGH để lùi

public: 
    RoverSide(uint8_t dirPin, uint8_t brakePin, bool reverseLogic = true);
    
    void addMotor(BldcDriver* motor);
    void begin();

    // Nhận tốc độ có dấu: -255 (Lùi Max) ... 0 ... 255 (Tiến Max)
    void setSpeed(float speed);
    
    // Kích hoạt phanh cứng (Ngắt động lực)
    void brake();
};

#endif
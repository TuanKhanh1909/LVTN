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

struct MotorUnit {
    BldcDriver* driver;
    int sensorID;
};

class RoverSide {
private:
    uint8_t _dirPin;   // Chân kích Transistor đảo chiều
    uint8_t _brakePin; // Chân kích Transistor phanh
    bool _reverseLogic;
    std::vector<MotorUnit> _motors; // Danh sách 3 động cơ con

public: 
    RoverSide(uint8_t dirPin, uint8_t brakePin, bool reverseLogic = false);
    
    void addMotor(BldcDriver* motor, int sensorID);
    void begin();
    void brake();
    void setDirectPWM(int pwm, bool isForward);
};

#endif
#include "RoverSide.h"

RoverSide::RoverSide(uint8_t dirPin, uint8_t brakePin, bool reverseLogic) 
    : _dirPin(dirPin), _brakePin(brakePin), _reverseLogic(reverseLogic) {}

void RoverSide::addMotor(BldcDriver* motor, int sensorID) {
    _motors.push_back({motor, sensorID});
}

void RoverSide::begin() {
    pinMode(_dirPin, OUTPUT);   
    pinMode(_brakePin, OUTPUT);
    digitalWrite(_brakePin, HIGH); // Mặc định khóa phanh lúc khởi động
    for(auto& m : _motors) {
        m.driver->begin();
    }
}

void RoverSide::brake() {
    digitalWrite(_brakePin, HIGH); // Đóng rơ-le phanh
    for(auto& m : _motors) {          
        m.driver->setThrottle(0);  
    }
}

void RoverSide::setDirectPWM(int pwm, bool isForward) {
    bool wantReverse = !isForward;
    // Gạt rơ-le hướng
    digitalWrite(_dirPin, _reverseLogic ? (wantReverse ? LOW : HIGH) : (wantReverse ? HIGH : LOW));
    digitalWrite(_brakePin, LOW); // Mở khóa phanh

    for(auto& m : _motors) {
        m.driver->setThrottle(pwm);
    }
}


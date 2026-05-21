#include "RoverSide.h"

RoverSide::RoverSide(uint8_t dirPin, uint8_t brakePin, bool reverseLogic) 
    : _dirPin(dirPin), _brakePin(brakePin), _reverseLogic(reverseLogic) {}

void RoverSide::addMotor(BldcDriver* motor) {
    _motors.push_back(motor);
}

void RoverSide::begin() {
    pinMode(_dirPin, OUTPUT);
    pinMode(_brakePin, OUTPUT);
    digitalWrite(_brakePin, HIGH); // Kích hoạt phanh khi vừa khởi động để an toàn
    
    for(auto m : _motors) m->begin();
}

void RoverSide::setSpeed(float speed) {
    // 1. Xử lý chiều quay (Direction Logic)
    // Theo báo cáo: MCU xuất mức Logic vào Transistor đệm để kéo chân DIR xuống Mass.
    // speed < 0 nghĩa là muốn lùi.
    bool wantReverse = (speed < 0);
    
    // Xử lý đảo chiều logic (nếu lắp động cơ bị ngược dây)
    if (_reverseLogic) { 
        digitalWrite(_dirPin, wantReverse ? LOW : HIGH); 
    } else {
        digitalWrite(_dirPin, wantReverse ? HIGH : LOW);
    }

    // 2. Xử lý Tốc độ & Phanh
    int duty = abs(speed);
    duty = constrain(duty, 0, 255);

    // Deadzone: Nếu PWM quá nhỏ, cắt hẳn về 0 và phanh lại
    if (duty < 10) { 
        digitalWrite(_brakePin, HIGH); // Kích phanh
        for(auto m : _motors) m->setThrottle(0);
    } else {
        digitalWrite(_brakePin, LOW);  // Nhả phanh
        for(auto m : _motors) m->setThrottle(duty);
    }
}

void RoverSide::brake() {
    digitalWrite(_brakePin, HIGH); 
    for(auto m : _motors) m->setThrottle(0);
}
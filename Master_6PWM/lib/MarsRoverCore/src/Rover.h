#ifndef ROVER_H
#define ROVER_H

#include "RoverSide.h"
#include "RoverTypes.h"
#include "SpeedMonitor.h"

/**
 * @class Rover
 * @brief Bộ não trung tâm.
 * Chứa Máy trạng thái (FSM) để đảm bảo an toàn (không đảo chiều đột ngột).
 * Xác định trạng thái hiển thị (MotionType).
 */
class Rover {
private:
    RoverSide* _leftSide;
    RoverSide* _rightSide;

    MotionType _currentMotion;     // Trạng thái hiển thị và vận hành hiện tại
    MotionType _nextMotionPending; // Trạng thái chờ (khi đang phanh)
    bool _isBraking;               // Cờ báo hiệu xe đang trong quá trình phanh

    unsigned long _brakeStartTime; // Thời điểm bắt đầu đạp phanh
    unsigned long _zeroDetectTime; // Thời điểm phát hiện RPM = 0 liên tục

    float _currentSpeedL, _currentSpeedR; // Biến lưu tốc độ hiện tại

public:
    Rover();
    void setSides(RoverSide* left, RoverSide* right);
    void begin();

    // Hàm cập nhật chính (gọi mỗi 20ms từ Task_DriveFSM)
    void update(ControlCommand cmd);

    // Lấy trạng thái để hiển thị (VD: Đang quay trái, đang lùi...)
    MotionType getMotionType();
    
    //----> 2 Hàm này chỉ để Unit Test <----
    float getCurrentSpeedL() { return _currentSpeedL; } 
    float getCurrentSpeedR() { return _currentSpeedR; }
};

#endif
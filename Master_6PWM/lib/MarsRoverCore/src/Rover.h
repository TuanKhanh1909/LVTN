#ifndef ROVER_H
#define ROVER_H

#include "RoverSide.h"
#include "RoverTypes.h"
#include "SpeedMonitor.h"
/**
 * @class Rover
 * @brief Bộ não trung tâm.
 * Chứa Máy trạng thái (FSM) để đảm bảo an toàn (không đảo chiều đột ngột).
 * Chuyển đổi Pulse (1000-2000) sang Tốc độ (-255 đến 255).
 * Xác định trạng thái hiển thị (MotionType).
 */
class Rover {
private:
    RoverSide* _leftSide;
    RoverSide* _rightSide;

    RoverState _currentState;       // Trạng thái FSM (An toàn)
    unsigned long _brakeStartTime;  // Thời gian bắt đầu phanh
    bool _isTargetForward;          // Hướng xe đang muốn đi (dùng để so sánh đảo chiều)

    float _currentSpeedL, _currentSpeedR; // Biến lưu tốc độ hiện tại cho Soft-Start

public:
    Rover();
    void setSides(RoverSide* left, RoverSide* right);
    void begin();

    // Hàm cập nhật chính (gọi mỗi 10-20ms)
    void update(ControlCommand cmd);

    // Lấy trạng thái để hiển thị (VD: Đang quay trái, đang lùi...)
    MotionType getMotionType();
    
private:
    int pulseToSpeed(uint16_t pulse);
};

#endif
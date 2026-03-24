#include "Rover.h"

#define BRAKE_TIME_MS 1000 // Thời gian chờ tối thiểu khi đảo chiều
#define RAMP_STEP 5.0      // Bước tăng tốc (Soft start) - Chỉnh nhỏ thì xe mượt, chỉnh lớn thì xe bốc

Rover::Rover() {
    _currentState = STATE_IDLE;
    _currentSpeedL = 0;
    _currentSpeedR = 0;
}

void Rover::setSides(RoverSide* left, RoverSide* right) {
    _leftSide = left;
    _rightSide = right;
}

void Rover::begin() {
    _leftSide->begin();
    _rightSide->begin();
}

int Rover::pulseToSpeed(uint16_t pulse) {
    // Chuyển đổi Pulse 1000-2000 sang -255 đến 255
    if (pulse > 1520) return map(pulse, 1520, 2000, 0, 255);  // Tiến
    if (pulse < 1480) return map(pulse, 1480, 1000, 0, -255); // Lùi
    return 0; // Điểm chết (Dừng)
}

void Rover::update(ControlCommand cmd) {
    if (!cmd.connected){
        _currentState = STATE_IDLE;
        _leftSide->brake();
        _rightSide->brake();
        _currentSpeedL = 0;
        _currentSpeedR = 0;
        return; //Ngắt điện ngay lập tức
    }
    // 1. Chuyển đổi lệnh
    int targetSpeedL = pulseToSpeed(cmd.pulseL);
    int targetSpeedR = pulseToSpeed(cmd.pulseR);

    // Xác định hướng mong muốn (Tổng dương là Tiến, Tổng âm là Lùi)
    bool desireForward = (targetSpeedL + targetSpeedR) >= 0;

    // 2. MÁY TRẠNG THÁI (FSM)
    switch (_currentState) {
        case STATE_IDLE:
            // Nếu có lệnh di chuyển
            if (abs(targetSpeedL) > 0 || abs(targetSpeedR) > 0) {
                _currentState = STATE_DRIVING;
                _isTargetForward = desireForward;
            }
            // Giữ phanh an toàn
            _leftSide->brake(); _rightSide->brake();
            _currentSpeedL = 0; _currentSpeedR = 0;
            break;

        case STATE_DRIVING:
            // Logic bảo vệ: Nếu đang chạy nhanh (>50) mà đảo chiều đột ngột -> Phanh gấp
            if (desireForward != _isTargetForward && (abs(_currentSpeedL) > 50 || abs(_currentSpeedR) > 50)) {
                _currentState = STATE_BRAKING_TO_SWITCH;
                _brakeStartTime = millis();
            } 
            else {
                // Soft Start: Tăng tốc từ từ đến giá trị mục tiêu
                // 1. Xử lý Bánh Trái
                if (abs(targetSpeedL - _currentSpeedL) <= RAMP_STEP) {
                    _currentSpeedL = targetSpeedL; // Đã gần tới đích -> Bắt dính luôn!
                } 
                else if (_currentSpeedL < targetSpeedL) {
                    _currentSpeedL += RAMP_STEP;
                } 
                else {
                    _currentSpeedL -= RAMP_STEP;
                }

                // 2. Xử lý Bánh Phải
                if (abs(targetSpeedR - _currentSpeedR) <= RAMP_STEP) {
                    _currentSpeedR = targetSpeedR; // Đã gần tới đích -> Bắt dính luôn!
                } 
                else if (_currentSpeedR < targetSpeedR) {
                    _currentSpeedR += RAMP_STEP;
                } 
                else {
                    _currentSpeedR -= RAMP_STEP;
                }

                // Xuất lệnh ra Motor
                _leftSide->setSpeed(_currentSpeedL);
                _rightSide->setSpeed(_currentSpeedR);
            }
            
            // Nếu thả tay (về 0) -> Chuyển về IDLE
            if (targetSpeedL == 0 && targetSpeedR == 0) _currentState = STATE_IDLE;
            break;

        case STATE_BRAKING_TO_SWITCH:
            _leftSide->brake(); _rightSide->brake();
            // Điều kiện thoát: Hết thời gian chờ HOẶC Xe đã dừng hẳn (RPM thấp)
            if (millis() - _brakeStartTime > BRAKE_TIME_MS || isRoverCompletelyStopped()) {
                _isTargetForward = desireForward; // Chấp nhận hướng mới
                _currentSpeedL = 0; _currentSpeedR = 0;
                _currentState = STATE_DRIVING;
            }
            break;
    }
    // --- [DEBUG CODE] IN GIÁ TRỊ RA MÀN HÌNH (Thêm đoạn này) ---
    static unsigned long lastDebugTime = 0;
    if (millis() - lastDebugTime > 200) { // In mỗi 200ms
        lastDebugTime = millis();
        
        Serial.print("IN Pulse[L:");
        Serial.print(cmd.pulseL);
        Serial.print("|R:");
        Serial.print(cmd.pulseR);
        Serial.print("] -> Target[L:");
        Serial.print(targetSpeedL);
        Serial.print("|R:");
        Serial.print(targetSpeedR);
        Serial.print("] -> PWM_Out[L:");
        Serial.print((int)_currentSpeedL);
        Serial.print("|R:");
        Serial.print((int)_currentSpeedR);
        Serial.print("] -> State:");
        
        if (_currentState == STATE_IDLE) Serial.println("IDLE");
        else if (_currentState == STATE_DRIVING) Serial.println("DRIVING");
        else Serial.println("BRAKING");
    }
    // -----------------------------------------------------------
}



// Hàm xác định trạng thái hiển thị (10 trạng thái như yêu cầu)
MotionType Rover::getMotionType() {
    if (_currentState != STATE_DRIVING || (_currentSpeedL == 0 && _currentSpeedR == 0)) 
        return MOTION_STOP;

    bool L_Fwd = (_currentSpeedL > 0);
    bool R_Fwd = (_currentSpeedR > 0);
    
    // Xoay tại chỗ
    if (!L_Fwd && R_Fwd) return MOTION_SPIN_LEFT;
    if (L_Fwd && !R_Fwd) return MOTION_SPIN_RIGHT;

    // Tiến
    if (L_Fwd && R_Fwd) {
        if (_currentSpeedL > _currentSpeedR + 20) return MOTION_FWD_RIGHT;
        if (_currentSpeedR > _currentSpeedL + 20) return MOTION_FWD_LEFT;
        return MOTION_FORWARD;
    }
    
    // Lùi
    if (!L_Fwd && !R_Fwd) {
        if (abs(_currentSpeedL) > abs(_currentSpeedR) + 20) return MOTION_BCK_RIGHT;
        if (abs(_currentSpeedR) > abs(_currentSpeedL) + 20) return MOTION_BCK_LEFT;
        return MOTION_BACKWARD;
    }

    return MOTION_STOP;
}
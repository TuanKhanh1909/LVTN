#ifndef ROVER_H
#define ROVER_H

#include "Arduino.h"
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

    MotionType _currentState;     // Trạng thái hiển thị và vận hành hiện tại
    MotionType _desireState;
    MotionType _nextMotionPending; // Trạng thái chờ (khi đang phanh)

    const float RPM_SAFE_ZERO = 6.0;
    const unsigned long BRAKE_TIMEOUT = 2000;
    const unsigned long CONFIRM_STOP_TIME = 200;
    const int MIN_PWM_START = 85;
    const int PWM_FIXED_REVERSE = 180;
    const int PWM_ASSIST_FWD = 100;
    const int PWM_TURN_FWD = 110;
    const int PWM_ASSIST_BCK = 85;
    
    //Ramp_step
    const float RAMP_STEP = 6.0;

    //Biến lưu PWM
    float _currentPWM_L;
    float _currentPWM_R;


    unsigned long _brakeStartTime; // Thời điểm bắt đầu đạp phanh
    unsigned long _zeroDetectTime; // Thời điểm phát hiện RPM = 0 liên tục

    float _actualSpeedL, _actualSpeedR; // Biến lưu tốc độ hiện tại

    int getDirection(uint16_t pulse);
    MotionType determineDesiredState(uint16_t pL, uint16_t pR);
    int mapPulseToPWM(uint16_t pulse);
    void applyDriveControl(int pwmL, int pwmR, bool dirL_isFwd, bool dirR_isFwd, bool doBrake);

    void processSoftStart(int targetL, int targetR);

public:
    Rover();
    void setSides(RoverSide* left, RoverSide* right);
    void begin();

    // Hàm cập nhật chính (gọi mỗi 20ms từ Task_DriveFSM)
    void update(ControlCommand cmd);

    // Lấy trạng thái để hiển thị (VD: Đang quay trái, đang lùi...)
    MotionType getMotionType() { return _currentState; }

    //----> 2 Hàm này chỉ để Unit Test <----
    float getCurrentSpeedL() { return _actualSpeedL; } 
    float getCurrentSpeedR() { return _actualSpeedR; }
};

#endif
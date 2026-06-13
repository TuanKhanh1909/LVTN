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
class Rover
{
private:
    RoverSide *_leftSide;
    RoverSide *_rightSide;

    DriveFSMState _fsmState;
    MotionType _currentMotion;
    MotionType _desireMotion;

    int _setupPwm;
    unsigned long _lastSetupStepTime;
    unsigned long _switchDirStartTime;
    unsigned long _zeroDetectTime;

    // Timeout an toàn 1.5 giây bọc lót cảm biến
    const unsigned long TIMEOUT_RPM_ZERO = 1500;

    // // Các hằng số vận hành VÒNG HỞ (Cấp PWM tĩnh cho các góc cua)
    // const int PWM_FIXED_REVERSE = 180; // Lùi tĩnh thẳng
    // const int PWM_TURN_INNER    = 90;  // Bánh trong khi tiến rẽ (ôm cua)
    // const int PWM_SPIN_FWD      = 95; // Bánh tiến khi xoay tại chỗ
    // const int PWM_SPIN_BCK      = 180; // Bánh lùi khi xoay tại chỗ
    // const int PWM_BCK_TURN      = 90;  // Bánh hỗ trợ khi lùi rẽ

    float _appliedPWM_L;
    float _appliedPWM_R;

    int getDirection(uint16_t pulse);
    MotionType determineDesiredState(uint16_t pL, uint16_t pR);
    float mapPulseToPWM(uint16_t pulse);

    // void applyDriveControl(float rpmL, float rpmR, bool dirL_isFwd, bool dirR_isFwd, bool doBrake);

    // Hàm tiện ích nội bộ cho FSM
    void getDirectionFlags(MotionType motion, bool &l_fwd, bool &r_fwd);
    bool requiresRelayChange(MotionType current, MotionType desired);
    bool isFullyStopped();
    void executeDrivingLogic(ControlCommand cmd);

public:
    Rover();
    void setSides(RoverSide *left, RoverSide *right);
    void begin();

    // Hàm cập nhật chính (gọi mỗi 20ms từ Task_DriveFSM)
    void update(ControlCommand cmd);

    // Lấy trạng thái để hiển thị (VD: Đang quay trái, đang lùi...)
    MotionType getMotionType() { return _currentMotion; }

    // ---> THÊM 2 DÒNG NÀY ĐỂ SỬA LỖI BIÊN DỊCH <---
    float getCurrentSpeedL() { return _appliedPWM_L; }
    float getCurrentSpeedR() { return _appliedPWM_R; }
};

#endif
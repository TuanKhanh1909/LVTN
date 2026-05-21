#include "Rover.h"

Rover::Rover()
{
    _currentState = MOTION_IDLE;
    _desireState = MOTION_IDLE;
    _nextMotionPending = MOTION_IDLE;
    _brakeStartTime = 0;
    _zeroDetectTime = 0;
    _actualSpeedL = 0;
    _actualSpeedR = 0;

    _currentPWM_L = 0;
    _currentPWM_R = 0;
}

void Rover::setSides(RoverSide *left, RoverSide *right)
{
    _leftSide = left;
    _rightSide = right;
}

void Rover::begin()
{
    _leftSide->begin();
    _rightSide->begin();
}

int Rover::getDirection(uint16_t pulse){
    if (pulse > 1520)
        return 1;
    else if (pulse < 1480)
        return -1;
    return 0;
}
MotionType Rover::determineDesiredState(uint16_t pL, uint16_t pR){
    int dirL = getDirection(pL);
    int dirR = getDirection(pR);

    if (dirL == 0 && dirR == 0) return MOTION_IDLE;
    if (dirL == 1 && dirR == 1) return MOTION_FORWARD;
    if (dirL == -1 && dirR == -1) return MOTION_BACKWARD;
    if (dirL == -1 && dirR == 1) return MOTION_SPIN_LEFT;
    if (dirL == 1 && dirR == -1) return MOTION_SPIN_RIGHT;
    if (dirL == 0 && dirR == 1) return MOTION_FWD_LEFT;
    if (dirL == 1 && dirR == 0) return MOTION_FWD_RIGHT;
    if (dirL == 0 && dirR == -1) return MOTION_BCK_LEFT;
    if (dirL == -1 && dirR == 0) return MOTION_BCK_RIGHT;
    return MOTION_IDLE;
}

int Rover::mapPulseToPWM(uint16_t pulse){
    int pwm = 0;
    if (pulse > 1520)
        pwm = map(pulse, 1520, 2000, MIN_PWM_START, 255);
    else if (pulse < 1480)
        pwm = PWM_FIXED_REVERSE;
    return constrain(pwm, 0, 255);
}

void Rover::applyDriveControl(int pwmL, int pwmR, bool dirL_isFwd, bool dirR_isFwd, bool doBrake){
    // Cập nhật biến để in lên Web
    _actualSpeedL = doBrake ? 0 : pwmL;
    _actualSpeedR = doBrake ? 0 : pwmR;
    
    // Xử lý truyền lệnh xuống RoverSide
    if (doBrake) {
        _leftSide->brake();
        _rightSide->brake();
    } else {
        // Hàm setSpeed chỉ nhận 1 số: Tiến thì truyền số dương, Lùi thì truyền số âm
        _leftSide->setSpeed(dirL_isFwd ? pwmL : -pwmL);
        _rightSide->setSpeed(dirR_isFwd ? pwmR : -pwmR);
    }
}

//THUẬT TOÁN SOFT-START
void Rover::processSoftStart(int targetL, int targetR) {
    if (_currentPWM_L < targetL) {
        _currentPWM_L += RAMP_STEP;
        if (_currentPWM_L > targetL) _currentPWM_L = targetL;
    } else if (_currentPWM_L > targetL) {
        _currentPWM_L -= RAMP_STEP;
        if (_currentPWM_L < targetL) _currentPWM_L = targetL;
    }

    if (_currentPWM_R < targetR) {
        _currentPWM_R += RAMP_STEP;
        if (_currentPWM_R > targetR) _currentPWM_R = targetR;
    } else if (_currentPWM_R > targetR) {
        _currentPWM_R -= RAMP_STEP;
        if (_currentPWM_R < targetR) _currentPWM_R = targetR;
    }
}
void Rover::update(ControlCommand cmd)
{
    if (!cmd.connected)
    {
        applyDriveControl(0, 0, true, true, true);
        _currentState = MOTION_IDLE;
        _currentPWM_L = 0;
        _currentPWM_R = 0; //Reset soft-start
        return;
    }

    _desireState = determineDesiredState(cmd.pulseL, cmd.pulseR);
    // Kịch bản xe đang phanh cứng chờ RPM = 0
    if (_currentState == MOTION_BRAKING) {
        float max_rpm_system = 0;
        for(int i=0; i<6; i++) {
            max_rpm_system = max(max_rpm_system, abs((float)calculateRPM(i)));
        }

        bool isReadyToSwitch = false;
        if (max_rpm_system > RPM_SAFE_ZERO) {
            _zeroDetectTime = 0;
        } else {
            if (_zeroDetectTime == 0) _zeroDetectTime = millis();
            else if (millis() - _zeroDetectTime > CONFIRM_STOP_TIME) isReadyToSwitch = true;
        }

        if (millis() - _brakeStartTime > BRAKE_TIMEOUT) isReadyToSwitch = true;

        if (isReadyToSwitch) {
            _currentState = _nextMotionPending;
            _zeroDetectTime = 0;
            _currentPWM_L = 0;
            _currentPWM_R = 0; 
        } else {
            applyDriveControl(0, 0, true, true, true);
        }
        return;
    } 
    else {
        // Đảo chiều đột ngột -> Đạp phanh ngay
        if (_desireState != _currentState) {
            _currentState = MOTION_BRAKING;
            _nextMotionPending = _desireState;
            _brakeStartTime = millis();
            applyDriveControl(0, 0, true, true, true);
            _currentPWM_L = 0;
            _currentPWM_R = 0;
            return;
        }
    }

    // TÍNH TOÁN TARGET PWM TỨC THỜI (Bơm thẳng, không qua Soft-Start)
    int target_L = 0, target_R = 0;
    int joystick_L = mapPulseToPWM(cmd.pulseL);
    int joystick_R = mapPulseToPWM(cmd.pulseR);

    switch (_currentState) {
        case MOTION_IDLE: target_L = 0; target_R = 0; break;
        case MOTION_FORWARD: target_L = joystick_L; target_R = joystick_R; break;
        case MOTION_BACKWARD: target_L = PWM_FIXED_REVERSE; target_R = PWM_FIXED_REVERSE; break;
        case MOTION_SPIN_LEFT: target_L = PWM_FIXED_REVERSE; target_R = PWM_ASSIST_FWD; break;
        case MOTION_SPIN_RIGHT: target_L = PWM_ASSIST_FWD; target_R = PWM_FIXED_REVERSE; break;
        case MOTION_FWD_LEFT: target_L = PWM_FIXED_REVERSE; target_R = PWM_TURN_FWD; break;
        case MOTION_FWD_RIGHT: target_L = PWM_TURN_FWD; target_R = PWM_FIXED_REVERSE; break;
        case MOTION_BCK_LEFT: target_L = PWM_ASSIST_BCK; target_R = PWM_FIXED_REVERSE; break;
        case MOTION_BCK_RIGHT: target_L = PWM_FIXED_REVERSE; target_R = PWM_ASSIST_BCK; break;
        default: target_L = 0; target_R = 0; break;
    }
    // GỌI SOFT-START: Kéo _currentPWM tiến dần về target
    processSoftStart(target_L, target_R);
    
    // Lấy giá trị sau khi đã vuốt mềm để xuất ra phần cứng
    int out_L = (int)_currentPWM_L;
    int out_R = (int)_currentPWM_R;

    // XUẤT LỆNH RA MOTOR DRIVER NGAY LẬP TỨC
    switch (_currentState) {
        case MOTION_IDLE: applyDriveControl(out_L, out_R, true, true, true); break;
        case MOTION_FORWARD: applyDriveControl(out_L, out_R, true, true, false); break;
        case MOTION_BACKWARD: applyDriveControl(out_L, out_R, false, false, false); break;
        case MOTION_SPIN_LEFT: applyDriveControl(out_L, out_R, false, true, false); break;
        case MOTION_SPIN_RIGHT: applyDriveControl(out_L, out_R, true, false, false); break;
        case MOTION_FWD_LEFT: applyDriveControl(out_L, out_R, false, true, false); break;
        case MOTION_FWD_RIGHT: applyDriveControl(out_L, out_R, true, false, false); break;
        case MOTION_BCK_LEFT: applyDriveControl(out_L, out_R, true, false, false); break;
        case MOTION_BCK_RIGHT: applyDriveControl(out_L, out_R, false, true, false); break;
        default: applyDriveControl(0, 0, true, true, true); break;
    }
}
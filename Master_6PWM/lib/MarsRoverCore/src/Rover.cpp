#include "Rover.h"

// --- THỜI GIAN BẢO VỆ PHANH ---
// Vẫn giữ Timeout 2s làm phao cứu sinh lỡ đứt dây cảm biến Hall
const unsigned long BRAKE_TIMEOUT = 2000;
const unsigned long CONFIRM_STOP_TIME = 200; // Phải đứng im liên tục 200ms mới cho đảo chiều

// --- CÁC THÔNG SỐ CỐ ĐỊNH KHI CUA/LÙI ---
const int PWM_FIXED_REVERSE = -180; // Thêm dấu (-) để mạch hiểu là chiều Lùi
const int PWM_ASSIST_FWD = 100;
const int PWM_TURN_FWD = 110;
const int PWM_ASSIST_BCK = 85;

Rover::Rover()
{
    _currentMotion = MOTION_STOP;
    _nextMotionPending = MOTION_STOP;
    _isBraking = false;
    _zeroDetectTime = 0;
    _currentSpeedL = 0;
    _currentSpeedR = 0;
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

void Rover::update(ControlCommand cmd)
{
    if (!cmd.connected)
    {
        _leftSide->brake();
        _rightSide->brake();
        _currentSpeedL = 0;
        _currentSpeedR = 0;
        _currentMotion = MOTION_STOP;
        _isBraking = false;
        return;
    }

    // ====================================================================
    // 1. PHÂN LOẠI LỆNH TỪ JOYSTICK
    // ====================================================================
    int dirL = 0, dirR = 0;
    if (cmd.pulseL > 1520)
        dirL = 1;
    else if (cmd.pulseL < 1480)
        dirL = -1;
    if (cmd.pulseR > 1520)
        dirR = 1;
    else if (cmd.pulseR < 1480)
        dirR = -1;

    MotionType desireState = MOTION_STOP;
    if (dirL == 1 && dirR == 1)
        desireState = MOTION_FORWARD;
    else if (dirL == -1 && dirR == -1)
        desireState = MOTION_BACKWARD;
    else if (dirL == -1 && dirR == 1)
        desireState = MOTION_SPIN_LEFT;
    else if (dirL == 1 && dirR == -1)
        desireState = MOTION_SPIN_RIGHT;
    else if (dirL == 0 && dirR == 1)
        desireState = MOTION_FWD_LEFT;
    else if (dirL == 1 && dirR == 0)
        desireState = MOTION_FWD_RIGHT;
    else if (dirL == 0 && dirR == -1)
        desireState = MOTION_BCK_LEFT;
    else if (dirL == -1 && dirR == 0)
        desireState = MOTION_BCK_RIGHT;

    // ====================================================================
    // 2. MÁY TRẠNG THÁI BẢO VỆ ĐẢO CHIỀU (DỰA VÀO RPM = 0)
    // ====================================================================
    if (_isBraking)
    {
        bool isReadyToSwitch = false;

        // Điều kiện 1: Đợi RPM = 0 (Điều kiện chính xác nhất)
        if (!isRoverCompletelyStopped())
        {
            _zeroDetectTime = 0;
        }
        else
        {
            // Xác nhận đứng im liên tục trong 200ms
            if (_zeroDetectTime == 0)
                _zeroDetectTime = millis();
            else if (millis() - _zeroDetectTime > CONFIRM_STOP_TIME)
                isReadyToSwitch = true;
        }

        // Điều kiện 2: Phao cứu sinh Timeout (Lỡ cảm biến hỏng)
        if (millis() - _brakeStartTime > BRAKE_TIMEOUT)
            isReadyToSwitch = true;

        if (isReadyToSwitch)
        {
            _currentMotion = _nextMotionPending; // Nhả phanh, sang trạng thái mới
            _isBraking = false;
            _currentSpeedL = 0;
            _currentSpeedR = 0;
            _zeroDetectTime = 0;
        }
        else
        {
            _leftSide->brake();
            _rightSide->brake();
        }
        return; // Đang phanh thì không cấp ga
    }
    else
    {
        // Nếu phát hiện đổi trạng thái -> Kích hoạt Phanh ngay lập tức
        if (desireState != _currentMotion)
        {
            _isBraking = true;
            _nextMotionPending = desireState;
            _brakeStartTime = millis();
            _leftSide->brake();
            _rightSide->brake();
            _currentSpeedL = 0;
            _currentSpeedR = 0;
            return;
        }
    }

    // ====================================================================
    // 3. TÍNH TOÁN TARGET PWM TỨC THỜI (KHÔNG CÓ ĐỘ TRỄ)
    // ====================================================================
    int target_L = 0;
    int target_R = 0;

    // THẦY FIX NHẸ: Bọc hàm constrain để lỡ pulse có vọt lên 2100 thì ngõ ra vẫn max 255 (Chống tràn số)
    int joystick_L = constrain(map(cmd.pulseL, 1520, 2000, 0, 255), 0, 255);
    int joystick_R = constrain(map(cmd.pulseR, 1520, 2000, 0, 255), 0, 255);

    switch (_currentMotion)
    {
    case MOTION_STOP:
        target_L = 0;
        target_R = 0;
        break;
    case MOTION_FORWARD:
        target_L = joystick_L;
        target_R = joystick_R;
        break;
    case MOTION_BACKWARD:
        target_L = PWM_FIXED_REVERSE;
        target_R = PWM_FIXED_REVERSE;
        break;
    case MOTION_SPIN_LEFT:
        target_L = PWM_FIXED_REVERSE;
        target_R = PWM_ASSIST_FWD;
        break;
    case MOTION_SPIN_RIGHT:
        target_L = PWM_ASSIST_FWD;
        target_R = PWM_FIXED_REVERSE;
        break;
    case MOTION_FWD_LEFT:
        target_L = PWM_FIXED_REVERSE;
        target_R = PWM_TURN_FWD;
        break;
    case MOTION_FWD_RIGHT:
        target_L = PWM_TURN_FWD;
        target_R = PWM_FIXED_REVERSE;
        break;
    case MOTION_BCK_LEFT:
        target_L = PWM_ASSIST_BCK;
        target_R = PWM_FIXED_REVERSE;
        break;
    case MOTION_BCK_RIGHT:
        target_L = PWM_FIXED_REVERSE;
        target_R = PWM_ASSIST_BCK;
        break;
    }

    // Gán trực tiếp (Loại bỏ hoàn toàn RAMP_STEP)
    _currentSpeedL = target_L;
    _currentSpeedR = target_R;

    // ====================================================================
    // 4. XUẤT TÍN HIỆU RA MOTOR
    // ====================================================================
    if (_currentMotion == MOTION_STOP)
    {
        _leftSide->brake();
        _rightSide->brake();
    }
    else
    {
        _leftSide->setSpeed(_currentSpeedL);
        _rightSide->setSpeed(_currentSpeedR);
    }
}

MotionType Rover::getMotionType()
{
    return _currentMotion;
}
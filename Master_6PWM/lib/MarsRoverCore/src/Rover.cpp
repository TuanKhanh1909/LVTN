#include "Rover.h"
#include "SpeedMonitor.h"

Rover::Rover()
{
    _fsmState = STATE_1_SETUP; 
    _currentMotion = MOTION_IDLE; // Mồi ga ở hướng tiến mặc định
    _desireMotion = MOTION_IDLE;
    _setupPwm = 0;
    _lastSetupStepTime = 0; // Để nó kích hoạt ngay vòng lặp đầu tiên
    _appliedPWM_L = 0;
    _appliedPWM_R = 0;
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

int Rover::getDirection(uint16_t pulse)
{
    if (pulse > 1520)
        return 1;
    else if (pulse < 1480)
        return -1;
    return 0;
}
MotionType Rover::determineDesiredState(uint16_t pL, uint16_t pR)
{
    int dirL = getDirection(pL);
    int dirR = getDirection(pR);

    /*

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

    // 3. Phân tích nhóm TIẾN (Có ít nhất 1 bánh tiến)
    if (dirL == 1 || dirR == 1) {
        // Bánh trái quay nhanh hơn -> Xe ôm cua sang Phải
        if (pL > pR + 40) return MOTION_FWD_RIGHT;
        // Bánh phải quay nhanh hơn -> Xe ôm cua sang Trái
        if (pR > pL + 40) return MOTION_FWD_LEFT;
        // Tốc độ ngang nhau -> Tiến thẳng
        return MOTION_FORWARD;
    }

    */
    // 1. Đứng yên
    if (dirL == 0 && dirR == 0)
        return MOTION_IDLE;

    // 2. Quay tại chỗ (Spin)
    if (dirL == -1 && dirR == 1)
        return MOTION_SPIN_LEFT;
    if (dirL == 1 && dirR == -1)
        return MOTION_SPIN_RIGHT;

    // 3. Nhóm TIẾN (Sử dụng logic cũ)
    // - Khi cả 2 bánh cùng tiến, trả về FORWARD. Hệ thống sẽ tự bẻ lái mềm mại theo tay ga (nhờ chênh lệch PWM).
    // - Khi có 1 bánh đứng im, 1 bánh tiến -> Ép vào rẽ gắt FWD_LEFT / FWD_RIGHT.
    if (dirL == 1 && dirR == 1)
        return MOTION_FORWARD;
    if (dirL == 0 && dirR == 1)
        return MOTION_FWD_LEFT;
    if (dirL == 1 && dirR == 0)
        return MOTION_FWD_RIGHT;

    // 4. Phân tích nhóm LÙI (Có ít nhất 1 bánh lùi, bánh kia lùi hoặc đứng im)
    if (dirL == -1 || dirR == -1)
    {
        // LƯU Ý: Với xung < 1500, số CÀNG NHỎ thì lùi CÀNG NHANH

        // Bánh phải lùi nhanh hơn bánh trái -> Đuôi xe văng sang trái
        if (pR < pL - 40)
            return MOTION_BCK_LEFT;

        // Bánh trái lùi nhanh hơn bánh phải -> Đuôi xe văng sang phải
        if (pL < pR - 40)
            return MOTION_BCK_RIGHT;

        // Không lệch nhau nhiều -> Lùi thẳng
        return MOTION_BACKWARD;
    }

    return MOTION_IDLE;
}

// CHUYỂN ĐỔI MAP XUNG -> PWM
float Rover::mapPulseToPWM(uint16_t pulse){
    int pwm = 0;
    if (pulse > 1520) {
        pwm = map(pulse, 1520, 2000, 75, 255); // 75 là MIN_PWM_START
    } else if (pulse < 1480) {
        pwm = 180; // PWM_FIXED_REVERSE
    }
    return (float)constrain(pwm, 75, 255);
}

// --- KIỂM TRA ĐIỀU KIỆN RƠ-LE VÀ TRẠNG THÁI DỪNG ---
void Rover::getDirectionFlags(MotionType motion, bool &l_fwd, bool &r_fwd) {
    switch (motion) {
        case MOTION_FORWARD:  l_fwd = true;  r_fwd = true;  break;
        case MOTION_BACKWARD: l_fwd = false; r_fwd = false; break;
        case MOTION_SPIN_LEFT:l_fwd = false; r_fwd = true;  break;
        case MOTION_SPIN_RIGHT:l_fwd = true; r_fwd = false; break;
        case MOTION_FWD_LEFT: l_fwd = false; r_fwd = true;  break; // Trái lùi, Phải tiến
        case MOTION_FWD_RIGHT:l_fwd = true;  r_fwd = false; break; // Trái tiến, Phải lùi
        case MOTION_BCK_LEFT: l_fwd = true;  r_fwd = false; break; // Trái tiến, Phải lùi
        case MOTION_BCK_RIGHT:l_fwd = false; r_fwd = true;  break; // Trái lùi, Phải tiến
        default: l_fwd = true; r_fwd = true; break;
    }
}

bool Rover::requiresRelayChange(MotionType current, MotionType desired) {
    if (desired == MOTION_IDLE) return false;
    bool curL, curR, desL, desR;
    getDirectionFlags(current, curL, curR);
    getDirectionFlags(desired, desL, desR);
    return (curL != desL) || (curR != desR);
}

bool Rover::isFullyStopped() {
    for (int i = 0; i < 6; i++) {
        if (abs(calculateRPM(i)) > 5.0) return false; // Chỉ cần 1 bánh còn quay là false
    }
    return true;
}

// void Rover::applyDriveControl(float rpmL, float rpmR, bool dirL_isFwd, bool dirR_isFwd, bool doBrake){
//     _currentRPM_L = doBrake ? 0 : rpmL;
//     _currentRPM_R = doBrake ? 0 : rpmR;
    
//     if (doBrake) {
//         _leftSide->brake();
//         _rightSide->brake();
//     } else {
//         // Truyền thẳng lệnh xuống, bỏ qua dt vì PID giờ sẽ xử lý bên dưới
//         _leftSide->setTargetRPM(rpmL, dirL_isFwd);
//         _rightSide->setTargetRPM(rpmR, dirR_isFwd);
//     }
// }
// THUẬT TOÁN SOFT-START
/*
void Rover::processSoftStart(int targetL, int targetR)
{
    if (_currentPWM_L < targetL)
    {
        _currentPWM_L += RAMP_STEP;
        if (_currentPWM_L > targetL)
            _currentPWM_L = targetL;
    }
    else if (_currentPWM_L > targetL)
    {
        _currentPWM_L -= RAMP_STEP;
        if (_currentPWM_L < targetL)
            _currentPWM_L = targetL;
    }

    if (_currentPWM_R < targetR)
    {
        _currentPWM_R += RAMP_STEP;
        if (_currentPWM_R > targetR)
            _currentPWM_R = targetR;
    }
    else if (_currentPWM_R > targetR)
    {
        _currentPWM_R -= RAMP_STEP;
        if (_currentPWM_R < targetR)
            _currentPWM_R = targetR;
    }
}*/


void Rover::update(ControlCommand cmd)
{
    if (!cmd.connected) _desireMotion = MOTION_IDLE;
    else _desireMotion = determineDesiredState(cmd.pulseL, cmd.pulseR);

    // BÍ KÍP 1: Chặn bộ điều khiển, KHÔNG CHO ĐỔI HƯỚNG hoặc nhận lệnh khi ĐANG MỒI GA SETUP
    if (_fsmState == STATE_1_SETUP && _desireMotion != MOTION_IDLE) {
        _desireMotion = _currentMotion; // Khóa chặt hướng cũ
    }

    bool relayChange = requiresRelayChange(_currentMotion, _desireMotion);

    switch (_fsmState) {
        // ---------------------------------------------------
        // [TRẠNG THÁI 0]: NGỦ ĐÔNG AN TOÀN
        // ---------------------------------------------------
        case STATE_0_IDLE:
            _leftSide->brake();
            _rightSide->brake();
            _appliedPWM_L = 0;
            _appliedPWM_R = 0;
            if (_desireMotion != MOTION_IDLE) {
                _currentMotion = _desireMotion; // Chốt hướng mục tiêu
                _fsmState = STATE_1_SETUP;
                _setupPwm = 0;
                _lastSetupStepTime = millis();
            }
            break;

        // ---------------------------------------------------
        // [TRẠNG THÁI 1]: MỒI GA SETUP (100ms - 120ms)
        // ---------------------------------------------------
        case STATE_1_SETUP:
            if (millis() - _lastSetupStepTime >= 20) {
                _lastSetupStepTime = millis();
                _setupPwm += 10; // Tăng 20 PWM/20ms
                
                bool l_fwd, r_fwd;
                getDirectionFlags(_currentMotion, l_fwd, r_fwd);
                
                int pwmL = min(_setupPwm, 70);
                int pwmR = min(_setupPwm, 70);
                
                _leftSide->setDirectPWM(pwmL, l_fwd);
                _rightSide->setDirectPWM(pwmR, r_fwd);

                _appliedPWM_L = l_fwd ? pwmL : -pwmL;
                _appliedPWM_R = r_fwd ? pwmR : -pwmR;

                // Hoàn tất Setup
                if (_setupPwm >= 70) {
                    // Nếu mồi ga xong mà cần ga vẫn đang đứng yên -> Qua chế độ Phanh chờ
                    if (_desireMotion == MOTION_IDLE) {
                        _fsmState = STATE_3_IDLE_HOLD;
                    } else {
                        _fsmState = STATE_2_DRIVING;
                    }   
                }
            }
            break;

        // ---------------------------------------------------
        // [TRẠNG THÁI 2]: CHẠY TỰ DO
        // ---------------------------------------------------
        case STATE_2_DRIVING:
            if (relayChange) {
                _fsmState = STATE_4_SWITCH_DIR;
                _switchDirStartTime = millis();
                _zeroDetectTime = 0;
            } else if (_desireMotion == MOTION_IDLE) {
                bool l_fwd, r_fwd;
                getDirectionFlags(_currentMotion, l_fwd, r_fwd);
                if (l_fwd && r_fwd) {
                    _fsmState = STATE_3_IDLE_HOLD; // Phanh chờ Tiến
                } else {
                    _fsmState = STATE_0_IDLE; // Đang Lùi/Spin mà nhả ga -> Cắt điện luôn
                    _currentMotion = MOTION_IDLE;
                }
            } else {
                _currentMotion = _desireMotion;
                executeDrivingLogic(cmd); // Gọi logic lái bên dưới
            }
            break;

        // ---------------------------------------------------
        // [TRẠNG THÁI 3]: PHANH CHỜ TIẾN (ÉP 70 PWM)
        // ---------------------------------------------------
        case STATE_3_IDLE_HOLD:
           bool l_fwd, r_fwd;
                getDirectionFlags(_currentMotion, l_fwd, r_fwd);
                _leftSide->setDirectPWM(70, l_fwd);
                _rightSide->setDirectPWM(70, r_fwd);
            _appliedPWM_L = l_fwd ? 70 : -70;
            _appliedPWM_R = r_fwd ? 70 : -70;

            if (_desireMotion != MOTION_IDLE) {
                if (relayChange) {
                    _fsmState = STATE_4_SWITCH_DIR;
                    _switchDirStartTime = millis();
                    _zeroDetectTime = 0;
                } else {
                    _currentMotion = _desireMotion;
                    _fsmState = STATE_2_DRIVING; // Vọt đi luôn không cần mồi
                }
            }
            break;

        // ---------------------------------------------------
        // [TRẠNG THÁI 4]: PHANH ĐẢO CHIỀU + BỌC LÓT TIMEOUT
        // ---------------------------------------------------
        case STATE_4_SWITCH_DIR:
            _leftSide->brake(); 
            _rightSide->brake();
            _appliedPWM_L = 0;
            _appliedPWM_R = 0;
            
            bool stopped = isFullyStopped();
            bool timeoutMet = (millis() - _switchDirStartTime > TIMEOUT_RPM_ZERO);

            if (stopped || timeoutMet) {
                if (_zeroDetectTime == 0) {
                    _zeroDetectTime = millis();
                } else if (millis() - _zeroDetectTime > 200 || timeoutMet) { 
                    // Chờ ổn định 200ms HOẶC dính Timeout -> Cưỡng ép qua bước
                    _currentMotion = _desireMotion;
                    if (_desireMotion == MOTION_IDLE) {
                        _fsmState = STATE_0_IDLE;
                    } else {
                        _fsmState = STATE_1_SETUP;
                        _setupPwm = 0;
                        _lastSetupStepTime = millis();
                    }
                }
            } else {
                _zeroDetectTime = 0; // Xe vẫn đang trượt
            }
            break;
    }
}

void Rover::executeDrivingLogic(ControlCommand cmd) {
    float pwmL = mapPulseToPWM(cmd.pulseL);
    float pwmR = mapPulseToPWM(cmd.pulseR);

    // HẰNG SỐ TỪ CODE CŨ 
    const int PWM_FIXED_REVERSE = 180;
    const int PWM_ASSIST_FWD = 100;
    const int PWM_TURN_FWD = 110;
    const int PWM_ASSIST_BCK = 90;

    // Tính target_L và target_R tương tự logic cũ
    float target_L = 0, target_R = 0;
    
    switch (_currentMotion) {
        // Cả 2 bánh tự map theo tay cầm (Cả thẳng lẫn Rẽ)
        case MOTION_FORWARD: 
        case MOTION_FWD_LEFT: 
        case MOTION_FWD_RIGHT: 
            target_L = pwmL; target_R = pwmR; break;
        case MOTION_BACKWARD: target_L = PWM_FIXED_REVERSE; target_R = PWM_FIXED_REVERSE; break;
        case MOTION_SPIN_LEFT: target_L = PWM_FIXED_REVERSE; target_R = PWM_ASSIST_FWD; break;
        case MOTION_SPIN_RIGHT: target_L = PWM_ASSIST_FWD; target_R = PWM_FIXED_REVERSE; break;
        //case MOTION_FWD_LEFT: target_L = PWM_FIXED_REVERSE; target_R = PWM_TURN_FWD; break;
        //case MOTION_FWD_RIGHT: target_L = PWM_TURN_FWD; target_R = PWM_FIXED_REVERSE; break;
        case MOTION_BCK_LEFT: target_L = PWM_ASSIST_BCK; target_R = PWM_FIXED_REVERSE; break;
        case MOTION_BCK_RIGHT: target_L = PWM_FIXED_REVERSE; target_R = PWM_ASSIST_BCK; break;
        default: target_L = 0; target_R = 0; break;
    }

    bool l_fwd, r_fwd;
    getDirectionFlags(_currentMotion, l_fwd, r_fwd);

    // Xuất thẳng PWM
    if (l_fwd) {
        _leftSide->setDirectPWM((int)target_L, true);
        _appliedPWM_L = target_L;
    } else {
        _leftSide->setDirectPWM((int)target_L, false);
        _appliedPWM_L = -target_L; // Biểu thị đang lùi
    }

    if (r_fwd) {
        _rightSide->setDirectPWM((int)target_R, true);
        _appliedPWM_R = target_R;
    } else {
        _rightSide->setDirectPWM((int)target_R, false);
        _appliedPWM_R = -target_R;
    }
}
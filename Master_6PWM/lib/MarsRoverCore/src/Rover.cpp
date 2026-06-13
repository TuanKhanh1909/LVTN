#include "Rover.h"
#include "SpeedMonitor.h"

// ==============================================================================
// HÀM KHỞI TẠO (CONSTRUCTOR) - ĐƯỢC GỌI 1 LẦN DUY NHẤT KHI BẬT NGUỒN ESP32
// ==============================================================================
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
    int dirL = getDirection(pL); // Lấy hướng hiện tại của cụm Trái
    int dirR = getDirection(pR); // Lấy hướng hiện tại của cụm Phải

    // 1. Nếu cả 2 tay gạt đều nằm trong vùng chết -> Xe đứng yên
    if (dirL == 0 && dirR == 0)
        return MOTION_IDLE;

    // 2. Kịch bản Xoay tại chỗ (Spin) - Hai bánh chạy ngược chiều nhau
    if (dirL == -1 && dirR == 1)
        return MOTION_SPIN_LEFT; // Trái lùi, Phải tiến -> Xoay Trái
    if (dirL == 1 && dirR == -1)
        return MOTION_SPIN_RIGHT; // Trái tiến, Phải lùi -> Xoay Phải

    // 3. Phân tích Nhóm TIẾN (Có ít nhất 1 bánh Tiến, KHÔNG CÓ bánh nào Lùi)
    if ((dirL == 1 || dirR == 1) && (dirL != -1 && dirR != -1))
    {
        // Lấy chênh lệch xung giữa 2 bánh để xác định ôm cua.
        // Ngưỡng 15 là rất nhỏ, giúp xe cực kỳ nhạy bén nhận diện lệnh bẻ lái dù đang chạy siêu chậm.
        if (pL > pR + 15)
            return MOTION_FWD_RIGHT; // Bánh trái bơm mạnh hơn bánh phải -> Ép xe rẽ Phải
        if (pR > pL + 15)
            return MOTION_FWD_LEFT; // Bánh phải bơm mạnh hơn bánh trái -> Ép xe rẽ Trái
        return MOTION_FORWARD;      // Hai bánh lệch nhau không đáng kể -> Đi thẳng
    }

    // 4. Phân tích Nhóm LÙI (Có ít nhất 1 bánh Lùi, KHÔNG CÓ bánh nào Tiến)
    if ((dirL == -1 || dirR == -1) && (dirL != 1 && dirR != 1))
    {
        // LƯU Ý MẠNG XUNG RC: Với xung < 1500, con số CÀNG NHỎ (ví dụ 1000) thì động cơ chạy CÀNG NHANH
        if (pR < pL - 15)
            return MOTION_BCK_LEFT; // Bánh phải lùi mạnh hơn bánh trái -> Kéo văng đuôi xe sang Trái
        if (pL < pR - 15)
            return MOTION_BCK_RIGHT; // Bánh trái lùi mạnh hơn bánh phải -> Kéo văng đuôi xe sang Phải
        return MOTION_BACKWARD;      // Lệch không đáng kể -> Lùi thẳng
    }

    // Nếu lọt ra ngoài các kịch bản trên -> Trả về Đứng yên an toàn
    return MOTION_IDLE;
}

// CHUYỂN ĐỔI MAP XUNG -> PWM
float Rover::mapPulseToPWM(uint16_t pulse)
{
    if (pulse > 1530)
        return (float)map(pulse, 1530, 2000, 85L, 255L);
    if (pulse < 1470)
        return (float)map(pulse, 1470, 1000, 85L, 255L);
    return 0.0;
}

// --- KIỂM TRA ĐIỀU KIỆN RƠ-LE VÀ TRẠNG THÁI DỪNG ---
void Rover::getDirectionFlags(MotionType motion, bool &l_fwd, bool &r_fwd)
{
    // Xác định chính xác trạng thái chân Rơ-le (DIR Pin) cho từng hành động
    switch (motion)
    {
    case MOTION_FORWARD:
    case MOTION_FWD_LEFT:
    case MOTION_FWD_RIGHT:
    case MOTION_IDLE: // Mặc định IDLE chốt rơ le ở chiều Tiến
        l_fwd = true;
        r_fwd = true;
        break;
    case MOTION_BACKWARD:
    case MOTION_BCK_LEFT:
    case MOTION_BCK_RIGHT:
        l_fwd = false; // Lùi -> Tắt Rơ le tiến
        r_fwd = false;
        break;
    case MOTION_SPIN_LEFT:
        l_fwd = false; // Trái lùi
        r_fwd = true;  // Phải tiến
        break;
    case MOTION_SPIN_RIGHT:
        l_fwd = true;  // Trái tiến
        r_fwd = false; // Phải lùi
        break;
    default:
        l_fwd = true;
        r_fwd = true;
        break;
    }
}

bool Rover::requiresRelayChange(MotionType current, MotionType desired)
{
    if (desired == MOTION_IDLE || current == MOTION_IDLE)
        return false;
    bool curL, curR, desL, desR;

    getDirectionFlags(current, curL, curR); // Lấy trạng thái rơ-le hiện tại
    getDirectionFlags(desired, desL, desR); // Lấy trạng thái rơ-le mong muốn

    // Nếu có ít nhất 1 bánh cần đảo rơ-le -> Trả về TRUE (Báo động cần phanh!)
    return (curL != desL) || (curR != desR);
}

bool Rover::isFullyStopped()
{
    // Quét qua 6 bánh xe. Chỉ cần 1 bánh có vòng tua > 5.0 RPM thì xe bị coi là chưa dừng hẳn.
    for (int i = 0; i < 6; i++)
    {
        if (abs(calculateRPM(i)) > 5.0)
            return false;
    }
    return true; // Tất cả đều < 5.0 RPM -> Trả về TRUE
}

void Rover::update(ControlCommand cmd)
{
    // Nếu mất sóng tay cầm -> Ép lệnh mục tiêu về IDLE để dừng xe
    if (!cmd.connected)
        _desireMotion = MOTION_IDLE;
    else
        _desireMotion = determineDesiredState(cmd.pulseL, cmd.pulseR); // Nếu có sóng -> Tính toán hướng lái

    // [LỚP PHÒNG THỦ TUYỆT ĐỐI LUẬT SẮT]:
    // Bất cứ khi nào nhận lệnh lật Rơ-le (Ví dụ đang Lùi mà gạt Tiến),
    // Lập tức ném hệ thống vào STATE_4 để cắt điện và đóng phanh!
    if (requiresRelayChange(_currentMotion, _desireMotion) && _fsmState != STATE_4_SWITCH_DIR)
    {
        _fsmState = STATE_4_SWITCH_DIR; // Chuyển sang State 4
        _switchDirStartTime = millis(); // Ghi nhận mốc thời gian bắt đầu phanh
        _zeroDetectTime = 0;            // Reset bộ đếm xác nhận dừng
    }

    switch (_fsmState)
    {
    // ---------------------------------------------------
    // [TRẠNG THÁI 0]: NGỦ ĐÔNG AN TOÀN
    // ---------------------------------------------------
    case STATE_0_IDLE:
        // Đóng rơ-le phanh cơ khí phần cứng
        _leftSide->brake();
        _rightSide->brake();
        // Cập nhật lên Web: PWM = 0
        _appliedPWM_L = 0;
        _appliedPWM_R = 0;

        // Nếu người lái bắt đầu gạt cần (Có lệnh khác IDLE)
        if (_desireMotion != MOTION_IDLE)
        {
            // Các lệnh ngược chiều đã bị bộ IF tổng bên trên tóm cổ.
            // Lọt được vào đây nghĩa là lệnh CÙNG CHIỀU với Rơ-le hiện tại.
            _currentMotion = _desireMotion; // Chốt hướng mục tiêu
            _fsmState = STATE_1_SETUP;      // Chuyển sang mồi ga
            _setupPwm = 0;                  // Bắt đầu mồi từ 0
            _lastSetupStepTime = millis();
        }
        break;

    // ---------------------------------------------------
    // [TRẠNG THÁI 1]: MỒI GA SETUP (100ms - 120ms)
    // ---------------------------------------------------
    case STATE_1_SETUP:
        if (_desireMotion != MOTION_IDLE)
        {
            _desireMotion = _currentMotion; // Ép giữ lại đúng cái hướng lúc vừa chớm mồi
        }
        else
        {
            _fsmState = STATE_0_IDLE; // Nhả tay thì hủy bỏ mồi ga lập tức
            break;
        }
        if (millis() - _lastSetupStepTime >= 20)
        {
            _lastSetupStepTime = millis();
            _setupPwm += 7; // Tăng 7 PWM/20ms

            bool l_fwd, r_fwd;
            getDirectionFlags(_currentMotion, l_fwd, r_fwd); // Đọc hướng để xuất Rơ-le

            // Bơm PWM Mồi xuống Motor (Chỉ bơm tới mức tối đa là 70, bỏ qua Joystick)
            int pwmL = min(_setupPwm, 70);
            int pwmR = min(_setupPwm, 70);

            _leftSide->setDirectPWM(pwmL, l_fwd);
            _rightSide->setDirectPWM(pwmR, r_fwd);

            _appliedPWM_L = l_fwd ? pwmL : -pwmL;
            _appliedPWM_R = r_fwd ? pwmR : -pwmR;

            // Hoàn tất Setup
            if (_setupPwm >= 70)
            {
                // Nếu mồi ga xong mà cần ga vẫn đang đứng yên -> Qua chế độ Phanh chờ
                if (_desireMotion == MOTION_IDLE)
                {
                    _fsmState = STATE_3_IDLE_HOLD;
                }
                else
                {
                    _fsmState = STATE_2_DRIVING;
                }
            }
        }
        break;

    // ---------------------------------------------------
    // [TRẠNG THÁI 2]: CHẠY TỰ DO
    // ---------------------------------------------------
    case STATE_2_DRIVING:
        if (_desireMotion == MOTION_IDLE)
        {
            _fsmState = STATE_3_IDLE_HOLD; // Nhả tay -> Phanh chờ 70
        }
        else
        {
            // Cập nhật hướng liên tục và đổ thẳng PWM vào motor
            _currentMotion = _desireMotion;
            executeDrivingLogic(cmd);
        }
        break;

    // ---------------------------------------------------
    // [TRẠNG THÁI 3]: PHANH CHỜ TIẾN (ÉP 70 PWM)
    // ---------------------------------------------------
    case STATE_3_IDLE_HOLD:
    {
        bool l_fwd, r_fwd;
        getDirectionFlags(_currentMotion, l_fwd, r_fwd);
        _leftSide->setDirectPWM(70, l_fwd);
        _rightSide->setDirectPWM(70, r_fwd);
        _appliedPWM_L = l_fwd ? 70 : -70;
        _appliedPWM_R = r_fwd ? 70 : -70;

        if (_desireMotion != MOTION_IDLE)
        {
            // Nếu lệnh mới ngược chiều -> IF đầu tiên đã ném nó sang STATE 4 rồi.
            // Vào được đây là cùng chiều -> Vọt luôn sang STATE 2 không cần mồi!
            _currentMotion = _desireMotion;
            _fsmState = STATE_2_DRIVING;
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

        // BƯỚC 1: BẮT BUỘC ĐỨNG IM TRONG 200MS ĐẦU TIÊN
        // Mục đích: Chờ năng lượng trong tụ và cuộn dây tiêu tán hết, chống hồ quang điện.
        if (millis() - _switchDirStartTime < 200)
        {
            _zeroDetectTime = 0;
            break;
        }

        // BƯỚC 2: SAU 200MS, BẮT ĐẦU ĐỌC CẢM BIẾN VÒNG TUA (HALL SENSOR)
        {
            bool stopped = isFullyStopped();
            bool timeoutMet = (millis() - _switchDirStartTime > 2000);

            if (stopped || timeoutMet)
            {
                // Vừa mới thấy nó dừng -> Bắt đầu đếm giờ
                if (_zeroDetectTime == 0)
                {
                    _zeroDetectTime = millis();
                }
                else if (millis() - _zeroDetectTime >= 100 || timeoutMet)
                {
                    _currentMotion = _desireMotion;
                    if (_desireMotion == MOTION_IDLE)
                    {
                        _fsmState = STATE_0_IDLE;
                    }
                    else
                    {
                        // Đảo chiều xong -> Về STATE 1 mồi ga lại!
                        _fsmState = STATE_1_SETUP;
                        _setupPwm = 0;
                        _lastSetupStepTime = millis();
                    }
                }
            }
            else
            {
                _zeroDetectTime = 0;
            }
        }
        break;
    }
}

void Rover::executeDrivingLogic(ControlCommand cmd)
{
    float pwmL = mapPulseToPWM(cmd.pulseL);
    float pwmR = mapPulseToPWM(cmd.pulseR);

    // Giới hạn dưới để chống chết bánh khi rẽ gắt
    const float MIN_DRIVE_PWM = 85.0;

    // Tính target_L và target_R tương tự logic cũ
    float target_L = 0, target_R = 0;

    switch (_currentMotion)
    {
    // ------------------------------------
    // NHÓM ĐI THẲNG: Cấp 100% PWM tay cầm
    // ------------------------------------
    case MOTION_FORWARD:
    case MOTION_BACKWARD: // Lùi bây giờ được bám 100% theo Joystick
        target_L = pwmL;
        target_R = pwmR;
        break;

    case MOTION_FWD_LEFT:
    case MOTION_BCK_LEFT:
        target_R = pwmR;
        target_L = pwmR * 0.6f;
        if (target_L < MIN_DRIVE_PWM)
            target_L = MIN_DRIVE_PWM;
        break;

    case MOTION_FWD_RIGHT:
    case MOTION_BCK_RIGHT:
        target_L = pwmL;
        target_R = pwmL * 0.6f;
        if (target_R < MIN_DRIVE_PWM)
            target_R = MIN_DRIVE_PWM;
        break;

    // Đã GHIM CHẶT (Hardcode) PWM như ý muốn: Bánh Tiến = 100, Bánh Lùi = 180
    case MOTION_SPIN_LEFT:
        target_L = 180.0f; // Bánh Trái quay lùi -> Cấp 180 PWM để thắng cản
        target_R = 100.0f; // Bánh Phải quay tiến -> Cấp 100 PWM
        break;

    case MOTION_SPIN_RIGHT:
        target_L = 100.0f; // Bánh Trái quay tiến -> Cấp 100 PWM
        target_R = 180.0f; // Bánh Phải quay lùi -> Cấp 180 PWM
        break;

    default:
        break;
    }

    bool l_fwd, r_fwd;
    getDirectionFlags(_currentMotion, l_fwd, r_fwd);

    // Xuất thẳng PWM
    if (l_fwd)
    {
        _leftSide->setDirectPWM((int)target_L, true);
        _appliedPWM_L = target_L;
    }
    else
    {
        _leftSide->setDirectPWM((int)target_L, false);
        _appliedPWM_L = -target_L; // Biểu thị đang lùi
    }

    if (r_fwd)
    {
        _rightSide->setDirectPWM((int)target_R, true);
        _appliedPWM_R = target_R;
    }
    else
    {
        _rightSide->setDirectPWM((int)target_R, false);
        _appliedPWM_R = -target_R;
    }
}
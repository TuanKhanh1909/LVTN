#include <Arduino.h>
#include <unity.h>
#include "InputManager.h"
#include "Rover.h"
#include "BldcDriver.h"

// =========================================================================
// KHỞI TẠO ĐỐI TƯỢNG ĐỂ TEST
// =========================================================================
InputManager inputMgr;

// Tạo các đối tượng giả (Dummy) cho Rover để không báo lỗi phần cứng
BldcDriver m_dummy(255, 0); 
RoverSide sideL_dummy(255, 255, true);
RoverSide sideR_dummy(255, 255, false);
Rover testRover;
//Hàm setUp() chạy TRƯỚC mỗi test case (giống SetUp() trong GTest)
void setUp(void){
    inputMgr = InputManager(); //Khởi tạo lại để dữ liệu luôn sạch

    // Khởi tạo lại Rover cho sạch sẽ
    testRover = Rover();
    sideL_dummy.addMotor(&m_dummy);
    sideR_dummy.addMotor(&m_dummy);
    testRover.setSides(&sideL_dummy, &sideR_dummy);
    testRover.update({1500, 1500, false});
}

//Hàm tearDown() chạy SAU mỗi test case

void tearDown(void){
    //Dọn dẹp nếu cần
}

//=============================================
//CÁC TEST CASE
//=============================================

// =========================================================================
// NHÓM 1: TOÁN HỌC & TÍNH TOÁN (TẬP TRUNG VÀO INPUT MIXING)
// =========================================================================

// Test 1.1: BVA & Scaling - Hàm RC khi thao tác cực đại (Max Throttle + Max Steering)
// Nhắm vào lỗi: Chuyển đổi tỉ lệ (Scaling) và Tràn biên
void test_math_rc_scaling_max_input() {
    // Bơm ga tối đa (2000) và rẽ phải tối đa (2000)
    inputMgr.updateRC(2000, 2000); 
    ControlCommand cmd = inputMgr.getCommand();
    
    // Theo toán học: Bánh trái = Ga + Lái = 500 + 500 = 1000. 
    // Cộng mốc 1500 = 2500. Bắt buộc phải bị hàm constrain ép về 2000.
    TEST_ASSERT_EQUAL(2000, cmd.pulseL);
    
    // Bánh phải = Ga - Lái = 500 - 500 = 0.
    // Cộng mốc 1500 = 1500 (Đứng yên).
    TEST_ASSERT_EQUAL(1500, cmd.pulseR);
}
// Test 1.2: BVA & Robustness - Hàm RC bị lỗi phần cứng (Gửi số 0 và số Max của uint16_t)
// Nhắm vào lỗi: Tràn số (Underflow/Overflow)
void test_math_rc_hardware_fault_underflow() {
    // Giả lập cáp đứt, tín hiệu RC trả về 0 (0 < 800) hoặc chập mạch trả về 65535
    inputMgr.updateRC(0, 65535);
    ControlCommand cmd = inputMgr.getCommand();
    
    // Yêu cầu: Hệ thống bộ lọc (if Throttle < 800) phải từ chối ngay lập tức.
    // Do đó, lệnh được trả ra phải là lệnh an toàn mặc định (1500)
    TEST_ASSERT_EQUAL(1500, cmd.pulseL);
    TEST_ASSERT_EQUAL(1500, cmd.pulseR);
    TEST_ASSERT_EQUAL(SOURCE_NONE, inputMgr.getActiveSource()); // Không ghi nhận nguồn RC
}

// Test 1.3: Dấu phẩy động (Floating Point) - Hàm Web khi biến trở Pot = 0
// Nhắm vào lỗi: Sai số thập phân gây trôi xe
void test_math_web_float_precision_zero_pot() {
    // Dù người dùng gạt cần Joystick hết mức (X=4095, Y=4095)
    // Nhưng Slider Tốc độ (pot) = 0
    inputMgr.updateWeb(4095, 4095, 0);
    ControlCommand cmd = inputMgr.getCommand();

    // Hệ số speed_factor = pot / 4095.0 bắt buộc phải bằng 0.0
    // Nếu có sai số, pulse sẽ bị lệch khỏi 1500. 
    // Ta khẳng định tuyệt đối nó phải triệt tiêu về đúng 1500.
    TEST_ASSERT_EQUAL(1500, cmd.pulseL);
    TEST_ASSERT_EQUAL(1500, cmd.pulseR);
}

// Test 1.4: Robustness & BVA - Hàm Web khi bị hack / rớt mạng (Dữ liệu rác siêu lớn)
// Nhắm vào lỗi: Tràn số nguyên (Integer Overflow) đánh sập bộ nhớ
void test_math_web_extreme_garbage_data() {
    int garbageX = -9999999;
    int garbageY =  9999999;
    int garbagePot = 888888; // Vượt xa mức 4095

    inputMgr.updateWeb(garbageX, garbageY, garbagePot);
    ControlCommand cmd = inputMgr.getCommand();

    // Bất chấp dữ liệu rác làm toán học bên trong sinh ra con số hàng chục ngàn
    // Khẳng định (Assert) hệ thống Input Validation đã chặn đứng dữ liệu này,
    // Không cho phép tính toán và trả về lệnh DỪNG XE (1500) an toàn tuyệt đối.
    TEST_ASSERT_EQUAL(1500, cmd.pulseL);
    TEST_ASSERT_EQUAL(1500, cmd.pulseR);
    
    // Nguồn Web phải bị đánh rớt (Không được coi là Active Source nữa)
    TEST_ASSERT_NOT_EQUAL(SOURCE_WEB, inputMgr.getActiveSource());
}

// Test 1.5: BVA & Logic - Kiểm tra Soft-start và lỗi Dao động (Oscillation)
void test_math_rover_softstart_boundary() {
    ControlCommand maxCmd = {2000, 2000, true}; // Lệnh chạy Tiến tối đa (Target = 255)
    
    // Giả lập hệ thống chạy 100 chu kỳ liên tục để ép tốc độ lên mức Max
    for(int i = 0; i < 100; i++) {
        testRover.update(maxCmd);
    }
    
    // Lần 1: Khẳng định tốc độ đã đạt đỉnh 255 và không bị vượt lố (Overflow) thành 260
    TEST_ASSERT_EQUAL_FLOAT(255.0, testRover.getCurrentSpeedL());
    
    // Lần 2: Chạy thêm 1 chu kỳ nữa. 
    // Nếu thuật toán đúng, tốc độ phải GIỮ NGUYÊN 255. 
    // Nếu thuật toán sai (như hiện tại), nó sẽ bị tụt xuống 250 và Test này sẽ báo FAIL!
    testRover.update(maxCmd);
    TEST_ASSERT_EQUAL_FLOAT(255.0, testRover.getCurrentSpeedL()); 
}

// Test 1.6: Scaling & Float - Kiểm tra hệ số Trim của động cơ
void test_math_bldc_trim_scaling() {
    BldcDriver testMotor(27, 0); // Tạo 1 motor ảo
    
    // Kịch bản 1: Trim = 0.9 (90%). Nếu nhập vào Max (255) thì ngõ ra phải là 229 (255 * 0.9 = 229.5, ép kiểu về int là 229).
    testMotor.setTrim(0.9);
    // Đoạn này ta tính tay xem 255 * 0.9 = bao nhiêu
    int expectedPWM = (int)(255 * 0.9); 
    TEST_ASSERT_EQUAL(229, expectedPWM); 

    // Kịch bản 2: BVA - Bảo vệ biến Trim không bị lỗi số âm hoặc lố 100%
    testMotor.setTrim(1.5);  // Cố tình nhập Trim = 150% (Sai logic)
    // Đọc ngược lại từ Object: Khẳng định nó TỰ ĐỘNG ép về 1.0
    TEST_ASSERT_EQUAL_FLOAT(1.0, testMotor.getTrim());
    
    testMotor.setTrim(-0.5); // Cố tình nhập Trim âm
    // Đọc ngược lại từ Object: Khẳng định nó TỰ ĐỘNG ép về 0.0
    TEST_ASSERT_EQUAL_FLOAT(0.0, testMotor.getTrim());
}

//===============================================
// HÀM MAIN CHẠY TEST
//===============================================

void setup(){
    // Chờ Serial Monitor kết nối để in kết quả
    delay(2000);

    UNITY_BEGIN(); //Khởi động Framework

   // Chạy 4 Test Case Nhóm 1: TOÁN HỌC & TÍNH TOÁN
    RUN_TEST(test_math_rc_scaling_max_input);
    RUN_TEST(test_math_rc_hardware_fault_underflow);
    RUN_TEST(test_math_web_float_precision_zero_pot);
    RUN_TEST(test_math_web_extreme_garbage_data);
    RUN_TEST(test_math_rover_softstart_boundary);
    RUN_TEST(test_math_bldc_trim_scaling);

    UNITY_END();
}

void loop(){
    // Code test chạy 1 lần trong setup rồi dừng lại ở đây
}
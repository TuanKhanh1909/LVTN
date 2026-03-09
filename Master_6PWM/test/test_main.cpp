#include <Arduino.h>
#include <unity.h>
#include "InputManager.h"
#include "Rover.h"
#include "NetworkService.h"
#include <WiFi.h>

// =========================================================================
// KHỞI TẠO ĐỐI TƯỢNG ĐỂ TEST
// =========================================================================
InputManager inputMgr;

// Tạo các đối tượng giả (Dummy) cho Rover để không báo lỗi phần cứng
BldcDriver m_dummy(255, 0); // Chân ảo 255
RoverSide sideL_dummy(255, 255, true);
RoverSide sideR_dummy(255, 255, false);
Rover testRover;

NetworkService testNet(&inputMgr);

//Hàm setUp() chạy TRƯỚC mỗi test case (giống SetUp() trong GTest)
void setUp(void){
    inputMgr = InputManager(); //Khởi tạo lại để dữ liệu luôn sạch
    testRover = Rover();

    sideL_dummy.addMotor(&m_dummy);
    sideR_dummy.addMotor(&m_dummy);
    testRover.setSides(&sideL_dummy, &sideR_dummy);
    
    // Gửi lệnh rỗng để khởi tạo FSM của Rover
    ControlCommand emptyCmd = {1500, 1500, false};
    testRover.update(emptyCmd);
}

//Hàm tearDown() chạy SAU mỗi test case

void tearDown(void){
    //Dọn dẹp nếu cần
}

//=============================================
//CÁC TEST CASE
//=============================================

// =========================================================================
// NHÓM 1: TEST THUẬT TOÁN TRỘN KÊNH (MIXING KINEMATICS)
// =========================================================================

// 1. Test RC Lái thẳng: Xung chuẩn tiến
void test_rc_move_forward() {
    inputMgr.updateRC(2000, 1500); // Ga = Tối đa tiến, Lái = Thẳng
    ControlCommand cmd = inputMgr.getCommand();
    
    TEST_ASSERT_EQUAL(2000, cmd.pulseL);
    TEST_ASSERT_EQUAL(2000, cmd.pulseR);
}

// 2. Test RC Xoay tại chỗ (Zero-Turn)
void test_rc_spin_right() {
    inputMgr.updateRC(1500, 2000); // Ga = Đứng yên, Lái = Rẽ phải max
    ControlCommand cmd = inputMgr.getCommand();
    
    // Rẽ phải tại chỗ: Bánh trái tiến, bánh phải lùi
    TEST_ASSERT_EQUAL(2000, cmd.pulseL);
    TEST_ASSERT_EQUAL(1000, cmd.pulseR);
}

// 3. Test Vùng chết (Deadband) chống rung tay ga RC
void test_rc_deadband_noise() {
    // Xung RC dao động nhẹ quanh 1500 (Nhiễu do tay rung)
    inputMgr.updateRC(1515, 1485); 
    ControlCommand cmd = inputMgr.getCommand();
    
    // Hệ thống phải lọc nhiễu và ép về đúng 1500 (Đứng yên)
    TEST_ASSERT_EQUAL(1500, cmd.pulseL);
    TEST_ASSERT_EQUAL(1500, cmd.pulseR);
}

// 4. Test ESP-NOW (Chỉ truyền mộc, không trộn)
void test_espnow_passthrough() {
    inputMgr.updateEspNow(1850, 1150);
    ControlCommand cmd = inputMgr.getCommand();
    
    TEST_ASSERT_EQUAL(1850, cmd.pulseL);
    TEST_ASSERT_EQUAL(1150, cmd.pulseR);
    TEST_ASSERT_EQUAL(SOURCE_ESP_NOW, inputMgr.getActiveSource());
}

// =========================================================================
// NHÓM 2: TEST TÍNH NĂNG BẢO VỆ AN TOÀN (FAILSAFE)
// =========================================================================

// 5. Mất tín hiệu phải tự động phanh
void test_failsafe_timeout() {
    inputMgr.updateRC(2000, 2000); // Đang chạy cực nhanh
    inputMgr.getCommand();
    TEST_ASSERT_EQUAL(SOURCE_RC, inputMgr.getActiveSource());

    // Giả lập thời gian trôi qua 300ms (Vượt quá SIGNAL_TIMEOUT_MS = 200ms)
    delay(300); 

    // Lấy lại lệnh hiện tại
    ControlCommand cmd = inputMgr.getCommand();
    
    // Yêu cầu hệ thống phải tự cắt về 1500 và ngắt kết nối
    TEST_ASSERT_EQUAL(1500, cmd.pulseL);
    TEST_ASSERT_EQUAL(1500, cmd.pulseR);
    TEST_ASSERT_FALSE(cmd.connected);
    TEST_ASSERT_EQUAL(SOURCE_NONE, inputMgr.getActiveSource());
}

// =========================================================================
// NHÓM 3: TEST MÁY TRẠNG THÁI (FSM) CỦA ROVER
// =========================================================================

// 6. Test gia tốc mềm (Soft-Start)
void test_rover_soft_start() {
    ControlCommand fwdCmd = {2000, 2000, true};
    
    // Cấp điện lần 1 (Nhờ RAMP_STEP = 5, tốc độ chưa thể lên 255 ngay được)
    testRover.update(fwdCmd);
    testRover.update(fwdCmd);
    MotionType state1 = testRover.getMotionType();
    
    // Phải ở trạng thái tiến
    TEST_ASSERT_EQUAL(MOTION_FORWARD, state1);
}

// 7. Test BẢO VỆ CẦU H (Phanh khẩn cấp khi đảo chiều đột ngột)
void test_rover_anti_jerk_protection() {
    // Bước 1: Cho xe chạy TIẾN thật nhanh (Gọi update nhiều lần để bơm Soft-start)
    ControlCommand fwdCmd = {2000, 2000, true};
    for(int i=0; i<20; i++) testRover.update(fwdCmd); 
    
    TEST_ASSERT_EQUAL(MOTION_FORWARD, testRover.getMotionType());

    // Bước 2: Bác tài gạt cần lùi đột ngột! (Pulse = 1000)
    ControlCommand reverseCmd = {1000, 1000, true};
    testRover.update(reverseCmd);

    // Bước 3: Kiểm chứng
    // Xe TUYỆT ĐỐI KHÔNG ĐƯỢC lùi ngay. Nó phải rơi vào trạng thái BRAKING.
    // Khi đang BRAKING, hàm getMotionType() sẽ trả về MOTION_STOP.
    MotionType panicState = testRover.getMotionType();
    TEST_ASSERT_EQUAL(MOTION_STOP, panicState);
}

// 8. Chết mạng -> Xe tự khóa phanh
void test_rover_network_loss_stop() {
    // Đang chạy
    ControlCommand fwdCmd = {2000, 2000, true};
    testRover.update(fwdCmd);
    testRover.update(fwdCmd);
    TEST_ASSERT_EQUAL(MOTION_FORWARD, testRover.getMotionType());

    // Đột ngột mất mạng (connected = false)
    ControlCommand deadCmd = {2000, 2000, false};
    testRover.update(deadCmd);

    // Xe phải dừng ngay lập tức bỏ qua mọi thứ
    TEST_ASSERT_EQUAL(MOTION_STOP, testRover.getMotionType());
}

// =========================================================================
// NHÓM 4: TEST HỆ THỐNG MẠNG (NETWORK SYSTEM)
// =========================================================================

// 9. Test Thermal Protection (Tắt WiFi bảo vệ chip sau 5 giây)
void test_network_thermal_protection() {
    // 1. Khởi động hệ thống mạng
    testNet.begin(); 
    
    // 2. Kiểm chứng 1: Vừa khởi động xong, WiFi phải đang BẬT
    // Trong ESP32, WIFI_MODE_NULL nghĩa là WiFi đã tắt hoàn toàn
    TEST_ASSERT_NOT_EQUAL(WIFI_MODE_NULL, WiFi.getMode());
    
    // 3. Kích hoạt cập nhật lần đầu (không có điện thoại kết nối)
    testNet.update();
    
    // 4. Giả lập thời gian trôi qua 5.1 giây (Vượt ngưỡng WIFI_TIMEOUT = 5000ms)
    // CHÚ Ý: Lệnh này sẽ làm bài test dừng lại 5 giây!
    delay(5100); 
    
    // 5. Cập nhật mạng lần 2 -> Hệ thống phải phát hiện quá giờ và cắt cầu dao WiFi
    testNet.update();
    
    // 6. Kiểm chứng 2: WiFi BẮT BUỘC phải chuyển sang trạng thái TẮT (WIFI_MODE_NULL)
    TEST_ASSERT_EQUAL(WIFI_MODE_NULL, WiFi.getMode());
}

//===============================================
// HÀM MAIN CHẠY TEST
//===============================================

void setup(){
    // Chờ Serial Monitor kết nối để in kết quả
    delay(2000);

    UNITY_BEGIN(); //Khởi động Framework

    // CHẠY TEST MIXING & INPUT
    RUN_TEST(test_rc_move_forward);
    RUN_TEST(test_rc_spin_right);
    RUN_TEST(test_rc_deadband_noise);
    RUN_TEST(test_espnow_passthrough);
    
    // CHẠY TEST AN TOÀN
    RUN_TEST(test_failsafe_timeout);

    // CHẠY TEST CƠ KHÍ FSM
    RUN_TEST(test_rover_soft_start);
    RUN_TEST(test_rover_anti_jerk_protection);
    RUN_TEST(test_rover_network_loss_stop);

    // CHẠY TEST BẢO VỆ NHIỆT 
    RUN_TEST(test_network_thermal_protection);

    UNITY_END();
}

void loop(){
    // Code test chạy 1 lần trong setup rồi dừng lại ở đây
}
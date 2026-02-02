/**
 * @file main.cpp
 * @brief Chương trình chính điều khiển Robot tự hành (Mars Rover) 6 bánh.
 * @author Nguyen Pham Tuan Khanh
 * @details
 * - Hệ điều hành: FreeRTOS (Dual-Core).
 * - Core 1: Chạy Task Control (Điều khiển động cơ, đọc cảm biến, FSM).
 * - Core 0: Chạy Task Network (WiFi, ESP-NOW, WebServer).
 */

#include <Arduino.h>
#include <MarsRoverCore.h> // Thư viện lõi tự viết (Chứa logic Rover, Driver, Network...)
#include "RPM_Meter.h"     // Thư viện đo tốc độ Hall Sensor

// --- CÁC THƯ VIỆN MẠNG ---
#include <WiFi.h>
#include <esp_now.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>

// =========================================================================================
// 1. KHẮC PHỤC LỖI LINKER (QUAN TRỌNG)
// =========================================================================================
// [FIX] Đây là nơi tạo biến timerMux thực sự trong bộ nhớ.
// Biến này dùng để khóa ngắt (Critical Section) khi đọc cảm biến Hall, tránh xung đột dữ liệu.
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

// =========================================================================================
// 2. KHỞI TẠO CÁC ĐỐI TƯỢNG (OBJECT INSTANTIATION)
// =========================================================================================

/* --- CẤU HÌNH PHẦN CỨNG ĐỘNG CƠ ---
 * Sử dụng Class BldcDriver để trừu tượng hóa việc điều khiển PWM.
 * Lưu ý: Logic đảo chiều (ReverseLogic) cần kiểm tra thực tế trên mạch.
 */

// --- CỤM BÊN TRÁI (LEFT SIDE) ---
BldcDriver m_L1(16, 0); // Động cơ L1: Pin 16, Kênh PWM 0
BldcDriver m_L2(13, 1); // Động cơ L2: Pin 13, Kênh PWM 1
BldcDriver m_L3(5, 2);  // Động cơ L3: Pin 5,  Kênh PWM 2

// Side Trái: Dir Pin 17, Brake Pin 23.
// reverseLogic = true: Nếu mạch của em dùng Transistor đảo mức (High = Lùi).
RoverSide sideLeft(17, 23, true); 

// --- CỤM BÊN PHẢI (RIGHT SIDE) ---
BldcDriver m_R1(4, 3);  // Động cơ R1: Pin 4,  Kênh PWM 3
BldcDriver m_R2(27, 4); // Động cơ R2: Pin 27, Kênh PWM 4
BldcDriver m_R3(14, 5); // Động cơ R3: Pin 14, Kênh PWM 5

// Side Phải: Dir Pin 18, Brake Pin 19.
RoverSide sideRight(18, 19, false);

/* --- ĐỐI TƯỢNG LOGIC HỆ THỐNG --- */
Rover myRover;          // Bộ não trung tâm: Xử lý Mixing, FSM
InputManager inputMgr;  // Bộ quản lý đầu vào: Web, RC, ESP-NOW

/* --- ĐỐI TƯỢNG DỊCH VỤ MẠNG --- */
// Truyền địa chỉ inputMgr vào để NetworkService có thể cập nhật lệnh điều khiển
NetworkService network(&inputMgr); 

/* --- BIẾN TOÀN CỤC CHO TASK --- */
QueueHandle_t displayQueue; // Hàng đợi gửi dữ liệu hiển thị (Core 1 -> Core 0)

// Biến lưu trạng thái đo RPM (cho bánh L1 đại diện)
// Dùng kiểu uint32_t và int64_t để khớp với hàm getRPM
uint32_t prev_L1_cnt = 0;
int64_t prev_L1_time = 0;

// =========================================================================================
// 3. ĐỊNH NGHĨA CÁC TÁC VỤ (TASKS)
// =========================================================================================

/**
 * @brief TASK 1: ĐIỀU KHIỂN THỜI GIAN THỰC
 * @core 1 (App Core)
 * @period 20ms (50Hz)
 * @details Chịu trách nhiệm lái xe, đảm bảo an toàn và phản hồi nhanh.
 */
void TaskControl(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(20); // Chu kỳ 20ms

    for (;;) {
        // 1. Lấy lệnh điều khiển chuẩn hóa (Pulse 1000-2000)
        // InputManager tự động chọn nguồn ưu tiên (RC > ESP-NOW > Web)
        ControlCommand cmd = inputMgr.getCommand();

        // 2. Đọc tốc độ thực tế (Feedback)
        // Hàm getRPM sẽ tự cập nhật prev_L1_cnt và prev_L1_time
        float rpmL = getRPM(0, prev_L1_cnt, prev_L1_time); 
        
        // 3. Thực thi logic điều khiển xe
        // Truyền lệnh và RPM thực tế vào Rover để xử lý máy trạng thái (FSM)
        myRover.update(cmd, abs(rpmL));

        // 4. Gửi trạng thái hiển thị sang Task Network
        // Dùng Queue để tránh xung đột vùng nhớ giữa 2 Core
        MotionType status = myRover.getMotionType();
        xQueueOverwrite(displayQueue, &status);

        // 5. Ngủ chính xác để đảm bảo chu kỳ 20ms
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

/**
 * @brief TASK 2: GIAO TIẾP MẠNG
 * @core 0 (Pro Core)
 * @details Xử lý WiFi, WebServer và gửi phản hồi trạng thái.
 */
void TaskNetwork(void *pvParameters) {
    MotionType currentStatus = MOTION_STOP;
    unsigned long lastBroadcast = 0;

    for (;;) {
        // 1. Duy trì hoạt động mạng (Dọn dẹp client, xử lý gói tin đến)
        // Hàm này nằm trong thư viện NetworkService.cpp
        network.update();

        // 2. Gửi phản hồi về Web mỗi 100ms (10Hz)
        if (millis() - lastBroadcast > 100) {
            // Kiểm tra xem có trạng thái mới từ Task Control không
            if (xQueueReceive(displayQueue, &currentStatus, 0) == pdPASS) {
                // Gửi chuỗi JSON hoặc Text về Web
                network.broadcastStatus("STATUS:" + String(currentStatus));
            }
            lastBroadcast = millis();
        }

        // Nhường CPU 10ms để tránh Watchdog Reset
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// =========================================================================================
// 4. KHỞI TẠO (SETUP)
// =========================================================================================

void setup() {
    Serial.begin(115200);
    Serial.println("--- MARS ROVER INITIALIZING ---");

    // 1. Khởi tạo Phần cứng & Cảm biến
    setupRPM(); // Khởi tạo ngắt Hall Sensor
    
    // 2. Lắp ráp các thành phần (Dependency Injection)
    sideLeft.addMotor(&m_L1); sideLeft.addMotor(&m_L2); sideLeft.addMotor(&m_L3);
    sideRight.addMotor(&m_R1); sideRight.addMotor(&m_R2); sideRight.addMotor(&m_R3);
    
    myRover.setSides(&sideLeft, &sideRight);
    
    // 3. Khởi động các Dịch vụ
    inputMgr.begin(); // Khởi động bộ quản lý đầu vào
    myRover.begin();  // Khởi động các chân PWM/Dir
    network.begin();  // Khởi động WiFi, WebServer, ESP-NOW

    // 4. Khởi tạo RTOS
    displayQueue = xQueueCreate(1, sizeof(MotionType)); // Hàng đợi độ dài 1

    // Tạo Task chạy trên Core 1 (Ưu tiên lái xe)
    xTaskCreatePinnedToCore(TaskControl, "Control", 8192, NULL, 2, NULL, 1);
    
    // Tạo Task chạy trên Core 0 (Ưu tiên mạng)
    xTaskCreatePinnedToCore(TaskNetwork, "Network", 8192, NULL, 1, NULL, 0);

    Serial.println("--- SYSTEM READY ---");
}

void loop() {
    // Xóa task loop mặc định để tiết kiệm tài nguyên
    vTaskDelete(NULL); 
}
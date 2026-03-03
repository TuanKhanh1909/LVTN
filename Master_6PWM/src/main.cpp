/**
 * @file main.cpp
 * @brief Chương trình chính điều khiển Robot tự hành (Mars Rover) 6 bánh.
 * @author Nguyen Pham Tuan Khanh
 * @details
 * - Hệ điều hành: FreeRTOS (Dual-Core).
 * - Core 1: Xử lý Động lực học & Cơ khí (Task 1: InputMixer, Task 2: DriveFSM).
 * - Core 0: Xử lý Mạng & Sự kiện (Task 3: NetworkCore, Task 4: Telemetry).
 */
// =========================================================================================
// 1. CÁC THƯ VIỆN (LIBRARY INSTANTIATION)
// =========================================================================================
#include <Arduino.h>
#include <MarsRoverCore.h> // Thư viện lõi tự viết (Chứa logic Rover, Driver, Network...)
#include "SpeedMonitor.h"

// --- CÁC THƯ VIỆN MẠNG ---
#include <WiFi.h>
#include <esp_now.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>

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
Rover myRover;         // Bộ não trung tâm: Xử lý Mixing, FSM
InputManager inputMgr; // Bộ quản lý đầu vào: Web, RC, ESP-NOW

/* --- ĐỐI TƯỢNG DỊCH VỤ MẠNG --- */
// Truyền địa chỉ inputMgr vào để NetworkService có thể cập nhật lệnh điều khiển
NetworkService network(&inputMgr);

// Tay cầm RC
RcService rcService(&inputMgr, 22, 21); // RC Throttle Pin 22, Steering Pin 21

// =========================================================================================
//  KHAI BÁO HÀNG ĐỢI (QUEUES - CẦU NỐI IPC GIỮA CÁC TASK)
// =========================================================================================
QueueHandle_t queue_Cmd;       // Truyền lệnh từ Task 1 -> Task 2
QueueHandle_t queue_Telemetry; // Truyền trạng thái từ Task 2 -> Task 4

// =========================================================================================
// 3. ĐỊNH NGHĨA CÁC TÁC VỤ (TASKS)
// =========================================================================================

/**
 * @brief TASK 1: NGƯỜI GOM LỆNH (Input Mixer)
 *@core 1 | Priority 3 | Chu kỳ: 20ms (50Hz)
 * @details Chịu trách nhiệm lái xe, đảm bảo an toàn và phản hồi nhanh.
 */
void Task_InputMixer(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(20); // Chu kỳ 20ms

    for (;;)
    {
        // 1. Cập nhật dữ liệu phần cứng (RC)
        rcService.update();

        // 2. Lấy lệnh ưu tiên cao nhất đã được trộn sẵn (Mixer & Failsafe)
        ControlCommand readyCmd = inputMgr.getCommand();

        // 3. Đẩy lệnh cho cơ khí (Ghi đè - Luôn giữ lệnh mới nhất)
        xQueueOverwrite(queue_Cmd, &readyCmd);

        // 4. Ngủ tuyệt đối chuẩn 20ms
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

/**
 * @brief TASK 2: TRÁI TIM CƠ KHÍ (Drive FSM)
 * @core 1 | Priority 4 (Cao nhất) | Hướng sự kiện (Event-driven)
 * @details Xử lý chính.
 */
void Task_DriveFSM(void *pvParameters)
{
    ControlCommand received_cmd;
    MotionType oldMotion = MOTION_STOP;

    for (;;)
    {
        // 1. Ngủ sâu chờ lệnh (Chỉ thức dậy khi Task 1 đẩy lệnh vào)
        if (xQueueReceive(queue_Cmd, &received_cmd, portMAX_DELAY) == pdPASS)
        {

            // 2. Giao phó sinh mạng cơ khí cho Tướng quân Rover xử lý (FSM, Deadband, Soft-start)
            myRover.update(received_cmd);

            // 3. Kiểm tra biến động để báo cáo (Bộ lọc sự kiện)
            MotionType newMotion = myRover.getMotionType();
            if (newMotion != oldMotion)
            {
                // Chỉ gửi báo cáo khi xe THỰC SỰ chuyển hướng
                xQueueSend(queue_Telemetry, &newMotion, 0);
                oldMotion = newMotion;
            }
        }
    }
}

/**
 * @brief TASK 3: BẢO VỆ MẠNG & HỆ THỐNG (Network Core)
 * @core 0 | Priority 1 (Thấp nhất) | Chu kỳ: 10ms
 */
void Task_NetworkCore(void *pvParameters)
{
    for (;;)
    {
        // 1. Duy trì WiFi, dọn dẹp WebSockets và Bảo vệ Nhiệt độ chip
        network.update();

        // 2. Nhường CPU 10ms để dỗ Watchdog Timer của lõi 0
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * @brief TASK 4: PHÓNG VIÊN BÁO CÁO (Telemetry)
 * @core 0 | Priority 2 | Hướng sự kiện (Event-driven)
 */
void Task_Telemetry(void *pvParameters)
{
    MotionType status;

    for (;;)
    {
        // 1. Ngủ sâu chờ có tin tức biến động từ Task 2
        if (xQueueReceive(queue_Telemetry, &status, portMAX_DELAY) == pdPASS)
        {
            // 2. Bắn gói tin cập nhật Digital Twin lên giao diện Web
            network.broadcastStatus("STATUS:" + String(status));
        }
    }
}

// =========================================================================================
// 4. KHỞI TẠO (SETUP)
// =========================================================================================

void setup()
{
    Serial.begin(115200);
    Serial.println("--- MARS ROVER INITIALIZING ---");

    // 1. Lắp ráp các thành phần (Dependency Injection)
    sideLeft.addMotor(&m_L1);
    sideLeft.addMotor(&m_L2);
    sideLeft.addMotor(&m_L3);
    sideRight.addMotor(&m_R1);
    sideRight.addMotor(&m_R2);
    sideRight.addMotor(&m_R3);

    // Cài đặt Trim
    m_L1.setTrim(1.0);
    m_L2.setTrim(0.9);
    m_L3.setTrim(1.0);
    m_R1.setTrim(1.0);
    m_R2.setTrim(0.92);
    m_R3.setTrim(1.0);

    myRover.setSides(&sideLeft, &sideRight);

    // 2. Khởi động các Dịch vụ
    setupSpeedMonitor(); // Bật ngắt Cảm biến Hall
    inputMgr.begin();    // Khởi động bộ quản lý đầu vào
    myRover.begin();     // Khởi động các chân PWM/Dir
    network.begin();     // Khởi động WiFi, WebServer, ESP-NOW
    rcService.begin();   // Khởi động cho RC
    // 3. Khởi tạo RTOS
    // Hàng đợi chỉ cần độ dài 1 (Zero-latency design)
    queue_Cmd = xQueueCreate(1, sizeof(ControlCommand));
    queue_Telemetry = xQueueCreate(1, sizeof(MotionType));

    // CHẠY TRÊN CORE 1 (CƠ KHÍ)
    xTaskCreatePinnedToCore(Task_InputMixer, "InputMixer", 4096, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(Task_DriveFSM, "DriveFSM", 4096, NULL, 4, NULL, 1);

    // CHẠY TRÊN CORE 0 (MẠNG)
    xTaskCreatePinnedToCore(Task_NetworkCore, "Network", 8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(Task_Telemetry, "Telemetry", 4096, NULL, 2, NULL, 0);

    Serial.println("--- SYSTEM READY ---");
}

void loop()
{
    // Xóa task loop mặc định để tiết kiệm tài nguyên
    vTaskDelete(NULL);
}
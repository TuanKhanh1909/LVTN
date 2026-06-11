/**
 * @file main.cpp
 * @brief Chương trình chính điều khiển Robot tự hành (Mars Rover) 6 bánh.
 * @author Nguyen Pham Tuan Khanh
 * @details
 * - Hệ điều hành: FreeRTOS (Dual-Core).
 * - Core 1: Xử lý Động lực học & Cơ khí (Task_Command, Task_Control).
 * - Core 0: Xử lý Mạng & Đo lường (Task_Input, Task_Report, Task_SystemMonitor).
 */
// =========================================================================================
// 1. CÁC THƯ VIỆN (LIBRARY INSTANTIATION)
// =========================================================================================
#include <Arduino.h>
#include <MarsRoverCore.h>
#include "SpeedMonitor.h"

#include <WiFi.h>
#include <esp_now.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <Arduino_JSON.h>

// =========================================================================================
// 2. KHỞI TẠO CÁC ĐỐI TƯỢNG (OBJECT INSTANTIATION)
// =========================================================================================


/*
 // Thêm vào đầu file
#define PIN_TRACE_CONTROL  2
#define PIN_TRACE_COMMAND  4
#define PIN_TRACE_INPUT    12
#define PIN_TRACE_REPORT   15
*/
// --- CỤM BÊN TRÁI (LEFT SIDE) ---
BldcDriver m_L1(27, 0);
BldcDriver m_L2(14, 1);
BldcDriver m_L3(13, 2);
RoverSide sideLeft(22, 23, false);

// --- CỤM BÊN PHẢI (RIGHT SIDE) ---
BldcDriver m_R1(17, 3);
BldcDriver m_R2(16, 4);
BldcDriver m_R3(5, 5);
RoverSide sideRight(19, 18, false);

/* --- ĐỐI TƯỢNG LOGIC HỆ THỐNG --- */
Rover myRover;
InputManager inputMgr;

/* --- ĐỐI TƯỢNG DỊCH VỤ MẠNG --- */
NetworkService network(&inputMgr);
RcService rcService(&inputMgr, 36, 39);

// =========================================================================================
//  KHAI BÁO HÀNG ĐỢI (QUEUES - THEO ĐÚNG SƠ ĐỒ)
// =========================================================================================
QueueHandle_t queue_Cmd;         // Truyền lệnh: Task_Command -> Task_Control
QueueHandle_t queue_DriveStatus; // Truyền nội bộ: Task_Control -> Task_Input
QueueHandle_t queue_Report;      // Truyền bưu phẩm: Task_Input -> Task_Report

// =========================================================================================
// 3. ĐỊNH NGHĨA CÁC TÁC VỤ (TASKS)
// =========================================================================================

/**
 * @brief TASK: COMMAND (Người gom lệnh)
 * @core 1 | Priority 3 | Chu kỳ: 20ms (50Hz)
 */
void Task_Command(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(20);

    for (;;)
    {
        // BẬT MỨC CAO: Đánh dấu bắt đầu tính toán
        //digitalWrite(PIN_TRACE_COMMAND, HIGH);

        // 1. Cập nhật dữ liệu phần cứng (RC)
        rcService.update();

        // 2. Lấy lệnh ưu tiên cao nhất
        ControlCommand readyCmd = inputMgr.getCommand();

        // 3. Đẩy lệnh vào Queue_Cmd
        xQueueOverwrite(queue_Cmd, &readyCmd);
        // TẮT MỨC THẤP: Đánh dấu kết thúc tính toán
        //digitalWrite(PIN_TRACE_COMMAND, LOW);

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

/**
 * @brief TASK: CONTROL (Trái tim Cơ khí / FSM)
 * @core 1 | Priority 4 (Cao nhất) | Chu kỳ: 20ms (50Hz)
 */
void Task_Control(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(20);
    ControlCommand currentCmd = {1500, 1500, false};

    for (;;)
    {
        // BẬT MỨC CAO: Đánh dấu bắt đầu tính toán
        //digitalWrite(PIN_TRACE_CONTROL, HIGH);

        // 1. Mở hộp thư từ QUEUE_CMD
        xQueueReceive(queue_Cmd, &currentCmd, 0);
        // =======================================================
        // BẮT BUỘC PHẢI THÊM DÒNG NÀY ĐỂ TÍNH RPM MỚI NHẤT MỖI 20MS
        updateSpeedMonitor(); 
        // =======================================================

        // 2. Chạy Cơ khí (Hàm này sẽ gọi xuống RoverSide -> gọi PID -> lấy RPM từ SpeedMonitor)
        myRover.update(currentCmd);

        // 3. Đẩy trạng thái vào QUEUE_DRIVE_STATUS
        DriveStatus dStatus;
        dStatus.pwmL = (int16_t)myRover.getCurrentSpeedL();
        dStatus.pwmR = (int16_t)myRover.getCurrentSpeedR();
        dStatus.motion = myRover.getMotionType();
        xQueueOverwrite(queue_DriveStatus, &dStatus);
       
        // TẮT MỨC THẤP: Đánh dấu kết thúc tính toán
        //digitalWrite(PIN_TRACE_CONTROL, LOW);

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

/**
 * @brief TASK: INPUT (Lọc nhiễu & Tính toán dữ liệu cảm biến)
 * @core 0 | Priority 3 | Chu kỳ: 50ms (20Hz)
 */
void Task_Input(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(50);

    DriveStatus dStatus = {0, 0, MOTION_IDLE};

    for (;;)
    {
        // BẬT MỨC CAO: Đánh dấu bắt đầu tính toán
        //digitalWrite(PIN_TRACE_INPUT, HIGH);

        // 1. Lấy dữ liệu từ QUEUE_DRIVE_STATUS
        xQueueReceive(queue_DriveStatus, &dStatus, 0);
        updateSpeedMonitor();
        // 2. Gom dữ liệu vào TelemetryPacket
        ReportPacket packet;
        packet.batteryVoltage = readBatteryVoltage();
        // Lấy RPM đã tính toán đẩy thẳng vào gói tin
        for (int i = 0; i < 6; i++) {
            packet.rpm[i] = (int16_t)calculateRPM(i); 
        }
        packet.pwmLeft = dStatus.pwmL;
        packet.pwmRight = dStatus.pwmR;
        packet.motionState = dStatus.motion;
        packet.activeMode = inputMgr.getActiveSource();
        packet.isFailsafeLatched = inputMgr.isFailsafeLatched();

        // 3. Đẩy vào QUEUE_REPORT
        xQueueOverwrite(queue_Report, &packet);

        // TẮT MỨC THẤP: Đánh dấu kết thúc tính toán
        //digitalWrite(PIN_TRACE_INPUT, LOW);

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

/**
 * @brief TASK: REPORT (Giám sát & Báo cáo Mạng)
 * @core 0 | Priority 2 | Chu kỳ: 100ms (10Hz)
 */
void Task_Report(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(100);
    ReportPacket packet;

    for (;;)
    {
        // BẬT MỨC CAO: Đánh dấu bắt đầu tính toán
        //digitalWrite(PIN_TRACE_REPORT, HIGH);

        // 1. Quản lý Mạng
        network.update();

        // 2. Đóng gói & Gửi (Lấy từ QUEUE_REPORT)
        if (xQueueReceive(queue_Report, &packet, 0) == pdPASS)
        {
            JSONVar jsonDoc;
            jsonDoc["type"] = "tele";
            jsonDoc["bat"] = (double)packet.batteryVoltage;

            JSONVar rpmArray;
            for (int i = 0; i < 6; i++)
            {
                rpmArray[i] = packet.rpm[i];
            }
            jsonDoc["rpm"] = rpmArray;
            jsonDoc["pwmL"] = packet.pwmLeft;
            jsonDoc["pwmR"] = packet.pwmRight;
            jsonDoc["motion"] = (int)packet.motionState;
            jsonDoc["mode"] = (int)packet.activeMode;
            jsonDoc["fs"] = packet.isFailsafeLatched;

            String jsonString = JSON.stringify(jsonDoc);
            network.broadcastStatus(jsonString);
            network.broadcastEspNowReport(packet);
        }

        // TẮT MỨC THẤP: Đánh dấu kết thúc tính toán
        //digitalWrite(PIN_TRACE_REPORT, LOW);

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// =========================================================================================
// 4. KHỞI TẠO (SETUP)
// =========================================================================================

void setup()
{
    Serial.begin(115200);
    Serial.println("--- MARS ROVER INITIALIZING ---");

    // ĐỒNG TỐC BẰNG TRIM (Ví dụ: Bánh Trái quay nhanh hơn 10%, ta ép nó về 0.9)
    m_L1.setTrim(1.0f);
    m_L2.setTrim(1.0f);
    m_L3.setTrim(1.0f);
    
    m_R1.setTrim(1.0f); 
    m_R2.setTrim(1.0f);
    m_R3.setTrim(1.0f);

    sideLeft.addMotor(&m_L1, 0);
    sideLeft.addMotor(&m_L2, 1);
    sideLeft.addMotor(&m_L3, 2);

    sideRight.addMotor(&m_R1, 3);
    sideRight.addMotor(&m_R2, 4);
    sideRight.addMotor(&m_R3, 5);

    myRover.setSides(&sideLeft, &sideRight);

    inputMgr.begin();
    myRover.begin();
    network.begin();
    rcService.begin();
    setupSpeedMonitor();

    /*
      
     // Thêm đoạn này để cấu hình ngõ ra cho Logic Analyzer
    pinMode(PIN_TRACE_CONTROL, OUTPUT);
    pinMode(PIN_TRACE_COMMAND, OUTPUT);
    pinMode(PIN_TRACE_INPUT, OUTPUT);
    pinMode(PIN_TRACE_REPORT, OUTPUT);
    
    digitalWrite(PIN_TRACE_CONTROL, LOW);
    digitalWrite(PIN_TRACE_COMMAND, LOW);
    digitalWrite(PIN_TRACE_INPUT, LOW);
    digitalWrite(PIN_TRACE_REPORT, LOW);

    */
    


    // Khởi tạo hàng đợi với tên mới (Chuẩn theo sơ đồ)
    queue_Cmd = xQueueCreate(1, sizeof(ControlCommand));
    queue_DriveStatus = xQueueCreate(1, sizeof(DriveStatus));
    queue_Report = xQueueCreate(1, sizeof(ReportPacket));

    // CORE 1 (CƠ KHÍ) - Đã đổi tên hàm và tên hiển thị RTOS
    xTaskCreatePinnedToCore(Task_Command, "Command", 4096, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(Task_Control, "Control", 4096, NULL, 4, NULL, 1);

    // CORE 0 (MẠNG & ĐO LƯỜNG) - Đã đổi tên hàm và tên hiển thị RTOS
    xTaskCreatePinnedToCore(Task_Input, "Input", 4096, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(Task_Report, "Report", 8192, NULL, 1, NULL, 0);

    // TASK TẠM THỜI CHỈ DÙNG ĐỂ TEST THÔNG SỐ (CORE 0)
    //xTaskCreatePinnedToCore(Task_SystemMonitor, "SysMonitor", 4096, NULL, 0, NULL, 0);

    Serial.println("--- SYSTEM READY ---");
}

void loop()
{
    vTaskDelete(NULL);
}
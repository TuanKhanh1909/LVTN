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

// --- CỤM BÊN TRÁI (LEFT SIDE) ---
BldcDriver m_L1(27, 0);
BldcDriver m_L2(14, 1);
BldcDriver m_L3(13, 2);
RoverSide sideLeft(23, 22, true);

// --- CỤM BÊN PHẢI (RIGHT SIDE) ---
BldcDriver m_R1(17, 3);
BldcDriver m_R2(16, 4);
BldcDriver m_R3(5, 5);
RoverSide sideRight(18, 19, true);

/* --- ĐỐI TƯỢNG LOGIC HỆ THỐNG --- */
Rover myRover;
InputManager inputMgr;

/* --- ĐỐI TƯỢNG DỊCH VỤ MẠNG --- */
NetworkService network(&inputMgr);
RcService rcService(&inputMgr, 36, 39);

// --- KHAI BÁO CÁC BIẾN ĐO LƯỜNG RTOS (PROFILING) ---
// Đã đổi tên chuẩn theo sơ đồ
volatile uint32_t execTime_Command = 0, cycleTime_Command = 0;
volatile uint32_t execTime_Control = 0, cycleTime_Control = 0;
volatile uint32_t execTime_Input = 0, cycleTime_Input = 0;
volatile uint32_t execTime_Report = 0, cycleTime_Report = 0;
volatile uint32_t execTime_Monitor = 0, cycleTime_Monitor = 0;

// =========================================================================
// BIẾN KIỂM TRA DEADLOCK (HEARTBEAT)
// =========================================================================
volatile bool alive_Command = false;
volatile bool alive_Control = false;
volatile bool alive_Report = false;

// =========================================================================================
//  KHAI BÁO HÀNG ĐỢI (QUEUES - THEO ĐÚNG SƠ ĐỒ)
// =========================================================================================
QueueHandle_t queue_Cmd;         // Truyền lệnh: Task_Command -> Task_Control
QueueHandle_t queue_DriveStatus; // Truyền nội bộ: Task_Control -> Task_Input
QueueHandle_t queue_Report;      // Truyền bưu phẩm: Task_Input -> Task_Report

// =========================================================================
// HỘP ĐEN GHI LOG TIMELINE RTOS
// =========================================================================
#define TRACE_SIZE 300
struct TraceEvent
{
    uint32_t time_us;
    uint8_t task_id;
    bool is_start;
};
TraceEvent traceLog[TRACE_SIZE];
volatile int traceIndex = 0;
volatile bool doTrace = false;

void logTrace(uint8_t task_id, bool is_start)
{
    if (!doTrace || traceIndex >= TRACE_SIZE)
        return;
    traceLog[traceIndex].time_us = micros();
    traceLog[traceIndex].task_id = task_id;
    traceLog[traceIndex].is_start = is_start;
    traceIndex++;
}

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

    uint32_t last_cycle_start = micros();

    for (;;)
    {
        alive_Command = true; // [HEARTBEAT]

        uint32_t current_cycle_start = micros();
        cycleTime_Command = current_cycle_start - last_cycle_start;
        last_cycle_start = current_cycle_start;
        logTrace(2, true);

        // 1. Cập nhật dữ liệu phần cứng (RC)
        rcService.update();

        // 2. Lấy lệnh ưu tiên cao nhất
        ControlCommand readyCmd = inputMgr.getCommand();

        // 3. Đẩy lệnh vào Queue_Cmd
        xQueueOverwrite(queue_Cmd, &readyCmd);

        execTime_Command = micros() - current_cycle_start;
        logTrace(2, false);
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

    uint32_t last_cycle_start = micros();
    ControlCommand currentCmd = {1500, 1500, false};

    for (;;)
    {
        alive_Control = true; // [HEARTBEAT]

        uint32_t current_cycle_start = micros();
        cycleTime_Control = current_cycle_start - last_cycle_start;
        last_cycle_start = current_cycle_start;
        logTrace(1, true);

        // 1. Mở hộp thư từ QUEUE_CMD
        xQueueReceive(queue_Cmd, &currentCmd, 0);

        // 2. Chạy Cơ khí
        myRover.update(currentCmd);

        // 3. Đẩy trạng thái vào QUEUE_DRIVE_STATUS
        DriveStatus dStatus;
        dStatus.pwmL = (int16_t)myRover.getCurrentSpeedL();
        dStatus.pwmR = (int16_t)myRover.getCurrentSpeedR();
        dStatus.motion = myRover.getMotionType();
        xQueueOverwrite(queue_DriveStatus, &dStatus);

        execTime_Control = micros() - current_cycle_start;
        logTrace(1, false);
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
    uint32_t last_cycle_start = micros();

    DriveStatus dStatus = {0, 0, MOTION_STOP};

    for (;;)
    {
        uint32_t current_cycle_start = micros();
        cycleTime_Input = current_cycle_start - last_cycle_start;
        last_cycle_start = current_cycle_start;
        logTrace(5, true);

        // 1. Lấy dữ liệu từ QUEUE_DRIVE_STATUS
        xQueueReceive(queue_DriveStatus, &dStatus, 0);

        // 2. Gom dữ liệu vào TelemetryPacket
        TelemetryPacket packet;
        packet.batteryVoltage = readBatteryVoltage();
        for (int i = 0; i < 6; i++)
        {
            packet.rpm[i] = calculateRPM(i);
        }
        packet.pwmLeft = dStatus.pwmL;
        packet.pwmRight = dStatus.pwmR;
        packet.motionState = dStatus.motion;
        packet.activeMode = inputMgr.getActiveSource();
        packet.isFailsafeLatched = inputMgr.isFailsafeLatched();

        // 3. Đẩy vào QUEUE_REPORT
        xQueueOverwrite(queue_Report, &packet);

        execTime_Input = micros() - current_cycle_start;
        logTrace(5, false);
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
    uint32_t last_cycle_start = micros();
    TelemetryPacket packet;

    for (;;)
    {
        alive_Report = true; // [HEARTBEAT]

        uint32_t current_cycle_start = micros();
        cycleTime_Report = current_cycle_start - last_cycle_start;
        last_cycle_start = current_cycle_start;
        logTrace(3, true);

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
            network.broadcastEspNowTelemetry(packet);
        }

        execTime_Report = micros() - current_cycle_start;
        logTrace(3, false);
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

/**
 * @brief TASK: TRẠM GIÁM SÁT RTOS TẠM THỜI (System Monitor / Profiler)
 * @core 0 | Priority 1 (Thấp nhất) | Chu kỳ: 100ms
 * @details Thay thế Task_Network cũ để test thời gian chạy các Task.
 */
void Task_SystemMonitor(void *pvParameters)
{
    uint32_t lastPrintTime1 = millis();
    uint32_t lastPrintTime2 = millis();
    uint32_t last_cycle_start = micros();

    for (;;)
    {
        uint32_t current_cycle_start = micros();
        cycleTime_Monitor = current_cycle_start - last_cycle_start;
        last_cycle_start = current_cycle_start;
        logTrace(4, true);

        // ====================================================================
        // 1. MỖI 2 GIÂY IN BÁO CÁO TỔNG HỢP (DEADLOCK + HIỆU NĂNG) 1 LẦN
        // ====================================================================
        if (millis() - lastPrintTime1 >= 2000)
        {
            Serial.println("\n=== BÁO CÁO TỔNG HỢP RTOS (TESTING MODE) ===");
            Serial.print("Trang thai Deadlock: ");
            if (alive_Control && alive_Command && alive_Report)
            {
                Serial.println("[SAFE] Khong phat hien Deadlock. Tat ca cac Task dang chay muot ma!");
            }
            else
            {
                Serial.println("[WARNING] PHAT HIEN DEADLOCK HOAC STARVATION!");
                if (!alive_Control)
                    Serial.println(" -> Task Control bi treo!");
                if (!alive_Command)
                    Serial.println(" -> Task Command bi treo!");
                if (!alive_Report)
                    Serial.println(" -> Task Report bi treo!");
            }

            Serial.println("--- Thoi gian thuc thi (Micro-giay) ---");
            Serial.printf("[Core 1] Command  (20ms) | Chu ky: %lu us | Thuc thi: %lu us\n", cycleTime_Command, execTime_Command);
            Serial.printf("[Core 1] Control  (20ms) | Chu ky: %lu us | Thuc thi: %lu us\n", cycleTime_Control, execTime_Control);
            Serial.printf("[Core 0] Input    (20ms) | Chu ky: %lu us | Thuc thi: %lu us\n", cycleTime_Input, execTime_Input);
            Serial.printf("[Core 0] Report   (50ms) | Chu ky: %lu us | Thuc thi: %lu us\n", cycleTime_Report, execTime_Report);
            Serial.printf("[Core 0] Monitor (100ms) | Chu ky: %lu us | Thuc thi: %lu us\n", cycleTime_Monitor, execTime_Monitor);
            Serial.println("===========================================\n");

            alive_Control = false;
            alive_Command = false;
            alive_Report = false;
            lastPrintTime1 = millis();
        }

        // ====================================================================
        // 2. CỨ MỖI 10 GIÂY IN TIMELINE
        // ====================================================================
        if (millis() - lastPrintTime2 >= 10000)
        {
            traceIndex = 0;
            doTrace = true;
            vTaskDelay(pdMS_TO_TICKS(100));
            doTrace = false;

            Serial.println("--- RTOS TIMELINE SNAPSHOT (100ms) ---");
            Serial.println("Time_us,Task,Event");
            if (traceIndex > 0)
            {
                uint32_t base_time = traceLog[0].time_us;
                for (int i = 0; i < traceIndex; i++)
                {
                    String taskName = "";
                    if (traceLog[i].task_id == 1)
                        taskName = "1_Control (Pri 4)";
                    if (traceLog[i].task_id == 2)
                        taskName = "2_Command (Pri 3)";
                    if (traceLog[i].task_id == 3)
                        taskName = "3_Report (Pri 2)";
                    if (traceLog[i].task_id == 4)
                        taskName = "4_Monitor (Pri 1)";
                    if (traceLog[i].task_id == 5)
                        taskName = "5_Input (Pri 3)";

                    String event = traceLog[i].is_start ? "START" : "STOP";
                    uint32_t rel_time = traceLog[i].time_us - base_time;
                    Serial.printf("%lu,%s,%s\n", rel_time, taskName.c_str(), event.c_str());
                }
            }
            Serial.println("--------------------------------------\n");
            lastPrintTime2 = millis();
        }

        execTime_Monitor = micros() - current_cycle_start;
        logTrace(4, false);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// =========================================================================================
// 4. KHỞI TẠO (SETUP)
// =========================================================================================

void setup()
{
    Serial.begin(115200);
    Serial.println("--- MARS ROVER INITIALIZING ---");

    sideLeft.addMotor(&m_L1);
    sideLeft.addMotor(&m_L2);
    sideLeft.addMotor(&m_L3);
    sideRight.addMotor(&m_R1);
    sideRight.addMotor(&m_R2);
    sideRight.addMotor(&m_R3);

    m_L1.setTrim(1.0);
    m_L2.setTrim(0.9);
    m_L3.setTrim(1.0);
    m_R1.setTrim(1.0);
    m_R2.setTrim(0.92);
    m_R3.setTrim(1.0);

    myRover.setSides(&sideLeft, &sideRight);

    inputMgr.begin();
    myRover.begin();
    network.begin();

    // Khởi tạo hàng đợi với tên mới (Chuẩn theo sơ đồ)
    queue_Cmd = xQueueCreate(1, sizeof(ControlCommand));
    queue_DriveStatus = xQueueCreate(1, sizeof(DriveStatus));
    queue_Report = xQueueCreate(1, sizeof(TelemetryPacket)); // Đã đổi tên thành queue_Report

    // CORE 1 (CƠ KHÍ) - Đã đổi tên hàm và tên hiển thị RTOS
    xTaskCreatePinnedToCore(Task_Command, "Command", 4096, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(Task_Control, "Control", 4096, NULL, 4, NULL, 1);

    // CORE 0 (MẠNG & ĐO LƯỜNG) - Đã đổi tên hàm và tên hiển thị RTOS
    xTaskCreatePinnedToCore(Task_Input, "Input", 4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(Task_Report, "Report", 8192, NULL, 2, NULL, 0);

    // TASK TẠM THỜI CHỈ DÙNG ĐỂ TEST THÔNG SỐ (CORE 0)
    xTaskCreatePinnedToCore(Task_SystemMonitor, "SysMonitor", 4096, NULL, 1, NULL, 0);

    Serial.println("--- SYSTEM READY ---");
}

void loop()
{
    vTaskDelete(NULL);
}
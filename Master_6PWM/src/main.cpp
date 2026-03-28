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
#include <Arduino_JSON.h> // Thư viện xử lý JSON
// =========================================================================================
// 2. KHỞI TẠO CÁC ĐỐI TƯỢNG (OBJECT INSTANTIATION)
// =========================================================================================

/* --- CẤU HÌNH PHẦN CỨNG ĐỘNG CƠ ---
 * Sử dụng Class BldcDriver để trừu tượng hóa việc điều khiển PWM.
 * Lưu ý: Logic đảo chiều (ReverseLogic) cần kiểm tra thực tế trên mạch.
 */

// --- CỤM BÊN TRÁI (LEFT SIDE) ---
BldcDriver m_L1(27, 0); // Động cơ L1: Pin 16, Kênh PWM 0
BldcDriver m_L2(14, 1); // Động cơ L2: Pin 13, Kênh PWM 1
BldcDriver m_L3(13, 2);  // Động cơ L3: Pin 5,  Kênh PWM 2

// Side Trái: Dir Pin 22, Brake Pin 23.
// reverseLogic = true: Nếu mạch của em dùng Transistor đảo mức (High = Lùi).
RoverSide sideLeft(22, 23, true);

// --- CỤM BÊN PHẢI (RIGHT SIDE) ---
BldcDriver m_R1(17, 3);  // Động cơ R1: Pin 4,  Kênh PWM 3
BldcDriver m_R2(16, 4); // Động cơ R2: Pin 27, Kênh PWM 4
BldcDriver m_R3(4, 5); // Động cơ R3: Pin 14, Kênh PWM 5

// Side Phải: Dir Pin 18, Brake Pin 19.
RoverSide sideRight(19, 18, false);

/* --- ĐỐI TƯỢNG LOGIC HỆ THỐNG --- */
Rover myRover;         // Bộ não trung tâm: Xử lý Mixing, FSM
InputManager inputMgr; // Bộ quản lý đầu vào: Web, RC, ESP-NOW

/* --- ĐỐI TƯỢNG DỊCH VỤ MẠNG --- */
// Truyền địa chỉ inputMgr vào để NetworkService có thể cập nhật lệnh điều khiển
NetworkService network(&inputMgr);

// Tay cầm RC
RcService rcService(&inputMgr, 36, 39); // Kênh 1: RC Throttle Pin 36, Kênh 2: Steering Pin 39

// --- KHAI BÁO CÁC BIẾN ĐO LƯỜNG RTOS (PROFILING) ---
volatile uint32_t execTime_Drive = 0, cycleTime_Drive = 0;
volatile uint32_t execTime_Input = 0, cycleTime_Input = 0;
volatile uint32_t execTime_Tele = 0,  cycleTime_Tele = 0;
volatile uint32_t execTime_Net = 0,   cycleTime_Net = 0;

// =========================================================================
// BIẾN KIỂM TRA DEADLOCK (HEARTBEAT)
// =========================================================================
volatile bool alive_DriveFSM = false;
volatile bool alive_InputMixer = false;
volatile bool alive_Telemetry = false;

// =========================================================================================
//  KHAI BÁO HÀNG ĐỢI (QUEUES - CẦU NỐI IPC GIỮA CÁC TASK)
// =========================================================================================
QueueHandle_t queue_Cmd;         // Truyền lệnh: InputMixer -> DriveFSM
QueueHandle_t queue_DriveStatus; // Truyền nội bộ: DriveFSM -> Sensors
QueueHandle_t queue_Telemetry;   // Truyền cục bưu phẩm: Sensors -> Telemetry

// =========================================================================
// HỘP ĐEN GHI LOG TIMELINE RTOS
// =========================================================================
#define TRACE_SIZE 300  // Lưu tối đa 300 sự kiện
struct TraceEvent {
    uint32_t time_us;
    uint8_t task_id;
    bool is_start;
};
TraceEvent traceLog[TRACE_SIZE];
volatile int traceIndex = 0;
volatile bool doTrace = false;  // Cờ kích hoạt hộp đen

// Hàm ghi log siêu tốc (chỉ mất ~1 micro-giây)
void logTrace(uint8_t task_id, bool is_start) {
    if (!doTrace || traceIndex >= TRACE_SIZE) return;
    traceLog[traceIndex].time_us = micros();
    traceLog[traceIndex].task_id = task_id;
    traceLog[traceIndex].is_start = is_start;
    traceIndex++;
}

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
   
   // uint32_t last_cycle_start = micros(); //Bấm giờ chu kỳ

    for (;;)
    {
        alive_InputMixer = true; // [HEARTBEAT] Báo cáo tôi còn sống!

        //uint32_t current_cycle_start = micros();
        //cycleTime_Input = current_cycle_start - last_cycle_start;
        //last_cycle_start = current_cycle_start;
        logTrace(2, true); // [TASK 2 START]
        // 1. Cập nhật dữ liệu phần cứng (RC)
        rcService.update();

        // 2. Lấy lệnh ưu tiên cao nhất đã được trộn sẵn (Mixer & Failsafe)
        ControlCommand readyCmd = inputMgr.getCommand();

        // 3. Đẩy lệnh cho cơ khí (Ghi đè - Luôn giữ lệnh mới nhất)
        xQueueOverwrite(queue_Cmd, &readyCmd);

        //execTime_Input = micros() - current_cycle_start;

        logTrace(2, false); // [TASK 2 STOP]

        // 4. Ngủ tuyệt đối chuẩn 20ms
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

/**
 * @brief TASK 2: TRÁI TIM CƠ KHÍ (Drive FSM)
 * @core 1 | Priority 4 (Cao nhất) | Chu kỳ: 20ms (50Hz)
 * @details Xử lý chính.
 */
void Task_DriveFSM(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(20);
    
    //uint32_t last_cycle_start = micros();

    // Lệnh lưu trữ nội bộ của Task 2
    ControlCommand currentCmd;
    currentCmd.pulseL = 1500; currentCmd.pulseR = 1500; currentCmd.connected = false;

    for (;;)
    {
        alive_DriveFSM = true; // [HEARTBEAT] Báo cáo tôi còn sống!

        // uint32_t current_cycle_start = micros();
        // cycleTime_Drive = current_cycle_start - last_cycle_start;
        // last_cycle_start = current_cycle_start;

        logTrace(1, true); // [TASK 1 START]

        // 1. MỞ HỘP THƯ (Timeout = 0: Không có thư thì thôi, KHÔNG chờ đợi!)
        if (xQueueReceive(queue_Cmd, &currentCmd, 0) == pdPASS) {
            // Lấy được thư mới, currentCmd đã được cập nhật
        }

        // 2. CHẠY CƠ KHÍ (Dù mạng lag không có thư mới, nó vẫn lấy thư cũ ra chạy để tính Soft-start)
        myRover.update(currentCmd);

        // 3. CẬP NHẬT TRẠNG THÁI CHO TASK 4
        MotionType currentMotion = myRover.getMotionType();
        DriveStatus dStatus;
        dStatus.pwmL = (int16_t)myRover.getCurrentSpeedL();
        dStatus.pwmR = (int16_t)myRover.getCurrentSpeedR();
        dStatus.motion = myRover.getMotionType();
        xQueueOverwrite(queue_DriveStatus, &dStatus);

        //execTime_Drive = micros() - current_cycle_start;
        
        logTrace(1, false); // [TASK 1 STOP]
        // 4. NGỦ CHUẨN XÁC 20ms
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

/**
 * @brief TASK 3: BẢO VỆ MẠNG & HỆ THỐNG (Network Core)
 * @core 0 | Priority 1 (Thấp nhất) | Chu kỳ: 10ms
 */
void Task_NetworkCore(void *pvParameters)
{
    uint32_t lastPrintTime1 = millis();
    uint32_t lastPrintTime2 = millis();
    //uint32_t last_cycle_start = micros();
    
    for (;;)
    {
        //uint32_t current_cycle_start = micros();
        //cycleTime_Net = current_cycle_start - last_cycle_start;
        //last_cycle_start = current_cycle_start;
        
        logTrace(4, true); // [TASK 4 START]

        // 1. Duy trì WiFi, dọn dẹp WebSockets và Bảo vệ Nhiệt độ chip
        network.update();
        
        logTrace(4, false); // [TASK 4 STOP]
        // MỖI 2 GIÂY IN BÁO CÁO 1 LẦN
        if (millis() - lastPrintTime1 >= 2000) {
            Serial.println("\n=== BÁO CÁO HIỆU NĂNG & DEADLOCK RTOS ===");
            
            // 1. Kiểm tra trạng thái Deadlock (Heartbeat)
            Serial.print("Trang thai Deadlock: ");
            if (alive_DriveFSM && alive_InputMixer && alive_Telemetry) {
                Serial.println("[SAFE] Khong phat hien Deadlock. Tat ca cac Task deu dang chay mượt mà!");
            } else {
                Serial.println("[WARNING] PHAT HIEN DEADLOCK HOAC STARVATION!");
                if (!alive_DriveFSM) Serial.println(" -> Task DriveFSM bi treo!");
                if (!alive_InputMixer) Serial.println(" -> Task InputMixer bi treo!");
                if (!alive_Telemetry) Serial.println(" -> Task Telemetry bi treo!");
            }

            // 2. Reset nhịp tim về false để kiểm tra cho chu kỳ 2 giây tiếp theo
            alive_DriveFSM = false;
            alive_InputMixer = false;
            alive_Telemetry = false;
            lastPrintTime1 = millis();
        }
        // CỨ MỖI 10 GIÂY, CHỤP TIMELINE TRONG 100ms VÀ IN RA
        if (millis() - lastPrintTime2 >= 10000) {
            traceIndex = 0;
            doTrace = true;  // Bật hộp đen
            vTaskDelay(pdMS_TO_TICKS(100)); // Để các Task khác chạy tự do trong 100ms
            doTrace = false; // Tắt hộp đen

            // In kết quả ra định dạng CSV (Dễ copy vào Excel)
            Serial.println("\n--- RTOS TIMELINE SNAPSHOT (100ms) ---");
            Serial.println("Time_us,Task,Event");
            uint32_t base_time = traceLog[0].time_us; // Lấy mốc 0
            
            for(int i = 0; i < traceIndex; i++) {
                String taskName = "";
                if(traceLog[i].task_id == 1) taskName = "1_DriveFSM (Pri 4)";
                if(traceLog[i].task_id == 2) taskName = "2_InputMix (Pri 3)";
                if(traceLog[i].task_id == 3) taskName = "3_Telemetry (Pri 2)";
                if(traceLog[i].task_id == 4) taskName = "4_NetCore (Pri 1)";
                
                String event = traceLog[i].is_start ? "START" : "STOP";
                uint32_t rel_time = traceLog[i].time_us - base_time;
                
                Serial.printf("%lu (us)---,%s,%s\n", rel_time, taskName.c_str(), event.c_str());
            }
            Serial.println("--------------------------------------\n");
            lastPrintTime2 = millis();
        }

        /*
        // --- ĐOẠN IN BÁO CÁO LÊN TERMINAL MỖI 2 GIÂY ---
        if (millis() - lastPrintTime >= 2000) {
            Serial.println("\n=== BÁO CÁO HIỆU NĂNG RTOS (Micro-giây) ===");
            Serial.printf("[Core 1] DriveFSM  (20ms) | Chu ky: %lu us | Thuc thi: %lu us\n", cycleTime_Drive, execTime_Drive);
            Serial.printf("[Core 1] InputMix  (20ms) | Chu ky: %lu us | Thuc thi: %lu us\n", cycleTime_Input, execTime_Input);
            Serial.printf("[Core 0] Telemetry (50ms) | Chu ky: %lu us | Thuc thi: %lu us\n", cycleTime_Tele, execTime_Tele);
            Serial.printf("[Core 0] NetCore   (10ms) | Chu ky: %lu us | Thuc thi: %lu us\n", cycleTime_Net, execTime_Net);
            Serial.println("===========================================");
            lastPrintTime = millis();
        }

        execTime_Net = micros() - current_cycle_start;
        */
        // 2. Nhường CPU 10ms để dỗ Watchdog Timer của lõi 0
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * @brief TASK 4: PHÓNG VIÊN BÁO CÁO (Telemetry)
 * @core 0 | Priority 2 | Chu kỳ: 50ms (20Hz)
 */
void Task_Telemetry(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(50); 
    
    TelemetryPacket packet;

    for (;;)
    {
        alive_Telemetry = true; // [HEARTBEAT]
        logTrace(3, true); 

        // 1. Lấy gói bưu phẩm "Nóng hổi" nhất từ Task_Sensors
        // Dùng xQueueReceive (xóa luôn gói trong hàng đợi) thay vì Peek
        if (xQueueReceive(queue_Telemetry, &packet, 0) == pdPASS) {
            
            // 2. Tạo đối tượng JSON
            JSONVar jsonDoc;
            jsonDoc["type"] = "tele";
            jsonDoc["bat"] = (double)packet.batteryVoltage; // Ép kiểu double cho an toàn
            
            // Tạo mảng RPM 6 bánh
            JSONVar rpmArray;
            for(int i = 0; i < 6; i++) {
                rpmArray[i] = packet.rpm[i];
            }
            jsonDoc["rpm"] = rpmArray;
            
            jsonDoc["pwmL"] = packet.pwmLeft;
            jsonDoc["pwmR"] = packet.pwmRight;
            jsonDoc["motion"] = (int)packet.motionState;
            jsonDoc["mode"] = (int)packet.activeMode;
            jsonDoc["fs"] = packet.isFailsafeLatched;

            // 3. Đóng gói thành chuỗi String và bắn đi qua WebSocket
            String jsonString = JSON.stringify(jsonDoc);
            network.broadcastStatus(jsonString);
        }
        
        logTrace(3, false); 
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

/**
 * @brief TASK 5: TRẠM ĐO LƯỜNG (Sensors Data Logger)
 * @core 0 | Priority 3 | Chu kỳ: 20ms (50Hz)
 */
void Task_Sensors(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(20);
    
    DriveStatus dStatus = {0, 0, MOTION_STOP}; 

    for (;;)
    {
        // 1. Nhặt dữ liệu động lực học từ Task 2 (Peek hoặc Receive đều được)
        xQueueReceive(queue_DriveStatus, &dStatus, 0);

        // 2. Gom tất cả dữ liệu vào "Bưu phẩm" TelemetryPacket
        TelemetryPacket packet;
        packet.batteryVoltage = readBatteryVoltage();
        for(int i = 0; i < 6; i++) {
            packet.rpm[i] = calculateRPM(i);
        }
        packet.pwmLeft = dStatus.pwmL;
        packet.pwmRight = dStatus.pwmR;
        packet.motionState = dStatus.motion;
        packet.activeMode = inputMgr.getActiveSource();
        packet.isFailsafeLatched = inputMgr.isFailsafeLatched();

        // 3. Đẩy gói bưu phẩm này qua cho Task Mạng phát sóng
        xQueueOverwrite(queue_Telemetry, &packet);

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
   queue_DriveStatus = xQueueCreate(1, sizeof(DriveStatus)); // <--- Thêm dòng này
    queue_Telemetry = xQueueCreate(1, sizeof(TelemetryPacket)); // <--- Sửa lại size

    // CHẠY TRÊN CORE 1 (CƠ KHÍ)
    xTaskCreatePinnedToCore(Task_InputMixer, "InputMixer", 4096, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(Task_DriveFSM, "DriveFSM", 4096, NULL, 4, NULL, 1);

    // CHẠY TRÊN CORE 0 (MẠNG & ĐO LƯỜNG)
    xTaskCreatePinnedToCore(Task_NetworkCore, "Network", 8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(Task_Telemetry, "Telemetry", 4096, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(Task_Sensors, "Sensors_Rx", 4096, NULL, 3, NULL, 0); // <--- Kích hoạt Task 5

    Serial.println("--- SYSTEM READY ---");
}

void loop()
{
    // Xóa task loop mặc định để tiết kiệm tài nguyên
    vTaskDelete(NULL);
}
#include <Arduino.h>
#include "esp_timer.h"
#include "SpeedMonitor.h"

// --- CẬP NHẬT CHÂN CẮM MỚI (CHUẨN AN TOÀN) ---
#define HALL_PIN_L1 34
#define HALL_PIN_L2 35
#define HALL_PIN_L3 33
#define HALL_PIN_R1 25
#define HALL_PIN_R2 26
#define HALL_PIN_R3 21

#define BATTERY_PIN 32 // ADC1_CH4 (An toàn tuyệt đối khi bật WiFi)

// --- CẤU HÌNH ---
#define DEBOUNCE_US 2000       // Lọc nhiễu: Bỏ qua xung < 2ms
#define SAMPLE_TIME_MS 350 // 350ms không có xung = Coi như dừng hẳn

// --- BIẾN TOÀN CỤC LƯU TRỮ NGẮT ---
static volatile int64_t last_time_L1 = 0, last_time_L2 = 0, last_time_L3 = 0;
static volatile int64_t last_time_R1 = 0, last_time_R2 = 0, last_time_R3 = 0;

// BIẾN ĐẾM XUNG (PULSE COUNTERS)
static volatile int32_t pulse_L1 = 0, pulse_L2 = 0, pulse_L3 = 0;
static volatile int32_t pulse_R1 = 0, pulse_R2 = 0, pulse_R3 = 0;

//BIẾN LƯU TRỮ RPM (CACHE)
static int16_t rpm_L1 = 0, rpm_L2 = 0, rpm_L3 = 0;
static int16_t rpm_R1 = 0, rpm_R2 = 0, rpm_R3 = 0;

portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
float ema_battery = 0.0; // Biến lọc trung bình Pin

// --- CÁC HÀM NGẮT (ISR) SIÊU NHẸ BÉN ---
// Thuật toán M: Ngắt chỉ làm 1 việc duy nhất là TĂNG BIẾN ĐẾM
void IRAM_ATTR onHallL1() {
    int64_t now = esp_timer_get_time(); portENTER_CRITICAL_ISR(&timerMux);
    if (now - last_time_L1 > DEBOUNCE_US) { pulse_L1++; last_time_L1 = now; } portEXIT_CRITICAL_ISR(&timerMux);
}
void IRAM_ATTR onHallL2() {
    int64_t now = esp_timer_get_time(); portENTER_CRITICAL_ISR(&timerMux);
    if (now - last_time_L2 > DEBOUNCE_US) { pulse_L2++; last_time_L2 = now; } portEXIT_CRITICAL_ISR(&timerMux);
}
void IRAM_ATTR onHallL3() {
    int64_t now = esp_timer_get_time(); portENTER_CRITICAL_ISR(&timerMux);
    if (now - last_time_L3 > DEBOUNCE_US) { pulse_L3++; last_time_L3 = now; } portEXIT_CRITICAL_ISR(&timerMux);
}
void IRAM_ATTR onHallR1() {
    int64_t now = esp_timer_get_time(); portENTER_CRITICAL_ISR(&timerMux);
    if (now - last_time_R1 > DEBOUNCE_US) { pulse_R1++; last_time_R1 = now; } portEXIT_CRITICAL_ISR(&timerMux);
}
void IRAM_ATTR onHallR2() {
    int64_t now = esp_timer_get_time(); portENTER_CRITICAL_ISR(&timerMux);
    if (now - last_time_R2 > DEBOUNCE_US) { pulse_R2++; last_time_R2 = now; } portEXIT_CRITICAL_ISR(&timerMux);
}
void IRAM_ATTR onHallR3() {
    int64_t now = esp_timer_get_time(); portENTER_CRITICAL_ISR(&timerMux);
    if (now - last_time_R3 > DEBOUNCE_US) { pulse_R3++; last_time_R3 = now; } portEXIT_CRITICAL_ISR(&timerMux);
}

// --- KHỞI TẠO ---
void setupSpeedMonitor() {
    pinMode(HALL_PIN_L1, INPUT); attachInterrupt(digitalPinToInterrupt(HALL_PIN_L1), onHallL1, CHANGE);
    pinMode(HALL_PIN_L2, INPUT); attachInterrupt(digitalPinToInterrupt(HALL_PIN_L2), onHallL2, CHANGE);
    pinMode(HALL_PIN_L3, INPUT); attachInterrupt(digitalPinToInterrupt(HALL_PIN_L3), onHallL3, CHANGE);
    pinMode(HALL_PIN_R1, INPUT); attachInterrupt(digitalPinToInterrupt(HALL_PIN_R1), onHallR1, CHANGE);
    pinMode(HALL_PIN_R2, INPUT); attachInterrupt(digitalPinToInterrupt(HALL_PIN_R2), onHallR2, CHANGE);
    pinMode(HALL_PIN_R3, INPUT); attachInterrupt(digitalPinToInterrupt(HALL_PIN_R3), onHallR3, CHANGE);

    pinMode(BATTERY_PIN, INPUT);
    // Lấy mẫu nháp đầu tiên. Công thức V_in = (ADC/4095) * 3.3 * (160k+10k)/10k = (ADC/4095) * 56.1
    ema_battery = (analogRead(BATTERY_PIN) / 4095.0) * 56.1; 
}

// --- ĐO ĐIỆN ÁP PIN (EMA FILTER) ---
float readBatteryVoltage() {
    float raw_voltage = (analogRead(BATTERY_PIN) / 4095.0) * 56.1;
    ema_battery = (0.1 * raw_voltage) + (0.9 * ema_battery); // Lọc mượt
    return ema_battery;
}

// --- TÍNH RPM (PHƯƠNG PHÁP M - LẤY MẪU 350ms) ---
int16_t calculateRPM(int motorID) {
    static unsigned long last_calc_time = 0;
    unsigned long now = millis();

    // 1. Chỉ gom số liệu và tính toán khi ĐÃ ĐỦ 350ms
    if (now - last_calc_time >= SAMPLE_TIME_MS) {
        uint32_t c_L1, c_L2, c_L3, c_R1, c_R2, c_R3;
        
        // Vào vùng Critical để copy và RESET BỘ ĐẾM XUNG thật nhanh
        portENTER_CRITICAL(&timerMux);
        c_L1 = pulse_L1; pulse_L1 = 0;
        c_L2 = pulse_L2; pulse_L2 = 0;
        c_L3 = pulse_L3; pulse_L3 = 0;
        c_R1 = pulse_R1; pulse_R1 = 0;
        c_R2 = pulse_R2; pulse_R2 = 0;
        c_R3 = pulse_R3; pulse_R3 = 0;
        portEXIT_CRITICAL(&timerMux);

        // Công thức quy đổi: RPM = (Số xung / 90) * (60000ms / 350ms)
        // Tính trước hệ số: 60000 / (90 * 350) = 1.90476
        float factor = 60000.0 / (90.0 * (float)SAMPLE_TIME_MS);
        
        rpm_L1 = (int16_t)(c_L1 * factor);
        rpm_L2 = (int16_t)(c_L2 * factor);
        rpm_L3 = (int16_t)(c_L3 * factor);
        rpm_R1 = (int16_t)(c_R1 * factor);
        rpm_R2 = (int16_t)(c_R2 * factor);
        rpm_R3 = (int16_t)(c_R3 * factor);

        // Cập nhật lại mốc thời gian lấy mẫu
        last_calc_time = now;
    }

    // 2. Trả về giá trị đã được Cache (Dù gọi 1000 lần trong lúc chờ 350ms thì vẫn không hao tốn CPU)
    switch(motorID) {
        case 0: return rpm_L1;
        case 1: return rpm_L2;
        case 2: return rpm_L3;
        case 3: return rpm_R1;
        case 4: return rpm_R2;
        case 5: return rpm_R3;
    }
    return 0;
}

bool isWheelStopped(int motorID) { return (calculateRPM(motorID) == 0); }

bool isRoverCompletelyStopped() {
    for (int i = 0; i < 6; i++) {
        if (!isWheelStopped(i)) return false;
    }
    return true;
}
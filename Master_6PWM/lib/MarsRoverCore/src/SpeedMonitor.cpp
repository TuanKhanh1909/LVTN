#include <Arduino.h>
#include "esp_timer.h"
#include "SpeedMonitor.h"

// --- CẬP NHẬT CHÂN CẮM MỚI (CHUẨN AN TOÀN) ---
#define HALL_PIN_L1 5
#define HALL_PIN_L2 15
#define HALL_PIN_L3 33
#define HALL_PIN_R1 25
#define HALL_PIN_R2 26
#define HALL_PIN_R3 21

#define BATTERY_PIN 32 // ADC1_CH4 (An toàn tuyệt đối khi bật WiFi)

// --- CẤU HÌNH ---
#define DEBOUNCE_US 2000       // Lọc nhiễu: Bỏ qua xung < 2ms
#define STOP_TIMEOUT_US 300000 // 300ms không có xung = Coi như dừng hẳn

// --- BIẾN TOÀN CỤC LƯU TRỮ NGẮT ---
static volatile int64_t last_time_L1 = 0, last_time_L2 = 0, last_time_L3 = 0;
static volatile int64_t last_time_R1 = 0, last_time_R2 = 0, last_time_R3 = 0;

// Biến lưu Chu kỳ (Micro-giây) giữa 2 xung
static volatile int32_t period_L1 = 0, period_L2 = 0, period_L3 = 0;
static volatile int32_t period_R1 = 0, period_R2 = 0, period_R3 = 0;

portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
float ema_battery = 0.0; // Biến lọc trung bình Pin

// --- CÁC HÀM NGẮT (ISR) SIÊU NHẸ BÉN ---
void IRAM_ATTR onHallL1() {
    int64_t now = esp_timer_get_time(); portENTER_CRITICAL_ISR(&timerMux);
    int32_t dt = now - last_time_L1; if (dt > DEBOUNCE_US) { period_L1 = dt; last_time_L1 = now; } portEXIT_CRITICAL_ISR(&timerMux);
}
void IRAM_ATTR onHallL2() {
    int64_t now = esp_timer_get_time(); portENTER_CRITICAL_ISR(&timerMux);
    int32_t dt = now - last_time_L2; if (dt > DEBOUNCE_US) { period_L2 = dt; last_time_L2 = now; } portEXIT_CRITICAL_ISR(&timerMux);
}
void IRAM_ATTR onHallL3() {
    int64_t now = esp_timer_get_time(); portENTER_CRITICAL_ISR(&timerMux);
    int32_t dt = now - last_time_L3; if (dt > DEBOUNCE_US) { period_L3 = dt; last_time_L3 = now; } portEXIT_CRITICAL_ISR(&timerMux);
}
void IRAM_ATTR onHallR1() {
    int64_t now = esp_timer_get_time(); portENTER_CRITICAL_ISR(&timerMux);
    int32_t dt = now - last_time_R1; if (dt > DEBOUNCE_US) { period_R1 = dt; last_time_R1 = now; } portEXIT_CRITICAL_ISR(&timerMux);
}
void IRAM_ATTR onHallR2() {
    int64_t now = esp_timer_get_time(); portENTER_CRITICAL_ISR(&timerMux);
    int32_t dt = now - last_time_R2; if (dt > DEBOUNCE_US) { period_R2 = dt; last_time_R2 = now; } portEXIT_CRITICAL_ISR(&timerMux);
}
void IRAM_ATTR onHallR3() {
    int64_t now = esp_timer_get_time(); portENTER_CRITICAL_ISR(&timerMux);
    int32_t dt = now - last_time_R3; if (dt > DEBOUNCE_US) { period_R3 = dt; last_time_R3 = now; } portEXIT_CRITICAL_ISR(&timerMux);
}

// --- KHỞI TẠO ---
void setupSpeedMonitor() {
    pinMode(HALL_PIN_L1, INPUT_PULLUP); attachInterrupt(digitalPinToInterrupt(HALL_PIN_L1), onHallL1, RISING);
    pinMode(HALL_PIN_L2, INPUT_PULLUP); attachInterrupt(digitalPinToInterrupt(HALL_PIN_L2), onHallL2, RISING);
    pinMode(HALL_PIN_L3, INPUT_PULLUP); attachInterrupt(digitalPinToInterrupt(HALL_PIN_L3), onHallL3, RISING);
    pinMode(HALL_PIN_R1, INPUT_PULLUP); attachInterrupt(digitalPinToInterrupt(HALL_PIN_R1), onHallR1, RISING);
    pinMode(HALL_PIN_R2, INPUT_PULLUP); attachInterrupt(digitalPinToInterrupt(HALL_PIN_R2), onHallR2, RISING);
    pinMode(HALL_PIN_R3, INPUT_PULLUP); attachInterrupt(digitalPinToInterrupt(HALL_PIN_R3), onHallR3, RISING);

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

// --- TÍNH RPM (T-METHOD) ---
int16_t calculateRPM(int motorID) {
    int64_t now = esp_timer_get_time();
    int32_t period = 0;
    int64_t last_time = 0;

    portENTER_CRITICAL(&timerMux);
    switch(motorID) {
        case 0: period = period_L1; last_time = last_time_L1; break;
        case 1: period = period_L2; last_time = last_time_L2; break;
        case 2: period = period_L3; last_time = last_time_L3; break;
        case 3: period = period_R1; last_time = last_time_R1; break;
        case 4: period = period_R2; last_time = last_time_R2; break;
        case 5: period = period_R3; last_time = last_time_R3; break;
    }
    portEXIT_CRITICAL(&timerMux);

    // Timeout: Nếu quá 250ms không có xung -> Xe đang đứng im
    if (now - last_time > STOP_TIMEOUT_US) return 0;
    if (period == 0) return 0;

    // RPM = (60 * 1,000,000) / (90 xung * chu kỳ)
    return (int16_t)(666666 / period); 
}

bool isWheelStopped(int motorID) { return (calculateRPM(motorID) == 0); }

bool isRoverCompletelyStopped() {
    for (int i = 0; i < 6; i++) {
        if (!isWheelStopped(i)) return false;
    }
    return true;
}
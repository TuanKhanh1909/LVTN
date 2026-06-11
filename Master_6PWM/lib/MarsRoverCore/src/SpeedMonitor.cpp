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

const int hallPins[NUM_MOTORS] = {HALL_PIN_L1, HALL_PIN_L2, HALL_PIN_L3, HALL_PIN_R1, HALL_PIN_R2, HALL_PIN_R3};

// --- CÁC BIẾN NGẮT (VOLATILE) ---
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t _pulseCounts[NUM_MOTORS] = {0};
volatile uint32_t _firstPulseTime[NUM_MOTORS] = {0};
volatile uint32_t _lastPulseTime[NUM_MOTORS] = {0};
volatile uint32_t _prevLastPulseTime[NUM_MOTORS] = {0};

float _rpmHistory[NUM_MOTORS][5] = {0};
int _historyIdx[NUM_MOTORS] = {0};
float _filteredRPM[NUM_MOTORS] = {0};

float ema_battery = 0.0; // Biến lọc trung bình Pin

// --- CẤU HÌNH ---
#define TIMEOUT_ZERO_RPM 150000
#define MIN_PULSE_TIME_US 1000 // Chặn nhiễu cực đại
#define PULSES_PER_REV 90.0 // Ne = 90 xung/vòng

/*// Cấu trúc dữ liệu chuẩn bị cho thuật toán M/T
struct MotorPulseData
{
    volatile int32_t count;      // Tương đương N
    volatile int64_t first_tick; // Tương đương ISR(first_time_tick)
    volatile int64_t last_tick;  // Tương đương ISR(last_time_tick)
};

// Khởi tạo mảng lưu trữ cho 6 bánh xe: [L1, L2, L3, R1, R2, R3]
MotorPulseData mData[6];
static int16_t rpm_cache[6] = {0};
*/



// =======================================================
// CÁC HÀM NGẮT PHẦN CỨNG (INTERRUPT SERVICE ROUTINES)
// Dùng MUX để đảm bảo an toàn trên ESP32 Dual Core
// =======================================================
void IRAM_ATTR isr_motor_core(int id) {
    uint32_t t = micros();
    portENTER_CRITICAL_ISR(&timerMux);
    uint32_t dt_noise = t - _lastPulseTime[id];
    
    if (dt_noise > MIN_PULSE_TIME_US) { 
        if (_pulseCounts[id] == 0) {
            _firstPulseTime[id] = t; 
        }
        _prevLastPulseTime[id] = _lastPulseTime[id]; 
        _lastPulseTime[id] = t;                 
        _pulseCounts[id]++;                  
    }
    portEXIT_CRITICAL_ISR(&timerMux);
}
void IRAM_ATTR isr_motor0() { isr_motor_core(0); }
void IRAM_ATTR isr_motor1() { isr_motor_core(1); }
void IRAM_ATTR isr_motor2() { isr_motor_core(2); }
void IRAM_ATTR isr_motor3() { isr_motor_core(3); }
void IRAM_ATTR isr_motor4() { isr_motor_core(4); }
void IRAM_ATTR isr_motor5() { isr_motor_core(5); }
// ====================================================================
// 2. KHỞI TẠO CHÂN NGẮT
// ====================================================================
void setupSpeedMonitor() {
    for (int i = 0; i < NUM_MOTORS; i++) {
        pinMode(hallPins[i], INPUT_PULLUP);
    }
    // Gắn ngắt (Sử dụng RISING hoặc CHANGE đều được, đồng bộ với PULSES_PER_REVOLUTION)
    attachInterrupt(digitalPinToInterrupt(hallPins[0]), isr_motor0, CHANGE);
    attachInterrupt(digitalPinToInterrupt(hallPins[1]), isr_motor1, CHANGE);
    attachInterrupt(digitalPinToInterrupt(hallPins[2]), isr_motor2, CHANGE);
    attachInterrupt(digitalPinToInterrupt(hallPins[3]), isr_motor3, CHANGE);
    attachInterrupt(digitalPinToInterrupt(hallPins[4]), isr_motor4, CHANGE);
    attachInterrupt(digitalPinToInterrupt(hallPins[5]), isr_motor5, CHANGE);

    // Cài đặt Pin
    pinMode(BATTERY_PIN, INPUT);
    ema_battery = (analogRead(BATTERY_PIN) / 4095.0) * 62.5; // Khởi tạo giá trị đầu
}


// =======================================================
// TÍNH TOÁN RPM
// =======================================================
void updateSpeedMonitor() {
    uint32_t now_us = micros();
    // 1. Đọc điện áp Pin (Bộ lọc EMA)
    float raw_voltage = (analogRead(BATTERY_PIN) / 4095.0) * 62.5;
    ema_battery = (0.1 * raw_voltage) + (0.9 * ema_battery);

    // 2. Tính toán tốc độ 6 bánh
    for (int i = 0; i < NUM_MOTORS; i++) {
        uint32_t count = 0;
        uint32_t tFirst = 0, tLast = 0, tPrev = 0;

        portENTER_CRITICAL(&timerMux);
        count = _pulseCounts[i];
        tFirst = _firstPulseTime[i];
        tLast = _lastPulseTime[i];
        tPrev = _prevLastPulseTime[i];
        _pulseCounts[i] = 0; 
        portEXIT_CRITICAL(&timerMux);

        float rawRPM = 0.0f;

        // THUẬT TOÁN HYBRID M/T (Cập nhật cho chu kỳ 50ms)
        if ((now_us - tLast) > TIMEOUT_ZERO_RPM) {
            rawRPM = 0.0f;
        } 
        else if (count >= 2) {
            uint32_t dt = tLast - tFirst;
            if (dt > 0) {
                rawRPM = (float)(count - 1) * (60000000.0f / (PULSES_PER_REVOLUTION * dt));
            }
        } 
        else if (count == 1) {
            uint32_t dt = tLast - tPrev; 
            if (dt > 0) {
                rawRPM = 60000000.0f / (PULSES_PER_REVOLUTION * dt);
            }
        }
        else {
            rawRPM = _rpmHistory[i][(_historyIdx[i] + 4) % 5]; // Lấy lại số cũ nếu chưa timeout
        }

        // BỘ LỌC CỬA SỔ TRƯỢT 5 MẪU
        _rpmHistory[i][_historyIdx[i]] = rawRPM;
        _historyIdx[i] = (_historyIdx[i] + 1) % 5;
        
        float sum = 0;
        for(int j = 0; j < 5; j++) sum += _rpmHistory[i][j];
        _filteredRPM[i] = sum / 5.0f;
    }
}

// Các hàm trả về dữ liệu
float calculateRPM(int motorIndex) {
    if (motorIndex < 0 || motorIndex >= NUM_MOTORS) return 0.0f;
    return _filteredRPM[motorIndex];
}

float readBatteryVoltage(){
    return ema_battery;
}

//bool isWheelStopped(int motorID) { return (calculateRPM(motorID) == 0); }

// bool isRoverCompletelyStopped()// {
//     for (int i = 0; i < 6; i++)
//     {
//         if (!isWheelStopped(i))
//             return false;
//     }
//     return true;
// }
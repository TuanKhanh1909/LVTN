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
#define SAMPLE_TIME_MS 350  // 350ms không có xung = Coi như dừng hẳn
#define PULSES_PER_REV 90.0 // Ne = 90 xung/vòng

// Cấu trúc dữ liệu chuẩn bị cho thuật toán M/T
struct MotorPulseData
{
    volatile int32_t count;      // Tương đương N
    volatile int64_t first_tick; // Tương đương ISR(first_time_tick)
    volatile int64_t last_tick;  // Tương đương ISR(last_time_tick)
};

// Khởi tạo mảng lưu trữ cho 6 bánh xe: [L1, L2, L3, R1, R2, R3]
MotorPulseData mData[6];
static int16_t rpm_cache[6] = {0};

/*
// --- BIẾN TOÀN CỤC LƯU TRỮ NGẮT ---
static volatile int64_t last_time_L1 = 0, last_time_L2 = 0, last_time_L3 = 0;
static volatile int64_t last_time_R1 = 0, last_time_R2 = 0, last_time_R3 = 0;

// BIẾN ĐẾM XUNG (PULSE COUNTERS)
static volatile int32_t pulse_L1 = 0, pulse_L2 = 0, pulse_L3 = 0;
static volatile int32_t pulse_R1 = 0, pulse_R2 = 0, pulse_R3 = 0;

//BIẾN LƯU TRỮ RPM (CACHE)
static int16_t rpm_L1 = 0, rpm_L2 = 0, rpm_L3 = 0;
static int16_t rpm_R1 = 0, rpm_R2 = 0, rpm_R3 = 0;
*/

portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
float ema_battery = 0.0; // Biến lọc trung bình Pin

// --- CÁC HÀM NGẮT (ISR) SIÊU NHẸ BÉN ---
// Thuật toán M: Ngắt chỉ làm 1 việc duy nhất là TĂNG BIẾN ĐẾM
void IRAM_ATTR onHallL1()
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&timerMux);
    if (mData[0].count == 0)
        mData[0].first_tick = now; // Ghi nhận T_first
    mData[0].last_tick = now;      // Liên tục cập nhật T_last
    mData[0].count++;              // Tăng đếm N
    portEXIT_CRITICAL_ISR(&timerMux);
}
void IRAM_ATTR onHallL2()
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&timerMux);
    if (mData[1].count == 0)
        mData[1].first_tick = now;
    mData[1].last_tick = now;
    mData[1].count++;
    portEXIT_CRITICAL_ISR(&timerMux);
}
void IRAM_ATTR onHallL3()
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&timerMux);
    if (mData[2].count == 0)
        mData[2].first_tick = now;
    mData[2].last_tick = now;
    mData[2].count++;
    portEXIT_CRITICAL_ISR(&timerMux);
}
void IRAM_ATTR onHallR1()
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&timerMux);
    if (mData[3].count == 0)
        mData[3].first_tick = now;
    mData[3].last_tick = now;
    mData[3].count++;
    portEXIT_CRITICAL_ISR(&timerMux);
}
void IRAM_ATTR onHallR2()
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&timerMux);
    if (mData[4].count == 0)
        mData[4].first_tick = now;
    mData[4].last_tick = now;
    mData[4].count++;
    portEXIT_CRITICAL_ISR(&timerMux);
}
void IRAM_ATTR onHallR3()
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&timerMux);
    if (mData[5].count == 0)
        mData[5].first_tick = now;
    mData[5].last_tick = now;
    mData[5].count++;
    portEXIT_CRITICAL_ISR(&timerMux);
}

// ====================================================================
// 2. KHỞI TẠO CHÂN NGẮT
// ====================================================================
void setupSpeedMonitor()
{
    for (int i = 0; i < 6; i++)
    {
        mData[i].count = 0;
    }

    // BẮT BUỘC DÙNG INPUT_PULLUP VÀ CHANGE NHƯ CODE PID
    pinMode(HALL_PIN_L1, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(HALL_PIN_L1), onHallL1, CHANGE);
    pinMode(HALL_PIN_L2, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(HALL_PIN_L2), onHallL2, CHANGE);
    pinMode(HALL_PIN_L3, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(HALL_PIN_L3), onHallL3, CHANGE);
    pinMode(HALL_PIN_R1, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(HALL_PIN_R1), onHallR1, CHANGE);
    pinMode(HALL_PIN_R2, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(HALL_PIN_R2), onHallR2, CHANGE);
    pinMode(HALL_PIN_R3, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(HALL_PIN_R3), onHallR3, CHANGE);

    pinMode(BATTERY_PIN, INPUT);
    ema_battery = (analogRead(BATTERY_PIN) / 4095.0) * 61.5;
}

float readBatteryVoltage()
{
    float raw_voltage = (analogRead(BATTERY_PIN) / 4095.0) * 62.5;
    ema_battery = (0.1 * raw_voltage) + (0.9 * ema_battery);
    return ema_battery;
}

// ====================================================================
// 3. THUẬT TOÁN TÍNH TỐC ĐỘ (M/T ALGORITHM)
// ====================================================================
int16_t calculateRPM(int motorID)
{
    static unsigned long last_calc_time = 0;
    unsigned long now_ms = millis();

    if (now_ms - last_calc_time >= SAMPLE_TIME_MS)
    {
        MotorPulseData localData[6];

        portENTER_CRITICAL(&timerMux);
        for (int i = 0; i < 6; i++)
        {
            localData[i].count = mData[i].count;
            localData[i].first_tick = mData[i].first_tick;
            localData[i].last_tick = mData[i].last_tick;
            mData[i].count = 0; // Khi count bị reset về 0, ISR sẽ tự động cập nhật lại first_tick ở lần ngắt tiếp theo
        }
        portEXIT_CRITICAL(&timerMux);

        for (int i = 0; i < 6; i++)
        {
            if (localData[i].count > 1)
            { // Giống y chang điều kiện N > 1 bên code PID
                float T0 = (localData[i].last_tick - localData[i].first_tick) / 1000000.0;

                if (T0 > 0)
                {
                    float current_rpm = ((localData[i].count - 1) * 60.0) / (T0 * PULSES_PER_REV);
                    if (current_rpm < 5.0)
                        current_rpm = 0.0; // Bỏ qua sai số khi quay quá chậm
                    rpm_cache[i] = (int16_t)current_rpm;
                }
                else
                {
                    rpm_cache[i] = 0;
                }
            }
            else
            {
                rpm_cache[i] = 0;
            }
        }
        last_calc_time = now_ms;
    }

    if (motorID >= 0 && motorID < 6)
        return rpm_cache[motorID];
    return 0;
}

bool isWheelStopped(int motorID) { return (calculateRPM(motorID) == 0); }

bool isRoverCompletelyStopped()
{
    for (int i = 0; i < 6; i++)
    {
        if (!isWheelStopped(i))
            return false;
    }
    return true;
}

// Hàm này lấy trực tiếp biến đếm thô từ phần cứng
int32_t getRawPulse(int motorID)
{
    if (motorID >= 0 && motorID < 6)
        return mData[motorID].count;
    return 0;
}
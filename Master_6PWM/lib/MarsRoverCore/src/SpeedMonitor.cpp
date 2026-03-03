#include <Arduino.h>
#include "esp_timer.h"

// --- CẤU HÌNH CHÂN HALL ---
#define HALL_PIN_L1 34
#define HALL_PIN_L2 35
#define HALL_PIN_L3 32
#define HALL_PIN_R1 33
#define HALL_PIN_R2 25
#define HALL_PIN_R3 26

// --- CẤU HÌNH THỜI GIAN ---
// 1. Chống nhiễu điện (Debounce): Bỏ qua các xung cách nhau dưới 2ms (2000us)
#define DEBOUNCE_US 2000

// 2. Thời gian quyết định DỪNG (Timeout):
// Nếu sau 250ms (1/4 giây) mà KHÔNG có xung nào mới -> Khẳng định xe đã dừng hẳn.
// (Số này em có thể tăng/giảm tùy thuộc vào quán tính thực tế của xe)
#define STOP_TIMEOUT_US 250000

// Số cặp cực (Thông thường Hub Motor là 15)
// #define POLE_PAIRS      15.0

// --- BIẾN TOÀN CỤC --- (Chỉ cần lưu Thời gian của xung cuối cùng) ---
static volatile int64_t last_time_L1 = 0;
static volatile int64_t last_time_L2 = 0;
static volatile int64_t last_time_L3 = 0;
static volatile int64_t last_time_R1 = 0;
static volatile int64_t last_time_R2 = 0;
static volatile int64_t last_time_R3 = 0;

portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

// extern portMUX_TYPE timerMux;

// --- CÁC HÀM NGẮT (ISR) ---
// --- CÁC HÀM NGẮT (ISR) Siêu nhẹ bén ---
// Chỉ làm 1 việc: Ghi lại thời điểm hiện tại khi bánh xe nhúc nhích
void IRAM_ATTR onHallL1()
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&timerMux);
    if (now - last_time_L1 > DEBOUNCE_US)
        last_time_L1 = now;
    portEXIT_CRITICAL_ISR(&timerMux);
}
void IRAM_ATTR onHallL2()
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&timerMux);
    if (now - last_time_L2 > DEBOUNCE_US)
        last_time_L2 = now;
    portEXIT_CRITICAL_ISR(&timerMux);
}
void IRAM_ATTR onHallL3()
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&timerMux);
    if (now - last_time_L3 > DEBOUNCE_US)
        last_time_L3 = now;
    portEXIT_CRITICAL_ISR(&timerMux);
}

void IRAM_ATTR onHallR1()
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&timerMux);
    if (now - last_time_R1 > DEBOUNCE_US)
        last_time_R1 = now;
    portEXIT_CRITICAL_ISR(&timerMux);
}
void IRAM_ATTR onHallR2()
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&timerMux);
    if (now - last_time_R2 > DEBOUNCE_US)
        last_time_R2 = now;
    portEXIT_CRITICAL_ISR(&timerMux);
}
void IRAM_ATTR onHallR3()
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&timerMux);
    if (now - last_time_R3 > DEBOUNCE_US)
        last_time_R3 = now;
    portEXIT_CRITICAL_ISR(&timerMux);
}

// --- HÀM KHỞI TẠO ---
void setupSpeedMonitor()
{
    pinMode(HALL_PIN_L1, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(HALL_PIN_L1), onHallL1, RISING);
    pinMode(HALL_PIN_L2, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(HALL_PIN_L2), onHallL2, RISING);
    pinMode(HALL_PIN_L3, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(HALL_PIN_L3), onHallL3, RISING);

    pinMode(HALL_PIN_R1, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(HALL_PIN_R1), onHallR1, RISING);
    pinMode(HALL_PIN_R2, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(HALL_PIN_R2), onHallR2, RISING);
    pinMode(HALL_PIN_R3, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(HALL_PIN_R3), onHallR3, RISING);
}

// --- HÀM KIỂM TRA TỪNG BÁNH (True = Đã dừng, False = Đang quay) ---
bool isWheelStopped(int motorID)
{
    int64_t last_time = 0;
    int64_t now = esp_timer_get_time();

    portENTER_CRITICAL(&timerMux);
    switch (motorID)
    {
    case 0:
        last_time = last_time_L1;
        break;
    case 1:
        last_time = last_time_L2;
        break;
    case 2:
        last_time = last_time_L3;
        break;
    case 3:
        last_time = last_time_R1;
        break;
    case 4:
        last_time = last_time_R2;
        break;
    case 5:
        last_time = last_time_R3;
        break;
    default:
        portEXIT_CRITICAL(&timerMux);
        return true; // Lỗi ID thì mặc định cho là an toàn (Dừng)
    }
    portEXIT_CRITICAL(&timerMux);

    // Nếu thời gian kể từ xung cuối cùng > 250ms -> Bánh xe đã dừng
    return (now - last_time > STOP_TIMEOUT_US);
}

// --- HÀM QUYẾT ĐỊNH CHO TỔNG THỂ XE ---
// Chỉ khi nào CẢ 6 BÁNH đều dừng hẳn, mới cho phép đảo chiều động cơ
bool isRoverCompletelyStopped()
{
    for (int i = 0; i < 6; i++)
    {
        if (!isWheelStopped(i))
        {
            return false; // Chỉ cần 1 bánh còn quay, lập tức báo False
        }
    }
    return true; // Tất cả đều đã dừng
}

#ifndef RPM_METER_H
#define RPM_METER_H

#include <Arduino.h>
#include "esp_timer.h"

// --- CẤU HÌNH CHÂN HALL ---
#define HALL_PIN_L1     34  
#define HALL_PIN_L2     35 
#define HALL_PIN_L3     32 
#define HALL_PIN_R1     33   
#define HALL_PIN_R2     25 
#define HALL_PIN_R3     26 

// Số cặp cực (Thông thường Hub Motor là 15)
#define POLE_PAIRS      15.0  

// BỘ LỌC NHIỄU (3000us = 3ms)
#define NOISE_FILTER_US 3000 

// --- BIẾN TOÀN CỤC ---
volatile uint32_t pulse_L1 = 0; volatile int64_t time_L1 = 0;
volatile uint32_t pulse_L2 = 0; volatile int64_t time_L2 = 0;
volatile uint32_t pulse_L3 = 0; volatile int64_t time_L3 = 0;

volatile uint32_t pulse_R1 = 0; volatile int64_t time_R1 = 0;
volatile uint32_t pulse_R2 = 0; volatile int64_t time_R2 = 0;
volatile uint32_t pulse_R3 = 0; volatile int64_t time_R3 = 0;

// Biến lưu thời gian xung trước để lọc nhiễu
volatile int64_t last_irq_L1 = 0, last_irq_L2 = 0, last_irq_L3 = 0;
volatile int64_t last_irq_R1 = 0, last_irq_R2 = 0, last_irq_R3 = 0;

extern portMUX_TYPE timerMux; 

// --- CÁC HÀM NGẮT (ISR) ---
void IRAM_ATTR onHallL1() { 
    int64_t now = esp_timer_get_time();
    if (now - last_irq_L1 > NOISE_FILTER_US) { 
        portENTER_CRITICAL_ISR(&timerMux); pulse_L1++; time_L1 = now; portEXIT_CRITICAL_ISR(&timerMux); 
        last_irq_L1 = now;
    }
}
void IRAM_ATTR onHallL2() { 
    int64_t now = esp_timer_get_time();
    if (now - last_irq_L2 > NOISE_FILTER_US) {
        portENTER_CRITICAL_ISR(&timerMux); pulse_L2++; time_L2 = now; portEXIT_CRITICAL_ISR(&timerMux); 
        last_irq_L2 = now;
    }
}
void IRAM_ATTR onHallL3() { 
    int64_t now = esp_timer_get_time();
    if (now - last_irq_L3 > NOISE_FILTER_US) {
        portENTER_CRITICAL_ISR(&timerMux); pulse_L3++; time_L3 = now; portEXIT_CRITICAL_ISR(&timerMux); 
        last_irq_L3 = now;
    }
}

void IRAM_ATTR onHallR1() { 
    int64_t now = esp_timer_get_time();
    if (now - last_irq_R1 > NOISE_FILTER_US) {
        portENTER_CRITICAL_ISR(&timerMux); pulse_R1++; time_R1 = now; portEXIT_CRITICAL_ISR(&timerMux); 
        last_irq_R1 = now;
    }
}
void IRAM_ATTR onHallR2() { 
    int64_t now = esp_timer_get_time();
    if (now - last_irq_R2 > NOISE_FILTER_US) {
        portENTER_CRITICAL_ISR(&timerMux); pulse_R2++; time_R2 = now; portEXIT_CRITICAL_ISR(&timerMux); 
        last_irq_R2 = now;
    }
}
void IRAM_ATTR onHallR3() { 
    int64_t now = esp_timer_get_time();
    if (now - last_irq_R3 > NOISE_FILTER_US) {
        portENTER_CRITICAL_ISR(&timerMux); pulse_R3++; time_R3 = now; portEXIT_CRITICAL_ISR(&timerMux); 
        last_irq_R3 = now;
    }
}

// --- HÀM KHỞI TẠO ---
void setupRPM() {
    pinMode(HALL_PIN_L1, INPUT_PULLUP); attachInterrupt(digitalPinToInterrupt(HALL_PIN_L1), onHallL1, RISING);
    pinMode(HALL_PIN_L2, INPUT_PULLUP); attachInterrupt(digitalPinToInterrupt(HALL_PIN_L2), onHallL2, RISING);
    pinMode(HALL_PIN_L3, INPUT_PULLUP); attachInterrupt(digitalPinToInterrupt(HALL_PIN_L3), onHallL3, RISING);
    
    pinMode(HALL_PIN_R1, INPUT_PULLUP); attachInterrupt(digitalPinToInterrupt(HALL_PIN_R1), onHallR1, RISING);
    pinMode(HALL_PIN_R2, INPUT_PULLUP); attachInterrupt(digitalPinToInterrupt(HALL_PIN_R2), onHallR2, RISING);
    pinMode(HALL_PIN_R3, INPUT_PULLUP); attachInterrupt(digitalPinToInterrupt(HALL_PIN_R3), onHallR3, RISING);
}

// --- HÀM TÍNH RPM (PHIÊN BẢN MỚI DÙNG MOTOR ID) ---
float getRPM(int motorID, uint32_t &prev_count, int64_t &prev_time) {
    uint32_t current_count = 0;
    int64_t current_time = 0;

    // Vào vùng cấm địa để lấy dữ liệu an toàn
    portENTER_CRITICAL(&timerMux);
    switch (motorID) {
        case 0: current_count = pulse_L1; current_time = time_L1; break;
        case 1: current_count = pulse_L2; current_time = time_L2; break;
        case 2: current_count = pulse_L3; current_time = time_L3; break;
        case 3: current_count = pulse_R1; current_time = time_R1; break;
        case 4: current_count = pulse_R2; current_time = time_R2; break;
        case 5: current_count = pulse_R3; current_time = time_R3; break;
        default: portEXIT_CRITICAL(&timerMux); return 0.0;
    }
    portEXIT_CRITICAL(&timerMux);

    uint32_t delta_pulses = current_count - prev_count;
    if (delta_pulses == 0) return 0.0;

    int64_t delta_time_us = current_time - prev_time;
    prev_count = current_count;
    prev_time = current_time;

    if (delta_time_us < 1000) return 0.0; 

    float rpm = ((float)delta_pulses / POLE_PAIRS) * (60000000.0 / (float)delta_time_us);
    return rpm;
}
#endif
#ifndef SPEED_MONITOR_H
#define SPEED_MONITOR_H

#include <Arduino.h>

#define PULSES_PER_REVOLUTION 90.0f
#define NUM_MOTORS 6

void setupSpeedMonitor();

// bool isWheelStopped(int motorID);

// Hàm này BẮT BUỘC phải được gọi mỗi 20ms trong Task_Control
void updateSpeedMonitor();

// Lấy RPM đã qua lọc nhiễu
float calculateRPM(int motorIndex);

// // 2 Hàm mới phục vụ Telemetry
// int16_t calculateRPM(int motorID);

float readBatteryVoltage();


#endif
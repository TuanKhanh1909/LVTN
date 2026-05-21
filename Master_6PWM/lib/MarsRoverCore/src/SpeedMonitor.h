#ifndef SPEED_MONITOR_H
#define SPEED_MONITOR_H

#include <Arduino.h>

void setupSpeedMonitor();
bool isWheelStopped(int motorID);
bool isRoverCompletelyStopped();

// 2 Hàm mới phục vụ Telemetry
int16_t calculateRPM(int motorID);
float readBatteryVoltage();

int32_t getRawPulse(int motorID); // Hàm mới dùng để debug

#endif
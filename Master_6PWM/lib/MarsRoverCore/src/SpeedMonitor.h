#ifndef SPEED_MONITOR_H
#define SPEED_MONITOR_H

#include <Arduino.h>

void setupSpeedMonitor();

bool isWheelStopped(int motorID);

bool isRoverCompletelyStopped();

#endif
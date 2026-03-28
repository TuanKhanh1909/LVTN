/**
 * @file RoverTypes.h
 * @brief Định nghĩa các kiểu dữ liệu dùng chung cho toàn bộ hệ thống.
 * Giúp các module (Input, Motor, Logic) giao tiếp với nhau theo một chuẩn thống nhất.
 */
#ifndef ROVER_TYPES_H
#define ROVER_TYPES_H

#include <Arduino.h>

// --- CẤU TRÚC LỆNH ĐIỀU KHIỂN CHUẨN (CONTROL COMMAND) ---
// Dùng xung (Pulse) làm đơn vị chuẩn vì độ mịn cao hơn PWM 0-255 và tương thích RC.
// Dải giá trị: 1000 (Lùi Max) - 1500 (Đứng yên) - 2000 (Tiến Max)
struct ControlCommand {
    uint16_t pulseL;    // Xung điều khiển bên Trái
    uint16_t pulseR;    // Xung điều khiển bên Phải
    bool connected;     // Cờ báo trạng thái kết nối (True = Có tín hiệu)
};

// --- CÁC NGUỒN ĐIỀU KHIỂN (PRIORITY SOURCE) ---
enum InputSource {
    SOURCE_NONE,    // Mất kết nối (Failsafe kích hoạt)
    SOURCE_RC,      // Tay cầm RC chuyên nghiệp (Ưu tiên 1)
    SOURCE_ESP_NOW, // Tay cầm tự chế (Ưu tiên 2)
    SOURCE_WEB      // Web Server (Ưu tiên 3)
};

// --- TRẠNG THÁI VẬN HÀNH AN TOÀN (SAFETY STATE MACHINE) ---
// Dùng để xử lý logic bên trong bộ điều khiển (Core Logic)
enum RoverState {
    STATE_IDLE,             // Xe đang đứng yên, phanh khóa
    STATE_DRIVING,          // Xe đang di chuyển (Soft-start hoạt động)
    STATE_BRAKING_TO_SWITCH // Xe đang phanh chờ dừng hẳn để đảo chiều (Bảo vệ hộp số)
};

// --- TRẠNG THÁI HIỂN THỊ (DISPLAY STATE) ---
// Dùng để hiển thị lên LCD hoặc Web cho người dùng biết xe đang làm gì
enum MotionType {
    MOTION_STOP,
    MOTION_FORWARD,     // Tiến thẳng
    MOTION_BACKWARD,    // Lùi thẳng
    MOTION_FWD_LEFT,    // Tiến + Rẽ trái
    MOTION_FWD_RIGHT,   // Tiến + Rẽ phải
    MOTION_BCK_LEFT,    // Lùi + Rẽ trái
    MOTION_BCK_RIGHT,   // Lùi + Rẽ phải
    MOTION_SPIN_LEFT,   // Xoay trái tại chỗ
    MOTION_SPIN_RIGHT   // Xoay phải tại chỗ
};
// --- TRẠNG THÁI TỪ CƠ KHÍ GỬI LÊN (NỘI BỘ) ---
struct DriveStatus {
    int16_t pwmL;
    int16_t pwmR;
    MotionType motion;
};

// --- GÓI TIN ĐO LƯỜNG TỔNG HỢP (TELEMETRY) ---
struct TelemetryPacket {
    float batteryVoltage;    
    int16_t rpm[6];          // [L1, L2, L3, R1, R2, R3]
    int16_t pwmLeft;         
    int16_t pwmRight;        
    MotionType motionState;  
    InputSource activeMode;  
    bool isFailsafeLatched;  
};
#endif
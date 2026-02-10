/**
 * @file rc_test.cpp
 * @brief Chương trình kiểm tra đọc tín hiệu PWM từ tay cầm RC HotRC F-06A
 */

#include <Arduino.h>

// --- CẤU HÌNH CHÂN ---
#define RC_CH1_PIN  22  // Kênh 1: Ga (Steering)
#define RC_CH2_PIN  21  // Kênh 2: Lái (Throttle)

// --- BIẾN LƯU TRỮ (Volatile để dùng trong ngắt) ---
volatile unsigned long pwm_start_ch1 = 0;
volatile unsigned long pwm_value_ch1 = 1500; // Mặc định giữa

volatile unsigned long pwm_start_ch2 = 0;
volatile unsigned long pwm_value_ch2 = 1500; // Mặc định giữa

// --- HÀM NGẮT (ISR) ---
// IRAM_ATTR giúp hàm chạy trong RAM -> Tốc độ cực nhanh

void IRAM_ATTR read_ch1() {
    unsigned long now = micros();
    // Nếu chân đang lên mức CAO -> Bắt đầu đếm
    if (digitalRead(RC_CH1_PIN) == HIGH) {
        pwm_start_ch1 = now;
    } 
    // Nếu chân xuống mức THẤP -> Kết thúc đếm
    else {
        pwm_value_ch1 = now - pwm_start_ch1;
    }
}

void IRAM_ATTR read_ch2() {
    unsigned long now = micros();
    if (digitalRead(RC_CH2_PIN) == HIGH) {
        pwm_start_ch2 = now;
    } else {
        pwm_value_ch2 = now - pwm_start_ch2;
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n--- TEST KET NOI TAY CAM RC (HotRC F-06A) ---");

    // Cấu hình chân Input
    pinMode(RC_CH1_PIN, INPUT);
    pinMode(RC_CH2_PIN, INPUT);

    // Gắn ngắt (Lắng nghe sự thay đổi trạng thái chân)
    attachInterrupt(digitalPinToInterrupt(RC_CH1_PIN), read_ch1, CHANGE);
    attachInterrupt(digitalPinToInterrupt(RC_CH2_PIN), read_ch2, CHANGE);
}

void loop() {
    // 1. Đọc giá trị từ biến chia sẻ (Nên tắt ngắt tạm thời để đọc an toàn)
    noInterrupts();
    unsigned long ch1 = pwm_value_ch1;
    unsigned long ch2 = pwm_value_ch2;
    interrupts();

    // 2. In ra màn hình
    Serial.print("CH1 (Lai): ");
    Serial.print(ch1);
    Serial.print(" us  |  ");

    Serial.print("CH2 (Ga): ");
    Serial.print(ch2);
    Serial.print(" us  |  ");

    // 3. Phân tích trạng thái (Demo)
    String status = "DUNG YEN";
    if (ch2 > 1600) status = "TIEN";
    if (ch2 < 1400) status = "LUI";
    if (ch1 > 1600) status += " + PHAI";
    if (ch1 < 1400) status += " + TRAI";

    Serial.println("-> " + status);

    delay(100); // Cập nhật 10 lần/giây
}
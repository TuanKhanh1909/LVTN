// ====================================================================================
// ==        CODE TAY CẦM MARS ROVER - PHIÊN BẢN HOÀN CHỈNH "PRO" (ĐÃ SỬA LỖI)       ==
// ====================================================================================

// --- PHẦN 1: KHAI BÁO THƯ VIỆN & PHẦN CỨNG ---
#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <EEPROM.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Định nghĩa phần cứng
#define JOY_X_PIN          34
#define JOY_Y_PIN          35
#define POT_PIN            33
#define CALIBRATE_BTN_PIN  32

// Cấu hình LCD (Em đang dùng 20x4, rất tốt!)
#define LCD_SDA_PIN 21
#define LCD_SCL_PIN 22
LiquidCrystal_I2C lcd(0x27, 20, 4); // Địa chỉ 0x27, 20 cột, 4 dòng

// --- PHẦN 2: CẤU HÌNH ESP-NOW & EEPROM ---
uint8_t receiverMAC[] = {0xD4, 0xE9, 0xF4, 0xE9, 0x4B, 0x30}; // MAC của xe
typedef struct {
  uint16_t pulse_left;
  uint16_t pulse_right;
} ControlPacket;
ControlPacket dataToSend;
// ---> THÊM CẤU TRÚC ĐỂ HỨNG DỮ LIỆU TỪ XE GỬI VỀ <---
typedef struct {
    float batteryVoltage;    
    int16_t rpm[6];          
    int16_t pwmLeft;         
    int16_t pwmRight;        
    int motionState;  // Ép kiểu int cho Enum
    int activeMode;   // Ép kiểu int cho Enum
    bool isFailsafeLatched;  
} TelemetryPacket;

TelemetryPacket roverData = {0.0, {0}, 0, 0, 0, 0, true};
unsigned long lastRoverDataTime = 0; // Thời gian nhận data cuối cùng

#define EEPROM_SIZE 16
#define EEPROM_MAGIC_VALUE 0xCA

// Biến lưu trữ giá trị hiệu chỉnh
uint16_t joyX_min = 0, joyX_max = 4095, joyX_center = 2048;
uint16_t joyY_min = 0, joyY_max = 4095, joyY_center = 2048;

// --- PHẦN 3: CÁC BIẾN TOÀN CỤC & HẰNG SỐ ---
unsigned long lastSendTime = 0;
const unsigned long SEND_INTERVAL = 50;
unsigned long lastSuccessTime = 0;

float joyX_filtered = 2048.0;
float joyY_filtered = 2048.0;
float pot_filtered = 2048.0;
const float ALPHA = 0.3;

// --- PHẦN 4: CÁC HÀM CHỨC NĂNG ---
void saveCalibration() {
  EEPROM.write(0, EEPROM_MAGIC_VALUE);
  EEPROM.put(1, joyX_min); EEPROM.put(3, joyX_max); EEPROM.put(5, joyX_center);
  EEPROM.put(7, joyY_min); EEPROM.put(9, joyY_max); EEPROM.put(11, joyY_center);
  EEPROM.commit();
}

bool loadCalibration() {
  if (EEPROM.read(0) == EEPROM_MAGIC_VALUE) {
    EEPROM.get(1, joyX_min); EEPROM.get(3, joyX_max); EEPROM.get(5, joyX_center);
    EEPROM.get(7, joyY_min); EEPROM.get(9, joyY_max); EEPROM.get(11, joyY_center);
    return true;
  }
  return false;
}

void runCalibration() {
  lcd.clear();
  lcd.print("CALIBRATION MODE");
  lcd.setCursor(0, 1);
  lcd.print("Move sticks now!");

  uint16_t temp_x_min = 4095, temp_x_max = 0;
  uint16_t temp_y_min = 4095, temp_y_max = 0;
  
  delay(1000);
  joyX_center = analogRead(JOY_X_PIN);
  joyY_center = analogRead(JOY_Y_PIN);
  
  unsigned long startTime = millis();
  while (millis() - startTime < 5000) {
    uint16_t x = analogRead(JOY_X_PIN);
    uint16_t y = analogRead(JOY_Y_PIN);
    if (x < temp_x_min) temp_x_min = x;
    if (x > temp_x_max) temp_x_max = x;
    if (y < temp_y_min) temp_y_min = y;
    if (y > temp_y_max) temp_y_max = y;
    delay(10);
  }
  
  joyX_min = temp_x_min; joyX_max = temp_x_max;
  joyY_min = temp_y_min; joyY_max = temp_y_max;

  saveCalibration();

  lcd.clear();
  lcd.print("Calib. Saved!");
  lcd.setCursor(0, 1);
  lcd.print("Rebooting...");
  delay(2000);
  ESP.restart();
}

// --- HÀM CALLBACK KHI NHẬN ĐƯỢC DATA TỪ XE ---
void onDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  if (len == sizeof(TelemetryPacket)) {
    memcpy(&roverData, incomingData, sizeof(roverData));
    lastRoverDataTime = millis();
  }
}

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    lastSuccessTime = millis(); // CẬP NHẬT THỜI GIAN KHI GỬI THÀNH CÔNG
  }
}

// ====================================================================================
// ==                                 HÀM SETUP()                                    ==
// ====================================================================================
void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  
  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Mars Rover CTRL");

  pinMode(CALIBRATE_BTN_PIN, INPUT_PULLUP);
  
  if (!loadCalibration()) {
    lcd.setCursor(0, 1);
    lcd.print("No calib data!");
    delay(2000);
  } else {
    lcd.setCursor(0, 1);
    lcd.print("Calib loaded OK");
    delay(1000);
  }
  
  joyX_filtered = joyX_center;
  joyY_filtered = joyY_center;

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) { return; }
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv); // <--- ĐĂNG KÝ HÀM NHẬN Ở ĐÂY
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
  
  lcd.clear();
}

// ====================================================================================
// ==                                  HÀM LOOP()                                    ==
// ====================================================================================
void loop() {
  // ---> THÊM ĐOẠN NÀY VÀO NGAY ĐẦU HÀM LOOP <---
  // Liên tục giám sát nút nhấn Calib
  if (digitalRead(CALIBRATE_BTN_PIN) == LOW) {
    delay(1000); // Chống dội phím (Debounce)
    if (digitalRead(CALIBRATE_BTN_PIN) == LOW) { // Xác nhận người dùng thực sự đang nhấn
      runCalibration();
    }
  }

  if (millis() - lastSendTime >= SEND_INTERVAL) {
    lastSendTime = millis(); // VỊ TRÍ ĐÚNG CỦA DÒNG NÀY

    // --- BƯỚC 1: ĐỌC VÀ LỌC NHIỄU TÍN HIỆU ---
    joyX_filtered = (ALPHA * analogRead(JOY_X_PIN)) + ((1 - ALPHA) * joyX_filtered);
    joyY_filtered = (ALPHA * analogRead(JOY_Y_PIN)) + ((1 - ALPHA) * joyY_filtered);
    pot_filtered =  (ALPHA * analogRead(POT_PIN))   + ((1 - ALPHA) * pot_filtered);
    
    // --- BƯỚC 2: MAP GIÁ TRỊ VÀ TÍNH TOÁN ---
    int speed_percent = map(pot_filtered, 0, 4095, 0, 100);
    float speed_factor = speed_percent / 100.0;

    uint16_t threshold_low_X = joyX_center - (joyX_center - joyX_min) * 0.2;
    uint16_t threshold_high_X = joyX_center + (joyX_max - joyX_center) * 0.2;
    uint16_t threshold_low_Y = joyY_center - (joyY_center - joyY_min) * 0.2;
    uint16_t threshold_high_Y = joyY_center + (joyY_max - joyY_center) * 0.2;

    uint16_t pulse_left = 1500;
    uint16_t pulse_right = 1500;
    String modeStr = "NEUTRAL";

    // --- BƯỚC 3: LOGIC ĐIỀU KHIỂN NÂNG CAO ---
    const int FORWARD_RANGE = 500, BACKWARD_RANGE = 500; 

    if (joyY_filtered < threshold_low_Y) { // NHÓM LỆNH TIẾN
      if (joyX_filtered < threshold_low_X) {
        modeStr = "FWD-LEFT";
        pulse_right = 1500 + (FORWARD_RANGE * speed_factor);
        pulse_left  = 1500 + (FORWARD_RANGE * speed_factor * 0.4);
      } else if (joyX_filtered > threshold_high_X) {
        modeStr = "FWD-RIGHT";
        pulse_left  = 1500 + (FORWARD_RANGE * speed_factor);
        pulse_right = 1500 + (FORWARD_RANGE * speed_factor * 0.4);
      } else {
        modeStr = "FORWARD";
        pulse_left  = 1500 + (FORWARD_RANGE * speed_factor);
        pulse_right = 1500 + (FORWARD_RANGE * speed_factor);
      }
    } 
    else if (joyY_filtered > threshold_high_Y) { // NHÓM LỆNH LÙI
      // (logic lùi giống như trước)
      if (joyX_filtered < threshold_low_X) { modeStr = "BCK-LEFT"; pulse_right = 1500 - (BACKWARD_RANGE * speed_factor); pulse_left = 1500 - (BACKWARD_RANGE * speed_factor * 0.4); }
      else if (joyX_filtered > threshold_high_X) { modeStr = "BCK-RIGHT"; pulse_left = 1500 - (BACKWARD_RANGE * speed_factor); pulse_right = 1500 - (BACKWARD_RANGE * speed_factor * 0.4); }
      else { modeStr = "BACKWARD"; pulse_left = 1500 - (BACKWARD_RANGE * speed_factor); pulse_right = 1500 - (BACKWARD_RANGE * speed_factor); }
    } 
    else { // NHÓM LỆNH TẠI CHỖ
      if (joyX_filtered < threshold_low_X) { modeStr = "SPIN-LEFT"; pulse_left = 1250; pulse_right = 1750; }
      else if (joyX_filtered > threshold_high_X) { modeStr = "SPIN-RIGHT"; pulse_left = 1750; pulse_right = 1250; }
    }

    // --- BƯỚC 4: GỬI DỮ LIỆU ĐI ---
    dataToSend.pulse_left = pulse_left;
    dataToSend.pulse_right = pulse_right;
    esp_now_send(receiverMAC, (uint8_t *)&dataToSend, sizeof(dataToSend));
    
    // --- BƯỚC 5: HIỂN THỊ LÊN LCD 20x4 CHUẨN CÔNG NGHIỆP ---
    char buffer[21]; // Buffer cho LCD 20 cột
    
    // Dòng 1: Tọa độ X, Y
    lcd.setCursor(0, 0);
    sprintf(buffer, "X:%-4.0f   Y:%-4.0f", joyX_filtered, joyY_filtered);
    lcd.printf("%-20s", buffer);

    // Dòng 2: Tốc độ % và ĐIỆN ÁP PIN TỪ XE GỬI VỀ
    lcd.setCursor(0, 1);
    if (millis() - lastRoverDataTime < 2000) { // Có sóng 2 chiều
        sprintf(buffer, "Spd:%3d%%  Bat:%.1fV", speed_percent, roverData.batteryVoltage);
    } else { // Mất sóng chiều về
        sprintf(buffer, "Spd:%3d%%  Bat:--.-V", speed_percent);
    }
    lcd.printf("%-20s", buffer);

    // Dòng 3: Trạng thái cơ khí
    lcd.setCursor(0, 2);
    sprintf(buffer, "Cmd:%s", modeStr.c_str());
    lcd.printf("%-20s", buffer);

    // Dòng 4: Tình trạng kết nối RF & Trạng thái Khóa Failsafe
    lcd.setCursor(0, 3);
    String connStr = (millis() - lastSuccessTime < 2000) ? "RF:OK " : "RF:ERR";
    
    String fsStr = "FS:--";
    if (millis() - lastRoverDataTime < 2000) { // Nếu nhận được tín hiệu từ xe
        fsStr = roverData.isFailsafeLatched ? "FS:LOCKED" : "FS:SAFE";
    }
    sprintf(buffer, "%s %s", connStr.c_str(), fsStr.c_str());
    lcd.printf("%-20s", buffer);

    // In ra Serial Monitor để debug
    Serial.printf("X: %.0f | Y: %.0f | Spd: %d%% || Sent -> L: %d | R: %d\n",
                  joyX_filtered, joyY_filtered, speed_percent, dataToSend.pulse_left, dataToSend.pulse_right);
  }
}
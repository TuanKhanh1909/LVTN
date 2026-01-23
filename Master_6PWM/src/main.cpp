// ====================================================================================
// ==    MASTER 6WD - HIGH SENSITIVITY VISUALIZATION (HIỂN THỊ ĐỘ NHẠY CAO)          ==
// ====================================================================================
#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "RPM_meter.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>

// --- 1. CẤU HÌNH WEB SERVER ---
const char *ssid = "ESP32-Mars-Rover";
const char *password = "12345678";
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Biến trạng thái Web
volatile bool webControlEnabled = false;
volatile uint16_t web_JoyX = 2048;
volatile uint16_t web_JoyY = 2048;
volatile uint16_t web_Pot = 0;
unsigned long lastWebTime = 0;

// --- 2. CẤU HÌNH XE ---
float TRIM_L1 = 1.00;
float TRIM_L2 = 0.9;
float TRIM_L3 = 1.00;
float TRIM_R1 = 1.00;
float TRIM_R2 = 0.9;
float TRIM_R3 = 1.00;

enum RobotState
{
    STATE_IDLE,
    STATE_FORWARD,
    STATE_BACKWARD,
    STATE_FWD_LEFT,
    STATE_FWD_RIGHT,
    STATE_BCK_LEFT,
    STATE_BCK_RIGHT,
    STATE_SPIN_LEFT,
    STATE_SPIN_RIGHT,
    STATE_BRAKING
};

#define PIN_PWM_L1 16
#define PIN_PWM_L2 13
#define PIN_PWM_L3 5
#define PIN_DIR_L 17
#define PIN_BRAKE_L 23

#define PIN_PWM_R1 4
#define PIN_PWM_R2 27
#define PIN_PWM_R3 14
#define PIN_DIR_R 18
#define PIN_BRAKE_R 19

#define CH_L1 0
#define CH_L2 1
#define CH_L3 2
#define CH_R1 3
#define CH_R2 4
#define CH_R3 5

const int PWM_FREQ = 5000;
const int PWM_RES = 8;
const int PWM_FIXED_REVERSE = 180;
const int PWM_ASSIST_FWD = 100;
const int PWM_TURN_FWD = 110;

unsigned long brakeStartTime = 0;
const unsigned long BRAKE_TIMEOUT = 2000;
unsigned long zeroDetectTime = 0;
const unsigned long CONFIRM_STOP_TIME = 200;

unsigned long lastControlTime = 0;
const unsigned long CONTROL_INTERVAL = 10;
unsigned long lastPrintTime = 0;
const unsigned long PRINT_INTERVAL = 200;
unsigned long lastBroadcastTime = 0;

const float RPM_SAFE_ZERO = 6.0;
const int MIN_PWM_START = 85;
float currentPWM_L = 0;
float currentPWM_R = 0;
const float RAMP_STEP = 6.0;

portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

// --- CẤU TRÚC GÓI TIN ---
typedef struct
{
    uint16_t pulse_left;
    uint16_t pulse_right;
} ControlPacket;

// Biến lưu xung nhận được
volatile uint16_t inPulse_L = 1500;
volatile uint16_t inPulse_R = 1500;
unsigned long lastRecvTime = 0;

RobotState currentState = STATE_IDLE;
RobotState desireState = STATE_IDLE;
RobotState nextStatePending = STATE_IDLE;

// BIẾN RPM
uint32_t prev_L1_cnt = 0;
int64_t prev_L1_time = 0;
uint32_t prev_L2_cnt = 0;
int64_t prev_L2_time = 0;
uint32_t prev_L3_cnt = 0;
int64_t prev_L3_time = 0;
uint32_t prev_R1_cnt = 0;
int64_t prev_R1_time = 0;
uint32_t prev_R2_cnt = 0;
int64_t prev_R2_time = 0;
uint32_t prev_R3_cnt = 0;
int64_t prev_R3_time = 0;

// ====================================================================================
// ==               XỬ LÝ DỮ LIỆU HIỂN THỊ (FIXED & AMPLIFIED)                       ==
// ====================================================================================

void broadcastStatus()
{
    // Chỉ gửi dữ liệu nếu đang dùng Tay cầm (ESP-NOW)
    if (webControlEnabled)
        return;

    uint16_t pL, pR;
    portENTER_CRITICAL(&timerMux);
    pL = inPulse_L;
    pR = inPulse_R;
    portEXIT_CRITICAL(&timerMux);

    // 1. Tính toán độ lệch
    long diffL = (long)pL - 1500;
    long diffR = (long)pR - 1500;

    // 2. Tách thành phần
    long fwd = (diffL + diffR) / 2;
    long turn = (diffL - diffR);

    // 3. CẤU HÌNH HIỂN THỊ
    // Độ nhạy cao cho Joystick (cho tốc độ thấp 12/255)
    const int SENSITIVITY_XY = 25;
    // Scale lớn cho POT để hiển thị đúng % lực yếu
    const int SCALE_POT = 500;

    // --- TRỤC Y (Tiến/Lùi) ---
    // Web: 0 là Lên, 4095 là Xuống
    // fwd > 0 là Tiến -> Map về 0
    int joyY_Web = map(fwd, SENSITIVITY_XY, -SENSITIVITY_XY, 0, 4095);

    // --- TRỤC X (Trái/Phải) ---
    // Web: 0 là Trái, 4095 là Phải
    // Mặc định: turn < 0 là Trái -> Map về 0
    int joyX_Web = map(turn, -SENSITIVITY_XY, SENSITIVITY_XY, 0, 4095);

    // [FIX QUAN TRỌNG] ĐẢO NGƯỢC TRỤC X KHI ĐANG LÙI
    // Khi lùi (fwd âm), logic hiệu số (L-R) bị ngược dấu so với khi tiến.
    // Ta dùng ngưỡng -5 để tránh nhảy loạn xạ khi đứng yên (Spin).
    if (fwd < -5)
    {
        joyX_Web = 4095 - joyX_Web;
    }

    // --- POT ---
    long maxVal = max(abs(fwd), abs(turn));
    int pot_Web = map(maxVal, 0, SCALE_POT, 0, 4095);

    // Giới hạn biên (Quan trọng sau khi đảo dấu)
    joyX_Web = constrain(joyX_Web, 0, 4095);
    joyY_Web = constrain(joyY_Web, 0, 4095);
    pot_Web = constrain(pot_Web, 0, 4095);

    // 4. GỬI LÊN WEB
    String msg = "X:" + String(joyX_Web) + ",Y:" + String(joyY_Web) + ",POT:" + String(pot_Web);
    ws.textAll(msg);
}

// Xử lý gói tin từ Web gửi xuống
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
    {
        data[len] = 0;
        String message = (char *)data;

        if (message.startsWith("CMD_EN:"))
        {
            portENTER_CRITICAL(&timerMux);
            webControlEnabled = (message.substring(7).toInt() == 1);
            if (!webControlEnabled)
            {
                web_JoyX = 2048;
                web_JoyY = 2048;
                web_Pot = 0;
            }
            portEXIT_CRITICAL(&timerMux);
        }
        else if (message.startsWith("X:"))
        {
            portENTER_CRITICAL(&timerMux);
            lastWebTime = millis();

            if (webControlEnabled)
            {
                int idxY = message.indexOf(",Y:");
                int idxPot = message.indexOf(",POT:");
                if (idxY > 0 && idxPot > 0)
                {
                    web_JoyX = message.substring(2, idxY).toInt();
                    web_JoyY = message.substring(idxY + 3, idxPot).toInt();
                    web_Pot = message.substring(idxPot + 5).toInt();
                }
            }
            portEXIT_CRITICAL(&timerMux);
        }
    }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    if (type == WS_EVT_DATA)
        handleWebSocketMessage(arg, data, len);
}

void initWebSocket()
{
    ws.onEvent(onEvent);
    server.addHandler(&ws);
}

void initFS()
{
    if (!SPIFFS.begin())
        Serial.println("SPIFFS Error");
}

void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
    if (webControlEnabled)
        return;

    if (len == sizeof(ControlPacket))
    {
        ControlPacket d;
        memcpy(&d, incomingData, sizeof(d));
        portENTER_CRITICAL(&timerMux);
        inPulse_L = d.pulse_left;
        inPulse_R = d.pulse_right;
        portEXIT_CRITICAL(&timerMux);
        lastRecvTime = millis();
    }
}

// --- LOGIC ---
int getDirection(uint16_t pulse)
{
    if (pulse > 1520)
        return 1;
    else if (pulse < 1480)
        return -1;
    else
        return 0;
}

RobotState determineDesiredState()
{
    uint16_t pL, pR;
    portENTER_CRITICAL(&timerMux);
    pL = inPulse_L;
    pR = inPulse_R;
    portEXIT_CRITICAL(&timerMux);

    if (millis() - lastRecvTime > 500)
        return STATE_IDLE;

    int dirL = getDirection(pL);
    int dirR = getDirection(pR);

    if (dirL == 0 && dirR == 0)
        return STATE_IDLE;
    if (dirL == 1 && dirR == 1)
        return STATE_FORWARD;
    if (dirL == -1 && dirR == -1)
        return STATE_BACKWARD;
    if (dirL == -1 && dirR == 1)
        return STATE_SPIN_LEFT;
    if (dirL == 1 && dirR == -1)
        return STATE_SPIN_RIGHT;
    if (dirL == 0 && dirR == 1)
        return STATE_FWD_LEFT;
    if (dirL == 1 && dirR == 0)
        return STATE_FWD_RIGHT;
    if (dirL == 0 && dirR == -1)
        return STATE_BCK_LEFT;
    if (dirL == -1 && dirR == 0)
        return STATE_BCK_RIGHT;

    return STATE_IDLE;
}

int mapPulseToPWM(uint16_t pulse)
{
    int pwm = 0;
    if (pulse > 1520)
        pwm = map(pulse, 1520, 2000, MIN_PWM_START, 255);
    else if (pulse < 1480)
        pwm = PWM_FIXED_REVERSE;
    return constrain(pwm, 0, 255);
}

void processSoftStart(int targetL, int targetR)
{
    if (currentPWM_L < targetL)
    {
        currentPWM_L += RAMP_STEP;
        if (currentPWM_L > targetL)
            currentPWM_L = targetL;
    }
    else if (currentPWM_L > targetL)
    {
        currentPWM_L -= RAMP_STEP;
        if (currentPWM_L < targetL)
            currentPWM_L = targetL;
    }

    if (currentPWM_R < targetR)
    {
        currentPWM_R += RAMP_STEP;
        if (currentPWM_R > targetR)
            currentPWM_R = targetR;
    }
    else if (currentPWM_R > targetR)
    {
        currentPWM_R -= RAMP_STEP;
        if (currentPWM_R < targetR)
            currentPWM_R = targetR;
    }
}

void applyMotorControl(int pwmL, int pwmR, bool dirL_isFwd, bool dirR_isFwd, bool doBrake)
{
    digitalWrite(PIN_BRAKE_L, doBrake ? HIGH : LOW);
    digitalWrite(PIN_BRAKE_R, doBrake ? HIGH : LOW);

    if (doBrake)
    {
        ledcWrite(CH_L1, 255);
        ledcWrite(CH_L2, 255);
        ledcWrite(CH_L3, 255);
        ledcWrite(CH_R1, 255);
        ledcWrite(CH_R2, 255);
        ledcWrite(CH_R3, 255);
        return;
    }

    digitalWrite(PIN_DIR_L, dirL_isFwd ? LOW : HIGH);
    digitalWrite(PIN_DIR_R, dirR_isFwd ? LOW : HIGH);

    ledcWrite(CH_L1, (int)(255 - pwmL * TRIM_L1));
    ledcWrite(CH_L2, (int)(255 - pwmL * TRIM_L2));
    ledcWrite(CH_L3, (int)(255 - pwmL * TRIM_L3));
    ledcWrite(CH_R1, (int)(255 - pwmR * TRIM_R1));
    ledcWrite(CH_R2, (int)(255 - pwmR * TRIM_R2));
    ledcWrite(CH_R3, (int)(255 - pwmR * TRIM_R3));
}

void runStateMachine()
{
    desireState = determineDesiredState();

    if (currentState == STATE_BRAKING)
    {
        float rpm_L1 = getRPM(0, prev_L1_cnt, prev_L1_time);
        float rpm_L2 = getRPM(1, prev_L2_cnt, prev_L2_time);
        float rpm_L3 = getRPM(2, prev_L3_cnt, prev_L3_time);
        float rpm_R1 = getRPM(3, prev_R1_cnt, prev_R1_time);
        float rpm_R2 = getRPM(4, prev_R2_cnt, prev_R2_time);
        float rpm_R3 = getRPM(5, prev_R3_cnt, prev_R3_time);

        float max_rpm_system = 0;
        max_rpm_system = max(max_rpm_system, abs(rpm_L1));
        max_rpm_system = max(max_rpm_system, abs(rpm_L2));
        max_rpm_system = max(max_rpm_system, abs(rpm_L3));
        max_rpm_system = max(max_rpm_system, abs(rpm_R1));
        max_rpm_system = max(max_rpm_system, abs(rpm_R2));
        max_rpm_system = max(max_rpm_system, abs(rpm_R3));

        bool isReadyToSwitch = false;
        if (max_rpm_system > RPM_SAFE_ZERO)
        {
            zeroDetectTime = 0;
        }
        else
        {
            if (zeroDetectTime == 0)
                zeroDetectTime = millis();
            else if (millis() - zeroDetectTime > CONFIRM_STOP_TIME)
                isReadyToSwitch = true;
        }

        if (millis() - brakeStartTime > BRAKE_TIMEOUT)
            isReadyToSwitch = true;

        if (isReadyToSwitch)
        {
            currentState = nextStatePending;
            currentPWM_L = 0;
            currentPWM_R = 0;
            zeroDetectTime = 0;
        }
        else
        {
            applyMotorControl(0, 0, true, true, true);
        }
        return;
    }
    else
    {
        if (desireState != currentState)
        {
            currentState = STATE_BRAKING;
            nextStatePending = desireState;
            brakeStartTime = millis();
            applyMotorControl(0, 0, true, true, true);
            currentPWM_L = 0;
            currentPWM_R = 0;
            return;
        }
    }

    int target_L = 0;
    int target_R = 0;
    int joystick_L = mapPulseToPWM(inPulse_L);
    int joystick_R = mapPulseToPWM(inPulse_R);
    int pwm_assist_target = 85;

    switch (currentState)
    {
    case STATE_IDLE:
        target_L = 0;
        target_R = 0;
        break;
    case STATE_FORWARD:
        target_L = joystick_L;
        target_R = joystick_R;
        break;
    case STATE_BACKWARD:
        target_L = PWM_FIXED_REVERSE;
        target_R = PWM_FIXED_REVERSE;
        break;
    case STATE_SPIN_LEFT:
        target_L = PWM_FIXED_REVERSE;
        target_R = PWM_ASSIST_FWD;
        break;
    case STATE_SPIN_RIGHT:
        target_L = PWM_ASSIST_FWD;
        target_R = PWM_FIXED_REVERSE;
        break;
    case STATE_FWD_LEFT:
        target_L = PWM_FIXED_REVERSE;
        target_R = PWM_TURN_FWD;
        break;
    case STATE_FWD_RIGHT:
        target_L = PWM_TURN_FWD;
        target_R = PWM_FIXED_REVERSE;
        break;
    case STATE_BCK_LEFT:
        target_L = pwm_assist_target;
        target_R = PWM_FIXED_REVERSE;
        break;
    case STATE_BCK_RIGHT:
        target_L = PWM_FIXED_REVERSE;
        target_R = pwm_assist_target;
        break;
    default:
        target_L = 0;
        target_R = 0;
        break;
    }

    processSoftStart(target_L, target_R);

    int out_L = (int)currentPWM_L;
    int out_R = (int)currentPWM_R;

    switch (currentState)
    {
    case STATE_IDLE:
        applyMotorControl(out_L, out_R, true, true, true);
        break;
    case STATE_FORWARD:
        applyMotorControl(out_L, out_R, true, true, false);
        break;
    case STATE_BACKWARD:
        applyMotorControl(out_L, out_R, false, false, false);
        break;
    case STATE_SPIN_LEFT:
        applyMotorControl(out_L, out_R, false, true, false);
        break;
    case STATE_SPIN_RIGHT:
        applyMotorControl(out_L, out_R, true, false, false);
        break;
    case STATE_FWD_LEFT:
        applyMotorControl(out_L, out_R, false, true, false);
        break;
    case STATE_FWD_RIGHT:
        applyMotorControl(out_L, out_R, true, false, false);
        break;
    case STATE_BCK_LEFT:
        applyMotorControl(out_L, out_R, true, false, false);
        break;
    case STATE_BCK_RIGHT:
        applyMotorControl(out_L, out_R, false, true, false);
        break;
    default:
        applyMotorControl(0, 0, true, true, true);
        break;
    }
}

// ====================================================================================
// ==  SETUP & LOOP                                                                  ==
// ====================================================================================
void setup()
{
    Serial.begin(115200);

    ledcSetup(CH_L1, PWM_FREQ, PWM_RES);
    ledcAttachPin(PIN_PWM_L1, CH_L1);
    ledcSetup(CH_L2, PWM_FREQ, PWM_RES);
    ledcAttachPin(PIN_PWM_L2, CH_L2);
    ledcSetup(CH_L3, PWM_FREQ, PWM_RES);
    ledcAttachPin(PIN_PWM_L3, CH_L3);
    ledcSetup(CH_R1, PWM_FREQ, PWM_RES);
    ledcAttachPin(PIN_PWM_R1, CH_R1);
    ledcSetup(CH_R2, PWM_FREQ, PWM_RES);
    ledcAttachPin(PIN_PWM_R2, CH_R2);
    ledcSetup(CH_R3, PWM_FREQ, PWM_RES);
    ledcAttachPin(PIN_PWM_R3, CH_R3);

    pinMode(PIN_BRAKE_L, OUTPUT);
    pinMode(PIN_BRAKE_R, OUTPUT);
    pinMode(PIN_DIR_L, OUTPUT);
    pinMode(PIN_DIR_R, OUTPUT);
    digitalWrite(PIN_BRAKE_L, HIGH); 
    digitalWrite(PIN_BRAKE_R, HIGH);

    setupRPM();
    initFS();
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ssid, password);

    if (esp_now_init() != ESP_OK)
    {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    esp_now_register_recv_cb(onDataRecv);

    initWebSocket();
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/index.html", "text/html"); });
    server.serveStatic("/", SPIFFS, "/");
    server.begin();

    Serial.println("Master Controller Ready!");
}

void loop()
{
    ws.cleanupClients();
    unsigned long currentMillis = millis();

    // --- LOGIC TRỘN TÍN HIỆU WEB VÀO HỆ THỐNG ---
    if (webControlEnabled)
    {
        portENTER_CRITICAL(&timerMux);
        uint16_t wX = web_JoyX;
        uint16_t wY = web_JoyY;
        uint16_t wPot = web_Pot;
        portEXIT_CRITICAL(&timerMux);

        float speed_factor = map(wPot, 0, 4095, 0, 100) / 100.0;
        const int RANGE = 500;
        uint16_t low = 1648, high = 2448;
        uint16_t tempPulseL = 1500, tempPulseR = 1500;

        if (wY < low)
        {
            if (wX < low)
            {
                tempPulseR = 1500 + (RANGE * speed_factor);
                tempPulseL = 1500 + (RANGE * speed_factor * 0.4);
            }
            else if (wX > high)
            {
                tempPulseL = 1500 + (RANGE * speed_factor);
                tempPulseR = 1500 + (RANGE * speed_factor * 0.4);
            }
            else
            {
                tempPulseL = 1500 + (RANGE * speed_factor);
                tempPulseR = 1500 + (RANGE * speed_factor);
            }
        }
        else if (wY > high)
        {
            if (wX < low)
            {
                tempPulseR = 1500 - (RANGE * speed_factor);
                tempPulseL = 1500 - (RANGE * speed_factor * 0.4);
            }
            else if (wX > high)
            {
                tempPulseL = 1500 - (RANGE * speed_factor);
                tempPulseR = 1500 - (RANGE * speed_factor * 0.4);
            }
            else
            {
                tempPulseL = 1500 - (RANGE * speed_factor);
                tempPulseR = 1500 - (RANGE * speed_factor);
            }
        }
        else
        {
            if (wX < low)
            {
                tempPulseL = 1500 - (RANGE * speed_factor);
                tempPulseR = 1500 + (RANGE * speed_factor);
            }
            else if (wX > high)
            {
                tempPulseL = 1500 + (RANGE * speed_factor);
                tempPulseR = 1500 - (RANGE * speed_factor);
            }
        }

        tempPulseL = constrain(tempPulseL, 1000, 2000);
        tempPulseR = constrain(tempPulseR, 1000, 2000);

        portENTER_CRITICAL(&timerMux);
        inPulse_L = tempPulseL;
        inPulse_R = tempPulseR;
        lastRecvTime = millis();
        portEXIT_CRITICAL(&timerMux);
    }

    // TÁC VỤ 1: ĐIỀU KHIỂN (10ms)
    if (currentMillis - lastControlTime >= CONTROL_INTERVAL)
    {
        lastControlTime = currentMillis;
        runStateMachine();
    }

    // TÁC VỤ 2: GỬI FEEDBACK WEB (100ms)
    if (currentMillis - lastBroadcastTime > 100)
    {
        broadcastStatus();
        lastBroadcastTime = currentMillis;
    }

    // TÁC VỤ 3: IN SERIAL (200ms)
    if (currentMillis - lastPrintTime > PRINT_INTERVAL)
    {
        lastPrintTime = currentMillis;
        float rL1 = getRPM(0, prev_L1_cnt, prev_L1_time);
        Serial.print("SRC:");
        Serial.print(webControlEnabled ? "WEB" : "NOW");
        Serial.print(" | pL:");
        Serial.print(inPulse_L);
        Serial.print(" pR:");
        Serial.print(inPulse_R);
        Serial.print(" | State:");
        Serial.print(currentState);
        Serial.print(" | RPM_L1:");
        Serial.println(rL1);
    }
}
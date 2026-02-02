#include "NetworkService.h"

// --- BIẾN TĨNH ĐỂ HỖ TRỢ CALLBACK ---
static NetworkService* instance = nullptr;

// Cấu trúc gói tin tay cầm cũ
typedef struct {
    uint16_t pulse_left;
    uint16_t pulse_right;
} EspNowPacket;

NetworkService::NetworkService(InputManager* inputMgr) 
    : _server(80), _ws("/ws"), _inputMgr(inputMgr) {
    _ssid = "ESP32-Mars-Rover";
    _password = "12345678";
    instance = this; // Gán con trỏ tĩnh
}

void NetworkService::begin() {
    // 1. Mount File System
    if (!SPIFFS.begin(true)) {
        Serial.println("[ERR] SPIFFS Mount Failed");
    }

    // 2. Setup WiFi & ESP-NOW
    setupWiFi();
    setupEspNow();

    // 3. Setup Web Server
    setupWebServer();
}

void NetworkService::update() {
    _ws.cleanupClients();
}

void NetworkService::broadcastStatus(String status) {
    _ws.textAll(status);
}

// --- PRIVATE SETUP ---

void NetworkService::setupWiFi() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(_ssid, _password);
    Serial.print("[INFO] AP IP: ");
    Serial.println(WiFi.softAPIP());
}

void NetworkService::setupEspNow() {
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ERR] ESP-NOW Init Failed");
        return;
    }
    // Đăng ký hàm Static làm callback
    esp_now_register_recv_cb(NetworkService::onEspNowRecv);
}

void NetworkService::setupWebServer() {
    // Xử lý sự kiện WebSocket
    _ws.onEvent(NetworkService::onWsEvent);
    _server.addHandler(&_ws);

    // Route file tĩnh
    _server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SPIFFS, "/index.html", "text/html");
    });
    _server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SPIFFS, "/style.css", "text/css");
    });
    _server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SPIFFS, "/script.js", "text/javascript");
    });

    _server.begin();
    Serial.println("[INFO] HTTP Server Started");
}

// --- STATIC CALLBACKS (Cầu nối giữa thư viện C và Class C++) ---

void NetworkService::onEspNowRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
    if (len == sizeof(EspNowPacket) && instance != nullptr) {
        EspNowPacket data;
        memcpy(&data, incomingData, sizeof(data));
        // Gọi vào InputManager thông qua con trỏ instance
        instance->_inputMgr->updateEspNow(data.pulse_left, data.pulse_right);
    }
}

void NetworkService::onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                               AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_DATA && instance != nullptr) {
        AwsFrameInfo *info = (AwsFrameInfo *)arg;
        if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
            data[len] = 0;
            String msg = (char *)data;

            // Phân tích cú pháp X:..,Y:..,POT:..
            if (msg.startsWith("X:")) {
                int idxY = msg.indexOf(",Y:");
                int idxPot = msg.indexOf(",POT:");
                if (idxY > 0 && idxPot > 0) {
                    int x = msg.substring(2, idxY).toInt();
                    int y = msg.substring(idxY + 3, idxPot).toInt();
                    int pot = msg.substring(idxPot + 5).toInt();
                    
                    // Đẩy dữ liệu vào InputManager
                    instance->_inputMgr->updateWeb(x, y, pot);
                }
            }
        }
    }
}
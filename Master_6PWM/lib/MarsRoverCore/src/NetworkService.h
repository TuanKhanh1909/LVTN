#ifndef NETWORK_SERVICE_H
#define NETWORK_SERVICE_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include "InputManager.h" // Cần truy cập InputManager để đẩy dữ liệu vào

class NetworkService {
private:
    AsyncWebServer _server;
    AsyncWebSocket _ws;
    InputManager* _inputMgr; // Con trỏ tới InputManager (Dependency Injection)

    const char* _ssid;
    const char* _password;

public:
    // Constructor nhận vào InputManager để sau này callback gọi được nó
    NetworkService(InputManager* inputMgr);

    void begin();
    
    // Hàm gọi trong vòng lặp TaskNetwork để dọn dẹp client
    void update();

    // Gửi trạng thái về Web
    void broadcastStatus(String status);

private:
    // Các hàm thiết lập nội bộ
    void setupWiFi();
    void setupEspNow();
    void setupWebServer();

    // Hàm xử lý sự kiện WebSocket (Static để dùng làm callback)
    static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                          AwsEventType type, void *arg, uint8_t *data, size_t len);
    
    // Hàm xử lý gói tin ESP-NOW (Static)
    static void onEspNowRecv(const uint8_t *mac, const uint8_t *incomingData, int len);
};

#endif
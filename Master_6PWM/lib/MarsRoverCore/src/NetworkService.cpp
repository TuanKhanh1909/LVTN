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

    // Địa chỉ MAC Tay cầm: B0:CB:D8:C5:E7:38
    uint8_t gamepadMac[6] = {0xB0, 0xCB, 0xD8, 0xC5, 0xE7, 0x38}; 
    memcpy(_remoteMac, gamepadMac, 6);
    _hasRemoteMac = true; // Khẳng định đã có MAC luôn từ đầu
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

    //4.Khởi tạo thời gian
    _lastClientConnectedTime = millis();
    _isWiFiOn = true;
}

void NetworkService::update() {
    //LOGIC BẢO VỆ CHIP
    unsigned long now = millis();

    //Chỉ kiểm tra khi WiFi đang BẬT
    if (_isWiFiOn){
        _ws.cleanupClients(); //Dọn dẹp client rác

        //Kiểm tra số lượng thiết bị đang kết nối vào WiFi AP
        //softAPgetStationNum() trả về số lượng điện thoại/laptop đang kết nối
        bool isWebConnected = (WiFi.softAPgetStationNum() > 0);

        // Kiểm tra xem tay cầm ESP-NOW có đang hoạt động không
        // Nếu nguồn đang lái là ESP_NOW thì TUYỆT ĐỐI KHÔNG TẮT WIFI
        bool isEspNowActive = (_inputMgr->getActiveSource() == SOURCE_ESP_NOW);

        //Nếu có BẤT KỲ kết nối nào (Web hoặc Tay cầm), reset bộ đếm timeout
        if(isWebConnected || isEspNowActive){
            _lastClientConnectedTime = now; //Có người dùng -> Reset timer
        }else{
            //Chỉ tắt khi KHÔNG dùng cả Web lẫn Tay cầm trong 30 giây
            if(now - _lastClientConnectedTime > WIFI_TIMEOUT){
                Serial.println("[INFO] No client! Turning OFF WiFi to save ESP32!");
                disableWiFi();
            }
        } 
    }else{
        //Nếu WiFi đang TẮT (Chế độ làm mát)
        //Đợi đủ 10 giây thì bật lại để tìm kết nối
        if (now - _wifiOffTime > WIFI_COOLDOWN){
            Serial.println("[INFO] Cooldown finished. Turning ON WiFi...");
            enableWiFi();
            _lastClientConnectedTime = now; //Reset timer để không bị tắt ngay lập tức
        }
    }    
}

void NetworkService::disableWiFi(){
    // Tắt chế độ AP để ngắt bộ phát sóng RF (nguồn nhiệt chính)
    // Lưu ý: Tắt WiFi có thể ảnh hưởng ESP-NOW nếu dùng chung kênh.
    // Nhưng an toàn là trên hết. Nếu muốn giữ ESP-NOW, chỉ tắt AP thôi.

    // Cách 1: Tắt hẳn WiFi (An toàn nhất cho nhiệt độ)
    WiFi.mode(WIFI_OFF);

    // Cách 2: Nếu muốn giữ ESP-NOW (Tay cầm vật lý) thì chỉ tắt AP (nhưng vẫn còn nhiệt)
    // WiFi.softAPdisconnect(true);
    // WiFi.mode(WIFI_STA);

    _isWiFiOn = false;
    _wifiOffTime = millis();

    // [CỰC KỲ QUAN TRỌNG] Báo cho InputManager biết là mất kết nối Web rồi
    // Để nó tự động phanh xe lại ngay lập tức
    // Cần thêm hàm resetWebInput() vào InputManager nếu chưa có
    //instance->_inputMgr->resetWebInput();
}

void NetworkService::enableWiFi(){
    // Bật lại WiFi AP + STA
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(_ssid, _password);
    
    // Cần khởi tạo lại ESP-NOW nếu lúc nãy đã tắt hoàn toàn
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ERR] ESP-NOW Restart Failed");
    } else {
        // ---> 4. THÊM LẠI PEER CỐ ĐỊNH SAU KHI RESTART ESP-NOW <---
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, _remoteMac, 6);
        peerInfo.channel = 0;
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);

        esp_now_register_recv_cb(NetworkService::onEspNowRecv);
    }
    
    _isWiFiOn = true;
}

void NetworkService::broadcastStatus(String status) {
    if(_isWiFiOn && WiFi.softAPgetStationNum() > 0){
        // CHỐNG TRÀN BỘ NHỚ (DROP FRAME CACHING)
        // 1. Kiểm tra xem có ai thực sự đang mở trang Web không (_ws.count() > 0)
        // 2. Kiểm tra xem hàng đợi WebSocket có đang rảnh rỗi không (availableForWriteAll)
        if (_ws.count() > 0 && _ws.availableForWriteAll()) {
            _ws.textAll(status);
        } else {
            // Nếu mạng đang nghẽn, thà "Bỏ rơi" (Drop) gói tin này còn hơn làm tràn RAM
            // Lặng lẽ bỏ qua, không làm gì cả
        }
    }
}   

// --- PRIVATE SETUP ---

void NetworkService::setupWiFi() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(_ssid, _password);
    // Giảm công suất phát sóng xuống một chút để đỡ nóng (Mặc định là 19.5dBm)
    WiFi.setTxPower(WIFI_POWER_17dBm);
    Serial.print("[INFO] AP IP: ");
    Serial.println(WiFi.softAPIP());
}

void NetworkService::setupEspNow() {
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ERR] ESP-NOW Init Failed");
        return;
    }
    // ---> 2. ĐĂNG KÝ PEER BẰNG MAC CỐ ĐỊNH TAY CẦM <---
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, _remoteMac, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) == ESP_OK) {
        Serial.println("[ESP-NOW] Da luu MAC Tay Cam co dinh thanh cong!");
    } else {
        Serial.println("[ERR] ESP-NOW Add Peer Failed!");
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
    _server.on("/rover.jpg", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SPIFFS, "/rover.jpg", "image/jpeg");
    });
    // ----------------------------------------------------
    _server.begin();
    Serial.println("[INFO] HTTP Server Started");
}

// --- STATIC CALLBACKS (Cầu nối giữa thư viện C và Class C++) ---

void NetworkService::onEspNowRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
    if (len == sizeof(EspNowPacket) && instance != nullptr) {
        
        // ---> 3. KIỂM TRA BẢO MẬT: BỘ LỌC MAC <---
        bool isFromMyGamepad = true;
        for (int i = 0; i < 6; i++) {
            if (mac[i] != instance->_remoteMac[i]) {
                isFromMyGamepad = false;
                break; // Sai 1 byte là từ chối luôn
            }
        }

        // Nếu ĐÚNG MAC tay cầm của em thì mới xử lý lệnh
        if (isFromMyGamepad) {
            EspNowPacket data;
            memcpy(&data, incomingData, sizeof(data));
            // Gọi vào InputManager thông qua con trỏ instance
            instance->_inputMgr->updateEspNow(data.pulse_left, data.pulse_right);
        }
    }
}

void NetworkService::onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                               AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_DATA && instance != nullptr) {
        AwsFrameInfo *info = (AwsFrameInfo *)arg;
        if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
            data[len] = 0;
            String msg = (char *)data;

            // ---> 1. XỬ LÝ LỆNH ĐỔI CHẾ ĐỘ TỪ CÔNG TẮC WEB <---
            if (msg.startsWith("MODE:")) {
                String mode = msg.substring(5);
                if (mode == "WEB") {
                    instance->_inputMgr->setControlMode(SOURCE_WEB);
                    Serial.println("[MODE] Da chuyen sang dieu khien WEB");
                }
                else if (mode == "ESPNOW") {
                    instance->_inputMgr->setControlMode(SOURCE_ESP_NOW);
                    Serial.println("[MODE] Da chuyen sang dieu khien ESP-NOW");
                }
                else if (mode == "RC") { // Thêm bộ giải mã cho RC
                    instance->_inputMgr->setControlMode(SOURCE_RC);
                    Serial.println("[MODE] Da chuyen sang dieu khien RC");
                }
                else { // Nếu nhận lệnh NONE (Tắt hết công tắc)
                    instance->_inputMgr->setControlMode(SOURCE_NONE);
                    Serial.println("[MODE] KHOA HE THONG (CHUA CHON NGUON)");
                }
            }
            
            // ---> 2. XỬ LÝ TỌA ĐỘ JOYSTICK TỪ WEB <---
            else if (msg.startsWith("X:")) {
                int idxY = msg.indexOf(",Y:");
                int idxPot = msg.indexOf(",POT:");
                if (idxY > 0 && idxPot > 0) {
                    int x = msg.substring(2, idxY).toInt();
                    int y = msg.substring(idxY + 3, idxPot).toInt();
                    int pot = msg.substring(idxPot + 5).toInt();
                    
                    // Chỉ cập nhật dữ liệu, quyền quyết định cho chạy hay không 
                    // là do InputManager (Hàm getCommand) lo.
                    instance->_inputMgr->updateWeb(x, y, pot);
                }
            }
        }
    }
}

void NetworkService::broadcastEspNowReport(ReportPacket packet) {
    // Chỉ gửi khi WiFi đang bật và đã bắt được MAC của tay cầm
    if (_isWiFiOn && _hasRemoteMac) {
        // Ép kiểu gói bưu phẩm TelemetryPacket thành mảng Byte và bắn đi
        esp_now_send(_remoteMac, (uint8_t *)&packet, sizeof(ReportPacket));
    }
}
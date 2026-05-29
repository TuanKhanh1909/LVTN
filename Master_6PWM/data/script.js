var gateway = `ws://${window.location.hostname}/ws`;
var websocket;

// Các biến Joystick
const joystick = document.getElementById('joystick');
const speedSlider = document.getElementById('speedSlider');
let isDragging = false;
let centerX = 0, centerY = 0;
const maxDistance = 70;

// Dữ liệu gửi đi (Mô phỏng phần cứng)
let currentX = 2048;   // Giữa (0-4095)
let currentY = 2048;   // Giữa (0-4095)
let currentPot = 0;    // Min (0-4095)
let lastSuccessTime = 0;
let currentMode = "NONE"; // Đổi khởi tạo ban đầu thành Khóa (NONE)
window.addEventListener('load', onload);

function onload(event) {
    initWebSocket();
    initJoystick();
    initSpeedSlider();
    // Gửi dữ liệu liên tục 50ms/lần (20Hz)
    setInterval(updateValuesAndSend, 50); 
}

function initWebSocket() {
    console.log('Connecting to WebSocket...');
    websocket = new WebSocket(gateway);
    websocket.onopen = function() { 
        document.getElementById('connectionStatus').className = 'connection-status connection-ok';
        document.getElementById('connectionStatus').textContent = '🟢 Connection: OK';
        lastSuccessTime = Date.now();
    };
    websocket.onclose = function() { 
        document.getElementById('connectionStatus').className = 'connection-status connection-lost';
        document.getElementById('connectionStatus').textContent = '🔴 Connection: LOST!';
        setTimeout(initWebSocket, 2000); 
    };
    websocket.onmessage = onMessage;
}

//Hàm xử lý nút gạt(mode)
// Hàm xử lý khi gạt 1 trong 3 nút công tắc
function toggleMode(source) {
    let webBtn = document.getElementById('webModeBtn');
    let espBtn = document.getElementById('espModeBtn');
    let rcBtn = document.getElementById('rcModeBtn');
    let statusText = document.getElementById('activeModeText');

    if (source === 'WEB') {
        if (webBtn.checked) {
            espBtn.checked = false; rcBtn.checked = false;
            currentMode = "WEB";
            statusText.textContent = "🌐 Điều khiển bằng WEB";
            statusText.style.color = "#3498db";
        } else currentMode = "NONE";
    } 
    else if (source === 'ESPNOW') {
        if (espBtn.checked) {
            webBtn.checked = false; rcBtn.checked = false;
            currentMode = "ESPNOW";
            statusText.textContent = "📡 Điều khiển bằng ESP-NOW";
            statusText.style.color = "#f1c40f";
        } else currentMode = "NONE";
    }
    else if (source === 'RC') {
        if (rcBtn.checked) {
            webBtn.checked = false; espBtn.checked = false;
            currentMode = "RC";
            statusText.textContent = "🎮 Tay cầm RC";
            statusText.style.color = "#2ecc71";
        } else currentMode = "NONE";
    }

    // Nếu cả 3 nút đều đang tắt -> Ép về chế độ Khóa
    if (!webBtn.checked && !espBtn.checked && !rcBtn.checked) {
        currentMode = "NONE";
        statusText.textContent = "⛔ ĐÃ KHÓA (Chưa chọn nguồn)";
        statusText.style.color = "#e74c3c";
    }

    // Gửi lệnh set Mode xuống ESP32
    if(websocket && websocket.readyState === WebSocket.OPEN) {
        websocket.send("MODE:" + currentMode);
    }

    // Nếu không phải WEB thì khóa tay cầm ảo lại, ép về giữa
    if (currentMode !== "WEB") {
        stopDrag();
    }
}

function onMessage(event) {
    lastSuccessTime = Date.now();
    // Chỉ cập nhật giao diện JoyStick ảo khi KHÔNG KÉO TAY
    // Nhưng các thông số Telemetry thì VẪN PHẢI CẬP NHẬT liên tục
    
    try {
        // Cố gắng giải mã JSON
        let data = JSON.parse(event.data);
        
        if (data.type === "tele") {
            // --- 1. XỬ LÝ CỜ FAILSAFE ---
            let banner = document.getElementById('failsafeBanner');
            if (data.fs === true) {
                banner.style.display = "block";
                if (currentMode === "WEB") stopDrag(); // Khóa cứng tay cầm trên Web
            } else {
                banner.style.display = "none";
            }

            // --- 2. CẬP NHẬT PIN ---
            let batBadge = document.getElementById('batteryStatus');
            let batVolts = parseFloat(data.bat).toFixed(1);
            batBadge.textContent = "🔋 Pin: " + batVolts + "V";
            // Đổi màu theo áp pin (Hệ 36V, đầy 42V)
            if (data.bat >= 37.0) batBadge.style.background = "#27ae60"; // Xanh lá
            else if (data.bat >= 34.0) batBadge.style.background = "#f39c12"; // Vàng
            else batBadge.style.background = "#c0392b"; // Đỏ

            // --- 3. CẬP NHẬT TỐC ĐỘ 6 BÁNH (RPM) ---
            document.getElementById('rpmL1').textContent = data.rpm[0];
            document.getElementById('rpmL2').textContent = data.rpm[1];
            document.getElementById('rpmL3').textContent = data.rpm[2];
            document.getElementById('rpmR1').textContent = data.rpm[3];
            document.getElementById('rpmR2').textContent = data.rpm[4];
            document.getElementById('rpmR3').textContent = data.rpm[5];

            // TÍNH TOÁN PWM TỪNG BÁNH (Dựa trên hệ số Trim trong main.cpp)
            document.getElementById('pwmL1').textContent = data.pwmL; // Trim 1.0
            document.getElementById('pwmL2').textContent = data.pwmL; // Trim 1.0
            document.getElementById('pwmL3').textContent = data.pwmL; // Trim 1.0
            
            document.getElementById('pwmR1').textContent = data.pwmR; // Trim 1.0
            
            document.getElementById('pwmR2').textContent = data.pwmR;
            //document.getElementById('pwmR2').textContent = Math.round(data.pwmR * 0.92); // Trim 0.92

            document.getElementById('pwmR3').textContent = data.pwmR; // Trim 1.0

            // --- 4. CẬP NHẬT PWM THỰC TẾ ---
            document.getElementById('telePwmL').textContent = data.pwmL;
            document.getElementById('telePwmR').textContent = data.pwmR;

            // --- 5. CẬP NHẬT TRẠNG THÁI VẬN HÀNH ---
                let modeText = "⏸️ STOP";
                switch(data.motion) {
                    case 0: modeText = "⏸️ STOP"; break;
                    case 1: modeText = "⬆️ FORWARD"; break;
                    case 2: modeText = "⬇️ BACKWARD"; break;
                    case 3: modeText = "↖️ FWD LEFT"; break;
                    case 4: modeText = "↗️ FWD RIGHT"; break;
                    case 5: modeText = "🔄 SPIN LEFT"; break;
                    case 6: modeText = "🔄 SPIN RIGHT"; break;
                }
                document.getElementById('modeDisplay').textContent = modeText;
            
        }
    } catch (e) {
        // Nếu chẳng may gói tin bị nhiễu rác không phải chuẩn JSON thì phớt lờ
        console.log("JSON Parse Error: ", e);
    }
}

function updateVisuals(x, y, pot) {
    // 1. Cập nhật các con số Text
    document.getElementById('xValue').textContent = x;
    document.getElementById('yValue').textContent = y;
    document.getElementById('potValue').textContent = pot;
    document.getElementById('speedValue').textContent = Math.round((pot / 4095) * 100) + "%";

    // 2. Dịch chuyển Joystick
    let deltaX = ((x - 2048) / 2047) * maxDistance;
    let deltaY = ((y - 2048) / 2047) * maxDistance;
    joystick.style.transform = `translate(calc(-50% + ${deltaX}px), calc(-50% + ${deltaY}px))`;
    
    // 3. Cập nhật vị trí thanh trượt
    speedSlider.value = pot;
 }
// --- LOGIC JOYSTICK ẢO ---
function initJoystick() {
    joystick.addEventListener('mousedown', startDrag);
    joystick.addEventListener('touchstart', startDrag);
    document.addEventListener('mousemove', drag);
    document.addEventListener('touchmove', drag);
    document.addEventListener('mouseup', stopDrag);
    document.addEventListener('touchend', stopDrag);
}

function startDrag(e) {
    // KHÓA: Chỉ cho phép kéo Joystick khi đang ở chế độ WEB
    if (currentMode !== "WEB") {
        alert("Vui lòng BẬT công tắc 'Cho phép WEB lái' để điều khiển!");
        return; 
    }
    isDragging = true;
    const rect = joystick.parentElement.getBoundingClientRect();
    centerX = rect.left + rect.width / 2;
    centerY = rect.top + rect.height / 2;
}

function initSpeedSlider() {
    speedSlider.addEventListener('input', function() {
        // KHÓA: Chỉ cho phép kéo Slider khi đang ở chế độ WEB
        if (currentMode !== "WEB") { 
            this.value = currentPot; 
            return;
        } 
        currentPot = parseInt(this.value);
        updateVisuals(currentX, currentY, currentPot);
    });
}

function drag(e) {
    if (!isDragging) return;
    e.preventDefault();
    const clientX = e.touches ? e.touches[0].clientX : e.clientX;
    const clientY = e.touches ? e.touches[0].clientY : e.clientY;
    
    let deltaX = clientX - centerX;
    let deltaY = clientY - centerY;
    const distance = Math.sqrt(deltaX*deltaX + deltaY*deltaY);
    
    if (distance > maxDistance) {
        const angle = Math.atan2(deltaY, deltaX);
        deltaX = Math.cos(angle) * maxDistance;
        deltaY = Math.sin(angle) * maxDistance;
    }
    
    //joystick.style.transform = `translate(calc(-50% + ${deltaX}px), calc(-50% + ${deltaY}px))`;
    
    // Map từ pixel sang giá trị 0-4095
    // Y đi lên là âm trong HTML, nhưng ta muốn Y<2048 là Tiến (giống tay cầm)
    // X đi phải là dương
    currentX = Math.round(2048 + (deltaX / maxDistance) * 2047);
    currentY = Math.round(2048 + (deltaY / maxDistance) * 2047);
    updateVisuals(currentX, currentY, currentPot); 
}

function stopDrag() {
    isDragging = false;
   // joystick.style.transform = 'translate(-50%, -50%)';
    currentX = 2048; // Về giữa
    currentY = 2048; // Về giữa
    updateVisuals(currentX, currentY, currentPot);
}


// --- GỬI DỮ LIỆU ---
function updateValuesAndSend() {
   // Chỉ gửi dữ liệu khi kết nối OK
    if (websocket && websocket.readyState === WebSocket.OPEN) {
        
        // QUAN TRỌNG: Chỉ gửi dữ liệu điều khiển khi ĐANG BẬT CHẾ ĐỘ WEB
        if (currentMode === "WEB") {
            let msg = `X:${currentX},Y:${currentY},POT:${currentPot}`;
            websocket.send(msg);
        }
        // Nếu tắt Web, hàm này KHÔNG làm gì cả -> Không ghi đè lên giao diện
    }
}
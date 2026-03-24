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
let currentMode = "RC"; // Mặc định mở Web lên là đang ở mode RC
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
// Hàm xử lý khi gạt 1 trong 2 nút công tắc
function toggleMode(source) {
    let webBtn = document.getElementById('webModeBtn');
    let espBtn = document.getElementById('espModeBtn');
    let statusText = document.getElementById('activeModeText');

    if (source === 'WEB') {
        if (webBtn.checked) {
            espBtn.checked = false; // Tự động tắt nút ESP-NOW
            currentMode = "WEB";
            statusText.textContent = "🌐 Điều khiển bằng WEB";
            statusText.style.color = "#3498db";
        } else {
            currentMode = "RC"; // Tắt Web thì tự về RC
        }
    } 
    else if (source === 'ESPNOW') {
        if (espBtn.checked) {
            webBtn.checked = false; // Tự động tắt nút WEB
            currentMode = "ESPNOW";
            statusText.textContent = "📡 Điều khiển bằng ESP-NOW";
            statusText.style.color = "#f1c40f";
        } else {
            currentMode = "RC"; // Tắt ESP-NOW thì tự về RC
        }
    }

    // Nếu cả 2 nút đều đang tắt -> Báo cáo về RC
    if (!webBtn.checked && !espBtn.checked) {
        currentMode = "RC";
        statusText.textContent = "🎮 Tay cầm RC (Mặc định)";
        statusText.style.color = "#2ecc71";
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
    // Chỉ cập nhật giao diện (Digital Twin) khi KHÔNG KÉO TAY
    if (isDragging) return;

    // Parse: "STATUS:1"
    let msg = event.data;
    if (msg.startsWith("STATUS:")) {
        // Cắt lấy con số phía sau chữ "STATUS:" (Ví dụ "STATUS:1" -> lấy số 1)
        let statusCode = parseInt(msg.substring(7));
        let modeText = "⏸️ STOP";
        
        // Map các số nguyên (Enum MotionType) thành Text để hiển thị
        switch(statusCode) {
            case 0: modeText = "⏸️ STOP"; break;
            case 1: modeText = "⬆️ FORWARD"; break;
            case 2: modeText = "⬇️ BACKWARD"; break;
            case 3: modeText = "↖️ FWD LEFT"; break;
            case 4: modeText = "↗️ FWD RIGHT"; break;
            case 5: modeText = "↙️ BCK LEFT"; break;
            case 6: modeText = "↘️ BCK RIGHT"; break;
            case 7: modeText = "🔄 SPIN LEFT"; break;
            case 8: modeText = "🔄 SPIN RIGHT"; break;
        }
        
        // Cập nhật trạng thái THỰC TẾ của xe lên màn hình
        document.getElementById('modeDisplay').textContent = modeText;
        return; // Đã xử lý xong gói STATUS thì thoát hàm
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
    updateVisuals(currentX, centerY, currentPot);
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
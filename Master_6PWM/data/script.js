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
let webEnabled = false;
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
function toggleWebControl() {
    webEnabled = document.getElementById('webEnableBtn').checked;
    var statusText = document.getElementById('safetyStatus');
    
    if (webEnabled) {
        statusText.innerHTML = "🟢 BẬT";
        statusText.style.color = "#2ecc71";
        if(websocket) websocket.send("CMD_EN:1");
    } else {
        statusText.innerHTML = "🔴 TẮT";
        statusText.style.color = "#e74c3c";
        if(websocket) websocket.send("CMD_EN:0");
        stopDrag(); // Reset về 0
    }
}

function onMessage(event) {
    lastSuccessTime = Date.now();
    // Chỉ cập nhật giao diện (Digital Twin) khi KHÔNG KÉO TAY
    if (isDragging) return;

    // Parse: "X:2048,Y:2048,POT:0,EN:0"
    let msg = event.data;
    if (msg.startsWith("X:")) {
        let parts = msg.split(',');
        
        // 1. Lấy giá trị
        let sX = parseInt(parts[0].split(':')[1]);
        let sY = parseInt(parts[1].split(':')[1]);
        let sPot = parseInt(parts[2].split(':')[1]);
        
        // 2. Đồng bộ trạng thái nút Bấm (nếu có gửi kèm)
        // (Tùy chọn: Nếu muốn web khác cũng thấy nút nhảy theo)
        
        // 3. Cập nhật hiển thị
        updateVisuals(sX, sY, sPot);
    }
}

function updateVisuals(x, y, pot) {
    document.getElementById('xValue').textContent = x;
    document.getElementById('yValue').textContent = y;
    document.getElementById('potValue').textContent = pot;
    
    let percent = Math.round((pot / 4095) * 100);
    document.getElementById('speedValue').textContent = percent + "%";

    // Di chuyển Joystick
    let deltaX = ((x - 2048) / 2047) * maxDistance;
    let deltaY = ((y - 2048) / 2047) * maxDistance;
    joystick.style.transform = `translate(calc(-50% + ${deltaX}px), calc(-50% + ${deltaY}px))`;
    
    // Cập nhật thanh trượt
    speedSlider.value = pot;

    // Cập nhật Mode text
    let mode = "⏸️ NEUTRAL";
    if (y < 1600) mode = "⬆️ FORWARD";
    else if (y > 2500) mode = "⬇️ BACKWARD";
    if (x < 1600) mode += " ⬅️ LEFT";
    else if (x > 2500) mode += " ➡️ RIGHT";
    document.getElementById('modeDisplay').textContent = mode;
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
    if (!webEnabled) return; // Chỉ kéo khi đã bật Web Control
    isDragging = true;
    const rect = joystick.parentElement.getBoundingClientRect();
    centerX = rect.left + rect.width / 2;
    centerY = rect.top + rect.height / 2;
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
    
    joystick.style.transform = `translate(calc(-50% + ${deltaX}px), calc(-50% + ${deltaY}px))`;
    
    // Map từ pixel sang giá trị 0-4095
    // Y đi lên là âm trong HTML, nhưng ta muốn Y<2048 là Tiến (giống tay cầm)
    // X đi phải là dương
    currentX = Math.round(2048 + (deltaX / maxDistance) * 2047);
    currentY = Math.round(2048 + (deltaY / maxDistance) * 2047);
    updateVisuals(currentX, currentY, currentPot); 
}

function stopDrag() {
    isDragging = false;
    joystick.style.transform = 'translate(-50%, -50%)';
    currentX = 2048; // Về giữa
    currentY = 2048; // Về giữa
}

// --- LOGIC SLIDER TỐC ĐỘ ---
function initSpeedSlider() {
    speedSlider.addEventListener('input', function() {
        if (!webEnabled){ this.value = currentPot; return;} // Chỉ thay đổi khi đã bật Web Control
        currentPot = parseInt(this.value);
        updateVisuals(currentX, currentY, currentPot);
    });
}

// --- GỬI DỮ LIỆU ---
function updateValuesAndSend() {
   // Chỉ gửi dữ liệu khi kết nối OK
    if (websocket && websocket.readyState === WebSocket.OPEN) {
        
        // QUAN TRỌNG: Chỉ gửi dữ liệu điều khiển khi ĐANG BẬT CHẾ ĐỘ WEB
        if (webEnabled) {
            let msg = `X:${currentX},Y:${currentY},POT:${currentPot}`;
            websocket.send(msg);
        }
        // Nếu tắt Web, hàm này KHÔNG làm gì cả -> Không ghi đè lên giao diện
    }
}
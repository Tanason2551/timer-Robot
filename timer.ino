#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

// ==========================================
// ⚙️ ตั้งค่า Wi-Fi (แก้ไขชื่อและรหัสผ่านตรงนี้)
// ==========================================
const char* ssid = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";

// ==========================================
// 📌 ตั้งค่าขา (Pins)
// ==========================================
// *** สำคัญ: ต้องใช้ ADC1 (32, 33, 34, 35) เพราะเปิด Wi-Fi แล้ว ADC2 จะใช้ analogRead ไม่ได้ ***
const int ANALOG_PIN = 34;      // เซนเซอร์ (เปลี่ยนจาก 25 เป็น 34)
const int RESET_BTN  = 26;      // ปุ่ม Reset (กดติดปล่อยดับ ต่อลง GND)

// --- ขาจอ MAX7219 ---
const int DATA_IN = 23;  // DIN
const int CLK_PIN = 18;  // CLK
const int CS_PIN  = 5;   // CS/LOAD

// ==========================================
// ⏱️ ตั้งค่าระบบจับเวลา
// ==========================================
const int THRESHOLD  = 2000;    // ค่ากลาง (ปรับตามหน้างาน)

// สถานะการทำงาน
enum State { 
  READY,          // 0: รอเริ่ม
  PASSING_START,  // 1: ช่วงหุ่นยนต์กำลังบังเซนเซอร์อยู่
  RUNNING,        // 2: หุ่นพ้นเซนเซอร์แล้ว รอรับสัญญาณเส้นชัย
  FINISHED        // 3: จบ
};

// ตัวแปรแบบ volatile เพื่อให้ทั้ง 2 Core อ่าน/เขียน ค่าอัปเดตได้ถูกต้อง
volatile State currentState = READY;
volatile unsigned long startTime = 0;
volatile unsigned long finalTime = 0;
volatile unsigned long currentElapsedTime = 0;
volatile bool webResetFlag = false; // รับคำสั่ง Reset จากเว็บ

unsigned long lastDisplayUpdate = 0;

// สร้าง Web Server ที่พอร์ต 80
AsyncWebServer server(80);
// สร้าง Event Source สำหรับส่งข้อมูลแบบ Real-time ไปที่หน้าเว็บ
AsyncEventSource events("/events");

// ==========================================
// 🌐 หน้าเว็บ HTML + CSS + JS (ฝังในตัวแปร)
// ==========================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="th">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Robot Timer Dashboard</title>
  <style>
    body { 
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; 
      background-color: #121212; 
      color: #ffffff; 
      text-align: center; 
      margin: 0; 
      padding: 20px; 
      display: flex; 
      flex-direction: column; 
      align-items: center; 
      justify-content: center; 
      min-height: 100vh; 
    }
    h1 { color: #00ffcc; font-size: 2.5rem; margin-bottom: 10px; }
    .timer-box { 
      background: #1e1e1e; border: 3px solid #333; border-radius: 15px; 
      padding: 40px; margin: 20px; box-shadow: 0 0 20px rgba(0, 255, 204, 0.2); 
      width: 80%; max-width: 800px; 
    }
    #timeDisplay { 
      font-size: 8rem; font-family: monospace; font-weight: bold; 
      color: #ff3366; text-shadow: 0 0 10px rgba(255, 51, 102, 0.5); margin: 0; 
    }
    #statusDisplay { 
      font-size: 2rem; color: #aaaaaa; margin-top: 15px; 
      text-transform: uppercase; letter-spacing: 2px; 
    }
    .btn { 
      background-color: #ff3366; color: white; border: none; padding: 15px 40px; 
      font-size: 1.5rem; border-radius: 8px; cursor: pointer; transition: 0.3s; 
      margin-top: 20px; font-weight: bold; 
    }
    .btn:hover { background-color: #ff0040; box-shadow: 0 0 15px rgba(255, 51, 102, 0.6); }
    .status-READY { color: #00ccff !important; }
    .status-PASSING { color: #ffaa00 !important; }
    .status-RUNNING { color: #00ffcc !important; }
    .status-FINISHED { color: #ff3366 !important; font-weight: bold; }
    
    @media (max-width: 600px) {
      #timeDisplay { font-size: 4.5rem; }
      h1 { font-size: 1.8rem; }
      .timer-box { padding: 20px; width: 90%; }
      .btn { font-size: 1.2rem; padding: 12px 25px; }
      #statusDisplay { font-size: 1.2rem; }
    }
  </style>
</head>
<body>
  <h1>🚀 ROBOT RACE TIMER</h1>
  <div class="timer-box">
    <div id="timeDisplay">00.00.000</div>
    <div id="statusDisplay" class="status-READY">READY (รอเริ่ม)</div>
  </div>
  <button class="btn" onclick="resetTimer()">RESET TIMER</button>

  <script>
    function formatTime(ms) {
      let minutes = Math.floor(ms / 60000);
      let seconds = Math.floor((ms % 60000) / 1000);
      let milliseconds = ms % 1000;
      
      let mStr = minutes.toString().padStart(2, '0');
      let sStr = seconds.toString().padStart(2, '0');
      let msStr = milliseconds.toString().padStart(3, '0');
      
      return `${mStr}.${sStr}.${msStr}`;
    }

    if (!!window.EventSource) {
      var source = new EventSource('/events');
      source.addEventListener('update', function(e) {
        var data = JSON.parse(e.data);
        document.getElementById('timeDisplay').innerText = formatTime(data.ms);
        
        var statusEl = document.getElementById('statusDisplay');
        if (data.state === 0) { statusEl.innerText = "READY (รอเริ่ม)"; statusEl.className = "status-READY"; }
        else if (data.state === 1) { statusEl.innerText = "PASSING START (หุ่นกำลังผ่านเซนเซอร์)"; statusEl.className = "status-PASSING"; }
        else if (data.state === 2) { statusEl.innerText = "RUNNING (หุ่นพ้นเส้นแล้ว กำลังวิ่ง)"; statusEl.className = "status-RUNNING"; }
        else if (data.state === 3) { statusEl.innerText = "FINISHED (เข้าเส้นชัยแล้ว!)"; statusEl.className = "status-FINISHED"; }
      }, false);
    }

    function resetTimer() {
      fetch('/reset').then(response => console.log("Reset sent"));
    }
  </script>
</body>
</html>
)rawliteral";

// ==========================================
// 🛠️ ฟังก์ชันสั่งงานจอ MAX7219
// ==========================================
void sendMax7219(byte address, byte data) {
  digitalWrite(CS_PIN, LOW);
  shiftOut(DATA_IN, CLK_PIN, MSBFIRST, address);
  shiftOut(DATA_IN, CLK_PIN, MSBFIRST, data);
  digitalWrite(CS_PIN, HIGH);
}

void clearDisplay() {
  for (int i = 1; i <= 8; i++) {
    sendMax7219(i, 0x0F);
  }
}

void displayTime(unsigned long timeMillis) {
  int minutes = (timeMillis / 60000) % 100;
  int seconds = (timeMillis / 1000) % 60;
  int msec    = (timeMillis % 1000);

  int m1 = minutes / 10;
  int m2 = minutes % 10;
  int s1 = seconds / 10;
  int s2 = seconds % 10;
  int ms1 = msec / 100;
  int ms2 = (msec / 10) % 10;
  int ms3 = msec % 10;

  sendMax7219(8, m1);          
  sendMax7219(7, m2 | 0x80);   
  sendMax7219(6, s1);          
  sendMax7219(5, s2 | 0x80);   
  sendMax7219(4, ms1);         
  sendMax7219(3, ms2);         
  sendMax7219(2, ms3);         
  sendMax7219(1, 0x0F);        
}

// ==========================================
// 🗂️ Task สำหรับ Core 0 (จัดการ Wi-Fi และส่งข้อมูลให้เว็บ)
// ==========================================
void WebUpdateTask(void * pvParameters) {
  for(;;) {
    // สร้าง JSON String เพื่อส่งไปที่หน้าเว็บ
    String jsonData = "{\"ms\":" + String(currentElapsedTime) + ",\"state\":" + String(currentState) + "}";
    
    // ส่ง Event กลับไปที่ Client ทุกๆ 50ms
    events.send(jsonData.c_str(), "update", millis());
    
    vTaskDelay(50 / portTICK_PERIOD_MS); // หน่วงเวลาให้ Core 0 ได้พัก
  }
}

// ==========================================
// 🚀 SETUP (ทำงานครั้งแรก)
// ==========================================
void setup() {
  Serial.begin(115200);
  pinMode(RESET_BTN, INPUT_PULLUP);
  
  // ตั้งค่าขาจอ
  pinMode(DATA_IN, OUTPUT);
  pinMode(CLK_PIN, OUTPUT);
  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH);

  // เริ่มต้นหน้าจอ
  sendMax7219(0x0F, 0x00); 
  sendMax7219(0x0C, 0x01); 
  sendMax7219(0x0B, 0x07); 
  sendMax7219(0x0A, 0x08); 
  sendMax7219(0x09, 0xFF); 
  
  clearDisplay(); 
  displayTime(0); 

  // --- เริ่มเชื่อมต่อ Wi-Fi ---
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  Serial.print("🌐 เปิดบราวเซอร์ไปที่ IP: ");
  Serial.println(WiFi.localIP());

  // --- ตั้งค่า Web Server ---
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request){
    webResetFlag = true; 
    request->send(200, "text/plain", "OK");
  });

  server.addHandler(&events);
  server.begin();

  // --- สร้าง Task ทำงานบน Core 0 ---
  xTaskCreatePinnedToCore(
    WebUpdateTask,   // ฟังก์ชันที่เรียกใช้
    "WebTask",       // ชื่อ Task
    4096,            // ขนาด Stack
    NULL,            // พารามิเตอร์
    1,               // Priority
    NULL,            // Task handle
    0                // รันบน Core 0
  );

  Serial.println("--- SYSTEM READY (Dual Core + Web) ---");
}

// ==========================================
// ⏱️ MAIN LOOP (ทำงานบน Core 1: จัดการการจับเวลาล้วนๆ)
// ==========================================
void loop() {
  int sensorValue = analogRead(ANALOG_PIN);
  unsigned long currentMillis = millis();

  // 1. ตรวจสอบปุ่ม Reset (จากปุ่มกดจริง หรือ จากหน้าเว็บ)
  if (digitalRead(RESET_BTN) == LOW || webResetFlag) {
    currentState = READY;
    webResetFlag = false; 
    currentElapsedTime = 0;
    
    Serial.println("[RESET]");
    sendMax7219(0x09, 0xFF); 
    clearDisplay();
    displayTime(0);
    
    delay(500); // กันกดรัว
    return;
  }

  // 2. คำนวณเวลาที่ผ่านไป (แก้ไข Error ตรงนี้โดยใช้ PASSING_START แทน BLIND_PHASE)
  if (currentState == PASSING_START || currentState == RUNNING) {
    currentElapsedTime = currentMillis - startTime;
  } else if (currentState == FINISHED) {
    currentElapsedTime = finalTime;
  }

  // 3. State Machine
  switch (currentState) {
    case READY:
      if (sensorValue < THRESHOLD) { // 1. หุ่นตัดเซนเซอร์
        startTime = currentMillis;
        currentState = PASSING_START;
        Serial.println(">>> START! (Passing Sensor...)");
      }
      break;

    case PASSING_START:
      // 2. รอให้หุ่นวิ่งพ้นเซนเซอร์ (ค่ากลับมา >= THRESHOLD) 
      // (แอบใส่ currentElapsedTime > 100ms ไว้กันเซนเซอร์รวนตอนขอบหุ่นพ้นเส้นพอดี)
      if (sensorValue >= THRESHOLD && currentElapsedTime > 100) { 
        currentState = RUNNING;
        Serial.println(">>> Sensor Cleared. Waiting for Finish Line...");
      }
      break;

    case RUNNING:
      if (sensorValue < THRESHOLD) { // 3. หุ่นตัดเซนเซอร์อีกครั้ง (เข้าเส้นชัย)
        finalTime = currentElapsedTime;
        currentState = FINISHED;
        Serial.print("FINISHED: ");
        Serial.println(finalTime);
      }
      break;

    case FINISHED:
      break;
  }

  // 4. อัปเดตหน้าจอ MAX7219 ทุก 30ms
  if (currentMillis - lastDisplayUpdate > 30) {
    lastDisplayUpdate = currentMillis;
    displayTime(currentElapsedTime);
  }
}
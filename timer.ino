// --- ตั้งค่าขา (Pins) ---
const int ANALOG_PIN = 25;      // เซนเซอร์
const int RESET_BTN  = 26;      // ปุ่ม Reset

// --- ขาจอ MAX7219 ---
const int DATA_IN = 23;  // DIN
const int CLK_PIN = 18;  // CLK
const int CS_PIN  = 5;   // CS/LOAD

// *** ตั้งค่าระบบจับเวลา ***
const int THRESHOLD  = 2000;    // ค่ากลาง (ปรับตามหน้างาน)
const int BLIND_TIME = 1000;    // *** ตาบอด 3 วินาที (กันหยุดเอง) ***

// สถานะการทำงาน
enum State { 
  READY,        // 0: รอเริ่ม
  BLIND_PHASE,  // 1: ช่วงตาบอด (เริ่มแล้ว แต่ไม่รับสัญญาณหยุด)
  RUNNING,      // 2: ช่วงจับเส้นชัย (พ้น 3 วิมาแล้ว รอรับสัญญาณหยุด)
  FINISHED      // 3: จบ
};

State currentState = READY;

unsigned long startTime = 0;
unsigned long finalTime = 0;
unsigned long lastDisplayUpdate = 0; // ตัวแปรกันจอกระพริบ

void setup() {
  Serial.begin(115200);
  pinMode(RESET_BTN, INPUT_PULLUP);
  
  // ตั้งค่าขาจอ
  pinMode(DATA_IN, OUTPUT);
  pinMode(CLK_PIN, OUTPUT);
  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH);

  // --- เริ่มต้นการทำงานของจอ MAX7219 (ตามที่คุณเทสผ่าน) ---
  sendMax7219(0x0F, 0x00); // Display Test: Off
  sendMax7219(0x0C, 0x01); // Shutdown: Normal Operation
  sendMax7219(0x0B, 0x07); // Scan Limit: All digits (0-7)
  sendMax7219(0x0A, 0x08); // Intensity: ความสว่างระดับกลาง
  sendMax7219(0x09, 0xFF); // Decode Mode: Code B (สำคัญ! ใช้ตัวถอดรหัสในชิป)
  
  clearDisplay(); // ล้างหน้าจอ
  displayTime(0); // โชว์ 00.00.000
  
  Serial.println("--- SYSTEM READY (Final Fusion) ---");
}

void loop() {
  int sensorValue = analogRead(ANALOG_PIN);
  unsigned long currentMillis = millis();

  // 1. ปุ่ม Reset
  if (digitalRead(RESET_BTN) == LOW) {
    currentState = READY;
    Serial.println("[RESET]");
    
    // รีเซ็ตค่าจอใหม่เผื่อรวน
    sendMax7219(0x09, 0xFF); 
    clearDisplay();
    displayTime(0);
    
    delay(500);
    return;
  }

  // 2. คำนวณเวลาที่ผ่านไป (Elapsed Time)
  unsigned long elapsed = 0;
  if (currentState == BLIND_PHASE || currentState == RUNNING) {
    elapsed = currentMillis - startTime;
  } else if (currentState == FINISHED) {
    elapsed = finalTime;
  }

  // 3. State Machine (ระบบความคิด)
  switch (currentState) {
    
    // --- รอเริ่ม ---
    case READY:
      if (sensorValue < THRESHOLD) { // เริ่มเมื่อค่าตก
        startTime = currentMillis;
        currentState = BLIND_PHASE; // เข้าสู่ช่วงตาบอดทันที
        Serial.println(">>> START! (Sensor Disabled for 3s)");
      }
      break;

    // --- ช่วงตาบอด (Blind Phase) ---
    // ช่วงนี้เราจะไม่เช็ค sensorValue เลย เพื่อกันหยุดเอง
    case BLIND_PHASE:
      if (elapsed > BLIND_TIME) {
        currentState = RUNNING;
        Serial.println(">>> Blind Time Over. Waiting for Finish Line...");
      }
      break;

    // --- ช่วงจับเส้นชัย (Running) ---
    // พ้น 3 วินาทีแล้ว ค่อยกลับมาเช็คเซนเซอร์
    case RUNNING:
      if (sensorValue < THRESHOLD) { 
        finalTime = elapsed;
        currentState = FINISHED;
        Serial.print("FINISHED: ");
        Serial.println(finalTime);
      }
      break;

    // --- จบ ---
    case FINISHED:
      // จบแล้ว รอ Reset อย่างเดียว
      break;
  }

  // 4. *** อัปเดตหน้าจอ (ทำแค่ทุกๆ 30ms เพื่อกันกระพริบ) ***
  if (currentMillis - lastDisplayUpdate > 30) {
    lastDisplayUpdate = currentMillis;
    displayTime(elapsed);
  }
}

// --- ฟังก์ชันสั่งงาน MAX7219 (Driver ที่คุณเทสผ่าน) ---
void sendMax7219(byte address, byte data) {
  digitalWrite(CS_PIN, LOW);
  shiftOut(DATA_IN, CLK_PIN, MSBFIRST, address);
  shiftOut(DATA_IN, CLK_PIN, MSBFIRST, data);
  digitalWrite(CS_PIN, HIGH);
}

void clearDisplay() {
  for (int i = 1; i <= 8; i++) {
    sendMax7219(i, 0x0F); // 0x0F คือค่าว่าง (Blank) ใน Code B
  }
}

void displayTime(unsigned long timeMillis) {
  // คำนวณเวลา
  int minutes = (timeMillis / 60000) % 100;
  int seconds = (timeMillis / 1000) % 60;
  int msec    = (timeMillis % 1000);

  // แยกตัวเลขแต่ละหลัก
  int m1 = minutes / 10;
  int m2 = minutes % 10;
  int s1 = seconds / 10;
  int s2 = seconds % 10;
  int ms1 = msec / 100;
  int ms2 = (msec / 10) % 10;
  int ms3 = msec % 10;

  // ส่งข้อมูลเข้าจอ (Address 8 คือหลักซ้ายสุด, 1 คือขวาสุด)
  // Code B: ถ้าบวก 0x80 (128) เข้าไป จะเป็นการสั่งเปิดจุด (.)
  
  sendMax7219(8, m1);          // นาทีหลักสิบ
  sendMax7219(7, m2 | 0x80);   // นาทีหลักหน่วย + จุด
  sendMax7219(6, s1);          // วินาทีหลักสิบ
  sendMax7219(5, s2 | 0x80);   // วินาทีหลักหน่วย + จุด
  sendMax7219(4, ms1);         // มิลลิวินาที 1
  sendMax7219(3, ms2);         // มิลลิวินาที 2
  sendMax7219(2, ms3);         // มิลลิวินาที 3
  sendMax7219(1, 0x0F);        // หลักสุดท้ายเว้นว่าง
}
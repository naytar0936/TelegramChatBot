/*
 * ============================================================
 *  PM2.5 IoT Detection System - FULL VERSION
 *  ESP32 + PMS5003 + OLED SSD1306 + LED(R/Y/G) + Buzzer
 *  + Blynk Mobile App + Web Dashboard + Telegram Bot
 * ============================================================
 *
 * === REQUIRED LIBRARIES (Arduino IDE Library Manager) ===
 *  - Blynk                  (by Volodymyr Shymanskyy)
 *  - Adafruit SSD1306       (by Adafruit)
 *  - Adafruit GFX Library   (by Adafruit)
 *  - ArduinoJson            (by Benoit Blanchon)
 *  - UniversalTelegramBot   (by Brian Lough)
 *  - ArduinoJson            (v6 — ใช้ตัวเดียวกันได้เลย)
 *
 * === TELEGRAM SETUP ===
 *  1. คุยกับ @BotFather บน Telegram → /newbot → copy TOKEN
 *  2. คุยกับ Bot ของคุณก่อน 1 ครั้ง แล้ว
 *     เปิด https://api.telegram.org/bot<TOKEN>/getUpdates
 *     เพื่อดู chat_id ของคุณ
 *  3. ใส่ TOKEN และ CHAT_ID ด้านล่าง
 *
 *  คำสั่ง Telegram:
 *    /read   → Reading Sensor ทันที + ตอบผลกลับ
 *    /status → ดูค่าล่าสุด (ไม่วัดใหม่)
 *
 * === BOARD SETTINGS ===
 *  Board: "ESP32 Dev Module"
 *  Upload Speed: 921600
 *
 * === PIN CONNECTIONS ===
 *  PMS5003  TX  → GPIO 16 (RX2)
 *  PMS5003  RX  → GPIO 17 (TX2)
 *  PMS5003  VCC → 5V
 *  PMS5003  GND → GND
 *  OLED     SDA → GPIO 21
 *  OLED     SCL → GPIO 22
 *  OLED     VCC → 3.3V / GND → GND
 *  Green LED    → GPIO 25 → 220Ω → GND
 *  Yellow LED   → GPIO 26 → 220Ω → GND
 *  Red LED      → GPIO 27 → 220Ω → GND
 *  Buzzer   +   → GPIO 14 / - → GND
 *  Button   leg → GPIO 32 (other leg → GND)
 *
 * === BLYNK SETUP ===
 *  Blynk Virtual Pins:
 *    V0 = PM2.5 Gauge (0-150)
 *    V1 = Status Label
 *    V2 = Trigger Button (push 1/0)
 *
 * === WEB DASHBOARD ===
 *  เปิดเบราว์เซอร์ไปที่: http://<ESP32_IP>/
 *  JSON API:              http://<ESP32_IP>/data
 * ============================================================
 */

// ========== BLYNK CONFIG ==========
#define BLYNK_TEMPLATE_ID "TMPL6M3QfxasB"
#define BLYNK_TEMPLATE_NAME "PM25 Monitor"
#define BLYNK_AUTH_TOKEN "5AVakuMhX9vJN6Ca2arJX7t8xpJwxUB2"

// ========== LIBRARIES ==========
#include <WiFi.h>
#include <WebServer.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HardwareSerial.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

// ========== TELEGRAM CONFIG ==========
#define BOT_TOKEN   "8604314186:AAFuDkSonmDdKNIOD9FUx9fRrxWYWvVdHt0"   // ← เปลี่ยนตรงนี้
#define CHAT_ID     "8705312352"               // ← เปลี่ยนตรงนี้

WiFiClientSecure telegramClient;
UniversalTelegramBot bot(BOT_TOKEN, telegramClient);
unsigned long lastTelegramCheck = 0;
const unsigned long TELEGRAM_INTERVAL = 2000;  // poll ทุก 2 วินาที

// ========== WiFi ==========
const char* ssid = "IOT";  // ← ใส่ชื่อ WiFi
const char* password = "mfuiot2023";     // ← ใส่ Password

// ========== PINS ==========
#define GREEN_LED 25
#define YELLOW_LED 26
#define RED_LED 27
#define BUZZER_PIN 14
#define BUTTON_PIN 32

// ========== OLED ==========
#define SCREEN_W 128
#define SCREEN_H 64
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);

// ========== PMS5003 UART ==========
HardwareSerial pmsSerial(2);

// ========== Web Server ==========
WebServer server(80);

// ========== GLOBAL STATE ==========
int pm25Value = 0;
unsigned long lastMeasureTime = 0;
unsigned long lastAutoTime = 0;
const unsigned long COOLDOWN = 5000;        // ms ระหว่างการวัด
const unsigned long AUTO_INTERVAL = 10000;  // ms ส่งอัตโนมัติ

// ===========================================================
//  SETUP
// ===========================================================
void setup() {
  Serial.begin(115200);
  pmsSerial.begin(9600, SERIAL_8N1, 16, 17);

  pinMode(GREEN_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Self-test
  selfTest();

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("[ERROR] OLED not found");
    while (true) delay(1000);
  }
  showBoot("Connecting WiFi...");

  // WiFi
  WiFi.begin(ssid, password);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected: " + WiFi.localIP().toString());
  }

  // Web Server Routes
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/refresh", handleRefresh);
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });
  server.begin();

  // Blynk
  Blynk.config(BLYNK_AUTH_TOKEN);
  Blynk.connect(3000);

  // Telegram
  telegramClient.setInsecure();  // ไม่ต้องใช้ certificate

  showBoot("Ready!");
  delay(1000);
  showReady();
}

// ===========================================================
//  LOOP
// ===========================================================
void loop() {
  Blynk.run();
  server.handleClient();

  // Physical button trigger
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50);
    if (digitalRead(BUTTON_PIN) == LOW) {
      if (millis() - lastMeasureTime > COOLDOWN) {
        lastMeasureTime = millis();
        doMeasure();
        while (digitalRead(BUTTON_PIN) == LOW)
          ;  // wait release
      }
    }
  }

  // Telegram polling
  if (millis() - lastTelegramCheck > TELEGRAM_INTERVAL) {
    lastTelegramCheck = millis();
    handleTelegram();
  }

  // Auto read + send every AUTO_INTERVAL
  if (millis() - lastAutoTime > AUTO_INTERVAL) {
    lastAutoTime = millis();
    if (readPMS5003()) {
      sendToBlynk();
    }
  }
}

// ===========================================================
//  BLYNK: Mobile Trigger Button (V2)
// ===========================================================
BLYNK_WRITE(V2) {
  if (param.asInt() == 1) {
    if (millis() - lastMeasureTime > COOLDOWN) {
      lastMeasureTime = millis();
      doMeasure();
    }
  }
}

// ===========================================================
//  MEASURE & OUTPUT
// ===========================================================
void doMeasure() {
  showMeasuring();
  if (readPMS5003()) {
    applyOutputs();
    sendToBlynk();
    Serial.printf("[PM2.5] %d µg/m³\n", pm25Value);
  } else {
    showError();
  }
}

bool readPMS5003() {
  while (pmsSerial.available()) pmsSerial.read();  // flush

  unsigned long t0 = millis();
  while (pmsSerial.available() < 32) {
    if (millis() - t0 > 2000) return false;
    delay(10);
  }

  uint8_t buf[32];
  pmsSerial.readBytes(buf, 32);

  if (buf[0] != 0x42 || buf[1] != 0x4D) return false;

  uint16_t cs = 0;
  for (int i = 0; i < 30; i++) cs += buf[i];
  if (cs != ((buf[30] << 8) | buf[31])) return false;

  pm25Value = (buf[12] << 8) | buf[13];  // PM2.5 atmospheric
  return true;
}

void applyOutputs() {
  ledsOff();
  noTone(BUZZER_PIN);

  String status;
  if (pm25Value <= 25) {
    digitalWrite(GREEN_LED, HIGH);
    status = "GOOD";
  } else if (pm25Value <= 50) {
    digitalWrite(YELLOW_LED, HIGH);
    status = "MEDIUM";
  } else {
    digitalWrite(RED_LED, HIGH);
    // Buzzer pattern: beep x3
    for (int i = 0; i < 3; i++) {
      tone(BUZZER_PIN, 1200, 300);
      delay(450);
    }
    noTone(BUZZER_PIN);
    status = "Badly!!";
  }

  showResult(pm25Value, status);
}

void sendToBlynk() {
  String s = (pm25Value <= 25) ? "GOOD" : (pm25Value <= 50) ? "MEDIUM"
                                                            : "SO BAD!";
  Blynk.virtualWrite(V0, pm25Value);
  Blynk.virtualWrite(V1, s);
  if (pm25Value > 50) {
    Blynk.logEvent("pm_alert", "PM2.5 = " + String(pm25Value) + " µg/m³");
  }
}

// ===========================================================
//  TELEGRAM BOT
// ===========================================================
void handleTelegram() {
  int numMsg = bot.getUpdates(bot.last_message_received + 1);
  for (int i = 0; i < numMsg; i++) {
    String chat_id = bot.messages[i].chat_id;
    String text    = bot.messages[i].text;
    String from    = bot.messages[i].from_name;

    if (text == "/read") {
      bot.sendMessage(chat_id, "⏳ กำลัง Reading Sensor...", "");
      if (millis() - lastMeasureTime > COOLDOWN) {
        lastMeasureTime = millis();
        doMeasure();
      }
      String status = (pm25Value <= 25) ? "✅ ดี (GOOD)" :
                      (pm25Value <= 50) ? "⚠️ ปานกลาง (MEDIUM)" :
                                          "🚨 อันตราย (SO BAD!)";
      String reply = "🌫️ PM2.5 Result\n";
      reply += "━━━━━━━━━━━━━\n";
      reply += "ค่าฝุ่น: " + String(pm25Value) + " µg/m³\n";
      reply += "สถานะ: " + status + "\n";
      reply += "━━━━━━━━━━━━━\n";
      reply += "🕐 " + String(millis()/1000) + "s uptime";
      bot.sendMessage(chat_id, reply, "");

    } else if (text == "/status") {
      String status = (pm25Value <= 25) ? "✅ ดี (GOOD)" :
                      (pm25Value <= 50) ? "⚠️ ปานกลาง (MEDIUM)" :
                                          "🚨 อันตราย (SO BAD!)";
      String reply = "🌫️ PM2.5 ล่าสุด\n";
      reply += "━━━━━━━━━━━━━\n";
      reply += "ค่าฝุ่น: " + String(pm25Value) + " µg/m³\n";
      reply += "สถานะ: " + status + "\n";
      reply += "━━━━━━━━━━━━━\n";
      reply += "💡 พิมพ์ /read เพื่อวัดใหม่";
      bot.sendMessage(chat_id, reply, "");

    } else if (text == "/start") {
      String reply = "👋 สวัสดี " + from + "!\n\n";
      reply += "🤖 PM2.5 Monitor Bot\n\n";
      reply += "คำสั่งที่ใช้ได้:\n";
      reply += "/read   — Reading Sensor ทันที\n";
      reply += "/status — ดูค่าล่าสุด";
      bot.sendMessage(chat_id, reply, "");
    }
  }
}

// ===========================================================
//  WEB SERVER HANDLERS
// ===========================================================
void handleRefresh() {
  if (millis() - lastMeasureTime > COOLDOWN) {
    lastMeasureTime = millis();
    doMeasure();
  }
  StaticJsonDocument<128> doc;
  doc["pm25"] = pm25Value;
  doc["status"] = (pm25Value <= 25) ? "GOOD" : (pm25Value <= 50) ? "MEDIUM"
                                                                 : "Really BAD!!";
  doc["ip"] = WiFi.localIP().toString();
  String out;
  serializeJson(doc, out);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", out);
}

void handleData() {
  StaticJsonDocument<128> doc;
  doc["pm25"] = pm25Value;
  doc["status"] = (pm25Value <= 25) ? "GOOD" : (pm25Value <= 50) ? "MEDIUM"
                                                                 : "Really BAD!!";
  doc["ip"] = WiFi.localIP().toString();
  String out;
  serializeJson(doc, out);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", out);
}

void handleRoot() {
  // ส่ง HTML Dashboard
  String html = F(R"HTML(<!DOCTYPE html>
<html lang="th">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PM2.5 Monitor</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Segoe UI',sans-serif;background:#0f172a;color:#e2e8f0;min-height:100vh;display:flex;flex-direction:column;align-items:center;padding:20px}
h1{font-size:1.6rem;margin-bottom:4px;color:#7dd3fc}
.sub{font-size:.8rem;color:#64748b;margin-bottom:24px}
.card{background:#1e293b;border-radius:16px;padding:28px 36px;width:100%;max-width:420px;text-align:center;margin-bottom:16px;box-shadow:0 4px 20px rgba(0,0,0,.4)}
.lbl{font-size:.85rem;color:#94a3b8;margin-bottom:4px}
.big{font-size:5rem;font-weight:800;line-height:1;transition:color .5s}
.unit{font-size:.9rem;color:#94a3b8;margin-top:4px}
.badge{display:inline-block;padding:6px 22px;border-radius:999px;font-size:1rem;font-weight:700;margin-top:14px;letter-spacing:1px}
.bg-good{background:#14532d;color:#4ade80}
.bg-med{background:#713f12;color:#facc15}
.bg-bad{background:#7f1d1d;color:#f87171}
.bar-wrap{background:#334155;border-radius:8px;height:12px;margin-top:16px;overflow:hidden}
.bar{height:100%;border-radius:8px;transition:width .6s,background .5s}
.row{display:flex;justify-content:space-between;font-size:.75rem;color:#475569;margin-top:3px}
.ts{font-size:.75rem;color:#475569;margin-top:10px}
.leg{display:flex;gap:8px;width:100%;max-width:420px;margin-bottom:16px}
.li{flex:1;background:#1e293b;border-radius:10px;padding:10px;text-align:center;font-size:.75rem}
.dot{width:9px;height:9px;border-radius:50%;display:inline-block;margin-right:3px}
button{background:#3b82f6;color:#fff;border:none;padding:10px 28px;border-radius:8px;font-size:1rem;cursor:pointer;margin-bottom:16px}
button:hover{background:#2563eb}
.hcard{background:#1e293b;border-radius:16px;padding:20px;width:100%;max-width:420px;margin-bottom:16px}
.htitle{font-size:.85rem;color:#94a3b8;margin-bottom:10px}
canvas{width:100%!important}
</style>
</head>
<body>
<h1>🌫️ PM2.5 Monitor</h1>
<p class="sub">ESP32 IoT Air Quality Dashboard</p>
<div class="card">
  <div class="lbl">ค่าฝุ่น PM2.5</div>
  <div class="big" id="val">--</div>
  <div class="unit">µg/m³</div>
  <span class="badge" id="badge">--</span>
  <div class="bar-wrap"><div class="bar" id="bar" style="width:0%"></div></div>
  <div class="row"><span>0</span><span>25</span><span>50</span><span>75+</span></div>
  <div class="ts" id="ts">อัพเดทล่าสุด: --</div>
</div>
<div class="leg">
  <div class="li"><span class="dot" style="background:#4ade80"></span><b>ดี</b><br>≤25</div>
  <div class="li"><span class="dot" style="background:#facc15"></span><b>ปานกลาง</b><br>26–50</div>
  <div class="li"><span class="dot" style="background:#f87171"></span><b>อันตราย</b><br>&gt;50</div>
</div>
<button onclick="fetch('/refresh').then(r=>r.json()).then(updateUI)">🔄 รีเฟรช</button>
<div class="hcard">
  <div class="htitle">📊 ประวัติค่าฝุ่น</div>
  <canvas id="c" height="130"></canvas>
</div>
<script>
const hist=[];
function updateUI(d){
  const v=d.pm25;
  document.getElementById('val').textContent=v;
  const bar=document.getElementById('bar');
  bar.style.width=Math.min(v/75*100,100)+'%';
  const badge=document.getElementById('badge');
  const valEl=document.getElementById('val');
  if(v<=25){badge.className='badge bg-good';badge.textContent='✅ ดี';bar.style.background='#4ade80';valEl.style.color='#4ade80';}
  else if(v<=50){badge.className='badge bg-med';badge.textContent='⚠️ ปานกลาง';bar.style.background='#facc15';valEl.style.color='#facc15';}
  else{badge.className='badge bg-bad';badge.textContent='🚨 อันตราย';bar.style.background='#f87171';valEl.style.color='#f87171';}
  document.getElementById('ts').textContent='อัพเดทล่าสุด: '+new Date().toLocaleTimeString('th-TH');
  hist.push({t:new Date().toLocaleTimeString('th-TH',{hour:'2-digit',minute:'2-digit',second:'2-digit'}),v});
  if(hist.length>20)hist.shift();
  drawChart();
}
function drawChart(){
  const cv=document.getElementById('c');
  const ctx=cv.getContext('2d');
  cv.width=cv.offsetWidth;const W=cv.width,H=130;cv.height=H;
  ctx.clearRect(0,0,W,H);
  if(hist.length<2)return;
  const mx=Math.max(...hist.map(h=>h.v),75);
  const p={t:10,b:28,l:28,r:8};
  const gW=W-p.l-p.r,gH=H-p.t-p.b;
  [25,50].forEach(lim=>{
    const y=p.t+gH-(lim/mx)*gH;
    ctx.strokeStyle=lim===25?'#14532d':'#713f12';
    ctx.lineWidth=1;ctx.setLineDash([4,4]);
    ctx.beginPath();ctx.moveTo(p.l,y);ctx.lineTo(W-p.r,y);ctx.stroke();
    ctx.setLineDash([]);ctx.fillStyle='#475569';ctx.font='9px sans-serif';
    ctx.fillText(lim,2,y+3);
  });
  ctx.beginPath();
  hist.forEach((h,i)=>{
    const x=p.l+(i/(hist.length-1))*gW,y=p.t+gH-(h.v/mx)*gH;
    i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);
  });
  ctx.strokeStyle='#38bdf8';ctx.lineWidth=2;ctx.stroke();
  hist.forEach((h,i)=>{
    const x=p.l+(i/(hist.length-1))*gW,y=p.t+gH-(h.v/mx)*gH;
    ctx.beginPath();ctx.arc(x,y,3,0,Math.PI*2);
    ctx.fillStyle=h.v<=25?'#4ade80':h.v<=50?'#facc15':'#f87171';ctx.fill();
  });
  ctx.fillStyle='#64748b';ctx.font='9px sans-serif';
  ctx.fillText(hist[0].t,p.l,H-6);
  const last=hist[hist.length-1].t;
  ctx.fillText(last,W-p.r-ctx.measureText(last).width,H-6);
}
function poll(){fetch('/data').then(r=>r.json()).then(updateUI).catch(()=>{})}
poll();setInterval(poll,10000);
</script>
</body>
</html>)HTML");
  server.send(200, "text/html", html);
}

// ===========================================================
//  OLED HELPERS
// ===========================================================
void showBoot(const char* msg) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(25, 10);
  display.println("PM2.5 Monitor");
  display.setCursor(10, 30);
  display.println(msg);
  display.display();
}

void showReady() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("System Ready");
  display.setCursor(0, 14);
  display.println("IP:");
  display.setCursor(0, 24);
  display.println(WiFi.localIP().toString());
  display.setCursor(0, 42);
  display.println("Press BTN to measure");
  display.display();
}

void showMeasuring() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 10);
  display.println("Reading");
  display.setCursor(20, 35);
  display.println("Sensor..");
  display.display();
}

void showResult(int val, String status) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("-- PM2.5 Result --");
  display.setTextSize(3);
  display.setCursor(15, 16);
  display.print(val);
  display.setTextSize(1);
  display.setCursor(85, 28);
  display.println("ug/m3");
  display.setTextSize(2);
  display.setCursor(10, 46);
  display.println(status);
  display.display();
}

void showError() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 18);
  display.println("SENSOR");
  display.setCursor(20, 38);
  display.println("ERROR!");
  display.display();
}

// ===========================================================
//  LED HELPERS
// ===========================================================
void ledsOff() {
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(RED_LED, LOW);
}

void selfTest() {
  digitalWrite(GREEN_LED, HIGH);
  delay(300);
  digitalWrite(YELLOW_LED, HIGH);
  delay(300);
  digitalWrite(RED_LED, HIGH);
  delay(300);
  tone(BUZZER_PIN, 1000, 200);
  delay(500);
  ledsOff();
  noTone(BUZZER_PIN);
}

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <EEPROM.h>
#include <Update.h> // ไลบรารีสำหรับระบบการอัปเดตเฟิร์มแวร์ไร้สาย (OTA)

// ================= ตั้งค่าระบบ =================
const char* AP_SSID = "ZhipTech-Smoke-Setup";
const char* AP_PASS = "12345678";

// ตั้งค่า Telegram
#define BOT_TOKEN "8992113068:AAHjS7-S07P6jRl9DDaSjmNwPoOBD-KEgZo"
#define ADMIN_CHAT_ID "6273522437"

// ตั้งค่า Hardware สำหรับ ESP32
const int mqPin = 34;  
const int ledPin = 2;  

// โครงสร้างหน่วยความจำ (EEPROM)
struct DeviceConfig {
  int threshold;
  char customerChatId[20];
  int magic;
};
DeviceConfig config;

WebServer server(80);
DNSServer dnsServer;

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

// ตัวแปรควบคุมการเชื่อมต่อ Wi-Fi
bool shouldConnect = false;
unsigned long pendingConnectTime = 0;
String targetSSID = "";
String targetPass = "";

bool connecting = false;
unsigned long connectStart = 0;
int currentGasValue = 0;
bool alertSent = false;
unsigned long lastReadTime = 0;

// ================= หน้าเว็บหลัก Dashboard =================
const char DASHBOARD_HTML[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="th">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>ZhipTech Dashboard</title>
  <style>
    body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; background: #0b1220; color: #eaf1ff; margin: 0; padding: 20px; display: flex; justify-content: center; }
    .container { background: rgba(255,255,255,0.05); padding: 30px; border-radius: 16px; border: 1px solid rgba(255,255,255,0.1); max-width: 400px; width: 100%; text-align: center; }
    h1 { font-size: 24px; margin: 0 0 5px 0; font-weight: 700; }
    .subtitle { color: #aaa; font-size: 13px; margin-bottom: 25px; line-height: 1.4; }
    .status-card { padding: 35px 20px; border-radius: 16px; margin-bottom: 25px; transition: 0.3s; }
    .safe { background: rgba(46, 125, 50, 0.15); color: #81c784; border: 1px solid rgba(76, 175, 80, 0.3); }
    .danger { background: rgba(198, 40, 40, 0.15); color: #e57373; border: 1px solid rgba(244, 67, 54, 0.3); }
    .val-text { font-size: 72px; font-weight: 700; margin: 10px 0; line-height: 1; }
    .setting-group { text-align: left; margin-bottom: 15px; }
    .input-wrap { min-height: 48px; }
    label { font-size: 13px; font-weight: 600; color: #ccc; display: block; margin-bottom: 8px; }
    input { width: 100%; padding: 14px; border-radius: 10px; border: 1px solid rgba(255,255,255,0.2); background: rgba(0,0,0,0.3); color: #fff; box-sizing: border-box; font-size: 15px; transition: 0.2s; }
    input:focus { border-color: #5b8cff; outline: none; }
    .hint { font-size: 11px; color: #888; margin-top: 6px; line-height: 1.4; }
    button { width: 100%; background: #5b8cff; color: #000; border: none; padding: 14px; border-radius: 10px; font-size: 15px; font-weight: 700; cursor: pointer; margin-top: 10px; transition: 0.2s; }
    button:hover { opacity: 0.9; }
    .nav-link { display: inline-block; margin-top: 20px; color: #5b8cff; font-size: 13px; text-decoration: none; opacity: 0.8; }
    .nav-link:hover { opacity: 1; text-decoration: underline; }
  </style>
</head>
<body>
  <div class="container">
    <h1>ZhipTech</h1>
    <div class="subtitle">ระบบบริหารจัดการและแผงควบคุมตรวจสอบความหนาแน่นของก๊าซและกลุ่มควัน</div>
    
    <div id="statusBox" class="status-card safe">
      <div id="statusText" style="font-weight:600; font-size: 16px;">สภาวะแวดล้อมปกติ : ปลอดภัย</div>
      <div id="valText" class="val-text">0</div>
      <div style="font-size: 12px; opacity: 0.7;">ค่าความเข้มข้นปัจจุบันจากเซนเซอร์ตรวจวัด</div>
    </div>

    <div class="setting-group">
      <label>เกณฑ์การตรวจวัดสูงสุด (Alarm Threshold)</label>
      <div id="wrapThresh" class="input-wrap"></div> 
    </div>
    <div class="setting-group">
      <label>รหัสประจำตัวผู้รับการแจ้งเตือน (Telegram Chat ID)</label>
      <div id="wrapChat" class="input-wrap"></div>
      <div class="hint">* โปรดตรวจสอบหมายเลขส่วนบุคคลผ่านแอปพลิเคชัน Telegram บอท: <b>@ZhipTech_Bot</b></div>
    </div>
    <button onclick="saveConfig()">บันทึกการตั้งค่าระบบ</button>
    
    <a href="/update" class="nav-link">🔄 การอัปเดตเฟิร์มแวร์ (Firmware Update)</a>
  </div>

  <script>
    setTimeout(async () => {
      try {
        let r = await fetch('/get_config');
        let d = await r.json();
        document.getElementById('wrapThresh').innerHTML = '<input type="number" inputmode="numeric" id="inputThresh" value="' + d.thr + '">';
        document.getElementById('wrapChat').innerHTML = '<input type="text" id="inputChatId" value="' + d.chatId + '" placeholder="ระบุตัวเลขรหัสประจำตัว">';
      } catch(e) {}
    }, 400);

    async function fetchSensor() {
      try {
        let res = await fetch('/get_sensor');
        let data = await res.json();
        document.getElementById('valText').innerText = data.val;
        let box = document.getElementById('statusBox');
        let text = document.getElementById('statusText');
        
        let inputEl = document.getElementById('inputThresh');
        let currentThresh = inputEl ? parseInt(inputEl.value || 1500) : 1500;
        if(data.val > currentThresh) {
           box.className = 'status-card danger';
           text.innerText = '⚠️ คำเตือน : ตรวจพบค่าความเข้มข้นสูงเกินมาตรฐานกำหนด';
        } else {
           box.className = 'status-card safe';
           text.innerText = 'สภาวะแวดล้อมปกติ : ปลอดภัย';
        }
      } catch(e) {}
      setTimeout(fetchSensor, 1500);
    }
    
    setTimeout(fetchSensor, 500);

    async function saveConfig() {
      let thr = document.getElementById('inputThresh').value;
      let chat = document.getElementById('inputChatId').value;
      await fetch('/set_config?thr=' + thr + '&chat=' + chat);
      alert('บันทึกการปรับปรุงข้อมูลโครงสร้างระบบเรียบร้อยแล้ว');
    }
  </script>
</body>
</html>
)=====";

// ================= หน้าเว็บส่วนบริการการอัปเดตเฟิร์มแวร์ไร้สาย (UI/UX ปรับปรุงใหม่แบบมี Progress Bar) =================
const char UPDATE_HTML[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="th">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Firmware Update</title>
  <style>
    body { font-family: -apple-system, sans-serif; background: #0b1220; color: #eaf1ff; display: flex; justify-content: center; align-items: center; min-height: 100vh; margin: 0; padding: 20px; box-sizing: border-box; }
    .card { background: rgba(255,255,255,0.05); padding: 35px 30px; border-radius: 16px; border: 1px solid rgba(255,255,255,0.1); width: 100%; max-width: 420px; text-align: center; box-shadow: 0 10px 30px rgba(0,0,0,0.4); }
    h2 { margin-top: 0; font-size: 22px; font-weight: 700; color: #fff; }
    p { color: #aaa; font-size: 13px; text-align: left; line-height: 1.5; margin-bottom: 25px; }
    
    .file-area { position: relative; margin: 20px 0; }
    input[type=file] { width: 100%; padding: 16px; border-radius: 10px; border: 1px dashed rgba(255,255,255,0.3); background: rgba(0,0,0,0.2); color: #fff; box-sizing: border-box; font-size: 14px; cursor: pointer; transition: 0.2s; }
    input[type=file]:hover { border-color: #5b8cff; background: rgba(91,140,255,0.05); }
    
    button { width: 100%; background: #5b8cff; color: #000; border: none; padding: 14px; border-radius: 10px; font-weight: 700; cursor: pointer; font-size: 15px; transition: 0.2s; }
    button:hover:not(:disabled) { opacity: 0.9; transform: translateY(-1px); }
    button:disabled { background: #334155; color: #64748b; cursor: not-allowed; }
    
    /* Progress Bar Styles */
    .progress-container { display: none; width: 100%; background: rgba(255,255,255,0.1); border-radius: 8px; height: 16px; margin: 25px 0 10px 0; overflow: hidden; position: relative; }
    .progress-bar { width: 0%; height: 100%; background: linear-gradient(90deg, #5b8cff, #3b82f6); border-radius: 8px; transition: width 0.1s linear; }
    
    #status-txt { margin-top: 15px; font-weight: 600; color: #5b8cff; font-size: 14px; min-height: 20px; }
    .back-btn { display: inline-block; margin-top: 25px; color: #888; font-size: 13px; text-decoration: none; transition: 0.2s; }
    .back-btn:hover { color: #fff; text-decoration: underline; }
  </style>
</head>
<body>
  <div class="card">
    <h2>การอัปเดตเฟิร์มแวร์ (Firmware Update)</h2>
    <p>โปรดเลือกไฟล์นามสกุล <b>.bin</b> ที่ผ่านการคอมไพล์ซอฟต์แวร์ระบบเรียบร้อยแล้ว เพื่อดำเนินการติดตั้งเฟิร์มแวร์ชุดใหม่ลงบนอุปกรณ์ในรูปแบบไร้สาย (OTA)</p>
    
    <form id="upload-form">
      <div class="file-area">
        <input type="file" id="file-input" name="update" accept=".bin" required>
      </div>
      <button type="submit" id="submit-btn" disabled>เริ่มต้นกระบวนการอัปเดต</button>
    </form>

    <div class="progress-container" id="prog-container">
      <div class="progress-bar" id="prog-bar"></div>
    </div>
    
    <div id="status-txt">สถานะ: รอการเลือกไฟล์ระบบ...</div>
    <a href="/app" id="back-link" class="back-btn">ย้อนกลับสู่หน้าควบคุมหลัก</a>
  </div>

  <script>
    const fileInput = document.getElementById('file-input');
    const submitBtn = document.getElementById('submit-btn');
    const progContainer = document.getElementById('prog-container');
    const progBar = document.getElementById('prog-bar');
    const statusTxt = document.getElementById('status-txt');
    const backLink = document.getElementById('back-link');

    // เปิดใช้งานปุ่มกดยืนยันเมื่อเลือกไฟล์เสร็จสิ้น
    fileInput.addEventListener('change', () => {
      if (fileInput.files.length > 0) {
        submitBtn.disabled = false;
        statusTxt.innerText = 'สถานะ: พร้อมดำเนินการตรวจสอบไฟล์';
      } else {
        submitBtn.disabled = true;
      }
    });

    // ควบคุมระบบการอัพโหลดด้วย AJAX เพื่อคำนวณร้อยละ 0-100%
    document.getElementById('upload-form').addEventListener('submit', (e) => {
      e.preventDefault();
      
      submitBtn.disabled = true;
      fileInput.disabled = true;
      backLink.style.display = 'none'; // ซ่อนลิงก์ย้อนกลับป้องกันระบบรวน
      progContainer.style.display = 'block';
      
      const formData = new FormData();
      formData.append('update', fileInput.files[0]);
      
      const xhr = new XMLHttpRequest();
      xhr.open('POST', '/update_exec', true);
      
      // ฟังก์ชันคำนวณหลอดร้อยละสถานะ
      xhr.upload.addEventListener('progress', (evt) => {
        if (evt.lengthComputable) {
          const percentComplete = Math.round((evt.loaded / evt.total) * 100);
          progBar.style.width = percentComplete + '%';
          statusTxt.innerHTML = 'กำลังอัปโหลดเฟิร์มแวร์ไปยังอุปกรณ์... ' + percentComplete + '%';
        }
      });
      
      // เมื่อการส่งข้อมูลเสร็จสมบูรณ์
      xhr.onload = function() {
        if (xhr.status === 200) {
          progBar.style.background = '#10b981'; // เปลี่ยนสีหลอดเป็นสีเขียวเมื่อสำเร็จ
          statusTxt.innerHTML = '✅ ติดตั้งสำเร็จ! อุปกรณ์กำลังรีบูตระบบใหม่...';
          
          // ระบบนับถอยหลังรีเฟรชตัวเองกลับไปหน้าแรกสุด (เสมือนเพิ่งเปิดเครื่องใหม่)
          let countdown = 5;
          const interval = setInterval(() => {
            statusTxt.innerHTML = '✅ ติดตั้งสำเร็จ! กำลังกลับสู่หน้าแรกใน ' + countdown + ' วินาที...';
            countdown--;
            if(countdown < 0) {
              clearInterval(interval);
              window.location.href = '/'; // บังคับรีเฟรชกลับหน้าแรกสุด
            }
          }, 1000);
        } else {
          progBar.style.background = '#ef4444'; // เปลี่ยนสีหลอดเป็นสีแดงเมื่อล้มเหลว
          statusTxt.innerHTML = '❌ การอัปเดตล้มเหลว กรุณารีเฟรชหน้าเว็บและลองอีกครั้ง';
          backLink.style.display = 'inline-block';
        }
      };
      
      xhr.send(formData);
    });
  </script>
</body>
</html>
)=====";

// ================= ฟังก์ชันพื้นฐาน =================
void loadConfig() {
  EEPROM.begin(sizeof(DeviceConfig));
  EEPROM.get(0, config);
  if (config.magic != 12345) {
    config.threshold = 1500; 
    strcpy(config.customerChatId, "");
    config.magic = 12345;
    EEPROM.put(0, config);
    EEPROM.commit();
  }
}

void saveConfigToEEPROM() {
  EEPROM.put(0, config);
  EEPROM.commit();
}

void sendDualAlert(String message) {
  if (String(BOT_TOKEN) == "ใส่_BOT_TOKEN_ที่นี่") return;
  if (String(ADMIN_CHAT_ID) != "ใส่_CHAT_ID_ของคุณที่นี่") {
    bot.sendMessage(ADMIN_CHAT_ID, "🚨 *[รายงานอุบัติการณ์ - ZhipTech System]*\n\nระบบตรวจพบปริมาณควันที่อาจเป็นอันตรายเกินกว่าเกณฑ์ที่กำหนดไว้\n" + message, "");
  }
  if (strlen(config.customerChatId) > 0) {
    bot.sendMessage(String(config.customerChatId), "🚨 *[รายงานอุบัติการณ์ - ZhipTech System]*\n\nระบบตรวจพบปริมาณควันที่อาจเป็นอันตรายเกินกว่าเกณฑ์ที่กำหนดไว้\n" + message, "");
  }
}

String scanNetworksHTML() {
  int n = WiFi.scanNetworks();
  String opt = "";
  if (n <= 0) return "<option value=''>ไม่พบสัญญาณเครือข่ายไร้สาย</option>";
  for (int i = 0; i < n; i++) { 
    opt += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + "</option>";
  }
  return opt;
}

// ================= Web Handlers =================
void handleRoot() {
  String html = R"=====(
    <!DOCTYPE html><html lang="th"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>Setup ZhipTech</title><style>
    body { font-family: -apple-system, sans-serif; background: #0b1220; color: #eaf1ff; display: flex; justify-content: center; padding: 20px; }
    .card { background: rgba(255,255,255,0.05); padding: 30px; border-radius: 16px; border: 1px solid rgba(255,255,255,0.1); width: 100%; max-width: 350px; }
    select, input, button { width: 100%; padding: 14px; margin: 8px 0 16px 0; border-radius: 10px; border: 1px solid rgba(255,255,255,0.2); background: rgba(0,0,0,0.3); color: #fff; box-sizing: border-box; font-size: 15px; }
    select option { background: #0b1220; color: #fff; }
    button { background: #5b8cff; color: #000; border: none; font-weight: 700; cursor: pointer; transition: 0.2s; }
    button:hover { opacity: 0.9; }
    .input-wrap { min-height: 48px; }
    </style></head><body>
    <div class="card">
      <h2 style="margin-top:0; font-size:20px;">การลงทะเบียนสถานีเครือข่าย</h2>
      <p style="color:#aaa; font-size:13px; margin-top:-10px; line-height:1.4;">โปรดเลือกจุดบริการเข้าถึงสัญญาณไร้สายที่ต้องการ เพื่อเปิดใช้งานระบบเชื่อมโยงข้อมูลสารสนเทศส่วนภูมิภาค</p>
      <form action="/save" method="POST">
        <label style="font-size:13px; color:#ccc; font-weight:600;">เลือกเครือข่าย Wi-Fi</label>
        <select name="ssid" required>)====="
                + scanNetworksHTML() + R"=====(</select>
        <label style="font-size:13px; color:#ccc; font-weight:600;">รหัสผ่านข้อมูลส่วนบุคคล</label>
        <div id="pwdWrap" class="input-wrap"></div>
        <button type="submit">เริ่มการติดตั้งโครงสร้างระบบ</button>
      </form>
    </div>
    <script>
      setTimeout(() => {
        document.getElementById('pwdWrap').innerHTML = '<input type="password" name="pass" placeholder="เว้นว่างในกรณีที่เป็นสัญญานสาธารณะ">';
      }, 400);
    </script>
    </body></html>
  )=====";
  server.send(200, "text/html", html);
}

void handleSave() {
  targetSSID = server.arg("ssid");
  targetPass = server.arg("pass");
  if (targetSSID == "") return server.send(400, "text/plain", "Missing SSID");

  String html = R"=====(
    <!DOCTYPE html><html lang="th"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>Connecting...</title><style>
    body{font-family:-apple-system,sans-serif;text-align:center;padding:40px 20px; background:#0b1220; color:#eaf1ff; margin:0;}
    .card { background: rgba(255,255,255,0.05); padding: 30px; border-radius: 16px; border: 1px solid rgba(255,255,255,0.1); max-width: 350px; margin: 0 auto; box-shadow: 0 10px 30px rgba(0,0,0,0.5); }
    h2 { margin-top: 0; font-size: 18px; color: #fff; }
    .status-box { background: rgba(0,0,0,0.3); border: 1px solid rgba(255,255,255,0.1); padding: 15px; border-radius: 12px; margin: 20px 0; font-size: 15px; font-weight: 600; color: #5b8cff; }
    p { color:#aaa; font-size: 12px; line-height: 1.5; margin-bottom: 0; }
    </style></head>
    <body>
      <div class="card">
        <h2>ระบบกำลังดำเนินการตรวจสอบสิทธิ์เครือข่าย</h2>
        <div id="st" class="status-box">⏳ กำลังตรวจสอบขั้นตอนความปลอดภัยไร้สาย...</div>
        <p>ระบบจะนำท่านเข้าสู่ส่วนงานแผงควบคุมหลังเสร็จสิ้นกระบวนการพิสูจน์ทราบ</p>
      </div>
      <script>
        setInterval(async () => {
          try {
            let r = await fetch('/status');
            let j = await r.json();
            document.getElementById('st').innerHTML = j.msg;
            if(j.ok) { setTimeout(() => location.href = '/app', 1000); }
          } catch(e) {}
        }, 1000);
      </script>
    </body></html>
  )=====";
  server.send(200, "text/html", html);

  shouldConnect = true;
  pendingConnectTime = millis();
  connectStart = millis(); 
  connecting = true;
}

void handleStatus() {
  bool ok = (WiFi.status() == WL_CONNECTED);
  String msg;

  if (ok) {
    msg = "✅ การเชื่อมต่อระบบเครือข่ายเสร็จสิ้นสมบูรณ์";
  } else if (connecting || shouldConnect) {
    unsigned long elapsed = millis() - connectStart;
    if (elapsed > 20000) {
      connecting = false;
      shouldConnect = false;
      msg = "<span style='color:#e57373;'>❌ การเชื่อมต่อล้มเหลว (เกินระยะเวลาที่กำหนด)</span><br><span style='font-size:12px; color:#aaa;'>โปรดตรวจสอบความถูกต้องของรหัสผ่านส่วนบุคคลอีกครั้ง</span>";
    } else {
      msg = "⏳ อยู่ระหว่างทำข้อตกลงการเชื่อมต่อสัญญาน... (" + String(elapsed / 1000) + " วินาที)";
    }
  } else {
    msg = "⏳ รอรับการสั่งการขั้นตอนถัดไป...";
  }

  String json = "{\"ok\":" + String(ok ? "true" : "false") + ",\"msg\":\"" + msg + "\"}";
  server.send(200, "application/json; charset=utf-8", json);
}

void handleApp() {
  server.send_P(200, "text/html", DASHBOARD_HTML);
}
void handleGetSensor() {
  server.send(200, "application/json", "{\"val\":" + String(currentGasValue) + "}");
}
void handleGetConfig() {
  server.send(200, "application/json", "{\"thr\":" + String(config.threshold) + ",\"chatId\":\"" + String(config.customerChatId) + "\"}");
}
void handleSetConfig() {
  if (server.hasArg("thr")) config.threshold = server.arg("thr").toInt();
  if (server.hasArg("chat")) {
    String chatStr = server.arg("chat");
    chatStr.toCharArray(config.customerChatId, sizeof(config.customerChatId));
  }
  saveConfigToEEPROM();
  server.send(200, "text/plain", "OK");
}

void handleCaptivePortalRequest() {
  server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "Redirecting...");
}

// ================= Setup & Loop =================
void setup() {
  Serial.begin(115200);
  loadConfig();
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  secured_client.setInsecure(); 

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  dnsServer.start(53, "*", WiFi.softAPIP());

  // Web Routes หลัก
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/app", HTTP_GET, handleApp);
  server.on("/get_sensor", HTTP_GET, handleGetSensor);
  server.on("/get_config", HTTP_GET, handleGetConfig);
  server.on("/set_config", HTTP_GET, handleSetConfig);
  
  // เรียกแสดงหน้าอัปเดตเฟิร์มแวร์ (หน้าตาหรูหราตัวใหม่)
  server.on("/update", HTTP_GET, []() {
    server.send_P(200, "text/html", UPDATE_HTML);
  });

  // ส่วนงานประมวลผลรับส่งข้อมูลแบบสตรีมไฟล์เพื่อนำไปคำนวณ Progress % บนหน้าเว็บ
  server.on("/update_exec", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    delay(1500);
    ESP.restart(); // รีสตาร์ทฮาร์ดแวร์เพื่อรับโค้ดเวอร์ชันใหม่
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Update Firmware: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { 
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { 
        Serial.printf("Update Success: %u bytes\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });
  
  // ดักสัญญาณระบบเพื่อสร้าง Captive Portal
  server.on("/generate_204", handleCaptivePortalRequest);
  server.on("/hotspot-detect.html", handleCaptivePortalRequest);
  server.on("/fwlink", handleCaptivePortalRequest);
  server.on("/ncsi.txt", handleCaptivePortalRequest);
  
  server.onNotFound([]() {
    String host = server.hostHeader();
    if (host != WiFi.softAPIP().toString() && host != "192.168.4.1") {
      handleCaptivePortalRequest();
    } else {
      server.send(404, "text/plain", "404 Not Found");
    }
  });
  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  if (shouldConnect && millis() - pendingConnectTime > 1000) {
    WiFi.begin(targetSSID.c_str(), targetPass.c_str());
    shouldConnect = false;
  }

  if (millis() - lastReadTime > 500) {
    lastReadTime = millis();
    currentGasValue = analogRead(mqPin);

    if (currentGasValue > config.threshold) {
      digitalWrite(ledPin, HIGH);
      if (!alertSent && WiFi.status() == WL_CONNECTED) {
        sendDualAlert("ความหนาแน่นของควันที่ตรวจวัดได้ในขณะนี้: " + String(currentGasValue) + " (เกณฑ์มาตรฐานที่ยอมรับได้: " + String(config.threshold) + ")\n\n📌 คำแนะนำ: โปรดตรวจสอบความปลอดภัยในพื้นที่ติดตั้งโดยด่วน");
        alertSent = true;
      }
    } else {
      digitalWrite(ledPin, LOW);
      if (alertSent && currentGasValue < (config.threshold - 20)) {
        alertSent = false;
      }
    }
  }
}
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>

const char* AP_SSID = "GasRobot-01";
const char* AP_PASSWORD = "gasrobot123";

constexpr int PIN_PWMA = 4;
constexpr int PIN_AIN1 = 5;
constexpr int PIN_AIN2 = 6;
constexpr int PIN_STBY = 7;
constexpr int PIN_BIN1 = 8;
constexpr int PIN_BIN2 = 9;
constexpr int PIN_PWMB = 10;
constexpr int PIN_BUZZER = 14;

constexpr int PIN_I2C_SDA = 12;
constexpr int PIN_I2C_SCL = 13;

constexpr int PIN_CH4_TX = 17;
constexpr int PIN_CH4_RX = 18;
constexpr int PIN_CO_TX = 15;
constexpr int PIN_CO_RX = 16;

constexpr int PWM_FREQ = 18000;
constexpr int PWM_RES = 8;
constexpr uint8_t URM09_ADDR = 0x11;
constexpr uint8_t URM09_DISTANCE_H_REG = 0x03;
constexpr uint8_t URM09_CONFIG_REG = 0x07;
constexpr uint8_t URM09_COMMAND_REG = 0x08;
constexpr uint8_t URM09_RANGE_500 = 0x30;
constexpr uint8_t URM09_MODE_PASSIVE = 0x00;
constexpr uint8_t URM09_CMD_READ_ONCE = 0x01;

constexpr float CH4_ALARM_PPM = 3000.0f;
constexpr float CO_ALARM_PPM = 35.0f;
constexpr float BLOCK_DISTANCE_CM = 25.0f;
constexpr unsigned long SENSOR_PREHEAT_MS = 180000UL;

constexpr unsigned long SENSOR_INTERVAL_MS = 500UL;
constexpr unsigned long CONTROL_INTERVAL_MS = 80UL;

HardwareSerial methaneSerial(1);
HardwareSerial coSerial(2);
Adafruit_SHT31 sht31 = Adafruit_SHT31();
WebServer server(80);

struct Telemetry {
  float ch4Ppm = 0.0f;
  float coPpm = 0.0f;
  float temperatureC = 0.0f;
  float humidityRh = 0.0f;
  float distanceCm = -1.0f;
  bool methaneFault = false;
  bool coFault = false;
  bool alarm = false;
  bool preheating = true;
  unsigned long uptimeS = 0;
  String state = "boot";
} telemetry;

struct MotionCommand {
  String mode = "stop";
  int speed = 170;
  int turn = 140;
  bool buzzer = false;
} commandState;

unsigned long bootMs = 0;
unsigned long lastSensorMs = 0;
unsigned long lastControlMs = 0;

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Gas Robot Local Console</title>
  <style>
    :root {
      --bg: #f8f4ec;
      --card: #fffdf8;
      --line: #e5d8c5;
      --text: #2f2a24;
      --muted: #7c6d5a;
      --accent: #c76d3a;
      --danger: #b63b47;
      --safe: #2d7d5f;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
      background:
        radial-gradient(circle at top left, #fff4df 0%, transparent 30%),
        radial-gradient(circle at bottom right, #ffe6d8 0%, transparent 30%),
        var(--bg);
      color: var(--text);
    }
    .wrap {
      max-width: 960px;
      margin: 0 auto;
      padding: 24px;
    }
    .card {
      background: var(--card);
      border: 1px solid var(--line);
      border-radius: 20px;
      padding: 20px;
      box-shadow: 0 12px 28px rgba(71, 47, 20, 0.08);
      margin-bottom: 18px;
    }
    h1 {
      margin: 0 0 8px;
      font-size: 30px;
    }
    p {
      margin: 0;
      color: var(--muted);
      line-height: 1.6;
    }
    .stats {
      display: grid;
      grid-template-columns: repeat(2, 1fr);
      gap: 12px;
      margin-top: 18px;
    }
    .item {
      border: 1px solid var(--line);
      border-radius: 16px;
      padding: 14px;
      background: #fffaf3;
    }
    .item .k {
      font-size: 12px;
      color: var(--muted);
      margin-bottom: 6px;
    }
    .item .v {
      font-size: 24px;
      font-weight: 700;
    }
    .pad {
      display: grid;
      grid-template-columns: repeat(3, 1fr);
      gap: 10px;
      margin-top: 16px;
    }
    button {
      border: none;
      border-radius: 15px;
      padding: 14px 12px;
      font-size: 15px;
      font-weight: 700;
      color: white;
      background: var(--accent);
      cursor: pointer;
    }
    button.danger { background: var(--danger); }
    button.safe { background: var(--safe); }
    button.gray { background: #7f8a96; }
    input[type="range"] {
      width: 100%;
      margin-top: 8px;
    }
    .status {
      margin-top: 14px;
      border-radius: 14px;
      padding: 12px 14px;
      font-weight: 700;
      background: #edf7f1;
      color: #235542;
    }
    .status.alarm {
      background: #feeef1;
      color: #8e2231;
    }
    .tiny {
      margin-top: 10px;
      color: var(--muted);
      font-size: 12px;
    }
  </style>
</head>
<body>
  <div class="wrap">
    <section class="card">
      <h1>近程控制版</h1>
      <p>手机或电脑连接小车热点后，直接在本地页面查看浓度、温湿度、避障距离，并下发移动指令。</p>
      <div class="stats">
        <div class="item"><div class="k">甲烷</div><div class="v" id="ch4">0 ppm</div></div>
        <div class="item"><div class="k">CO</div><div class="v" id="co">0 ppm</div></div>
        <div class="item"><div class="k">温度</div><div class="v" id="temp">0 ℃</div></div>
        <div class="item"><div class="k">湿度</div><div class="v" id="hum">0 %RH</div></div>
        <div class="item"><div class="k">前方距离</div><div class="v" id="dist">- cm</div></div>
        <div class="item"><div class="k">状态</div><div class="v" id="state">boot</div></div>
      </div>
      <div class="status" id="statusBox">等待预热完成...</div>
      <div class="tiny" id="metaLine">热点地址：192.168.4.1</div>
    </section>

    <section class="card">
      <label for="speed">速度 PWM：<span id="speedText">170</span></label>
      <input id="speed" type="range" min="80" max="255" value="170">

      <div class="pad">
        <button class="gray" onclick="sendMode('left')">左转</button>
        <button onclick="sendMode('forward')">前进</button>
        <button class="gray" onclick="sendMode('right')">右转</button>
        <button class="gray" onclick="sendMode('backward')">后退</button>
        <button class="danger" onclick="sendMode('stop')">停止</button>
        <button class="safe" onclick="sendMode('auto')">自动巡航</button>
      </div>

      <div class="pad" style="margin-top:10px;">
        <button onclick="sendBuzzer(true)">鸣笛</button>
        <button class="gray" onclick="sendBuzzer(false)">关蜂鸣器</button>
        <button class="safe" onclick="refreshData()">刷新</button>
      </div>
    </section>
  </div>

  <script>
    const speed = document.getElementById("speed");
    const speedText = document.getElementById("speedText");
    speed.addEventListener("input", () => speedText.textContent = speed.value);

    async function postCommand(payload) {
      payload.speed = Number(speed.value);
      payload.turn = 140;
      await fetch("/api/command", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload)
      });
    }

    function sendMode(mode) {
      postCommand({ mode });
    }

    function sendBuzzer(on) {
      postCommand({ buzzer: on });
    }

    async function refreshData() {
      const res = await fetch("/api/telemetry");
      const data = await res.json();
      document.getElementById("ch4").textContent = `${Number(data.ch4_ppm || 0).toFixed(0)} ppm`;
      document.getElementById("co").textContent = `${Number(data.co_ppm || 0).toFixed(1)} ppm`;
      document.getElementById("temp").textContent = `${Number(data.temperature_c || 0).toFixed(1)} ℃`;
      document.getElementById("hum").textContent = `${Number(data.humidity_rh || 0).toFixed(1)} %RH`;
      document.getElementById("dist").textContent = data.distance_cm >= 0 ? `${Number(data.distance_cm).toFixed(0)} cm` : "- cm";
      document.getElementById("state").textContent = data.state || "unknown";

      const statusBox = document.getElementById("statusBox");
      if (data.alarm) {
        statusBox.className = "status alarm";
        statusBox.textContent = "报警中：已停机，请检查环境";
      } else if (data.preheating) {
        statusBox.className = "status";
        statusBox.textContent = "传感器预热中：暂不执行移动命令";
      } else {
        statusBox.className = "status";
        statusBox.textContent = "运行正常：当前为近程热点控制";
      }

      document.getElementById("metaLine").textContent =
        `热点地址：192.168.4.1  |  运行时长：${data.uptime_s ?? 0}s  |  最后状态：${data.state || "-"}`;
    }

    setInterval(refreshData, 1000);
    refreshData();
  </script>
</body>
</html>
)HTML";

void initMotors();
void setMotor(bool leftForward, int leftPwm, bool rightForward, int rightPwm);
void stopCar();
void setMotion(const String& mode, int speed, int turn);
void updateSensors();
void updateAlarmState();
void updateBuzzer();
void applyCommand();
bool readSerialFrame(HardwareSerial& port, uint8_t expectedGasType, uint8_t* frame);
uint8_t calcChecksum(const uint8_t* frame);
bool readMethane(float& ppm, bool& fault);
bool readCO(float& ppm);
void initURM09();
bool readURM09Distance(float& distanceCm);
bool isPreheating();

void handleIndex() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void handleTelemetry() {
  StaticJsonDocument<384> doc;
  doc["ch4_ppm"] = telemetry.ch4Ppm;
  doc["co_ppm"] = telemetry.coPpm;
  doc["temperature_c"] = telemetry.temperatureC;
  doc["humidity_rh"] = telemetry.humidityRh;
  doc["distance_cm"] = telemetry.distanceCm;
  doc["alarm"] = telemetry.alarm;
  doc["preheating"] = telemetry.preheating;
  doc["uptime_s"] = telemetry.uptimeS;
  doc["state"] = telemetry.state;
  doc["methane_fault"] = telemetry.methaneFault;
  doc["co_fault"] = telemetry.coFault;

  String body;
  serializeJson(doc, body);
  server.send(200, "application/json", body);
}

void handleCommand() {
  StaticJsonDocument<256> doc;
  deserializeJson(doc, server.arg("plain"));

  const String mode = String(doc["mode"] | commandState.mode);
  if (mode == "stop" || mode == "forward" || mode == "backward" || mode == "left" || mode == "right" || mode == "auto") {
    commandState.mode = mode;
  }

  commandState.speed = constrain(doc["speed"] | commandState.speed, 0, 255);
  commandState.turn = constrain(doc["turn"] | commandState.turn, 0, 255);
  commandState.buzzer = doc["buzzer"] | commandState.buzzer;

  server.send(200, "application/json", "{\"ok\":true}");
}

void setup() {
  bootMs = millis();
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  initMotors();
  stopCar();

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  sht31.begin(0x44);
  initURM09();

  methaneSerial.begin(9600, SERIAL_8N1, PIN_CH4_RX, PIN_CH4_TX);
  coSerial.begin(9600, SERIAL_8N1, PIN_CO_RX, PIN_CO_TX);
  methaneSerial.setTimeout(20);
  coSerial.setTimeout(20);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  server.on("/", HTTP_GET, handleIndex);
  server.on("/api/telemetry", HTTP_GET, handleTelemetry);
  server.on("/api/command", HTTP_POST, handleCommand);
  server.begin();
}

void loop() {
  server.handleClient();

  const unsigned long now = millis();
  if (now - lastSensorMs >= SENSOR_INTERVAL_MS) {
    lastSensorMs = now;
    updateSensors();
    updateAlarmState();
  }

  if (now - lastControlMs >= CONTROL_INTERVAL_MS) {
    lastControlMs = now;
    applyCommand();
    updateBuzzer();
  }
}

void initMotors() {
  pinMode(PIN_AIN1, OUTPUT);
  pinMode(PIN_AIN2, OUTPUT);
  pinMode(PIN_BIN1, OUTPUT);
  pinMode(PIN_BIN2, OUTPUT);
  pinMode(PIN_STBY, OUTPUT);
  ledcAttach(PIN_PWMA, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_PWMB, PWM_FREQ, PWM_RES);
}

void setMotor(bool leftForward, int leftPwm, bool rightForward, int rightPwm) {
  leftPwm = constrain(leftPwm, 0, 255);
  rightPwm = constrain(rightPwm, 0, 255);

  digitalWrite(PIN_STBY, HIGH);
  digitalWrite(PIN_AIN1, leftForward ? HIGH : LOW);
  digitalWrite(PIN_AIN2, leftForward ? LOW : HIGH);
  digitalWrite(PIN_BIN1, rightForward ? HIGH : LOW);
  digitalWrite(PIN_BIN2, rightForward ? LOW : HIGH);
  ledcWrite(PIN_PWMA, leftPwm);
  ledcWrite(PIN_PWMB, rightPwm);
}

void stopCar() {
  ledcWrite(PIN_PWMA, 0);
  ledcWrite(PIN_PWMB, 0);
  digitalWrite(PIN_AIN1, LOW);
  digitalWrite(PIN_AIN2, LOW);
  digitalWrite(PIN_BIN1, LOW);
  digitalWrite(PIN_BIN2, LOW);
  digitalWrite(PIN_STBY, LOW);
}

void setMotion(const String& mode, int speed, int turn) {
  speed = constrain(speed, 0, 255);
  turn = constrain(turn, 0, 255);

  if (mode == "forward") {
    setMotor(true, speed, true, speed);
  } else if (mode == "backward") {
    setMotor(false, speed, false, speed);
  } else if (mode == "left") {
    setMotor(false, turn, true, turn);
  } else if (mode == "right") {
    setMotor(true, turn, false, turn);
  } else {
    stopCar();
  }
}

bool isPreheating() {
  return (millis() - bootMs) < SENSOR_PREHEAT_MS;
}

void updateSensors() {
  float ppm = 0.0f;
  bool fault = false;

  if (readMethane(ppm, fault)) {
    telemetry.ch4Ppm = ppm;
    telemetry.methaneFault = fault;
  }

  if (readCO(ppm)) {
    telemetry.coPpm = ppm;
  }

  const float tempC = sht31.readTemperature();
  const float humRh = sht31.readHumidity();
  if (!isnan(tempC)) {
    telemetry.temperatureC = tempC;
  }
  if (!isnan(humRh)) {
    telemetry.humidityRh = humRh;
  }

  float distanceCm = -1.0f;
  if (readURM09Distance(distanceCm)) {
    telemetry.distanceCm = distanceCm;
  }

  telemetry.preheating = isPreheating();
  telemetry.uptimeS = millis() / 1000UL;
}

void updateAlarmState() {
  if (isPreheating()) {
    telemetry.alarm = false;
    return;
  }

  telemetry.alarm =
    telemetry.methaneFault ||
    telemetry.coFault ||
    telemetry.ch4Ppm >= CH4_ALARM_PPM ||
    telemetry.coPpm >= CO_ALARM_PPM;
}

void applyCommand() {
  if (telemetry.preheating) {
    telemetry.state = "preheating";
    stopCar();
    return;
  }

  if (telemetry.alarm) {
    telemetry.state = "alarm_stop";
    stopCar();
    return;
  }

  if (commandState.mode == "auto") {
    if (telemetry.distanceCm > 0 && telemetry.distanceCm < BLOCK_DISTANCE_CM) {
      telemetry.state = "avoid_right";
      setMotion("right", commandState.turn, commandState.turn);
    } else {
      telemetry.state = "auto_forward";
      setMotion("forward", commandState.speed, commandState.turn);
    }
    return;
  }

  const bool forwardLike = commandState.mode == "forward" || commandState.mode == "left" || commandState.mode == "right";
  if (forwardLike && telemetry.distanceCm > 0 && telemetry.distanceCm < BLOCK_DISTANCE_CM) {
    telemetry.state = "blocked";
    stopCar();
    return;
  }

  telemetry.state = commandState.mode;
  setMotion(commandState.mode, commandState.speed, commandState.turn);
}

void updateBuzzer() {
  const bool buzzerOn = telemetry.alarm || commandState.buzzer;
  digitalWrite(PIN_BUZZER, buzzerOn ? HIGH : LOW);
}

uint8_t calcChecksum(const uint8_t* frame) {
  uint8_t sum = 0;
  for (int i = 1; i <= 7; ++i) {
    sum += frame[i];
  }
  return static_cast<uint8_t>(~sum + 1);
}

bool readSerialFrame(HardwareSerial& port, uint8_t expectedGasType, uint8_t* frame) {
  while (port.available() >= 9) {
    if (port.peek() != 0xFF) {
      port.read();
      continue;
    }

    const size_t count = port.readBytes(frame, 9);
    if (count != 9) {
      return false;
    }

    if (frame[0] != 0xFF || frame[1] != expectedGasType) {
      continue;
    }

    if (calcChecksum(frame) == frame[8]) {
      return true;
    }
  }
  return false;
}

bool readMethane(float& ppm, bool& fault) {
  uint8_t frame[9];
  if (!readSerialFrame(methaneSerial, 0x01, frame)) {
    return false;
  }

  fault = (frame[4] & 0x80) != 0;
  const uint16_t raw = (static_cast<uint16_t>(frame[4] & 0x1F) << 8) | frame[5];
  ppm = static_cast<float>(raw);
  return true;
}

bool readCO(float& ppm) {
  uint8_t frame[9];
  if (!readSerialFrame(coSerial, 0x04, frame)) {
    return false;
  }

  const uint16_t raw = (static_cast<uint16_t>(frame[4]) << 8) | frame[5];
  float value = static_cast<float>(raw);
  for (uint8_t i = 0; i < frame[3]; ++i) {
    value /= 10.0f;
  }

  ppm = value;
  telemetry.coFault = false;
  return true;
}

void urm09WriteByte(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(URM09_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

int16_t urm09ReadWord(uint8_t reg) {
  Wire.beginTransmission(URM09_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return -1;
  }

  if (Wire.requestFrom(URM09_ADDR, static_cast<uint8_t>(2)) != 2) {
    return -1;
  }

  const uint8_t highByte = Wire.read();
  const uint8_t lowByte = Wire.read();
  return static_cast<int16_t>((highByte << 8) | lowByte);
}

void initURM09() {
  urm09WriteByte(URM09_CONFIG_REG, URM09_RANGE_500 | URM09_MODE_PASSIVE);
  delay(50);
}

bool readURM09Distance(float& distanceCm) {
  urm09WriteByte(URM09_COMMAND_REG, URM09_CMD_READ_ONCE);
  delay(40);

  const int16_t distance = urm09ReadWord(URM09_DISTANCE_H_REG);
  if (distance < 0 || distance > 500) {
    return false;
  }

  distanceCm = static_cast<float>(distance);
  return true;
}

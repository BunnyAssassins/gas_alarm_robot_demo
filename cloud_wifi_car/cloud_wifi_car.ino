#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* SERVER_BASE_URL = "http://192.168.1.100:8000";
const char* ROBOT_ID = "gas-car-01";

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
constexpr unsigned long COMMAND_INTERVAL_MS = 300UL;
constexpr unsigned long TELEMETRY_INTERVAL_MS = 1000UL;
constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 5000UL;

HardwareSerial methaneSerial(1);
HardwareSerial coSerial(2);
Adafruit_SHT31 sht31 = Adafruit_SHT31();

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
  int wifiRssi = 0;
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
unsigned long lastCommandMs = 0;
unsigned long lastTelemetryMs = 0;
unsigned long lastWifiRetryMs = 0;

void connectWiFi();
void ensureWiFi();
void initMotors();
void setMotor(bool leftForward, int leftPwm, bool rightForward, int rightPwm);
void stopCar();
void setMotion(const String& mode, int speed, int turn);
void updateSensors();
void updateAlarmState();
void updateBuzzer();
void applyCommand();
void fetchCommand();
void postTelemetry();
bool readSerialFrame(HardwareSerial& port, uint8_t expectedGasType, uint8_t* frame);
uint8_t calcChecksum(const uint8_t* frame);
bool readMethane(float& ppm, bool& fault);
bool readCO(float& ppm);
void initURM09();
bool readURM09Distance(float& distanceCm);
bool isPreheating();

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

  WiFi.mode(WIFI_STA);
  connectWiFi();
}

void loop() {
  ensureWiFi();

  const unsigned long now = millis();

  if (now - lastSensorMs >= SENSOR_INTERVAL_MS) {
    lastSensorMs = now;
    updateSensors();
    updateAlarmState();
  }

  if (now - lastCommandMs >= COMMAND_INTERVAL_MS) {
    lastCommandMs = now;
    fetchCommand();
    applyCommand();
    updateBuzzer();
  }

  if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryMs = now;
    postTelemetry();
  }
}

void connectWiFi() {
  Serial.printf("Connecting to Wi-Fi: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  const unsigned long now = millis();
  if (now - lastWifiRetryMs < WIFI_RETRY_INTERVAL_MS) {
    return;
  }

  lastWifiRetryMs = now;
  WiFi.disconnect();
  connectWiFi();
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
  telemetry.wifiRssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
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

void fetchCommand() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  HTTPClient http;
  const String url = String(SERVER_BASE_URL) + "/api/command/" + ROBOT_ID;
  http.begin(url);
  const int code = http.GET();

  if (code == HTTP_CODE_OK) {
    StaticJsonDocument<256> doc;
    const DeserializationError err = deserializeJson(doc, http.getString());
    if (!err) {
      commandState.mode = String(doc["mode"] | commandState.mode);
      commandState.speed = doc["speed"] | commandState.speed;
      commandState.turn = doc["turn"] | commandState.turn;
      commandState.buzzer = doc["buzzer"] | commandState.buzzer;
    }
  }

  http.end();
}

void postTelemetry() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  HTTPClient http;
  const String url = String(SERVER_BASE_URL) + "/api/telemetry";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<384> doc;
  doc["robot_id"] = ROBOT_ID;
  doc["state"] = telemetry.state;
  doc["preheating"] = telemetry.preheating;
  doc["alarm"] = telemetry.alarm;
  doc["methane_fault"] = telemetry.methaneFault;
  doc["co_fault"] = telemetry.coFault;
  doc["ch4_ppm"] = telemetry.ch4Ppm;
  doc["co_ppm"] = telemetry.coPpm;
  doc["temperature_c"] = telemetry.temperatureC;
  doc["humidity_rh"] = telemetry.humidityRh;
  doc["distance_cm"] = telemetry.distanceCm;
  doc["uptime_s"] = telemetry.uptimeS;
  doc["wifi_rssi"] = telemetry.wifiRssi;

  String body;
  serializeJson(doc, body);
  http.POST(body);
  http.end();
}

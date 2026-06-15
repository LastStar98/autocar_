/*
  ESP32-CAM robot car with camera web UI, ESP32 forward PWM, and MCP23017
  reverse motor pins + line sensor inputs.

  Web UI:
    - Open http://<car-ip>/ in a browser.
    - Drive and debug ESP32 motor pins from the page.

  MCP23017 wiring:
    ESP32-CAM-MB IO13 -> MCP SDA
    ESP32-CAM-MB IO14 -> MCP SCL
    ESP32-CAM-MB GND  -> MCP GND
    ESP32-CAM-MB 3V3  -> MCP VCC

  Motor driver wiring:
    MCP PA0 -> L298N IN1, right motor reverse side
    ESP32-CAM-MB IO15 -> L298N IN2, right motor forward PWM
    ESP32-CAM-MB IO12 -> L298N IN3, left motor forward PWM
    MCP PA1 -> L298N IN4, left motor reverse side

  Keep L298N ENA/ENB jumpers installed.

  Line sensor wiring:
    Line sensor L -> MCP PB0
    Line sensor C -> MCP PB1, optional
    Line sensor R -> MCP PB2

  Final battery mode:
    Battery + -> L298N +VDD
    Battery - -> L298N GND
    L298N 5V -> ESP32-CAM-MB 5V/VCC
    L298N GND -> ESP32-CAM-MB GND and MCP GND

  Do not feed USB 5V and L298N 5V into the ESP32-CAM-MB at the same time.
*/

#include "esp_camera.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "secrets.h"

const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;
const char *apSsid = "autocar";
const char *apPassword = "12345678";

// AI Thinker ESP32-CAM camera pins.
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27

#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

#define LED_GPIO_NUM 4

// Bit-banged I2C pins for MCP23017.
#define I2C_SDA_PIN 13
#define I2C_SCL_PIN 14

// MCP23017 register addresses, IOCON.BANK=0 default.
#define MCP_IODIRA 0x00
#define MCP_IODIRB 0x01
#define MCP_GPPUB 0x0D
#define MCP_GPIOA 0x12
#define MCP_GPIOB 0x13
#define MCP_OLATA 0x14

#define MOTOR_R_FORWARD_PIN 15
#define MOTOR_L_FORWARD_PIN 12

#define PA0_IN1 0
#define PA1_IN4 1

const uint8_t REVERSE_NONE = 0x00;
const uint8_t REVERSE_RIGHT_IN1 = (1 << PA0_IN1);
const uint8_t REVERSE_LEFT_IN4 = (1 << PA1_IN4);
const uint8_t REVERSE_BOTH = REVERSE_RIGHT_IN1 | REVERSE_LEFT_IN4;

const bool USE_CENTER_LINE_SENSOR = false;  // false: use L/R only. true: use L/C/R.
const int SPEED_MIN_LEVEL = 1;
const int SPEED_MAX_LEVEL = 5;
const int SPEED_STEP_PERCENT = 20;

int speedLevel = 3;
int motorSpeed = 153;

bool Video_Flip = true;
bool mcpReady = false;
uint8_t mcpAddress = 0;
char motorPattern[18] = "stop";
uint8_t lineL = 1;
uint8_t lineC = 1;
uint8_t lineR = 1;
uint8_t lineGpioB = 0xFF;
bool lineFollowEnabled = false;
char lastAction[20] = "boot";
unsigned long lastLineFollowAt = 0;
uint8_t reverseLatch = REVERSE_NONE;
SemaphoreHandle_t mcpMutex = NULL;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t camera_httpd = NULL;
httpd_handle_t stream_httpd = NULL;

void startCameraServer();

void sdaRelease() {
  pinMode(I2C_SDA_PIN, INPUT);
}

void sdaLow() {
  pinMode(I2C_SDA_PIN, OUTPUT);
  digitalWrite(I2C_SDA_PIN, LOW);
}

void sclRelease() {
  pinMode(I2C_SCL_PIN, INPUT);
}

void sclLow() {
  pinMode(I2C_SCL_PIN, OUTPUT);
  digitalWrite(I2C_SCL_PIN, LOW);
}

void i2cDelay() {
  delayMicroseconds(8);
}

bool waitSclHigh() {
  sclRelease();
  unsigned long start = micros();
  while (digitalRead(I2C_SCL_PIN) == LOW) {
    if (micros() - start > 1000) return false;
  }
  return true;
}

bool i2cStart() {
  sdaRelease();
  if (!waitSclHigh()) return false;
  i2cDelay();
  sdaLow();
  i2cDelay();
  sclLow();
  i2cDelay();
  return true;
}

bool i2cStop() {
  sdaLow();
  i2cDelay();
  if (!waitSclHigh()) return false;
  i2cDelay();
  sdaRelease();
  i2cDelay();
  return true;
}

bool i2cWriteByte(uint8_t value) {
  for (int bit = 7; bit >= 0; bit--) {
    if (value & (1 << bit)) {
      sdaRelease();
    } else {
      sdaLow();
    }
    i2cDelay();
    if (!waitSclHigh()) return false;
    i2cDelay();
    sclLow();
    i2cDelay();
  }

  sdaRelease();
  i2cDelay();
  if (!waitSclHigh()) return false;
  bool ack = digitalRead(I2C_SDA_PIN) == LOW;
  sclLow();
  i2cDelay();
  return ack;
}

uint8_t i2cReadByte(bool ack) {
  uint8_t value = 0;
  sdaRelease();

  for (int bit = 7; bit >= 0; bit--) {
    i2cDelay();
    if (!waitSclHigh()) return 0xFF;
    if (digitalRead(I2C_SDA_PIN)) value |= (1 << bit);
    i2cDelay();
    sclLow();
    i2cDelay();
  }

  if (ack) {
    sdaLow();
  } else {
    sdaRelease();
  }
  i2cDelay();
  waitSclHigh();
  i2cDelay();
  sclLow();
  sdaRelease();
  i2cDelay();
  return value;
}

bool takeMcpBus() {
  return mcpMutex == NULL || xSemaphoreTake(mcpMutex, pdMS_TO_TICKS(200)) == pdTRUE;
}

void giveMcpBus() {
  if (mcpMutex != NULL) xSemaphoreGive(mcpMutex);
}

bool probeAddressUnlocked(uint8_t address) {
  if (!i2cStart()) return false;
  bool ack = i2cWriteByte((address << 1) | 0);
  i2cStop();
  return ack;
}

bool scanMcpAddress() {
  if (!takeMcpBus()) return false;

  for (uint8_t address = 0x20; address <= 0x27; address++) {
    if (probeAddressUnlocked(address)) {
      mcpAddress = address;
      mcpReady = true;
      giveMcpBus();
      return true;
    }
  }

  mcpAddress = 0;
  mcpReady = false;
  giveMcpBus();
  return false;
}

bool mcpWriteRegister(uint8_t reg, uint8_t value) {
  if (!mcpReady || !takeMcpBus()) return false;

  bool ok = false;
  if (i2cStart() &&
      i2cWriteByte((mcpAddress << 1) | 0) &&
      i2cWriteByte(reg) &&
      i2cWriteByte(value) &&
      i2cStop()) {
    ok = true;
  } else {
    i2cStop();
  }

  giveMcpBus();
  return ok;
}

bool mcpReadRegister(uint8_t reg, uint8_t &value) {
  if (!mcpReady || !takeMcpBus()) return false;

  bool ok = false;
  if (i2cStart() &&
      i2cWriteByte((mcpAddress << 1) | 0) &&
      i2cWriteByte(reg) &&
      i2cStart() &&
      i2cWriteByte((mcpAddress << 1) | 1)) {
    value = i2cReadByte(false);
    ok = i2cStop();
  } else {
    i2cStop();
  }

  giveMcpBus();
  return ok;
}

bool configureMcp() {
  sdaRelease();
  sclRelease();
  delay(50);

  if (!scanMcpAddress()) {
    strcpy(lastAction, "mcp-missing");
    return false;
  }

  // PA0/PA1 outputs for reverse-side IN1/IN4. PA2-PA7 remain inputs/spares.
  if (!mcpWriteRegister(MCP_OLATA, REVERSE_NONE)) {
    mcpReady = false;
    strcpy(lastAction, "mcp-olata-fail");
    return false;
  }

  if (!mcpWriteRegister(MCP_IODIRA, 0xFC)) {
    mcpReady = false;
    strcpy(lastAction, "mcp-iodira-fail");
    return false;
  }

  // Port B inputs for line sensors. PB0/PB1/PB2 use pullups.
  if (!mcpWriteRegister(MCP_IODIRB, 0xFF)) {
    mcpReady = false;
    strcpy(lastAction, "mcp-iodirb-fail");
    return false;
  }

  if (!mcpWriteRegister(MCP_GPPUB, 0x07)) {
    mcpReady = false;
    strcpy(lastAction, "mcp-gppub-fail");
    return false;
  }

  reverseLatch = REVERSE_NONE;
  strcpy(lastAction, "ready");
  return true;
}

int speedWithTrim(int trim) {
  int speed = motorSpeed + trim;
  if (speed < 0) return 0;
  if (speed > 255) return 255;
  return speed;
}

int speedPercent() {
  return speedLevel * SPEED_STEP_PERCENT;
}

void setSpeedLevel(int level) {
  if (level < SPEED_MIN_LEVEL) level = SPEED_MIN_LEVEL;
  if (level > SPEED_MAX_LEVEL) level = SPEED_MAX_LEVEL;
  speedLevel = level;
  motorSpeed = (speedPercent() * 255 + 50) / 100;
}

bool setReverseLatch(uint8_t reverseMask) {
  reverseMask &= REVERSE_BOTH;
  if (!mcpReady && !configureMcp()) return false;
  if (!mcpWriteRegister(MCP_OLATA, reverseMask)) {
    mcpReady = false;
    strcpy(lastAction, "reverse-fail");
    return false;
  }
  reverseLatch = reverseMask;
  return true;
}

void setForwardPwm(int rightSpeed, int leftSpeed) {
  analogWrite(MOTOR_R_FORWARD_PIN, rightSpeed);
  analogWrite(MOTOR_L_FORWARD_PIN, leftSpeed);
}

bool setMotorAction(const char *action) {
  if (strcmp(action, "forward") == 0) {
    setForwardPwm(0, 0);
    if (!setReverseLatch(REVERSE_NONE)) return false;
    setForwardPwm(speedWithTrim(0), speedWithTrim(5));
    strcpy(motorPattern, "forward");
  } else if (strcmp(action, "backward") == 0) {
    setForwardPwm(0, 0);
    if (!setReverseLatch(REVERSE_BOTH)) return false;
    strcpy(motorPattern, "backward");
  } else if (strcmp(action, "left") == 0) {
    setForwardPwm(0, 0);
    if (!setReverseLatch(REVERSE_NONE)) return false;
    setForwardPwm(speedWithTrim(0), 0);
    strcpy(motorPattern, "left-soft");
  } else if (strcmp(action, "right") == 0) {
    setForwardPwm(0, 0);
    if (!setReverseLatch(REVERSE_NONE)) return false;
    setForwardPwm(0, speedWithTrim(5));
    strcpy(motorPattern, "right-soft");
  } else if (strcmp(action, "back_left") == 0) {
    setForwardPwm(0, 0);
    if (!setReverseLatch(REVERSE_LEFT_IN4)) return false;
    strcpy(motorPattern, "back-left");
  } else if (strcmp(action, "back_right") == 0) {
    setForwardPwm(0, 0);
    if (!setReverseLatch(REVERSE_RIGHT_IN1)) return false;
    strcpy(motorPattern, "back-right");
  } else if (strcmp(action, "pin1") == 0) {
    setForwardPwm(0, 0);
    if (!setReverseLatch(REVERSE_RIGHT_IN1)) return false;
    strcpy(motorPattern, "pin1");
  } else if (strcmp(action, "pin2") == 0) {
    setForwardPwm(motorSpeed, 0);
    if (!setReverseLatch(REVERSE_NONE)) return false;
    strcpy(motorPattern, "pin2");
  } else if (strcmp(action, "pin3") == 0) {
    setForwardPwm(0, motorSpeed);
    if (!setReverseLatch(REVERSE_NONE)) return false;
    strcpy(motorPattern, "pin3");
  } else if (strcmp(action, "pin4") == 0) {
    setForwardPwm(0, 0);
    if (!setReverseLatch(REVERSE_LEFT_IN4)) return false;
    strcpy(motorPattern, "pin4");
  } else {
    setForwardPwm(0, 0);
    if (!setReverseLatch(REVERSE_NONE)) return false;
    strcpy(motorPattern, "stop");
  }

  strncpy(lastAction, action, sizeof(lastAction) - 1);
  lastAction[sizeof(lastAction) - 1] = '\0';
  return true;
}

void stopMotors() {
  setMotorAction("stop");
}

bool readLineSensors() {
  if (!mcpReady && !configureMcp()) return false;

  uint8_t gpiob = 0xFF;
  if (!mcpReadRegister(MCP_GPIOB, gpiob)) {
    mcpReady = false;
    strcpy(lastAction, "line-read-fail");
    return false;
  }

  lineGpioB = gpiob;
  lineL = (gpiob >> 0) & 0x01;
  lineC = (gpiob >> 1) & 0x01;
  lineR = (gpiob >> 2) & 0x01;
  return true;
}

const char *lineToMotorAction() {
  bool leftDetected = lineL == 0;
  bool centerDetected = lineC == 0;
  bool rightDetected = lineR == 0;

  if (!USE_CENTER_LINE_SENSOR) {
    if (leftDetected && !rightDetected) return "left";
    if (rightDetected && !leftDetected) return "right";
    if (!leftDetected && !rightDetected) return "forward";
    return "stop";
  }

  if (centerDetected && !leftDetected && !rightDetected) return "forward";
  if (leftDetected && !rightDetected) return "left";
  if (rightDetected && !leftDetected) return "right";
  if (centerDetected && leftDetected && !rightDetected) return "left";
  if (centerDetected && rightDetected && !leftDetected) return "right";
  return "stop";
}

void updateLineFollow() {
  if (!lineFollowEnabled) return;
  if (millis() - lastLineFollowAt < 80) return;
  lastLineFollowAt = millis();

  if (!readLineSensors()) {
    lineFollowEnabled = false;
    stopMotors();
    return;
  }

  setMotorAction(lineToMotorAction());
}

void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_HVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_VGA);
  if (Video_Flip) {
    s->set_vflip(s, 1);
    s->set_hmirror(s, 0);
  }
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  Serial.setDebugOutput(false);
  pinMode(MOTOR_R_FORWARD_PIN, OUTPUT);
  pinMode(MOTOR_L_FORWARD_PIN, OUTPUT);
  pinMode(LED_GPIO_NUM, OUTPUT);
  digitalWrite(LED_GPIO_NUM, LOW);

  mcpMutex = xSemaphoreCreateMutex();
  configureMcp();
  stopMotors();

  setupCamera();

  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.softAP(apSsid, apPassword);

  Serial.print("Fallback AP Ready: http://");
  Serial.println(WiFi.softAPIP());

  WiFi.begin(ssid, password);
  unsigned long wifiStartAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStartAt < 12000) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    Serial.print("Camera Stream Ready! Go to: http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("STA WiFi not connected. Use fallback AP instead.");
  }
  Serial.print("Fallback UI: http://");
  Serial.println(WiFi.softAPIP());

  if (MDNS.begin("autocar")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS ready: http://autocar.local");
  }

  startCameraServer();
}

void loop() {
  updateLineFollow();
  delay(10);
}

static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<!doctype html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-CAM MCP Robot</title>
  <style>
    body {
      margin: 0;
      font-family: Arial, sans-serif;
      background: #f4f6f8;
      color: #1f2933;
      text-align: center;
    }
    header {
      padding: 14px 10px 8px;
      background: #18212f;
      color: white;
    }
    h1 {
      margin: 0;
      font-size: 20px;
      font-weight: 700;
    }
    main {
      max-width: 720px;
      margin: 0 auto;
      padding: 12px;
    }
    img {
      width: 100%;
      max-width: 640px;
      height: auto;
      background: #111827;
      border: 2px solid #18212f;
      border-radius: 8px;
    }
    .status {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 8px;
      margin: 12px auto;
      max-width: 640px;
      text-align: left;
    }
    .status div {
      background: white;
      border: 1px solid #d9e2ec;
      border-radius: 6px;
      padding: 8px 10px;
      font-size: 14px;
    }
    .controls {
      display: grid;
      grid-template-areas:
        ". forward ."
        "left stop right"
        ". backward .";
      gap: 10px;
      max-width: 330px;
      margin: 14px auto;
    }
    .debug {
      display: grid;
      grid-template-columns: repeat(5, 1fr);
      gap: 8px;
      max-width: 520px;
      margin: 10px auto;
    }
    .auto-row {
      display: grid;
      grid-template-columns: repeat(2, 1fr);
      gap: 8px;
      max-width: 330px;
      margin: 10px auto;
    }
    button {
      min-height: 58px;
      border: 0;
      border-radius: 8px;
      background: #2f4468;
      color: white;
      font-size: 17px;
      font-weight: 700;
      touch-action: none;
    }
    button.stop {
      background: #a61b1b;
    }
    button.debug-button {
      min-height: 48px;
      background: #475569;
      font-size: 15px;
    }
    button.auto-button {
      min-height: 50px;
      background: #1f7a4d;
      font-size: 15px;
    }
    button.auto-off {
      background: #8a3a1c;
    }
    .forward { grid-area: forward; }
    .backward { grid-area: backward; }
    .left { grid-area: left; }
    .right { grid-area: right; }
    .stop { grid-area: stop; }
  </style>
</head>
<body>
  <header><h1>ESP32-CAM MCP Robot</h1></header>
  <main>
    <img id="photo" alt="camera stream">

    <section class="status">
      <div>MCP: <strong id="mcp">loading</strong></div>
      <div>ADDR: <strong id="addr">--</strong></div>
      <div>GPIOA: <strong id="gpioa">--</strong></div>
      <div>GPIOB: <strong id="gpiob">--</strong></div>
      <div>Line: <strong id="line">--</strong></div>
      <div>AUTO: <strong id="auto">--</strong></div>
      <div>Pattern: <strong id="pattern">--</strong></div>
      <div>Speed: <strong id="speed">--</strong></div>
      <div>Action: <strong id="action">--</strong></div>
      <div>IP: <strong id="ip">--</strong></div>
    </section>

    <section class="controls">
      <button class="forward" data-go="forward">FWD</button>
      <button class="left" data-go="left">LEFT</button>
      <button class="stop stop" data-go="stop">STOP</button>
      <button class="right" data-go="right">RIGHT</button>
      <button class="backward" data-go="backward">BACK</button>
    </section>

    <section class="auto-row">
      <button class="auto-button" data-go="auto_on">AUTO ON</button>
      <button class="auto-button auto-off" data-go="auto_off">AUTO OFF</button>
    </section>

    <section class="debug">
      <button class="debug-button" data-go="pin1">IN1</button>
      <button class="debug-button" data-go="pin2">IN2</button>
      <button class="debug-button" data-go="pin3">IN3</button>
      <button class="debug-button" data-go="pin4">IN4</button>
      <button class="debug-button" data-go="speed1">G1</button>
      <button class="debug-button" data-go="speed2">G2</button>
      <button class="debug-button" data-go="speed3">G3</button>
      <button class="debug-button" data-go="speed4">G4</button>
      <button class="debug-button" data-go="speed5">G5</button>
    </section>
  </main>

  <script>
    const photo = document.getElementById("photo");
    photo.src = "http://" + location.hostname + ":81/stream";
    document.getElementById("ip").textContent = location.host;

    const sendAction = (go) => {
      return fetch("/action?go=" + encodeURIComponent(go), { cache: "no-store" })
        .then(() => updateStatus())
        .catch(() => {});
    };

    const updateStatus = () => {
      return fetch("/status", { cache: "no-store" })
        .then((res) => res.json())
        .then((data) => {
          document.getElementById("mcp").textContent = data.mcpReady ? "ready" : "not ready";
          document.getElementById("addr").textContent = data.address;
          document.getElementById("gpioa").textContent = data.gpioa;
          document.getElementById("gpiob").textContent = data.gpiob;
          document.getElementById("line").textContent = "L:" + data.lineL + " C:" + data.lineC + " R:" + data.lineR;
          document.getElementById("auto").textContent = data.auto ? "on" : "off";
          document.getElementById("pattern").textContent = data.pattern;
          document.getElementById("speed").textContent = data.speedLevel + " / " + data.speedPercent + "% / PWM " + data.speed;
          document.getElementById("action").textContent = data.action;
        })
        .catch(() => {
          document.getElementById("mcp").textContent = "offline";
        });
    };

    document.querySelectorAll("button[data-go]").forEach((button) => {
      const go = button.dataset.go;
      if (go === "stop" || go === "auto_on" || go === "auto_off" || go.startsWith("speed")) {
        button.addEventListener("click", () => sendAction(go));
        return;
      }

      button.addEventListener("pointerdown", (event) => {
        event.preventDefault();
        sendAction(go);
      });
      button.addEventListener("pointerup", () => sendAction("stop"));
      button.addEventListener("pointercancel", () => sendAction("stop"));
      button.addEventListener("pointerleave", () => sendAction("stop"));
    });

    updateStatus();
    setInterval(updateStatus, 1000);
  </script>
</body>
</html>
)rawliteral";

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, (const char *)INDEX_HTML, strlen(INDEX_HTML));
}

static esp_err_t status_handler(httpd_req_t *req) {
  uint8_t gpioa = 0xFF;
  bool gpioaOk = mcpReady && mcpReadRegister(MCP_GPIOA, gpioa);
  bool lineOk = mcpReady && readLineSensors();

  char response[520];
  snprintf(response, sizeof(response),
           "{\"mcpReady\":%s,\"address\":\"0x%02X\",\"gpioa\":\"%s0x%02X\",\"gpiob\":\"%s0x%02X\",\"pattern\":\"%s\",\"speed\":%d,\"speedLevel\":%d,\"speedPercent\":%d,\"action\":\"%s\",\"auto\":%s,\"lineL\":%u,\"lineC\":%u,\"lineR\":%u}",
           mcpReady ? "true" : "false",
           mcpAddress,
           gpioaOk ? "" : "?",
           gpioa,
           lineOk ? "" : "?",
           lineGpioB,
           motorPattern,
           motorSpeed,
           speedLevel,
           speedPercent(),
           lastAction,
           lineFollowEnabled ? "true" : "false",
           lineL,
           lineC,
           lineR);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t action_handler(httpd_req_t *req) {
  char query[96];
  char action[20];

  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "go", action, sizeof(action)) != ESP_OK) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "bad action", HTTPD_RESP_USE_STRLEN);
  }

  if (strcmp(action, "auto_on") == 0) {
    lineFollowEnabled = true;
    lastLineFollowAt = 0;
    readLineSensors();
    strncpy(lastAction, "auto-on", sizeof(lastAction) - 1);
    lastAction[sizeof(lastAction) - 1] = '\0';
    return httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
  }

  if (strcmp(action, "auto_off") == 0) {
    lineFollowEnabled = false;
    stopMotors();
    strncpy(lastAction, "auto-off", sizeof(lastAction) - 1);
    lastAction[sizeof(lastAction) - 1] = '\0';
    return httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
  }

  if (strncmp(action, "speed", 5) == 0 && action[5] >= '1' && action[5] <= '5' && action[6] == '\0') {
    setSpeedLevel(action[5] - '0');
    snprintf(lastAction, sizeof(lastAction), "speed-%d", speedLevel);
    lastAction[sizeof(lastAction) - 1] = '\0';
    return httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
  }

  if (strcmp(action, "forward") != 0 &&
      strcmp(action, "backward") != 0 &&
      strcmp(action, "left") != 0 &&
      strcmp(action, "right") != 0 &&
      strcmp(action, "stop") != 0 &&
      strcmp(action, "pin1") != 0 &&
      strcmp(action, "pin2") != 0 &&
      strcmp(action, "pin3") != 0 &&
      strcmp(action, "pin4") != 0) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "bad action", HTTPD_RESP_USE_STRLEN);
  }

  lineFollowEnabled = false;
  bool ok = setMotorAction(action);
  httpd_resp_set_type(req, "text/plain");
  if (!ok) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_send(req, "motor action failed", HTTPD_RESP_USE_STRLEN);
  }
  return httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t led_handler(httpd_req_t *req) {
  return httpd_resp_send(req, "flash LED disabled: GPIO4 is MCP SCL", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char part_buf[64];

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      if (fb->format != PIXFORMAT_JPEG) {
        bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
        esp_camera_fb_return(fb);
        fb = NULL;
        if (!jpeg_converted) {
          Serial.println("JPEG compression failed");
          res = ESP_FAIL;
        }
      } else {
        _jpg_buf_len = fb->len;
        _jpg_buf = fb->buf;
      }
    }

    if (res == ESP_OK) {
      size_t hlen = snprintf(part_buf, sizeof(part_buf), _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }

    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if (_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }

    if (res != ESP_OK) break;
  }

  return res;
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;

  httpd_uri_t index_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_handler,
    .user_ctx = NULL
  };
  httpd_uri_t action_uri = {
    .uri = "/action",
    .method = HTTP_GET,
    .handler = action_handler,
    .user_ctx = NULL
  };
  httpd_uri_t status_uri = {
    .uri = "/status",
    .method = HTTP_GET,
    .handler = status_handler,
    .user_ctx = NULL
  };
  httpd_uri_t led_uri = {
    .uri = "/led",
    .method = HTTP_GET,
    .handler = led_handler,
    .user_ctx = NULL
  };
  httpd_uri_t stream_uri = {
    .uri = "/stream",
    .method = HTTP_GET,
    .handler = stream_handler,
    .user_ctx = NULL
  };

  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &action_uri);
    httpd_register_uri_handler(camera_httpd, &status_uri);
    httpd_register_uri_handler(camera_httpd, &led_uri);
  }

  config.server_port = 81;
  config.ctrl_port = 32769;
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}

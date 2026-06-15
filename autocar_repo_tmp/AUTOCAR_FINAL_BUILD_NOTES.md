# Autocar Final Build Notes

이 문서는 현재 완성 방향 기준의 최종 정리입니다. 실험 중간에 사용했던 배선과 테스트 코드는 제외하고, 실제로 사용할 구조만 정리합니다.

대상:

- Keyestudio KS5023 계열 ESP32-CAM 2WD Camera Robot Car
- ESP32-CAM + ESP32-CAM-MB
- L298N 모터 드라이버
- Waveshare MCP23017 I/O Expansion Board
- TCRT5000 3채널 라인센서

---

## 1. 최종 목표

최종 목표는 다음입니다.

- ESP32-CAM 카메라 웹 UI 유지
- 웹 UI에서 수동 주행 가능
- 웹 UI에서 라인센서 상태 확인 가능
- 웹 UI에서 라인트레이싱 자동모드 ON/OFF 가능
- 전진 속도는 1단~5단으로 조절
- MCP23017은 라인센서 입력과 후진 방향핀 보조 제어에 사용

현재 최종 구조:

```text
ESP32-CAM
  - Camera / Wi-Fi / Web UI
  - IO15: 오른쪽 전진 PWM
  - IO12: 왼쪽 전진 PWM
  - IO13/IO14: MCP23017 I2C

MCP23017
  - PA0: 오른쪽 후진 방향핀, L298N IN1
  - PA1: 왼쪽 후진 방향핀, L298N IN4
  - PB0/PB2: 라인센서 L/R
  - PB1: 라인센서 C, 나중에 연결
```

---

## 2. 최종 배선

### MCP23017 I2C

```text
ESP32-CAM-MB IO13 -> MCP SDA
ESP32-CAM-MB IO14 -> MCP SCL
ESP32-CAM-MB 3V3  -> MCP VCC
ESP32-CAM-MB GND  -> MCP GND
```

MCP23017 주소는 Waveshare 보드 기본값 기준 `0x27`입니다.

### L298N 모터 입력

```text
MCP PA0              -> L298N IN1
ESP32-CAM-MB IO15   -> L298N IN2
ESP32-CAM-MB IO12   -> L298N IN3
MCP PA1              -> L298N IN4
```

역할:

```text
IN1 = 오른쪽 모터 후진 방향핀, MCP 제어
IN2 = 오른쪽 모터 전진 PWM, ESP32 제어
IN3 = 왼쪽 모터 전진 PWM, ESP32 제어
IN4 = 왼쪽 모터 후진 방향핀, MCP 제어
```

L298N의 `ENA/ENB` 점퍼는 그대로 꽂아둡니다.

### 라인센서

현재 점퍼선 부족 때문에 2센서 모드로 사용합니다.

```text
라인센서 VCC -> MCP VCC
라인센서 GND -> MCP GND
라인센서 L   -> MCP PB0
라인센서 R   -> MCP PB2
라인센서 C   -> 연결 안 함
```

나중에 점퍼선이 생기면:

```text
라인센서 C -> MCP PB1
```

코드에서 아래 값을 바꾸면 3센서 모드로 전환할 수 있습니다.

```cpp
const bool USE_CENTER_LINE_SENSOR = true;
```

현재는 2센서 모드입니다.

```cpp
const bool USE_CENTER_LINE_SENSOR = false;
```

### 전원

최종 주행 모드는 USB 없이 배터리만 사용합니다.

```text
배터리 + -> L298N +VDD / VIN / +12V
배터리 - -> L298N GND

L298N 5V  -> ESP32-CAM-MB 5V/VCC
L298N GND -> ESP32-CAM-MB GND
ESP32-CAM-MB GND -> MCP GND
```

GND는 반드시 공통이어야 합니다.

```text
L298N GND
ESP32-CAM-MB GND
MCP GND
```

이 셋은 모두 서로 연결되어 있어야 합니다.

주의:

```text
USB 5V와 L298N 5V를 동시에 ESP32-CAM-MB 5V/VCC에 넣지 않습니다.
```

업로드할 때 USB를 쓴다면, `L298N 5V -> ESP32-CAM-MB 5V/VCC` 선은 빼는 것이 안전합니다.

---

## 3. 왜 이런 구조를 쓰는가

원래 RC카 코드는 L298N의 `ENA/ENB`가 아니라 `IN1~IN4`에 `analogWrite()`를 걸어서 속도 제어를 했습니다.

원래 전진 제어 방식:

```text
오른쪽 모터:
IN1 = 0
IN2 = PWM

왼쪽 모터:
IN3 = PWM
IN4 = 0
```

따라서 전진 속도 제어를 살리려면 `IN2`, `IN3` 두 핀만 ESP32 PWM 핀에 직접 연결하면 됩니다.

이번 최종 구조:

```text
IN2 -> ESP32 IO15, 오른쪽 전진 PWM
IN3 -> ESP32 IO12, 왼쪽 전진 PWM
IN1 -> MCP PA0, 오른쪽 후진 방향핀
IN4 -> MCP PA1, 왼쪽 후진 방향핀
```

장점:

- `IO13/IO14`의 안정적인 MCP I2C를 유지합니다.
- `IO16/IO4`를 쓰지 않아 카메라/LED 충돌을 피합니다.
- 전진 속도 제어가 살아납니다.
- 라인트레이싱은 대부분 전진 기반이므로 실용적입니다.

한계:

- 후진 속도 제어는 정교하지 않습니다.
- 후진은 MCP의 HIGH/LOW 제어입니다.
- 라인트레이싱 수업에서는 후진이 핵심이 아니므로 이 절충이 적합합니다.

---

## 4. 속도 단수

현재 속도는 1단~5단입니다.

웹 UI 버튼:

```text
G1 / G2 / G3 / G4 / G5
```

단수 매핑:

```text
G1 = 1단 = 20% = PWM 51
G2 = 2단 = 40% = PWM 102
G3 = 3단 = 60% = PWM 153
G4 = 4단 = 80% = PWM 204
G5 = 5단 = 100% = PWM 255
```

기본값:

```text
3단 = 60% = PWM 153
```

UI 표시 예:

```text
3 / 60% / PWM 153
```

작은 DC 모터는 너무 낮은 PWM에서 움직이지 않을 수 있습니다. 1단에서 안 움직이면 2단 또는 3단부터 테스트합니다.

---

## 5. 웹 UI

업로드 후 브라우저에서 접속합니다.

```text
http://차_IP/
```

또는 자체 AP를 사용할 때:

```text
Wi-Fi 이름: autocar
비밀번호: 12345678
주소: http://192.168.4.1/
```

UI 기능:

```text
FWD      전진
BACK     후진
LEFT     왼쪽 보정
RIGHT    오른쪽 보정
STOP     정지

AUTO ON  라인트레이싱 자동모드 켜기
AUTO OFF 라인트레이싱 자동모드 끄기

IN1      MCP PA0 -> L298N IN1 테스트
IN2      ESP IO15 -> L298N IN2 테스트
IN3      ESP IO12 -> L298N IN3 테스트
IN4      MCP PA1 -> L298N IN4 테스트

G1~G5    속도 단수 선택
```

상태 표시:

```text
MCP ready/not ready
ADDR
GPIOA
GPIOB
Line L/C/R
AUTO on/off
Pattern
Speed
Action
IP
```

---

## 6. 라인센서 값

현재 라인센서는 Active Low로 처리합니다.

```text
1 = 감지 안 됨
0 = 감지됨
```

예:

```text
L:1 R:1 -> 양쪽 모두 감지 안 됨
L:0 R:1 -> 왼쪽 센서 감지
L:1 R:0 -> 오른쪽 센서 감지
L:0 R:0 -> 양쪽 모두 감지
```

현재 2센서 모드의 자동주행 판단:

```text
L=1, R=1 -> 전진
L=0, R=1 -> 왼쪽 보정
L=1, R=0 -> 오른쪽 보정
L=0, R=0 -> 정지
```

---

## 7. 라인센서 부착 위치

라인센서는 차 앞쪽 아래에 부착합니다.

방향:

```text
센서의 적외선 부품이 있는 면 -> 바닥
핀과 전선이 있는 쪽 -> 위쪽 또는 뒤쪽
```

위치:

```text
차 앞쪽
앞바퀴보다 약간 앞 또는 앞바퀴 근처
바닥에서 약 5~10mm 위
```

센서 배열:

```text
차 왼쪽  [ L  C  R ]  차 오른쪽
```

현재는 C를 연결하지 않으므로 L/R만 사용합니다.

---

## 8. 바닥 라인

바닥에는 검은색 라인을 만듭니다.

추천:

```text
바탕: 흰 종이, 흰 우드락, 밝은 바닥
라인: 검은 절연테이프 또는 검은 마스킹테이프
라인 폭: 15~25mm
```

처음 테스트:

```text
직선
완만한 곡선
큰 사각형 트랙
```

피해야 할 것:

```text
반짝이는 바닥
너무 얇은 라인
급커브
검은 바닥
```

2센서 모드에서는 검은 선이 L/R 센서 사이 중앙에 오도록 맞춥니다.

---

## 9. 업로드와 주행 모드

### 업로드할 때

```text
USB -> ESP32-CAM-MB
IO0-GND 임시 연결, 필요할 때만
L298N 5V -> ESP32-CAM-MB 5V/VCC 선은 빼기
```

업로드 후:

```text
IO0-GND 제거
USB 제거
```

### 배터리 주행할 때

```text
USB 제거
L298N 5V -> ESP32-CAM-MB 5V/VCC 연결
배터리 ON
휴대폰/PC에서 UI 접속
```

---

## 10. 피해야 할 구성

### MCP를 IO16/IO4에 연결

테스트 결과:

- 배터리 전원과 UI는 켜졌지만 카메라 스트림이 안 나왔습니다.
- 센서 변화값도 UI에 안 나왔습니다.
- IO4는 플래시 LED 핀이라 LED가 켜졌습니다.

따라서 최종 구성에서 사용하지 않습니다.

```text
MCP SDA -> IO16
MCP SCL -> IO4
```

이 구성은 피합니다.

### MCP로 IN1~IN4 전체 제어

MCP23017은 일반 GPIO 확장칩입니다. HIGH/LOW 제어는 가능하지만 안정적인 PWM 속도 제어에는 적합하지 않습니다.

따라서 IN1~IN4 전체를 MCP로 보내면:

```text
직진/후진/좌우/정지는 가능
속도 제어는 사실상 포기
```

현재 최종 구조는 전진 PWM을 ESP32가 직접 담당하므로 이 문제를 피합니다.

---

## 11. 현재 코드 위치

스케치:

```text
sketch_may9a/sketch_may9a.ino
```

Wi-Fi 정보:

```text
sketch_may9a/secrets.h
```

현재 Wi-Fi:

```text
SSID: sw98
PASSWORD: 12345678
```

Fallback AP:

```text
SSID: autocar
PASSWORD: 12345678
URL: http://192.168.4.1/
```

---

## 12. 현재 코드의 핵심 상수

```cpp
#define I2C_SDA_PIN 13
#define I2C_SCL_PIN 14

#define MOTOR_R_FORWARD_PIN 15
#define MOTOR_L_FORWARD_PIN 12

#define PA0_IN1 0
#define PA1_IN4 1

const bool USE_CENTER_LINE_SENSOR = false;
const int SPEED_MIN_LEVEL = 1;
const int SPEED_MAX_LEVEL = 5;
const int SPEED_STEP_PERCENT = 20;

int speedLevel = 3;
int motorSpeed = 153;
```

---

## 13. 참고 자료

- Waveshare MCP23017 IO Expansion Board Wiki  
  https://www.waveshare.com/wiki/MCP23017_IO_Expansion_Board

- Microchip MCP23017 Datasheet  
  https://ww1.microchip.com/downloads/aemDocuments/documents/APID/ProductDocuments/DataSheets/MCP23017-Data-Sheet-DS20001952.pdf

- Keyestudio KS5023 2WD Camera Robot Car  
  https://docs.keyestudio.com/projects/KS5023/en/latest/docs/2WD%20Camera%20Robot%20Car.html

- ESP32-CAM AI Thinker Pinout  
  https://randomnerdtutorials.com/esp32-cam-ai-thinker-pinout/

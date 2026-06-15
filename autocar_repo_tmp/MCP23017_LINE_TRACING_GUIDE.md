# ESP32-CAM RC카 MCP23017 라인트레이싱 연결 가이드

이 문서는 Keyestudio KS5023 계열 ESP32-CAM RC카에 Waveshare MCP23017 I/O 확장 보드와 TCRT5000 3채널 라인센서를 연결해서 라인트레이싱 기능을 추가하기 위한 강의용 정리입니다.

중요한 기준은 다음입니다.

- ESP32-CAM-MB는 별도 제어 보드가 아니라 ESP32-CAM을 USB로 업로드하고 핀을 빼주는 보드입니다.
- MCP23017은 I2C로 ESP32-CAM과 통신하고, 부족한 입출력 핀을 확장합니다.
- 라인센서는 MCP23017의 입력핀으로 읽습니다.
- 모터 드라이버의 IN1~IN4는 나중에 MCP23017의 출력핀으로 옮기는 구성이 가장 깔끔합니다.
- 카메라, Wi-Fi, I2C, 라인센서, 모터를 동시에 쓰려면 ESP32-CAM의 핀 충돌을 피해야 합니다.

---

## 1. 전체 구조

최종 구조는 아래처럼 잡습니다.

```text
휴대폰 핫스팟/공유기
        |
        | Wi-Fi
        |
ESP32-CAM + ESP32-CAM-MB
        |
        | I2C: SDA=IO13, SCL=IO14
        |
Waveshare MCP23017 I/O Expansion Board
        |
        +-- PB0/PB1/PB2: TCRT5000 라인센서 L/C/R 입력
        |
        +-- 추후 GPA0~GPA3 등: 모터 드라이버 IN1~IN4 출력
```

지금까지 확인된 정상 동작은 다음입니다.

```text
MCP23017 bitbang I2C test
I2C SDA=GPIO13 SCL=GPIO14
Idle SDA:1 SCL:1
Scanning 0x20-0x27...
  0x20: no
  0x21: no
  0x22: no
  0x23: no
  0x24: no
  0x25: no
  0x26: no
  0x27: ACK
Using MCP23017 address 0x27
Writing IODIRB...
Writing GPPUB...
MCP23017 ready. Line sensors: L=PB0, C=PB1, R=PB2
```

`0x27: ACK`가 나오면 ESP32-CAM과 MCP23017 사이의 I2C 통신은 성공한 것입니다.

---

## 2. 부품 역할

### ESP32-CAM

카메라, Wi-Fi, 메인 프로그램 실행을 담당합니다.

주의할 점:

- ESP32-CAM은 사용 가능한 GPIO가 매우 적습니다.
- 카메라가 이미 많은 핀을 사용합니다.
- 일부 핀은 부팅 모드와 관련된 스트래핑 핀이므로 잘못 연결하면 업로드나 부팅이 실패합니다.
- `IO4`는 플래시 LED와 연결되어 있어 I2C SCL 같은 신호를 연결하면 LED가 계속 켜질 수 있습니다.

### ESP32-CAM-MB

ESP32-CAM-MB는 ESP32-CAM을 쉽게 업로드하기 위한 USB 시리얼 보드입니다.

중요:

- ESP32-CAM-MB 자체가 새로운 제어 보드는 아닙니다.
- MB 보드에 보이는 `IO13`, `IO14`, `GND`, `VCC` 등은 ESP32-CAM 쪽 핀을 밖으로 빼준 것입니다.
- 배선은 MB 보드 핀에 꽂아도 실제로는 ESP32-CAM 핀에 연결되는 것입니다.

### Waveshare MCP23017 I/O Expansion Board

MCP23017은 I2C 방식의 16비트 GPIO 확장칩입니다.

특징:

- I2C 주소 범위: `0x20` ~ `0x27`
- Waveshare 보드 기본 주소: `0x27`
- 16개 GPIO 제공: A 포트 8개, B 포트 8개
- `INTA`, `INTB`는 인터럽트 출력핀입니다. 이 프로젝트에서는 사용하지 않습니다.

### TCRT5000 3채널 라인센서

라인센서는 바닥의 흰색/검은색 반사 차이를 보고 디지털 값을 출력합니다.

이번 테스트 결과 기준:

```text
1 = 감지 안 됨
0 = 감지됨
```

예시:

```text
L:1 C:1 R:0  -> 오른쪽 센서 감지
L:1 C:0 R:0  -> 가운데+오른쪽 감지
L:0 C:1 R:1  -> 왼쪽 센서 감지
L:1 C:1 R:1  -> 셋 다 감지 안 됨
```

---

## 3. 최종 MCP23017 테스트 배선

먼저 모터는 연결하지 말고 MCP23017과 라인센서만 테스트합니다.

### ESP32-CAM-MB와 MCP23017

| ESP32-CAM-MB 핀 | MCP23017 핀 | 설명 |
|---|---|---|
| 성공 확인된 `GND` | `GND` | 공통 접지 |
| `VCC` 또는 `3V3` | `VCC` | MCP 전원 |
| `IO13` | `SDA` | I2C 데이터 |
| `IO14` | `SCL` | I2C 클럭 |
| 연결 안 함 | `INTA` | 사용 안 함 |
| 연결 안 함 | `INTB` | 사용 안 함 |

실제 테스트에서 MB 보드의 GND 위치에 따라 통신 성공 여부가 달라졌습니다.

강의에서는 이렇게 설명하면 됩니다.

- 이론적으로 같은 보드의 GND는 모두 같은 기준 전압이어야 합니다.
- 하지만 실제 키트에서는 점퍼 접촉, 커넥터 접촉, 보드 핀 위치 때문에 특정 GND에서만 안정적으로 동작하는 경우가 있습니다.
- 이번 실습에서는 `0x27: ACK`가 확인된 GND, 즉 IO0 근처에서 성공한 GND를 사용합니다.

### MCP23017과 라인센서

| 라인센서 핀 | MCP23017 핀 | 설명 |
|---|---|---|
| `VCC` | `VCC` | 센서 전원 |
| `GND` | `GND` | 센서 접지 |
| `L` | `PB0` | 왼쪽 센서 입력 |
| `C` | `PB1` | 가운데 센서 입력 |
| `R` | `PB2` | 오른쪽 센서 입력 |

주의:

- 라인센서의 `L/C/R`은 ESP32-CAM에 직접 연결하지 않습니다.
- 라인센서의 `L/C/R`은 MCP23017의 `PB0/PB1/PB2`에 연결합니다.
- MCP23017의 `INTA/INTB`는 연결하지 않습니다.

---

## 4. 전원 선택: 3.3V와 5V

가능하면 MCP23017은 ESP32-CAM의 I/O 전압과 같은 3.3V로 쓰는 것이 가장 보수적입니다.

권장 순서:

1. `ESP32-CAM-MB 3V3 -> MCP VCC`로 먼저 테스트합니다.
2. `0x27: ACK`가 안정적으로 나오면 3.3V를 유지합니다.
3. 3.3V에서 불안정하거나 전류가 부족해 보이면 `ESP32-CAM-MB VCC/5V -> MCP VCC`를 테스트합니다.

이번 실습에서는 5V에서도 변화가 없었고, 실제 원인은 전압보다 GND 연결 위치/접촉 문제였습니다.

주의:

- 배터리팩의 원전압을 MCP23017의 `VCC`에 직접 넣으면 안 됩니다.
- 2셀 배터리팩은 충전 상태에 따라 약 7.4V~8.4V가 될 수 있습니다.
- MCP23017 보드의 VCC에는 3.3V 또는 USB 5V 같은 규정된 전압만 넣어야 합니다.
- MCP를 5V로 구동하면 MCP GPIO 출력도 5V 기준이 될 수 있으므로, MCP 출력핀을 ESP32-CAM GPIO에 직접 연결하지 않는 것이 안전합니다.
- MCP 출력핀을 L298N 같은 모터 드라이버 입력으로 보내는 것은 일반적으로 가능합니다.

---

## 5. MCP23017 I2C 주소

MCP23017의 7비트 I2C 주소는 다음 형식입니다.

```text
0100 A2 A1 A0
```

주소 계산:

```text
0x20 + A2*4 + A1*2 + A0
```

Waveshare 보드는 기본 상태에서 `A2/A1/A0`가 모두 High라서 주소가 `0x27`입니다.

| A2 | A1 | A0 | 주소 |
|---:|---:|---:|---:|
| 0 | 0 | 0 | `0x20` |
| 0 | 0 | 1 | `0x21` |
| 0 | 1 | 0 | `0x22` |
| 0 | 1 | 1 | `0x23` |
| 1 | 0 | 0 | `0x24` |
| 1 | 0 | 1 | `0x25` |
| 1 | 1 | 0 | `0x26` |
| 1 | 1 | 1 | `0x27` |

정상 스캔 예시:

```text
Scanning 0x20-0x27...
  0x20: no
  0x21: no
  0x22: no
  0x23: no
  0x24: no
  0x25: no
  0x26: no
  0x27: ACK
```

---

## 6. 왜 IO13과 IO14를 쓰는가

ESP32-CAM은 핀 선택이 까다롭습니다.

이번 구성에서 I2C는 다음처럼 정했습니다.

```text
SDA = GPIO13
SCL = GPIO14
```

이유:

- 카메라가 이미 많은 GPIO를 사용합니다.
- `GPIO16`은 ESP32-CAM의 PSRAM과 충돌할 가능성이 있어 피합니다.
- `GPIO4`는 플래시 LED와 연결되어 있어 피합니다.
- `GPIO2`, `GPIO12`, `GPIO15`는 부팅 상태에 영향을 줄 수 있어 조심해야 합니다.
- `GPIO13`, `GPIO14`는 기존 모터선에 쓰이던 핀이지만, 모터선을 분리하면 I2C로 재사용할 수 있습니다.

주의:

- `GPIO13`, `GPIO14`는 microSD 관련 핀이기도 합니다.
- 이 프로젝트에서는 microSD를 사용하지 않는다는 전제에서 사용합니다.
- 기존 모터 드라이버 IN1~IN4가 IO13/IO14에 계속 연결되어 있으면 I2C 테스트가 실패할 수 있습니다.

---

## 7. 기존 모터 배선

원래 RC카 코드의 모터 드라이버 입력 배선은 다음과 같습니다.

| 모터 드라이버 | ESP32-CAM-MB |
|---|---|
| `IN1` | `IO14` |
| `IN2` | `IO15` |
| `IN3` | `IO13` |
| `IN4` | `IO12` |

MCP23017을 쓰려면 이 배선을 그대로 둘 수 없습니다.

이유:

- `IO13`, `IO14`를 I2C로 사용해야 합니다.
- 모터 드라이버가 IO13/IO14에 계속 붙어 있으면 I2C 신호를 방해할 수 있습니다.
- ESP32-CAM의 남는 핀이 부족하므로 모터 입력도 MCP23017로 옮기는 것이 구조적으로 맞습니다.

추후 권장 모터 배선:

| 모터 드라이버 | MCP23017 출력 예시 |
|---|---|
| `IN1` | `GPA0` |
| `IN2` | `GPA1` |
| `IN3` | `GPA2` |
| `IN4` | `GPA3` |

보드 실크가 `GPA0` 대신 `PA0`처럼 적혀 있을 수 있습니다.

같은 의미:

```text
GPA0 = PA0 = Port A bit 0
GPB0 = PB0 = Port B bit 0
```

---

## 8. 업로드 방법

Arduino IDE에서 업로드할 때는 다음을 확인합니다.

1. 보드 선택
   - `AI Thinker ESP32-CAM`

2. 포트 선택
   - 예: `COM5`
   - 포트는 USB를 다시 꽂으면 `COM3`, `COM5`처럼 바뀔 수 있습니다.

3. 업로드 모드
   - 업로드가 안 되면 `ESP32-CAM-MB IO0 -> ESP32-CAM-MB GND`를 같은 MB 보드 안에서 연결합니다.
   - 그 상태로 업로드합니다.
   - 업로드가 끝나면 `IO0-GND` 연결을 빼고 리셋하거나 전원을 다시 넣습니다.

중요:

- `IO0-GND`는 업로드 모드로 들어가기 위한 연결입니다.
- `MCP GND`를 `IO0`에 넣으라는 뜻이 아닙니다.
- `IO0`와 `GND`는 ESP32-CAM-MB 보드 안에서 임시로 연결하는 것입니다.

업로드 성공 로그 예시:

```text
Hash of data verified.
Leaving...
Hard resetting via RTS pin...
```

---

## 9. Serial Monitor와 Output 구분

Arduino IDE에는 주로 두 곳을 봅니다.

### Output

Output은 컴파일과 업로드 로그를 보는 곳입니다.

예시:

```text
Compiling sketch...
esptool.py v4.5.1
Serial port COM5
Writing at 0x00010000... (100 %)
Hash of data verified.
```

### Serial Monitor

Serial Monitor는 ESP32-CAM이 실행 중에 출력하는 로그를 보는 곳입니다.

MCP23017 테스트, 라인센서 값 확인은 Serial Monitor에서 봅니다.

설정:

```text
Baud rate = 115200
```

정상 예시:

```text
MCP23017 ready. Line sensors: L=PB0, C=PB1, R=PB2
Line raw L:1 C:1 R:1 GPIOB:0x27
Line raw L:1 C:1 R:0 GPIOB:0x23
Line raw L:0 C:1 R:1 GPIOB:0x26
```

---

## 10. 라인센서 값 해석

현재 테스트 기준으로 라인센서는 Active Low입니다.

```text
1 = 감지 안 됨
0 = 감지됨
```

센서 로그 예시:

| Serial Monitor 값 | 의미 |
|---|---|
| `L:1 C:1 R:1` | 아무 센서도 감지 안 함 |
| `L:0 C:1 R:1` | 왼쪽 센서 감지 |
| `L:1 C:0 R:1` | 가운데 센서 감지 |
| `L:1 C:1 R:0` | 오른쪽 센서 감지 |
| `L:0 C:0 R:0` | 세 센서 모두 감지 |

라인트레이싱 제어 예시:

| 센서 상태 | 차 동작 |
|---|---|
| `L:1 C:0 R:1` | 직진 |
| `L:0 C:1 R:1` | 왼쪽으로 보정 |
| `L:1 C:1 R:0` | 오른쪽으로 보정 |
| `L:0 C:0 R:1` | 왼쪽으로 강하게 보정 |
| `L:1 C:0 R:0` | 오른쪽으로 강하게 보정 |
| `L:1 C:1 R:1` | 선을 잃음. 정지 또는 마지막 방향으로 탐색 |
| `L:0 C:0 R:0` | 교차로 또는 넓은 검은 영역. 정책 필요 |

---

## 11. 문제 해결

### `MCP23017 not found on 0x20-0x27`

가능한 원인:

- MCP VCC가 연결되지 않음
- MCP GND가 ESP32-CAM-MB의 안정적인 GND와 연결되지 않음
- SDA/SCL이 반대로 연결됨
- SDA/SCL이 잘못된 GPIO에 연결됨
- 기존 모터 드라이버가 IO13/IO14에 남아 있어 I2C를 방해함
- 점퍼선 접촉 불량

확인 순서:

1. MCP와 라인센서 외의 모든 배선을 뺍니다.
2. `ESP32-CAM-MB 성공한 GND -> MCP GND`를 연결합니다.
3. `ESP32-CAM-MB VCC 또는 3V3 -> MCP VCC`를 연결합니다.
4. `ESP32-CAM-MB IO13 -> MCP SDA`를 연결합니다.
5. `ESP32-CAM-MB IO14 -> MCP SCL`을 연결합니다.
6. Serial Monitor에서 `0x27: ACK`가 나오는지 봅니다.

### `Idle SDA:1 SCL:1`인데 주소가 전부 `no`

SDA/SCL이 기본 HIGH라서 완전한 쇼트는 아닙니다.

이 경우는 주로 다음 문제입니다.

- MCP 전원이 실제로 들어오지 않음
- GND가 안정적으로 공유되지 않음
- SDA/SCL이 MCP의 SDA/SCL이 아닌 다른 핀에 연결됨
- MCP 주소 점퍼 상태가 예상과 다름

Waveshare 기본 주소는 `0x27`입니다. 스캐너가 `0x20`부터 `0x27`까지 모두 확인해야 합니다.

### 이상한 문자 출력

가능한 원인:

- Serial Monitor baud rate가 115200이 아님
- ESP32-CAM이 계속 리셋 중
- 전원 또는 GND 접촉이 불안정함
- SDA/SCL 연결이 실행 중 전원/GND에 영향을 주고 있음

조치:

1. Serial Monitor baud rate를 `115200`으로 맞춥니다.
2. MCP를 모두 빼고 ESP32-CAM 단독으로 정상 출력되는지 확인합니다.
3. GND, VCC, SDA, SCL 순서로 하나씩 다시 연결합니다.
4. `0x27: ACK`가 뜬 GND 위치를 그대로 사용합니다.

### `waiting for download`

ESP32-CAM이 업로드 모드에 들어간 상태입니다.

원인:

- `IO0-GND`가 연결되어 있음

조치:

1. 업로드가 끝났으면 `ESP32-CAM-MB IO0 -> ESP32-CAM-MB GND` 연결을 뺍니다.
2. 리셋 버튼을 누르거나 USB를 다시 연결합니다.

### 플래시 LED가 계속 켜짐

원인:

- `IO4`는 ESP32-CAM 플래시 LED와 연결되어 있습니다.
- `IO4`에 I2C SCL 같은 신호를 연결하면 LED가 켜질 수 있습니다.

조치:

- I2C에는 `IO4`를 쓰지 않습니다.
- 현재 권장 I2C는 `IO13=SDA`, `IO14=SCL`입니다.

---

## 12. 강의 진행 순서 추천

강의에서는 한 번에 전체 자율주행 코드를 올리지 말고 아래 순서로 진행하는 것이 좋습니다.

### 1단계: ESP32-CAM 업로드 확인

목표:

- Arduino IDE에서 ESP32-CAM에 코드가 올라가는지 확인합니다.

확인:

```text
Hash of data verified.
Leaving...
Hard resetting via RTS pin...
```

### 2단계: Serial Monitor 확인

목표:

- ESP32-CAM이 실행 로그를 출력하는지 확인합니다.

확인:

```text
MCP23017 bitbang I2C test
I2C SDA=GPIO13 SCL=GPIO14
```

### 3단계: MCP23017 I2C ACK 확인

목표:

- ESP32-CAM과 MCP23017의 I2C 통신을 확인합니다.

확인:

```text
0x27: ACK
MCP23017 ready
```

### 4단계: 라인센서 입력 확인

목표:

- `L/C/R`이 각각 0과 1로 변하는지 확인합니다.

확인:

```text
Line raw L:1 C:1 R:0
Line raw L:1 C:0 R:1
Line raw L:0 C:1 R:1
```

### 5단계: 모터 입력을 MCP23017로 이동

목표:

- ESP32-CAM의 부족한 GPIO 문제를 해결합니다.
- 모터 드라이버 IN1~IN4를 MCP23017 출력으로 제어합니다.

예시:

```text
MCP GPA0 -> 모터 드라이버 IN1
MCP GPA1 -> 모터 드라이버 IN2
MCP GPA2 -> 모터 드라이버 IN3
MCP GPA3 -> 모터 드라이버 IN4
```

### 6단계: 라인트레이싱 로직 추가

목표:

- 라인센서 값에 따라 직진, 좌회전, 우회전, 정지를 수행합니다.

기본 정책:

```text
C 감지 -> 직진
L 감지 -> 왼쪽 보정
R 감지 -> 오른쪽 보정
아무것도 감지 안 됨 -> 정지 또는 탐색
```

---

## 13. 현재 확인된 올바른 결론

- MCP23017의 기본 주소는 `0x27`입니다.
- `0x27: ACK`가 나오면 I2C 통신은 성공입니다.
- 이번 실습의 안정적인 I2C 핀은 `IO13=SDA`, `IO14=SCL`입니다.
- `IO16`은 ESP32-CAM에서 피하는 것이 좋습니다.
- `IO4`는 플래시 LED와 연결되어 있어 피하는 것이 좋습니다.
- `IO0-GND`는 업로드 모드용 임시 연결입니다.
- MCP의 `INTA/INTB`는 이번 프로젝트에서 연결하지 않습니다.
- 라인센서는 `PB0/PB1/PB2`에 연결합니다.
- 이번 라인센서 출력은 Active Low입니다.
- `1`은 감지 안 됨, `0`은 감지됨입니다.
- 기존 모터 입력선 `IN1~IN4`는 나중에 MCP 출력핀으로 옮기는 것이 맞습니다.
- 전원 문제보다 GND 접촉/위치 문제가 더 직접적인 원인이었습니다.

---

## 14. 참고 자료

- Waveshare MCP23017 IO Expansion Board Wiki  
  https://www.waveshare.com/wiki/MCP23017_IO_Expansion_Board

- Waveshare MCP23017 IO Expansion Board 제품 페이지  
  https://www.waveshare.com/mcp23017-io-expansion-board.htm

- Microchip MCP23017 Datasheet  
  https://ww1.microchip.com/downloads/aemDocuments/documents/APID/ProductDocuments/DataSheets/MCP23017-Data-Sheet-DS20001952.pdf

- Keyestudio KS5023 2WD Camera Robot Car 문서  
  https://docs.keyestudio.com/projects/KS5023/en/latest/docs/2WD%20Camera%20Robot%20Car.html

- ESP32-CAM AI Thinker Pinout 참고  
  https://randomnerdtutorials.com/esp32-cam-ai-thinker-pinout/

---

## 15. MCP23017 모터 제어 테스트

라인센서 입력이 확인되면 다음 단계는 모터 드라이버 `IN1~IN4`를 MCP23017 출력핀으로 옮기는 것입니다.

선이 부족하면 이 단계에서는 라인센서를 모두 빼고 모터만 테스트합니다. 라인센서 `VCC/GND/L/C/R`은 임시로 연결하지 않아도 됩니다.

### 모터 배선

기존에는 모터 드라이버 `IN1~IN4`가 ESP32-CAM-MB의 `IO14/IO15/IO13/IO12`에 연결되어 있었습니다.

MCP23017을 쓰는 최종 배선은 아래처럼 바꿉니다.

| 모터 드라이버 | MCP23017 핀 | 의미 |
|---|---|---|
| `IN1` | `PA0` 또는 `GPA0` | 오른쪽 모터 방향 입력 1 |
| `IN2` | `PA1` 또는 `GPA1` | 오른쪽 모터 방향 입력 2 |
| `IN3` | `PA2` 또는 `GPA2` | 왼쪽 모터 방향 입력 1 |
| `IN4` | `PA3` 또는 `GPA3` | 왼쪽 모터 방향 입력 2 |

센서 배선은 그대로 유지합니다.

| 라인센서 | MCP23017 |
|---|---|
| `L` | `PB0` |
| `C` | `PB1` |
| `R` | `PB2` |

주의:

- 모터 드라이버의 전원 배선은 기존 RC카 배선을 유지합니다.
- MCP23017은 모터에 전원을 공급하는 것이 아닙니다.
- MCP23017은 모터 드라이버의 `IN1~IN4` 신호만 켜고 끕니다.
- 모터 드라이버의 `ENA/ENB` 점퍼가 꽂혀 있으면 모터는 최대 속도 기준으로 켜지고 꺼집니다.

### 속도 제어

MCP23017만으로 정교한 속도 제어를 하는 것은 권장하지 않습니다.

이유:

- MCP23017은 PWM 전용 칩이 아니라 일반 GPIO 확장칩입니다.
- I2C로 계속 ON/OFF를 반복해서 소프트웨어 PWM을 만들 수는 있지만, 카메라/Wi-Fi와 같이 쓰면 타이밍이 흔들립니다.
- 라인트레이싱 수업에서는 속도 제어보다 안정적인 정지/직진/좌우 보정이 더 중요합니다.

따라서 이번 구성은 아래처럼 동작합니다.

| 동작 | 모터 제어 방식 |
|---|---|
| 직진 | 왼쪽 모터 ON, 오른쪽 모터 ON |
| 왼쪽 보정 | 오른쪽 모터 ON, 왼쪽 모터 STOP |
| 오른쪽 보정 | 왼쪽 모터 ON, 오른쪽 모터 STOP |
| 정지 | 왼쪽 모터 STOP, 오른쪽 모터 STOP |

### 테스트 코드 동작

모터 전용 테스트 스케치는 부팅 직후 모터를 움직이지 않습니다.

모터를 움직이려면 Arduino IDE의 **Serial Monitor**에서 명령을 보내야 합니다.

Serial Monitor 설정:

```text
Baud rate = 115200
```

명령:

| 명령 | 동작 |
|---|---|
| `t` | `PA0`, `PA1`, `PA2`, `PA3`를 하나씩 켜는 모터 배선 테스트 |
| `1` | `PA0 -> IN1`만 3초 켜기 |
| `2` | `PA1 -> IN2`만 3초 켜기 |
| `3` | `PA2 -> IN3`만 3초 켜기 |
| `4` | `PA3 -> IN4`만 3초 켜기 |
| `f` | 전진 패턴 3초 실행 |
| `b` | 후진 패턴 3초 실행 |
| `s` | 모터 정지 |

`t` 명령을 보내면 3초 뒤에 다음 순서로 테스트합니다.

```text
1. PA0 -> IN1만 켜기
2. 정지
3. PA1 -> IN2만 켜기
4. 정지
5. PA2 -> IN3만 켜기
6. 정지
7. PA3 -> IN4만 켜기
8. 정지
9. 전진 패턴 실행
10. 정지
11. 후진 패턴 실행
12. 정지
```

테스트 전에 차를 손으로 들거나 바퀴가 바닥에 닿지 않게 해야 합니다.

### 방향이 반대일 때

`t` 테스트에서 오른쪽 바퀴가 뒤로 돌면 오른쪽 모터 방향이 반대입니다.

해결 방법:

```text
오른쪽 모터가 반대로 돌면 IN1/IN2 방향을 바꿉니다.
왼쪽 모터가 반대로 돌면 IN3/IN4 방향을 바꿉니다.
```

코드 기준 현재 방향은 아래와 같습니다.

```text
오른쪽 전진: IN1=0, IN2=1
왼쪽 전진:   IN3=1, IN4=0
```

### 라인트레이싱 자동모드

`a` 명령을 보내면 라인센서 값에 따라 모터가 자동으로 움직입니다.

현재 정책:

| 센서 값 | 동작 |
|---|---|
| `L:1 C:0 R:1` | 직진 |
| `L:0 C:1 R:1` | 왼쪽 보정 |
| `L:1 C:1 R:0` | 오른쪽 보정 |
| `L:0 C:0 R:1` | 왼쪽 보정 |
| `L:1 C:0 R:0` | 오른쪽 보정 |
| `L:1 C:1 R:1` | 정지 |
| `L:0 C:0 R:0` | 정지 |

자동모드를 끄려면 Serial Monitor에서 `s`를 보냅니다.

---

## 16. 최종 절충 구조: 전진 PWM + MCP 센서/후진핀

ESP32-CAM에서 카메라를 유지하면서 속도 제어도 살리려면, 모든 모터 입력을 MCP로 보내는 대신 전진에 필요한 두 핀만 ESP32가 직접 PWM으로 제어하는 구성이 좋습니다.

이 구조에서는 라인트레이싱 주행에 필요한 전진 속도 제어가 살아납니다.

### 최종 역할 분담

| 기능 | 담당 |
|---|---|
| 오른쪽 전진 속도 | ESP32 `IO15 -> L298N IN2` |
| 왼쪽 전진 속도 | ESP32 `IO12 -> L298N IN3` |
| 오른쪽 후진 방향핀 | MCP `PA0 -> L298N IN1` |
| 왼쪽 후진 방향핀 | MCP `PA1 -> L298N IN4` |
| 라인센서 L/R/C | MCP `PB0/PB2/PB1` |
| I2C | ESP32 `IO13=SDA`, `IO14=SCL` |

### 최종 배선

```text
ESP32-CAM-MB IO13 -> MCP SDA
ESP32-CAM-MB IO14 -> MCP SCL
ESP32-CAM-MB 3V3  -> MCP VCC
ESP32-CAM-MB GND  -> MCP GND

MCP PA0 -> L298N IN1
ESP32-CAM-MB IO15 -> L298N IN2
ESP32-CAM-MB IO12 -> L298N IN3
MCP PA1 -> L298N IN4

라인센서 L -> MCP PB0
라인센서 C -> MCP PB1, 선택 사항
라인센서 R -> MCP PB2

배터리 + -> L298N +VDD / VIN / +12V
배터리 - -> L298N GND
L298N 5V -> ESP32-CAM-MB 5V/VCC
L298N GND -> ESP32-CAM-MB GND -> MCP GND
```

L298N의 `ENA/ENB` 점퍼는 그대로 꽂아둡니다. 속도 제어는 `IN2/IN3`에 PWM을 넣는 방식으로 합니다.

### 왜 이 구조를 쓰는가

원래 코드의 속도 제어는 `ENA/ENB`가 아니라 `IN1~IN4`에 `analogWrite()`를 걸어서 만들었습니다.

전진 기준:

```text
오른쪽 모터: IN1=0, IN2=PWM
왼쪽 모터:   IN3=PWM, IN4=0
```

따라서 전진 주행과 라인트레이싱 속도 제어에는 `IN2`, `IN3` 두 개의 ESP32 PWM 핀만 있으면 됩니다.

MCP는 다음을 담당합니다.

```text
IN1/IN4를 평소 LOW로 잡아줌
후진이 필요할 때 IN1/IN4를 HIGH로 만듦
라인센서 입력을 읽음
```

### 한계

이 구조에서 전진 속도 제어는 가능합니다.

다만 후진 속도 제어는 정교하지 않습니다. 후진은 MCP가 `IN1/IN4`를 HIGH/LOW로만 제어하기 때문입니다. 라인트레이싱에서는 보통 후진이 핵심이 아니므로 이 절충이 실용적입니다.

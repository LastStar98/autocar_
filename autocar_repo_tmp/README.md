# ESP32-CAM 2WD Robot Car

Keyestudio KS5023 ESP32-CAM 2WD Camera Robot Car 작업 기록입니다.

## 현재 상태

- Arduino IDE에서 ESP32-CAM 보드로 업로드 가능
- 보드 설정: `AI Thinker ESP32-CAM`
- 포트 예시: `COM8`
- ESP32 보드 패키지: `esp32 by Espressif Systems` 2.0.12 권장
- 휴대폰 2.4GHz 핫스팟에 연결 성공
- 브라우저 조종 페이지 동작
- 카메라 스트리밍 동작
- LED ON/OFF 동작
- 전진, 후진, 좌회전, 우회전, 정지 동작

## 접속 주소

자동차가 Wi-Fi에 연결되면 Arduino IDE Serial Monitor에 IP가 출력됩니다.

예시:

```text
http://10.187.205.100
```

카메라 스트림 직접 주소:

```text
http://10.187.205.100:81/stream
```

IP는 핫스팟이나 공유기가 새로 할당하면 바뀔 수 있습니다.

## 배선

ESP32-CAM 쪽 핀과 L298N 모터드라이버 연결:

```text
GPIO14 -> IN1
GPIO15 -> IN2
GPIO13 -> IN3
GPIO12 -> IN4
5V     -> 5V
GND    -> GND
```

모터 연결:

```text
오른쪽 모터 검정 -> OUT1
오른쪽 모터 빨강 -> OUT2
왼쪽 모터 검정   -> OUT3
왼쪽 모터 빨강   -> OUT4
```

배터리:

```text
배터리 빨강(+) -> L298N +VDD / +12V / VIN
배터리 검정(-) -> L298N GND
```

`ENA`, `ENB` 점퍼가 꽂혀 있어야 모터가 돕니다.

## 코드 수정 내용

실제 Arduino 업로드 파일:

```text
sketch_may9a/sketch_may9a.ino
```

반영한 수정:

- 카메라 상하 반전: `Video_Flip = true`
- 전진 시 왼쪽 모터 보정: `MOTOR_L_Forward_Trim = 5`
- 후진 시 왼쪽 모터 보정: `MOTOR_L_Backward_Trim = 1`
- Wi-Fi 이름/비밀번호는 `secrets.h`로 분리

## Wi-Fi 설정

GitHub에는 실제 Wi-Fi 정보가 올라가지 않도록 `secrets.h`는 `.gitignore`에 넣었습니다.

처음 받는 환경에서는 아래 파일을 복사해서:

```text
sketch_may9a/secrets.example.h
```

다음 이름으로 저장합니다:

```text
sketch_may9a/secrets.h
```

그리고 값을 수정합니다:

```cpp
#define WIFI_SSID "your_wifi_name"
#define WIFI_PASSWORD "your_wifi_password"
```

## 조종 명령

웹 UI가 내부적으로 호출하는 명령입니다.

```text
/action?go=forward
/action?go=backward
/action?go=left
/action?go=right
/action?go=stop
/action?go=plus
/action?go=minus
/action?led=on
/action?led=off
```

## 다음 목표

- PC에서 Python/OpenCV로 `:81/stream` 영상을 읽기
- 검은 선 라인트레이싱 구현
- PC가 `/action?go=...` 명령을 보내 자율주행 제어
- 폰에서 Auto ON/OFF를 누르면 PC 자율주행과 수동 조종을 전환하는 구조 추가

## 주의

- ESP32-CAM은 2.4GHz Wi-Fi만 사용합니다.
- 배터리를 켜기 전에는 바퀴를 띄운 상태로 테스트하는 것이 안전합니다.
- 집 Wi-Fi가 안 붙으면 휴대폰 핫스팟을 2.4GHz로 설정해 테스트합니다.

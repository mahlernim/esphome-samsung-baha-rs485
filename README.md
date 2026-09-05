# ESPHome Samsung BAHA RS485

삼성중공업 BAHA 월패드의 조명과 난방을 연결하는 ESPHome 외부 컴포넌트다. `BHWP-2711C/A` 한 설치에서 확인한 패킷을 사용하며, 자세한 근거는 [BAHA 패킷 지식베이스](https://github.com/mahlernim/baha-wallpad-packets)에 정리되어 있다.

**v0.1.1**은 난방 외출 온도가 64°C 높게 해석되던 오류를 수정한다. 예를 들어 `E0`는 96°C가 아닌 32°C, `CA`는 74°C가 아닌 10°C다. [변경 이력](CHANGELOG.md)

## 지원 범위

| 노드 | 엔티티와 기능 |
| --- | --- |
| `10 04` | 4채널 조명 상태 조회와 On/Off |
| `1F 0F` | 현관·일괄소등 상태 조회와 On/Off |
| `40 90` | 5개 존의 현재·목표 온도 센서와 난방 switch |

UART는 `9600 8N1`을 사용한다. 6채널 노드 `10 06`은 지원하지 않는다. 제조사 공식 통합이 아니며, 다른 모델·펌웨어·배선의 호환성은 별도로 확인해야 한다.

## 난방 switch의 동작

난방 switch는 현재 온도에 증감값을 더해 **일반 모드 목표 온도**를 요청한다. Home Assistant 자동화에서 이 요청을 사용할 수 있도록 switch 형태로 제공한다.

| 요청 | 계산 | 기본값 |
| --- | --- | --- |
| On | 현재 온도 + `on_delta` | +1°C |
| Off | 현재 온도 + `off_delta` | −2°C |

목표는 5~35°C로 제한된다. 예를 들어 현재 온도가 22°C이면 기본 On은 23°C, Off는 20°C를 요청한다. 외출 중에 요청하면 일반 모드로 전환하며, 이미 일반 모드에서 같은 목표를 사용 중이면 중복 쓰기를 생략한다.

Off는 외출 명령이나 릴레이 정지 명령이 아니다. 표시되는 switch 상태는 **목표 온도 > 현재 온도**인지 비교한 결과이며, 버너·밸브의 실제 동작 피드백은 아니다. 외출 설정용 엔티티는 제공하지 않는다.

## 설치와 업데이트

1. [예제 YAML](examples/baha-rs485.example.yaml)을 복사하고 보드·UART 핀을 배선에 맞게 설정한다.
2. [secrets 예시](examples/secrets.example.yaml)를 참고해 `secrets.yaml`에 Wi-Fi, API, OTA 값을 입력한다.
3. `zone1`~`zone5`의 엔티티 이름과 난방 증감값을 설정한다.
4. ESPHome에서 설정을 검증하고 펌웨어를 빌드·설치한다.

배포 버전을 고정하려면 다음과 같이 지정한다.

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/mahlernim/esphome-samsung-baha-rs485
      ref: v0.1.1
    components: [baha_rs485]
```

기존 설정도 `ref: v0.1.1`로 바꿔 빌드·설치하면 수정이 적용된다. GitHub 릴리스만으로 장치 펌웨어가 자동 갱신되지는 않는다. `main`을 사용하는 경우 외부 컴포넌트 캐시 갱신 시점은 [ESPHome 안내](https://esphome.io/components/external_components/#refresh)를 참고한다.

## 개발과 검증

로컬 수정본은 `external_components`의 `type: local` 소스로 확인할 수 있다. 컴포넌트 코드는 [`components/baha_rs485`](components/baha_rs485)에 있다.

회귀 테스트는 Python 3.10 이상과 C++17 컴파일러로 실행한다. Windows에서는 Visual Studio 개발자 셸을 사용한다.

```sh
python tests/run_tests.py
```

테스트는 실제 파서·디코더·송신 큐에 기록된 패킷과 경계 사례를 입력한다. ESPHome의 센서·UART 인터페이스는 호스트용 대역을 사용하며, 장치에 연결하지 않는다. 펌웨어 빌드와 실제 배선·장치 시험은 별도 검증이다.

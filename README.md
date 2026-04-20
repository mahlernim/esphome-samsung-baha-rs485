# ESPHome Samsung BAHA RS485

삼성중공업 BAHA 월패드 계열 RS485를 ESPHome 외부 컴포넌트로 붙이기 위한 저장소임.

패킷 해석과 실측 근거는 별도 저장소에 정리해 두었음.

- 패킷 문서 저장소: [baha-wallpad-packets](https://github.com/mahlernim/baha-wallpad-packets)

현재 코드는 `BHWP-2711C/A` 1개 설치 환경 기준으로 검증한 상태임. 제조사 공식 통합이 아니며, 같은 BAHA 계열이라도 펌웨어나 배선에 따라 다를 수 있음.

## 현재 지원 범위

- 조명 노드 `10 04` 4채널 상태 조회와 on/off 제어
- 현관 / 일괄소등 노드 `1F 0F` 상태 조회와 on/off 제어
- 난방 노드 `40 90` 5개 존 현재 온도 조회
- 난방 노드 `40 90` 5개 존 목표 온도 조회
- 난방 on/off용 `switch` 엔티티

## 왜 `climate`가 아닌 `switch`인가

실사용 기준으로는 `climate`보다 `switch`가 더 맞았음.

- `climate`처럼 목표 온도를 올려 두면 방 컨트롤러 온도가 변할 때까지 난방이 너무 오래 지속됐음.
- 경우에 따라 몇 시간 단위로 계속 돌아 비효율적이었음.
- 실제 운용은 "짧게 켜고 쉬었다가 다시 켜는" 식의 자동화가 훨씬 현실적이었음.
- 예를 들면 5분 켜고 10분 쉬는 식의 제어가 체감상 더 나았음.
- 그래서 온도조절기형 인터페이스보다 Home Assistant 자동화에 바로 물리기 쉬운 `switch` 형태를 택했음.

이 `switch`는 절대적인 릴레이 on/off가 아님.

- `On`이면 현재 온도보다 `on_delta`만큼 높은 목표 온도를 씀.
- `Off`이면 현재 온도보다 `off_delta`만큼 낮은 목표 온도를 씀.
- 예제 기본값 `on_delta: 1`은 "현재 온도 +1도"를 뜻함.
- 예제 기본값 `off_delta: -2`는 "현재 온도 -2도"를 뜻함.

즉 예제 기준으로는 `On`일 때 현재 온도보다 1도 높게 설정하고, `Off`일 때 현재 온도보다 2도 낮게 설정하는 방식임.

`on_delta`, `off_delta` 값은 집마다 다를 수 있음.

## 존 이름

예제와 스키마는 `zone1`부터 `zone5`까지 중립적인 이름만 사용함. 실제 방 이름은 각자 ESPHome 쪽 entity name에서 바꾸면 됨.

## 설치

가장 단순한 방법은 예제 YAML을 복사해서 자신의 노드 설정에 맞게 고치는 방식임.

1. [`examples/baha-rs485.example.yaml`](examples/baha-rs485.example.yaml)을 복사함
2. 보드 종류와 UART 핀을 실제 배선에 맞게 바꿈
3. `secrets.yaml`을 만들어 Wi-Fi, API, OTA 값을 채움
4. 필요하면 엔티티 이름과 `on_delta`, `off_delta` 값을 자기 집 기준으로 바꿈

예제는 이 저장소를 GitHub external component 소스로 참조하도록 잡아 두었음.

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/mahlernim/esphome-samsung-baha-rs485
      ref: main
    components: [baha_rs485]
```

로컬에서 직접 클론해 테스트할 때는 `type: local`로 바꿔도 됨.

## 파일 구성

- [`components/baha_rs485`](components/baha_rs485): 외부 컴포넌트 본체
- [`examples/baha-rs485.example.yaml`](examples/baha-rs485.example.yaml): 공개용 예제 설정
- [`examples/secrets.example.yaml`](examples/secrets.example.yaml): `secrets.yaml` 예시

## 검증 범위

- 검증 하드웨어: `BHWP-2711C/A`
- UART: `9600 8N1`
- 검증 환경: 실거주 1개 설치 환경

다른 설치 환경에서도 출발점으로는 쓸 수 있겠지만, 그대로 동작한다고 가정하면 안 됨.

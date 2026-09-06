# 보안 공격 시뮬레이션 테스트

## 현재 적용 범위

`FRPGSecurityValidationMath`는 월드와 액터에 의존하지 않는 서버 판정 계층이다. 런타임의 `RPGSecurityValidationComponent`, Skill Targeting 정책, 보안 BP Library가 이 판정을 공유하므로 테스트와 실제 서버 로직이 분리되지 않는다.

현재 자동화한 공격 시나리오:

- 동일 Ability 최소 간격을 위반하는 연사 요청
- 시간 창의 최대 활성화 횟수를 넘는 Ability 폭주
- 스킬 사거리를 벗어난 서버 Hit
- 대상 서버 Bounds와 일치하지 않는 위조 피격점
- 스킬 또는 전역 상한을 넘거나 유한하지 않은 피해량
- 허용 거리, 서버 조준 방향 또는 유한 좌표 규칙을 위반하는 TargetData
- 지상 및 공중 속도핵과 승인되지 않은 이동 불연속

연사와 활성화 폭주가 차단되면 `AbilityActivationRate` 위반 이벤트도 서버 위험 점수에 반영된다.

## 실행 방법

에디터가 프로젝트 DLL을 사용 중이지 않은 상태에서 Editor 타깃을 빌드한 후 다음 필터를 실행한다.

```text
Automation RunTests ProjectRPG.Security.
```

전체 회귀 필터는 다음과 같다.

```text
Automation RunTests ProjectRPG.
```

최근 검증 결과:

- 보안 테스트: 17 성공, 테스트 경고 0, 실패 0
- 전체 회귀 테스트: 53개 실행, 실패 0
- 텔레메트리 API 스모크: 인증, 멱등 재전송, 변조 충돌, 종료 후 감사 이벤트,
  보안 전용 유예 토큰 격리, 세션 집계 성공

텔레메트리 API와 BP 연결 방법은
[`Backend/SECURITY_TELEMETRY_API.md`](../Backend/SECURITY_TELEMETRY_API.md)를 참고한다.

## 다음 테스트 계층

현재 테스트는 서버 판정 자체를 결정론적으로 검증한다. 출시 전에는 별도 서버·클라이언트 프로세스로 다음 항목을 추가해야 한다.

- RPC 재전송, 순서 변경, 중복 전송
- 인위적 패킷 지연·손실·지터에서의 오탐 측정
- 여러 클라이언트가 동시에 제출하는 TargetData와 Ability 활성화
- 세션 재접속과 맵 전환 전후의 위험 점수·감사 이벤트 수명주기
- 장시간 부하에서 활성화 기록과 텔레메트리 큐의 메모리 상한

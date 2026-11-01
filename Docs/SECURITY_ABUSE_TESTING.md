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
- 사망/유효하지 않은 전투 상태에서의 피해 실행, 시체 재타격과 파괴된 대상의 이전 Hit 재사용

연사와 활성화 폭주가 차단되면 `AbilityActivationRate` 위반 이벤트도 서버 위험 점수에 반영된다.

## 실행 방법

에디터가 프로젝트 DLL을 사용 중이지 않은 상태에서 Editor 타깃을 빌드한 후 다음 필터를 실행한다.

```text
Automation RunTests ProjectRPG.Security.
```

명령줄에서 전체 보안 테스트와 필수 회귀 테스트의 실행 여부를 함께 확인하려면
다음 스크립트를 실행한다. 결과와 로그는 `Saved/Tests/Security/Automation-{id}`에
남는다. 종료 코드가 0이어도 테스트가 실행되지 않았거나 실패한 경우에는 실패로
처리하며, 오래된 DLL에 신규 큐 포화/샘플 간격 테스트가 빠진 경우도 감지한다.

```powershell
Scripts/Tests/Run-SecurityAutomationTest.ps1
```

전체 회귀 필터는 다음과 같다.

```text
Automation RunTests ProjectRPG.
```

이전 검증 결과:

- 보안 테스트: 17 성공, 테스트 경고 0, 실패 0
- 전체 회귀 테스트: 53개 실행, 실패 0
- 텔레메트리 API 스모크: 인증, 멱등 재전송, 변조 충돌, 종료 후 감사 이벤트,
  보안 전용 유예 토큰 격리, 세션 집계 성공
- 실제 네트워크 E2E: 소비/장착/해제 후 Restricted 상태의 추가 소비 차단,
  `InvalidDamage`, `EnforcementStateChanged`, `RestrictedActionAttempt` 저장 성공
- 서버 재시작 복구 E2E: 백엔드 승인 전 JSONL 3건 보존, 재기동 후 정확히 3건 저장,
  승인된 스풀 삭제 성공
- PostgreSQL 장애 복구 E2E: 임시 DB 정지 중 HTTP 500에 2초/4초 백오프,
  JSONL 3건 보존, UE 서버 재시작 없이 복구, 동일 배치 재전송 시 중복 3건 처리 성공
- HTTP 연결 중단/승인 응답 유실 E2E: 연결 오류에 2초/4초 백오프, PostgreSQL COMMIT
  직후 응답 연결 차단, 미승인 JSONL 보존, 8초 뒤 UE 자동 재시도의 중복 3건 승인,
  스풀 삭제 및 총 저장 건수 3 유지 성공
- HTTP 장애 프록시 단독 테스트: 실제 loopback HTTP 연결에서 제어 API 인증,
  연결 차단, 저장 이후 응답 차단, 복구 후 중복 승인 검증 성공
- 텔레메트리 단독 자동화: 큐/JSON 계약과 신규 이벤트의 전송·재시도 타이머 보존을
  포함한 4개 성공. 타이머 검증용 Game Instance 초기화 중 기존 VFX 자산의
  `ApexDestruction` 의존성 경고가 기록되었으나 테스트 실패는 없음

2026-10-01에는 Game/Editor Development 빌드, PostgreSQL 기본 통합, DB 장애 복구,
UE 서버 재시작 복구를 확인했다. 이후 추가한 타이머 보존 수정은 양쪽 빌드와 위 4개
자동화 테스트를 통과했다. Docker 재실행 및 최신 Game Development 재빌드 후,
해당 수정까지 포함한 최종 DB 장애 E2E와 HTTP 연결/승인 응답 유실 E2E도 통과했다.

최종 실제 프로세스 검증 로그:

- DB 장애 복구: `Saved/Logs/ItemBackendE2E/20261001-235122`
- HTTP 연결/승인 응답 유실: `Saved/Logs/ItemBackendE2E/20261001-235211`
  (`Telemetry.http-proof.json`, `Telemetry.unacknowledged.jsonl` 포함)

두 E2E 모두 인증된 UE 클라이언트의 소비/장착/해제, Restricted 추가 소비 거절,
아이템 수량/리비전 보존과 던전 정상 정산을 함께 확인했다. 테스트 소유 프로세스와
임시 PostgreSQL 컨테이너는 종료 시 정리하고 기존 Compose DB는 사용하지 않았다.

텔레메트리 API와 BP 연결 방법은
[`Backend/SECURITY_TELEMETRY_API.md`](../Backend/SECURITY_TELEMETRY_API.md)를 참고한다.

2026-10-03 큐 포화 보강 검증에서는 Editor Development 빌드와 보안 테스트 20개가
성공했다. 신규 테스트 2개는 Critical 증거/전송 중 배치 보존 및 서버 샘플 간격을
검증한다. 로그는
`Saved/Tests/Security/Automation-b6337b2552be412fab6bac34dbc1ac8a/automation.log`에
있다. 타이머 테스트에서 Game Instance 초기화 중 기존 VFX 자산의
`ApexDestruction` 참조 경고가 발생했으며 테스트 실패는 없었다. 이 실행에는
실제 패킷 지연·손실 또는 PostgreSQL 장애 시나리오를 포함하지 않았다.

## 실제 이동 패킷 지연·손실 검증

Editor Development 빌드 후 다음 명령으로 별도 dedicated server와 클라이언트를
실행한다. `-Runtime Editor`는 프로젝트의 Player BP와 입력 자산을 직접 사용하며
별도의 Cook은 필요하지 않다. 기본값은 지연/손실 없는 기준 시나리오다.

```powershell
Scripts/Network/Run-IrisMultiClientTest.ps1 `
  -SkipBuild -Runtime Editor -ClientCount 1 -TimeoutSeconds 180 `
  -VerifyMovementSecurity

Scripts/Network/Run-IrisMultiClientTest.ps1 `
  -SkipBuild -Runtime Editor -ClientCount 1 -TimeoutSeconds 180 `
  -VerifyMovementSecurity -PacketLagMinMs 100 -PacketLagMaxMs 200 -PacketLossPercent 5
```

지연은 각 방향의 송신 패킷에 설정된다. 위 예는 각 방향 100~200ms이며 왕복
100~200ms라는 뜻은 아니다. 손실률 역시 각 방향에 적용하는 설정값이며, 짧은
실행에서 실제 손실 비율이 정확히 그 값이 되는 것은 아니다.

Development 전용 프로브는 프로젝트의 `InputTag.Move` 입력 액션에 원형 이동
입력을 주입한다. 입력 바인딩, 서버/클라이언트의 실제 Iris 및 NetDriver 설정,
서버의 이동 보안 활성화를 확인한다. Spawn grace 이후 정상 이동 12초 동안 서버
관측 거리 1500cm 이상, 관측 샘플 60개 이상, 절반 이상 지상 이동 및 이동 위반
이벤트 0건을 요구한다. 마지막에 서버 위치를 승인 없이 10000cm 이동시켜 실제
보안 컴포넌트가 `MovementDiscontinuity` 이벤트를 내야 통과한다. 이 양성 대조는
판정기가 꺼져 있어 오탐이 0인 경우를 배제하기 위한 것이며, 변조된 클라이언트
패킷으로 속도핵을 실행하는 시나리오는 별도다.

결과는 `Saved/Logs/IrisMultiClient/{timestamp}/Movement.proof.json` 및 양쪽 로그에
남는다. 실행 중 위반이 발견되면 허용치를 자동으로 완화하지 않고 실패로 처리한다.
이 검증은 짧은 지상 이동의 회귀 테스트이며 전체 스킬/대시/점프 조합의 오탐률을
대표하지 않는다. 스킬 입력 프로브와는 따로 실행해야 한다. 로컬 테스트는
기존 `RPGNetTestMode`의 Development 전용 입장 우회를 사용하므로 백엔드 인증
경로는 기존 아이템/텔레메트리 E2E에서 검증한다.

2026-10-03 Editor Development 빌드 및 단일 클라이언트 실측 결과:

| 각 방향 송신 지연/손실 설정 | 서버 관측 시간 | 이동 거리 | 관측 샘플 | 정상 이동 위반 보고 |
| --- | --- | --- | --- | --- |
| 0ms / 0% | 12.01초 | 4834.6cm | 121 | 0 |
| 100~200ms / 5% | 12.10초 | 4804.7cm | 122 | 0 |
| 250~450ms / 10% | 12.00초 | 4768.7cm | 121 | 0 |

세 실행 모두 `IA_Move` 바인딩과 양쪽 패킷 설정을 확인했으며, 마지막 비정상
순간이동을 High 등급 `MovementDiscontinuity`로 감지했다. 서버 보안 허용치는
기존 정책을 그대로 사용했다. 실행별 로그 및 JSON 증거 경로는 다음과 같다.

- 기준: `Saved/Logs/IrisMultiClient/20261003-005036`
- 100~200ms/5%: `Saved/Logs/IrisMultiClient/20261003-005138`
- 250~450ms/10%: `Saved/Logs/IrisMultiClient/20261003-005240`

## 승인 이동과 BP 호출 경로

```powershell
Scripts/Network/Run-IrisMultiClientTest.ps1 `
  -SkipBuild -Runtime Editor -ClientCount 1 -TimeoutSeconds 180 `
  -VerifyAuthorizedMovement -PacketLagMinMs 250 -PacketLagMaxMs 450 -PacketLossPercent 10
```

이 옵션은 기본 지상 이동 검증 후 다음 단계를 추가한다.

- 클라이언트가 BP 함수 디스패치로 이동 승인을 요청해도 무효 GUID를 받는지 확인한다.
- 클라이언트 `Jump`가 실제 서버에서 상승·착지하고 이동 면제 없이 위반 보고가 없는지 확인한다.
- 실제 `Authorized Movement Window` AbilityTask의 `On Authorized`에서 서버를 700cm 이동시킨다. 거리 예산 소모와 태스크 취소 시 회수를 확인한다.
- 서버 BP 함수 디스패치로 승인을 열고 `LaunchCharacter` 넉백 및 승인 만료를 확인한다.
- 같은 `Reason`의 태스크 두 개를 겹친 뒤 이전 태스크만 취소해도 최신 승인이 남는지, 이후 자동 만료가 발생하는지 확인한다. 잘못된 핸들/기간도 최신 승인을 변경하지 않아야 한다.
- `On Authorized`에서 즉시 태스크를 취소하는 동작을 확인한다.
- 활성 승인 예산이 200cm일 때 10000cm 이동하면 실제 컴포넌트가 위반을 보고해야 한다.

서버/클라이언트는 프로젝트 Player BP를 사용한다. 테스트 Ability는 서버에서만
부여하는 Development fixture이며 기존 스킬 BP 그래프, 몽타주/Root Motion 연출
또는 실제 ShieldBash 공격 판정을 실행하는 것은 아니다. BP 함수의 UFunction
메타데이터 및 디스패치, BP에 노출된 실제 AbilityTask 수명, 서버 이동 검증과
클라이언트 점프 복제를 검증한다. 기존 스킬 그래프별 연출 검증은 별도다.
`Movement.proof.json`의 `authorizedMovementChecksVerified`와 양쪽 단계별 로그로
완료 여부를 확인한다. BP 연결 방법은 [BP 통합 가이드](BP_SECURITY_INTEGRATION.md)를
참고한다.

2026-10-04에는 Editor Development 빌드 결과를 바탕으로 단일 클라이언트에서
기준 조건과 각 방향 250~450ms 송신 지연/10% 손실 조건을 모두 통과했다.
두 실행 모두 정상 이동 및 승인 이동 중 위반 보고 0건, 클라이언트 승인 거절,
동일 이름의 태스크 중첩, 조기 취소, 자동 만료와 콜백 안의 즉시 취소를 확인했다.
200cm 추가 예산을 승인한 상태의 10000cm 이동은 실제 `MovementDiscontinuity`
High 위반으로 감지했다. 서버 정책 허용치는 완화하지 않았다.

- 기준: 정상 이동 12.09초/4859.2cm/122샘플, 점프 높이 88.9cm,
  대시 후 남은 추가 예산 329.1cm, 넉백 최대 평면 변위 617.6cm.
  로그: `Saved/Logs/IrisMultiClient/20261004-010406`.
- 250~450ms/10%: 정상 이동 12.10초/4796.4cm/122샘플, 점프 높이 89.6cm,
  대시 후 남은 추가 예산 325.0cm, 넉백 최대 평면 변위 612.1cm.
  로그: `Saved/Logs/IrisMultiClient/20261004-010609`.

넉백 변위에는 정상 이동도 포함되므로 추가 예산 600cm와 일대일 비교하지 않는다.
각 로그 폴더의 `Movement.proof.json`에서 승인 이동 검증 완료를 확인할 수 있다.
동일 빌드의 기존 보안 자동화 20개도 모두 성공했으며 로그는
`Saved/Tests/Security/Automation-a9678531d874412c96a1c672071a53b3/automation.log`에 있다.
테스트 소유 서버·클라이언트와 자동화 프로세스는 종료 후 정리했다.

## 실제 구르기 BP 그래프 검증

```powershell
Scripts/Network/Run-IrisMultiClientTest.ps1 `
  -SkipBuild -Runtime Editor -ClientCount 1 -TimeoutSeconds 300 `
  -VerifyAuthoredRoll

Scripts/Network/Run-IrisMultiClientTest.ps1 `
  -SkipBuild -Runtime Editor -ClientCount 1 -TimeoutSeconds 300 `
  -VerifyAuthoredRoll -PacketLagMinMs 250 -PacketLagMaxMs 450 -PacketLossPercent 10
```

기본 정상 이동 관측 후 실제 `IA_Roll` Enhanced Input을 두 번 주입한다.
테스트용 이동 Ability를 사용하지 않고 저장된 `GA_Player_Roll` BP 그래프와
`AM_Roll`을 실행한다. 기본 로드아웃에 이 Ability가 없는 경우에만 서버에서 실제
BP Ability와 `InputTag.Roll`을 부여한다. 물리 키보드 입력이나 로드아웃 획득은
검증 범위가 아니다. 일반 이동 중 조작 방향을 제공해 기존 Motion Warping
계산도 실행한다.

첫 구르기는 자연 종료, 두 번째는 실제 서버의 `CancelAbilityHandle`로 중단한다.
양쪽에서 몽타주 재생과 실제 Ability 종료를 관찰하고 서버에서 `PlayerRoll` 승인,
Root Motion 재생, 정상 구르기의 100~700cm 평면 이동 및 종료 후 승인 회수를
요구한다. 승인과 종료 상태를 테스트 코드가 대신 설정하지 않는다. 마지막에는
기존 비정상 순간이동 양성 대조를 실행한다. 완료 시
`Movement.proof.json`의 `authoredRollGraphVerified`가 true여야 한다.
일반 승인 fixture 옵션인 `-VerifyAuthorizedMovement`와는 별도로 실행한다.

서버의 두 번째 종료는 반드시 취소로 보고돼야 한다. 높은 RTT에서는 클라이언트의
예측 몽타주가 서버 취소 도착 전에 자연 종료될 수 있으므로, 클라이언트는 실제
몽타주 재생과 두 번의 종료/비활성 상태를 요구하고 로컬 취소 여부를 로그에 남긴다.
클라이언트의 종료 보고만으로 서버 취소·승인 회수 성공을 판정하지 않는다.
구르기 BP는 `Local Predicted`와 `Server Only Termination`을 함께 사용한다.
클라이언트의 예측 완료가 지연된 활성화 RPC와 함께 도착하더라도 서버 몽타주를
건너뛰거나 승인 태스크 수명을 단축하지 않아야 한다. 이 정책은 구르기에만 적용한다.

BP 재생 길이/목표 거리/연결의 편집기 검사는
`Scripts/Editor/ConfigurePlayerRollSecurity.py`로 재현한다. 기본 모드는 자산을
저장하지 않으며, BP 컴파일과 실제 저장된 연결을 검사한다.

2026-10-04 Editor Development 빌드와 단일 클라이언트의 실제 구르기 BP 검증은
기준 조건과 각 방향 250~450ms 송신 지연/10% 손실 조건에서 모두 통과했다.
두 실행 모두 정상 구르기와 서버 취소를 구분했고, 종료 후 이동 승인이 남지 않았다.
정상 이동 및 구르기의 위반 보고는 0건이며, 종료 후 승인 없는 10000cm 이동은
`MovementDiscontinuity` High 위반으로 감지했다. 허용치 정책은 완화하지 않았다.

- 기준: 정상 이동 12.10초/4885.2cm/122샘플, 정상 구르기 387.8cm,
  서버 취소 구르기 275.2cm. 로그: `Saved/Logs/IrisMultiClient/20261004-220130`.
- 250~450ms/10%: 정상 이동 12.00초/4820.0cm/121샘플, 정상 구르기 369.7cm,
  서버 취소 구르기 259.2cm. 로그: `Saved/Logs/IrisMultiClient/20261004-220031`.

각 폴더에 `authoredRollGraphVerified=true`인 `Movement.proof.json`이 있다.
지연 조건의 클라이언트는 두 번째 구르기도 로컬에서 자연 종료했지만, 서버 로그는
두 번째 종료의 `Cancelled=1`과 `AuthorizationRevoked=1`을 별도로 확인했다.
이번 구르기의 샘플별 변위는 기본 거리 허용치 안에 있어 추가 예산 차감은 0이었다.
구르기 총 이동 거리를 추가 예산 소모량으로 해석하지 않는다. 추가 예산 소모와
예산 초과 검사는 별도 `-VerifyAuthorizedMovement` 시나리오가 담당한다.

적용 과정에서 독립 보안 틱의 Root Motion 상태 조회, 몽타주 중단의 정상 종료
오분류, 지연된 클라이언트 종료가 서버 몽타주를 조기에 끝내는 경로를 보강했다.
최종 자산은 새 편집기 프로세스에서 저장 없이 재컴파일/연결/서버 종료 정책을
검증했다. 로그: `Saved/Logs/PlayerRollServerTerminationVerify.log`.

동일 최종 빌드에서 기존 승인 이동 fixture도 각 방향 250~450ms/10% 조건으로
재실행해 통과했다. 정상 이동 12.10초/4755.9cm/122샘플과 위반 보고 0건,
점프·대시·넉백·중첩/즉시 취소·클라이언트 권한 거절을 확인했다. 200cm 활성 예산을
넘는 10000cm 이동도 감지했다. 로그 및 JSON: `Saved/Logs/IrisMultiClient/20261004-220238`.
보안 자동화 20개도 모두 통과했다. 로그:
`Saved/Tests/Security/Automation-be630161a76a4d84bead83ccac23b3e0/automation.log`.
테스트 소유 프로세스는 종료 후 정리했다. 이번 검증은 Editor Development/단일
클라이언트 범위이며, Cook/Shipping 패키지나 변조된 클라이언트 RPC를 검증한 것은 아니다.

## 다중 플레이어 구르기 승인 격리

```powershell
Scripts/Network/Run-IrisMultiClientTest.ps1 `
  -SkipBuild -Runtime Editor -ClientCount 2 -TimeoutSeconds 300 `
  -VerifyAuthoredRoll

Scripts/Network/Run-IrisMultiClientTest.ps1 `
  -SkipBuild -Runtime Editor -ClientCount 4 -TimeoutSeconds 300 `
  -VerifyAuthoredRoll -PacketLagMinMs 250 -PacketLagMaxMs 450 -PacketLossPercent 10

Scripts/Tests/Test-MovementSecurityProof.ps1
```

다중 클라이언트 옵션에서는 모두의 실제 입력 준비를 기다린 뒤 서버가 공통 시작
시각을 정한다. 각 소유 클라이언트는 GameState의 서버 시각을 기준으로 일반 이동과
두 번의 `IA_Roll` 입력을 주입한다. 테스트용 필드는 소유자에게만 복제하며,
PlayerController의 테스트 컴포넌트는 Shipping에서 생성하지 않는다. Iris의
CDO/인스턴스 프로토콜이 일치하도록 복제 기본값을 생성자에서 고정하고,
게임 도중 컴포넌트 복제 여부를 바꾸지 않는다. 테스트 opt-in이 없으면 입력 주입,
준비 RPC 처리와 시작 시각 설정은 실행하지 않는다.

서버는 실제 승인과 Root Motion이 겹친 플레이어 수를 관찰한다. 자기 Ability를
취소하기 직전에 활성 상태인 다른 플레이어를 기록하고, 취소 직후 그 플레이어의
Ability 활성 상태·승인 Reason·남은 예산이 변하지 않았는지 비교한다. 활성 상대가
없는 경우를 격리 검사 성공으로 세지 않는다. 다중 실행은 최소 두 플레이어의 실제
겹침과 최소 한 건의 활성 상대 보존 증거가 있어야 통과하며, 전원 동시 실행을
보장했다고 해석하지 않고 실제 최대 겹침 수를 JSON에 기록한다.
손실로 활성화 RPC 도착이 흩어질 수 있으므로 다인 테스트의 두 번째 취소는
아직 누구도 격리 증거를 얻지 못했고 활성 상대가 없을 때 시작 후 최대 0.4초까지만
기다린다. 실제 보존 증거를 한 번 확보한 뒤에는 나머지 플레이어가 취소된 상대를
기다리지 않는다. Ability·몽타주·이동 승인 수명은 연장하지 않는다. 그때도 겹침
증거를 얻지 못하면 성공으로 처리하지 않는다.

`MovementSecurityProof.ps1`은 각 PlayerId의 서버 측 정상 이동 1회, 구르기 2회,
종료 및 순간이동 양성 대조를 각 클라이언트 로그와 대조한다. 중복 ID·누락된 ID·
중복 구르기 실행·잘못된 취소 결과·없는 몽타주 재생·겹침/격리 증거 누락·엔진
Ensure를 거부한다. 예전 바이너리의 식별자 없는 로그도 성공으로 간주하지 않는다.
동일 플레이어 쌍의 보존 로그를 중복 기록해 격리 검사 수를 부풀리는 것도 거부한다.
새 JSON에는 `playerIdentityCoverageVerified`, `serverRollMeasurements`,
`clientRollMeasurements`, `maximumConcurrentRollPlayers`,
`peerCancellationIsolationChecks`가 추가된다.

2026-10-04~05 Editor Development 실행에서 다음 증거를 확보했다.

| 조건 | 결과 | 실제 최대 동시 구르기 | 활성 상대 승인 보존 | 로그 폴더 |
| --- | --- | --- | --- | --- |
| 2인, 지연/손실 없음 | 통과 | 2명 | 1건 | `Saved/Logs/IrisMultiClient/20261004-223627` |
| 4인, 지연/손실 없음 | 통과 | 4명 | 6건 | `Saved/Logs/IrisMultiClient/20261005-025319` |
| 4인, 각 방향 250~450ms/10%, 취소 대기 보강 전 | 격리 증거 부족으로 실패 | 3명 | 0건 | `Saved/Logs/IrisMultiClient/20261005-025932` |
| 4인, 각 방향 250~450ms/10%, 취소 대기 보강 후 | 통과 | 2명 | 3건 | `Saved/Logs/IrisMultiClient/20261005-034816` |
| 4인, 지연/손실 없음, 첫 대기 보강 | 마지막 플레이어 자연 종료로 실패 | 4명 | 6건 | `Saved/Logs/IrisMultiClient/20261005-034931` |
| 4인, 지연/손실 없음, 최종 대기 보강 | 통과 | 4명 | 6건 | `Saved/Logs/IrisMultiClient/20261005-035346` |
| 4인, 각 방향 250~450ms/10%, 최종 대기 보강 | 통과 | 4명 | 5건 | `Saved/Logs/IrisMultiClient/20261005-131159` |

4인 기준 실행은 PlayerId 256~259 각각 정상 이동 12.09초/4837.7~4867.9cm/
122샘플, 정상 구르기 244.4~315.3cm, 서버 취소 구르기 272.2~275.7cm를 기록했다.
전원 정상 이동·구르기 위반 보고 0건, 정상/취소 종료 후 승인 회수와 승인 없는
10000cm 이동 감지를 확인했다. 성공한 두 폴더에 플레이어별 `Movement.proof.json`이 있다.

보강 전 지연 실행은 전원 정상 이동·구르기의 위반 보고 0건과 정상 종료/서버 취소·승인
회수·10000cm 이동 감지는 확인했지만, 취소 순간에 활성 상대가 없어 전체 성공으로
기록하지 않았다. 프로젝트 에디터 종료를 확인하고 제한된 취소 대기를 포함한
Editor Development 빌드를 검증했다. 빌드 로그:
`Saved/Logs/ConcurrentRollPeerWaitBuild.log`.
보강 후 지연 실행은 정상 이동 12.01~12.10초/4722.6~4787.3cm/121~122샘플,
정상 구르기 351.7~388.2cm, 서버 취소 구르기 271.9~387.9cm를 기록했다.
전원 정상 이동·구르기 위반 보고 0건, 두 번의 종료 후 승인 회수와 10000cm 이동
감지를 확인했고, 플레이어별 JSON이 저장됐다. 실제 최대 겹침은 2명이므로 지연
환경에서 4명 모두가 동시에 구른 것으로 해석하지 않는다. 높은 RTT에서 전원 두 번째
구르기가 클라이언트 예측상 자연 종료했으나 서버 취소와 승인 회수는 별도로 확인했다.
첫 대기 보강의 기본 네트워크 재검증에서는 앞선 플레이어들이 보존 증거를 이미
6건 남긴 뒤 마지막 플레이어가 취소된 상대를 기다리다 자연 종료했다. 이 실행도
성공으로 집계하지 않았다. 대기를 최대 0.4초로 제한하고 다른 플레이어의 실제
보존 증거가 있으면 즉시 취소하는 경계 조건을 추가했다.
최종 경계 조건을 포함한 빌드 확인 로그는
`Saved/Logs/ConcurrentRollFinalTimingBuild.log`다. 최종 기본 네트워크 재실행은
정상 이동 12.00초/4782.8~4802.7cm/121샘플, 정상 구르기 344.1~396.6cm,
서버 취소 구르기 152.5~224.4cm와 전원 승인 회수·위반 보고 0건·10000cm 이동
감지를 확인해 통과했다. 마지막 플레이어도 자연 종료가 아닌 서버 취소로 보고됐다.
동일 최종 DLL의 지연·손실 재검증도 정상 이동 12.01~12.11초/4706.2~4806.4cm/
121~122샘플, 정상 구르기 369.9~376.1cm, 서버 취소 구르기 135.7~248.6cm와
전원 승인 회수·위반 보고 0건·10000cm 이동 감지를 확인해 통과했다. 이번 실행에서
실제 최대 겹침은 4명이고 활성 상대 보존은 5건이다. 지연·손실의 임의 순서에서
항상 전원 동시 실행된다는 보장은 아니다. 최종 두 실행 사이 DLL SHA256이
변하지 않았음을 별도로 확인했다.
동일 최종 DLL의 일반 승인 이동 fixture도 각 방향 250~450ms/10% 조건으로
회귀 검증을 통과했다. 정상 이동 12.09초/4730.5cm/122샘플 및 위반 보고 0건,
점프 높이 89.9cm, 대시 후 남은 추가 예산 345.7cm, 넉백 평면 변위 616.7cm를
기록했다. 클라이언트 승인 거절, 같은 이름의 승인 중첩, 즉시 취소와 만료,
200cm 활성 추가 예산을 넘는 10000cm 이동 감지도 확인했다.
로그 및 `authorizedMovementChecksVerified=true`인 JSON:
`Saved/Logs/IrisMultiClient/20261005-131528`.
증거 검증기 자체의 정상/거부 21개 검사는 통과했다. 이는 네이티브 보안 자동화나
변조 클라이언트 검증을 대신하지 않는다.
취소 대기 보강 전의 동일 Editor DLL에서 기존 네이티브 보안 자동화 20개도
통과했다. 로그:
`Saved/Tests/Security/Automation-61cb1ccae55142ebaa63cd8984a09c87/automation.log`.
열려 있는 프로젝트 에디터를 임의 종료하거나 사용 중인 DLL을 덮어쓰지 않았으며,
테스트 소유 서버·클라이언트·자동화 프로세스만 종료 후 정리했다.
최종 DLL의 네이티브 보안 자동화 20개도 다시 모두 통과했다. 로그:
`Saved/Tests/Security/Automation-06f6c977ce074c3784cf6ee9796ccd6f/automation.log`.
증거 검증기 21개와 스크립트 구문/변경 파일 공백 검사를 통과했고, 최종 네트워크·
자동화 종료 후 테스트 소유 프로세스가 남지 않았음을 확인했다.

첫 다인 시도 `Saved/Logs/IrisMultiClient/20261004-223109`는 BeginPlay에서 테스트
컴포넌트 복제 여부를 변경해 Iris 프로토콜 Ensure가 발생한 실패 로그다. 생성자의
복제 기본값을 고정한 뒤 위 기준 실행에서 Ensure 없는 CDO/인스턴스 일치를 확인했다.

이는 실제 BP 구르기의 다인 실행과 서버 승인 격리 검사다. 다른 계정의 Ability를
직접 대상으로 한 변조 RPC, 소유권 우회 또는 다른 이동 스킬의 적용 검증은 아니다.

## TargetData RPC 진입 방어

```powershell
Scripts/Tests/Run-SecurityAutomationTest.ps1 -TimeoutSeconds 300

Scripts/Network/Run-IrisMultiClientTest.ps1 `
  -SkipBuild -Runtime Editor -ClientCount 2 -TimeoutSeconds 300 -VerifySkillInput

Scripts/Network/Run-IrisMultiClientTest.ps1 `
  -SkipBuild -Runtime Editor -ClientCount 2 -TimeoutSeconds 300 -VerifySkillInput `
  -PacketLagMinMs 250 -PacketLagMaxMs 450 -PacketLossPercent 10
```

서버 ASC는 TargetData 및 TargetData 취소 RPC에서 GAS 캐시 생성·콜백 전에
부여된 활성 Ability와 원래 활성화 예측 키를 확인한다. 메시지 키의 중복·역순을
검사하고, 15비트 키 순환은 허용한다. 같은 메시지 키의 확인과 취소는 각각 한 번씩
허용한다. 키 집합을 무한 저장하지 않고 활성화별 마지막 키/처리 종류만 보관하며,
종료 시 제거한다. 재진입 콜백보다 먼저 처리 상태를 예약한다.

기본 서버 요청 창은 고정 1초/64건이고 중복·거절·취소도 집계한다. 경계 버스트는
이웃한 두 창에 걸칠 수 있다. 페이로드는 최대 32개 항목과 합계 64개 Actor,
동시 추적 활성화는 64개로 제한한다. 무효 핸들·종료된 활성화·잘못된 키는 새 GAS
캐시를 만들지 않는다. 높은 RTT의 종료 후 도착이나 이전 활성화의 데이터는
점수 가산 없이 버린다. 미부여 핸들·형식/크기/유한값 위반·요청률 초과의 위험
보고는 ASC마다 최대 1초에 한 번으로 제한한다. 기존 피해·거리·시야 검사는 유지한다.

2026-10-05 Editor Development 빌드를 통과했다. 로그:
`Saved/Logs/TargetDataAdmissionFinalBuild.log`.
기존 보안 20개와 새 TargetData 5개를 합친 자동화 25개가 모두 통과했다. 로그:
`Saved/Tests/Security/Automation-149b9ceeb7e74689a53ebdc2cf8fe81e/automation.log`.
새 테스트는 순서/중복/키 순환, 10000회 요청의 카운터 상한, 실제 ASC RPC 구현의
캐시·콜백 진입, 다른 ASC의 핸들, 종료·재활성화, 콜백 재진입, 취소 중복,
null/항목 과다/Actor 과다/비유한 hit, 요청 창 회복과 literal-only 스킬 코덱을 검사한다.

2인 기본 네트워크에서 실제 `GA_Player_ChargeSlash`의 Enhanced Input 활성화·해제와
두 서버 실행의 정상 종료가 통과했다:
`Saved/Logs/IrisMultiClient/20261005-134145`.
각 방향 250~450ms/10% 손실의 첫 실행은 서버의 해제 구간 이후 취소로 실패했다:
`Saved/Logs/IrisMultiClient/20261005-134529`.
진입/종료 추적을 추가한 재현도 같은 이유로 실패했다:
`Saved/Logs/IrisMultiClient/20261005-135256`.
추적 재현에서 두 플레이어의 메시지 키 2/3/5, 합계 6건의 TargetData는 모두
`Accepted=1`이었다. 실패를 새 TargetData 필터의 거절로 기록하지 않는다.
서버 몽타주 중단 뒤 취소 종료가 관찰됐으며, 클라이언트 자연 종료 통지와 서버의
입력 대기 task 사이의 상호작용은 별도 엔진 로그로 원인을 확인한다.
이 실패 실행은 고지연 네트워크 통과 증거로 집계하지 않는다.

다른 테스트들과 함께 실행된 `Saved/Logs/IrisMultiClient/20261005-212005`는
스킬 시작 전 서버 probe의 `Timeout`으로 실패했다. 종료 취소 재현으로 집계하지 않는다.
종료 보강 전 재시험은 1인 `Saved/Logs/IrisMultiClient/20261005-212317`,
2인 `Saved/Logs/IrisMultiClient/20261005-212512` 모두 각 방향 250~450ms/10% 조건을
통과했다. 보강 전 실패가 타이밍에 따라 달라진다는 증거이지 문제가 해소됐다는
증거는 아니다.

정책 기반 SkillContainer는 서버의 원래 활성화와 일치하는 클라이언트 자연 종료
RPC를 GAS 처리 전에 무시한다. 서버 정책/몽타주가 실행을 끝내며, 명시적 취소 RPC,
기존 Action 스킬과 다른 GAS Ability는 기존 동작을 유지한다. 자연 종료 통지를
위반으로 보고하지 않는다. `-VerifySkillInput` 실행에서만 TargetData 승인과 종료
추적 및 서버 `LogAbilitySystem Log`를 활성화한다. 진입 로그는 요청 창의 상한,
무시한 자연 종료 로그는 ASC별 1초에 한 번으로 제한하며 Shipping에서는 제외한다.

종료 보강의 Editor 빌드 `Saved/Logs/TargetDataPolicyTerminationBuild.log`에서는
TargetData 진입 코드, SkillContainer, 두 보안 fixture/테스트 소스의 컴파일이
완료됐다. 전체 빌드는 별도 무기 Loadout 작업의
`RPGPlayerWeaponLoadoutComponent.cpp:29`에서 `OutLifetimeProps` 미선언 오류로
실패했다. 이 상태의 이전 DLL로 새 종료 검사가 통과했다고 집계하지 않는다.
새 `ProjectRPG.Security.Skill.AuthoritativeTermination` 검사는 클라이언트 자연 종료
차단, 이후 TargetData 유지, 잘못된 활성화의 취소 거절, 명시적 취소/서버 정상 종료,
기본 GAS 경로의 입력 대기 강제 취소 대조군과 다른 Ability의 호환성을 비교한다.
보안 자동화 실행기는 새 검사를 필수로 포함하므로 이전 25개만 있는 DLL을 거절한다.

무기 Loadout의 복제 매개변수명과 누락된 InputMappingContext 헤더는 해당 작업에서
수정됐다. 보안 작업에서는 그 파일을 변경하지 않았다. 수정 후 Editor Development
전체 빌드가 통과했다: `Saved/Logs/TargetDataPolicyTerminationVerifiedBuild.log`.
중간 헤더 누락 실패는 `Saved/Logs/TargetDataPolicyTerminationFinalBuild.log`에 남긴다.

새 검사 포함 첫 자동화 `Saved/Tests/Security/Automation-59e13ac3fbbd44a8a407a859d8eb6678/automation.log`는
기존 25개 성공, 종료 검사의 다른 Ability 호환성 assertion 실패로 전체 실패했다.
단독 실행 월드의 원격 종료 비허용 기본값을 잘못 가정한 대조군이었다. 다른 Ability의
확인된 원격 활성화 상태를 명시해 이 대조군을 수정했다. 이 첫 실행은 26개 통과로
집계하지 않는다. 동일 검사 내 기본 GAS 대조군에서 입력 대기 중 자연 종료가 취소로
전환됐고, 보강된 경로의 자연 종료 차단/타깃 유지/명시적 취소/서버 완료는 확인됐다.

대조군 수정 후 Editor Development 빌드도 통과했다:
`Saved/Logs/TargetDataTerminationFixtureBuild.log`.
종료 보강을 포함한 보안 자동화 26개가 모두 통과했다:
`Saved/Tests/Security/Automation-204811cb839f40e09474b602dbba37d9/automation.log`.
동일 DLL의 보강 후 2인 기본 네트워크도 서버 2명/클라이언트 2명 모두 정상 종료로
통과했다: `Saved/Logs/IrisMultiClient/20261005-214704`.
두 플레이어의 TargetData 메시지 2/3/5, 합계 6건이 모두 승인됐다.
보강 후 2인 각 방향 250~450ms/10% 손실도 동일하게 전원 정상 종료로 통과했다:
`Saved/Logs/IrisMultiClient/20261005-214821`.
이 실행도 두 플레이어의 TargetData 6건이 모두 승인됐으며 종료 취소/Ensure는 없었다.
기본 실행 뒤 다른 작업에서 공유 DLL이 재빌드됐으므로 두 네트워크 실행을
동일 바이너리의 결과로 묶지 않는다. 고지연 실행의 전후 DLL SHA256은
`9CBC5B004C5AA26765999166E96FD52CD3EB3ED6D7D21DFB4E9C6B542241A28C`로 일치했다.
같은 DLL의 1인 각 방향 250~450ms/10% 실제 BP Roll 회귀도 통과했다:
`Saved/Logs/IrisMultiClient/20261005-215032`.
정상 구르기 374.6cm/몽타주 5샘플, 서버 취소 구르기 232.5cm/2샘플,
두 종료의 서버 승인 회수와 정상 이동 위반 0건, 무단 10000cm 이동 감지를 확인했다.
이번 Roll 회귀는 1인 검사이므로 다인 승인 격리 증거로 확대 해석하지 않는다.
고지연 스킬/구르기 실행과 같은 최신 공유 DLL의 보안 자동화 26개도 다시 모두
통과했다: `Saved/Tests/Security/Automation-708b03df6bf848cf86e1bcc6bc335816/automation.log`.
기존 타기팅 정책/TargetData 코덱 호환성 2개도 별도 자동화로 통과했다:
`Saved/Tests/Security/Targeting-956d4f01974744628907fcaf40b84f4f/automation.log`.
이후 공유 DLL이 다시 빌드됐으므로 이 2개 검사를 고지연 네트워크와 같은 바이너리의
검사라고 단정하지 않는다. 보안 작업의 C++ 구현은 이 후속 공유 빌드 동안 바꾸지 않았다.
현재 공유 DLL의 보안 자동화 26개도 모두 재통과했다:
`Saved/Tests/Security/Automation-69a3991a59bc4031ae1c5b425ec30758/automation.log`.
이 자동화의 전후 SHA256은
`CBE80D2C1BFC644285BCDAC51F29C18301994CC5B3CC09BD1519BECD2C667D8D`로 일치했고,
종료 후 테스트 소유 Unreal 프로세스가 남지 않았음을 확인했다. 변경 파일 공백,
PowerShell 구문 검사와 이동 증거 검증기 21개도 통과했다.

RPC 구현 테스트는 자동화 월드에서 실제 서버 구현을 직접 호출한다. 변조 페이로드의
네트워크 직렬화·다른 계정 소유 Actor의 실제 RPC 호출·악성 Ability 활성화/Batch
재전송까지 검증한 것은 아니다. 예측 키는 인증 토큰이나 영구 재전송 방지 ID가
아니며, 원래 활성화 RPC 자체의 재전송 판정을 이 TargetData 검사로 대체하지 않는다.
`-VerifySkillInput`은 기존 실제 Enhanced Input/스킬 실행의 정상 네트워크 회귀용이다.
BP 설정은 [TargetData 통합 가이드](BP_SECURITY_INTEGRATION.md#6-targetdata와-지면-지정-스킬)를 참고한다.

## TargetData 실제 Iris 변조 테스트

`-VerifyTargetDataAbuse`는 Editor Development에서 별도 dedicated server/두 클라이언트를
실행한다. 기존 캐릭터/BP 에셋 대신 PlayerController 소유의 격리된 Actor에 생산용
`RPGAbilitySystemComponent` 방어를 상속한 ASC를 사용한다. 실제 GAS RPC 직렬화와
Iris 수신 경계를 검사하며 `_Implementation` 직접 호출로 네트워크 성공을 대체하지 않는다.

```powershell
./Scripts/Network/Run-IrisMultiClientTest.ps1 -ClientCount 2 -VerifyTargetDataAbuse -TimeoutSeconds 300
./Scripts/Network/Run-IrisMultiClientTest.ps1 -SkipBuild -ClientCount 2 -VerifyTargetDataAbuse -TimeoutSeconds 300 -PacketLagMinMs 250 -PacketLagMaxMs 450 -PacketLossPercent 10
./Scripts/Tests/Test-TargetDataWireProof.ps1
```

프로브는 피어별 12단계를 수행한다: 정상 대조군, 중복, 역순, 잘못된 활성화,
다른 ASC의 부여 핸들, 합계 65개 Actor 참조, 중복 취소, 정상 회복, 종료 후 재전송,
재활성화, 이전 활성화 재전송, 다른 소유자의 RPC. 과다 대상은 GAS 직렬화의 항목당
31개 제한을 지킨 31/31/3개 배열로 보내므로 송신 단계의 잘림을 서버 차단으로 오인하지 않는다.

각 서버 단계는 실제 수신 횟수/승인 콜백/취소 콜백/잘못된 키의 캐시 생성 여부를
검사한다. checkpoint도 TargetData/활성화와 동일한 ASC subobject의 reliable RPC로
보내며, root Actor와 subobject 사이의 처리 순서를 가정하지 않는다.
다른 소유자 호출은 개발 전용 `UReplicationSystem::SendRPC`로 송신측 로컬 소유권
검사를 우회하되 수신측 Iris 코드는 변경하지 않는다. 동일 저수준 경로의 자기 소유
정상 요청이 수신·승인되어야 하며, 다른 소유자 요청은 서버 `LogIrisRpc Verbose`의
`target not being owned by the connection` 거부 로그가 정확한 상대 Actor/ASC에 남아야 한다.
단순 `no owning connection` 클라이언트 경고는 증거로 인정하지 않는다.

상대의 실제 부여 핸들과 현재 활성화 키를 알고 있는 공격도 검사한다. GAS의
`FPredictionKey`는 다른 소유자의 연결에 전달하면 0으로 직렬화되므로, 개발 전용
challenge가 검증된 키의 정수 값만 전달하고 공격 클라이언트가 이를 재구성한다.
운영 코드의 키 직렬화나 Iris 소유권 검사는 바꾸지 않는다. 상대 활성화가 서버에서
유효한 상태여야 하며, 단순히 0 또는 오래된 키라서 거절된 요청을 소유권 방어로 인정하지 않는다.

`TargetDataWireProof.ps1`의 schema 3은 독립 피어/전체 단계/정확한 수신·콜백 수/
상대 root/실제 활성화 challenge/수신측 거부를 검증한다. 서버·송신 클라이언트·
Iris 수신 로그의 대상 ASC `NetRefHandle` ID가 같아야 한다. 동적 Actor의 로컬 이름은
프로세스마다 다를 수 있으므로 교차 프로세스 식별자로 사용하지 않는다. 부모 Actor의
ID를 ASC RPC의 ID로 잘못 사용해도 검증이 실패한다.
실행기는 DLL SHA256이 시작·완료 시 동일할 때만
`TargetData.proof.json`을 생성한다. 성공 문자열 개수만으로 통과하지 않는다.
기존 스킬/이동 프로브와는 별도로 정확히 두 클라이언트를 사용한다.

2026-10-06 현재 검증 상태:

- 증거 검증기 28개, 기존 이동 증거 검증기 21개 통과. 정상 증거 외에도 0/불일치
  활성화 키, 비활성 상대, 잘못된 부여 핸들, 중복/0/불일치 ASC 네트워크 ID를 거절한다.
- 첫 Editor 전체 빌드에서 테스트 ASC의 protected 복제 기본값 API를 외부에서
  호출한 오류를 수정했고 `Saved/Logs/TargetDataWireVerifiedBuild.log` 빌드 통과.
- 첫 실제 네트워크 실행 `Saved/Logs/IrisMultiClient/20261006-010735-c2532899`는
  두 클라이언트의 `ClientActivation` 실패로 중단됐다. 격리 Actor가 simulated
  avatar여서 GAS가 예측 활성화를 거절한 테스트 구성 문제이며 방어 성공으로 기록하지 않는다.
- 서버 테스트 Actor를 소유 연결에서만 AutonomousProxy가 되도록 수정했다.
  수정 소스는 `Saved/Logs/TargetDataWireRoleCompile.log`의 Development 단일 파일
  컴파일에서 통과했다. `-NoLink` 실행이므로 수정 후 네트워크 실행이 통과했다고 주장하지 않는다.
- 기존 보안 26개 회귀 통과:
  `Saved/Tests/Security/Automation-0ca21faf87fc46118a753fb9c204590f/automation.log`.
  실행 전후 DLL SHA256은 `A36405179DBF53484199AC2A03A25EE9FE8E3838B45833FFC9CE9AB4470C1C3A`로 동일하다.
  이 DLL에는 후속 Actor 역할 수정이 링크되지 않았다.
- `Saved/Logs/TargetDataWireShippingCompile.log`에서 방어와 프로브 두 소스의
  Win64 Shipping 단일 파일 컴파일 통과. Shipping 프로브 오브젝트의 symbol 검사에서
  `SendIris` 주입 헬퍼가 없음을 확인했다. `-NoLink`이므로 전체 Shipping 링크/Cook/
  패키지/실행 통과 증거가 아니다.
- PlayerController/기존 보안 fixture까지 확대한 Shipping 컴파일의 첫 실행
  `Saved/Logs/TargetDataWireShippingAttachmentCompile.log`은 기존 코드의
  UDataTable 선언/TimerManager/GameInstance/GEngine include 누락으로 실패했다.
  해당 선언과 include만 보완했고 두 소스 모두
  `Saved/Logs/TargetDataWireShippingAttachmentVerifiedCompile.log`에서 통과했다.
  테스트 컴포넌트 부착의 Shipping 조건도 컴파일되지만 여전히 전체 패키지 검증은 아니다.

재개 후 수정과 검증:

- 공유 변경 중 `TargetDataWireResumeBuild.log`는 HealthComponent 소스와 생성 헤더의
  불일치로, `TargetDataWireResumeVerifiedBuild.log`는 실행 중인 별도 테스트의 DLL
  잠금으로 실패했다. HealthComponent 소스를 덮어쓰거나 해당 프로세스를 강제 종료하지 않았다.
- `Saved/Logs/IrisMultiClient/20261006-141314-a602e08b`는 실제 24단계와 수신측 거부가
  끝났지만 서로 다른 프로세스의 Actor 이름을 같다고 가정한 증거 검증이 실패했다.
  `20261006-141616-9df3c4a5`는 부모 Actor ID와 대상 ASC ID의 차이로 검증이 실패했다.
  이 두 실행을 성공으로 기록하지 않고, 검증을 완화하지 않은 채 ASC ID 대조로 수정했다.
- `Saved/Logs/TargetDataWireASCIdentityBuild.log`의 Editor 전체 빌드 통과.
- 기본 2클라이언트 실행 통과:
  `Saved/Logs/IrisMultiClient/20261006-141803-de1cb30c/TargetData.proof.json`.
- 각 송신 방향 250~450ms/손실 10% 실행 통과:
  `Saved/Logs/IrisMultiClient/20261006-141916-de36c884/TargetData.proof.json`.
  서버와 두 클라이언트 모두 `LogNet`에 실제 지연/손실 설정 적용을 확인했다.
- 각 실행에서 독립 피어 256/257의 12단계씩, 피어별 Confirm RPC 수신 15회/Cancel
  수신 2회/승인 콜백 5회/취소 콜백 1회를 확인했다. 자기 소유 저수준 대조군은
  정상 승인됐고, 유효한 상대 핸들·활성화 키를 위조한 요청은 Iris 수신 경계에서
  상대 ASC 40/78에 대해 거절되어 native TargetData 구현에 진입하지 않았다.
  잘못된/종료된/이전 활성화 키의 캐시 미생성과 정상 회복도 통과했다.
- 두 성공 실행의 전후 DLL SHA256은 모두
  `7A0AB1D6A633711BC33BEA0F14B2C01B2DD58AEA70EAAC07F93A1C66EA73B5B1`로 동일하다.
- `Saved/Logs/TargetDataWireASCIdentityShippingCompile.log`에서 프로브/생산용 방어/
  PlayerController/보안 fixture 4개 소스의 Shipping 단일 파일 컴파일 통과.
  오브젝트에서 `SendIris` 헬퍼와 PlayerController의 테스트 컴포넌트 참조가 없으며,
  `ServerWireCheckpoint`/`ClientRunCase`/`ClientComplete` 구현은 빈 반환 명령뿐임을 확인했다.
  테스트 클래스/RPC의 reflection metadata까지 제거됐다는 뜻은 아니다.
  `-NoLink`이므로 전체 Shipping 링크/Cook/패키지/실행은 아직 검증하지 않았다.

이후 별도 공유 빌드 `Saved/Logs/RaidNaturalCombatBuild-20261006.log`가 새 로비 UI의
`URPGOnlineLobbyWidget` 구현 심볼 29개 누락으로 실패해 공용 Editor DLL이 일시적으로
없어졌다. 보안 작업에서 해당 UI를 임의로 구현/삭제하지 않았고, 로비 구현이 준비된
뒤 `Saved/Logs/RaidNaturalCombatRetryBuild-20261006.log`의 공유 전체 빌드가 통과했다.
재개한 회귀는 앞선 성공 DLL과 구분하여 기록한다:

- 새 공유 DLL의 보안 자동화 26개 전부 통과:
  `Saved/Tests/Security/Automation-5f2c6b2404dd43ee8bb3358a7f9fad47/automation.log`.
- 같은 DLL의 2클라이언트 실제 Enhanced Input 차지 스킬 회귀도 각 방향
  250~450ms/손실 10%에서 통과: `Saved/Logs/IrisMultiClient/20261006-142517`.
  fixture가 스킬/Rage를 부여한 조건에서 기존 BP의 활성화/TargetData 전달/비취소
  Release 섹션을 서버와 두 클라이언트에서 확인했다. 실제 피해/HP 검증으로 확대하지 않는다.
- 같은 DLL에서 악성 wire 2클라이언트 24단계와 상대 소유권 거부도 각 방향
  250~450ms/손실 10%에서 재통과:
  `Saved/Logs/IrisMultiClient/20261006-142618-f515c758/TargetData.proof.json`.
  서버와 두 클라이언트의 실제 패킷 시뮬레이션 적용 및 동일한 수신/콜백/캐시 대조를 확인했다.
- 세 검사 각각의 전후 SHA256은
  `98186DE9D704396545DFEB679182B1597A4B3100FDCE8D1DD1F92EB0ACBB5C99`로 동일하다.
  앞선 기본/고지연 wire 실행의 `7A0...` DLL과는 구분한다. 이번 테스트 소유 프로세스는
  종료됐으며, 독립적으로 실행 중인 다른 작업의 Unreal 프로세스는 종료하지 않았다.

기본 12단계 프로브는 실제 피해/HP/스폰, authored BP 전투 효과, Ability 활성화/Batch 재전송
정책, 직렬화 전 메모리 DoS 또는 wire rate-flood를 검증하지 않는다. 요청 빈도 상한은
기존 native 자동화에 있고, 악성 네트워크 부하 실측은 별도 작업이다.

## TargetData 실제 요청 폭주 검증

`-VerifyTargetDataRate`는 기본 2클라이언트 wire 검증을 먼저 수행한 뒤 생산용 ASC의
기본 64건/서버 시각 1초 제한을 추가로 검사한다. 실행기에서 `-VerifyTargetDataAbuse`를
함께 활성화하며 정확히 두 클라이언트/Editor runtime/별도 스킬·이동 검사와의 분리를
유지한다. 명시적 개발 플래그 `-RPGTargetDataRateE2E`가 없는 기존 검증 동작은 바꾸지 않는다.

```powershell
./Scripts/Network/Run-IrisMultiClientTest.ps1 -ClientCount 2 -VerifyTargetDataRate -TimeoutSeconds 300 -MaxFPS 30
./Scripts/Network/Run-IrisMultiClientTest.ps1 -SkipBuild -ClientCount 2 -VerifyTargetDataRate -TimeoutSeconds 300 -MaxFPS 30 -PacketLagMinMs 250 -PacketLagMaxMs 450 -PacketLossPercent 10
./Scripts/Tests/Test-TargetDataRateProof.ps1
```

낮은 피어 ID의 격리 ASC를 폭주 클라이언트, 다른 피어를 정상 대조군으로 정한다.
서버의 실제 부여 핸들과 활성화 키로 다음 3단계를 수행한다.

- 새 예측 메시지 키를 사용한 Confirm 48회/Cancel 48회, 합계 96건을 전송한다.
  Confirm/Cancel이 각각 별도 64건 예산을 받지 않고 같은 요청 창을 소비해야 한다.
- 잘못된 활성화/이전 메시지/취소 재전송 64건 뒤 유효한 새 Confirm 32건을 전송한다.
  거절된 요청도 예산을 소비하므로 실제 초과 창 안의 유효한 후속 요청은 차단되어야 한다.
- 양쪽 ASC의 reliable checkpoint 뒤 서버 시각으로 2초 이상 기다리고 새 요청을 보낸다.
  폭주한 ASC와 정상 ASC 모두 복구 요청을 1회 승인해야 한다.

첫 두 단계에서 정상 클라이언트는 약 0.125초 간격으로 48건씩 보내며 전부 승인돼야 한다.
서버가 정상 피어의 첫 요청을 실제로 수신한 뒤에만 폭주 클라이언트의 송신을 시작한다.
실제 초과 창이 열린 동안 정상 피어 요청이 승인된 서버 수신 시각도 대조한다. 서로 다른
시각에 정상/폭주 요청을 실행한 결과만으로 플레이어별 제한 격리를 주장하지 않는다.

수신 관찰기는 생산용 limiter의 상태를 읽거나 재설정하지 않고, 실제 native RPC 도착
시각과 순서로 독립된 1초 창을 구성한다. 잘못된 키의 캐시 미생성과 요청별 정확한 콜백을
확인한다. 지연/손실로 96건이 여러 창에 나뉘면 각 창의 실제 예산을 계산하며, 실행 전체의
승인 수를 무조건 64건으로 가정하지 않는다. 실제로 유효 요청이 제한된 초과 창이 없는
실행은 성공으로 인정하지 않는다. 제출 횟수나 마지막 성공 로그만으로 통과하지 않는다.

`TargetDataRate.proof.json` schema 5는 기본 24단계/수신측 소유권 검증을 포함한다.
추가로 6개 피어별 단계, 모든 수신 창/정상 콜백의 시각·수신·승인·취소 수, 과거·취소 요청의
예산 소비, 상대 피어 격리, 복구, 모든 프로세스의 실제 패킷 시뮬레이션 설정과 DLL 해시를
확인한다. 기본 범위만 검증하는 `TargetData.proof.json` schema 3도 별도로 유지한다.

2026-10-07 검증 기록:

- 기존 wire 증거 검증 28개, 폭주 증거/실행 모드 검증 42개, 이동 증거 검증 21개 통과.
  폭주 검증에는 여러 창의 정상 증거와 누락/중복/잘못된 승인/잘못된 집계/조기 창 초기화/
  정상 요청 비겹침/미적용 패킷 설정/복구 실패 등의 거절 검사가 포함된다.
- 첫 Development 단일 파일 검사 `Saved/Logs/TargetDataRateDevCompile-20261007-005259.log`는
  다른 프로세스가 `EditorSettings.ini`를 사용 중인 일시적 파일 잠금으로 실패했다.
  설정 파일 변경이나 프로세스 강제 종료 없이 재시도했고
  `Saved/Logs/TargetDataRateDevVerifiedCompile-20261007-005325.log`에서 컴파일 통과.
  `-NoLink` 전후 공용 DLL 해시가 같았다. 이 컴파일 자체를 링크/실행 증거로 대체하지 않는다.
- 초기 schema 4/정상 20건 프로브가 포함된 공용 DLL에서 기본 실제 네트워크 실행 통과:
  `Saved/Logs/IrisMultiClient/20261007-005640-35dfed14/TargetDataRate.proof.json`.
  새 키 Confirm/Cancel 96건 중 32+32건 승인/32건 제한, 거절 압박 96건에서는
  앞의 64건이 창을 소모해 뒤의 유효한 32건도 제한됐다. 정상 피어의 20+20건은 모두
  승인됐고, 두 압박 창과 실제 수신 시각이 겹쳤다. 양쪽 복구 요청도 승인됐다.
  시작·완료 DLL SHA256은
  `935477E6DB43764C09F5B1048B1034CBA1AFDA3A207F31026A1551A8E144FDEF`로 동일하다.
- 같은 초기 DLL의 고지연 실행 `Saved/Logs/IrisMultiClient/20261007-005739-b9ac7739`는
  요청 제한/거절 압박/정상 요청 승인/복구까지 끝났지만 실제 초과 창 안의 정상 요청
  수신이 겹치지 않아 최종 증거 검증이 실패했다. 성공 JSON을 만들지 않고 프로세스를 정리했다.
  동시 송신 명령만으로 동시 수신을 가정한 구성 문제이므로 방어 실패나 성공으로 기록하지 않는다.
  정상 요청을 48건으로 늘리고 서버의 첫 정상 수신을 확인한 뒤 폭주를 시작하도록 수정했다.
  schema 5는 이 순서도 대조하며 이전 schema 4 결과를 새 프로브 성공으로 대체하지 않는다.
- 수정 후 Editor 전체 빌드 통과:
  `Saved/Logs/TargetDataRateCoordinatedBuild-20261007-010336.log`.
- schema 5 기본 2클라이언트 검증 통과:
  `Saved/Logs/IrisMultiClient/20261007-010519-c87b6fa7/TargetDataRate.proof.json`.
- 같은 DLL의 각 방향 250~450ms/손실 10% 검증 통과:
  `Saved/Logs/IrisMultiClient/20261007-010822-a1d0f1f2/TargetDataRate.proof.json`.
  두 실행 모두 기본 24단계와 폭주/거절 압박/복구 6개 피어별 단계를 통과했다.
  실제 96건 Confirm/Cancel 창에서 32+32건 승인/32건 제한, 거절 압박 창에서 뒤의
  새 유효 요청 32건 제한을 확인했다. 정상 피어의 48+48+복구 1건, 총 97건은 모두
  승인됐으며 두 과다 요청 창과 실제 승인 수신 시각이 겹쳤다. 폭주 피어도 복구됐다.
- 보안 native 자동화 26개도 같은 DLL에서 전부 통과:
  `Saved/Tests/Security/Automation-b1bf74caba9f4fdbb6609dd2a05451bf/automation.log`.
- 같은 DLL의 2클라이언트 기존 BP 차지 스킬 고지연·손실 회귀 통과:
  `Saved/Logs/IrisMultiClient/20261007-010822`.
  fixture가 스킬/Rage를 부여한 조건의 Enhanced Input/TargetData/비취소 Release 검사다.
  실제 피해·HP 또는 새 BP 적용을 검증한 것은 아니며 이번 작업에서 BP 에셋은 변경하지 않았다.
- 위 네 실행 각각의 전후 SHA256은
  `4FEAAC8CCBCDAEE382FE4D43925E68CB758E3E4EB6C196BAE2A6B770C74E6868`로 동일하다.
- 수정 후 Shipping 단일 파일 4개 컴파일 통과:
  `Saved/Logs/TargetDataRateCoordinatedShippingCompile-20261007-010519.log`.
  오브젝트에서 `SendIris`/`RateRequest`/`RateEnabled` 주입·계획 헬퍼, 테스트 trace 문자열,
  PlayerController의 프로브 부착 참조가 없음을 확인했다. 기본/폭주 체크포인트와 두 실행/
  완료 client RPC, 총 6개 테스트 RPC 구현은 빈 `ret 0` 명령뿐이다. Reflection metadata
  제거 또는 전체 Shipping 링크/Cook/패키지/실행 통과를 의미하지 않는다.

이 검사는 제한된 크기의 96건 버스트에 대한 전달 경계 검증이다. 무제한 연결/패킷 부하,
직렬화 전 메모리 DoS, 실제 BP 피해·HP·스폰, 운영 서버 처리량/장시간 메모리 상한 또는
전체 Shipping 패키지/실행을 검증한 것은 아니다. 서버 보안 허용치와 기존 BP 에셋을
완화하지 않으며, 개발용 송신/관찰/RPC 본문은 Shipping에서 동작하지 않아야 한다.

## 정책 기반 스킬의 BP 피해 이벤트 연결

2026-10-07: 읽기 전용 콘텐츠 감사에서 `GA_Player_ChargeSlash`의
`AM_PlayerChargeSkill` → `AN_SendGameplayEvent` → `Player.Event.AOE` 연결과
`GE_Shared_ApplyDamage` 기본값을 확인했다. 기존 정책 컨테이너는 이 이벤트를
레거시 고정 반경 피해 경로로 보내고 있어 정의의 부채꼴 HitQuery / 누적 타격 예산을
우회할 수 있었다. 표준 Hit/AOE 이벤트를 가상 처리기로 정책 컨테이너에 연결해
서버의 고정된 RuntimeSpec 판정을 사용하도록 수정했다. BP 에셋은 저장하지 않았다.

- 기본 피해: `SkillDataHandle.AttackPower`를 `RuntimeSpec.BaseDamage`에 고정한다.
  감사한 현재 행 값은 ChargeSlash 250, AssultBlade 40, JumpSmash 100, WhirlWind 7이다.
  클라이언트 이벤트 대상/목록/피해 크기는 사용하지 않는다.
- 범위·팀·무적·배율 적용 SetByCaller 피해 상한과 누적 타격 예산을 승인 경로에서 검사한다.
  HP 감소량은 GE의 공격력/방어력/치명타 계산 결과이며 이 상한과 동일하지 않다.
  프로필별 최종 HP 감소 상한/스탯 경계는 별도 후속 검증 대상이다.
- 종료 중/종료 후의 피해 및 HitQuery를 거절한다. 효과 콜백 전 예산 예약,
  재진입 거절, 활성화 세대 번호 검사로 이전 타격 목록이 새 활성화에 이어지지 않게 한다.
- 실패한 대상은 예산을 반환하며, 예산보다 앞에 정렬된 무적 대상 때문에 정상 대상이
  누락되지 않도록 승인된 타수로 반복을 제한한다.

재현용 읽기 전용 감사 스크립트: `Scripts/Editor/AuditSkillDamageContent.py`.
감사 완료 로그: `Saved/Logs/SkillDamageContentAudit-20261007-DamageTable.log`
(`COMPLETE NoAssetsSaved=1`). 이 감사는 BP 구조/기본값/Notify/행을 읽은 결과이며,
실제로 BP 몽타주를 재생해 HP가 복제되었다는 증거가 아니다.

`ProjectRPG.Security.SkillDamage.*` 네이티브 자동화 6개는 실제 ASC, 서버 HitQuery,
GameplayEffect 실행 계산 및 `RPGAttributeSet` HP 변경을 사용한다. 활성화 로컬
구성은 테스트 픽스처에서 제공하므로 실제 BP 입력·몽타주 Notify 타이밍,
클라이언트 악성 RPC, 네트워크 HP 복제를 검증했다고 주장하지 않는다.
테스트 러너는 필수 테스트 누락 또는 실행 전후 Editor DLL SHA256 변경을 실패로
판정한다. 신규 CPP를 추가한 경우 UBT 파일 목록을 다시 수집해야 한다.

2026-10-08 검증 기록:

- Editor 재빌드/링크 통과:
  `Saved/Logs/SkillDamageSecurityResumedBuild-20261008.log`.
  초기 2026-10-07 빌드는 신규 CPP 누락 캐시 및 중단된 빌드와 구분하며,
  `-NoUBTMakefiles`로 전체 소스를 다시 수집했다.
- 보안 자동화 **32개 통과**(위 피해 테스트 6개 포함):
  `Saved/Tests/Security/Automation-9e9f760ea79e4df898eb56b61e128eca/Security.proof.json`.
  실행 전후 Editor DLL SHA256:
  `FBE1CE2D8E9FBCB4A296AB8B0625C8CC55CFE5152EA55856295175BA392720ED`.
- 앞선 `Automation-fa5425270afa4208b07010f878f5cdba`도 32개 테스트 자체는
  성공했지만 도중 DLL이 교체되어 러너가 실패로 처리했고 증거 JSON을 만들지 않았다.
  최초 `Automation-8ac11213b838451397ca2859aa19e365`는 새 테스트 6개가 누락되어
  성공으로 인정하지 않았다. 필수 테스트/해시 검사를 완화하지 않았다.
- Shipping 소스 5개 단일 파일 컴파일 통과:
  `Saved/Logs/SkillDamageSecurityShippingCompile-20261008-Verified.log`.
  컨테이너/정의/RuntimeSpec/테스트 픽스처/테스트 CPP를 검사했다. 최초 Shipping
  컴파일에서 드러난 직접 헤더 의존성을 명시했다(`UWorld`, `APawn`, `FHitResult`,
  `UMaterialInterface`, `UTexture2D`). 이는 전체 Shipping 링크/Cook/패키지 통과가 아니다.
  오브젝트의 테스트 Ability `CanActivateAbility`는 `xor al, al; ret`(항상 거절),
  `Configure` / `Finish`는 빈 `ret 0` 본문이고 테스트 실행/등록 심볼은 없다.
  리플렉션 클래스 메타데이터가 제거되었다고 주장하지 않는다.
- 기존 PowerShell 증거 검증기 회귀: TargetData wire 28개, rate 42개, movement 21개 통과.
- 실제 BP 차지 입력 회귀: 서버 + 클라이언트 2개, 지연 250~450ms / 손실 10%에서
  비취소 Release 완료. 로그 `Saved/Logs/IrisMultiClient/20261008-005952`.
  실행 전후 해시는 위 보안 자동화의 `FBE1CE2D...720ED`와 동일하다. 스킬/Rage는
  픽스처에서 지급하며, 이 검증은 실제 타격·HP 감소/복제 검증이 아니다.
  앞선 `20261008-005642`도 내부 입력 검증은 통과했지만 외부 실행 명령의 오래된
  `$LASTEXITCODE` 판정이 잘못되어, PowerShell 오류 전파와 해시 검사를 사용해 재실행했다.

위 단계에서 미검증이었던 BP 네트워크 피해/HP 복제는 아래 별도 실행으로 검증했다.
당시 미검증이었던 프로필별 최종 피해 상한은 아래 별도 구현/네이티브 검증으로 연결했다.

## 실제 Charge BP 타격과 HP 복제

2026-10-08: `-VerifySkillDamage`는 전용 서버 + Editor 클라이언트 **1개**로
실제 `GA_Player_ChargeSlash`의 Enhanced Input 주입 → 정상 Release 몽타주 →
`AN_SendGameplayEvent` / `Player.Event.AOE` → 서버 승인 HitQuery →
`GE_Shared_ApplyDamage` → 서버 HP → Iris Attribute 복제/클라이언트 변경 알림을 검증한다.
BP 에셋이나 스킬 정의/서버 보안 허용치는 변경하지 않았다.

피해 함수, GameplayEffect 적용 함수 또는 GameplayEvent를 픽스처가 직접 호출해
타격을 만들어 내지 않는다. 기본 피해 250, 배율 1, 실제 소스 공격력 2.5,
대상 방어력 1로 HP 감소 **625**를 계산하고 서버/클라이언트 양쪽과 대조한다.
치명타 확률은 픽스처에서 서버 0으로 고정한다. 스킬/Rage는 기존 입력 픽스처에서
지급하며 물리 키 입력/정상 로드아웃 획득을 검증했다고 주장하지 않는다.

대상 4개는 운영 ASC/AttributeSet/UI 인터페이스를 사용하는 네이티브 테스트 Pawn이다.
서버가 스킬의 검증된 `HitQueryTransform`에 맞춰 위치를 유지하고, 실제 GE 적용
콜백에서도 위치/팀/NetRefId를 다시 확인한다. BP의 반경 650/각도 120은 그대로다.
HP 1,000,000과 컨트롤러 팀은 테스트 준비값이며 클라이언트는 HP를 직접 쓰지 않는다.

| 대상 | 쿼리 로컬 위치 | 서버 GE/HP | 클라이언트 HP 알림 |
| --- | --- | --- | --- |
| 정상 적 | (300, 0, 0) | GE 1회, HP 999,375 | 같은 NetRefId, 변경 알림 1회 |
| 부채꼴 뒤쪽 적 | (-300, 0, 0) | GE 0회, HP 1,000,000 | 없음 |
| 아군 | (300, 120, 0) | GE 0회, HP 1,000,000 | 없음 |
| 사거리 밖 적 | (1500, 0, 0) | GE 0회, HP 1,000,000 | 없음 |

클라이언트에서 4개 대상의 실제 HP/NetRefId가 준비되고 변경 알림 구독이 완료된 뒤
입력을 시작한다. 서버 자연 종료 뒤 3초 관찰하고 서버 완료 플래그와 실제 복제 HP가
모두 일치해야 완료한다. 테스트 기대값 복제만으로 성공시키지 않는다.
`SkillDamageProof.ps1`은 원본 로그의 GE 클래스/SetByCaller, 실제 이벤트, 타격 순간
위치/팀, 대상별 HP/알림/NetRefId, 동일 활성화 키, 비취소 종료와 순서를 검사한다.
실제 `LogNet`의 마지막 패킷 시뮬레이션 설정 및 실행 전후 DLL SHA256도 대조한다.
누락/중복/추가/잘못된 증거 또는 fatal/assert/ensure는 실패이며 성공 JSON을 만들지 않는다.

재현(먼저 Editor Development 빌드, 다른 테스트 옵션과 분리):

```powershell
.\Scripts\Network\Run-IrisMultiClientTest.ps1 -SkipBuild -VerifySkillDamage -ClientCount 1 -MaxFPS 30 -TimeoutSeconds 150
.\Scripts\Network\Run-IrisMultiClientTest.ps1 -SkipBuild -VerifySkillDamage -ClientCount 1 -MaxFPS 30 -TimeoutSeconds 150 -PacketLagMinMs 250 -PacketLagMaxMs 450 -PacketLossPercent 10
.\Scripts\Tests\Test-SkillDamageProof.ps1
```

검증 기록:

- Editor 소스 재수집/컴파일/링크 통과:
  `Saved/Logs/SkillDamageNetworkBuild-20261008-Verified.log`.
  최초 빌드의 복제 매크로 인자 이름 오류는 수정하고 재실행했다.
- 기본 실행: `Saved/Logs/IrisMultiClient/20261008-014212-db7bd8b5/SkillDamage.proof.json`.
  판정기에 추가한 Iris/활성화 키/종료 순서 검사를 동일 원본 로그로 다시 검증했다.
- 지연 250~450ms / 손실 10% 실행:
  `Saved/Logs/IrisMultiClient/20261008-014336-0932c842/SkillDamage.proof.json`.
  두 실행의 전후 Editor DLL SHA256은
  `08E7A0215BAB9050F52C381E2E53CEDB77503B9ADEF82043122A490172F2A172`로 동일하다.
- 동일 DLL의 기존 보안 자동화 **32개 통과**:
  `Saved/Tests/Security/Automation-0dbacc6db19746139cc154a27aeddbc1/Security.proof.json`.
  필수 테스트와 실행 전후 해시 검사를 유지했다.
- 증거/CLI 판정 테스트 **51개 통과**. 골든 로그와 누락/변조/순서/잘못된 옵션을
  검사하는 합성 증거 테스트이며, 이 숫자는 실제 네트워크 타격 횟수가 아니다.
  기존 wire 28개 / rate 42개 / movement 21개 판정 회귀도 통과했다.
- Shipping `-NoLink` 단일 파일 **2개 실제 컴파일 통과**:
  `Saved/Logs/SkillDamageNetworkShippingCompile-20261008-Verified.log`.
  새 피해 네트워크 픽스처 및 기존 입력 프로브 CPP를 검사했다.
  앞선 세미콜론 연결 `-SingleFile` 실행은 0개 action이어서 컴파일 증거로 인정하지 않고,
  각각의 `-SingleFile` 인자를 사용해 재실행했다.
  새 픽스처 오브젝트의 `Fail` / `TickClient` / `TickServer`는 빈 `ret 0` 본문이다.
  메타데이터/클래스 제거 또는 전체 Shipping 링크/Cook/패키지/실행을 의미하지 않는다.

이 결과는 단일 플레이어의 실제 Charge BP 타격과 네이티브 대상 HP 복제 계층이다.
다른 스킬, 무적 대상의 실제 BP 타격(네이티브 단위 테스트는 별도), 운영 NPC 사망/리셋,
여러 플레이어 간 피해 격리, 악성 wire RPC의 실제 HP 불변, 최종 스킬별 HP 감소 상한,
실제 화면 UI 및 장시간 부하/전체 Shipping 패키지를 검증한 것은 아니다.

## GE 계산 후 스킬별 최종 피해 상한

2026-10-08: `FRPGSkillSecurityProfile.MaximumFinalDamagePerHit`를 추가했다.
기존 `MaximumDamagePerHit`는 배율을 적용한 **입력 SetByCaller** 한계이고,
새 필드는 공격력·방어력·치명타 계산 후의 한계다. 1~1,000,000,000 범위의 유한한
값만 허용한다. 상한 초과는 Clamp 적용이 아니라 **피해 거절**이며, 정수 변환 전과
정수→float 메타 Attribute 저장 경계에서 검사한다. 기본값은 기존 전역 한계와 같은
1,000,000,000으로 호환성을 유지했다. BP 에셋의 밸런스 수치를 자동 변경하지 않았다.

정의의 상한은 활성화 RuntimeSpec에 고정되고 서버 내부 `FRPGDamageExecutionGuard`
기록으로 실제 GE 실행까지 이어진다. 기록은 소스 ASC·대상 ASC·컨텍스트에 묶이고
정확히 한 번만 사용한다. 중첩 실행은 독립 기록을 만들고 종료 시 이전 기록을 복원한다.
SetByCaller 이름/태그로 서버 상한을 높일 수 없다. 다른 대상/컨텍스트나 재사용은
기록을 빌리거나 전역 상한으로 우회할 수 없다.

`Apply Authorized Server Damage`와 피해 태그가 있는 `ApplyGameplayEffectSpecHandleToServerHit`
호환 경로에 연결했다. 실제 계산 승인이 없으면 성공 핸들/대상을 반환하지 않고,
SkillContainer의 예약된 타격 예산도 반환한다. 따라서 승인된 타격 기반 후속 연출/보상
경로로 이어지지 않는다. GAS 적용 콜백은 계산 실패에도 발생할 수 있으므로 그 콜백
자체를 피해 승인으로 사용하지 않는다. 직접 서버 ASC 호출은 이 API 계약 밖이며,
별도의 스킬 기록이 없으면 네이티브 TakeDamage의 전역 한계만 적용된다.

상한 우회를 막기 위해 지원 GE는 Instant + 단일 정확한 네이티브 TakeDamage 실행으로
제한한다. 직접 Modifier, 추가 실행, 커스텀 계산 클래스, 실행 Scoped Modifier,
조건부/연결 GE, 지연/주기 효과와 추가 피해 컴포넌트는 거절한다.
컴포넌트는 정확한 네이티브 AssetTags/TargetTags/TargetTagRequirements만 허용한다.
기존 공유 피해 GE의 TargetTagRequirements(무적 등 조건)는 애플리케이션을 제한하며
추가 피해를 실행하지 않는다. 임의 커스텀 GE를 안전하다고 인정한 것이 아니다.
네이티브 실행 계산은 서버 소스/대상과 같은 World도 요구한다.

새 네이티브 자동화 6개는 실제 GAS 실행 및 HP를 사용하는 증폭/경계/위조/추가 효과
거절과 서버 내부 기록의 격리·재사용을 검증한다. 기존 고정 테이블 테스트는 정의 변경이
기존 활성화의 최종 상한을 바꾸지 않는지도 검사한다. NaN/Infinity 계산 자체는 네이티브
기록의 단위 검사이며, 악성 Attribute 패킷을 실제 송신했다고 주장하지 않는다.

검증 기록:

- 보안 자동화 **38개 통과**(새 최종 피해 테스트 6개 포함):
  `Saved/Tests/Security/Automation-ef7f9474494540eea274e0ee0b8702b0/Security.proof.json`.
- 실제 Charge BP 정상 피해 회귀(HP 감소 625, 거절 대상 3개 불변, 실제 HP 알림/복제):
  기본 `Saved/Logs/IrisMultiClient/20261008-221843-79d9dfc2/SkillDamage.proof.json`,
  지연 250~450ms / 손실 10%
  `Saved/Logs/IrisMultiClient/20261008-222308-f69c440c/SkillDamage.proof.json`.
- 위 세 실행 모두 시작/종료 Editor DLL SHA256:
  `EC332E633F04304E0DA5193A6266247540AC60E703AEADD14B1629D21E688329`.
  앞선 `Automation-229597ed2e614590bf03d3d927960ae3`도 38개 통과했으나 다른
  `69DAEED2...3A2F1F4` 모듈이라 최종 네트워크 실행과 같은 빌드의 증거로 합치지 않았다.
  공유 작업의 후속 링크로 현재 DLL이 바뀔 수 있으므로 이 증거는 위 해시에 한정된다.
- Editor `-NoLink` 분리 컴파일에서 헤더 의존성 `GameFramework/Pawn.h`를 명시했다.
  `FinalDamageCapNoLink-20261008-Retry.log`의 나머지 5개 CPP 및 수정 후
  `Saved/Logs/FinalDamageCapNoLink-20261008-Verified.log`의 라이브러리 CPP가 통과했다.
  최초 빌드 충돌은 성공으로 인정하지 않았다. 다른 프로젝트 프로세스를 중단하지
  않았으며 자체 링크 요청은 편집기 사용 중/조용한 구간 대기 시간 초과로 유보했다.
  이후 갱신된 모듈에 필수 새 테스트가 포함됐음을 검사하고 위 해시로 실제 실행했다.
- Shipping `-NoLink` 분리 컴파일 7개 통과:
  `Saved/Logs/FinalDamageCapShipping-20261008.log`.
  TargetTagRequirements 호환 화이트리스트까지 포함한 최신 Guard CPP도 추가 분리
  컴파일 통과: `Saved/Logs/FinalDamageCapShippingWhitelist-20261008.log`.
  테스트 Ability의 CanActivate는 `xor al, al; ret`(항상 거절)이며, 테스트용
  직접 HP Modifier 생성 본문도 Shipping에서 제거된다. 클래스 메타데이터는 남는다.
- 합성 증거/CLI 회귀: damage 51개, wire 28개, rate 42개, movement 21개 통과.

이 검증은 네이티브 최종 상한의 거절/HP 불변과 실제 BP **정상 피해** 회귀다.
실제 BP에서 상한을 초과하도록 만든 네트워크 타격, 악성 wire RPC의 실제 HP 불변,
모든 스킬의 치명타/버프/저방어 경계와 에셋별 상한 튜닝, 여러 플레이어 피해 격리,
전체 Shipping 링크/Cook/패키지/실행은 아직 별도 검증이 필요하다.

## TargetData 악성 wire 요청과 실제 HP 검증

2026-10-08: `-VerifyTargetDataDamage`를 추가했다. 기존 2클라이언트/12단계 wire 검증에
서버가 생성한 peer별 native 피해 대상, 생산용 `Apply Authorized Server Damage`,
native TakeDamage GE 실행과 `RPGAttributeSet`의 실제 HP 감소를 연결한다.
캐릭터·스킬 BP나 에셋 수치, 생산용 RPC 제한은 변경하지 않았다.

두 클라이언트가 **두 대상 모두의 실제 초기 HP 1000**을 받은 뒤, 소유 ASC의 개발 전용
준비 RPC로 서버에 알린다. 서버는 두 연결이 준비되기 전에는 fixture 활성화를 시작하지
않는다. 클라이언트는 HP를 직접 설정하거나 예상 HP 변수를 복제해 성공으로 판정하지 않는다.
서버의 실제 AttributeSet HP, 실제 GE 적용 delegate, Iris TargetASC NetRef ID와 각 클라이언트의
실제 HP 변경 알림을 서로 비교한다. 지연 때문에 여러 HP 변경이 한 번에 복제되는 경우는
허용하되, 초기 1000부터 최종 950까지 단조 감소하는 실제 알림 이력을 요구한다.

Baseline / Reversed / Recovery / OldActivationReplay / ForeignOwner는 각 1회 정상 요청을
포함하므로 각각 HP -10과 GE 1회를 요구한다. Reversed의 역순 메시지와 OldActivationReplay의
이전 활성화 메시지는 **추가** 피해를 만들면 안 된다. Duplicate / WrongActivation / ForeignSpec /
OversizedActors / DuplicateCancel / EndedReplay / Reactivate는 HP 변화와 피해 GE가 모두 0이어야
한다. 취소 승인 자체도 피해를 적용하지 않는다. 따라서 peer별 승인 5회/피해 50/최종 HP 950이다.

정상 승인 경로의 네 요청에도 고의로 **상대 peer의 피해 대상 Actor**를 TargetData로 제출한다.
fixture는 제출 대상·수치를 피해 권한으로 사용하지 않고 서버가 고정한 자기 peer의 대상으로
native hit를 구성한다. 각 실제 피해 적용 직전·직후에 상대 대상 HP가 그대로인지 확인하며,
판정기는 양 peer의 GE 로그 순서로 두 HP 이력을 재구성한다. ForeignOwner는 상대의 실제 활성
토큰을 사용한 저수준 Iris RPC와 자기 소유의 동일 전송 방식 정상 요청을 함께 수행한다.
상대 ASC NetRef ID에 대한 **서버 Iris 수신 거절**도 기존 wire 판정기로 반드시 확인한다.
두 peer의 모든 checkpoint 이후 3초 동안 실제 HP/GE/승인 횟수가 유지되어야 서버를 완료한다.
두 클라이언트가 두 대상의 최종 HP와 변경 알림을 받기 전에는 전체 테스트를 완료하지 않는다.

```powershell
.\Scripts\Network\Run-IrisMultiClientTest.ps1 -SkipBuild -VerifyTargetDataDamage -ClientCount 2 -TimeoutSeconds 180
.\Scripts\Network\Run-IrisMultiClientTest.ps1 -SkipBuild -VerifyTargetDataDamage -ClientCount 2 -TimeoutSeconds 180 -PacketLagMinMs 250 -PacketLagMaxMs 450 -PacketLossPercent 10
.\Scripts\Tests\Test-TargetDataDamageProof.ps1
```

이 옵션은 Editor/정확히 두 클라이언트 전용이며 abuse 옵션을 자동으로 켠다. Rate와 스킬 입력·BP
피해·이동 등 다른 probe는 함께 실행하지 않는다. 테스트 전 Editor 모듈을 빌드한다. 실행 전후
DLL SHA256이 다르면 성공 파일을 만들지 않는다. 독립 실행 디렉터리와 소유 PID 정리를 사용한다.
기본 `TargetData.proof.json`의 실제 전투 미검증 범위는 유지하고, 확장 검증은 별도
`TargetDataDamage.proof.json`에 기록한다.

검증 기록:

- Editor 컴파일·링크 성공: `Saved/Logs/TargetDataDamageBuild-20261008.log`.
  최초 단일 CPP 컴파일에서 복제 매크로 인자 이름 오류를 발견해 수정 후 전체 링크했다.
- 기본 환경: `Saved/Logs/IrisMultiClient/20261008-225356-f37b2a80/TargetDataDamage.proof.json`.
- 각 방향 지연 250~450ms / 손실 10%:
  `Saved/Logs/IrisMultiClient/20261008-225454-3f6e4b23/TargetDataDamage.proof.json`.
- 보안 자동화 **38개 통과**:
  `Saved/Tests/Security/Automation-47ccfc5c483f4e36a7b42f3612e7e8b9/Security.proof.json`.
- 기존 요청 폭주 경로의 고지연·손실 회귀:
  `Saved/Logs/IrisMultiClient/20261008-225622-11c24f1d/TargetDataRate.proof.json`.
  생산용 64회 제한과 실제 96회 burst, 정상 peer 격리·복구 검증을 유지한다.
- 실제 Charge BP 정상 피해/HP 복제 기본 회귀:
  `Saved/Logs/IrisMultiClient/20261008-225622-5425b1ab/SkillDamage.proof.json`.
- 위 다섯 실행 모두 실행 전후 Editor DLL SHA256:
  `C633F6B790ED0B53938A382992D0D09D21621AE7C57B6DC78C9EA7416A2EF743`.
- 확장 증거/CLI 판정기 **41개 통과**: 정상·합쳐진 HP 알림, HP/대상/GE 횟수·순서 위조,
  누락·중복, 패킷 설정 불일치, 잘못된 옵션 조합을 검사한다. 이 숫자는 합성 로그 판정기
  테스트 수이며 실제 타격/네트워크 실행 횟수가 아니다. 기존 wire 28 / rate 42 / BP damage 51 /
  movement 21 판정기 회귀도 통과했다.
- Shipping `-NoLink`에서 wire CPP **실제 1개 컴파일 통과**:
  `Saved/Logs/TargetDataDamageShipping-20261008.log`.
  해당 SingleFile object의 준비 RPC/피해 적용/완료 tick 본문은 `ret 0`, 클라이언트 준비 검사는
  항상 false인 `xor al, al; ret`이다. 피해 GE 실행 등록도 Shipping에서는 컴파일에서 제외한다.
  native 클래스/RPC 메타데이터는 남으며 전체 Shipping 링크/Cook/패키지 실행 증거는 아니다.

이 결과는 **생산용 RPC 방어 → native 서버 고정 hit → 생산용 GE/실제 HP → 두 클라이언트 복제**의
통합 검증이다. 실제 플레이어/BP 스킬의 TargetData 소비·몽타주·충돌 query를 경유한 악성 타격,
생산용 Pawn 팀 판정/NPC 사망·리셋, BP에서 최종 피해 상한을 초과시킨 네트워크 타격, 대규모
동시 접속·장시간 flood/DoS·완전한 Shipping 실행은 검증하지 않았다. 앞 절에서 남겨 둔 악성
wire HP 검증 중 **격리 native 경로**를 완료한 것이며, authored BP 경로는 후속 범위로 남는다.

## 실제 Charge BP의 악성 TargetData와 정상 해제 복구

2026-10-09: `-VerifySkillTargetDataAbuse`를 추가했다. 서버가 지급한 기존
`GA_Player_ChargeSlash`의 Enhanced Input을 유지한 채, 실제 플레이어 ASC에
`ServerSetReplicatedTargetData` RPC를 전송한다. 입력을 직접 종료하거나 피해 함수를 호출하는
대신 모든 악성 요청의 서버 검증과 HP 불변 확인이 복제된 뒤 정상 입력을 해제한다.
캐릭터/스킬/몽타주 BP 자산, 생산용 RPC 허용량과 제재 임계값은 변경하지 않는다.

최초 활성화와 실제 입력 Press에서 정상 TargetData가 각각 전송되는 것을 먼저 확인한다.
두 요청의 플레이어 ASC Iris NetRef ID, SpecHandle, 원래 활성화 키를 서버/클라이언트에서
비교한다. 재전송에는 최초 실제 메시지 키를 사용하고, 새로운 요청은 GAS의
`FScopedPredictionWindow`로 키를 생성한다. 임의의 큰 키로 정상 Release를 막지 않는다.

| 요청 | RPC admission | 실제 SkillContainer 소비 결과 |
| --- | --- | --- |
| 최초 메시지 그대로 재전송 | 거절 | 기존 정상 대상 유지 |
| 같은 메시지 키에 Actor payload로 바꿔 재전송 | 거절 | 기존 정상 대상 유지 |
| 다른 활성화 키 | 거절 | 기존 정상 대상 유지 |
| 직렬화 가능한 31+31+3개 Actor 슬롯 | 거절 | 기존 정상 대상 유지 |
| 사거리 밖 대상의 ActorArray만 제출 | 허용 | 위치 데이터 없는 형식을 거절, 현재 대상 무효화 |
| 유효한 위치 형식으로 1,000,000 단위 밖 좌표 제출 | 허용 | 서버 거리 검증 거절, 현재 대상 무효화 |
| 피해 SetByCaller 태그를 실행 이벤트 태그로 제출 | 허용 | 허용되지 않은 이벤트 태그 거절, 현재 대상 무효화 |

RPC를 통과한 마지막 세 요청은 피해 승인이 아니다. 각 요청마다 실제 SkillContainer의
KnownTag/Decoded/Validated 상태, 활성 Ability 상태, 피해 이벤트 0회와 네 대상의 실제
AttributeSet HP/GE 횟수를 기록한다. 정상 입력 해제의 새로운 TargetData가 **같은 활성화**의
대상을 다시 검증한 뒤 기존 BP 몽타주 Notify/HitQuery/피해 GE가 정상 적 한 명만 타격해야 한다.
기존 뒤쪽·아군·사거리 밖 대상의 HP 불변과 클라이언트 HP 변경 알림/복제도 그대로 요구한다.
피해 대상으로는 기존 native fixture Pawn을 사용하며, 새 피해 RPC나 합성 피해 이벤트는 없다.

```powershell
.\Scripts\Network\Run-IrisMultiClientTest.ps1 -SkipBuild -VerifySkillTargetDataAbuse -ClientCount 1 -TimeoutSeconds 180
.\Scripts\Network\Run-IrisMultiClientTest.ps1 -SkipBuild -VerifySkillTargetDataAbuse -ClientCount 1 -TimeoutSeconds 180 -PacketLagMinMs 250 -PacketLagMaxMs 450 -PacketLossPercent 10
.\Scripts\Tests\Test-SkillTargetDataAbuseProof.ps1
```

이 옵션은 Editor/정확히 한 클라이언트 전용이며 스킬 입력/피해 검증을 자동으로 켠다.
다른 probe와 분리해 실행한다. `-SkipBuild`는 **이 코드가 링크된 모듈이 이미 준비된 경우**에만
사용한다. 실행 전후 DLL SHA256이 같아야 별도의 `SkillTargetDataAbuse.proof.json`이 생성된다.
기존 `SkillDamage.proof.json`의 일반 피해 검증 범위는 확장했다고 주장하지 않는다.

확인된 범위 (2026-10-09):

- 실제 Editor DLL 링크 완료:
  `Saved/Logs/RaidWeaponSweepFinalBuild-20261009.log`의 75개 action 중
  `UnrealEditor-Project_RPG.dll` 링크와 최종 `Result: Succeeded`를 확인했다.
  후속 `Saved/Logs/SkillTargetDataAbuseBuild-20261009-Resume.log`는 UHT 생성 파일 변경
  0개/빌드 action 0개인 최신 상태 재확인이며 별도의 컴파일 증거로 세지 않는다.
  Live Coding 패치는 실행하지 않았다.
- 실제 Charge BP 악성 RPC **7개와 요청별 네 대상 HP 불변**, 정상 입력 해제 후
  적 한 명의 HP **1,000,000 → 999,375** 및 클라이언트 HP 변경 알림/복제 통과:
  - 기본 환경: `Saved/Logs/IrisMultiClient/20261009-223512-31194745/SkillTargetDataAbuse.proof.json`.
  - 각 방향 250~450ms 지연/패킷 손실 10%:
    `Saved/Logs/IrisMultiClient/20261009-224135-b339bda3/SkillTargetDataAbuse.proof.json`.
  RPC admission에서 네 요청을 거절하고, 나머지 세 요청은 실제 authored 소비 단계에서
  형식/거리/이벤트 태그 검증으로 거절했다. 정상 Release의 새 메시지로 같은 활성화의
  대상을 복구했고 기존 BP 몽타주/Notify/GE가 승인된 적에게만 **625 피해**를 적용했다.
- 새 DLL에서 기존 **2인 native wire 피해 격리 회귀 통과**:
  `Saved/Logs/IrisMultiClient/20261009-224243-a190c42c/TargetDataDamage.proof.json`.
  연결당 12개 요청 검증, 거절/재전송 시 HP 불변, 다른 소유자의 RPC 수신 거절,
  제출된 다른 대상 무시, 각 연결의 승인 피해 50 및 두 클라이언트의 양쪽 HP 복제를
  확인했다. 이 결과는 여러 플레이어의 **authored BP 스킬** 격리를 증명하지 않는다.
- native 보안 자동화 **38개 통과**:
  `Saved/Tests/Security/Automation-96ddf3c72c384d9f878ecec21da04344/Security.proof.json`.
  위 세 네트워크 실행과 보안 자동화는 같은 Editor DLL SHA256
  `276D16CB0B5FD6D1353DFF257331B4C303CA4162851E833F27F86D74E7F44846`에 연결된다.
  네트워크 판정기는 실행 전후 모듈 해시가 다르면 증거를 생성하지 않는다.
- 합성 로그 증거/CLI 판정기 **49개 통과**, 기존 일반 BP 피해 판정기 **51개 통과**.
  이 숫자는 실제 악성 RPC 전송 횟수가 아니다. 실제 실행은 연결당 위 일곱 요청을 보낸다.
- 수정한 C++ 네 파일은 MSVC로 별도 출력 폴더에서 컴파일 통과:
  `Saved/Tests/SkillTargetDataAbuseCompile-20261009/`.
  기존 PCH/UHT 산출물을 사용한 분리 컴파일이며 DLL 링크·실행 성공을 의미하지 않는다.
- Shipping UHT 갱신 및 `-NoLink` **네 CPP 실제 컴파일 통과**:
  `Saved/Logs/SkillTargetDataAbuseShipping-20261009.log`.
  해당 SingleFile COFF에서 `HoldForAbuseVerification`은 `32 C0 C3`(항상 false),
  `ObserveSentTargetData`/`ObserveReceivedTargetData`/`ObserveTargetData`는
  `C2 00 00`(빈 `ret 0`) 본문임을 확인했다. 오브젝트 해시와 함수 바이트 요약은
  `Saved/Tests/SkillTargetDataAbuseShipping-20261009/Shipping.proof.json`에 기록했다.
  대형 임시 COFF 덤프는 정리했고 보존된 오브젝트로 재생성할 수 있다.
  최초 수동 검사의 PCH/UHT 불일치 실패는 통과로 세지 않고 정식 UHT/분리 빌드로
  재검증했다. 전체 Shipping 링크/실행 증거는 아니다.
- 초기 Editor 빌드 중 다른 작업의 헤더 변경으로 발생한 UHT 줄 번호 불일치는
  `-ForceHeaderGeneration`으로 해소했다. 인벤토리 드래그 테스트의 `FPaintArgs` 첫 인수는
  `TSharedRef::Get()` 참조의 주소로 한 줄 수정했고 분리 컴파일 및 최종 Editor 빌드는
  통과했다. 인벤토리 게임플레이/드래그 동작을 런타임 검증했다는 뜻은 아니다.
  초기 실패/중단 로그와 취소된 대기 로그 `SkillTargetDataAbuseBuild-20261009-Verified.log`는
  링크 성공 증거로 세지 않는다. 공유 빌드의 `WITH_LIVE_CODING` 값 차이로 발생하는
  반복 전체 컴파일은 기본 Editor 설정을 유지해 피했다.

이 검증은 ChargeSlash 한 스킬의 악성 조준 데이터 거절과 정상 입력 복구를 대상으로 한다.
여러 플레이어의 동시 악성 스킬 격리, 다른 BP 스킬, 생산용 NPC 사망/리셋, BP 경유 최종 피해
상한 초과, 장시간 flood/DoS, 전체 Shipping 링크/Cook/패키지 실행은 후속 범위다.
관찰·주입 본문은 `!UE_BUILD_SHIPPING`에 한정한다. native fixture 클래스와 복제 필드
메타데이터 자체를 Shipping에서 제거했다는 뜻은 아니다.

## 두 플레이어의 동시 Charge BP 악성 요청 격리

2026-10-10 검증 완료: `-VerifySkillMultiPeerAbuse`는 정확히 두 Editor 클라이언트의 기존
`GA_Player_ChargeSlash`를 같은 서버에서 활성화한다. 플레이어별 ASC Iris NetRef ID,
PlayerState의 PlayerId, SpecHandle/활성화 키와 네 대상의 NetRef ID를 각각 묶어서 검증한다.
1인 로그를 두 번 합쳐서 다중 접속 증거로 사용하지 않는다.

두 클라이언트가 **양쪽 대상 여덟 개의 실제 HP 기준값과 변경 delegate**를 준비한 뒤,
각자 소유한 native 조정 Actor의 준비 RPC를 보낸다. AI가 빙의한 피해 대상 Pawn의
네트워크 소유권을 변경해서 준비 RPC를 전달하지 않는다. 이 RPC는 피해를 적용하거나 스킬을 승인하지
않으며 서버가 두 준비 요청을 받기 전에는 테스트 입력을 시작하지 않는다.
최초 Started 입력은 비활성 Ability를 활성화하므로 활성 Spec의 추가 Press를 반드시
발생시키지는 않는다. fixture는 실제 활성화 후 기존 Enhanced Input의 Started 바인딩을
한 번 더 실행한다. `CLIENT_INPUT_PRIMED ... FixtureBinding=1`으로 이 제어된 재-Press를
명시하고, 동일 Ability가 생성한 새 GAS 키/TargetData의 실제 서버 소비를 요구한다.
물리 키를 두 번 눌렀다는 증거나 입력을 종료했다는 뜻은 아니다.
서버에서 두 기존 BP 스킬의 실제 초기/제어된 재-Press TargetData가 검증·소비된 뒤에만 각 클라이언트가
일곱 악성 요청을 전송한다. ActorForgery 요청에는 상대 플레이어의 승인 대상 Actor를 넣는다.

각 악성 수신마다 자기 대상 HP 네 개에 더해 **상대의 실제 활성 스킬/ASC/활성화 키,
피해 이벤트 0회와 상대 대상 HP/GE 횟수 네 개**를 검사한다. 두 연결의 총 14개 요청을
모두 검증하기 전에는 어느 클라이언트도 정상 입력을 해제하지 않는다. 이후 각 기존 BP의
정상 Release/몽타주 Notify/서버 query/GE를 거쳐 자기 승인 적 한 명만 타격해야 하며,
두 클라이언트에서 양쪽 대상 여덟 개의 최종 HP와 두 변경 알림을 확인한다.

피해 대상은 native fixture이고 플레이어는 공간적으로 분리한 정지 fixture다.
서버가 fixture 준비 때만 플레이어 위치를 옮기고 이동을 멈춘 뒤 이동 기준값을 재설정한다.
이동/텔레포트 보안 검증의 증거가 아니며 RPC 허용량, 제재 임계값, authored 최대 차지
시간과 스킬/BP 자산은 바꾸지 않는다. 준비 RPC와 새 관찰/주입 본문은 Shipping에서 비활성이다.
native 클래스/복제/RPC 메타데이터 자체가 Shipping에서 제거된다는 뜻은 아니다.

```powershell
.\Scripts\Network\Run-IrisMultiClientTest.ps1 -SkipBuild -VerifySkillMultiPeerAbuse -ClientCount 2 -TimeoutSeconds 300 -MaxFPS 30
.\Scripts\Network\Run-IrisMultiClientTest.ps1 -SkipBuild -VerifySkillMultiPeerAbuse -ClientCount 2 -TimeoutSeconds 300 -MaxFPS 30 -PacketLagMinMs 250 -PacketLagMaxMs 450 -PacketLossPercent 10
.\Scripts\Tests\Test-SkillMultiPeerAbuseProof.ps1
```

다른 probe/Progression/Tripod와 분리해 실행하며 이 코드가 링크된 DLL이 먼저 필요하다.
`SkillMultiPeerAbuse.proof.json`만 별도 생성하고 기존 1인 `SkillDamage.proof.json` 또는
`SkillTargetDataAbuse.proof.json`의 검증 범위를 조용히 확장하지 않는다. 양쪽 활성화가
겹친 수신 순서, 상대 HP 기록의 요청별 위치, 서로 다른 실제 ASC/대상 ID, 양쪽 클라이언트
복제와 실행 전후 동일 DLL 해시를 판정기가 요구한다.

확인한 증거:

- Editor UHT/실제 C++ 컴파일/링크 통과:
  `Saved/Logs/SkillMultiPeerBuild-20261009-Priming.log`. 조정 Actor와 제어된 재-Press를 포함한다.
- 실제 기본 2인 통과:
  `Saved/Logs/IrisMultiClient/20261010-034250-d4f5855e/SkillMultiPeerAbuse.proof.json`.
- 실제 각 방향 250~450ms 지연/10% 손실 2인 통과:
  `Saved/Logs/IrisMultiClient/20261010-034336-eaaff97f/SkillMultiPeerAbuse.proof.json`.
  두 실행 모두 연결당 7개, 총 **14개 실제 악성 RPC** 동안 자기·상대 대상 HP/GE 불변과
  상대의 활성 스킬을 확인했다. 정상 입력 해제 후 각 승인 적 한 명만 **625 피해**를 받았고,
  양쪽 클라이언트에 양쪽 HP와 변경 알림이 복제됐다.
- 같은 DLL의 native 보안 자동화 **38개 통과**:
  `Saved/Tests/Security/Automation-022f583cd92349588c2aea7002552cb2/Security.proof.json`.
- 같은 DLL의 기존 1인 정상 입력/피해 경로 회귀 통과:
  `Saved/Logs/IrisMultiClient/20261010-034920-abbac3fe/SkillDamage.proof.json`.
- 위 네 실행의 DLL SHA256은
  `025B79CDC946D02FC0DA14D4D7C1F52ED3D659C7F86A2D83F795C2C752362AC7`로 같다.
  이전 기본 통과 기록 `20261009-232254-3fcccb45`의 DLL은 `F3E1917...`로 다르다.
  이후 공유 DLL이 바뀌어 현재 DLL에서 기본/지연·손실/native/1인 회귀를 다시 수행했다.
  중단되어 완료 JSON이 없는 `20261009-232505-10778e35`와
  `Automation-8458cd6b6e504d659519ddf78a391442`는 통과로 세지 않는다.
- 합성 증거/CLI 판정기 **52개 통과**, 기존 1인 악성 **49개**/일반 피해 **51개** 회귀 통과.
  이는 판정기 테스트 수이며 실제 악성 RPC 수가 아니다. 이전 기본 로그도 최신 판정기로 재검사했다.
- Shipping UHT 갱신 및 `-NoLink` **세 CPP 실제 컴파일 통과**:
  `Saved/Logs/SkillMultiPeerShipping-20261010.log`. 준비 RPC 구현, probe Tick과 두 TargetData
  관찰 함수는 해당 COFF에서 `C2 00 00`(빈 본문), `HoldForAbuseVerification`은
  `32 C0 C3`(항상 false)다. 세 오브젝트 모두 multi-peer/제어된 재-Press 개발 표식이 없으며,
  생성된 coordinator 클래스/RPC 메타데이터는 남음을 확인했다. Editor DLL은 전후 동일했다.
  원본 오브젝트 사본과 해시는
  `Saved/Tests/SkillMultiPeerShipping-20261010-Verified/Shipping.proof.json`에 보존한다.
  `Scripts/Tests/Test-SkillMultiPeerShipping.ps1`로 함수 바이트/표식/실제 컴파일 기록을 판정하며,
  기존 증거 덮어쓰기와 Editor 로그의 Shipping 증거 오인도 거절한다.
  전체 Shipping 링크/Cook/패키지 실행 증거는 아니다.

Shipping 판정기는 성공한 `-NoLink` 로그와 현재 소스보다 오래되지 않은 SingleFile 오브젝트를
요구한다. 재실행 때는 기존 증거와 다른 새 디렉터리를 지정한다.

```powershell
.\Scripts\Tests\Test-SkillMultiPeerShipping.ps1 -BuildLog .\Saved\Logs\SkillMultiPeerShipping-20261010.log -ProofDirectory .\Saved\Tests\SkillMultiPeerShipping-recheck
```

초기 실패는 통과 기록과 구분한다. AI 소유 대상 Pawn의 준비 RPC가 도달하지 않는 문제는
별도 플레이어 소유 Actor로 해결했고, 최대 차지 자동 해제가 정상 입력 해제보다 먼저 발생한
실행은 판정기가 거절했다. 위 제어된 재-Press로 fixture 의존성을 제거한 뒤에만 통과했다.
첫 native 실행의 로딩 중 180초 초과와 초기 `GetPawn()` 포인터 추론 컴파일 오류도 통과로 세지 않는다.

이 테스트는 ChargeSlash 두 소스의 타격/TargetData 격리만 검증한다. 장시간 flood/DoS,
다른 BP 스킬, 생산용 NPC 사망/리셋과 전체 Shipping 패키지 실행은 후속 범위다.
이 단계에서 남겨 둔 BP 경유 최종 피해 상한 초과는 아래 별도 테스트로 검증한다.

## 실제 Charge BP의 최종 피해 상한 경계

`-VerifySkillFinalCap`은 기존 `GA_Player_ChargeSlash`의 Enhanced Input, 입력 해제,
Release 몽타주/Notify, 서버 HitQuery와 `GE_Shared_ApplyDamage`를 그대로 실행한다.
서버에서 승인된 동일한 타격 안에 정상 적과 상한 초과 적을 두고 실제 native 실행 영수증의
승인 결과를 관찰한다. 직접 피해 이벤트나 별도 피해 RPC로 몽타주를 대신하지 않는다.

테스트 전용 fixture는 실제 비활성 스킬 인스턴스의 Definition을 transient 복제하고
`MaximumFinalDamagePerHit`만 **625**로 설정한다. 서버와 소유 클라이언트 모두 활성화
전에 준비하며 원본 DataAsset/CDO, 활성 RuntimeSpec, 기존 BP 자산은 수정하지 않는다.
SetByCaller 250, 서버 공격력 2.5, 치명타 확률 0은 기존 정상 피해 fixture와 같다.
기존 정상 적의 방어력 1에서는 최종 피해 **625**가 정확히 승인된다. 추가 native 적은
같은 적 팀/쿼리 내부 위치 `(300, -120, 0)`에 있고 방어력만 0.5여서 **1,250**이 계산된다.
이 값은 전역 상한 1e9보다 작지만 해당 스킬의 고정된 최종 HP 피해 상한 625를 넘는다.

상한 초과 적에도 GAS 적용 콜백은 한 번 발생한다. 이를 피해 승인으로 세지 않고 실제
`FRPGDamageExecutionGuard`의 동일 ASC/대상/활성화에 결합된 거절 결과와 대조한다.
서버/클라이언트 HP는 1,000,000 그대로이며 HP 변경 알림도 없어야 한다. 승인 타격 목록은
정상 적 한 개, 소비한 서버 타격 예산은 한 단위여야 한다. 따라서 거절된 적이 적용 목록에
들어가거나 예약했던 예산을 반환하지 않는 경우도 실패한다. 기존 네 대상의 기하/팀 거절,
정상 625 피해와 실제 HP 변경 알림/복제 증거를 함께 요구한다.

```powershell
.\Scripts\Network\Run-IrisMultiClientTest.ps1 -SkipBuild -VerifySkillFinalCap -ClientCount 1 -TimeoutSeconds 300 -MaxFPS 30
.\Scripts\Network\Run-IrisMultiClientTest.ps1 -SkipBuild -VerifySkillFinalCap -ClientCount 1 -TimeoutSeconds 300 -MaxFPS 30 -PacketLagMinMs 250 -PacketLagMaxMs 450 -PacketLossPercent 10
.\Scripts\Tests\Test-SkillFinalCapProof.ps1
```

현재 코드가 포함된 DLL을 먼저 빌드해야 한다. Editor/한 클라이언트 전용이고 악성
TargetData/2인 테스트, Tripod/진행도/이동 테스트와 섞으면 CLI에서 거절한다.
서버와 클라이언트에 `-RPGSkillFinalCapE2E`를 전달하고 이번 실행에는
`SkillFinalCap.proof.json`만 생성한다. 새 plain native helper에는 UCLASS/RPC/BP 노드가
없다. 생산용 판정의 비교식, 허용량, 제재 임계값은 바꾸지 않았으며 관찰 호출은 Shipping에서
컴파일에서 제외한다.

`SkillFinalCapProof.ps1`은 기존 정상 피해 판정에 더해 서버 9개/클라이언트 4개 표식의
유일성, 실제 Iris ASC/다섯 대상 ID, 동일 Handle/OriginalPredictionKey, 프로필 준비부터
입력/실행/콜백/승인 목록/종료까지의 순서와 마지막 유효 패킷 설정을 대조한다. 표식 일부가
빠졌거나 중복·추가·다른 활성화의 증거가 섞여도 실패한다. 러너는 실행 전후 DLL 해시가
같아야 증거를 저장한다. 새 JSON의 `finalPerSkillHealthDamageCapVerified=true`만 이 범위를
추가하며, 중첩된 기존 `normalDamageProof`의 동명 false 필드는 원래 정상 경로 범위를 유지한다.

2026-10-10 실행 기록:

- 기본 환경: `Saved/Logs/IrisMultiClient/20261010-143009-1d1e7703/SkillFinalCap.proof.json`.
- 각 방향 250~450ms 지연/손실 10%:
  `Saved/Logs/IrisMultiClient/20261010-143125-ebf94acb/SkillFinalCap.proof.json`.
  두 실행 모두 정확한 상한 625 승인, 1,250 거절, 거절 대상 양쪽 HP 불변과 적용 타격/예산
  한 단위를 확인했다. DLL SHA256은
  `7C1BF26CFB7C6CEE30AB4D94AEA2AA46EBB45719CA19A924E8BDDD5469B2C5B9`이다.
- 같은 DLL의 native 보안 자동화 **38개 통과**:
  `Saved/Tests/Security/Automation-45a451a13d364b6191c67cc62488e349/Security.proof.json`.
  새 fixture 없이 실행한 기존 1인 BP 정상 피해 회귀도 통과했다:
  `Saved/Logs/IrisMultiClient/20261010-143542-dd652888/SkillDamage.proof.json`.
- 같은 DLL의 기존 2인 악성 요청 회귀는 고정 지연 100ms/손실 0%/60FPS에서 통과했다:
  `Saved/Logs/IrisMultiClient/20261010-143620-867a94ae/SkillMultiPeerAbuse.proof.json`.
  연결당 일곱 악성 RPC, 상대 대상 위조 거절, 양쪽 HP 불변과 두 정상 625 타격/복제를
  기존 판정기 그대로 확인했다. 최종 상한 테스트와 달리 `RPGSkillFinalCapE2E`는 사용하지 않는다.
- 새 합성 증거/CLI 판정기 **51개** 통과. 기존 정상 피해 51개, 1인 악성 요청 49개,
  2인 악성 요청 52개 판정기도 통과했다. 이는 실제 네트워크 실행과 구분한다.
- Shipping `-NoLink` **네 CPP 실제 컴파일 통과**:
  `Saved/Logs/SkillFinalCapNetworkShipping-20261010.log`. 새 helper/기존 컴포넌트/피해 Guard/
  SkillContainer를 검사했다. helper의 프로필 준비는 false 반환, Tick/실행/적용 관찰과
  컴포넌트의 관찰 브리지 두 개는 빈 반환이다. 실제 COFF 섹션의 여섯 함수 바이트와 네
  오브젝트의 ASCII/UTF-16 테스트 표식 부재를 판정하고, 오브젝트 복사본/소스/헤더/로그
  해시를 `Saved/Tests/SkillFinalCapNetworkShipping-20261010-Verified/Shipping.proof.json`에
  보존했다. Editor 로그를 Shipping 증거로 쓰거나 기존 증거를 덮어쓰는 요청도 거절한다.

Shipping 판정기를 재실행할 때는 네 SingleFile 오브젝트가 현재 소스/헤더보다 오래되지
않아야 하며 새 증거 디렉터리를 지정한다. 전체 링크/Cook/패키지/실행 증거는 아니다.

```powershell
.\Scripts\Tests\Test-SkillFinalCapShipping.ps1 -BuildLog .\Saved\Logs\SkillFinalCapNetworkShipping-20261010.log -ProofDirectory .\Saved\Tests\SkillFinalCapShipping-recheck
```

공유 Editor 빌드로 DLL이 갱신된 뒤 위 두 네트워크 실행을 새 해시에 맞춰 다시 수행했다.
이전 B5E0260B... DLL의 04:09/04:10 실행을 새 DLL 증거와 합치지 않는다. 전용 모듈 빌드
`Saved/Logs/SkillFinalCapNetworkModuleBuild-20261010-Resume.log`는 성공했지만 최신 상태의
0-action 완료이므로 이를 새 컴파일 증거로 세지 않는다. 초기 전체 타깃의 공유 Tripod
컴파일 오류/플러그인 DLL 잠금 실패도 성공 기록으로 세지 않는다.

2인 회귀의 첫 무지연/30FPS 실행 `Saved/Logs/IrisMultiClient/20261010-143255-1a233d6a/`는
엄격한 종료 표식 판정에서 실패했으며 성공 증거 JSON을 만들지 않았다. 클라이언트 한 명은
`Cancelled=0 Replicate=0`으로 끝났다. 이는 GAS의 정상 서버 종료 수신과 부합하지만,
당시에는 실제 RPC 출처를 기록하지 않았으므로 그 실행의 종료 출처를 소급 입증하지 않는다.
서버 종료와 양쪽 Release/HP 결과는 정상이어도 당시 판정기는 클라이언트 자체 종료의
`Replicate=1`을 요구했다. 생산용 종료 동작/보안 정책을 바꾸지 않고 고정 지연 환경에서
회귀를 분리했다. 아래 별도 종료 출처 검증과 구분하며, 100ms 성공 기록을 무지연 성공으로
표시하거나 실패 로그에 성공 증거를 새로 만들지 않는다.

이는 transient 프로필과 낮은 방어력의 native Pawn으로 **authored BP 경유 상한 차단**을
검증한 것이다. 악성 스탯 RPC, 운영 BP/DataAsset의 실제 상한 튜닝, 생산용 NPC 사망/리셋,
다른 BP 스킬, 장시간 flood/DoS와 전체 Shipping 실행은 아직 검증하지 않았다.

## Charge 종료 출처와 무지연 2인 검증

GAS의 `ClientEndAbility`는 서버 통지로 활성 클라이언트 인스턴스를 끝낼 때
`Replicate=0`을 사용한다. 이 값만 보고 실패하거나 무조건 허용하지 않는다.
`FRPGSkillEndNetworkObservation`은 실제 상속 RPC 구현의 호출 범위에서 ASC 포인터,
Ability 핸들, **전체 예측 키**를 기록하고 실제 `EndAbility` 인자와 비교한다. 중첩 호출이
끝나면 이전 관찰 범위를 복원한다. 새 RPC/BP 노드, 가짜 종료 호출, 운영 종료 정책 변경은 없다.
기존 GAS superclass의 원격 종료 허용/예측 키 검증은 그대로 실행된다.

`RPG_SKILL_END_ORIGIN`은 실제 Iris ASC NetRefId/핸들/키와 다음 출처를 기록한다.

- 서버: `AuthorityLocal`, `Replicate=1`, RPC 일치 표식/키 0만 허용한다.
- 클라이언트 자체 종료: `ClientLocal`, `Replicate=1`, RPC 일치 표식/키 0만 허용한다.
- 실제 서버 통지 수신: `AuthorityRPC`, `Replicate=0`, `RpcMatched=1`, 같은 RPC 키만 허용한다.

양쪽 출처는 실제 활성화의 동일 ASC/핸들/키에 묶이며 Release 이후, 기존 END_TRACE 이전에
정확히 한 번 나타나야 한다. 취소/비활성/재진입/잘못된 보안 정책/다른 연결의 기록은 거절한다.
악성 요청과 상한 테스트는 출처를 실제 TargetData wire 활성화에도 연결한다. 새 runner의
모든 authored 피해 검증은 `-RequireEndOrigin`을 요구하므로 새 DLL을 먼저 빌드해야 한다.
직접 판정기의 구형 로그 호환 모드는 **양쪽 Replicate=1**에 한해 출처 미검증으로 표시한다.
이 모드도 출처 없는 Replicate=0이나 일부만 있는 출처 표식을 승인하지 않는다.

2026-10-10 출처 관찰을 포함한 실제 실행 기록:

- 무지연/손실 0%/서버·클라이언트 30FPS의 독립 2인 실행 세 번 통과:
  `Saved/Logs/IrisMultiClient/20261010-145806-14370c8e/SkillMultiPeerAbuse.proof.json`,
  `20261010-145833-b70729dc/SkillMultiPeerAbuse.proof.json`,
  `20261010-145858-62716bdb/SkillMultiPeerAbuse.proof.json`.
- 각 방향 250~450ms/손실 10%/30FPS의 2인 실행 통과:
  `Saved/Logs/IrisMultiClient/20261010-145921-4fa1f015/SkillMultiPeerAbuse.proof.json`.
  연결당 일곱 악성 RPC, 양쪽 대상 HP 불변, 두 정상 625 피해와 양쪽 복제 검증을 유지했다.
- 같은 DLL의 1인 최종 상한 회귀 통과:
  `Saved/Logs/IrisMultiClient/20261010-145955-b7d5943a/SkillFinalCap.proof.json`.
  native 보안 자동화 38개 통과:
  `Saved/Tests/Security/Automation-78c9d119b2f94bca893a9eb9c1bfb57e/Security.proof.json`.
- 위 기록의 DLL SHA256은
  `345A6ADC8BE2EFD1B52C9A2753610A8CEFF31CE7D64EC5F42DE6E574717DD93E`이다.
  실제 클라이언트 종료 출처는 모두 `ClientLocal`이었다. 이 기록만으로 실제
  `AuthorityRPC` 종료 경로가 실행됐다고 주장하지 않는다.

프레임 차이 회귀는 `-ClientMaxFPS`로 분리한다. 기본값 0은 서버의 `MaxFPS`를 상속하고,
명시 값은 10~240이다. runner는 각 로그의 마지막 실제 `t.MaxFPS` 출력까지 대조해 JSON의
`frameRates`에 남긴다. 서버 60/클라이언트 10FPS의 무지연 2인 실행도 통과했다:
`Saved/Logs/IrisMultiClient/20261010-150304-e5063a42/SkillMultiPeerAbuse.proof.json`.
이 실행은 별도 DLL `504ED650254DC42696FC4D123EA8A7CB4A97C769CD2EE3BF405E6D2787EA2A9C`이며
양쪽 모두 `ClientLocal`이었다. 서로 다른 빌드의 기록을 하나의 DLL 검증으로 합치지 않는다.

### 명시적 서버 종료 수신 fixture

```powershell
.\Scripts\Network\Run-IrisMultiClientTest.ps1 -SkipBuild -VerifySkillEndRace -ClientCount 2 -TimeoutSeconds 600 -MaxFPS 30
```

이 옵션만 `RPGSkillClientEndRaceE2E`를 켜서 실제 클라이언트의 Release 몽타주 재생 속도를
1에서 0.5로 낮춘다. 서버 몽타주/GE/자연 종료, BP 자산/CDO/운영 프로필은 바꾸지 않는다.
관찰 코드가 RPC나 EndAbility를 대신 호출하지 않으며, 기존 GAS 서버 종료 RPC가 아직
활성인 클라이언트를 끝내야 통과한다. 양쪽의 실제 `AuthorityRPC` 출처, 자신의 핸들/키,
Release 이후 지연 적용 순서, 실제 속도 변경을 모두 요구한다. `delayedClientMontageFixture`
및 `authorityRpcEndOnBothClientsVerified`로 일반 실행과 명시적으로 구분한다.
일반 피해/1인 악성 요청/상한 판정기는 지연 표식을 거절한다. 2인 판정기도 명시 옵션 없이
지연 표식이 있거나, 서버에 표식이 있거나, 다른 연결의 표식/순서를 빌리면 실패한다.
이는 **통제된 클라이언트 연출 지연 조건**이지 수정 없는 무지연 환경에서 RPC가 먼저 오는
빈도를 측정한 결과가 아니다. 최초 실패 실행의 출처를 소급 증명하는 것도 아니다.

2026-10-10 실제 지연 fixture는
`Saved/Logs/IrisMultiClient/20261010-195925-994d0f64/SkillMultiPeerAbuse.proof.json`에서 통과했다.
양쪽 클라이언트의 실제 속도는 1→0.5, 종료 출처는 모두 `AuthorityRPC`, RPC 일치/키는 1,
종료는 `Cancelled=0 Replicate=0 Active=1 Ending=0 NetSecurity=0`이었다. 서버의 두 종료는
`AuthorityLocal/Replicate=1`이었다. 실제 ASC NetRefId 22/54와 핸들 19/20은 서버/각 소유
클라이언트에서 일치했다. 악성 요청 14건 동안 양쪽 대상 HP 불변, 정상 625 타격 두 건과
양쪽 HP 알림/복제도 같은 실행에서 검증했다.
지연 설정 없이 실행한 1인 최종 상한 회귀도 통과했다:
`Saved/Logs/IrisMultiClient/20261010-200529-1256a4c8/SkillFinalCap.proof.json`.
지연 fixture 없이 30FPS의 일반 2인 회귀도 다시 통과했다:
무지연/손실 0%는 `Saved/Logs/IrisMultiClient/20261010-200756-7593cb54/SkillMultiPeerAbuse.proof.json`,
각 방향 250~450ms/손실 10%는 `Saved/Logs/IrisMultiClient/20261010-200756-c936963a/SkillMultiPeerAbuse.proof.json`이다.
두 실행 모두 클라이언트 출처는 `ClientLocal`이고 `delayedClientMontageFixture=false`였다.
악성 RPC 14건/정상 두 625 타격/양쪽 HP 복제/마지막 실제 프레임 설정을 유지했다.
native 보안 자동화 38개가 통과한 기록은
`Saved/Tests/Security/Automation-aafc9976a5bd4f5b80a90945cd9733aa/Security.proof.json`이다.
이 다섯 실행의 시작/종료 DLL SHA256은 모두
`90E16873097B6AF9F5380BC01CF63F6D945C454B031B0516C4EB73E3B1104A77`이다.
기존 345A/504E 빌드의 증거와 합치지 않는다. scoped Editor 게임 모듈 빌드
`Saved/Logs/SkillEndOriginFixtureEditor-20261010.log`에서 변경 컴포넌트의 실제 컴파일과
게임 DLL 링크가 성공했다. 전체 플러그인 타깃 빌드 성공이라는 뜻은 아니다.

`Scripts/Tests/Test-SkillEndOriginProof.ps1`의 합성 출처/fixture/CLI 검사 **87개**가 통과했다.
기존 정상 피해 51개/1인 악성 49개/2인 악성 52개/상한 51개 검사도 유지한다. 합성 양성
로그는 실제 네트워크 RPC 실행 증거로 집계하지 않는다. 실제 로그와 빌드 기록은 별도다.

Shipping `-NoLink`로 관찰 helper/상속 RPC 구현/기존 TargetData 검증/SkillContainer/
기존 테스트 컴포넌트의 **다섯 CPP 실제 컴파일**이 통과했다:
`Saved/Logs/SkillEndOriginFixtureShipping-20261010-Retry.log`.
`Scripts/Tests/Test-SkillEndOriginShipping.ps1`은 세 관찰 함수의 실제 COFF 본문이 무동작이고,
상속 RPC 구현은 인자 복사 후 기존 GAS superclass를 한 번 호출하는 것만 남음을 검사했다.
단일 REL32 재배치 대상도 실제 `UAbilitySystemComponent::ClientEndAbility_Implementation`
심볼과 일치했다. 다섯 오브젝트의 ASCII/UTF-16 종료 출처/지연 fixture/TLS 표식은 없었다.
오브젝트 복사본과 소스/헤더/로그 해시는
`Saved/Tests/SkillEndOriginShipping-20261010-FixtureVerified/Shipping.proof.json`에 보존했다.
Editor 로그 제출과 기존 증거 덮어쓰기도 거절했고, 보존한 해시를 대조했다.
전체 Shipping 링크/Cook/패키지/실행 증거는 아니다.

첫 Shipping 시도 `Saved/Logs/SkillEndOriginFixtureShipping-20261010.log`는 공용
`HttpWebManager.h`의 동시 변경과 기존 생성 헤더의 줄 번호 불일치로 실패했다. 다른 작업의
헤더를 수정하지 않고 UHT를 재실행했으며, 실패 로그나 일부 오브젝트를 성공으로 집계하지
않았다. 첫 native 실행 `Saved/Tests/Security/Automation-8968b062b3d44d0ab6009cddcfc7a2cd/`
역시 초기 로딩 중 300초 제한에 도달해 테스트 완료/성공 JSON이 없었다. 900초로 재실행한
위 38개 완료 기록만 새 DLL의 native 회귀 성공으로 집계한다.

물리 키 두 번 입력, 움직이는 생산용 NPC의 사망/리셋, 다른 스킬 BP, 장시간 flood/DoS와
전체 Shipping 패키지 실행은 이번 종료 출처 검증의 범위 밖이다.

## 일반 NPC의 실제 사망 BP와 강제 교체 격리

2026-10-10에는 공통 `GE_ExecCalc_TakeDamage`의 최종 실행 경계에 **현재 서버 생존 상태**
검사를 추가했다. 공격자와 대상 모두 `URPGAttributeSet`이 있고, 현재 HP가 유한한 양수이며,
현재 ASC에 `Shared.Status.Death`가 없어야 기존 계산/승인 경계로 진행한다.
효과 Spec 생성 시 캡처한 태그로 생존을 판단하지 않는다. HP가 0이 된 뒤 사망 태그가
전파되기 전의 재진입도 거절하고, 사망 태그를 가진 양수 HP 상태 역시 거절한다.
기존 피해 상한, 사거리, 진영, 서버 권위와 활성화별 타격 예산은 완화하지 않았다.

같은 실행 계산을 사용하는 직접 GAS 적용에도 이 검사가 적용된다. GAS 효과 적용 핸들만으로
실제 피해 승인/보상을 판단해서는 안 된다. 거절된 실행은 피해/아이덴티티 출력과 승인 receipt를
내지 않고, 승인 API의 적용 대상 목록/성공 반환 및 타격 예산 환급이 실제 승인을 나타낸다.
**사망한 공격자의 사후 폭발이나 지속 피해를 허용하려면 별도 서버 승인 계약이 필요하다.**
단순히 사망 태그를 지우거나 클라이언트 플래그로 공통 생존 검사를 우회하지 않는다.

추가한 native 단위 테스트는 다음 세 개다.

- `ProjectRPG.Security.SkillDamage.DeadCombatantsRejectExecution`: Spec 생성 뒤 HP/태그 변경,
  HP 0/음수/NaN/Infinity, 직접 GAS와 승인 API 거절, 적용 목록/예산 보존, 정상 생존 상태 복구.
  비유한 HP는 native fixture에만 초기화하며 실제 네트워크 속성으로 보내는 검사가 아니다.
- `ProjectRPG.Security.SkillDamage.LethalHitRejectsCorpseReplay`: 실제 lethal GE 후 3회 재시도,
  HP 0/사망 태그 수 1/사용 예산 1 보존과 승인 타격 0.
- `ProjectRPG.Security.SkillDamage.DestroyedHitCannotAliasReplacement`: 파괴된 액터의 실제
  `FHitResult`가 같은 위치의 새 액터/ASC에 적용되지 않고, 새 Hit만 정상 적용됨.

`Run-SecurityAutomationTest.ps1`은 세 테스트의 성공을 필수로 요구한다. 예전 38개만
포함한 DLL은 이번 실행의 성공 증거가 될 수 없다. 새 빌드의 보안 테스트 **41개**가 통과했다.
이 단위 테스트의 대상은 native pawn이며 실제 NPC BP 증거와 혼합하지 않는다.

실제 NPC BP 검사는 별도 필터/스크립트로 실행한다.

```powershell
.\Scripts\Tests\Run-NPCDamageLifecycleTest.ps1 -TimeoutSeconds 900
.\Scripts\Tests\Test-NPCDamageLifecycleProof.ps1
```

빌드가 반영된 Editor DLL이 필요하다. 첫 스크립트는 다음 저장된 자산을 실제로 로드한다.

- NPC: `BP_Dummy_Gdn_C`, startup: `DA_Guardian` 및 실제 startup GE/Ability 부여
- 피해: `GE_Shared_ApplyDamage_C`, 사망: `GA_Guardian_Death_C` 및 상위 `GA_NPC_Death_Base`

독립 Game 월드에서 실제 NPC의 async startup 완료를 기다린 뒤, native 공격자 fixture로
공통 피해 GE를 적용한다. NPC 이동/Brain은 정지하고 실제 사망 몽타주는 계속 tick한다.
바닥 없는 월드의 낙하를 막기 위해 생성 직후 movement를 정지한다. 사망 태그/HP를
테스트가 직접 주입하거나 사망 Ability를 강제로 활성화하지 않는다. 실제 Ability 활성화와
종료 delegate를 관찰하며 강제 취소가 아닌 종료 및 실제 활성 몽타주를 요구한다.

관찰 결과는 HP **70 → 0**, 사망 태그/Ability 활성화 각각 **1회**, 시체 공격 **3회 거절**,
사망 몽타주/Ability 정상 종료였다. 종료 후 서버가 남아 있는 이전 NPC를 파괴하고
같은 좌표에 새 NPC를 생성한다. 새 액터/ASC는 다른 identity, HP 70, 사망 태그 0을 가지며,
이전 Hit/Spec은 거절되고 새 Hit만 **70 → 60**의 피해를 적용한다.

판정기 `NPCDamageLifecycleProof.ps1`은 원본 native `LogTemp: Display:` 관찰 9행과
그 이후의 실제 자동화 성공 1회를 요구한다. AutomationController의 로그 재출력은
관찰 증거로 사용하지 않는다. 사망/재시도 0·1·2/정상 종료/교체/새 피해의 순서,
이전 actor/ASC identity 결합과 새 identity 분리, 정확한 HP 차이 및 fixture 범위를 검증한다.
실패 결과, fatal/assert/ensure 및 `Accessed None` 등 BP 실행 오류도 거절한다.
synthetic 판정기 검사 **68개**가 통과했지만 이는 실제 NPC 실행 횟수가 아니다.

실행 전후 DLL/판정기와 주요 자산 **7개**의 해시가 달라지면 성공 JSON을 만들지 않는다.
NPC proof에는 원본 로그 해시, 실제 관찰 identity/HP, 별도의 범위 플래그가 남는다.
해시는 모든 전이 의존 자산이나 전체 프로젝트 콘텐츠 검증을 의미하지 않는다.

아래 실제 실행은 동일한 Editor 모듈
`C70EF9A61691716E6D7141F144755956FA128D74D14FCA86F3ABEC0522092960`에서 통과했다.

| 검증 | 실제 실행 증거 |
| --- | --- |
| 보안 native 41개 | `Saved/Tests/Security/Automation-89a9dc7ba03742edaf899077f5511257/Security.proof.json` |
| 저장된 NPC 사망·강제 교체 | `Saved/Tests/NPCDamageLifecycle/Automation-2621dbb8ac784ef6b898f6c08253eaa0/NPCDamageLifecycle.proof.json` |
| Charge 1인 최종 상한 625 승인/1250 거절 | `Saved/Logs/IrisMultiClient/20261010-203641-13b133f4/SkillFinalCap.proof.json` |
| Charge 2인 기본 네트워크 회귀 | `Saved/Logs/IrisMultiClient/20261010-204050-fa7f3b4d/SkillMultiPeerAbuse.proof.json` |
| Charge 2인 250~450ms·손실 10% 회귀 | `Saved/Logs/IrisMultiClient/20261010-203843-6b7d3e34/SkillMultiPeerAbuse.proof.json` |

두 멀티플레이 회귀는 각 공격자의 실제 악성 요청 7개, 합계 14개 동안 HP 보존과 정상
해제 후 각 625 피해/양쪽 클라이언트 HP 복제를 확인했다. 대상은 기존 native fixture이므로
이를 NPC의 사망 태그/몽타주 네트워크 복제 증거로 해석하지 않는다.
Editor 모듈 빌드 기록은 `Saved/Logs/NPCDamageLifecycle-EditorModule-20261010-FixtureFix.log`다.

Shipping은 변경된 피해 계산/기존 피해 단위 테스트/새 NPC 테스트의 **세 CPP 실제
`-NoLink` 컴파일**이 통과했다. `Test-NPCDamageLifecycleShipping.ps1`은 Shipping 빌드 로그의
3개 실제 compile action, 소스/주요 헤더보다 최신이고 해당 빌드 시간에 생성된 오브젝트,
두 테스트 오브젝트의 ASCII/UTF-16 개발 표식 및 등록/latent 본문 symbol 부재를 요구한다.
공통 피해 실행 symbol은 Shipping에도 남아 있어야 한다. 검사 중 C++ 소스 파일명 자체는
개발용 namespace symbol로 오인하지 않는다. 오브젝트/소스/주요 헤더/빌드 로그 사본과
각 해시는 `Saved/Tests/NPCDamageLifecycleShipping-20261010-Verified/Shipping.proof.json`에 보존했다.
빌드 원본은 `Saved/Logs/NPCDamageLifecycle-Shipping-20261010-FixtureFix.log`다.
전체 Shipping 링크/Cook/패키지 실행이나 Shipping 사망 연출 증거는 아니다.

첫 Editor 단일 파일 컴파일은 새 테스트의 전처리 가드 앞 `CoreMinimal.h` 누락으로
`UE_BUILD_SHIPPING`이 정의되지 않아 실패했다. include 순서를 수정했다. 첫 NPC 실행
`Saved/Tests/NPCDamageLifecycle/Automation-1c9595aba9db4e7380c3677fb0a2de99/automation.log`은
startup 대기 중 NPC가 낙하해 사거리 검증에 걸렸다. 테스트 이동 고정만 수정했으며,
운영 사거리 검사를 완화하지 않았다. 이 두 실패 기록은 성공 집계에 포함하지 않는다.

읽기 전용 콘텐츠 감사는 `Saved/Logs/NPCDeathContentAudit-20261010.log`에 있다.
기존 `BT_Guardian.uasset`은 478바이트의 불완전 패키지로 로드 경고가 있으며,
startup 무기 BP에는 미등록 `Enemy.Weapon` 태그 경고가 남아 있다. 수정/저장/삭제하지 않았다.
독립 headless 월드에서는 피해 표시의 `No game viewport was found` 경고도 기록됐다.
따라서 Guardian BT/내비게이션/무기 등록 정상성이나 실제 화면 UI까지 검증한 것으로 보지 않는다.

현재 일반 NPC에는 in-place 부활/리셋 API가 없다. 플레이어 전용
`RestoreEncounterPresentation`이나 보스 전용 encounter reset을 일반 NPC에 재사용하지 않았다.
이번 검증은 **native 공격자·정지 AI·서버 강제 교체·독립 월드** 범위다.
실제 플레이어 BP 입력으로 움직이는 NPC를 처치하는 네트워크 사망 복제, 자연 스포너의
재생성, 같은 NPC의 부활/리셋, 드롭/보상 백엔드 중복 정산, 전체 Shipping 실행은 별도다.
이 단계의 독립 월드 증거와 아래 다중 클라이언트 증거는 별도로 집계한다.

## 일반 NPC 사망·강제 교체의 Iris 2인 검증

2026-10-11에는 저장된 `BP_Dummy_Gdn_C`와 Guardian startup/death BP, 공통 피해 GE를
전용 서버와 실제 클라이언트 2개에서 검증했다. 일반 NPC ASC를 Minimal 방식으로 복제하고
AI Pawn의 `BeginPlay`에서 actor info를 초기화했다. 일반 NPC 사망 태그는 서버에서 GAS
`TagOnly`로 복제하며, 클라이언트에 HP/태그를 주입하거나 사망 Ability를 강제 활성화하지 않는다.

서버 HP **70 → 0**, 사망 태그 1개와 실제 `AM_Guardian_Death`를 두 클라이언트가 관찰했다.
서버 사망 Ability는 1회 활성화/정상 종료하고 시체 피해 3회를 거절했다. 이전 NPC 제거 후
새 actor/ASC의 Iris NetRef가 서로 다름을 서버·양쪽 클라이언트에서 확인했다. 이전 Hit/Spec은
거절하고 새 Hit의 **70 → 60**만 양쪽에 복제했다. 각 프로세스의 원본 관찰 4행과 서로 다른
peer 2개를 결합하며, 누락/중복/재출력/순서 변경/identity·HP 불일치와 fatal/assert/ensure,
BP 실행 오류가 있으면 성공 JSON을 만들지 않는다. 판정기 합성 검사 **32개**는 실행 증거와 별도다.

동일 Editor 모듈 `742F8D677F4E12A649D5ECA88B67A5A3C3BF255BB717B433FFEC75569664514C`에서
아래 실행이 통과했다. NPC 주요 자산 7개의 실행 전후 해시도 동일하다.

| 검증 | 실제 실행 증거 |
| --- | --- |
| Iris 2인 기본 조건 | `Saved/Logs/IrisMultiClient/20261011-014827-6f79e90a/NPCDeathNetwork.proof.json` |
| Iris 2인 250~450ms·손실 10% | `Saved/Logs/IrisMultiClient/20261011-014932-d6a79016/NPCDeathNetwork.proof.json` |
| 보안 native 41개 | `Saved/Tests/Security/Automation-12bc215531ad4085b5b6dac4a304fced/Security.proof.json` |

지연·손실 실행에서는 서버/양쪽 클라이언트의 실제 `PktLagMin`, `PktLagMax`, `PktLoss` 설정
로그도 확인했다. 재실행은 `Run-IrisMultiClientTest.ps1 -SkipBuild -VerifyNPCDeath -ClientCount 2
-TimeoutSeconds 300 -MaxFPS 30`을 사용하고, 지연 조건은 `-PacketLagMinMs 250
-PacketLagMaxMs 450 -PacketLossPercent 10`을 추가한다. 다른 입력/스킬/이동 probe와 혼합하지 않는다.

실행 중 발견한 전용 서버의 Niagara 반환값 `Accessed None`은 `BP_RPGNPCBase`의
Niagara async load 경로에만 `IsDedicatedServer` 분기를 추가해 해결했다. 충돌 해제/dissolve/
무기 정리/드롭 활성화/actor 제거 연결은 유지했다. 원본은
`Saved/Backups/NPCDeathPresentation-2f64bf90e83e4c5db8346d96545a0db5/BP_RPGNPCBase.uasset`에 있다.
현재 자산 해시는 `7D1BF29F2453A71A48E22750588B6AFE0E8899775E802562FDA1C4AE0531676E`이며,
읽기 전용 재검증 로그는 `Saved/Logs/NPCDeathPresentationVerify-20261011-Retry.log`다.
앞선 4회 진단 실행은 BP 오류/클라이언트 이름 선택/fixture cull 반경 ensure가 있어 성공으로
집계하지 않았다. cull 반경은 Iris 한계 이내로 수정했고, ensure 판정을 완화하지 않았다.

변경된 NPC/AttributeSet/새 NPC probe/기존 네트워크 probe의 Shipping **4 CPP 실제 `-NoLink`
컴파일**도 통과했다. 개발용 표식은 ASCII/UTF-16 모두 없고 새 NPC probe의 server/client 본문은
없으며 실제 Tick COFF 본문은 `C2 00 00`(즉시 반환)이다. 운영 NPC 초기화와 AttributeSet 실행
symbol은 남아 있다. 오브젝트/소스/주요 헤더/빌드 로그와 해시는
`Saved/Tests/NPCDeathNetworkShipping-20261011-Verified/Shipping.proof.json`에 보관했다.
이는 전체 Shipping 링크/Cook/패키지 실행 증거가 아니다.

이번 범위는 **native 공격자·정지 AI·always-relevant NPC·고정 관찰 대기·서버 강제 교체**다.
관찰 없이 대기 시간만으로 통과하지 않는다. 플레이어 BP 입력, 이동 중 NPC 처치, 자연 스포너,
동일 actor 부활/리셋, 클라이언트 VFX 렌더링과 드롭/보상 중복 정산은 검증하지 않았다.
기존 Guardian BT/`Enemy.Weapon` 및 별도 GladiatorCore BP 경고는 수정하지 않았으므로
프로젝트 전체 콘텐츠 정상성을 주장하지 않는다.

## 다음 테스트 계층

현재 테스트는 서버 판정과 주요 아이템/텔레메트리 네트워크 경로를 검증한다. 출시
전에는 별도 서버·클라이언트 프로세스로 다음 항목을 추가해야 한다.

- RPC 재전송, 순서 변경, 중복 전송
- 기존 스킬 BP의 몽타주/Root Motion 그래프별 승인 창 연결과 연출 검증
- 대시·점프·넉백 반복과 장시간/버스트 패킷 손실 조합에서의 오탐 측정
- 여러 클라이언트가 동시에 제출하는 TargetData와 Ability 활성화
- 세션 재접속과 맵 전환 전후의 위험 점수 수명주기
- 장시간 부하에서 활성화 기록과 텔레메트리 큐의 메모리/디스크 상한

큐 포화 단위 테스트는 1024건의 낮은 심각도 이벤트가 쌓여도 전송 중인 배치와
미전송 Critical 이벤트가 보존되는지, 새 Low가 High를 밀어내지 못하는지,
전부 전송 중인 큐에서 새 이벤트를 거절하는지 확인한다. 이동 단위 테스트는
서버 시각 기준의 불규칙한 샘플 간격과 긴 간격 뒤 재동기화, 이후 속도 위반
재탐지를 확인한다. 이 두 테스트는 실제 패킷 지연·손실 프로세스 테스트나
장시간 장애 부하 테스트를 대체하지 않는다.

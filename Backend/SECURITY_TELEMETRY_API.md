# Security telemetry API

보안 텔레메트리는 클라이언트가 직접 호출하는 신고 API가 아니다. UE 전용 서버가
권위 판정으로 생성한 이벤트만 세션 전용 Bearer 토큰으로 전송하며, 백엔드는 토큰의
서버 ID·던전 세션 ID와 이벤트 캐릭터의 활성 멤버십을 다시 확인한다.

## 수집 흐름

```text
URPGSecurityValidationComponent (server authority)
  -> URPGSecurityTelemetrySubsystem (bounded queue, batch, retry)
  -> POST /api/security/events/batch
  -> ISecurityTelemetryRepository (Memory or PostgreSQL)
  -> GET /api/security/sessions/{sessionId}/summary
```

전용 서버에는 다음 환경 변수가 필요하다.

```text
PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN
PROJECT_RPG_DUNGEON_SESSION_ID
```

토큰과 세션 ID는 빌드나 `.ini`에 넣지 않는다. 텔레메트리 URL과 큐 정책은
`[/Script/Project_RPG.RPGSecurityTelemetrySubsystem]`에서 설정한다.
INI의 URL은 `ApiUrl="http://localhost:3000/api"`처럼 반드시 따옴표로 감싼다.
따옴표가 없으면 Unreal 설정 파서가 `//` 뒤를 잘라낼 수 있으며, 런타임도 유효한
`http://` 또는 `https://` 스킴이 아니면 텔레메트리를 활성화하지 않는다.

운영에서는 `Project_RPGServer` 전용 타깃에서만 활성화한다. 설치형 Unreal Engine은
Server 타깃 빌드를 지원하지 않을 수 있으므로 배포 빌드 머신은 소스 빌드 엔진을
사용해야 한다. 로컬 자동화의 listen server는 Development 전용
`-RPGSecurityTelemetryDevListenServer` 플래그를 사용하며 이 우회 경로는 Shipping
빌드에 포함되지 않는다.

세션 종료 직전에 만들어진 감사 이벤트가 유실되지 않도록 백엔드는 `Cleared` 또는
`Failed` 전환 후 기본 300초 동안 같은 게임 서버 토큰의 제한된 접근을 허용한다.
보안 API 외의 읽기 전용 예외는 자기 세션의 `GET /api/dungeon-sessions/{id}/reward-snapshot/{characterId}`와
`GET /api/dungeon-sessions/{id}/membership-snapshot`뿐이다. 멤버십 스냅샷은 배정된 서버에만
현재 멤버 기록과 명시적 포기 영수증을 반환하며 새 입장/경제/정산 권한을 부여하지 않는다.
전원 포기로 `Closed`가 된 세션도 마지막 포기 영수증이 있는 경우에만 같은 기본 300초의
제한된 조회 유예를 사용한다. 자연 만료(일부 포기 후 만료 포함)는 이 유예를 받지 않는다.
토큰 자체 만료 및 서버/세션 배정 검사는 유지한다. 포기자의 기존 감사 배치 처리 정책은 별도 미완료이며,
이 변경으로 삭제된 멤버의 이벤트를 다시 수락하도록 멤버 검사를 완화하지 않는다.
보상 스냅샷 경로는 실제 멤버만 조회하며 Pending은 202, Cleared는 200, Failed는 409를 반환한다.
정산 완료도 같은 유예 정책을 사용한다. 자세한 계약은
[재접속 없는 보상 갱신](../Docs/DUNGEON_REWARD_LIVE_REFRESH.md)을 참고한다.
이 유예 토큰으로 `/api/dungeon-sessions` 같은 일반 게임 API를 호출하면 403을 반환한다.
유예 시간은 백엔드의 `SecurityTelemetry:PostSessionGraceSeconds`로 설정하며 최대 3600초다.

## 이벤트 배치 저장

```http
POST /api/security/events/batch
Authorization: Bearer {session-scoped-game-server-token}
Content-Type: application/json
```

```json
{
  "dungeonSessionId": "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
  "events": [
    {
      "eventId": "11111111-2222-3333-4444-555555555555",
      "characterId": "66666666-7777-8888-9999-aaaaaaaaaaaa",
      "steamId": "76561198000000000",
      "type": "InvalidCombatHit",
      "severity": "High",
      "score": 5.0,
      "riskAfter": 17.0,
      "serverTimeSeconds": 42.5,
      "detail": "Server hit range exceeded."
    }
  ]
}
```

한 배치는 1~64건이다. `eventId`는 서버가 생성하는 멱등성 키다. 같은 ID와 같은
내용을 재전송하면 `duplicateCount`로 처리하고, 같은 ID를 다른 내용에 재사용하면
409 Conflict를 반환한다. 성공 응답의 `acceptedCount + duplicateCount`가 전송 건수와
일치할 때만 UE 큐가 해당 배치를 제거한다.

PostgreSQL 구현은 배치 전체의 세션 멤버 권한을 한 번의 집합 쿼리로 확인하고, 이벤트
저장과 기존 fingerprint 조회도 각각 배치 쿼리로 처리한다. 따라서 이벤트 수만큼 인증·
저장 쿼리가 증가하지 않는다.

지원 이벤트 유형:

- `MovementSpeed`
- `MovementDiscontinuity`
- `AbilityActivationRate`
- `InvalidTargetData`
- `InvalidCombatHit`
- `InvalidDamage`
- `RestrictedActionAttempt`
- `EnforcementStateChanged`
- `PlayerRemoval`

심각도는 `Low`, `Medium`, `High`, `Critical`이다. 상세 문자열은 제어 문자를 제거하고
512자로 제한한다.

## 세션 요약

```http
GET /api/security/sessions/{dungeonSessionId}/summary
Authorization: Bearer {admin-token-or-matching-game-server-token}
```

응답은 전체 이벤트 수·누적 점수·최대 위험 점수와 유형/심각도/캐릭터별 집계를
반환한다. 원문 `detail`은 요약 API에 포함하지 않는다. 종료된 세션은 관리자 토큰으로
계속 조회할 수 있다.

## UE 및 Blueprint 적용

`URPGSecurityValidationComponent::ReportViolation`은 위험 점수를 갱신한 뒤 자동으로
텔레메트리 큐에 이벤트를 넣는다. 이 함수는 Authority 전용 Blueprint 노드로도 노출되어
프로젝트 고유의 서버 BP 판정을 같은 흐름에 연결할 수 있다.

Blueprint에서는 Game Instance Subsystem의
`RPGSecurityTelemetrySubsystem`을 얻어 다음 값을 운영 HUD나 서버 진단에 사용할 수 있다.

- `Is Available`
- `Get Queued Event Count`
- `Get Dropped Event Count`
- `Flush Now`

전송 대기 이벤트는 기본적으로
`Saved/SecurityTelemetry/{dungeonSessionId}.jsonl`에 기록된다. 정상 응답을 받기 전
종료되거나 프로세스가 재시작되어도 이 파일에서 복구한다. 네트워크 오류와 5xx는 최대
지연에 도달한 뒤에도 계속 재시도하며, 계약 오류 같은 영구 4xx 응답만
`{dungeonSessionId}.deadletter.jsonl`로 격리한다. `MaximumAttempts`는 폐기 횟수가
아니라 지수 백오프가 최대 지연에 도달하기까지의 단계 수다.

새 이벤트는 이미 예약된 재시도 시점을 변경하지 않는다. 정상 전송에서는 배치가 꽉 차면
더 일찍 보낼 수 있지만, 후속 이벤트로 기존 전송이 계속 미뤄지지는 않는다.
`Flush Now`를 명시적으로 호출하면 기존 타이머를 취소하고 즉시 전송한다.

런타임 enforcement는 `Monitoring`, `Elevated`, `Restricted`,
`RemovalRecommended` 단계로 동작한다. Restricted 상태에서는 플레이어 입력 Ability와
BP가 `Can Perform Protected Action`으로 보호한 동작을 차단한다. 자동 Kick은 정책에서
명시적으로 활성화한 경우에만 수행하며 기본값은 꺼져 있다. 이 경로는 영구 밴을
생성하지 않는다. 영구 제재는 세션 집계, 반복 세션 이력, 오탐 검토를 거치는 별도 운영
정책에서 결정해야 한다.

## 검증

이미 실행 중인 Development 백엔드에 대해:

```powershell
Backend/security-telemetry-smoke-test.ps1 `
  -BaseUrl http://127.0.0.1:3000 `
  -AdminToken $env:PROJECT_RPG_BACKEND_ADMIN_TOKEN
```

PostgreSQL 저장과 백엔드 재시작 후 보존은 다음 통합 테스트에 포함된다.

```powershell
Backend/postgres-integration-test.ps1
```

UE 큐와 JSON 계약 자동화 테스트:

```text
Automation RunTests ProjectRPG.Security.Telemetry
```

실제 인증 클라이언트의 아이템 명령, Restricted 차단, 3종 감사 이벤트 전송을 함께
검증하려면 다음을 실행한다. 두 번째 명령은 백엔드 승인 전에 UE 서버를 종료하고 같은
세션으로 재기동하여 JSONL 복원, 정확히 3건의 백엔드 효과, 승인 후 스풀 삭제까지
검증한다.

```powershell
Scripts/Network/Run-ItemBackendE2ETest.ps1 -TimeoutSeconds 120
Scripts/Network/Run-ItemBackendE2ETest.ps1 `
  -TimeoutSeconds 120 `
  -TestTelemetryRestartRecovery
```

DB 장애와 복구 중의 재시도는 Docker가 실행 중이고 localhost:3000이 비어 있는 상태에서
다음 명령으로 검증한다. 먼저 `Project_RPG Win64 Development` 타깃을 최신 코드로 빌드해야 한다.

```powershell
Scripts/Network/Run-ItemBackendE2ETest.ps1 `
  -TimeoutSeconds 120 `
  -TestTelemetryBackendRecovery
```

이 시나리오는 임시 PostgreSQL 컨테이너와 테스트 소유 백엔드를 시작한다. 인증과 정상
소비/장착/해제를 마친 뒤 DB를 일시 정지하고, Development 전용 파일 체크포인트로
Restricted 판정을 시작한다. 실패 응답 뒤 2초/4초 지수 백오프와 JSONL 3건 보존을
확인하고 DB를 복구한다. UE 서버를 재시작하지 않고 정확히 3건 저장, 승인 후 스풀 삭제,
deadletter 미생성을 검증한다. 장애 중 캡처한 동일 이벤트를 다시 전송해
`acceptedCount=0`, `duplicateCount=3`, 총 이벤트 수 3도 확인한다.

검증 로그와 장애 중 JSONL 사본은 `Saved/Logs/ItemBackendE2E/{timestamp}`에 남는다.
종료 시 테스트 컨테이너와 임시 DB는 제거된다. 기존 Compose DB나 볼륨은 사용하지 않는다.
이 검증은 DB 타임아웃/5xx 복구와 동일 배치 재전송을 다룬다. HTTP 연결 중단과
COMMIT 이후 승인 응답 유실은 다음 별도 시나리오로 검증한다.

```powershell
Scripts/Network/Run-ItemBackendE2ETest.ps1 `
  -TimeoutSeconds 120 `
  -TestTelemetryHttpRecovery
```

Docker와 Node.js가 필요하고 localhost:3000/3002는 비어 있어야 한다. 실제 PostgreSQL
백엔드는 3002에서 실행하고 UE가 사용하는 3000에는 테스트 전용 HTTP 프록시를 띄운다.
인증과 아이템 명령을 마친 뒤 HTTP 연결을 끊어 `HTTP=0` 실패와 2초/4초 백오프를
확인한다. 다음 재시도에서는 PostgreSQL이 3건을 COMMIT하고 200 응답을 보낸 직후
프록시가 UE 쪽 연결을 끊는다. 이때 DB에는 3건이 저장되지만 UE의 JSONL은 그대로
남아 있어야 한다. 연결 복구 후 UE가 8초 백오프로 같은 ID를 재전송해
`acceptedCount=0`, `duplicateCount=3`을 받았을 때만 스풀이 삭제되는지 검증한다.

프록시 제어 API는 별도 임의 포트의 loopback 주소에만 바인딩하고 실행마다 생성한
Bearer 토큰을 요구한다. 이 프록시는 게임/백엔드 런타임에 연결되는 개발 검증 도구이며
배포 코드에 포함되지 않는다. 프로세스와 임시 DB는 테스트 종료 시 정리한다.
검증 디렉터리에는 `Telemetry.unacknowledged.jsonl`과 `Telemetry.http-proof.json`을
남겨 저장된 이벤트 ID, 유실된 승인, 자동 재시도의 중복 처리 결과를 확인할 수 있다.

프록시 자체의 연결 차단·저장 후 응답 유실·중복 승인 동작은 임의 loopback 포트에서
실행하는 다음 테스트로도 확인할 수 있다. 이 단독 테스트는 메모리 저장 fixture를
사용하며, 실제 PostgreSQL과 UE 조합은 위 E2E 명령으로 검증한다.

```powershell
node --test Scripts/Network/Fixtures/TelemetryHttpFaultProxy.test.mjs
```

재시작, DB 장애, HTTP 장애 옵션은 각각 별도 실행해야 한다. 큐 포화 시 서버는
전송 중인 배치를 보존하고, 미전송 이벤트 중 가장 낮은 심각도의 오래된 항목부터
교체한다. 새 이벤트의 심각도가 남은 항목보다 낮거나 전부 전송 중이면 새 이벤트를
거절한다. 두 경우 모두 `GetDroppedEventCount()`에 누락을 반영한다. 이 정책은
증거의 우선순위를 정할 뿐, 장시간 장애에 모든 이벤트를 무손실 보존한다는 뜻은
아니다. 재시작을 넘기는 누락 집계의 영속화와 운영 경보도 아직 별도 과제다.

큐 포화 및 불규칙한 서버 샘플 간격은 자동화 단위 테스트로 검증한다. 실제
서버·클라이언트 프로세스의 짧은 지상 이동 지연/손실 검증은
[`SECURITY_ABUSE_TESTING.md`](../Docs/SECURITY_ABUSE_TESTING.md)의
`-VerifyMovementSecurity` 시나리오를 사용한다. 스킬/강제 이동까지 포함한 오탐률과
장시간 장애의 디스크/메모리 압력 측정은 추가 검증이 필요하다.

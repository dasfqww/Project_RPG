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

세션 종료 직전에 만들어진 감사 이벤트가 유실되지 않도록 백엔드는 `Cleared` 또는
`Failed` 전환 후 기본 300초 동안 같은 게임 서버 토큰의 보안 API 접근만 허용한다.
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

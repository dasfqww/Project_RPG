# 운영툴 로그 추적 계약

작성일: 2026-11-04 · 상세 확정: 2026-11-05 · 설계 v1.1 · 상태: 설계, 아래 계측·검색 기능 신규 구현 아님

상위 문서: [운영툴 아키텍처](OPERATIONS_TOOL_ARCHITECTURE.md), [1인 개발 제작 계획](SOLO_OPERATIONS_TOOL_PLAN.md). 실제 sink·물리 JSON·버퍼·검색 알고리즘은 [구현 계약 §6](OPERATIONS_TOOL_IMPLEMENTATION_SPEC.md#6-로그의-실제-기록회전검색-구현)에서 확정한다.

사용자가 요청한 로그 추적은 **운영 화면의 사건에서 HTTP 요청·업무 처리·저장 결과·게임 반영 기록을 찾아가는 기능**이다. 모든 로그를 한 화면에 출력하는 것으로 완료하지 않는다. SOLID에 따라 업무 규칙, 기록 생성, 저장/수집, 검색, 표시의 책임을 분리한다.

## 1. 세 가지 기록의 역할

| 기록 | 답하는 질문 | 저장/신뢰 경계 |
|---|---|---|
| 기술 로그와 trace | 어디서 느려지거나 실패했는가, 어느 배포/프로세스가 처리했는가 | 구조화 로그·선택적 span 수집. 유실/샘플링/보존 만료가 가능하므로 지급 증거를 대신하지 않음 |
| 게임 영수증·정산 기록 | 어떤 요청이 실제 게임 데이터를 변경했는가 | 기존 권위 DB의 멱등/거래/정산 기록. 재시작·응답 유실 뒤 결과 판단 근거 |
| 운영 감사 기록 — S3 | 누가 누구에게 어떤 근거로 무엇을 변경했는가 | 변경 효과·멱등 영수증과 같은 트랜잭션에 성공 기록. 기술 로그와 보존 정책 분리 |

조회/로그인/거절도 기술 기록을 남긴다. '지급 버튼을 눌렀다', 'HTTP 200이다', '로그에 완료 문장이 있다'는 사실만으로 게임 반영을 확정하지 않는다. DB 커밋, 서버 재조회, 클라이언트 표시 관측을 각각 구분한다.

현재 [정산 worker](../Backend/ProjectRpg.Backend/Application/DungeonRewardSettlementWorker.cs)는 DungeonSessionId를 포함한 상태 로그를 남긴다. 이것만으로 운영자 검색·전체 trace·UE 연결이 구현된 것은 아니다. 기존 영수증 ID·게임 로그의 실제 필드부터 조사하고 부족한 계측은 해당 단계에서 추가한다.

## 2. ID와 연결 규칙

| ID | 수명/생성 주체 | 용도 |
|---|---|---|
| `HttpRequestId` | 서버가 HTTP 수신마다 새로 생성 | 로그인 실패·잘못된 입력도 찾을 수 있는 단일 시도 번호 |
| `TraceId`, `SpanId`, `ParentSpanId` | .NET Activity/W3C 컨텍스트 | 짧은 호출 흐름의 구간과 인과 관계 |
| `OperationRequestId` | 지급 등 확정된 업무 요청의 ID, 재전송 시 유지 | HTTP 응답 유실·재시작·다른 trace 사이의 같은 지급 연결 |
| `CaseId` | 운영자가 선택한 사건의 서버 검증 식별자 | 문의/장애 단위 조사. 실제 중복 지급 기준과 연계 |
| `CharacterId`, `RosterId` | 권위 데이터에서 확인한 ID | 대상과 실제 지급 소유 범위 추적 |
| `DungeonSessionId`, `SettlementId/Key` | 기존 세션·정산 기록의 실제 식별자 | UE→backend→worker→정산 결과 연결. 없는 ID를 합성해 실제 기록처럼 표시하지 않음 |
| `TransactionRequestId`, `ItemId` | 게임 영수증/아이템 ID | 재화 증감·아이템 생성/이동/수령 연결 |
| `EventId`, `Attempt` | 기록마다 고유 ID / 실제 시도 순번 | 중복 수집 제거와 재시도 구분 |

`.NET Activity`의 TraceId/SpanId는 호출 구간을 연결하며, 업무 요청 ID와 목적이 다르다. 이 설계에서는 별도 필드로 유지한다. [Microsoft 분산 추적 개념](https://learn.microsoft.com/en-us/dotnet/core/diagnostics/distributed-tracing-concepts)

- 같은 지급을 세 번 재전송하면 OperationRequestId는 같고 HttpRequestId와 실행 trace는 달라질 수 있다. 새 HTTP 시도를 새 지급으로 계산하지 않는다.
- 짧은 동기/비동기 호출에는 부모-자식 span을 사용한다. 저장 후 실행하는 worker·다음 날 재시도는 새 trace를 시작하고 원래 업무 ID 및 선택적인 원래 trace/span 링크로 연결한다. 한 span을 작업 대기 시간 내내 열어 두지 않는다.
- trace 컨텍스트의 보존/전달 메타데이터는 원래 영수증·정산 ID를 대체하지 않는다. worker가 원래 trace를 받지 못해도 업무 ID로 조사할 수 있어야 한다.
- UE 계측이 trace 전달을 지원하지 않는 구간은 동일 DungeonSessionId/요청·거래 ID로 연결하고 '업무 ID로 연계됨'으로 표시한다. 같은 trace가 실제 전파됐다고 표현하지 않는다.
- 외부 `traceparent`·헤더·RequestId는 신원/권한 증거가 아니다. 형식·길이를 검증하고, 외부 trace와 내부 생성 HttpRequestId·검증된 주체를 구분한다. 어떤 trace ID를 안다고 로그 조회 권한을 얻지 못한다.
- 환경·서비스·인스턴스·배포 버전을 모든 기술 기록에 함께 둔다. 서로 다른 환경의 같은 업무 ID를 자동으로 합치지 않는다.
- 시각만으로 다른 서비스의 인과 관계를 추정하지 않는다. UTC 시각·수집 시각·시도 번호·명시적 ID를 함께 사용하고 시계 차이가 있을 수 있음을 표시한다.

## 3. 최소 구조화 이벤트

필드는 허용 목록으로 직렬화한다. 자유로운 객체/HTTP 본문 덤프는 이벤트 계약이 아니다.

| 묶음 | 필수/선택 필드 |
|---|---|
| 공통 | SchemaVersion, EventId, EventName, TimestampUtc, Level, Environment, Service, InstanceId, BuildVersion |
| 호출 | HttpRequestId(HTTP), TraceId/SpanId(계측 시), ParentSpanId(있으면), OperationName, DurationMs(완료 시) |
| 확정된 맥락 | OperatorId, CharacterId, RosterId, DungeonSessionId, OperationRequestId, TransactionRequestId, CaseId 중 실제 확인한 항목 |
| 결과 | Outcome, ErrorCode, HttpStatus(HTTP), Attempt, Retryable, ResultReference |
| 코드 탐색 | LoggerCategory, 안정된 EventName/수치 이벤트 코드, BuildVersion. 예외에는 정제된 종류·안전한 stack 정보 |

유효한 trace를 수집하지 않는 경우 TraceId/SpanId는 null일 수 있다. 로그가 남았다고 span 저장도 성공했다고 가정하지 않는다. 입력을 파싱하기 전 실패했으면 확인되지 않은 대상/업무 ID를 확정 필드에 넣지 않는다.

이벤트 예시 — 정식 값이 아닌 형식 설명용:

```json
{
  "schemaVersion": 1,
  "eventId": "01bce596-8d58-4d37-ad96-52670b3933df",
  "eventName": "ops.character.query.completed",
  "timestampUtc": "2026-11-04T10:00:00Z",
  "level": "Information",
  "environment": "Development",
  "service": "ProjectRpg.Backend",
  "instanceId": "local-ops-01",
  "buildVersion": "example-build",
  "httpRequestId": "e0b4d89d-9d47-4d17-a3ee-cc373235df64",
  "traceId": "4bf92f3577b34da6a3ce929d0e0e4736",
  "spanId": "00f067aa0ba902b7",
  "operationName": "GetCharacterSummary",
  "characterId": "4c549d8e-289d-4f7c-97db-d810615b8254",
  "outcome": "Succeeded",
  "durationMs": 18,
  "operationRequestId": null
}
```

초기 이벤트 목록은 아래 업무 경계에 한정한다. 메서드 진입마다 같은 사건을 중복 기록하지 않는다.

| 이벤트 계열 | 언제 기록하는가 |
|---|---|
| `ops.http.started/completed` | 요청 수신/최종 응답, 미인증/잘못된 입력 포함 |
| `ops.access.denied` | 운영 인증/권한 거절. 로그인 시도 원문/비밀번호 제외 |
| `ops.character.query.completed/failed` | 캐릭터 검색·상세의 결과와 지연 |
| `ops.storage.query.completed/failed` | 필요한 DB 조회 구간. SQL 원문·파라미터 덤프 제외 |
| `ops.logs.search.completed/failed` | 로그 검색 자체의 범위·소요 시간·잘림 여부. 검색 결과 내용을 재기록하지 않음 |
| `ops.compensation.committed/replayed/conflicted/outcome_unknown` — S3 | 권위 저장 결과에 따른 조치 상태 |
| `game.settlement.attempt.completed/failed` — S2 | 원래 정산 ID·시도와 커밋/정산 상태 연결 |
| `game.projection.observed` — 필요한 UE 연결 단계 | 실제 관측한 데이터 갱신. DB 성공만 보고 생성하지 않음 |

EventName은 검색 계약이다. 이름/필드를 변경하면 SchemaVersion·호환 읽기 정책을 함께 검토한다. 사용자에게 보여주는 한국어 문구 변경이 이벤트 키를 바꾸지 않게 한다. 코드 위치는 LoggerCategory와 해당 BuildVersion 소스의 이벤트 선언으로 찾으며, 절대 개발자 PC 경로를 사용자에게 노출하지 않는다.

## 4. 생성·수집·검색의 의존 분리

```mermaid
flowchart LR
    Boundary[Web·저장 어댑터의 업무 경계] --> Logging[ILogger·ActivitySource]
    Logging --> Sink[호스트 로그/trace 제공자]
    Sink --> Store[기술 로그 저장소]
    UI[운영 로그 조회 화면] --> UseCase[SearchOperationLogs]
    UseCase --> Port[IOperationLogReader]
    Adapter[로그 검색 어댑터] -. 구현 .-> Port
    Adapter --> Store
    Command[원자 운영 명령] --> DB[(게임 효과·영수증·감사 DB)]
```

호스트는 구조화 `ILogger` 이벤트를 기록하고 제공자 설정으로 저장 대상을 바꾼다. Core 업무 로직은 제공자와 무관하다. [ASP.NET 로깅 제공자](https://learn.microsoft.com/en-us/aspnet/core/fundamentals/logging/?view=aspnetcore-9.0)

구간 계측은 표준 `ActivitySource`를 사용한다. OpenTelemetry 등 수집 도구를 채택할 경우 호스트의 등록/내보내기 계층에 두고, 업무 클래스마다 수집 SDK를 호출하지 않는다. listener/샘플링 때문에 Activity 생성 결과가 없더라도 업무는 동작해야 한다. [Microsoft 계측 지침](https://learn.microsoft.com/en-us/dotnet/core/diagnostics/distributed-tracing-instrumentation-walkthroughs)

초기 1인 운영 구성:

- backend의 ILogger→Serilog File/Async 어댑터가 허용된 OpsEvent만 고정 운영 디렉터리의 JSONL에 기록한다. UTC 일별/32MiB 회전, 30파일·14일 제한, 4096이벤트 버퍼를 사용한다. JSON console 출력만으로 영속 보관이 완료됐다고 보지 않으며 실제 회전·재시작·드롭을 검증한다.
- S1 검색 어댑터는 등록된 로컬 로그 원본만 제한적으로 읽는다. 파일에 자체 범용 검색 엔진/색인을 만들지 않고 시각·정확한 ID 필터와 읽기 예산을 적용한다.
- 로그가 여러 머신에 분산되거나 읽기 예산 때문에 조사가 반복해서 잘리면 중앙 수집/검색 어댑터로 바꾼다. 이때 Core 검색 계약·사건 ID는 유지한다.
- 범용 대시보드/수집 제품 선정은 비용·호스팅 조건을 확인한 뒤 결정한다. 게임 DB에 모든 기술 로그를 무제한 적재하지 않는다.
- 원자 감사 DB와 기술 로그 저장소는 서로 장애 의미가 다르다. 로그 제공자 장애 때문에 이미 커밋된 지급을 실패/롤백됐다고 표시하지 않는다. 감사 저장 실패는 지급과 함께 롤백한다.

## 5. 운영 화면의 검색 흐름

S1에 검색·캐릭터 상세와 함께 **기본 로그 조회 페이지**를 둔다. 오류 화면의 '추적 번호'를 복사하거나 해당 조회 화면에서 열 수 있다. 운영자 인증과 진단 조회 권한을 적용한다.

| 진입점 | 조회 순서 |
|---|---|
| 조회 오류의 HttpRequestId | 해당 시도 → trace/조회 어댑터 → 오류 코드/배포·인스턴스 |
| 캐릭터와 시간 범위 | 운영 조회·변경 기록 → 관련 던전/거래 ID → 각 기술 로그 |
| 지급 OperationRequestId — S3 | 원자 영수증/감사 결과 → 모든 HTTP 시도 → 커밋 결과와 게임 갱신 관측 |
| 던전 세션 — S2 | 완료 보고 → worker 시도 → 원래 재화/아이템 영수증 → 가방/우편/수령 기록 |

타임라인의 각 항목에는 출처·상태·관측 시각·연결 근거를 표시한다. 직접 trace 관계인지, 업무 ID로 연계한 것인지, 계측하지 않은 구간인지 구분한다. 게임 영수증은 기술 로그 페이지에 복사해 새로운 지급 근거로 만들지 않고 원래 기록을 참조한다.

검색 계약:

- `OperationLogQuery`는 허용된 환경/서비스, 시작·종료 시각, 정확한 ID, 이벤트/수준, 페이지 크기·커서만 받는다. 임의 SQL·정규식·파일 경로·셸 명령을 받지 않는다.
- 기본 기간 1시간, 한 검색 최대 24시간, 기본 100/최대 200건이며 하나 이상의 정확한 ID 또는 EventName을 요구한다. 화면에 적용 한도를 표시한다. 장기 게임 이력의 7일/30일 범위와 기술 로그 검색의 한도를 혼동하지 않는다.
- 로컬 파일 검색은 요청당 3초·64MiB에서 중지한다. 한도에 도달하면 Coverage=Partial·TruncationReason과 실제 검색한 기간/원본·cursor를 반환하고 '해당 로그 없음'으로 확정하지 않는다.
- 결과는 `Events`, `NextCursor`, `Coverage`, `ObservedAtUtc`, `TruncationReason`을 포함한다. Coverage는 Complete/Partial/Expired/NotInstrumented/Unknown 등 실제 근거를 표현한다. 저장소 장애는 성공한 빈 결과가 아니다. Complete는 선택한 보존 원본의 검색 완료를 뜻하며 모든 사건의 무손실 수집을 보장하지 않는다.
- 수집/보관 계층은 등록 원본·서비스/인스턴스·보존 시간 범위·회전/삭제·마지막 수집 시각의 작은 메타데이터를 유지한다. 검색 어댑터는 이 근거와 배포별 계측 범위로 Coverage를 계산한다. 보존/계측 여부를 모르면 Unknown을 반환하며 없는 기록을 추정하지 않는다.
- 커서는 서버가 등록한 원본과 필터에 묶인 불투명 값이다. 파일 경로를 그대로 받아 열지 않는다. 회전·삭제된 원본의 커서는 만료로 반환하고 새 검색을 안내한다.
- 고정 로그 root 밖, 임의 절대 경로, junction/symlink를 따라 읽지 않는다. 로그 파일을 브라우저에 직접 공개하거나 웹 정적 파일로 제공하지 않는다.
- 로그 원문/메시지를 HTML로 실행하지 않고 길이 제한·이스케이프를 적용한다. 자동 새로고침과 전체 다운로드는 첫 구현에 포함하지 않는다.

## 6. 응답 유실·재시작 사례

1. 운영자 지급 요청 `R`을 HTTP 시도 `H1`/trace `T1`로 받는다.
2. DB에 효과·영수증·감사를 함께 커밋한다. 원자 기록에는 `R`, 확정된 주체/대상, 원래 실행의 `H1`/`T1` 등 연결 가능한 맥락을 저장한다.
3. HTTP 응답이 유실되면 화면은 결과 미확정으로 표시한다. 서버의 성공 기술 로그도 유실될 수 있다.
4. 재시작 뒤 `R`을 같은 입력으로 조회/재전송한다. `H2`/`T2`는 새로 생기고, 저장된 결과를 반환하며 `Replayed`를 기록한다. 새 지급은 없다.
5. 운영자는 `R`로 원자 기록과 두 시도를 함께 찾는다. `T1` 로그가 보존되지 않았으면 원자 기록은 성공, 기술 로그는 부분/유실로 표현한다.

worker 재시도·UE 응답 누락도 같은 원칙으로 조사한다. '로그가 없으니 지급되지 않았다'는 자동 판단이나 자동 보상을 만들지 않는다. 클라이언트 반영 로그가 없으면 표시 갱신은 미확인 상태로 남긴다.

## 7. 민감정보·보존·장애 처리

- 인증 토큰·쿠키·Steam 티켓·DB 연결 문자열·비밀번호·전체 요청/응답 본문은 기록하지 않는다. 일반 기술 로그에는 Steam ID·이름·메일 대신 필요한 내부 ID나 일관된 가명 키를 사용한다. 지급 사유 원문·변경 전후 상세는 접근이 제한된 감사 기록으로 관리한다.
- 정제 전 exception message/SQL 오류에 파라미터·비밀값이 들어갈 수 있다. 알려진 오류 코드·예외 종류·안전한 stack만 허용하고, 문제 분석을 이유로 무조건 원문 덤프하지 않는다. 로거 메서드에 전달하기 전에 최소화한다.
- 기술 로그는 노드당 14일 또는 32MiB 파일 30개 중 먼저 도달한 한도로 보존하며 메타데이터 포함 1GiB를 예산으로 삼는다. Debug는 기본 비활성/문제 분석 때 짧게 활성화다. 실제 이벤트량으로 조정하고 삭제/누락 구간을 검색 결과에 표시한다.
- 영수증·감사에는 기술 로그의 삭제 정책을 적용하지 않는다. v1의 감사 상세·게임 영수증·요청/사건 키는 자동 삭제하지 않고 보존량을 감시한다. 향후 상세 아카이브를 추가해도 중복 키/결과 참조를 유지하며 오래된 요청을 새 지급으로 실행하지 않는다.
- 같은 오류 폭주를 묶고 exporter/버퍼/디스크 용량을 제한한다. critical 업무 결과와 거절 요약은 애플리케이션 수준에서 샘플링 제외로 설계하되, 그 사실이 물리적 무손실 저장을 보장하지는 않는다. trace span 샘플링과 별개로 결과 이벤트를 남긴다.
- 로그 수집 실패·드롭 수·마지막 수집 시각·보존 공간 부족을 별도로 감시한다. 조회/성공한 커밋을 외부 로그 수집기가 복구될 때까지 기다리게 하지 않는다.
- 사건/캐릭터/요청 ID는 로그 검색 필드로 사용한다. 지표의 무제한 label로 넣어 시계열 수가 폭증하지 않도록 한다.
- 운영 로그의 정상 보존은 기능 데이터 정책이다. 개발 검증의 임시 로그 자동 폐기와 혼동하지 않으며, 다른 담당의 Saved/원본 엔진 로그를 회전 정리 대상으로 삼지 않는다.

## 8. 단계별 추적 완료 기준

| 단계 | 완료해야 할 것 | 아직 주장하면 안 되는 것 |
|---|---|---|
| S1 | 운영 HTTP→권한/조회→DB 어댑터→응답의 추적 번호/필드, 재시작 후 로그 검색, 제한/마스킹/인가/부분 검색 표시 | UE 전체 흐름이나 과거 미수집 로그까지 연결됨 |
| S2 | 실제 던전·정산·worker·아이템/재화 영수증·필요한 UE 구간의 ID 연결, 미계측 범위 표시 | 모든 구간에 동일 trace가 자동 전파됨 |
| S3 | 운영 요청·사건 중복·원자 감사·재시도/응답 유실·게임 반영 관측 연결 | HTTP 성공만으로 클라이언트 갱신 완료 |

구현 시 집중 검증:

1. 정상 조회와 권한 거절·DB 시간 초과를 각각 발생시키고 화면의 번호로 해당 시도/구간을 찾는다. 서로 다른 요청·환경이 섞이지 않는지 확인한다.
2. 동시 요청에서 scope가 다른 운영자/캐릭터에 섞이지 않는지, 재시도는 새 HTTP 번호와 같은 업무 ID를 갖는지 확인한다.
3. 테스트용 가짜 비밀값을 헤더·본문·예외에 넣어 출력/저장/검색/내보내기에 포함되지 않는지 확인한다. 실제 자격값은 검사에 쓰지 않는다.
4. 로그 회전·파일 잘림·커서 만료·읽기 예산 초과·수집 장애에서 Partial/Expired/Unavailable 의미와 게임 동작 보존을 확인한다.
5. S3에서 DB 커밋 후 응답 유실·로그 유실·프로세스 재시작을 거쳐 같은 영수증을 찾고 지급은 한 번인지 검증한다. 감사 저장 실패는 게임 효과도 없는지 확인한다.
6. UE/worker는 실제 추가 계측 구간만 성공으로 보고한다. 종전 로그·가상 fixture를 현재 전체 연결의 증거로 취급하지 않는다.

설계 변경 시 이벤트 필드/키·ID 연결·검색 계약·보존 의미를 함께 검토한다. 로그 수집 제품 변경은 Infrastructure/호스트의 변경으로 제한하고 게임 업무 규칙과 감사 원자성을 유지한다.

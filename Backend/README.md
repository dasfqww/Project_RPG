# Project RPG Backend

PvE 인스턴스형 MORPG를 위한 ASP.NET Core 백엔드다. UE Dedicated Server가
전투·이동·AI를 판정하고 Iris가 실시간 상태를 복제하며, 이 서비스는 Steam 인증,
캐릭터/인벤토리 영속화, 던전 참가 lease와 일회용 입장 티켓을 담당한다.

## 현재 구현 범위

- Steam Web API 티켓 검증 및 opaque Bearer 세션 발급
- 계정, 캐릭터, 인벤토리 저장
- 계정별 기본 원정대와 Account/Roster/Character 범위의 서버 권위 재화 저장
- 원자적 다중 재화 지급·소모, 거래 영수증, 멱등 요청 처리
- 던전 파티 전체의 재화·아이템 보상을 묶는 영속 정산 작업과 결정적 영수증
- 1~4인 던전 세션 생성·참가·이탈
- 캐릭터가 동시에 두 던전에 들어가는 것을 막는 만료형 lease
- `Waiting → Loading → InProgress → SettlementPending → Cleared/Failed` 상태 전이
- 원자적 Waiting 세션 claim, 전용 서버 할당, heartbeat, 정상 종료 보고
- 동일한 성공/실패 종료 보고의 멱등 재시도
- 던전 세션 및 서버 ID에 결합된 단기·일회용 입장 티켓
- 메모리 저장소와 PostgreSQL 저장소

## 로컬 실행

```powershell
$env:ASPNETCORE_ENVIRONMENT='Development'
$env:PROJECT_RPG_BACKEND_ADMIN_TOKEN='replace-with-a-long-random-secret'
dotnet run --project Backend/ProjectRpg.Backend/ProjectRpg.Backend.csproj
```

Development 환경에서는 실제 Steam 호출 없이 `dev:{SteamID64}`를 Steam 티켓으로
보내는 개발 인증을 사용할 수 있다. 이 우회는
`Steam:AllowDevelopmentAuthentication=true`인 Development 환경에서만 동작한다.

서버가 실행 중일 때 전체 인증/캐릭터/파티/입장/인벤토리 계약을 검사한다.

```powershell
Backend/smoke-test.ps1
```

원정대 및 재화 계약은 다음 스모크 테스트로 검사한다.

```powershell
Backend/economy-smoke-test.ps1
```

재화 정의와 API 계약은 `ECONOMY_API.md`를 참고한다.

## 던전 입장 흐름

```text
플레이어(파티장)
  POST /api/dungeon-sessions
  { characterId, dungeonId, difficulty }
  ← Waiting 던전 세션

플레이어(재접속 복구)
  GET /api/characters/{characterId}/active-dungeon-session
  ← Waiting/Loading/InProgress 세션 또는 204 No Content

파티원
  POST /api/dungeon-sessions/{sessionId}/members
  { characterId }

할당 서비스
  POST /api/dungeon-sessions/claim
  Authorization: Bearer {administrator token}
  { serverId, serverAddress }
  ← 가장 오래 기다린 세션을 Loading으로 원자적 전환

또는 특정 세션 수동 할당
  POST /api/dungeon-sessions/{sessionId}/activate
  Authorization: Bearer {administrator token}
  { serverId, serverAddress }
  ← Loading

Dedicated Server
  PROJECT_RPG_GAME_SERVER_ID={serverId}
  PROJECT_RPG_DUNGEON_SESSION_ID={sessionId}
  POST .../{sessionId}/start
  POST .../{sessionId}/heartbeat (30초 주기)

플레이어
  POST /api/join-tickets
  { characterId, dungeonSessionId }
  ← { dungeonSessionId, characterId, joinTicket, expiresAt }
  ClientTravel("{server}?JoinTicket={joinTicket}")

Dedicated Server PreLoginAsync
  POST /api/join-tickets/consume
  { joinTicket, serverId }
  ← { dungeonSessionId, characterId, steamId }

Dedicated Server
  POST .../{sessionId}/settle-rewards
  { serverId, rewardVersion, changes, itemRewards }
  ← 202 Accepted (SettlementPending)

Backend settlement worker
  파티 전체 재화 변경과 아이템 우편 배송을 한 트랜잭션으로 반영
  성공: SettlementPending → Cleared
  영구 오류/재시도 소진: SettlementPending → Failed

Dedicated Server 또는 allocator 장애 보상
  POST .../{sessionId}/finish
  { serverId, outcome: "Failed" }
```

입장 티켓은 해시만 저장되며 기본 60초 동안 유효하다. 잘못된 서버의 소비 시도는
티켓을 소모하지 않고, 올바른 서버의 최초 요청만 성공한다. UE 서버는 응답의
던전 세션 ID와 실제 Steam 연결 ID를 모두 대조한 뒤 `PlayerState`를 만든다.

## 전용 서버 환경변수

```text
PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN
PROJECT_RPG_GAME_SERVER_ID
PROJECT_RPG_DUNGEON_SESSION_ID
```

`PROJECT_RPG_GAME_SERVER_ID`와 `PROJECT_RPG_DUNGEON_SESSION_ID`는 할당 서비스가
세션을 `activate`한 뒤 전용 서버 프로세스를 시작할 때 주입한다. 게임 서버 토큰은
해당 서버 ID와 던전 세션에만 결합된 단기 자격 증명이다. 관리자 토큰과 게임 서버 토큰은
클라이언트 빌드나 `.ini`에 넣지 않는다.

소스 빌드 엔진으로 `Project_RPGServer.exe`를 만든 뒤에는 개발용 단일 포트
allocator를 다음처럼 실행할 수 있다.

```powershell
Backend/Tools/Start-LocalDungeonServer.ps1 `
  -ServerExecutable 'C:\path\Project_RPGServer.exe' `
  -PublicHost '127.0.0.1' `
  -Port 7777
```

세션 ID를 생략하면 가장 오래 기다린 던전 하나를 원자적으로 claim한다. 특정 세션을
수동 할당하려면 `-DungeonSessionId '{session-guid}'`를 추가한다. 이 도구는 먼저
`{ serverId, serverAddress }`로 백엔드 `claim` 또는 `activate`를 호출하고,
세션 전용 게임 서버 토큰·서버 ID·던전 세션 ID를 자식 프로세스에만 상속시켜 숨김 창으로 서버를
시작한다. PID와 로그 경로에는 비밀값을 기록하지 않는다. 같은 세션의 기록된 서버
프로세스 또는 같은 포트의 서버가 아직 실행 중이면 중복 실행하지 않는다.
프로세스 실행 자체가 실패하면 이미 claim한 세션을 즉시 `Failed`로 보상 처리한다.

여러 로컬 서버를 계속 공급하려면 supervisor를 실행한다.

```powershell
Backend/Tools/Run-LocalDungeonAllocator.ps1 `
  -ServerExecutable 'C:\path\Project_RPGServer.exe' `
  -PublicHost '127.0.0.1' `
  -StartPort 7777 `
  -PortCount 4 `
  -PollIntervalSeconds 5
```

supervisor는 포트별 PID·실행 파일 경로·프로세스 시작 시각을 대조해 PID 재사용을
기존 서버로 오인하지 않는다. 빈 포트마다 Waiting 세션을 claim하며, 프로세스 종료를
발견하면 백엔드를 조회한다. 동일 할당이 아직 `Loading` 또는 `InProgress`일 때만
`Failed`로 정리하고, 이미 `Cleared`, `Failed`, `Closed`인 세션은 변경하지 않는다.
`-RunOnce`를 지정하면 한 번만 스캔하므로 CI 및 로컬 진단에 사용할 수 있다.

UE 서버는 `start` 응답이 확인될 때까지 시작 보고를 재시도한다. 클리어 시에는
`settle-rewards`가 202 응답을 반환할 때까지 heartbeat와 재전송을 유지한다. 백엔드는
요청을 먼저 영속 작업으로 저장하고 세션을 `SettlementPending`으로 바꾼다. 이후 워커가
모든 파티원의 재화 변경과 아이템 우편 배송을 원자적으로 커밋한 뒤에만 `Cleared`로
전이한다. 아이템은 세션·캐릭터·보상 순번으로 만든 결정적 ID와 세션 전용 `Mail`
컨테이너에 저장된다. UE 프로세스나
백엔드 프로세스가 중간에 종료되어도 작업 lease와 결정적 영수증으로 이어서 처리한다.

일반 `finish` 엔드포인트는 `Failed` 장애 보상만 허용한다. `Cleared` 직접 호출은 409로
거부되므로 보상 정산을 우회하거나 정산 요청과 경합할 수 없다. 보상이 없는 클리어도
빈 `changes`, 빈 `itemRewards`와 별도 보상 버전으로 같은 영속 정산 경로를 사용한다.

실제 Steam 및 PostgreSQL 운영 환경에서는 다음 값도 설정한다.

```text
STEAM_WEB_API_PUBLISHER_KEY
STEAM_APP_ID
STEAM_TICKET_IDENTITY
POSTGRES_CONNECTION_STRING
```

`POSTGRES_CONNECTION_STRING`이 있으면 PostgreSQL 저장소로 전환되며 시작 시
`Database/schema.sql`을 적용한다. 연결 문자열과 비밀키는 저장소에 커밋하지 않는다.

## 운영 전 남은 작업

- 다중 호스트 allocator 조정, graceful drain, 서버 시작 health timeout
- 실제 PostgreSQL에 대한 통합/부하 테스트
- TLS, secret manager, rate limit, 관측성
- 재접속 유예와 전용 서버 장애 복구 정책

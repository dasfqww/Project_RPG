# Project RPG Backend

PvE 인스턴스형 MORPG를 위한 ASP.NET Core 백엔드다. UE Dedicated Server가
전투·이동·AI를 판정하고 Iris가 실시간 상태를 복제하며, 이 서비스는 Steam 인증,
캐릭터/인벤토리 영속화, 던전 참가 lease와 일회용 입장 티켓을 담당한다.

## 현재 구현 범위

- Steam Web API 티켓 검증 및 opaque Bearer 세션 발급
- 서버 `issuedAt`/`expiresAt` 메타데이터와 UE 단조 증가 시간 기반 플레이어 인증 만료
- 계정, 캐릭터, 인벤토리 저장
- 첫 성장 구간의 경험치·해금·SP·트라이포드·8슬롯 저장 API. 구현·검증 범위와 UE 연결 잔여는 [성장 API](CHARACTER_PROGRESSION_API.md) 참고
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
- PostgreSQL 연결을 확인하는 liveness/readiness와 장애 복구 검증
- 인증 전 IP 및 인증 후 주체별 API 요청 제한

## 구형 인벤토리 API의 서버 권한 범위 (2026-11-01)

호환용 `/api/loadInventory`, `/api/saveInventory`와 `inventory_items`는 유지하지만,
게임 서버 토큰만 있다는 이유로 임의 캐릭터를 조회하거나 전체 덮어쓰기 할 수는 없다.
서버 조회/저장은 토큰의 정확한 Dungeon Session·배정 Server ID, `Loading`/`InProgress`,
세션/멤버/캐릭터 lease와 실제 소유자를 확인한다. 동일 Server ID를 다른 세션에서 재사용해도
권한을 공유하지 않으며 같은 계정의 미참가 캐릭터도 제외한다. 소유 플레이어의 조회와
플레이어/관리자의 구형 저장 금지는 유지하고 응답에 `Cache-Control: no-store`를 설정한다.

저장은 API 밖에서도 검증한다. HTTP가 인증한 배정을 `SaveInventoryAsync`에 전달하고,
Memory는 dungeon gate 안에서, PostgreSQL은 부모 던전 행 잠금과 DELETE/INSERT의 같은
트랜잭션에서 재확인한다. 자격을 잃으면403/쓰기0, INSERT 실패는 기존 inventory를 복원한다.
저장소의 배정 인자 생략은 기존 신뢰된 프로세스 내부 fixture/import 호환용이며 외부 요청이
생략할 수 있는 옵션이 아니다. 현재 UE는 이 구형 HTTP 함수를 사용하지 않고 Item V2를 사용한다.
이번 수정은 Item V2 권한/아이템·보상/우편, DB 스키마·기존 데이터 이전/삭제를 변경하지 않는다.

수정 전 실제 Memory backend HTTP에서 타 세션 토큰의 저장204를 재현했다. 수정 후 실제
진입점/Bearer HTTP3그룹과 직접 저장소 Memory2/PostgreSQL3그룹을 확인했다. 후자는
실제 행 잠금 대기→다른 트랜잭션의 종료 확정→대기 저장 거절 및 INSERT 실패의 전체 롤백을
포함한다. 초기 테스트의 전역 instance ID 중복/완료 트랜잭션 재정리 실패를 보존하고 fixture만
수정해 저장소만 재검사했다. HTTP와 최종 저장소는 별도 실행/검사 DLL이며 제품 소스4개의
당시/최종/현재 해시가 같음을 확인했다. 112개 닫힌 입력의 격리 Release 빌드 경고/오류0,
UE·기존 DB/서비스·공유 bin/obj/타 담당 실행은 미변경이다. 5개 임시 사본48,849,311 bytes
(46.59MiB)와 소유 tmpfs PG4개를 자동 폐기·잔여 프로세스/Temp/컨테이너·정리 예외0이며,
tmpfs 용량은 파일 정리 용량에 합산하지 않았다. 작은 실패/최종 결과·명령/해시만
`Saved/BuildChecks/LegacyInventoryScope-20261101/`에 보존한다. 삭제된 검사 DLL의 해시는
당시 증거이며 원본 서비스 배포/UE·Steam 통합 성공을 뜻하지 않는다.

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

세션 응답의 `members[]`는 기존 ID/참가 시각/lease와 선택적인 `characterName` 표시명을
포함한다. 별도 조회 권한/입력 필드는 추가하지 않으며 이름은 인증 식별자가 아니다.
구버전/null 이름의 fallback과 저장소별 연결 범위는
[`ONLINE_LOBBY_UI.md`](../Docs/ONLINE_LOBBY_UI.md#같은-던전-파티원의-캐릭터-이름-2026-10-25-소스-구현검증-보류)를 따른다.

플레이어 로그인 응답은 `{ accessToken, expiresAt, steamId, issuedAt }`이다.
새 UE 클라이언트는 `issuedAt`을 요구하므로 백엔드를 먼저 배포한다. 시계 오차·변경,
요청 지연 차감 및 만료/401 재인증 계약은
[`PLAYER_AUTHENTICATION_CLOCK.md`](../Docs/PLAYER_AUTHENTICATION_CLOCK.md)를 참고한다.

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

방장(모집 완료)
  POST /api/dungeon-sessions/{sessionId}/departure
  { characterId, requestId, expectedLobbyRevision }
  ← Waiting + departureRequestedAt (이때부터 할당 가능)

방장(배정 전 취소)
  POST /api/dungeon-sessions/{sessionId}/departure/cancel
  { characterId, requestId, expectedLobbyRevision: 출발을 처음 요청한 버전 }

할당 서비스
  POST /api/dungeon-sessions/claim
  Authorization: Bearer {administrator token}
  { serverId, serverAddress }
  ← 카탈로그가 지원하며 출발 요청된 세션을 Loading으로 원자적 전환
    + serverMapPackage (관리자 배정 응답 전용)

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
  { joinTicket, serverId, consumptionRequestId? }
  ← { dungeonSessionId, characterId, steamId, connectionGeneration }

Dedicated Server
  POST .../{sessionId}/settle-rewards
  { serverId, rewardVersion, changes, itemRewards }
  ← 202 Accepted (SettlementPending)

Backend settlement worker
  파티 전체 재화 변경과 아이템 인벤토리 우선/초과 우편 지급을 한 트랜잭션으로 반영
  성공: SettlementPending → Cleared
  영구 오류/재시도 소진: SettlementPending → Failed

Dedicated Server 또는 allocator 장애 보상
  POST .../{sessionId}/finish
  { serverId, outcome: "Failed" }
```

입장 티켓은 해시만 저장되며 기본 60초 동안 유효하다. 잘못된 서버의 소비 시도는
티켓을 소모하지 않고, 올바른 서버의 최초 요청만 성공한다. UE 서버는 응답의
던전 세션 ID와 실제 Steam 연결 ID를 모두 대조한 뒤 `PlayerState`를 만든다.
`connectionGeneration`은 그 멤버의 티켓 소비마다 증가하는 백엔드 입장 세대다.
티켓 소비만으로 실제 입장 완료나 단절 유예 취소를 확정하지 않는다. UE는 이
필드를 파싱하고 실제 `InitNewPlayer` 성공 뒤 서버 전용 입장 확인 API를 호출한다.
확인 성공 전에는 `PlayerState` 신원·Pawn·기본 `PostLogin`을 활성화하지 않는다.
게임 클라이언트 튕김 자동 탈퇴는 구현 중이다.

배정 서버는 `POST /api/join-tickets/consumption-evidence`에
`{ dungeonSessionId, serverId, consumptionRequestId, ticketHash, expectedSteamId }`를 보내
기한이 지난 소비 기록을 읽을 수 있다. `ticketHash`는 원본 티켓의 SHA-256 hex이며
응답은 `{ state: "Recorded", consumption: { ... } }` 형태의 과거 증거이지 입장 허가가 아니다.
기록이 없으면 409 `join_ticket_consumption_unresolved`이며 미커밋이나 안전 가드 해제의
증거로 사용할 수 없다. 일반 재시도 허용은 계속 최초 소비 후 10분으로 제한된다.
UE의 기한 초과 요청 보존/이 조회 연결은 아직 별도다.

실제 UE 입장 완료 뒤 배정 전용 서버는 세션 전용 서버 토큰으로
`POST /api/dungeon-sessions/{sessionId}/members/{characterId}/admission-confirmation`에
`{ "steamId": "...", "connectionGeneration": 1 }`을 보낼 수 있다. 백엔드는 세션·서버·
멤버·현재 세대와 서버 시각을 확인하고, 최초 단절로부터 10분 **미만**일 때만
`disconnected_at`을 지운다. 성공은 200, 만료되었거나 이미 확인된 연결이 다시
단절된 뒤의 재전송은 409다. 같은 연결이 살아 있는 동안의 중복 확인은 200으로
응답한다. UE는 이 API를 호출하지만 새 UE 바이너리의 실제 네트워크 검증은 아직
완료되지 않았다. 확인 응답이 유실되거나 실패하면 입장은 닫힌다. 확인은 커밋됐으나
응답만 유실된 경우에는 UE가 같은 세대의 단절 보고를 시도한다. 보고가 확인보다
먼저 저장되어도 `disconnected_generation`이 같은 세대의 늦은 입장 확인을
막는다. 실제 UE/HTTP/PostgreSQL 응답 유실 통합 검증은 아직 필요하다.

## 명시적 던전 포기 (개발 단계)

`POST /api/dungeon-sessions/{sessionId}/members/{characterId}/abandon`은 인증된
플레이어의 자기 캐릭터만 `Loading`/`InProgress`에서 포기시키고 이번 보상 대상에서
제외한다. 남은 파티는 유지하고 마지막 참가자가 포기하면 세션을 닫는다. 정산이 먼저
시작되면 새 포기는 거절하며, 이미 성공한 요청은 저장된 영수증으로 멱등 재시도한다.
기존 대기방 DELETE와 로컬 비상 복귀는 별도다.

개발용 로컬 로비의 UE 포기 확인창은 구현했으며 기본 실행/게임 중/Shipping에는 공개하지 않는다.
`GET /api/dungeon-sessions/{sessionId}/membership-snapshot`은 세션 전용 게임 서버 토큰과
배정된 서버를 대조하여 현재 멤버 기록과 포기 영수증을 일관되게 반환한다. 전원 포기 후에는
기존 제한 유예 안에서 조회만 허용하며 입장/하트비트/정산 권한은 복구하지 않는다.
메모리·실제 HTTP 및 PostgreSQL 신규 조회/공유 행 잠금/유예 검증을 완료했다.
UE 서버의 주기 조회와 포기 영수증이 확인된 늦은 입장 차단도 연결했다. 이미 접속한 캐릭터의
권한 회수/퇴장과 실제 UE HTTP·2~4인 게임 E2E는 후속 단계다. 계약·실행·배포 제한은
[DUNGEON_ABANDONMENT.md](../Docs/DUNGEON_ABANDONMENT.md)를 따른다.

2026-10-25 파티 HUD용 선택 필드 `characterName`·`disconnectedAt`·`reconnectExpiresAt`·
문자열 `connectionGeneration`/`disconnectedGeneration`과 저장소 시계의 `observedAt`을
추가했다. 단절 세대는 새 티켓 소비 세대와 별개이며 이름/참고 기한으로 권한을 판정하지
않는다. 표현 불가능한 기한은 생략하되 명단/불변 영수증 조회는 유지하며 조회가 멤버를
만료시키지는 않는다. 기존 서버 전용 인증·스키마·10분/보상 정책은 유지한다. 변환 검사와
UE 연결/실행 단계는 [파티 전투 HUD](../Docs/DUNGEON_PARTY_COMBAT_HUD.md)를 따른다.

## 던전 콘텐츠 허용 목록과 맵 배정 (2026-10-23)

`ProjectRpg.Backend/appsettings.json`의 `DungeonContent:Entries`는 운영자가 관리하는
새 세션/할당 허용 목록이다. 플레이어의 생성 요청에는 여전히 캐릭터·ID·난이도만 받으며,
맵/실행 파일/보상 설정을 클라이언트로부터 받지 않는다. 기본값은 기존 실물 콘텐츠 하나다.

```json
"DungeonContent": {
  "Entries": [
    {
      "DungeonId": "Dungeon.RaidSwordMan",
      "MapPackage": "/Game/Maps/bossmap",
      "Difficulties": [ "Easy", "Normal", "Hard", "Hell" ]
    }
  ]
}
```

- 시작 시 전체 목록을 검증하고 프로세스 수명 동안 고정한다. 1~64개, 정확한 ASCII ID,
  중복 없는 알려진 난이도 1~4개, `/Game/` 또는 `/Engine/` 아래 ASCII 패키지 경로만
  허용한다. 빈 목록/중복/잘못된 행은 DB 초기화 전에 시작 실패하며 permissive fallback이 없다.
  맵 경로에 `.asset`, URL 옵션, 공백/명령 인자, 빈 세그먼트나 `..`는 넣지 않는다.
- 새 생성은 정확한 ID·난이도 쌍만 허용한다. 미등록 ID는 400
  `dungeon_content_not_available`, 해당 ID의 미지원 난이도는 400
  `dungeon_difficulty_not_available`이다. 앞뒤 공백/대소문자 별칭도 허용하지 않으며,
  거절된 요청은 멤버십/세션을 만들지 않는다.
- `claim`은 동일한 목록을 저장소의 잠금/트랜잭션 안에 전달해 **ID와 난이도 쌍**으로
  필터한다. 미지원된 오래된 Waiting 행이 지원 세션을 막거나 임의 맵으로 배정되지 않는다.
  메모리는 기존 gate, PostgreSQL은 기존 `FOR UPDATE SKIP LOCKED`를 유지한다.
  명시적 빈 저장소 필터는 아무것도 배정하지 않으며, 구형 내부 저장소 호출의 선택 인자
  생략 호환을 HTTP의 우회 경로로 노출하지 않는다.
- `activate`도 변경 불가능한 저장된 ID/난이도를 먼저 검사한다. 미지원 기존 세션은
  409로 거절하며 새 배정/서버 자격을 발급하지 않는다. 일반 조회·active-session 복구,
  기존 배정의 start/heartbeat/입장/정산은 새 목록으로 소급 폐기하지 않는다.
  기존 세션/멤버/보상 데이터는 삭제/변환하지 않으며 기존 만료 정책은 그대로다.
- 관리자 `claim`/`activate` 응답의 `serverMapPackage`는 동일 고정 카탈로그의 선택값이다.
  일반 플레이어/게임 서버 세션 조회에서는 null이고 신규 배정 권한은 계속 관리자 전용이다.
  할당기는 기존 공통 `ServerExecutable`로 **해당 맵의 별도 서버 프로세스**를 실행한다.
  던전마다 실행 파일을 새로 만들거나 클라이언트가 맵을 지정하는 구조가 아니다.
- `Start-LocalDungeonServer.ps1`/supervisor의 `-Map`은 기본값이 없으며, 지정하더라도
  배정 맵과 같아야 하는 운영자 assertion일 뿐 override가 아니다. 누락/안전하지 않은 맵,
  assertion 불일치, 프로세스 실행 실패는 실행을 거절하고 자기 배정만 기존 Failed API로
  정리한다. `testmap` fallback은 없다. 상태 파일/반환값에 ID·난이도·맵을 기록하되 토큰은
  기록하지 않는다. 실행 중인 다른 세션/맵의 포트 재사용도 거절하고 원래 프로세스를 보존한다.
  HTTP·아이템·재화의 백엔드 URL도 자식에 전달하고, 부모 관리자 환경 토큰은 상속하지 않는다.

### 서버 주소 형식과 IPv6 배정 (2026-11-01)

관리자 claim/activate의 `serverAddress`는 `호스트:포트` 또는 `[IPv6]:포트`다. 포트는
1~65535이며 사용자 정보(`user@host`/`user:password@host`), 경로·역슬래시·query·fragment,
제어/내부 공백과 포트 없는 주소는 거절한다. 잘못된 주소는 배정/임대/출발 대기를 바꾸지 않는다.

로컬 실행기의 `-PublicHost ::1`은 `[::1]:7777`처럼 배정하고, 이미 대괄호가 있는 호스트를
이중으로 감싸지 않는다. Editor 개발 프로필의 소켓 바인딩은 기존 raw `-MULTIHOME=::1`을
유지한다. backend URI의 IPv6 루프백은 PS5의 확장 Host 문자열과 단순 비교하지 않고 IP 값으로
대조한다. 정확한 IPv4/IPv6 루프백만 허용하며 `localhost`나 원격 IPv6를 개발 신원 예외로
허용하지 않는다. 기본 IPv4/DNS 및 명시적 Editor/개발 신원 opt-in 조건은 유지한다.

수정 전 HTTP의 user-info 주소 배정과 실행기의 대괄호 없는 IPv6 문자열을 각각 재현했다.
수정 후 실제 backend 진입점의 사설 Memory/Training HTTP3그룹·기동 mock22사례/PS구문,
Release/실행 종료0·경고/오류0·입력115 불변을 확인했다. 이는 IPv6 주소/인자·배정 metadata
검사이며 실제 IPv6 UDP/Iris·UE 접속, Steam/PG 통합이나 운영 배포 성공은 아니다.
근거: `Saved/BuildChecks/DungeonEndpointFormat-20261101/Summary.proof.json`. 자기 두 시도의
artifacts/PS 사본·fixture16,703,786 bytes(15.93MiB)는 자식 종료 후 자동 폐기·잔여/예외0,
작은 명령/입력 해시·실패/최종 로그만 보존했다. 삭제 검사 DLL 해시는 당시 증거만 남는다.

### 로컬 할당 상태의 backend 범위 (2026-11-01)

할당 상태에는 자격 정보가 없는 정규화된 `backendUrl`도 저장한다. 실행 파일/PID/시작
시각이 같더라도 다른 backend에 연결된 프로세스를 현재 backend의 서버로 재사용하지
않는다. URL의 호스트·기본 포트·끝 슬래시는 정규화하되 배포 경로의 대소문자는 구분한다.
다른 호스트/포트/scheme/경로, 자격·query·fragment가 있는 URL은 같은 범위로 인정하지 않는다.

supervisor도 먼저 이 범위를 확인한다. 다른 backend 또는 backend 정보가 없는 구형/손상된
상태는 `Error`로 남기고 해당 포트의 재사용·상태 덮어쓰기·GET/Failed 정리·새 claim을
수행하지 않는다. 원래 프로세스와 상태 파일은 보존한다. 실행 중인 구형 상태를 현재 URL로
추정 보정하지 않으며, 필요하면 소유자가 명시적으로 기존 서버를 정리하거나 별도 상태
디렉터리와 비점유 포트를 선택한다. 일치하는 backend의 정상 재사용·빈 포트 claim과
종료된 서버의 기존 정리는 유지한다. 이 검사는 포트 독점/다중 allocator fencing을 대신하지 않는다.

`Backend/Tools/Test-LocalDungeonAllocatorScope.ps1 -OutputDirectory <자기 결과 경로>`는
실제 상태 파일과 자기 PS 프로세스 신원, 모의 HTTP/기동/종료를 사용한다. 수정 전 다른
backend 포트의 서버가 재사용되던 실패를 재현했고 수정 후 범위/보존/정리 20사례와
기존 직접 영향 launch mock 17사례를 확인했다. UE/실제 HTTP·DB·Steam 실행 증거는 아니다.
본인 fixture/입력 사본 70,135 bytes(68.49KiB)는 자동 폐기했으며 작은 실패/최종 결과와
명령·입력 해시만 `Saved/BuildChecks/AllocatorBackendScope-20261101/`에 보존했다.

### 로컬 서버 재사용의 실행 설정 범위 (2026-11-01)

같은 backend·PID·시작 시각만으로 현재 실행 설정과 같다고 판단하지 않는다. 직접 실행기와
supervisor는 공통 검사로 `PublicHost:Port`의 배정 주소, 정규 경로의 `EditorProject`,
`DevelopmentIdentityTesting`, 명시한 `Map`을 대조한다. 직접 실행기의 명시적 세션 ID와
`RuntimeUserDirectory` 대조도 유지한다. UserDir를 생략한 호출은 기존 UserDir 변경을
요구하지 않는다. IPv6 주소 조립도 공용화하여 `::1`/`[::1]` 모두 `[::1]:포트`와 비교한다.

실행 중 서버의 주소/프로필이 다르면 직접 실행기는 readiness 조회 전에 거절한다.
supervisor는 실행 중/종료 여부를 관측하기 전에 범위를 검사하고, 불일치/주소 누락이면
`Error`만 출력한다. 기존 프로세스·상태 파일을 보존하고 GET/Failed 종료 조정·새 claim을
하지 않는다. 요청 설정을 기존 상태에 덮어써서 일치시키지 않으며, 원래 설정으로 관리하거나
소유자가 기존 서버를 명시적으로 회수한 뒤 새로 실행해야 한다. 일치한 서버 재사용/관측,
종료 서버의 기존 정리와 빈 포트의 배정은 유지한다. 다중 allocator의 포트 독점 보장은 아니다.

수정 전 주소/개발 인증/맵 불일치와 다른 주소의 종료 서버 처리를 4개 실제 PS 호출로
재현했다. 수정 후 `Test-LocalDungeonAllocatorScope.ps1` 37사례와 직접 영향 기동 mock
22사례가 통과했다. 전자는 `-CaseName <이름>`으로 집중 실행할 수도 있다. 모두 자기 상태
파일·PS 프로세스 신원과 모의 HTTP/기동/종료를 사용한 검사이며 UE/실제 HTTP·DB 실행은
아니다. 임시 사본/fixture 152,281 bytes(148.71KiB)는 자동 폐기·잔여/예외0,
작은 명령/입력 해시·실패/최종 결과는
`Saved/BuildChecks/AllocatorLaunchScope-20261101/`에 보존했다.

새 콘텐츠는 클라이언트 `URPGDungeonDefinition`, 실제 GameMode의
`URPGDungeonServerDefinition`(정확한 맵/지원 난이도별 보상), 이 백엔드 목록과 cook 설정을
함께 맞춘다. 백엔드 목록은 UE 에셋의 존재나 보상 내용을 검증/대체하지 않으며, 서버의
실제 맵 대조/첫 보상 payload 고정은 기존 서버 정의 계약이 담당한다. 공통 최대 파티 4명과
인벤토리 우선/초과 우편·멱등 정산 계약은 변경하지 않았다.

기존 HTTP smoke 도구 5개는 기본 `-DungeonId Dungeon.RaidSwordMan`을 사용하고,
별도 허용된 콘텐츠라면 인자로 바꿀 수 있다. UE 보상·아이템·픽업·멤버십 하네스는 자기
백엔드 시작 인자에서만 정확한 fixture ID/맵/난이도를 Entries:1로 추가한다. 운영 기본
목록에는 테스트 별칭을 넣지 않는다. 외부 백엔드를 재사용하는 fixture는 그 운영자가
테스트 콘텐츠를 명시적으로 등록해야 하며 Development라는 이유만으로 우회하지 않는다.

집중 검사:

```powershell
# 선택: PROJECT_RPG_ABANDON_TEST_POSTGRES에 로컬 개발 DB 연결을 설정한다.
Backend/test-dungeon-content.ps1 -RequirePostgres
```

최종 증거는 `Saved/Tests/DungeonContentCatalog/71346fd520764e18ae289b3c4d57eb67/`의
`Verification.proof.json`/닫힌 로그 5개다. 자체 격리 Release 빌드(경고/오류 0), 순수 정책과
잘못된 설정 18종, Memory/실제 PG의 미지원 FIFO 건너뛰기·빈 필터 거절·동시 고유 배정·기존
세션/멤버/임대 보존, 실제 Program의 HTTP 개발 인증→생성 거절/두 ID의 서로 다른 맵
claim/activate→배정 서버 start/active-session 복구가 runner exit 0이었다. 스크립트 13개
파싱, 모의 실행 경계 6종·관리자 토큰 제거/환경 복원, 선택 입력 25개/모듈·닫힌 로그 해시
일치를 확인했다. HTTP는 격리 Memory, PG는 생성한 private schema에서만 실행/정리했고
기존 public 데이터/DB 컨테이너는 유지했다. 최종 HTTP 로그의 fail/Unhandled/Fatal/Assert/Ensure는 0이다.

이 증거는 **실제 UE 실행 파일 기동/자동 입장·새 플레이 가능한 콘텐츠·4인·Steam·Shipping**
성공이 아니다. 기존 UE 전투 E2E/광역 테스트는 반복하지 않았으며, 하네스 호환 수정도 파싱
검사이지 전체 재실행 성공이 아니다. 다른 던전의 실제 에셋 연결, 배포용 Server 타깃,
프로세스 시작 후 health/준비 확인, 다중 호스트/배포 버전 fencing과 카탈로그 버전·배정 맵의
재시작 간 영속 고정/실행 파일별 프로파일은 후속 범위다. 여러 백엔드는 동일 운영 목록으로
배포해야 한다. 이전 중간 proof 두 건은 그 시점의 결과로만 보존했다.

## 개발용 실제 서버 자동 기동과 준비 대기 (2026-10-23)

설치형 엔진에서 배포용 Server 타깃을 만들 수 없는 현재 환경을 위해 할당기에 명시적인
`-EditorProject` 개발 실행 프로파일을 추가했다. 지정하면 정확한 `.uproject`와
`UnrealEditor[-Cmd].exe`가 필요하고, 생략하면 기존 패키지 Server 실행 파일 경로를 유지한다.
Editor를 자동 fallback으로 선택하지 않는다. 개발 프로파일은 숨김 dedicated `-server`,
nullrhi/noSteam/NoLiveCoding/DisablePython과 세션별 짧은 `Saved/LocalServers/` UserDir을
사용하며 맵·보상·에디터 에셋을 저장/변경하지 않는다.

```powershell
Backend/Tools/Run-LocalDungeonAllocator.ps1 `
  -ServerExecutable 'D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  -EditorProject 'C:\UE5\Project_RPG\Project_RPG.uproject' `
  -WaitForReady -StartupTimeoutSeconds 240 `
  -StartPort 7777 -PortCount 1 -RunOnce
```

`-WaitForReady`를 지정한 신규 기동은 다음 증거가 동시에 있어야 `Ready=True`를 반환하고
포트 상태 파일의 `startupState`를 `Launched`에서 `Ready`로 바꾼다.

- 같은 실행 파일/PID/프로세스 시작 시각이 여전히 유효하다.
- 실제 백엔드 조회의 세션·서버·주소·던전·난이도가 배정과 같고 상태가 `InProgress`다.
- 해당 세션 로그에 정확한 신원의 `RPG_DUNGEON_SERVER_READY`가 있다. 이 표시는 실제
  dedicated GameMode가 인증된 던전 ID·현재 맵·지원 난이도를 받아 보상 payload를 고정하고,
  초기화 이벤트 후에도 같은 살아 있는 월드/모드임을 확인했을 때 한 번만 남긴다.

단순 PID 생성, `Loading`, 백엔드 `InProgress`만으로는 준비 완료가 아니다. 조회 실패나 다른
세션/맵의 표시는 근거가 되지 않는다. 별도 서버 콘텐츠 정의가 없는 구형 GameMode도 이
엄격한 준비 표시를 내보내지 않는다. 준비 표시는 토큰/Join Ticket을 포함하지 않으며 실제
클라이언트 입장·전투 준비 전체·보상 지급 성공을 의미하지 않는다.

기동 후 종료/신원 변경/terminal 상태/제한 시간 초과나 상태 기록 실패는 실패로 반환한다.
이 호출에서 직접 만든 프로세스만 실행 파일/PID/시작 시각으로 재확인한 뒤 종료하고,
자기 세션/서버의 기존 Failed API로 정리를 시도한다. 백엔드 정리 확인 실패는 경고로 남기며
다른 배정/남은 파티를 임의로 변경하거나 기록을 삭제하지 않는다. 실행 중인 프로세스의
직접 launcher 재사용에도 `-WaitForReady`를 붙여 재확인할 수 있으나, 그 대기 실패는 기존
프로세스를 종료하거나 Failed 처리하지 않는다. 다른 프로젝트/명시적 세션·맵 재사용도 거절한다.
옵션 생략은 기존 비동기 실행이며 `Ready=False`는 준비를 관측하지 않았다는 뜻이다.
supervisor의 기존 `Running` 이벤트 역시 프로세스 생존 관측일 뿐 준비 확인으로 계산하지 않는다.

새 집중 실행은 `Scripts/Network/Run-LocalDungeonAllocatorStartupTest.ps1`이다. 증거:
`Saved/Tests/LocalDungeonAllocator/de104d3fb9d8485c96b31b341020254b/Verification.proof.json`.
격리 HTTP Memory 백엔드의 실제 개발 인증/캐릭터/Waiting 생성 뒤 **실제 supervisor claim→
실제 UE bossmap/Normal 시작→인증된 콘텐츠 준비 표시→같은 프로세스 재사용/잘못된 맵 거절**이
runner exit 0이었다. 하네스가 activate/start를 대신 호출하거나 준비 표시를 주입하지 않는다.
실제 포트 18885 바인딩과 GameNetDriver Iris=1, 잘못된 재사용 뒤 같은 서버·멤버 보존을
확인했다. 실제 클라이언트는 연결하지 않았다.

자체 Editor 타깃 17개 작업/모듈·관련 공유 입력 플러그인 링크/전체 Success와 별도 백엔드
Release 경고/오류 0, 할당기 모의 경계 7종·readiness 모의 7종/스크립트 6개 파싱도 통과했다.
선택 입력 19개/Project DLL·닫힌 로그 13개(실제 서버·백엔드 3개와 빌드/모의 로그 포함)의
해시를 고정했다. 실제 서버/백엔드 로그의 Error/fail/Unhandled 및 Fatal/Assert/Ensure는 0이다.
공유 입력 플러그인의 기능 변경을 이번 온라인 기능 변경으로 계산하지 않는다. 자기 서버와
격리 Memory 백엔드만 종료했고 기존 에디터/DB·에셋은 유지했다. PostgreSQL·기존 전투/보상
E2E는 반복하지 않았다. 다음은 로비 생성→자동 할당→실제 클라이언트 입장의 연결이며,
배포용 Server/Steam·다른 PC/Shipping·새 콘텐츠·4인은 여전히 별도다. 재시작 간 영속
카탈로그/맵 버전 고정, 실행 프로파일/다중 호스트 조정도 완료하지 않았다.

2026-10-24 후속: 같은 러너의 `-LobbyEntry -ClientCount 2`는 실제 로비 생성/참가→자동
할당→클라이언트 입장까지만 실행한다. 로컬 no-Steam 전용 `-DevelopmentIdentityTesting`은
명시적 EditorProject/loopback 주소에서만 기존 Steam 신원 비교 예외를 전달하고 실제 소켓도
루프백에 바인딩한다. 기본/패키지 서버의 티켓·멤버십 검사는 그대로다. 실제 검증 범위와
상시 할당 전 모집/출발 상태를 분리해야 하는 제약은
[로비 입장 계약](../Docs/ONLINE_LOBBY_UI.md#로비-생성에서-자동-할당실제-2인-입장까지-2026-10-24)을 따른다.

2026-10-24 출발 요청 분리 후에는 관리자 수동 activate도 새 Waiting 방의 방장 출발 요청을
요구한다. 관리자/서버 토큰으로 플레이어 출발 의사를 대신 만들지 않는다.
`DungeonSessions:MinimumDeparturePartySize` 기본 1은 기존 솔로 흐름을 보존하며,
`Tools/Request-DungeonDeparture.ps1`는 기존 테스트 도구에서 방장 플레이어 자격으로만
호출하는 helper다. 실제 로비는 출발/취소 버튼을 사용한다. 새 스키마/API와 클라이언트를
함께 배포해야 하며 구형 방에 방장/출발을 추정해 채우지 않는다. 권한·재시도·이탈·복원과
최종 증거는 [출발 요청 계약](../Docs/ONLINE_LOBBY_UI.md#모집방장-출발-요청-분리-2026-10-24)을 따른다.

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

### 던전 세션 조회의 서버 자격 범위 (2026-11-01)

`GET /api/dungeon-sessions/{id}`는 게임 서버 자격의 `DungeonSessionId`를 조회 ID와
먼저 대조하고, 저장된 배정의 서버 ID도 함께 확인한다. 서로 다른 던전에 같은 서버 ID를
사용하더라도 자격이 결합된 자기 던전만 조회할 수 있다. 범위가 다르거나 없는 서버 자격은
저장소 조회 전에403이며, 관리자/해당 계정 멤버의 기존 조회 권한은 유지한다. 세션 GET은
비공개 상태 응답의 캐시를 막기 위해 `Cache-Control: no-store`를 지정한다.

실제 backend 진입점을 사설 Development/Memory Training 프로세스로 띄워 두 플레이어가
출발 요청/관리자 activate를 거쳐 같은 서버 ID·서로 다른 세션/토큰을 받는 상황을 구성했다.
수정 전 교차 조회200을 재현했고, 수정 후 실제 Bearer/HTTP4그룹에서 양방향 교차·알 수
없는 세션403, 자기 서버/멤버/관리자200, 타 계정403/미인증401, 기존 snapshot/start 차단과
전체 파티/배정/버전/임대 불변을 확인했다. Release 빌드/집중 실행0·경고/오류0·입력110개
불변이다. UE/Steam/PostgreSQL 통합이나 실행 중 서비스의 배포/재시작은 수행하지 않았다.

작은 명령/해시·수정 전 실패/최종 로그는
`Saved/BuildChecks/DungeonQueryScope-20261101/Summary.proof.json`을 따른다. 두 시도의
자기 artifacts/bin/obj16,560,130 bytes(15.79MiB)는 자식 종료 뒤 자동 폐기했으며 임시
경로/정리 예외는 없다. 검사 DLL 해시는 당시 증거이며 같은 바이너리 재실행은 불가능하다.

소스 빌드 엔진으로 `Project_RPGServer.exe`를 만든 뒤에는 개발용 단일 포트
allocator를 다음처럼 실행할 수 있다.

```powershell
Backend/Tools/Start-LocalDungeonServer.ps1 `
  -ServerExecutable 'C:\path\Project_RPGServer.exe' `
  -PublicHost '127.0.0.1' `
  -Port 7777
```

세션 ID를 생략하면 허용 목록에서 지원하는 가장 오래 기다린 던전 하나를 원자적으로 claim한다. 특정 세션을
수동 할당하려면 `-DungeonSessionId '{session-guid}'`를 추가한다. 이 도구는 먼저
`{ serverId, serverAddress }`로 백엔드 `claim` 또는 `activate`를 호출하고,
반환된 콘텐츠 맵을 선택하고 세션 전용 게임 서버 토큰·서버 ID·던전 세션 ID를 상속시켜 숨김 창으로 서버를
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
기존 서버로 오인하지 않는다. 빈 포트마다 출발 요청된 Waiting 세션을 claim하며, 프로세스 종료를
발견하면 백엔드를 조회한다. 동일 할당이 아직 `Loading` 또는 `InProgress`일 때만
`Failed`로 정리하고, 이미 `Cleared`, `Failed`, `Closed`인 세션은 변경하지 않는다.
`-RunOnce`를 지정하면 한 번만 스캔하므로 CI 및 로컬 진단에 사용할 수 있다.

UE 서버는 `start` 응답이 확인될 때까지 시작 보고를 재시도한다. 클리어 시에는
`settle-rewards`가 202 응답을 반환할 때까지 heartbeat와 재전송을 유지한다. 백엔드는
요청을 먼저 영속 작업으로 저장하고 세션을 `SettlementPending`으로 바꾼다. 이후 워커가
모든 파티원의 재화 변경과 아이템 지급을 원자적으로 커밋한 뒤에만 `Cleared`로
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

`POSTGRES_CONNECTION_STRING`이 있으면 PostgreSQL 저장소로 전환한다.
2026-10-30 소스부터 운영 일반 기동은 migration 이력을 SELECT/검증만 하므로,
먼저 별도 배포 역할로 같은 릴리스의 `dotnet ProjectRpg.Backend.dll --migrate-schema`를
실행해야 한다. 이 명령은 누락 버전을 적용하고 웹/정산 워커를 시작하지 않은 채 종료한다.
이후 DDL 권한이 없는 별도 런타임 역할로 일반 서버를 실행한다. 계정/GRANT 자동 설정은 아니다.
Development/Testing의 기존 전체 `Database/schema.sql` 재적용은 유지한다.
스키마 적용 경로들은 같은 DB의 트랜잭션 수준 advisory lock으로 직렬화하며,
버전 이력·SQL·커밋이 같은 트랜잭션이다. 첫 전환에는 유지보수/백업이 필요하고
격리 PG에서 기존 데이터 전환/권한 제한 역할의 실제 운영 모드 기동·거절은 집중 확인했다.
실제 운영 DB 전환/역할 배포·publish와 rolling upgrade는 별도다. 연결 문자열/비밀키는 커밋하지 않는다.
실패/취소/시간 초과 회귀와 운영 범위는
[`POSTGRES_SCHEMA_INITIALIZATION.md`](POSTGRES_SCHEMA_INITIALIZATION.md)를 따른다.

## PostgreSQL 통합 검증

개발용 PostgreSQL을 실행하고 전체 API·정산 원자성·백엔드 재시작 영속성을 검증한다.

```powershell
docker compose -f Backend/compose.postgres.yml up -d --wait
Backend/postgres-integration-test.ps1
```

테스트는 실행마다 별도의 Steam ID, 서버 ID, 재화 코드를 사용하므로 같은 개발 DB에서
반복 실행할 수 있다. 세부 옵션과 데이터 제거 방법은
[`POSTGRES_INTEGRATION.md`](POSTGRES_INTEGRATION.md)를 참고한다.

## 운영 전 남은 작업

던전 아이템 보상은 새 UE 페이로드에서 인벤토리 우선, 초과 수량만 우편으로 지급한다.
이전 정산 데이터는 우편 전용 동작을 유지한다. 상세 지급 계약은
[`DUNGEON_REWARD_INVENTORY_FIRST.md`](../Docs/DUNGEON_REWARD_INVENTORY_FIRST.md)를 참고한다.
정산 후 서버의 세션 전용 읽기 API로 재접속 없이 열린 가방·우편과 지갑을 갱신한다.
권한 유예/조회 제한/검증 범위는
[`DUNGEON_REWARD_LIVE_REFRESH.md`](../Docs/DUNGEON_REWARD_LIVE_REFRESH.md)를 참고한다.
실제 80슬롯 포화/부분 스택의 2인 우편 지급·수령·재시작 검증은
[`DUNGEON_REWARD_OVERFLOW_E2E.md`](../Docs/DUNGEON_REWARD_OVERFLOW_E2E.md)를 참고한다.

- 다중 호스트 allocator 조정, graceful drain, 서버 시작 health timeout
- CI 및 운영 사양 PostgreSQL에 대한 통합/부하 테스트
- TLS, secret manager, 다중 인스턴스 edge rate limit, 관측성
- 재접속 유예와 전용 서버 장애 복구 정책

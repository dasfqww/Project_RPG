# Project RPG 온라인 PvE 아키텍처

현재 목표는 MMORPG의 공유 월드가 아니라 던전앤파이터·디아블로 계열의
PvE 인스턴스형 MORPG다. 기본 파티는 1~4명이며, 구조가 안정된 뒤 별도 규칙으로
8인 레이드를 추가한다. PvP와 대규모 필드는 현재 범위에서 제외한다.

## 역할 분리

```text
Steam
  계정 ID · 친구/초대 · 로비/세션 검색
        │
UE5 Client ───── UE5 Dedicated Server
  UI/입력             전투·이동·AI·드롭 판정
        ╲             Iris 실시간 복제
         ╲ HTTPS
          Backend API ─── PostgreSQL
          인증·캐릭터·인벤토리·던전 lease·입장 티켓
```

Steam 로비는 사람을 모으고 접속 목적지를 전달하는 발견 계층이다. 게임플레이
권한과 DB 쓰기 권한은 갖지 않는다. 실시간 플레이는 UE 네트워크와 Iris가 담당하고,
장기 보존 데이터와 서버 간 조정은 백엔드가 담당한다.

## 던전 세션 수명주기

| 상태 | 소유 주체 | 의미 |
|---|---|---|
| `Waiting` | 플레이어/백엔드 | 파티 모집 중, 최대 4명 |
| `Loading` | 할당 서비스 | 특정 `ServerId`가 인스턴스를 예약함 |
| `InProgress` | Dedicated Server | 맵이 준비되고 플레이 가능 |
| `SettlementPending` | 백엔드 정산 워커 | 클리어 요청을 영속화하고 파티 보상을 처리 중 |
| `Cleared` | 백엔드 정산 워커 | 파티 보상 커밋까지 끝난 권위 있는 성공 결과 |
| `Failed` | Dedicated Server | 권위 있는 실패 결과 |
| `Closed` | 백엔드 | 대기/heartbeat lease 만료 또는 빈 파티 |

캐릭터마다 활성 던전 lease가 하나만 존재하므로 동시에 두 파티나 두 서버에 들어갈
수 없다. 현재 설정은 대기 세션 5분, 활성 lease 90초, 전용 서버 heartbeat 30초다.
서버가 비정상 종료되면 종료 보고를 위조하지 않고 heartbeat 만료로 lease를 회수한다.

## 전체 접속 순서

1. 클라이언트가 Steam `GetAuthTicketForWebApi` 결과를 16진수 문자열로 백엔드에
   보내고 플레이어 Bearer 토큰을 받는다.
2. 파티장이 `CreateDungeonSession(CharacterId, DungeonId, Difficulty)`를 호출한다.
3. 초대받은 파티원은 Steam 로비 메타데이터의 `DungeonSessionId`로
   `JoinDungeonSession`을 호출한다.
4. 할당 서비스가 서버 ID와 공개 접속 주소를 정하고 백엔드의 `/claim`으로 가장
   오래 기다린 세션을 원자적으로 가져온다. 특정 세션 수동 할당에는 `/activate`를
   사용한다.
5. 할당 서비스가 아래 환경변수를 넣어 UE Dedicated Server를 시작한다.
6. 서버 `GameMode`가 `/start`를 호출하고 30초마다 `/heartbeat`를 호출한다.
7. 각 클라이언트가 `RequestJoinTicket(CharacterId, DungeonSessionId)`를 호출한다.
8. 클라이언트는 `ConnectWithJoinTicket`으로 `?JoinTicket=` 옵션을 붙여 접속한다.
9. `ARPGGameModeBase::PreLoginAsync`가 플레이어 생성 전에 티켓을 소비한다.
10. 백엔드는 요청의 `ServerId` 결합을 검사하고, UE 서버는 응답의
    `DungeonSessionId`와 실제 연결의 `SteamId`를 대조한다.
11. 검증된 캐릭터 GUID와 던전 세션 GUID만 `ARPGPlayerState`에 소유자 전용으로
    복제하고, 인벤토리 저장 키로 캐릭터 GUID만 사용한다.
12. 클리어 시 `ReportConfiguredDungeonClear`를 한 번 호출한다. 실패 시에만
    `ReportDungeonFinished(false)`를 호출한다.

`start`와 정산 요청은 응답 유실에 대비해 재시도한다. UE 서버는 `settle-rewards`가
요청을 영속화했다는 2xx 응답을 받기 전까지 lease 유지 타이머를 멈추지 않는다.
백엔드가 요청을 수락한 뒤에는 워커가 재시작 복구와 최종 상태 전이를 소유한다.

```text
PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN
PROJECT_RPG_GAME_SERVER_ID
PROJECT_RPG_DUNGEON_SESSION_ID
```

세 값은 전용 서버 프로세스에만 주입한다. `PROJECT_RPG_GAME_SERVER_ID`는
`/activate` 요청의 `serverId`와 정확히 같아야 하며,
`PROJECT_RPG_DUNGEON_SESSION_ID`는 해당 프로세스가 호스팅할 세션 GUID다.

## UE Blueprint 표면

UI Blueprint는 고수준 `URPGDungeonSessionSubsystem`을 우선 사용한다.

```text
SelectCharacter(CharacterId)
CreateDungeonSession(DungeonId, Difficulty)
JoinDungeonSession(DungeonSessionId)
ResumeDungeonSession
RefreshDungeonSession
LeaveDungeonSession
ConnectToDungeon(PlayerController, ServerAddress)
```

클라이언트를 다시 실행했거나 접속이 끊긴 경우에는 Steam 백엔드 인증을
완료한 뒤 `SelectCharacter`와 `ResumeDungeonSession`을 순서대로 호출한다.
백엔드는 해당 캐릭터가 lease를 보유한 `Waiting`, `Loading`, `InProgress`
세션만 반환한다. 복구할 세션이 없으면 이는 오류가 아니라 `Idle` 상태이며,
세션이 `Loading` 또는 `InProgress`라면 복구된 `ServerAddress`로
`ConnectToDungeon`을 호출한다. 재접속도 기존 티켓을 보관하거나 재사용하지
않고 새로운 일회용 입장 티켓을 발급받는다.

서브시스템은 선택 캐릭터와 현재 세션을 보관하고, 동시에 두 요청이 섞이지 않도록
상태를 관리하며, 입장 티켓 발급이 성공하면 자동으로 `ClientTravel`을 실행한다.
UI는 `OnFlowStateChanged`, `OnDungeonSessionChanged`,
`OnDungeonTravelStarted` 이벤트만 처리하면 된다. 실패 사유는 현지화 가능한
`ErrorCode` 문자열로 전달한다.

`UHttpWebManager`의 `CreateDungeonSession`, `RequestJoinTicket`,
`ConnectWithJoinTicket` 등은 저수준 전송 API로 계속 사용할 수 있지만, 동일 UI에서
고수준 서브시스템과 직접 호출을 섞으면 응답 상관관계가 모호해질 수 있으므로 피한다.

Dedicated Server에서 클리어 보상을 지급할 때는 `ARPGGameModeBase`의
`ReportConfiguredDungeonClear()`를 사용한다. GameMode는 현재 난이도로
`DungeonClearRewardsByDifficulty`의 `URPGDungeonRewardDefinition`을 선택하고,
아이템 정의 에셋의 `PrimaryAssetId`와 정의 버전을 영속 전송 타입으로 변환한다. 따라서
클리어 Blueprint가 보상 ID나 수량 배열을 직접 조립하지 않는다. 저수준 호출이 필요한
특수 콘텐츠는 `ReportDungeonClearedWithRewards` 또는 기존 재화 전용
`ReportDungeonClearedWithCurrencyReward`를 계속 사용할 수 있다.

콘텐츠 보상은 다음 순서로 설정한다.

1. 지급할 `URPGItemDefinition`에 유효한 `ItemTag`, `DefinitionVersion`,
   `MaxStackSize`를 지정한다.
2. 난이도마다 `URPGDungeonRewardDefinition` 에셋을 만들고 고유한
   `RewardVersion`, 재화 변경, 아이템 보상을 입력한다.
3. 던전 GameMode Blueprint의 `DungeonClearRewardsByDifficulty`에 에셋을 연결한다.
4. 보스/스테이지의 서버 권위 클리어 이벤트에서 `ReportConfiguredDungeonClear`를 정확히
   한 번 호출한다. 플레이어별 `GiveContentReward`에서는 호출하지 않는다.

보상 에셋은 에디터 Data Validation과 런타임 변환 양쪽에서 식별자, 양수 재화, 중복 재화,
아이템 참조, 최대 스택, 내구도, 스탯 태그를 검증한다. `bGiveReward`가 참인데 현재 난이도
보상 에셋이 없거나 유효하지 않으면 성공 정산을 보내지 않는다. 보상이 없는 콘텐츠는
`bGiveReward`를 끄고 같은 함수를 호출하면 명시적인 `no_reward` 정산 경로를 사용한다.

UE 서버는 보상 버전과 공통 재화·아이템 보상만 전송하며 파티원 목록과 캐릭터 소유권은
백엔드가 세션 스냅샷에서 결정한다. 백엔드는 동일 세션의 동일 명령 재전송을 멱등 처리하고,
다른 내용으로 같은 보상 버전을 재사용하면 충돌로 거절한다.

수락된 명령은 `dungeon_reward_settlements`에 영속화되고 세션은 즉시
`SettlementPending`이 된다. 워커는 파티 전체 재화와 아이템 우편 배송을 단일 원자
트랜잭션으로 커밋하며, 각 캐릭터 영수증과 아이템 인스턴스에는 결정적 ID를 사용한다.
아이템 충돌이 발생하면 같은 정산의 재화도 롤백된다. 커밋 직후 프로세스가 종료되어도 다음
워커가 영수증을 재사용해 `Cleared` 전이를 끝낸다. 영구 도메인 오류나 재시도 한도 소진은
작업과 세션을 `Failed`로 종료하므로 heartbeat로 서버를 무기한 붙잡지 않는다.

파티 UI는 `URPGSteamLobbySubsystem`을 사용한다.

```text
CreatePartyLobby(MaxPlayers)
FindPartyLobbies(MaxResults)
ShowPartyInviteOverlay()
JoinPartyLobbyByIndex(ResultIndex)
AcceptPendingPartyInvite()
PublishDungeonServerAddress()
ConnectToDungeonServer(PlayerController)
LeavePartyLobby()
```

`CreatePartyLobby`는 파티장이 백엔드 `CreateDungeonSession`을 완료한 뒤 호출한다.
검색 결과 참가와 Steam 초대 참가는 모두 Steam 로비 참가 후 백엔드
`JoinDungeonSession`까지 성공해야 `InLobby`가 된다. Steam 초대가 로그인 또는
캐릭터 선택보다 먼저 도착하면 `OnPartyInviteAccepted`와 함께 보관되며, 준비가 끝난
뒤 `AcceptPendingPartyInvite`를 호출한다. UI는 `OnLobbyStateChanged`,
`OnLobbySearchCompleted`, `OnLobbyChanged`, `OnPartyInviteAccepted`를 구독한다.

## Steam 로비 메타데이터

첫 연동에서는 로비에 최소한 다음 값만 둔다.

```text
dungeon_session_id
dungeon_id
difficulty
dungeon_state
server_id
build_id
server_address   (할당 완료 후)
```

캐릭터 소유권, 보상, 인벤토리, 관리자/게임 서버 토큰은 로비에 넣지 않는다. 로비 데이터는
탐색 힌트일 뿐이며 최종 참가 가능 여부는 항상 백엔드가 다시 검증한다.
allocator는 공개 접속 주소를 백엔드 세션에 기록한다. 클라이언트는 로비의
`server_id`와 주소가 백엔드 세션과 일치하는지 확인하고, 백엔드 응답의 주소만
최종 접속 기준으로 사용한 뒤 일회용 입장 티켓을 요청한다.

## 로컬 및 배포 제약

현재 AppID `480`은 로컬 개발용 Spacewar 설정이다. 실제 출시 전 발급받은 AppID와
Publisher Key로 교체해야 한다. 클라이언트의 백엔드 주소는
`[/Script/Project_RPG.HttpWebManager] ApiUrl=` 설정으로 HTTPS 운영 주소를 넣는다.

Epic Games Launcher 배포 엔진은 `Project_RPGServer` 타깃을 만들 수 없다.
실제 전용 서버 바이너리와 배포 파이프라인에는 UE 5.8 소스 빌드 엔진이 필요하다.
Launcher 엔진에서는 Editor 및 일반 Game 타깃으로 코드/네트워크 로직을 검증한다.

## 다음 구현 순서

1. 소스 빌드 UE의 `Project_RPGServer` Cook/Stage
2. 실제 Steam 2계정 및 PostgreSQL E2E
3. 다중 호스트 allocator 조정, graceful drain, 장애 재할당 정책
4. 결과 정산 원장, 재시도 idempotency, 재접속 정책
5. 4인 던전 부하 측정 후 별도 8인 레이드 프로파일

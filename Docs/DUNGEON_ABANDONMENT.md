# 던전의 명시적 포기 — 저장·클라이언트 계약

## UE 멤버십 전송의 책임 (2026-11-14)

`FRPGDungeonMembershipTransport`가 멤버십 GET과 단절 만료 POST의 공유 진행 요청·
세대·취소/종료 상태를 소유한다. 동일한 할당/자격 검사, URL·서버 전용 헤더·timeout
작성과 응답 상관 처리를 한 경로로 묶었다. Manager의 기존 API는 유지하고 객체에
위임하며 호스트는 현재 endpoint/세션/서버·실행 여부·서버 자격 검증/적용만 제공한다.
플레이어 우선 공통 헤더를 사용하거나 Player/Admin 자격으로 대체하지 않는다.

GET/POST는 기존처럼 한 진행 슬롯을 공유한다. 취소는 세대를 올리고 delegate를
해제한다. 종료 때는 기존 순서대로 먼저 callback을 차단하고 다른 복구 서비스를 종료한
뒤 요청을 취소한다. 완료는 현재 세대/정확한 요청일 때 자기 슬롯을 먼저 해제하고,
다른 할당의 GET은 실패로 완료하며 다른 할당의 POST는 후속 조회를 예약하지 않는다.
호스트 UObject와 서비스의 약한 참조는 소멸 뒤 callback 접근과 요청 소유권 순환을 막는다.

GET은 광고된 길이와 실제 UTF8 본문 모두 64KiB 이하일 때 기존 codec을 호출한다.
codec의 별도 문자/기록 수·세션/서버·불변 영수증 검사와 실제 권한 회수/Pawn 퇴장은
바꾸지 않았다. POST는 성공/거절/전송 실패 모두 현재 할당의 후속 GET으로 이어져야
하며, POST 응답이나 멤버 누락·조회 실패만으로 포기/퇴장을 확정하지 않는다.

`Scripts/Tests/Run-MembershipTransportTests.ps1`은 실제 서비스·기존 codec/monitor를
격리 모듈로 링크해 전용 헤더·공유 슬롯·취소/재진입·할당 교체·두 바이트 상한·POST
결과·종료/수명을 검사하고, 전체 UHT와 Manager/기존 멤버십·연결 순서 검사 CPP를
컴파일한다. 주입 응답 검사이며 실제 HTTP/DB·설치 DLL/전체 게임·Iris 퇴장 검증과 구분한다.

2026-10-09에 HTTP API·메모리/PostgreSQL 저장소·경합 검증을 시작했고,
2026-10-10에 UE 개발용 로비의 확인창·HTTP 영수증 검증·같은 요청 재시도를 추가했다.
2026-10-13에는 실제 2인·4인 Iris 퇴장→남은 파티 정산/복귀와 러너의 포기 응답 유실을
통합 검증했고, 2026-10-14에는 실제 UE 로비의 미확정 수동 재시도와 401 뒤
같은 개발 계정 재로그인을 검증했다. 운영 게임 중 포기 UI·Steam·Shipping 검증은 아직 닫혀 있다.

## 정책과 API

기본 정책은 본인만 포기하고 이번 던전의 보상 대상에서 제외하는 것이다. 남은 참가자의
멤버십·서버 할당·세션 만료 시각을 유지하고 마지막 참가자가 포기하면 `Closed`로 닫는다.
이 플레이어 API에는 파티장 전체 중단이나 자동 포기를 추가하지 않는다. 예기치 않은
클라이언트 종료 10분 후 자동 탈퇴·보상 제외는 별도 서버 권위 정책이며 구현/검증 현황은
[DUNGEON_CONNECTION_RECOVERY.md](DUNGEON_CONNECTION_RECOVERY.md)를 따른다.
기존에 지급된 재화/아이템은 회수하지 않는다.

```text
POST /api/dungeon-sessions/{dungeonSessionId}/members/{characterId}/abandon
Authorization: Bearer <player access token>
```

- 인증된 플레이어가 소유한 자기 캐릭터만 호출한다. 관리자/게임 서버/다른 계정은 거절한다.
  요청 본문에서 계정/서버 권한을 받지 않는다. 응답은 `Cache-Control: no-store`다.
- 처음 포기할 수 있는 상태는 `Loading`/`InProgress`다. `Waiting`은 기존 DELETE 나가기를
  사용하며, 기존 API의 대기방 전용 정책은 바꾸지 않는다.
- `SettlementPending`/`Cleared`/`Failed`/`Closed`에서는 새 포기를 거절한다. 기준은 로컬
  승리나 UI 표시가 아니라 백엔드가 정산 요청을 수락하고 참가자 목록을 확정한 시점이다.
- 200 응답은 `dungeonSessionId`, `characterId`, `abandonedAt`,
  `sessionStateAtAbandonment`, `rewardExcluded: true`를 반환한다. 이 상태는 최초 포기
  당시의 상태이며 남은 파티의 현재 상태가 아니다. 다른 참가자 정보/서버 주소/토큰은 없다.
- 이미 성공한 같은 계정·세션·캐릭터 요청은 최초 영수증을 그대로 반환한다. 응답 유실 후
  재시도나 파티의 정산/종료 이후 재시도도 새 포기가 아니다. 다른 계정에는 재사용할 수 없다.
- 401은 인증 실패, 403은 소유권/참가 권한 실패, 404는 없는 캐릭터/세션,
  409의 `dungeon_abandonment_not_available`는 새 포기를 허용하지 않는 상태다.

## 저장 및 경합 계약

`dungeon_session_abandonments`는 세션·캐릭터 복합 키의 불변 영수증 테이블이다.
기존 멤버십을 포기 기록으로 소급 변환하지 않는다. 테이블은 기존 스키마 초기화 잠금
아래에서 추가되며 운영용 버전 migration/DDL 권한 분리의 완료를 뜻하지 않는다.

메모리 저장소에서는 기존 dungeon gate, PostgreSQL에서는 같은 세션의 `FOR UPDATE`
잠금과 단일 트랜잭션으로 다음을 처리한다.

1. 동일 영수증이 있으면 계정만 대조하고 반환한다. 현재 멤버십/새 캐릭터 lease를 지우지 않는다.
2. 상태와 계정·캐릭터 멤버십을 검사한다.
3. 자기 멤버십과 해당 옛 세션의 lease만 제거하고, 해당 세션·캐릭터의 저장된 입장 티켓을 폐기한다.
4. 참가자가 남으면 상태/할당/만료를 유지하고, 전원이 포기했으면 `Closed`로 닫는다.
5. 영수증을 기록하고 함께 커밋한다. 기록 실패 시 멤버십·lease·티켓·상태도 롤백한다.

보상 수락과 포기가 같은 잠금을 사용한다. 포기가 먼저 커밋되면 기존 정산 코드가 남은
멤버만 불변 보상 목록에 캡처한다. 정산이 먼저 수락되면 포기는 거절하고 기존 목록을
유지한다. 전원 포기가 먼저면 정산을 거절하여 빈 참가자 작업을 만들지 않는다.

입장 티켓 소비도 같은 세션 잠금 이후 멤버십·lease·서버·만료를 검증한다. 포기 뒤 늦게
저장된 티켓으로도 멤버십을 복원할 수 없다. 포기 전 이미 소비된 티켓/기존 연결을 소급
취소하는 API는 아니므로 게임 서버의 실제 퇴장/입장 직후 재검증은 후속 연결이 필요하다.

로컬 비상 복귀는 이 API를 호출하지 않으며 포기 기록이나 보상 제외를 만들지 않는다.
정상 완료 복귀와의 구분은 [DUNGEON_CONNECTION_RECOVERY.md](DUNGEON_CONNECTION_RECOVERY.md)를 따른다.

## 실행과 검증

```powershell
docker compose -f Backend/compose.postgres.yml up -d --wait
$env:PROJECT_RPG_ABANDON_TEST_POSTGRES = 'Host=127.0.0.1;Port=54329;Database=project_rpg;Username=project_rpg;Password=project_rpg_dev_only'
dotnet run --project Backend/ProjectRpg.Backend.DungeonAbandonmentTests/ProjectRpg.Backend.DungeonAbandonmentTests.csproj -- --require-postgres
```

위 비밀번호는 기존 Compose의 로컬 개발 전용 값이다. 운영에서는 사용하지 않는다.
검증기는 원격 DB를 거절하고 자체 생성한 private schema만 사용/정리한다. 환경변수가
없으면 PostgreSQL을 SKIP하며, `--require-postgres`가 있으면 종료 코드 2로 실패한다.

- 새 기능: 메모리 19개 + PostgreSQL 19개 + PostgreSQL 영속 영수증/트랜잭션 롤백/
  실제 admission 잠금 대기 3개, 총 41개 시나리오 성공.
  각 저장소에서 포기/정산·포기/티켓·마지막 참가자/정산 경합을 20회씩, 총 120회 검사했다.
- 실제 루프백 HTTP에서 운영의 새 endpoint와 `ApiAuthenticationMiddleware`/
  `BearerAuthenticator`를 실행했다. 플레이어 auth 데이터는 fixture로 저장한 테스트
  토큰이며 Steam 교환/실제 UE/전체 운영 호스트를 실행한 것은 아니다.
- 권한 거절·멤버십/다른 lease 보존·중복 응답·정산 이후 재시도·만료·취소·새/옛 티켓,
  미가입 자기 캐릭터·관리자/서버/정산 이후 제한된 서버 credential 거절을 검사했다.
- PostgreSQL 영수증은 별도 repository/연결에서 재조회했다. PostgreSQL 프로세스나
  전체 백엔드를 재시작한 포기 E2E와 구분한다. 제약 조건으로 영수증 INSERT를 실패시켜
  롤백/재시도 복구를 확인했고, `pg_stat_activity`의 실제 Lock 대기로 admission 직렬화도 확인했다.
- 최종 로그: `Saved/Logs/DungeonAbandonmentTests-20261009-Confirmed.log`.
  전체 시나리오/검증 범위 및 DLL·테스트·스키마 해시:
  `Saved/Logs/DungeonAbandonmentTests-20261009.proof.json`.
  검증한 Debug 백엔드 SHA-256:
  `BCBF7AE2562A8C93FA1519F999D5741A257DACA2387D1843F3719E9FD26888C2`.
- 기존 회귀: 토큰 6개, API rate-limit 4개, 메모리 보상 지급 25개와 권한 2개,
  실제 PostgreSQL private schema 초기화/업그레이드 10개 성공.
  로그는 `Saved/Logs/DungeonAbandonment-{TokenTests,RateLimitTests,RewardDeliveryTests,SchemaTests}-20261009.log`다.
  기존 보상 저장소의 PostgreSQL 지급 suite는 이번 배치에서 의도적으로 SKIP했다.
- Debug 컴파일 및 Release 빌드 성공. Release 경고/오류 0개:
  `Saved/Logs/DungeonAbandonmentReleaseBuild-20261009.log`. UE Shipping 검증이 아니다.
- 최초 실행은 만료 거절 뒤 fixture의 더 이른 시각으로 읽은 테스트 기대가 잘못되어
  중단됐다. 동일 만료 경계의 정상 GET으로 수정했다. 이후 잠금 검사 추가 실행은 커밋된
  테스트 트랜잭션의 Connection 속성을 정리 중 읽어서 중단됐다. 명시적인 커밋 여부로
  정리를 수정하고 전체를 다시 통과했다. 두 실패는 운영 저장소 수정으로 숨기지 않았고,
  `DungeonAbandonmentTests-20261009.log`와 `...-Verified.log`를 성공으로 계산하지 않는다.
  `...-Final.log`는 당시 추가 잠금/마지막 참가자 경합 전의 성공 이력이다.
- 모든 생성한 포기/스키마 회귀 private schema가 제거됐음을 DB에서 별도로 확인했다.
  기존 public 데이터와 개발 DB volume은 변경/삭제하지 않았다. 개발 PostgreSQL은 켜 두었다.

## 다음 단계와 배포 제한

운영 UX 완료 항목이 아니다. 다음 연결을 마친 뒤 게임 내 포기 동작을 공개한다.

- 게임 서버의 멤버십 갱신·실제 캐릭터 퇴장·이미 연결/입장 중인 플레이어 재검증,
  포기 전 진행 중 요청과 보안 telemetry 큐의 정리/배출 정책을 연결한다.
  이번 HTTP 수락이 UE Actor를 즉시 제거하거나 이미 진행 중인 요청을 롤백하지는 않는다.
- 실제 2~4인 서버/UE UI/DB 정산을 연결해 포기자 지급 제외, 남은 파티 완료,
  응답 유실·재로그인·서버/백엔드 재시작과 재입장 경계를 검증한다.
- 로그아웃/계정 전환, 실제 Steam·외부 네트워크·패키징/Shipping은 별도다.

## UE 클라이언트 첫 단계 (2026-10-10)

현재 노출은 **개발용 로컬 로비에 한정**한다. 비 Shipping 실행에
`-RPGDungeonAbandonmentTesting -nosteam`을 함께 지정해야 확인 버튼을 볼 수 있다.
`RPGDevelopmentLogin`은 기존 개발 로그인 UI를 쓰려는 경우에만 별도로 지정한다.
명령줄 플래그가 없는 기본 실행과 Shipping에서는 dungeon flow의 포기 진입이 거절된다.
이번 단계에서는 게임 중인 원격 클라이언트나 listen host에서 포기하지 못한다.
기존 `DisconnectToLobby`로 먼저 연결을 끊고 로비에 돌아온 뒤 시험할 수 있다.
단, 로컬 연결 종료가 게임 서버 Actor 제거/멤버십 재검증의 완료를 뜻하지는 않는다.

- 로비의 별도 확인 영역에서 본인만 보상 제외됨·남은 파티 유지·정산 시작 후 거절을 안내한다.
  열 때의 계정·캐릭터·세션을 캡처하고 확인할 때 다시 검사한다. 취소는 HTTP를 보내지 않으며
  이미 제출했던 불확실한 POST를 취소하거나 지우지도 않는다. 자동 포기는 없다.
- 최초 요청은 `Loading`/`InProgress`와 기존 자기 멤버십에서만 가능하다. 기존 Waiting DELETE,
  최신 완료 상태를 확인하는 정상 복귀, 로컬 비상 복귀는 유지한다. 승리 결과 UI에 포기를 붙이지 않았다.
- 전용 bodyless POST를 사용하고 플레이어 인증만 보낸다. 관리자/게임 서버 토큰으로 대체하지 않는다.
- 200만 영수증으로 처리하며 정확한 두 GUID·`rewardExcluded: true`·허용된 당시 상태·명시적 UTC
  시각(`Z`/`+00:00`)을 검사한다. 영수증의 과거 상태를 남은 파티의 현재 상태로 적용하지 않는다.
- 수락 전에 캐시/멤버십/선택을 지우지 않는다. 수락 후에는 자기의 옛 캐시만 지우고 계정/캐릭터를
  유지한다. 로비에서 실행하므로 이 동작 자체가 UE Actor 퇴장/여행을 수행하지 않는다.
- timeout·401·403·404·잘못된 200은 결과 불명확이다. 메모리의 세션·캐릭터·계정 식별자를 유지하고,
  로그인/캐릭터 목록 조회는 허용하되 다른 던전 생성·참가·복원·새로고침·입장·Reset을 막는다.
  같은 계정의 로그인 갱신 후 같은 POST로 확인한다. 이미 제출한 시도는 캐시가 정산 상태가 되어도
  영수증을 재요청할 수 있다. 인증된 정확한 409 `dungeon_abandonment_not_available`만 확정 거절로
  처리하여 캐시를 보존한 채 정상 완료 확인으로 돌아간다. 임의 409/오류 본문은 권한을 주지 않는다.
- 요청 세대·제출 계정·인증 헤더를 검사한다. 옛 401이 새 인증을 지우거나 늦은 응답이 새 캐시를
  지우지 못한다. 로컬 복구/이동 실패가 불확실성 식별자를 지우지 않는다.
- 식별자는 현재 프로세스의 메모리에만 있다. 프로세스 재시작 이후 미확정 포기 복구 UX/전체 E2E는
  남아 있으며 영수증/토큰을 임의로 디스크에 저장하지 않는다.

검증기: `Scripts/Tests/Run-DungeonClientFlowAutomationTest.ps1`.
새 `DungeonAbandonmentCodec/Flow/RetryPolicy/Transport/Confirmation` 5개와 기존 던전 검사 14개,
총 19개를 실행하여 모두 성공했다.
검사는 가짜 인증·HTTP 응답과 격리된 world를 사용하며, 실제 HTTP 송신/Steam/DB/서버 퇴장 E2E가 아니다.
요청 구성은 실제 생성기로 URL·POST·빈 본문·플레이어 헤더를 검사하고, 확인창의 실제 UButton
디스패처/취소/만료된 확인과 재구성 정리는 실행한다. 유효한 확인 클릭의 전체 HTTP 수락 경로는 별도다.
최신 변경의 C++ 5개 파일(Codec/Flow/Transport/Lobby/Tests)은 MSVC로 각각 컴파일 성공했다.
공유 산출물을 덮어쓰지 않는 `Saved/Tests/DungeonAbandonmentCompile/20261010`에만 객체를 썼다.
`Compile.proof.json`에 소스·응답 파일·객체·로그 해시와 **컴파일만 검증한 범위**를 기록했다.
이 Compile proof 자체는 DLL 링크/테스트 실행 성공이 아니다.

이후 공유 작업이 최신 게임 모듈을 링크한 DLL로 위 19개를 실제 실행했다.
`Saved/Tests/DungeonFlow/Automation-2d0cbdc4df83401f9d83ab9ba758b630/automation.log`와
`DungeonFlow.proof.json`에 결과·로그/소스 해시·DLL 해시 및 범위를 남겼다.
DLL SHA-256은 `B5E0260B531330F09E4D04732F930C6FBA0A91C33F5AEF5D25CB9737AE3E2619`다.
실행 중 DLL과 관련 소스가 바뀌지 않았음을 검사했다. 테스트 19개 성공과 전체 Editor 타깃
빌드 성공은 구분하며, 실제 HTTP/UI 확인 클릭 수락/서버 퇴장/DB/Steam E2E는 아니다.

전체 UBT 빌드는 다른 공유 작업의 UBA 폴더 접근 오류로 먼저 대기했다. UE 5.8의
`-NoUBA`도 저장소 초기화는 수행하므로 이번 빌드에만 `-UBARootDir`로 workspace 내
전용 캐시를 지정했다. 기본/전역 엔진 설정이나 다른 작업의 빌드/소스는 수정하지 않았다.
실제로 시작한 `DungeonAbandonmentClientBuild-20261010-WorkspaceCache.log`는 빌드 중
수정된 스킬 네트워크 probe의 소스/생성 헤더 줄 번호 불일치로 실패했다.
`-ForceHeaderGeneration`을 쓴 `...-Regenerated.log`는 그 불일치를 해소했지만 다른 작업의
`RPGSkillTripodNetworkProbe`에 미선언 입력 태그, 중복 Spec 변수, 불완전 UObject 타입
오류가 있어 실패했다. 두 로그를 성공으로 계산하지 않는다. 이 별도 스킬 E2E 소스도
수정 중이므로 덮어쓰거나 가짜 통과를 위해 제외하지 않았다.
공유 작업의 `SkillFinalCapNetworkBuild-20261010-Verified.log`는 게임 모듈을 새로 링크했지만
별도 `RPGOptionsUIRuntime` DLL의 LNK1104로 전체 타깃은 실패했다. 위 자동화는 새 게임 모듈로
통과한 별도 증거이며 이 공유 빌드 로그도 성공으로 계산하지 않는다. 전체 Editor 타깃 재검증은 남는다.

## 게임 서버 재검증용 조회 계약 (2026-10-10)

아래는 조회 계약을 추가한 시점의 **준비 단계** 기록이다. 이후 PostgreSQL 검증과 서버
주기 조회/늦은 입장 차단의 최신 상태는 다음 절을 따른다. Pawn 퇴장과 게임 중 포기 UI는
아직 공개하지 않으며 위 개발 로비·Shipping 제한도 유지한다.

```http
GET /api/dungeon-sessions/{dungeonSessionId}/membership-snapshot
Authorization: Bearer {session-scoped-game-server-token}
```

- 배정된 동일 서버·동일 세션의 게임 서버 토큰만 사용한다. 플레이어/관리자/다른 세션은 403이다.
  응답은 `Cache-Control: no-store`이며 세션/서버 ID, 현재 상태/갱신·만료 시각,
  `members`와 `abandonments`를 반환한다. 접근 토큰/서버 주소/인벤토리는 반환하지 않는다.
- `members`는 현재 저장된 캐릭터/SteamID/lease 만료 기록이다. 이 목록만으로 입장이나
  게임플레이 권한을 부여하지 않는다. 정산/종료 뒤에도 남을 수 있는 기록이다.
- `abandonments`는 해당 옛 세션의 불변 캐릭터/SteamID/포기 시각/당시 상태/`rewardExcluded: true`
  기록이다. **네 식별자(세션·서버·캐릭터·SteamID)가 모두 맞는 영수증**만 명시적 포기를 증명한다.
  목록 누락·401/403/404/429·timeout·잘못된 JSON·자연 만료를 포기 성공으로 해석하지 않는다.
- Memory는 기존 dungeon gate, PostgreSQL은 포기/정산/티켓 소비와 같은 부모 세션의
  `FOR UPDATE` 잠금 안에서 두 목록을 읽는다. 회원 삭제 전 기록과 삭제 후 영수증을 섞지 않는다.
  이 GET은 만료 정리, lease 갱신/삭제나 상태 전환을 수행하지 않는다. 별도 DDL은 추가하지 않았다.
- 전원 포기로 `Closed`가 되면 기존 서버 토큰을 즉시 잃는 대신 **마지막 포기의 Closed 영수증**이
  있는 경우에만 기존 `PostSessionGraceSeconds` 안에서 제한된 조회 자격을 유지한다(기본 300초).
  토큰 자체 만료·서버 배정 검사는 유지한다. 일반 게임/입장/하트비트/정산 API는 403이다.
  자연 만료 및 일부 포기 후 남은 파티의 만료에는 이 유예를 새로 부여하지 않는다.
  기존 보안 API의 멤버 검사는 완화하지 않았다. 포기 전 감사 큐의 배출/폐기 정책은 아직 남는다.
- UE native Codec은 200·정확한 세션/서버·허용 상태·UTC 시각·유효한 GUID/17자리 SteamID·진짜
  Boolean true를 요구한다. 배열 누락/중복 캐릭터/멤버-영수증 충돌/비정상 원소를 거부하고 실패 시
  이전 출력도 지운다. 현재 MORPG 계약은 64Ki 문자/총 128개 기록 상한이며 초과 시 권한을 추정하지 않는다.
  `IsExplicitlyAbandoned`는 현재 멤버 누락만으로 true를 반환하지 않는다.

### 이번 단계의 검증 범위

- 백엔드 Debug/Release 빌드 경고/오류 0개. 공유 Backend/bin 대신
  `Saved/Build/DungeonMembershipBackend-20261010` 아래 독립 산출물을 사용했다.
- 메모리/실제 loopback HTTP 신규 11개와 기존 포기 19개, 동시 포기/정산/티켓 60회 및
  멤버십 조회 경합 20회를 통과했다. 이미 소비한 티켓 이후 포기, 전원 포기 후 조회,
  자기 세션의 GET만 허용, 플레이어/관리자/다른 세션/다른 method·path 거절, 유예/토큰 만료,
  자연 만료·새 lease 보존 및 정산/완료 이후 불변 기록을 검사한다. HTTP는 실제 인증 middleware와
  endpoint를 사용하지만 Steam 교환/운영 전체 host/UE 접속은 포함하지 않는다.
- 기존 토큰 6개·요청 제한 4개·인벤토리 우선 보상 25개·제한된 보상 조회 권한 2개도 통과했다.
  총 **67개 고유 백엔드 검사**이며 Release 재실행을 별도 고유 검사로 중복 집계하지 않는다.
  최종 로그: `Saved/Logs/DungeonMembershipBackend-Final-20261010.log`,
  `DungeonMembership-{TokenTests,RateLimitTests,RewardDeliveryTests}-Final-20261010.log`.
  `DungeonMembershipBackend-20261010.proof.json`은 산출물·소스·로그 해시와 실제 검증 범위를 기록한다.
- UE 신규 Codec/Tests 두 파일은 독립 `Saved/Tests/DungeonMembershipCompile/20261010` 객체로
  MSVC 컴파일 성공(각 종료 0). `Compile.proof.json`은 **파일 단위 컴파일** 증거다.
  새 `ProjectRPG.Online.DungeonMembershipSnapshotCodec` 자동화와 runner의 필수 목록(현재 20개)을
  추가했다. 초기 파일 단위 컴파일과 아래 최종 DLL/자동화 실행을 구분하며 기존 19개 통과 DLL의
  증거를 이번 Codec의 런타임 결과로 대체하지 않는다.
- Docker daemon이 현재 꺼져 있어 이번 신규 PostgreSQL 조회/Closed 자격/실제 행 잠금 검사는
  미실행이다. 같은 11개 시나리오 및 실제 잠금 대기를 검증할 테스트를 추가해 두었으며,
  이전 2026-10-09 DB 통과 결과를 새 구현의 증거로 집계하지 않는다. public DB/volume은 건드리지 않았다.
- 초기 백엔드/파일 단위 컴파일 동안 열린 사용자 에디터와 다른 UBT 빌드는 종료하지 않았고,
  이후 에디터/다른 UBT가 모두 끝난 것을 확인한 뒤 아래 전체 빌드를 진행했다. `.NET Release`는 UE Shipping이 아니다.

### 후속 전체 Editor 빌드와 UE 실행 검증

- `Saved/Logs/DungeonMembershipEditorBuild-20261010.log`: Win64 Development Editor 15개 작업
  `Result: Succeeded`, 명령 종료 0. 신규 Codec/Tests의 컴파일과 새 게임 모듈 DLL 링크를 포함한다.
  UBA가 메모리 부족으로 두 컴파일을 재시도했지만 최종 완료했다. 프로젝트 내부의 전용 UBA root를
  사용했고 사용자 에디터/다른 빌드 종료나 전역 엔진 설정·범위 밖 스킬/UI 수정은 하지 않았다.
  앞 절의 오래된 전체 빌드 실패를 이 성공과 혼동하지 않는다.
- 던전 20개: `Saved/Tests/DungeonFlow/Automation-29e2dcbb10644f869bb2affb03217f77/automation.log`
  및 `DungeonFlow.proof.json`, runner 종료 0. 새 멤버십 Codec의 정확한 scope/계정/영수증,
  상태·시각·Boolean·중복·배열·크기 제한·실패 시 출력 초기화를 실제 실행했다.
- 보안 38개: `Saved/Tests/Security/Automation-3337fcd8bb084f4494d0a577e8c22744/automation.log`
  및 `Security.proof.json`, runner 종료 0.
- 보상 스냅샷 3개 최종 재실행: `Saved/Tests/RewardSnapshot/Automation-68d21996be32430891e6612483413d97/automation.log`,
  runner 종료 0. 초기 `Automation-1b1fba0e054848b9bc3191e205409ec0`도 통과했지만 이 runner는 원래
  모듈 해시를 기록하지 않으므로 새 실행을 실행 전후 해시 검사로 감쌌다. 재실행을 고유 검사로
  중복 계산하지 않는다. 합계 **61개 UE 네이티브 검사**이며 보스/실제 네트워크 검사를 새 실행한 것은 아니다.
  `Saved/Logs/DungeonMembershipUE-20261010.proof.json`에 빌드/각 로그 해시, 정확한 테스트 목록과
  **각 실행의 모듈**을 구분해 기록한다.
- 던전 20개·보안 38개의 DLL SHA256:
  `415768D1FD30EC8862FADA592843B58EEA83E9D05F1689D9DED1F352CA48F451`.
  두 runner의 개별 실행 전후 해시 검사는 통과했다. 그 뒤 다른 공유 빌드가 DLL을 갱신하여
  스냅샷 3개 재실행의 전후 SHA256은
  `608D41FF5F9C34D8EE37717AA84A4DD572E80E8C57F896A37E15E4D2BD75791C`다.
  **61개 모두를 하나의 동일 DLL로 검증했다고 주장하지 않는다.** 이 실행은 가짜 인증/HTTP 응답과
  격리 native fixture이며 서버의 실제 membership HTTP 조회·캐릭터 퇴장/Steam/DB/Shipping E2E가 아니다.

다음 연결은 이 조회 계약의 PostgreSQL 검증 후, 구성된 던전 서버에서 단일 in-flight
요청과 세대/세션/서버/접속 신원 검사를 거쳐 명시적 포기자만 처리하는 것이다. 권한을 먼저
무효화하고 진행 중 GAS/이동/아이템 요청 및 telemetry 큐를 정리한 뒤 정상 Logout 경로를 사용해야 한다.
남은 파티/보스 Threat·전멸 판정은 기존 서버 경로를 유지한다. 서버 권한 및 이탈한 Pawn을
공격 대상으로 남기지 않는 `raid-monster-ai` 지침을 적용하며, 클라이언트 수락 ACK를 서버 권한으로 쓰지 않는다.

## 서버 주기 조회와 늦은 입장 차단 (2026-10-10)

PostgreSQL 조회 경계를 실제 실행했고 UE 서버의 요청/증거 캐시/입장 가드를 연결했다.
**이미 연결된 플레이어의 권한 회수·퇴장 완료는 아니다.** 게임 중 포기 UI는 계속 닫아 둔다.

- `UHttpWebManager::LoadConfiguredDungeonMembershipSnapshot`은 구성된 서버 런타임에서만
  bodyless GET을 보낸다. 플레이어 인증을 우선하는 공통 헤더를 사용하지 않고 서버 bearer만
  명시적으로 보낸다. 서버 자격 누락/잘못된 할당/unsafe header/종료 후 요청은 거부한다.
- 요청은 최대 하나만 진행하며 유한 1~30초 timeout, 응답 64KiB byte 상한, 엄격한 기존 Codec을
  사용한다. 요청 포인터·세대·세션·서버를 재검사하고 종료 시 delegate 해제/취소로 늦은 콜백을
  무효화한다. HTTP 실패/잘못된 JSON/외부 scope는 새로운 포기 증거가 아니다.
- `ARPGGameModeBase`는 구성된 세션에서 기본 5초(설정 1~30초) 주기로 독립 조회한다.
  Closed에서는 일반 heartbeat 권한이 없으므로 heartbeat 응답에 의존하지 않는다.
  콜백은 약한 GameMode/world 참조와 read serial을 확인한다. EndPlay는 타이머와 scope를 정리한다.
- `FRPGDungeonMembershipMonitor`는 정확한 불변 영수증만 scope 내에서 누적한다(최대 128개).
  멤버 누락/만료/실패는 포기로 해석하지 않는다. 이후 빈 응답/오류도 이미 확인한 포기를
  되돌리지 않는다. 다른 던전으로 scope가 바뀌면 이전 증거와 진행 중 read serial을 폐기한다.
- 티켓 소비 콜백에서 validated admission을 보관하기 전, `InitNewPlayer`에서 인증 신원을
  주입하기 전에 같은 가드를 검사한다. 일치하는 증거가 있으면 `dungeon_member_abandoned`로
  거절한다. 다른 계정/다른 세션/남은 파티원의 입장은 이 기록으로 거절하지 않는다.
  조회 전 이미 완료된 입장이나 조회 주기 사이의 경합은 **후속 connected actor 회수**가 필요하다.

### 백엔드 실행 증거

- Release 빌드 경고/오류 0개, 메모리 30개 + PostgreSQL 30개 + DB 추가 4개 = 64개 통과.
  각 저장소의 기존 포기 경합 60회와 새 read 경합 20회(회당 조회 4개)를 실제 실행했다.
  실제 PostgreSQL 부모 행 잠금 대기, 전원 포기 후 제한 조회 유예, 자연 만료의 유예 거부,
  불변 영수증 보존·정산 경계·새 lease 불변 및 loopback HTTP 권한 검사를 포함한다.
- DB worker의 `ClaimNext`는 schema 전체의 대기 작업을 선택한다. 이전 정산 테스트의 Pending
  작업을 membership 테스트가 소비하지 않도록 **테스트 suite별 생성 schema를 분리**하고
  claimed session 일치도 명시적으로 검사했다. 운영 worker 선택 정책은 바꾸지 않았다.
- `Saved/Logs/DungeonMembershipServer-BackendBuild-Final-20261010.log`,
  `Saved/Logs/DungeonMembershipServer-Postgres-Final-20261010.log`,
  `Saved/Logs/DungeonMembershipServer-Backend-20261010.proof.json`에 64개 이름·60개 소스/DDL 해시·
  빌드/실행 종료 코드·schema 생성/정리 기록을 남겼다. 검증 중 소스 해시는 같았다.
  최종 검증에서 생성한 `rpg_abandon_test_*` schema 2개만 소유권 검사 후 정리했으며 public 데이터/volume은 보존했다.
  로컬 Docker/PostgreSQL은 실행 상태로 남겼다. 실제 Steam/전체 운영 backend host/UE HTTP E2E가 아니다.

### UE 검증 결과

- 요청 콜백·read serial/단일 in-flight·scope 교체·오류와 누락의 비포기 처리·증거의 단조 유지·
  production admission scope guard·EndPlay 정리의 네이티브 fixture 2개를 추가했다.
  fixture는 HTTP 응답을 주입하며 실제 HTTP 송신/PreLogin handshake/캐릭터 퇴장을 수행하지 않는다.
- 초기 분리 컴파일은 오래된 공유 UHT 헤더 때문에 실패했다. 검증 전용 manifest의 모든 출력
  경로를 `Saved/Tests/DungeonMembershipServerCompile/20261010` 내부로 옮기고 RPG 모듈만 재생성했다.
  UHT 직접 호출의 중복 Log writer 및 임시 manifest 경로 정규화 문제를 수정한 뒤 private UHT와
  Codec/MonitorTests/HttpWebManager/GameMode 4개 파일 컴파일은 모두 종료 0이었다.
  기존 공유 생성 헤더·DLL을 이 분리 검증으로 수정하지 않았다. 컴파일 단독 성공은 DLL/런타임 성공이 아니다.
- 에디터와 다른 빌드 종료를 확인한 뒤 전체 `Project_RPGEditor Win64 Development`를 빌드했다.
  `Saved/Logs/DungeonMembershipServer-EditorBuild-20261010.log`: 58개 작업, `Result: Succeeded`, 종료 0.
  private compile 이후의 최종 요청/테스트 보강도 이 빌드에 포함한다. 전역 설정이나 다른 작업의
  프로세스를 종료하지 않았다. 기존 StructUtils deprecation 경고는 별도 사항이다.
- 동일 DLL에서 **던전 22개·보안 38개·기존 보스 15개·보상 스냅샷 3개 = 78개** 성공/runner 종료 0.
  신규 Monitor/Transport fixture 2개와 기존 Codec·로비/포기/인증/보상·전투 수명주기 회귀를 포함한다.
  검증용 에디터의 초기 로드가 약 100초 걸렸지만 assertion/성공 조건을 완화하지 않았다.
- 실행 로그:
  - 던전: `Saved/Tests/DungeonFlow/Automation-1bcd86b1431f4684a8d753bc5373c999/automation.log`
  - 보안: `Saved/Tests/Security/Automation-162499de1a504770a5dc7783d8b0b329/automation.log`
  - 보스: `Saved/Tests/BossState/Automation-6666cae8612f4ddf8d88997bf5a78a4d/automation.log`
  - 스냅샷: `Saved/Tests/RewardSnapshot/Automation-5be8a91e5df746c6a2e6fc7147e8f910/automation.log`
- 최종 DLL SHA256은 모든 실행 전후에
  `70C8D237C95A844DF00112312DDB0701566A01E633DEDB9902A01B415B0FE8DF`로 동일했다.
  `Saved/Logs/DungeonMembershipServer-UE-20261010.proof.json`에 78개 정확한 이름, 각 실행/빌드 로그
  및 runner proof 해시와 빌드~검증 사이 동일한 소스 8개 해시를 남겼다.
  실제 서버의 주기 조회 HTTP·입장 handshake·접속자 퇴장·Iris 네트워크/Steam/Shipping 검증은 아니다.

다음은 접속 중인 포기자의 신원/아이템·저장 요청/감사 큐 권한을 먼저 회수하고, GAS·이동·Pawn 및
정상 Logout 경로를 정리하는 작업이다. 남은 파티·Threat/전멸 판정을 보존하고 2~4인 실제
backend/UE/DB E2E를 통과한 뒤 게임 중 UI를 공개한다. 클라이언트 ACK는 서버 퇴장 권한으로 쓰지 않는다.

## 접속자 권한 회수 기반 (2026-10-11, 퇴장 연결 전)

포기 영수증을 확인한 서버가 호출할 **네이티브 회수 경계**를 추가했다.
이 단계는 `PollDungeonMembership`의 자동 회수나 접속자 퇴장 완료가 아니다.
GAS/이동/Pawn/정상 Logout 정리까지 구현·검증하기 전에는 조회 콜백에 회수를 연결하지 않는다.
게임 중 포기 UI의 비활성화도 유지한다. `raid-monster-ai`의 서버 권한 원칙에 따라
클라이언트 ACK, 멤버 누락, HTTP 실패를 회수 권한으로 사용하지 않는다.

- `ARPGPlayerState::RevokeAuthenticatedIdentity`는 authority와 현재 세션·캐릭터·Steam의
  정확한 일치를 요구한다. 한 번의 회수는 해당 PlayerState에서 되돌릴 수 없고 재입장에는
  새 PlayerState가 필요하다. 호출자가 먼저 불변 영수증을 검증해야 하며 RPC/BP 명령을 추가하지 않았다.
- 알림 전에 인증 장벽을 세우고 신원·지갑·보상 알림을 비운다. 지갑 타이머와 요청 세대를
  무효화한다. 지갑 응답은 세대뿐 아니라 캡처한 세션·계정·캐릭터를 모두 확인한다.
- 해당 Controller가 여전히 같은 PlayerState를 소유할 때만 아이템 명령과 projection을
  무효화한다. 진행 중 명령/대기 첫-commit 효과와 서버 캐시·인벤토리/우편 read model을
  비우며 늦은 full-load/commit/reward refresh/소비 효과 적용을 차단한다.
  이는 **서버 메모리와 표시의 정리**이며 DB 아이템·우편·이미 확정된 보상을 삭제하지 않는다.
- 아이템 orchestrator의 이후 약한 콜백은 폐기한다. 동기 알림 중 회수된 receipt가 여전히
  orchestrator를 소유할 수 있으므로 sink adapter를 회수 시 파괴하지 않고 EndPlay까지 보존한다.
  각 sink가 인증 장벽을 다시 검사한다. 제출된 DB 작업을 취소/롤백했다고 주장하지 않는다.
- 바닥 아이템의 frozen 지급 명령도 인증 소유자가 사라지면 새 저장 재시도를 중단한다.
  이전 POST의 확정 여부가 불분명하면 아이템을 다른 플레이어에게 다시 노출하지 않고
  claimed/pending 상태를 유지한다. 이미 진행 중인 요청의 유효한 성공 영수증은 월드 아이템만
  정리하며 회수된 projection은 복구하지 않는다. 미확정 건의 별도 조회/복구는 후속 통합 범위다.
- 감사 큐는 정확한 세션/캐릭터/계정만 별도 보관한다. 해당 멤버가 들어 있는 전송 batch는
  로컬 취소 후 나머지 멤버의 event ID를 보존해 다시 예약할 수 있다. 복사된 옛 콜백은 새 batch를
  완료시키지 못한다. 이미 backend가 수락한 감사 기록은 되돌리지 않는다.
- 감사 기록을 dead-letter 파일에 보존하지 못하면 원본 큐를 유지하고 새 전송을 막는다.
  보존 뒤 spool 재작성에 실패해도 원본 파일은 남고 현재 subsystem의 회수 캐시는 재전송을
  차단한다. 이 첫 기반 단계에서는 회수 캐시가 메모리에만 있었다. 이후 아래
  **감사 회수 기록의 영속화와 복원** 단계에서 파일 복원을 구현했다. 영속 기록이 없는 이전
  spool의 초기 멤버십 조회/전송 순서와 실제 서버 재시작 통합은 여전히 남아 있다.

첫 기반 단계의 네이티브 fixture `ProjectRPG.Online.DungeonAuthorityRevocation` 4종은 신원 일치/서버 권한,
중복·재진입 회수, 늦은 아이템·지갑 결과, 남은 멤버의 큐 보존과 감사 보존 실패를 검사한다.
별도 Item/PickupGrant 신규 1종은 회수 후 저장 재시도 중단·불확실한 claim 보존과
진행 중 성공 영수증의 월드 아이템 정리/회수된 projection 불변을 검사한다.
현재 fixture는 실제 HTTP/DB 및 감사 파일 저장 성공을 검증하지 않는다.
최종 빌드와 실행 결과는 아래에 기록한다.

### 검증 중 수정한 회귀

- 최초 Item 실행은 비인증 실패 알림을 검증하는 기존 4개 테스트가 실패한 뒤,
  신규 픽업 테스트의 격리 fixture에서 `bHasBegunPlay` assert로 종료했다.
  서버의 새 인증 장벽이 일반 실패 알림까지 생략한 것이 첫 원인이었다.
  비인증 요청에는 기존 `ServerStateError` 알림을 복원하되 큐/백엔드 제출은 계속 거절한다.
  이미 회수됐거나 EndPlay 중인 컴포넌트는 알림도 보내지 않는다.
  기존 테스트에 인증을 주입하거나 assertion을 완화하지 않았다.
- 픽업 fixture는 actor BeginPlay를 실행하지 않는 격리 월드였다. pending 정리에 필요한
  grant 컴포넌트의 tick 등록/BeginPlay/EndPlay를 정상 짝으로 실행하도록 수정했다.
  실패 이력은 `Saved/Tests/Item/Automation-19c9893669194752871e779707211388/automation.log`에 보존한다.
- 수정 후 첫 재빌드는 DLL 링크 잠금 `LNK1104`로 종료 6이었다. 잠금 원인을 특정하지 않은 채
  읽기 전용 독점 접근 가능 여부와 프로세스를 재확인한 후 재시도했다. 다른 작업의 프로세스를
  종료하거나 소스를 되돌리지 않았다. 빌드 실패 로그도 보존한다.

### 최종 검증 결과

- 전체 `Project_RPGEditor Win64 Development`의 최초 42개 작업과 마지막 수정 전 7개 작업은
  각각 성공했다. 회귀 수정 후 DLL 잠금 실패(5개 작업, 종료 6)를 재시도했고,
  `Saved/Logs/DungeonAuthorityRevocation-EditorBuild-20261011-04.log`의 11개 작업은
  `Result: Succeeded`, 종료 0이었다. 공유 작업의 변경 파일도 포함하는 증분 타깃 빌드이며
  클린/무경고/Shipping 빌드로 계산하지 않는다. 기존 StructUtils 및 AssetRegistry 경고는 별도다.
- 이 자체 빌드의 DLL SHA256은
  `65A4CCF2D019C31E38B3C5C1B9DE0905D48AAD19766AB9D416365BD59AB34F6D`였다.
  검증 전에 별도 공유 모듈 빌드가 DLL을 갱신했다. 종료를 기다린 후 테스트용 DLL을 다시 고정했고,
  아래 **최종 137개 실행은 모두 같은 DLL**
  `0226EBF0817FD189EDC761007F76C33D3C8FD0870F703C3D8F8B334E937DDA65`에서 실행했다.
  자체 빌드 출력과 테스트 DLL을 같은 것으로 표시하지 않는다.
- **던전 26개·아이템/우편 52개·보안 41개·기존 보스 15개·보상 스냅샷 3개 = 137개** 성공,
  각 runner 종료 0. 보안 41개에는 공유 작업에서 추가된 3개가 포함되며 이번 기능의 신규 검사는
  앞서 설명한 신원/아이템/지갑/감사 4개와 픽업 1개다. 최초 던전 성공 26개는 중복 합산하지 않았다.
- 실행 로그:
  - 던전: `Saved/Tests/DungeonFlow/Automation-7d900a33450c4a578fadbc5323abddfb/automation.log`
  - 아이템: `Saved/Tests/Item/Automation-a0b89fea79fd40b7aaa41d67849161c2/automation.log`
  - 보안: `Saved/Tests/Security/Automation-e78d32545cc54538b6e9ee557c21c699/automation.log`
  - 보스: `Saved/Tests/BossState/Automation-49279eaeef9b4483a82b5fe4f547194a/automation.log`
  - 스냅샷: `Saved/Tests/RewardSnapshot/Automation-ac791c7354a346e3af01f534e142e199/automation.log`
- 네 runner는 자체 DLL 해시를 실행 전후 확인하며 스냅샷 runner는 별도 바깥 검증으로 감쌌다.
  `Saved/Logs/DungeonAuthorityRevocation-UE-20261011.proof.json`에 137개 정확한 이름,
  각 로그/runner proof와 빌드 로그 해시, 회귀 수정 후 빌드~검증 동안 같았던 소스/runner 15개
  해시 및 이전 실패를 남겼다. **최종 스냅샷 실행 뒤 다시 공유 빌드가 DLL을 갱신했다.**
  proof의 검증 DLL과 생성 시점 DLL을 구분하며 더 새 바이너리가 이 137개를 통과했다고 주장하지 않는다.
- 서버 자동 회수·GAS/이동/Pawn 정리·Logout/실제 퇴장·실제 HTTP/DB·감사 파일 보존 성공·
  감사 회수 캐시의 재시작 복원·Iris 네트워크/Steam/Shipping 검증은 이번 실행 범위가 아니다.

다음 연결 순서: GAS/이동 및 소유 Pawn의 권한·지연 공격 정리 → 정상 Logout/서버 퇴장 →
명시적 영수증 조회에 회수 연결 → 실제 2~4인 backend/UE/DB 및 이동 RPC 경합 E2E → 게임 중 UI.
엔진 기본 `GameSession::KickPlayer`는 Pawn을 즉시 파괴하므로 이를 그대로 연결해
26절의 이동 RPC 경합을 해결한 것으로 취급하지 않는다 ([레이드 수명주기 기록](RAID_MONSTER_AI_ARCHITECTURE.md)).

## 접속자 전투·이동 격리 (2026-10-11, 실제 퇴장 연결 전)

정확한 `RevokeAuthenticatedIdentity` 경계에 해당 Pawn의 GAS 정리를 연결했다.
서버의 현재 Controller/Pawn/PlayerState 소유 관계를 모두 확인한 ASC만 회수되며,
Pawn 내부의 되돌릴 수 없는 장벽은 PlayerState가 분리돼도 남는다. `raid-monster-ai`의
이탈 타깃 즉시 재검사와 파티별 수명주기 원칙을 적용했다.

- 해당 플레이어의 입력을 지우고 진행 중 능력을 취소한다. 인스턴스의 취소 불가 상태도
  서버 회수 때 종료할 수 있게 한다. 새 능력은 공통 GAS 태그 차단 경계에서 거절하므로
  RPG 능력/BP 태그가 없는 일반 능력도 대상이다. 능력 부여 목록·영구 효과·HP/사망 상태와
  다른 파티원의 ASC는 제거하거나 초기화하지 않는다.
- 회수 이후 일반 GAS 입력 이벤트/payload/TargetData RPC는 콜백과 캐시 생성 전에 거절한다.
  이는 권한 장벽이며 클라이언트 입력 확인을 서버 회수 승인으로 사용하지 않는다.
- 열린 서버 무기 충돌 창과 이동 authorization을 닫고, 이후 재개 요청도 거절한다.
  플레이어 전용 CharacterMovement는 속도·누적 힘·root motion을 비우고 `MOVE_None`을 유지한다.
  회수된 Pawn의 `PerformMovement`와 packed 이동 처리 함수는 새 움직임을 실행하지 않는다.
  NPC의 이동 컴포넌트는 교체하지 않는다.
- 이미 만든 피해 spec/동기 permit도 최종 공통 damage execution에서 source와 target의
  현재 회수 상태를 다시 확인한다. 타깃 쿼리와 보스 Threat 또한 회수된 Pawn을 제외한다.
  Threat getter는 주기적 갱신 전에 타깃을 무효화하고 갱신 때 남은 플레이어를 선택한다.
  전체 Threat나 encounter를 비우는 방식이 아니다.
- 유지 중인 회수된 Pawn은 생존 참가자로 계산하지 않으며 재도전의 체력/이동 복원이나
  재도전 요청으로 권한을 되찾지 못한다. 실제 연결과 참가 기록의 최종 제거는 기존
  정상 Logout/퇴장 통합 단계에 남긴다. 재인증 재입장은 새 PlayerState/Pawn으로 설계한다.

새 네이티브 검사 3종 `Gameplay`, `LateDamage`, `ThreatAndParticipation`은 실제 GAS 종료와
공통 GE 피해 실행, 무기 창·movement 함수, 생존 판정/남은 Threat 보존을 격리 월드에서 검사한다.
실제 HTTP/DB·Steam·멀티클라이언트 Iris 이동 drain/연결 종료 검사가 아니다.

`Run-DungeonClientFlowAutomationTest.ps1 -GameplayRevocationOnly`는 이 3종과 직접 영향을 받는
기존 신원·Threat/피해/재도전/NPC 초기화·무기 충돌/스윕 회귀만 선택한다(총 11종).
이전 137개 전체 성공 기록은 역사적 기반 검증으로 보존하고 이번 변경의 재실행 결과로
표시하지 않는다. DLL/소스/로그 해시는 runner의 새 proof에 따로 남긴다.

자동 멤버십 회수, Pawn 파괴, 실제 퇴장,
실제 서버 재시작의 감사 조회/전송 순서 및 미확정 픽업 복구, 게임 중 포기 UI는 아직
연결/공개하지 않는다. 감사 회수 기록의 파일 복원은 다음 단계에서 별도로 구현/검증했다.

### 이번 단계의 최종 검증

- `Saved/Logs/DungeonGameplayRevocation-Compile-20261011.log`: Editor 타깃 `-NoLink`의
  영향받는 59개 컴파일 작업 Succeeded/종료 0. 링크/런타임 성공과 구분한다.
- 작업 중 선행 공유 빌드 `NPCDeathNetwork-EditorModule-20261011-Final.log`는 새 RPC override
  선언과 이전 오브젝트가 섞인 `LNK2001`/`LNK1120`로 실패했다. 로그/오브젝트/다른 작업의
  프로세스를 삭제하거나 되돌리지 않았다. 위 최신 컴파일 뒤 공유
  `NPCDeathNetwork-EditorModule-20261011-Final-Retry.log`의 lib/DLL 링크 2개는 Succeeded였다.
- 이어 실행한 전체 `Project_RPGEditor Win64 Development` 명령은
  `Saved/Logs/DungeonGameplayRevocation-Link-20261011.log`에서 Succeeded/종료 0.
  해당 명령의 실제 작업은 metadata 1개이며 이미 완료된 공유 링크를 재사용했다.
  자체 명령에서 DLL을 다시 링크하거나 전체 59개를 재컴파일했다고 표시하지 않는다.
- `Saved/Tests/DungeonFlow/Automation-bd2ee9a2822b4b21a7767189e1e133aa/automation.log`:
  새 3종과 직접 영향 회귀 8종, **11종 모두 Success/runner 종료 0**.
  `DungeonFlow.proof.json`에 정확한 테스트 이름, 소스/runner 63개 해시, 실행 전후 같은 DLL과
  로그 해시·검증 범위를 저장했다. 검증 DLL SHA256:
  `742F8D677F4E12A649D5ECA88B67A5A3C3BF255BB717B433FFEC75569664514C`.
  로그 SHA256: `451A72D4799839C94A57475C0ECC44ABFD172EA017D0B0C4760F029BD25BC3F7`.
- 이전 137개 전체/아이템·우편·지갑·감사 회수/실제 네트워크 검사를 다시 실행하지 않았다.
  이번 성공은 네이티브 전투·이동 격리와 선택한 회귀의 성공이다. 실제 Iris queued RPC drain,
  정상 Logout/연결 종료, 서버 영수증 조회의 자동 회수, backend/DB/Steam/Shipping 완료가 아니다.

## 감사 회수 기록의 영속화와 복원 (2026-10-11)

`RPGSecurityTelemetrySubsystem`은 정확한 현재 세션/캐릭터/Steam 계정의 회수 기록을
`Saved/SecurityTelemetry/<session>.revocations.json`에 보존한다. 이는 로컬 감사 재전송을
막기 위한 파생 기록이며, 백엔드의 불변 포기 영수증이나 게임플레이 회수 권한을 대신하지 않는다.
새 RPC/UI, 정상 Logout, 서버 영수증 조회의 자동 회수 연결은 추가하지 않았다.

- 회수 기록을 먼저 저장한 뒤 해당 감사 이벤트를 dead-letter에 보관하고 큐/spool을 정리한다.
  기록 저장에 실패하면 원본 큐/spool을 유지하고 새 전송을 막는다. 이미 backend가 수락한
  이벤트나 진행 중 DB 트랜잭션을 되돌리지 않는다.
- 초기화는 회수 기록 복원 → 기존 spool 로드 → 회수 이벤트 격리 → 전송 예약 순서다.
  오래된 spool이 남아도 복원된 회수 신원과 정확히 일치하는 이벤트를 먼저 제외한다.
  남은 멤버 및 같은 캐릭터/다른 계정의 event ID와 순서는 보존한다.
- schema 1, 정확한 세션, 유효한 0이 아닌 GUID/17자리 ASCII Steam ID, 최대 128명,
  파일별 최대 64 KiB를 검사한다. 같은 파일의 중복 캐릭터도 거절한다. 본 파일과 `.tmp`를
  모두 검사해 일치하는 회수 기록을 합친 뒤 저장한다. 서로 다른 계정의 충돌이나 손상된
  파일은 부분 적용하지 않고 초기 전송을 비활성화하며 원본 파일/spool을 보존한다.
- 저장은 UTF-8 임시 파일 작성 후 이동/교체한다. 파일 수정 시간으로 어느 쪽을 무시할지
  추정하지 않는다. OS 전원 상실의 fsync/내구성 또는 dead-letter의 exactly-once 행 수를
  보장한 것은 아니다. 운영 파일은 이 단계에서 삭제하지 않는다.

### 선택 검증과 남은 통합

- `ProjectRPG.Security.TelemetryRecovery` 신규 4종은 실제 격리 파일을 새 subsystem에서
  읽어 오래된 spool, 본 파일/임시 파일 합치기와 승격, 손상·충돌 12경계, 쓰기 실패/재시도를
  검사한다. 기존 `ProjectRPG.Online.DungeonAuthorityRevocation.Telemetry` 1종과 함께
  **5종 모두 Success/runner 종료 0**. 실제 게임 서버 프로세스를 재시작한 검사는 아니다.
- `Saved/Logs/TelemetryRecovery-ModuleLink-20261011.log`: Development 게임 모듈 컴파일과
  lib/DLL 링크 8개 작업 Succeeded. 전체 Editor 타깃 성공과 구분한다. 선행
  `TelemetryRecovery-Compile-20261011.log`는 실제 작업 0개여서 컴파일 증거로 쓰지 않는다.
- `Saved/Tests/TelemetryRecovery/Automation-8a46c2abe5cc4ecf85569b8328d67a60/automation.log`와
  같은 디렉터리의 `TelemetryRecovery.proof.json`에 정확한 검사 목록, 검증 DLL/로그/빌드 로그
  해시 및 인계 시점 소스 6개 해시를 기록했다. 실행 전후 DLL/관련 소스 불변을 확인했고,
  인계 시점에도 검증 DLL과 이번 변경 소스 3개의 기록된 해시가 일치한다.
  검증 DLL SHA256: `61F9B63C7A867F88055EC59C5A7C5E22DDE34BEF4F4FF1DB21A2A68133266914`.
- `Saved/Logs/TelemetryRecovery-ShippingCompile-20261011.log`는 단일 파일 NoLink 작업 예약 뒤
  종료 결과가 없고 이전 실행 ID도 조회할 수 없다. **Incomplete**로 보존하며 컴파일 성공,
  Shipping 링크/패키징으로 계산하지 않는다. 이번 인계에서 기존 전체 회귀를 반복하지 않았다.

회수 파일이 없는 기존 spool은 호환성을 위해 그대로 읽는다. 최초 기록 쓰기가 디스크에
도달하기 전에 실패/종료되면 메모리의 회수 의도가 복원된다고 보장하지 않는다. 따라서 초기
권위 있는 멤버십 조회와 감사 전송 장벽, 실제 서버 재시작/HTTP 연동 검증은 후속 범위다.
정상 Logout/queued Iris RPC drain, 미확정 픽업 복구, 실제 Steam·2~4인 포기 E2E와
게임 중 포기 UI 공개 조건도 그대로 남는다.

### 최초 멤버십 조회와 감사 전송 장벽 (2026-10-29)

이전 spool에 로컬 회수 기록이 없을 때도 전송 전에 현재 서버의 검증된 멤버십 조회를
기다리도록 `RPGSecurityTelemetryMembership.cpp`를 연결했다. 별도 GET/API나 GameMode
정책을 만들지 않고, 기존 GameMode 조회 캐시와 불변 영수증 monitor를 재사용한다.

- `Flush`/`FlushNow`는 batch 예약 전에 같은 GameInstance/World의 authority GameMode와
  현재 Web 서버 배정의 정확한 세션/서버·검증된 조회/기존 신선도 한계를 확인한다.
  최초 조회 전·조회 실패로 아직 유효 캐시가 없는 경우·오래된 캐시·배정 불일치에는
  EventId/원본 큐·spool을 유지하고 기존 타이머로 재시도한다. 초기화/정상 종료의
  복원·저장과 기존 네트워크 backoff는 유지한다.
- 조회의 멤버 누락/로컬 시각은 탈퇴 근거가 아니다. 전송 가능한 신선한 조회가 있어도
  격리는 기존 monotonic monitor의 정확한 세션/캐릭터/Steam 포기·자동 만료 영수증으로만
  판단한다. 접속한 PlayerState/Pawn이 없는 이전 이벤트에도 적용하며, 후속 빈 조회가
  이전 영수증을 지우지 않는다. 남은 멤버·같은 캐릭터/다른 계정의 이벤트와 순서를 보존한다.
- 추가할 회수 기록을 먼저 임시 합집합으로 검사한다. 계정 충돌/128명 상한에서는 부분
  적용하지 않는다. 기존의 영속 회수 기록 → dead-letter 보존 → 큐/spool 정리 순서를
  유지하며, 보존 쓰기 실패 시 전송하지 않고 원래 증거를 남긴다. 복구 후 같은 EventId를
  처리하고 이미 수락된 backend 감사/DB 트랜잭션을 롤백하지 않는다.
- 기존 신선한 캐시가 있는 동안 후속 GET 실패가 즉시 새 영수증을 만드는 것은 아니다.
  이 장벽은 조회와 backend 커밋 사이의 원자성을 보장하거나 게임플레이/입장 자격을
  부여하지 않는다. backend의 최종 권한 검사와 기존 주기 조회/회수 경계는 유지한다.

CL 개별 컴파일 3개·분리 DLL 링크 종료 0. 새 native `TelemetryMembership.InitialReadBarrier`/
`ReceiptBeforeDelivery` 2개와 직접 영향 기존 `TelemetryRecovery.StaleSpool`/`WriteFailure`,
`Telemetry.NewEventsPreserveFlushAndRetryDeadlines` 3개가 **모두 Success·UE/호스트 종료 0**이다.
원래 배정 불일치/토큰 부재·빈 조회·신선도·명시적 포기와 만료·다른 계정 보존·기록 쓰기
실패/수리와 새 subsystem의 실제 격리 파일 복원을 확인했다. 합성 HTTP 결과를 기존
GameMode monitor에 전달하는 native fixture이며 실제 GET/POST나 OS 서버 재시작은 아니다.

proof: `Saved/BuildChecks/TelemetryMembershipBarrier-20261029/Verification.proof.json`와
`Native-f68663acc6674dd184d4679018d4bdbe/Native.proof.json`.
실제로 로드한 별도 DLL·선택 소스 10개/사본 입력 452개는 전후 불변이다. 기존 기동
Condition failed Error 15줄과 검사 시작 이후/종료 Error 0을 구분한다. 새 필드/반사 API는
없으며 원본 DLL/Intermediate·GUI/MCP·에셋·기존 backend/DB/다른 담당은 변경하지 않았다.
완료된 입장/Iris·HTTP/보상/600초 검사는 반복하지 않았다. 정상 공유 모듈 반영과 실제
서버 프로세스 재시작의 초기 GET→격리→감사 POST·PostgreSQL/Steam/Shipping은 별도다.

### 거절된 감사 batch의 보존 실패 시 유실 방지 (2026-10-29)

이전 코드는 영구 HTTP 거절/인코딩 실패에서 dead-letter 쓰기 결과를 확인하지 않고
예약 이벤트를 제거했다. `bDeadLetterPending`/`FinishPendingDeadLetter`로 **보존 성공 전
원본 예약·큐/spool과 EventId를 유지**한다. 보존에 성공한 경우에만 예약 제거/제거 수를
반영하고, 뒤에 도착한 다른 이벤트는 같은 순서로 남긴다.

- 해당 프로세스의 저장 대기 단계에서는 `Flush`/`FlushNow`·타이머가 로컬 보존만
  재시도한다. 거절된 batch를 다시 POST하거나 실패를 ACK로 바꾸지 않는다. 저장만 처리한
  `FlushNow`의 반환은 HTTP 요청이 없으므로 false다. 기존 backoff를 보존하고 새 관측이
  저장 대기의 예약을 빼앗거나 재시도 시각을 앞당기지 못하게 한다.
- 그 사이 정확한 탈퇴 영수증이 도착해도 이미 거절된 혼합 batch의 예약을 일반 네트워크
  취소처럼 풀지 않는다. 영속 tombstone 우선·남은 멤버 보존과 이전 HTTP callback 격리는
  유지한다. 정상 ACK·408/429/5xx의 기존 확인/네트워크 재시도 분류도 유지했다.
- 정상 종료는 예약을 포함한 원본 이벤트를 spool에 보존한다. 새 subsystem 복원에서는
  기존의 tombstone 복원/정확한 탈퇴자 격리가 먼저 적용되고 최초 멤버십 전송 장벽도 유지한다.
  저장 대기 phase 자체를 별도 journal에 영속화한 것은 아니므로 실제 재시작 뒤의 이전
  거절 상태 분류/재전송 억제나 OS 장애 내구성을 보장하지 않는다.
- 부분 append 실패 뒤 같은 EventId의 dead-letter 행이 중복될 수 있다. 보존 후 spool
  재작성 실패는 원래 spool을 남기고 경고한다. exactly-once 파일 행/fsync·디스크 고장 복구,
  비영속 설정의 내구성을 추가 보장하거나 backend 커밋을 되돌리는 변경은 아니다.

제품 CPP/테스트 CPP 2개 개별 컴파일와 분리 링크가 성공했다. 첫 native 실행은 새 보존/
종료 복원/응답 호환 3개와 직접 회귀 3개 중 **5개 Success/1개 Fail·호스트 종료 1**이다.
실패는 복원 직후에도 3개를 기대했지만 기존 tombstone 필터가 이미 2개로 줄이는 테스트
가정이었다. 첫 기대 보완도 여러 JSON 문서인 spool을 단일 JSON body로 읽어 실패했다.
제품 코드는 추가 변경하지 않고, 종료 직전의 세 ID/정확한 문서 수 → 실제 복원 parser의
두 멤버/격리 기록으로 구분하도록 기대를 수정했다.

각 실패 모듈/소스 사본·proof를 보존한 채 테스트 CPP만 두 번 개별 컴파일/분리 재링크했고,
마지막에는 **수정 대상 `PendingPreservationShutdown` 하나만 Success·UE/호스트 종료 0**으로
재확인했다. 앞선 나머지 5개를 반복하지 않았다. 이전/최종 링크 응답은 테스트 객체와
출력 경로 외 동일하다. 이는 같은 제품 객체의 소스 단위 집중 근거이며 **최종 단일 DLL에서
6개 전체가 다시 통과한 결과로 표시하지 않는다**. 인코딩 실패 진입도 같은 보존 helper로
연결했지만 손상 payload의 실제 진입을 새 검사에서 별도로 실행한 것은 아니다.

통합 기록: `Saved/BuildChecks/TelemetryDeadLetterPreservation-20261029/Verification.proof.json`.
최종 단일 사례: `Native-51cb687411544d71b0c5a084039e49c1/Native.proof.json`.
선택 소스 11개·최종 고정 입력 453개/실제 로드 분리 DLL 불변, 기동 Error 15줄/마지막
검사 이후 Error 0을 구분한다. 원본 DLL·Intermediate·GUI/MCP/에셋·기존 backend/DB와 다른
담당을 유지하고 본인 예약을 해제했다. 실제 HTTP/OS 서버 재시작·PG/Iris·Steam/Shipping과
정상 공유 DLL 반영은 별도이며, 완료된 입장/보상/600초 및 정상 5개 회귀는 반복하지 않았다.

## 연결 종료와 정상 Logout의 네이티브 경계 (2026-10-11, 자동 조회 연결 전)

`ARPGGameModeBase::RequestDungeonMemberEjection`을 서버 네이티브 경계로 추가했다.
아직 `PollDungeonMembership`에는 호출을 연결하지 않았고 새 RPC/BP 명령·게임 중 포기 UI도
추가하지 않았다. 불변 영수증의 자동 소비와 실제 네트워크 퇴장 완료는 별도 검증 단계다.

- 현재 authority GameMode/world, Controller/PlayerState/Pawn 소유 관계와 인증된
  세션/캐릭터/Steam의 정확한 회수 캐시 일치를 확인한다. 멤버 누락·HTTP 실패만으로
  회수하지 않는다. 회수 알림 뒤 소유 관계를 다시 검사해 교체된 연결/Pawn을 닫지 않는다.
- 현재 world의 서버 NetDriver에 등록된, 정확한 Controller가 소유하는 열린 연결만 닫는다.
  로컬 플레이어·클라이언트 측 driver·다른 연결·child/replay 연결과 자식 플레이어가 있는
  primary 연결은 거절한다. 같은 연결의 다른 참가자를 함께 퇴장시키는 fallback은 없다.
- 먼저 기존 신원/경제·GAS/이동 회수 장벽을 적용한 다음 `UNetConnection::Close`를 요청한다.
  `KickPlayer`, Pawn/Controller 즉시 파괴, 수동 `Logout`이나 `UnPossess`는 호출하지 않는다.
  연결 정리가 actor 제거와 정상 Logout을 수행하도록 남겨 둔다. DB 보상/아이템은 삭제하거나
  롤백하지 않는다. 회수 알림 중 소유 관계가 바뀌어 종료 요청을 중단해도 권한은 복원하지 않는다.
- UE 5.8의 `ACharacter::ServerMovePacked_Implementation`은 non-virtual이다.
  `ARPGPlayer::ProcessEvent`는 회수된 authority 플레이어의 `ServerMovePacked` 서버 RPC만
  이동 컴포넌트의 packed move 해석 전에 거절한다. 다른 이벤트와 비회수 플레이어의
  dispatch는 그대로 Super로 전달한다. Iris의 RPC 실행도 이 dispatch를 사용하지만,
  이 코드 확인/네이티브 검사는 실제 Iris 큐 drain 또는 외부 RPC 매개변수 해석 검증이 아니다.

### 선택 실행 결과와 남은 검증

- `Run-DungeonClientFlowAutomationTest.ps1 -EjectionOnly`는 신규 `DungeonEjection.Lifecycle`,
  `Guards`, `Reentry` 3종과 기존 `DungeonMembershipMonitor`,
  `DungeonAuthorityRevocation.Gameplay` 2종만 선택한다. 두 focused 옵션 동시 지정은 거절한다.
- 실제 engine Close/CleanUp/Controller destruction/상속된 boss Logout을 격리 world에서 실행해
  종료 직후 Pawn 유지, 정리 단계의 Logout 1회, 남은 참가자/재도전 기록과 연결 보존을 검사했다.
  신원·계정·scope·연결 topology 거절 및 동기 알림의 연결 교체도 포함한다. 실제 socket,
  HTTP 영수증, Iris replication은 구성하지 않았다. 비회수 raw 이동 RPC가 정상 decode에
  도달하는 것을 보이는 PackageMap 부재 진단 1회만 fixture에서 예상한다.
- 최초 직접 override 컴파일 오류 이후 **소유한** 실패 빌드만 중단했다. 수정 후 Development
  게임 모듈 링크 39개 작업과 최종 경계 보강 4개 작업은 성공했다. 첫 네이티브 실행은
  fixture의 snapshot state/GUID 및 연결 초기화 누락으로 실패/종료 777003이었다.
  해당 로그와 `DungeonEjection.Failed.proof.json`을 보존한다.
- fixture 초기화 보완에서 설치 엔진의 non-exported `InitChannelData` 호출은 링크 실패했다.
  필요한 빈 control-slot bookkeeping과 driver-owned connection handle만 준비하는
  fixture로 수정했고 `DungeonEjection-ModuleLink-20261011-FixtureFix-Retry.log`의
  **3개 작업(compile/lib/DLL) Succeeded/종료 0**을 확인했다. 운영 가드를 완화하지 않았다.
- `Saved/Tests/DungeonFlow/Automation-be761d0fff3840f88edb9e3d25c30799/automation.log`:
  **선택 5종 모두 Success/runner 종료 0**. 같은 디렉터리 `DungeonFlow.proof.json`에
  정확한 검사 목록, 실행 전후 같은 DLL/소스·runner 66개, 로그/빌드 로그 해시와 실패 이력을
  기록했다. 검증 DLL SHA256:
  `2BB3C23E65B09C820B44BAFCB728A5F5B6344EEDFC6B6C04C06E4EECE2C66558`.

네이티브 fixture는 다음 driver tick의 정리 경계 대신 `CleanUp`을 직접 호출한다.
실제 2~4인 Iris 이동/RPC의 지연·패킷 손실·응답 유실, 클라이언트 접속 종료/로비 복귀,
주기 조회 사이의 이미 완료된 입장, 남은 파티의 자연 전투/보상, 서버 재시작/HTTP/Steam 및
Shipping 링크·패키징은 이번 성공에 포함하지 않는다. 이 네트워크 회귀 뒤에 서버 조회의 자동
회수를 연결한다. 기존 전체 회귀와 직전 감사 파일 복원 suite는 반복하지 않았다.

## 실제 Iris 퇴장·격리 러너 준비 (2026-10-11, 실행 미완료)

기존 `Run-IrisMultiClientTest.ps1`에 별도 개발 검증 모드를 추가했다.
새 네이티브 GameMode/Controller/Player fixture는 명시적인
`-RPGDungeonEjectionNetE2E -RPGNetTestMode -nosteam` 조합에서만 활성화된다.
서버는 Dedicated net mode와 실제 Iris/packet simulation을 확인한다. Shipping에서는
신원/영수증 주입과 테스트 RPC 처리가 활성화되지 않는다. 운영 주기 조회, 게임 중 포기 UI,
Steam 인증 및 백엔드 정책은 바꾸지 않았다.

- `-VerifyDungeonEjection`: 이동 중 정확한 합성 영수증으로 운영
  `RequestDungeonMemberEjection`을 호출한다. 같은 프레임의 권한 회수/연결 종료/액터 보존,
  **수동 CleanUp 없이** 다음 엔진 정리의 Pawn/Controller 제거·Logout 1회,
  참가자/retry 상태에서 한 명만 제거, 실제 클라이언트 network failure와 남은 멤버의
  연결·신원·추가 이동/RPC 및 클라이언트 연결 수 스냅샷을 검증하도록 구성했다.
- `-VerifyDungeonQuarantine`: 다른 프로세스 시나리오에서 연결을 계속 열어 두고 신원 권한만
  회수한다. 회수 뒤 실제 Iris `ServerMovePacked`가 20개 이상 도착하며 운영 `ARPGPlayer`
  dispatch를 통과해도 서버 위치/이동 모드가 고정되는지와 다른 멤버의 이동 유지를 검증한다.
  이 시나리오는 **닫힌 연결의 큐 drain 관측 또는 정상 퇴장 검증을 대신하지 않는다**.
- 네이티브 이동 입력 fixture이며 authored Enhanced Input/BP 전투/자연 콘텐츠 검증은 아니다.
  신원과 영수증은 합성이므로 실제 HTTP/DB 조회·입장/재입장·서버 재시작·Steam·Shipping 및
  패키징은 별도다. 두 모드 모두 2~4인만 허용하며 다른 보안/스킬/NPC probe와 함께 실행하지 않는다.

증거 파서는 중복/누락/불일치 신원, 잘못된 퇴장 대상, 다른 파티원 접속 종료,
packet simulation 불일치, 이동 정지, stale 클라이언트 스냅샷, 실제 정리/RPC 증거 부재 및
runtime assertion/ensure를 실패로 처리한다. `Test-DungeonEjectionNetworkProof.ps1`의
**신규 검사 14개는 성공**했다. 실제 실행 시 선택 소스·맵/DLL의 전후 해시를 비교하고,
이 러너가 띄운 프로세스만 정리한 뒤 최종 로그 해시를 저장한다.

현재 검증 상태는 **Incomplete**다. 최초 모듈 빌드는 fixture의 전방 선언 누락/멤버와
같은 지역 변수명을 수정했다. 두 번째 빌드에서는 새 probe C++ 컴파일이 성공했지만,
별도 공유 작업의 `RPGNPCPickupNetworkProbe.cpp:271`에서 unbraced `if`/`UE_LOG` 뒤
`else`가 C2181로 실패해 lib/DLL 링크까지 도달하지 못했다. 이 NPC 파일은 수정하지 않았고,
오래된 DLL로 새 fixture를 실행하거나 기존 전체 네이티브 suite를 반복하지 않았다.

로그/해시/범위 기록:
`Saved/Tests/DungeonEjectionNetwork/Prepared-20261011-a224d24e/Preparation.proof.json`.
빌드 로그는 `Saved/Logs/DungeonEjectionNetwork-Module-20261011.log`와
`Saved/Logs/DungeonEjectionNetwork-Module-Retry-20261011.log`에 보존했다.

공유 NPC 빌드 오류 정리 후 모듈 링크를 완료하고 아래 **서로 다른 두 시나리오**를 실행한다.
아래 명령은 준비한 실행 계획이며 아직 성공한 실행 기록이 아니다.

```powershell
.\Scripts\Network\Run-IrisMultiClientTest.ps1 -SkipBuild -VerifyDungeonEjection -ClientCount 4 -Port 17991 -TimeoutSeconds 150 -PacketLagMinMs 80 -PacketLagMaxMs 160 -PacketLossPercent 5
.\Scripts\Network\Run-IrisMultiClientTest.ps1 -SkipBuild -VerifyDungeonQuarantine -ClientCount 2 -Port 17992 -TimeoutSeconds 150 -PacketLagMinMs 80 -PacketLagMaxMs 160 -PacketLossPercent 5
```

실제 네트워크 proof 확보 전 자동 membership 퇴장 및 gameplay 포기 UI는 연결하지 않는다.

## 실제 Iris 퇴장·열린 연결 격리 검증 (2026-10-11, 기록 정리 2026-10-12)

위 준비 단계의 빌드 차단은 해소됐다. 공유 NPC 작업에서 해당 `if/else` 중괄호가 이미
수정되고 DLL이 갱신된 것을 확인했으며, 이 작업에서는 NPC 코드를 중복 수정하지 않았다.
자체 `DungeonEjectionNetwork-Module-Resume-20261011.log`는 0개 작업/up-to-date 성공이다.
첫 네트워크 실행에서는 같은 시작 위치의 Pawn 충돌로 4명 중 한 명이 생성되지 않아
퇴장 실행 전 timeout이 발생했다. **개발용 fixture의 시작 위치만 분리**한 뒤 자체
`DungeonEjectionNetwork-SpawnFix-20261011.log`의 compile/lib/DLL 3개 작업이 성공했다.
운영 맵·NPC·입장 정책은 변경하지 않았다.

같은 DLL에서 다음 두 실제 socket/Iris 시나리오가 각각 runner 종료 0으로 통과했다.
각 방향의 outgoing packet simulation은 **80~160ms 지연/5% 손실**이며, 서버와 모든
클라이언트가 런타임 설정을 확인했다.

- **4인 퇴장:** 이동 중 합성 불변 영수증으로 운영 `RequestDungeonMemberEjection` 호출.
  권한 회수/Close 직후 Pawn·Controller는 유지하고 Logout은 0회였다. 이후 수동
  `CleanUp` 없이 엔진 정리가 액터를 제거하고 정상 Logout을 1회 실행했다. 참가자와
  retry 상태는 각각 3명으로 줄었고, 퇴장 대상 클라이언트는 실제 network failure를 받았다.
  남은 3명은 같은 연결과 신원을 유지하며 추가 이동/RPC를 처리하고 연결 수 3을 수신했다.
- **2인 열린 연결 격리:** 신원 권한 회수 뒤 연결을 열어 둔 상태에서 실제 packed 이동 RPC
  **285개**가 운영 Player dispatch에 도착했다. 정상 처리 카운터는 증가하지 않았고
  서버 위치와 `MOVE_None`이 유지됐다. 다른 멤버는 같은 신원/연결로 계속 이동했으며,
  두 클라이언트의 연결 수 2 스냅샷이 유지됐다. 이 결과는 닫힌 연결의 큐 drain 관측이 아니다.

검증 DLL SHA256:
`53811DC06785ED8B6774B7041A18B916091108FA3D953A96905F66419CE50C07`.
각 proof에 선택 소스·맵 26개와 실행 전후 같은 DLL을 기록했다.

- 4인: `Saved/Logs/IrisMultiClient/20261011-195854-213b5161/DungeonEjectionNetwork.proof.json`.
  첫 러너는 열린 로그의 사전 해싱 오류를 출력했지만 종료 후 finalization에서 채운
  **최종 로그 5개의 해시는 별도 읽기 검증에서 모두 일치**했다. 그 후 사전 해싱을
  제거하고 `logsFinalized` 표시를 추가했다. 이 실행의 소스 증거는 수정 전 러너에
  해당하며, 새 러너의 4인 재실행 성공으로 표시하지 않는다.
- 2인: `Saved/Logs/IrisMultiClient/20261011-200004-a511b14c/DungeonEjectionNetwork.proof.json`.
  수정한 러너의 `logsFinalized=true`, 최종 로그 3개 해시 일치를 확인했다.
- 최초 실패:
  `Saved/Logs/IrisMultiClient/20261011-195531-7ad6ce98/DungeonEjectionNetwork.Failed.proof.json`.
  기존 준비 단계의 Incomplete proof/컴파일 실패 로그도 역사적 기록으로 보존한다.
- 묶음 증거:
  `Saved/Tests/DungeonEjectionNetwork/Verified-20261012/Verification.proof.json`.

재개 시 실행을 반복하지 않고 기존 final log/proof 해시와 현재 DLL을 확인했다.
4인 proof의 현재 소스 차이는 위 로그 finalization 수정의 러너 1개뿐이며,
2인 proof의 선택 소스·맵 26개와 현재 DLL은 일치한다. 다른 작업의 에디터는 종료하지 않았다.
기존 네이티브 전체 suite나 앞서 성공한 증거 파서 14개도 다시 실행하지 않았다.

이는 **네이티브 이동 입력·합성 신원/영수증**의 실제 로컬 네트워크 검증이다.
HTTP/DB에서 온 영수증과 주기 조회의 자동 퇴장, 이미 닫힌 연결의 queued drain 관측,
입장/응답 유실/재입장 경합, 자연 파티 전투·Threat·보상 완료, 서버 프로세스 재시작,
Steam/멀티 PC 및 Shipping 패키징은 별도다. 자동 membership 퇴장과 게임 중 포기 UI는
여전히 연결하지 않았다. 다음은 실제 멤버십 조회와 권한 회수/퇴장 연결의 통합 검증이다.

## 멤버십 조회 완료와 자동 회수 연결 (2026-10-12, 네이티브 검증 완료)

`PollDungeonMembership`의 실제 서버 HTTP 완료 콜백을 새 private C++ 처리 경계
`CompleteDungeonMembershipRead`에 연결했다. 새 player RPC/BP 명령이나 UI는 추가하지 않았다.

액터/연결을 처리하는 완료 delegate의 실행 정책을 명시적으로 game thread에 고정했다.
새 두 네이티브 처리 함수도 game thread 밖에서는 동작하지 않는다.

- 현재 authority/world/auth GameMode 관계 및 read serial을 확인하고 기존 Monitor가
  정확한 세션/배정 서버·200 응답·불변 영수증을 원자적으로 수락한 경우에만 새 증거를 처리한다.
  실패/잘못된 응답/멤버 목록 누락은 새 회수 권한이 아니다.
- `ApplyCachedDungeonAbandonments`는 현재 Controller의 약한 참조 목록을 먼저 만들고,
  매 대상 처리 전에 world/GameMode 및 세션/서버 scope가 그대로인지 재검사한다.
  알림에서 Controller가 제거되거나 scope가 종료되면 더 이상 같은 순회를 진행하지 않는다.
  실제 신원/액터/연결 검증·권한 회수·Close는 기존 `RequestDungeonMemberEjection`에 위임한다.
- 주기 시작에도 이전에 확인한 불변 영수증을 처리한다. 조회 뒤 늦게 인증된 연결을 다음
  주기에 처리할 수 있고 이후 조회 실패가 이미 확인한 영수증을 지우지 않는다.
  새로운 요청 전에도 알림으로 scope가 바뀌었는지 확인한다. 종료된 연결/회수된 신원을
  중복 처리하거나 동기 알림에서 교체된 연결을 강제로 닫는 fallback은 추가하지 않았다.
- 기존 신원 회수의 경제/GAS/이동/감사 격리 순서와 엔진 정리/정상 Logout을 유지한다.
  남은 파티를 전체 제거하거나 진행 중 DB 트랜잭션을 롤백하지 않는다.

`Run-DungeonClientFlowAutomationTest.ps1 -MembershipEjectionOnly`를 추가했다.
다른 focused 옵션과 동시 지정은 거절하며 다음 5개만 선택한다.

- 신규 `DungeonEjection.MembershipCompletion`: 실패/비200/외부 세션·서버/누락·외부 계정,
  잘못된 영수증의 원자적 거절, 정확한 완료의 자동 Close, 정리 전 액터 유지와 정상 Logout.
- 신규 `DungeonEjection.MembershipCachedSweep`: 다른 던전·오래된 read serial·비권위 거절,
  실패 뒤 증거 보존, 늦은 인증 연결의 캐시 처리와 중복 처리 차단.
- 신규 `DungeonEjection.MembershipScopeReentry`: 첫 회수 알림에서 scope를 종료했을 때
  이후 후보를 처리하지 않고 남은 멤버의 신원/연결을 유지하는 경계.
- 직접 영향 기존 `DungeonMembershipMonitor`, `DungeonMembershipTransport`.

위 검사는 격리 world/네이티브 연결 및 주입된 HTTP 응답을 사용한다. 실제 HTTP 송신/서버
주기 타이머·입장 handshake/Iris 자동 퇴장 통합, DB 영수증, 자연 파티 전투/보상,
서버 재시작/Steam/Shipping 검증은 아니다. 기존 네트워크 성공을 이 새 자동 연결의
실행 성공으로 재사용하지 않는다. 게임 중 포기 UI는 계속 비활성 상태다.

구현 및 runner 구문/patch 검사 뒤 **새 DLL의 선택 5종 모두 성공/runner 종료 0**을 확인했다.
자체 초기 모듈 빌드는 공유 소스 변경으로 252개 작업을 예약했지만 진행 중 추가된
`RPGBossEnrageEffect.generated.h` 누락으로 컴파일 오류가 발생했다. 낭비되는 작업을 줄이기
위해 정확한 자체 UBT root/자식 **합계 3개** 프로세스만 중단했으며 다른 작업의
빌드/백엔드는 종료하지 않았다. 정상 대기 경로로 공유 빌드 종료 뒤 재시도했고,
자체 최종 **20개 작업의 Development 게임 모듈 compile/lib/DLL 링크가 성공**했다.
전체 Editor 타깃 또는 Shipping 패키징 성공으로 표시하지 않는다.
로그: `Saved/Logs/DungeonMembershipEjection-Module-20261012.log`,
`Saved/Logs/DungeonMembershipEjection-Module-Retry-20261012.log`.

검증 증거:

- `Saved/Tests/DungeonFlow/Automation-1181f12e38e648f8a3255f7c334771a8/automation.log`:
  신규 완료/캐시/scope 재진입 3개와 Monitor/Transport 2개, 정확한 5개 결과가 Success다.
- 같은 디렉터리 `DungeonFlow.proof.json`에 실행 전후 같은 DLL·소스/runner 66개와
  로그 해시를 기록했다. 전용 `MembershipEjection.BuildContext.proof.json`에 초기 중단/최종
  빌드 로그 해시, 자체/공유 빌드 구분과 미검증 범위를 기록했다.
- 검증 DLL SHA256:
  `E353CAC5A7143849DD985DDBCEAF2D2EF57471487F91D71002F69670E38DCEDD`.
  최종 전달 전 현재 DLL도 같았다. 이전 네트워크 성공은 다른 DLL의 역사적 증거다.

실제 서버 HTTP 완료 콜백과 주기 시작에 자동 소비 코드는 연결됐지만 이번 실행은
네이티브 consumer/HTTP mock 검증이다. 실제 HTTP/DB 영수증→주기 조회→Iris 자동 퇴장 및
늦은 입장/응답 유실/남은 파티 완료의 통합 검증은 다음 단계다. 게임 중 포기 UI는 공개하지
않았고, 기존 전체 suite·이전 Iris 퇴장/격리 성공 시나리오는 다시 실행하지 않았다.

## 실제 PostgreSQL·HTTP 주기 조회와 Iris 자동 퇴장 (2026-10-12)

`Scripts/Network/Run-DungeonMembershipBackendE2ETest.ps1`는 기존 티켓 입장 및
네트워크 퇴장 프로브를 재사용한다. 고유 PostgreSQL schema에서 개발 계정/캐릭터와
2~4인 세션을 API로 만들고, 실제 UE 로그인·Resume·새 티켓 발급/소비 후 입장한다.
`RPGNetTestMode`는 사용하지 않으며 신원·영수증을 주입하거나 퇴장 함수를 직접 호출하지 않는다.
설치형 엔진의 `-server` 런타임과 `-nosteam` 개발 신원 비교 예외는 사용한다.

서버 기본 5초 멤버십 조회가 실제 플레이어 포기 POST의 영수증을 수락하고, game thread
권한 회수 알림→운영 Close→엔진 액터 정리/Logout으로 이어지는지 관측한다.
나머지 접속자는 같은 신원/연결과 이동 RPC를 유지해야 한다. 패킷 조건은 양방향
80~160ms 지연/5% 손실이다. 다른 계정의 포기 403, 동일 POST의 불변 재시도,
배정 서버 GET 및 독립 SQL의 영수증/남은 멤버도 확인한다.

각 실행 전에 DLL·관련 소스/러너 해시와 attempt를 저장하며, 종료 후 닫힌 로그만 해싱한다.
시작 초기화 동안에만 배정 서버 heartbeat를 보조하고 실제 입장/이동 준비 후에는 중단한다.
자체 프로세스만 종료하며 public 데이터·기존 schema·다른 작업은 건드리지 않는다.
새 테스트 schema와 실패 기록은 감사용으로 보존한다.

증거 파서의 직접 영향 회귀는 **23개 통과**했다. 실제 통합 첫 실행
`Saved/Tests/DungeonMembershipBackend/43534fe85cb145efb18216ced8710880/Failed.proof.json`은
실제 입장/HTTP 회수/Close 뒤 입장 전용 프로브가 정상 비상 로비 복귀를
`UnexpectedLobbyTravel`로 오판해서 실패했다. 중단 처리로 남은 클라이언트가 종료된 뒤의
서버 `LivePeerChanged`는 이 실행을 성공으로 볼 근거가 아니다.
입장 전용 완료 시 전체 보상/복귀 시나리오의 travel observer도 해제하도록 수정했다.
자체 Development 모듈 최초 5개 작업 및 수정 후 3개 작업 compile/lib/DLL 링크가 성공했다.
로그: `Saved/Logs/DungeonMembershipBackend-Module-20261012.log`,
`Saved/Logs/DungeonMembershipBackend-Module-Retry-20261012.log`.

수정 후 **2인 통합 runner 종료 0/통과**:
`Saved/Tests/DungeonMembershipBackend/40b40079b104464dbc57b35c5c76b908/DungeonMembershipBackend.proof.json`.
실제 대상 PlayerId 257의 회수 알림은 연결 Open·액터 유지·Logout 0 상태에서 발생했고,
이후 대상 클라이언트 ConnectionLost 및 엔진 Pawn/Controller 제거·정상 Logout 1회를 확인했다.
남은 PlayerId 256은 동일 신원/연결에서 packed 이동 RPC **143개**, 추가 이동 **2013.4**를
처리하고 연결 수 1을 수신했다. 서버 참가/retry 상태도 각각 1명으로 유지됐다.
테스트의 선수 신원과 영수증은 합성이 아니지만 이동 입력은 네이티브 fixture다.
실제 Steam 연결의 신원 일치나 authored Enhanced Input/자연 전투 검증은 아니다.

검증 DLL SHA256: `06B8E5832C404A5529ED18D7E891D758D7796683AD6DF3A558394184B5404291`.
실행 전후/전달 전 같은 DLL 및 선택 소스/러너 **21개**, 종료한 로그 **5개** 해시 일치를
확인했다. 포기 receipt와 남은 멤버는 고유 schema의 독립 SQL에서도 일치했다.
이 증거는 해당 DLL/선택 소스의 범위다. 전체 Editor 타깃/클린 빌드·엔진/프로젝트 오류
0건의 증거가 아니며, 인접 콘텐츠의 기존 unloadable/missing asset 및 Niagara 경고는 별도다.
이전 전체 네이티브 suite/합성 Iris 성공 시나리오는 반복하지 않았다.

포기자 보상 제외·남은 파티의 실제 던전 완료/정산은 아래 2인·4인으로 연결해 검증했다.
다음은 자연 전투/Threat, 실제 서버 재시작·응답 유실·늦은 입장/재입장 경합,
Steam·다른 PC·Shipping은 별도이며 게임 중 포기 UI는 계속 공개하지 않는다.

## 남은 파티의 보스 완료·보상·복귀 연결 (2026-10-13)

기존 `Run-DungeonMembershipBackendE2ETest.ps1`에
`-VerifyRemainingPartySettlement`를 추가했다. 고유 schema/실제 개발 인증·티켓/포기 POST와
5초 주기 회수 경로는 유지하며, 저장된 `bossmap`과 기존 `DungeonFlowE2E`를 재사용한다.
프로브는 포기 전 신원/액터/연결/보스 encounter를 관측하고, 실제 권한 회수 알림·액터 정리
뒤 남은 멤버의 동일 신원/연결/진행 중 encounter가 유지되는지 확인한다.
그 후 남은 정상 플레이어 ASC의 기존 서버 GAS 치명타가 Dead→Victory→운영 정산을
발생시킨다. 테스트에서 승리/정산/보상 snapshot을 직접 주입하지 않는다.
자연 협동 전투나 포기 UI 동작 검증은 아니다.

남은 클라이언트의 owner-only 지갑/인벤토리 안내, 실제 결과 버튼 클릭과 로비 맵 로드를
기존 프로브로 확인한다. 포기자는 이 전체 복귀 시나리오 observer를 입장 후 해제하고
별도 GameInstance observer로 실제 network failure를 관측한다. 러너는 포기자 보상 0과
정산 후 동일 영수증 재시도/보상 snapshot 403, 남은 멤버 골드 500·물약 3·투구 1의
인벤토리 지급을 API/독립 SQL로 검사하며 불변 정산 수령인 목록도 대조한다.
새 증거 파서의 2인/4인 로그 형식·오류/신원/순서/잘못된 보상 및 전송 조건 **19종 통과**는
파서 검증이지 실제 4인 실행 결과가 아니다.

자체 `DungeonAbandonmentSettlement-Module-20261012.log`는 중단 시 완료 결과가 없어
Incomplete로 보존한다. 재개 후 `DungeonAbandonmentSettlement-Module-20261013.log`는
공유 빌드 완료 뒤 **0 action/up-to-date/Succeeded** 확인이다. 자체 신규 링크 완료로
표시하지 않는다. 실제 통합 첫 실행
`Saved/Tests/DungeonMembershipBackend/3f5fd42d0e9945ecb3d7c971b94a84fc/Failed.proof.json`은
서버 BeginPlay의 기존 BP UI 생성 실패/`Accessed None`을 엄격한 판정으로 발견해 실패했다.
이전 더 약한 러너의 통과로 해당 경고를 정상 처리하거나 이번 성공으로 표시하지 않는다.

`GuardBossBattleStartupUI.py`는 원본 백업 후 전용 서버 UI 분기 및 생성 반환값 유효성
가드만 추가하도록 준비했다. 기존 노드 간 게임플레이 연결과 pin 기본값의 보존을
검사하고 컴파일 오류가 있으면 저장하지 않는다. 원본은
`Saved/Backups/BossStartupUI-bc9b3caeaa494693a1ca59bac1ebe648/BP_BossBattleGameMode.uasset`.
초기 audit의 pin API 오류는 저장 없이 실패했으며 수정 후 읽기 audit를 완료했다.
`BP_BossBattleGameMode` 가드를 컴파일·저장했다. 두 번째 실제 실행
`Saved/Tests/DungeonMembershipBackend/16ea581b2c9845b580dfa5e427551520/Failed.proof.json`은
전용 서버에서 공유 `GA_NPC_DrawBossHpBar_Base`의 위젯 생성 실패 후 `BossNameText` 접근이
`Accessed None`을 발생시켜 실패했다. 이 능력 BP도 원본을
`Saved/Backups/BossHealthBarUI-c8114e3b0e4a4177873b4eaf62e40321/GA_NPC_DrawBossHpBar_Base.uasset`에
백업한 뒤 `GuardBossHealthBarUI.py`로 전용 서버/위젯 생성 실패 분기를 기존 `End Ability`에
연결했다. 정상 클라이언트 UI 경로와 원래 그래프의 모든 연결·pin 기본값을 보존한 상태로
컴파일·저장했으며, 별도 프로세스의 읽기 검증도 통과했다
(`Saved/Logs/BossHealthBarUI-Apply-20261013.log`, `BossHealthBarUI-Verify-20261013.log`).

세 번째 실행 `c94c8fde7d5d4f858870104f684e3711`에서는 실제 입장·회수·승리까지
진행됐으나 새 테스트 schema에 운영 기본 통화 `RosterGold`를 시드하지 않아 백엔드 정산이
`DefinitionNotFound`로 영구 실패했다. 러너가 기존 `Backend/seed-game-economy.ps1`을
신규 schema에 적용하고 영구 정산 실패를 즉시 감지하게 수정했다. 네 번째 실행
`4be7b90e5a94441283ef525657266520`은 정산·결과 버튼·로비 복귀와 독립 SQL에서
포기자 0/남은 멤버 골드 500·아이템 2종(수량 4)까지 확인했으나 러너가 아이템
`revision`을 잘못된 `state.revision`에서 읽어 실패했다. 응답 계약대로 최상위 `revision`을
확인하도록 수정했다. 두 실패를 통과로 계산하지 않으며 스키마와 로그는 보존한다.

첫 **2인 통합 실행 종료 0/통과**는
`Saved/Tests/DungeonMembershipBackend/fab1b68e29544bc3bb7dfc1f731e913c/DungeonAbandonmentSettlement.proof.json`이다.
이후 공유 UE DLL이 다른 작업의 빌드로 교체되어, 현재 DLL에서 다시 실행한 **최종 2인 통합 종료 0/통과**는
`Saved/Tests/DungeonMembershipBackend/d498ff1711a24ed79f497dada0c78708/DungeonAbandonmentSettlement.proof.json`이다.
실제 개발 인증·티켓 입장, Iris 80~160ms 지연/5% 손실, 본인 포기 POST/불변 영수증,
5초 주기 멤버십 회수와 포기자 연결 종료, 동일 연결·액터·encounter의 남은 1인,
서버 GAS 치명타→Dead→Victory, 백엔드 원자 정산, 실제 결과 버튼/로비 복귀가 이어졌다.
API와 독립 PostgreSQL 조회 모두 포기자 보상 0/정산 수령인 제외, 남은 멤버 골드 500과
인벤토리 물약 3·투구 1을 확인했다. 포기자 snapshot은 403이고 정산 뒤 재전송한 포기
영수증은 동일했다. 닫힌 로그 5개와 소스·러너·자산 36개의 SHA256이 현재 파일과 일치한다.
최종 검증 DLL SHA256은 `C65A8DE9937EF25EA7F8DAC2A828A07D2D420E43756F7DAC3C711BEB3482CCF1`이며
실행 직후 현재 DLL과 일치했다. 첫 통과 DLL은
`B558962396A33EE71037C9593C14BFCBED35321FD14479EB26F5C09824B07701`로 구분한다.
이 실행은 2인 개발용 NullRHI 및 게이트 후 서버 GAS 피해 fixture이며 실제 Steam,
자연 협동 전투, 4인 런타임, 게임 중 포기 UI, Shipping/패키지 서버 검증은 아니다.

## 4인 포기·남은 파티 완료 통합 검증 (2026-10-13)

기존 러너의 `-VerifyRemainingPartySettlement -ClientCount 4`로 새 고유 PostgreSQL schema와
실제 개발 인증·티켓을 사용한 **4인 종료 0/통과** 증거는
`Saved/Tests/DungeonMembershipBackend/6bd137340c3546ab8107e866e26ed093/DungeonAbandonmentSettlement.proof.json`이다.
Iris 80~160ms 지연/5% 손실에서 4인 모두 실제 입장했고 1명 포기 후 5초 주기 회수로
그 연결과 액터가 제거됐다. 남은 3명은 같은 연결·액터·진행 중 보스 encounter를 유지했고,
정상 멤버의 서버 GAS 피해로 Dead→Victory가 발생했다. 세 클라이언트 모두 실제 결과 버튼과
로비 맵 복귀를 완료했다. 포기자에겐 결과 지급·로비 완료 마커가 없다.

백엔드 API와 독립 PostgreSQL 조회에서 정산 상태 `Completed`, 수령인 정확히 3명,
포기자 골드/아이템 0, 나머지 각 골드 500·인벤토리 물약 3/투구 1을 확인했다.
포기 영수증 재전송은 정산 뒤에도 불변이며 포기자 보상 snapshot은 403이다.
닫힌 로그 7개와 소스·러너·자산 36개의 SHA256이 일치했고, 검증 DLL
`C65A8DE9937EF25EA7F8DAC2A828A07D2D420E43756F7DAC3C711BEB3482CCF1`은
실행 직후 현재 DLL과 일치했다. 이는 개발용 NullRHI, 테스트 게이트 후 서버 GAS 치명타이며
자연 협동 전투/Threat·전멸, 응답 유실·재입장·서버 재시작, Steam·다른 PC·Shipping 및
게임 중 포기 UI의 검증은 아니다.

## 포기 POST 응답 유실·재입장 차단 통합 검증 (2026-10-13)

`Run-DungeonMembershipBackendE2ETest.ps1 -VerifyRemainingPartySettlement
-VerifyAbandonmentResponseLoss -ClientCount 2`를 추가했다. 로컬 전용
`DungeonAbandonmentHttpFaultProxy.mjs`는 정확한 플레이어 인증·세션·캐릭터의 bodyless POST
한 건만 백엔드에 전달한다. 백엔드의 200 영수증 본문을 끝까지 받은 다음 클라이언트 연결을
끊어 응답을 유실시키며, 영수증 내용과 인증값은 로그에 남기지 않는다. 프록시의 정상 유실 및
잘못된 상류 영수증 거절 테스트 2건이 통과했다.

실제 **2인 PostgreSQL/HTTP→Iris→보상·복귀 실행 종료 0/통과**:
`Saved/Tests/DungeonMembershipBackend/ab75946e360a4250b21cb57cc4bf05cf/DungeonAbandonmentResponseLoss.proof.json`.
재전송 전 독립 SQL에서 포기 영수증 1건·포기자 멤버십 0·남은 입장 티켓 0을 확인했다.
미리 발급한 미사용 티켓 소비는 401, 새 티켓 발급과 같은 파티 재참가는 각각 409로
거절됐다. 직접 재전송한 POST의 200 본문 SHA256은 유실된 원본과 같았고 정산 후 재전송도
같은 영수증을 반환했다. 그 사이 서버의 5초 멤버십 조회는 포기자만 퇴장시켰으며,
남은 1인의 동일 연결·전투 승리·골드 500/인벤토리 수량 4·결과 버튼/로비 복귀와
포기자 보상 0이 확인됐다. 닫힌 로그 7개와 소스·러너·자산 38개 해시가 일치했다.
검증 당시 UE DLL SHA256은 `C65A8DE9937EF25EA7F8DAC2A828A07D2D420E43756F7DAC3C711BEB3482CCF1`이다.
실행 후 공유 DLL이 다른 빌드로 교체되어 이 proof를 현재 DLL의 통과로 확대하지 않는다.

첫 시도 `f6095fa36e9f4deb884b41256567fd76`은 빈 프록시 시작 로그 파싱에서 중단돼
포기 POST를 보내지 않았다. 다음 시도 `4cd696c9d5a14dc18b350a712a3a19ba`는 공유
백엔드 파일의 동시 편집 중 구문 오류로 빌드 단계에서 중단됐다. 서로 겹친 한 글자 수정을
되돌린 뒤 별도 백엔드 빌드 경고/오류 0을 확인했고, 두 시도를 성공으로 계산하지 않는다.
이 검증의 요청 재전송 주체는 테스트 러너다. 실제 UE 로비의 미확정 요청 UX/재로그인,
포기 커밋과 동시에 진행 중인 티켓 소비, 퇴장 뒤 새 UE 연결 시도, 서버/백엔드 재시작,
자연 전투·Steam·Shipping은 아직 별도다.

## 실제 UE 로비의 응답 유실·수동 재시도 (2026-10-14)

`Run-DungeonMembershipBackendE2ETest.ps1 -VerifyRemainingPartySettlement
-VerifyAbandonmentResponseLoss -VerifyAbandonmentClientRetry -ClientCount 2`는 실제 로비의
개발용 로그인/소유 캐릭터 선택/세션 복원과 확인 버튼을 누른다. 로비 프로세스는 같은
테스트 계정의 던전 접속 프로세스와 별도로 띄운다. 최초 POST는 UE가 보내며 로컬
프록시가 백엔드 200 영수증을 받은 뒤 응답만 끊는다. 처음에는 UE HTTP 계층이 같은
POST를 자동 재전송하여 곧바로 성공 처리하는 것이 관측됐다. 미확정 UX를 별도로
검증하기 위해 새 opt-in 프록시 모드는 그 자동 재전송을 백엔드에 전달하지 않고 503으로
종료한다. 독립 DB 조회 및 재입장 거절 검사 뒤에만 수동 재시도를 통과시킨다.

최종 2인 실행은 `Saved/Tests/DungeonMembershipBackend/56e7405f031549dc87c1e1fab238aa3f/DungeonAbandonmentClientRetry.proof.json`
에서 종료 0이다. 실제 버튼 첫 클릭 후 UE는 미확정 신원·세션/선택을 유지했고 새 던전
생성·입장을 잠갔다. 같은 확인 버튼의 두 번째 클릭으로 새 UE POST가 나갔으며,
프록시는 유실된 원본과 SHA256이 같은 영수증을 한 번 전달했다. UE는 포기를 확정하고
선택 캐릭터를 보존하며 새 던전 버튼을 다시 열었다. 재시도 전 PostgreSQL은 영수증 1,
해당 멤버·티켓 0이었고 기존 티켓 소비 401·새 티켓/재참가 409였다. 서버 주기 조회의
실제 퇴장 후 남은 1인은 같은 encounter에서 서버 GAS 승리→DB 보상(골드 500,
인벤토리 수량 4)→실제 로비 복귀를 마쳤으며 포기자 보상은 0이다. 프록시 단위 3건,
닫힌 로그 8개/선택 소스·러너·자산 41개 해시가 일치했다. 검증 DLL SHA256은
`DD2742F7CDDCE6DE63A5EF307190DBF7A8389DDCC7874D1DD45AB696A60A5096`이고
검증 직후 현재 DLL과 일치했다. 별도 클라이언트 UI 테스트의 `ApplicationCore` 링크
의존성 한 줄을 보완해 게임 모듈 빌드를 성공시켰다.

앞선 두 실제 시도 `4295b0d4c4a6467c99acf907ebb41be3`,
`f69e56ddf5e04700b2066250bc257712`는 자동 HTTP 재전송이 영수증을 받는 경계를
발견한 실패 기록으로 보존했다. 이번 통과는 **실제 UE 로비의 수동 재시도** 증거지만,
한 클라이언트의 던전 연결 종료→같은 프로세스 로비 복귀, 미확정 상태에서 인증 만료→
실제 재로그인→재시도, 서버/클라이언트 프로세스 재시작, Steam/Shipping 및 게임 중
포기 UI 공개의 증거는 아니다. 운영 UI는 계속 비활성이다.

## 미확정 포기 후 실제 재로그인 통합 검증 (2026-10-14)

기존 실제 로비 프로브에 커밋 후 응답 유실→미확정 상태에서 인증 세션 만료→UE의
재시도 401→고정된 같은 계정의 개발 로그인 버튼→소유 캐릭터 목록 재조회→같은
포기 요청 재시도 단계를 추가했다. 러너는 **이번 실행의 격리 PostgreSQL schema에
생성된 계정 세션만** 만료시키고, 실제 401과 갱신된 200 영수증을 구분한다.
프록시에는 401을 그대로 전달하되 불변 영수증 일치 검사를 최종 200에 유지하는
경로를 추가했다. 프록시 단위 4건·PowerShell 구문 검사와 변경 UE 테스트 파일의
`-SingleFile -NoLink` 컴파일이 먼저 성공했다. 에디터 종료 후 첫 공유 DLL 링크는
별도 아이템 UI 테스트 서버 `UnrealEditor-Cmd.exe`(당시 PID 8568)가 동일 DLL을
로드하고 있어 `Build.log`의 LNK1104로 중단됐다. 사용자의 종료 요청 뒤 정확히
그 서버만 종료했으며 다른 테스트 프로세스·파일은 유지했다. 재시도한 Development
게임 모듈 링크는 성공했다.

`Scripts/Network/Run-DungeonMembershipBackendE2ETest.ps1 -ClientCount 2
-VerifyRemainingPartySettlement -VerifyAbandonmentResponseLoss
-VerifyAbandonmentClientRetry -VerifyAbandonmentReauthentication -NoBackendRestore
-TimeoutSeconds 360`이 종료 0으로 통과했다. 결과는
`Saved/Tests/DungeonMembershipBackend/2e30b63b48f1404f8f144fe96e08838b/DungeonAbandonmentReauthentication.proof.json`.
격리 schema에서 해당 계정의 인증 세션 3개를 만료한 뒤 이전 UE 토큰의 실제 백엔드
401 한 번을 전달했다. 로비는 미확정 신원·캐릭터/세션 선택과 새 던전 잠금을 유지했고,
같은 개발 계정의 실제 로그인 버튼/소유 캐릭터 목록 재조회 후 갱신된 UE POST를
수락했다. 원래 유실된 200과 재시도 영수증의 SHA256이 같고 영수증은 DB에 1개뿐이다.
유실 직후 멤버/티켓 0, 기존 티켓 소비 401·새 티켓/재참가 409를 재확인했다.
남은 1인은 동일 encounter에서 서버 GAS 승리→골드 500/인벤토리 수량 4 정산→
실제 로비 복귀까지 통과했다. 포기자 지급은 0이다. 검증 DLL 해시는 proof와
일치하며 선택 소스/러너 41개와 닫힌 로그 8개도 재해시 결과 불일치가 없다.

UE HTTP 계층의 자동 재전송은 테스트 프록시가 보류해 수동 재시도 경계를 분리했다.
로비는 던전 접속자와 별도 프로세스이고 개발 인증/서버 GAS 처치 fixture다.
실제 Steam 인증, 같은 프로세스의 던전→로비 복귀, 자연 협동 전투, Shipping은
이 실행의 증거가 아니다. 운영 게임 중 포기 UI도 계속 비활성이다.

## 포기된 접속자의 동일 프로세스 로비 복귀 검증 (2026-10-14)

기존 2인 정산 E2E의 포기 대상 클라이언트에서, 입장 관측 직후 끝내던 프로브를
선택 모드 `-VerifyAbandonedClientLobbyReturn`에서는 유지한다. 실제 서버 조회가
연결을 닫고 클라이언트의 운영 네트워크 실패 처리에서 로비 이동이 시작된 다음,
**같은 GameInstance/프로세스**의 실제 `LobbyMap` 로드와 standalone 상태, 인증된
계정·선택 캐릭터·기존 세션 보존, Join Ticket URL 제거를 검사하도록 했다. 로컬
비상 복귀만으로 포기 영수증을 임의 확정하지 않았는지도 확인한다. 이 모드는
2인 남은 파티 정산에만 추가하고 게임 중 포기 UI를 공개하지 않는다.

러너 PowerShell 구문 검사와 변경 UE 프로브 개별 컴파일이 통과했다. 첫 게임 모듈
빌드는 병행 변경된 보스 헤더/UHT 줄 불일치, 새 `RPGBossDashAbility.cpp`의 링크
누락과 C4458에 차례로 막혔다. 실패 로그는
`Saved/Tests/DungeonAbandonedClientLobbyReturn/{Build.log,Build-ForceUHT.log,Build-Final.log}`에
보존했다. 이후 병행 작업의 수정이 반영된 정상 게임 모듈 빌드가 링크에 성공했고,
그 DLL로 다음 선택 모드를 실행해 종료 코드 0을 확인했다.

`Run-DungeonMembershipBackendE2ETest.ps1 -ClientCount 2
-VerifyRemainingPartySettlement -VerifyAbandonedClientLobbyReturn -NoBackendRestore
-TimeoutSeconds 360`

증거는 `Saved/Tests/DungeonMembershipBackend/6acdf06133764dd6a9ab88437669945f/DungeonAbandonedClientLobbyReturn.proof.json`이다.
클라이언트 1 로그에서 `RPG_EJECTION_NET CLIENT_DISCONNECTED` 뒤
`DUNGEON_FLOW_E2E_ABANDONED_LOBBY_RETURN`이 같은 프로세스에 기록됐다.
실제 `LobbyMap`/standalone 복귀와 같은 계정·선택 캐릭터·세션 보존, URL의
Join Ticket 제거, 로컬 비상 복귀에 의한 영수증 임의 확정 부재를 확인했다.
남은 클라이언트는 동일 encounter에서 서버 GAS 처치→승리/정산→실제 로비
복귀를 완료했다. 독립 SQL로 포기자 골드·아이템 0, 남은 1인 골드 500/아이템
수량 4와 정산 수령인 1명을 확인했고, 선택 소스/러너 37개·닫힌 로그 5개 및
실행 DLL의 SHA256이 proof와 일치한다.

포기 POST는 UE 게임 중 버튼이 아니라 러너가 해당 플레이어의 인증으로
호출했다. 따라서 **같은 프로세스의 수동 포기 POST/영수증 수락**이나 실제 게임 중
포기 버튼, Steam/Shipping, 자연 협동 전투를 이 증거로 주장하지 않는다.

## 동일 프로세스 로비의 영수증 재확인 검증 (2026-10-15)

`-VerifyAbandonedClientReceipt`는 앞의 동일 프로세스 복귀 모드에만 붙인다.
기존 러너의 인증된 본인 POST가 먼저 포기를 커밋하고 서버 주기 조회가 실제
던전 연결을 닫는다. 복귀한 **바로 그 UE 클라이언트**는 개발 테스트용 로비의
기존 포기 확인창을 열어 같은 세션·캐릭터·계정으로 POST를 다시 보낸다.
테스트는 요청 중 상태와 불변 영수증 수락 후 세션 해제, 인증·캐릭터 선택 보존,
새 던전 버튼의 재활성화를 구분해 관찰한다. 백엔드의 기존 동일 요청 재전송
계약을 사용하며, 로컬 네트워크 복귀만으로 영수증을 확정하지 않는다.

PowerShell 러너 구문 검사와 변경 `RPGDungeonFlowE2EProbe.cpp`의 Development
개별 NoLink 컴파일이 통과했다. 에디터 종료 뒤 Development 게임 모듈
컴파일·DLL 링크(115개 작업)가 성공했다. 첫 E2E 시도는 Docker 엔진 미기동으로
런타임 전에 중단됐고, 설치된 Docker Desktop을 시작한 뒤 다음 선택 모드를
재실행해 종료 코드 0을 확인했다.

`Run-DungeonMembershipBackendE2ETest.ps1 -ClientCount 2
-VerifyRemainingPartySettlement -VerifyAbandonedClientLobbyReturn
-VerifyAbandonedClientReceipt -NoBackendRestore -TimeoutSeconds 360`

증거는 `Saved/Tests/DungeonMembershipBackend/0b1b9c78747949659952daf7b4214e2d/DungeonAbandonedClientReceipt.proof.json`이다.
클라이언트 1의 단일 로그에서 실제 `CLIENT_DISCONNECTED`→같은 프로세스
`LobbyMap` 복귀(로컬 영수증 미확정)→살아 있는 로비 확인 버튼/UE POST→불변
영수증 적용·세션 해제·새 던전 버튼 활성화 순서를 확인했다. 백엔드·PostgreSQL
영수증/멤버십과 남은 클라이언트의 같은 encounter 서버 GAS 승리·정산·실제
로비 복귀도 proof의 `passed=true`로 확인했다. 선택 소스/러너 39개·닫힌 로그
5개와 실행 DLL의 SHA256을 재계산해 불일치 0개였다.

이 모드는 던전 안의 UE가 최초 포기 POST를 보내는 경로나 게임 중 포기 버튼을
검증·공개하지 않는다. 최초 포기 요청은 여전히 러너가 보낸다.

## 던전 접속 UE의 최초 포기 POST 검증 (2026-10-15)

기존 2인 정산 러너의 `-VerifyDungeonClientInitialAbandonment` 선택 모드는
`-VerifyAbandonedClientLobbyReturn -VerifyAbandonedClientReceipt`를 요구한다.
러너는 입장 완료 뒤 타인 소유 요청의 403과 포기 영수증 0개를 확인하고 게이트를
연다. 그때 **실제 던전에 연결된 포기 대상 UE 클라이언트**의 개발 E2E 프로브가
기존 플레이어 `UHttpWebManager::AbandonDungeonSession`으로 최초 POST를 보낸다.
프로브는 인증된 로컬 계정·선택 캐릭터·세션과 입장한 PlayerState를 대조하며,
UE가 받은 정상 영수증의 세션·캐릭터·포기 시각을 러너의 백엔드 재조회와
일치시킨다. 러너는 첫 포기 POST를 보내지 않는다.

실행 명령은 `Run-DungeonMembershipBackendE2ETest.ps1 -ClientCount 2
-VerifyRemainingPartySettlement -VerifyAbandonedClientLobbyReturn
-VerifyAbandonedClientReceipt -VerifyDungeonClientInitialAbandonment
-NoBackendRestore -TimeoutSeconds 360`이다. 증거는
`Saved/Tests/DungeonMembershipBackend/68f0231e7e464c4a8e76558aa18afc3a/DungeonClientInitialAbandonment.proof.json`.
클라이언트 1 로그에서 최초 POST와 영수증 수신→실제 서버 회수/Iris 연결 종료→
같은 프로세스 `LobbyMap` 복귀(흐름에서 영수증 자동 확정 없음)→기존 로비 확인
버튼의 불변 영수증 재확인·세션 해제/새 던전 버튼 활성화 순서가 남았다.
독립 PostgreSQL 조회로 포기자 보상 0, 남은 1인의 골드 500/아이템 수량 4와
정산 수령인 1명을 확인했다. Development 프로브 개별 NoLink 컴파일과
병행 작업의 정상 게임 모듈 DLL 링크, E2E 종료 0이 확인됐고 선택 소스/러너
39개·닫힌 로그 5개/실행 DLL 해시 재계산 불일치가 없다.

이 **개발 프로브는 로비 전용 `URPGDungeonSessionSubsystem::AbandonDungeonSession`
UI 가드를 우회해 기존 플레이어 HTTP API를 직접 호출한다.** 따라서 운영의
게임 중 확인창·권한 가드·미확정 응답 UX가 구현 또는 검증됐다는 뜻은 아니다.
실제 Steam/Shipping, 자연 협동 전투 및 동시 입장/정산 경합은 별도다.

## 포기 커밋 앞에 대기한 입장 티켓의 재검증 (2026-10-16)

PostgreSQL private schema 회귀에 `CheckAdmissionAfterAbandonment`를 추가했다.
테스트가 세션 행 잠금을 잡은 상태에서 실제 저장소의 본인 포기를 먼저 대기시키고,
그다음 같은 캐릭터의 티켓 소비를 대기시킨다. 두 요청의 잠금 대기를
`pg_stat_activity`로 확인한 뒤 잠금을 풀면, 포기 영수증 커밋 후 입장 소비는
`null`로 거절되고 남은 파티원 1명의 멤버십은 유지된다. 이는 이미 저장된
티켓이 포기 후 권한을 복원하지 않는다는 DB 경합 검증이며, 포기 전에 완료된
입장의 UE 연결 종료 또는 게임 서버의 입장 직후 재검증 증거는 아니다.

`dotnet build` 경고/오류 0개, `DungeonAbandonmentTests --require-postgres`
전체 종료 코드 0을 확인했다. 테스트는 로컬 개발 PostgreSQL의 자체 생성
스키마 2개를 실행 후 정리했고 `public` 데이터는 건드리지 않았다. 다음은 실제
UE 서버에서 포기 커밋과 입장 완료 시점이 겹칠 때의 재검증·퇴장, 주기 조회
사이의 요청 격리를 확인하는 것이다.

## 패배 확인 → 로비 도착 후 포기 요청 (2026-10-20, 개발 한정)

게임에 연결된 동안 HTTP 포기를 보내도록 기존 로비 가드를 풀지 않는다. 패배 화면의
네이티브 결과 패널에서 별도 확인을 받은 뒤 기존 연결 종료/로컬 로비 이동을 이용하고,
정확한 로비 맵 도착 뒤 기존 `AbandonDungeonSession`으로 요청한다. 결과 BP를 재저장하지 않는다.

- 세 플래그 `-RPGDungeonAbandonmentTesting -RPGDungeonResultAbandonmentTesting -nosteam`을
  함께 사용한 비 Shipping 실행에서만 결과 버튼을 연다. 기본 실행/Shipping에서는 닫혀 있다.
  이는 실제 Steam/운영 게임 중 포기 완료가 아니라 개발 연결 단계다.
- 로컬 원격 클라이언트의 현재 `Defeated`·백엔드 `InProgress`·동일 PlayerState 계정/캐릭터/세션·
  현재 멤버십을 검사한다. 승리·완료 보상·재도전/HTTP/재로그인 진행·회수된 신원·미확정 요청·
  listen host는 진입하지 못한다. 확인창을 열 때 전투 Revision과 신원을 고정하고 최종 클릭에서 재검사한다.
- 취소는 HTTP/이동/멤버십 변경을 하지 않는다. 확인한 뒤에는 같은 신원을 이동 전에 보관하고
  재도전/새 세션/재접속을 잠근다. 로비 도착 전에는 POST를 보내지 않으며, 이동 자체를 영수증이나
  보상 제외로 취급하지 않는다. 이동을 큐에 넣지 못한 경우에만 아직 전송하지 않은 확인 정보를 해제한다.
- 로비 도착 뒤 같은 계정의 유효 인증으로 한 번 요청한다. 이동 중 인증 만료/신원 변경/맵 오류가 있으면
  고정한 신원을 유지하고 기존 로비 재로그인/수동 확인으로 이어간다. 전송 후 응답 유실은 기존 미확정
  POST 재시도를 사용한다. 취소/맵 이동/인증 만료가 이미 제출된 요청을 취소하거나 성공으로 바꾸지 않는다.
- 정상 200 영수증만 자기 캐시를 해제한다. 이동 중 남은 파티가 정산을 먼저 수락하면 기존 409 거절을
  따르므로 보상 제외를 미리 약속하지 않는다. 남은 파티·서버의 세션 상태를 클라이언트가 변경하지 않는다.

새 native 검사는 네트워크/맵/HTTP를 실제 실행하지 않는 격리 fixture다. 실제 패배 확인창→
연결 종료→로비 POST·커밋 후 응답 유실/재로그인·남은 파티의 재도전/정산을 함께 실행하는
2인 통합은 다음 단계이며, 이전 E2E를 이번 새 경로의 성공으로 재사용하지 않는다.

최초 검증은 미완료였다. `Saved/Tests/ResultAbandonment-916a5281/Build.log`에서 UHT 생성과
변경된 포기 테스트/결과 위젯 CPP 컴파일까지 확인했지만, 이번에 수정하지 않은 공유
`RPGDungeonEjectionTestFixture.h`의 생성 헤더 줄 번호 불일치와 `final`인
`UGameInstance::GetWorld()` 재정의 오류로 전체 타깃이 막혔다. 해당 파일을 덮어쓰지 않았다.
이 실패를 확인한 자체 전체 빌드는 중단했고, 별도 개별 컴파일은 다른 `Build.bat`의
잠금을 기다려 자체 대기만 취소했다. 다른 작업의 빌드/프로세스는 중단하지 않았다.

당시 선택 입력 8개는 유지됐고 DLL은 이전 재도전 단계의 바이너리였다. 새 native 검사 2개를
추가했지만 실행하지 않았으며 해당 시도의 링크/native/HTTP/DB/네트워크 성공을 주장하지 않는다.
증거와 구분은 `Saved/Tests/ResultAbandonment-916a5281/Verification.proof.json`에 기록했다.
사용자 승인 후 재개 시점에는 공유 fixture의 `GetWorld()` 재정의가 이미
`SetTestWorldContext`로 교체돼 있어 중복 수정하지 않았다. 포기 테스트도 이 바인딩을 재사용하고
해제 시 컨텍스트를 정리하도록 보완했다. 엔진의 final `GetWorld()`가 현재 테스트 월드를 반환하며
HTTP 서브시스템을 초기화하지 않는 회귀 검사를 추가했다. 엔진/운영 권한 코드는 바꾸지 않았다.

`Saved/Tests/ResultAbandonmentFix-0f8333e7/Build.log`에서 강제 UHT 검사가 통과했고 생성 매크로의
줄 번호 4개가 소스와 일치했다. 변경 CPP 컴파일도 성공했다. 다만 별도의 전용 서버
PID 24556이 게임 DLL을 로드하고 있어 링크가 `LNK1104`로 실패했다. 이 서버는 임의 종료하지 않았다.
선택 입력 10개 불일치 0이며 `Verification.proof.json`에 기록했다. 이 시점에는 최신 타깃 링크와
새 native 실행이 미완료였고 서버 종료/작업 조정이 필요했다.

### 후속: 빌드 차단 해소와 집중 native 완료

재개 중 기존 서버/클라이언트가 종료돼 다른 프로세스를 직접 중단하지 않았다. 첫 타깃의
게임/옵션 UI DLL 잠금 실패 후 재링크 3개 작업은 성공했다. 최초 native 14개 중 새 결과 확인
fixture 1개에서 `LocalPlayer`의 Outer가 잘못돼 Ensure가 발생했고, `NewObject<ULocalPlayer>(GEngine)`로
바로잡았다. 검사/Ensure를 무시하도록 바꾸지 않았다. 이후 별도 작업의 공용 HTTP 헤더와
단절 테스트 파일 갱신은 보존했다. 해당 테스트의 파일 API 오류는 소유 작업에서 수정됐다.

- 최종 Editor 타깃 증분 확인 종료 0 (`Build-final.log`, up-to-date). 최신 공유 DLL에서 검증했다.
  자체 컴파일/링크 시도와 공유 갱신은 구분하며 클린/Shipping 빌드 성공을 뜻하지 않는다.
- 최종 native는 포기 7개(새 결과 확인/큐 2개 포함), 결과 화면 4개, 복귀/복구 2개로
  **13개 성공**, 러너 종료 0이다. 테스트 구간 Error/Fatal/Assert/Ensure 0건이다.
  기존 기동 Error 15줄은 남아 있으므로 엄격 오류 0 감사 성공은 아니다.
- 최초 실행의 추가 단절 재시작 검사는 통과했지만 해당 소스가 별도 수정 중이어서 최종
  범위에서 제외했다. 최초 Ensure 실패와 최종 13개 성공을 섞어 14개 최신 통과로 표시하지 않는다.
- 선택 입력 13개 및 DLL의 실행 전후 해시가 일치했다. 증거:
  `Saved/Tests/ResultAbandonmentVerify-a45a54fb/Verification.proof.json`.
  실패 빌드와 `Native-first-summary.json`도 보존했다. 결과 BP는 변경하지 않았다.

이 단계의 개발 연결/fixture 검증은 완료다. 실제 2인 패배 확인→연결 종료/로비 POST·HTTP/DB
응답 유실/재로그인·남은 파티 재도전/정산 통합은 아직 실행하지 않았다. 기본/Shipping 차단과
기존 로비 전용 HTTP 가드를 유지하며 이 통합이 다음 작업이다.

### 실제 패배 경로의 2인 통합 러너 연결 (2026-10-20, 실행 대기)

기존 `Run-DungeonBossReturnE2ETest.ps1`에 `-VerifyResultAbandonment`를 추가했다.
별도 게임플레이 포기 API나 클라이언트 피해 RPC를 만들지 않고, 기존 개발 probe에서
서버 TakeDamage GE로 전멸시키고 실제 결과 패널의 취소→재확인 디스패처를 사용한다.
`project-rpg-online`의 로비 전용 POST/영수증 경계와 `raid-monster-ai`의 서버 권한
재도전·기존 Pawn 복구 경계를 유지했다. 운영 C++/BP와 Shipping 노출 정책은 바꾸지 않았다.

- 포기자는 같은 UE 프로세스에서 연결 종료→실제 LobbyMap→기존 flow POST→검증된
  영수증으로 이어진다. 러너가 포기 POST를 대신 호출하지 않는다. 연결 중 요청 없음,
  신원 고정, 취소/중복 확인, 포기 대기 중 재도전 차단을 관찰한다.
- HTTP/DB 영수증과 정확히 남은 멤버 1명·정산 미시작을 확인한 뒤에만 나머지 플레이어의
  실제 결과 Retry 버튼을 누른다. 서버/클라이언트의 새 보스·같은 Pawn·체력/사망/충돌/입력
  복구를 기다린 뒤 기존 서버 GAS 처치 fixture로 정산을 진행한다.
- PostgreSQL의 불변 영수증과 단일 정산 수령인, 포기자의 보상 없음·기존 장비 행 보존,
  남은 참가자의 정상 보상과 로비 복귀를 판정한다. 자신의 실행에서 만든 행만 읽으며
  멤버십/영수증/정산 행을 fixture SQL로 변경하지 않는다. 기존 개발 DB의 생성 행은 보존한다.
- 정확히 2명·로컬 개발 DB에 한정하고 다른 전투/재로그인 fixture 조합은 거절한다.
  선택 소스와 DLL의 실행 전후 해시도 대조한다. 기동 SDK 재검사 생략은 프로세스 로컬이며 복원한다.

```powershell
& ./Scripts/Network/Run-DungeonBossReturnE2ETest.ps1 -VerifyResultAbandonment -Port 18847 -BackendUrl http://127.0.0.1:3047
```

이 새 옵션의 HTTP/DB 실행은 아직 통과하지 않았다. 준비 확인 시 Docker Desktop Linux
엔진 파이프가 없어 DB 실행이 막혔고, 구문 검사 및 잘못된 옵션 조합 4건의 조기 거절만
통과했다. 이후 다른 빌드의 잠금 해제 뒤 자체 Editor 타깃 20개 작업이 성공/종료 0이며,
새 probe CPP 컴파일과 게임/옵션 UI DLL 링크까지 완료했다. 선택 입력 10개 불일치 0이다.
증거는 `Saved/Tests/ResultAbandonmentE2E-2febc8d0/Verification.proof.json`과 `Build.log`다.
운영 코드는 변경하지 않아 기존 native 13개를 반복 실행하지 않았으며, 이 빌드/러너 준비를
실제 네트워크 성공으로 간주하지 않는다. 서버 시험용 전멸/처치이며 자연 전투·응답 유실·
재로그인·Steam·Shipping 증거도 아니다. 다음 단계는 Docker 기동 뒤 위 새 옵션의 실제 실행이다.

### 정상 2인 HTTP/DB·남은 파티 재도전/정산 통과 (2026-10-20)

Docker Desktop과 개발 PostgreSQL을 기동한 뒤 위 옵션을 실제 실행했다. 최초 실행의
PowerShell 5.1 Docker stderr 오판은 진행 로그를 보존하고 종료 코드를 검사하도록 수정했다.
긴 UserDir 때문에 소비 복구 journal 하위 폴더 생성이 실패해 입장이 차단된 시도도 보존했다.
시험 UserDir만 `Saved/Tests/DBR/<12자리>/s,c1,c2`로 줄였고, 예상 최장 `.json.tmp` 경로는
251~252자다. 저장 실패 시 입장 차단, journal 내용/경로 검증, 운영 POST 가드는 풀지 않았다.

이후 실제 입장·전멸·취소까지 진행했으나 probe가 `IsRequestInFlight()`를 HTTP 전용으로
오해해 중단했다. 이 함수는 로비 이동도 잠근다. `ReturningToLobby`와 아직 소비되지 않은
queued consent·busy 상태를 함께 확인하도록 **probe만** 수정했다. 게임 잠금을 바꾸지 않았다.

- 최종 실행: `Saved/Tests/DungeonBossReturn/d5758d1b1b6c4240a49913b7a5174842/result.json`,
  `Run-DungeonBossReturnE2ETest.ps1 -VerifyResultAbandonment -Port 18847 -BackendUrl http://127.0.0.1:3047 -NoBackendRestore`, 종료 0.
- 두 클라이언트의 실제 인증/Iris 입장→서버 GAS 전멸→포기자의 결과 확인/취소/재확인→
  연결 종료/동일 프로세스 LobbyMap→flow의 실제 player POST/검증된 영수증/자기 세션만 해제를 통과했다.
- 정산 전 PostgreSQL의 영수증 1개·남은 멤버 1명·InProgress/정산 없음 확인 후, 남은 참가자의
  실제 Retry 디스패처/RPC·중복 잠금·같은 Pawn/최대 체력/사망 해제/충돌·입력/새 보스 복구를 확인했다.
  기존 서버 GAS 처치 후 단일 정산 수령인·불변 영수증·Cleared, 골드 500/물약 3/투구 1과 로비 복귀를 확인했다.
  포기자는 보상이 없고 기존 장착 무기 레코드 전체가 불변이다. 러너가 포기/정산 POST를 대신 보내지 않았다.
- 자체 Editor 증분 확인 종료 0(up-to-date)과 공유 83+2개 작업의 성공/링크는 구분해 기록했다.
  이전 기능 관찰 시도는 실행 중 공용 HTTP 소스가 갱신돼 runner가 거절했다. 해당 시도를 성공 증거로
  대체하지 않고 최신 공유 DLL에서 다시 실행해 선택 입력 14개/DLL 일치와 닫힌 로그 3개 해시를 확보했다.
- Fatal/Assert/Ensure 0이다. 각 클라이언트의 기존 CommonUI viewport Error 1개, 예정된 연결 종료
  Net Error 1개, 복귀~로비 완료 사이 standalone Iris 진단 1개는 남아 엄격 오류 0을 뜻하지 않는다.
  실제 버튼 디스패처를 사용했지만 물리 마우스/CommonUI 입력 라우팅·GPU 표시를 검증한 것은 아니다.

전체 증거와 실패 시도 구분: `Saved/Tests/ResultAbandonmentRun-f0492aee/Verification.proof.json`.
이번 작업은 러너/probe만 수정했으며 운영 C++/BP·기본/Shipping 차단은 유지했다. 개발 인증과
서버 시험용 전멸/처치에 한정한다. 다음 범위는 **이 새 경로**의 커밋 후 응답 유실·401 뒤 같은
계정 재로그인·같은 포기 신원/영수증 복구다. 이전 로비 E2E를 그 범위의 증거로 대신하지 않는다.

### 새 패배 경로의 응답 유실·401/재로그인 복구 통과 (2026-10-20)

기존 프록시와 로비 재시도 probe를 **새 결과 포기 경로의 같은 UE 프로세스**에 연결했다.
러너의 `-VerifyResultAbandonmentResponseLoss`는 기본 2인 옵션을 요구하고,
`-VerifyResultAbandonmentReauthentication`은 유실 옵션을 요구한다. 프록시는 루프백/
이번 세션·캐릭터 POST만 유실시키고, 제어 토큰/플레이어 토큰/영수증 본문은 로그에 출력하지 않는다.

최종 실제 실행은 `Saved/Tests/DungeonBossReturn/c90bacabbff549bdb29775e5d2e05ce8/result.json`,
runner 종료 0이다. 사용 옵션은 아래와 같다.

```powershell
& ./Scripts/Network/Run-DungeonBossReturnE2ETest.ps1 -VerifyResultAbandonment -VerifyResultAbandonmentResponseLoss -VerifyResultAbandonmentReauthentication -Port 18848 -BackendUrl http://127.0.0.1:3048 -TimeoutSeconds 300 -NoBackendRestore
```

- 결과 확인/취소/재확인→연결 종료→실제 로비 POST의 최초 커밋 200만 끊었다. 자동 replay를
  503으로 보류해 미확정 신원·선택/세션과 새 던전/입장 잠금을 실제 UI에서 확인했다.
- 영수증 1개/남은 멤버 1명/InProgress/정산 없음의 DB 확인 후, 포기자가 계속 미확정인 동안
  남은 참가자의 재도전·같은 Pawn 복구·GAS 처치·단일 수령인 정산/Hard 보상/로비 복귀를 완료했다.
- 그 뒤 이번 실행의 포기 계정 인증 행만 만료시켰다. 실제 로비 재시도 401, 같은 계정에 고정된
  로그인 버튼/중복 잠금·소유 캐릭터 목록 재조회·같은 포기 POST를 거쳐 원래 영수증을 받았다.
  최초 전달/응답 유실/자동 replay 보류/401/최종 200 replay는 각각 1건이고 두 영수증 SHA256이 같다.
- 복구 전후 Cleared/정산 수령인/멤버십/불변 영수증 DB 스냅샷이 같았다. 포기자 보상 없음과 기존
  장착 무기 레코드 불변, 남은 참가자의 골드 500·물약 3·투구 1을 확인했다. 하네스 인증은 독립
  갱신했고 UE에 토큰을 주입하지 않았다. 러너가 포기/정산 POST를 대신 보내지 않았다.
- 자체 Editor 타깃 19·8·25개 작업/링크가 성공했고, 기존 프록시 단위 4개와 잘못된 옵션 조합
  3개가 통과했다. 최종 선택 입력 15개/DLL 및 닫힌 로그 5개 해시를 기록했다. 최초 빈 로그 읽기,
  새 로비/소유 목록 생성 대기, 같은 자격 401이 포기 결과는 `unconfirmed`로 유지한다는 probe
  가정을 보완했다. DLL 교체 감지로 입장 전에 중단한 기록까지 성공과 분리해 보존했다.

증거: `Saved/Tests/ResultAbandonmentRecovery-e6aeb55e/Verification.proof.json`.
이번 작업은 러너/probe만 변경했다. `project-rpg-online`의 로비 전용 POST·불변 영수증·자기
멤버십/남은 파티 경계를 유지하며 운영 인증·포기 코드와 BP는 바꾸지 않았다. Fatal/Assert/Ensure는
0이지만 기존 CommonUI viewport/standalone Iris 진단과 예정된 연결 종료 Error는 남는다.
개발 인증·서버 전멸/처치·버튼 디스패처에 한정한 **유실+재로그인 복합 시나리오**의 증거다.
유실만 단독인 새 옵션을 별도로 실행했다는 뜻은 아니다. 물리 입력/GPU, 프로세스 재시작 복구,
Steam/Shipping은 별도이며 기본 노출 차단을 유지한다. 다음은 실제 입력 라우팅/개발 플레이 점검이다.

## 전투 중 포기 확인과 로비 미확정 복구 연결 (2026-10-22, 개발 한정)

비 Shipping에서 `-RPGDungeonAbandonmentTesting -RPGDungeonGameplayAbandonmentTesting -nosteam`을
함께 지정하면, 전투 중 기존 메뉴 입력 또는 Controller의 `ShowDungeonAbandonmentConfirmation()`으로
별도 확인창을 연다. 패배 전용 `RPGDungeonResultAbandonmentTesting` 플래그/진입은 별개이며
기본 실행·Shipping·listen host/standalone·승리/패배 상태에서 이 새 전투 중 진입은 닫혀 있다.

- 기존 네이티브 결과 위젯의 확인/취소 컨트롤만 재사용하고 결과/보상/재도전/새로고침/로그인
  동작은 숨기는 동시에 비활성화한다. 이 화면은 전투나 다른 파티원을 일시 정지하지 않는다.
- 메뉴가 가방/스킬 창을 먼저 닫는 기존 순서는 유지한다. 확인창의 이동/시점/커서·포커스는 기존
  창 입력 소유권에 등록한다. 취소/Back·전투 Revision 변경·Controller 종료 시 자기 창만 닫고
  자기 잠금만 해제한다. 늦은 닫기/생성 콜백은 교체된 창을 제거하거나 다른 잠금을 초기화하지 않는다.
- 현재 로컬 원격 Controller·배정 PlayerState의 계정/캐릭터/세션·현재 멤버십·백엔드 인증·전투
  `InProgress`를 확인하고 최종 클릭에서 같은 Revision/신원을 재검사한다. 재로그인/요청/이동/
  재도전·미확정 포기·회수된 권한·이미 적용된 보상은 진입하지 못한다.
- 입장 후 클라이언트 캐시가 `Loading`에 머물 수 있으므로 전투 중 진입은 `Loading`/`InProgress`
  캐시와 서버에서 복제된 실제 `InProgress`를 함께 확인한다. 패배 전용 가드는 기존대로 유지한다.
  이 검사가 백엔드의 현재 상태/정산 잠금을 대체하거나 포기 성공 권한을 만들지는 않는다.
- 취소는 HTTP/이동/멤버십을 바꾸지 않는다. 확인은 같은 신원을 먼저 고정하고 기존 계획된 연결
  종료/로컬 복귀를 예약한다. **연결 중 POST는 계속 거절하고 실제 로비 맵 도착 후에만** 기존
  bodyless 플레이어 POST를 보낸다. 서버 영수증 전에는 보상 제외를 확정하지 않는다.
- 이동 중 인증 만료, 응답 유실/잘못된 응답은 고정한 신원과 멤버십을 유지한다. 기존 로비의
  같은 계정 재로그인/같은 요청 재확인을 재사용하고 확인 전 새 던전/입장을 잠근다. 로비의
  미전송 안내도 패배 화면에 한정하지 않게 고쳤다. 이미 전송한 요청은 늦은 취소로 지우지 않는다.

집중 검사: `Scripts/Tests/Run-DungeonAbandonmentAutomationTest.ps1 -TimeoutSeconds 600`.
신규 전투 중 큐/확인 2개 + 기존 포기 7개/결과 화면 4개, **13개 성공·러너 exit 0**이다.
실제 UButton 디스패처·확인창 Back/탐색·다른 소유자의 이동 잠금 보존, stale Revision/신원/
상태/플래그 차단, `Loading` 캐시, 연결 중 POST 차단과 미확정/영수증 복구를 격리 fixture로
확인했다. 실제 네트워크/맵 이동·HTTP/DB·메뉴 물리 입력/GPU·다인 게임플레이 검증은 아니다.
테스트 전 기존 Error 15줄과 테스트 중/이후 Error 0을 구분하며 Fatal/Assert/Ensure는 0이다.

증거: `Saved/Tests/DungeonAbandonment/31b878b52d234629af249c415d60f12f/Verification.proof.json`.
DLL·선택 입력 21개·닫힌 로그 해시가 일치했다. 자체 Editor 55개 작업/전체 링크와 표시/창
재진입 보완 5개 작업 링크가 성공했다. 그 후 `Loading` 캐시 보완의 CPP 2개는 컴파일됐으나
별도 스택 분할 E2E의 private 접근 오류로 추가 타깃은 실패했다. 해당 파일은 수정하지 않았고
소유 작업의 수정/공유 링크 뒤 자체 최종 타깃이 up-to-date/exit 0임을 확인해 위 native를 실행했다.
마지막 공유 링크까지 새 자체 링크로 계산하지 않는다. 빌드 로그는
`Saved/Logs/DungeonGameplayAbandonment{Build,Build-final,LoadingBuild,Build-verified}-20261022.log`다.

최초 native 시도 `Saved/Tests/DungeonAbandonment/06d2f4199eb245519e24f67ddf7cde8e/`는 테스트
시작 전 Editor 초기화가 180초를 넘어 중단됐고 실패 proof/로그를 보존했다. 새 실행은 600초
상한으로 완료했다. 소유 검사 프로세스만 정리했으며 다른 작업/기존 DB·BP/백엔드 API는
변경하지 않았다. 광역 회귀·자연 10분은 반복하지 않았다. `project-rpg-online`의 명시적 동의/
로비 전용 POST·불변 영수증·남은 파티 보존 경계를 유지했다.

다음은 이 **새 전투 중 메뉴 진입 경로**의 실제 2인 확인/취소→연결 종료/로비 POST→포기자 제외·
남은 파티 완료/보상 연동이다. 앞선 패배 화면 E2E를 새 메뉴 경로의 성공으로 대신하지 않는다.
응답 유실/재시작·입장/정산 경합·queued 이동 drain과 Steam/Shipping 운영 노출 조건은 별도다.

## 전투 중 확인창의 실제 2인 연동 (2026-10-22)

기존 멤버십/남은 파티 러너에 `-VerifyGameplayAbandonment`를 추가했다. 정확히 2명과
`-VerifyRemainingPartySettlement`만 함께 사용하며 다른 포기/단절 시나리오와 섞지 않는다.

```powershell
& ./Scripts/Network/Run-DungeonMembershipBackendE2ETest.ps1 `
  -VerifyRemainingPartySettlement -VerifyGameplayAbandonment -ClientCount 2 `
  -Port 18871 -BackendUrl http://127.0.0.1:3071 -TimeoutSeconds 600 `
  -SkipPostgresStart -NoBackendRestore
```

실제 개발 인증·새 티켓·PostgreSQL/Iris 입장 뒤, 대상 클라이언트가 공개 Controller 진입으로
실제 확인창을 만들고 UButton 디스패처로 취소한다. 같은 Controller/Pawn/State/연결과 입력
잠금/커서 복원을 확인하고 게이트에서 대기한다. 이때 독립 SQL에서 두 멤버/lease·확인된
접속 세대/단절 기록·포기/만료 영수증 0·정산 0이 모두 그대로여야 최종 확인을 허용한다.
러너는 멤버십/세대/신원·관측 시각을 쓰거나 성공 영수증을 주입하지 않는다.

최종 확인은 같은 신원을 고정한 계획된 연결 종료→같은 UE 프로세스의 실제 LobbyMap→기존
플레이어 POST→정상 영수증 수락/자기 세션만 해제로 이어진다. 영수증 콜백이 로비 이전이거나
신원이 다르면 실패한다. 러너의 본인 HTTP 재전송은 UE 수락 이후 불변 영수증 비교에만 사용한다.
서버는 이미 닫힌 대상의 Actor 제거와 운영 주기 조회의 포기 캐시를 관측한다. **열린 연결의
자동 퇴장 증거로 재사용하지 않으며**, 남은 플레이어의 원래 연결/액터·encounter를 유지한다.

최종 러너 **exit 0**:
`Saved/Tests/DungeonMembershipBackend/dd531a8463894789ad29653a7ab411a1/DungeonGameplayAbandonment.proof.json`.
취소 SQL 전후는 `GameplayConsentCancel.proof.json`에 보존했다. 포기 영수증 정확히 1개/남은
멤버 1명, `Completed` 정산 수령인 1명이다. 포기자는 골드/추가 아이템 0·기본 대검 1개 보존,
남은 참가자는 골드 500·가방 물약 3/투구 1·우편 0·기본 대검 보존과 실제 결과 버튼/로비 복귀를
완료했다. 서버 처치는 기존 시험용 GAS hit이며 자연 플레이어 전투나 보스 AI 가동 증거는 아니다.

자체 Editor 16개 작업과 보완 후 10개 작업/타깃·DLL 링크가 성공했다
(`Saved/Logs/DungeonGameplayAbandonmentE2EBuild{,-final}-20261022.log`). 백엔드 실행별 빌드
경고/오류 0, 파서 집중 47건과 잘못된 옵션 조합 4건의 실행 전 거절이 통과했다. 실행 및
사후 확인 때 선택 입력 50개/DLL·닫힌 로그 5개 해시가 일치했다. 서버/백엔드 Error 0,
두 클라이언트는 예정된 복귀의 엔진 ConnectionLost Error 각 1개이며 Fatal/Assert/Ensure/흐름 실패는 0이다.

첫 시도 `.../6133119e47e0417abff32241f3aa36e8/`는 새 probe가 서버 전용 접속 세대를 클라이언트에서
검사해 입장 직후 실패했고 proof/schema/로그를 보존했다. 세대를 새로 복제하거나 클라이언트에
주입하지 않고, 클라이언트는 실제 PlayerId/연결 객체를 계속 대조하도록 고쳤다. 접속 세대는
서버가 초기 실제 admission에서 보관한 int64와 독립 DB 확인값을 대조한다. 최초 battle snapshot이
신원/Pawn보다 늦게 복제되는 준비 단계도 기다리며, 이후 전투/신원 변경은 계속 실패한다.

이번 변경은 opt-in probe/러너/판정기뿐이며 운영 C++/BP·백엔드 정책·기본/Shipping 차단은
바꾸지 않았다. 기존 native 13개/광역 회귀/자연 10분은 반복하지 않았다. 꺼져 있던 Docker
Desktop만 숨김으로 기동해 기존 PostgreSQL을 사용했고 DB 초기화/volume 삭제 없이 격리
schema와 증거를 보존했다. 실행 소유 프로세스만 종료하고 다른 작업과 DB 서비스는 유지했다.
`project-rpg-online`의 명시적 동의·로비 전용 POST·불변 영수증·남은 파티 보존 경계를 유지한다.

공개 Controller 호출과 실제 버튼 디스패처의 증거이며 **Enhanced Input 메뉴 키/물리 입력·Slate
포인터/GPU 경로는 미검증**이다. 다음은 이 새 전투 중 경로의 응답 유실·같은 계정 재로그인 복구다.
실제 4인·프로세스 재시작·입장/정산 경합·queued 이동 drain·Steam/Shipping 운영 공개 조건도 남는다.

## 입장 확인 대기 중 확정 탈퇴의 즉시 차단 (2026-10-28, 소스 구현·개별 컴파일)

기존 `InitNewPlayer`/입장 확인 완료 콜백은 이미 캐시된 정확한 포기/만료 영수증을
재검사한다. 그러나 `PostLogin`이 확인 HTTP를 보내기 전에는 별도 재검사가 없었고,
멤버십 완료/캐시 sweep은 인증된 PlayerState만 회수하므로 아직 미인증인 확인 대기 연결은
HTTP 응답이 돌아올 때까지 남았다. 이 공백만 보완했으며 새 조회/API/정책은 추가하지 않았다.

- `PostLogin`은 검증된 원래 연결을 확인한 뒤 알려진 불변 탈퇴/만료 기록을 다시 검사한다.
  해당 기록이 있으면 확인 HTTP를 시작하지 않고 그 대기 항목을 제거한 뒤 연결을 닫는다.
  이 시점은 엔진이 Connection의 PlayerController를 최종 연결하기 전일 수 있으므로 기존
  PostLogin의 소유 연결 검증을 유지한다.
- `RequestPendingDungeonAdmissionEjection`은 별도 private CPP로 분리했다. 게임 스레드/
  authority·현재 World/GameMode·양수 소비 세대·정확한 대기 State/Controller/Connection/
  Driver와 부모 연결 소유 관계를 확인한다. 이미 활성화된 입장·인증/회수 State·Pawn·
  교체 연결·다른 driver·replay·분할 화면 연결은 처리하지 않는다.
- 멤버십 완료와 기존 캐시 sweep에서 이 대기 경계를 먼저 적용한다. 정확한 세션/캐릭터/
  SteamID의 불변 영수증이 있어야 하며, 실패한 GET/멤버 누락/로컬 타이머는 근거가 아니다.
  연결 종료 **전에** 원래 waiter를 제거해 늦은 성공 응답이 신원/활성 입장/Pawn을 만들지
  못하게 한다. 기존 인증된 멤버의 권한 회수 경로는 그대로다.
- 미인증 대상에게 가짜 신원 회수/예기치 않은 단절 보고를 만들거나 수동 Logout/Pawn
  제거를 수행하지 않는다. 엔진 Close/CleanUp이 정리하며 다른 파티원의 전투/보상은 유지한다.

기존 native fixture에 `PendingAdmissionRevocation`/`PendingAdmissionRevocationGuards`
두 사례를 추가했다. 포기/자동 만료 각각의 멤버십 완료 → 대기 연결 종료/늦은 200 무효·
엔진 Logout·남은 파티 보존과, 실패 조회/외부 세션·계정/세대 0/클라이언트 권한/교체 소켓/
다른 owning actor/자식 연결의 거절을 다룬다. 기존
`Run-DungeonClientFlowAutomationTest.ps1 -MembershipEjectionOnly`와 전체 모드의 예상
목록에 포함했고 새 CPP를 입력 해시 추적에 추가했다. 테스트는 합성 신원/영수증과 native
연결 fixture이며 실제 HTTP/Iris 경합의 증거는 아니다.

`Saved/BuildChecks/PendingAdmissionRevocation-20261028-113247/Compile.log`에서 변경 CPP
3개가 `-FileList -NoLink`로 성공했다. Live Coding이 켜진 에디터를 유지해 출력은 임시
컴파일 위치이며 **이 구현 단계에서는 정상 DLL 링크·native 테스트 실행·실제 입장/포기 HTTP
경합을 실행하지 않았다.** 집중 native 후속 결과는 아래와 구분한다.
Runner의 PS 구문/신규 선택 목록을 확인했고 공용 Controller/스킬/보스 AI/아이템 UI·WBP는
수정하지 않았다. 새 모듈 반영 후 위 집중 검사를 실행하고 실제 확인 응답/포기 순서와
주기 조회 사이 경합을 이어서 확인한다. 현재 소스만으로 전체 입장 경합 완료나 운영 노출
승인을 주장하지 않는다.

### 별도 DLL의 입장 대기 차단 집중 native 완료 (2026-10-29)

공유 DLL 읽기 고정과 Intermediate 검증 예약이 있어 원본 모듈을 재링크하거나 다른 작업을
종료하지 않았다. 완료된 `LobbySkillPreviewLive-20261029`의 별도 DLL에 새 경계/테스트가
포함된 것을 확인하고 전용 `PendingAdmissionNative-20261029/RuntimeProject`로 복사했다.
그 DLL의 실제 로드를 PID/경로/해시로 확인했으며 기존 분리 링크 산출물 재사용이지 이번
작업의 새 빌드·링크나 정상 공유 타깃 반영은 아니다.

`Saved/BuildChecks/PendingAdmissionNative-20261029/Native.proof.json`의 **passed=true**,
소유 UE/실행 스크립트 종료 0이며 다음 5개가 각각 Success다.

- 새 `PendingAdmissionRevocation`/`PendingAdmissionRevocationGuards`: 확정 포기/만료의
  미인증 대기 연결 종료, 늦은 200의 신원/입장/Pawn 복원 차단, 엔진 정리와 남은 파티 유지,
  외부 세션·계정/불명 세대·권한/교체/자식 연결 등의 거절.
- 직접 영향 기존 `MembershipCompletion`/`MembershipCachedSweep`/`MembershipScopeReentry`:
  완료/캐시 회수와 동기 scope 종료 중 다른 참가자 보존.

복사 DLL `2FACBDF5…E5A6C3E56`과 관련 소스 8개/복사 artifact는 전후 불변이다. 입장 경계와
테스트의 핵심 소스 4개는 앞선 개별 컴파일 기록과도 일치한다. 원본 DLL `AA6C9CB0…0127810`
은 유지했고 source/에셋·다른 담당의 출력/프로세스는 수정·종료하지 않았다. 예약은 실행 전
재조회했고 완료 후 본인 범위만 해제했다. 실제 GameMode/State·네이티브 연결에 합성 신원/
영수증을 사용하는 검사이며 실제 HTTP/PostgreSQL·Iris/소켓·Steam/Shipping은 아니다.

기존 기동 Condition failed Error 15줄은 별도이며 테스트 시작 이후/종료 Error는 0이다.
미설정 성장 transport의 fixture 경고도 실제 성장 조회 성공으로 해석하지 않는다. 앞서
완료한 10분/보상/재접속 검사와 광역 suite는 반복하지 않았다. 정상 공유 모듈 반영은 공통
검증 예약 종료 후 순차 진행하고, 이 기능의 다음 실연결 범위는 입장 확인 응답/포기 커밋의
교차와 주기 조회 사이의 권한·남은 파티 보존이다. 아래 HTTP 후속도 UE 연결 전체와는
구분한다.

### 원래 확인 응답의 지연 전달과 실제 HTTP 순서 (2026-10-29)

기존 `DungeonConfirmationHoldProxy.mjs`는 요청을 늦추거나 커밋된 응답을 폐기하는 도구였다.
이번에는 `after-commit-replay`를 추가해 이미 받은 원래 응답 바이트를 메모리에 보류한 뒤
게이트에서 **한 번만** 전달한다. 성공 확인을 새로 POST하거나 합성 200을 만들지 않는다.
첫 입장 세대 1도 선택할 수 있지만 기본값은 기존 재접속 세대 2다. 기존 before-commit/
after-commit의 지연·폐기 동작은 유지한다. 새 전달 모드의 원래/전달 본문 해시를 대조하고,
잘못된 자격·비정상 200·이미 닫힌 수신자는 전달 성공으로 기록하지 않는다. 자격/본문은
메모리에만 두고 증거에는 토큰을 저장하지 않는다.

새 `Scripts/Network/Run-DungeonAdmissionAbandonmentHttpTest.mjs --backend-build <완료된 로컬 빌드>`는
기존 backend artifacts를 전용 폴더로 복사해 개발 인증/Memory·동적 loopback HTTP로만 실행한다.
실제 캐릭터/2인 세션·출발·할당·start·티켓 발급/소비를 사용하지만 서버 역할 HTTP를 fixture가
호출하며 **UE 서버·클라이언트/소켓을 실행한 검사가 아니다.** C# 소스·실행 중 backend·DB는
바꾸지 않고, 기존 빌드 재사용을 최신 아이템 API 변경의 배포/검증으로 계산하지 않는다.

| 순서 | 늦게 받은 응답 | 실제 HTTP에서 확인한 상태 |
|---|---|---|
| 확인 요청 보류 → 자기 포기 커밋 → 원래 요청 전달 | 403 | 멤버 누락의 정확한 거절이며 포기 기록/남은 멤버 유지 |
| 확인 200 수신 보류 → 자기 포기 커밋 → 원래 응답 전달 | 원래 200 그대로 | 이미 확정된 포기/보상 제외/남은 멤버가 복원·변경되지 않음 |

양쪽 모두 포기자의 reward-snapshot 403·새 확인 POST 403·새 티켓 409, 불변 포기 영수증
replay와 같은 남은 멤버 레코드/InProgress를 확인했다. 지연 응답은 딱 한 번 전송됐고
후자의 보류/전달 본문 SHA256은 같다. 확인 시각/세대의 내부 DB 상태를 공개 멤버십 GET의
표시 필드로 추정하거나, 과거 200을 현재 게임 권한이라고 해석하지 않는다.

프록시의 기존 6개+신규 5개 **11개 검사**와 구문 확인, 실제 HTTP 순서 2개/러너 종료 0이다.
최종 증거:
`Saved/BuildChecks/AdmissionAbandonmentHttp-20261029/eef6d128cd904fd08f09ff0e26e1a28c/Http.proof.json`.
복사 backend `BAC021D4…D05D9FCA`·선택 스크립트/닫힌 로그 등 14개와 두 proxy 증거 해시는
사후 일치했고 원본 UE DLL은 유지했다. 테스트 소유 backend/proxy만 정리하고 예약을 해제했다.
첫 기본 폴더의 실패는 없는 `confirmedConnectionGeneration` 표시 필드 가정,
`69a3afb48d60427c923d099dc27d28fb` 실패는 멤버 삭제 후 403을 기존 재접속의 409로 기대한
가정이었다. 둘 다 보존하고 테스트 기대만 실제 API 계약에 맞췄으며 운영 동작은 바꾸지 않았다.

이 결과는 실제 HTTP/Memory 순서의 근거다. 이전 native 5개 성공과 합쳐 UE/Iris/PostgreSQL의
단일 전체 경합 PASS로 표시하지 않는다. `nativePendingConnectionIntegration=false`이며,
다음은 이 게이트를 실제 UE의 미인증 대기 연결/영수증 캐시/Close 관측에 연결하는 것이다.
정상 공유 DLL 반영·주기 조회 사이의 전체 권한 격리/남은 파티 자연 전투는 별도다.

### 실제 UE/Iris 첫 입장 대기 연결과 늦은 원래 200 접합 완료 (2026-10-29)

`Run-DungeonAdmissionAbandonmentNetworkTest.mjs`와 기존 flow probe의 별도 관측 CPP를
연결했다. 기존 완료 backend 복사본의 개발 인증/Memory HTTP와 별도 RuntimeProject의
실제 bossmap Dedicated Server·두 클라이언트/Iris를 사용한다. `/start`와 티켓 발급·소비·
입장 확인은 운영 UE 경로가 수행하며 fixture가 소비/완료·영수증 주입·Close/Logout을
대신하지 않는다. 전투 AI/스킬·보상·WBP·운영 입장 C++ 정책은 이번에 변경하지 않았다.

남은 참가자를 먼저 실제 입장시킨 뒤, 대상의 첫 세대 확인 200을 기존 프록시에 보류했다.
서버는 정확한 미인증 PC/State/원래 열린 연결·세대 1/대기 항목을 읽고 Pawn·활성 입장이
없음을 관측한다. 플레이어 권한의 자기 포기 POST 후 **기존 주기 멤버십 조회**가 불변
영수증을 캐시해, 원래 HTTP 응답보다 먼저 waiter를 제거하고 소켓을 닫았다. 그때 원래
콜백의 request-state 소유자가 아직 살아 있음도 확인했다.

게이트에서 이미 보류한 원래 200 바이트를 한 번만 전달했다. 원래 콜백의 남은 소유자가
해제된 뒤 2초간 대기 항목·활성 입장/신원·Pawn이 생성되지 않았고, 남은 참가자의 같은
PC/Pawn/State/소켓·세대/신원·체력이 유지됐다. 콜백을 수동 호출하거나 합성 200으로
대체한 검사가 아니다. backend도 `InProgress`/동일한 남은 멤버·포기 영수증 1개/만료 0,
포기자 reward-snapshot 403·새 티켓 409를 유지했다.

최종 실제 연결 proof의 **passed=true**, 호스트 러너 종료 0:
`Saved/BuildChecks/PendingAdmissionIris-20261029/570e86d6c1cb4b45ab85d384f76688f5/Network.proof.json`.
별도 CL 2개/분리 DLL 링크 종료 0·세 UE 프로세스의 실제 모듈 경로/해시
`2A635CF1…EEDCB53`를 확인했다. 기록 입력 26개와 닫힌 로그 11개/프록시 증거는 사후
일치한다. 복사 기반 obj 434개 중 기존 probe 한 개만 의도적으로 재컴파일했고 새 probe
obj를 추가했다. 원래 사본 메타데이터는 보존하며 별도 `Verification.proof.json`에서
이 교체와 예상 외 사본 변경 0을 구분했다. 첫 사후 감사에서 JSON 배열 440개를 `@()`로
한 번 더 감싼 메타데이터 판정 실패는 보존하고 배열 읽기만 바로잡았으며, 게임 검사를
재실행하거나 원래 빌드/실행 증거를 수정하지 않았다.

서버·남은 클라이언트 `Error` 0, 대상은 예정된 연결 거절의 `ConnectionLost` Error 1개다.
flow 실패/Fatal/Assert/Ensure는 0이며 기존 누락 패키지 LoadErrors 블록 각 4개는 남는다.
관측 후 소유 서버부터 일괄 종료했으므로 UE/backend의 **자연 종료 코드 0을 주장하지 않는다**.
다른 작업/프로세스·에셋·기존 DB/backend와 원본 DLL `AA6C9CB0…0127810`은 유지했다.
스킬의 최소 검증 원칙에 따라 완료된 native/HTTP-only 두 순서/보상/600초 검사는 반복하지
않았다. 이 단계는 실제 UE/Iris + Memory의 첫 입장/성공 응답 보류 순서 하나다.
같은 시나리오의 PostgreSQL·요청 커밋 전 보류·Steam/Shipping 및 정상 공유 모듈 반영은
별도이며, 다음 통합 시 이미 완료한 검사를 다시 늘리지 않고 이 반영 범위를 인계한다.

### 첫 확인 요청의 커밋 전 보류와 실제 UE/PostgreSQL 연결 (2026-11-01)

기존 완료의 Memory/원래200 replay는 반복하지 않았다. 기존 실행기에 opt-in
`--confirmation-mode before-commit --storage-provider Postgres`와 새 실행 UUID를 추가해
아직 확인 요청이 백엔드에 전달되기 전인 순서만 실제 UE 서버/두 클라이언트와 PostgreSQL로
연결했다. 기존 생산 C++/BP·입장/멤버십/포기 정책은 변경하지 않았다.

- `/start`·티켓 생성/소비·첫 세대 확인은 UE 생산 경로가 실행했다. 남은 참가자를 먼저
  입장시킨 뒤 대상의 원래 확인 요청을 프록시에서 보류했으며 held1/forwarded0/committed0이다.
- 서버가 실제 미인증 PC/State/열린 소켓·세대1/Pawn0를 관측한 뒤, 대상 플레이어 권한으로
  자기 포기를 확정했다. 기존 주기 멤버십 조회가 불변 영수증을 캐시해 waiter를 없애고
  원래 요청의 전송/응답보다 먼저 소켓을 닫았다. 콜백 소유자는 이때까지 살아 있었다.
- 게이트 해제 후 **보류한 원래 요청 한 개만** 전달했고 실제 확인 API는403을 반환했다.
  확인 커밋/200 replay는0이며 원래 콜백 소유자 해제 뒤2초 동안 신원·Pawn·활성 입장이
  생기지 않았다. 남은 참가자의 같은 연결/PC/Pawn/State·신원/세대·HP를 유지했다.
- backend는 InProgress/남은 멤버1/불변 포기1·만료0, 포기자 reward-snapshot403·새 티켓409다.
  정상 heartbeat의 리스 연장은 허용하되 다른 멤버 필드 전체는 정확히 같고, 리스는 소수점
  정밀도까지 후퇴하지 않으며 관측 시각 이후의 유효한 값이어야 한다.

최초 실행은 위 실제 차단까지 도달한 뒤, 단순 멤버 전체 JSON 비교가 정상 리스 연장도
거절해 실패했다. 이를 다른 필드 동등성+유효/단조 리스 판정으로 보완하고 최초 실패와
중간 성공을 구분했다. 추가 보안 점검에서 PostgreSQL 선택 시 테스트 DB 연결/관리자
자격이 UE에 상속되지 않도록 분리했다. backend만 DB/admin 자격을 받고 UE 서버는
자기 배정 토큰만, 클라이언트는 서버 토큰도 없이 실행한다. 집중17개와 실제 Node 자식의
환경 경계를 확인했고, 그 보완 후 같은 새 경로의 실제 UE/PostgreSQL 최종 성공을 얻었다.

사설 모듈 `7CD047BA…DDD104E7`을 세 UE 프로세스에서 실제 로드했다. 서버/클라이언트는
닫힌 module/config/plugin/HUD overlay를 사용하고 공유 원본 DLL은 실행/고정 입력이 아니다.
새 C++/UHT/UBT·에셋 저장이나 다른 담당 프로세스 종료 없이 backend 소스도 사본에서
빌드했다. 프로세스는 관측 뒤 소유자 확인 후 종료했으므로 자연 종료코드0을 주장하지 않는다.
기존 원래200 성공/프로세스 재시작/전체 파티 정산·Steam/Shipping 검사를 빌려오지 않았다.

근거: `Saved/BuildChecks/PendingAdmissionBeforeCommit-20261101/Summary.proof.json`.
최종 case `7a686aecbf4743aab6861e07173d74f0/Network.proof.json`과 `ConfirmationHold.proof.json`,
준비 실행 `9a7116ada8444075a50fdca7626bc33a/LoadedModules.json`·입력/명령을 보존했다.
모든 경로는 같은 요약 폴더 아래다. 닫힌 원문 로그 해시와 Join Ticket 마스킹본은 각 준비
실행의 `RetainedLogs.json`에서 구분하며, 폐기한 원문/사본 해시는 당시 증거로만 취급한다.

세 번의 자기 backend/runtime/DLL/캐시/UserDir·proxy 임시를 합쳐 **222,564,456 bytes
(212.254MiB)**를 자동 폐기했다. 잔여/정리 예외0·본인 Source/Node/포트/DB fixture/사본
예약 해제이며 정상 출력/기존 복구 백업·기존 DB/자기 fixture 행과 타 담당은 유지했다.
실제 Steam/다중 PC/Shipping·서버 전체 장애/관측 저장 전 복원·전체 전투 검증은 별도다.

## 전투 중 포기 응답 유실과 정산 후 재로그인 복구 (2026-10-23)

새 전투 중 경로를 기존 로비의 미확정/재인증 probe에 연결했다. 별도
`-VerifyGameplayAbandonmentResponseLoss`는 전투 중 확인 모드를 요구하며,
`-VerifyGameplayAbandonmentReauthentication`은 이 응답 유실 모드를 추가로 요구한다.
기존 직접 POST/별도 로비 프로세스/예기치 않은 단절 시나리오와 혼합하지 않는다.

```powershell
& ./Scripts/Network/Run-DungeonMembershipBackendE2ETest.ps1 `
  -VerifyRemainingPartySettlement -VerifyGameplayAbandonment `
  -VerifyGameplayAbandonmentResponseLoss -VerifyGameplayAbandonmentReauthentication `
  -ClientCount 2 -Port 18872 -BackendUrl http://127.0.0.1:3072 `
  -TimeoutSeconds 600 -SkipPostgresStart -NoBackendRestore
```

실제 확인창 취소/SQL 불변 확인 뒤 재확인→계획된 종료→같은 프로세스 LobbyMap에서 운영
POST를 보낸다. 기존 loopback 프록시가 정확한 대상 POST의 커밋 200만 폐기하고, 명시적
재시도 게이트 전에는 HTTP 재전송을 보류한다. 자동 재전송은 transport에 따라 없을 수도
있으므로 실제 관측 건수를 기록하며 발생 자체를 성공 조건으로 요구하지 않는다.

미확정 상태의 신원/기존 던전/새 입장 잠금은 유지한다. 러너는 이때 본인 POST로 영수증을
미리 받지 않고 서버 전용 멤버십 조회와 독립 SQL로 커밋을 관측한다. 남은 파티의 승리·보상·
실제 결과 버튼/로비 복귀와 `Completed` 정산 뒤에만 대상의 복구 게이트를 연다. 재인증 모드는
격리 schema의 해당 테스트 계정 인증 행만 만료시킨다. 실제 기존 UE 자격의 POST 401→
동일 계정 로그인 버튼·소유 캐릭터 목록 재조회→같은 옛 던전/캐릭터 POST를 사용한다.
러너의 인증은 독립 갱신하며 UE에 토큰을 주입하지 않는다.

최종 UE 영수증의 정확한 신원/원래 UTC·프록시 최초/최종 응답 SHA256, 복구 전후 정산 행과
불변 영수증 전체의 동일성을 별도로 대조한다. 기존 골드/아이템 제외·남은 파티 지급·기본
장비 보존 검사는 그대로 수행한다. 결과는 실행별 `DungeonGameplayAbandonmentRecovery.proof.json`,
`GameplayRecovery.proof.json`, 최초 유실 프록시 스냅샷에 기록한다.

현재 변경은 개발용 probe/러너/판정기이며 포기 운영 C++/BP·백엔드 정책·기본/Shipping 차단은
그대로다. 공개 Controller/실제 버튼 디스패처 경로이며 Enhanced Input 메뉴 키/물리·Slate
포인터/GPU·실제 Steam/4인/프로세스 재시작의 증거가 아니다.

실제 위 조합의 러너 **exit 0**:
`Saved/Tests/DungeonMembershipBackend/7d1b1681e3534d23aa384fd4edcdfd2c/DungeonGameplayAbandonmentRecovery.proof.json`.
첫 실제 UE POST의 커밋 200 폐기 1건, 자동 재전송 관측 0건, 실제 기존 자격 401 1건,
최종 UE 영수증 replay 1건이며 최초/최종 응답 SHA256이 같다. 격리 계정의 UE/러너 인증
2개만 만료했고 실제 같은 계정 로그인/소유 목록 재조회로 복구했다. 대상의 미확정 상태 중에
남은 1인이 실제 결과 버튼/로비 복귀를 완료한 뒤 `Cleared`/`Completed`를 독립 SQL로 읽었다.
복구 전후 불변 영수증 전체·정산 행 전체·멤버 신원/세대/단절 시각·lease 목록이 동일하다
(`GameplayRecovery.proof.json`). 포기자 골드/추가 아이템 0·기본 대검 1개 보존, 남은 참가자
골드 500·가방 물약 3/투구 1·우편 0·기본 대검 보존도 통과했다.

초기 60개 작업의 공유 타깃 빌드는 인벤토리 UI의 지역 `Slot`이 UWidget 멤버를 가리는
C4458에서 실패했다. 그 파일의 지역 변수와 인자 두 곳만 `DestinationSlot`으로 바꿔 동작을
보존했고, 자체 후속 Editor **4개 작업/타깃·lib/DLL 링크**가 성공했다.
`Saved/Logs/DungeonGameplayAbandonmentRecoveryBuild{,-final}-20261022.log`에 실패/성공을
모두 보존했다. 실행별 백엔드 빌드 경고/오류 0, 직접 영향 로그 파서 **61건**·잘못된 조합
4건의 실행 전 거절이 통과했다. 실행/사후 확인의 입력 53개·닫힌 로그 7개/DLL 해시 일치,
Fatal/Assert/Ensure/흐름 실패 0이다. 각 클라이언트는 계획된 복귀 ConnectionLost 1건의
로그 두 줄, 대상은 의도한 HTTP 무응답 경고를 남겼으며 무오류 로그로 표현하지 않는다.

자연 10분·정상 경로 전체·기존 native 13개/광역 회귀는 반복하지 않았다. 소유 UE/백엔드/
프록시만 종료했고 격리 schema·증거와 기존 DB 서비스/다른 작업은 유지했다.
`project-rpg-online`에 따라 실제 로비 POST·자기 신원 고정·불확실성 보존·남은 파티 권위를
분리했다. 다음은 실제 메뉴 입력 진입이며 Slate/물리 입력·4인·재시작/경합은 별도다.

## 종료 검증의 임시 사본 폐기 (2026-10-30)

새 공통 지침에 따라 본인 `Saved/BuildChecks/` 아래 `AdmissionAbandonmentHttp`,
`PendingAdmissionNative`, `PendingAdmissionIris`, `TelemetryMembershipBarrier`,
`TelemetryDeadLetterPreservation`의 `-20261029` 검증 입력/객체·분리 DLL/PDB·임시
실행 프로젝트/UserDir/캐시·backend-build를 폐기했다. 소스·유지할 검증 스크립트·작은
결과/해시·필수 로그는 보존했다. 위 기록의 모듈/입력 해시는 당시 실행의 역사적 증거이며,
삭제된 동일 바이너리 재실행/고정 입력 재감사를 여전히 할 수 있는 것으로 취급하지 않는다.
다음 실행에 필요한 입력은 새로 생성해야 한다. 이전의 별도 담당 PCH/OBJ 정리와도 구분한다.

로비/결과 검증까지 본인 10개 디렉터리 합계 27,233,618,745 bytes(25.363 GiB), 일반
파일 3,258개 삭제·링크 34개 대상 미추적 해제, 보존 파일 233개/원본 보호 파일 7개
SHA256 동일·보존 예외 없음이다. 원본 Source/Content/Config/Plugins/Binaries/Intermediate·
공유 DDC·타 담당 예약 입력/프로세스는 정리하지 않았다. 게임/백엔드 검사도 반복하지 않았다.
정리 계획/결과: `Saved/Maintenance/DungeonPartyTempCleanup-20261030/{Plan,Result}.json`.

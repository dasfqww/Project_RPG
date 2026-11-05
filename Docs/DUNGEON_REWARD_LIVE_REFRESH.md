# 던전 보상: 재접속 없는 화면 갱신

정산이 DB에 완료되면 전용 서버가 해당 파티원의 지갑과 활성 아이템을 다시 읽어
서버 캐시에 적용하고, 소유 클라이언트에만 복제한다. 이미 열린 가방·우편 창도
목록과 `Rewards delivered: Inventory N, Mail M` 안내를 갱신한다.
N/M은 슬롯이나 첨부 개수가 아니라 아이템 수량이며, 영문 UI 문자열은 지역화 키를 제공한다.

## 보상 명령 생성과 영수증 ID

보상 명령 생성은 `DungeonRewardCommitPlanner`가 맡고, 필요한 캐릭터 정보는
`ICharacterEconomyRepository`로 조회한다. 정산 워커는 큐 임대·재시도·실패 분류와
원자 Commit 호출을 담당한다. 후보 명령 생성은 가방의 빈칸이나 스택을 미리 조회하지
않으며, 실제 인벤토리/초과 우편 배치는 기존 저장소 트랜잭션 안에서 수행한다.

정산과 조회 API는 `DungeonRewardIdentity`의 영수증 ID 규칙을 공유한다. 조회 API는
백그라운드 워커를 참조하지 않는다. 기존 해시 입력·GUID 바이트 순서·아이템 seed와
원래 보상 인덱스는 저장 호환 계약이므로 바꾸지 않는다. 회귀 검사는
`Scripts/Tests/Run-RewardPlanningTests.ps1`에서 고정 ID·재시도 명령 및 기존
Memory/PostgreSQL/HTTP 보상 흐름을 함께 확인한다.

## 읽기 전용 API와 권한

`GET /api/dungeon-sessions/{sessionId}/reward-snapshot/{characterId}`

- 세션에 묶인 GameServer 토큰만 자기 서버/세션의 실제 멤버를 조회할 수 있다.
  Administrator도 조회할 수 있으나 Player 토큰은 거절한다.
- `SettlementPending`에서는 202와 `state=Pending`; `Cleared`에서는 200과
  `state=Ready`, `wallet`, 활성 `items` 전체를 반환한다. 다른 상태는 409다.
- `Cache-Control: no-store`. 활성 아이템이 500개를 넘으면 409로 거절한다.
  일부 목록으로 전체 캐시를 교체하지 않으며, 페이지네이션은 후속 작업이다.
- 정산 중/완료 후 토큰은 제한 상태다. 예외는 이 GET과 기존 보안 API뿐이다.
  다른 세션, 멤버가 아닌 캐릭터, POST 및 일반 지갑·아이템 API는 403으로 차단한다.
- 기존 `SecurityTelemetry:PostSessionGraceSeconds` 유예(기본 300초, 최대 3600초)를
  사용한다. Pending은 세션 갱신 시각, 완료 후에는 완료 갱신 시각부터 계산하며,
  원래 토큰 만료도 계속 적용된다. 유예가 0이거나 토큰이 만료되면 실시간 조회를
  보장하지 않는다. 제한된 토큰을 일반 변경 권한으로 되돌리지 않는다.

정산 요청은 세션의 입장 유효기간을 즉시 끝낸다. 따라서 Pending 조회는 입장 만료가
아니라 위 유예 기준으로 인증한다. PostgreSQL 정산 완료 시에도 자격 증명을 즉시
삭제하지 않고 제한된 인증으로 남긴다. Pending 조회 유예가 끝나도 메모리 저장소는
아직 만료되지 않은 자격 증명을 버리지 않아, 늦은 완료 후 유예 조회가 가능하다.

응답은 조회 시점의 현재 지갑/아이템 상태이지 불변 정산 영수증은 아니다.
현재 세션의 `DungeonReward.<sessionId>` 우편 수량으로 가방/우편 안내를 산정한다.
중복 지급 방지는 기존 정산·아이템·재화 영수증이 담당한다.

### 개인 추첨의 실제 지급 수량 (2026-11-02)

서버가 `?rewardVersion=<고정 정산 버전>`을 붙이면 Ready 응답은 선택 필드
`delivery: { rewardVersion, itemQuantity }`를 포함한다. `itemQuantity`는 해당
세션/캐릭터/버전의 기존 결정적 아이템 명령 영수증 `AffectedQuantity`다. 추첨 후보
전체나 현재 가방의 총수량을 세지 않으며, 순수 재화/모두 미당첨으로 아이템 명령이
없으면0이다. 새 DB 컬럼·지급 명령·권한 확대는 없다. query가 없으면 `delivery=null`로
옛 응답/소비자를 유지한다. 버전은64자 이내의 기존 ASCII 식별자 규칙을 사용한다.

UE는 JSON 정수0..INT32_MAX와 같은 보상 버전을 확인하고, 현재 세션의 우편 수량을
뺀 값을 가방 안내로 쓴다. 추첨 보상에 수량 필드가 없는 구형 backend 응답은 후보
합계로 대체하지 않고 갱신 재시도를 유지한다. 기존 확정 전용 보상만 옛 합계 fallback을
허용한다. `delivery` 역시 지급 영수증 전체가 아니며 현재 items/wallet은 조회 시점의
투영이다. 새 Worker 배포 전 가중치 콘텐츠를 켜면 안 된다는 제한은 그대로다.

## UE 적용과 실패 처리

`RPGGameModeBase`는 정산 접수 후 1초 간격으로 최대 120초 동안 파티원별로 조회한다.
한 캐릭터에 동시 조회를 중복 발행하지 않으며 Pending/실패는 제한 시간 안에서 재시도한다.
연결이 끊긴 멤버나 조회 제한 초과는 완료로 표시하지 않고, DB 보상은 다음 입장에서 복구된다.

`RPGDungeonRewardSnapshotCodec`는 세션·캐릭터·지갑 캐릭터·아이템 소유자를 검증한다.
중첩 지갑과 아이템의 int64 값은 JSON 숫자를 문자열로 보존해 정밀도를 잃지 않는다.
PlayerState는 계정·로스터와 각 잔액의 소유 범위를 다시 검증한다.

아이템 명령 진행 중에는 적용하지 않는다. 조회 시작 때 캐시 세대를 기록하고, 응답 전에
명령 결과가 캐시를 바꿨으면 이전 응답을 거절하고 다시 조회한다. 알려진 종료 아이템은
늦은 멱등 영수증 처리를 위해 보존한다. 성공한 갱신은 더 오래된 전체 아이템 로드와
지갑 로드의 콜백도 무효화한다.

지급 안내는 PlayerState의 owner-only 속성/이벤트로 전달한다. 인벤토리 ViewModel은
현재 안내를 읽고 이후 이벤트를 구독하므로, 지급 전에 열어 둔 창과 지급 후 새로 여는
창이 모두 처리된다. 지갑과 아이템은 서로 다른 복제 경로이므로 같은 네트워크 프레임의
도착을 전제하지 않는다.

## 검증과 남은 범위

```powershell
Scripts/Tests/Run-RewardSnapshotAutomationTest.ps1
Backend/postgres-integration-test.ps1 -NoRestore -KeepArtifacts
Scripts/Network/Run-DungeonRewardPersistenceE2ETest.ps1 -ClientCount 2
Scripts/Network/Run-DungeonRewardPersistenceE2ETest.ps1 -ClientCount 2 -ClaimMail -ConcurrentClaims
```

2026-10-05 확인:

- UE Editor 빌드 성공: `Saved/Logs/RewardLiveRefreshBuild-FinalVerified-20261005.log`.
- 보상 스냅샷 자동화 3종(코덱/정밀도·세대/종료 기록·지갑/안내) 성공:
  `Saved/Tests/RewardSnapshot/Automation-2f18cc2d6d6e4a0cb25c892df5511a40/automation.log`.
  지급 후 여는 ViewModel의 안내 복원과 새 던전 진입 시 열린 창의 안내 초기화도 검증했다.
- 저장소 50개와 조회 권한/메모리 인증 경계 2개 성공:
  `Saved/Logs/RewardLiveRefresh-BackendTests-Final-20261005.log`.
- 8개 난이도/호환 HTTP 정산, 제한 토큰의 Pending/Ready 조회·일반 API 차단,
  기존 아이템·재화·보안 회귀와 재시작 성공:
  `Saved/Logs/RewardLiveRefresh-HttpIntegration-Pass-20261005.log`.
- 실제 dedicated+Iris 2인 인벤토리 직접 지급, 열린 창/골드 갱신 및 재시작 복구 성공:
  `Saved/Logs/DungeonRewardE2E/20261005-133801-0c84e7b3b482/result.json`.
- 이전 우편 전용 호환 fixture도 2인 열린 우편 창 갱신·동시 수령·수령 후 재시작 복구 성공:
  `Saved/Logs/DungeonRewardE2E/20261005-133834-3f0609964e86/result.json`.
- 기존 `ProjectRPG.Item` 자동화 42개 성공:
  `Saved/Logs/RewardLiveRefresh-ItemRegression-Final-20261005.log`.

2026-10-06 최신 바이너리에서도 Item 48개(무기 로드아웃 포함)·보상 스냅샷 3개·
저장소/인증 52개와 실제 dedicated/Iris 2인 직접 지급·완전 포화·부분 스택·이전 우편
동시 Claim 네 통합 실행을 재검증했다. 별도 장비 빌드 오류의 중단 이력은 해소됐다.
빌드 방식과 최신 결과 로그는 [포화 가방 E2E](DUNGEON_REWARD_OVERFLOW_E2E.md)를 참고한다.

`result.json`의 `LiveRewardRefreshVerified`가 재접속 전 검증이다. 기존 Claim 실행은
이전 우편 전용 페이로드의 호환 검사이며 가방 포화 E2E가 아니다. 빈/부분/포화 가방의
초과 우편 지급은 저장소 테스트로 검증했고, 실제 포화/부분 스택 가방의 2인 화면 갱신·
수령·재시작 통합 검증도 `-InventoryOverflow`로 통과했다.
[포화 가방 E2E](DUNGEON_REWARD_OVERFLOW_E2E.md)를 참고한다.
실제 보스 전투→결과 버튼→로비 맵 이동, Steam 운영 인증 및
Shipping 패키지 검증도 이 테스트의 완료 범위에 포함하지 않는다.

# 던전 보상 지급·재접속 E2E

`Scripts/Network/Run-DungeonRewardPersistenceE2ETest.ps1`은 PostgreSQL 백엔드와
UE 전용 서버, 헤드리스 클라이언트를 실행해 Hard 던전 보상을 검증한다.
정산은 UE의 실제 `ReportConfiguredDungeonClear()` 경로에서 요청한다.

2026-10-05 변경: 기본 실행은 정의에서 추출한 스택 제한을 사용해 인벤토리 우선 지급을
검사한다. `-ClaimMail` 실행만 명시적 `RPGRewardE2ELegacyMail` 테스트 플래그로
이전 우편 전용 페이로드를 만들어 기존 우편 UI/수령 회귀를 검사한다.
이 Claim 실행을 가방 포화로 인한 초과분 우편 E2E로 간주하지 않는다.
실제 포화/부분 스택의 지급·열린 창 갱신·슬롯 확보 후 수령은 `-InventoryOverflow`와
`-PartialStackOverflow`로 검증한다. [포화 가방 E2E](DUNGEON_REWARD_OVERFLOW_E2E.md)를 참고한다.

## 실행

프로젝트 루트에서 실행한다. UE 모듈을 빌드할 때에는 해당 프로젝트의
에디터와 커맨드렛을 종료한다. Docker Desktop이 실행 중이어야 한다.

```powershell
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' `
    Project_RPGEditor Win64 Development `
    'C:\UE5\Project_RPG\Project_RPG.uproject' `
    -WaitMutex -NoHotReloadFromIDE

& .\Scripts\Network\Run-DungeonRewardPersistenceE2ETest.ps1

# 우편 화면·수령·수령 후 재시작 복구까지 검증
& .\Scripts\Network\Run-DungeonRewardPersistenceE2ETest.ps1 -ClaimMail

# 두 우편 수령 버튼을 투영 갱신 전에 연속 클릭하는 별도 검증
& .\Scripts\Network\Run-DungeonRewardPersistenceE2ETest.ps1 -ClaimMail -ConcurrentClaims

# 같은 던전의 2인 파티: 각자 보상·수령·재시작 복구
& .\Scripts\Network\Run-DungeonRewardPersistenceE2ETest.ps1 -ClientCount 2 -ClaimMail

# 4인 파티의 각 클라이언트에서 두 첨부를 연속 수령
& .\Scripts\Network\Run-DungeonRewardPersistenceE2ETest.ps1 -ClientCount 4 -ClaimMail -ConcurrentClaims

# 실제 포화 가방과 부분 스택의 초과 우편 지급·수령·재시작 복구
& .\Scripts\Network\Run-DungeonRewardPersistenceE2ETest.ps1 -ClientCount 2 -InventoryOverflow
& .\Scripts\Network\Run-DungeonRewardPersistenceE2ETest.ps1 -ClientCount 2 -InventoryOverflow -PartialStackOverflow
```

`-ConcurrentClaims`는 같은 클라이언트 프레임에서 물약·투구의 Claim 버튼을 차례로
눌러, 첫 요청의 응답이나 인벤토리 투영 갱신 전에 둘째 슬롯을 예약하는지 검증한다.
두 요청 모두 성공하고 슬롯 0/1, 동일 GUID·수량, revision 2로 저장되는지와
백엔드·전용 서버 재시작 후 복구되는지를 확인한다. 순차 수령의 재요청·오류 응답
검증은 기존 `-ClaimMail` 실행에 남겨 두었다.

기본 포트는 백엔드 `3015`, UE 서버 `17815`, PostgreSQL `54329`다.
백엔드·UE 포트가 사용 중이면 실행 전에 실패한다. `-BackendUrl`, `-Port`,
`-ConnectionString`으로 변경할 수 있다. `-SkipPostgresStart`는 이미 준비된
PostgreSQL을 사용하고, `-NoBackendRestore`는 .NET 복원을 생략한다.
`-ClientCount`는 1~4를 받으며 기본값은 1이다. 각 실행은 인원수만큼 새로운
개발 계정·캐릭터를 생성하고, 세션 활성화 전에 모두 같은 백엔드 파티에 가입한다.

## 검증 흐름

1. `/health/ready`에서 실제 `Postgres` 저장소의 준비 상태를 확인한다.
2. 개발 계정·캐릭터와 Hard 세션을 생성하고 전용 서버를 할당한다.
3. `UnrealEditor-Cmd -server`에서 세션을 시작하고 백엔드 JoinTicket으로
   클라이언트의 입장을 검증한다.
4. 모든 클라이언트가 인증·초기 지갑·아이템 투영을 확인하고 서버에 준비 완료를
   알린 뒤 실제 Hard 보상 에셋을 사용해 클리어를 두 번 호출한다. UE 중복 호출
   방지 경로를 거쳐 파티원마다 정확히 한 번 지급돼야 한다.
5. `Cleared` 상태와 각자의 골드 500, 물약 3, 투구 1을 확인한다. 두 아이템은
   `revision=1`, `Active`, 해당 캐릭터 소유의 `Inventory` 레코드여야 하며 파티원 사이에
   GUID가 겹치면 실패한다. 다른 계정으로 파티원의 지갑·아이템을 읽으면 403이어야 한다.
   재접속 전에 모든 클라이언트의 이미 열린 가방/우편 ViewModel과 실제 표시 문구,
   골드가 갱신됐는지도 확인한다. 서버의 `RPG_REWARD_REFRESH_APPLIED`와 각 클라이언트의
   `LIVE_REFRESH_CLIENT_PASS`가 모두 있어야 다음 단계로 진행한다.
6. 클라이언트·전용 서버·백엔드를 종료하고 동일 PostgreSQL을 사용하는
   새 백엔드를 시작한다. 캐릭터, 완료된 세션, 골드, 아이템 GUID와
   revision이 유지돼야 한다.
7. 같은 파티로 새 세션·전용 서버·각자의 JoinTicket·클라이언트를 생성한다.
   서버 캐시는 캐릭터별 예상 GUID·소유권·수량·리비전을 확인한다. 클라이언트도
   자신의 예상 GUID 두 개만 투영에서 조회할 수 있어야 하며, 골드 500을 확인한다.
8. 복구 검증 세션은 `Failed`로 종료한다. 실행이 만든 UE·백엔드 프로세스는
   성공·실패 시 정리한다. PostgreSQL 컨테이너와 데이터는 유지한다.

`Saved/Logs/DungeonRewardE2E/<timestamp>-<runId>/`에 각 단계의 로그와
성공 시 `result.json`을 저장한다. 파티 실행의 클라이언트 로그에는 인덱스가 붙는다.
서버의 `PARTY_RESTORE_PASS Players=<인원>`과 모든 클라이언트의 성공 마커가 있어야
통과한다. `result.json`의 `Members`에는 캐릭터별 보상 결과가 있고,
`ClientOwnerProjectionVerified`와 `HttpOwnerIsolationVerified`는 소유권 검증 결과다.
기존 최상위 `CharacterId`·아이템 GUID 필드는 첫 파티원의 값이다.
오류 출력에서는 JoinTicket을 가린다.

## 생성 출처 계약

`FRPGItemRecordMetadata.CreationSource`는 백엔드의 감사용 문자열이며 최대
128자다. 예를 들어 `DungeonClear.pve_hard_v1`을 GameplayTag 등록 없이
그대로 저장·복원한다. 아이템의 `instanceTags`와 `statTag`는 계속 등록된
GameplayTag를 사용한다. JSON 아이템 GUID는 `state.instanceId` 필드다.

## 검증 범위

전용 서버는 에디터 실행 파일의 실제 dedicated 모드이며 Iris 활성 여부를
검사한다. 로컬 개발 인증과 `-nosteam`을 사용하므로 Steam 실인증 및 패키징된
배포 서버 검증은 별도로 필요하다. 이 E2E는 1~4명의 Hard 보상 정산·수령 경계다.
클리어는 테스트 GameMode에서 명시적으로 호출한다. 실제 보스 전투, Steam 로비 UI,
전투 종료 후 로비 맵 복귀까지의 전체 게임 흐름은 별도 검증이 필요하다.
다른 난이도·HTTP 재요청 정산 검증은 기존 백엔드 보상 smoke test가 담당한다.

기본 실행의 복구 단계에서 직접 지급된 두 아이템은 인벤토리 Projection에 나타난다.
이전 우편 전용 테스트의 보상은 서버의 전체 아이템 캐시에 보존되며,
수령 전에는 클라이언트 인벤토리가 비어 있어야 한다.
우편 목록은 별도의 owner-only MailProjection으로 복제된다.

`-ClaimMail` 옵션은 복구 단계에서 네이티브 우편 화면의 Claim 버튼을
실제로 호출해 물약과 투구를 수령한다. 요청 중 화면 재그리기와 중복
입력 차단도 확인한다. 같은 수령 요청의 `AlreadyApplied`,
점유 슬롯·오래된 리비전의 `Conflict`, 범위 밖 슬롯·이미 수령된 아이템의
`InvalidRequest`를 확인한다. DB에는 동일 GUID/수량으로 `Inventory`,
슬롯 0/1, `revision=2`가 저장돼야 한다. 세 번째 백엔드·전용 서버·
클라이언트에서 우편 0/인벤토리 2와 골드 500을 복구한다.
자세한 사용법과 경계는 [보상 우편](REWARD_MAIL.md)을 참고한다.

2026-10-01 검증: Editor 전체 빌드 성공, `ProjectRPG.Item` 자동화 테스트
17개 성공, 전용 서버 지급·백엔드 재시작·새 클라이언트 복구 E2E 성공.

2026-10-02 검증: `-ClaimMail`로 수령 후 동일 GUID/수량, revision 2,
우편 0/인벤토리 2 및 백엔드·전용 서버·클라이언트 재시작 복구 성공.

2026-10-04 검증: `ClaimButton.OnClicked`에서 수령 요청을 시작하고,
화면 재그리기 중 버튼 비활성/중복 입력 차단까지 확인했다.

2026-10-04 추가 검증: `-ClaimMail -ConcurrentClaims`에서 같은 프레임의 두 클릭,
두 수령 성공, 슬롯 0/1, 재시작 후 복구를 확인했다. 기존 `-ClaimMail`의 순차
수령·재요청·충돌·잘못된 요청 검증도 다시 통과했다.

2026-10-04 파티 검증: `-ClientCount 2 -ClaimMail`과
`-ClientCount 4 -ClaimMail -ConcurrentClaims`가 통과했다. 전원의 지급·수령·재시작
복구, 클라이언트의 자기 GUID 투영 및 다른 계정의 HTTP 조회 거절을 확인했다.
결과: `Saved/Logs/DungeonRewardE2E/20261004-171442-4f99a0797d5d/result.json` (2인),
`Saved/Logs/DungeonRewardE2E/20261004-171702-0aff4bc2846b/result.json` (4인).

기본값인 1인·미수령 경로도 다시 통과했다. 우편 상태의 동일 GUID/수량,
revision 1, 골드 500이 백엔드·전용 서버·클라이언트 재시작 후 복구됐다.
결과: `Saved/Logs/DungeonRewardE2E/20261004-171824-81d2b80b6bdb/result.json`.

2026-10-05 검증: 최신 UE 빌드에서 2인 인벤토리 직접 지급과 이전 우편 전용 동시 Claim
호환 E2E가 통과했다. 재접속 전에 열어 둔 창의 목록·지급 안내 및 지갑 갱신을 확인하고,
전원 프로세스 재시작 후 동일 GUID·수량·리비전을 복구했다.
결과: `Saved/Logs/DungeonRewardE2E/20261005-133801-0c84e7b3b482/result.json` (직접 지급),
`Saved/Logs/DungeonRewardE2E/20261005-133834-3f0609964e86/result.json` (우편 동시 수령).
두 결과의 `LiveRewardRefreshVerified=true`가 새 검증이며 실제 포화 가방 E2E와는 구분한다.
권한·실패 처리와 자동화는 [재접속 없는 보상 갱신](DUNGEON_REWARD_LIVE_REFRESH.md)을 참고한다.

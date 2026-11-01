# 포화 가방 보상·우편 수령 E2E

`Run-DungeonRewardPersistenceE2ETest.ps1 -InventoryOverflow`는 실제 80슬롯 가방을
채운 뒤, 정의 기반 스택 제한을 사용하는 던전 정산 경로를 검증한다.
이전 우편 전용 페이로드를 만드는 `-ClaimMail` 호환 fixture와 구분한다.

## 실행

Development Editor 모듈을 빌드하고 Docker Desktop/PostgreSQL을 준비한 뒤 실행한다.
UE 모듈 빌드 중에는 해당 프로젝트의 에디터·커맨드렛을 종료해야 한다.

```powershell
# 모든 슬롯이 보상과 병합 불가능한 아이템으로 찬 경우
Scripts/Network/Run-DungeonRewardPersistenceE2ETest.ps1 -ClientCount 2 -InventoryOverflow

# 슬롯은 모두 찼지만 기존 물약 스택에 2개를 합칠 수 있는 경우
Scripts/Network/Run-DungeonRewardPersistenceE2ETest.ps1 -ClientCount 2 -InventoryOverflow -PartialStackOverflow
```

`-PartialStackOverflow`는 `-InventoryOverflow`가 필요하다. `-ClaimMail`과
`-InventoryOverflow`는 함께 사용할 수 없다. `-ConcurrentClaims`는 기존
`-ClaimMail` fixture 전용이며, 포화 검증은 순차 수령과 동일 요청 재시도를 검사한다.
포트/DB 옵션과 프로세스 정리는 [기본 E2E](DUNGEON_REWARD_PERSISTENCE_E2E.md)와 같다.

## 준비 데이터와 기대 결과

각 캐릭터의 활성화된 세션이 발급한 GameServer 토큰으로
`POST /api/item-transactions/commit`을 호출한다. 기존 최대 16변경 제한을 지켜
16개씩 5번 생성한다. Administrator의 일반 아이템 쓰기 권한을 추가하지 않는다.

가방의 모든 슬롯은 `Red.Large` 물약으로 채운다. 보상 물약은 Unbound이므로
CharacterBound 준비 물약과 병합되지 않는다. 이는 **빈 슬롯이 없는 가방**이지,
모든 스택의 수량이 최대인 가방을 뜻하지 않는다.

| 모드 | 준비 가방 | 물약 3·투구 1 정산 직후 |
| --- | --- | --- |
| InventoryOverflow | CharacterBound 물약 1개씩 80슬롯 | 가방 80, 우편 물약 3·투구 1 |
| PartialStackOverflow | 슬롯 0은 Unbound 물약 8/10, 나머지 79슬롯은 CharacterBound 물약 1 | 기존 물약 10/revision 2, 우편 물약 1·투구 1 |

두 모드 모두 캐릭터마다 골드 500을 한 번 지급한다. 부분 스택의 기존 GUID·시드·
생성 출처는 유지되고, 새 우편 첨부는 정산으로 생성된 별도의 GUID를 갖는다.

## 실제 검증 흐름

1. 두 클라이언트가 80슬롯과 초기 골드 0을 확인하고 우편 창을 먼저 연다.
2. UE의 `ReportConfiguredDungeonClear()`를 두 번 호출해 중복 클리어 지급을 방지한다.
3. 재접속 없이 골드 500, 열린 창의 우편 목록·지급 안내·실제 표시 텍스트를 확인한다.
   완전 포화는 안내 수량 Inventory 0/Mail 4, 부분 스택은 Inventory 2/Mail 2다.
4. 실제 Claim 버튼 클릭은 포화 안내를 표시하고 명령/예약을 생성하지 않는다.
5. 백엔드·전용 서버·클라이언트를 재시작하고 **새 활성 세션**에 접속한다.
   완료 세션의 제한된 토큰을 쓰기 권한으로 되돌리지 않는다.
6. 소비 명령으로 준비 물약 하나를 제거해 슬롯을 비운 뒤 실제 Claim 버튼으로 물약을 수령한다.
   다시 찬 가방에서 투구 Claim은 차단된다. 두 번째 준비 물약을 소비한 뒤 투구를 수령한다.
7. 첫 수령의 같은 RequestId를 재시도해 `AlreadyApplied`를 확인한다.
8. 전체 프로세스를 다시 재시작하고, 가방 80/우편 0, 보상 GUID·수량·revision 2와
   골드 500의 복구를 확인한다. 부분 모드의 슬롯 0 물약은 계속 10/revision 2다.

HTTP 검사도 모든 준비 아이템과 수량 합계를 확인한다. 소비한 두 GUID는 종료 위치의
Consumed/수량 0/revision 2여야 한다. 플레이어의 종료 아이템 일괄 조회 권한은
그대로 차단하며, 이미 아는 두 GUID만 기존 소유자별 단건 조회로 검사한다.
다른 파티원의 지갑·아이템 조회는 403이어야 한다.

## 검증 기록

2026-10-05: 로컬 PostgreSQL + 실제 dedicated 모드 + Iris의 2인 파티에서 두 모드 통과.

- 완전 포화: `Saved/Logs/DungeonRewardE2E/20261005-212518-39c33a303236/result.json`.
- 부분 스택: `Saved/Logs/DungeonRewardE2E/20261005-212529-9d6d8f349590/result.json`.
- 준비 코드 포함 UE 빌드: `Saved/Logs/RewardOverflowBuild-20261005.log`.
- 저장소/인증 회귀 52개: `Saved/Logs/RewardOverflow-BackendRegression-20261005.log`.

### 추가 전체 빌드 검증의 중단 지점 (2026-10-05 실패 이력)

위 두 E2E는 `RewardOverflowBuild-20261005.log`의 성공 바이너리에서 통과했다.
이후 추가 전체 빌드에서는 동시에 변경 중인 별도 무기 장비 코드 때문에 실패했다.
최초에는 `RPGPlayerWeaponLoadoutComponent` 헤더와 UHT 생성 헤더의 줄 번호가
달랐고, 재생성 후에는 장비 CPP의 복제 인자 이름 오류가 드러났다. 다른 작업에서
인자 이름이 수정된 뒤 마지막 시도는 같은 CPP의 `UInputMappingContext` 불완전
타입/약한 포인터 대입 오류(C2679, 약 254행)로 실패했다.

- 실패 이력: `Saved/Logs/RewardOverflowBuild-Final-20261005.log`.
- 마지막 시도: `Saved/Logs/RewardOverflowBuild-Verified-20261005.log`.

해당 장비 파일은 당시 보상 작업에서 수정하지 않았다. 당시 결과를 최신 전체 소스의
링크 성공이나 추가 UE 자동화/기본·이전 우편 회귀 성공으로 간주하지 않았다.
이 중단 상태의 후속 확인은 아래 2026-10-06 기록을 참고한다.

`result.json`의 `InventoryOverflowVerified`, `FullInventoryClaimBlocked`,
`LiveRewardRefreshVerified`, `ClaimedItemsRestartRestored`가 모두 true다.
부분 모드는 `PartialStackOverflowVerified=true`,
`PotionQuantityMergedIntoExistingStack=2`, `PotionQuantity=1`이다.

### 2026-10-06 빌드 중단 해소와 최신 바이너리 재검증

재확인 시 무기 장비 CPP에 `InputMappingContext.h` 포함과 `OutLifetimeProps` 인자명
수정이 이미 반영돼 있었다. 보상 작업에서 중복 수정하지 않고, 현재 소스의 Editor 타깃
빌드와 실제 바이너리의 자동화/네트워크 경로를 확인했다. 이전 빌드 실패는 현재 상태가
아니라 보존된 실패 이력이다.

- Editor 타깃 빌드 성공: `Saved/Logs/RewardOverflowBuild-Verified-20261006.log`.
  `Target is up to date`, 0 actions인 증분 확인이며 클린 재빌드나 Shipping 검증은 아니다.
- `ProjectRPG.Item` 48개 성공: `Saved/Logs/RewardOverflow-ItemRegression-20261006.log`.
  무기 로드아웃 4개와 우편 4개가 포함된다.
- 보상 스냅샷 3개 성공:
  `Saved/Tests/RewardSnapshot/Automation-3cafb8e652084793b4ea9bc8d67c27dd/automation.log`.
- 메모리/PostgreSQL 및 인증 경계 52개 성공:
  `Saved/Logs/RewardOverflow-BackendRegression-20261006.log`.
- 최신 바이너리 2인 완전 포화 성공:
  `Saved/Logs/DungeonRewardE2E/20261006-001815-d736419c7e48/result.json`.
- 최신 바이너리 2인 부분 스택 성공:
  `Saved/Logs/DungeonRewardE2E/20261006-001825-c4895d568669/result.json`.
- 최신 바이너리 2인 기본 직접 지급 회귀 성공:
  `Saved/Logs/DungeonRewardE2E/20261006-001945-94b74dcaff0c/result.json`.
- 최신 바이너리 2인 이전 우편 동시 Claim 회귀 성공:
  `Saved/Logs/DungeonRewardE2E/20261006-001955-5e30e41ccf8c/result.json`.

자동화/저장소 검증은 총 103개(48+3+52), 실제 dedicated/Iris 2인 통합 실행은
네 모드 모두 통과했다. 추가 빌드·회귀 검증의 중단 상태는 해소됐으며, 아래 범위 밖
검증까지 완료한 것으로 확대하지 않는다.

두 포화 실행에서 열린 우편 창의 재접속 없는 갱신, 가득 찬 가방의 수령 차단,
소비로 슬롯 확보 후 수령, 중복 재시도, 백엔드/서버/클라이언트 재시작 복구와 계정 간
조회 차단이 다시 통과했다. 부분 모드의 서버 복구 로그도 실제 첨부 수량 `Potion=1`을
표시해 이전 고정 문자열 `Potion=3`을 사용하지 않는다.

## 결과 화면 연결 후 준비 검사 보완 (2026-10-06)

위 103개/네 모드 결과는 00시 초기 기준선이다. 결과 화면 연결 작업의 추가 회귀에서
공유 DB의 두 백엔드 동시 기동은 스키마 DDL `40P01` 교착으로 실패했고, 부분 스택
실행은 서버 BeginPlay 이후 약 65초 지연과 티켓 소비 HTTP 401/시간 초과로 실패했다.
이 두 실행을 통과 근거로 사용하지 않는다. 동시 DB 초기화 문제는 별도 할 일로 남겼다.

테스트 전용 GameMode의 최초 Tick에 `RPG_REWARD_E2E SERVER_TICK_READY Iris=true`를
추가하고, PowerShell이 백엔드 `InProgress`와 BeginPlay/실제 틱을 모두 확인한 뒤
티켓을 발급한다. 운영 60초 티켓 제한·멤버십·일회 소비·보상 권한은 바꾸지 않았다.
진단/빌드/실패 로그는 `Docs/DUNGEON_CLIENT_RETURN_FLOW.md`에 기록했다.

보완 후 2인 부분 스택 순차 실행:
`Saved/Logs/DungeonRewardE2E/20261006-010338-50419d2cbe72/result.json` — 성공.
서버 세 기동 단계 모두 준비 마커를 확인했으며, 기존 스택 물약 2개 병합, 물약 1개/투구
1개 우편, 열린 창 갱신, 가득 찬 가방 수령 차단, 슬롯 확보/수령과 재시작 복구가 통과했다.
이 실행은 테스트 GameMode 기반 보상 경계이며 실제 보스 결과 화면/로비 이동 E2E가 아니다.

같은 최종 바이너리의 2인 기본 직접 지급·실시간 갱신·중복 클리어 방지·재시작 복구도
`Saved/Logs/DungeonRewardE2E/20261006-010520-6df6fa55b44a/result.json`에서 성공했다.
던전 결과/복귀/보상 정의 6종·보스 10종·보상 스냅샷 3종, 총 19개 추가 자동화도 통과했다.
이 추가 회귀에서 완전 포화/이전 우편 전용 동시 Claim을 다시 실행한 것으로 표시하지 않는다.

## 범위 밖

테스트 GameMode가 클리어를 호출하며, 최소 테스트 Pawn을 사용한다. 소비 명령의
아이템 제거는 검사하지만 물약의 실제 회복 효과는 검사하지 않는다. 우편 수령은
첨부 전체를 빈 슬롯으로 옮기며 자동 병합·부분 수령을 추가하지 않았다.
로컬 개발 인증/`-nosteam`이므로 실제 보스 전투→결과 버튼→로비 복귀,
시각적 배치·한국어 지역화, Steam 운영 인증, Shipping 패키지는 별도 검증 범위다.
준비 계정·캐릭터·아이템은 개발 DB에 남기며 삭제하지 않는다. 운영 DB에는 실행하지 않는다.

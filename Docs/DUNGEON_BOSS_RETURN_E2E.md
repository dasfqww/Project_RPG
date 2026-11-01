# 실제 보스 맵 · 백엔드 · 결과 버튼 · 로비 복귀 E2E

## 검증 범위

`Scripts/Network/Run-DungeonBossReturnE2ETest.ps1`은 개발용 PostgreSQL과 실제
`/Game/Maps/bossmap`의 저장된 GameMode/Player/Controller/보스 클래스를 사용한다.
최소 Pawn/테스트 GameMode에서 정산 API를 직접 호출하던 보상 경계 E2E와 별개다.

기본 모드는 아래의 Entry 입장 경로를 유지한다. `-LobbyUI`를 추가하면 실제 로비 UI의
로그인·캐릭터 선택·대기 세션 생성/참가·입장 버튼을 사용하는 후속 모드를 실행한다.
화면 계약과 검증 결과는 [ONLINE_LOBBY_UI.md](ONLINE_LOBBY_UI.md)를 따른다.

1. 고유 개발 계정·캐릭터·파티 세션을 만들고 백엔드에 서버를 할당한다.
2. 실제 서버 틱에서 보스 초기화와 Iris 준비를 확인한다. 클라이언트는 로컬 Entry에서
   `URPGSteamAuthSubsystem::LoginToDevelopmentBackend`로 HTTP 인증하고, 공개 던전 흐름 API로
   캐릭터 선택 → 활성 세션 복원 → 새 Join Ticket 발급 → 실제 서버 이동을 수행한다.
   테스트가 클라이언트의 private 세션 상태나 Access Token을 주입하지 않는다.
3. 양쪽 클라이언트가 인증된 PlayerState, 같은 캐릭터/세션/개발 SteamID,
   Pawn의 GAS Avatar/입력, 인벤토리 준비와 Iris를 확인한 뒤 로컬 준비 파일로 처치를 허용한다.
   이 파일은 테스트 진행 장벽이지 운영 입장·보상 권한이 아니다.
4. 서버가 정상 참가자의 ASC로 Instant GE를 만들고 운영
   `URPGBossThreatComponent::ApplyDamageFromServerHit`를 통과시킨다.
   운영 피해 실행 → 보스 Dead → 네이티브 Victory → 설정된 Hard 보상 정산이 이어진다.
   `ReportConfiguredDungeonClear`를 테스트에서 직접 호출하거나 보스를 Destroy하여 승리시키지 않는다.
5. 클라이언트는 실제 승리 스냅샷, 소유자 전용 보상 안내/지갑/아이템 및 살아 있는
   `URPGDungeonResultActionsWidget`의 지급 안내와 활성화된 복귀 버튼을 기다린다.
   위젯의 실제 `OnClicked` 디스패처를 실행해 백엔드 재확인과 이동을 시작한다.
6. 같은 GameInstance에서 Returning 상태 이벤트와 실제 `/Game/Maps/LobbyMap` 로드를 확인한다.
   Standalone/Idle, 인증·캐릭터 선택·완료 세션 보존, 이전 서버 주소/JoinTicket URL 옵션 제거를 검사한다.
   로비 이동 후 플레이어 권한으로 DB의 완료 세션·지갑·아이템을 다시 조회한다.

`raid-monster-ai` 스킬의 서버 권한 원칙에 따라 클라이언트에 피해·사망·보상 결정 권한을
추가하지 않았다. 새 fixture는 명시적인 `-RPGDungeonFlowE2E -nosteam`에서만 활성화되며,
Shipping에서는 활성화되지 않는다. 서버 probe는 전용 서버/명시적 할당을 요구하고
`RPGNetTestMode`의 입장 우회가 켜져 있으면 실패한다.

Steam 연결 신원 대조는 기존 Development 전용
`-RPGItemE2EServer -RPGItemE2EAllowUnverifiedIdentity` fixture로 제외한다.
Join Ticket의 발급·서버 원자 소비·할당 세션·캐릭터 소속 확인은 운영 경로를 유지한다.
따라서 마커의 `AdmissionBypass=0`은 **백엔드 티켓 입장 우회 없음**을 뜻하며,
실제 Steam 연결 신원까지 검증했다는 뜻이 아니다.

## 실행

Editor 모듈을 빌드하고 개발용 Docker/PostgreSQL을 준비한다. 스크립트는 점유된 포트를
거절하고 실행별 별도 백엔드 빌드/로그와 고유 계정·캐릭터·세션을 사용한다.

```powershell
& .\Scripts\Network\Run-DungeonBossReturnE2ETest.ps1 -ClientCount 2 -NoBackendRestore
```

기본 포트는 UE `18827`, 백엔드 `3027`, 개발 DB `54329`다. 백엔드는 loopback만 허용한다.
파티 인원은 1~4명이다. 각 단계의 제한 시간은 기본 300초이며 테스트 프로세스만 종료한다.
DB 데이터와 실패 로그는 삭제하지 않는다. 공유 DB의 DDL 동시 초기화 문제를 해결한
것으로 간주하지 않으며 다른 백엔드 테스트와 동시에 기동하지 않는다.

완료 마커 뒤에도 2초간 모든 UE 프로세스/로그를 재검사한다.
`LogIris*`/`LogNet*` Error, Fatal/Assertion/Ensure, fixture 실패 또는 입장 우회를 검출하면 실패한다.

## 확인된 결과 (2026-10-06)

- fixture 추가 빌드: `Saved/Logs/DungeonBossReturnBuild-20261006.log` 성공.
- 최초 실행 `Saved/Tests/DungeonBossReturn/93300cc579fc44c791812d876176a9a5/`는
  두 클라이언트의 실제 인증 입장까지 진행했지만 서버의 피해 처리 시
  `SlateApplication.h:321 CurrentApplication.IsValid()` assert로 실패했다. 통과 근거가 아니다.
  운영 `ARPGBaseCharacter::ShowDamageFont/ShowInvincibleFont`가 전용 서버에서도 위젯을
  만들던 경로를 확인하여 `NM_DedicatedServer`에서는 표시만 건너뛰도록 수정했다.
  GAS 피해/체력/사망/정산 경로와 클라이언트·리슨 화면 동작은 유지한다.
- 수정 빌드: `Saved/Logs/DungeonBossReturnBuild-ServerPresentation-20261006.log` 성공.
- 수정 바이너리 던전 자동화 6종:
  `Saved/Tests/DungeonFlow/Automation-6adaa5f65a4047c99e286b8366a22d64/automation.log` 성공.
- 수정 바이너리 보스 자동화 10종:
  `Saved/Tests/BossState/Automation-32c7f7ed1bcf4f3b8a6c0897fe55a5f4/automation.log` 성공.
- 수정 바이너리 보상 스냅샷 자동화 3종:
  `Saved/Tests/RewardSnapshot/Automation-1cd13aced4144477acfe7821c13799a7/automation.log` 성공.
- 2인 실제 맵/백엔드/결과 버튼/로비 복귀:
  `Saved/Tests/DungeonBossReturn/b2cbeb91f0a6496cb476e80780ad340e/result.json` 성공.
  양쪽 모두 골드 500·물약 3·투구 1, 인벤토리 지급 수량 4·우편 0,
  실제 로비 로드·Idle·인증/선택 보존·JoinTicket 제거를 확인했다.
  최초 서버 틱 이후 준비와 완료 뒤 2초 추가 검사를 포함해 종료 코드 0이다.
- 성공 실행의 서버/두 클라이언트 로그를 별도로 재검사하여 위 네트워크/런타임 실패
  패턴 0건을 확인했다. 자동화 성공 마커는 6+10+3개이며 실패/Assertion/Ensure는 0건이다.
  실행이 생성한 백엔드/서버/클라이언트는 종료됐고 `3027`/`18827` 포트가 닫혔다.
  다른 작업의 프로세스는 종료하지 않았으며 개발 DB와 생성 데이터는 보존했다.

## 로비 UI 후속 검증 (2026-10-06)

```powershell
& .\Scripts\Network\Run-DungeonBossReturnE2ETest.ps1 -ClientCount 2 -LobbyUI -SkipPostgresStart -NoBackendRestore
```

두 클라이언트를 서버보다 먼저 실제 `/Game/Maps/LobbyMap`에서 시작한다.
로그인 버튼·인증된 내 캐릭터 선택을 거쳐 리더가 `Waiting` 세션을 만들고,
참가자가 개발 전용 GUID 참가 버튼으로 같은 세션에 들어간다. 하네스가 두 멤버를
확인한 뒤 기존 관리자 할당/서버 기동을 수행한다. UI가 백엔드 준비 상태를 조회하고
실제 입장 버튼으로 새 티켓 발급/이동을 실행한다. 이후의 처치·결과 클릭·DB 보상·
로비 복귀 검증은 기본 모드와 같으며 새 로비 위젯의 선택 표시 복원도 확인한다.

- 로비 UI 빌드: `Saved/Logs/OnlineLobbyBuild-20261006.log` 성공.
- 던전 8종·보스 10종·보상 스냅샷 3종 자동화 성공. 상세 경로는 로비 UI 문서에 기록했다.
- 새 UI 모드: `Saved/Tests/DungeonBossReturn/e4cf8876c60a4c0f9248f357af07547f/result.json` 성공.
  `LobbyUIVerified`/`WaitingSessionCreatedInUE`/`BackendPartyJoinFromUI`는 true다.
- 기본 Entry 회귀: `Saved/Tests/DungeonBossReturn/7aad3c1d43834f1b86e174eb26124ec5/result.json` 성공.
- 두 실행 모두 각자 골드 500·인벤토리 지급 4·우편 0, 인증/선택 보존·티켓 옵션 제거를 확인했다.
  서버/클라이언트 실패 경계 재검사 0건 및 테스트 포트 종료를 확인했다.

세션 GUID/서버 준비 파일은 진행 장벽이다. 토큰/private 세션 상태를 주입하지 않는다.
계정·캐릭터 사전 생성 및 관리자 서버 할당은 하네스가 담당한다.
실제 Steam 검색/초대/전송과 운영 할당 인프라는 검증하지 않았다.

2026-10-07 후속 `-LobbyUI -CreateCharactersInUI`는 캐릭터 사전 생성을 생략한다.
빈 계정의 실제 생성 버튼·이름 검사·중복 클릭 가드·소유 목록 갱신부터 동일한
입장/보상/복귀 경로를 검증한다. 상세 실행 결과와 실패 이력은 로비 UI 문서를 따른다.

추가 `-LobbyUI -ReauthenticateInLobby`는 대기 파티 구성 후 생성된 개발 계정의
DB 인증만 만료시키고 실제 HTTP 401·로그인 버튼 재실행·같은 계정의 캐릭터/세션 복원을
거쳐 입장한다. `-CreateCharactersInUI`와 함께 사용할 수 있다. 기본 로컬 개발 PostgreSQL만
대상이며 다른 실행/계정의 인증과 서버 서비스 자격 증명은 수정하지 않는다.
전체 계약과 검증 결과는 [ONLINE_LOBBY_UI.md](ONLINE_LOBBY_UI.md)의 재로그인 항목을 따른다.

2026-10-08 추가 `-LobbyUI -ReauthenticateAfterAdmission`은 실제 인증 입장 이후,
아직 보상 전인 `InProgress`에서 플레이어 인증을 만료시킨다. 서버 정산은 유지하며
결과 화면의 같은 계정 재로그인·최신 완료 조회 후 정상 복귀를 확인한다.
로비 만료 모드와 함께 사용할 수 있다. 계약/실행 기록은
[DUNGEON_CLIENT_RETURN_FLOW.md](DUNGEON_CLIENT_RETURN_FLOW.md)의 결과 화면 복구 항목을 따른다.

## 이 결과에 포함되지 않는 것

- 보스 Brain을 멈추고 서버에서 재현 가능한 치명타를 주는 fixture다.
  자연 협동 전투/플레이어 공격 Ability·실제 무기 충돌/회피/NavMesh 전투 검증이 아니다.
- 기본 Entry 모드만으로 로비 UI를 검증하지 않는다. 후속 `-LobbyUI` 모드는 실제 로비의
  캐릭터 선택·백엔드 대기 세션 생성/참가를 검증한다. Steam 초대·다른 PC·실제 Steam 티켓·
  운영 서버 할당 인프라는 두 모드 모두 포함하지 않는다.
- 이 실행은 빈 인벤토리에 직접 지급하는 사례다. 80슬롯 포화/부분 스택 우편 수령·
  중복 정산·백엔드/서버 재시작은 별도 기존 보상 E2E의 검증 범위다.
- NullRHI 실행이다. GPU 레이아웃·애니메이션·게임패드·번역·Cook/Shipping·지연/손실을
  통과한 것으로 표시하지 않는다.
- 기존 미동기화 FX 에셋 참조/Blueprint Accessed None 등의 프로젝트 경고가 남아 있다.
  네트워크/런타임 실패 경계 통과를 프로젝트 전체 무오류 판정으로 확대하지 않는다.

## 영속 시작 대검의 플레이어 공격 경로 (2026-10-18)

새 캐릭터는 생성 시 영속 대검 1개를 기본 오른손 장비 슬롯에 받는다. 이 변경에 맞춰
기본 흐름의 클라이언트 투영과 PostgreSQL 최종 검사는 물약 3개·투구 1개 외에
장비 슬롯의 시작 대검 1개도 요구한다. 보상 수량 4개에는 시작 대검을 포함하지 않는다.

`Run-DungeonBossReturnE2ETest.ps1 -NaturalPlayerAttack -ClientCount 2`는 선택형 경로다.
서버는 인증된 각 캐릭터의 authoritative 아이템 레코드, 실제 장비 액터와 공격 Ability가
같은 대검을 가리키는지 확인한다. 그 후 로컬 클라이언트는 실제 Enhanced Input의 이동·
대검 기본 공격을 사용하고, 서버는 수동 피해 없이 보스 HP 감소·Dead→Victory를 기다린다.
이후 PostgreSQL 보상·결과 버튼·로비 복귀는 기본 경로와 같다. 보스 Brain은 기존
고정 표적 fixture처럼 멈추므로 보스 AI/회피/자연 협동전 전체 검증은 아니다.

최종 C++은 UE `-ForceHeaderGeneration` 후 단일 컴파일과 Editor 타깃 전체 링크를
통과했다. 이전 전체 빌드의 생성 헤더 줄 번호 불일치는 헤더 강제 생성으로 해소했고,
`RPGAIController.cpp`의 C4458은 상속 멤버를 가리던 지역 변수 이름만 변경했다.

첫 2인 실행 `Saved/Tests/DungeonBossReturn/8f7e04e38149425c8911b70de2502876/`은
대검/Ability 확인→플레이어 입력 처치→보상 화면·복귀 클릭까지 진행했으나,
의도된 서버 로비 이탈이 남기는 UE `ConnectionLost` 로그를 러너가 실패로 판정해
로비 완료 전 소유 프로세스를 종료했다. 러너는 클라이언트 복귀 클릭 이후이며
서버가 `dungeon_planned_lobby_disconnect`로 닫은 정확한 `ConnectionLost`만 허용한다.
그 외 네트워크 오류와 로비 완료 마커 검사는 유지한다.

재실행 `Saved/Tests/DungeonBossReturn/e4715474245a43eeace9417660457608/result.json`은
종료 코드 0이다. 로컬 개발 인증 2인, Iris 보스 맵 입장, 각자의 영속 시작 대검·
네이티브 공격 Ability 및 Enhanced Input 확인, 수동 피해 없는 실제 플레이어 공격으로
보스 HP 444→Dead/Victory, PostgreSQL 각자 골드 500·물약 3·투구 1 지급과
장비 슬롯의 시작 대검 보존, 두 클라이언트의 결과 버튼/로비 맵 복귀를 확인했다.
검증 DLL SHA-256은 `370661B9AC40B2841D8AB7DA51786B6B1C30E23807788B31D22ABC4F7A5F43E5`다.
보스 Brain 정지·로컬 개발 인증·`-nosteam`·NullRHI의 범위이므로 보스 AI와 싸우는
자연 협동전, 실제 Steam, Shipping 또는 GPU 화면 검증으로 확대하지 않는다.

## 보스 AI 가동 2인 전투·정산·복귀 (2026-10-18)

`-NaturalPlayerAttack -LiveBossAI -ClientCount 2`는 기존 영속 시작 대검 검사를
유지하면서, 인증·장비 준비 장벽 뒤 보스 Brain을 재개한다. 서버는 자연 BT의 공격
예고가 같은 전투 페이즈에서 종료되는지 확인하고 플레이어 피격은 별도로 기록한다.
공격이 빗나가는 것 자체는 실패가 아니다. 클라이언트는 기존 Enhanced Input 이동·
대검 기본 공격을 사용하며, 테스트의 수동 피해·직접 Ability 활성화는 없다.

`Saved/Tests/DungeonBossReturn/820ae590b4744ae2aa6228f14f7a30cc/result.json`은
종료 코드 0이다. 실제 보스 맵/전용 서버/원격 클라이언트 2개에서 Brain 재개 →
자연 공격 예고·종료 → 플레이어 HP 150→97 → 영속 대검 공격으로 보스 Dead/Victory →
PostgreSQL 각자 골드 500·물약 3·투구 1, 시작 대검 보존 → 두 결과 버튼·로컬
로비 맵 복귀를 확인했다. `NaturalCombatVerified=true`,
`BossAttackDamageObserved=true`이며 DLL SHA-256은
`F8F475E766256D252951E13678EE3B79C87894C9F7BBBFD10CEB9F174CF24623`이다.

실행 중 발견한 UIExtension 플러그인의 `AddReferencedObjects` 부모 호출 누락을
보완했다. 첫 AI 실행의 GC assert는 기록을 보존했고, 보완 뒤 최종 실행에서는 재현되지
않았다. 이 한 번의 통과만으로 assert의 유일한 원인을 확정하거나 장시간 안정성을
증명하지는 않는다. 또 로비 복귀에서 클라이언트 `ConnectionLost`가 서버의 계획 종료
로그보다 먼저 출력되는 경합을 수정했다. 정확한 클릭 이후의 해당 오류만 임시 허용하고,
성공 전에 서버의 `dungeon_planned_lobby_disconnect`를 두 명분 요구한다.

이 결과는 로컬 개발 인증·`-nosteam`·NullRHI·2인·기본 공격/해당 보스 패턴의
검증이다. 실제 Steam 로비/운영 서버 할당, 4인 전투, 모든 패턴·회피·전멸/재시도,
Shipping/Cook·GPU·지연/패킷 손실은 별도다.

## 로비 선택 난이도의 전투·보상 연결 (2026-10-19)

`-LobbyDifficulty Easy|Normal|Hard|Hell`을 UI 전용 실행뿐 아니라 전체 흐름에도
사용한다. 기본값은 기존 Hard다. 실제 로비의 ComboBox 선택과 생성·참가 버튼,
요청 중 중복 입력 차단, 세션 생성 뒤 백엔드 난이도의 읽기 전용 표시를 확인한 뒤
서버 배정·입장·전투·정산·결과 버튼·로컬 로비 복귀까지 이어간다.
`-LobbyDifficultyOnly`는 기존처럼 Waiting 확인 뒤 종료하며 전투 검증이 아니다.

운영 GameMode와 결과 위젯은 이미 난이도별 보상 정의/지급 안내를 사용한다.
이번 연결은 probe와 실행기에 남은 Hard 고정 기대값을 제거한다. 클라이언트는
인증된 현재 세션의 난이도를 기준으로 보상 버전·지갑·소유자 아이템 투영을 확인하고,
복귀 뒤에는 실제 백엔드 조회로 다시 확인한다. 잘못된 난이도는 Hard로 대체하지 않는다.
아래 값은 저장된 보상 에셋의 fresh-character 테스트 계약이며 게임에 주입하지 않는다.

| 난이도 | 초기 NPC Ability 레벨 | 보상 버전 | 골드 | 물약 | 투구 |
| --- | ---: | --- | ---: | ---: | ---: |
| Easy | 1 | pve_easy_v1 | 100 | 1 | 0 |
| Normal | 2 | pve_normal_v1 | 250 | 2 | 0 |
| Hard | 3 | pve_hard_v1 | 500 | 3 | 1 |
| Hell | 4 | pve_hell_v1 | 1000 | 5 | 1 |

시작 대검 1개는 Equipment 슬롯 9에 보존한다. Easy/Normal은 대검과 물약 두 레코드,
Hard/Hell은 투구까지 세 레코드다. 지급 안내 수량에는 시작 대검을 포함하지 않는다.
새 캐릭터의 빈 가방을 사용하므로 이 실행의 우편 지급 수량은 0이다.

```powershell
& .\Scripts\Network\Run-DungeonBossReturnE2ETest.ps1 `
  -LobbyUI -LobbyDifficulty Normal -ClientCount 2 `
  -NaturalPlayerAttack -LiveBossAI -NoBackendRestore
```

서버 권한·입장 티켓·정산 멱등성·계획 종료의 좁은 오류 판정과 실행 전후 DLL 해시
검사는 유지한다. BP 저장/수치 조정, 실제 Steam 또는 Shipping 검증은 추가하지 않는다.

### 실제 Normal 2인 결과

`Saved/Tests/DungeonBossReturn/55c4cf54e7e64c39abe332ed53f58323/result.json`은
종료 코드 0이다. 실제 LobbyMap의 Normal 선택·Waiting 생성·다른 참가자의 동일 값
표시/변경 차단 → HTTP 서버 배정 → 미확정 대기/확정 → NPC Ability 4개 레벨 2·
초기 보스 HP 333 → 두 명의 실제 티켓/Iris 입장 → 기존 보스 BT 공격 예고·종료와
Enhanced Input 대검 공격 → Dead/Victory → 각자 PostgreSQL 골드 250·물약 2·
투구 없음·장비 대검 보존 → 지급 안내/두 결과 버튼 → 실제 로비 복귀를 확인했다.
보상 버전은 `pve_normal_v1`, 지급 안내는 인벤토리 2·우편 0이다.
플레이어 피격은 관측되지 않았으며 피격 성공으로 표시하지 않는다.

동일 DLL에서 `ProjectRPG.Online.DungeonReward.ConfiguredAssetsAreValid` 1개도
Success/종료 0이었다. 저장 보상 에셋 4개와 기존 GameMode 매핑을 검사한 것이며,
네 난이도의 전체 네트워크 전투를 모두 실행했다는 뜻은 아니다. 선택 입력 15개와
native/통합 실행 전후 DLL SHA-256
`3ABEF82E4AB7EE9EAFE46F6508CC7E8706C772971904B7F58A7A58D8BA926040`은 일치했다.
증거 요약은 `Saved/Tests/NormalDifficulty-5d918e74/Verification.proof.json`이다.

첫 Editor 빌드 중 별도 작업의 설정 헤더 변경/생성 헤더 갱신으로 전체 타깃은 실패했다.
변경 probe 자체는 컴파일됐고, 다른 빌드 종료 뒤 자체 증분 확인은 up-to-date/성공이었다.
설정 코드나 생성 파일을 덮어쓰지 않았으며 최초 실패 로그를 보존했다. 이를 자체
클린 빌드나 80개 작업 전체 성공으로 계산하지 않는다.

native 시작 전 Error 15줄, 통합 클라이언트 Error 합계 14줄이 남아 엄격 오류 0 감사는
실패다. 후자는 UIActionRouter 2·AttributeSet 캐시 재초기화 6·Standalone 로비의
테스트용 Iris 요구 4·계획된 ConnectionLost 2로 분류했다. 서버 Error와 전체
Fatal/Assert/Ensure는 0이었으며 기존 러너의 오류 허용 범위를 넓히지 않았다.
로컬 개발 인증·2인·NullRHI의 범위이며 실제 Steam/다른 PC·4인·Shipping/Cook·GPU,
전멸/재도전과 모든 난이도/패턴의 통과는 별도다. 후속은 입장/복귀 시 남는 Ability
초기화 경로 보완과 기존 P0 운영 연결이다.

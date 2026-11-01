# 보스 승리와 로비 복귀 경계

## 서버 보상

`RPGBossBattleGameMode`는 서버 BossAI의 `Dead` 상태와 BattleGameState의
승리 승인 이후 `ReportConfiguredDungeonClear()`를 한 번 호출한다. 기존 BP의
`GiveContentReward()` 호출도 남아 있으며 GameMode의 정산 가드가 중복을 막는다.

백엔드에 할당된 온라인 던전의 승리 후에는 같은 세션으로 보스를 재시작하지 않는다.
이미 정산한 세션을 재사용하지 않고 새 백엔드 세션이 필요하다. 단독 실행의 승리 후
재시도는 유지한다. 패배는 아직 백엔드를 종료하지 않아 기존 참가자 재시도가 가능하다.
패배 후 포기/세션 종료 정책은 별도 연결이 필요하다.

## 클라이언트 복귀 API

결과 UI에서 로컬 PlayerController의 `RequestDungeonReturnToLobby()` 또는
`RPGDungeonSessionSubsystem.ReturnToLobby(PlayerController)`를 호출한다.
현재 선택한 캐릭터와 세션이 있는 **온라인 클라이언트** 경로다.

1. 자신의 로컬 컨트롤러와 현재 세션 소속을 검사한다.
2. 인증된 HTTP 요청으로 현재 백엔드 세션을 다시 읽는다.
3. 같은 세션·캐릭터이며 상태가 `Cleared` 또는 `Failed`일 때만 복귀한다.
4. `ReturningToLobby` 동안 중복 입력/캐릭터 변경/세션 초기화를 차단한다.
5. 로컬 맵이 로드되면 `Idle`, 이동 실패 시 `Error / lobby_travel_failed`로 전이한다.

`Waiting`, `Loading`, `InProgress`, `SettlementPending`은 복귀를 허용하지 않는다.
특히 `SettlementPending`은 활성 세션으로 취급하여 새 캐릭터/던전을 선택하지 못하게 한다.
단순한 전투 승리나 정산 요청 수락을 DB 정산 완료로 간주하지 않는다.

기본 목적지는 `/Game/Maps/LobbyMap`이며 설정은 다음과 같다.

```ini
[/Script/Project_RPG.RPGDungeonSessionSubsystem]
LobbyMapPackage=/Game/Maps/LobbyMap
```

목적지는 존재하는 `/Game/` 맵 패키지만 허용한다. 절대 로컬 이동을 사용하므로 이전
서버 주소나 `JoinTicket` URL 옵션을 가져오지 않는다. 선택 캐릭터·백엔드 로그인과
완료 세션 스냅샷은 GameInstance에 유지한다. Steam 로비의 생성/해제는 이 API가
자동 수행하지 않는다. 로비 재구성·다음 던전 세션 생성 UI는 후속 `ONLINE_LOBBY_UI.md`의 구현을 따른다.

UI는 `OnFlowStateChanged`의 오류 코드를 표시하고 `OnLobbyTravelStarted`를 통해
로비 이동 시작을 관찰할 수 있다. 정산 중 거절되면 나중에 같은 복귀 요청을 다시 할 수 있다.

## 온라인 결과 화면 연결

`RPGPlayerController`는 서버가 복제한 보스 승리/패배 화면 위에
`RPGDungeonResultActionsWidget`을 추가한다. 기존 결과 화면 에셋은 수정하지 않는다.
온라인 세션이 없는 단독 실행의 재시도/나가기 동작은 유지한다.

- 선택한 캐릭터, 인증된 PlayerState의 캐릭터/세션 및 백엔드 멤버십이 대소문자까지 일치해야 버튼을 사용할 수 있다. 늦게 준비된 PlayerState/클라이언트 세션도 기존 결과 화면에 연결한다.
- 승리 후에는 1초 간격으로 기존 인증된 세션 조회 API를 확인한다. 자동 조회는 최대 120초이며, 요청 중/복귀 중에는 추가 요청하지 않는다. 제한 시간 이후에도 수동 새로고침을 할 수 있다.
- 백엔드 상태가 `Cleared`/`Failed`여야 복귀 버튼이 활성화된다. 보상 안내가 먼저 복제되어도 `SettlementPending`에서 복귀를 승인하지 않는다. 클릭 시에는 캐시만 믿지 않고 복귀 API가 다시 백엔드를 확인한다.
- 현재 세션의 적용된 소유자 전용 보상 안내만 표시한다. 인벤토리/우편 수량은 아이템 종류나 첨부 개수가 아니라 지급 수량이다. 이전 세션 안내는 표시하지 않는다.
- 기존 패배 화면 `ExitButton.OnButtonClicked`의 직접 맵 이동은 온라인 인스턴스에서만 제거하고 `RequestDungeonReturnFromResult()`에 연결한다. 에셋/CDO는 변경하지 않는다. 일반 UButton도 지원하며 void/무인자 서명이 맞지 않으면 바인딩 함수는 기존 이벤트를 건드리지 않고 실패한다. 컨트롤러는 이런 미지원 나가기 버튼을 비활성화하여 기존 직접 이동을 우회 실행하지 못하게 한다.
- 화면 해제·전투 재시작·컨트롤러 종료 시 타이머와 이벤트를 해제한다. HTTP/이동 실패는 상태 안내에 오류 코드를 표시하고 재시도를 허용한다.

패배 자체는 여전히 백엔드 세션 종료가 아니다. 패배 후 백엔드가 `InProgress`이면
복귀 버튼은 비활성화되며 기존 나가기 클릭도 백엔드 확인에서 거절된다. 참가자 재시도는
유지한다. 이를 포기 처리/강제 종료 API로 대체하지 않는다.

새 안내와 버튼은 `NSLOCTEXT` 키를 사용하는 네이티브 기본 UI다. 한국어 번역 수집,
GPU 화면 배치/겹침 및 게임패드 탐색은 별도 검증이 필요하다.

## 검증 범위

```powershell
& .\Scripts\Tests\Run-DungeonClientFlowAutomationTest.ps1
& .\Scripts\Tests\Run-BossStateAutomationTest.ps1
```

`ProjectRPG.Online.DungeonReturn`은 격리 월드에서 운영 응답 처리와 이동 예약을 실행한다.
미완료 정산·다른 세션·HTTP 실패·소속 불일치·컨트롤러 소실 거절, 완료 상태 이동,
중복 입력, 로드 완료/이동 실패와 캐릭터 선택 보존을 검사한다. HTTP 응답만 주입하며
실제 백엔드 요청/Steam 작업/로비 맵 로드를 수행하는 E2E는 아니다.

`ProjectRPG.Online.DungeonResult`는 정산 대기/완료, 인증 범위, 보상 안내의 세션/수량,
요청 중/복귀 중 버튼 잠금, 오류 재시도 및 자동 조회 기한 이후의 수동 새로고침 정책을 검사한다.
`ProjectRPG.Online.DungeonResultWidget`은 실제 네이티브 위젯 트리/Slate 생성과 해제,
미인증 버튼 잠금, 클릭 가드 및 재생성 시 바인딩 복구를 검사한다. 격리 월드이며
실제 화면의 시각 품질, 인증된 원격 클라이언트의 복귀나 HTTP를 실행하지 않는다.

`ProjectRPG.Boss.BattleRetry`는 실제 서버 보스 사망→승리 메서드가 클리어 보고를 한 번
선택하는지 검사한다. 테스트용 보고 메서드는 횟수만 기록한다. DB 정산 자체는 별도의
파티 보상 E2E에서 검증하며, 둘을 실제 보스전 맵 전체 통과로 간주하지 않는다.

`ProjectRPG.Boss.Presentation`은 저장된 패배 화면의 실제 ExitButton 템플릿을 복제하여
직접 이동 제거, 복귀 디스패처 연결, 재바인딩, 잘못된 서명 거절 및 원본 보존을 검사한다.

아직 남은 연결: 실제 Steam 파티 검증·운영 서버 할당 인프라,
자연 협동 전투, 패배 후 포기 및 Steam 운영 검증.
개발 인증의 실제 보스 맵 입장·서버 GAS 처치·정산·복귀는 아래 후속 E2E 기록을 따른다.

## 현재 검증 상태 (2026-10-04)

- 초기 빌드: `Saved/Logs/DungeonReturnBuild-20261004.log` 성공.
- 보스 자동화 10종: `Saved/Tests/BossState/Automation-1c9b7d8d8747474491dd1c7d924effe2/automation.log` 성공. 네이티브 사망→클리어 보고 및 온라인 승리 재시도 차단 포함.
- 복귀 자동화 최초 실행은 격리 월드의 로컬 컨트롤러 설정 누락으로 실패했다. 테스트 설정과 잘못된 소속 응답을 거절하는 처리 순서를 보완했으며 이 최초 실행을 성공 근거로 사용하지 않는다.
- 중간 재빌드는 작업 범위 밖 `RPGItemDefinitionFragments.cpp`의 소비 아이템 검증 함수에서 장비 전용 필드를 참조하는 오류로 실패했다. 별도 작업에서 해당 코드가 장비 검증 함수로 옮겨진 뒤 `Saved/Logs/DungeonReturnBuild-Verified-20261004.log` 증분 빌드가 성공했다. 로비 작업에서는 장비 소스를 수정하지 않았다. 실패 빌드를 성공 근거로 사용하지 않는다.
- 최종 복귀 자동화: `Saved/Tests/DungeonFlow/Automation-f5f2bafc424643d5b6456242b3c6f28f/automation.log` 성공. URL 옵션/원격 주소/없는 맵 거절을 포함한다. 실제 맵 로드는 실행하지 않는다.
- 최종 바이너리의 보스 10종 회귀: `Saved/Tests/BossState/Automation-a363a9ecdd9e4cda948275d7297481a0/automation.log` 성공. 네이티브 클리어 보고/온라인 승리 재시도 차단과 기존 서버 재시도·GAS/BT 검사를 함께 통과했다.
- 2인 동시 우편 수령·재시작 복구 회귀: `Saved/Logs/DungeonRewardE2E/20261004-221502-2c36dd0472a9/result.json` 성공. 기존 파티 보상 경계의 회귀이며, 새 복귀 API를 원격 맵에서 실행한 E2E가 아니다.

## 결과 화면 검증 상태 (2026-10-06)

- UI 단계 Editor 증분 빌드: `Saved/Logs/DungeonResultActionsBuild-UIFinal-20261006.log` 성공. 아래의 서버 준비 보완을 포함한 최종 빌드는 `DungeonResultActionsBuild-NetworkReady-20261006.log`다. 클린/Shipping 빌드는 아니다.
- 준비 보완까지 포함한 던전 자동화 6종: `Saved/Tests/DungeonFlow/Automation-f110258522354ae399be4be74ce01938/automation.log` 성공. 복귀·결과 정책·네이티브 위젯 생성/해제 3종 및 기존 보상 정의 3종을 모두 확인한다. 앞선 UI 단계 6종도 `Automation-999b11116c73450b927bbecb406c9da1`에서 성공했다.
- 최종 바이너리 보스 자동화 10종: `Saved/Tests/BossState/Automation-afd040bee10b4d779882f4171270982f/automation.log` 성공. 실제 저장된 ExitButton 템플릿 바인딩 검사를 포함한다.
- 최종 바이너리 보상 스냅샷 자동화 3종: `Saved/Tests/RewardSnapshot/Automation-6464698ae5dd4ea0a6bf285370a55a17/automation.log` 성공.
- 최초 두 빌드의 UI 변수명 충돌/테스트 델리게이트 비교 오류는 수정했다. `DungeonResultActionsBuild-20261006.log`, `DungeonResultActionsBuild-Verified-20261006.log`는 실패 이력이며 통과 근거가 아니다.
- 첫 던전 자동화 `Automation-f9b9ff5e6ab6429f8e1ac3f8b7667470`는 대소문자가 다른 `cleared`를 UI에서 완료로 취급해 실패했다. 상태·캐릭터·세션·멤버십·보상 안내 비교를 `CaseSensitive`로 맞추고 회귀를 보강했다. 운영 복귀 API의 엄격한 완료 확인은 유지했으며 실패 실행을 통과로 표시하지 않는다.
- 공유 DB에 두 E2E 백엔드를 동시에 기동하던 `Saved/Logs/DungeonRewardE2E/20261006-005230-e66239e103b5/`는 `PostgresGameRepository.EnsureCreatedAsync`의 스키마 DDL에서 `40P01` 교착으로 준비 이전에 실패했다. DB 초기화 동시성은 수정하지 않았으며 순차 회귀와 구분한다. 로그/기존 데이터는 삭제하지 않았다.
- 같은 초기 실행의 부분 스택 회귀 `20261006-005230-12c3c49e6c5c`는 입장 티켓 소비 HTTP 401 이후 시간 초과로 실패했다. 서버 BeginPlay 준비 마커(00:52:43 KST) 이후 최초 네트워크 처리(00:53:49 KST)까지 약 65초가 걸려, 60초 티켓 만료와 일치하는 지연을 관측했다. 개별 티켓 만료 시각을 별도로 저장하지 않았으므로 시간 관계를 근거로 한 진단이다. 실패 정리의 세션 종료 요청도 경고를 남겼으며 개발 준비 데이터는 보존한다.
- 테스트 전용 GameMode가 최초 서버 Tick에서 `SERVER_TICK_READY Iris=true`를 기록하고, PowerShell이 BeginPlay 마커·실제 틱 마커·백엔드 `InProgress`를 모두 확인한 뒤 티켓을 발급하도록 보완했다. 운영 티켓 60초 제한·인증/일회 소비·보상 가드는 변경하지 않았다. 보완 빌드: `Saved/Logs/DungeonResultActionsBuild-NetworkReady-20261006.log` 성공.
- 보완 이전 UI 바이너리의 순차 2인 직접 지급·실시간 갱신·백엔드/서버/클라이언트 재시작 복구는 `Saved/Logs/DungeonRewardE2E/20261006-005550-ee3f01940bef/result.json`에서 성공했다. 이 실행은 새 준비 마커를 검사하지 않으며, 실제 결과 버튼/로비 맵 이동 E2E가 아니다.
- 준비 보완 후 2인 부분 스택 초과 우편·실시간 갱신·가방 포화 수령 차단·슬롯 확보 후 수령·중복 요청·재시작 복구: `Saved/Logs/DungeonRewardE2E/20261006-010338-50419d2cbe72/result.json` 성공. 세 서버 기동 단계 모두 최초 틱의 Iris 준비 마커를 확인했다. UI 결과 화면을 띄우는 실제 보스 전투 E2E로 확대하지 않는다.
- 준비 보완 후 2인 기본 직접 지급·실시간 갱신·중복 클리어 방지·재시작 복구: `Saved/Logs/DungeonRewardE2E/20261006-010520-6df6fa55b44a/result.json` 성공. 두 서버 기동 단계 모두 최초 틱의 Iris 준비 마커를 확인했다. 최종 증분 빌드, 자동화 19종(6+10+3) 및 두 보상 경계 E2E가 통과했다. 공유 DB 동시 기동의 DDL 교착은 별도로 남는다.
- 두 성공 E2E의 모든 서버/클라이언트 단계 로그를 추가 검사하여 `LogIris*`/`LogNet*` Error, Fatal/Assertion/Ensure 및 보상 프로브 실패 마커가 없음을 확인했다. 실행한 테스트의 네트워크/런타임 경계 검사이며 프로젝트 전체 에셋 경고 0건 판정은 아니다. 이 실행들이 사용한 3025/3026·17825/17826 엔드포인트도 종료됐음을 확인했으며 별도 작업의 프로세스는 건드리지 않았다.

위 UI 단계 당시에는 실제 보스 맵→백엔드→로비 전체 E2E를 실행하지 않았다.
후속 개발 인증 2인 입장→서버 GAS 치명타→운영 Dead/Victory→DB 보상→살아 있는 결과 버튼→
실제 로비 로드는 `Docs/DUNGEON_BOSS_RETURN_E2E.md`의 2026-10-06 기록에서 통과했다.
전용 서버의 피해 숫자 UI 생성 assert도 해당 실행에서 수정·재검증했다.
자연 협동 전투/Steam/GPU 검증과 구분한다. Pawn 파괴 시 이동 RPC 진단·복구 fixture의
범위는 `RAID_MONSTER_AI_ARCHITECTURE.md` 26절을 따르며 이 복귀 작업의 수정 실적이 아니다.

## 로비 UI 후속 연결 (2026-10-06)

`URPGOnlineLobbySubsystem`/`URPGOnlineLobbyWidget`이 실제 로비에서 인증·내 캐릭터 선택·
대기 세션 생성/참가·준비된 서버 입장을 연결하며 복귀 후 UI와 선택 표시를 재생성한다.
Editor 증분 빌드와 던전 8종·보스 10종·스냅샷 3종, 로비 UI 2인 E2E 및 기존 Entry
경로 2인 회귀가 통과했다. 로비 UI E2E는 결과 복귀 버튼을 실행하고 실제 맵 로드·Idle·
인증/선택 보존·티켓 URL 제거를 확인한다. 실제 Steam/운영 할당/자연 전투/GPU 통과와
구분하며 계약과 정확한 로그는 [ONLINE_LOBBY_UI.md](ONLINE_LOBBY_UI.md)를 따른다.

## 던전 플레이 중 인증 만료의 결과 화면 복구 (2026-10-08)

같은 날짜의 후속 시계 변경 경계는 [PLAYER_AUTHENTICATION_CLOCK.md](PLAYER_AUTHENTICATION_CLOCK.md)를 따른다.
결과 화면 재인증·최신 완료 조회·별도 복귀 검증은 유지하며 인증 만료 계산만 보강한다.

전투 입장의 인증된 PlayerState와 플레이어의 HTTP access token은 별개다. 플레이어
토큰이 만료되어도 서버 권한 전투/정산을 취소하거나 보상을 다시 지급하지 않는다.
결과 화면에서 같은 계정으로 재로그인해 백엔드 완료 상태를 다시 확인하는 경로를 추가했다.

- access token 만료/HTTP 401 상태에서는 결과의 복귀·조회·자동 폴링을 잠근다.
  캐시가 `Cleared`이거나 보상 안내가 먼저 왔어도 만료 인증으로 복귀하지 않는다.
- `ResultLoginSteamButton`은 기존 Steam 인증 교환을 사용한다. 개발 버튼은 비 Shipping의
  `-RPGDevelopmentLogin -nosteam`에서만 만들고 ID 입력 없이 현재 고정 계정으로 로그인한다.
- 선택 캐릭터·세션·멤버십, 입장 시 인증된 PlayerState 및 고정 SteamID가 일치해야 한다.
  재로그인 중 중복 클릭/복귀/조회는 잠그며, 완료 시에도 클릭 당시의 계정·캐릭터·세션을 대조한다.
- 성공 후 같은 세션을 다시 조회한다. 로그인 자체로 이동하거나 정산/보상을 호출하지 않는다.
  사용자 복귀 클릭은 기존 `ReturnToLobby`의 별도 최신 HTTP 조회와 완료/소속 검증을 거친다.
- 결과 화면 해제 시 자신의 로그인 이벤트/버튼 바인딩과 캐시를 제거한다.

```powershell
& .\Scripts\Network\Run-DungeonBossReturnE2ETest.ps1 -ClientCount 2 -LobbyUI -CreateCharactersInUI -ReauthenticateAfterAdmission -SkipPostgresStart -NoBackendRestore
```

`-ReauthenticateAfterAdmission`은 `-LobbyUI`와 기본 로컬 개발 PostgreSQL 연결/컨테이너만
허용한다. `-ReauthenticateInLobby`와 함께 실행하면 로비와 던전에서 순서대로 두 번 만료된다.
모든 클라이언트의 실제 입장/Iris/인증된 PlayerState를 확인한 뒤 이번 실행의 고유 개발
계정 인증만 DB에서 만료시킨다. 보상 전 `InProgress`의 실제 세션 조회 HTTP 401과 신원
보존을 확인한 후 기존 서버 GAS 처치 fixture를 진행한다. 결과의 만료 복귀/조회 강제
클릭 거절, 실제 재로그인 버튼과 중복 잠금, 같은 계정/캐릭터/세션의 최신 완료 조회,
별도 복귀 클릭·실제 로비·DB 보상을 검증한다. UE private 인증/세션 상태는 주입하지 않는다.
결과의 `DungeonAuthenticationExpiryVerified`/`ResultReauthenticationVerified`는 다른 모드에서 false다.

자연 전투·실제 Steam·60분 자연 경과·시계 오차·네트워크 단절/강제 포기·Shipping/GPU
검증이 아니다. 인증 서버에 다시 로그인할 수 없는 경우의 강제 로비 탈출 정책과,
백엔드가 아직 `InProgress`인 패배의 포기 처리는 별도로 남는다.

검증 기록 (2026-10-08):

- Editor 증분 빌드: `Saved/Logs/DungeonResultReauthenticationBuild-20261008.log` 성공.
  다른 빌드의 실행 순서를 기다린 뒤 검증했으며 별도 소스 변경은 보존했다.
- 던전/결과/로비/인증 자동화 12종:
  `Saved/Tests/DungeonFlow/Automation-e9cb8cd1546d4de385867b0ee63017eb/automation.log` 성공.
  새 `DungeonResultAuthentication`은 만료·정산 대기·완료 캐시/보상 안내의 복귀 불가,
  같은 계정/멤버십·중복 로그인/조회/복귀 잠금과 재인증 후 최신 조회 정책을 검사한다.
  기존 실제 Slate 테스트는 로그인 버튼 가드/바인딩 해제·재생성 및 기본 개발 버튼 제외도 검사한다.
- 보스 10종: `Saved/Tests/BossState/Automation-585cc138ce60497baaeeee4c30381af6/automation.log` 성공.
- 보상 스냅샷 3종: `Saved/Tests/RewardSnapshot/Automation-c38a50cd1cba490d9e33da4886be2eff/automation.log` 성공.
  성공 마커 12+10+3개, 실패/Fatal/Assertion/Ensure 0건을 별도로 확인했다.
- 2인 입장 후 만료·결과 재로그인·입장/보상/복귀:
  `Saved/Tests/DungeonBossReturn/401ba7603a0b4f50b5add355da393e67/result.json` 성공.
  두 클라이언트 모두 실제 `InProgress`/보상 전 401과 신원 보존, 결과 재로그인 버튼·
  중복 입력 잠금·만료 복귀/조회 강제 클릭 거절·같은 계정의 최신 완료 조회를 확인했다.
  골드 500·인벤토리 4·우편 0을 받고 실제 결과 복귀 버튼/로비와 캐릭터 선택 표시가 복구됐다.
  서버/클라이언트 네트워크 Error·Fatal/Assertion/Ensure·fixture 실패·입장 우회 0건이다.
- 4인 로비 만료 → 재로그인 → 입장 후 다시 만료 → 결과 재로그인 → 보상/복귀:
  `Saved/Tests/DungeonBossReturn/5ad296fb79e7475ba96ca27c01580261/result.json` 성공.
  `-LobbyUI -CreateCharactersInUI -ReauthenticateInLobby -ReauthenticateAfterAdmission`으로 실행했다.
  네 클라이언트 각각 실제 HTTP 401 두 번, 로비/결과 재로그인, 인증된 입장 및 실제 로비 복귀
  마커를 확인했다. 모두 골드 500·인벤토리 4·우편 0이며 두 재인증 검증 플래그가 true다.
- 기존 Entry 경로 2인 회귀:
  `Saved/Tests/DungeonBossReturn/9f6c480ae0c3422e937ed4052192ef50/result.json` 성공.
  만료 옵션 없이 실제 입장·서버 GAS 처치·DB 보상·결과 버튼·로비 복귀를 검증했다.
  새 던전 인증 만료/결과 재로그인 플래그는 false이며 각 캐릭터의 지급 수량은 동일하다.
- 세 E2E의 모든 서버/클라이언트 로그를 별도로 검사해 `LogIris*`/`LogNet*` Error,
  Fatal/Assertion/Ensure, fixture 실패 및 입장 우회 마커가 없음을 확인했다.
  테스트 소유 프로세스와 3027/TCP·18827/UDP 리스너도 종료됐다. 로그·개발 DB는 보존했다.

## 2026-10-09 후속: 로컬 비상 복귀와 정상 완료 복귀 구분

위의 `InProgress`/`SettlementPending` 복귀 거절은 정상 `ReturnToLobby` 계약이며 유지한다.
인증 서버 불통/네트워크 단절에서는 별도의 `DisconnectToLobby`로 연결만 종료하고 로컬
로비에 이동한다. 계정·선택·세션·멤버십은 보존하고, 보상은 서버 정산을 확인해야 한다.
이동만으로 백엔드 세션을 완료/포기 처리하거나 보상/아이템을 지급하지 않는다.
미완료 세션은 로비에서도 활성 상태이며 새 입장에는 새 백엔드 Join Ticket이 필요하다.

요청 세대 무효화, 실패 이벤트 격리, UI 및 현재 검증/미구현 범위는
[DUNGEON_CONNECTION_RECOVERY.md](DUNGEON_CONNECTION_RECOVERY.md)에 기록한다.
백엔드의 명시적 포기·보상 제외·로그아웃/계정 전환은 후속 작업이다.

## 패배 결과의 재도전과 세션 유지 이동 (2026-10-20)

기존 패배 BP의 `RetryButton`은 같은 무인자 `RequestBossBattleRestart` 디스패처를 유지한다.
네이티브 결과 패널에도 파티 재도전 버튼을 연결했다. BP 에셋을 재저장하지 않는다.

- 요청은 현재 복제된 전투 `Revision`과 요청 번호를 사용한다. 서버는 실제 참가자·회수 여부·
  현재 결과·정확한 전투 번호를 검사하고 기존 Pawn/GAS 복구와 보스 교체를 실행한다.
  무효 번호와 이전 전투의 지연 요청은 복구 전에 거절한다. 온라인 승리 재도전 금지는 유지한다.
- 처리 중에는 중복 재도전과 결과 패널의 조회/이동을 잠근다. 수락 응답만으로 클라이언트가
  전투를 바꾸지 않으며 새 서버 스냅샷을 기다린다. 거절 또는 단조 증가 시계의 10초 timeout은
  잠금을 해제하고 재시도를 안내한다. 이전 요청의 늦은 응답은 다음 요청을 해제하지 않는다.
  서버는 컨트롤러마다 복구 시도를 1초 간격으로 제한한다.
- 온라인 UI는 같은 계정·캐릭터·현재 멤버십과 정확한 `InProgress` 패배에서만 재도전을 허용한다.
  현재 세션의 지급 완료 안내·진행 중 HTTP/재로그인·포기 미확정·로비 이동은 재도전을 막는다.
  이 검사는 표시/입력 가드이며 서버 권한 검사를 대체하지 않는다. 비영속 직접 IP/오프라인
  재도전은 백엔드 로그인을 요구하지 않는다.
- 패배에서는 보상 정산을 기다린다고 표시하지 않는다. 입장 시 남은 `Loading` 캐시를 갱신하기
  위해 인증된 세션을 한 번 조회한다. 승리의 120초 정산 폴링과 정상 복귀의 최신 완료 검증은 유지한다.
- 패배 화면의 별도 로비 이동은 **세션/멤버십 유지 연결 종료**다. 백엔드 포기/완료 요청이나
  보상 변경을 하지 않는다. 실제 플레이 중 포기 버튼은 아직 노출하지 않으며 개발 로비 전용
  가드는 유지한다. 파티원의 서버 재도전은 기존처럼 참가자 한 명이 요청하는 정책이다.

검증:

- Editor 타깃 최종 빌드 8개 작업·DLL 링크 성공. 첫 빌드의 `Player` 지역 변수/엔진 멤버
  이름 충돌은 수정했고 실패 로그를 보존했다. 새 테스트 파일도 빌드 목록에 포함했다.
- 집중 native 7개와 러너 종료 0: `Boss.BattleRetry`, `Boss.BattleRetryRequest`,
  `Boss.Presentation`, `Online.DungeonResult`, `Online.DungeonResultAuthentication`,
  `Online.DungeonResultDefeat`, `Online.DungeonResultWidget` (모두 `ProjectRPG.` 접두사).
  테스트 구간 Error/Fatal/Assert/Ensure 0, 기존 기동 Error 15줄은 남는다.
- 동일 DLL의 `Run-BossMapE2ETest.ps1 -DeathAbilities -SkipSDKSetup` 종료 0.
  로컬 Editor 전용 서버 역할/Iris 클라이언트 2개에서 저장된 BP 사망 연출·패배 결과·
  실제 Retry 디스패처/운영 RPC·중복 요청 차단·새 보스·같은 Pawn의 체력/이동/입력·
  캡슐/애니메이션 복구를 확인했다. 네트워크 Error/Fatal/Assert/Ensure/fixture 실패 0,
  기존 클라이언트 `LogUIActionRouter` Error는 각 1줄이다. 엄격 오류 0 감사 통과는 아니다.
- 증거: `Saved/Tests/DefeatRetry-d8fb9051/Verification.proof.json`,
  `Saved/Tests/BossMap/6f79e0878ff143eda53a5dc101e518d4/BossMap.proof.json`.
  최종 빌드 전후 선택 입력 14개와 테스트 전후 DLL이 일치했으며 결과 BP 에셋은 변경하지 않았다.

이 통합 실행은 비영속 직접 IP 재도전이다. 온라인 패배 패널의 실제 HTTP/DB·세션 유지
로비 이동, 실제 포기, Steam/다른 PC/Shipping/GPU 배치는 이번 통과 범위가 아니다.
다음 기능은 게임 중 본인 포기의 확인 UI·미확정 응답 복구와 남은 파티의 계속 진행/재도전 연결이다.

## PIE 로비 로드의 복귀 완료 판정 (2026-10-27)

실제 렌더링 2인 클라이언트에서 로비가 로드돼도 `ReturningToLobby`가 해제되지 않던 경로를
수정했다. `HandleLobbyMapLoaded`가 같은 GameInstance·ReturningToLobby·설정한 목적지를
확인할 때 **PIE 월드만** 엔진의 `UWorld::RemovePIEPrefix`로 `UEDPIE_<id>_`를 제거한다.
일반 Game 월드의 이름 비교와 기존 FString 대소문자 규칙은 유지한다. 서버 주소/티켓을
이어받거나 HTTP 완료 검증·세션/선택 보존·동의/멤버십 정책을 바꾸지 않는다.

정상 Editor 컴파일/링크·타깃과 집중 `ProjectRPG.Online.DungeonPIELobbyCompletion` 1개가
성공했다. 해당 native는 잘못된 맵/인스턴스·일반 Game의 PIE 이름을 거절하고 올바른 PIE
완료·선택/완료 세션 보존을 다룬다. 초기 잘못된 대소문자 가정의 native 실패와 DLL 잠금
로그는 보존했다. 최종 증거는 `Saved/BuildChecks/PartyHudVitalsRuntime-20261027-014438/`의
`PIELobbyCompletion-Final.proof.json`이다. 기동 기존 Error 15줄과 테스트 성공을 구분한다.

같은 DLL의 개발 인증/PostgreSQL·80~160ms/5% 손실 2인 실행은 실제 피격/전투 불능·단절/
새 티켓 재접속·정산 뒤 양쪽 로비 로드/Standalone/Idle·인증/선택 보존·티켓 제거까지
통과했다. 원래 무기 보존과 각자 골드 500·가방 보상 수량 4/우편 0도 확인했다.
증거: `Saved/Tests/DungeonMembershipBackend/b0fdfdb55fb9441fba4f7e4a9f6d05de/DungeonReconnectWithinGrace.proof.json`.
파티 HUD 화면 9개와 실패/최종 실행의 구분은 [HUD 기록](DUNGEON_PARTY_COMBAT_HUD.md#실제-피격전투-불능-관측과-pie-복귀-후속-2026-10-27)을 따른다.
자연 전투·물리 입력·10분 자연 만료·Steam/다중 PC/Shipping 검증은 아니다.

## 결과 화면의 키보드·패드 탐색 (2026-10-29)

네이티브 결과 조작에 로비의 기존 탐색 코드를 재사용했다. 표시·활성 조작 전체를 UMG
트리 순서의 한 사이클로 연결하고 Tab/역 Tab·상하와 버튼의 좌우를 처리한다. 숨김/비활성
조상도 확인하며, 사용할 버튼이 하나 또는 없으면 Stop으로 결과 패널 안에 머문다.
확인창이 열리면 확인/취소 범위만 탐색하고, 확정이 비활성이면 취소만 남는다.
닫힌 확인창은 일반 순환에 들어오지 않는다. 기존 취소 우선 기본 포커스는 유지한다.

공통 `ConfigureButtonRows`는 버튼에만 일반 좌우 순환을 적용한다. 기존 로비용
`Configure`의 계약/심볼과 일반 공간·텍스트 caret 이동은 유지했다. 결과 화면은 기존
`RefreshPresentation` 끝에서 탐색 메타데이터만 갱신하며, 커스텀 BP root는 기존 설정을
유지한다. 추가 포커스 이동·타이머/입력 모드·클릭/동의 dispatch·서버 요청은 없다.
버튼의 표시/활성 권한, 재도전·보상·정산/복귀·세션 유지 이동 판정도 변경하지 않았다.

새 `DungeonResultNavigation.NativeControls`는 실제 네이티브 UMG/Slate 트리에서 순서/
순환·가용성 변경·숨김 조상·한 개만 남은 경우·확인창 범위·멱등 설정·커스텀 root·입력값/
콤보 선택/caret 설정 보존과 presentation 연결을 검사한다. 조작 가용성/확인창 상태는
UI fixture로 구성하며 인증된 결과 흐름, 실제 키/사용자 포커스나 클릭의 증거는 아니다.

CPP 3개 개별 CL/분리 링크 종료 0. 최초 실행은 기존 `DungeonResultWidget`/
`DungeonLobbyNavigation.OrderAndAvailability` 2개 Success, 새 사례의 마지막 기대 1개
Fail이었다. `InitializeResult`는 모드만 선택한다는 계약에 맞춰 테스트에서 기존
`RefreshPresentation`을 명시 호출하도록 한 줄 보완했다. 제품 객체는 그대로 두고 시험
객체만 재컴파일/분리 링크한 뒤 **수정 사례 1개 Success·UE/호스트 종료 0**을 확인했다.
통과한 기존 2개는 반복하지 않았으며 단일 최종 DLL의 전체 3개 PASS와 구분한다.

proof: `Saved/BuildChecks/ResultInputNavigation-20261029/Verification.proof.json`,
최종 실행: `TestRefreshFix/Native-456e9c28aecd4b078ceab4a858c9ad75/Native.proof.json`.
선택 소스 12개/선택 고정 입력 800개·실제 로드 DLL은 불변이고, 442개 링크 객체 중
테스트 보완으로 바뀐 것은 새 시험 객체 하나뿐이다. 컴파일 경고 0·기동 Error 15줄/
검사 이후 Error 0, 전체 러너 입력/필수 이름 각 1개·PS AST 오류 0을 확인했다.
원본 DLL/Intermediate·열린 GUI/MCP·HUD/훈련·에셋/Config/backend/DB와 다른 담당은
유지했다. 정상 공유 모듈 반영·실제 결과/확인창의 키보드·패드/GPU·Steam/Shipping은
남으며, 이전 보상/네트워크/600초·전체 native 검사는 반복하지 않았다.

### 검증 임시 사본 폐기 (2026-10-30)

새 공통 지침에 따라 본인 `Saved/BuildChecks/ResultInputNavigation-20261029/`의
Inputs/Objects·분리 DLL/PDB·RuntimeProject/UserDir/캐시를 폐기했다. 게임 소스·유지할
검증 스크립트·작은 결과/해시·필수 실패/최종 로그는 보존했다. 위의 proof와 해시는 당시
실행의 증거로만 취급하며, 삭제한 같은 바이너리의 재실행/고정 입력 재감사는 불가능하다.
필요한 다음 검사는 새 정상 빌드/입력으로 진행한다. 다른 본인 검증 9개를 포함해
25.363 GiB·일반 파일 3,258개 삭제/링크 34개 대상 미추적 해제, 보존 233개·원본 보호
7개 해시 동일/보존 예외 없음이다. 원본 DLL/Intermediate·에셋·타 담당을 유지했고
게임 빌드/실행은 하지 않았다. 기록: `Saved/Maintenance/DungeonPartyTempCleanup-20261030/Result.json`.

## 훈련 완료 결과·복귀 후보의 C++ 확인 (2026-10-31, 원본 미반영)

훈련 GameMode는 기존 정산을 보고하지만 Controller의 결과 조작 생성은 보스 결과 UI를
전제로 한다. 개인 실습 네 목표를 끝낸 뒤 결과/복귀 조작이 없는 부분을 guide에서 기존
`URPGDungeonResultActionsWidget`으로 연결하는 후보를 준비했다. 후보 위치는
`Saved/BuildChecks/TrainingCompletionActions-20261031/Candidate/Source/Project_RPG/`의
`{Public,Private}/UI/Content/RPGTrainingGuideWidget.{h,cpp}` 두 파일이다.

후보의 동작 계약은 다음과 같다. 아래는 구현한 후보의 의도이며 UE 실행으로 확인한 결과가 아니다.

- 로컬 네트워크 클라이언트의 현재 살아 있는 소유 Pawn, 표시 중인 guide, 정확한 인증 계정/
  선택 캐릭터/PlayerState/훈련 Normal 세션·멤버십을 대조한다.
- 개인 실습 완료는 비패배 결과 조작을 한 번 표시하는 계기일 뿐 clear/보상 지급 판정이 아니다.
  기존 결과 위젯의 정산 GET·인증 복구 및 복귀 클릭의 별도 fresh terminal GET을 재사용한다.
  `InProgress`/`SettlementPending`에서 개인 완료만으로 정상 복귀를 허용하지 않는다.
- Pawn/계정/캐릭터/세션·guide 수명이 달라지면 자기 결과 위젯을 제거한다. 제거 전에 자기
  포인터를 비우고 강한 참조로 보호하며, 자기 UI가 사용자 포커스를 가졌을 때만 입력 모드를
  복구한다. 새로운 정산/RPC/포기 API, 가짜 보스 승리 상태나 직접 맵 이동은 추가하지 않는다.

선행 `airborne-platform-network-20261031`의 전체 Source/공유 검증 입력 고정과 소비 접합
대기를 유지했다. **원본 Source 두 파일과 공유 DLL은 수정하지 않았다.** 기존 MSVC 명령을
별도 rsp로 펼쳐 후보 Public include를 우선하고 PCH를 끈 뒤 모든 출력은 자기 Temp로
재지정했다. 최종 후보 CPP 한 개의 개별 객체 컴파일은 종료 0·오류/경고 0이며, 후보/원본/
rsp·실제 사용 프로젝트 헤더 등 80개 입력의 전후 해시가 같았다. 엔진 헤더 전체를 고정한
스냅샷은 아니며 UBT/UHT/링크/native·실제 UI/로비 왕복 검증도 아니다. 새 reflected 멤버가
있으므로 원본 적용 후 UHT/정상 모듈 생성이 반드시 필요하다. 이 객체를 공유 모듈에 링크하지 않는다.

첫 두 준비 시도는 PS5 JSON 배열 취급/상대 include 기준 경로에서 컴파일 실행 전에 실패했다.
첫 컴파일은 추가 전방 선언이 기존 GENERATED_BODY 줄 번호를 바꿔 생성 매크로와 불일치했다.
UE의 `TObjectPtr<class ...>` 전방 선언 형태로 후보만 보완해 기존 매크로 위치를 유지했다.
생성 헤더를 편집하거나 UHT 성공으로 간주하지 않았다. 이후 실제 개별 컴파일 통과를 구분해
보존했으며 최종 패치는 99줄 추가/1줄 제거, 문맥/공백 `git apply --check` 종료 0이다.

근거는 `Saved/BuildChecks/TrainingCompletionActions-20261031/CompileSummary.proof.json`,
최종 상세는 `Compile/a2c9e94a884040648c9859ca57ab4847/Result.json` 및 같은 디렉터리의
작은 컴파일 로그/명령/입력 해시다. 각 시도 종료 후 자기 compiler 사용 해제·최신 예약·절대
경계/reparse 검사를 거쳐 임시 객체/rsp/진단/의존성 총 **51,258,259 bytes(48.88MiB)**를
자동 폐기했다. 잔여 실행 디렉터리/정리 예외는 0이며 작은 후보/패치와 결과만 보존한다.
삭제한 객체의 SHA256은 당시 증거일 뿐 같은 객체의 재링크/재감사가 가능한 상태는 아니다.

다음은 선행 담당 종료/인계 후 원본 해시/문맥 재확인·별도 예약/적용이다. 이어 완료 후 진행
복제/성장 세대 변화, 인증·HTTP 실패, 다른 신원/세션, Pawn 사망/교체·HUD 종료/포커스와
정산 전 복귀 차단을 확인하고 실제 로비 입장→네 훈련 입력/XP→결과 버튼으로 로비 복귀를
연결해야 한다. `ONLINE_LOBBY_UI.md`는 훈련 런처 담당의 후속 링크 예약 때문에 이번에 미편집했다.

### 기존 후보의 적용 담당 인계 (2026-10-31)

위 절은 후보 준비/개별 컴파일 당시 기록이다. 후속 `training-completion-install-20261031`
담당이 기존 후보 재사용과 원본 guide/직접 영향 테스트·공유 빌드·정상 설치 및 실제 완료 UI
연결을 예약해 진행 중임을 조율표로 확인했다. 원본 guide h/cpp에도 `CompletionActions`,
`RefreshCompletionActions`, `ClearCompletionActions`와 새 완료 안내 호출이 존재한다.
이는 코드 존재의 확인이며 UHT/링크·정상 DLL·실제 훈련 왕복 성공의 확인은 아니다.

따라서 `training-completion-actions-20261031`의 **다음 원본 적용 대기는 해당 담당 진행으로
전환**한다. 별도 인계 없이 같은 기능/패치를 재적용하거나 재컴파일하지 않고, 완료 상태와
남은 범위는 적용 담당의 최종 결과 및 `ONLINE_LOBBY_UI.md` 후속을 따른다. 이번에는 이
계약의 인계 후속과 본인 조율 행만 수정했으며 게임 코드/빌드/실행·다른 예약은 변경하지
않았다. 임시 산출물 생성/삭제는 0 bytes이고 정리 예외도 없다.

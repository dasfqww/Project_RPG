# 첫 성장 구간의 조작 훈련 목표

작성일: 2026-10-29  
상태: **개인 목표·훈련 에셋·GameMode 연결과 정상 모듈 반영 후 실제 1인 입력/목표 복제·완료 정산·XP 저장 통과.** 2~4인·Steam/운영 DB·GPU/물리 입력·로비 공개는 후속이다. (2026-10-30 갱신)

현재 파티 집계와 부모 전환 근거는 [전용 GameMode의 적용 기록](PLAYER_PROGRESSION_TRAINING_COMPLETION.md#2026-10-30-컴파일bp-전환과-독립-재로드-완료)을 따른다. 아래 에셋 배치 표와 마지막 계획은 최초 작성 시점의 기록이다.

[훈련 해금 순서](PLAYER_PROGRESSION_CONTENT_INTEGRATION.md#32-훈련-중-고유-능력-사용과-해금-순서)에 맞춰 처음부터 사용할 수 있는 네 동작을 관찰하는 [URPGTrainingObjectiveComponent](../Source/Project_RPG/Public/Component/Progression/RPGTrainingObjectiveComponent.h)를 추가했다. 기존 스킬·기본 공격·회피·ASC·GameMode의 동작은 변경하지 않았다.

## 이번 조작 훈련의 완료 조건

| 목표 | 기록 조건 |
|---|---|
| 기본 공격 | 현재 부여된 대검 기본 공격 Ability를 관찰한 뒤 GAS 정상 종료 1회 |
| 회피 | 현재 부여된 회피 Ability를 관찰한 뒤 GAS 정상 종료 1회 |
| 돌진 | 클래스가 소유한 돌진 시전의 커밋과 같은 활성화의 정상 종료 1회 |
| 차지 | 커밋 당시의 실제 차지 정책이 최대 충전 상태로 해제됐고, 같은 활성화가 정상 종료 1회 |

순서는 자유이며 각 목표는 한 번만 기록한다. 회전·고유 능력·도약의 사용은 요구하지 않는다. 네 목표가 모두 참이면 `IsControlPracticeComplete()`가 참이다. 이는 **개인 조작 실습 완료**이며 던전 완료·수령 자격·경험치 지급 영수증이 아니다.

이번 단계는 적중을 요구하지 않는다. 기본 공격 연계 전체 적중·회피로 공격 피하기·돌진 접근 거리·차지의 실제 완충 피해를 증명하는 관찰기가 아니다. 이 결과만으로 A1~A3나 자연 전투 숙련까지 완료 처리하지 않는다.

## 서버 관찰과 수명

- 컴포넌트를 배치한 `ARPGPlayer`에서만 동작한다. 서버가 `Progression.Training`/Normal 입장을 확정하고, 해당 세션의 인증된 캐릭터·연결 세대와 영속 클래스/스킬 복원이 준비돼야 관찰을 시작한다. 개발용 가변 구성이나 다른 콘텐츠에서는 비활성이다.
- 0.25초 간격으로 준비 상태를 확인한다. ASC의 기존 활성화·커밋·종료 delegate를 구독하며, 구독 전에 시작된 시전을 완료 기록으로 소급하지 않는다. 클라이언트 완료 통보 RPC나 Blueprint 목표 설정 함수는 없다.
- GAS가 `bWasCancelled=true`로 보고한 시전, 없는/회수된 grant, 다른 Pawn/ASC/PlayerState/Controller·세션/캐릭터/연결 세대·스킬 복원 세대, 사망/재사용된 Pawn의 늦은 종료는 진행으로 받지 않는다. 기존 수명 검사를 재사용하며 자기 delegate와 관찰 기록만 정리한다.
- 돌진/차지는 커밋 시 활성화 번호를 저장한다. 차지는 실제 시전 정책을 약참조로 기록하고 정상 종료에서 그 정책의 완충 해제 사실을 읽는다. 기본 차지의 부분 해제를 허용하는 설정도 사용할 수 있으며 부분 해제 자체는 실습 완료로 인정하지 않는다. 종료 전 RuntimeSpec 초기화를 고려해 종료 시 새 설정을 다시 해석하지 않는다. [기본 차지 보완과 검증 범위](PLAYER_PROGRESSION_TRAINING_CHARGE.md)를 따른다.
- 같은 Ability 인스턴스가 이전 종료 알림 중 다시 활성화됐다면 이전 알림으로 새 시전을 완료하지 않는다. 종료 기록을 진행 알림 전에 제거하며 완료된 목표의 반복 종료는 추가 진행을 만들지 않는다.
- Pawn/인증/복원/전투 수명이 바뀌면 이 컴포넌트의 일시적인 실습 상태를 비운다. 체력·투지·쿨타임·회복약·영속 경험치/SP는 바꾸지 않는다. 재접속을 새 전투 시도로 판정하거나 과거 실습을 복원하는 정책은 이번에 추가하지 않았다.

정상/취소의 구분은 기존 GAS 종료 보고에 의존한다. 개별 Blueprint가 중단을 정상 종료로 보고하는지, 실제 애니메이션·입력 해제와 종료 알림이 일치하는지는 플레이 연결 뒤 확인해야 한다. 이번 컴파일을 실제 취소/완충 동작 검증으로 표시하지 않는다.

## 표시와 정산의 연결 경계

`FRPGTrainingObjectiveProgress`는 활성 여부와 네 목표의 bool만 저장한다. 소유자에게만 복제하도록 구현했고 `OnTrainingProgressChanged`·`GetTrainingProgress()`로 읽는다. 후속으로 [조작 안내 HUD](PLAYER_PROGRESSION_TRAINING_GUIDE.md)의 읽기 전용 표시와 로컬 HUD 생성/회수를 연결했다. 아래 훈련 전용 Pawn/맵에도 참조를 배치했으며 서버 목표 관찰의 실제 실행·OwnerOnly 복제 전송·해당 맵에서의 HUD 표시는 아직 별도다.

컴포넌트는 경험치·SP·스킬을 지급하거나 GameMode의 완료 보고를 호출하지 않는다. 후속 [훈련 GameMode](PLAYER_PROGRESSION_TRAINING_COMPLETION.md)가 권위 서버에서 참가자별 실습 상태와 명부를 읽고 기존 `ReportConfiguredDungeonClear()`에 연결하도록 구현했으며 해당 BP 부모 전환도 완료했다. 실제 목표 승인/XP 정산은 후속이다. 클라이언트 표시값이나 최초 접속자 한 명의 완료만으로 파티 보상을 확정하지 않는다.

최초 소스 구현에서는 에셋을 편집하지 않았다. 이후 기존 `BP_Player`를 상속한 훈련 전용 자식에만 컴포넌트를 배치하고 새 맵/GameMode/서버 정의를 작성했다. [작성된 로비 정의](PLAYER_PROGRESSION_DUNGEON_ASSETS.md)와 다른 담당자의 [경험치 전용 보상 경계](DUNGEON_REWARD_INVENTORY_FIRST.md#경험치-전용-완료의-작성-경계-2026-10-29)를 재사용한 범위는 아래와 같다.

## 확인한 범위

[구현 cpp](../Source/Project_RPG/Private/Component/Progression/RPGTrainingObjectiveComponent.cpp)와 새 헤더 두 개만 게임 소스로 추가했다. [ImplementationResult.json](../Saved/BuildChecks/ProgressionTrainingObjectives-20261029/ImplementationResult.json)과 같은 폴더의 `Compile.log`가 근거다.

- UHT 성공·생성 파일 3개, 새 CPP 1개의 `-FileList -NoLink` 컴파일과 프로세스 종료 0. 로그의 총 시간은 42.63초다.
- 공유 게임 DLL 링크/핫리로드·에셋 배치·PIE/native/HTTP/DB·실제 복제/게임 입력 검사는 실행하지 않았다.
- 선택한 보존 대상 24개는 컴파일 전후 해시가 같다. 기존 ASC·스킬·공격·GameMode·PlayerState·수명 코드, 플레이어 능력/HUD 에셋·Config/backend 성장 규칙/기본 설정·공유 DLL을 포함한다. 다른 담당의 별도 변경까지 없었다는 의미는 아니다.

위 내용은 최초 소스 컴파일의 증거다. 후속 에셋 배치의 증거와 실행 한계는 아래에 구분한다.

## 훈련 전용 콘텐츠 배치 — 2026-10-29 작성, 10-30 인계

다음 네 에셋을 새로 저장했다. 기존 플레이어 BP·일반/보스 GameMode·보상 정의·공용 HUD는 편집하지 않았다.

| 에셋 | 연결 내용 |
|---|---|
| `Content/Blueprints/Character/Player/Progression/BP_TrainingPlayer.uasset` | 기존 `BP_Player`의 자식. 시작 데이터 상속을 유지하고 복제되는 `URPGTrainingObjectiveComponent` 한 개 추가 |
| `Content/Blueprints/GameMode/Progression/BP_TrainingGameMode.uasset` | 네이티브 `ARPGGameModeBase`의 자식. 훈련 Pawn과 기존 Controller/PlayerState/GameState/HUD 지정 |
| `Content/Maps/Progression/L_ProgressionTraining.umap` | 훈련 GameMode 지정. 80m 정사각형 바닥·벽 4개·PlayerStart 4개·조명 2개·Experience 선택자 1개 |
| `Content/System/Dungeons/Progression/DA_DungeonServer_Training.uasset` | `Progression.Training`/Normal → 위 맵과 기존 XP 전용 `DA_DungeonReward_ProgressionTraining` 매핑 |

훈련 GameMode는 보상 보고를 허용하는 `bGiveReward=true`와 Normal을 사용하고 서버 정의 목록에는 새 훈련 정의만 둔다. 기존 JoinTicket·Steam 신원 일치 요구를 유지한다. 보스 GameMode를 상속하지 않으므로 보스 스폰 로직을 훈련에 가져오지 않는다. 맵은 기존 `DA_ClientPresentationAndOptionsHint`를 명시적으로 로드한다. 현재 목표는 적중을 요구하지 않으므로 표적/몬스터는 배치하지 않았다.

XP 전용 보상은 다른 담당자가 작성한 `FirstSlice.v1`/재화·아이템 0 정의를 읽기 참조했다. 이 최초 매핑은 **보상 작성 경로의 준비**였으며 실제 완료나 XP 지급을 실행한 결과는 아니다. 당시 없던 파티 완료 집계/완료 보고는 위 전용 GameMode 후속으로 연결했다. 로비 공개 목록·backend 카탈로그·기본 맵 설정은 그대로다.

### 작성과 재로드 근거

[CreateProgressionTrainingContent.py](../Scripts/Editor/CreateProgressionTrainingContent.py)는 기본 읽기 전용이며 `-RPGCreateProgressionTrainingContent`를 명시해야 누락된 대상만 만든다. 이미 있는 대상은 설정/소유 마커/dirty 상태를 확인하고 보존한다. 사용자 설정이 다르면 실패하며 덮어쓰지 않는다. `RPG_TRAINING_CONTENT_REPORT`에 결과 JSON의 절대 경로를 지정한다. 재실행 전에는 목표 컴포넌트·훈련 안내·XP 전용 검증이 포함된 모듈이 필요하다.

[ImplementationResult.json](../Saved/BuildChecks/ProgressionTrainingContent-20261029/ImplementationResult.json)에 최초 저장·반복 작성·새 프로세스 재로드를 구분했다.

- 최초 작성에서 네 에셋 저장. 이후 반복 작성/독립 읽기 전용 재로드는 각각 종료 0·저장 0이며 네 파일의 해시와 보고된 속성이 모두 같다. 대상 패키지의 dirty 검사도 통과했다.
- 두 BP는 `BS_UP_TO_DATE`, 서버 정의·GameMode CDO·XP 보상은 `Valid`/검증 오류·경고 없음이다. 바닥/벽 충돌과 시작점 4개 등은 저장된 속성으로 확인했다. 엔진 로그의 기존 경고와 데이터 검증 경고를 구분해 보존했다.
- 처음 실행한 PowerShell 래퍼는 native 저장/종료 요청 0 뒤 OS 종료 코드를 수집하지 못했고 중복 Console 로그의 잠금으로 정리 일부가 실패했다. 실패 기록을 남기고 래퍼를 고친 뒤 실제 두 프로세스 종료 0·최종 정리 완료를 확인했다.
- 재개 시 보호 대상 24개 중 23개는 최초 해시와 같다. 공용 `WBP_PlayerHUD` 한 개의 변경은 별도 [스킬바 모듈화](PLAYER_HUD_MODULARIZATION.md) 기록이 있는 외부 변경으로 보존했으며 이 작업에서 편집하지 않았다. 현재 HUD를 포함한 전체 런타임 검증으로 확대하지 않는다.

공유 UHT/UBT/Intermediate를 사용하지 않고 완료된 TrainingGuideHUD 모듈의 자체 사본(`775B33F0…9593CB`)으로 작성했다. 원본 게임 DLL(`AA6C9CB0…0127810`)은 그대로다. 따라서 정상 공유 모듈의 새 타입 반영·PIE/실제 입력·목표 승인/복제·파티 정산·GPU 화면 검증은 완료로 계산하지 않는다.

자체 RuntimeProject/UserDir·DLL 사본/캐시는 종료 후 **16,770,128 bytes(약 16MiB)** 폐기했다. Content/Plugins junction은 링크만 해제했고 원본 대상을 보존했다. 중복 로그도 1,324,072 bytes 축소/정리해 측정 합계 **18,094,200 bytes(17.26MiB)**를 폐기했으며 잔여 임시 경로/예외는 없다. 작은 결과·명령·해시·필수 로그만 남겼다. 이 해시는 당시 증거이며 폐기한 모듈의 재실행/고정 입력 재감사에는 새 사본 생성이 필요하다. 다른 담당자의 원본 검증 DLL은 삭제하지 않았으며 이번 인계에서 읽기 예약을 해제한다.

### 다음 연결

1. 기존 멤버십/수령 대상 명부와 개인 목표를 권위 서버에서 집계한다. 미입장자·재접속 유예·확정 포기·Pawn 교체의 정책을 기존 계약과 맞추고 첫 완료자만으로 파티 완료를 확정하지 않는다.
2. 집계 조건이 성립하면 기존 `ReportConfiguredDungeonClear()`와 XP 정산을 한 번 연결한다. 개인 실습 bool을 영속 경험치나 완료 영수증으로 대체하지 않는다.
3. 공통 파일 담당 종료/인계와 정상 모듈 반영 뒤 로비/backend 등록을 진행한다. 실제 목표 인정·소유자 안내/복제·XP 지급과 재시도는 그때 연결 범위에 맞춰 확인한다.

## 2026-10-30 실제 1인 조작·정산 확인

기본 공격 에셋의 대검 exact 분류 태그 누락과 회피 에셋의 옛 `Player.Roll` 분류를 보완했다. 기존 범용 공격 태그·몽타주/콤보·입력/네트워크 기본값을 유지하고 두 에셋의 분류 태그만 저장했다. 개인 목표 컴포넌트나 서버의 정상 종료/완충/인증·수명 조건은 완화하지 않았다.

설치된 정상 모듈의 자기 사본과 개발 인증/Memory·HTTP 티켓/Iris로 실제 훈련 맵에서 기존 입력을 수행했다. 서버/소유자 모두 **0→1→3→7→15**, 세션 Cleared, 경험치100/레벨2/SP3와 회전 베기·투지 해방 해금을 확인했다. 완료 뒤 반복 조회와 동료 캐릭터 불변도 통과했고 UE 두 프로세스는 종료0, probe 이후 Error0이다. 실제 안내 HUD의 GPU 표시·적중/자연 전투·2~4인 전원 완료 검사는 아니다.

[원인·작성/반복 저장·최종 proof와 정리](PLAYER_PROGRESSION_TRAINING_PLAYTEST.md#2026-10-30-기본-공격회피-태그-보완과-1인-전체-흐름-통과)를 따른다. 현재 로비/운영 목록 공개는 여전히 별도이며 위 초기 작성 시점의 미연결 계획과 구분한다.

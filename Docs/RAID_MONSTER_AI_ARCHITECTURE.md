# Multiplayer Raid Monster AI Architecture

상태: 서버 권위 SwordMan GAS/BT 공격·복제 예고 기반 로컬 장판·보스전 세션/재시도 구현. 실제 bossmap의 자연 감지→BT 접근→GAS 피해 및 Iris 이동/예고/HP 복제는 전용/리슨 서버에서 통과했다 (28절). 실제 플레이어 GAS 사망→기존 Pawn의 HP·충돌·애니메이션·입력 준비 복구도 통과했다 (27절). 서버 시험용 GE의 HP 임계치 자극으로 페이즈 전환·기존 공격 취소/지연 피해 부재·자연 HeavyStrike와 복제를 검증했다 (29절). 원격 플레이어의 서버 구르기 방향 오류를 수정하고 실제 Enhanced Input→GAS/AM_Roll→이동 승인/정상 종료→Strike 공간 이탈/HP 불변과 두 클라이언트 복제를 전용/리슨 서버에서 통과했다 (30절). 실제 플레이어의 이동/기본 콤보 입력→운영 무기 loadout/몽타주→서버 overlap/GAS 보스 피해·위협도 증가→두 Iris 클라이언트의 무기/HP 복제도 전용/리슨 서버에서 통과했다. 장비는 서버 메모리 fixture이며 백엔드 획득/장착 증거는 아니다. 보스 자동화 12종, 공통 보안 자동화 38종과 최종 전용 서버 구르기 회귀도 통과했다 (2026-10-08, 31절). 서버 스윕 보강 후 단일 기본 스윙은 전용/리슨 30·15FPS에서 통과했으며, 15FPS 양쪽에서 실제 overlap 없이 sweep→GAS 피해/threat/두 클라이언트 복제를 확인했다. 같은 DLL의 보스 13종/장비 충돌 3종과 기존 3연타 콤보 회귀도 통과했다 (2026-10-09, 33절). Pawn 소실 검사는 테스트용 이동 전송 정지 장벽을 적용한 별도 범위이다 (26절). 돌발 Pawn 파괴 중 이동 RPC 경합·다른 플레이어 스킬/AOE·백엔드 장착부터의 연속 전투·구르기 무적 프레임·착용 장비/래그돌 복구·보스 몽타주/Cue·GPU·패키징·지연/손실 검증은 남아 있다.  
최신 진행 (2026-10-10): 위의 다른 플레이어 스킬/AOE 범위 중 정책 기반 보스 피해 연결은 네이티브 52종으로 검증했다 (34절). 실제 JumpSmash AOE 입력·ground targeting·몽타주·서버 HitQuery/GAS 피해/threat·두 원격 Iris 클라이언트의 몽타주/무기/HP 복제를 전용·리슨 서버에서 통과했다. 같은 DLL의 보스 14종과 기본 공격 단일 스윙 회귀도 통과했다 (35절). 이어서 AssultBlade의 실제 원격/호스트 입력·soft targeting·서버 query/GAS 보스 피해·threat·두 원격 몽타주/무기/HP 복제를 전용·리슨 서버에서 통과했으며, 같은 새 DLL의 보스 14종과 전용 JumpSmash 회귀도 통과했다 (36절). ChargeSlash도 실제 원격/호스트의 차지 유지·입력 해제·서버 query/GAS 피해·threat·정상 종료·두 원격 Iris 관찰을 전용·리슨 서버에서 통과했다. Rage 선행 태그/보스 2500HP는 시험 fixture이며 실제 Rage 발동·보스 처치/보상 검증은 아니다 (37절). 공통 검증과 스킬별 설정/구현 읽기 어댑터를 1차 분리했고 해당 DLL의 보스 자동화 15종을 통과했다 (38절). WhirlWind 및 구형 비정책 AOE, 백엔드 연속 전투·Shipping/GPU·지연/손실 검증은 남아 있다.

범위: Project_RPG 런타임 기준  
참고: D1_SourceCode/D1Game은 직접 복사할 대상이 아니라 구조와 구현 패턴을 참고하는 별도 모듈이다.

최신 추가 (2026-10-11): WhirlWind의 실제 유지 입력·반복 타격·수동 중지·정상 종료 뒤 추가 피해 부재 및 두 원격 Iris 관찰을 전용/리슨 서버에서 통과했다. 채널 관찰 어댑터를 분리했고, 같은 최종 DLL의 보스 자동화 15종과 전용 ChargeSlash 회귀도 통과했다 (39절). 장비는 메모리 fixture이며, 자동 종료/돌발 취소·구형 비정책 AOE·실제 Rage 발동·백엔드 연속 전투/처치/보상·Shipping/GPU·지연/손실은 별도 남은 범위다. 이전 날짜/DLL의 통과를 새 DLL의 회귀 결과로 대신하지 않는다.

후속 추가 (2026-10-11): 구형 AOE 호환 코드의 서버 보스 피해·threat 연결과 전투/시전자/종료/재활성화 경계 검사를 구현했다. 보스 자동화 16종·스킬 피해 보안 15종 및 같은 DLL의 전용 JumpSmash Iris 회귀를 통과했다 (41절). 구형 AOE의 실제 운영 입력·몽타주·멀티플레이 검증을 완료했다는 뜻은 아니다. 현재 저장된 운영 스킬 4개는 이미 정책 기반이며, 구형 스킬을 새로 부여하거나 에셋을 되돌리지 않았다.

## 1. 목표

서버 권위 멀티플레이 환경에서 다음을 만족하는 레이드 몬스터 기반을 만든다.

- 여러 플레이어를 대상으로 하는 어그로와 타겟 선택
- HP 및 이벤트 기반 페이즈 전환
- Behavior Tree 기반의 이동·판단·패턴 선택
- Gameplay Ability 기반의 공격·소환·기믹
- 보스 상태와 전투 결과의 안정적인 복제
- 전멸, 리셋, 재시작, 늦은 참가자 동기화

## 2. 현재 코드에서 재사용할 기반

### Project_RPG

- Source/Project_RPG/Private/Controller/RPGAIController.cpp
  - AI Perception, TargetActor 블랙보드 갱신, Crowd Following
- Source/Project_RPG/Private/AI/BTTask_RotateToFaceTarget.cpp
  - 타겟을 향한 회전 Task
- Source/Project_RPG/Private/AI/BTService_OrientToTargetActor.cpp
  - 타겟 방향 회전 Service
- RPG의 AbilitySystem, Attribute, Health, NPC Combat 계층
- Content/Blueprints/Character/NPC 아래의 기존 BT, AIC, Blackboard, EQS 에셋

### D1_SourceCode

- D1_SourceCode/D1Game/AI/D1MonsterAIController.cpp
  - 팀 태도, Perception, Crowd Avoidance, PlayerState 연동
- D1_SourceCode/D1Game/AI/D1MonsterComponent.cpp
  - Game Framework Init State와 AbilitySystem 초기화
- D1_SourceCode/D1Game/Data/D1MonsterData.cpp
  - AI Controller 클래스와 PawnData 매핑
- D1_SourceCode/D1Game/Actors/D1TargetPointMonster.cpp
  - 몬스터 스폰, Controller Possess, PawnData 주입

D1 코드는 D1Game, ALyraPlayerState, ULyraPawnExtensionComponent 등에 의존하므로 현재 RPG 코드에 그대로 포함하지 않는다.

## 3. 권장 런타임 흐름

    Encounter/Spawn
        -> Boss Pawn + AIController
        -> BossAIComponent 초기화
        -> 서버 Perception/Threat 갱신
        -> 서버 Behavior Tree 판단
        -> Gameplay Ability 활성화
        -> 서버 Attribute/GameplayEffect 처리
        -> Replicated State + GameplayCue/Event
        -> 클라이언트 UI, 텔레그래프, 애니메이션, VFX

Behavior Tree는 의사결정과 실행 순서를 담당한다. 페이즈, 어그로, 피해, 소환, 리셋의 최종 권한은 서버와 명시적인 게임플레이 상태 객체가 가진다.

## 4. 핵심 구성 요소

### BossAIComponent

초기에는 다음 상태를 소유하는 복제 가능한 Actor Component를 검토한다.

- 현재 페이즈 ID
- 전투 상태: 대기, 전투, 전멸, 리셋, 사망
- 광폭화 여부
- 현재 패턴 또는 서버 이벤트 ID
- 필요할 경우 현재 대표 타겟
- 페이즈 전환 세대 또는 전투 시작 시퀀스

블랙보드는 이 상태를 조회하거나 판단에 사용할 수 있지만, 복제와 저장의 기준이 되어서는 안 된다.

### Threat/Target 시스템

Perception은 후보 Actor를 제공하는 역할로 제한한다. 별도 서버 시스템이 다음을 결정한다.

- 위협도 누적 및 감소
- 거리, 시야, 사망 여부, 팀 관계 필터
- 도발/타겟 고정/강제 전환
- 타겟 상실 및 전투 이탈
- 전멸 또는 리셋 시 전체 위협도 초기화

### Phase Data

페이즈별 설정은 Data Asset으로 분리하는 방향을 우선한다.

- 진입 조건
- 페이즈 Gameplay Tags
- 사용 가능한 패턴/Ability 목록
- 쿨다운 및 반복 정책
- 페이즈 전환 시 실행할 이벤트
- 무적, 이동 제한, 소환 정책

### Behavior Tree

공용 BT Task/Service/Decorator 후보:

- 타겟 유효성 검사
- 위협도 기반 타겟 선택
- 공격 범위 및 시야 검사
- 페이즈/태그 조건 검사
- Ability 활성화 및 완료 대기
- 공격 전 방향 정렬
- 소환/기믹 실행
- 리셋 및 전투 종료

### Gameplay Ability

공격 판정과 상태 변화는 Ability/GAS 계층에서 처리한다.

- 서버 권위 피해와 GameplayEffect
- 비용, 쿨다운, 차단 태그
- 무적/슈퍼아머/시전 상태
- GameplayCue 기반 VFX/SFX
- 소환 Actor 생성
- 애니메이션 이벤트와 실제 판정 시점 연결

## 5. 복제 계약

| 상태 | 권위 | 클라이언트 처리 |
|---|---|---|
| HP/속성 | 서버 ASC/Attribute | HUD와 상태 표시 |
| 페이즈 | 서버 BossAIComponent | 페이즈 UI와 패턴 표시 |
| 어그로/타겟 | 서버 Threat 시스템 | 타겟 UI가 필요할 때만 복제 |
| 공격 판정 | 서버 Ability/Effect | 결과와 Cue 수신 |
| 텔레그래프 | 서버 이벤트/타임스탬프 | 클라이언트 연출 |
| 소환/장판 | 서버 Spawn/Effect | 복제 Actor 또는 Cue |
| 전멸/리셋 | 서버 Encounter 상태 | 전체 클라이언트 동기화 |

클라이언트가 전달한 타겟, 위치, 적중 결과는 신뢰하지 않는다. 클라이언트 입력이 필요한 기믹도 서버에서 범위와 유효성을 검증한다.

## 6. 첫 번째 수직 슬라이스

처음부터 범용 레이드 프레임워크 전체를 만들지 않고 아래 한 가지 보스로 검증한다.

1. 서버에서 보스 스폰 및 AI 시작
2. 플레이어 감지와 타겟 선택
3. 추적 및 방향 정렬
4. 기본 공격 Ability 실행
5. 서버 피해 적용과 클라이언트 GameplayCue
6. HP 70% 진입 시 2페이즈 전환
7. 페이즈 전환 상태 복제
8. 보스 사망 또는 전멸 후 리셋
9. 늦게 참가한 클라이언트가 현재 상태를 정상 수신

이 수직 슬라이스가 통과한 뒤에 소환, 광폭화, 다중 기믹, 복합 패턴을 추가한다.

## 7. 작업 순서

### Phase 0: 통합 경계 확인

- 현재 Project_RPG 타깃의 빌드와 기존 의존성을 확인한다. D1Game은 참고 소스이며 별도 빌드/모듈 추가를 요구하지 않는다.
- 현재 RPG AI Controller, NPC, GAS, Health, BT 에셋의 실제 연결 확인
- D1 코드 중 포팅할 동작과 제외할 동작 결정

### Phase 1: 상태 기반 만들기

- BossAIComponent의 서버 상태 정의
- Replication 및 OnRep 이벤트 정의
- 전투 시작/종료/리셋 수명주기 정의

### Phase 2: 타겟/어그로

- Perception 후보 수집
- Threat 데이터와 타겟 선택
- 타겟 상실, 사망, 이탈, 전멸 처리

### Phase 3: BT/GAS 연결

- Ability 실행 Task
- Ability 완료/실패/취소 처리
- 페이즈 조건 및 패턴 선택 Service/Decorator

### Phase 4: 페이즈/기믹

- Phase Data Asset
- HP 구간과 이벤트 기반 전환
- 소환, 장판, 무적, 광폭화

### Phase 5: 멀티플레이 검증

- Listen Server + 2명 이상 클라이언트
- Dedicated Server
- 늦은 참가
- 플레이어 이탈
- 전멸/리셋 반복
- 공격 중 페이즈 전환
- 클라이언트와 서버의 이벤트 중복 실행 여부

## 8. 설계 원칙

- D1 코드 전체 이식보다 현재 Project_RPG 계층에 맞춘 동작 포팅을 우선한다.
- 반복되는 규칙은 C++, 보스별 수치와 패턴 조합은 Data Asset/Blueprint로 둔다.
- BT는 상태 저장소가 아니라 의사결정 계층으로 사용한다.
- GAS는 피해와 상태 효과의 권위 계층으로 사용한다.
- 보스마다 별도 Controller를 만들기보다 공용 Controller + Boss Data 구성을 우선 검토한다.
- 모든 중요한 전투 결과는 서버 로그와 재현 가능한 상태 전이로 추적할 수 있어야 한다.

## 9. 아직 결정하지 않은 사항

- Threat는 별도 서버 전용 Actor Component로 분리했다(13절). 타겟 UI 복제 여부는 미결정이다.
- 현재 타겟을 클라이언트 UI에 복제할지
- 패턴 선택을 BT 중심으로 둘지, 서버 BossAIComponent의 Pattern Scheduler와 분리할지
- 보스 Ability를 전부 Blueprint로 시작할지, 공용 Ability Task부터 C++로 만들지
- Encounter Director를 보스별로 둘지 던전/레이드 단위로 둘지

## 10. Phase 1 구현 계약 (2026-09-10)

이 단계는 보스 상태 관리와 복제 기반이다. 기존 몬스터 BP에 자동 적용되지 않으며, 어그로·공격 Ability Task·전투장 정리는 후속 구현이다.

| 코드 | 책임 |
|---|---|
| Source/Project_RPG/Public/AI/RPGBossDefinition.h | 고유 페이즈 태그와 내림차순 HP 임계치, 데이터 검증 |
| Source/Project_RPG/Public/AI/RPGBossAIComponent.h | 서버 상태 전이, ASC 체력 변화 구독, 현재 상태 복제 |
| Source/Project_RPG/Public/AI/BTService_RPGBossState.h | 보스 상태를 서버 블랙보드에 주기적으로 반영 |
| Source/Project_RPG/Private/Tests/RPGBossStateTests.cpp | 데이터 검증, 페이즈 전진, 리셋, 권한 차단 자동 테스트 |
| Source/Project_RPG/Private/Tests/RPGBossTestFixture.cpp | 기존 게임 에셋과 독립된 ASC/보스 복제 테스트 환경 |
| Scripts/Network/Run-BossStateNetworkTest.ps1 | 서버와 원격 클라이언트 두 개를 실행하고 로그로 검증 |
| Scripts/Tests/Run-BossStateAutomationTest.ps1 | 자동테스트별 성공 로그 검증, 시간 제한 및 실행 프로세스 정리 |

### 상태 전이

- Idle → StartEncounter → InCombat
- InCombat → 체력 0 이하 → Dead
- InCombat → MarkWiped → Wiped
- InCombat 또는 Wiped → BeginReset → Resetting
- Resetting → 정리 및 체력 복원 완료 후 CompleteReset → Idle
- Dead는 종결 상태다. 재도전 시 새 보스 인스턴스를 생성한다.

StartEncounter는 초기 Ability/Effect 적용이 끝난 후 서버에서 명시적으로 호출한다. ASC Avatar가 소유 Actor와 같고 RPG 체력 속성이 존재해야 한다. 2026-09-12의 NPC 연결부터 소유자가 ARPGNonPlayerCharacter이면 서버의 IsNPCStartUpDataReady, 유효한 Controller, BeginPlay 완료까지 검사한다. NPC가 아닌 소유자는 기존 ASC/체력 검사를 유지한다.

페이즈 데이터의 첫 임계치는 1.0이며, 이후 임계치는 0보다 크고 엄격히 감소해야 한다. 한 번에 여러 임계치를 넘으면 최종 해당 페이즈로 이동한다. 회복은 페이즈를 되돌리지 않는다. 현재 구현은 중간 페이즈 연출을 각각 실행하지 않는다. 해당 기믹이 필요하면 전환 대기열을 별도로 설계한다.

StartEncounter에서 페이즈 설정을 복사해 해당 전투 동안 고정한다. 체력·최대 체력은 기존 ASC에서 읽고, 별도의 HP 복제 필드를 만들지 않는다. PhaseTag는 상태 식별자이며 현재 단계에서는 ASC의 소유 태그로 자동 부여하지 않는다.

### 복제와 이벤트

FRPGBossState는 EncounterState, EncounterId, Revision, PhaseIndex, PhaseTag, bEnraged, ServerStateTime을 복제한다. 서버와 클라이언트 모두 OnBossStateChanged를 받는다. UI는 구독 직후 GetBossState를 읽어 이미 전달된 상태를 표시해야 한다.

복제는 최신 상태를 전달하므로 같은 프레임의 Wiped → Resetting이나 Idle → InCombat이 클라이언트에 각각 보인다고 보장하지 않는다. 반드시 모두 실행돼야 하는 기믹 이벤트는 별도의 전달 계약이 필요하다. 상태 이벤트 안에서 즉시 다른 상태 변경 API를 호출하면 거부된다. 후속 전이는 다음 틱으로 예약한다.

소유 Actor와 ASC의 복제 설정도 필요하다. 보스 상태 컴포넌트를 붙이는 것만으로 기존 ASC·애니메이션·장판의 복제가 자동 완성되지는 않는다. 서버 GameState 시간은 클라이언트의 GetServerWorldTimeSeconds와 비교한다.

### 기존 보스에 연결하는 순서

1. 새 보스 BP는 ARPGBossCharacter를 부모로 만들면 기존 NPC의 ASC를 재사용하면서 보스 컴포넌트와 ASC Minimal 복제 설정을 함께 얻는다. 기존 NPC BP에 RPGBossAIComponent만 추가하는 방식도 가능하지만 그 경우 ASC 복제는 별도로 설정한다. 기존 BP의 부모를 자동 변경하지 않는다.
2. RPGBossDefinition Data Asset을 만들고 페이즈 태그와 HP 임계치를 지정한다.
3. 서버에서 IsNPCStartUpDataReady를 확인하고 OnNPCStartUpDataReady를 구독한다. 준비 완료 후 실제 전투 진입 시 StartEncounter를 호출한다. 스폰 후 곧바로 싸우는 보스만 ARPGBossCharacter의 bStartEncounterOnReady를 켠다(기본 false).
4. Blackboard에 BossInCombat(Bool), BossPhaseIndex(Int), BossEnraged(Bool)를 만든다. 페이즈 인덱스는 0부터 시작하며 미설정 값은 -1이다.
5. BT 루트 브랜치에 BTService_RPGBossState를 붙인다. 공격 브랜치는 BossInCombat을 조건으로 사용한다. Service가 멈춘 브랜치 안에만 있으면 상태 갱신도 멈춘다.
6. 서버의 전멸 감지자가 MarkWiped/BeginReset을 호출한다. 공격 취소, 이동 중지, 소환물·장판 정리, 체력 복원은 전투 소유자가 수행한다. 그 작업이 끝난 뒤 CompleteReset을 호출한다.

BT Service는 기본 0.1초 간격이므로 즉각적인 공격 금지는 서버의 보스 상태 검사로도 보장해야 한다. Service만으로 실행 중인 Ability가 취소되지는 않는다.

### 검증 실행

Editor 타깃을 먼저 빌드한 뒤 Automation에서 ProjectRPG.Boss 테스트를 실행한다.

```powershell
& 'D:/UE_5.8/Engine/Build/BatchFiles/Build.bat' Project_RPGEditor Win64 Development '-Project=C:/UE5/Project_RPG/Project_RPG.uproject' -WaitMutex -NoHotReloadFromIDE -MaxParallelActions=2
& ./Scripts/Tests/Run-BossStateAutomationTest.ps1
& ./Scripts/Network/Run-BossStateNetworkTest.ps1 -TimeoutSeconds 240
& ./Scripts/Network/Run-BossStateNetworkTest.ps1 -ListenServer -Port 18792 -TimeoutSeconds 240
```

네트워크 테스트는 에디터 실행 파일의 서버 모드를 사용한다. 패키징된 Dedicated Server 타깃의 검증과는 구분한다. 초기 상태 테스트는 HP를 ASC API로 변경했다. 15절부터 피격은 기존 피해 계산 GameplayEffect를 사용하고 리셋 회복만 ASC API로 수행한다. 서버 HitResult를 주입하므로 실제 충돌/가시선·애니메이션 검증은 아니다. 결과 로그는 Saved/Tests/BossState의 실행별 폴더에 남는다.

두 실행 스크립트는 프로젝트 `Saved/Tests/BossState` 아래의 파일 캐시와 실행별 `UserDir`를 사용한다. 보스 검증에 불필요한 에디터 Python 시작 스크립트는 `-DisablePython`으로 끈다. 이미 빌드된 바이너리의 검증에서 사용자 폴더 권한 때문에 SDK 초기화가 실패하면 `-SkipSDKSetup`을 추가할 수 있다. 이 옵션은 자식 프로세스의 SDK 검사만 생략하며 빌드 성공을 보증하지 않는다. 호출 전 환경변수는 종료 시 복원한다.

`TestExit=Automation Test Queue Empty`는 테스트 실패 시에도 종료 코드 0을 반환할 수 있다. 자동테스트 스크립트는 종료 코드뿐 아니라 Definition/Lifecycle/NPCStartup/Threat/Ability/Damage 각각의 성공 결과와 실패·assert 로그를 검사한다. 네트워크 스크립트는 서버와 두 클라이언트의 Iris ReplicationSystem 생성, 각 상태의 검증 마커 및 치명적 오류·Iris/Net 오류를 검사한다. 프로젝트 전체의 모든 로그 오류가 없음을 보장하는 검사는 아니다.

### 다음 구현

- 서버 어그로/타겟 계층 구현·검증은 13절 참조
- 보스 전투 상태를 확인하는 Ability 실행 Task와 완료·실패·취소 수명주기 구현·검증은 14절 참조
- 첫 실제 보스 BP/BT/Ability의 공격 및 초기화 흐름 연결
- 전멸 감지, 장판·소환물 정리, 이탈·지연·패킷 손실 시나리오

## 11. 검증 결과 (2026-09-12)

환경: UE 5.8.1, Win64 Development Editor, 프로젝트 기본 Iris 복제, localhost. 실제 콘텐츠 에셋을 수정하지 않고 네이티브 테스트 Pawn/GameMode로 상태 계층을 검증했다.

| 검사 | 결과 | 증거 로그 (프로젝트 루트 기준) |
|---|---|---|
| C++ 컴파일 및 링크 | 성공, 종료 코드 0 | `Saved/Logs/BossStateBuild-cleanup-20260912.log` |
| Definition / Lifecycle 자동테스트 | 2개 성공, 테스트 이벤트 경고 없음 | `Saved/Tests/BossState/Automation-8824718558fb4240895979bb092a8c3d/automation.log` |
| 전용 서버 모드 + 원격 클라이언트 2개 | 성공 | `Saved/Tests/BossState/ff4c1483fcd54346aa4c05871745727f/` |
| 리슨 서버 호스트 + 원격 클라이언트 2개 | 성공 | `Saved/Tests/BossState/389c324ed179475c93fedf7db114205d/` |

네트워크 검증에서 두 원격 클라이언트 모두 HP 69/2페이즈/광폭화를 수신했고, 클라이언트 상태 변경 호출이 거부됐다. 중도 참가자도 같은 상태를 받았다. 이후 Resetting의 페이즈·광폭화 초기화, EncounterId 2의 HP 100 재전투, HP 0의 Dead 상태를 확인했다. 전멸은 테스트 GameMode가 명시적으로 선언하며 플레이어 전멸 자동 감지를 검증한 것은 아니다.

이번 검증 과정에서 수정한 항목:

- GameMode/GameState가 없는 자동테스트 월드에 BeginPlay 전달을 추가하고 ASC 초기화를 명시적으로 검증했다.
- 체력과 최대 체력이 각각 유한값이어도 나눗셈이 오버플로할 수 있어, 비율 계산 전에 체력을 `[0, MaxHealth]`로 제한했다. 극단적인 유효 숫자에 대한 시작 테스트를 추가했다.
- 전투 중 원본 페이즈 데이터를 수정해도 진행 중 전투의 복사본이 유지되는지 검사했다.
- 테스트 월드를 파괴하기 전에 EndPlay를 호출하도록 정리했다.
- 자동테스트 종료 코드만으로 성공을 판단하지 않도록 실행 스크립트를 추가했다.

환경/기존 프로젝트의 별도 경고도 남아 있다. UBT는 컴파일·링크 성공 뒤 사용자 폴더 `Trace.uba` 기록 권한 오류를 출력했다. 빌드 캐시는 프로젝트 내부에서 재생성했고 기존 Editor `Makefile.bin`은 `.boss-validation-20260912-backup`, `.boss-cleanup-20260912-backup`으로 보관했다. 네트워크 실행의 CommonUI Viewport 설정 오류와 기존 `BT_Guardian.uasset` 로드 경고 등은 이번 보스 상태 작업에서 수정하지 않았다. 따라서 위 성공은 해당 검사 범위의 통과이며 프로젝트 전체 무오류 판정이 아니다.

아직 미검증/미구현: 기존 NPC 비동기 초기화와 실제 보스 BP 연결, BT 서비스의 실행 중 갱신, 공격 Ability/피해 Effect/GameplayCue, 어그로와 플레이어 이탈, 전투장 정리, 패킷 손실·지연, 패키징된 전용 서버. 네트워크 검증 이후 변경은 자동테스트 월드의 종료 정리뿐이며 보스 런타임 및 네트워크 시나리오 코드는 동일하다.

## 12. NPC 초기화와 보스 연결 (2026-09-12)

11절은 상태 계층 단독 검증 당시 기록이다. 후속 구현은 기존 ARPGNonPlayerCharacter의 StartUpData 수명주기를 보완하고, 선택적으로 사용하는 ARPGBossCharacter를 추가한다. 일반 NPC에 보스 컴포넌트나 ASC 복제 모드를 강제로 추가하지 않는다.

- 비동기 로드는 이미 메모리에 있는 데이터도 기존 StreamableManager 경로를 거친다. 약한 UObject 콜백, 요청 세대 번호, 로드 핸들 취소로 파괴·빙의 해제 후 오래된 완료 콜백을 차단한다.
- 능력 부여를 시작하기 전에 재진입 방지 플래그를 세운다. 같은 Pawn의 재빙의는 능력·초기 Effect를 다시 부여하지 않으며 완료 알림도 한 번만 보낸다. 로드 중 취소했다면 다음 빙의에서 새 요청을 만들 수 있다.
- IsNPCStartUpDataReady와 OnNPCStartUpDataReady는 서버 로컬 계약이다. 클라이언트 UI 준비는 복제된 ASC 속성/보스 상태로 판단하며 이 알림을 기다리지 않는다.
- Ready는 `GiveToAbilitySystemComponent` 호출이 정상 반환했다는 뜻이다. 이 void API가 개별 Effect 적용 성공이나 시작된 비동기 Ability의 종료까지 보증하지는 않는다. StartEncounter는 살아 있는 유효한 체력을 별도로 검사한다.
- 초기화 부여가 시작된 뒤에는 부분 적용의 롤백/재시도를 제공하지 않는다. 설정의 런타임 교체와 ASC Avatar 변경도 지원 범위 밖이다. 새 설정이 필요하면 새 Pawn을 생성한다.
- ARPGBossCharacter는 NPC ASC를 그대로 사용하며 Minimal 복제와 등록된 서브오브젝트 방식을 설정한다. PlayerState가 없는 클라이언트 NPC도 BeginPlay에서 ASC ActorInfo를 초기화한다.
- bStartEncounterOnReady는 기본 false다. 켜면 준비·BeginPlay·빙의가 모두 충족된 최초 전투만 자동 시작한다. 리셋이나 재빙의가 두 번째 전투를 자동 시작하지 않는다. 자동 시작이 꺼져 있으면 서버의 전투 진입 로직에서 명시적으로 StartEncounter를 호출한다.
- 빙의 해제는 로드 취소와 새 전투 시작 차단만 처리한다. 진행 중 전투의 wipe/reset·공격 중지는 전투 소유자의 책임이며 이후 어그로/BT 통합에서 연결한다.

추가 코드: `Character/RPGBossCharacter.h/.cpp`, NPC 초기화 API, `Tests/RPGBossNPCStartupTests.cpp`, `Tests/RPGBossStartupTestFixture.h/.cpp`. 기존 네트워크 시나리오에 실제 NPC/Boss 상속 계층의 테스트 캐릭터를 하나 더 배치한다. 테스트용 인메모리 StartUpData가 ASC 체력을 100으로 초기화하고, 초기화 도중 전투 시작 거부와 서버 자동 시작, 두 클라이언트의 체력/ActorInfo/보스 상태 수신을 검사한다. 디스크에서 미로드 상태인 실제 보스 에셋이나 공격 Ability를 검증하는 테스트는 아니다.

### 실제 보스 BP 설정

1. ARPGBossCharacter 기반 BP에 기존 NPC StartUpData와 BossAIComponent의 Definition을 지정한다. StartupData에는 초기 체력/능력 설정이 필요하다.
2. 기존 AIController/Blackboard/BT 에셋을 선택한다. 이 캐릭터 클래스 자체가 BT를 생성하거나 실행하지 않는다.
3. 기본값(자동 시작 꺼짐)에서는 서버의 전투 진입 로직이 준비 상태를 조회한 뒤 StartEncounter를 호출하고 성공 여부를 확인한다. Ready 이벤트를 늦게 구독할 수 있으므로 이벤트만 기다리지 않는다.
4. 스폰 직후 전투가 필요한 경우에만 bStartEncounterOnReady를 켠다. 기존 보스 BP의 부모나 설정은 이번 작업에서 자동 변경하지 않았다.

### 초기화 연결 검증 기록

- 빌드: `Saved/Logs/BossStartupBuild-20260912.log` — UHT, C++ 컴파일·링크 성공(종료 코드 0). 기존 사용자 폴더 Trace.uba 기록 권한 오류는 별도로 남았다.
- 자동테스트: `Saved/Tests/BossState/Automation-111064b7483948b094a51896d5b258d4/automation.log` — Definition, Lifecycle, NPCStartup 3개 성공, 테스트 이벤트 오류/경고 없음.
- 전용 서버: `Saved/Tests/BossState/b8d469d95a88422f8ea4d6465bdc71a2/` — 저장 로그를 2026-09-13에 재검사해 서버 완료 및 두 클라이언트의 모든 검사 마커와 치명적 오류 부재 확인.
- 리슨 서버: `Saved/Tests/BossState/f3456eeb4e2945a897fa3501b59f0fd0/` — 2026-09-13 재실행 성공(종료 코드 0). 호스트와 원격 클라이언트 2개 구성에서 NPC 초기화/ASC/보스 상태 복제 및 기존 리셋·재전투·사망 시나리오 통과. 앞선 `4975727799304479954f5e4e2db52746` 실행은 리셋까지만 기록되어 최종 성공 근거로 사용하지 않았다.

NPCStartup은 초기화 전·부여 도중 전투 시작 차단, 부여 도중 재빙의의 중복 적용 방지, 로드 중 빙의 해제/파괴, 요청 교체 후 최신 요청만 적용, 기본 수동 시작, 최초 자동 시작, 리셋 후 재빙의의 자동 재시작 방지를 검사한다. 현재는 네이티브 NPC/Boss 상속 계층 연결까지 구현됐으며, 실제 보스 콘텐츠의 전투 진입·어그로·공격 Ability 연결은 후속 작업이다.

멀티플레이 로그에는 기존 CommonUI/에셋 경고 외에도 에디터 모듈 로딩 중 Iris serializer 등록 변경 경고가 남아 있다. 위 결과는 관측한 상태·초기화 시나리오의 통과이며 모든 복제 타입이나 패키징 환경의 무오류 보증이 아니다.

## 13. 서버 어그로/타겟 계층 (2026-09-13)

`URPGBossThreatComponent`는 위협도와 현재 타겟의 서버 전용 원본이다. `ARPGBossCharacter`가 기본 컴포넌트로 소유하며, 일반 NPC의 동작은 유지한다. 위협도 목록과 현재 타겟은 복제하지 않는다. 클라이언트에서는 조회가 null/0이고 변경 API가 거부되며, 서버 RPC를 추가하지 않았다. 타겟 UI/공격 텔레그래프의 복제 계약은 공격 연결 단계에서 별도로 정한다.

### 선택 및 무효화 규칙

- 전투 중이며 소유 보스가 유효한 Controller에 빙의되어 있을 때만 선택한다. 후보는 같은 월드의 살아 있는 적대 Pawn이어야 한다. 양쪽 Controller의 명시적인 팀 ID와 보스 Controller의 팀 태도, 대상 ASC Avatar/체력/사망 태그, PlayerState의 비활성·관전자 여부와 거리를 검사한다. 플레이어뿐 아니라 조건을 충족한 적대 NPC/소환수도 대상이 될 수 있다.
- 기본 최대 거리는 보스의 현재 위치에서 6000cm이며, 전투장 원점 기반 leash는 아니다. 기본 추적 상한은 128개, 런타임 최대 상한은 512개다. 점수는 양의 유한 입력만 받고 1e12에서 제한한다.
- 가장 높은 점수를 선택하되, 유효한 기존 타겟의 점수에 기본 1.1을 곱한 값을 엄격히 초과해야 전환한다. 동점은 기존 타겟/먼저 등록한 후보를 유지한다. 난수 선택은 하지 않는다.
- `GetCurrentTarget`은 호출 때마다 재검사하여 사망·파괴·거리 이탈·빙의 해제/Controller 교체를 즉시 거부한다. 목록 정리와 대체 타겟/블랙보드 갱신은 기본 0.2초 주기이며 `RefreshTargets`로 앞당길 수 있다. 전멸·리셋·보스 사망·EncounterId 변경 시 목록을 비운다. BossCharacter의 빙의 해제 시에도 즉시 비운다.
- 타겟이 같은 Pawn이어도 Controller가 바뀌면 이전 점수를 승계하지 않는다. 같은 Controller가 틱 사이에 해제 후 즉시 재빙의하는 경우까지 감지하는 빙의 세대 시스템은 아직 없다.
- `TargetActor` Object 키는 조회용 사본이다. 보스가 Threat 컴포넌트를 갖고 있으면 기존 RPG Controller의 직접 타겟 쓰기 대신 이 컴포넌트로 시야 이벤트를 전달한다. BP/BT에서 같은 키를 덮어쓰지 않아야 한다. 키 이름을 None으로 두면 사본 갱신을 끌 수 있다.
- 블랙보드 알림 도중 AddThreat/Refresh 재진입은 거부한다. 상태 전환에 따른 ClearThreat는 재진입해도 허용하여 초기화가 우선되도록 한다.

### Perception 및 피해 연결

RPG AIController의 Sight 감지는 초기 최소 점수(기본 1)를 제공한다. 반복 감지는 점수를 누적하지 않는다. 주기적인 Perception 조회로 전투 시작 전에 이미 보이던 후보도 반영한다. 시야 상실은 해당 후보의 점수를 제거한다. 그 뒤 유효한 서버 피해 이벤트가 발생하면 시야 밖 대상도 다시 등록할 수 있으므로, 실제 공격의 거리/시야/적중 판정은 Ability에서 별도로 검사해야 한다.

서버 적중/피해 로직은 `AddThreat(공격 주체 Pawn, 검증된 양의 위협도, ExpectedEncounterId)`를 호출한다. `ExpectedEncounterId`는 지연 피해가 도착한 순간이 아니라 공격 생성 시점의 보스 전투 ID를 캡처해야 한다. 이전 전투의 투사체나 지연 콜백이 새 전투에 위협도를 쌓지 못하도록 검사한다. 현재 단계에서는 기존 모든 피해 Effect에 자동 연결하지 않았다.

실제 BP 연결 시 `ARPGBossCharacter` + `ARPGAIController` 계층을 사용하고, 블랙보드에 Pawn/Actor를 받는 `TargetActor` Object 키를 둔다. 보스의 초기화가 끝난 뒤 서버 전투 진입 로직에서 StartEncounter를 호출해야 하며, 시야 감지만으로 전투를 자동 시작하지 않는다. 기존 콘텐츠 BP/BT 에셋은 변경하지 않았다.

### 검증 범위

`ProjectRPG.Boss.Threat` 자동테스트는 점수/동점/전환 임계값, 잘못된 숫자·아군·관전자·비활성 대상 거부, 시야 초기 후보와 상실 콜백, 블랙보드 반영, 클라이언트 호출 차단, 사망/파괴/빙의 변경/거리 이탈, 용량 제한, 리셋 세대 검사와 알림 재진입을 다룬다. 시야 테스트는 엔진 Perception에 자극을 주입하며 실제 지형의 가시선 추적은 검증하지 않는다.

멀티플레이 테스트는 초기화된 실제 Boss/NPC 상속 계층에 서버에서 위협도 100/10을 주입한다. 전용 서버 또는 리슨 호스트와 원격 클라이언트 2개를 실행하고, 기존 늦은 참가·상태/체력 복제·리셋/재전투/사망 검사 후 우선 타겟의 클라이언트 프로세스를 종료한다. 서버의 실제 Logout 이후 남은 대상으로 전환되고 위협도 리셋이 비워지는지 검사한다. 이 시나리오에서는 시야 감지를 끄며 테스트 프로세스에만 ConnectionTimeout 20초를 적용한다. 종료 대상은 스크립트가 생성한 첫 번째 클라이언트뿐이다.

미구현: 위협도 자연 감소, 도발/타겟 고정, 자동 전투 진입/전멸 판정, Ability 취소, 이동 중단/귀환, 장판·소환물 정리, 타겟 UI 복제, 실제 피해 Effect 연결. 다음은 선택된 타겟과 보스 전투 세대를 확인하는 BT/GAS 실행 Task 및 완료·실패·취소 수명주기다.

### 어그로 계층 검증 기록

- 빌드: `Saved/Logs/BossThreatBuild-20260913.log` 및 테스트 월드 초기화 보완 후 `Saved/Logs/BossThreatBuild-fixture-20260913.log` — 컴파일·링크 성공, 종료 코드 0. 기존 사용자 폴더 Trace.uba 기록 권한 오류는 남아 있다. 기존 Makefile은 프로젝트 Intermediate 아래 고유한 `.boss-threat-...-backup`/`.boss-threat-fixture-...-backup`으로 보관했다.
- 자동테스트: `Saved/Tests/BossState/Automation-59f38666c82f47c7bc77d6705b68f14e/automation.log` — Definition, Lifecycle, NPCStartup, Threat 4개 성공. 앞선 `Automation-89e6070ae67f439f94f1bda8f587a857` 실행은 테스트 월드의 AI 시스템 누락으로 시야 자극 등록 시 assert가 발생했다. 컨트롤러 생성 전에 CreateAISystem을 명시하고 재실행했다. 실패 실행을 성공 근거로 사용하지 않는다.
- 전용 서버 모드 + 원격 클라이언트 2개: `Saved/Tests/BossState/8459d09b82c941e1811d03e48173c376/` — 성공, 종료 코드 0.
- 리슨 호스트 + 원격 클라이언트 2개: `Saved/Tests/BossState/1935d2cbe9e5406582f9ca561447c5bb/` — 성공, 종료 코드 0.

두 네트워크 실행 모두 양쪽 클라이언트의 기존 5개 검사 마커와 서버의 위협도 우선 선택·실제 접속 종료 후 대체 타겟 선택·리셋 마커를 확인했다. 생성한 테스트 프로세스는 모두 종료했다. 네트워크 검증 이후 소스 변경은 자동테스트 월드의 AI 시스템 생성뿐이며, 런타임 어그로/Controller/네트워크 시나리오는 동일하다. CommonUI/기존 에셋/Iris 모듈 등록 등 앞 절의 별도 경고는 여전히 이번 판정 범위 밖이다.

## 14. BT → GAS 실행 연결 (2026-09-14)

`URPGBossGameplayAbility`는 기존 `URPGNPCGameplayAbility`를 상속하는 단일 타겟·중단 가능한 보스 공격의 기반이다. `UBTTask_RPGBossAbility`는 이미 부여된 해당 계층의 Ability 한 개를 실행하고 종료를 기다린다. 다른 NPC Ability나 기존 BP/BT 에셋을 자동 교체하지 않는다.

- Ability는 InstancedPerActor, ServerOnly 실행/보안 정책, 수동 활성화를 요구한다. 클라이언트 입력 RPC를 추가하지 않으며, BT 자체와 Ability 실행 컨텍스트도 복제하지 않는다. 기존 보스 상태/ASC는 프로젝트의 Iris 설정과 등록된 서브오브젝트 경로로 복제한다. 이 단계에 커스텀 Iris serializer는 추가하지 않는다.
- 활성화 시 현재 Threat 타겟, 소유 Controller, EncounterId, PhaseIndex 및 실행 일련번호를 저장한다. Blueprint ActivateAbility보다 먼저 저장하므로 BP에서 조회할 수 있다. 이미 실행 중인 Ability는 재활성화하거나 Task가 인수하지 않는다.
- `IsBossExecutionValid`/`GetBossExecutionTarget`은 저장한 전투·페이즈·타겟·Controller가 여전히 유효한지 서버에서 검사한다. 실제 피해/소환 직전에 검사해야 한다. 이 함수는 공격 거리·가시선·충돌 판정이나 비용/쿨다운 CommitAbility를 대체하지 않는다.
- 페이즈 변경, 리셋, 전멸, 보스 사망 시 Ability가 상태 이벤트로 취소된다. 타겟 변경/사망/접속 종료나 빙의 변경은 컨텍스트 조회에서 즉시 거부되고, 실행 중인 BT Task의 다음 틱에서 취소한다. 직접 활성화해 BT가 소유하지 않는 실행은 호출자가 타겟 수명주기를 처리해야 한다.
- 이번 기본 클래스는 취소 가능 상태를 유지한다(`CanBeCanceled` final). 취소 불가능한 시네마틱/기믹, 타겟 없이 쓰는 광역 패턴, 페이즈를 넘어 지속되는 패턴은 별도 계약이 필요하다. 네이티브 파생 Ability는 CanActivate/Activate/End의 부모 구현을 호출해야 한다.

| 상황 | BT 결과/처리 |
|---|---|
| 정상 EndAbility | Succeeded — 명중/피해 성공이 아니라 실행 종료 성공 |
| GAS 활성화 거부, 미부여, 타겟 없음, 잘못된 설정 | Failed, 다른 실행은 건드리지 않음 |
| 취소된 EndAbility, 컨텍스트 무효, 시간 초과 | Failed, 자신이 시작한 실행만 취소 |
| BT Abort | Aborted, 자신이 시작한 실행 취소 |
| BT 종료/인스턴스 정리 | 종료 delegate 해제 및 남은 소유 실행 취소 요청 |

Task는 트리 인스턴스별 UObject다. 종료 delegate를 활성화 전에 연결하여 즉시 끝나는 Ability도 처리하고, delegate 안에서는 BT를 완료시키지 않고 결과만 기록한다. 활성화 핸들과 실행 일련번호로 같은 Ability의 후속 실행을 구분한다. 기본 제한 시간은 서버 월드 시간 15초이며 0.01~600초의 유한값만 허용한다. 엔진 Ability scope lock 중 취소는 GAS가 lock 해제까지 지연할 수 있으므로 리셋의 모든 정리가 완료됐다는 보증으로 Task 종료만 사용하지 않는다.

### 콘텐츠 연결 순서

1. `URPGBossGameplayAbility` 기반 공격 BP를 만들고 기존 NPC StartUpData에서 부여한다. 수동/ServerOnly/InstancedPerActor 설정을 유지한다.
2. 기존 BT의 전투 브랜치에 `Execute Boss Ability` Task를 배치하고 AbilityClass와 제한 시간을 설정한다. 블랙보드는 선택에 사용할 수 있지만 최종 대상은 서버 Threat에서 얻는다.
3. Ability에서 CommitAbility 성공을 확인하고 몽타주/서버 적중 로직을 실행한다. 피해 직전에 `IsBossExecutionValid`, 공격 거리/시야/적중 검사를 수행한다. 성공 또는 실패/취소 경로에서 반드시 종료한다. 정상 EndAbility는 BT에서 성공이므로 Commit 실패 등을 Task 실패로 전달하려면 취소 경로(CancelAbility 또는 cancelled EndAbility)를 사용한다.
4. 지연 효과에 필요한 EncounterId는 `GetBossExecutionEncounterId`로 생성 시점에 복사한다. 현재 실행 유효성만 검사하면 이전 실행의 임의 타이머를 식별하지 못하므로 AbilityTask의 종료 정리 또는 별도 실행 토큰도 필요하다. 타이머/소환물 정리는 구체 Ability의 책임이다.
5. 클라이언트 몽타주/텔레그래프/GameplayCue의 전달과 늦은 참가자용 공격 상태는 후속 콘텐츠 작업에서 구현한다. 이번 네트워크 테스트는 서버 BT/GAS 수명주기와 기존 Iris 상태 복제까지 다룬다.

자동테스트 `ProjectRPG.Boss.Ability`는 실제 네이티브 one-shot Behavior Tree를 실행한다. 즉시/지연 완료, 즉시 취소, GAS 거부, BT 중단, 제한 시간, 페이즈/리셋/사망, 접속 종료를 모사한 빙의 해제, 클라이언트 역할 차단, 외부 실행 보호와 같은 spec의 후속 실행 보호를 검사한다. 네트워크 시나리오는 기존 실제 접속 종료 검사에 보스 BT의 서버 Ability 활성화와 취소/Task 실패 검사를 추가했다. 아직 실제 데미지 Effect나 공격 연출 테스트는 아니다.

### BT/GAS 검증 기록

- 빌드: `Saved/Logs/BossAbilityBuild-20260914.log`, 테스트 틱 보완 후 `Saved/Logs/BossAbilityBuild-tick-20260914.log` — 컴파일·링크 성공, 종료 코드 0. 기존 사용자 폴더 Trace.uba 기록 권한 오류는 별도로 남았다. 이전 Makefile은 프로젝트 Intermediate 아래 고유한 `.boss-ability-...-backup`/`.boss-ability-tick-...-backup`으로 보관했다.
- 자동테스트: `Saved/Tests/BossState/Automation-ab70c6945af1463db9ab796ad17254f1/automation.log` — Ability, Definition, Lifecycle, NPCStartup, Threat 5개 성공. Ability의 테스트 이벤트 오류/경고 없음.
- 전용 서버 모드 + 원격 클라이언트 2개: `Saved/Tests/BossState/3196efea61064347842406ae62f1fa4e/` — 성공, 종료 코드 0.
- 리슨 호스트 + 원격 클라이언트 2개: `Saved/Tests/BossState/8a712c5011604924ada9db3e1c7c51d6/` — 성공, 종료 코드 0.

네트워크 양쪽 구성에서 서버와 두 클라이언트의 Iris 활성화, 기존 상태/초기화/늦은 참가 검사, 서버 BT의 Ability 실행, 대상 클라이언트 종료 후 Ability 취소·Task 실패·대체 타겟 선택·리셋을 확인했다. 테스트 프로세스는 종료 시 정리한다. 전용 서버 테스트 이후 C++ 수정은 자동테스트 틱 처리뿐이며 런타임/네트워크 시나리오는 동일하다.

첫 `Automation-cbb70272c680457fb1469609888ffc55` 실행은 중단되어 성공 근거가 아니다. 이후 `Automation-7ce5092f688340fda2e1875e01567625` 실행은 Ability 테스트만 실패했다. 단일 엔진 프레임 안에서 World::Tick을 반복해도 TickTaskManager의 프레임 중복 방지로 BT가 계속 진행되지 않는 테스트 구성 문제였다. 월드 시간 진행과 함께 BT 컴포넌트를 명시적으로 수동 틱하고, 결과뿐 아니라 one-shot 트리 종료도 검사하도록 보완했다. 최종 성공은 수정 후 실행의 결과다.

남은 작업: 실제 보스의 공격 BP/BT 콘텐츠와 서버 충돌 판정 연결, 몽타주/GameplayCue/텔레그래프 및 늦은 참가자의 진행 중 공격 표시, 전투장 정리와 패킷 지연/손실·패키징 검증. 피격 GE→위협도 연결은 15절을 따른다. 기존 CommonUI/에셋/Iris 모듈 등록 경고는 별도 사항이며 위 결과는 테스트한 범위의 통과다.

## 15. 보스 피격 GameplayEffect → 위협도 (2026-09-14~16)

아래 반환값/0 피해 설명은 당시 계약이다. 2026-10-08의 공통 피해 receipt 경로는 GAS 적용과 서버 피해 승인 모두를 요구하며, 비정상 능력치·정수 절삭 후 0 피해는 false다. 현재 계약과 후속 회귀는 [BOSS_DAMAGE_EFFECT_CONTRACT.md](BOSS_DAMAGE_EFFECT_CONTRACT.md)를 따른다.

`URPGBossThreatComponent::ApplyDamageFromServerHit`는 보스가 피해를 받는 선택적 서버 진입점이다. 기존 적중 보안 검사와 `URPGAbilityFunctionLibrary::ApplyGameplayEffectSpecHandleToServerHit`를 재사용한다. 기존 모든 공격을 자동으로 가로채거나 일반 NPC 피해 경로를 교체하지 않는다. 보스가 플레이어를 공격하는 구체 Ability는 별도 후속 작업이다.

### 호출 계약

1. 공격 생성 시 **피격 보스**의 EncounterId를 저장한다. 보스 공격 Ability의 실행 EncounterId(공격자 세대)와 혼동하지 않는다.
2. 서버 Ability/투사체가 비용·쿨다운, 실행 유효성, 충돌 범위·가시선, 중복 적중을 검사한다. 클라이언트에서 받은 HitResult를 그대로 넘기지 않는다.
3. 원래 공격 Pawn의 RPG ASC에서 만든 Instant DamageSpec과 서버 HitResult, 저장한 EncounterId, 공격별 SecurityProfile을 피격 보스 Threat 컴포넌트에 전달한다. spec은 `Shared.SetByCaller.BaseDamage`에 해당하는 기존 네이티브 태그를 사용하며, 정상 RPG 피해 계산/AttributeSet 계약을 따라야 한다.
4. 이 함수가 GE를 적용하므로 호출자가 동일 GE를 따로 적용하거나 같은 피해에 AddThreat를 다시 호출하지 않는다. 반환 true는 GE 적용 성공이며, `OutHealthDamage`는 동기 적용 전후 순 HP 감소량, `bOutThreatAdded`는 위협도 등록 여부다. 피해 0/무효화/반올림, 사망, 리셋, 위협도 목록 포화 등으로 GE 성공과 위협도 등록 여부가 다를 수 있다.

### 경계와 제한

- 서버·전투 상태·저장된 전투 세대를 **GE 적용 전에** 검사한다. 이전 전투의 지연 공격은 새 전투의 HP와 위협도를 모두 바꾸지 못한다. 클라이언트용 RPC는 추가하지 않았다.
- 기존 Threat 후보 조건(생존/빙의/팀/관전자/월드/거리)을 충족하는 공격자만 받는다. 따라서 공격별 보안 거리뿐 아니라 MaxTargetDistance도 적용된다. 전투 밖 선제공격의 자동 풀링, 공격자 사망 후 남은 투사체 피해는 현재 계약에서 허용하지 않는다.
- 양쪽 RPG ASC와 대상 Avatar, spec 원본 ASC, 양의 유한 기본 피해, 유효 HP/최대 HP, Instant 정책을 검사한다. 피해 태그가 없는 spec은 보안 검사를 우회하지 못하게 거부한다. 실제 피해량/무적/방어력 계산은 기존 GE의 책임이다.
- 위협도는 순 HP 감소량만 더한다. 보호막 피해/도발/치유 위협도는 포함하지 않는다. 치명타격은 HP를 0으로 만들고 기존 AttributeSet의 사망 태그 및 BossAI의 Dead 전이를 사용한다. 사망/리셋 후 위협도를 다시 살리지 않는다.
- GE 적용 중 같은 진입점으로 재진입하는 피해는 거부한다. 동기 콜백이 리셋하거나 빙의 주체를 교체하면 후속 위협도 등록을 거부한다. 임의 콜백에서 다른 경로로 피해/회복을 섞으면 순 HP 변화가 합산될 수 있으므로, 정확한 피해 이벤트별 계측이 필요한 복합 GE는 별도 계약이 필요하다. 이 함수는 트랜잭션 롤백이나 전투 중 모든 외부 변경의 잠금 장치가 아니다.
- DOT/지속 GE는 최초 적용 이후 틱별 세대 검증과 정리가 필요하므로 거부한다. 호출 간 중복 Hit ID 저장은 하지 않으며, 같은 전투의 정상 연타는 허용한다. 서버 trace/LOS, Ability commit, 공격자 실행 토큰 및 투사체 수명주기는 호출자의 책임이다.
- HP/사망 태그/페이즈는 기존 ASC와 BossAI의 Iris 복제를 사용한다. 위협도나 damage spec 자체의 새 복제 필드는 추가하지 않았다.

### 피해 계산과 사망 태그 보완

기존 `UGE_ExecCalc_TakeDamage`를 공유하므로 숫자 검증은 일반 전투에도 적용된다. 기본 피해/공격력/치명 배율의 음수·비유한값과 0 이하/비유한 방어력은 실행을 중단한다. 치명 확률은 유한값을 확인한 뒤 0~1로 제한한다. 중간 계산은 double로 수행하고, 최종 피해가 1e9를 초과하면 정수 변환 전에 실행을 중단한다(상한으로 잘라 적용하지 않음). 이는 기본 전투 보안 상한 및 기존 int→float 메타 속성 저장을 고려한 안전 경계이며, 보안 프로필의 기본 피해 검사와 별개로 **최종 계산 결과**에도 적용된다. GE 적용 자체는 성공이어도 출력 피해는 0일 수 있다. 통상 수식/정수 절삭은 유지하되 중간 계산 정밀도는 달라진다.

첫 네트워크 실행은 서버의 피해/Dead 전이까지 확인됐지만 클라이언트 사망 태그 확인을 완료하지 못했다. 기존 `AddGameplayTagToActorIfNone`는 복제 옵션 없이 loose tag를 추가한다. 이를 일반 태그 전체에 걸쳐 변경하지 않고, `URPGAttributeSet`의 TakeDamage 치명타격 경로에서 BossAI 컴포넌트가 있는 서버 Avatar의 사망 태그만 `EGameplayTagReplicationState::TagOnly`로 추가하도록 보완했다. 보스 Dead는 종결 상태이므로 부활/태그 제거 계약은 추가하지 않는다. 일반 NPC/플레이어의 기존 태그 헬퍼 동작은 유지한다.

### 검증 구성

`ProjectRPG.Boss.Damage`는 네이티브 테스트 GE에서 **기존 `UGE_ExecCalc_TakeDamage`와 `URPGAttributeSet`**을 실행한다. 격리 Pawn에 실제 RPG ASC와 UI 인터페이스를 제공했으며, 운영 AttributeSet의 UI 조건을 테스트 때문에 제거하지 않았다. 피해 31→HP 69→2페이즈→위협도 31, 0 피해, GAS 거부, 잘못된 spec/숫자/적중 위치/거리/팀/권한, 공격자 사망·빙의 해제, 리셋/세대 변경, 재진입과 치명타격을 검사한다.

네트워크 시나리오는 서버가 저장한 spec으로 피해를 적용하고, 두 클라이언트(중도 참가 포함)가 HP 69/2페이즈/광폭화를 받도록 한다. 이후 리셋→HP 100/세대 2에서 이전 공격 거부→새 치명타격→HP 0/Dead/사망 태그를 검사한다. 별도 실제 Boss/NPC 테스트 캐릭터의 초기화·BT/GAS·접속 종료 취소 검사도 유지한다. 피해를 받는 대상은 격리 Pawn이며, 실제 NPC 보스 콘텐츠에 대한 공격 연출·충돌 검증은 아니다.

9월 14일 첫 빌드와 자동테스트 6개는 성공했지만(`Saved/Logs/BossDamageBuild-20260914.log`, `Saved/Tests/BossState/Automation-4a0a1acff6b3443094988b09ce4ffcb3/automation.log`), 후속 숫자/태그 보완분의 최종 검증 근거는 아니다. `Saved/Tests/BossState/69a0344c9ffb46f397ac5b84ec0532bf/`와 `d864c3d7a5ed450295fe142c89e87925/`, `97cbbafec854495f99e00fb0ed9cd36a/`는 실행이 중단됐으므로 네트워크 성공 근거로 사용하지 않는다.

보완 후 최종 검증(2026-09-18, KST):

- 빌드: `Saved/Logs/BossDamageBuild-guard-20260916.log` — 컴파일·링크 성공, 종료 코드 0. 기존 Trace.uba 사용자 폴더 쓰기 권한 오류는 별도로 남는다. 빌드 뒤 AttributeSet의 들여쓰기만 정리했으며 바이너리와 런타임 의미는 같다.
- 자동테스트: `Saved/Tests/BossState/Automation-732f4982bc3447cb809a27737909771d/automation.log` — Ability, Damage, Definition, Lifecycle, NPCStartup, Threat 6개 모두 성공. 숫자 경계값과 spec 지속시간 변경 검사 포함.
- 전용 서버 모드 + 원격 클라이언트 2개: `Saved/Tests/BossState/9727c07274804b228a50d8c438440dd8/` — Iris 활성화, 피해/위협도, 중도 참가, 이전 세대 피해 거부, 치명타격·사망 태그 복제, 접속 종료 후 Ability 취소·대체 타겟·위협도 초기화 성공.
- 리슨 호스트 + 원격 클라이언트 2개: `Saved/Tests/BossState/e28969a71d18428a98e7f01f8093b60b/` — 같은 시나리오 성공. 세 로그 모두 Iris 활성화 및 치명적 오류·assert·ensure·Iris/Net 오류 부재 확인.

## 16. 보스 단일 타겟 서버 공격 (2026-09-25)

당시의 GE 적용 성공/0 피해 설명과 현재의 서버 피해 receipt 승인 계약은 구분한다. 2026-10-08 변경 및 회귀는 [BOSS_DAMAGE_EFFECT_CONTRACT.md](BOSS_DAMAGE_EFFECT_CONTRACT.md)를 따른다.

`URPGBossGameplayAbility::ExecuteTargetedServerDamage`는 기존 HitQuery → 피해 GE → AttributeSet 경로를 보스 Ability에 연결한다. 서버가 활성화 시 저장한 위협도 타겟만 공격하며, 결과 HP는 기존 ASC의 Iris 복제로 전달한다. 새 RPC나 별도 피해 복제 필드는 없다.

### 호출 계약

1. 구체 Ability에서 `CommitAbility` 성공을 확인하고 서버가 소유한 적중 시점에 호출한다. 이 함수가 비용/쿨다운을 대신 커밋하지 않는다.
2. 서버 위치에서 만든 QueryTransform, 공격 형상/필터, Instant 피해 GE 클래스, 기본 피해와 SecurityProfile을 전달한다. `HostileOnly`, 자기 자신 제외, 권한 HitQuery 설정을 요구한다. 가시선은 HitProfile의 설정을 따른다.
3. 실제 물리 조회 결과 중 저장된 위협도 타겟이 있어야 하며, 적용 직전 전투 세대·페이즈·타겟·Controller를 다시 확인한다. 조회 최대 결과 수는 SecurityProfile 상한으로 제한되므로 타겟이 결과에서 잘리면 실패한다.
4. 성공한 GE 적용은 실행당 한 번만 허용한다. 적용 중 동기 콜백의 재진입도 거부하며, 콜백이 Ability를 재시작해도 이전 실행의 완료가 새 실행의 중복 방지 상태를 덮어쓰지 않는다. 실패한 조회/GE 적용은 같은 실행에서 재시도할 수 있다.
5. 반환 true는 GE 적용 성공이다. 방어력·반올림 등으로 실제 HP 감소는 0일 수 있다. Duration/Periodic GE는 거부한다. 다중 타격·광역 피해·DOT는 별도 실행 계약이 필요하다.

GAS 콜백의 임의 외부 변경을 롤백하는 기능은 없다. 타이머/몽타주 이벤트의 실행 토큰과 종료 정리, 연출, 공격 BP/BT 에셋 연결은 구체 Ability의 책임이다. 네이티브 테스트 공격은 즉시 실행하며 실제 몽타주/텔레그래프/GameplayCue를 검증하지 않는다.

### 팀 판정 통합

`UTeamManager::FindTeamFromObject`는 표준 `IGenericTeamAgentInterface`도 조회한다. 기존 구현은 프로젝트 확장 인터페이스만 조회해 실제 RPG AI/플레이어 컨트롤러를 NoTeam으로 처리했고, HitQuery의 HostileOnly가 대상을 배제했다. 직접 객체·Instigator·Owner·Pawn Controller·PlayerState의 기존 조회 순서는 유지했다. 이 수정은 TeamManager를 사용하는 일반 전투에도 적용되며, 확장 인터페이스의 팀 변경 API는 변경하지 않았다.

### 검증 구성과 결과

자동테스트는 실제 물리 overlap과 공유 팀 필터, 캡처한 타겟에만 피해 적용, 범위 밖 타겟 거부, 비타겟 HP 유지, 두 번째 호출/GE 콜백 재진입 차단, 지속 GE 거부를 검사한다. 기존 6종 보스 테스트도 함께 실행한다. 네트워크 테스트는 NPC/Boss 상속 캐릭터의 BT가 공격을 실행하고 첫 원격 클라이언트가 HP 69를 수신하는지 확인한다. 기존 늦은 참가·리셋/재시작·사망 태그·접속 종료 후 Ability 취소와 대체 타겟 검사도 유지한다.

격리 테스트의 공격자는 치명타 확률을 0으로 고정한다. 네트워크의 타겟/예비 타겟은 보스에서 100/150cm에 배치한다. 이전 `Automation-177cd0a17e2148778e44d8124d852c13`은 팀 조회 실패, 전용 서버 `f165c27f879d47b180e984adbc409ab7`은 공격 검사 실패였다. 위치 보완 후 `53c7e2b6733d4debafaaeb424e952880`은 통과했으나 리슨 서버 `607f5f6d0ff54eb29445f3444bc2dc7a`은 무작위 치명타로 HP 38을 기록해 고정 피해 검사를 통과하지 못했다. 이 실행들은 최종 보완본의 검증 근거로 사용하지 않는다.

최종 검증(2026-09-25, KST):

- 빌드: `Saved/Logs/BossAttackBuild-guard-20260925.log` — 컴파일·링크 성공, 종료 코드 0. 종료 시 사용자 폴더 Trace.uba 쓰기 권한 오류는 기존과 같이 남았다.
- 자동화: `Saved/Tests/BossState/Automation-42110f648448416ab3383b18d29f2229/automation.log` — Ability, Damage, Definition, Lifecycle, NPCStartup, Threat 6종 모두 통과.
- 리슨 호스트 + 원격 클라이언트 2개: `Saved/Tests/BossState/f5ce2ddff39641e6be94cff922f8f115/` — 모든 검사 통과, 스크립트 종료 코드 0.
- 전용 서버 모드 + 원격 클라이언트 2개: `Saved/Tests/BossState/550e35909f934a6f988bf94e93a579bb/` — 모든 검사 통과, 스크립트 종료 코드 0.

두 구성에서 서버 단일 피해 적용/중복 차단, 클라이언트 HP 복제, 기존 상태·사망·리셋·접속 종료 검사를 통과했다. 서버와 각 클라이언트는 Iris NetDriver를 사용하며 치명적 오류·assert·ensure·Iris/Net 오류 검사를 통과했다. 기존 CommonUI 및 에디터 모듈 로딩 경고는 별도 사항이다. 패키징된 서버, 지연/손실 환경, 실제 공격 콘텐츠와 연출은 아직 검증하지 않았다.

## 17. 진행 중인 보스 공격 예고 복제 (2026-09-25)

`URPGBossAIComponent`에 현재 공격 예고의 복제 스냅샷을 추가했다. `FRPGBossTelegraphState`는 활성 여부, 조우 ID, 단조 증가 순번, 공격 태그, 예고 위치, 서버 시각 기준 시작/종료 시각을 담는다. 예고는 서버만 시작·종료하며 한 번에 하나만 활성화할 수 있다. 잘못된 태그·시간은 거부하고, 지정 시간이 지나면 서버 타이머가 종료한다. 종료 호출은 조우 ID와 순번이 모두 맞아야 하므로 이전 Ability의 지연 콜백이 새 공격을 지우지 못한다. 와이프·리셋·사망 시 활성 예고를 해제하고 액터 종료 시 타이머를 정리한다. 18절의 지연 공격부터 자동 정리 타이머는 명시적 적중 종료보다 0.1초 늦게 실행된다.

`URPGBossGameplayAbility`의 `StartBossTelegraph`/`FinishBossTelegraph`가 이 스냅샷을 실행 생명주기에 연결한다. `EndAbility`는 취소를 포함해 예고를 종료한다. 블루프린트 클라이언트는 `GetTelegraphState`를 먼저 읽고 `OnTelegraphChanged`를 구독해야 한다. 현재 스냅샷은 예고 표현용이며 적중 판정·피해 권한을 부여하지 않는다. 실제 몽타주, 장판/GameplayCue, 공격별 위치·지속시간 데이터와 패키징/지연·손실 환경 검증은 후속 작업이다.

검증: `Saved/Logs/BossTelegraphBuildFinal-20260925.log` 빌드 성공; `Saved/Tests/BossState/Automation-05b57b4763cc427c820177807f0a1c0e/automation.log` 보스 자동화 6종 통과; 전용 서버 `Saved/Tests/BossState/0bedb0ac9f414c78800d622ee3952439/` 및 리슨 서버 `Saved/Tests/BossState/623c66938ad747979a2534d4c05e7685/`에서 Iris 활성화, 첫/늦은 접속 클라이언트의 예고 활성·종료, 기존 공격/리셋/접속 종료 시나리오를 모두 통과했다.

## 18. 설정형 예고 후 단일 타겟 공격 Ability (2026-09-26~27)

`URPGBossTelegraphedStrikeAbility`는 실제 보스 공격 BP의 공용 부모로 쓸 수 있다. 공격 태그, 예고 시간, 구형 적중 반경, Instant 피해 GE, 기본 피해, 보안 프로필을 BP 기본값으로 설정한다. 서버 Ability가 활성화되면 비용/쿨다운을 Commit한 뒤 현재 Threat 타겟의 위치를 한 번 저장하고 17절의 예고를 게시한다. 예고 시간이 지나면 저장된 위치에서 기존 HitQuery와 GAS 피해 경로를 한 번 실행한다. 적중 직전에도 같은 전투·페이즈·타겟·Controller 및 예고 세대를 검사한다. 타겟이 원래 예고 위치를 벗어나면 빗나간다. 정상적인 빗나감은 공격 실행 완료로 취급한다.

설정값은 유효한 태그, 0.01~120초 예고, 0보다 크고 2500cm 이하인 반경, 양의 유한 피해, Instant GE와 유효한 서버 적중 보안 프로필이어야 한다. HitQuery 후보는 보안 프로필의 상한으로 제한하며 그 안에서 저장된 Threat 타겟만 피해를 입힌다. BT 중단·Ability 취소·페이즈 변경·전멸·사망 시 타이머와 예고를 정리하고 지연 콜백은 실행 번호로 무효화한다. 실제 보스 콘텐츠는 이 클래스를 상속한 BP에 설정을 넣어 StartUpData로 부여하고, 기존 `Execute Boss Ability` BT Task의 AbilityClass로 지정한다.

복제 스냅샷은 공격 태그·중심점·시간만 담는다. 클라이언트 장판 크기는 태그에 대응하는 연출 데이터로 정해야 한다. 몽타주·GameplayCue·장판 메시·지형 접지·가시선 강제·광역 다중 대상 피해는 아직 이 공용 구형 공격에 포함되지 않는다. 이 단계의 검증은 에디터 실행 파일의 전용 서버 모드와 리슨 서버다.

최종 검증(2026-09-26~27): 빌드 `Saved/Logs/BossStrikeBuildVerified-20260926.log`, 전용 서버 `Saved/Tests/BossState/dd21fcaae7654d6396b0543ffd189ba5/`, 리슨 서버 `Saved/Tests/BossState/e622bc857a1346c183cb1ccd7d26b82f/` 모두 통과했다. 두 원격 클라이언트는 공격 예고와 피해 HP 38을 받았고, 서버는 예고 중 피해 없음·회피 시 피해 없음·BT 취소 후 예고와 타이머 정리를 확인했다. 기존 늦은 참가·리셋·사망·접속 종료 검증도 통과했다. 리슨 서버 `67cd342742364527a1f8e7ab37de0fdb` 실행은 초기화 후 중단되어 성공 근거로 사용하지 않는다.

기반 자동화는 공용 예고 콜백 보강 후 `Saved/Tests/BossState/Automation-43afeeef36ce4d24aa7a40b594e47d52/automation.log`에서 보스 6종이 통과했다. 마지막 공격 활성화 조건·후보 상한 변경은 위 최종 네트워크 시나리오에서 검증했다.

## 19. 실제 보스전 GameMode 연결 (2026-10-01, 빌드·격리 세션 테스트 통과)

`RPGBossBattleGameMode`는 이제 `ARPGBossCharacter`를 서버에서 생성한다. NPC StartUpData와 보스 AI, 스폰 지점 주변의 생존 플레이어 준비가 끝난 뒤에만 전투와 제한 시간을 시작한다. 참가자는 보스 스폰 지점 반경 `ParticipationRadius`(기본 15000cm) 안에서 생존 상태로 등록되며, 전투 중 늦게 들어온 플레이어도 등록된다. 비참가자는 전멸 판정에 영향을 주지 않는다. 보스 AI가 Dead가 되면 승리하고, 제한 시간 만료·등록된 참가자 전원 사망/접속 종료·살아 있는 보스의 예기치 않은 파괴는 패배다. 서버의 `RestartBattle`은 기존 보스를 파괴하고 새 보스를 생성해 보스 ASC/HP/공격 타이머를 새 인스턴스로 시작한다. 등록된 플레이어 Pawn은 최대 HP와 사망 태그·이동 상태를 복구한다. Pawn이 이미 파괴되었거나 빙의가 해제된 경우의 재생성/재빙의는 아직 별도 부활 시스템이 필요하다. 패배/승리 이후 자동 재시작하지 않는다.

`RPGBossBattleGameState`가 보스 참조·전투 결과·서버 종료 시각을 단일 복제 스냅샷으로 제공한다. UI는 `GetBattleSnapshot`을 즉시 읽은 뒤 `OnBattleSnapshotChanged`를 구독하고, 남은 시간은 `GetRemainingBattleTime`으로 계산한다. 기존 `RPGBossBattleInfoWidget`은 이 시각을 사용하며 `OnBattleSnapshotUpdated` BP 이벤트를 제공한다. 이제 로컬 `RPGPlayerController`가 각 클라이언트에서 스냅샷을 구독하고 기존 승리/패배 위젯을 한 번만 표시하며, 늦게 참가한 클라이언트도 현재 결과를 표시한다. 기존 GameMode BP의 `DisplayResultWidget` 호출은 호스트 사운드만 처리해 결과 위젯 중복을 막는다. 클라이언트는 승패를 결정하거나 서버 GameMode를 읽지 않는다.

에셋 연결: 기존 `BP_SwordMan`은 `BP_RPGNPCBase`의 자식이므로 원본 둘 다 보존했다. `Scripts/Editor/ConfigureRaidSwordMan.py`가 `BP_RaidNPCBase`를 복제 후 `ARPGBossCharacter`로 리패런트하고, `BP_RaidSwordMan`을 복제 후 새 베이스로 리패런트했다. `DA_RaidSwordMan`에는 `Boss.Phase.One`(100%)과 `Boss.Phase.Two`(70%)를 넣었고, 보스 AI 컴포넌트의 Definition과 `BP_BossBattleGameMode`의 BossClass/GameStateClass를 지정했다. 이후 레이드 전용 BT/StartUpData 및 공격 BP 연결은 20절을 따른다. HUD 장판 연출은 아직 없다. `RPGPlayerController::RequestBossBattleRestart`는 결과 스냅샷을 확인한 뒤 소유 컨트롤러 RPC로 요청하고, 서버 GameMode는 등록 참가자 및 종료 상태만 허용한다. 패배 위젯 인스턴스의 `RetryButton` 기존 `OpenLevel` 클릭 바인딩을 로컬에서 제거하고 이 RPC 경로로 연결한다. BP 에셋 자체의 그래프는 원형 유지한다.

검증: `Saved/Logs/RaidContentBuild.log`에서 Editor 타깃 컴파일·링크 성공(종료 코드 0), `Saved/Tests/BossState/Automation-06031c962ee5443c967bbe233e09d5b7/automation.log`에서 BattleSession을 포함한 보스 자동화 7종이 통과했다. AppData 설정 쓰기 차단은 프로젝트 내부 XmlConfigCache와 로그/UBA 루트를 지정하고, 기존 Editor `Makefile.bin`만 고유한 백업으로 옮겨 재생성하여 우회했다. 종료 시 사용자 폴더 Trace.uba 쓰기 경고는 남는다. 이 검증은 GameState의 세션 계약과 기존 격리 AI/GAS 테스트이며, 실제 보스전 맵의 참가자 자동 등록·전멸·재시도 RPC/UI를 통합 검증한 결과는 아니다. 해당 맵의 전용/리슨 서버 두 원격 클라이언트·늦은 참가·전멸/재시도 검증, 패키징된 서버와 지연/손실 환경은 남아 있다.

후속 보강: GameState는 같은 월드의 서버 보스만 받아들이고, 실제 BossAI InCombat 전에는 제한 시간을 시작하지 않는다. 승리는 BossAI Dead, 패배는 살아 있는 보스라면 BossAI Wiped를 요구한다. 살아 있는 이전 보스가 남아 있으면 새 시도를 준비하지 않는다. GameMode는 GameState가 결과를 받아들였을 때만 로컬 상태를 바꾼다. `ProjectRPG.Boss.BattleSession` 자동테스트는 준비 전 시작 거부, 타이머 중복 시작 거부, 가짜 승패 거부, 전멸 후 재시작 및 새 보스 사망 승리를 검사한다. 자동테스트 실행 스크립트도 이 테스트의 성공 마커를 요구하며 위 실행에서 통과했다.

## 20. SwordMan 레이드 전용 GAS/BT 콘텐츠 (2026-10-01)

`Scripts/Editor/ConfigureRaidSwordManCombat.py`가 레이드 전용 에셋을 생성·저장하고 `BP_RaidSwordMan`에 연결한다. `-RPGApplyRaidSwordManCombat` 스위치가 있을 때만 저장하며, 스위치 없이 실행하면 저장된 참조를 읽기 전용으로 검사한다. 기존 SwordMan 에셋은 수정하지 않는다. 공용 서버 판정은 C++를 재사용하고 보스별 수치와 패턴 조합은 BP/BT에 둔다.

- `GA_RaidSwordMan_Strike`: 예고 1.2초, 구형 적중 반경 200cm, 기본 피해 20.
- `GA_RaidSwordMan_HeavyStrike`: 예고 1.6초, 반경 300cm, 기본 피해 35.
- 두 공격 모두 `URPGBossTelegraphedStrikeAbility`를 상속하며 `GE_Shared_ApplyDamage`를 사용한다. 실제 피해는 기존 공격력·방어력·치명타 계산을 따른다. 광역처럼 보이는 반경도 현재 계약에서는 캡처한 단일 타겟만 피해를 받는다.
- `DA_RaidSwordManStartUp`은 원본의 초기 Effect·HUD 및 HitReact/Death Ability를 보존하고, 수동 공격 목록만 두 레이드 공격으로 교체한다.
- `BB_RaidSwordMan`은 기존 TargetActor 키에 BossInCombat/BossPhaseIndex/BossEnraged 키를 추가한다.
- `AIC_RaidSwordMan`의 BTAsset은 `BT_RaidSwordMan`이며, 새 컨트롤러와 StartUpData를 레이드 BP에 지정한다.

BT 루트의 `BTService_RPGBossState`가 서버 상태를 읽는다. 전투·타겟 조건을 충족하면 이동→방향 정렬→페이즈별 Ability 실행→회복 대기 순으로 진행한다. 1페이즈는 기본 공격 후 2초, 2페이즈는 강공격 후 1.5초 대기한다. 조건에는 Observer Abort를 설정하여 대기/공격 중 변경을 반영한다. 이동 성공은 근접 도달 판정이며 실제 맵의 먼 거리 추적에는 NavMesh가 필요하다. 공격 예고 중심은 활성화 시 서버가 저장한 타겟 위치다.

생성된 BT는 런타임 노드를 저장한다. 저장된 편집 그래프가 없을 때 엔진 BehaviorTreeEditor가 첫 편집 시 런타임 노드로 그래프를 복원하는 경로를 사용한다. 커맨드릿에서 원본 그래프를 복사하거나 서버 트리를 별도 C++로 하드코딩하지 않는다.

실행 검사에서 발견한 보완:

- 페이즈 변경 이벤트 도중 Ability가 취소되면 재진입 차단 때문에 EndTelegraph가 거부될 수 있다. BossAI가 페이즈 전환을 게시하기 전에 기존 예고를 직접 정리해 다음 페이즈의 공격이 막히지 않게 했다. Lifecycle 테스트는 즉시 정리·새 예고 시작·옛 순번 콜백 거부를 검사한다.
- 기존 회전 Task는 타겟 소멸 시 실패 완료 후에도 잘못된 약한 참조를 사용했다. 메모리를 비우고 즉시 반환하도록 보완했다.

`ProjectRPG.Boss.RaidContent`는 저장된 BT·페이즈 DA·두 공격 BP를 실제 로드하고 격리 GAS Pawn에서 실행한다. 근접 도달 검사를 위해 테스트 Pawn에 이동 컴포넌트를 제공하고, 타이머가 같은 엔진 프레임에서 중복 틱을 막는 특성에 맞춰 프레임 번호를 해당 월드 틱 동안만 진행시킨 뒤 복원한다. 대기 중 공격 금지, 예고 중 피해 없음, 공유 GE 피해, 공격 중 70% 페이즈 전환, 강공격, 전멸 중 지연 피해 금지, 새 조우, 타겟 빙의 해제 및 회전 중 타겟 소멸을 검사한다. 자동화 실행 스크립트는 이 테스트를 포함한 8종의 성공 마커를 요구한다.

검증: 에셋 저장 후 참조 검사는 `Saved/Logs/RaidCombatApply.log`와 `Saved/Logs/RaidCombatVerify.log`에서 통과했다. 런타임 보완 후 `Saved/Logs/RaidPhaseClearBuild.log`에서 컴파일·링크 성공(종료 코드 0), `Saved/Tests/BossState/Automation-d637a30d7b574936a9bad833d121635c/automation.log`에서 BattleSession/RaidContent를 포함한 보스 자동화 8종이 모두 통과했다. 기존 Trace.uba 사용자 폴더 쓰기 경고는 별도로 남는다. 이전 실패 실행은 성공 근거로 사용하지 않는다.

동일 바이너리의 기존 Iris 네트워크 프로브 회귀 결과: 전용 서버 모드 + 원격 클라이언트 2개는 `Saved/Tests/BossState/bd962090cbcb4036a5c05dea5089114c/`, 리슨 호스트 + 원격 클라이언트 2개는 `Saved/Tests/BossState/bef4aa70db804e0bb969e9639d35e67a/`에서 모두 통과했다(스크립트 종료 코드 0). 예고/HP 복제, 늦은 참가, 리셋·재전투·사망, 접속 종료 시 BT/GAS 취소 및 대체 타겟을 검사한다. 이 프로브는 네이티브 격리 캐릭터/트리를 사용하며 새 레이드 에셋을 원격 맵에서 실행한 검사는 아니다. 패키징된 Dedicated Server 검증과도 구분한다.

남은 범위: 실제 보스전 맵의 참가자/승패/재시도 통합 검증, 파괴된 플레이어 Pawn 재생성·재빙의, 복제 스냅샷 기반 장판 표시와 몽타주/Cue, 추가 기믹 및 소환물 정리, 패키징·지연/손실 검증. 격리 에셋 실행 테스트나 기존 네트워크 프로브를 실제 보스전 맵 전체의 통과 결과로 간주하지 않는다.

## 21. 복제 예고 기반 로컬 장판 표시 (2026-10-03~04, 검증 완료)

`URPGBossTelegraphPresentationComponent`를 BossCharacter의 기본 컴포넌트로 추가한다. 일반 NPC, GAS 적중 판정, RPC 및 Iris 스냅샷 필드는 바꾸지 않는다. 스킬의 서버 권한 원칙에 따라 장판은 표시만 담당하고, 피해·페이즈·공격 종료 결정은 기존 서버 코드에 남긴다.

- 태그별 `FRPGBossTelegraphStyle`은 반경·색상·소프트 머티리얼 참조를 갖는다. 반경은 서버 Ability 수치와 맞아야 하며, 모르는 태그·중복 태그·잘못된 설정은 표시하지 않는다.
- BeginPlay에서 변경 이벤트를 구독한 뒤 **현재 스냅샷을 즉시 읽는다**. 연출 머티리얼은 비동기로 로드하고 완료 시 오래된 공격을 재생하지 않고 최신 상태를 다시 읽는다.
- 진행률은 GameState 서버 시각과 복제된 시작/종료 시각으로 계산한다. 중도 참가자는 0부터 재생하지 않는다. 서버 clear가 늦더라도 종료 시각 이후에는 표시를 숨긴다. 클라이언트에 GameState가 아직 없으면 시각이 준비될 때까지 기다린다.
- 전투/예고 속성은 다른 순서로 도착할 수 있으므로 둘 다 구독한다. 조우 ID가 다르거나 InCombat이 아니면 표시하지 않는다. 취소·페이즈 전환·전멸·리셋·사망은 예고/상태 변경으로 숨긴다.
- 데칼은 로컬 비복제 컴포넌트이며 보스에 부착하지 않는다. 서버가 저장한 중심점은 보스 이동에 따라 끌려가지 않는다. 시작 시 WorldStatic 지형을 한 번 조회하여 접지하고, 없으면 저장된 위치를 사용한다. 지형 높이 보정은 연출만 바꾸며 서버 구형 판정을 변경하지 않는다.
- 전용 서버에서는 이벤트 구독·머티리얼 로드·데칼 생성을 건너뛴다. 리슨 호스트/단독 실행은 로컬 표시를 사용한다. 활성 예고 동안만 30Hz로 진행률을 갱신한다.
- EndPlay에서 delegate·로드 핸들·로컬 데칼을 정리한다. 클라이언트 타이머로 새로운 공격이나 피해를 만들지 않는다.

`Scripts/Editor/ConfigureRaidSwordManPresentation.py`는 `-RPGApplyRaidSwordManPresentation` 스위치로 원형 링+진행률 채움 데칼 머티리얼 `M_RaidSwordManTelegraph`를 생성하고 레이드 BP의 두 공격 스타일을 연결한다. 스타일 태그/반경은 저장된 서버 공격 BP에서 읽어 초기 설정을 맞춘다. 스위치가 없으면 저장된 연결만 검사한다. 외부 텍스처나 추가 플러그인은 필요하지 않다. 이후 공격 반경을 바꾸면 연출 설정도 갱신하고 검증해야 한다.

검증 추가: `ProjectRPG.Boss.Presentation`은 저장된 BP의 스타일/머티리얼, 서버 반경 일치, 중도 참가 진행률, 시각/조우/상태 경계, 실제 로컬 데칼 생성·위치 고정·취소/페이즈/전멸 정리를 검사한다. 기존 네트워크 프로브는 두 원격 클라이언트의 표시/제거 및 전용 서버 생략 마커를 추가로 요구한다. NullRHI 검사는 컴포넌트·머티리얼 연결과 동기화 검증이며 실제 화면 픽셀의 품질 검증은 아니다.

검증 기록 (2026-10-04, KST):

- 빌드: `Saved/Logs/RaidPresentationTestsBuild.log` — Editor 타깃 컴파일·링크 성공. 기존 Trace.uba 사용자 폴더 쓰기 경고는 별도로 남는다.
- 에셋: `Saved/Logs/RaidPresentationVerify-20261004.log` — 별도 프로세스에서 저장된 머티리얼과 두 공격의 스타일 태그·반경·참조 확인 성공, 종료 코드 0. 첫 적용 실행의 검증 실패는 Python GameplayTag 래퍼 비교 문제였으며, 태그의 텍스트 값 비교로 수정했다. C++ 자동화에서는 네이티브 태그 값 비교도 통과했다.
- 자동화: `Saved/Tests/BossState/Automation-fbf328f2f5f74d2ab57dee97a0a88a30/automation.log` — Presentation/BattleSession/RaidContent를 포함한 보스 9종 모두 성공, 실행 스크립트 종료 코드 0.
- 전용 서버 모드 + 원격 클라이언트 2개: `Saved/Tests/BossState/362101a581854fb4b7245a0b431800ed/` — Iris 활성화, 두 클라이언트 장판 표시/제거·중도 참가, 전용 서버 표시 생략 및 기존 피해·리셋·재전투·사망·접속 종료 시 BT/GAS 취소/대체 타겟 검사 성공, 스크립트 종료 코드 0.
- 리슨 호스트 + 원격 클라이언트 2개: `Saved/Tests/BossState/3eb6c09ee3994b119905eb765d65e6e9/` — 같은 원격 클라이언트 표시/제거 및 기존 회귀 검사 성공, 스크립트 종료 코드 0. 호스트 화면의 GPU 렌더링 검사는 아니다.

현재 상태: 장판 코드·머티리얼·레이드 BP 연결, 격리 자동화 9종 및 전용/리슨 서버 Iris 회귀 검증 완료. 네트워크 프로브는 네이티브 격리 캐릭터에 같은 저장 머티리얼을 연결하며, 실제 레이드 맵/BP 전체의 원격 실행 검증과 구분한다. 테스트 스크립트가 생성한 프로세스는 종료 시 정리했다. 기존 CommonUI·누락된 음성 에셋·Iris 모듈 로딩 경고는 별도 사항으로 남아 있으며 프로젝트 전체 무오류 판정이 아니다. 몽타주·GameplayCue, 실제 보스전 맵 통합 및 GPU 화면 확인, 패키징·지연/손실 환경은 별도 남은 범위다.

## 22. 참가자 Pawn 소실 후 서버 재시도 복구 (2026-10-04)

19절에서 남겼던 Pawn 파괴/빙의 해제 복구를 `RPGBossBattleGameMode`에 연결한다. 별도 부활 프레임워크·RPC·Iris 복제 필드는 추가하지 않는다. 기존 참가자 승인 RPC와 엔진의 서버 `RestartPlayerAtTransform`/Possess 및 플레이어 GAS 초기화를 재사용한다.

- 최초 참가 등록 시 서버가 진입 Transform과 마지막 Pawn의 약한 참조를 저장한다. 이후 살아 있는 참가자의 Pawn 참조는 갱신하되 최초 진입 위치는 유지한다. Logout에서 참가 승인과 복구 기록을 함께 지운다.
- 생존 조건은 유효한 ASC Avatar, 유한 양의 HP, 사망 태그 없음이다. 사망 태그가 남은 플레이어를 단순 양의 HP만으로 생존자로 보지 않는다.
- 현재 Pawn이 없으면 엔진의 GameMode 기본 Pawn 선택/생성/빙의 경로를 사용해 등록 위치에 복구한다. 기존 미빙의 Pawn은 정리하며, 다른 Controller가 소유한 Pawn은 건드리지 않는다. 기존 Pawn을 재빙의하여 StartUp Ability/Effect를 중복 부여하지 않는다.
- 기존 참가자 Pawn은 기존 체력·사망 loose tag 복구를 유지한다. 결과 UI가 비활성화했던 이동 모드는 기본 Walking 복구보다 먼저 복원하여 Flying/Custom 모드를 덮어쓰지 않는다.
- 참가자 복구가 실패하면 결과 상태·기존 보스·참가 승인 정보를 유지한다. 같은 승인된 참가자가 다시 요청할 수 있으며, 실패 상태에서 새 보스 전투를 시작하지 않는다. 부분 복구는 롤백하지 않는다. 복구 중 중복 재시작은 가드로 거부한다.
- 복구 후에만 기존 보스를 파괴하고 다음 조우를 준비한다. 참가자 기록은 새 조우 시작 전에 비우고 새 살아 있는 Pawn을 다시 등록한다.

`ProjectRPG.Boss.BattleRetry`는 격리 월드에서 **운영 GameMode의 등록·전멸·재시도 메서드**를 실행한다. 테스트용 RPGPlayer/Boss 상속 캐릭터는 인메모리 GAS 초기화를 제공하고 Pawn 생성 실패만 주입한다. 범위 밖 플레이어, 중도 참가, 생존자 유무, 비참가자/전투 중/중복 재시도 거부, Pawn 파괴·빙의 해제, 실패 후 재시도, 진입 위치·단일 초기 빙의·체력 복구, 새 보스/재등록, Logout 후 전멸, 기존 Pawn 유지 및 이동 모드 복원을 검사한다. 자동화 실행 스크립트는 이 테스트를 포함한 10종을 요구한다.

검증 진행: `Saved/Logs/RaidRetryBuildFinal-20261004.log`에서 컴파일·링크 성공, 종료 코드 0. 최초 빌드는 작업 범위 밖 인벤토리 UI 소스/생성 헤더 불일치로 실패했고 `-ForceHeaderGeneration`으로 재생성한 뒤 빌드를 완료했다. 관련 UI 소스는 수정하지 않았다. 기존 Trace.uba 사용자 폴더 쓰기 경고는 별도다.

- 최종 자동화: `Saved/Tests/BossState/Automation-0ea2a2fcce7843739ebb0522100fdc9d/automation.log` — BattleRetry를 포함한 10종 모두 성공, 스크립트 종료 코드 0. 생성 실패를 의도적으로 주입한 검사에서 복구 대기 경고 1건은 예상된 출력이다.
- 이전 전용 서버 실행: `Saved/Tests/BossState/070ce681688f41048d9d80dce0ec9b86/` — 저장 로그를 재검사해 현재 스크립트의 필수 마커 45개와 치명적 오류·assert·ensure·Iris/Net 오류 부재를 확인했다. 원래 스크립트 반환값을 복원한 것은 아니다.
- 전용 서버 회귀 재실행: `Saved/Tests/BossState/3013bb7d05b14866882c570ccd827b5d/` — 서버와 원격 클라이언트 2개 구성의 전체 필수 검사 통과, 스크립트 종료 코드 0.
- 리슨 서버 회귀 재실행: `Saved/Tests/BossState/1935f587f90a4403b492cc5b575713f9/` — 호스트와 원격 클라이언트 2개 구성의 전체 필수 검사 통과, 스크립트 종료 코드 0.
- 회귀 자동화 재실행: `Saved/Tests/BossState/Automation-07cf7e0200b8494aaf0494cdacb143e3/automation.log` — 보스 10종 모두 통과, 스크립트 종료 코드 0.

리슨 서버의 첫 재실행 `46b2e018f74e4ea2ad30c20f66ad2e12`에서는 호스트만 참가한 시점에 예고를 시작해 원격 클라이언트의 초기 로드 도중 예고가 만료됐다. 네트워크 테스트 GameMode의 Stage 0을 첫 원격 참가 이후 시작하도록 수정했다(리슨 2명, 전용 1명). 공격의 최대 예고 시간과 검증 마커는 유지했으며 운영 GameMode/Ability는 변경하지 않았다. 수정 빌드는 `Saved/Logs/RaidListenTimingBuild-20261004.log`에서 성공했다. 이 실패 실행과 초기화 이후 중단된 `3d84066737644f9a85b4abc7f86c58df`는 성공 근거로 사용하지 않는다.

이 테스트는 실제 보스전 맵의 NavMesh·플레이어 BP 사망 몽타주/장비 복원·원격 Possess·재시도 버튼 UI까지 검증한 결과는 아니다. 사망 Ability가 지속 Effect/래그돌/지연 파괴를 남기는 기존 Pawn의 완전한 부활, 맵 기반 참가자 복구·RPC/UI, 패키징·지연/손실 환경 검증은 남아 있다.

## 23. 실제 보스전 맵의 읽기 전용 연결 검사 (2026-10-04)

`Scripts/Editor/VerifyRaidBossMap.py`는 `/Game/Maps/bossmap`을 커맨드릿으로 로드해 설정을 검사한다. 에셋 저장·액터 변경·PIE·UI 이벤트 실행은 하지 않는다.

- WorldSettings의 `BP_BossBattleGameMode`, 레이드 BossClass 및 네이티브 복제 BattleGameState 연결을 확인한다.
- 기본 Pawn과 PlayerController가 각각 RPGPlayer/RPGPlayerController 계층인지 검사하고 플레이어 StartUpData·입력 설정·MappingContext의 참조를 확인한다.
- 패배 위젯이 `WBP_DefeatWidget`인지 확인해 네이티브 RetryButton 템플릿 검사 대상과 실제 설정이 일치하도록 한다.
- 보스 스폰 TargetPoint가 정확히 하나인지, PlayerStart가 존재하고 모두 참가 등록 반경 안인지 확인한다.

검사 결과: `Saved/Logs/RaidMapPreflightFinalClasses-20261004.log` — 설정 검사 통과, 커맨드릿 종료 코드 0. 맵의 TargetPoint/PlayerStart는 각각 1개이며, 실제 플레이어는 `BP_Player`, 컨트롤러는 `BP_PlayerController`, 시작 데이터는 `DA_Player`다. 참조 존재 여부를 확인한 것이며 비동기 GAS 초기화 완료·실제 입력 동작을 검사한 결과는 아니다.

네이티브 위젯 템플릿 검사에서 실제 연결 오류를 발견했다. `RetryButton`은 `UButton`이 아니라 `TPWBP_MessageBoxButton` 커스텀 UserWidget이며, `OnButtonClicked` 디스패처로 클릭을 전달한다. 기존 Controller의 `Cast<UButton>`은 실패해 재시도 요청을 연결하지 못했다. `Private/UI/RPGBossBattleRetryBinding.h`의 작은 내부 헬퍼로 일반 UButton의 `OnClicked`와 현재 커스텀 위젯의 `OnButtonClicked`를 지원한다. 무인자 multicast 서명만 허용하며, 위젯 인스턴스의 기존 `OpenLevel` 바인딩을 제거하고 기존 Controller의 `RequestBossBattleRestart`에 연결한다. 내부 버튼의 클릭 전달/연출, BP 원본·CDO, 참가자 승인 RPC와 서버 권한 결정은 유지한다.

`ProjectRPG.Boss.Presentation`에 저장된 실제 패배 위젯의 RetryButton/서명 검사, 템플릿 복사본의 기존 바인딩 제거·운영 Controller 메서드 연결·반복 바인딩의 단일 핸들러 유지·일반 UButton/누락 위젯 처리를 추가했다. UI 초기화나 클릭/RPC는 실행하지 않는다. 최초 추가 검사 `Automation-f4258263a63a4cfb825c0ab4320c6153`는 타입 불일치를 검출한 실패 실행이며 성공 근거가 아니다.

수정 후 검증:

- `Saved/Logs/RaidRetryWrapperBuild-20261004.log` — 변경한 Controller/Presentation 테스트의 컴파일·링크 성공, 종료 코드 0. 기존 Trace.uba 사용자 폴더 기록 권한 경고는 별도다.
- `Saved/Tests/BossState/Automation-e7f3cb8a2d7a4c21a0f027d55f3d74b3/automation.log` — 수정된 버튼 바인딩 검사를 포함한 보스 자동화 10종 모두 통과, 스크립트 종료 코드 0.
- 앞선 빌드 `RaidMapButtonBuild-20261004.log`는 범위 밖 소스/생성 헤더 불일치 및 동시 작업의 컴파일 오류로 실패했다. `RaidMapButtonBuildRegenerated-20261004.log`는 기존 Makefile 검사 중 사용자 설정 폴더 쓰기 권한으로 실패했다. 기존 Makefile을 프로젝트 내부의 고유 백업으로 보관해 재생성한 `RaidMapButtonBuildFinal-20261004.log`는 성공했고, 이후 위 Wrapper 빌드에서 실제 수정본을 컴파일했다. 범위 밖 소스나 권한 설정은 변경하지 않았으며 실패 빌드는 성공 근거로 사용하지 않는다.

22절의 전용/리슨 서버 프로브는 버튼 수정 전 회귀 결과이며 원격 UI/RPC 통합 검사가 아니다. 이번 수정은 로컬 위젯 바인딩에 한정되며 서버 GameMode/Ability/Iris 계약은 바꾸지 않았다. 다음 검증은 실제 bossmap에서 중도 참가→전멸→원격 재시도 요청→서버 참가자 복구→새 조우까지의 통합 흐름이다.

맵 로드 중 직렬화된 NavMesh의 maxTiles 불일치에 따른 재생성 경고가 남는다. 종료 중 CrowdManager의 RecastNavMesh 탐색 경고도 있으므로 실제 경로 탐색 성공 여부는 별도 런타임 검증이 필요하다. 이 검사를 실제 보스전 맵의 원격 전투·전멸·재시도 버튼 클릭/RPC·Pawn 복구 통합 통과로 간주하지 않는다. GPU 화면·몽타주/Cue·장비/래그돌 복구 및 패키징·지연/손실 환경도 여전히 미검증이다.

## 24. 실제 bossmap 원격 재시도 통합 검사 준비 (2026-10-04~05, 당시 기록)

아래는 진단·빌드 차단 당시 기록이다. 이후 수정본의 최신 검증 결과와 남은 문제는 25절을 따른다.

`Scripts/Network/Run-BossMapE2ETest.ps1`과 `Tests/RPGBossMapE2EProbeComponent`는 실제 `/Game/Maps/bossmap` 및 저장된 GameMode/Player/Controller/Boss BP를 사용한다. 테스트 GameMode로 교체하지 않는다. Development의 명시적 `-RPGBossMapE2E` 실행에서만 GameMode/Controller에 컴포넌트를 생성하며 Shipping에서는 생성하지 않는다. 스킬의 서버 권한 원칙에 따라 테스트의 확인 RPC는 진행 장벽일 뿐이며, 참가 승인·전멸·Pawn 복구·새 보스 생성은 운영 서버 경로를 사용한다.

- 첫 원격 클라이언트가 진행 중 전투와 살아 있는 Pawn을 확인한 뒤 두 번째 클라이언트를 중도 참가시킨다.
- 서버에서 참가자의 실제 Pawn을 파괴한 뒤 운영 전멸 판정, 각 클라이언트의 실제 결과 위젯/재시도 바인딩을 확인한다.
- 생성된 위젯의 클릭 디스패처를 실행해 기존 `RequestBossBattleRestart`/운영 RPC를 거친다. 물리적 마우스 입력 검사는 아니다.
- 새 Pawn/보스, 원격 로컬 빙의, GAS Avatar·최대 체력, InputComponent·이동 모드, 결과 위젯 제거를 요구한다.
- 차가운 클라이언트 로드 동안 전멸하지 않도록 테스트 인스턴스의 제한 시간을 늘리고 보스 Brain을 정지한다. 에셋을 저장하지 않으며 자연 공격·NavMesh·몽타주/Cue·래그돌 복구를 검증한 것으로 간주하지 않는다.
- 로컬 검증용 기존 `RPGNetTestMode`를 사용한다. 자식 프로세스에서 던전 할당/서버 인증 환경변수를 제거하고 종료 시 복원한다. 백엔드 입장·정산 검증은 아니다. 종료 시 자신이 생성한 프로세스만 정리한다.

현재 증거:

- `Saved/Tests/BossMap/265538cad13a4734bad8ad0765c27817/`: 실제 맵/클래스와 첫 원격 접속은 실행됐다. 서버·클라이언트 양쪽에서 플레이어 ASC 복제, 해당 Pawn의 Avatar, HP 300을 확인했지만 BattleState는 Waiting에 머물렀다. 조사 중 중단했으며 통과 결과가 아니다. 앞선 `f5bcfa5ec2ce4e00b80c543ce199a8a7`도 통과 결과가 아니다.
- `Saved/Logs/RaidMapE2EBuildDiagLink-20261004.log`: 기존 진단 컴포넌트 빌드 성공. 이후 보스 준비/HP/상태, 실행 중 Mode Tick, 참가 거리/반경, 사망 태그 진단을 추가했으며 **이 마지막 C++ 변경은 아직 빌드·실행하지 못했다**.
- `Saved/Tests/BossState/Automation-e77d166d598f46fb84523ae724098905/automation.log`: 마지막 추가 진단 이전, 테스트 스위치를 끈 보스 자동화 10종 통과. 실제 맵 통합 통과와 구분한다.
- `Saved/Logs/RaidMapTickPreflightFinal-20261005.log`: 읽기 전용 검사에 Tick 기본 설정 로그를 추가했다. 저장된 BP의 `StartWithTickEnabled=True`, `AllowTickOnDedicatedServer=True` 및 기존 맵 연결 검사 통과, 커맨드릿 종료 코드 0. 실행 중 Tick 여부나 보스 준비 완료를 입증하지 않는다. 첫 검사 `RaidMapTickPreflight-20261005.log`는 Python에 노출되지 않는 CanEverTick 조회로 실패했으며, 노출된 두 속성만 조회하도록 수정했다.

2026-10-05 빌드는 다른 실행 중 Unreal 프로세스가 끝나기를 90초 기다린 뒤 진행하지 않고 종료했다. 다른 작업의 프로세스나 에디터는 종료하지 않았다. 다음 단계는 에디터/다른 테스트의 종료 후 추가 진단을 빌드하고, 실제 Waiting 조건을 확인해 수정한 뒤 전용/리슨 서버 및 자동화 검증을 수행하는 것이다. 아직 원인을 확정하거나 운영 전투 코드를 추정 수정하지 않았다.

### 2026-10-05 후속 진단 및 수정 (빌드·회귀 검증 대기)

`Saved/Logs/RaidMapModeStateBuildFinal-20261005.log`에서 추가 진단을 컴파일·링크했고 종료 코드 0을 확인했다. 기존 사용자 폴더 Trace.uba 기록 경고는 남았다. 앞선 `RaidMapStartRuntimeBuild-20261005.log`와 `RaidMapModeStateBuild-20261005.log`는 공유 Makefile 검사 중 사용자 설정 폴더 권한 오류로 실패했다.

실행 `Saved/Tests/BossMap/8dc634f84b554ce49b892eaa2917a1f6/`에서 GameState는 Waiting(3), GameMode 내부는 InProgress(0), BossAI는 Idle(0)인 **상태 불일치**를 확인했다. 보스 NPCReady=1/HP=300, 플레이어 Avatar/HP=300/사망 태그 없음, 참가 거리 약 723cm/반경 15000cm였다. 이 불일치 때문에 `TryStartBattle`의 Waiting 가드를 통과하지 못하고 잘못된 전투 중 분기로 진입한다. 읽기 전용 검사 `RaidMapInitialStatePreflight-20261005.log`에서는 저장된 BP CDO가 Waiting인 것도 확인했다. 이 커맨드릿은 맵 검사 마커는 출력했지만 동시 작업 중 RPGClientUI GameFeatureData 누락 오류로 전체 종료 코드 1이므로 전체 통과로 기록하지 않는다. CDO 이후 런타임 초기화에서 값이 바뀌었으며, 정확한 BP 그래프 쓰기 노드는 아직 추적하지 않았다.

운영 `RPGBossBattleGameMode::BeginPlay`에서 부모/Blueprint 초기화 이후 로컬 상태를 복제 GameState의 현재 스냅샷과 맞추도록 수정했다. 상태를 무조건 Waiting으로 덮어쓰지 않아 이미 정상적으로 시작된 세션도 보존한다. 맵 프로브는 GameMode/스냅샷 불일치를 즉시 실패로 검출하도록 보강했다. **이 운영 수정과 새 검증 조건은 아직 빌드·실행하지 않았다.** 위 진단 실행과 앞선 `27121e7adc6646b3a793e78c0b25d2d7`은 원인 조사 후 중단했으며 통합 통과 근거가 아니다. 제가 시작한 서버/클라이언트는 정리했고 다른 작업의 프로세스는 건드리지 않았다. 다음 작업은 수정 빌드 → 실제 맵 전용 서버 → 리슨 서버 → 보스 자동화 10종 회귀 검사다.

### 초기화 회귀 검사 보강 및 빌드 차단 (2026-10-05)

`ProjectRPG.Boss.BattleRetry`의 테스트 GameMode가 운영 `BeginPlay`를 우회하지 않도록 변경했다. Deferred Spawn 중 로컬 InProgress/복제 Waiting 불일치를 주입하고 실제 TargetPoint 및 운영 초기화 경로를 실행한다. 준비된 플레이어가 없으면 GameMode와 GameState 모두 Waiting을 유지하는지 검사한 뒤 기존 참가자·전멸·재시도 검사를 진행한다. 에셋/CDO는 수정하지 않으며 별도 테스트 종류를 늘리지 않는다.

`Saved/Logs/RaidInitialSyncBuild-20261005.log`에서 변경한 GameMode, 맵 프로브, BattleRetry 테스트/fixture의 C++ 컴파일은 완료됐다. 그러나 다음 작업 범위 밖 오류로 **전체 빌드 실패, 종료 코드 1**이므로 수정 DLL 링크 및 런타임 검증은 아직 완료되지 않았다.

- `Tests/RPGDungeonRewardE2EGameMode.cpp:121`, `UI/MVVM/RPGInventoryProjectionViewModel.cpp:52`: 존재하지 않는 `GetDungeonRewardDeliveryNotice()` 호출. 현재 PlayerState는 `GetRewardDeliveryNotice()`를 제공한다.
- `RPGInventoryProjectionViewModel`의 `HandleDungeonRewardDelivered`는 값 인자이며 PlayerState multicast는 `const FRPGDungeonRewardDeliveryNotice&` 인자이므로 AddUniqueDynamic/RemoveDynamic 서명이 일치하지 않는다.
- `PawnCombatComponent.h`, `RPGAuthoritativeEquipmentComponent.h`에서 UCLASS/GENERATED_BODY 관련 컴파일 오류도 함께 관측했다. 생성 헤더 정합성 확인이 필요하며 원인을 확정하지 않았다.

다른 작업의 소스는 변경하지 않았다. 기존 DLL로 검증을 실행해 수정본 통과로 오인하지 않도록 전용/리슨 서버 및 자동화 실행은 보류했다. 위 외부 빌드 오류 해결 후 수정본 전체 빌드와 회귀 검증을 다시 진행해야 한다. 이번에는 테스트 프로세스를 시작하지 않았고 빌드 프로세스는 종료됐다.

## 25. 실제 맵 초기화·OnGiven 수정 및 원격 재시도 검증 (2026-10-05, 당시 기록)

당시 미해결 RPC의 식별과 변경된 복구 검사 범위/최신 검증 결과는 26절을 따른다.

### 운영 코드 수정

- `RPGBossBattleGameMode::BeginPlay`가 부모/Blueprint 초기화 이후 로컬 전투 상태를 GameState의 현재 스냅샷과 맞춘다. 무조건 Waiting으로 초기화하지 않으며 유효한 진행 상태를 보존한다. 맵 프로브도 두 상태의 불일치를 실패로 검사한다.
- 첫 재시도 실행 `Saved/Tests/BossMap/00ffe7b8586e408588c6154b977363bc/`에서는 실제 맵 전투 시작·중도 참가·전멸·원격 버튼/RPC까지 도달했으나, 새 Pawn의 무기 등록 중복 assert로 실패했다. 읽기 전용 검사 `Saved/Logs/RaidWeaponPolicyPreflight-20261005.log`에서 실제 `GA_SpawnWeapon`의 LocalPredicted 실행 정책을 확인했다.
- `URPGGameplayAbility::OnGiveAbility`의 OnGiven 자동 활성화는 서버만 시작한다. 원격 LocalPredicted/LocalOnly 실행은 GAS의 기존 `ClientTryActivateAbility` 경로로 전달되므로 클라이언트의 복제 spec 수신 시 별도로 다시 예측 활성화하지 않는다. OnGiven 종료 시 spec 제거 역시 서버만 수행한다. 기존 무기 중복 `checkf`, 수동 입력/OnSpawn 정책과 보스 ServerOnly 공격은 유지한다. 스킬의 서버 권한 원칙에 따라 클라이언트에 능력 부여·제거 권한을 추가하지 않았다.
- BattleRetry fixture는 운영 BeginPlay를 실행하고, `FinishSpawning` 중 엔진이 만든 실제 GameState를 그 이후 조회한다. 별도로 만든 GameState를 미리 연결하면 엔진 PreInitializeComponents에서 교체되어 잘못된 snapshot을 검사하게 된다. 최초 보강 실행 `Automation-599a59e566664e4d95995e67fbf5efc6`의 Boss=null 실패는 이 fixture 문제였으며 수정 후 기존 10종을 다시 실행했다.

### 빌드 및 검사 결과

24절의 인벤토리/보상 API·생성 헤더 불일치는 재확인 시 동시 작업의 수정이 이미 반영되어 있었다. 별도 스킬 테스트의 API 오류도 다음 빌드 전에 동시 작업에서 수정됐다. 이 변경들을 보스 작업의 수정 실적으로 포함하지 않는다. 초기 실패 로그 `RaidInitialSyncBuild-20261005.log`, `RaidStartupOnceBuild-20261005.log`는 통과 근거가 아니다.

- 최종 Editor 빌드: `Saved/Logs/RaidStartupOnceBuildFinal-20261005.log` — `Result: Succeeded`. 수정된 OnGiven/fixture/GameMode를 포함한 DLL로 아래 검사를 실행했다. 기존 사용자 폴더 Trace.uba 기록 권한 경고는 별도 사항이다.
- 보스 자동화: `Saved/Tests/BossState/Automation-73fb876903fc40f68e0c99e442ab2613/automation.log` — Ability/BattleRetry/BattleSession/Damage/Definition/Lifecycle/NPCStartup/Presentation/RaidContent/Threat 10종 모두 성공, 실행 스크립트 종료 코드 0.
- 리슨 호스트 + 원격 클라이언트 2개: `Saved/Tests/BossMap/c2e386cd231c4ba683665ae080368e44/` — 실제 맵 중도 참가·Pawn 소실 전멸·살아 있는 결과 위젯 디스패처/운영 RPC·새 Pawn 빙의/GAS/입력 준비·새 보스 전투까지 완료, 이전 스크립트 종료 코드 0. 확대된 Iris/Net 오류 검사로 이 저장 로그를 재검사해도 통과했다. 그러나 아래 재실행에서 같은 IrisRpc 오류가 발생했으므로 리슨 구성을 안정적으로 통과했다고 간주하지 않는다.
- 전용 서버 모드 + 원격 클라이언트 2개: `Saved/Tests/BossMap/461cc18c3c7f4dcf8a56ede4afd0243d/` — 동일한 재시도 흐름과 양쪽 클라이언트 복구 마커는 완료됐다. 그러나 Pawn 파괴 직후 서버에 `LogIrisRpc: Error`의 missing object/function 및 rejected RPC가 남았다. 이전 스크립트가 `LogIris`/`LogNet`만 검사해 종료 코드 0을 반환했으나 **완전 통과 결과로 인정하지 않는다**. RPC 종류와 송신 시점은 아직 확정하지 않았다.
- 오류 판정 보강 후 리슨 서버 재실행: `Saved/Tests/BossMap/9e9277a021c14d77b295c0af7592f254/` — 실제 중도 참가와 Pawn 파괴까지 실행했고, 다음 서버 프레임에서 missing object/function의 `LogIrisRpc: Error`를 검출해 스크립트 종료 코드 1로 실패했다. `FuncLocator=(0:16)`, 파괴된 객체 핸들 `Id=110`이 기록됐으나 아직 구체 RPC 이름을 확정하지 않았다. 생성한 서버/두 클라이언트 프로세스는 정리됐다.

### 오류 판정 보강 및 다음 범위

`Run-BossMapE2ETest.ps1`의 공통 상태 검사로 모든 프로세스와 세 로그를 확인하고, `LogIrisRpc` 등 `LogIris*`/`LogNet*` 계열 오류를 검출한다. 완료 마커 뒤에도 2초 동안 오류를 재검사해 비동기 진단이 기록되기 전에 PASS를 선언하지 않도록 했다. PowerShell 구문 분석 및 실제 검사 함수를 통한 저장 로그 회귀 검사에서 위 전용 서버 로그 거부·리슨 로그 허용을 확인했다. 주요 오류 카테고리 검출과 Warning을 Error로 오인하지 않는 검사도 통과했다. 오류를 무시하는 예외나 기존 gameplay assert 완화는 추가하지 않았다.

우선 남은 작업은 전용/리슨 서버의 Pawn 파괴 직후 도착하는 미해결 RPC를 추적하고, 해결 후 보강한 스크립트로 두 구성을 검증하는 것이다. 이 테스트는 실제 저장 BP와 운영 세션/RPC/재생성 경로를 사용하지만, 보스 Brain을 멈추고 Pawn을 파괴하는 복구 시나리오다. 자연 공격·NavMesh 추적·사망 Ability의 지속 Effect/래그돌·장비 전체 복원·몽타주/GameplayCue·GPU 화면·패키징·지연/손실·백엔드 입장/정산은 통과한 것으로 간주하지 않는다. 기존 CommonUI·에셋·에디터 Iris serializer 등록 경고도 프로젝트 전체 무오류 판정과 구분한다.

## 26. Pawn 파괴 시 이동 RPC 경합 진단 및 복구 검사 분리 (2026-10-06)

`Run-BossMapE2ETest.ps1 -TraceRPC`는 선택적으로 Iris Verbose 로그와 `net.Iris.LogReplicationProtocols=1`을 켠다. 오류 판정은 완화하지 않는다. 진단 실행 `Saved/Tests/BossMap/6750c2534b93432fb7910d949fd9fc84/`는 의도적으로 이전 즉시 파괴 시나리오를 실행했고, Pawn 소실 다음 서버 프레임의 IrisRpc 오류로 종료 코드 1이었다.

로그에서 BP_Player의 protocol fragment 0은 `BP_Player_C_Functions`이며, 해당 descriptor의 RPC 순서에서 인덱스 16은 엔진 `ServerMovePacked`다. 서버 핸들 Id=42/78은 각각 BP_Player 인스턴스에 연결되어 있다가 파괴 시 분리됐고, 이후 동일 핸들의 `(0:16)` RPC가 도착해 missing object/function으로 거부됐다. 엔진 소스의 `NetRPC_GetFunctionLocator`는 descriptor와 함수 배열 인덱스를 그대로 사용하며, `UCharacterMovementComponent::ServerMovePacked_ClientSend`는 CharacterOwner의 이 unreliable RPC를 보낸다. 재시도 Controller RPC나 보스 GAS 공격 RPC가 아니다. 운영 재시도의 실패로 단정했던 25절의 미해결 RPC는 이제 이 전송/파괴 경합으로 구분한다.

세션 복구 검사의 파괴 준비 단계를 보강했다. 각 원격 클라이언트는 최초 Pawn의 로컬 빙의와 InputComponent 준비를 확인하고 그 Pawn의 이동 컴포넌트 tick만 정지한다. localhost에서 이전에 큐에 들어간 이동 패킷을 처리할 시간을 주기 위해 1초 후 기존 ready 확인 RPC를 보낸다. 그 사이 Pawn·생존·전투 상태가 바뀌거나 이동 tick이 다시 켜지면 검사를 실패시킨다. 서버의 실제 Pawn 파괴·운영 전멸·위젯/RPC·재생성 경로는 유지하며 새 RPC나 에셋 변경을 추가하지 않았다. 재생성 Pawn은 이동 tick이 다시 활성화되어야 복구 완료로 인정한다.

이 준비 시간은 테스트를 현재 localhost 조건에서 안정적으로 분리하기 위한 것이며, 패킷 손실/임의 지연의 drain 보장이나 운영 사망 프로토콜이 아니다. 갑작스러운 파괴 중 전송과 실제 사망 Ability/장비/래그돌 복구는 별도 미검증 범위다. 에디터/엔진 코드나 운영 Pawn 수명주기를 오류 로그 제거 목적으로 바꾸지 않는다. `LogIris*`/`LogNet*` Error·assert·ensure 검사는 계속 적용한다.

### 검증 결과

- Editor 빌드: `Saved/Logs/RaidPawnLossPreparationBuild-20261006.log` — 변경된 probe/관련 GameMode·Controller를 컴파일·링크, `Result: Succeeded`, 종료 코드 0. 기존 사용자 폴더 Trace.uba 기록 권한 경고는 별도로 남는다.
- 보스 자동화: `Saved/Tests/BossState/Automation-ef5c9a81347c49b5bb5d3b841e3e7c8c/automation.log` — 기존 10종 모두 성공, 실행 스크립트 종료 코드 0.
- 실제 맵 전용 서버 + 원격 클라이언트 2개: `Saved/Tests/BossMap/1797335f0094476c8b01ef78a0e1020f/` — 준비 장벽·중도 참가·실제 Pawn 소실/운영 전멸·결과 위젯 디스패처/기존 재시도 RPC·새 Pawn의 GAS/입력/이동 tick·새 보스 전투까지 통과, 스크립트 종료 코드 0.
- 실제 맵 리슨 호스트 + 원격 클라이언트 2개: `Saved/Tests/BossMap/7443cb6f49364a95aac18ac3b01ce2af/` — 동일한 시나리오 통과, 스크립트 종료 코드 0. 소실 Pawn 수는 전용 2개, 리슨 3개이며 두 원격 클라이언트 모두 복구 완료를 확인했다.

각 네트워크 실행의 세 프로세스 모두 Iris 활성화 및 `LogIris*`/`LogNet*` Error·assert·ensure 부재를 검사했고, 서버 완료 후 2초의 추가 검사도 통과했다. 선택적 진단 스크립트의 PowerShell 구문 분석도 통과했다. 생성한 테스트 프로세스는 종료 시 정리했고, 다른 작업이 시작한 Unreal 프로세스는 종료하지 않았다. 진단 실패 실행을 성공 근거로 포함하지 않는다.

이번 변경은 Development 복구 probe와 실행 스크립트에만 한정된다. 스킬의 서버 권한 원칙에 따라 클라이언트 확인은 테스트의 준비 장벽일 뿐이고 전멸 판정·참가 승인·Pawn 재생성·새 보스 생성의 운영 서버 권한은 유지했다. 다음은 실제 사망 Ability 및 자연 전투의 맵 검증이다. 위 통과는 이동 중 돌발 파괴 경합을 운영 코드에서 해결했다거나 패키징/지연·손실 환경까지 완료했다는 뜻이 아니다.

## 27. 플레이어 실제 사망 Ability와 기존 Pawn 복구 (2026-10-06)

실제 `DA_Player`의 reactive ability인 `GA_Player_Death`를 조사했다. 기존 설정은 `LocalPredicted` + `Shared.Status.Death / OwnedTagAdded`였다. 엔진 `HasNetworkAuthorityToActivateTriggeredAbility`는 LocalPredicted 태그 트리거를 로컬 컨트롤러에서만 실행한다. 플레이어 사망 태그는 서버 loose tag였으므로 원격 플레이어의 서버 사망으로는 이 Ability가 실행되지 않았다. 기존 NPC 사망 Ability는 변경하지 않았다.

- `ConfigureRaidPlayerDeath.py`로 기존 플레이어 사망 BP의 실행 정책을 `ServerInitiated`, 보안 정책은 `ServerOnly`로 변경했다. 사망 시작/종료 결정은 서버 소유이며 클라이언트는 GAS 실행 통지를 받아 표시한다. 그래프 연결·사망 몽타주·startup 목록은 유지했다. `VerifyRaidBossMap.py`도 두 정책을 검사한다.
- `AuditRaidPlayerDeath.py`는 클래스 기본값과 UObject 텍스트 그래프를 읽기 전용으로 조사한다. 그래프에서 몽타주 종료 → `K2_EndAbility` → `K2_OnEndAbility` → 애니메이션 pause/캡슐 NoCollision을 확인했다. 충돌 비활성화는 Ability 활성화 직후가 아닌 **종료 시점**이다. Survival GameMode cast 실패도 UI 처리로 이어지는 연결이며 bossmap에서 사망 자체를 차단하는 분기가 아니다.
- `ARPGPlayer::RestoreEncounterPresentation`은 서버에서 살아 있는 GAS 상태를 확인하고 사망 태그로 분류된 Ability만 취소한 뒤 캡슐/애니메이션의 시작 상태를 복구한다. 복구 revision을 Iris로 복제해 원격 클라이언트에도 적용한다. 취소 불가능한 사망 Ability는 복구 실패로 남긴다. 전체 Effect 제거, 전체 Ability 취소, 재빙의/startup 재부여는 하지 않는다. 이 계약은 현재 BP의 캡슐/애니메이션 처리 범위이며 임의의 래그돌·파괴 timer·지속 사망 GE 정리 계약은 아니다. 2026-10-25에 기존 `URPGHealthComponent`에서 Player로 옮겼으며, 아래 10월 6일 증거는 이전 구현의 실행 기록이다. 이전 증거를 이동 후 멀티플레이 재검증으로 간주하지 않는다. 현재 검증은 [공통 피격·상태이상 계획 §12](COMBAT_REACTION_AND_STATUS_EFFECT_PLAN.md#12-구현-진행--2026-10-25)를 따른다.
- 실제 맵 probe의 `-DeathAbilities` 옵션은 Pawn 파괴/이동 tick 정지 없이 서버 Instant GE의 TakeDamage를 통해 운영 AttributeSet 사망 콜백과 실제 BP Ability를 실행한다. 원격 두 클라이언트의 사망 종료 표시와 결과 위젯 확인을 기다린 뒤 기존 위젯/RPC로 재시도하고 **같은 Pawn**의 HP·충돌·애니메이션·입력 준비·새 보스를 검사한다. 기존 Pawn 소실 시나리오는 별도로 유지한다. 보스 Brain은 여전히 정지하므로 자연 전투/공격 수식 검증을 대신하지 않는다.

### 확인된 증거

- 최초 기본값 조사: `Saved/Tests/BossMap/DeathAudit-91dc9290f0014764bcd2acd474f6aaf0/audit.log`.
- 변경된 정책/그래프 재조회: `Saved/Tests/BossMap/DeathGraphAudit-72cdfed78af94c9a80a44bd52331b59e/audit.log`. 서버 실행/보안 정책의 저장 후 재로드를 확인했다. 진단 그래프는 해당 commandlet UserDir 아래 Saved/Tests/BossMap에 생성되며 콘텐츠 패키지는 저장하지 않는다.
- Editor 빌드: `Saved/Logs/RaidPlayerDeathRecoveryBuild-20261006.log`, probe 수정 후 최종 `Saved/Logs/RaidPlayerDeathLifecycleBuild-20261006.log` 모두 Succeeded/종료 0. 기존 Trace.uba 사용자 폴더 쓰기 경고는 별도이다.
- 최종 빌드 보스 자동화: `Saved/Tests/BossState/Automation-6e6dfb1060cd4e3eb9358cafbb28db04/automation.log` 10종 성공/종료 0. BattleRetry에 기존 dead Pawn의 캡슐/애니메이션 복구 검사를 추가했다.
- 실제 맵 전용 서버 + 원격 클라이언트 2개: `Saved/Tests/BossMap/bdb96faa202c4cf1a54321b2e5d815a2/` 통과/종료 0. 실제 서버 사망 Ability 활성화, 두 클라이언트 종료 표시, 같은 Pawn의 HP·캡슐·애니메이션 복구, 새 보스를 확인했다. 세 프로세스의 Iris 활성화와 Iris/Net Error·assert·ensure 부재 및 완료 후 2초 추가 검사를 통과했다.
- 실제 맵 리슨 호스트 + 원격 클라이언트 2개: `Saved/Tests/BossMap/03e73c843c1643adb8371366d68fae01/` 동일 경로 통과/종료 0. 호스트 포함 플레이어 3명의 서버 사망 Ability 실행과 회복 후 생존, 원격 두 클라이언트의 같은 Pawn/캡슐/애니메이션/위젯/입력 준비 복구를 확인했다. 이 실행도 세 프로세스 Iris 및 엄격한 오류 검사를 통과했다. NullRHI 검사이므로 몽타주의 화면 품질/래그돌을 검증하지 않는다.

초기 맵 실행 `9b93212999c74a86947881cb04df968e`는 시작 무기가 반드시 있다는 probe 가정 때문에 준비 단계에서 중단했다. 현재 장비 마이그레이션된 시작 데이터는 무기 없이 시작할 수 있으므로 해당 가정을 제거했다. `91e965b8a3b14cb491e60337f90c1553`은 캡슐 NoCollision을 Ability 활성화 직후 검사해 실패했으며, 읽기 전용 그래프 조사로 실제 종료 시점에 검사하도록 수정했다. 두 실행은 통과 증거가 아니다. 기존 시작 무기 참조가 있으면 복구 후 유지 여부를 확인하고 실제 개수를 로그에 남긴다. 전용 서버 통과 실행의 두 클라이언트는 모두 개수 0이므로 **장비를 착용한 플레이어의 복구 검증으로 인정하지 않는다**. 공통 Player Ability의 최초 CachedAttributeSet 지연 초기화 관련 LogTemp Error/성공 재초기화 로그도 관측되며, 프로젝트 전체 무오류를 주장하지 않는다.

## 28. 실제 bossmap 자연 전투 검사 (2026-10-06)

`Run-BossMapE2ETest.ps1 -NaturalCombat`과 개발 빌드의 `-RPGBossMapE2ENaturalCombat`을 추가했다. 기존 Pawn 소실/실제 사망 재시도 검사는 유지하며, `-DeathAbilities`와 동시에 선택하면 실행 전에 거부한다. Shipping에서는 기존 probe 활성화 자체를 허용하지 않는다.

- 실제 저장된 bossmap, GameMode, RaidSwordMan, BT, 플레이어 시작 데이터를 사용한다. 중도 참가하는 원격 클라이언트 2개의 GAS/Pawn/입력 준비 장벽까지 보스 Brain을 정지한 뒤 실제 BT를 다시 시작한다. 준비 장벽은 테스트 인스턴스에만 적용된다.
- 보스/참가자 위치의 NavMesh 투영과 완전한 경로를 먼저 읽기 전용으로 검사한다. 보스를 순간 이동하거나 플레이어를 근접 위치에 옮기지 않는다. 경로가 없으면 실패시키며, 부분 경로를 성공으로 인정하지 않는다.
- Threat를 주입하거나 보스 Ability를 수동 활성화하거나 테스트 GE로 피해를 만들지 않는다. 서버에서 실제 타깃/예고, 100cm 초과 보스 이동과 참가자의 HP 감소를 확인한다. 전투 시작 후 45초 내에 완료하지 못하면 실패한다.
- 두 원격 클라이언트에서 보스 이동, 활성 예고 스냅샷과 로컬 장판 컴포넌트 표시, 참가자 HP 감소의 복제를 모두 확인한 뒤 서버 완료 장벽을 통과한다. 피해 대상은 파티 중 한 명 이상이며, 모든 참가자가 맞아야 한다는 검사는 아니다. NullRHI이므로 실제 GPU 픽셀·몽타주 화면 연출을 검증하지 않는다.
- 검사 범위는 기본 페이즈의 자연 접근/첫 피해와 복제이다. 자연 전투 중 전체 페이즈 전환·회피 입력·전멸/재시도·지연/손실·착용 장비/래그돌은 별도 검증이 남아 있다.

### 자연 감지 유지 거리 수정

최초 자연 전투 실행 `Saved/Tests/BossMap/4803fba938404011879a0e516603edc3/`는 완전한 NavMesh 경로와 약 535cm의 실제 이동을 확인했으나, 타깃이 반복적으로 해제되고 공격 예고가 시작되지 않아 45초 제한에서 실패/종료 1이었다. 맵 NavMesh 자체를 수정하거나 이 실행을 통과로 취급하지 않았다.

원인은 레이드 컨트롤러가 원본에서 상속한 `SightRadius=5000`, `LoseSightRadius=0` 설정이다. UE5.8 `AISense_Sight.cpp`는 이미 보인 타깃을 검사할 때 획득 반경이 아니라 유지 반경을 사용하므로, 같은 위치에 겹치지 않은 타깃은 다음 검사에서 사라진다. 원본/레이드의 저장된 실제 Perception sense config를 읽어 이 값을 확인했다 (`PerceptionAudit-93b83b8c522d4fcd84160f4575d1413a/audit.log`). 기존 RaidContent 검사는 Sense를 비활성화하고 Threat를 주입한 격리 검사여서 이 문제를 검출하지 못했다.

`Scripts/Editor/ConfigureRaidSwordManPerception.py`는 기본 읽기 전용 검사이며, `-RPGApplyRaidSwordManPerception`으로 실행할 때만 레이드 소유 설정의 잘못된 유지 반경을 획득 반경 + 500cm로 교정한다. `AIC_RaidSwordMan`만 저장했으며, 원본 `AIC_SwordMan`/네이티브 일반 NPC/맵은 변경하지 않았다. 저장된 레이드 Blueprint에서 사용하지 않는 보조 sight 포인터가 null일 수 있어, 실제 Perception의 instanced `SensesConfig`를 기준으로 검사한다. 적용 로그는 `Saved/Tests/BossMap/PerceptionConfigure-002424ac0eb1430da4a8f2f5f5701d9e/configure.log`, 별도 프로세스 재로드 확인은 `Saved/Tests/BossMap/PerceptionReload-2d51a133b3dd4eb8b8872214d5b9a45b/reload.log`이다. 모두 종료 0, 레이드 유지 반경 5500/원본 유지 반경 0을 확인했다. RaidContent 자동화와 bossmap 읽기 전용 사전 검사에 유지 반경 불변식도 추가했다.

### 확인한 증거

- 최종 빌드 `Saved/Logs/RaidNaturalCombatVerifiedBuild-20261006.log`: Succeeded/종료 0. 기존 Trace.uba 사용자 디렉터리 쓰기 경고는 별도이다. 최초 빌드는 병행 로비 구현 파일이 추가되기 전 링크 실패했고, 중간 회귀 검사 빌드의 const API 호출 오류는 최종 빌드에서 수정했다.
- 감지 회귀 검사를 포함한 보스 자동화 `Saved/Tests/BossState/Automation-c830c4c31cdf48d0a99e2eb13497cb3d/automation.log`: 10종 통과/종료 0.
- 변경한 실제 맵 사전 검사 `Saved/Tests/BossMap/NaturalPreflight-149d37b244ca40e1b9f8dedcd537a094/preflight.log`: 유지 거리/맵 클래스/시작 데이터 확인, 종료 0.
- 전용 서버 + 원격 클라이언트 2개 `Saved/Tests/BossMap/b56f16fb9da64067bcd086b48291239d/`: 자연 전투 통과/종료 0. 두 참가자까지 완전한 경로, 약 573cm 보스 이동, 실제 기본 Strike 예고/서버 HP 300→269를 확인했다. 두 클라이언트 모두 이동/예고/로컬 장판 표시 상태/피해 복제를 확인했다. 세 프로세스 Iris 활성화 및 Iris/Net Error·assert·ensure 부재와 완료 뒤 2초 추가 검사도 통과했다.
- 리슨 서버 호스트 + 원격 클라이언트 2개 `Saved/Tests/BossMap/c12ad1af442e45b8acec42835e0e19d5/`: 자연 전투 통과/종료 0. 세 참가자까지 완전한 경로, 약 583cm 보스 이동, 호스트를 자연 타깃으로 선택한 실제 Strike와 HP 300→269를 확인했다. 두 원격 클라이언트도 이동/예고/로컬 장판 표시 상태/피해 복제를 확인했다. 세 프로세스 Iris 활성화 및 동일한 엄격한 오류 검사/완료 뒤 2초 추가 검사도 통과했다.

재실행: `Scripts/Network/Run-BossMapE2ETest.ps1 -NaturalCombat -SkipSDKSetup` (전용), 같은 명령에 `-ListenServer -Port 18813` 추가 (리슨). 기존 `-DeathAbilities`/기본 Pawn 소실 시나리오와 각각 별도 실행한다.

## 29. 실제 맵의 공격 중 페이즈 전환 검사 (2026-10-07)

`Run-BossMapE2ETest.ps1 -CombatPhases`는 28절의 자연 감지/접근/첫 Strike와 복제 검사를 먼저 실행한 뒤, 두 번째 이후 Strike 예고 중 페이즈 전환을 검사한다. `-NaturalCombat`을 포함하는 별도 시나리오이며 `-DeathAbilities`와 함께 선택하면 실행 전에 거부한다. 공용 opt-in probe에 별도 `RPGBossCombatPhaseProbe.cpp`를 연결했다. Shipping 비활성화, 기존 Pawn 소실/사망/기본 자연 전투 검사는 유지하며 운영 보스 코드·BT·에셋은 변경하지 않았다.

- 두 원격 클라이언트가 같은 진행 중 Strike의 장판을 실제 확인한 뒤에만 서버가 전환 자극을 적용한다. 확인 RPC는 테스트 장벽이며, 클라이언트가 피해량이나 페이즈를 전달하지 않는다. 서버가 현재 예고 순번과 상태를 재검사한다.
- 임계치 검사는 **서버 시험용 Instant GE의 TakeDamage 주입**이다. 기존 AttributeSet 피해 콜백으로 보스 HP를 70% 아래로 낮추고, 운영 BossAI가 페이즈를 전환하도록 한다. HP/PhaseIndex를 직접 대입하거나 보스 Ability를 수동 실행하거나 Threat를 주입하지 않는다. 이 과정은 플레이어 공격 입력·무기/스킬의 피해 계산 및 보안 경로를 검증한 것이 아니다.
- 전환 직후 기본 Strike spec 비활성화와 기존 예고 제거를 확인한다. 취소된 공격의 예정 종료 시각 + 0.15초까지 모든 참가자 HP가 그대로인지 확인한다. 별도로 참가자 GAS Effect 적용 이벤트를 구독해 전환 후 기본 Strike 컨텍스트의 GE가 적용되면 즉시 실패한다.
- 실제 BT가 새 HeavyStrike를 선택해야 한다. 새 예고/페이즈와 보스 HP, 두 클라이언트의 300cm 장판 표시 상태를 확인한다. 서버는 GE의 **보스 ASC/실제 HeavyStrike Ability 컨텍스트와 참가자 HP 감소**를 확인하고, 두 클라이언트가 이후 새 HP 감소를 수신한 뒤에만 완료한다.
- 테스트 delegate는 성공/실패/EndPlay에서 해제한다. 첫 피해 확인 이후 전환/강공격 검사에는 45초 제한을 적용한다. 오류 판정과 서버 완료 후 2초의 Iris/Net 오류 재검사는 유지한다.

### 확인한 증거

최초 빌드 `Saved/Logs/RaidCombatPhasesBuild-20261007.log`는 새 검사/헤더/프로브를 컴파일·링크해 Succeeded/종료 0이다. 기존 사용자 디렉터리 Trace.uba 쓰기 경고는 별도다. 보스 자동화 `Saved/Tests/BossState/Automation-32585f7069ff4cfc948c325db99420a4/automation.log`는 10종 통과/종료 0이다.

최초 전용 서버 실행 `Saved/Tests/BossMap/ddd210694abc45ecbfa60f2d07576dbc/`는 페이즈 전환·Strike 취소·기존 공격 종료 시각의 HP 불변·후속 HeavyStrike 및 두 클라이언트 피해 복제까지 관측했으나, 서버 피해 검증 콜백 오류로 실패/종료 1이다. UE5.8의 `OnGameplayEffectAppliedDelegateToSelf` 첫 인자는 피해자가 아닌 Source ASC다. 대상 ASC를 구독 시 별도 바인딩하도록 수정했다. 이 실패 실행을 통과 증거로 취급하지 않으며 아래 수정본 결과와 구분한다.

- 콜백 수정본 빌드 `Saved/Logs/RaidCombatPhasesCallbackBuild-20261007.log`: Succeeded/종료 0. Trace.uba 사용자 디렉터리 경고는 별도다.
- 수정본 보스 자동화 `Saved/Tests/BossState/Automation-eb6a41c855cd4a0d9d9776e3cb79fae9/automation.log`: 10종 통과/종료 0.
- 전용 서버 + 원격 클라이언트 2개 `Saved/Tests/BossMap/aa2bf575f2e1484382e9ed1d6720bb1d/`: 통과/종료 0. 자연 접근 약 584cm, 첫 Strike 피해 후 두 번째 Strike 순번 2 도중 보스 HP 300→207로 임계치 자극을 적용했다. 기본 공격 취소/예고 제거와 기존 공격 종료 시각까지 참가자 HP 불변, 자연 HeavyStrike 순번 3과 실제 Ability 컨텍스트의 참가자 HP 238→184를 확인했다. 두 원격 클라이언트가 페이즈/HP/300cm 장판과 이후 피해를 수신했다. 세 프로세스 Iris 활성화 및 Iris/Net Error·assert·ensure 부재, 완료 후 2초 추가 검사와 테스트 프로세스 정리도 확인했다.
- 리슨 서버 호스트 + 원격 클라이언트 2개 `Saved/Tests/BossMap/bc842e51c3864948b88acc32c10664d9/`: 통과/종료 0. 자연 접근 약 583cm, 첫 Strike 피해를 받은 호스트 HP 300→269, 순번 2 도중 보스 HP 300→207의 임계치 자극, 기존 공격 취소/지연 피해 부재, 자연 HeavyStrike 순번 3과 호스트 HP 269→215를 확인했다. 두 원격 클라이언트의 페이즈/HP/300cm 장판/호스트 피해 복제 및 동일한 Iris 활성화·엄격한 오류 검사·완료 후 2초 추가 검사도 통과했다.

재실행: `Scripts/Network/Run-BossMapE2ETest.ps1 -CombatPhases -SkipSDKSetup` (전용), 같은 명령에 `-ListenServer -Port 18813` 추가 (리슨).

실제 플레이어 공격/회피 입력·착용 장비 사망 복구·GPU 연출·패키징·지연/손실은 아직 이 검사 범위 밖이다.

## 30. 실제 구르기 입력으로 보스 Strike 회피 검사 (2026-10-07)

`Run-BossMapE2ETest.ps1 -Dodge`와 `RPGBossDodgeProbe.cpp`를 추가한다. 기존 자연 감지/BT 접근/첫 Strike 피해 및 두 원격 클라이언트의 복제 확인을 먼저 수행하고, 이후 새 Strike에서 실제 Threat 타깃의 로컬 Enhanced Input 이동/구르기 입력을 실행한다. 서버가 선택한 타깃을 바꾸거나 직접 이동/Ability 활성화/무적/피해/페이즈를 주입하지 않는다. 서버 권위·기존 Iris·운영 GAS/BT 및 에셋은 유지한다. 페이즈 임계치 자극 검사와 회피 검사는 별도 시나리오이며 사망 검사와도 동시에 실행할 수 없다.

- 현재 시작 데이터/장비 마이그레이션을 감안하여, 기존 이동 보안 검사와 같은 **서버 시험용 실제 GA_Player_Roll 부여**를 사용한다. 이미 부여돼 있으면 재부여하지 않으며, 입력 태그를 검사하고 없는 경우를 로그에 명시한다. 이는 운영 startup가 자동으로 구르기를 제공한다는 검증이 아니다.
- 두 원격 클라이언트의 실제 Move/Roll Started/Completed 바인딩 및 Roll spec 준비를 기다리고 자연 전투를 재개한다. 타깃의 로컬 입력은 카메라 기준으로 보스 반대 방향을 지정한다. 리슨 서버에서 호스트가 자연 타깃이면 호스트의 로컬 입력 경로를 사용한다.
- 서버는 실제 구르기 GAS 실행·AM_Roll 루트 모션 진행·PlayerRoll 이동 승인·정상 Ability 종료/승인 해제, 신규 보안 위반 없음과 시작 HP 불변을 검사한다. 처음부터 예고 반경 밖이었던 타깃은 거부하며 충분히 장판을 벗어나야 한다.
- 보스 Strike의 정상 종료를 별도 delegate로 확인하여 취소/타깃 상실을 회피 성공으로 인정하지 않는다. 예고 종료 뒤 0.2초 및 구르기 정상 종료를 기다린다. 두 원격 클라이언트의 타깃 이동/HP 관측과 타깃 로컬 입력/GAS/몽타주 정상 종료 확인이 있어야 완료한다. 테스트 확인 RPC는 피해/적중 승인 경로가 아니다.
- 입력 주입과 Ability 종료 delegate는 성공/실패/EndPlay에서 정리한다. Shipping 비활성화, 엄격한 Iris/Net 오류 판정 및 완료 후 2초 추가 검사는 유지한다.

### 원격 서버 구르기 방향 보정 (2026-10-08)

기존 `GA_Player_Roll.ComputeRollDirectionAndDistance`는 `GetLastMovementInputVector`로 방향을 계산한다. 이 값은 Pawn의 로컬 입력 소비 결과이며 원격 플레이어의 서버 이동 계약이 아니다. 실제 전용 서버에서는 LastInput이 0이어도 CharacterMovement의 서버 처리 가속도는 이동 입력 방향으로 들어왔다. 읽기 전용 검사 `Saved/Tests/BossMap/RollDirectionLegacy-e8ba7be8b17a40e0bbb1eaabd3325f10/audit.log`에서 기존 그래프와 거리 400을 확인했다.

`ARPGPlayer::GetAbilityMovementDirection`은 로컬 예측/리슨 호스트에서 기존 로컬 입력을 유지하고, 권위 서버의 원격 Pawn에서는 CharacterMovement가 처리한 현재 가속도의 수평 정규화 방향을 사용한다. 시뮬레이션 프록시와 비정상 값은 0을 반환한다. 별도 방향/목적지 RPC, 서버 위치 강제 변경, 보안 예산 증가는 없다. `ConfigureRaidPlayerRollDirection.py`는 기본 읽기 전용이며 명시적 apply에서 기존 Player 소유자 연결을 재사용하여 방향 조회 노드만 교체한다. 저장 전 GAS 실행/보안 정책과 이동 승인 프로파일이 보존되는지 확인한다.

회귀 검사는 BP 함수 지역 변수 `CachedRollingDirection`을 인스턴스 속성으로 조회하지 않는다. 실제 AM_Roll 루트 모션 재생 시 서버의 `RollingDirection` Motion Warping 출력이 보스 반대 방향과 일치해야 하며, 실행 스크립트도 별도 방향 일치 마커를 요구한다. 단순히 장판 밖으로 이동했거나 HP가 그대로인 것만으로 방향 보정 성공을 인정하지 않는다. GAS spec이 먼저 활성화되더라도 BP 방향 계산/승인 이후 몽타주가 재생되는 시점에 출력 방향을 확인한다.

수정 전 대조 실행 `Saved/Tests/BossMap/edb0032ebf0f4489b9962520d6e92612/`는 예상한 `RaidDodgeServerRollDirectionMismatch`로 실패/종료 1이다. 서버 가속도는 약 (-2045,108,0), 예상 방향은 약 (-0.999,0.053,0)이지만 실제 Warp 방향은 (1,0,0)이었다. 이전 `2bcb357a559248aeb30995bed2e46282`의 함수 지역 변수 조회 및 `b7279271db7a4b2cba678a3d912f22e9`의 너무 이른 Warp 조회 실패는 검사 구현 오류이며 수정했다. 방향 확인 없이 통과했던 `5bb68aa862cb4707806fecefd7ad6899`는 방향 보정 증거가 아니다.

### 확인한 증거

- 최종 Editor 빌드 `Saved/Logs/RaidDodgeMontageDirectionBuild-20261008.log`: Succeeded/종료 0. 네이티브 방향 API와 probe를 포함하며 기존 사용자 디렉터리 Trace.uba 쓰기 경고는 별도다.
- 에셋 적용 `Saved/Tests/BossMap/RollDirectionApply-c4ed84b9c2994a399683cc8d4bfe7586/audit.log`: 종료 0, 방향 조회 교체만 저장하고 거리 400·GAS 정책·이동 승인 프로파일을 유지했다. 같은 디렉터리의 `GA_Player_Roll-before.uasset`은 적용 직전 에셋을 해시 검증한 백업이다.
- 별도 프로세스 재로드 `Saved/Tests/BossMap/RollDirectionReload-40b95f67b1bd4381b450d0ba9294099f/audit.log`: 종료 0, 새 API 연결/기존 입력 조회 해제/거리 400 확인, 저장 없음.
- 최종 보스 자동화 `Saved/Tests/BossState/Automation-cfdbb92b261f4a80be3d3795a6ce16dc/automation.log`: 11종 Success/종료 0. 새 `DodgeDirection`은 역할별 입력 원천 선택, 원격 서버의 오래된 Pawn 입력 배제, 수평 정규화, 정지/비정상 값 처리를 검사한다. 실제 역할/에셋 통합은 아래 네트워크 실행으로 별도 확인한다.
- 전용 서버 + 원격 클라이언트 2개 `Saved/Tests/BossMap/8749df62bc7e4237a2687d106e7a0d4b/`: 통과/종료 0. 자연 BT/GAS 첫 피해 후 순번 2에서 원격 타깃의 실제 입력으로 구르기를 실행했다. 서버 LastInput=0, 처리 가속도 약 (-2046,98,0), 실제 Warp 방향 약 (-0.999,0.048,0)이 입력과 일치했다. 실제 AM_Roll 루트 모션/PlayerRoll 승인/정상 구르기 종료와 승인 해제, 취소되지 않은 Strike 정상 종료, HP 269 유지, 장판 중심과 거리 약 377cm를 확인했다. 두 클라이언트 모두 타깃 이동/HP 불변을 수신했고 입력 소유자의 GAS/몽타주 정상 종료도 확인했다.
- 리슨 서버 호스트 + 원격 클라이언트 2개 `Saved/Tests/BossMap/f30e1291328946fbbb67621545d34494/`: 통과/종료 0. 자연 Threat 타깃인 호스트의 로컬 Enhanced Input/GAS/AM_Roll 경로를 사용했다. 서버 Warp 방향 약 (-0.998,0.056,0)이 입력과 일치했고, 동일한 이동 승인/정상 종료/승인 해제와 Strike 정상 종료, HP 269 유지, 장판 중심과 거리 약 382cm를 확인했다. 두 원격 클라이언트가 호스트 이동/HP 불변을 수신했다.

두 네트워크 실행 모두 세 프로세스 Iris 활성화와 Iris/Net Error·assert·ensure 부재, 완료 후 2초 추가 검사 및 생성한 테스트 프로세스 정리를 통과했다. 기존 콘텐츠/초기화 경고를 포함하므로 프로젝트 전체 무오류를 주장하지 않는다. 이 검사는 공간 이탈 회피이며 구르기 무적 프레임·실제 플레이어 공격/장비 장착·GPU·패키징·지연/손실을 검증하지 않는다. 다음 공격 검증에는 현재 장비 장착/활성 무기/GAS 입력/서버 적중 경로를 연결해야 한다.

재실행: `Scripts/Network/Run-BossMapE2ETest.ps1 -Dodge -SkipSDKSetup` (전용), 같은 명령에 `-ListenServer -Port 18813` 추가 (리슨). 에셋 검사는 `Scripts/Editor/ConfigureRaidPlayerRollDirection.py` 기본 읽기 전용을 사용한다.

## 31. 실제 기본 공격 입력과 서버 무기 적중 연결 (2026-10-08)

현재 기본 공격은 `GA_Player_DefaultAttack` → 네이티브 `URPGComboSkillAbility` → `AM_GreatSword_DefaultAttack`의 기존 `ANS_ToggleWeaponCollision` → 서버 Combat/무기 Box overlap → `Shared.Event.Hit` 경로다. 읽기 전용 조사 `Saved/Tests/BossMap/AttackAudit-e37aa809c4db43bca5c2f787a6b622da/audit.log`에서 이 경로와 DT_Skill의 DefaultAttack 수치(AttackPower 20, ManaCost 0, IdentityGain 1)를 확인했다. 네이티브 PerformTrace Notify를 사용하는 몽타주라고 가정해 TargetData로 교체하지 않았다. 실제 공격 에셋·몽타주·피해 수치·GAS 정책은 변경하지 않는다.

- `URPGPlayerGameplayAbility`의 기존 단일 피해 처리에 가상 서버 적용 hook을 두었다. 기본 구현과 일반 NPC 피해 경로는 유지하고, 기본 콤보가 보스를 맞힐 때만 `URPGBossThreatComponent::ApplyDamageFromServerHit`을 사용한다. 기존 GE 피해 계산·피격 Cue/HitReact·Identity 획득 흐름을 재사용하며 피해와 threat를 따로 주입하지 않는다. 다른 스킬/AOE의 보스 연결을 완료했다는 뜻은 아니다.
- 콤보 활성화 시 서버가 진행 중 보스의 EncounterId와 공격 주체/Controller를 캡처한다. 적중 시 새 ID를 조회해 대체하지 않으며, 이전 전투의 공격·뒤늦게 시작된 보스·Controller 교체·클라이언트 컨텍스트를 거부한다. Ability 종료 시 캡처를 비운다. 실제 활성 무기 Box의 QueryOnly/overlap도 재검사한다. 충돌 창별 중복 방지는 기존 Combat 경로를 사용한다.
- `RPGBossPlayerAttackProbe.cpp`와 `Run-BossMapE2ETest.ps1 -PlayerAttack`은 실제 bossmap의 자연 BT/GAS 첫 피해 후 현재 Threat 타깃에게 실제 Enhanced Input 이동/공격을 보낸다. 거리와 바라보는 방향이 맞을 때 공격하며, 순간 이동·강제 회전·직접 Ability 활성화·수동 피해/threat 없이 실행한다. 보스 Brain은 준비 장벽 뒤 계속 실행한다.
- 장비는 명시적인 테스트 옵션에서만 서버 메모리의 native item record를 기존 Equipment.Reconcile에 전달한다. 기존 장비를 덮어쓰지 않고, 운영 WeaponLoadout이 실제 무기·SourceObject·입력 태그·IMC·Ability를 구성하게 한다. DB 지급/인벤토리 저장/운영 startup 변경이나 직접 공격 Ability 부여는 없다. 이 검사는 백엔드 획득/장착 트랜잭션 검증과 구분한다.
- 서버는 실제 기본 공격 컨텍스트/무기 overlap/HP 감소/threat 증가, GAS·몽타주 정상 종료 및 신규 보안 위반 부재를 검사한다. 입력 소유자의 정상 종료 ACK와 원격 두 클라이언트의 공개 무기 Actor/보스 HP 복제 ACK를 요구한다. 기존 세 프로세스 Iris 확인·엄격한 오류 판정·완료 후 2초 검사·자체 프로세스/입력/delegate 정리를 유지한다. Shipping에서는 probe가 비활성화된다.
- 새 `ProjectRPG.Boss.PlayerAttack` 자동테스트는 생성 시 전투 ID 캡처, 늦은 전투 시작, 공격 주체/Controller 변경, 리셋 후 오래된 공격의 HP/threat 부정, 새 공격의 피해/threat 적용과 클라이언트 캡처 차단을 검사한다. 이 격리 테스트 자체가 실제 플레이어 입력/무기 충돌을 증명하지는 않는다.

첫 맵 실행 `Saved/Tests/BossMap/ad8f877f07334bf68c13884ca1e75bf7/`은 실제 입력/몽타주 정상 종료를 확인했으나 서버 적중을 확인하지 못해 시간 제한에서 실패/종료 1이었다. 공격 전에 거리/방향을 확인하지 않던 probe를 보완하고 서버 무기/mesh 진단을 추가했다. 이 실행은 적중이나 threat 연결 성공 근거가 아니다. 최초 빌드 두 번은 병행 파일의 컴파일 오류 및 사용자 폴더 캐시 접근 오류로 실패했다. 병행 파일의 수정 후 기존 Makefile을 고유 백업으로 보관하고 전체 재빌드했다.

### 서버 애니메이션과 기존 스킬 데이터 연결

`ACharacter`의 기본 Mesh 정책 `AlwaysTickPose`는 화면 없는 서버에서 포즈를 계산하더라도 무기 소켓의 bone transform 갱신을 보장하지 않는다. 권위 서버의 `ARPGPlayer::BeginPlay`에서만 `AlwaysTickPoseAndRefreshBones`를 지정했다. 클라이언트 정책이나 무기 Box 크기·소켓·몽타주는 변경하지 않았다. `Saved/Tests/BossMap/10ada04d3a694555a12a64a5658c286b/`에서는 실제 이동 입력으로 캡슐 접촉 거리까지 접근하고, 서버 MeshTick=0/움직이는 무기/열린 충돌 창/공격 정상 종료를 확인했지만 피해 적용은 확인하지 못했다. 따라서 이 실행도 공격 성공 증거가 아니다.

기본 공격의 기존 `LoadSkillData`는 `URPGGameInstance::GetSkillData`에 의존한다. 프로젝트 설정에 GameInstanceClass 지정이 없었던 상태를 확인하고, 기존 `BP_RPGGameInstance`를 `DefaultEngine.ini`의 GameMapsSettings에 연결했다. 읽기 전용 `AttackGameInstanceAudit-4989eeefd3c54f7a84924e9cbcd7cb9a/audit.log`에서 이 에셋의 DT_Skill 연결과 별도 BP 초기화 그래프 부재를 확인했다. authored DefaultAttack 피해 20은 유지한다. 테스트 준비 단계는 실제 실행 중인 GameInstance와 양수 피해 row를 검사한다. 이것만으로 피해 실패 원인이 모두 해결됐다고 판단하지 않는다.

설정 연결 직후 `Saved/Tests/BossMap/981f6f0fb55f46b981b64f8979e6fc34/`는 기존 GameInstance의 로딩 화면이 전용 서버에서 Slate를 호출하여 시작 중 assert/종료 1로 실패했다. `OnPreLoadMap`은 전용 서버 또는 Slate 미초기화 환경에서 화면 생성을 건너뛰도록 수정했다. 현재 이 수정의 최종 빌드와 실제 공격 네트워크 통과를 확인하는 중이다. 서버 hit event 및 실제 Box 위치의 읽기 전용 physics overlap 진단을 추가했으며, 진단 query가 피해·적중·threat를 주입하지 않는다.

이 단계의 격리 자동화는 `Saved/Tests/BossState/Automation-fd9340bdf3c648e7966f2538294bd6d2/automation.log`의 12종 Success/종료 0을 확인했으나 실제 기본 공격은 아직 검증 중이었다. 아래 최종 결과와 구분한다.

### 공통 피해 검증 변경과 저장된 GE의 호환성

GameInstance/서버 Slate 수정이 포함된 `AuthenticationClockBuild-Verified-20261008.log`는 Succeeded였다. 후속 전용 실행 `Saved/Tests/BossMap/3aae1d43e8f34fd285b00ed28c0b7418/`은 맵 로딩과 양쪽 무기/입력 준비, 실행 GameInstance 및 authored 피해 20을 확인했지만 보스의 첫 자연 Strike 피해가 발생하지 않아 실패했다. 같은 DLL의 `Automation-2e18b90a46f74822a3e355e29e90adfc/automation.log`도 Damage와 RaidContent가 실패했다. 이전 12종 통과 결과를 이 DLL의 결과로 대체해 주장하지 않는다.

읽기 전용 `AttackDamageContractAudit-78218419f9e64b6db4e28f9a507a6759/audit.log`에서 기존 shared GE는 Instant/직접 modifier 없음/단일 네이티브 TakeDamage execution이지만 네이티브 `TargetTagRequirementsGameplayEffectComponent`를 포함함을 확인했다. 새 공통 `FRPGDamageExecutionGuard::Supports`의 허용 목록에서 이 컴포넌트가 빠져 정상 보스/플레이어 피해가 모두 거부됐다. 엔진 구현 확인 후 **정확한 네이티브 클래스만** 허용 목록에 추가했다. 임의 subclass·추가 effect/execution·직접 modifier는 계속 거부하며 GE의 태그 적용 조건을 제거하거나 에셋을 변경하지 않았다.

Damage 자동화에 실제 shared GE의 guarded 지원과 GAS의 무적 대상 거부/HP 불변 검사를 추가했다. malformed defense/attack/0으로 반올림되는 피해는 공통 receipt 거부가 반환값에도 반영되는 새 계약에 맞춰 false를 요구한다. HP/threat 불변 검사는 유지한다. 이 수정의 최종 빌드·회귀 및 실제 공격 검증을 이어간다.

수정 후 `Saved/Logs/RaidPlayerAttackGuardBuild-20261008.log`는 Succeeded/종료 0이다. 보스 자동화 `Automation-7ad7e432d9b44f3db77a230ce46a8b61/automation.log`는 12종 Success/종료 0, 공통 보안 자동화 `Saved/Tests/Security/Automation-f237d158cbcd47849d321076187120aa/`는 38종/종료 0이며 Security.proof.json에 DLL 해시와 개별 결과가 있다. 전용 맵 `Saved/Tests/BossMap/bfe145a6b5bf4310827368662b4d8cac/`에서는 실제 입력/서버 무기 overlap/기본 공격 context의 HP 300→284를 최초 확인했지만 전체 실행은 실패했다. 기본 피해는 authored 20과 기존 공격/방어 속성 계산을 사용하며 HP 차이를 20으로 강제하지 않는다.

이 실행은 후속 self-applied 피격 반응 GE를 다른 피해 source로 오인한 probe 판정과, 두 클라이언트에서 `Enemy.Status.UnderAttack` 미등록 Iris ensure가 발생했다. 피해 checkpoint는 BaseDamage SetByCaller가 있는 spec만 검사하도록 구분했으며 정상 피격 GE를 제거하지 않는다. 기존 피격 상태는 `DefaultGameplayTags.ini`에서 `Enemy.Status.UnderAttack`→네이티브 `NPC.Status.UnderAttack`으로 redirect해 로딩 시 정규화한다. 별도의 옛 상태 태그를 중복 등록하거나 ensure 검사를 완화하지 않는다. 이 보완 후 전체 전용/리슨 통과가 필요하다.

### 최종 검증 결과

- `Saved/Logs/RaidPlayerAttackComboBuild-20261008.log`: Succeeded/종료 0. 서버 피해 hook/전투 ID 캡처/서버 bone 갱신/GameInstance 화면 분리/공통 GE 호환성과 최종 콤보 probe를 포함한다. Trace.uba 사용자 폴더 접근 경고는 빌드 성공과 구분한다.
- `Saved/Tests/BossState/Automation-d9f034d25dff4506aed9bae31c22c316/automation.log`: 최종 DLL의 보스 12종 Success/종료 0. shared GE의 무적 대상 거부, 실제 저장된 Strike/HeavyStrike 피해, 이전 전투 공격 거부와 새 공격 피해/threat를 포함한다. 공통 피해 검증 변경의 보안 회귀 38종은 앞서 기재한 `Security.proof.json`의 DLL/개별 결과로 확인한다.
- 전용 서버/원격 두 클라이언트 `Saved/Tests/BossMap/1873c9f2d53d4cd7871b0e531c438281/`: 통과/종료 0. 원격 타깃의 실제 이동/공격 입력을 3초 유지해 기존 콤보를 수행했다. 세 차례 authored ability context+서버 무기 overlap의 HP 300→284→268→235, threat 증가, 입력 소유자와 서버의 GAS/몽타주 정상 종료 및 두 클라이언트의 공개 무기 Actor/HP 복제를 확인했다.
- 리슨 호스트/원격 두 클라이언트 `Saved/Tests/BossMap/5270a4b583474d358149d289a9e87291/`: 통과/종료 0. 자연 Threat 타깃인 호스트(Host=1)의 실제 동일 입력/콤보/세 적중/HP 300→235/threat/정상 종료와 두 원격 관전자 복제를 확인했다. 전용/리슨 모두 수동 피해·직접 공격 Ability 실행·Brain 동결·충돌 Box 확대 없이 수행했다.
- 최종 전용 구르기 회귀 `Saved/Tests/BossMap/d0237a99f23b411faaef4d05b4a18aec/`: 통과/종료 0. 실제 입력과 서버 Warp 방향 일치, GAS/root motion/이동 승인·해제와 정상 구르기/Strike 종료, 시작 HP 238 유지/거리 약 355cm 및 양쪽 Iris 관찰을 확인했다. 무적 프레임 검증은 아니다.

세 네트워크 실행 모두 세 프로세스 Iris 활성화, Iris/Net Error·assert·ensure 부재, 완료 후 2초 확인 및 자체 테스트 프로세스/입력/delegate 정리를 통과했다. 첫 단일 스윙만 검사한 전용 `dbf820ca6d0045be9d33d38ea222570a`도 HP 300→284와 threat/복제를 통과했지만, 리슨 `71ae64f0c947454a81c323fd167e7ee6`은 정상 입력/몽타주 종료에도 적중 없이 시간 제한에서 실패했다. 최종 probe는 기존의 3초 held-input 콤보를 검사하며 모든 첫 스윙이 적중한다고 주장하지 않는다. 얇은 Box/이산 overlap의 단일 스윙 기하·프레임 변화 재현성은 별도 범위이며, sweep 기반 보강을 구현/검증한 것은 아니다. 장비 fixture 및 다른 스킬/AOE 범위 제한도 유지한다.

재실행: `Scripts/Network/Run-BossMapE2ETest.ps1 -PlayerAttack -SkipSDKSetup` (전용), 같은 명령에 `-ListenServer -Port 18813` 추가 (리슨). 읽기 전용 에셋 조사는 `Scripts/Editor/AuditRaidPlayerAttack.py`를 사용한다. NullRHI의 화면/VFX 품질, 패키징된 서버, 지연/손실, 구르기 무적 프레임, 장비 착용 상태 사망/재시도 및 다른 플레이어 스킬 연결은 별도 범위다.

## 32. 공통 피해 계약의 후속 회귀 재확인 (2026-10-09)

인증 시계 검증 당시의 `Damage`/`RaidContent` 실패를 현재 공유 수정과 대조했다. 앞 절의 정확한 네이티브 태그 조건 컴포넌트 호환과 receipt 기대값 수정은 이미 반영돼 있었으므로 새 런타임 수정으로 집계하지 않는다. 실제 공유 GE의 운영 서버 guard 적용·무적 거절·HP/threat 검사를 보강하고, 허용된 태그 조건 컴포넌트가 금지된 추가 효과 경로를 숨기지 못하는 회귀를 추가했다. 서버 권한과 최종 피해 상한은 유지한다.

Editor 증분 빌드와 자동화 66개(보스 12/보안 38/던전·인증 13/스냅샷 3), 전용·리슨 네이티브 상태/늦은 참가/이탈/사망/리셋, 전용·리슨 실제 bossmap의 자연 BT 추적/GAS 피해/Iris 관찰을 통과했다. 전체 보스 결과와 DLL 해시를 기록하는 proof, 더 엄격한 Iris*/Net* 오류 판정 및 완료 후 추가 검사를 포함한다. 정확한 로그·해시·중단 실행 이력·재실행 명령은 [BOSS_DAMAGE_EFFECT_CONTRACT.md](BOSS_DAMAGE_EFFECT_CONTRACT.md)에 기록한다.

이번 실제 맵 재검증은 `-NaturalCombat` 경로이며 플레이어 공격/구르기 입력의 새 재검증이 아니다. 앞 절의 입력 검증 기록과 분리한다. 테스트 입장 우회/NullRHI/Development Editor 환경이므로 실제 Steam·백엔드 인증된 자연 협동 전투와 보상 전체 흐름·Shipping·지연/손실 완료로 확장하지 않는다.

## 33. 서버 무기 스윕 기반 단일 스윙 보강 (2026-10-09)

구현: 기존 `UPlayerCombatComponent`만 열린 서버 무기 충돌 창에서 매 프레임 PostPhysics 판정을 수행한다. 캐릭터 Mesh를 tick prerequisite로 지정하여 서버 소켓 갱신 후의 Box transform을 사용한다. `UPawnCombatComponent`의 기존 무기/장비 변경 정리와 창별 중복 적중 목록을 재사용하며, NPC 무기에는 자동 적용하지 않는다. 기존 Box의 크기·채널·응답·overlap 설정 및 팀 판정을 유지한다. 이동/회전 구간을 세분화해 실제 서버 physics sweep 결과만 기존 Hit 이벤트로 전달한다. 프레임당 최대 128 query이며 비정상 transform/scale 변화/예산을 초과하는 이동은 구간을 버린다. 취소/충돌 창 종료/장비 교체/EndPlay에서는 마지막 피해를 만들어내지 않고 즉시 판정과 임시 적중 정보를 무효화한다.

스윕 결과는 동기 Hit 이벤트 처리 중에만 읽을 수 있는 서버 전용 정보다. 창 generation, 현재 장비, 권한, QueryOnly, 동일 대상 검사를 모두 요구한다. 이벤트 반환 후 재사용하거나 종료 후 새 창에서 재사용할 수 없다. 기본 콤보의 보스 피해 hook은 실제 overlap 또는 이 임시 서버 스윕 정보가 있어야 동작하며, 시작 시 캡처한 encounter/source/controller와 공통 GAS 피해 보안 검사는 그대로 유지한다. 추가 RPC/클라이언트 적중 승인/박스 확대/에셋 변경은 없다. 미세한 회전은 유한 substep 근사이므로 모든 궤적·애니메이션에 대한 수학적 무누락 보장을 뜻하지 않는다.

새 `ProjectRPG.Boss.WeaponSweep` 자동화는 양 끝에서는 닿지 않는 이동/회전 적중, 창별 중복 방지, 정보의 동기 수명, 종료/재시작 중 generation 무효화, 닫힌 창/클라이언트/같은 팀/ignore 채널 거부, 과도한 이동 제한과 사거리 비확장을 검사한다. 보스 자동화 runner는 이 결과를 필수로 요구한다. 맵 runner의 `-SingleSwing`은 실제 기본 공격 입력을 0.15초만 유지하고 `-MaxFPS`로 모든 프로세스의 프레임 제한을 지정한다. 기존 3초 콤보 시나리오는 `-PlayerAttack`으로 유지한다. 맵 피해 checkpoint는 실제 HP 감소와 authored ability context에 더해 서버 overlap 또는 동기 스윕 정보, threat/정상 종료/두 클라이언트 Iris 복제를 요구한다.

후속 보완: 공유 빌드에서 새 스윕 테스트의 `Shared_Event_Hit` 선언 헤더 누락을 확인해 `RPGGameplayTags.h`를 명시적으로 추가했다. 테스트 종료 시 열린 충돌 창과 Hit 이벤트 구독도 정리한다. 단일 스윙 맵 checkpoint는 서버 피해 적용 횟수가 정확히 1이어야 하며 중복 피해는 즉시 실패한다. 콤보 로그의 Before는 첫 HP가 아니라 직전 적중 후 HP를 사용하여 매 적중의 실제 감소를 확인한다. 서버 권한/GAS/encounter/threat/Iris 판정을 완화하지 않는다.

검증 상태: PowerShell 문법 및 관련 변경의 `git diff --check`를 통과했다. `RPGEquipmentCombatTests.cpp`와 `RPGBossPlayerAttackProbe.cpp`는 현재 Editor compile response/PCH를 사용하되 OBJ/SARIF/dependency 출력을 `Saved/Build/WeaponSweepCompile-83ccf1a091524736b3e7d32f59e154b6/`로 분리한 최종 컴파일에서 모두 종료 0이었다. 최초 분리 컴파일은 Engine/Source 상대 include 경로가 필요한 cwd를 사용하지 않아 실패했고, 올바른 cwd에서 재검증했다. 이는 파일 단위 컴파일이며 최종 DLL 링크나 자동화 실행 성공이 아니다.

최초 전체 Editor 검증 시도 `Saved/Logs/RaidWeaponSweepVerifiedBuild-20261009.log`는 공유 빌드 mutex 대기 후 사용자 폴더의 UBT XML/Makefile 캐시 접근 제한으로 Failed/종료 6이었다. 선행 공유 빌드의 던전/UI 컴파일 오류 및 병행 수정은 이번 스윕의 런타임 성공 근거로 집계하지 않는다. 당시 다른 전체 Editor 재빌드가 다시 진행 중이어서 그 빌드/테스트 프로세스를 중단하거나 공유 Makefile을 이동하지 않고 대기했다. 이후 빌드가 없는 상태를 확인하고 프로젝트 내부의 정확한 Makefile을 고유 백업으로 보관한 뒤 재빌드했다. 아래 최종 결과가 이 변경의 실제 빌드/실행 근거이며 31·32절의 이전 통과 결과와 구분한다.

### 최종 검증 결과

- `Saved/Logs/RaidWeaponSweepFinalBuild-20261009.log`: Win64 Development Editor 증분 75개 작업 Succeeded/종료 0. 성공 후 사용자 폴더 Trace.uba 접근 경고는 소스 오류나 빌드 실패와 구분한다. 다른 작업의 프로세스를 종료하거나 캐시를 삭제하지 않았다.
- `Saved/Tests/BossState/Automation-d7f344268c8e484ca407135c1485b8e5/automation.log`: 보스 13종 Success/종료 0. 새 WeaponSweep과 기존 Ability/Damage/PlayerAttack/Threat/리셋/사망/실제 저장된 레이드 콘텐츠 검사를 포함한다. 같은 폴더 `Boss.proof.json`에 13개 이름과 DLL 해시가 있다.
- `Saved/Tests/BossState/EquipmentCombat-b503c6b997da4cc7afbf73de157e45d7/automation.log`: 기존 장비 충돌 3종 Success/검증 명령 종료 0. 네이티브 무기·authored 피해 읽기, 장비 전환/오래된 이벤트 차단, 클라이언트 충돌 활성화·적중 차단을 확인했다. 검사 전후 DLL 해시가 동일했다.

모든 아래 맵 실행은 종료 0이며 `Saved/Tests/BossMap/` 아래 해당 ID의 server/client1/late-client 로그를 사용한다. 서버와 원격 두 클라이언트의 Iris 활성화, 실제 Enhanced Input/몽타주/GAS 정상 종료, 서버 피해/threat, 양쪽 공개 무기/HP 복제, 신규 보안 위반 부재 및 완료 후 2초의 assert/ensure/Iris*/Net* 오류 부재와 자체 프로세스 정리를 검사했다.

| 서버/시나리오 | 프레임 제한 | 적중 경로 | 보스 HP | 피해 횟수 | 실행 ID |
|---|---|---|---|---|---|
| 전용, 단일 스윙 | 30FPS | Overlap=1 / Sweep=0 | 300→284 | 1 | `88cce59cf41440cfabcabf7fc3af363c` |
| 리슨, 단일 스윙 | 30FPS | Overlap=1 / Sweep=0 | 300→267 | 1 | `2aaf028a20bd4cf089173161b94a2a3b` |
| 전용, 단일 스윙 | 15FPS | **Overlap=0 / Sweep=1** | 300→267 | 1 | `ae79b73bee814a83a475b03285d112e5` |
| 리슨, 단일 스윙 | 15FPS | **Overlap=0 / Sweep=1** | 300→267 | 1 | `9d5ce1355d3241debcd476a35d88ad96` |
| 전용, 기존 3초 유지 콤보 | 30FPS | 각 적중 Overlap=1 / Sweep=0 | 300→284→268→235 | 3 | `c6272eca72da44baad29b8d3696a0818` |

15FPS의 두 실행은 현재 Box의 cached overlap이 없는 상태에서 서버 스윕 정보만으로 기존 콤보 hook→보스 Threat→공통 GAS guard→실제 HP 감소/위협도 증가와 양쪽 복제가 통과한 증거다. 단순 겹침 판정의 우연한 성공이나 수동 피해 주입으로 대체하지 않는다. 단일 입력은 0.15초 유지, 콤보 회귀는 기존 3초 유지이며 정확한 횟수를 로그로 구분한다. authored DefaultAttack 피해 20, 기존 공격/방어/치명타 계산 및 Box 크기/소켓/몽타주는 유지한다. HP 차이를 20으로 강제하지 않았다.

이 실행들의 최종 Editor 모듈 SHA256은 `276D16CB0B5FD6D1353DFF257331B4C303CA4162851E833F27F86D74E7F44846`이며 자동화 proof와 모든 실행 후 검사 값이 일치했다. 공통 보안 38종이나 다른 네트워크 시나리오를 이번 DLL로 새 실행했다고 주장하지 않는다. 네트워크 장비는 계속 서버 메모리 fixture이고, 실제 백엔드 획득/장착/Steam 인증·Shipping/패키징·지연/손실·GPU/VFX·다른 공격/소켓/스킬의 전 프레임 무누락 보장은 미검증이다.

재실행: `Scripts/Tests/Run-BossStateAutomationTest.ps1 -SkipSDKSetup`; `Scripts/Network/Run-BossMapE2ETest.ps1 -SingleSwing -SkipSDKSetup` (전용), 같은 명령에 `-ListenServer -Port 18813` 추가 (리슨). 저프레임 확인은 `-MaxFPS 15`를 추가한다. GPU/VFX, 패키징/지연/손실, 무적 프레임, 백엔드 실장착 및 다른 스킬/AOE 연결은 기존의 미완료 범위를 유지한다.

## 34. 정책 기반 플레이어 스킬/AOE의 보스 피해 연결 (2026-10-09)

`URPGGameplayAbility_SkillContainer`의 기존 서버 HitQuery 피해 연산에 보스 분기를 연결했다. 활성화의 Super/Blueprint 호출 전에 진행 중인 보스들의 encounter ID와 시전자 Pawn/controller를 서버 로컬로 캡처한다. 종료 유효성 검사 직후, policy/action 취소 콜백보다 먼저 이를 지운다. 보스 피해는 캡처된 시전자·controller·encounter를 요구하며 실제 서버 HitResult와 ability context를 가진 level 1 GE를 기존 `URPGBossThreatComponent::ApplyDamageFromServerHit`에 전달한다. 이 경로에서 공통 피해 guard/최종 피해 상한과 실제 HP 감소량 기반 threat가 적용된다. 유휴/나중에 시작한/리셋된 보스 및 Threat 컴포넌트 없는 보스는 일반 NPC 피해로 우회하지 않는다. 일반 NPC는 기존 승인된 피해 helper를 그대로 사용한다.

피해량·범위·팀 필터·무적 태그·활성화 누적 적중 예산·재진입/activation serial 검사는 유지했다. 보스 전투 ID를 클라이언트 payload나 적중 순간에 취득하지 않으며 새 RPC/복제 상태/에셋 변경은 없다. 신규 `ProjectRPG.Boss.SkillDamage`는 네이티브 GAS 활성화 fixture로 AOE/Hit 이벤트의 실제 서버 shape query, 다중 보스/일반 NPC 혼합 피해, 능력/적중 context, HP/threat, 예산, 종료/재진입, 전투 시작/리셋/재시작, 빙의 변경, 클라이언트 권한, 무적 및 금지 GE를 검사한다. fixture는 입력·몽타주 시작을 대체하므로 실제 네 스킬의 네트워크 입력/몽타주 성공 증거로 확대하지 않는다. 보스 runner가 새 테스트 성공을 필수로 요구한다.

저장 콘텐츠의 읽기 전용 audit는 종료 0 및 `RPG_SKILL_DAMAGE_AUDIT COMPLETE NoAssetsSaved=1`을 확인했다 (`Saved/Logs/RaidSkillDamageContentAudit-20261009.log`). ChargeSlash/AssultBlade/JumpSmash/WhirlWind 모두 SkillContainer 부모와 DT_Skill 피해 250/40/100/7을 사용한다. 이 audit는 에셋 연결 확인이지 새 피해 경로 실행 증거가 아니다. 구형 비정책 `URPGPlayerGameplayAbility` AOE fallback 전체를 마이그레이션한 것으로 집계하지 않는다.

검증 이력: 새 테스트의 native GameplayTag initializer 복사 오류를 수정해 `Saved/Build/RaidSkillTestCompile-0535b44bf3d540ac9d3232247cb9a535/` 분리 컴파일은 종료 0이었다. 최초 전체 빌드 `Saved/Logs/RaidSkillDamageBuild-20261009.log`는 UBT 캐시의 사용자 폴더 접근 제한으로 종료 6이었다. 최초 SkillContainer 분리 컴파일은 공유 스킬 probe의 오래된 UHT 헤더 때문에 실패했으며 다른 작업의 파일을 수정하지 않았다. 공유 UHT 재생성 후 `Saved/Build/RaidSkillCompile-355777819cbc435e909ebd2306a2e840/`의 SkillContainer 분리 컴파일도 종료 0이었다. 공유 캐시 이동/삭제 및 다른 프로세스 종료 없이 아래 최종 DLL로 검증했다.

### 최종 검증 결과

- `Saved/Logs/SkillMultiPeerBuild-20261009-Coordinator.log`: 공유 Win64 Development Editor 빌드 19개 작업 Succeeded. 새 `RPGBossSkillDamageTests.cpp`, SkillContainer 및 변경한 native fixture의 컴파일과 최종 DLL 링크를 포함한다. 별도 작업의 빌드를 이번 전용 빌드 명령 성공으로 바꾸어 기록하지 않는다.
- `Saved/Tests/BossState/Automation-bfdca70fca604ed89ab34794915abe23/automation.log`: 보스 14종 Success/runner 종료 0. 새 SkillDamage 및 기존 기본 공격·스윕·피해·Threat·저장 콘텐츠·전투 수명주기를 포함한다. 같은 폴더 `Boss.proof.json`에 정확한 테스트 목록과 DLL 해시를 기록한다.
- `Saved/Tests/Security/Automation-e0822db15453430e9b6ae7871a0e0718/automation.log`: 공통 보안 38종 Success/runner 종료 0. 일반 NPC 스킬 피해 12종의 서버 query/범위/예산/종료/재활성화/최종 피해/추가 GE 경로 차단 회귀를 포함한다. `Security.proof.json`의 DLL 해시가 보스 proof와 같다. 최초 `Automation-2d78be18667a406784f6884126f44aa1`은 writable DDC 부재로 테스트 시작 전 종료 3이었다. 재실행에서는 프로세스 환경의 `UE-LocalDataCachePath`를 프로젝트의 `Saved/Tests/BossState/DerivedDataCache`로 지정했으며 공통 runner나 사용자 설정을 수정하지 않았다.

최종 SHA256은 `371DE3D26885C0F921AA0812ADDA1D00EF423FB2D2D679FEC4C5CCA366469165`이며 두 runner의 실행 전후 검사와 마지막 확인에서 동일했다. 총 52종 네이티브 회귀 통과는 이번 변경의 실행 증거다. 네 저장 스킬 각각의 실제 입력/몽타주 및 전용·리슨/Iris 관찰·지연/손실은 이 변경으로 새 검증하지 않았다. 다음 작업은 실제 스킬/AOE 입력을 bossmap 네트워크 checkpoint에 연결하는 것이다. 구형 비정책 AOE, 백엔드 실장착/Steam 인증, Shipping/패키징, GPU/VFX는 별도 남은 범위다.

## 35. 실제 JumpSmash AOE의 전용·리슨 서버 검증 (2026-10-10)

기존 bossmap 플레이어 공격 probe와 runner에 `-JumpSmash` 시나리오를 추가했다. 기본 공격과 상호 배타적으로 사용한다. 서버 메모리 장비 fixture를 운영 native weapon reconcile/loadout에 전달하고, 실제 대검의 SpecialWeaponAbilities가 제공하는 JumpSmash 클래스·입력 태그·무기 SourceObject를 요구한다. 직접 ability grant/활성화/피해/적중 주입이나 스킬 에셋·범위·피해량 변경은 없다. 시전자는 Enhanced Input으로 접근하고 보스 근처 지면을 정상 control aim으로 조준한 뒤 실제 스킬 입력을 0.15초 유지한다. ground targeting, 서버 target-data 검증 및 AM_PlayerSkill의 authored JumpSmash section/event가 기존 SkillContainer를 실행한다.

기본 공격의 overlap/sweep 검사는 유지하고, JumpSmash에서는 그것을 적중 근거로 사용하지 않는다. 실제 GE의 JumpSmash ability/instigator context와 보스를 가리키는 HitResult, 활성화의 frozen profile을 사용하는 서버 HitQuery에 보스가 포함되는지, 실제 HP 감소 및 정확히 한 번의 피해를 검사한다. 서버/소유자의 활성화·몽타주 관찰과 취소되지 않은 종료, threat 증가, 신규 보안 위반 부재가 모두 필요하다. 두 원격 클라이언트는 각각 공격자의 실제 복제 몽타주를 먼저 관찰하고 공개 무기/보스 HP 복제를 확인해야 한다. RPC로 받은 시험용 완료 선언만으로 통과시키지 않는다.

runner는 기존 Iris/Net 오류, assert/ensure/fatal 검사와 완료 후 추가 2초 검사를 유지하며, 실행 전후 Editor DLL SHA256 불변을 요구하고 `BossMap.proof.json`에 모드/시나리오/프레임 제한/로그/해시를 기록한다. 초기 `Saved/Tests/BossMap/66b4534c79e04bdb81c570a3fc63850f/` 실행은 컨텍스트 전환 중 중단되어 완료 proof가 없으므로 성공으로 집계하지 않는다.

검증 중 기존 단일 스윙 실행 `0081a7444e72467d9e66699bef87eaac`는 첫 클라이언트의 PendingNetDriver ConnectionTimeout으로 실패했다. 서버는 BeginPlay에서 READY/Iris/listen을 출력한 뒤 남은 Engine 모듈/에셋 초기화에 머물렀으며 실제 첫 틱의 START_DIAG가 없었다. 공격 실행 전 실패로 분리한다. 연결 시간 제한이나 오류 판정을 완화하지 않고, 클라이언트 실행 전에 실제 TickComponent가 출력하는 기존 `RAIDMAP_E2E_START_DIAG ModeTick=1`을 기다리도록 runner의 순서를 보완했다. 이후 단일 스윙 재실행이 통과했다.

### 최종 검증 결과

- `Saved/Logs/RaidJumpSmashBuild-20261009.log`: Win64 Development Editor 10개 작업 Succeeded/명령 종료 0. 빌드가 없는 상태에서 정확한 프로젝트 내부 Makefile을 고유 backup으로 보관하고 재생성했다. 다른 작업의 캐시 삭제/프로세스 종료는 없었다. 성공 후 사용자 폴더 Trace.uba 접근 경고는 빌드 실패와 구분한다. probe의 분리 컴파일도 `Saved/Build/RaidJumpSmashCompile-f0408a18f14b4aa7b36da2ec733e7cf7/`에서 종료 0이었다.
- 전용 JumpSmash: `Saved/Tests/BossMap/d0b8aa0fee4b44ccbcbc24bb49c16f31/`, runner 종료 0. 실제 원격 시전자 입력, HP **300→134**, HitCount=1, ServerHitQuery=1, threat/정상 종료 및 두 원격 몽타주/무기/HP 복제 통과.
- 리슨 JumpSmash: `Saved/Tests/BossMap/c8ba4ff0fe5943f0b3645f0ca125ca6e/`, runner 종료 0. 실제 호스트 시전자 입력 (`Host=1`), HP **300→217**, HitCount=1 및 동일한 두 원격 관찰/서버 계약 통과.
- 전용 기본 공격 단일 스윙 회귀: `Saved/Tests/BossMap/a5e0d1b86fcd46c494c357c628d07489/`, 보완된 접속 순서로 runner 종료 0. HP **300→267**, Overlap=1/Sweep=0, HitCount=1, 정상 종료·threat·두 원격 공개 무기/HP 복제 통과. JumpSmash의 Query 판정을 기본 공격에 대입하지 않았다.
- `Saved/Tests/BossState/Automation-2a56b9d425184b4d8efe35583985bc43/automation.log`: 보스 14종 Success/runner 종료 0. 같은 폴더 `Boss.proof.json`에 정확한 목록과 DLL 해시를 기록한다. 34절의 공통 보안 38종을 이번 DLL로 재실행한 것으로 집계하지 않는다.

세 맵 실행과 보스 자동화의 최종 모듈 SHA256은 `025B79CDC946D02FC0DA14D4D7C1F52ED3D659C7F86A2D83F795C2C752362AC7`이며 마지막 확인까지 동일했다. 프레임 제한은 모두 30FPS다. 저장된 JumpSmash base damage 100 및 기존 공격/방어/치명타 계산을 유지하므로 HP 감소를 100으로 강제하지 않는다. runner가 자기 프로세스만 정리했으며 proof 없는 중단/실패 이력은 위의 성공 결과와 구분했다.

재실행: `Scripts/Network/Run-BossMapE2ETest.ps1 -JumpSmash -SkipSDKSetup -Port 18814` (전용), 같은 명령에 `-ListenServer -Port 18815` (리슨); 기본 공격은 `-SingleSwing -Port 18816`. 장비는 서버 메모리 fixture이며 실제 백엔드 획득/장착·Steam 인증·Shipping/패키징·GPU/VFX·지연/손실의 증거가 아니다. 다음 스킬 범위는 AssultBlade/ChargeSlash/WhirlWind이고, 구형 비정책 AOE도 별도로 남아 있다.

## 36. 실제 AssultBlade 입력과 보스 피해의 네트워크 검증 (2026-10-10)

기존 공격 probe에 `-AssultBlade`를 추가했다. 저장 콘텐츠의 정확한 이름 `GA_PlayerSkill_AssultBlade`를 사용하며 대검 SpecialWeaponAbilities의 실제 클래스·입력 태그·SourceObject를 확인한다. JumpSmash와 달리 보스 중심을 정상 control aim으로 조준하고 soft targeting 및 `Shared.Event.Hit` 경로를 사용한다. 지면 조준과 `Player.Event.AOE`는 JumpSmash에만 유지한다. runner와 native probe는 두 스킬 동시 선택을 거절하고, runner는 기본 공격/구르기/페이즈 시험과의 충돌도 거절한다.

운영 native reconcile/loadout과 실제 Enhanced Input·GAS·몽타주·서버 HitQuery를 사용한다. 직접 ability grant/활성화/피해/적중 주입은 없다. AssultBlade의 저장 피해값 40, soft-target 설정, sector 범위 및 montage는 수정하지 않았다. GE의 선택한 ability/instigator context와 실제 HitResult, 활성 스킬의 서버 query에 포함된 보스, 정확히 한 번의 실제 HP 감소, threat 증가, 취소되지 않은 종료, 신규 보안 위반 부재를 요구한다. 두 원격 클라이언트는 실제 복제 몽타주·공개 무기·보스 HP를 관찰해야 한다. 기본 공격의 overlap/sweep 판정은 바꾸지 않았다.

스크립트 구문 검사와 세 가지 충돌 입력(JumpSmash+AssultBlade, AssultBlade+SingleSwing, AssultBlade+Dodge)의 거절 검사를 통과했다. `BossMap.proof.json`에 `assultBlade` 선택을 추가하며 기존 DLL 불변·Iris/Net 오류·assert/ensure/fatal 및 완료 후 2초 검사를 유지한다.

### 최종 검증 결과

- `Saved/Logs/RaidAssultBladeBuild-20261010.log`: Win64 Development Editor 6개 작업 Succeeded/명령 종료 0. 변경한 두 probe C++ 컴파일과 Editor DLL 링크를 포함한다. 빌드가 없는 것을 확인하고 프로젝트 내부의 정확한 Makefile을 `.raid-assult-82075f5f3e194dde93597d751cadcbfa-backup`으로 보관해 재생성했다. 성공 후 사용자 폴더 Trace.uba 접근 경고는 빌드 실패와 구분한다. 다른 작업의 에디터·테스트·프로세스는 종료하지 않았다.
- 전용 AssultBlade: `Saved/Tests/BossMap/426d4b3a4efe4abb97ecdc8672cd078d/`, runner 종료 0. 실제 원격 시전자 (`Host=0`), EnhancedInput=1/ManualActivation=0, HP **300→267**, HitCount=1, ServerHitQuery=1, threat/정상 종료·두 원격 몽타주/무기/HP 복제 통과. 서버의 실제 Shared.Event.Hit도 관찰했다.
- 리슨 AssultBlade: `Saved/Tests/BossMap/bd22409495f24822af6af3e9d55eb1eb/`, runner 종료 0. 실제 호스트 시전자 (`Host=1`), HP **300→267**, HitCount=1 및 동일한 서버·두 원격 관찰 계약 통과.
- 전용 JumpSmash 회귀: `Saved/Tests/BossMap/c985a48eff3f42c48990358c90fdc8e7/`, runner 종료 0. 공통 스킬 선택 분기 변경 후에도 실제 ground targeting/AOE 입력·서버 query/GAS 피해·threat·정상 종료 및 두 클라이언트 몽타주/무기/HP 복제 통과.
- `Saved/Tests/BossState/Automation-e3256f6b320a4321a7fee98cb6ff9288/automation.log`: 보스 14종 Success/runner 종료 0. 기본 공격·WeaponSweep·SkillDamage·Threat·수명주기·저장 콘텐츠 회귀를 포함하며, 정확한 목록과 DLL 해시는 `Boss.proof.json`에 있다. 34절의 공통 보안 38종 및 35절의 기본 공격 맵 실행을 이번 DLL로 재실행한 것으로 집계하지 않는다.

세 맵 실행과 보스 자동화 proof의 SHA256은 모두 `7C1BF26CFB7C6CEE30AB4D94AEA2AA46EBB45719CA19A924E8BDDD5469B2C5B9`이며 마지막 확인도 동일했다. 세 맵 실행은 30FPS이고 후발 참가자를 포함한 두 원격 클라이언트 및 서버에서 Iris를 요구한다. 저장된 base damage와 기존 공격/방어/치명타 계산을 유지하며 HP 감소값을 강제하지 않는다. runner는 각각 자신의 시험 프로세스만 정리했다.

재실행: `Scripts/Network/Run-BossMapE2ETest.ps1 -AssultBlade -SkipSDKSetup -Port 18817` (전용), 같은 명령에 `-ListenServer -Port 18818` (리슨); JumpSmash 회귀는 `-JumpSmash -Port 18819`. 서버 메모리 장비 fixture이므로 백엔드 획득/실장착·Steam 인증·Shipping/패키징·GPU/VFX·지연/손실 검증은 아니다. 다음 실제 스킬 검증은 ChargeSlash와 WhirlWind이며, 차지 유지/해제 및 반복 타격은 해당 스킬의 authored 동작과 별도 계약으로 검사해야 한다. 구형 비정책 AOE도 별도 남은 범위다.

## 37. ChargeSlash의 실제 차지 유지·해제 및 보스 피해 검증 (2026-10-10)

bossmap 공격 probe/runner에 `-ChargeSlash`를 추가했다. 대검 native loadout이 제공한 `GA_Player_ChargeSlash`의 실제 SourceObject와 입력 태그로 Enhanced Input을 구동한다. 기존 camera targeting, `AM_PlayerChargeSkill` 및 `Player.Event.AOE`를 사용한다. 활성화의 frozen charge config와 `Shared.Stat.ChargeTime` 배율을 읽어 입력 유지 시간을 계산하고, 자동 발사 시점 전에 입력을 해제한다. 클라이언트는 직접 policy/ability 활성화·section jump·적중/피해를 호출하지 않는다. 준비 중 보스 HP 불변, 서버의 누름/해제 상태와 실제 Attack section, 두 원격 클라이언트의 Prepare/Loop→Attack 관찰을 기존 query/GAS 피해·threat·정상 종료 계약에 추가했다.

최초 전용 실행 `Saved/Tests/BossMap/728f95079186423cabe65ea245ff9c34/`은 실제 입력 후 스킬이 활성화되지 않아 Stage=2 timeout으로 실패했다. `URPGPlayerGameplayAbility::CanActivateAbility` 및 기존 스킬 네트워크 probe를 확인한 결과 ChargeSlash는 IdentitySkill이며 `Player.Status.Rage.Active`가 필요하다. 오류 판정이나 운영 활성화 조건을 완화하지 않았다. 읽기 전용 `Scripts/Editor/AuditBossChargeInput.py`로 입력 바인딩/트리거 및 두 저장 스킬의 GAS 실행·태그·비용 기본값을 확인했다. 초기 감사 로그 `Saved/Logs/RaidChargeInputAudit-20261010.log`와 입력 액션의 실제 태그/트리거를 확대한 `Saved/Logs/RaidChargeInputAudit2-20261010.log`는 NoAssetsSaved=1/종료 0이다.

기존 스킬 probe와 같은 격리된 선행 상태 fixture를 추가했다. 서버가 누락된 Rage.Active loose tag 한 개만 준비하고 CountToOwner로 복제한다. 각 원격 클라이언트는 태그 도착을 기다린 뒤 입력 준비를 보고한다. 직접 ability grant나 공격/방어·피해량·스킬 에셋 변경은 없다. 추가한 ASC만 기록하여 완료/실패/EndPlay 정리에서 해당 loose tag 한 개를 제거한다. 기존 Rage 상태는 덮어쓰거나 제거하지 않는다. proof의 `ragePrerequisiteFixture=true`는 실제 Rage 게이지 획득/발동의 증거가 아님을 명시한다.

초기 빌드 `Saved/Logs/RaidChargeSlashBuild-20261010.log`는 Editor 11개 작업 Succeeded/종료 0이며, 초기 DLL SHA256 `608D41FF5F9C34D8EE37717AA84A4DD572E80E8C57F896A37E15E4D2BD75791C`에서 보스 14종은 `Saved/Tests/BossState/Automation-5da9b1fe241a44aa98d4e1b44d76e9b8/`의 proof/종료 0으로 통과했다. 이 결과는 선행 Rage fixture를 추가한 최종 DLL의 증거와 구분한다.

선행 Rage 수정 후 `Saved/Logs/RaidChargeSlashRageBuild-20261010.log`는 10개 작업 Succeeded/종료 0이며, SHA256 `345A6ADC8BE2EFD1B52C9A2753610A8CEFF31CE7D64EC5F42DE6E574717DD93E`의 보스 14종도 `Saved/Tests/BossState/Automation-290639ea20a54748b3b6b7eca4d70f72/`에서 통과했다. 읽기 전용 최종 감사 `Saved/Logs/RaidChargeInputAuditFinal-20261010.log`는 ChargeSlash의 실제 `Player.Ability.IdentitySkill` 태그를 확인하고 NoAssetsSaved=1/종료 0이었다.

그 DLL의 전용 실행 `Saved/Tests/BossMap/4908d0519f904769b3d729b9b1f77e51/`에서는 서버 hold/입력 해제/Attack section, 클라이언트의 실제 0.700004초 유지/해제 및 서버 query의 한 번의 피해 **300→0**까지 확인했다. 그러나 보스 처치가 백엔드 배정 없는 로컬 세션의 보상 처리를 시작했고 기존 NaturalCombatEncounterEnded 조건에서 실패했다. 부분 증거를 전체 PASS로 집계하지 않는다. 백엔드 처리나 오류 판정을 수정하지 않았다.

피해 후 threat·스킬 정상 종료를 격리하여 검사할 수 있도록 ChargeSlash 시나리오에서만 서버 보스의 MaxHP/HP를 2500으로 준비하는 durability fixture를 추가했다. NPC 초기화 완료 후, encounter가 Idle인 상태에서만 기존 ASC API로 준비한다. MaxHP 알림이 전투 중 페이즈를 바꾸지 않도록 준비 시점을 엄격히 제한한다. 리슨 호스트는 첫 probe tick 전에 encounter가 시작될 수 있으므로 기존 GameMode의 시작 준비 경계에서 development-only opt-in probe를 호출한다. Shipping 코드 및 일반 게임/다른 시나리오에는 적용되지 않는다. runner는 fixture 완료 로그를 기다린 뒤 첫 원격 클라이언트를 실행한다. 저장 피해값 250과 공격/방어/치명타 계산은 유지한다. `bossDurabilityFixture=true`이며 300HP 실제 보스의 처치·승리/보상 흐름을 검증한 것으로 해석하면 안 된다.

`Saved/Logs/RaidChargeSlashReadyBuild-20261010.log`의 준비 경계 보완 빌드는 10개 작업 Succeeded/종료 0이었다. 이후 전용 `Saved/Tests/BossMap/8ce00b9f46fd458fa36c72507079d5cb/`와 리슨 `Saved/Tests/BossMap/f150ec1815a54f6aa5ba2d0417efbe9a/`는 Idle/2500HP fixture 로그가 있으나 재개 시 완료 proof가 없었다. 전체 PASS로 집계하지 않는다. 당시 DLL `D0D2A663E936D2DC749F91703FB4FAC8479DE501DFEA6D4E972A74E974FC69CE`의 최종 회귀 통과를 주장하지 않는다.

### 최종 ChargeSlash 실행 결과

38절의 SOLID 경계 분리까지 포함한 공유 Editor 빌드 DLL로 아래 시험을 실행했다. 두 runner 모두 종료 0 및 `BossMap.proof.json` 생성, 실행 전후 해시 불변·완료 후 2초 오류 검사 통과다.

- 전용 서버: `Saved/Tests/BossMap/49d31fd0812a41a4bbf32d2215f9aff9/`. 실제 원격 시전자 (`Host=0`)의 Enhanced Input 유지/해제, 0.824174초 유지 및 frozen TimePerLevel=0.5, 준비 중 보스 HP 불변, 서버 입력 해제/Attack section, query/GAS 피해 **2500→2292**, HitCount=1, 정상 종료·threat 및 두 원격 클라이언트의 Prepare/Loop→Attack·무기·HP 관찰 통과.
- 리슨 서버: `Saved/Tests/BossMap/c79bf1b4835a4e03bcbaffd072ae71a6/`. 실제 호스트 시전자 (`Host=1`)의 0.70075초 유지/Enhanced Input 해제, query/GAS 피해 **2500→2084**, HitCount=1 및 같은 수명주기·threat·두 원격 관찰 계약 통과.

두 맵 proof와 보스 15종 proof의 SHA256은 `70C8D237C95A844DF00112312DDB0701566A01E633DEDB9902A01B415B0FE8DF`다. 30FPS이며 후발 참가자를 포함한 두 원격 클라이언트와 서버에서 Iris를 요구한다. 저장 base damage 250 및 기존 공격/방어/치명타 계산을 유지하므로 피해량을 250으로 강제하지 않았다. Rage 선행 태그와 보스 2500HP는 opt-in 시험 fixture이고, 실제 Rage 획득/발동·300HP 보스 처치/승리/보상·백엔드 획득/실장착·Steam 인증·Shipping/패키징·GPU/VFX·지연/손실 검증은 아니다. 과거 미완료/실패 실행과 이번 성공 proof를 구분한다.

재실행: `Scripts/Network/Run-BossMapE2ETest.ps1 -ChargeSlash -SkipSDKSetup -Port 18822` (전용), 같은 명령에 `-ListenServer -Port 18823` (리슨). WhirlWind의 반복 타격/중지 계약과 구형 비정책 AOE는 별도 남은 범위다.

## 38. 플레이어 스킬 변경을 고려한 SOLID 경계 (2026-10-10)

보스 런타임은 특정 플레이어 스킬 클래스·몽타주를 의사결정 조건으로 사용하지 않는다. 서버가 확인한 적중, GAS effect/attribute, encounter 세대와 threat 계약을 유지한다. 플레이어의 시전·타겟팅·정책 변경은 플레이어 쪽 책임이며, 기존 서버 보안 검사를 우회하는 범용 피해 API를 새로 만들지 않는다.

검증 코드도 변경 이유별로 분리한다. `Tests/RPGBossAttackScenario.h/.cpp`의 불변 시나리오에는 클래스 경로·몽타주·이벤트·조준 방식·입력 유지 시간·선행 태그·예상 피해 횟수·관찰 섹션을 둔다. 공통 probe는 실제 입력·ASC 수명주기·피해 출처/HP·threat·두 클라이언트 관찰을 담당한다. 기존 SkillContainer의 서버 query 증거와 frozen charge config 읽기는 구체 어댑터에 한정한다. 공통 probe는 `IRPGBossAttackHitEvidence`와 `IRPGBossAttackChargeObservation`의 작은 읽기 전용 계약에 의존하고, 일반 스킬에 차지 메서드를 강제하지 않는다.

새 스킬 구현이 생기면 해당 구현의 **실제 서버 적중 증거**를 읽는 어댑터와 시나리오 연결을 추가한다. 단순 성공 플래그나 시험용 적중 주입은 어댑터 대체로 인정하지 않는다. 공통 피해 출처·서버 권위·정상 종료·어그로·Iris 관찰 조건을 약화하지 않는다. 단발에서 다단/지속/토글로 동작이 바뀌면 기대 횟수뿐 아니라 틱 간격·중지·취소 계약도 별도로 정의하고 검증해야 한다.

이번 변경은 1차 분리다. command-line 시나리오 선택/충돌 검사는 registry를 사용하지만, PowerShell의 시나리오 옵션/증거 marker와 ChargeSlash 전용 durability fixture 및 차지 구동 상태는 여전히 별도 책임이다. 완전한 모든 스킬 공통 실행 프레임워크 또는 SOLID 전면 적용 완료로 집계하지 않는다. 새 `ProjectRPG.Boss.AttackScenario` 자동화는 선택/충돌·입력 fallback 거절·차지 선행 조건·잘못된 ability/effect의 증거 거절을 검사하며 runner의 필수 성공 목록에 추가했다.

### 이번 변경의 검증 범위

`Saved/Build/RaidAttackScenarioCompile-8bf4adc8a0c6440b9e29191e19ef44bc/`에서 시나리오/구체 어댑터, 새 자동화, 플레이어 공격 probe, map probe의 Editor C++ **4개 파일 분리 컴파일은 최종 종료 0**이다. WITH_DEV_AUTOMATION_TESTS=1이며 각 최종 sarif의 diagnostics는 0개다. 초기 시도는 engine-relative include를 해석할 작업 디렉터리 오류로 실패했다. 디렉터리를 수정한 뒤 새 2개 파일은 통과했지만 기존 probe는 다른 작업의 최신 HttpWebManager/RPGGameModeBase 헤더와 오래된 UHT 생성 줄 번호가 달라 실패했다. 해당 소스나 생성 헤더를 수동 수정하지 않았다.

활성 Unreal builder가 없는 것을 확인하고 정확한 프로젝트 Editor Makefile을 `.raid-solid-uht-d884f23756c5453798518d54a2158113-backup`으로 보관한 뒤 `-SkipBuild -ForceHeaderGeneration`으로 UHT만 재생성했다. `Saved/Logs/RaidAttackScenarioHeaders-20261010.log`는 Succeeded/명령 종료 0, 생성 파일 8개이며 이 뒤 위 4개 컴파일을 모두 다시 통과했다. 사용자 폴더 Trace.uba 쓰기 경고는 명령 실패와 구분한다. 공유 모듈을 쓰는 다른 에디터·시험 프로세스는 종료하지 않았다.

보스 자동화 runner 및 맵 runner의 PowerShell 구문 검사는 통과했다. 당시 공유 Editor DLL을 쓰는 프로세스 때문에 새 DLL 링크·보스 15종·네트워크 회귀는 미검증으로 남겼으며 분리 컴파일을 실행 증거로 집계하지 않았다. 다음 재개에서 아래 전체 빌드·자동화 결과를 확인했다.

### 전체 빌드 및 자동화 후속 검증

`Saved/Logs/DungeonMembershipServer-EditorBuild-20261010.log`의 공유 Win64 Development Editor 빌드는 58개 작업 Succeeded였다. 변경한 `RPGBossAttackScenario.cpp`, `RPGBossAttackScenarioTests.cpp`, `RPGBossPlayerAttackProbe.cpp`, `RPGBossMapE2EProbeComponent.cpp`의 컴파일과 최종 DLL 링크가 각각 로그에 있다. 별도 작업의 빌드를 이번 전용 빌드 명령 성공으로 바꾸어 기록하지 않는다. 이번 전용 명령은 다른 Unreal 빌드가 진행 중이라 제한 시간까지 대기 후 **빌드 시작 전** 중단했으며 캐시/프로세스를 변경하지 않았다. 공유 빌드의 성공과 소스보다 새로운 DLL을 확인한 뒤 해당 DLL로 테스트했다.

`Saved/Tests/BossState/Automation-c17ab238ea5748ff9769a9ee6d9231d8/`는 새 AttackScenario를 포함한 보스 **15종 Success/runner 종료 0**이다. `Boss.proof.json`의 정확한 목록과 실행 전후 동일 DLL SHA256은 `70C8D237C95A844DF00112312DDB0701566A01E633DEDB9902A01B415B0FE8DF`다. runner 구문 검사와 ChargeSlash+JumpSmash/AssultBlade/SingleSwing/Dodge의 네 가지 충돌 입력을 실행 전 거절하는 검사도 통과했다. 네트워크 결과와 공통 보안 38종을 이 자동화 성공으로 대체하지 않는다.

### 같은 DLL의 실제 스킬 네트워크 회귀

- 전용 AssultBlade: `Saved/Tests/BossMap/42acf7e3c89a4a70968905ca5093bc47/`, runner 종료 0/완료 proof. 실제 원격 입력·soft targeting·Shared.Event.Hit·서버 query/GAS 피해 **300→234**, HitCount=1, 정상 종료·threat·두 원격 몽타주/무기/HP 관찰 통과. Rage/보스 durability fixture는 false다.
- 전용 JumpSmash: `Saved/Tests/BossMap/fd90c81bfc22408f99d023a5b02ca2d6/`, runner 종료 0/완료 proof. 실제 원격 입력·ground targeting·AOE 이벤트·서버 query/GAS 피해 **300→134**, HitCount=1 및 같은 정상 종료·threat·두 원격 관찰 통과. 두 fixture는 false다.
- ChargeSlash의 전용·리슨 결과는 37절의 `49d31fd0812a41a4bbf32d2215f9aff9`/`c79bf1b4835a4e03bcbaffd072ae71a6` 완료 proof에 있다. 이번 두 일반 스킬 회귀로 해당 결과를 대체하지 않는다.

네 맵 실행과 보스 자동화 **총 5개 proof**의 DLL SHA256 및 마지막 확인은 모두 `70C8D237C95A844DF00112312DDB0701566A01E633DEDB9902A01B415B0FE8DF`다. 맵 시험은 30FPS, 후발 참가자를 포함한 두 원격 클라이언트/서버에서 Iris를 사용하며, 각 runner의 완료 후 오류 검사와 실행 전후 DLL 불변 검사도 통과했다. 이번 검증 중 운영 스킬 에셋·피해 계산·네트워크 오류 조건을 변경하지 않았다. 각 runner는 자신이 시작한 프로세스만 정리했다.

따라서 이번 **SOLID 1차 경계 분리의 전체 빌드·보스 15종·현재 세 스킬의 위 네트워크 경로**는 검증 완료다. 일반 스킬의 리슨 서버 및 기본 공격 맵 시험을 이번 DLL에서 새로 실행했다고 집계하지 않으며, 34절의 보안 38종도 이번 DLL의 새 결과가 아니다. 다음 구현 범위는 WhirlWind의 반복 타격·틱 간격·입력 중지 후 추가 피해 부재와 복제 계약이다. 실제 Rage 획득/발동, 백엔드 장착부터의 연속 전투, 처치/보상, 구형 비정책 AOE, Shipping/GPU/지연·손실은 별도 남은 범위다.

## 39. WhirlWind 반복 타격·수동 중지 계약 (2026-10-11)

### 구현 경계

WhirlWind는 현재 SkillContainer + Holding 정책을 사용하는 **hold-to-channel** 스킬이다. 과거 Toggle 부모 이름과 달리 두 번째 누름으로 중지하는 토글 시험이 아니다. `Saved/Logs/RaidWhirlWindAudit3-20261011.log`의 읽기 전용 commandlet은 종료 0/`NoAssetsSaved=1`이며, 실제 AOE 이벤트·Holding 정책·4초 유지/0~4초 성공 구간·Default 시작/End 종료·AM_PlayerToggleSkill 반복 구간의 AOE notify를 확인했다. 앞선 audit 1/2는 Python 비노출 속성명 때문에 실패했으며 성공으로 집계하지 않는다. 에셋을 저장하거나 피해/타겟 범위를 수정하지 않았다.

`RPGBossAttackScenario`에 WhirlWind 정의와 작은 `IRPGBossAttackChannelObservation` 계약을 추가했다. 기존 적중 증거/차지 관찰 계약을 늘려 일반 스킬에 채널 메서드를 강제하지 않는다. 구체 SkillContainer 어댑터만 frozen Holding config/Shared.Stat.HoldTime·실제 몽타주 재생 속도·시나리오가 지정한 Toggle 반복 섹션의 자체 연결/길이·프레임별 실제 애니메이션 진행량을 읽는다. 유효 수동 해제 시점은 자동 종료 이전 성공 구간 안이어야 하며, 공통 probe에서 구체 정책을 직접 호출하거나 몽타주를 점프하지 않는다. 시나리오/어댑터 분리는 스킬 구조 변경 시 공통 서버 피해·threat·복제 검증을 유지하기 위한 SOLID 1차 경계 확장이다.

`Run-BossMapE2ETest.ps1 -WhirlWind`는 다른 스킬/기본 공격/구르기와의 충돌을 거절한다. 실제 무기가 부여한 ability와 authored Enhanced Input을 사용한다. 서버는 실제 GE 출처·현재 HitQuery·HP 감소를 매 타격 확인하고 최소 3회 피해를 요구한다. Queued notify는 긴 애니메이션 update에서 여러 주기를 처리한 뒤 함께 전달될 수 있으므로 callback 간격만으로 중복으로 판정하지 않는다. 읽기 전용 실제 montage DeltaMoved/재생 속도로 정한 프레임별 `ceil(진행량/주기)` budget과 직전 update 진행량을 보정한 최소 간격으로 검사한다. 진행량이 없는 근접 burst, 일반 update 중복, catch-up budget 초과, 역행 시간/잘못된 주기는 거절한다. 정밀한 주기 일치나 모든 notify의 개별 복제를 주장하는 검사는 아니다. 입력 유지 중 Toggle, 입력 해제 후 End, 정상 GAS 종료 뒤 최소 `max(0.5초, 반복 주기 2회)`의 감시 구간까지 추가 피해/HP 변화가 없어야 한다. 두 원격 관찰자도 Toggle→End·최소 두 번의 HP 감소·무기 복제를 확인해야 완료 RPC를 보낸다. 로그의 `LoopSection`/`LoopSeconds`는 반복 구간을 뜻하는 공통 표기이며 실제 섹션 이름을 Loop로 가정하지 않는다. Rage/2500HP fixture는 사용하지 않는다.

### 빌드와 자동화

전용 `Saved/Logs/RaidWhirlWindBuild-20261011.log`는 새 어댑터의 포인터 접근 오타로 실패했다. 오타를 수정했으며 다른 활성 builder의 캐시/프로세스를 건드리지 않았다. 뒤이은 공유 `Saved/Logs/NPCDeathNetwork-EditorModule-20261011.log`는 수정된 RPGBossAttackScenario 컴파일·DLL 링크를 포함한 6개 작업 Succeeded이다. 전용 빌드에서 이미 성공한 새 자동화/공통 probe/map probe 오브젝트의 시각과 최종 DLL이 모든 수정 소스보다 새로운 것을 확인하고 사용했다. 공유 빌드를 이번 전용 빌드 명령 성공으로 기록하지 않는다. Editor Makefile 보관본은 `.raid-whirl-...-backup`이며 삭제가 아니다.

`Saved/Tests/BossState/Automation-18f450fea7074d09b8aab5a779fa70df/`는 보스 **15종 Success/runner 종료 0/완료 proof**이다. AttackScenario는 채널/차지 capability 분리·Rage 비필수·옵션 충돌·빈 관찰의 실패/오래된 상태 초기화를 추가 검사한다. 이 결과의 DLL SHA256은 `0226EBF0817FD189EDC761007F76C33D3C8FD0870F703C3D8F8B334E937DDA65`다.

### 실제 멀티플레이 검증

초기 전용 `999ebcefb07943deb9f23c188e7c71ec`/리슨 `2eb80a6a8ef84945acad773c8432ee4f` 실행은 채널 config 관찰 실패로 종료 1이며 완료 proof가 없다. `Saved/Logs/RaidWhirlWindAudit4-20261011.log`의 추가 읽기 전용 조회는 종료 0/NoAssetsSaved=1, 실제 섹션이 **Default→Toggle→End**임을 확인했다. 앞선 probe가 차지와 같은 이름 `Loop`를 가정한 것이 원인이므로, 반복 섹션을 어댑터 생성 인자로 분리하고 시나리오에 Toggle을 명시했다. 반복 연결/주기·3회 피해·수동 중지·quiet 감시는 약화하지 않았다. 수정 후 전체 빌드/네이티브/전용·리슨 재실행을 완료하기 전에는 런타임 통과로 집계하지 않는다.

반복 이름 수정 후 `Saved/Logs/RaidWhirlWindBuild3-20261011.log`는 7개 작업 Succeeded/명령 종료 0이었다. DLL `ABE6BA3D503FB2CB71DAF60BEBD151FD2075CC4A010B7ED0C3F767C2419775EE`의 보스 15종 (`Automation-3664b2d05ae04b9299e174324c68d222`)과 전용 ChargeSlash 회귀 (`2e2e2cc16bf24a9f93fb91a5c580614d`)는 종료 0/완료 proof다. 그러나 WhirlWind 전용 `2b0480fe30f54182a1d173a39926c8c2`는 6회 실제 피해/수동 해제 뒤 정상 종료 관찰 중 frozen config 조회 실패, 리슨 `7abfbf677f3c4a369443c721994e517a`는 긴 update 후 두 notify의 callback 간격으로 중복 판정하여 종료 1이었다. 이 두 실행은 완료 proof가 없으며 성공으로 집계하지 않는다.

이후 종료/End 관찰 뒤에는 정리된 frozen config를 다시 읽지 않되 기존 End 구간·정상 종료 필수 조건을 유지했다. 위 실제 montage 진행량 budget을 추가하고 자동화에 일반 중복/진행량 없는 burst/catch-up 허용 및 초과/늦은 dispatch/잘못된 주기/역행 시간 검사를 추가했다. 공유 `Saved/Logs/NPCDeathNetwork-EditorModule-20261011-ActorSelection.log`는 변경한 시나리오·자동화·공통 player/map probe 컴파일과 DLL 링크를 포함한 13개 작업 Succeeded다. 이번 전용 Build4 시도는 활성 builder 검사에서 빌드 시작 전 종료했으며 캐시/프로세스를 변경하지 않았다. 새 DLL로 보스 15종 및 전용/리슨을 다시 실행 중이고, 완료 proof 확인 전에는 성공으로 집계하지 않는다.

DLL `D666CE3E90F9876D9368BBFE3D7607844D50AC38283E13AD1228D800CA2267E2`에서 보스 15종 (`Automation-0893eec3b10c4bffb12dbdc0a2650181`)과 전용 ChargeSlash (`c5e633c4f062423dbaf54ec896921132`)는 종료 0/완료 proof이다. WhirlWind 전용 `028e5f8c0eb941fa9f3673217cfb494f`/리슨 `60c30d88125c4a498e5abd5b5c07649e`는 실제 6회 피해와 약 1.8초 입력 해제를 확인했으나, 해제 약 0.1초 후 소유자의 frozen 관찰 실패로 종료 1/완료 proof 없음이다. 반복 피해만으로 전체 통과를 주장하지 않는다.

후속 진단에서 GAS CurrentMontage/GetActiveInstanceForMontage는 **블렌드아웃 시작 시** 먼저 비지만 End 구간/ability는 아직 종료 중임을 확인했다. 단순히 완료된 ability의 설정 조회를 건너뛰는 앞선 보완만으로 해결된 문제가 아니었다. 관찰 어댑터 입력을 실제 `FAnimMontageInstance`로 좁히고, 공통 probe는 해당 시전에서 관측한 instance ID로 블렌드아웃 중 같은 몽타주의 End 구간을 계속 추적한다. 서버/소유자/두 원격 관찰자 모두 같은 원칙이며, 다른 active montage로 대체하지 않는다. End 구간·수동 입력 해제·정상 GAS 종료·quiet 구간·threat·Iris 필수 조건은 유지한다. 운영 정책/피해 코드를 변경하거나 관찰 조건을 삭제하지 않았다. 이 보완의 전체 빌드/실행 결과 확인은 후속 검증 범위다.

몽타주 instance 보완 후 `Saved/Build/RaidWhirlBlendCompile-1791650469478/`의 시나리오/어댑터·자동화·player probe·map probe C++ **4개 분리 컴파일은 각각 종료 0/최종 sarif diagnostics 0개**이다. 공용 오브젝트/DLL 출력은 덮어쓰지 않았으며 링크나 런타임 성공 증거로 대신하지 않는다. 전용 Build5 시도는 공유 builder/모듈 사용자에게 최대 180초 대기한 뒤 **빌드 시작 전** 종료했으며 Makefile/다른 프로세스를 변경하지 않았다. 동시 공유 Final 빌드는 다른 작업의 GAS replicated-event RPC 선언/구현 오브젝트 불일치로 링크 실패했다. 해당 소스를 이번 레이드 작업에서 수정하지 않았다. 뒤이은 `DungeonGameplayRevocation-Compile-20261011.log`의 공유 빌드/실행 결과를 아래에서 최종 확인했다.

### 최종 동일 DLL 검증 결과

`Saved/Logs/DungeonGameplayRevocation-Compile-20261011.log`의 공유 Win64 Development Editor 빌드는 **59개 작업 Succeeded**다. 최신 시나리오/player/map 소스 컴파일과 DLL 링크, 소스보다 새로운 오브젝트/최종 DLL을 확인한 뒤 아래 시험을 실행했다. 공유 빌드를 전용 Build5 명령의 성공으로 바꾸어 기록하지 않는다.

- 보스 자동화: `Saved/Tests/BossState/Automation-9128ba9bc9c44037bc3e97677b555876/`, **15종 Success/runner 종료 0/완료 proof**. 추가한 채널 cadence 정상/중복/catch-up budget/역행·잘못된 주기 계약도 포함한다.
- WhirlWind 전용: `Saved/Tests/BossMap/60af3e165c844ed28ca87c8d3443216f/`, **runner 종료 0/완료 proof**. 실제 원격 시전자 Host=0, 유지 1.80006초/반복 주기 0.233333초, 실제 서버 query/GAS 피해 **300→264, 6회**, 입력 해제/End 구간/정상 종료·threat, 종료 후 **0.500132초** HP 불변/추가 피해 부재. 두 원격 클라이언트 모두 Toggle→End 및 HP 감소 6회·몽타주/무기 복제를 확인했다.
- WhirlWind 리슨: `Saved/Tests/BossMap/0b25c8b2d9ea4a4fa99c85f664715eef/`, **runner 종료 0/완료 proof**. 실제 호스트 시전자 Host=1, 유지 1.80002초/반복 주기 0.233333초, **300→258, 6회**, 같은 해제/End/정상 종료/threat 계약 및 **0.500002초** quiet 감시 통과. 두 원격 클라이언트의 Toggle→End/HP 감소 6회·몽타주/무기 복제도 통과했다.
- 기존 ChargeSlash 전용 회귀: `Saved/Tests/BossMap/4f57ebac8b844e8e948ec7e5fe24c766/`, **runner 종료 0/완료 proof**. 원격 입력 유지 0.700003초/frozen TimePerLevel=0.5, 서버 query/GAS 피해 **2500→2084, 1회**, 정상 종료/threat 및 두 원격 관찰 통과. 이 회귀에만 기존 Rage 태그/보스 2500HP fixture가 적용된다.

총 **4개 완료 proof**의 실행 전후 DLL SHA256 및 최종 확인은 모두 `742F8D677F4E12A649D5ECA88B67A5A3C3BF255BB717B433FFEC75569664514C`다. 세 맵 실행은 30FPS, 후발 참가자를 포함한 두 원격 클라이언트와 서버에서 Iris 필수이며, 완료 뒤 2초의 Fatal/Assert/Ensure/Iris·Net 오류 검사와 DLL 불변 검사도 통과했다. WhirlWind의 Rage/보스 durability fixture는 모두 false다. 치명타·공격/방어 계산을 유지하므로 피해량/최종 HP를 고정하지 않았고, 정상 시험의 타격 수도 6회로 강제하지 않는다(최소 3회 계약). 각 runner는 자신이 시작한 프로세스만 정리했다.

따라서 **WhirlWind 수동 유지/반복/해제/End/정상 종료/quiet 감시 및 전용·리슨 Iris 관찰**은 이번 코드에서 검증 완료다. 구체 SkillContainer 읽기는 채널 어댑터에 남기고 공통 서버 피해·threat·복제 계약을 유지했다. 앞선 실패/미완료 실행을 성공으로 소급하지 않는다. 공통 보안 38종, 기본 공격/AssultBlade/JumpSmash 및 ChargeSlash 리슨을 이 최종 DLL에서 새로 재시험했다고 집계하지 않는다.

재실행: `Scripts/Network/Run-BossMapE2ETest.ps1 -WhirlWind -SkipSDKSetup -Port 18826` (전용), 같은 명령에 `-ListenServer -Port 18827` (리슨). 다음 범위는 구형 비정책 AOE의 실제 서버 피해 연결/복제 및 아래 별도 미검증 항목이다.

자동 4초 종료·돌발 취소/사망/세션 종료 중 채널 중단, Shipping/GPU/VFX·지연/손실·백엔드 연속 장착/전투/처치/보상은 이번 수동 해제 계약과 별도 범위다.

## 40. 명시적 던전 포기자의 전투·이동 권한 격리 (2026-10-11)

서버의 정확한 세션/캐릭터/Steam 회수 경계가 현재 Controller/Pawn/PlayerState 소유 관계를
확인한 뒤 해당 ASC만 격리한다. 플레이어 내부 장벽은 PlayerState 분리 뒤에도 유지된다.
해당 플레이어의 진행 중 GAS 인스턴스를 취소하고 입력을 비우되, 능력 부여 목록·효과·HP를
일괄 제거하거나 다른 파티원/보스의 ASC를 취소하지 않는다. 일반 GAS의 새 activation과
queued input/payload/TargetData, 무기 충돌 재개 및 이동 grant도 차단한다.

보스 Threat와 공유 타깃 쿼리는 격리된 Pawn을 즉시 부적격으로 취급한다. Threat getter는
주기 갱신 전에도 그 타깃을 반환하지 않으며 갱신 때 남은 멤버를 선택한다. 전체 Threat나
encounter를 초기화하지 않는다. 최종 damage execution은 이미 만든 spec/permit이라도
source/target 회수 여부를 다시 검사해 지연 공격을 차단한다. retained live Pawn은 생존자나
재도전 복원 대상으로 취급하지 않아 전멸을 막거나 권한을 회복할 수 없다.

플레이어 전용 CharacterMovement는 회수 시 속도/힘/root motion을 비우고 `MOVE_None`을
유지하며 native movement/packed 이동 처리에서 새 실행을 거절한다. Pawn을 즉시 파괴하지
않는다. 이는 26절의 Iris object 수명 경합을 해결하기 위한 첫 경계이며 실제 connection
종료/queued RPC drain 검증 완료가 아니다. NPC의 movement 컴포넌트는 변경하지 않았다.

새 `Gameplay`/`LateDamage`/`ThreatAndParticipation` 3종과 직접 영향 회귀 8종, **11종만**
격리 네이티브 자동화에서 Success/runner 종료 0을 확인했다. 새 입력·취소 불가 인스턴스 종료,
실제 공통 GE HP 피해 차단, 이동 함수/무기 창, 남은 Threat와 생존 판정을 검사한다.
DLL `742F8D677F4E12A649D5ECA88B67A5A3C3BF255BB717B433FFEC75569664514C`의
`Saved/Tests/DungeonFlow/Automation-bd2ee9a2822b4b21a7767189e1e133aa/DungeonFlow.proof.json`에
정확한 이름·소스/runner 63개·로그 해시와 범위를 기록했다. 이전 137개 전체 검사는 반복하지 않았다.
NoLink 59개 컴파일·최신 공유 2개 링크·전체 Editor 타깃의 metadata 갱신과 선행 링크 실패는
[DUNGEON_ABANDONMENT.md](DUNGEON_ABANDONMENT.md)에 구분해 기록했다.

정상 Logout/실제 퇴장과 서버 영수증 조회의 자동 회수는 후속이다. 게임 중 포기 UI는 계속
비활성화하며 클라이언트 ACK를 서버 권한으로 사용하지 않는다. 실제 2~4인 backend/UE/DB,
Iris 지연/손실·응답 유실/재시작/재입장 및 Shipping/Steam 검증으로 확대하지 않는다.

## 41. 구형 AOE 호환 경로의 서버 보스 피해 연결 (2026-10-11)

### 구현과 범위

기존 콘텐츠 audit와 `Content/Blueprints/Character/Player/Abilities/Skill` 목록에는 운영 스킬 4개만 있으며 모두 정책 기반 SkillContainer로 전환돼 있다. 따라서 기존 스킬을 구형 부모로 되돌리거나 가상의 운영 스킬/입력/네트워크 시나리오를 만들지 않았다. 이번 구현 대상은 남아 있는 `URPGPlayerGameplayAbility`의 구형 AOE 호환 코드다.

- AOE가 일반 GE helper를 직접 호출하던 부분을 기존 가상 `ApplyLegacyDamageToServerHit` hook으로 연결했다. 일반 NPC의 승인된 피해 경로·기존 sphere/box trace·피해 수치·팀/거리/피해 guard·이벤트별 대상 상한·피드백은 유지한다.
- 활성화에서 Super/Blueprint 콜백 전에 진행 중 보스의 encounter ID와 시전자 Pawn/controller를 서버 로컬로 캡처한다. 보스 피해는 기존 `URPGBossThreatComponent::ApplyDamageFromServerHit`의 공통 GE/최종 피해 검증과 실제 HP 감소량 기반 threat를 사용한다. 유휴/뒤늦게 시작한/리셋된 전투·controller 교체·종료된 능력·Threat 없는 보스는 일반 NPC 피해로 우회하지 않는다.
- 여러 대상에 공유되는 GE spec은 보스별로 복사하고 context를 독립 복제하여 실제 서버 HitResult를 보존한다. 유효한 종료에서는 Super 콜백 전에 snapshot을 비운다. AOE 루프에는 activation serial/active/source 검사를 두어 GE 콜백이 같은 능력을 종료·재시작해도 이전 query가 새 활성화로 계속되지 않게 한다.
- 공통 네트워크 probe에 구형 클래스 cast나 별도 피해 승인 경로를 추가하지 않았다. 기존 다형성 hook을 재사용하고 호환 테스트는 별도 native fixture로 분리했다. 운영 스킬 에셋·BT·복제 상태·RPC·보안 허용량은 변경하지 않았다.

`RPGBossLegacyAOETestAbility`는 테스트에서만 부여되는 native GAS fixture다. 실제 base 활성화/종료·WaitGameplayEvent·서버 sphere trace·GE/HP/threat 경로를 사용하지만 운영 입력/무기/몽타주를 증명하지 않는다. 새 `ProjectRPG.Boss.LegacyAOE`는 다중 보스의 대상별 hit context와 능력 출처, 일반 NPC 혼합 피해, 위조된 원거리 target/magnitude 무시, Threat 없는 보스 차단, 종료 후 callback, 늦은 전투 시작/리셋/재시작, controller 교체, 클라이언트 권한, GE 콜백 중 재활성화의 이전 루프 중단과 신규 query를 검사한다. 보스 runner는 이 테스트 성공도 필수로 요구한다.

### 검증 결과

- 전용 Editor 빌드 `Saved/Logs/RaidLegacyAOEBuild-20261011.log`: **33개 작업 Succeeded/명령 종료 0**. 새 fixture/test와 PlayerGameplayAbility·Combo·SkillContainer를 포함한다. 마지막 source 수정 이후 UHT/관련 object/DLL 생성 시각을 확인했다. Makefile은 검증한 workspace 내부의 고유 `.raid-legacy-...-backup`으로 보관했고 삭제하지 않았다. 빌드 성공 뒤 기존 사용자 폴더 Trace.uba 쓰기 제한 경고는 별도이며 다른 프로세스를 종료하지 않았다.
- `Saved/Tests/BossState/Automation-300d01267a854e49bb37e43ce4cfb147/Boss.proof.json`: **보스 16종 Success/runner 종료 0**. 신규 LegacyAOE와 기존 SkillDamage/PlayerAttack/WeaponSweep/AttackScenario/상태·콘텐츠 회귀를 포함한다.
- `Saved/Tests/Security/RaidLegacyRegression-13cc977be19a4987be8b91754f9b1ae8/automation.log`: **스킬 피해 보안 15종 Success/검증 명령 종료 0**. 현재 소스의 필수 이름 전체와 로그 성공 목록을 대조하고 Fatal/Assert/Ensure/Fail 부재 및 실행 전후 DLL 불변을 검사했다. 별도 Security.proof.json을 만들지 않았으며 보안 전체 실행으로 집계하지 않는다.
- `Saved/Tests/BossMap/a40ae509de724a7d80b3730ee534cf12/BossMap.proof.json`: **전용 JumpSmash 회귀 runner 종료 0**. 실제 원격 Enhanced Input/ground targeting·운영 무기 부여 GAS/몽타주·서버 HitQuery/피해 **300→134, 1회**·threat·늦은 참가자를 포함한 두 원격 클라이언트의 몽타주/무기/HP Iris 복제 통과. 30FPS/nullrhi, 세 프로세스 Iris 필수, 완료 후 2초 엄격한 오류 검사와 자체 프로세스 정리도 통과했다. Rage/보스 durability fixture는 false다.

위 두 proof 및 보안 검사·마지막 확인의 DLL SHA256은 모두 `843ACBD35CB5F413AC3F1965817D29E1ED72EAB80458DA1BF36FE69EA52AAF05`다. 같은 DLL의 다른 맵/리슨 서버/보안 전체 검사를 반복했다고 주장하지 않는다. JumpSmash는 정책 기반 **회귀**이며 구형 AOE의 실제 입력/Iris 성공 근거로 바꾸어 기록하지 않는다.

구형 AOE 호환 코드의 서버 보스 연결과 native 경계 검사는 완료했다. 구형 운영 콘텐츠가 추가되거나 사용 경로가 확인되면 해당 authored 입력/몽타주/무기 grant에 맞춘 읽기 어댑터와 멀티플레이 검증이 필요하다. 실제 구형 스킬 입력/Iris, WhirlWind 자동 종료/돌발 취소, 실제 Rage, 백엔드 장착부터의 연속 전투/처치/보상 및 Shipping/GPU/지연·손실은 이번 결과 밖이다. 재검사: `Scripts/Tests/Run-BossStateAutomationTest.ps1 -SkipSDKSetup`, 정책 맵 회귀: `Scripts/Network/Run-BossMapE2ETest.ps1 -JumpSmash -SkipSDKSetup -Port 18828`.

## 42. 보스 사망 시 서버 전투 정리와 실제 GAS 사망 검증 (2026-10-11)

### 구현과 검증 경계

이번 범위는 NPC·보스뿐이다. 플레이어 스킬 및 백엔드 코드를 변경하지 않았다. `raid-monster-ai` 지침에 따라 기존 보스 상태·GAS·BT·Iris 경계를 유지했다.

- `ARPGBossCharacter`는 서버의 `Dead` 상태에서 AI 이동·Gameplay focus·Brain logic과 CharacterMovement를 정지한다. BeginPlay와 재빙의에도 같은 terminal guard를 적용하고 EndPlay에서 상태 delegate를 해제한다. `CancelAllAbilities`로 실제 사망 Ability를 중단하지 않는다. 기존 공격 Ability 취소·telegraph 해제·threat 정리는 기존 상태 구독 경로를 사용한다.
- `ProjectRPG.Boss.NPCStartup`에 HP 0 이후 이동 정지, 사망체 재빙의 시 이동 재차단, 시작 데이터 중복 부여 방지 회귀를 추가했다. 이 native 테스트의 직접 HP 자극은 실제 치명타/사망 몽타주 증거와 구분한다.
- 새 `RPGBossDeathProbe.cpp`와 runner의 `-BossDeath`는 실제 bossmap에서 자연 BT 이동·GAS Strike 피해 및 두 원격 관찰을 먼저 확인한다. 이후 다음 Strike 예고를 두 클라이언트가 확인한 동안 서버 전용 native 공격 fixture의 receipt-backed GE hit를 기존 Threat 피해 승인 경로에 전달한다. 직접 보스 HP 대입이나 사망 Ability 수동 활성화, probe의 강제 BT 정지는 없다. 플레이어 입력/운영 스킬의 치명타를 주장하지 않는다.
- 치명타 이후 다음 실제 tick에서 HP 0/Dead/사망 tag, 기존 Strike 비활성화, 예고·threat·target 해제, Brain·이동 정지, Victory와 남은 시간 0을 검사한다. 실제 사망 Ability의 정확히 한 번 활성화·취소 없는 자연 종료, 서버 capsule 충돌 해제, 서버와 원격 두 클라이언트의 사망 몽타주/HP/tag/예고 해제를 요구한다. 이전 Strike 마감 이후와 사망 Ability 종료 후 최소 0.5초 동안 참가자 HP 불변도 검사한다. 관찰 ACK는 피해나 Victory의 승인 권한이 아니다.

### 빌드 및 native 결과

공유 `NPCDropSecurity-EditorModule-20261011-DissolveObservation.log`의 관련 소스 컴파일과 전체 Succeeded를 확인했다. 자체 `RaidBossDeathBuild-20261011.log`는 최신 death probe 컴파일에는 성공했으나 동시 DLL 사용으로 링크 LNK1104/종료 6이었다. 다른 프로세스를 종료하지 않았고, 이후 자체 `RaidBossDeathLink-20261011.log`는 DLL 링크·metadata **2개 작업 Succeeded/종료 0**이다. workspace 내부 Makefile 백업은 보존했다.

`Saved/Tests/BossState/Automation-3ec7877022a3494b84cc3c39e6faf900/Boss.proof.json`: **보스 16종 Success/runner 종료 0**. 이번에 확장한 NPCStartup과 기존 보스 회귀를 포함한다. DLL SHA256은 `B66582A9FC82F729B62DF1F6370584F278E1712B5D9FB3BAAE7BFC0D5881D58D`다. C++ 재수정이 없으므로 native 전체를 반복하지 않았다.

### 멀티플레이 발견 사항과 보완

최초 전용 실행 `5e8b85bf0923425a9d409e07bb6dbea8`는 기존 GameMode 결과 위젯 BeginPlay 경고를 과도하게 잡아 실패했다. runner는 Fatal/Assert/Ensure/Iris·Net/Blueprint Error 검사를 유지하면서, `Accessed None`의 NPC asset 경고 블록을 따로 검사한다. 기존 GameMode 위젯 및 Shared `GA_NPC_DrawBossHpBar_Base`의 전용 서버 UI 생성 경고는 이번 사망 정리 검증 밖이며 해당 UI 에셋을 수정하지 않았다.

재실행 `b8e0c16e212647a2809dee369768b7d1`은 치명타와 서버/두 클라이언트의 사망 완료까지 도달했지만, `BP_RaidNPCBase`의 Niagara `SpawnSystemAttached` 반환값이 전용 서버에서 null이어서 strict NPC Blueprint 경고 검사로 **종료 1/완료 proof 없음**이다. 완료 marker만으로 통과 처리하지 않았다.

`ConfigureRaidBossDeathPresentation.py`는 일반 NPC가 아니라 `BP_RaidNPCBase` 하나의 사망 VFX 비동기 load 실행 edge에 `IsDedicatedServer` guard만 삽입한다. 서버 gameplay·충돌·dissolve timeline·무기 정리·파괴 흐름은 유지하고 클라이언트/리슨 VFX edge는 유지한다. 원본은 `Saved/Backups/RaidBossDeath-33f71efac40540fb8f47c54a0662d0a6/BP_RaidNPCBase.uasset`에 보존했다. 첫 commandlet은 writable DDC 누락으로 script 실행 전 종료 3이며, workspace DDC/SDK skip를 지정한 `RaidBossDeathPresentation2-20261011.log`는 **OnlyRaidNPCBaseSaved=1/종료 0**이다. runner는 실행 전후 DLL 및 해당 에셋 hash 불변도 요구한다.

전용 재실행 `a604e546f1934a50aa559c829faf544a/BossMap.proof.json`은 **runner 종료 0/완료 proof**다. 자연 BT/GAS 공격, 예고 중 native 치명타 **300→0**, 실제 사망 Ability 자연 종료, Brain/이동/예고/threat 정리, 잔여 피해 없음 및 늦은 참가자 포함 두 Iris 원격 관찰을 확인했다. DLL은 위 native proof와 같고 당시 에셋 hash는 `7CC503C9241C786F3AC8FB33986495A18BCAB5142ACB01688210FB11865F7FA6`이다. 로그에는 의도적으로 백엔드 assignment 없는 fixture의 보상 보고 거부가 남는다. 이는 실제 보상 성공 증거가 아니며 백엔드 코드를 수정하거나 해당 오류를 없애지 않았다.

리슨 `83c7a2839a984106b813eae9e4d50a83`도 native 치명타 뒤 NullRHI 환경의 Niagara null 반환으로 **종료 1/proof 없음**이었다. 읽기 전용 `RaidBossDeathVFXInspect-20261011.log`로 반환값의 사용처를 확인했다. setter 뒤에 dissolve timeline의 `SetPlayRate`가 이어지므로 전체 후속 실행을 차단하지 않았다. `ConfigureRaidBossDeathPresentation.py`에 `IsValid(SpawnSystemAttached 반환값)` 분기를 추가해 유효할 때만 Niagara 변수 설정을 실행하고, 유효/무효 양쪽 모두 원래 `SetPlayRate`로 이어지게 했다. `RaidBossDeathVFXApply-20261011.log`는 **ComponentValidityGuard=1/DissolveContinuationPreserved=1/OnlyRaidNPCBaseSaved=1/종료 0**이다. 첫 guard가 있는 에셋도 같은 backup 폴더의 `BP_RaidNPCBase-dedicated-guard.uasset`로 보존했다. renderer를 가정하지 않는 운영 VFX 반환값 검사이며 테스트 모드 전용 우회가 아니다.

### 재개 후 최종 에셋 검증 결과

중단 전 `e7d409966ae141048bf8c5862fcb6c54`와 `6c79f7726c7c4a96af2c32dfb63dfd3b`는 완료 proof가 없고 재개 시 실행 프로세스도 남아 있지 않았다. 성공으로 집계하지 않는다. 재개 후 전용 `87fc4df6adf44a5c95959a1f96674253`는 클라이언트 기동 직후 빈 로그를 regex에 넘기는 runner 예외로 종료됐고 proof가 없다. 로그를 명시적으로 string으로 변환해 빈 파일을 빈 문자열로 검사하도록 수정했다. 오류 guard/완료 조건은 유지했으며 수정 스크립트의 PowerShell 구문 검사도 통과했다. shell exit code만으로 성공을 집계하지 않고 완료 proof를 확인했다.

- 리슨: `Saved/Tests/BossMap/9004b6fc516d41fab3ac412a4a3cb95e/BossMap.proof.json`, **runner 종료 0/완료 proof**.
- 전용: `Saved/Tests/BossMap/08608ee4283c4f9096ac4d8327f10eb5/BossMap.proof.json`, **runner 종료 0/완료 proof**.

두 실행은 최종 에셋에서 자연 BT/GAS Strike 이후 다음 예고 중 서버 native 치명타 **300→0**, 실제 사망 Ability 정확히 한 번의 활성화·취소 없는 자연 종료, Brain/이동/예고/threat 정리, 이전 공격의 잔여 피해 부재 및 늦은 참가자 포함 **원격 두 클라이언트**의 HP/Dead/tag/몽타주/예고 해제/Victory Iris 관찰을 통과했다. 30FPS/NullRHI, 모든 프로세스의 Iris 사용 필수, 완료 후 2초 오류 감시와 runner 자체 프로세스 정리도 통과했다. 리슨 호스트 자체 화면 관찰 증거가 아니라 서버와 두 원격 관찰자의 증거다.

두 proof 및 마지막 확인의 DLL SHA256은 `F6752B6E424AB07849464881E6E5565F2043A7C2C8F6C743353F2DC919530F1B`, 최종 `BP_RaidNPCBase`는 `39D92F68622B6B28DE616FA664B9DA0F8E9DFE819187BFAEB78EE124C84B1810`이다. 재개 사이 공유 빌드로 DLL이 앞선 native 16종의 `B665...`와 달라졌으므로, native 16종을 최종 DLL에서 다시 실행했다고 주장하지 않는다. 이번 재개에서는 C++ 빌드나 무관한 테스트를 반복하지 않았다.

보스 사망 정리와 전용/리슨 Iris 사망 관찰은 이 범위에서 완료했다. 다음 NPC 작업은 타깃 상실·이탈·리셋 예외와 공격 연출이다. 실제 플레이어 입력 치명타, Shipping 패키지, GPU/VFX 화질, 지연·손실, 백엔드 처치/보상 영속화는 이번 범위 밖이다. 재실행: `Scripts/Network/Run-BossMapE2ETest.ps1 -BossDeath -SkipSDKSetup -Port 18829`; 리슨은 `-ListenServer -Port 18830` 추가.

## 43. 같은 타깃 Pawn의 Controller 교체 시 이전 보스 공격 차단 (2026-10-11)

기존 Threat는 시야 상실·사망·거리 이탈·Controller 교체 때 타깃/위협도를 무효화하고, 리셋은 예고와 공격 Ability를 정리한다. 다만 같은 Pawn이 새 Controller로 재등록되면 `Threat->GetCurrentTarget() == ExecutionTarget`이 다시 참이 된다. 이전 보스 공격은 보스의 Controller만 캡처하고 타깃 Controller는 캡처하지 않아, 새 타깃의 위협도 재등록이 오래된 공격 context까지 되살릴 수 있었다.

`URPGBossGameplayAbility`는 활성화 시 타깃 Pawn뿐 아니라 타깃 Controller의 weak identity를 서버 로컬에 캡처한다. `IsBossExecutionValid`는 그 Controller의 생존과 현재 Pawn의 Controller 일치를 추가로 요구한다. 기존 GAS·BT의 취소/타임아웃, hit 직전 검증, 전투 ID/페이즈/보스 Controller 검증을 유지한다. 새 RPC·복제 상태·피해 경로·자동 리셋 정책은 추가하지 않았고, 플레이어 스킬·백엔드·에셋도 수정하지 않았다. `raid-monster-ai` 지침에 따라 승인 판단을 서버 보스 Ability 경계에 유지했으며 타깃의 구체적인 플레이어 스킬 클래스에 의존하지 않는다.

- `ProjectRPG.Boss.Ability`: 실행 중 같은 Pawn의 Controller를 교체하고 새 위협도를 등록한 뒤에도 이전 context가 무효임을 확인한다. 기존 BT가 이전 실행을 취소/실패 처리하고, 다음 실행은 새 타깃 Controller를 정상 캡처하는지 검사한다.
- `ProjectRPG.Boss.Damage`: 기존 native 공격 fixture에 opt-in `bDeferHit`만 추가해 실제 commit/예고 뒤 피해 시점을 유예한다. Controller 교체/새 위협도 등록 이후 이전 hit 요청은 HP 100을 유지하고 빈 hit 결과를 반환해야 한다. 취소 시 예고가 해제되고, 다음 실행의 실제 서버 query/GAS 피해는 HP **100→69**로 적용돼야 한다. 기존 즉시 공격 테스트의 기본 동작은 유지한다. 이는 native fixture이며 실제 플레이어 입력/재접속 증거가 아니다.

`Saved/Logs/RaidTargetControllerBuild-20261011.log`: 자체 Win64 Development Editor **19개 작업 Succeeded/종료 0**. Makefile은 workspace 내부 `.raid-target-controller-0ca52728f8c94acbb90862bf3a40427d-backup`으로 보존했고 다른 프로세스를 종료하지 않았다. 빌드 완료 뒤 AppData `Trace.uba` 쓰기 제한 경고는 별도다.

### 검증 결과

- `Saved/Tests/BossState/Automation-cc967e657fd54482b67751a49253185c/Boss.proof.json`: **보스 16종 Success/runner 종료 0**. 이번 Ability/Damage 회귀와 기존 Threat·리셋·NPCStartup·저장된 레이드 콘텐츠를 포함한다.
- `Saved/Tests/BossState/196ffe744e244a5bbc7e0925ce470527/`: **전용 서버/원격 두 클라이언트 네트워크 runner 종료 0**. 늦은 참가, 서버 timed Strike의 적중/회피/취소, Iris HP/예고/로컬 indicator 관찰, 리셋/재시작/사망과 실제 첫 클라이언트 종료에 따른 남은 타깃 선택(`RAID_THREAT_DISCONNECT Fallback=1`), BT/GAS 취소/Task 실패(`RAID_ABILITY_DISCONNECT Cancelled=1 TaskFailed=1`), Threat reset을 통과했다. 의도적 종료는 runner 자신의 클라이언트 하나뿐이다. 완료 뒤 2초 Fatal/Assert/Ensure/Iris·Net 오류 감시도 통과했다. 이 기존 네트워크 회귀는 **같은 Pawn의 Controller 교체를 직접 주입한 네트워크 증거가 아니며**, 해당 경계는 앞의 native Ability/Damage 테스트로 검증한다. 별도 네트워크 proof JSON은 생성하지 않았다.

native proof와 네트워크 실행 전후 hash 검사의 DLL SHA256은 `ABB7D30C53BEB3914C940D5AEA91041D6D96B652751BB9CECF5066AC68ADECD5`다. 네트워크 wrapper는 runner 성공 이후에도 DLL 불변을 확인하고 종료 0을 반환했다. 그 뒤 마지막 확인에서는 공유 DLL이 `9BC856D49A02BCA95F95E07433239AE7D49D51A3BFE346A8E6B90A670EE3FFCF`로 바뀌어 있었다. 따라서 위 통과는 **ABB7... 검증 빌드**의 결과이며 이후 DLL을 재검증했다고 주장하지 않는다. 불필요한 공유 빌드/테스트 반복은 하지 않았다. 다른 맵/리슨/보안 전체 회귀나 이전 보스 사망 E2E를 새 DLL에서 반복했다고 주장하지 않는다. 같은 Controller의 모든 빙의 세대, Steam/실제 재접속, Shipping/GPU/지연·손실 검증은 별도 범위다. 시야 상실마다 전투를 자동 리셋하거나 arena 정책을 바꾸지 않았다.

재실행: `Scripts/Tests/Run-BossStateAutomationTest.ps1 -SkipSDKSetup`; NPC 네트워크 회귀: `Scripts/Network/Run-BossStateNetworkTest.ps1 -SkipSDKSetup -Port 18793 -TimeoutSeconds 240`. 다음 NPC 구현 범위는 보스 공격의 몽타주/Cue 연출 연결이다.

## 44. 레이드 공격의 GAS 몽타주·경고 Cue 연결 (2026-10-11)

### 구현과 권한 경계

NPC·보스 범위만 변경했다. `raid-monster-ai` 지침에 따라 BT 선택, 서버 타이머의 공격 시점, 기존 HitQuery/GAS 피해 승인, 페이즈·타깃·전투 context 검증과 Iris 상태 복제를 유지한다. 플레이어 스킬·입력·백엔드 코드는 수정하지 않았다.

- `URPGBossTelegraphedStrikeAbility`에 선택적인 in-place `PresentationMontage`, 재생 속도, 일회성 `WindupCueTag`, 서버 `RecoverySeconds`를 추가했다. 기존 미설정 Ability는 recovery 0으로 동작한다. ASC `PlayMontage`로 복제하며 anim instance가 없는 native fixture에서도 서버 공격은 정상 진행한다. root motion 몽타주는 활성화 시 거절한다.
- 피해는 기존 `WindupSeconds` 타이머에서 한 번만 적용한다. animation notify나 클라이언트의 몽타주 종료가 공격 시점을 결정하지 않는다. 회복 시간에는 같은 서버 타이머·activation serial을 사용하며, 취소 시 타이머를 비운다. 자기 Ability가 여전히 소유한 실행 몽타주만 정지하고 GAS animation 소유권을 해제한다. 사망·피격 등 다른 Ability의 몽타주는 정지하지 않는다.
- 새 native `URPGBossAttackWarningCue`와 `/Game/GameplayCue/GC_Boss_AttackWarning`은 `GameplayCue.Boss.AttackWarning`의 cosmetic-only OnExecute다. 기존 Niagara `NS_UnbloackableWarning`을 재사용하고, 전용 서버에서는 시각 효과를 생성하지 않는다. NullRHI에서 실패한 spawn 반환값을 역참조하지 않는다. gameplay tag·충돌·피해·이동 상태를 변경하지 않는다.
- 기존 `GC_NPC_AttackWarning` 및 일반 NPC 공격 몽타주는 수정하지 않았다. 원본 공격에는 무기 충돌/MotionWarping notify가 있으므로 직접 연결하지 않고 레이드 전용 sequence 두 개를 복사했다. 복사본은 모든 notify/sync marker를 제거하고 root motion을 비활성화하며 root lock을 강제한다. 해당 복사본만 참조하는 몽타주 두 개를 생성하고 기존 SwordMan slot을 사용한다. AnimBP와 원본 sequence는 저장하지 않았다.

새 에셋은 `/Game/Blueprints/Character/NPC/SwordMan/Presentation/`의 `A_RaidSwordMan_Strike`, `AM_RaidSwordMan_Strike`, `A_RaidSwordMan_HeavyStrike`, `AM_RaidSwordMan_HeavyStrike`다. 기존 `GA_RaidSwordMan_Strike/HeavyStrike` 두 BP에는 몽타주·Cue·recovery 0.35초만 설정했다. 원본 SwingAndSlam/Uppercut의 첫 충돌 시점 1.644185/2.896176초를 기존 서버 windup 1.2/1.6초에 맞추는 재생 속도를 사용한다. 이후 원본 combo 공격은 연출/피해에 포함하지 않는다.

일회성 Cue는 늦은 참가자가 복원해야 하는 gameplay 상태가 아니다. 기존 복제 telegraph의 위치·서버 마감 시간과 indicator가 예고 상태의 기준이다. 몽타주와 Cue는 그 상태의 연출이며, transient Cue의 지연·손실·재접속 재생을 이번 결과로 보장하지 않는다.

### 에셋 설정 및 native 검증

읽기 전용 `AuditRaidBossAttackPresentation.py`로 원본 montage section/notify 및 경고 Cue를 확인했다. 첫 audit는 Python에 노출되지 않은 `composite_sections` 조회로 실패했으며, 노출된 section API로 수정한 `RaidAttackPresentationAudit2-20261011.log`는 종료 0/`NoAssetsSaved=1`이다.

`ConfigureRaidBossAttackPresentation.py`는 기본 실행에서 검증만 수행하며 `-RPGApplyRaidBossAttackPresentation`일 때만 위 레이드 에셋을 설정한다. 기존 새 에셋은 검증/재사용하고 덮어쓰지 않는다. 변경 전 두 Ability BP는 `Saved/Backups/RaidAttackPresentation-46c3e93aafaf4319bf50067de80f54f1/`에 보존했다. 첫 설정 실행은 존재하지 않는 Python notify 제거 메서드 호출로 저장 전에 실패했다. 로컬 UE 소스에서 확인한 `remove_all_animation_notify_tracks`로 보완한 `Saved/Logs/RaidAttackPresentationConfig2-20261011.log`는 **종료 0/`RaidOnly=1 InPlace=1 NoGameplayNotifies=1 OriginalNPCAssetsSaved=0 Applied=1`**이다. 저장 로그는 레이드 sequence/montage 4개, 기존 레이드 Ability BP 2개, 새 Cue BP 1개만 포함한다.

`ProjectRPG.Boss.RaidContent`에 두 저장된 공격의 montage/sequence root motion 비활성화, root lock, notify 부재, 재생 속도·Cue·recovery 설정 검사를 추가했다. 기존 실제 BT/공격 BP를 mesh 없는 GAS Pawn에서 실행하는 피해·페이즈 취소·wipe/reset·타깃 상실 회귀도 유지한다.

- `Saved/Logs/RaidAttackPresentationBuild-20261011.log`: 자체 Win64 Development Editor **13개 작업 Succeeded/종료 0**. Makefile은 workspace 내부 `.raid-attack-29c329cb6db64b68ad663ccf4217be04-backup`으로 보존했다. 빌드 완료 후 AppData Trace.uba 쓰기 제한 경고는 별도이며 외부 경로를 수정하지 않았다.
- `Saved/Tests/BossState/Automation-a1c6802dbbcf414ca8eed1e25b73d7f4/Boss.proof.json`: **보스 16종 Success/runner 종료 0**.

### 실제 맵의 전용·리슨 Iris 검증

`Run-BossMapE2ETest.ps1 -BossAttackPresentation`은 기존 NaturalCombat/CombatPhases 시나리오를 사용한다. 새 probe는 실제 AnimInstance의 `Montage_IsPlaying`과 ASC montage를 관찰하고, 페이즈 ACK 전에 해당 공격의 몽타주가 실제 재생 중인지 확인한다. 공격 수동 활성화·새 피해 RPC·강제 montage 재생은 없다. 페이즈 전환 자극은 기존 서버 전용 threshold GE이며 실제 플레이어 공격으로 간주하지 않는다.

최초 전용 실행 `601b5ae4c6c4441d84d4b3e63ca9731c`은 양쪽 클라이언트 Cue와 서버/클라이언트 montage, 페이즈 취소·강공격·잔여 피해 부재까지 확인했지만, runner가 전용 서버 자체 Cue 실행 marker도 요구했다. UE `GameplayCueManager`의 기본 `AbilitySystem.GameplayCue.RunOnDedicatedServer=0` 때문에 그 marker가 생기지 않아 대기가 지속됐고, 이후 전투 종료로 client `NaturalCombatClientEncounterChanged`가 발생해 **종료 1/proof 없음**이다. 성공으로 집계하지 않는다. runner 조건을 양쪽 원격 클라이언트 Cue 필수, 리슨일 때만 host Cue 추가 필수로 수정했다. 전용 서버에 cosmetic 실행을 강제하거나 오류 guard를 완화하지 않았다. 해당 프로세스들은 runner 자체 cleanup으로 종료됐다.

- 전용: `Saved/Tests/BossMap/2aa044613faf4c7f81d1a3eee59fb78d/BossMap.proof.json`, **runner 종료 0/완료 proof**.
- 리슨: `Saved/Tests/BossMap/b570961058424d81b5b9cdc04acf62b2/BossMap.proof.json`, **runner 종료 0/완료 proof**. 더 엄격했던 초기 runner에서 host Cue까지 통과했으므로 재실행하지 않았다.

두 실행 모두 자연 BT/GAS 일반 공격·강공격, 서버와 원격 두 클라이언트의 실제 ASC montage 재생, 두 클라이언트의 native Cue 실행, threshold GE에 따른 페이즈 전환/이전 montage 소유권 해제, 취소된 공격의 잔여 피해 부재 및 실제 HeavyStrike GAS 피해·HP 복제를 확인했다. 늦은 참가자 포함, 30FPS/NullRHI, 전 프로세스 Iris 필수, 완료 후 2초 Fatal/Assert/Ensure/Iris·Net/Blueprint Error 검사와 NPC/Cue `Accessed None` 검사도 통과했다. 기존 GameMode/Shared HUD 및 무관한 선택 콘텐츠 경고를 이번 작업에서 수정하지 않았다.

native proof·두 맵 proof와 최종 확인의 DLL SHA256은 모두 `EC636976DA05B5ECED033256BD73BEB43D592BEC82045FBAC9016B19606E1903`이다. 두 맵 proof에는 위 7개 레이드 에셋의 실행 전후 SHA256이 기록돼 있으며, 최종 확인에서도 서로 동일하고 현재 파일과 일치했다. 플레이어/백엔드 전체 검증이나 이전 사망 E2E를 이 DLL에서 반복했다고 주장하지 않는다. GPU 연출 품질·Shipping·지연/손실·재접속과 실제 플레이어 입력의 페이즈 전환은 별도 검증 범위다.

재실행: `Scripts/Network/Run-BossMapE2ETest.ps1 -BossAttackPresentation -SkipSDKSetup -Port 18831`; 리슨은 `-ListenServer -Port 18832` 추가. 이번 기본 공격 연출 연결은 완료했으며, 신규 패턴/보스 콘텐츠 제작과 GPU 기반 연출 조정은 별도 후속 작업이다.

## 45. 페이즈 데이터 기반 GAS 공격 우선순위 선택 (2026-10-11, 빌드 차단/실행 대기)

### 구현 범위

`raid-monster-ai` 지침에 따라 공격 선택은 서버 BT, 실행 가능 여부는 기존 GAS, 결과/연출은 기존 보스 상태·telegraph·ASC/Iris 경계에 유지한다. 플레이어 스킬·이동·백엔드는 수정하지 않았다. 새 공격 종류나 수치를 임의로 만들지 않고, 같은 페이즈에 공격을 추가할 때 BT의 실행 노드를 매번 늘리지 않아도 되는 선택 경계를 구현했다.

- `FRPGBossPhaseDefinition::AbilityPatterns`: 우선순위 순서의 보스 Ability 클래스 목록. null·추상/폐기 클래스·같은 페이즈 내 중복은 정의 검증에서 거절한다. 서로 다른 페이즈에서 같은 Ability를 재사용할 수 있다. 빈 목록은 기존 명시적 AbilityClass 방식과의 호환을 위해 정의 자체에서는 허용한다.
- `URPGBossAIComponent::GetActivePhaseDefinition`: 서버 전투 중에만 전투 시작 시 복사한 `ActiveDefinition`의 현재 페이즈를 읽는다. 편집 가능한 원본 Definition에서 공격을 선택하지 않는다. 원본 목록 수정은 다음 전투 시작 때 반영한다. 클라이언트/Idle/Resetting/Wiped/Dead에는 선택 데이터를 반환하지 않는다.
- `UBTTask_RPGBossAbility::bUsePhasePatterns`: 기본 false라 기존 명시적 클래스 실행을 유지한다. opt-in 시 AbilityClass를 무시하고 현재 페이즈 목록에서 이미 부여돼 있으며 비활성이고 GAS `CanActivateAbility`를 통과하는 첫 공격을 선택한다. 태그·쿨다운·비용 조건은 GAS가 판단하며 별도 쿨다운 시스템을 만들지 않았다. 미부여/이미 실행 중/거절된 공격은 건너뛰고 전부 불가하거나 목록이 비면 실패한다. 자동 grant나 명시적 클래스 fallback, 다른 실행의 채택/취소는 없다.
- 선택 query에 authored callback이 있을 수 있으므로, query 이후 상태 Revision 또는 frozen phase identity가 바뀌면 선택을 무효화한다. 이후 실제 GAS 활성화와 기존 activation serial·타깃/Controller·전투/페이즈 검증, timeout·취소 처리도 유지한다.

이 정책은 무작위/순환이 아니라 **첫 실행 가능한 공격의 우선순위 선택**이다. 상위 공격이 항상 가능하면 계속 그 공격을 고른다. 다양하게 섞으려면 Ability의 GAS 쿨다운/활성화 조건을 함께 설정해야 한다. 기존 레이드 BT의 두 phase gate·추적·회전·회복 wait는 보존하며, 세 번째 페이즈의 BT gate나 소환/다중 타격/새 장판 기믹까지 자동 제작하는 기능으로 간주하지 않는다.

### 에셋 적용 준비와 테스트 소스

새 `Scripts/Editor/ConfigureRaidBossPhasePatterns.py`는 기본 실행에서 검증만 한다. `-RPGApplyRaidBossPhasePatterns`일 때 기존 `DA_RaidSwordMan`의 각 페이즈를 Strike/HeavyStrike 목록으로 설정하고, 기존 `BT_RaidSwordMan`의 공격 task 두 개만 phase selection opt-in으로 변경한다. 트리/그래프를 재구축하거나 공격 BP·startup grant·공격 수치·일반 NPC 에셋을 저장하지 않는다. 예상 밖의 phase layout/기존 custom pattern/명시적 공격은 덮어쓰지 않고 실패한다. 이미 동일한 설정이면 저장하지 않는다.

**아직 에셋 적용 스크립트를 실행하지 않았다.** 기존 Definition과 BT는 `Saved/Backups/RaidPhasePatterns-02ab0143abb54f2195cdded13d7a1c94/`에 백업했다. 기존 `ConfigureRaidSwordManCombat.py`의 검증도 명시적 task와 데이터 선택 task를 모두 인식하도록 보완했다. 신규 공격을 실제 콘텐츠에 추가할 때는 해당 Ability BP/ActionTag와 서버 startup grant, phase 목록, 클라이언트 telegraph style도 함께 작성해야 한다.

- 새 `ProjectRPG.Boss.PhasePatterns` 소스: 실제 native BT/GAS의 우선순위 선택, 미부여 skip/grant 부재, ASC cooldown tag를 사용하는 GAS CheckCooldown fallback, 모든 후보 거절, 외부 실행 보존, 원본 수정과 frozen 목록, phase 전환, 빈 목록의 fallback 거절, 재시작, 클라이언트/Wiped 차단 및 activation query 중 reset을 검사하도록 작성했다. cooldown tag 자극은 native fixture이며 운영 cooldown GE의 시간 경과를 증명하지 않는다.
- 기존 `Definition`에는 pattern class 검증을, `RaidContent`에는 저장된 phase 목록과 두 task migration 검사를 추가했다. 자동화 runner는 이제 새 테스트를 포함한 **17종을 요구**한다. 아직 17종 실행/통과 결과는 없다.
- 맵 runner는 기존 `-BossAttackPresentation` 시나리오를 재사용하되 서버의 실제 phase-data 선택 marker를 추가로 요구하고, Definition/BT를 포함한 레이드 에셋 **9개**의 실행 전후 hash를 검사하도록 보강했다. 신규 RPC·새 전투 자극·별도 테스트 프레임워크는 없다. 두 Python/두 PowerShell 스크립트의 구문 검사는 통과했다.

### 빌드 결과와 차단 요인

`Saved/Logs/RaidPhasePatternsBuild-20261011.log`: UHT 처리 후 전체 Editor 빌드는 **Failed (OtherCompilationError)/종료 6**, 새 DLL 링크와 에셋 적용/자동화/네트워크 실행은 미완료다. `BTTask_RPGBossAbility`, `RPGBossAIComponent`, `RPGBossDefinition`, 신규 `RPGBossPhasePatternTests`, 확장 `RPGBossRaidContentTests`·`RPGBossStateTests`의 분리 컴파일은 확인했지만 전체 빌드나 테스트 성공으로 집계하지 않는다.

새 테스트 fixture의 local `Boss`가 부모 Ability의 private member 이름을 가린다는 C4458 오류는 `EncounterState`로 이름을 바꿔 수정했다. **수정한 fixture의 재컴파일은 대기 중**이다. 동시에 작업 범위 밖 `Source/Project_RPG/Public/Character/RPGPlayer.h`의 `ARPGPlayer::ServerMovePacked_Implementation(... ) override`에서 C3668이 발생해 해당 헤더를 포함하는 맵/다른 모듈 소스가 실패했다. 로컬 UE5.8 `Engine/Classes/GameFramework/Character.h`의 대응 `_Implementation` 선언은 virtual이 아니다. 그 플레이어 선언을 임의로 삭제/수정하거나 의도한 이동 보안 경로를 우회하지 않았다.

전체 빌드를 반복하거나 기존 DLL에서 새 테스트를 실행하지 않았다. Makefile은 workspace 내부 `.raid-phase-pattern-b509e47944f841408aa471c05fa49d4b-backup`으로 보존했다. 별도 AppData Trace.uba 쓰기 제한 경고는 이번 C++ 실패 원인과 구분한다. 앞서 공유 DLL을 쓰던 다른 실행이 종료되기를 기다렸고 외부 프로세스를 종료하지 않았다.

다음 순서: 범위 밖 플레이어 컴파일 오류 해결 → 수정된 fixture 포함 Editor 빌드 → 위 opt-in 스크립트로 Definition/BT 두 에셋만 적용 → 보스 자동화 17종 → 기존 공격 연출 시나리오의 전용/리슨 Iris 검증. 현재는 **NPC 구현 소스 작성 완료, 적용·실행 검증 대기**이며 44절의 이전 16종/두 맵 통과를 새 구현의 검증 근거로 바꾸지 않는다.

### 재개 후 빌드·적용·자동화 결과

사용자가 필요한 수정까지 허용한 뒤 현재 소스를 확인했을 때, 병행 작업에서 플레이어의 비가상 `ServerMovePacked_Implementation` override를 이미 제거하고 `ProcessEvent`의 실제 서버 RPC dispatch 차단으로 바꾼 상태였다. 함수 이름과 `FUNC_Net | FUNC_NetServer`, 서버 권한 및 던전 권한 회수 조건을 검사하며 정상 호출은 부모 구현으로 전달한다. 이 변경을 덮어쓰거나 플레이어 보안 의도를 우회하지 않았다. 플레이어/백엔드 전체 검증 완료를 의미하지 않는다.

`Saved/Logs/DungeonEjection-ModuleLink-20261011.log`에서 수정된 `RPGBossAbilityTestFixture.cpp` 재컴파일을 확인했고, 후속 `DungeonEjection-ModuleLink-20261011-Retry.log`와 최종 `DungeonEjection-ModuleLink-20261011-Final.log`의 Editor 모듈 링크는 **Succeeded**다. 완료된 공유 빌드를 재사용했으며 별도 반복 빌드나 외부 프로세스 종료는 하지 않았다. 45절 위쪽의 실패/미적용 기록은 당시 상태를 보존한 것이며 아래 결과가 최신 상태다.

`Saved/Logs/RaidPhasePatternsConfig-20261011.log`: opt-in 적용 **종료 0**, `RAID_PHASE_PATTERNS_CONFIG COMPLETE FrozenPhaseSelection=1 GASPriority=1 OnlyRaidDefinitionAndBTSaved=1 Applied=1`. 적용 직전 Definition/BT 두 파일이 기존 백업과 각각 같은 SHA256인 것을 확인했다. 두 에셋만 저장했으며 공격 BP·startup grant·일반 NPC·플레이어 에셋은 이 스크립트로 저장하지 않았다.

`Saved/Tests/BossState/Automation-72f44f93ab9c49e8935d06bd813946c6/Boss.proof.json`: **보스 자동화 17종 Success/runner 종료 0**. 새 PhasePatterns와 저장된 에셋을 검사하는 RaidContent 모두 통과했다. 테스트 중 고정된 Editor DLL SHA256은 `147131045702D04CC4FB6F9432BA52C38AF0B2A398386A072C28A469EAD44F80`이다. 전용/리슨 실제 맵 검증 결과는 별도로 기록한다.

### 실제 맵의 전용·리슨 Iris 검증 완료

- 전용: `Saved/Tests/BossMap/3548c4a64e984c6f9bca59fdf8c1e6ea/BossMap.proof.json`, **runner 종료 0/완료 proof**.
- 리슨: `Saved/Tests/BossMap/2d96c0fa201246fe8b4149d2e480b4b9/BossMap.proof.json`, **runner 종료 0/완료 proof**.

두 실행 모두 기존 `-BossAttackPresentation` 시나리오에서 phase 0의 Strike와 phase 1의 HeavyStrike가 **서버의 phase-data 선택**을 거쳐 실제 BT/GAS로 실행된 것을 확인했다. 서버 및 두 원격 Iris 클라이언트의 ASC/AnimInstance montage, 두 클라이언트의 cosmetic Cue, phase 전환 시 이전 공격·montage 취소, 취소된 공격의 잔여 피해 부재와 자연 HeavyStrike GAS 피해를 검사했다. 리슨은 host Cue도 확인했고 전용 서버의 cosmetic Cue 실행은 요구하지 않았다. 중도 참가 클라이언트를 포함하며 30FPS/NullRHI 실행이다. 두 proof의 `bossPhasePatternSelectionVerified`와 `bossAttackMontageCueAndPhaseCancelVerified`가 모두 true다.

최종 확인에서 자동화 및 두 맵 proof의 DLL SHA256이 위 값과 모두 같고 현재 DLL과 일치했다. 두 맵 proof의 레이드 에셋 9개 hash는 서로 같으며 현재 파일과도 일치했다. 두 새 저장 파일은 Definition `F3D3957CE6DAEAB703D8DD5A08CF66C332448A65A1EF645B847984F8155DFA5C`, BT `950242414BB64D9B93709B3724CC7C20DBF53E9223D2C88D4E4150E0EDD467EC`다. 이전 44절 proof를 재사용해 통과로 집계한 것이 아니라, 새 DLL/적용된 데이터로 실행한 결과다.

**페이즈 데이터 기반 GAS 우선순위 선택의 구현·두 에셋 적용·17종 자동화·전용/리슨 Iris 검증은 완료**했다. `raid-monster-ai`의 서버 권한 및 기존 GAS/Iris 경계 지침을 유지했고 새 RPC·별도 쿨다운 시스템·클라이언트 피해 판정은 추가하지 않았다. GPU/VFX의 실제 화면 품질, Shipping, 지연/손실·재접속, 추가 레이드 공격 콘텐츠는 이번 검증 범위 밖이다. PhasePatterns의 cooldown 검사는 실제 GAS cooldown tag 거절 경로이며 운영 GE의 시간 경과 시험은 아니다. 데이터 목록을 늘리면 BT 실행 노드를 늘릴 필요는 없지만 새 공격의 Ability/BP·서버 grant·telegraph style 제작은 여전히 필요하다.

## 46. 보스 패턴의 선택적 GAS 기간 쿨다운 (2026-10-11)

`raid-monster-ai` 지침에 따라 BT 선택 타이머나 플레이어 스킬 데이터 의존성을 추가하지 않고, 기존 보스 Ability의 GAS commit 경계에 선택적 기간 쿨다운을 구현했다.

- `URPGBossGameplayAbility::PatternCooldownSeconds` 기본값은 **0초/비활성**이며 기존 `CooldownGameplayEffectClass` 동작을 그대로 사용한다. 저장된 Strike/HeavyStrike BP나 페이즈 목록, 피해량·반복 간격은 변경하지 않았다.
- 0초보다 큰 설정에는 유효한 `PatternCooldownTag`와 600초 이하의 유한한 기간이 필요하다. 별도의 authored `CooldownGameplayEffectClass`와 동시에 지정한 경우 활성화/commit을 거절해 기존 GE를 몰래 대체하지 않는다.
- commit 시 `URPGBossPatternCooldownEffect`의 HasDuration spec에 기간과 DynamicGrantedTags를 기록해 서버 ASC에 적용한다. 기존 GAS CheckCooldown이 BT 후보를 거절하고 GAS TimerManager가 GE와 태그를 만료시킨다. 태그를 다르게 지정하면 독립 쿨다운, 의도적으로 같게 지정하면 공유 쿨다운이다. 비용 검사와 commit 시점은 여전히 구체 Ability의 책임이다.
- commit한 쿨다운은 공격 종료/취소, 페이즈 전환, 전투 reset/restart로 환급하지 않는다. 동일 ASC에서 원래 GE 만료까지 유지한다. 다른 효과를 함께 제거하는 전체 ASC cleanup은 추가하지 않았다. 클라이언트가 쿨다운이나 선택을 승인하지 않는다.

`PhasePatterns`에 실제 production cooldown을 사용하는 native Ability fixture를 추가했다. 실제 commit→기간 GE/태그→차순위 선택→reset/restart 후 유지→GE 만료→우선 패턴 재활성화, 공유 태그 차단, 취소 후 유지, 비정상 설정 거절과 0초 호환 동작을 검사한다. 이전 loose-tag 검사는 별도로 유지한다.

### 첫 빌드와 테스트 수정 이력

`Saved/Logs/RaidPatternCooldownBuild-20261011.log`는 Makefile 로드 중 AppData XML 설정 접근 제한으로 실패했다. 다른 빌드/런타임이 없는 상태를 확인하고 workspace의 Makefile을 `.raid-cooldown-2cd2c344179d42dea2f01a8aad2764f6-backup`으로 보존했다. `Saved/Logs/RaidPatternCooldownBuild2-20261011.log`는 **27개 작업 Succeeded/종료 0**이며 신규 런타임 코드와 테스트의 컴파일·링크를 완료했다. 완료 뒤 AppData Trace.uba 쓰기 경고는 C++ 결과와 구분한다.

첫 자동화 `Saved/Tests/BossState/Automation-e5a0957a456d44ad8213a8fb176d4a14/automation.log`는 **PhasePatterns Fail/runner 종료 1/완료 proof 없음**이다. 기간 만료 3개 assertion이 실패했다. 동기 테스트가 동일 GFrameCounter에서 여러 번 World::Tick만 호출해 TimerManager가 한 프레임만 진행한 원인이다. 기존 RaidContent와 같은 scoped simulated frame 방식을 재사용해 각 로컬 틱 동안만 프레임 번호를 바꾸고 실제 번호를 복원하도록 테스트를 수정했다. GAS 만료 로직이나 실패 판정을 완화하지 않았다.

첫 새 DLL에서 `Saved/Tests/BossMap/44007e77f06b4e589cbc9cfd084325dd/BossMap.proof.json`의 **전용 서버/두 원격 Iris 클라이언트 회귀는 runner 종료 0**이다. 저장된 기본 공격/강공격의 phase-data 선택·montage/Cue·이전 공격 취소와 잔여 피해 부재, 9개 에셋 불변을 확인했다. 기존 BP는 0초 설정이므로 이 맵 결과가 **활성화된 기간 쿨다운의 네트워크 관찰 증거는 아니다**. 수정된 테스트 포함 최종 빌드/자동화 결과는 아래에 별도로 기록한다.

### 수정된 테스트의 최종 검증

수정된 테스트 빌드 `Saved/Logs/RaidPatternCooldownTestBuild-20261011.log`는 공유 UBT mutex 대기 후 기존 Makefile의 AppData 설정 접근 제한으로 **종료 6**이었다. 병행 빌드를 중단하거나 사용 중인 캐시를 이동하지 않았다. 이어 `Saved/Logs/DungeonEjectionNetwork-Module-Retry-20261011.log`의 `[1/6] Compile RPGBossPhasePatternTests.cpp`에서 수정된 테스트 object가 생성됐지만, 다른 NPC pickup probe의 로그 매크로를 감싼 `if/else`에 중괄호가 없어 모듈 링크는 실패했다. 이 문법만 고치려던 patch 시점에 병행 작업에서 이미 동일하게 수정돼 patch를 적용하지 않았다. pickup/아이템/백엔드 동작 변경은 이번 보스 작업에 포함하지 않는다.

`Saved/Logs/NPCPickupParty-EditorModule-Final-20261011.log`: **3개 작업 Succeeded**, 위 수정된 테스트 object를 포함한 최종 Editor DLL 링크 성공. 불필요한 별도 재빌드 대신 완료된 공유 결과를 재사용했다.

`Saved/Tests/BossState/Automation-a7a696c448d44e8cb5763df020a40ba6/Boss.proof.json`: **보스 17종 Success/runner 종료 0**. PhasePatterns의 실제 기간 GE 만료·태그 제거·차순위 선택·우선 패턴 복귀, 공유 태그 차단, 취소 및 reset/restart 후 유지, 잘못된 설정/0초 검사가 모두 통과했다. 최종 확인한 현재 DLL과 proof의 SHA256은 `4DE3589D942DE041618D8D59A5FF255A5DBF0D3F613CF223F16332B3D990EBC2`로 일치했다.

앞의 전용 맵 proof는 **다른 DLL** `F5AFE6FF49AD11392ADE9C43802FD3D5956CE0FE2B32F5E4C98E89C0EC0E3B9D`에서 얻은 결과다. 테스트 프레임 진행 수정과 병행 작업의 링크가 이후에 있었으므로 이를 최종 DLL의 네트워크 회귀라고 집계하지 않는다. 관련 레이드 에셋 9개는 최종 확인 시에도 해당 맵 proof와 동일했다. 첫 자동화 실패 역시 최종 통과로 소급하지 않는다.

**선택적 보스 GAS 기간 쿨다운의 소스 구현 및 실제 GE 경로 네이티브 검증은 완료**했다. 저장된 보스의 쿨다운/패턴 조합은 그대로다. 다음 콘텐츠 작업에서는 패턴별 태그와 기간·우선순위를 실제 공격 BP/phase 목록에 설계한 뒤 활성화된 쿨다운 경로의 전용/리슨 네트워크 동작을 검증해야 한다. GPU/Shipping/지연·손실 및 신규 공격 기믹의 완성은 주장하지 않는다.

## 47. 강공격의 다중 참가자 범위 피해 구현 (2026-10-12)

기능 구현 우선 요청에 따라 기존 원형 예고 강공격을 실제 다중 대상 공격으로 연결했다. `raid-monster-ai`의 서버 판정/GAS 피해/기존 Iris 경계를 유지했으며 새로운 AI 프레임워크나 피해 RPC는 추가하지 않았다.

- `ExecuteAreaServerDamage`: 한 실행에 한 번만 서버 HitQuery를 수행하고 범위 안의 살아 있는 Threat 참가자에게 각 1회 instant GE 피해를 적용한다. 비참가자·아군·무효 Controller·사망/회수된 대상은 제외하고 보안 프로필의 대상 수 제한을 유지한다. 빗나간 범위 공격도 재호출로 다시 타격할 수 없고 단일 대상 피해와 중복 사용할 수 없다.
- 첫 피해 전에 각 후보의 Controller를 캡처한다. 피해 콜백이 다른 후보의 Controller를 교체하고 새 Threat를 등록해도 이전 범위 공격을 이어받지 못한다. 취소·reset·phase/encounter 변경 또는 재활성화 serial 변경은 남은 타격을 중단한다. 같은 순간의 범위 피해로 조준 대상이 사망한 경우에는 나머지 참가자의 피해를 계속 처리한다.
- 기존 `URPGBossTelegraphedStrikeAbility::bHitAllThreatParticipants` 기본값은 false다. `ConfigureRaidBossAreaStrike.py -RPGApplyRaidBossAreaStrike`로 **GA_RaidSwordMan_HeavyStrike만 true로 저장**했다. 기본 Strike는 단일 대상이며 반경·피해량·예고/회복 시간·몽타주·phase/BT/grant는 유지했다. 예고 지점 고정과 서버 타이머 적중도 기존 경로다. 이전 HeavyStrike는 `Saved/Backups/RaidAreaStrike-781d8986a8d948c2920a0cec2efd593c/`에 보존했다.

첫 `RaidAreaStrikeBuild-20261012.log`에서 보스 코드의 컴파일/모듈 링크는 완료됐지만 옵션 UI 플러그인의 JsonUtilities 의존성 누락으로 전체 빌드는 종료 6이었다. 해당 Build.cs의 private Json/JsonUtilities 의존성만 보완했고 UI 동작 코드는 수정하지 않았다. `RaidAreaStrikeLink-20261012.log`는 **9개 작업 Succeeded/종료 0**이다. 초기 켜져 있던 에디터가 종료된 뒤 빌드/에셋 저장을 수행했으며 외부 에디터를 강제 종료하지 않았다. 설정 로그 `Saved/Logs/RaidAreaStrikeConfig-20261012.log`는 **종료 0/HeavyAOE=1 StandardTargeted=1 OnlyHeavyStrikeSaved=1 Applied=1**이다.

검증 결과:

- `Saved/Tests/BossState/Automation-9793cfb1f2ed4884a61573fb00a5363e/Boss.proof.json`: **보스 18종 Success/runner 종료 0**. 새 AreaStrike의 동시 두 참가자 피해·대상 상한·중복/종료 후 차단·조준 대상 치명타 후 나머지 피해·reset 콜백 중단·Controller 교체·범위 이탈 회피/취소를 통과했다. RaidContent도 실제 저장 BT/공격 BP로 기본 공격은 주 타깃에게만 20 피해, HeavyStrike는 주 타깃과 근처 비어그로 참가자에게 각각 35 피해를 확인했다.
- `Saved/Tests/BossMap/ca2eadb880774902926c95c82424f1e1/BossMap.proof.json`: **전용 서버/중도 참가 포함 원격 두 클라이언트 Iris 회귀 종료 0**. 자연 Strike/HeavyStrike·phase-data 선택·서버/클라이언트 montage/Cue·phase 취소와 잔여 피해 부재를 확인했다. 다중 참가자 동시 피해 자체의 직접 증거는 위 isolated native/RaidContent이며, 이 맵 시나리오를 원격 플레이어 두 명의 동시 범위 피격 검사로 간주하지 않는다. 리슨 서버 재실행은 하지 않았다.

최종 확인에서 두 proof와 현재 Editor DLL SHA256은 모두 `B73DCDD7A317086196F005A85CD1B646313467A5974B4D14C55F2F4550A8FB11`로 일치했고 맵 proof의 관련 레이드 에셋 9개도 현재 파일과 일치했다. 새 HeavyStrike SHA256은 `7BBC4B08FDD9ABA7E9757CCB53AD64B382831F9A47866BE888A26DC0F5C973A9`다. 강공격의 다중 참가자 범위 피해 구현 및 기존 콘텐츠 연결은 완료했다. 다중 원격 피격의 별도 직접 관찰, GPU/VFX 화면 품질, Shipping 및 지연/손실 검증은 이번 결과에 포함하지 않는다.

## 48. 자동 광폭화와 GAS 공격력 효과 (2026-10-12, 실행 검증 대기)

`raid-monster-ai`의 서버 권한/GAS 효과/복제 상태 경계로 자동 광폭화를 구현했다. 기존 Definition에 `EnrageSeconds`(기본 0/자동 발동 비활성), `EnragedAttackMultiplier`(기본 1/기존 수동 flag 호환)를 추가했다. 실제 SwordMan 설정·공격 수치·에셋은 변경하지 않았다.

전투 시작 때 복사한 설정으로 서버 타이머를 예약하고 기존 `FRPGBossState`에 GameState 서버 시계 기준 `EnrageServerTime`을 담는다. 페이즈 전환이나 원본 Definition 수정이 예약 시간/배율을 바꾸지 않는다. 현재 encounter ID와 상태를 확인해 자동 발동하며 클라이언트 타이머는 판정 권한이 없다. 수동 광폭화 변경은 자동 예약을 소비한다.

광폭화 시 전용 infinite `URPGBossEnrageEffect`를 서버 ASC에 적용해 Attack 속성을 MultiplyCompound로 증가시킨다. 기존 피해 계산/승인 경로가 이 공격력을 사용한다. 적용이 거절되면 광폭화 성공 flag를 게시하지 않는다. GE 적용/제거 중 재진입 lifecycle 변경은 차단하고 HP 변경 처리는 이후 평가한다. 광폭화 해제·wipe·reset·사망·ASC unbind/EndPlay는 소유한 효과 handle만 제거한다. 다른 공격력 효과나 원래 base 값은 덮어쓰지 않는다.

새 `ProjectRPG.Boss.Enrage` 테스트는 자동 발동·고정 설정·서버 deadline·phase 유지·실제 GAS 피해 31→62·중복 방지·다른 공격력 효과 보존·수동 해제·클라이언트 권한·reset/restart의 이전 타이머 차단·비활성 기본값·GE 거절·사망 정리를 검사하도록 작성했다. 기존 runner는 이를 포함한 19종을 요구한다. 이 시점에는 테스트를 실행하지 않았으며 이전 18종 결과로 대체하지 않는다.

병행 중인 `Saved/Logs/DungeonMembershipEjection-Module-20261012.log`의 전체 빌드는 신규 클래스가 추가되기 전에 생성 파일/작업 목록이 결정돼 `RPGBossEnrageEffect.generated.h` 누락으로 BossAIComponent 컴파일이 실패했다. 이를 올바른 UHT 갱신 후의 신규 코드 컴파일 결과로 집계하지 않는다. 사용 중인 생성 파일/Makefile을 덮어쓰거나 외부 빌드를 종료하지 않고 종료 후 빌드·필요한 검증을 진행한다.

후속 공유 빌드 `Saved/Logs/NPCPickupLostAck-EditorModule-Final-20261012.log`에서 신규 UHT 생성 파일 갱신과 `[5/199] RPGBossAIComponent.cpp`, `[6/199] RPGBossEnrageEffect.cpp`, `[7/199] RPGBossEnrageTests.cpp` 컴파일을 확인했다. 실제 object 생성 시각은 각 소스 수정 시각보다 늦다. 공격력 배율 적용 전 오버플로 검사 및 해당 native 테스트도 포함했다. 이 빌드가 진행 중일 때 별도 빌드/출력물 덮어쓰기를 하지 않았다. **현재 기능 소스 및 테스트 작성·분리 컴파일 완료, 전체 DLL 링크·19종 실행·새 서버 deadline의 Iris 관찰은 대기**다. 실제 보스 설정과 모든 콘텐츠 에셋은 변경하지 않았다.

### 최종 실행 결과 (2026-10-12)

위 공유 전체 빌드는 Succeeded로 완료됐다. 기존 NPC 네트워크 fixture의 수동 광폭화 자극을 **시험 전용 8초/공격력 2배 자동 타이머**로 바꿨다. 첫 피해를 HP 100일 때만 적용해 광폭화 대기 중 반복 피해가 생기지 않도록 했고, 원래 spec은 stale-hit 검사에 그대로 유지했다. 클라이언트는 발동 전 deadline 수신, 광폭화 상태/Attack 2/소비된 deadline 0, reset 후 Attack 1/deadline 0 및 권한 거절을 검사한다. 새 RPC나 별도 네트워크 프레임워크는 없다. 실제 보스 에셋의 발동 시간·배율은 변경하지 않았다.

최신 fixture를 포함한 `Saved/Logs/DungeonMembershipEjection-Module-Retry-20261012.log`는 **Succeeded**다. source/object/DLL 시각을 확인해 완료된 공유 결과를 재사용했다. 최초 자동화 `Automation-f404e2049e7143fe880bce9d7b010805`는 사용자 중단 당시 초기화 단계 로그만 있고 완료 결과/proof가 없어 성공으로 집계하지 않는다.

- `Saved/Tests/BossState/Automation-be45a564cbf44489b1f4a34f9ebcab8d/Boss.proof.json`: **보스 19종 Success/runner 종료 0**. Enrage의 자동 발동·frozen 설정·실제 승인 피해 31→62·중복 방지·다른 GE 보존·수동/클라이언트 권한·reset/restart·비활성 기본값·GE 거절·공격력 overflow 거절·사망 정리를 통과했다.
- `Saved/Tests/BossState/8adb22c4222b43a2965c6731f990b02f/`: `Run-BossStateNetworkTest.ps1 -SkipSDKSetup -Port 18795 -TimeoutSeconds 240` **전용 서버/두 원격 클라이언트 runner 종료 0**. 서버 `RAID_ENRAGE_AUTO_SERVER Attack=2 DeadlineConsumed=1`, 첫 클라이언트 `RAID_ENRAGE_CLOCK_REPLICATED DeadlinePositive=1 Attack=1`, 첫/중도 참가 클라이언트의 `RAID_ENRAGE_REPLICATED Attack=2 DeadlineConsumed=1` 및 `RAID_ENRAGE_RESET_REPLICATED Attack=1 Deadline=0`를 필수 검사했다. 기존 phase/HP/예고·로컬 decal·reset/restart/death·실제 첫 클라이언트 종료 후 Threat/BT/GAS 취소와 완료 후 2초 오류 감시도 통과했다. 종료한 클라이언트는 runner 자신이 생성한 것뿐이다.

최종 확인에서 native proof/current DLL, 네트워크 실행 전후 DLL SHA256은 모두 `E353CAC5A7143849DD985DDBCEAF2D2EF57471487F91D71002F69670E38DCEDD`로 일치했다. 네트워크 runner는 기존 방식대로 로그 증거만 남기며 별도 proof JSON을 추가하지 않았다. **자동 광폭화 기능과 19종/전용 서버 Iris 검증은 이 범위에서 완료**다. 실제 SwordMan의 자동 광폭화는 기본 0초/배율 1 설정을 유지한다. 리슨 재실행·실제 GPU 연출·Shipping/지연·손실은 이번 검증에 포함하지 않는다.

## 49. 전멸·리셋 중 보스 이동과 BT 정지 (2026-10-13)

필수 NPC 기능에 한정해 `ARPGBossCharacter`의 서버 상태 처리를 보완했다. 기존 BattleGameMode 재도전은 이전 보스를 제거하고 스폰 위치에 새 보스를 생성하므로 귀환/리시 시스템을 추가하지 않았다. `raid-monster-ai`의 서버 BT/GAS 실행 및 기존 Iris 상태 경계를 유지한다. 플레이어·백엔드·운영 콘텐츠 에셋은 변경하지 않았다.

- Wiped/Resetting에서 속도를 즉시 제거하고 이동을 비활성화하며 AI 이동 요청과 Gameplay focus를 정리한다. 실행 중이던 BT는 Forced StopTree로 중단해 기존 task-owned GAS 취소 경로를 사용한다.
- 이 보스가 비활성화한 이전 이동 모드/custom mode와 정지시킨 Brain만 기억한다. CompleteReset의 Idle에서는 재개하지 않고 명시적인 InCombat 전환 때만 복원한다. 이동 모드가 외부에서 변경됐으면 덮어쓰지 않으며, 원래 비활성 이동·외부 일시정지 Brain·교체/해제된 Controller의 Brain은 강제로 깨우지 않는다.
- AI 이동 정리 콜백 뒤 상태/Controller를 다시 확인한다. Dead는 복구 소유권을 버리고 기존 영구 정지를 유지한다. CancelAllAbilities는 호출하지 않아 GAS 사망 연출을 보존한다.

기존 NPCStartup 테스트에 실제 AIController/Wait BT를 사용한 wipe 정지·focus 해제·Idle 정지 유지·명시적 재시작의 Flying 모드/Brain 복구·외부 pause 보존 검사를 추가했다. 새 테스트 프레임워크나 네트워크 RPC는 없다.

`Saved/Logs/RaidResetMovementBuild-20261012.log`의 98개 작업과 `Saved/Logs/RaidResetMovementTestBuild-20261012.log`의 4개 작업은 **Succeeded**다. 보완 빌드는 테스트 월드 AISystem 초기화 수정이 이전 object보다 늦었기 때문에 필요했다. 재개 시 현재 DLL은 이 기록보다 최신이었으며 별도 재빌드하지 않고 아래 실행에서 실제 사용한 DLL을 해시로 고정했다. 켜져 있던 사용자 에디터는 종료하거나 덮어쓰지 않았다.

- `Saved/Tests/BossState/Automation-4f3f6d2962bc462d91f14adaf7021bc9/Boss.proof.json`: **보스 19종 Success/runner 종료 0**, 확장된 NPCStartup 포함.
- `Saved/Tests/BossState/62e638dce1564cab8b1bf8d7f48230e5/`: `Run-BossStateNetworkTest.ps1 -SkipSDKSetup -Port 18796 -TimeoutSeconds 240` **전용 서버/두 원격 클라이언트 runner 종료 0**. 기존 Iris 상태·Threat·공격/예고·HP·자동 광폭화·중도 참가·reset/restart/death 및 실제 disconnect의 BT/GAS 취소 회귀를 통과했다. 새 이동 모드/Brain 복원 자체의 직접 assertion은 native NPCStartup이며 이 네트워크 runner의 추가 직접 관찰로 간주하지 않는다.

native proof와 네트워크 실행 전후 DLL SHA256은 모두 `4929070DCC97A5747065E3F7EA423F87C5A9CAC664570A076B220F4ADA1BE9E3`으로 일치했다. 이번 전멸·리셋 정지/재시작 기능은 위 범위에서 완료했다. 리슨·GPU·Shipping·지연/손실 전체 재검증이나 신규 레이드 기믹 완성을 주장하지 않는다.

## 50. 보스 공격 시작 전 서버 거리 검증 (2026-10-13)

필수 NPC 실행 조건을 보완했다. 기존 Strike/HeavyStrike는 피해 적용 때 `SecurityProfile.MaximumServerHitDistance`를 검사했지만 공격 시작에는 더 넓은 Threat 자격 거리만 요구했다. 따라서 추적 가능한 대상이라도 피해 허용 거리 밖에서 예고와 Commit을 시작할 수 있었다.

`URPGBossTelegraphedStrikeAbility`의 GAS CanActivate에 현재 Threat 대상의 거리 검사를 추가하고 실제 Commit 직전 및 직후에도 캡처된 대상을 다시 확인한다. 거리 밖에서는 기본 공격 실행을 거절하고, 활성화 도중 범위를 벗어나면 예고를 게시하지 않고 종료한다. Commit 이후 콜백에서 범위를 벗어난 경우 이미 소모된 비용/쿨다운을 환불하지 않는다. 피해 승인에 사용하는 `FRPGSecurityValidationMath::ValidateCombatHit`의 bounds allowance를 재사용해 큰 대상의 기존 허용 거리를 임의로 좁히지 않는다. 실제 적중은 계속 예고 종료 시 서버 HitQuery/GAS에서 승인하며 예고 도중 이동한 대상의 회피와 빗나감 동작은 유지한다.

`raid-monster-ai`의 서버 GAS/BT 판단·기존 Iris 경계를 유지했다. 별도 거리 수치·클라이언트 RPC·AI 프레임워크는 없다. 공격 BP, Definition, BT 에셋, 플레이어·백엔드 코드는 변경하지 않았다. 현재 보스의 광폭화/쿨다운 활성화나 신규 기믹은 이번 작업에 포함하지 않는다.

기존 RaidContent에 저장된 두 공격 BP의 거리 초과 활성화 거절·예고/sequence 소비 부재·추적 자격 유지·정상 거리 허용·충돌 bounds allowance 보존을 검사하도록 추가했다. 이후 기존 자연 BT의 일반/강공격 피해와 페이즈 전환·wipe/reset·타깃 상실 검사를 그대로 실행한다.

첫 빌드 `Saved/Logs/RaidStrikeReachBuild-20261013.log`는 지역 변수 `Threat`가 부모 Ability의 private 멤버를 가리는 C4458로 종료 6이었다. `TargetSelection`으로 수정한 `Saved/Logs/RaidStrikeReachBuild-Fix-20261013.log`는 **4개 작업 Succeeded/종료 0**이며 첫 빌드에서 컴파일된 확장 테스트도 링크했다. AppData Trace.uba 접근 제한 경고는 별도이며 외부 경로를 수정하지 않았다. 기존 Unreal 프로세스가 정상 종료된 뒤 빌드했고 강제 종료/사용 중 DLL 교체는 하지 않았다.

- `Saved/Tests/BossState/Automation-45bf56f1011741c696bffc90b395d4c7/Boss.proof.json`: **보스 19종 Success/runner 종료 0**, 확장된 RaidContent 포함.
- `Saved/Tests/BossState/037c48cd82264fa7b69071aadf4609b0/`: `Run-BossStateNetworkTest.ps1 -SkipSDKSetup -Port 18797 -TimeoutSeconds 240` **전용 서버/두 원격 클라이언트 Iris 회귀 종료 0**. 기존 상태·Threat·timed strike·예고/HP·중도 참가·reset/restart/death·disconnect BT/GAS 취소를 통과했다. 신규 거리 거절의 직접 증거는 native RaidContent이고 네트워크 시나리오에서 거리 초과를 새로 주입한 것은 아니다.

native proof와 네트워크 실행 전후 DLL SHA256은 모두 `B558962396A33EE71037C9593C14BFCBED35321FD14479EB26F5C09824B07701`로 일치했다. 위 공격 시작 거리 검증은 완료했다. 리슨·GPU·Shipping·지연/손실 전체 재검증은 이번 범위가 아니다.

## 51. 승인된 SwordMan 초기 전투 튜닝 (2026-10-13)

사용자가 선택한 초기 튜닝안: Strike 독립 GAS 쿨다운 4초, HeavyStrike 8초, 2페이즈 목록 `[HeavyStrike, Strike]`, 전투 시작 180초 후 공격력 1.5배 광폭화. HP·기본 피해·예고/회복 시간·몽타주·BT 그래프·startup grant는 유지한다. 최종 난이도 확정이 아닌 초기 콘텐츠 설정이다.

- `Config/DefaultGameplayTags.ini`에 두 `Boss.Cooldown.SwordMan.*` 태그를 추가했다.
- `ConfigureRaidBossCombatTuning.py`는 기본 verify-only이며 `-RPGApplyRaidBossCombatTuning`으로 Definition과 공격 BP 두 개만 저장한다. 기존 기본값 또는 승인된 동일 설정만 허용하고 custom 패턴/쿨다운/별도 cooldown GE/광폭화 설정은 덮어쓰지 않는다. BP 컴파일 뒤 현재 generated class를 다시 읽는다.
- 기존 `ConfigureRaidBossPhasePatterns.py`는 승인된 Heavy/Strike 조합을 인식하고 재실행 때 단일 Heavy 목록으로 되돌리지 않는다.
- 기존 RaidContent에 실제 콘텐츠의 cooldown 태그/기간·180초 deadline·거리 거절 시 cooldown 미소모·자연 BT의 강공격/일반 공격 fallback·강공격 복귀 검사를 추가했다. 플레이어/백엔드 런타임이나 새로운 테스트 프레임워크는 변경하지 않았다.

원본 에셋 세 개는 `Saved/Backups/RaidCombatTuning-d2d76c93c80941a2802fae5effbe134a/`에 보존했다. `Saved/Logs/RaidCombatTuningBuild-20261013.log`는 **4개 작업 Succeeded/종료 0**이다. Python 스크립트 AST 구문 검사도 통과했다.

첫 적용 `Saved/Tests/BossState/Tuning-65ee254808b048b2866f374064fb9079/configure.log`는 cooldown guard에서 저장 전에 **종료 -1**이었다. GameplayTag struct 객체 비교를 TagName 값 비교로 보완했다. 이어 다른 UnrealEditor-Cmd 프로세스들이 실행 중이라 재적용은 시작하지 않았고 해당 프로세스를 강제로 종료하지 않았다. **이 시점에는 에셋 미적용/확장 RaidContent 실행 전**이며 이전 19종 통과를 새 튜닝 검증으로 집계하지 않는다. 다음은 안전한 에셋 저장 → 보스 19종 → 실제 보스 맵 Iris 회귀다.

### 최종 적용·검증 결과

다른 Unreal 실행이 정상 종료된 뒤 `Saved/Tests/BossState/Tuning-5503b1cca6a543cdb8d7687cb2483883/configure.log`에서 **종료 0/`StrikeCooldown=4 HeavyCooldown=8 HeavyFallback=Strike EnrageSeconds=180 EnrageAttack=1.5 SavedAssets=3 Applied=1`**을 확인했다. 저장 로그는 위 Definition과 레이드 공격 BP 두 개만 포함한다. BT·일반 NPC·몽타주·startup·플레이어 에셋은 저장하지 않았다. 초기 guard 실패를 성공으로 소급하지 않는다.

- `Saved/Tests/BossState/Automation-52ae980fe7764045bbef77cfe2c435fb/Boss.proof.json`: **보스 19종 Success/runner 종료 0**. 확장 RaidContent에서 실제 저장된 두 쿨다운 GE/태그 commit, 거리 거절 시 쿨다운 미소모, 강공격 쿨다운 동안 자연 BT의 일반 공격 fallback과 단일 대상 20 피해, 만료 후 강공격 복귀, 기존 강공격 두 참가자 35 피해·phase 취소·wipe/reset·타깃 상실을 확인했다. 실제 Definition의 180초/1.5배 설정과 서버 deadline 예약도 확인했다. 자동 광폭화 GE 발동/정리는 기존 Enrage native 검사에 포함된다.
- 전용 맵: `Saved/Tests/BossMap/68f6c0b31e5f41428eed42df33bfca43/BossMap.proof.json`, **runner 종료 0**.
- 리슨 맵: `Saved/Tests/BossMap/b31753096d54448bbb669b582d4b969b/BossMap.proof.json`, **runner 종료 0**.

두 맵은 `Run-BossMapE2ETest.ps1 -BossAttackPresentation -SkipSDKSetup`에 각각 `-Port 18835`, `-ListenServer -Port 18836`을 사용했다. 자연 BT/GAS 기본·강공격, phase-data 선택, 서버/두 원격 Iris 클라이언트의 ASC 몽타주·경고 Cue·예고/HP, 공격 중 phase 전환·이전 공격 취소와 잔여 피해 부재를 통과했다. 리슨 host Cue와 완료 후 runtime 오류 감시도 기존 runner 조건대로 유지했다. 이 맵 시나리오는 **180초 광폭화 발동까지 대기하거나 원격 클라이언트의 쿨다운 fallback/만료를 새로 직접 관찰한 검사는 아니다**. 새 fallback과 실제 cooldown의 직접 실행 증거는 native RaidContent다.

최종 확인에서 native proof·두 맵 proof·현재 DLL SHA256은 모두 `C65A8DE9937EF25EA7F8DAC2A828A07D2D420E43756F7DAC3C711BEB3482CCF1`로 일치했고 두 맵 proof의 레이드 에셋 9개도 현재 파일과 일치했다. **승인된 초기 튜닝 적용과 위 검증은 완료**했다. 최종 난이도 밸런싱·GPU 화면 품질·Shipping·지연/손실 및 신규 기믹은 별도 범위다.

## 52. 실제 원격 두 참가자의 강공격 동시 피격 검증 (2026-10-14 정리)

기존 맵의 phase 검사는 서버에서 HeavyStrike 피해 1회, 각 클라이언트에서 임의 참가자 HP 감소만으로 완료할 수 있었다. 이를 두 원격 참가자가 모두 같은 범위 공격에 피격됐다는 직접 증거로 간주하지 않고, 기존 runner/probe에 opt-in `-BossAreaStrike` 검사를 연결했다. `raid-monster-ai`의 서버 Perception/BT/GAS/Iris 경계를 그대로 사용한다. 보스 전투 수치·콘텐츠 에셋·플레이어 스킬/백엔드 동작은 변경하지 않았다.

- 이 옵션은 기존 BossAttackPresentation/CombatPhases 경로를 포함한다. 검증 전용 서버 placement로 실제 remote Pawn 두 개를 현재 타깃 근처에 배치하고, production Perception/Threat가 두 참가자를 등록하기를 기다린다. 수동 보스 공격 활성화·참가자 피해 GE·Threat 주입은 없다. 기존 서버 전용 보스 HP threshold 자극은 phase 전환 검사에 그대로 사용하므로 실제 플레이어 입력으로 phase를 전환한 증거는 아니다.
- 실제 HeavyStrike 예고 범위에 두 참가자가 있는지 검사하고, GE context의 **실제 Ability instance** 실행 serial을 캡처한다. `GetAbility()`는 CDO이므로 실행 식별에는 서버의 `GetAbilityInstance_NotReplicated()`를 사용한다.
- 두 remote Pawn의 GAS 피해가 같은 serial/예고 sequence와 같은 `GFrameCounter`에서 각각 1회 발생해야 한다. 중복 피해나 다른 실행/프레임이면 실패한다. 클라이언트는 자신의 Pawn HP 감소를 관측해야 기존 ACK를 보낼 수 있고 서버는 실제 피격한 해당 Pawn의 ACK만 집계한다. 새로운 RPC는 추가하지 않았다.
- runner는 서버의 `RemoteVictims=2 SameExecution=1 SameFrame=1 OnceEach=1 OwnedHPClients=2` 및 양쪽 클라이언트의 `OwnHPDecreased=1`을 필수로 요구한다. proof에는 두 remote 피격 검증과 server-position fixture 사용 여부를 기록한다. 기존 phase 취소/잔여 피해 부재·ASC 몽타주/Cue·late join·Iris·runtime 오류 검사도 유지한다.

`Saved/Logs/RaidRemoteAreaBuild-20261013.log`: **12개 작업 Succeeded/종료 0**. 외부 Unreal 테스트가 정상 종료된 뒤 빌드했고 사용 중 DLL을 교체하거나 외부 프로세스를 종료하지 않았다. AppData Trace.uba 접근 경고는 별도이며 외부 경로를 수정하지 않았다.

- `Saved/Tests/BossState/Automation-3a64e25206284f569307275be80e7b39/Boss.proof.json`: **보스 19종 Success/runner 종료 0**.
- 전용: `Saved/Tests/BossMap/687df3c6114c44b9bcfb503ad37807d1/BossMap.proof.json`, **runner 종료 0**. Sequence 3/serial 1/frame 1678의 두 remote 피해를 확인했고 각각 HP 269→215, 300→246으로 복제됐다.
- 리슨: `Saved/Tests/BossMap/12b98a1d521248738c0e62f2f6ca7311/BossMap.proof.json`, **runner 종료 0**. Sequence 3/serial 1/frame 1680에서 두 remote Pawn이 각각 한 번 피격됐으며 HP 300→246, 300→192를 각 소유 클라이언트가 관측했다. 피해량은 기존 공격/방어/치명타 계산을 사용하며 동일 수치로 강제하지 않았다.

재실행은 `Run-BossMapE2ETest.ps1 -BossAreaStrike -SkipSDKSetup -Port 18837`, 리슨은 `-ListenServer -Port 18838`이다. 두 실행 뒤 runner의 출력 문구만 참가자 피해 주입과 기존 보스 HP threshold 자극을 구분하도록 보완했다. 판정 조건이나 proof는 바꾸지 않았고 테스트를 반복하지 않았다.

재개 후 최종 확인에서 native proof·두 맵 proof·현재 DLL SHA256은 모두 `92D9005252447C8DD20B996297BE62456B125DA85ECEEAEBBBD940A8C87A3E52`로 일치했고 두 맵 proof의 레이드 에셋 9개도 현재 파일과 일치했다. **원격 두 참가자의 동시 강공격 피격 직접 검증은 위 범위에서 완료**다. 임의 입력으로 범위에 모이는 플레이, 180초 광폭화 실제 발동 관찰·GPU·Shipping·지연/손실 전체 검증은 이번 결과에 포함하지 않는다.

## 53. 공통 지속 장판 GAS 패턴 (2026-10-14, 소스 작성/빌드 대기)

여러 보스가 선택해 조합할 공통 패턴을 장판 → 돌진 → 소환 순서로 구현한다는 방향에 따라 첫 지속 장판의 소스를 작성했다. `raid-monster-ai`의 서버 Perception/BT/GAS 피해·복제 상태 경계를 유지한다. SwordMan의 실제 패턴·startup grant·공격 수치·에셋은 변경하지 않았으며 플레이어 스킬/백엔드 코드는 수정하지 않았다. 기존 단발 범위 공격의 한 실행 한 판정/중복 방지 규칙도 변경하지 않았다.

- `URPGBossPersistentHazardAbility`: 서버 GAS 시전·commit과 고정 예고 지점을 사용한다. 예고 종료 후 `ARPGBossPersistentHazard`에 상태를 인계하고 Ability를 종료해 BT가 다음 패턴을 실행할 수 있도록 했다. 시전 취소는 timer/미완성 spawn을 정리한다. 장판 spawn 전에는 현재 타깃·Controller·전투/페이즈·activation serial·예고 sequence를 검사한다. 필수 ActionTag/instant damage GE/BaseDamage는 보스 제작자가 설정해야 하며 미설정 상태로 공격을 허용하지 않는다.
- `ARPGBossPersistentHazard`: 별도 ASC 없이 시전자의 instant GAS spec/보안 프로필·반경·주기를 복사한다. 즉시 첫 pulse 후 one-shot 서버 timer로 다음 pulse를 예약하며 지나간 주기를 한 프레임에 몰아 타격하지 않는다. 각 pulse마다 기존 HitQuery를 다시 수행해 살아 있는 Threat 참가자에게 각 1회 피해를 승인한다. 빗나감도 pulse를 소비하고 비참가자/아군/무효 Controller를 제외한다. 원래 조준 대상의 사망은 다른 참가자의 현재/후속 pulse를 취소하지 않는다.
- 주기는 0.1~30초, 수명은 0.1~120초, pulse 예산은 최대 128이며 보안 프로필의 MaximumHitsPerActivation도 넘지 않도록 검사한다. 보스당 동시 장판 수는 해당 Ability의 1~16 설정으로 제한한다. 기존 최종 피해 상한/서버 거리 승인은 공통 피해 경로가 계속 적용한다.
- 장판은 생성 당시 전투 ID·페이즈·source ASC/Controller를 보관한다. wipe/reset/death·phase 변경·시전자 파괴는 즉시 정리하고 Source Controller 변경 delegate와 주기 검증으로 stale damage를 차단한다. 같은 pulse 안의 대상 Controller 교체는 보류 중 타격을 이어받지 못하며 정상 재등록된 새 Controller는 이후 pulse의 새 후보가 될 수 있다.
- `FRPGBossHazardState`는 source Pawn·전투/페이즈·시전 sequence·ActionTag·중심·반경·서버 시작/종료 시각·pulse 수를 Actor UPROPERTY로 복제하도록 연결했다. Actor 수명/상태가 중도 참가의 기준이며 피해 RPC나 클라이언트 timer 판정은 없다. 로컬 표시에는 기존 BossTelegraphPresentation의 ActionTag별 decal material/color를 재사용한다. 해당 style 설정이 없는 native fixture는 시각 decal을 생성하지 않는다. 이 시점에는 Iris 실행 결과가 없다.

새 `ProjectRPG.Boss.Hazard` 소스는 실제 GAS 시전/예고·두 참가자 반복 피해·미등록/아군 제외·범위 이탈/재진입·생성 후 설정 변경의 frozen spec 보존·시전 종료/장판 지속·동시 개수 제한·windup 취소·원래 타깃 사망·wipe/phase/reset 중 타격 중단·Controller 교체/시전자 빙의 해제·사망·수명 종료를 검사하도록 작성했다. 기존 자동화 runner는 이제 Hazard를 포함한 **20종**을 요구한다. 아직 신규 테스트를 실행하지 않았으며 이전 19종 결과를 대체 근거로 사용하지 않는다.

기존 `-RPGBossNetworkTest` fixture에는 별도 테스트 source와 HP 10000인 AI target 두 개를 추가해 기존 플레이어 HP checkpoint를 건드리지 않도록 했다. 예고 0.1초/수명 120초/주기 1초/피해 10 및 예산 128은 시험 전용 값이다. 기존 phase2 barrier 뒤 장판을 생성하고 runner는 실제 장판 시작 marker를 기다린 다음 late client를 연결한다. 양쪽 클라이언트의 활성 상태·주기 수·두 fixture target의 HP 감소·권한 거절·Actor 소멸, 서버 reset 직후 제거와 3초 후 추가 피해 부재를 필수 검사하도록 연결했다. native fixture의 target은 실제 원격 플레이어가 아니며 GPU 연출이나 자연 BT 콘텐츠 연결을 검증하는 시나리오로 간주하지 않는다.

두 PowerShell runner의 AST 구문 검사는 통과했다. 로컬 UE 소스에서 Pawn Controller 변경 delegate와 GE context API를 확인했지만 **새 C++/UHT 빌드·20종·전용/리슨 실행 검증은 대기**다. 현재 사용자 UnrealEditor(PID 8540)와 다른 UnrealEditor-Cmd가 실행 중이므로 빌드/출력물 교체를 하지 않았고 외부 프로세스를 종료하지 않았다. 에디터 종료 후 전체 Editor 빌드 → 20종 자동화 → 기존 보스 상태 runner의 장판 포함 전용/리슨 검증 순서로 이어간다. 돌진·소환 구현은 이번 단계에 포함하지 않는다.

## 54. 공통 지속 장판 실행 검증 완료 (2026-10-14)

53절에서 대기 중이던 장판 실행 검증을 완료했다. `raid-monster-ai`의 서버 GAS 피해·명시적인 복제 상태·중도 참가/리셋 검증 기준을 적용했고 별도 AI 프레임워크, 플레이어 코드, SwordMan 패턴/에셋/밸런스 변경은 없다.

직접 시도한 `Saved/Logs/RaidHazardCompileOnly-20261014.log`의 UBT `-SingleFile -DisableUnity` 빌드는 사용자 AppData 설정 폴더 접근 제한으로 **종료 6**이었다. C++ 오류로 판정하거나 빌드 성공으로 기록하지 않는다. 설치형 엔진은 NonUnity 중간 환경을 기본 환경으로 강제하므로 공유 Makefile 캐시를 이동하지 않았으며, 실행 중인 외부 UnrealEditor-Cmd를 종료하거나 DLL을 교체하지 않았다. 이후 작업 폴더에서 갱신된 DLL과 장판 생성 헤더/컴파일 산출물을 확인하고 해당 DLL로 아래 검증을 수행했다. 이는 실패한 빌드 시도 자체의 성공 증거는 아니다.

- `Saved/Tests/BossState/Automation-705c738e4cda4d7da20aacc2813457d5/Boss.proof.json`: **Hazard 포함 보스 20종 Success, runner 종료 0**. 53절의 시전/독립 장판 유지·반복 피해·범위/참가자 필터·수명·취소·wipe/reset/phase/death·Controller 변경 안전성을 실행 확인했다.
- 전용 서버: `Saved/Tests/BossState/bd5285a40ee440cba7d8fa1bf4b428aa/`, `Run-BossStateNetworkTest.ps1 -SkipSDKSetup -Port 18845 -TimeoutSeconds 240`, **runner 종료 0**.
- 리슨 서버: `Saved/Tests/BossState/a5ebe994d3b74e0dbe80d3a09c4fd4e3/`, 같은 runner에 `-ListenServer -Port 18846`, **runner 종료 0**.

두 네트워크 실행 모두 서버의 `Persistent=1 Targets=2 CastEnded=1` 뒤 late client를 연결했다. 첫/중도 참가 클라이언트 각각에서 활성 장판 상태와 클라이언트 초기화 거절, pulse 2회 이상, 두 fixture 대상 HP 감소, 장판 Actor 제거를 확인했다. 서버는 reset 직후 `Removed=1`과 3초 후 `Targets=2 HealthUnchanged=1`을 확인했다. 기존 보스 상태/Threat·timed strike·예고/HP·reset/restart/death·disconnect BT/GAS 취소 조건도 통과했다.

native proof와 두 네트워크 실행 전후 DLL SHA256은 모두 `B5C3990A5F8FDE6B56BFE90E52590B4CB62C76651202BB4C66FC93AF89476234`로 동일했다. **공통 지속 장판의 native 및 전용/리슨 Iris 기능 검증은 완료**다. 장판 피해 대상은 시험용 AI Pawn 두 개이며 실제 원격 플레이어 두 명의 장판 피해 검증이나 장판 decal의 GPU 표시 확인을 뜻하지 않는다. 콘텐츠용 장판 BP/style/자연 BT 연결, Shipping/지연·손실 검증도 포함하지 않는다. 다음 공통 기능은 **돌진**, 그다음 **소환**이며 아직 구현하지 않았다.

## 55. 공통 돌진 GAS 기능 (2026-10-14 구현, Editor/native 검증 완료·네트워크 대기)

`raid-monster-ai`의 서버 GAS 이동/피해와 명시적인 복제 상태 기준으로 공통 돌진 소스를 구현했다. 기존 플레이어 스킬/이동 코드를 상속하거나 수정하지 않았고 SwordMan Ability grant·Definition·BT·콘텐츠 에셋·공격 수치를 변경하지 않았다. 보스 기본 캐릭터에는 상태만 복제하는 기본 서브오브젝트를 추가했으며, 돌진 Ability를 부여하기 전에는 기존 패턴을 실행한다.

- `URPGBossDashAbility`: 기존 서버 전용 `URPGBossGameplayAbility`를 상속한다. ActionTag·instant damage GE·BaseDamage는 작성자가 설정해야 한다. 예고 종료 후 기존 GAS Root Motion ConstantForce task로 수평 이동하며 캐릭터 이동 모드와 충돌 응답을 변경하지 않는다. 예고 때 고정한 목표 지점을 추적하지 않고 실제 서버 이동 구간을 HitQuery LineSweep/정지 시 Sphere로 판정한다. 기본 캐릭터 충돌이 벽/다른 Pawn을 막으면 돌진도 그 충돌을 따른다.
- 살아 있는 Threat 참가자만 기존 서버 피해 승인 경로로 타격한다. 대상 Controller를 판정 전에 캡처하고 타격 직전에 다시 검사하며, 같은 Pawn은 전체 돌진에서 한 번만 소비한다. 시전별 instant spec·보안 프로필·반경·이동 시간/거리 제한을 보관한다. LineOfSight 검사로 벽 너머 피해를 차단하고 teleport/불연속 위치 변경은 이동 공격 구간으로 인정하지 않는다. 한 구간의 원래 조준 대상이 죽어도 이미 판정한 다른 참가자는 타격할 수 있지만 이후 이동/BT tick은 기존 타깃 유효성 규칙에 따라 취소한다.
- wipe/reset/페이즈 변경/사망·시전자 파괴·시전자/조준 대상 Controller 변경은 기존 취소 규칙과 delegate로 정리한다. 취소 시 자신의 task만 종료하며 다른 Root Motion이나 비활성 이동 모드를 복구/제거하지 않는다. 서버 종료 timer도 이동 task의 최대 수명을 제한한다.
- `URPGBossDashComponent`와 `FRPGBossDashState`: 전투/페이즈·예고 sequence·ActionTag·시작/목표 위치·타격 반경·서버 시작/종료 시각과 active 상태를 UPROPERTY로 복제한다. 실제 위치는 기존 CharacterMovement 복제를 사용한다. 컴포넌트는 ASC·이동 판정·피해·RPC를 소유하지 않으며, native 서버 Begin/End와 읽기용 BP API만 제공한다. 중도 참가자는 현재 상태를 읽을 수 있도록 설계했지만 아직 돌진 네트워크 실행 증거는 없다.

이동 거리는 1~3000cm, 이동 시간은 0.05~5초, 최대 설정 속도는 6000cm/s, 타격 반경은 0~500cm(0 제외)로 제한한다. Walking/NavWalking/Flying 및 복제 이동을 사용하는 Character만 허용하며 비활성 이동이나 이미 존재하는 Root Motion과 겹쳐 시작하지 않는다. 실제 프로젝트 보스용 예고/돌진 시각 에셋은 추가하지 않았다.

`ProjectRPG.Boss.Dash` 테스트는 실제 보스 캐릭터/ASC/Root Motion으로 예고와 고정 목표, 이동·대상별 1회/frozen 피해, 비참가자·아군 제외, 타깃 이동 시 비추적, windup 취소, wipe/reset/phase/death, Controller 손실, 피해 callback 안의 reset, 벽 충돌/벽 뒤 피해 제외, 잘못된 설정 거절을 검사하도록 작성했다. 기존 자동화 runner는 Dash를 포함한 **21종**을 요구한다. runner AST 구문 검사는 통과했다.

공유 빌드/Unreal 실행과 겹쳐 DLL 링크·캐시 이동·외부 프로세스 종료는 하지 않았다. 기존 엔진 컴파일 설정과 생성 헤더를 읽고 `Saved/Build/RaidDashCompile/`의 별도 응답/OBJ/로그로 **DashComponent·DashAbility·DashTests·BossCharacter 4개 C++ 컴파일, 각각 종료 0**을 확인했다. 처음 발견한 부모 멤버 이름 숨김 C4458과 UE 이동 복제 getter 오류는 수정 후 해당 파일만 다시 컴파일했다. `UHT-validation.log`는 기존 manifest에 두 돌진 헤더가 포함된 상태에서 `-Mode=UnrealHeaderTool -NoOutput -NoLog -WarningsAsErrors`로 **Succeeded/종료 0, 생성 파일 쓰기 0**이다. 최초 UHT 시도의 중복 로그 writer 오류는 성공으로 집계하지 않는다. AppData Trace.uba 쓰기 제한 경고는 별도로 남았으며 외부 경로는 변경하지 않았다.

최초 실행 시도는 DLL이 최신 테스트 소스보다 오래된 것을 확인해 시작 전에 거절했다. 이후 외부 Unreal/컴파일/엔진 빌드 프로세스가 종료된 것을 확인하고 전체 Editor 빌드를 수행했다. 공유 캐시는 실행 중에 이동하지 않았으며, 재생성 전에 `Intermediate/Build/Win64/x64/Project_RPGEditor/Development/Makefile.bin.RaidDash-a4fbc5d7de22440c9476ccaa0dee1877.bak`로 보존했다. 외부 프로세스 강제 종료나 사용 중 DLL 교체는 없다.

### 최종 Editor/native 검증 결과 (2026-10-15 정리)

- `Saved/Build/RaidDashCompile/EditorBuild.log`: **4개 작업 Succeeded/종료 0**, 최신 DashTests 컴파일과 모듈 DLL 링크 포함. 기존 plugin 의존성/StructUtils 경고와 AppData Trace.uba 접근 제한은 별도이며 해당 외부 설정이나 다른 기능을 수정하지 않았다.
- `Saved/Tests/BossState/Automation-a71b06d60d8944a5a0392ad54040d10d/Boss.proof.json`: `Run-BossStateAutomationTest.ps1 -SkipSDKSetup -TimeoutSeconds 600`, **Dash 포함 보스 21종 Success/runner 종료 0**. 위에 기술한 실제 서버 이동·고정 목표/비추적·frozen/대상별 1회 피해·참가자 필터·수명/취소·wipe/reset/phase/death·Controller 손실·피해 callback 중 reset·벽 충돌과 피해 차단을 실행 확인했다.

native proof와 테스트 후 현재 DLL SHA256은 `09C5DAF00849F1D01E37EABAD4C764E63B390FAE3876F0ECCFDE567340C9204C`로 동일하다. **돌진 소스 구현과 전체 Editor/native 실행 검증은 완료**했다. **돌진 전용/리슨 Iris 검증은 미완료**이며 54절의 장판 네트워크 결과를 돌진 증거로 재사용하지 않는다. 다음은 기존 네트워크 fixture에 돌진 상태/실제 이동·피해/중도 참가·취소 검증을 연결하는 작업이다. 콘텐츠용 돌진 BP/시각 연출·Shipping/지연·손실 검증과 소환 구현은 아직 시작하지 않았다.

## 56. 돌진 네트워크 검증 연결 (2026-10-15, 컴파일/UHT 통과·Editor 빌드 대기)

`raid-monster-ai`의 서버 권한·현재 상태 복제·실제 중도 참가 기준으로 기존 fixture/runner에 opt-in `-BossDash`를 추가했다. 기본 네트워크 시나리오는 기존 경로를 유지하며, 새 검증은 `-RPGBossNetworkTest -RPGBossNetworkDash`인 non-shipping 테스트에만 동작한다. production 돌진 Ability/Component·플레이어 코드·SwordMan 패턴/에셋/밸런스는 수정하지 않았다.

`RPGBossNetworkDash.cpp`는 기존 테스트 GameMode/StartupTestCharacter의 보조 메서드다. 독립적인 서버 보스 캐릭터와 AI 대상 두 개를 사용하고 기존 플레이어 HP 및 장판 checkpoint와 분리했다. 위치 `(6000,0,1000)`, 목표 `(6350,0,1000)`, Flying/예고 0.1초/돌진 5초/반경 100/피해 10 및 대상 HP 20000은 fixture 전용 값이다. production 이동 모드/충돌 설정을 바꾸지 않는다.

짧은 돌진이 클라이언트 초기 로딩 중 끝나는 것을 피하기 위해 late client의 standalone Entry world를 먼저 로딩한다. runner가 요청 파일을 만들면 서버가 실제 GAS 돌진을 시작하고, active 상태를 확인한 뒤 접속 gate를 기록한다. late client는 그때 기존 ClientTravel로 접속한다. 서버 PostLogin에서도 `ActiveAtLogin=1`을 요구한다. 파일 gate는 해당 실행의 고유 `Saved/Tests` 경로만 허용하며 일반 게임의 이동/피해나 새 RPC/AI 프레임워크를 추가하지 않는다.

양쪽 원격 클라이언트가 활성 상태/고정 목표·권한 거절, 서로 다른 실제 위치 두 개, 두 AI 대상의 정확한 1회 피해를 관측해야 reset을 요청한다. 서버는 자연 종료까지 1.25초 이상 남은 상태에서 reset하고 즉시 시전/상태/속도 정리, 이후 3초간 위치와 HP 불변을 검사한다. 클라이언트는 inactive/Resetting 복제와 자연 종료 시각 전의 정지를 검사하도록 연결했다. 서버 이동/두 대상 1회 타격/활성 중도 참가 marker도 필수다.

기존 runner는 오류를 기다림 도중에도 감지하고 모듈 해시 변경을 거절한다. `-BossDash` 성공 시에만 `BossDash.proof.json`에 해시·실행 모드·로그·정확한 검증 범위를 남긴다. 피해 대상은 AI fixture이며 실제 원격 플레이어의 돌진 피해, 자연 BT 돌진 선택, GPU 연출/Shipping 검증으로 간주하지 않는다.

`Saved/Build/RaidDashNetwork/`의 별도 OBJ/로그로 **NetworkDash·NetworkBossFixture·NetworkStartupFixture 3개 C++ 컴파일 종료 0**, 기존 manifest의 **UHT `-NoOutput -NoLog -WarningsAsErrors` Succeeded/종료 0, 생성 파일 쓰기 0**, runner AST 구문 검사를 확인했다. AppData Trace.uba 쓰기 제한 경고는 별도로 남았으며 외부 경로는 수정하지 않았다. 사용자 UnrealEditor(PID 31984)와 외부 Cmd가 실행 중이므로 공유 DLL 링크/캐시 이동/외부 프로세스 종료는 하지 않았다.

**새 fixture가 포함된 전체 Editor 빌드와 전용/리슨 실행은 아직 미완료**다. 이전 21종 native proof나 장판 네트워크 결과는 이 새 검증의 성공 근거가 아니다. 에디터/외부 실행이 종료된 뒤 최신 DLL을 빌드하고 `Run-BossStateNetworkTest.ps1 -BossDash -SkipSDKSetup -TimeoutSeconds 240 -Port 18847`, 리슨은 `-ListenServer -Port 18848`로 이어간다.

## 57. 공통 돌진 전용/리슨 Iris 실행 검증 완료 (2026-10-15)

56절의 대기 중이던 실행 검증을 완료했다. `raid-monster-ai`의 서버 권한·현재 상태 복제·실제 활성 중도 참가·리셋 취소 기준을 기존 fixture에 그대로 적용했다. 이번 재개에서는 production 코드나 콘텐츠 에셋을 추가 수정하지 않았다. 테스트 판정/수치를 완화하거나 이전 결과로 대체하지 않았다.

에디터와 외부 Unreal/컴파일/엔진 빌드 프로세스가 없는 것을 확인한 뒤 빌드했다. Makefile은 `Intermediate/Build/Win64/x64/Project_RPGEditor/Development/Makefile.bin.RaidDashNetwork-47a4f9f4e07b49df9972daca1b702b84.bak`에 보존했다. `Saved/Build/RaidDashNetwork/EditorBuild.log`는 **10개 작업 Succeeded/종료 0**이며, 증분 빌드에서 기존 Project_RPG 모듈 출력은 재사용했다. 실제 DLL에 active-dash/deferred-join 코드가 포함된 것을 확인한 뒤 실행했다. 기존 플러그인 의존성/StructUtils 경고와 AppData Trace.uba 쓰기 제한은 별도이며 외부 설정은 변경하지 않았다. 사용 중 DLL 교체나 외부 프로세스 강제 종료는 없다.

- 전용: `Saved/Tests/BossState/a46b0de0ee6b4f37b37d2a37b4586006/BossDash.proof.json`, `Run-BossStateNetworkTest.ps1 -BossDash -SkipSDKSetup -TimeoutSeconds 240 -Port 18847`, **runner 종료 0**.
- 리슨: `Saved/Tests/BossState/450bf8edf7f54fdfaf6fb77d3da5ed48/BossDash.proof.json`, 같은 runner에 `-ListenServer -Port 18848`, **runner 종료 0**.

두 실행 모두 서버의 `Active=1 BeforeLateJoin=1` 이후에 두 번째 원격 연결의 `ActiveAtLogin=1`을 확인했다. 전용은 UTC `03:05:23.484` 시작 뒤 `23.685` 로그인, 리슨은 `03:07:18.398` 시작 뒤 `18.666` 로그인이다. 첫/중도 참가 클라이언트 각각에서 고정 목표와 활성 상태·클라이언트 EndDash 권한 거절, 서로 다른 실제 위치 두 개, 두 AI 대상의 HP `20000→19990`을 관측했다. 서버는 두 대상 각각 1회 타격을 확인했다.

양쪽 클라이언트의 이동/피해 관측 후 서버가 자연 종료보다 이른 시점에 reset했고, 즉시 시전/상태 취소·속도 0 및 3초 뒤 위치/HP 불변을 확인했다. 양쪽 클라이언트도 inactive/Resetting 복제와 원래 종료 시각 전 정지를 확인했다. 기존 상태/Threat·timed strike·장판·예고/HP·late join·reset/restart/death·BT/GAS disconnect 취소와 runtime/Iris/Net 오류 검사도 통과했다.

두 network proof, 각 runner 실행 전후, 최종 현재 DLL SHA256은 모두 `289B98FE2091A3DC9CE9853A60C5B6295AFE5B13101BE4692B05DAC6FE337FAD`로 동일했다. **공통 돌진의 전용/리슨 Iris 실행 검증은 위 범위에서 완료**다. 55절의 21종 native 검증은 당시 별도 DLL의 기록이며 이번에는 불필요한 반복 실행 없이 새 네트워크 시나리오만 검증했다.

피해 대상은 AI fixture 두 개다. 실제 원격 플레이어의 돌진 피해, 콘텐츠 BT의 자연 돌진 선택, GPU 연출, Shipping/지연·손실 검증을 완료한 의미는 아니다. 다음 공통 기능은 **소환**이며 아직 구현하지 않았다.

## 58. 공통 소환 GAS 기능 구현 및 native 검증 (2026-10-15)

`raid-monster-ai` 기준으로 서버 GAS 시전과 전투 소유 소환물의 수명을 분리했다. 플레이어/백엔드 코드, SwordMan의 grant·BT·Definition·콘텐츠 에셋·밸런스는 변경하지 않았다.

- `URPGBossSummonAbility`: 기존 서버 전용 Boss Ability를 상속한다. 필수 ActionTag·NPC 클래스·로컬 소환 위치, 예고 시간·수명·초기화 제한 시간·동시 개수 상한을 설정한다. 기본적으로 NavMesh에 투영한 위치만 승인하며, 설정과 위치는 commit 시 고정한다. 예고 종료 후 한 묶음을 넘기고 시전을 끝내므로 BT는 다음 패턴으로 진행할 수 있다. 취소/부분 생성 실패는 해당 묶음을 되돌린다.
- `URPGBossSummonComponent`: 보스 기본 서브오브젝트다. 현재 NPC 참조·전투/페이즈·시전 sequence/slot·초기화 여부·서버 생성/종료 시각을 UPROPERTY로 복제한다. 한 묶음 1~8마리, 총 최대 32마리, 수명 0.1~300초로 제한한다. 초기화 중인 NPC와 남아 있는 시체도 상한에 포함한다. 수명/초기화 제한 만료, wipe/reset/페이즈 변경/사망, 소유자나 멤버의 컨트롤러 변경 시 정리한다.
- 소환된 NPC는 기존 ASC·비동기 startup·AIController/BT 경로를 재사용한다. opt-in 컨트롤러 생성 경로는 Possess 전에 원본 보스 팀을 적용하고 자신이 생성한 AI만 기록한다. 일반 NPC는 기존 `Super::SpawnDefaultController()` 경로를 그대로 사용한다. 소환된 NPC의 사망 아이템 드롭은 서버에서 단방향 차단하며 일반 NPC의 드롭 정책은 유지한다.
- 생성/점유 callback 중 reset되는 경우 초기화 요청과 GAS를 먼저 중단하고 파괴를 다음 tick으로 미룬다. 자신이 만든 AI만 정리하며, 소환물을 인계받은 외부 컨트롤러나 다른 Pawn으로 이전된 AI는 파괴하지 않는다.

사용하려면 이 Ability의 BP에 실제 NPC BP/위치/ActionTag와 필요한 비용·쿨다운을 설정하고 기존 startup grant와 페이즈의 허용 패턴/BT 선택에 연결한다. 현재는 공통 기능까지만 구현했으며 production 보스에 자동 부여하지 않는다. 복제 상태는 현재 소환물 조회용이고 별도 피해 RPC나 클라이언트 소환 승인 경로는 없다.

최초 Editor 빌드는 ASC 조회의 const 타입 오류로 실패했으며 수정했다. `Saved/Build/RaidSummon/EditorBuild-Fixed.log`: **6개 작업 Succeeded/종료 0**, 수정된 소환 컴포넌트·테스트·NPC 컴파일 및 DLL 링크 포함. 최초 UHT는 새 생성 파일 8개를 처리했다. AppData `Trace.uba` 접근 경고는 별도이며 외부 설정을 변경하지 않았다. 외부 Unreal/빌드 프로세스가 없는 것을 확인하고 workspace의 정확한 Makefile만 `.RaidSummon-*.bak`으로 보존한 뒤 빌드했다. 외부 프로세스 강제 종료나 사용 중 DLL 교체는 없다.

`Run-BossStateAutomationTest.ps1 -SkipSDKSetup -TimeoutSeconds 600`: **Summon 포함 보스 22종 Success/runner 종료 0**. proof는 `Saved/Tests/BossState/Automation-c3a03dea0d4f4d2d9bba707295d3718c/Boss.proof.json`이다. 실제 GAS 예고/생성·시전 종료 후 유지·기존 NPC startup/ASC·팀/소유권·고정 설정·클라이언트 변경 거절·개수 상한/시체·드롭 차단·수명·windup 취소·초기화 제한·wipe/reset/페이즈/사망·컨트롤러 교체·컨트롤러 생성 callback 중 reset/누수 방지를 검증했다. runner 실행 전후와 최종 DLL SHA256은 `44FD97DE79A5EF5366A266D929445DC9A712DB53C6BBC422337BF8F7D971FD2B`로 동일하다.

**소환의 전용/리슨 Iris 실행·실제 중도 참가·콘텐츠 NPC의 자연 BT 선택·GPU 연출·Shipping/지연·손실 검증은 아직 하지 않았다.** 이전 장판/돌진 네트워크 proof를 소환 검증으로 대신하지 않는다. 다음 작업은 기존 네트워크 fixture에 소환 시나리오를 연결하여 현재 NPC/메타데이터의 중도 참가 복제와 reset 후 제거를 확인하는 것이다.

## 59. 소환 Iris 네트워크 시나리오 연결 (2026-10-16, 실행 대기)

`raid-monster-ai`의 서버 권한·현재 상태 복제·생성 이후 중도 접속 기준으로 `RPGBossNetworkSummon.cpp`와 기존 runner의 opt-in `-BossSummon`을 연결했다. production 소환 코드·NPC 정책·플레이어/백엔드·보스 콘텐츠 에셋은 변경하지 않았다. 기존 장판/돌진 시나리오는 유지하며 소환 시나리오는 별도 분기로 격리했다. `-BossDash`와 `-BossSummon`은 동시에 사용할 수 없다.

서버는 실제 startup test NPC를 소환하는 GAS Ability를 실행한다. 두 NPC의 기존 startup 완료·소유권·팀·드롭 차단·시전 종료를 확인한 뒤 HP를 각각 80/90으로 설정한다. 먼저 접속한 원격 클라이언트가 있는 상태에서, 미리 로컬 월드를 준비한 두 번째 클라이언트를 소환 완료 gate 이후에 접속시킨다. 서버의 두 번째 PostLogin에서도 기존 NPC 두 마리가 남아 있는지 확인한다. gate 파일은 이전 돌진 검증과 마찬가지로 workspace `Saved/Tests` 안에서만 허용하며 새 게임플레이 RPC는 없다.

양쪽 원격 클라이언트에서 복제된 NPC 참조·서로 다른 slot·전투/페이즈/sequence·서버 생성/종료 시각, 실제 위치, 각 NPC의 ASC avatar와 HP 80/90·최대 HP 100, 클라이언트의 정리/개수 승인 거절을 확인하도록 작성했다. runner가 양쪽 관측을 확인한 다음 서버 reset을 요청한다. 서버는 NPC와 자신이 만든 AI의 제거 및 3초 뒤 재생성 부재를 확인한다. 양쪽 클라이언트도 registry 비움과 관측했던 실제 NPC 두 마리의 파괴가 자연 수명 120초보다 먼저 일어나야 한다. 실행 성공 시에만 DLL hash·전용/리슨 모드·각 로그와 검증 범위를 `BossSummon.proof.json`에 기록한다.

검증: runner PowerShell AST와 변경 파일 diff 공백 검사 통과. 기존 manifest의 `-Mode=UnrealHeaderTool -NoOutput -NoLog -WarningsAsErrors`는 **Succeeded/종료 0, 생성 파일 쓰기 0**이다. 현재 공유 컴파일 설정/PCH를 읽어 workspace `Saved/Build/RaidSummonNetwork/`의 별도 OBJ로 `RPGBossNetworkSummon.cpp`, `RPGBossTestFixture.cpp`, `RPGBossStartupTestFixture.cpp`, `RPGBossNetworkDash.cpp` **4개를 각각 컴파일/종료 0**으로 확인했다. 로그와 응답 파일도 같은 폴더에 있다. AppData `Trace.uba` 경고는 외부 권한 문제이며 변경하지 않았다.

**전체 Editor 빌드/DLL 링크와 전용·리슨 실행은 아직 대기**다. 현재 UnrealEditor(PID 11900)가 실행 중이어서 사용 중 DLL이나 공유 Makefile/생성 헤더를 바꾸지 않았고 외부 프로세스를 종료하지 않았다. 58절의 22종 native 결과는 이전 DLL의 결과이며 이번 변경의 실행 증거로 대신하지 않는다. 에디터 종료 후 전체 Editor 빌드를 수행하고 `Scripts/Network/Run-BossStateNetworkTest.ps1 -BossSummon -SkipSDKSetup -TimeoutSeconds 240 -Port 18849`, 이후 `-ListenServer -Port 18850`으로 실행할 예정이다.

이 검증은 10분 재접속 유예나 원래 파티/던전 복귀 기능을 구현하는 것이 아니다. 콘텐츠 NPC의 자연 BT 선택·GPU 연출·Shipping/지연·손실 검증도 포함하지 않는다.

## 60. 소환 전용/리슨 Iris 실행 검증 완료 (2026-10-16)

59절의 대기 중이던 소환 네트워크 실행 검증을 완료했다. 이후 이미 갱신된 Editor 모듈(DLL 수정 시각 2026-10-16 04:24:48)을 사용했으며, 이번 작업에서 재빌드·DLL 교체·새 테스트 클래스 추가·production 코드/에셋 수정은 하지 않았다. 실행 중이던 외부 에디터/UnrealEditor-Cmd는 건드리지 않았고 runner가 생성한 자기 프로세스만 정리했다. `raid-monster-ai`의 서버 권한·현재 상태 복제·실제 생성 이후 중도 접속 기준을 유지했다.

- 전용: `Scripts/Network/Run-BossStateNetworkTest.ps1 -BossSummon -SkipSDKSetup -TimeoutSeconds 240 -Port 18849`, **runner 종료 0**. proof: `Saved/Tests/BossState/86ec094eff4a483f9a7d197c9a9f08de/BossSummon.proof.json`.
- 리슨: 같은 runner에 `-ListenServer -Port 18850`, **runner 종료 0**. proof: `Saved/Tests/BossState/b5dba8a596ed4c478af60cd89f17d656/BossSummon.proof.json`.

양쪽 실행에서 서버가 두 NPC의 초기화와 GAS 시전 종료를 확인한 뒤 두 번째 원격 클라이언트가 접속했다. 전용의 UTC 로그는 활성 19:49:42.433 → 로그인 42.643, 리슨은 활성 19:50:54.856 → 로그인 55.056이다. 첫/중도 참가 클라이언트 각각에서 두 NPC 참조·소환 메타데이터·실제 위치·ASC avatar·HP 80/90(최대 100), 클라이언트의 소환 관리 변경 권한 거절을 관측했다.

양쪽 관측 후 reset으로 서버의 NPC 두 마리와 소유 AI 두 개를 제거했다. 양쪽 클라이언트도 registry 비움과 실제 NPC 두 마리의 파괴를 자연 수명 120초보다 먼저 확인했다. 서버는 3초 후에도 소환물/소유 AI가 남거나 다시 생성되지 않은 것을 확인했다. runner의 runtime/Iris/Net 오류 검사도 통과했다.

두 proof, 각 실행 전후 및 최종 현재 DLL의 SHA256은 모두 `2A3DEF3F2675EC4700D0BE2B4C374AEED92624F22CC90DD57835CD9D98257E61`로 일치했다. **공통 소환의 전용/리슨 Iris 기능 검증은 위 범위에서 완료**다. 58절의 native 22종은 당시 별도 DLL의 결과이며, 이번에는 불필요한 전체 재실행 없이 대기하던 네트워크 시나리오만 실행했다. 재접속 유예·기존 파티/던전 복귀·실제 콘텐츠 NPC의 자연 BT 선택·GPU·Shipping/지연·손실 검증을 완료했다는 의미는 아니다.

## 61. 새 보스용 공용 BT/Blackboard 템플릿 (2026-10-16)

기존 SwordMan의 BT/GAS 연결은 이미 완료되어 있어 변경하지 않았다. 다음 보스가 재사용할 시작 에셋으로 `/Game/Blueprints/Character/NPC/Raid/Common/BT_RaidBossTemplate`과 `BB_RaidBossTemplate`을 생성했다. `raid-monster-ai` 기준으로 기존 서버 권한/GAS 선택 노드를 재사용했고 새 런타임/테스트 클래스나 별도 AI 프레임워크는 추가하지 않았다. MCP는 현재 연결되지 않아 기존 프로젝트 방식인 Editor Python commandlet을 사용했다.

트리는 root Selector와 서버 상태 Service 아래에 전투/타깃 gate → 추적 → 방향 정렬 → 현재 페이즈 GAS 패턴 실행 → 회복 대기를 둔다. 조건 불충족·사용 가능한 패턴 부재 시 짧은 대기 branch로 돌아간다. gate는 Both abort이며, 실행 Task는 `bUsePhasePatterns=true`, 명시 AbilityClass 없음, timeout 15초다. 고정된 1/2페이즈 branch 없이 보스 Definition의 동결된 현재 페이즈 패턴 목록과 GAS 비용/쿨다운 검사를 사용한다. 기본 접근 반경 180cm·회복 0.5초는 이 템플릿의 출발값이며 실제 보스의 사거리/패턴에 맞게 복제본에서 조정해야 한다.

Blackboard는 `TargetActor`(Pawn), `BossInCombat`(Bool), `BossPhaseIndex`(Int), `BossEnraged`(Bool)를 가지며 엔진이 추가하는 `SelfActor`도 허용한다. 모든 키의 instance sync를 금지해 보스 간 타깃/상태 공유를 막는다. Blackboard는 상태/타깃의 mirror일 뿐 복제나 최종 판단의 기준이 아니다.

`Scripts/Editor/CreateRaidBossBTTemplate.py`는 `-RPGCreateRaidBossBTTemplate`일 때 두 에셋이 모두 없는 경우에만 새로 만든다. 이미 존재하면 저장하지 않고 검증하며, 한쪽만 존재하면 자동 복구/덮어쓰기를 거절한다. 기존 보스 BP·AIC·startup·Definition·Ability는 변경하지 않는다. 실제 사용 순서는 다음과 같다.

1. 두 템플릿을 새 보스 폴더로 복제하고, 복제 BT의 Blackboard 참조를 복제 BB로 지정한다.
2. 해당 보스 AIController의 `BTAsset`에 복제 BT를 지정하고, 보스 BP의 AIController/기존 NPC startup 데이터를 연결한다.
3. 보스 `BossAIComponent.Definition`의 페이즈별 `AbilityPatterns`를 작성하고, 같은 Ability를 해당 NPC startup에서 부여한다. BT는 Ability를 자동 부여하지 않는다.
4. 보스별 추적 반경·회복 시간·시전 timeout과 필요한 추가 branch를 조정하고 실제 플레이로 검증한다. 공용 원본은 직접 연결/편집하지 않는 시작 템플릿이다.

검증: 생성 commandlet `Saved/Tests/BossBTTemplate/bbc992b76daa46cca332d63a51866585/create.log` **종료 0**. 별도 프로세스의 apply 없는 재로드/구조 검증 `Saved/Tests/BossBTTemplate/526a2257ca504fc4a889bd2bda7edb04/verify.log`도 **종료 0**이다. 기존 SwordMan BT/BB/Definition/startup/BP/AIC 6개와 DLL은 생성 전후 hash가 같았고, 재로드 시에는 새 템플릿까지 9개 파일 모두 hash 불변이었다. 초기 Python 호출 오류(BaseClass native 속성명, BlackboardEntry 초기화)와 엔진 PostLoad의 SelfActor 추가에 대한 검증을 수정한 뒤 통과했다. 실패한 시도는 성공 증거로 집계하지 않았다.

최종 BT SHA256 `D8AC665962A0AF8F38BD8088ED4DFDEFDB69FEB2B9A78180A0BA726F756C931A`, BB SHA256 `092DB56508741DB6063D64508165ECB2DF66C96B3970C329CF4B94D39C8C3C64`. 에디터/DLL을 닫거나 재빌드하지 않았으며, 외부 에디터는 그대로 두고 자신이 만든 commandlet만 정리했다. **이번 범위는 저장된 공용 에셋 생성/재로드 검증이다.** 새 템플릿을 실제 보스에 적용한 전투·에디터 그래프 시각 확인·멀티플레이 실행 검증까지 완료했다는 의미는 아니다.

## 62. 공용 BT를 사용하는 SwordMan 별도 복제본 연결 (2026-10-16)

권장했던 기존 SwordMan 별도 복제본으로 진행했다. 원본이나 전투 맵을 변경하지 않고 `/Game/Blueprints/Character/NPC/Raid/SwordManVariant`에 다음 6개 에셋을 연결했다.

- `BP_RaidSwordManVariant` → 전용 `AIC_RaidSwordManVariant`, `DA_RaidSwordManVariantStartUp`, BossAIComponent의 `DA_RaidSwordManVariant`.
- `AIC_RaidSwordManVariant.BTAsset` → `BT_RaidSwordManVariant` → `BB_RaidSwordManVariant`.

BT/BB는 61절의 공용 템플릿 복제본으로, 한 실행 Task가 동결된 현재 페이즈의 GAS 패턴을 선택한다. 보스 BP·AIController·startup·Definition은 기존 SwordMan에서 분리 복제했다. 시작 시 부여/반응/즉시 실행 Ability와 GameplayEffect, 페이즈 태그/HP 임계치/패턴 우선순위, 광폭화 시간/배율은 원본과 일치하도록 검증했다. 공격 Ability와 연출 에셋 자체는 공유 재사용하며 새로 만들거나 수정하지 않았다. 추적/회복/timeout은 공용 템플릿의 시작 설정이므로 원본 BT의 모든 행동 타이밍까지 동일하다는 의미는 아니다.

`raid-monster-ai`에 따라 기존 서버 GAS/BT/컴포넌트를 재사용했고 새 C++·테스트 클래스는 추가하지 않았다. `Scripts/Editor/CreateRaidSwordManVariant.py`는 `-RPGCreateRaidSwordManVariant`일 때 6개가 모두 없는 경우에만 생성한다. 존재하면 읽기 전용으로 참조/설정을 검증하고, 일부만 존재하면 덮어쓰기/자동 복구를 거절한다. 기존 템플릿의 구조 검증 함수를 재사용하며, 각 페이즈 패턴이 실제 startup 부여 목록에 있는지와 BossAIComponent가 복제 BP 자신의 CDO 소유인지도 확인한다.

검증: `Saved/Tests/BossVariant/11b7766df6734881860a18aefe8a8e2e/create.log`와 같은 폴더의 `verify.log` 모두 **commandlet 종료 0**. 생성 시 두 BP를 컴파일했고, 별도 프로세스에서 apply 없이 6개 에셋을 다시 불러와 모든 연결을 확인했다. 원본 SwordMan 6개·기존 공격 BP 2개·공용 템플릿 2개·DLL의 총 11개 hash가 생성/재로드 전후 같았고, 읽기 전용 재로드에서는 복제본 6개까지 총 17개 hash 불변을 확인했다. 최초 생성 시도의 PowerShell 보호 경로 배열 구성 및 GameplayTag Python 객체 비교 오류는 수정했으며, 실패한 시도는 성공으로 집계하지 않았다. GameplayTag는 반사 객체 동일성이 아니라 실제 TagName 값으로 비교한다.

기존 맵·GameMode·원본 보스 BP의 BossClass 선택은 바꾸지 않았다. 자동 전투 시작 정책도 원본과 같으므로 새 BP를 단순 배치했다고 전투 시작이 보장되는 것은 아니다. 실제 사용 시 전투 소유자가 이 복제 BP를 스폰하고 기존 startup 완료/StartEncounter 경로를 사용해야 한다. **이번 완료 범위는 독립 보스 에셋 연결·BP 컴파일·저장/재로드 검증**이며, 복제본의 실제 BT 추적/패턴 피해·페이즈 전환·리셋 및 멀티플레이 실행은 다음 단계다.

## 63. 복제본의 최소 실제 전투·리셋 검증 완료 (2026-10-16)

필요한 동작 확인만 진행했다. 새 테스트 클래스·보스 에셋·맵은 추가하지 않고, 기존 `RPGBossMapE2EProbeComponent.cpp`, `RPGBossCombatPhaseProbe.cpp`와 보스맵 runner를 재사용했다. opt-in `-SwordManVariant`는 실행 중인 GameMode 인스턴스의 BossClass만 복제본으로 바꾼다. 실제 스폰 클래스와 실행 중인 root BT가 복제본인지 검사하며, 저장된 GameMode/맵/보스 설정이나 플레이어 코드는 변경하지 않는다. `raid-monster-ai`의 서버 GAS/BT·권한·취소 기준을 유지했다.

최종 실행: `Scripts/Network/Run-BossMapE2ETest.ps1 -SwordManVariant -SkipSDKSetup -TimeoutSeconds 300 -Port 18852`, **전용 서버 + 원격 클라이언트 2개, runner 종료 0**. proof는 `Saved/Tests/BossMap/50b5035f443442dba47cae9db7851f5c/BossMap.proof.json`이다.

- 실제 복제 BP의 감지/위협도 → NavMesh/BT 추적 → 기본 Strike GAS 피해를 확인했다. 첫 공격 시 이동 거리는 약 583cm였다. 수동 Ability 활성화나 플레이어 피해 주입으로 공격 성공을 대신하지 않았다.
- 서버 시험용 GE로 보스 HP를 300→207(69%)로 바꿔 페이즈 전환을 자극했다. 이전 Strike 예고/Ability 취소·이전 타격 창의 추가 피해 부재·새 페이즈의 자연 HeavyStrike/GAS 피해 및 두 Iris 클라이언트의 페이즈/예고/HP/피해 관측을 확인했다.
- 그다음 실제 BT 공격 예고 도중 `MarkWiped()` → `BeginReset()`을 실행했다. BT/이동/어그로/보스 Ability가 중단되고 원래 타격 창을 넘는 최소 3초간 위치와 참가자 HP가 유지되는지 확인했다. 보스 HP를 복원하고 `CompleteReset()` → `StartEncounter()` 후 새 전투 ID와 1페이즈 복귀도 확인했다.

리셋을 포함한 최종 proof, 실행 전후 및 최종 DLL SHA256은 `F0379D6E72C5E323692E995796C88BA1085FA4AD6CE0040EE35128FED4ADB881`로 일치한다. 복제본 6개 에셋 hash도 실행 전후/최종 파일과 일치한다. 첫 실행 `Saved/Tests/BossMap/4fae80b299a44f72aa3b314602dde366/`은 추적/공격/페이즈까지만 통과했고 기존 시나리오에 리셋이 없어, 그 부분만 같은 경로에 추가한 뒤 최종 실행했다. 첫 결과를 리셋 증거로 대신하지 않는다.

`Saved/Build/RaidVariantSmoke/EditorBuild.log`는 69개 작업, `EditorBuild-Reset.log`는 67개 작업으로 각각 **Succeeded/종료 0**이다. 공유 프로젝트의 추가 컴파일도 포함됐지만 관련 없는 소스는 수정하지 않았다. AppData `Trace.uba` 접근 경고와 기존 플러그인 경고는 별도다. 에디터/빌드 프로세스가 없는 상태에서 빌드했고 Makefile만 정확한 workspace 경로의 `.RaidVariantSmoke-*.bak`/`.RaidVariantReset-*.bak`에 보존했다. runner가 생성한 프로세스만 정리했다.

**공용 BT를 실제 보스에 연결한 최소 기능 검증은 위 범위에서 완료**다. 리셋 검증은 같은 보스의 BossAI 수명주기이며 GameMode의 패배 UI/새 Pawn 생성 재시도 전체를 다시 검증한 것은 아니다. 리셋 후 클라이언트 상태의 별도 ACK, 리슨 반복, 전체 native 자동화 재실행, GPU/Shipping/지연·손실 검증은 추가하지 않았다. 다음은 검증 구조 확장이 아니라 실제 보스 패턴 제작이다.

## 64. SwordMan 복제본 2페이즈 장판 패턴 연결 (2026-10-17)

`raid-monster-ai`에 따라 검증 프레임워크나 C++ 클래스를 늘리지 않고 기존 서버 전용 `URPGBossPersistentHazardAbility`를 콘텐츠로 연결했다. 복제본 폴더에 `GameplayAbility/GA_RaidSwordManVariant_GroundHazard` BP 하나를 생성하고, 복제본 startup의 부여 목록·Definition의 2페이즈 패턴·보스 BP의 예고 스타일만 수정했다. 원본 SwordMan, 공용 BT/BB, 복제본 BT/BB/AIC, 맵, GameMode, 플레이어 코드와 DLL은 변경하지 않았다.

- 1페이즈는 기존 그대로다. 2페이즈는 **장판 → 기존 HeavyStrike → 기존 Strike** 우선순위이며 GAS 쿨다운/자격 검사를 통과하는 첫 패턴을 사용한다.
- 장판은 시전 시점의 대상 위치에 고정된다. 초안 수치는 예고 1.5초, 반경 250cm, 유지 5초, pulse 간격 1초, pulse당 기본 피해 12, 독립 쿨다운 12초, 동시 장판 최대 1개다. 실제 피해에는 기존 GAS 피해 계산이 적용되며 최종 밸런스 확정값이 아니다.
- 예고 종료 후 장판 Actor에 수명을 넘기고 시전은 종료한다. BT는 다른 공격을 계속할 수 있다. 서버 피해·Iris 현재 상태·페이즈/wipe/reset/사망 정리는 기존 공통 기능을 재사용한다. 전용 Action/Cooldown 태그 두 개와 기존 decal 재질을 사용하며 새 시각/애니메이션 에셋은 만들지 않았다.

`Scripts/Editor/ConfigureRaidSwordManVariantHazard.py`는 기본 읽기 전용, `-RPGApplySwordManVariantHazard`일 때만 해당 4개 에셋을 생성/변경한다. 기존 커스텀 페이즈/부여/장판 수치를 덮어쓰지 않는다. 초기 복제 스크립트 `CreateRaidSwordManVariant.py`의 후속 검증도 이 명시적 확장만 허용하도록 갱신했다.

검증 경로: `Saved/Tests/BossVariantHazard/3dba3ff5362b4fae9a4f5c04fb1d8361/`. 변경 전 복제본 BP·Definition·startup 3개는 `Backup/`에 복사했다. `apply.log`는 BP 컴파일·저장 후 `RAID_SWORDMAN_VARIANT_HAZARD COMPLETE ... Applied=1` 및 commandlet `result 0`을 기록했다. 별도 읽기 전용 재로드는 **프로세스 종료 0**, `verify.log`에 같은 완료 marker와 `Applied=0`을 기록했다. 보호 대상 14개 hash가 적용 전후 같고, 재로드 전후에는 보호 대상과 복제본 7개를 합친 21개 검사 항목 모두 불변이다.

**이번 완료 범위는 실제 패턴 BP 작성·startup/페이즈/예고 연결·컴파일·저장/재로드 확인**이다. 공통 장판의 이전 멀티플레이 검증과 새 SwordMan 장판의 자연 BT 실행 검증은 구분한다. 이 새 콘텐츠의 실제 전투/회피 난이도·GPU 표시·멀티플레이 실행은 아직 확인하지 않았으며 63절의 과거 proof를 새 패턴의 검증 결과로 사용하지 않는다. 불필요한 전체 native/전용·리슨 반복이나 새 테스트 클래스는 추가하지 않았다.

## 65. 복제본 장판의 기존 전투 검증 연결 — 실행 대기 (2026-10-17)

`raid-monster-ai`에 따라 기존 `RPGBossCombatPhaseProbe.cpp`와 probe 헤더, `Run-BossMapE2ETest.ps1 -SwordManVariant`만 확장했다. 새 테스트 클래스/맵/생산 코드는 추가하지 않았다. 자연 BT 예고 → 장판 Actor로의 시전 인계 → 실제 해당 BP/장판 SourceObject를 가진 GAS 피해 → 두 클라이언트의 동일 sequence 상태/indicator 관측 → 살아 있는 장판을 포함한 reset 제거를 요구한다. 기존 확인 RPC의 checkpoint 하나를 재사용하고 runner는 장판 BP까지 복제본 7개 에셋 hash와 성공 marker를 요구한다. 수동 보스 Ability 활성화나 참가자 피해 주입은 추가하지 않았다.

runner PowerShell AST와 공백 검사는 통과했고 `Saved/Build/RaidVariantHazard/EditorBuild.log`에서 수정된 C++ 및 관련 의존성 컴파일은 통과했다. 그러나 **전체 빌드 종료 6 / 링크 실패**다. 이번에 수정하지 않은 `Source/Project_RPG/Private/GameMode/RPGGameModeBase.cpp:511`의 `UNetConnection::GNetConnectionBeingCleanedUp` 참조가 LNK2001/LNK1120을 발생시킨다. 설치 엔진 `NetConnection.h:1003`의 해당 static 멤버는 `ENGINE_API` 없이 선언되어 있다. 기존 dirty GameMode/엔진 파일을 수정하거나 이전 소스로 교체해 우회하지 않았다. 실패한 링크 후 `Binaries/Win64/UnrealEditor-Project_RPG.dll`은 존재하지 않음을 확인했다.

에디터/외부 빌드가 없는 상태에서 실행했으며 정확한 Makefile만 `.RaidVariantHazard-1e10045b23d644d5a2c167a4f595987f.bak`으로 보존했다. AppData Trace.uba 접근 경고는 링크 실패와 별도다. **전용 서버 실행은 시작하지 않았고 새 성공 proof도 없다.** NPC 외 GameMode의 빌드 문제를 해결한 뒤 같은 runner를 실행해야 한다. 현 단계는 검증 코드 연결/컴파일까지만 완료이며 실제 장판 전투 검증 완료가 아니다.

## 66. 설치 엔진 링크 복구 및 복제본 장판 전투 검증 완료 (2026-10-17)

승인받은 링크 오류를 수정했다. export되지 않는 엔진 static 변수 대신 `ARPGPlayerController::OnNetCleanup`의 실제 연결을 콜백 범위에서만 기록하고 GameMode의 기존 연결/신원/세대 검사를 유지한다. 엔진·플레이어 스킬·보스 콘텐츠는 변경하지 않았다. 관련 온라인 스킬의 종료 계약과 보스 스킬의 서버 GAS/Iris 기준을 함께 적용했고 기존 테스트만 보완했다.

`Saved/Build/RaidCleanupFix/EditorBuild.log`의 65개 작업과 최종 `EditorBuild-Assertion-Retry.log`의 363개 작업은 각각 **Succeeded/종료 0**이다. 최초 native Lifecycle의 정리 후 weak 포인터 비교는 객체 식별자 비교로 보완했다. 캐시 재사용 권한 실패와 최초 native 비교 실패 로그는 별도 보존하며 성공 증거로 집계하지 않는다. 다른 작업의 컴파일이 진행 중일 때 캐시를 옮기거나 프로세스를 종료하지 않았다.

- 최종 DLL의 접속 종료 3종: `Saved/Tests/DungeonCleanupFix/33ae2433eb134cd4b04c42e2b36b6919/Cleanup.proof.json`, 모두 Success. 실제 정리 콜백 범위/복구 및 종료 분류의 소유권·세대·계획된 종료 검사를 확인했다. HTTP/실제 온라인 재접속 E2E와는 구분한다.
- `Run-BossMapE2ETest.ps1 -SwordManVariant -SkipSDKSetup -TimeoutSeconds 420 -Port 18853`: **전용 서버 + 실제 원격 클라이언트 2개, runner 종료 0**. `Saved/Tests/BossMap/9bc4e8c83c6541b4af123baa21e3a7d5/BossMap.proof.json`의 `swordManVariantNaturalHazardVerified`와 reset/restart 결과가 true다.
- 자연 BT의 장판 예고 sequence 3 → 실제 콘텐츠 BP/장판 SourceObject의 GAS 피해(참가자 HP 269→251) → 두 클라이언트의 동일 장판 sequence/반경/수명/pulse 상태 및 indicator 관측 → 기존 강공격 병행 → 살아 있는 장판을 포함한 reset 제거·3초 무추가 피해·새 전투 1페이즈 재시작을 통과했다. 페이즈 임계치 자극은 기존 서버 시험용 GE이며 보스 Ability 수동 실행이나 참가자 피해 주입은 아니다.

두 proof와 최종 DLL SHA256은 `8C1E2799AB9A6A3AB573804D2FA8D65226AD1983042C683A4FC2FACBA622B631`로 일치한다. 선택 소스 10개 hash 불변과 복제본 7개 에셋의 실행 전후 hash도 확인했다. **새 SwordMan 장판의 서버 전투/두 Iris 클라이언트 복제/reset 검증은 완료**다. headless indicator 검사는 GPU 외형 검수가 아니며, 장판 도중의 새 접속·리슨 반복·전체 native 회귀·실제 회피 입력/밸런스 확정·Steam/Shipping/손실 환경 검증은 추가하지 않았다. 검증 프로세스는 각 runner가 소유한 것만 정리했다.

## 67. SwordMan 복제본 돌진 콘텐츠·중거리 BT 연결 (2026-10-17)

`raid-monster-ai`의 기존 서버 GAS/이동/Iris 계약을 재사용했다. C++·엔진·플레이어 코드·새 테스트 클래스는 추가/수정하지 않았다. 복제본에 `GameplayAbility/GA_RaidSwordManVariant_Dash` BP 하나를 만들고 startup·Definition·보스 예고 스타일·복제 BT만 변경했다. 원본 SwordMan과 공용 BT/BB는 그대로다.

- 돌진 초안: 예고 1.2초, 최대 거리 800cm, 이동 0.6초, 타격 반경 120cm, 기본 피해 25, 독립 GAS 쿨다운 14초. 실제 피해에는 기존 계산이 적용된다. 시전 시 고정한 목표 지점·한 대상당 한 번의 서버 타격·충돌·취소는 기존 돌진 구현을 사용한다.
- 복제 BT의 기존 추적 앞에 **2페이즈이고 수평 거리 400cm 초과~800cm 이하일 때 방향 정렬 → 돌진**을 시도하는 분기를 넣었다. 조건 불충족이나 GAS 거절/쿨다운이면 기존 180cm 추적으로 돌아간다. 거리 gate는 진입 검사만 하며, 돌진 도중 400cm를 통과했다고 스스로 중단하지 않는다. 전투/타깃 gate와 GAS의 reset/페이즈 취소 규칙은 유지한다.
- 기존 1페이즈는 그대로이고, 추적 후 2페이즈의 우선순위는 장판 → 강공격 → 돌진 → 기본 공격이다. 중거리 진입 분기와 이 근접 보조 돌진은 동일 Ability/쿨다운을 공유한다. 기존 decal을 고정 목표 지점에 연결했으며 전용 이동 몽타주·경로형 예고/VFX는 제작하지 않았다.

`ConfigureRaidSwordManVariantDash.py`는 `-RPGApplySwordManVariantDash`일 때만 적용하고 기본 실행은 읽기 전용이다. 기존 생성/장판 스크립트도 이 명시적 확장을 검증·보존하도록 보완했다. 공용 템플릿 스크립트의 추가 검증은 복제본에만 허용하며 공용 에셋을 바꾸지 않는다. 기존 네트워크 runner는 새 돌진 BP를 포함해 복제본 8개 에셋의 hash를 기록한다.

증거: `Saved/Tests/BossVariantDash/f97f9bdbfe584c6f919eabce49c00d3b/`의 `Backup/`에 변경 전 4개 에셋을 보존했다. `apply-checked.log`와 별도 읽기 전용 `verify.log`는 모두 commandlet **종료 0**이고 적용/재로드 완료 marker가 있다. 재로드 시 기존 생성·장판 계약도 함께 통과했다. Python 비편집 속성/enum 반사 처리 오류는 수정했으며 실패한 시도의 로그도 같은 폴더에 보존했다. 보호 대상 14개(원본/공용·변경하지 않은 복제 에셋·DLL) 및 복제본 8개 hash 불변을 확인했고 AST 4개·runner 구문/공백 검사도 통과했다.

기존 2인 흐름 회귀: `Run-BossMapE2ETest.ps1 -SwordManVariant -SkipSDKSetup -TimeoutSeconds 420 -Port 18854`, **전용 서버 + 원격 클라이언트 2개, 종료 0**. proof는 `Saved/Tests/BossMap/5c17e7417be14dbcb3dbdecff0d9bb78/BossMap.proof.json`, DLL SHA256은 `95D55E1FAAF59FC0CC9286A9382D5D46E490298B03D607259E743BFA0066DBFE`로 현재 파일과 일치한다. 외부 빌드/검증이 끝난 후 적용했고 이 작업에서는 DLL을 재빌드하거나 교체하지 않았다.

**완료 범위는 돌진 BP 작성·부여/페이즈/예고/BT 연결·저장/재로드 및 기존 추적/공격/장판/페이즈/reset 흐름의 회귀 확인**이다. 위 runner는 새 중거리 돌진의 실제 이동·적중·회피를 판정하지 않으므로 해당 성공 증거로 사용하지 않는다. 새 콘텐츠의 중거리 자연 돌진 실행/클라이언트 이동 관측과 GPU 연출·밸런스 확인은 아직 남는다. 이전 공통 돌진의 네트워크 proof와도 구분한다.

## 68. SwordMan 복제본 2페이즈 Guardian 소환 연결 (2026-10-18)

`raid-monster-ai`에 따라 기존 서버 전용 `URPGBossSummonAbility`와 보스 소환 관리 컴포넌트를 재사용했다. 복제본의 `GameplayAbility/GA_RaidSwordManVariant_Summon` BP를 만들고 startup 부여 목록·2페이즈 패턴·보스 예고 스타일에 연결했다. 기존 Guardian NPC(`Gruntling/Guardian/BP_Gruntling_Guardian`)의 GAS 시작 데이터와 AIController/BT/BB를 그대로 사용하며 원본 NPC는 수정하지 않았다. C++·플레이어 코드·새 테스트 클래스·추가 BT 분기는 작성하지 않았다.

- 1페이즈는 유지하고, 추적 후 2페이즈 우선순위는 소환 → 장판 → 강공격 → 돌진 → 기본 공격이다. 기존 중거리 돌진 진입 분기는 유지한다.
- 초안: 예고 2초, Guardian 2마리, 유지 25초, 시작 완료 제한 10초, 독립 GAS 쿨다운 30초, 소유 중인 소환수 최대 2마리다. 보스 로컬 위치 `(-250,-300,0)`, `(-250,300,0)`을 NavMesh에 투영하며 유효한 소환 공간과 수용량이 있어야 실행한다.
- 보스 중심의 파란 decal(반경 450cm)은 피해 없는 시전 표시이며 소환 위치/타격 범위가 아니다. 소환 후 시전은 끝나고 보스는 다른 패턴을 계속한다. 소환수 수명·동시 수 제한·드롭 억제·페이즈/전멸/reset/사망 정리는 기존 서버 구현을 사용한다.

`Scripts/Editor/ConfigureRaidSwordManVariantSummon.py`는 기본 읽기 전용이며 `-RPGApplySwordManVariantSummon`일 때만 적용한다. 기존 생성·장판·돌진 스크립트도 이 명시적 확장을 보존하도록 보완했다. 기존 네트워크 runner의 에셋 지문 목록에는 새 소환 BP만 추가했다. 기능 구현에 집중하기 위해 새 테스트나 네트워크 실행은 추가하지 않았다.

증거: `Saved/Tests/BossVariantSummon/2281b24607b44a82af22f67d49838f64/`. 변경 전 보스 BP·Definition·startup 3개는 `Backup/`에 보존했다. `apply.log`와 별도 프로세스의 읽기 전용 `verify.log` 모두 **종료 0**, 소환 완료 marker의 `Applied=1`/`Applied=0`을 확인했다. BP 컴파일·부모 클래스·소환 NPC의 GAS/AI 참조·부여/패턴/예고 설정과 기존 생성·장판·돌진 계약이 통과했다. 보호 대상 25개(원본/공용/미변경 복제 에셋·Guardian 관련 에셋·DLL)는 적용/재로드 전후 불변이며, 복제본 9개도 읽기 전용 재로드 전후 hash가 같다. 관련 Python 4개와 runner AST도 통과했다. DLL은 재빌드하지 않았다.

**이번 완료 범위는 실제 소환 콘텐츠 작성·GAS/페이즈/예고 연결·저장/재로드 확인**이다. 이 Guardian 조합의 실제 소환·추적/공격·클라이언트 복제·소환수가 남은 상태의 reset 검증은 아직 수행하지 않았다. 과거 공통 소환 테스트나 장판 proof를 새 콘텐츠의 실행 증거로 대체하지 않는다. 원본 보스·맵·GameMode의 보스 선택은 그대로다.

## 69. 보스 소환수의 서버 타깃 재선택 (2026-10-18, 소스 작성·빌드 대기)

일반 NPC의 기존 감지 경로는 `TargetActor`가 비어 있을 때만 최초 타깃을 기록한다. 보스전 도중 소환된 Guardian이 감지 콜백을 놓치거나 최초 타깃이 사망/이탈하면 새 참가자를 선택한다는 보장이 없어, `ARPGAIController`에 **보스 소환수에 한정한** 타깃 갱신을 추가했다.

- 서버에서 해당 NPC의 `GetEncounterSummonController()`가 자신인지 확인하고, 자체 보스 Threat 컴포넌트를 가진 Pawn은 제외한다. 일반 NPC 감지와 보스 자체의 단일 어그로 작성자는 유지한다.
- 소환수의 GAS 시작 준비·생존·소유자/Instigator·팀을 검사한다. 현재 타깃이 보스 Threat 장부의 유효한 참가자이면 유지하고, 그렇지 않으면 장부의 현재 유효 타깃을 사용한다. 타깃 검증·사망·던전 참가 자격·거리·Controller 동일성은 기존 Threat API를 재사용한다.
- 유효한 타깃이 없거나 소환수가 사망/정리되면 Blackboard를 비우고, 타깃 교체 시 이전 이동과 전투 focus를 해제한다. MoveTo 중단 콜백 이후 Pawn/Blackboard 소유권도 재확인한다. 새 RPC·복제 필드·BT·타깃 장부는 만들지 않았다. 감지 콜백과 서버 Tick 모두 같은 갱신 경로를 사용한다.

기존 `RPGBossSummonTests.cpp`의 소환 테스트에 일반 NPC 하나를 사용한 초기 타깃 획득·유효 타깃 유지·타깃 빙의 해제/사망·참가자 없음·재획득·소환수 사망·소유권 상실·정리 검사를 추가했다. 새 테스트 클래스나 운영 BP/에셋은 추가/수정하지 않았다.

구문 공백 검사는 통과했다. 빌드 기록은 `Saved/Build/SummonTarget-dc12c003345345c8adae489b115854cb/`다. 다른 작업의 UBT가 잠금을 점유하고 있어 `-WaitMutex`에서 대기했으며 **이번 컴파일은 시작되지 않았다**. 불필요한 대기를 지속하지 않도록 이번 작업이 생성한 대기 UBT(PID 26924)만 경로/시작 시각/로그를 확인한 뒤 종료했고, 기존 빌드(PID 15556)는 건드리지 않았다. 따라서 **이번 C++ 변경의 컴파일·추가 검사 실행·실제 Guardian 멀티플레이 동작은 미검증**이다. 다음 단계는 외부 빌드 종료 후 증분 Editor 빌드와 기존 `ProjectRPG.Boss.Summon` 검사이며, 과거 소환 통과 결과를 이번 변경의 증거로 사용하지 않는다.

## 70. 서버 도발·시간 제한 타깃 고정 API (2026-10-18, 소스 구현)

기능 구현을 우선한다는 요청에 따라 계획의 미구현 항목이던 도발/타깃 고정을 기존 `URPGBossThreatComponent`에 추가했다. `raid-monster-ai`의 서버 권위·기존 GAS 연계 기준을 유지하며, 플레이어 스킬/BT/에셋은 수정하지 않았다.

- 서버 GAS 또는 보스 기믹에서 `ForceTarget(Target, DurationSeconds, ExpectedEncounterId)`를 호출한다. 허용 기간은 0.01~120초이며 성공 시 양수 토큰, 실패 시 0을 반환한다. 나중에 승인된 요청이 이전 고정을 교체한다. 실제 스킬의 거리/LOS·Commit·쿨다운 검사는 호출하는 서버 Ability의 책임이고, 클라이언트 RPC는 제공하지 않는다.
- `ReleaseForcedTarget(ExpectedEncounterId, ExpectedLockToken)`은 일치하는 고정만 해제한다. 토큰은 전투 리셋 때 재사용하지 않아 이전 Ability의 종료 콜백이나 타이머가 새 고정을 풀 수 없다. `GetForcedTarget()`은 현재 유효한 서버 고정 대상을 조회한다.
- 고정 중에도 정상 위협도를 누적한다. 새 참가자에게는 최소 위협도 1만 부여하며 최상위로 만드는 영구 가산은 하지 않는다. 기간 만료/해제 후에는 누적 위협도로 다시 선택하고, 이전 고정 대상에 일반 타깃 전환 완충값을 적용해 억지로 남겨두지 않는다.
- 전투/페이즈·보스/대상 Controller·생존/참가 자격/거리·위협도 등록을 재확인한다. 대상 무효화·페이즈 변경·전멸/reset/사망·보스 빙의 해제 시 고정을 정리한다. 서버 타이머 만료 전이라도 조회 시 만료/무효 대상을 반환하지 않는다.
- 이미 시작된 공격의 고정 타깃을 새 대상으로 덮어쓰지 않는다. 현재 타깃이 바뀌면 기존 `IsBossExecutionValid()`와 BT Task의 취소 규칙을 따른다. 타깃 선택의 기준은 서버 Threat 컴포넌트이며 Blackboard는 여전히 결과의 mirror다. 클라이언트용 도발 UI/상태 복제나 실제 플레이어 도발 Ability 연결은 이번 범위가 아니다.

새 테스트 클래스·네트워크 시나리오·검증 도구는 추가하지 않았다. 변경 파일 공백 검사만 수행했으며 **69절과 이번 C++ 변경의 컴파일·실행 확인은 기능 묶음 단위로 후속 진행할 미검증 항목**으로 유지한다. 이번 턴에는 빌드 잠금 대기나 반복 검증을 시작하지 않았다.

## 71. 기믹 완료 등 서버 이벤트에 의한 페이즈 전환 (2026-10-18, 소스 구현)

`raid-monster-ai`의 데이터 기반 페이즈/서버 권위 원칙에 따라 기존 Definition과 BossAIComponent를 확장했다. `FRPGBossPhaseDefinition.bRequiresExplicitTransition`을 켠 페이즈 앞에서는 HP 자동 전환을 멈춘다. 기본값은 false라 기존 보스는 변경되지 않으며, 최초 페이즈에 설정하는 것은 금지한다. HP 임계치는 기존처럼 내림차순으로 작성하되, 명시적 진입 자체에는 HP 조건을 요구하지 않는다.

서버 기믹/GAS/BP는 시작 시 캡처한 전투 ID·현재 페이즈 태그와 다음 페이즈 태그로 `AdvanceToPhase(ExpectedEncounterId, ExpectedCurrentPhase, NextPhase)`를 호출한다. 살아 있는 전투 중 보스에서 바로 다음의 명시적 진입 페이즈로만 진행한다. 다른 전투/페이즈의 지연 콜백·중복 완료·후퇴/중간 페이즈 건너뛰기는 거절한다. 진입 후에는 HP를 재평가하므로 이미 조건을 만족한 후속 HP 전용 페이즈로 추가 진행할 수 있지만 다음 명시적 진입 경계는 건너뛰지 않는다.

기존 전환 경로와 동일하게 이전 예고를 비우고 상태 변경을 발행해 공격 취소·장판/소환수 정리·도발 해제와 Iris 페이즈 스냅샷 복제를 재사용한다. 이벤트 게이트는 자동 전환만 막으며, 피해 차단/무적을 부여하지 않는다. 새 RPC·BT·테스트 클래스·운영 에셋/플레이어 코드는 추가/수정하지 않았다. 실제 기믹 조건이 없는 기존 SwordMan을 강제로 대기 페이즈로 바꾸지는 않았다. **이번에는 소스 구현과 변경 파일 공백 확인만 수행했으며, 컴파일·실행 검증은 기능 묶음 후속 항목으로 남긴다.**

## 72. 선택적 페이즈 무적의 GAS 연결 (2026-10-18, 소스 구현)

`raid-monster-ai` 기준으로 `FRPGBossPhaseDefinition.bInvulnerable`을 추가했다. 기본값은 false여서 기존 보스 설정은 유지한다. 선택한 페이즈에서는 새 네이티브 `URPGBossPhaseInvulnerabilityEffect`의 Infinite GE spec으로 기존 `Shared.Status.Invincible` 태그를 부여한다. 기존 GAS 피해 쿼리/공유 피해 GE의 무적 차단 정책과 ASC의 현재 태그 복제를 재사용하며, 플레이어 스킬·일반 NPC·공유 피해 계산 코드는 수정하지 않았다. 별도 Blackboard 플래그나 중복 무적 상태/RPC도 추가하지 않았다.

전투 시작·HP 전환·`AdvanceToPhase` 진입 전에 보호 효과를 준비한다. 적용 실패 시 새 전투/페이즈 진입을 거절한다. 무적 페이즈 사이에서는 기존 효과를 유지하고, 일반 페이즈 진입·전멸·reset·사망·ASC 교체/EndPlay에는 소유한 GE handle만 제거한다. 다른 Ability의 GE/loose tag를 직접 지우지 않는다. 태그 변경 콜백 중의 중첩 상태 변경을 막고 HP 변경 평가는 지연하며, 적용/제거 후 ASC/소유자/Controller 문맥도 재확인한다.

이벤트 대기 페이즈에 무적을 함께 지정하는 식으로 사용할 수 있다. 기존 SwordMan 에셋을 임의로 무적 상태로 바꾸지는 않았다. 이 기능은 기존 무적 태그를 존중하는 GAS 피해 경로의 보호이며, 관리/리셋용 서버 직접 HP 대입까지 막는 정책은 아니다. 새 테스트/검증 도구와 에셋 작업은 하지 않았다. **변경 파일 공백 확인만 수행했으며, 새 UCLASS의 UHT/컴파일 및 실제 피해 차단·GAS/Iris 태그 복제는 기능 묶음 후속 검증 항목으로 남긴다.**

## 73. 기믹 페이즈의 선택적 일반 AI 정지/재개 (2026-10-18, 소스 구현)

`raid-monster-ai`의 서버 AI/GAS 분리 기준에 따라 `FRPGBossPhaseDefinition.bSuspendCombatAI`를 추가했다. 기본값은 false다. 지정한 페이즈에서는 `ARPGBossCharacter`가 기존 전멸/reset 정지 경로를 재사용해 일반 BT·이동·전투 focus를 정리한다. 전투 상태 자체는 InCombat으로 유지하므로 서버 위협도 관리와 외부 서버에서 직접 실행하는 GAS 기믹/이벤트 페이즈 전환은 사용할 수 있다. 무적은 별도 `bInvulnerable` 옵션으로 지정한다.

다음의 비정지 전투 페이즈에서 이 전투가 저장한 이동 모드와 정지한 동일 Brain만 복구한다. 다른 소유자의 정지/일시정지, 교체된 Controller/Brain, 이미 다른 경로에서 재개된 Brain은 강제로 재시작하지 않는다. 전멸/reset 중에는 계속 정지하며 Idle만으로 재개하지 않고, 사망 상태는 기존 영구 정지 경로를 따른다. 이동 중단 콜백 이후에는 전투 상태 revision과 Controller도 확인한다.

다른 시스템이 BT를 시작하더라도 `BTTask_RPGBossAbility`는 정지 페이즈에서 명시적 AbilityClass/페이즈 패턴 모두의 실행을 거절한다. GAS 기반 클래스 자체는 막지 않아 서버가 의도적으로 실행하는 기믹까지 차단하지 않는다. 기존 SwordMan/일반 NPC/플레이어/에셋은 수정하지 않았고 새 RPC·테스트 클래스·검증 도구도 추가하지 않았다. **변경 파일 공백 확인만 수행했으며, 컴파일과 실제 정지/재개·기믹 조합 동작은 기능 묶음 후속 확인 항목이다.**

## 74. 소환 묶음 생성·개별 처치의 서버 기믹 이벤트 (2026-10-18, 소스 구현)

`raid-monster-ai`에 따라 기존 `URPGBossSummonComponent`에 BP 바인딩 가능한 서버 이벤트 두 개를 추가했다. 별도 처치/기믹 프레임워크나 테스트 클래스는 만들지 않았다.

- `OnWaveSpawned(EncounterId, PhaseIndex, Sequence, MemberCount)`: 묶음의 모든 Actor/AIController 생성 후 알린다. 비동기 GAS 시작 준비까지 완료되었다는 뜻은 아니다. 실패한 부분 생성에는 발행하지 않으며, 이미 등록된 같은 전투/묶음 ID를 중복 생성하지 않는다.
- `OnSummonDefeated(EncounterId, PhaseIndex, Sequence, Slot)`: 시작 준비가 끝난 소유 NPC의 서버 HP가 0 이하가 되면 슬롯당 한 번 알린다. 기존 GAS HP 변경 delegate를 구독하고 registry tick에서도 확인한다. 현재 전투/페이즈·소유권/Controller/팀·ASC·수명을 검증하며 초기화 실패·수명 만료·단순 제거·reset 정리를 처치로 만들지 않는다. 제거 시 해당 ASC의 구독도 해제한다.

서버 BP/GAS 기믹은 생성 이벤트의 묶음 ID/인원수를 저장하고, 동일 전투·페이즈·묶음의 처치 슬롯을 집계해 목표 수를 채우면 기존 `AdvanceToPhase`를 호출할 수 있다. registry가 비었다는 이유만으로 성공시키지 않는다. 집계와 시간 제한/초기화 실패/소환수 소실에 대한 실패 정책은 호출하는 기믹이 소유하며, 이번 변경이 자동으로 페이즈를 전환하지는 않는다.

처치 이벤트 안의 페이즈 전환/reset은 허용하되, NPC/AI 파괴는 기존 다음 틱 정리 경로로 보내 GAS HP 알림 스택을 보호한다. 중첩된 실제 처치도 기록할 수 있도록 정리 보호와 소환 중 플래그를 구분했다. 이벤트 처리 중의 중첩 소환은 거절하므로 다음 소환은 다음 서버 틱에 시작한다. 이벤트는 RPC/클라이언트 판정이 아니며, 결과 페이즈와 NPC 상태는 기존 Iris/GAS 복제를 사용한다. 플레이어·운영 에셋·검증 도구는 수정하지 않았다. **변경 파일 공백 확인만 수행했으며, UHT/컴파일·동시 처치·콜백 중 페이즈 전환 실행은 기능 묶음 후속 확인 항목으로 남긴다.**

## 75. 선택적 페이즈 제한시간과 서버 실패 판정 (2026-10-18, 소스 구현)

`raid-monster-ai` 기준으로 페이즈의 `TimeLimitSeconds`를 추가했다. 0은 비활성화, 설정 시 0.01~86400초를 허용한다. 페이즈 진입마다 전용 서버 타이머를 시작하고 마감 시각을 GameState 서버 시계 기준으로 계산한다. 광폭화 타이머와 독립적이며, 광폭화 등 같은 페이즈의 상태 갱신으로 제한시간을 다시 시작하지 않는다. 기본값 0이라 기존 SwordMan 에셋/튜닝은 바꾸지 않는다.

시간이 끝나면 기존 Wiped 전환/정리 경로로 보스전을 실패 처리한다. 소환수 초기화 실패/소실 등으로 처치 목표를 채울 수 없는 기믹이 영구 대기하지 않도록 무적·AI 정지·이벤트 페이즈와 조합할 수 있다. 이것은 보스전 실패 판정이며 공격대원에게 치명적 피해를 주는 전멸기 Ability는 아니다. 타이머 콜백은 전투 ID/페이즈를 확인하고 중첩 상태 변경 중에는 다음 틱에 재시도한다. 이전 페이즈/전투의 콜백은 무시하며 페이즈 이탈·전멸/reset·사망·ASC 해제/EndPlay에 타이머를 정리한다.

`AdvanceToPhase`와 생존 중 HP 기반 전환도 마감 시각을 확인해 타이머 처리 전에 늦은 완료가 성공하는 것을 막는다. 이미 HP가 0인 경우는 기존 terminal Dead 처리를 우선해 죽은 보스를 재시도 가능한 Wiped로 바꾸지 않는다. 기존 복제 스냅샷 `FRPGBossState`에 `PhaseEndServerTime`과 `bPhaseTimedOut`을 추가해 중도 참가자도 남은 시간 기준/시간 초과 실패 사유를 읽을 수 있게 했다. 실패 사유는 Wiped 동안 유지하고 reset/새 전투에 지운다. 클라이언트 타이머나 RPC를 판정 근거로 사용하지 않는다.

새 테스트 클래스·도구·플레이어/일반 NPC 코드·운영 에셋은 추가/수정하지 않았다. **이번에도 변경 파일 공백 확인만 수행했으며, UHT/컴파일·마감 경합·실제 실패/정리와 Iris 상태 복제는 기능 묶음 후속 검증 항목으로 남긴다.**

## 76. 소환 무리 처치 목표와 페이즈 성공 연결 (2026-10-18, 소스 구현)

페이즈의 `SummonWaveCompletionPhase`에 바로 다음 페이즈 태그를 지정하면, 해당 페이즈에서 처음 성공적으로 생성된 소환 무리의 모든 슬롯을 처치한 뒤 다음 페이즈로 진입한다. 빈 태그는 비활성화다. 설정 검증은 현재 페이즈의 양수 `TimeLimitSeconds`와 다음 페이즈의 `bRequiresExplicitTransition`을 요구한다. 이후 생성된 다른 무리의 처치는 목표에 섞지 않으며, 소환 실패/초기화 실패/수명 만료/단순 제거는 처치로 세지 않아 제한시간의 기존 Wiped 경로로 실패한다.

`raid-monster-ai`의 책임 분리에 따라 `URPGBossSummonObjectiveComponent`가 성공 집계만 담당한다. 기존 보스 기본 컴포넌트로 연결하되 소환수 생성·수명은 SummonComponent, 피해는 GAS, 마감·전환은 BossAI에 남겼다. 소환 자체를 자동 실행하지는 않으므로 기존 BT/서버 GAS에서 소환한다. `bSuspendCombatAI`를 함께 쓰는 페이즈는 서버 기믹 경로에서 직접 소환해야 한다.

서버는 전투/페이즈/무리/슬롯을 대조하고 중복 처치를 무시한다. 전원 처치 후 GAS 알림 스택 밖의 다음 틱에서 `AdvanceToPhase`를 호출하며, 전환 시점의 마감 검사는 기존 BossAI가 수행한다. 따라서 마감 직전 마지막 처치라도 실제 전환 콜백이 마감 이후라면 성공하지 않는다. 페이즈 이탈/전멸/reset/사망/EndPlay는 예약 전환을 취소하고, Controller 교체는 해당 페이즈의 목표를 취소해 이전 처치를 재사용하지 않는다. 광폭화 등 동일 페이즈 갱신은 집계를 초기화하지 않는다.

목표 활성 여부·전투/페이즈·무리 ID·필요 처치 수·현재 처치 수는 복제 스냅샷과 변경 알림으로 제공한다. UI는 중도 참가 시 스냅샷을 읽고, 서로 다른 컴포넌트의 복제 도착 순서를 가정하지 말고 BossState의 전투/페이즈와 일치할 때 표시한다. 새 RPC·플레이어 코드·운영 에셋·테스트 클래스는 추가/수정하지 않았다. **변경 소스 공백만 확인했으며 UHT/컴파일·실제 소환 기믹/동시 처치·Iris 진행도 복제는 후속 묶음 검증 대상이다. 기존 보스 에셋에는 이 기믹을 아직 활성화하지 않았다.**

## 77. 기믹 페이즈 진입 시 GAS Ability 실행 연결 (2026-10-19, 소스 구현)

`PhaseEntryAbility`로 페이즈 진입 시 실행할 보스 Ability를 선택할 수 있다. 기본값은 비어 있어 기존 보스 동작을 유지한다. 설정된 페이즈는 `bSuspendCombatAI`와 양수 `TimeLimitSeconds`가 필수다. 일반 BT와의 실행 경쟁을 피하고, 실행 조건 미충족/시전 실패에도 무한 대기하지 않도록 기존 페이즈 실패 경로를 사용한다. NPC 시작 데이터에 이미 부여된 Ability만 사용하며 런타임 자동 부여나 쿨다운 우회는 하지 않는다.

`raid-monster-ai`의 서버 페이즈/GAS 책임 분리에 맞춰 BossAI는 실행 예약만 담당한다. 페이즈 알림으로 이전 BT·Ability 정리와 소환 목표 준비를 마친 다음 서버 틱에 실행을 시도한다. 기존 GAS 대상/비용/쿨다운/소환 위치 검사가 허용할 때까지 0.1초 간격으로 확인하지만, 실제 `TryActivateAbility`를 한 번 호출한 뒤에는 실패·취소돼도 자동 재시전하지 않는다. 대상 고정과 시전 중 대상 상실 취소 규칙도 기존 보스 Ability 그대로다. 광폭화 등 동일 페이즈 상태 갱신은 재실행하지 않는다. 미부여·미인스턴싱·이미 활성인 Ability는 경고 후 예약을 중단한다.

전투/페이즈와 예약 세대, Controller·ASC 신원을 확인한다. 페이즈 이탈/전멸/reset/사망/ASC 해제/EndPlay 및 Controller 교체 시 예약을 무효화하며, GAS 활성화 검사 안의 동기 콜백으로 취소된 예약도 되살리지 않는다. 기존 소환 Ability를 지정하고 `SummonWaveCompletionPhase`를 함께 설정하면 BT가 멈춘 동안에도 소환→전원 처치→후속 페이즈 전환을 데이터로 연결할 수 있다. 피해·예고·NPC 및 목표 진행도는 기존 GAS/Iris 복제를 재사용하고 새 RPC나 별도 전투 프레임워크는 추가하지 않았다.

운영 에셋·플레이어 코드·테스트 클래스는 변경하지 않았다. **변경 소스 공백 확인만 수행했으며, UHT/컴파일과 실제 페이즈 진입 소환·취소·Iris 동작은 후속 묶음 검증 대상이다. 에셋의 기능 활성화는 아직 하지 않았다.**

## 78. 어그로 대상에 독립적인 소환 기믹 (2026-10-19, 소스 구현)

소환 Ability에 `bRequireThreatTarget`을 추가했다. 기본값 true는 기존 대상 고정/대상 변경 취소 동작을 보존한다. false로 설정하면 보스 위치 기준 소환은 어그로 대상 없이 시작할 수 있고, 도발·대상 사망/이탈·대상 Controller 교체만으로 시전을 무효화하지 않는다. 보스 자신의 전투/페이즈·Controller·ASC 실행 문맥과 서버 권위, GAS 비용/쿨다운 및 기존 소환 계획/소유권 검사는 유지한다. 전멸/reset/보스 사망/페이즈 이탈은 기존 실행 취소·소환수 정리 경로를 따른다.

`raid-monster-ai`의 GAS 재사용 기준에 따라 별도 기믹 Ability 계층을 만들지 않고, 공통 보스 Ability에 네이티브 `RequiresExecutionTarget` 확장 지점만 추가했다. 실행 시 대상 필요 여부를 고정하고 대상 독립 실행에서는 대상 자체를 캡처하지 않는다. 일반 공격·돌진·대상 위치 장판은 기본 true를 유지하며 옵션은 소환에서만 노출한다. 대상 독립 실행의 `GetBossExecutionTarget`은 null이므로 단일 대상 피해 헬퍼가 새 대상을 임의로 얻지 않는다.

플레이어·운영 에셋·테스트 클래스·RPC는 변경하지 않았다. 변경 소스 공백을 확인하고 누적 변경의 Editor 빌드를 한 번 시도했으나, 기존 외부 Build.bat 실행의 잠금 대기로 컴파일에 도달하지 못했다. 이번 작업이 시작한 대기 cmd만 실행 경로·시작 시각으로 대조하여 종료했고 기존 빌드는 유지했다. **UHT/컴파일·대상 변경 중 실제 소환·Iris 동작은 아직 미검증이며, 에셋 옵션 활성화도 아직 하지 않았다.**

## 79. 독립 SwordMan 소환 기믹 프리셋 생성 경로 (2026-10-19, 생성 코드 준비)

`Scripts/Editor/CreateRaidSwordManSummonMechanic.py`를 추가했다. 최신 네이티브 코드를 빌드한 뒤 Unreal Python에서 `-RPGCreateSwordManSummonMechanic`으로 실행하면 기존 SwordManVariant에서 보스·Controller·BT·Definition·Startup·소환 Ability 여섯 에셋을 `/Game/Blueprints/Character/NPC/Raid/SwordManSummonMechanic`으로 복제해 연결한다. 기본 실행은 읽기 전용이며, 기존 묶음은 덮어쓰지 않고 부분 생성 상태도 자동 복구/삭제하지 않는다. 오래된 네이티브 코드나 원본의 예상하지 못한 기믹 설정은 복제 전에 거절한다.

구성은 초기 전투 → 70% HP에서 무적/BT 정지·대상 독립 Guardian 둘 소환 → 30초 안에 전원 처치 시 전투 재개다. 기믹 소환은 별도 Ability로 분리해 전투 중 소환의 기존 쿨다운과 격리하며, 소환 수명은 45초로 둔다. 후속 전투는 기존 2페이즈 패턴에서 일반 소환만 제외해 기믹을 반복하지 않는다. 후속 HP 임계치 69%는 데이터의 내림차순 정렬용이며 명시적 완료 전환은 그 HP까지 기다리지 않는다. 복제 BT의 돌진 오프너 조건은 재개 페이즈 인덱스 2로 변경하고, 기존 Blackboard·피해/GAS·예고 표현은 재사용한다.

스킬의 데이터 중심 제작 기준에 맞춰 추가 네이티브 전투 프레임워크 없이 기존 기능을 조합했다. 원본/기존 Variant·맵·GameMode·실제 전투 보스 선택은 변경하지 않는다. **현재는 태그 등록과 생성 스크립트의 구문/공백 확인까지만 완료했다. 스크립트를 Unreal에서 실행하지 않았으므로 여섯 에셋은 아직 생성하지 않았으며, 누적 C++ 빌드 후 생성·로드와 실제 소환 기믹 실행 확인이 남아 있다.**

후속 빌드 시도(2026-10-19): 외부 빌드 잠금은 해소됐지만 실행 환경에서 `%APPDATA%/Unreal Engine/UnrealBuildTool`에 대한 쓰기가 거부되어 UHT/컴파일 전에 중단됐다. 프로젝트의 기존 `XmlConfigCache.bin`과 프로젝트 내부 UBA 저장 경로를 지정하면 초기 설정/UBA 경로 문제는 지나가지만, Makefile 로드 단계가 다시 사용자 설정 디렉터리 쓰기를 요구한다. `-NoUBTMakefiles`도 이 접근을 없애지 못했다. 해당 실행 로그는 `Saved/Build/RaidMechanicPreset-d680f339449f49e0a5a214184f80f8d5/`에 있다. 반복 오류를 내던 이번 작업의 UBT 프로세스만 신원을 대조해 종료했으며, 사용자 설정/엔진/외부 빌드는 변경하지 않았다. 일반 로컬 터미널 등 필요한 디렉터리에 쓸 수 있는 환경에서 Editor 빌드를 완료한 뒤 프리셋 생성으로 이어가야 한다. 이번 시도는 C++ 컴파일 실패의 증거가 아니라 환경 권한에 의한 빌드 시작 실패이며, 에셋 생성은 실행하지 않았다.

## 80. 소환 기믹 프리셋 실제 생성·독립 재로드 완료 (2026-10-19)

사용자의 빌드 완료 후 갱신된 Editor DLL을 확인하고 79절 생성 스크립트를 실행했다. `/Game/Blueprints/Character/NPC/Raid/SwordManSummonMechanic` 아래 보스·Controller·BT·Definition·Startup·소환 Ability 여섯 에셋을 생성했다. 새 Blueprint 컴파일과 저장 후 별도 프로세스에서 적용 플래그 없이 다시 로드해 세 페이즈·Ability 부여/설정·참조 연결·재개 페이즈의 돌진 조건을 확인했다. 생성 실행과 재로드 모두 종료 코드 0, Python 스크립트 성공 및 `RAID_SWORDMAN_SUMMON_MECHANIC READY`를 기록했다. 기존 GAS/BT/소환/목표 컴포넌트를 재사용하며 추가 전투 프레임워크는 만들지 않았다.

소환 NPC의 실제 Controller가 사용하는 BT/Blackboard 로드 가능 여부도 생성 코드의 사전 검사에 추가했다. 실제 참조는 `/Game/Blueprints/Character/NPC/Gruntling/Guardian/BT_Gruntling`이었다. 로그에는 기존 구형 `/Gruntling/BT_Guardian` 패키지 경고가 남지만, 실제 소환 NPC의 유효 BT 참조와 혼동하지 않는다. 생성 전후 원본/Variant/Guardian 에셋 15개의 SHA-256이 동일하고, 신규 여섯 에셋도 읽기 전용 재로드 전후 동일했다. 맵·GameMode·전투 보스 선택은 변경하지 않았다.

기본 DDC는 이 실행 환경의 사용자 디렉터리 권한으로 시작에 실패해, 실행 옵션만 `-ddc=InstalledNoZenLocalFallback -LocalDataCachePath=C:/UE5/Project_RPG/Saved/DerivedDataCache`로 지정했다. 사용자/엔진 설정은 수정하지 않았다. 시작 중 SDK 조회용 UBT의 AppData 오류와 비치명적 경고는 남았으나 Python 명령 실행·저장·재로드는 성공했다. 증거는 `Saved/Tests/BossSummonMechanicPreset/565ed1bd451947cb8f188bf1a39d98eb/Apply-LocalDDC.log`와 `Reload.log`다. **이 결과는 에셋 생성/로드 확인이며, 실제 70% 진입·소환수 처치·30초 실패·일반 AI 복구와 전용/리슨 서버 Iris 동작을 실행 검증한 것은 아니다. 해당 실제 전투 확인이 다음 작업이다.**

## 81. 소환 기믹의 단일 권위 월드 실행 확인 (2026-10-19)

기존 `ProjectRPG.Boss.Summon`의 latent 검사에 기믹 흐름을 추가했다. 새 테스트 클래스나 프레임워크 없이, 저장된 프리셋 Definition·진입 Ability·BT를 읽고 기존 NPC 시작 픽스처를 재사용한다. `raid-monster-ai` 기준에 따라 실제 서버 소유 BossAI/GAS/소환 registry/목표 컴포넌트의 상태 변화를 확인하며 결과를 에셋 로드 검사와 구분했다.

확인 항목은 70% 임계치 진입, GAS 무적 부여·BT/이동 정지, 대상 독립 진입 소환과 어그로 변경 중 시전 유지, 슬롯당 처치 1회 집계, 전원 처치 후 명시적 후속 페이즈·무적 해제·BT/이동 복구·소환수 정리다. 재시작 시 새 전투 ID와 빈 처치 수를 확인하고, 살아 있는 소환수의 단순 파괴를 성공으로 세지 않으며 게임 월드를 30초 넘게 진행하면 `Wiped/bPhaseTimedOut`·목표/무적/소환수 정리·BT 정지가 적용되는 것도 확인했다.

첫 실행은 대상 없는 시전의 복합 assertion 한 항목이 실패했다. 감지를 끄더라도 기존 지각 캐시를 다시 읽는 경로가 있으므로, 검사에만 대상 팀을 일시적으로 우호적으로 만들어 재감지로 대상이 들어오는 조건을 차단하고 assertion을 분리했다. 운영 코드는 바꾸지 않았다. 수정된 검사 파일의 컴파일은 성공했다. 증분 링크 시도는 동시 외부 링크와 경합했으므로 해당 작업이 끝난 뒤 새 DLL에 보정된 assertion이 포함된 것을 확인하고 실행했다. 최종 `Summon-Retest.log`에는 `Test Completed. Result={Success} Name={Summon} Path={ProjectRPG.Boss.Summon}`이 기록됐고 프로세스 종료 코드도 0이다. 테스트의 종료 코드만으로 성공을 판단하지 않았다.

증거 디렉터리는 `Saved/Tests/BossSummonMechanicRuntime/37127c03e0a340cdb332765c71541c3e/`다. 최종 실행 전후 모듈 SHA-256은 `A01E0D4E305CBBF99927B1BD25E891A43EEEEE84DC16F39F478591D74B784486`으로 동일하고, 프리셋 에셋 여섯 개도 변경되지 않았다. **이 검사는 독립된 권위 게임 월드에서 소환 NPC를 기존 시작 픽스처로, 내비게이션 요구를 false로, 시전 예고를 0.1초로 바꾼 인메모리 실행이다. 실제 Guardian의 내비게이션/공격·실제 맵의 보스전·전용/리슨 서버 다중 클라이언트 Iris 검증은 아니다. 해당 운영 콘텐츠/네트워크 확인은 남아 있다.**

## 82. 실제 bossmap/Guardian/Iris 소환 기믹 실행 경로 추가 (2026-10-19 구현, 2026-10-20 전용 서버 통과)

2026-10-20 후속 확인: 첫 전용 서버 실행(`Saved/Tests/BossMap/45b79080f57c4d77ac7b1ff43599aad0/server.log`)은 프리셋 선택 검사가 `bCombatPhaseScenario`만 허용해 `VariantSelectionInvalid`로 중단됐다. `bSummonMechanicScenario`도 허용하되 클래스 속성·로드·상속 검사는 유지하도록 검증 코드를 수정했고, 사용자 재빌드 후 이 단계는 통과했다.

실제 Guardian 실행(`Saved/Tests/BossMap/f59f03525274428f8e24a2abfcf185c8/`)에서는 초기화와 두 내비게이션 경로까지 통과했으나, `NPC.Ability.Melee`에 두 능력이 일치해 공용 GAS의 단일 능력 선택 검사가 활성화를 거절했다. 피해 없이 30초가 지나 기믹이 실패했다. 읽기 전용 에셋 확인으로 `GA_Guardian_Melee1/2`의 동일 태그를 확인한 뒤 `ConfigureGuardianMeleeSelection.py -RPGApplyGuardianMeleeSelection`으로 두 BP와 실제 Guardian `BT_Gruntling`만 저장했다. `NPC.Ability.Melee.Guardian.Primary/Secondary`를 등록하고 각 BT 노드와 대응 공격에 연결했다. 기존 부모 태그 매칭·공용 GAS 검사·능력 그래프·실행 정책·Startup·맵/운영 보스 선택은 변경하지 않았다. 수정 전 세 에셋은 해당 실행 디렉터리의 `Guardian-Before/`에 백업했다. 스크립트 기본 모드는 읽기 전용이며 사용자 커스텀 태그나 예상 밖 BT 구조는 거절한다.

수정 후 동일 전용 서버 명령이 통과했다. 증거는 `Saved/Tests/BossMap/72d09c0822914fe896bcdb50ae0e6cef/BossMap.proof.json` 및 세 프로세스 로그다. 실제 Guardian 둘의 준비/경로, 이동, Guardian GAS 출처의 HP 감소(300→294), 양쪽 Iris 클라이언트의 소환수·목표·피해 관찰, 1/2 진행도와 완료 페이즈 동기화, 서버 무적 해제/BT·이동 재개/소환수 정리를 확인했다. `RAIDMAP_E2E_SUMMON_MECHANIC_COMPLETE`와 양쪽 `CLIENT_COMPLETE`를 확인했으며 프로세스 종료 코드도 0이다. 모듈 SHA-256은 `1CAC4D9B75E90E04000EB7228A11BD1C375273460EAE4B586927978B80CBEE18`이고 검사 전후 모듈 및 소환 기믹 프리셋 여섯 에셋은 동일했다. **처치는 서버 GE 자극이며 플레이어 스킬 처치 입력 증거가 아니다. 리슨 서버·기믹 도중 참가·시간 초과·지연/손실·Shipping 검증까지 완료한 것은 아니다.**

기존 `Run-BossMapE2ETest.ps1`에 `-SummonMechanic`을 추가하고, 기존 `URPGBossMapE2EProbeComponent`의 전용 시나리오로 연결했다. 새 UCLASS·RPC·별도 검사 프레임워크는 만들지 않았다. 선택 시에만 런타임 GameMode 인스턴스가 소환 기믹 프리셋을 사용하며, 맵/운영 BossClass/에셋은 저장하지 않는다. 기존 자연 감지·보스 BT 접근/GAS 피해·두 원격 클라이언트 준비 장벽 후, 서버 HP 자극으로 기믹에 진입한다.

경로는 실제 Guardian 둘의 초기화와 각 내비게이션 경로, 실제 이동과 Guardian ASC/Ability 문맥의 참가자 HP 감소를 요구한다. 두 Iris 클라이언트가 소환수 참조·목표·참가자 HP 감소를 관찰한 뒤 첫 처치를 자극하고, 양쪽의 1/2 진행도 관찰 후 두 번째 처치를 자극한다. 마지막에는 서버의 무적 해제/BT·이동 재개/소환수 제거와 양쪽 클라이언트의 후속 페이즈·비활성 목표·빈 소환 목록을 확인하도록 했다. 처치는 명시적인 서버 GE 자극이며 플레이어 스킬 입력/HitQuery 처치 증거가 아니다. 클라이언트 확인 RPC는 현재 서버 단계/전투·페이즈 스냅샷/소환 시퀀스를 검사하며 서로 다른 컴포넌트의 복제 도착 순서를 가정하지 않는다. 진행도 중간 단계가 복제되기 전에 다음 처치를 진행하지 않는다.

실행 도구는 오래된 DLL을 프로세스 시작 전에 거절하고, 여섯 프리셋 에셋의 변경 여부를 확인한다. 스킬의 서버 판정/클라이언트 관찰 분리에 맞춰 클라이언트 확인은 관찰 장벽일 뿐 전투 판정으로 사용하지 않는다. 실제 Guardian 피해와 클라이언트 HP 관찰은 기록하되, 각 원격 소유 Pawn의 피해·플레이어 처치 입력·기믹 도중 신규 참가·시간 초과·지연/손실 검증까지 완료한 것으로 표시하지 않는다.

구현 당시(2026-10-19)에는 구문 검사만 완료했고 다른 작업의 GameMode 헤더/UHT 불일치와 실행 중인 에디터 때문에 링크·실제 실행은 보류했었다. 이후 사용자 빌드와 위 Guardian 에셋 수정으로 `Scripts/Network/Run-BossMapE2ETest.ps1 -SummonMechanic -SkipSDKSetup`의 **실제 맵/전용 서버/두 Iris 클라이언트 실행까지 통과했다.** 이번 후속 수정은 C++ 재빌드가 필요 없는 에셋/태그 설정 변경이며 새 테스트 클래스나 전투 프레임워크는 추가하지 않았다.

## 83. 소환 기믹 리슨 서버 실행 확인 (2026-10-20)

`raid-monster-ai`의 서버 판정/클라이언트 관찰 분리에 따라 기존 명령에 `-ListenServer`만 추가해 실행했다. 호스트 플레이어가 있는 실제 bossmap에서 두 원격 Iris 클라이언트를 연결했다. Guardian 두 마리의 초기화/경로, 실제 이동과 Guardian GAS 피해(300→297), 양쪽 원격 클라이언트의 소환수/목표/참가자 HP 감소 관찰, 1/2 처치 진행도와 완료 페이즈 동기화를 확인했다. 서버의 무적 해제·BT/이동 재개·소환수 정리도 확인했고 종료 코드는 0이다. 새 코드·에셋·검사 클래스는 추가하지 않았다.

증거는 `Saved/Tests/BossMap/02cbe42521eb48b18ce6b6517df4652a/BossMap.proof.json`과 세 로그다. `listenServer=true`, `guardianNavigationGASAndProgressReplicationVerified=true`이며 서버 완료와 양쪽 클라이언트의 wave/partial/complete 마커를 확인했다. 이번 실행 모듈 SHA-256은 `E86A1414F576D8CA4C12127909C85D703980E29F7F04EA204C2D8D6D1840AFFB`로 앞선 전용 서버 때와 다르므로 동일 바이너리의 양 모드 비교라고 주장하지 않는다. 이번 실행 전후의 모듈/소환 기믹 프리셋 여섯 에셋은 동일했다. **처치는 서버 GE 자극이다. 플레이어 스킬 처치·각 원격 소유 Pawn의 피해·기믹 도중 신규 참가/재접속·실제 네트워크 시간 초과·지연/손실·Shipping까지 검증한 것은 아니다.**

## 84. 원격 스킬 입력으로 마지막 소환수 처치 확인 경로 (2026-10-20, 빌드/실행 대기)

소환수 처치 집계는 이미 공용 GAS HP 변경에 연결돼 있어 운영 NPC/플레이어 코드를 추가하지 않았다. 기존 보스 맵 검사에 `-SummonPlayerKill` 선택지를 추가했다. 기존 무기 로드아웃 준비·Enhanced Input 접근/조준/스킬 입력·몽타주/자연 종료 관찰과 동일한 RPC를 재사용한다. 입력 요청 RPC에는 피해 대상 Pawn을 명시해 기존 보스와 소환수를 구분한다. 새 검사 클래스·별도 전투 프레임워크·플레이어 능력/피해 구현·운영 에셋은 추가하거나 수정하지 않았다.

이 모드는 `SummonMechanic`을 포함한다. Guardian 실제 이동/GAS 피해와 두 클라이언트 관찰 이후 첫 처치만 기존 서버 GE 자극으로 실행한다. 양쪽의 1/2 진행도 확인 뒤 남은 Guardian에 인메모리 HP 1 조건을 적용하고, 가장 가까운 원격 참가자가 기존 `AssultBlade` 스킬을 입력하도록 요청한다. 마지막 처치에서는 직접 피해 GE를 만들지 않는다. 서버는 캡처한 공격 spec 활성 상태·능력 클래스·출처 ASC·GE의 서버 히트 대상·치명 HP·보안 위반 수 불변·능력 자연 종료·입력 소유자 완료 확인을 요구하고, 기존 두 클라이언트의 후속 페이즈/정리 확인을 유지한다. 일반 NPC 피해는 GE 문맥에 Ability를 기록하지 않으므로 **GE 문맥의 능력 provenance를 검증했다고 주장하지 않는다.** 치명타 직후에는 사망 처리로 충돌이 꺼질 수 있으므로 새로운 사후 히트 쿼리가 아니라 적용된 GE의 히트 기록을 확인한다. 스킬별 관찰은 기존 테스트 어댑터 안에 유지해 NPC 집계가 플레이어 구현에 종속되지 않게 했다.

PowerShell 파싱·변경 파일 공백 확인 및 오래된 DLL의 프로세스 시작 전 차단을 확인했다. 소환 기믹 probe와 스킬 관찰 어댑터의 MSVC `/Zs` 구문 검사도 통과했다. 새 옵션은 아직 빌드되지 않은 DLL에서 실행되지 않는다. 수동 구문 확인에서 입력 파일은 수정된 RPC와 기존 UHT 생성 선언의 불일치로 전체 확인을 끝내지 못했으며, 생성 헤더는 수동 편집하지 않았다. **UHT/Editor 전체 빌드와 실제 맵 실행 통과 결과는 아직 없다.** 정상 `Project_RPGEditor` 재빌드 후 `Scripts/Network/Run-BossMapE2ETest.ps1 -SummonPlayerKill -SkipSDKSetup`으로 확인한다. 무기 장착과 낮은 HP는 검사 조건이며, 두 소환수 모두 플레이어 처치·원래 체력에서의 밸런스·실제 아이템 획득/백엔드 장착·기믹 시간 초과 검증을 의미하지 않는다.

## 85. 소환수 처치 검사를 현재 스킬 퀵슬롯 입력에 연결 (2026-10-20, 퀵슬롯 입력 확인)

84절 코드가 포함된 사용자 빌드를 확인하고 전용 서버에서 실행했다. `Saved/Tests/BossMap/ef53efb09ea346b8a2c15476eeeb122f/`에서 실제 Guardian 이동/피해, 양쪽 1/2 진행도 및 원격 Enhanced Input 요청까지 진행됐으나 스킬 활성화/처치 없이 기믹이 시간 초과됐다. GAS 상세 로그를 활성화한 한 차례의 진단 실행(`Saved/Tests/BossMap/e86ffd82cd2346eface589a6b41ebbaa/`)에서도 능력 부여와 입력 요청만 확인됐다. 현재 Controller는 퀵슬롯이 소유하는 키의 예전 고정 능력 입력을 명시적으로 무시하고, 스킬 슬롯은 비어 있으므로 기존 검사의 고정 능력 입력으로는 스킬이 활성화되지 않는 구조였다. 운영 NPC 처치 집계 실패로 판정하지 않았다.

`raid-monster-ai`의 기존 시스템 재사용 원칙에 따라 운영 플레이어/Controller/퀵슬롯 코드는 변경하지 않았다. NPC 검사 모드에 한해 서버가 기존 `SetSkillSlot`/`CanAssignSkillSlot` 공개 API로 소유 스킬을 등록한다. 기존 동일 스킬 등록을 재사용하거나 유효한 입력 바인딩이 있는 빈 슬롯만 사용하며 다른 스킬은 덮어쓰지 않는다. 클라이언트는 owner-only 슬롯 복제를 확인한 뒤 해당 native 슬롯 액션을 Enhanced Input으로 주입한다. 직접 활성화나 내부 슬롯 배열 주입은 하지 않는다. 스킬 Definition의 정체성 조회는 기존 공격 시나리오 어댑터에 격리했고, 입력/접근/조준/종료 확인은 재사용했다. 기존 일반 보스 공격 검사 경로의 입력 선택은 변경하지 않았다.

수정한 입력 probe와 시나리오 어댑터의 MSVC `/Zs`, PowerShell 파싱·공백 확인·새 슬롯 준비 마커 없는 DLL 차단을 통과했다. 사용자 재빌드 후 실제 실행(`Saved/Tests/BossMap/260e577762324a309a2e955cc76f3c0b/`)에서 양쪽 슬롯 준비와 원격 `AssultBlade`의 클라이언트/서버 활성화를 확인했다. **퀵슬롯 입력 문제는 해소됐으나 처치/기믹 완료는 통과하지 않았다.** 서버가 제출된 soft target을 거리·팀·태그·시야 규칙으로 거절해 능력을 취소했고, 검사는 `SummonPlayerKillSecurityChanged`로 중단됐다. 보안 검사를 완화하거나 위반을 성공으로 무시하지 않았다. 첫 서버 처치/마지막 소환수 HP 1/임시 무기 및 슬롯 등록 조건은 증거와 분리해서 유지한다. 새 검사 클래스·운영 에셋·백엔드 쓰기는 추가하지 않았다.

## 86. 소환수 스킬 대상 거절 원인 관찰과 조준 정지 (2026-10-20, 재빌드 대기)

85절 실패 로그는 여러 대상 규칙을 하나의 오류로 기록해 실제 제출된 Actor를 구분할 수 없었다. 원인이 죽은 소환수 선택인지, 움직이는 대상의 조준/복제 차이인지 확인되지 않은 상태에서 운영 NPC나 플레이어 타깃/보안 정책은 수정하지 않았다. 기존 검사 컴포넌트가 선택된 능력의 서버 활성화 시점에 target-data delegate를 관찰하도록 했다. 능력의 소비자보다 먼저 제출된 Actor·예상 소환수 일치·서버 HP/사망 태그/충돌·일반 자격 검사·서버 soft-target 후보 포함 여부·거리/방향/출발점 차이를 기록한다. 데이터는 읽기만 하고 소비·변경·재전송하지 않으며, 종료 시 두 delegate를 해제한다. 스킬별 조회는 기존 시나리오 어댑터에 유지했다.

또한 이 검사 모드에서 사거리 안에 들어간 뒤 조준 대기 중에도 계속 이동 입력을 주던 동작을 정리했다. 접근 입력을 중지하고 정지 상태에서 조준 대기를 시작하며, 재접근하면 조준 대기를 다시 시작한다. 이는 검사 입력 제어 변경이며 실제 이동 대상 검증 문제가 해결됐다는 증거는 아니다. 새 관찰 마커 없는 DLL은 프로세스 시작 전에 차단한다. **수정 후 실제 맵 실행은 아직 하지 않았다.** `Project_RPGEditor` 재빌드 후 `Scripts/Network/Run-BossMapE2ETest.ps1 -SummonPlayerKill -SkipSDKSetup`으로 확인하며, 실패하면 `RAIDMAP_E2E_SUMMON_PLAYER_TARGET_DATA`를 근거로 원인을 좁힌다.

## 87. 정지 조준 검사에서 정상 카메라 전송 요청 (2026-10-20, 재빌드 대기)

86절 사용자 빌드를 실행한 결과(`Saved/Tests/BossMap/370092db12c9488280df4f9da713ed5a/`) 원격 스킬은 활성화됐으나 `Replicated aim diverges from the server-known control aim.`으로 거절됐다. 따라서 그 실행의 직접 거절 사유는 서버 조준 방향 불일치다. 제출 대상 관찰 마커는 남지 않아 실제 선택된 Actor나 이전 실행의 후보 거절 원인까지 확정하지 않았다.

엔진 `APlayerController`는 이동 입력/속도가 있을 때 카메라 전송을 요청하며, 정지 상태의 검사 `SetControlRotation`만으로는 전송 요청이 보장되지 않는다. 검사 모드의 조준 대기 중 `PlayerCameraManager->bShouldSendClientSideCameraUpdate`를 요청해 정상 카메라 tick/전송 경로를 사용하도록 보정했다. 서버 카메라·control aim을 직접 변경하거나 별도 RPC로 조준을 강제하지 않으며 보안 허용치는 유지한다. 새 `RAIDMAP_E2E_SUMMON_PLAYER_AIM_SYNC` 마커 없는 DLL은 실행 전에 차단한다.

관찰 delegate 등록/예측 키를 기록하고, 수신 데이터 해독 실패나 frozen soft config/쿼리/카메라 정보 부족도 기록하도록 했다. 관찰은 계속 읽기 전용이다. 변경한 입력 probe·소환 기믹 probe·시나리오 어댑터의 MSVC `/Zs`, PowerShell 파싱과 공백 확인은 통과했다. **이번 카메라 보정은 아직 재빌드/실행 전이며 마지막 소환수의 원격 스킬 처치 통과를 의미하지 않는다.** 정상 `Project_RPGEditor` 재빌드 후 기존 `-SummonPlayerKill -SkipSDKSetup` 명령으로 이어간다. 운영 NPC/플레이어 코드·에셋은 변경하지 않았다.

## 88. 원격 퀵슬롯 스킬로 마지막 소환수 처치 — 전용 서버 통과 (2026-10-20)

87절 사용자 빌드의 카메라 보정을 확인한 뒤 기존 `Scripts/Network/Run-BossMapE2ETest.ps1 -SummonPlayerKill -SkipSDKSetup`을 실행했다. 실제 bossmap/전용 서버/원격 Iris 클라이언트 둘에서 통과했다. 증거는 `Saved/Tests/BossMap/206176f5850d46478b7d8083cf315d92/BossMap.proof.json`과 세 프로세스 로그다. 검사 전후 모듈 SHA-256은 `5480168288D16B91D28BA4B431823330E89A487F1FFC3DB2E8719F89F0D2D81A`로 동일했고 소환 기믹 프리셋 여섯 에셋도 동일했다. 이번 턴에 추가 C++/운영 에셋 변경은 하지 않았다.

실제 Guardian 이동/GAS 피해와 양쪽 1/2 진행도 관찰 이후 원격 참가자가 등록된 퀵슬롯의 `AssultBlade`를 Enhanced Input으로 실행했다. 카메라 갱신 요청, 선택된 능력의 서버 활성 상태와 출처 ASC, 마지막 Guardian을 가리키는 GE 히트 기록 및 HP 0, 능력 자연 종료, 입력 소유자 완료 확인, 보안 위반 수 불변을 확인했다. 양쪽 클라이언트는 후속 페이즈·비활성 목표·빈 소환 목록을 관찰했고 서버는 무적 해제/BT·이동 재개/소환수 정리를 확인했다. `RAIDMAP_E2E_SUMMON_PLAYER_KILL_COMPLETE`, 기존 기믹 완료 및 양쪽 클라이언트 완료 마커와 종료 코드 0이 기록됐다. 기존 GAS/입력/퀵슬롯/복제를 재사용하고 보안 허용치는 유지했다.

관찰 로그의 soft target은 보스였고(`Match=0`, `ServerCandidate=1`), 실제 피해 GE의 히트 대상이 Guardian이었다. 따라서 **Guardian이 soft target으로 직접 선택됐다고 주장하지 않으며**, 유효한 스킬 공격의 실제 히트로 마지막 소환수를 처치하고 기믹이 완료되는 연결을 확인한 결과다. 일반 NPC GE 문맥에 능력 provenance가 없는 제약은 그대로다.

**검사 조건:** 첫 소환수는 서버 GE 자극으로 처치했고 마지막 소환수는 HP 1 조건이다. 무기/스킬 슬롯 등록은 인메모리 검사 준비다. 원래 체력에서의 전투 밸런스, 두 소환수 모두 플레이어 처치, 실제 획득/백엔드 장착, 기믹 도중 참가/재접속, 시간 초과·지연/손실·Shipping은 검증하지 않았다. 리슨 서버의 소환 기믹 자체는 83절에서 통과했지만, **이번 원격 스킬 마지막 처치 모드의 리슨 서버 실행은 아직 별도 확인이 필요하다.**

## 89. 리슨 서버 원격 처치 — 바닥 fallback 입력 방지 (2026-10-20, 재빌드 대기)

기존 `-SummonPlayerKill -ListenServer -SkipSDKSetup`을 실행했다. `Saved/Tests/BossMap/6d2ccbda926943aaba619fe531edb4b1/`에서 호스트/원격 둘의 준비, 실제 Guardian 피해와 양쪽 1/2 진행도, 원격 스킬 활성화까지 진행됐으나 서버가 soft target을 거절했다. 관찰 데이터의 제출 대상은 남은 Guardian이 아니라 `Floor`였다. 거절 후 능력이 종료돼 frozen soft config는 비어 있었으며, 이 상세 로그로 거리·팀·태그·시야 중 하나를 확정하지는 않았다. 리슨 원격 처치 통과로 기록하지 않았다.

운영 스킬의 fallback/타깃 선택/보안 정책을 수정하지 않고 검사 입력 준비만 보완했다. 기존 시나리오 어댑터가 공개 Definition/runtime-spec 생성과 현재 스킬 저장 데이터를 읽어 원래 soft-target 후보 필터/각도/거리로 현재 카메라 방향의 후보를 조회한다. 예상 Guardian이 살아 있고 실제 후보에 있을 때만 조준 대기를 완료하며, 후보가 아니면 입력 없이 대기하고 조준 대기를 재시작한다. 실제 스킬은 계속 기존 퀵슬롯 Enhanced Input을 사용하며, 대상 데이터 직접 지정/대상 강제 선택/필터 완화는 하지 않는다. 서버 관찰과 입력 준비는 공통 읽기 전용 후보 조회를 재사용한다. 이는 입력 시점의 유효 후보를 확인하는 검사 조건이지 이동 대상이나 네트워크 경합을 모두 해결했다는 보장은 아니다.

새 `RAIDMAP_E2E_SUMMON_PLAYER_AIM_CANDIDATE_READY` 없는 DLL은 시작 전에 거절한다. 변경한 어댑터/입력 probe의 MSVC `/Zs`, PowerShell 파싱과 공백 확인을 통과했다. **수정 후 리슨 서버 재실행은 아직 하지 않았다.** `Project_RPGEditor` 재빌드 후 `Scripts/Network/Run-BossMapE2ETest.ps1 -SummonPlayerKill -ListenServer -SkipSDKSetup`으로 이어간다. 88절의 전용 서버 성공은 이전 빌드의 증거로 유지하며 이번 보정 빌드가 통과했다고 바꾸지 않는다. 운영 플레이어/NPC 코드·에셋·새 검사 클래스·백엔드 쓰기는 추가하지 않았다.

## 90. 리슨 재실행의 선행 Guardian 피해 단계 시간 초과 (2026-10-21, 관찰 코드 재빌드 대기)

89절 사용자 빌드를 확인하고 리슨 명령을 실행했다. `Saved/Tests/BossMap/ec929d5895394b3b88db8bdc30905c12/`에서 두 Guardian 초기화/경로까지 통과했으나 실제 피해가 확인되지 않아 30초 기믹 제한시간이 끝나 Wiped가 됐다. 바뀐 조준 입력 단계에는 도달하지 않았다. 코드를 변경하지 않은 재현 확인 한 번(`Saved/Tests/BossMap/2a4210aee679439496ff47ec38d269fc/`)에서도 같은 단계의 시간 초과가 발생했다. 두 번째 실행에는 Guardian Melee1 활성화가 있으므로 "공격 능력이 전혀 시작되지 않았다"고 단정하지 않는다. 실제 피해 마커가 없고 참가자 HP가 유지된 결과와 능력 활성화는 구분한다. 두 실행 모두 리슨 원격 스킬 처치 성공이 아니다.

추가 반복은 중단하고 기존 소환 기믹 검사의 피해 대기 단계에 5초 간격 읽기 전용 상태 기록만 추가했다. `RAIDMAP_E2E_SUMMON_GUARDIAN_DIAG`는 초기화/소유자/Controller/팀, BT 실행과 활성 노드, Blackboard 타깃/거리, 이동 상태/실제 이동량, mesh tick/visibility animation 정책/최근 렌더링/몽타주/ASC 태그를 기록한다. 피해가 확인되면 이 관찰을 멈춘다. 새 검사 클래스나 RPC 없이 기존 컴포넌트에 한정했고, 새 관찰 코드 없는 DLL은 원격 처치 실행 전에 차단한다. 운영 AI/피해/기믹 제한시간/에셋은 변경하지 않았다.

**현재 Guardian 피해 미발생의 원인은 확정하지 못했고, 새 관찰 코드는 재빌드 전이다.** `Project_RPGEditor` 재빌드 후 기존 `-SummonPlayerKill -ListenServer -SkipSDKSetup` 명령의 상태 로그를 근거로 NPC 타깃·이동·공격 연결을 좁힌다. 기존 전용 서버 성공 및 이전 빌드의 리슨 소환 기믹 성공은 별도 증거로 유지하며, 이번 빌드/원격 처치 모드의 성공으로 대체하지 않는다.

## 91. 클라이언트의 소환수 팀 조회 연결 (2026-10-21, 재빌드 대기)

90절 사용자 빌드로 실행한 `Saved/Tests/BossMap/b2f5a35a00b441598a0612197a90056a/`에서는 Guardian 이동·실제 GAS 피해(300→297)와 두 클라이언트의 1/2 진행 관찰까지 확인했다. 마지막 Guardian에 대한 조준 후보 검사는 계속 false여서 입력을 보내지 않았고, `PlayerAttackVictimLostBeforeInput`으로 종료됐다. Guardian 피해가 이번에 발생했다는 사실로 이전 두 실행의 피해 미발생 원인까지 해결됐다고 주장하지 않는다.

코드상 `UTeamManager`는 NPC Pawn에 팀 인터페이스가 없으면 AIController를 통해 팀을 찾는다. 원격 클라이언트에는 이 컨트롤러가 없고, 소환수의 `EncounterSummonTeam`도 서버 로컬이어서 적대 대상 후보 필터에 필요한 팀을 제공하지 못했다. NPC에 읽기 전용 `IGenericTeamAgentInterface` 조회를 구현하고, 기존 서버 설정 팀 값을 Pawn의 복제 프로퍼티로 등록했다. 일반 NPC는 기존 instigator/owner/controller/PlayerState 조회 순서를 유지하며 클라이언트 팀을 임의로 추정하지 않는다. 스킬·HitQuery·팀 매니저의 필터/보안 정책, 플레이어 코드, 에셋과 기믹 제한시간은 변경하지 않았다.

기존 소환 검사에 컨트롤러 생성 전 팀 조회·복제 프로퍼티 확인을 추가했다. 기존 맵 검사는 두 클라이언트가 두 Guardian을 자신의 Pawn과 적대 팀으로 판정해야 wave를 확인하고, `RAIDMAP_E2E_SUMMON_CLIENT_TEAMS_READY`를 기록한다. 러너는 이 코드가 없는 DLL을 시작 전에 거절한다. 새 검사 클래스/RPC는 추가하지 않았다. 갱신된 UHT 헤더와 일치하는 상태에서 변경한 C++ 3개 파일의 MSVC `/Zs`, PowerShell 파싱과 tracked 변경 공백 검사를 통과했다. **Editor 전체 빌드/링크 완료 및 수정 후 실제 Iris 실행은 아직 확인하지 못했으며, 조준/처치 전체 통과를 의미하지 않는다.**

후속 실행(2026-10-22): 새 DLL에서 팀 확인 마커를 확인하고 리슨 서버 명령을 한 번 실행했다. `Saved/Tests/BossMap/0c93518f704b4fb880971fe59653b332/`의 양쪽 원격 클라이언트에 `RAIDMAP_E2E_SUMMON_CLIENT_TEAMS_READY`가 기록됐고, 실제 Guardian GAS 피해와 1/2 진행 관찰을 통과했다. 원격 입력의 유효 후보 검사도 통과했으며, 서버 수신 대상이 남은 Guardian과 일치했다(`Match=1 Eligible=1 ServerCandidate=1`). 실제 스킬 피해로 HP 0 및 `RAIDMAP_E2E_SUMMON_PLAYER_KILL_HIT`까지 확인했다. 따라서 소환수 팀 복제/후보 조회 연결은 실제 Iris 환경에서 확인했다.

전체 검사는 통과하지 않았다. 마지막 처치 후 페이즈 전환과 소환수 제거(15:10:02.540 UTC)가 발생했고, 이어 수신된 두 항목 타깃 데이터는 디코딩에 실패했다(15:10:02.552 UTC). 공통 SkillContainer가 이를 `Malformed GAS TargetData` 보안 위반으로 기록해 `SummonPlayerKillSecurityChanged`로 종료됐다. 타깃 코덱은 ActorArray의 약한 Actor 참조가 유효하지 않으면 실패하므로, 서버 처치/제거와 지연된 스킬 타깃 데이터 사이 경합이 유력하다. 단, 디코딩의 실패 분기 자체를 계측한 것은 아니다. 전체 완료 proof는 생성되지 않았고, 자연 종료/보안 불변/양쪽 최종 재개까지 통과했다고 주장하지 않는다. NPC 정리 지연이나 보안 검사 완화로 결과를 우회하지 않았으며, 공통 GAS 수신 처리 수정은 기존 NPC 한정 범위 밖이므로 사용자 확인 전 진행하지 않는다.

## 92. 즉발형 스킬의 불필요한 입력 타깃 재전송 제거 (2026-10-22, 빌드 대기)

공통 GAS 경로 최소 수정에 대한 사용자 승인 후 입력 전송 경로를 확인했다. `PreReplicateAbilityInputPressed/Released`는 실행 정책과 무관하게 타깃을 갱신·전송했지만, Instant 정책은 두 입력 콜백을 사용하지 않는다. 이 때문에 즉발 타격 후의 키 해제만으로도 아직 클라이언트에 남아 있는 처치 대상 참조가 다시 전송될 수 있었다.

실행 정책에 `RequiresInputTargetRefresh()` 계약을 추가했다. 기본값은 true로 기존 입력 기반 정책과 확장 정책의 동작을 유지하고, Instant만 false를 반환한다. 컨테이너는 false인 정책에서 불필요한 타깃 갱신을 생략하되 입력 복제 자체는 계속 허용한다. 초기 타깃 설정, 실제 공격/GAS 피해, 서버 타깃 검증·코덱·보안 위반 처리, 소환수 정리 시점은 변경하지 않았다. NPC 검사 코드가 플레이어 스킬 종류를 판별하지 않도록 재조준 필요 여부는 실행 정책이 소유한다.

기존 `ProjectRPG.Security.Skill.AuthoritativeTermination` 검사에 Instant의 입력 통과/타깃 비갱신과 Charge/Holding의 재조준 계약 확인을 추가했다. 새 검사 클래스나 에셋은 만들지 않았다. **이번 변경의 전체 Editor 빌드·자동화 실행·리슨/전용 서버 재실행은 아직 확인하지 못했다.** 입력으로 재조준이 실제 필요한 정책의 대상 소멸 경합까지 모두 해결됐다고 주장하지 않는다. vtable 변경이 있으므로 이전 DLL의 실행 결과를 재사용하지 않고 정상 Editor 재빌드 후 확인한다.

후속 확인(2026-10-22): 사용자 Editor 빌드 이후 기존 `-SummonPlayerKill -ListenServer -SkipSDKSetup` 명령을 한 번 실행해 통과했다. 증거는 `Saved/Tests/BossMap/030077eec9e747c3ab566f7a663db845/BossMap.proof.json`과 세 프로세스 로그다. DLL SHA-256은 `6D021DF5D98FDD384CAF1F4C3067CFB43D4C4E165F069B0B01C5B84F97DC3EAE`이며 러너가 실행 전후 DLL/소환 기믹 에셋 6개의 불변을 확인했다. 양쪽 클라이언트의 적대 팀·wave·1/2·최종 페이즈 관찰, Guardian의 실제 이동/GAS 피해, 원격 AssultBlade 입력과 Guardian 직접 선택(`Match=1`), 서버 치명타, 자연 종료, 보안 위반 수 불변 및 보스 BT/이동 재개까지 확인했다. 종료 코드는 0이다. 이 실행에서 추가 런타임 코드는 수정하지 않았다.

첫 처치는 서버 GE 자극이고 마지막 Guardian은 HP 1, 무기/퀵슬롯 등록은 메모리 fixture다. 두 마리 모두의 플레이어 처치나 원래 HP에서의 밸런스·백엔드·Shipping 검증은 아니다. 새 빌드의 전용 서버 실행과 추가한 네이티브 자동화는 아직 확인하지 않았다. 차지·홀딩의 대상 소멸 경합까지 통과한 것으로 확대하지 않는다.

전용 서버 후속 확인(2026-10-22): 같은 DLL(`6D021DF5D98FDD384CAF1F4C3067CFB43D4C4E165F069B0B01C5B84F97DC3EAE`)로 `-SummonPlayerKill -SkipSDKSetup`을 한 번 실행해 통과했다. 증거는 `Saved/Tests/BossMap/b9373f88dcbd4cda9868b401b6d1e27a/BossMap.proof.json`과 세 로그다. 양쪽 Iris 클라이언트의 소환수 적대 팀·wave·1/2·최종 페이즈 동기화, Guardian 실제 GAS 피해, 원격 AssultBlade의 남은 Guardian 직접 선택(`Match=1`)과 치명타, 자연 종료, 보안 위반 수 불변 및 보스 전투 재개를 확인했다. 종료 코드는 0이며 러너의 DLL/소환 기믹 에셋 6개 불변 검사도 통과했다. 따라서 이번 수정의 마지막 소환수 처치 연결은 동일 빌드의 리슨·전용 서버에서 모두 확인됐다. HP 1/첫 서버 처치/장비·퀵슬롯 fixture 한계는 그대로이며, 위에서 추가한 네이티브 자동화는 아직 별도 실행하지 않았다. 추가 런타임 코드나 검사 클래스는 만들지 않았다.

## 93. 기믹 실패를 보스전 패배/재시도에 연결 (2026-10-22, 빌드 대기)

BossAI의 페이즈 제한시간은 이미 Wiped로 전환하지만, GameMode는 Dead 이벤트만 결과에 연결해 생존 참가자가 있으면 전투가 InProgress로 남을 수 있었다. 기존 서버 GameMode Tick에서 현재 보스의 Wiped 상태를 확인해 기존 `Defeated()` 경로로 연결했다. BossAI 상태 알림 스택 밖에서 결과를 처리하며, 기존 GameState 결과 복제·결과 UI·참가자 재시도 권한을 재사용한다. 플레이어 강제 사망, 별도 RPC/타이머/검사 클래스는 추가하지 않았다.

공백 검사는 통과했다. 단일 파일 MSVC 구문 검사는 변경하지 않은 `RPGDungeonFlowE2EProbe.h`와 생성 헤더의 줄 번호 불일치(현재 GENERATED_BODY 26/125/133, 생성본 25/123/131)로 완료하지 못했다. 해당 헤더/생성 파일은 수정하지 않았으며 정상 UHT/Editor 빌드가 필요하다. 반복 네트워크 검사는 실행하지 않았다.

## 94. 페이즈 태그 기반 BT 분기 (2026-10-22, 빌드/에셋 연결 대기)

`UBTDecorator_RPGBossPhase`를 추가했다. BT 가지에 `RPG Boss Phase`를 붙여 `RequiredPhase`를 지정하며, 기본은 정확한 태그 일치이고 `bExactMatch=false`이면 자식 페이즈 태그도 허용한다. 비어 있는 태그, 서버 권한/보스 컴포넌트 부재, InCombat 이외 상태는 원시 조건을 통과하지 않는다. Blackboard 페이즈 번호 대신 BossAI의 현재 서버 스냅샷을 조회한다.

AI별 노드 인스턴스가 보스 상태 변경을 구독하고 엔진의 조건부 flow abort로 분기를 재평가한다. 기본 Observer Aborts는 Both이며 BT의 표준 반전/abort 설정을 따른다. 가지 비활성화와 노드 인스턴스 제거 시 구독을 해제한다. 기존 서비스/BT/GAS 실행과 복제 계약은 유지하며 기존 에셋에 자동 삽입하지 않았다. 새 런타임 UCLASS이므로 UHT/Editor 빌드 후 사용할 수 있고, 컴파일·실제 에셋 연결은 아직 확인하지 않았다. 새 테스트 클래스와 네트워크 반복 실행은 추가하지 않았다.

## 95. 페이즈별 무작위 패턴 선택 (2026-10-22, 빌드 대기)

페이즈 데이터에 `bRandomizePatterns`를 추가했다. 기본 false는 기존 배열 우선순위 선택을 유지한다. true인 페이즈에서 `UBTTask_RPGBossAbility`의 `bUsePhasePatterns`를 사용하면 서버가 이미 부여됐고 실행 중이 아니며 GAS 활성화 조건을 통과한 패턴 중 하나를 동일 확률로 선택한다. 별도 패턴 프레임워크/공유 선택 상태 없이 reservoir sampling을 사용하며, 연속해서 같은 패턴이 나올 수 있다. 명시적 AbilityClass 실행과 기믹 중 BT 실행 차단은 변경하지 않았다.

GAS 조건 확인 중 페이즈/상태가 바뀌거나 ASC actor info가 사라지면 선택을 취소하고, 실제 활성화도 기존 GAS 경로를 거친다. 실행 중 복제 상태나 플레이어 스킬 코드는 바꾸지 않았고, 기존 에셋의 옵션은 켜지 않았다. 공백 확인만 수행했으며 새 UPROPERTY를 포함한 UHT/Editor 빌드 및 실제 실행은 아직 남아 있다. 새 테스트 클래스나 반복 네트워크 실행은 추가하지 않았다.

## 96. 패턴별 발동 거리/시야 조건 (2026-10-22, Editor 빌드 대기)

공통 보스 Ability에 `MinimumActivationDistance`, `MaximumActivationDistance`, `bRequireActivationLineOfSight`를 추가했다. 거리는 Pawn 중심 간 수평 cm 기준이고 각 0은 해당 경계 비활성화다. 최소/최대는 포함 경계이며 설정은 유한한 0..1000000 범위여야 하고 활성화된 최대 거리는 최소 거리보다 작을 수 없다. 시야는 서버 Controller의 기존 LOS 판정을 사용한다. 기본값은 거리 제한/시야 요구 없음으로 기존 에셋을 유지한다.

GAS CanActivate 및 캡처한 타깃의 실제 활성화 직전에서 조건을 확인하므로 우선순위/무작위 선택과 명시적 AbilityClass 실행에 함께 적용된다. 타깃 없는 기믹은 대상 거리/시야 판정에서 제외한다. 발동 후 지속 취소 조건으로 사용하지 않으며 기존 적중 프로필·피해 보안·쿨다운/커밋 경로는 유지했다. 새 테스트 클래스/에셋 변경/반복 네트워크 실행 없이 해당 cpp의 MSVC `/Zs`와 공백 검사만 통과했다. 새 프로퍼티를 반영한 UHT/Editor 빌드와 실제 실행은 아직 확인하지 않았다.

## 97. 소환 기믹 프리셋 BT에 페이즈 태그 조건 적용 (2026-10-23)

새 DLL의 네이티브 클래스/프로퍼티 포함을 확인하고 기존 `CreateRaidSwordManSummonMechanic.py`에 명시적 `-RPGApplySummonMechanicPhaseTagGate` 적용 옵션을 추가했다. 기존 번들 확인을 통과한 뒤 프리셋 BT의 돌진 진입 조건 하나만 BossPhaseIndex=2에서 복귀 페이즈 태그의 정확한 일치 조건으로 교체한다. 거리 조건·순서·태스크·Blackboard 참조와 기존 Observer Aborts=None은 보존한다. 기본 실행은 읽기 전용이며 이미 적용된 조건은 덮어쓰지 않는다. 편집 그래프가 있는 BT는 실행 노드와 그래프의 불일치를 막기 위해 적용을 거부한다.

에디터 적용을 완료해 `BT_RaidSwordManSummonMechanic.uasset` 하나만 저장했다. 프리셋 에셋 6개의 전후 해시 비교에서 다른 5개는 그대로였고, 소스 보스/맵/스킬 수치/선택 기본값은 변경하지 않았다. 저장 전 원본은 `Saved/Editor/RaidPhaseGate/4a77b8e701294b1788542613b4c81335/Before/BT_RaidSwordManSummonMechanic.uasset`에 있다. BT SHA-256은 기존 `92DE83AA20A6268F3EF848A0CA1F547400B8913DFC9FEBE93F7C8F6488D1BDFC`에서 `FC43550AA4301E58B3E160EC626B03F981BCC6313CFB0EB4756DCE8BA0E8784D`로 바뀌었다. Python 구문 검사와 적용 스크립트의 설정 확인/저장 마커를 확인했으며, 로그는 같은 디렉터리의 `apply.log`다.

최초 읽기 전용 실행은 Python에서 보호된 BTGraph 속성을 읽다가 실패했고 에셋 저장은 없었다. 엔진의 공개 ObjectIterator로 직접 소유한 EdGraph 유무를 확인하도록 고쳐 적용했다(`BT_GRAPH_PRESENT=0`). 새 테스트 클래스/반복 네트워크 실행은 추가하지 않았다. 이전 기믹 네트워크 통과는 변경 전 BT의 증거이며, 이번 에셋 변경 후 멀티플레이를 새로 검증한 것으로 간주하지 않는다.

## 98. 기본 SwordMan BT의 두 페이즈 분기 연결 (2026-10-23)

기존 `ConfigureRaidBossPhasePatterns.py`에 `-RPGApplyRaidBossPhaseTagGates` 옵션을 추가하고 적용했다. 기본 `BT_RaidSwordMan`의 페이즈 0/1 비교 조건 두 개를 데이터에 정의된 각 페이즈 태그의 정확한 일치 조건으로 교체했다. 두 분기의 순서·공격 태스크·회복 대기·거리 조건·Blackboard 참조와 Observer Aborts=Both를 보존했다. 이미 적용된 조건은 저장하지 않으며, 예상과 다른 조건이나 편집 그래프가 있으면 적용을 거부한다.

에디터 저장 및 SwordMan 폴더 하위 uasset 전후 해시 비교에서 `BT_RaidSwordMan.uasset` 하나만 변경됐다. 원본은 `Saved/Editor/RaidPhaseGate/ebd2bd4767b84ef5b4898d97a081ec2e/Before/BT_RaidSwordMan.uasset`, 적용 로그는 같은 실행 디렉터리의 `apply.log`다. BT SHA-256은 `950242414BB64D9B93709B3724CC7C20DBF53E9223D2C88D4E4150E0EDD467EC`에서 `FF87A080FA3F27DA6ADC70B71EE402050E43C67A2DF118BE1BA2793D642AEB69`로 바뀌었다. Python 구문/설정 확인·저장 마커를 확인했다. 정의·스킬 수치·맵은 변경하지 않았고 새 테스트 클래스/반복 네트워크 검사는 추가하지 않았다. 변경 후 실제 멀티플레이를 검증했다는 의미는 아니다.

## 99. 변형 보스 BT 및 복제 제작 경로 연결 (2026-10-23)

`ConfigureRaidSwordManVariantDash.py`에 `-RPGApplySwordManVariantPhaseTagGate` 옵션을 추가해 기존 변형 BT의 돌진 진입 조건을 페이즈 태그로 교체했다. 기존 400..800cm 거리 조건·스킬·순서·Observer Aborts=None은 보존했다. 이 스크립트로 새 돌진 분기를 만들 때도 태그를 사용한다. 공통 템플릿 확인은 기존 번호/새 태그 조건을 모두 지원하고, 소환 기믹 제작은 원본의 태그 조건을 그대로 복제한다. 기존 번호 조건만 기믹 삽입에 맞춰 재배치하므로 태그 조건에 int_value를 쓰지 않는다.

소환 기믹 프리셋과의 호환 확인을 저장 전에 완료했고, Raid 폴더 전체 uasset 해시 비교에서 `BT_RaidSwordManVariant.uasset` 하나만 변경됐다. 원본은 `Saved/Editor/RaidPhaseGate/a155b5f7bd244d6095230fe3c1b17f8b/Before/BT_RaidSwordManVariant.uasset`, 적용 로그는 같은 실행 디렉터리의 `apply.log`다. BT SHA-256은 `C8D015E836EF046F15DBEB75FF660D22B4ED3CD803837E6E19ACD12EF5ADAB7F`에서 `CBF3FE3991AA5A7F27EB1324BA8CD1AD977099745D44450D438BE6E883D91FBB`로 바뀌었다. 변경한 Python 3개 파일의 구문 검사를 통과했다. 기본·변형·소환 기믹 BT의 페이즈 태그 연결은 적용 완료했으며, 변경 후 멀티플레이 반복 검사나 새 테스트 클래스는 추가하지 않았다.

## 100. 돌진 Ability의 발동 거리 연결 준비 (2026-10-23, 에셋 적용 대기)

변형 돌진은 BT의 400..800cm 진입 분기 외에 페이즈 패턴 배열에도 등록되어 있다. 돌진 네이티브 조건은 최소 1cm만 요구하므로, BT 거리 조건만으로는 패턴 배열 선택의 거리 범위를 제한하지 못한다. 기존 `ConfigureRaidSwordManVariantDash.py`에 `-RPGApplySwordManDashActivationRange` 옵션을 추가해 해당 돌진 BP의 MinimumActivationDistance/MaximumActivationDistance에 기존 400/800 값을 연결하도록 구현했다. 이전 범위 (0,0)와 목표 범위만 허용하며 사용자 지정 범위는 덮어쓰지 않는다. 새 돌진 BP 생성 시에도 같은 범위를 설정한다. 피해·실제 돌진 최대 이동 거리·쿨다운·시야 요구·BT 조건은 바꾸지 않는다.

Python 구문 검사를 통과했다. 별도 dotnet/cl 빌드가 계속 실행 중이라 DLL 잠금 충돌을 피하기 위해 에디터는 시작하지 않았다. **이번 옵션으로 기존 BP를 저장한 것은 아직 아니다.** 빌드 종료 후 `GA_RaidSwordManVariant_Dash.uasset`을 백업하고 이 옵션만 적용해야 한다. 새 테스트 클래스나 반복 네트워크 검사는 추가하지 않았다.

후속 적용(2026-10-23): 빌드/에디터 프로세스 종료를 확인한 뒤 해당 옵션으로 기존 돌진 BP에 발동 거리 400/800을 적용했다. BP 컴파일 뒤 CDO 값 재확인과 저장을 완료했고, Raid 폴더 uasset 전후 해시 비교에서 `GA_RaidSwordManVariant_Dash.uasset` 하나만 변경됐다. 이 Ability를 공유하는 변형/소환 기믹 프리셋 모두 같은 GAS 발동 조건을 사용한다. 피해량·실제 이동 거리·쿨다운·시야 요구·BT 조건은 그대로다. 원본은 `Saved/Editor/RaidDashActivation/fe333894d5e54e1d816a1c4cd9293ea4/Before/GA_RaidSwordManVariant_Dash.uasset`, 적용 로그는 같은 실행 디렉터리의 `apply.log`다. 반복 네트워크 검사나 새 테스트 클래스는 추가하지 않았고, 변경 후 실전 동작을 새로 검증했다는 의미는 아니다.

## 101. 실제 보스전의 SwordMan 소환 기믹 연결 (2026-10-23)

`BP_BossBattleGameMode.BossClass`를 기존 기본형에서 검증된 `BP_RaidSwordManSummonMechanic` 프리셋으로 변경했다. 실제 전투는 일반 공격 → HP 70%에서 무적/일반 AI 정지 및 수호자 두 마리 소환 → 30초 안에 모두 처치하면 무적 해제/강화 패턴 재개로 이어진다. 기믹 시간 초과는 기존 서버 Wiped/보스전 패배 처리에 연결된다. 판단·소환·처치 집계·성공/실패는 기존 서버 BT/GAS/컴포넌트가 담당한다.

기존 `CreateRaidSwordManSummonMechanic.py`에 `-RPGSelectSwordManSummonEncounter`를 추가했다. 기존 프리셋 전체 연결을 확인한 후 알려진 기본형/목표 프리셋만 선택 대상으로 허용하며 GameMode 하나만 컴파일·저장한다. 다른 생성/마이그레이션 옵션과의 동시 실행은 거부하고, 이미 선택된 경우 저장하지 않는다. 초기 `ConfigureRaidSwordMan.py` 재실행도 선택된 기믹형을 보존하도록 맞췄고 `VerifyRaidBossMap.py`는 새 기본 선택을 확인한다.

실제 에디터 적용과 완료 마커를 확인했다. 비교한 Raid/SwordMan 에셋과 GameMode/bossmap 총 55개 중 `Content/Blueprints/GameMode/BP_BossBattleGameMode.uasset`만 변경됐다. 이전 SHA-256은 `E0C036AAAE74CFA593676DBCEDD04826CA533266961642BED44FB4362F89A320`, 이후는 `8D3F83CCE8A883BD9320003F07214E7CD617FD57CCD6192B433331CAC8179ED7`이다. 원본과 적용 로그는 `Saved/Editor/RaidEncounterSelection/ae4cd91c46f645d380a7b429ac7d32d9/Before/` 및 같은 실행 디렉터리의 `apply.log`에 있다.

기존 `RPGBossMapE2EProbeComponent`는 명시적으로 선택한 소환/변형 시나리오 외에는 기본형을 런타임 인스턴스에 지정하도록 수정했다. 실제 플레이용 기본 선택이 바뀌어도 기존 두 페이즈 검증의 전제가 바뀌지 않게 하기 위한 수정이다. 새 클래스나 검증 시나리오는 추가하지 않았다. 해당 cpp의 MSVC `/Zs`, Python 3개 및 PowerShell 러너의 구문 검사를 통과했다. 수정된 검증 코드를 실행하려면 Editor 빌드가 필요하며 러너가 구형 DLL을 차단한다. 전체 빌드/멀티플레이 재실행은 하지 않았고 이번 적용이 새 런타임 검증을 의미하지는 않는다.

## 102. 보스전 소환 기믹 안내 표시 (2026-10-23, Editor 빌드 대기)

기존 `URPGBossBattleInfoWidget`에 소환 준비/처치 진행/남은 시간 표시를 구현했다. `GetSummonObjectiveStatusText()`는 GameState의 현재 Boss를 기준으로 BossAI와 소환 목표 스냅샷을 읽고 전투 상태·EncounterId·PhaseIndex가 일치할 때만 안내를 반환한다. 별도 컴포넌트의 복제 도착 순서를 가정하지 않는다. 남은 시간은 복제된 PhaseEndServerTime과 GameState 서버 시계의 차이를 올림해 표시하며 0초가 되어도 클라이언트에서 성공/실패를 판정하지 않는다. 소환 준비 중에는 준비 안내를, 웨이브가 생성된 뒤에는 처치 수/목표 수를 표시한다.

`SummonObjectiveText`라는 선택적 UTextBlock 바인딩을 제공하며, 디자이너가 해당 라벨을 만들면 기존 배치를 사용한다. 라벨이 없으면 초기화 시 기존 위젯 트리에 중앙 상단 기본 라벨을 생성한다. Canvas 루트에는 슬롯을 추가하고, 다른 루트는 Overlay 안에 기존 트리를 보존해 배치한다. 기믹 외에는 Collapsed, 표시 중에는 HitTestInvisible이며 기존 NativeTick으로 현재 상태/시간을 다시 읽는다. UI 생성 시 현재 스냅샷으로 표시하므로 과거 이벤트 수신에 의존하지 않으며 종료/재시도/페이즈 불일치 시 이전 안내를 숨긴다. 네트워크 상태·RPC·타이머·테스트 클래스는 추가하지 않았다.

해당 cpp의 MSVC `/Zs` 구문 검사와 변경 파일의 공백 검사를 통과했다. 에셋은 수정하지 않았으며 UHT/Editor 전체 빌드와 실제 UI 배치·멀티플레이 실행 확인은 아직 하지 않았다. 기존 WBP_BossBattleInfoWidget에 새 네이티브 코드가 반영되려면 Editor 빌드가 필요하다.

## 103. 원격 클라이언트의 소환 기믹 UI 생성 연결 (2026-10-23, Editor 빌드 대기)

기존 GameMode의 시작 UI는 원격 클라이언트에서 실행되지 않으므로 102절의 표시 코드만으로는 원격 플레이어에게 기믹 안내가 생기지 않았다. 기존 PlayerController의 보스전 표시 갱신 경로에서 로컬 원격 클라이언트(`NM_Client`)가 복제된 InProgress 스냅샷을 확인하면 `URPGBossBattleInfoWidget` 네이티브 인스턴스를 만들고 AddToPlayerScreen으로 표시하도록 연결했다. 리슨 호스트/단독 플레이는 기존 GameMode UI를 사용한다.

네이티브 인스턴스는 WidgetTree에 루트가 없을 때 Canvas와 기본 안내 라벨을 생성한다. 원격에서 GameMode를 읽을 수 있는 구형 WBP 그래프를 실행할 필요 없이 같은 읽기 전용 상태/시간 표시 코드를 사용한다. 화면 전체 루트와 위젯은 SelfHitTestInvisible, 안내 라벨은 HitTestInvisible로 입력을 가로채지 않는다. 기존의 비 Canvas 루트용 Overlay도 SelfHitTestInvisible로 설정했다.

현재 스냅샷을 매번 확인하므로 중도 참가/LocalPlayer의 늦은 준비에도 안내가 생성된다. 승패 확정·재시도 대기·GameState 소실·Controller EndPlay에는 소유한 인스턴스를 제거한다. 중복 생성 없이 기존 인스턴스를 유지하고, 화면 추가가 아직 되지 않았다면 기존 갱신에서 다시 시도한다. 별도 RPC·복제 필드·타이머·새 클래스·테스트 시나리오는 추가하지 않았다. 변경한 PlayerController와 보스전 위젯 cpp 두 파일의 MSVC `/Zs` 및 공백 검사를 통과했다. 전체 Editor 빌드와 실제 멀티플레이 UI 확인은 아직 하지 않았다.

## 104. 열린 에디터의 MCP로 콤보형 SwordMan 조립 (2026-10-24)

현재 에디터의 UE 내장 MCP 서버(`http://127.0.0.1:8000/mcp`)에 연결했다. 이 세션의 기본 도구 목록에는 Unreal 도구가 없었지만, 에디터 서버에는 Asset/Object/Blueprint/BehaviorTree/Slate 도구가 등록되어 있었다. BT 전용 도구는 조회 기능만 제공하므로 새 노드 생성은 MCP Slate의 콘솔 입력으로 `Scripts/Editor/AssembleRaidSwordManCombo.py`를 실행했다. 에셋 조회, 노드/Blackboard 확인, 에디터 열기, 저장 및 GameMode 선택 변경에도 MCP 도구를 사용했다. 실행용 클라이언트와 조회/스크린샷 기록은 `Saved/Editor/RaidAnimationAssembly/`에 있다.

지정한 두 폴더의 레지스트리에서 AnimSequence 390개(`/Game/Assets`)와 66개(`/Game/GreatswordComboPack`)를 확인했다. Greatsword 원본은 보스와 스켈레톤이 다르다. BossyEnemy에 리타깃된 전체 콤보도 있지만 원본 개별 클립과 길이가 달라 임의 구간 분할은 적용하지 않았다. 이번 프리셋은 보스 스켈레톤과 동일한 `Boss_Attack_Swing_RM`, `Boss_Attack_Uppercut_RM`, `Boss_Attack_SwingAndSlam_RM`을 복사해 사용한다. 복사본만 인플레이스/루트 잠금 및 노티파이 제거를 적용했고, 각 몽타주와 서버 GAS 능력은 개별 에셋으로 분리했다. 베기의 소스 적중 시점 2.05초는 초기 설정이며 시각적 보정이 필요하다. 올려베기/내려찍기 소스 적중 시점은 기존 레이드 연출 설정에서 재사용했다.

새 폴더는 `/Game/Blueprints/Character/NPC/Raid/SwordManCombo`다. BP/AIC/DA/Startup/소환 GA/BB/BT 7개와 공격 GA·시퀀스·몽타주 각 3개, 총 16개를 생성했다. BT는 전투/타겟 확인 → 2페이즈 중거리 돌진 또는 추적 → 회전 → 페이즈별 행동으로 구성된다. 1페이즈에는 베기·올려베기·내려찍기를 연속 실행한다. HP 70%에는 기존 30초/수호자 2마리 기믹이 실행되고, 완료 후에는 장판을 시도하며 쿨다운 중에는 3연계와 강공격을 실행한다. 공격 GA 태스크는 총 9개이고, 공격 예고·적중·피해·취소는 기존 서버 GAS를 사용한다. 새 연계 능력은 피해 12/15/18, 예고 1.2/1.4/1.6초, 회복 각 0.35초, 발동 거리 최대 400cm로 초기 구성했다. 최종 연계 뒤 BT 회복은 0.6초다.

실제 BT 편집기를 열어 그래프도 생성·저장했다. UE의 새 그래프 Root 생성 과정이 프로젝트 기본 Blackboard를 선택하는 동작을 확인했으므로, 편집기 Root의 Blackboard도 새 `BB_RaidSwordManCombo`로 지정해 실행 데이터와 일치시켰다. MCP로 편집기 생성 전후 노드가 동일함을 확인했으며 최종 노드 29개와 새 Blackboard 연결을 확인했다. 생성 스크립트는 기존 에셋을 재구축하지 않고 기본 실행 시 연결을 확인한다.

MCP에서 PIE 비활성 및 GameMode의 미저장 변경 부재를 확인하고, `BP_BossBattleGameMode.BossClass`를 새 `BP_RaidSwordManCombo`로 변경·컴파일·저장했다. 기존 GameState와 전투 제한 100초는 보존했다. 원본 GameMode는 `Saved/Editor/RaidAnimationAssembly/BeforeGameMode/`에 백업했고 선택 전후 값은 `selected-encounter.json`에 기록했다. 초기 SwordMan 설정 스크립트도 선택된 콤보 프리셋을 보존하며, 맵 확인 스크립트는 새 선택을 확인하도록 맞췄다. 비교한 원본/기존 NPC 에셋 130개의 파일 해시는 모두 동일했다. 작성한 Python 구문 및 공백 검사는 통과했다. C++·새 테스트 클래스·맵은 변경하지 않았으며 새 프리셋의 PIE/멀티플레이 실전 검증·공격 연출/타이밍/밸런스 조정은 아직 하지 않았다.

## 105. 콤보 BT 저장 문제 수정 및 무적 플레이어 단계 테스트 (2026-10-25)

104절 이후 재로드에서 편집 그래프를 생성한 BT의 자식/데코레이터 참조가 소실되는 문제를 발견했다. 편집기의 전체 선택·복사 경로로 노드 소유권과 transient 플래그를 정리한 뒤 네이티브 저장을 수행해 canonical `BT_RaidSwordManCombo`와 AIC 연결을 복구했다. 재로드 검증은 노드 29개, 능력 태스크 9개, 누락 자식/데코레이터 0개였다. 조립 스크립트의 기본 저장은 안전한 실행 트리만 생성하며, 검증 시 null/공유 노드를 거부한다. 원본 백업은 `Saved/Editor/RaidAnimationAssembly/BTRepairBefore/`에 있다. 수정 후 단일 에디터 프로세스의 서버 역할 PIE와 원격 클라이언트 2개에서 추적·공격 몽타주·플레이어 HP 감소를 확인했고, 별도 세션에서 원격 재시도 RPC 이후 서버 전투 재개도 확인했다.

`Scripts/Editor/TestRaidBossProgression.py`는 PIE에만 적용되는 수동 관찰용 도구다. 서버 플레이어에게 기존 무적 GE의 개별 spec으로 `Shared.Status.Invincible`을 부여하고, 5초 후부터 보스 HP를 초당 10씩 줄인다. 무적/소환 목표 중에는 감소를 멈춘다. 기본 모드에서는 소환몹을 직접 처치하며, 선택 인자 `auto_clear_summons=True`는 현재 목표의 초기화된 소환 기록에 속하는 몹만 순서대로 모의 처치한다. HP 조절은 기존 transient 테스트 GE의 modifier를 동기적으로 교체·복원해 GAS 속성 변경 통지를 사용한다. 실제 플레이어 스킬 피해나 생존성·밸런스 검증을 대신하지 않는다. 에셋/CDO의 영구 변경, 새 C++ 테스트 클래스, 피해 보안 규칙 변경은 없다.

자동 모드 실행에서 보스 HP 300→210의 기믹 진입, 두 원격 클라이언트의 소환 UI 0/2→1/2, 전투 단계 0→1→2, HP 0의 승리 처리를 확인했다. 모든 관측 월드의 플레이어 HP 최솟값은 300이었다. 종료 후 서버와 두 클라이언트의 무적 태그 해제 및 테스트 GE의 원래 modifiers=0/executions=1 복원을 확인했다. 이후 자동 처치 대상 선택을 전체 NPC 탐색에서 목표별 소환 레지스트리 필터로 좁혔으며 이 마지막 변경은 구문/소스 확인만 수행했다. 실행 기록은 `Saved/Editor/RaidAnimationAssembly/invincible-progression.json`, 종료 확인은 `rehearsal-cleanup.json`이다. 테스트 PIE를 종료하고 이전 플레이 설정과 Entry 맵을 복원했다. 공격 타이밍·실제 플레이어 스킬 전투·별도 프로세스 배포 검증은 남아 있다.

## 106. 공용 스턴과 보스 기본 면역 (2026-10-25)

`ARPGBossCharacter`의 공용 `CombatControlComponent`는 기본적으로 스턴 면역이다. BP의 대상 설정에서 명시적으로 해제할 수 있으며, 임시 면역은 `Shared.Immunity.Control.Stun` 태그로 표현한다. 스턴의 원본은 GAS Duration GE와 태그이며 Blackboard에 제어 상태나 만료 타이머를 추가하지 않는다.

공용 AIController는 스턴 중 새 MoveTo를 거절하고 진행 중 이동을 중단한다. 회전 태스크와 전투 능력도 공용 제약을 따르며, 스턴 종료가 멈춘 Brain을 재개하지 않는다. 보스의 기본 면역을 해제한 모든 패턴 조합의 실전 회귀 검증은 별도다. 이번 실행 검증은 Player와 일반 Guardian의 양방향 스턴을 대상으로 했다.

구성·수명·설정 방법과 native 17개/2인 Iris/GPU 검증 범위는 [공통 피격·상태이상 계획 §16](COMBAT_REACTION_AND_STATUS_EFFECT_PLAN.md#16-공용-스턴-구현--2026-10-25)을 따른다. 통합 증거는 `Saved/CombatStun/Verification.proof.json`이다.

## 107. 공용 수평 넉백과 보스 기본 면역 (2026-10-25)

Player와 일반 NPC는 같은 CombatControlComponent·GAS Duration GE·CharacterMovement Root Motion Source로 수평 넉백을 처리한다. NPC 시작 데이터의 `AttackKnockbacks`에서 기존 공격 클래스에 거리/기간을 지정하며 부모 클래스 설정을 상속할 수 있다. 피격 방식 때문에 몬스터별 GA 자식 BP를 추가하지 않는다. 기존 공격의 기본 거리는 0cm다.

`ARPGBossCharacter`의 `bKnockbackImmune` 기본값은 true다. 스턴 면역과 독립적이며 대상 BP에서 명시적으로 해제하거나 `Shared.Immunity.Control.Knockback`으로 임시 면역을 부여할 수 있다. 면역 중에도 승인 피해는 유지한다. 넉백 중 MoveTo/회전/일반 전투 발동은 공통 제약을 따르며 종료가 멈춘 Brain을 재개하지 않는다. 교체·벽 충돌·정화·사망·Avatar 재사용은 해당 넉백의 GE/RMS/이동 승인만 정리한다.

구성·수명·검증 결과와 한계는 [공통 피격·상태이상 계획 §17](COMBAT_REACTION_AND_STATUS_EFFECT_PLAN.md#17-공용-넉백-구현--2026-10-25)을 따른다. 실행 검사는 Player/일반 Guardian을 대상으로 하며, 보스 면역을 해제한 모든 패턴 조합과 에어본·다운·기상의 검증을 대신하지 않는다.

최신 공유 소스 통합과 현재 프로젝트의 native 54개·2인 Iris/GPU 넉백(80~160ms/5% 손실)·스턴·일반 피격 회귀는 [같은 계획 §18](COMBAT_REACTION_AND_STATUS_EFFECT_PLAN.md#18-최신-공유-소스-통합과-현재-프로젝트-검증--2026-10-26)에 기록했다. 현재 적용 증거는 `Saved/CombatKnockbackIntegration/Verification.proof.json`이다.

## 108. 공용 에어본·다운·자동 기상과 보스 면역 (2026-10-26)

같은 공격 설정의 `Height`가 양수면 CMC JumpForce RMS로 공중 이동하고 실제 서버 착지 후 Downed/GettingUp GE로 전환한다. Player와 일반 NPC는 같은 CombatControlComponent를 사용하며, NPC `AttackKnockbacks`의 기존 공격 클래스 설정에 거리·높이·다운/기상 기간을 넣는다. 수평 넉백은 높이 0으로 유지한다. 단계마다 GA 자식 BP를 추가하지 않는다.

`ARPGBossCharacter`는 `bAirborneImmune=true`가 기본이다. `Shared.Immunity.Control.Airborne`은 스턴·수평 넉백 면역과 독립이며 승인 피해 자체를 취소하지 않는다. 공중 이동·다운·기상 동안 일반 행동/MoveTo/회전은 공통 제약을 따른다. 실제 착지, 정화·억제·교체·사망·재사용은 자기 GE/RMS/보안 승인/몽타주만 정리하며 외부 스턴·이동과 Brain 정지를 보존한다. 기상 입력·무적·쿨다운은 추가하지 않았다.

실행 방법, 증거와 한계는 [공통 계획 §19](COMBAT_REACTION_AND_STATUS_EFFECT_PLAN.md#19-공용-에어본실제-착지다운자동-기상--2026-10-26)를 따른다. Guardian은 전용 엎어진 다운·기상 아트가 없어 웅크림·회복 클립을 임시로 사용한다. 보스 면역을 해제한 모든 패턴 조합이나 최종 애니메이션 품질을 검증한 것은 아니다.

## 109. 공용 둔화와 보스 기본 면역 (2026-10-26)

Player/일반 NPC는 같은 CombatControlComponent의 승인 적중 둔화 GE를 사용한다. NPC `AttackSlows`는 기존 공격 클래스의 부모 설정을 상속하며 Percent=0으로 비활성화한다. MoveSpeed는 기본 배율 1, MoveSpeedPercent는 백분율로 공용 CMC가 소비한다. 가장 강한 감소만 적용하고 약한 효과의 기간은 별도 유지하므로 강한 효과 종료 후 남은 약한 효과가 이어진다. 종료가 다른 버프나 기본 속도를 덮지 않는다.

보스의 `bSlowImmune` 기본값은 true다. `Shared.Immunity.Movement.Slow`는 스턴·변위 면역과 독립이며 승인 피해를 취소하지 않는다. 면역 해제 시 둔화는 일반 걷기/추적 속도에 적용되지만 공격 취소·Brain 정지·경고/패턴 시간 변경·RMS 이동 속도 변경은 수행하지 않는다. 공격자가 사라져도 이미 부여된 둔화는 대상 GE의 기간을 따른다. 자기 생명의 GE만 정리하며 보스 기믹/다른 출처의 제약을 해제하지 않는다.

native 65개와 현재 프로젝트 2인 Iris/GPU·지연/손실·늦은 참가 검증, 상태 UI와 실제 이동 범위는 [공통 계획 §20](COMBAT_REACTION_AND_STATUS_EFFECT_PLAN.md#20-공용-둔화와-이동-속성-소비--2026-10-26), `Saved/CombatSlow/Verification.proof.json`에 기록했다. 보스 전체 패턴 조합이나 여러 PC/Shipping 검증을 대신하지 않는다.

## 110. 공용 면역 분류와 스킬 실행 수명 (2026-10-26)

CombatControlComponent의 공통 면역 판정은 HitReaction, Displacement(넉백/에어본), StatusEffects(현재 스턴/둔화) 및 개별 종류를 구분한다. 보스의 기존 4종 기본 면역은 유지하고 일반 경직 기본 면역을 추가했다. 면역은 승인 피해를 취소하거나 이미 걸린 상태를 정화하지 않는다. 개별 기존 태그도 계속 지원한다.

NPC 시작 데이터의 `AbilityImmunities`는 가장 가까운 기존 공격 클래스 설정을 사용한다. 같은 GA를 재사용하며 몬스터별 면역 GA BP를 만들지 않는다. 일반 GA의 활성화 및 SkillContainer의 성공 commit에 연결된 서버 GE가 선택한 면역 태그를 소유한다. 실행별 GUID/GE 핸들로 중첩을 합성하며 종료·취소·사망·재사용에서 자기 면역만 제거한다. 기존 비취소 능력 보호와 전멸/기믹 제어는 유지한다.

최초 분리 검증 뒤 별도 던전 검사의 모듈 사용 때문에 통합을 보류했으며, 이후 아래 111절에서 현재 프로젝트 적용·재검증을 완료했다. 구성과 최초 검증은 [공통 계획 §21](COMBAT_REACTION_AND_STATUS_EFFECT_PLAN.md#21-공용-면역-분류와-실행-단위-스킬-면역--2026-10-26)을 따른다. 몽타주 구간별 면역과 원격 예측 운영 스킬·보스 전체 패턴 검증은 별도다.

## 111. 공용 면역의 현재 실행 모듈 통합 완료 (2026-10-26)

공유 프로젝트 전체 Editor 타깃 빌드의 `6428E48045F7818417FE8A3684FCB9F82DF24B7722F9BEA28D097A46E41BE1F6` DLL에서 현재 프로젝트 native 72개와 원격 2인 Iris(각 NetDriver 80~160ms/5% 손실, 활성 중 늦은 참가)를 직접 통과했다. Source 947개를 포함한 입력 1610개의 검사 전후 해시가 같고, 실제 로드한 현재 프로젝트 DLL 경로와 해시도 확인했다. 별도 분리 DLL로 덮어쓰거나 다른 검사를 중단하지 않았다.

기본 경직 면역과 데이터로 선택하는 실행 단위 면역을 현재 프로젝트에서 사용할 수 있다. 피해와 제어 면역의 독립성, 중첩 하나의 종료, 취약한 종류의 스턴 적용, 사망 정리와 양방향 복제를 검사했다. 전체 운영 보스 패턴/원격 예측 스킬/Shipping 검증을 대신하지 않는다. [공통 계획 §22](COMBAT_REACTION_AND_STATUS_EFFECT_PLAN.md#22-공용-면역의-현재-프로젝트-통합재검증-완료--2026-10-26), `Saved/CombatImmunityIntegration/Verification.proof.json`에 기록했다.

## 112. 공용 화상과 보스 전투 수명 (2026-10-26)

Player/NPC는 공용 CombatControlComponent의 화상을 사용한다. 기존 공격/SkillDefinition의 `Burn` 또는 NPC 시작 데이터 `AttackBurns`에 설정하며 새 몬스터별 GA BP를 요구하지 않는다. 부여 시 확정한 고정 최종 피해를 기간 GE가 살아 있는 서버 회차에서만 실행한다. 기존 근접 피해 guard를 완화하거나 과거 HitResult를 재사용하지 않는다. 화상은 공격 취소·사거리 이탈·출처 스턴 이후에도 유지하지만 출처/대상의 사망·권한·Avatar/생명·Controller 변경 때 끝난다.

출처와 대상 중 보스가 있으면 양쪽의 EncounterId와 InCombat을 검증한다. 전멸·리셋·다음 전투로 이전 화상을 넘기지 않는다. 실제 HP 감소분만 원래 출처와 캡처한 전투 번호로 기존 AddThreat에 전달하며, 기존 어그로 자격/거리 규칙과 사망·보상 경로를 유지한다. 보스의 기존 제어/둔화 기본 면역은 유지한다. 화상은 기본적으로 허용하며 StatusEffects 그룹 또는 Burn 개별 면역 데이터로 새 부여를 막을 수 있다.

독립 GE 중첩 최대 8개와 회차 예산, 정확한 자기 효과 정화, 억제/무적 중 회차 폐기, 복제 상태 라벨을 연결했다. 현재 DLL `3C1CD079FEE5D88B77F99BEB2059B74CD4955AF49600EAC677C8DB6F1D75CCBA`에서 화상 7개를 포함한 native 79개와 2인 Iris(80~160ms/5% 손실·활성 중 늦은 참가)가 통과했다. 보스 전투/어그로는 집중 native 검증이고 네트워크 피해 대상은 실제 Player/Guardian fixture다. 운영 보스 전체 패턴이나 최종 연출을 검증한 것으로 확대하지 않는다. [공통 계획 §23](COMBAT_REACTION_AND_STATUS_EFFECT_PLAN.md#23-공용-화상-지속-피해--2026-10-26), `Saved/CombatBurn/Verification.proof.json`에 범위와 증거를 기록했다.

## 113. 몽타주 구간별 공용 면역 (2026-10-26)

기존 Player/NPC GAS 몽타주에 `RPG Combat Immunity Window` Notify를 배치해 같은 면역 비트를 설정한다. 기본 3은 경직/변위만 보호하며 스턴·둔화·화상은 독립이다. 서버 ASC가 구간을 복사하고 실제 몽타주 위치와 재생 인스턴스/능력 실행 GUID/활성화 시 생명·보스 전투 번호로 검증한다. 보스 전멸·리셋·다음 전투에 이전 능력의 구간을 재사용하지 않는다. 공유 Notify 객체에는 Actor나 GE 핸들을 저장하지 않으며 새 몬스터별 GA BP를 만들지 않는다.

겹치는 구간·실행 전체 면역·외부 GE를 보존하고, pause/속도 0/블렌드아웃/취소·재발동/생명·AnimInstance 교체에서 자기 구간만 정리한다. 구간 데이터가 있는 재생의 서버 메시가 `OnlyTickPoseWhenRendered`라면 최소 `OnlyTickMontagesWhenNotRendered`로 올린다. 포즈/Root Motion을 수동으로 진행하거나 종료 때 과거 설정을 덮어 복구하지 않는다.

검증 DLL `82021ADCA4DD7B9A0923E4CA935EA9C082245401AF24083884F0B875F87301B1`에서 native 85개와 원격 2인 Iris(80~160ms/5% 손실·구간 중 늦은 참가)가 통과했다. 전투 검증 입력 변화는 0이며 계속 갱신된 로비/인벤토리 검증 파일은 별도 변경 목록에 남겨 전체 소스 동기화와 구별한다. 네트워크는 Player/Guardian의 실제 몽타주를 서버 전용 네이티브 능력으로 재생하는 개발 fixture이며 원격 예측 운영 스킬 전체·운영 보스 전체 패턴/최종 연출/Shipping 검증은 아니다. 운영 구간 배치는 콘텐츠 작업으로 남긴다. [공통 계획 §24](COMBAT_REACTION_AND_STATUS_EFFECT_PLAN.md#24-몽타주-구간별-공용-면역--2026-10-26), `Saved/CombatMontageImmunity/Verification.proof.json`을 따른다.

## 114. 기간형 일반 경직과 공용 제어 우선순위 (2026-10-27)

Player/NPC의 기존 CombatControlComponent와 GAS에 짧은 일반 경직을 연결한다. 플레이어 부여·SkillDefinition의 `Stagger`, NPC 시작 데이터의 `AttackStaggers`에 0 또는 0.05~2초를 설정한다. 기존 공격 클래스의 부모 설정을 재사용하며 몬스터별 경직 GA BP는 추가하지 않는다. 기본값은 0이다.

기간 GE의 Staggered 태그를 기존 IsActionBlocked에 연결해 이동·입력·전투 능력과 AI MoveTo/회전을 제약한다. 중력과 외부 이동, 패시브/사망 능력, 기존 취소 불가 정책을 보존한다. 실제 스턴·넉백·에어본이 적용되면 자기 일반 경직을 정리한다. 보스의 기존 HitReaction 기본 면역과 실행/몽타주 구간의 공용 경직 면역을 재사용하고 StatusEffects 면역과 구분한다. HP 피해와 면역은 독립적이다.

승인 적중에서 설정을 복사하고 생명/ASC를 검증한다. 경직이 부여된 뒤 공격자가 사라져도 자기 기간은 유지한다. 대상 사망·재사용에는 자기 효과만 정리한다. 독립 중첩 최대 16개, 정확한 핸들 해제와 외부 효과 보존, 활성 상태/시각/방향 복제로 늦은 참가를 지원한다. 운영 기간과 최종 경직 애니메이션은 콘텐츠 작업이다.

현재 프로젝트 빌드 DLL `04D85273D156439A591BBF20D945F2A28ABB86BD85A9DC51B1C31A3AF723B690`에서 native 91개와 2인 Iris(80~160ms/5% 손실·경직 중 늦은 참가)가 통과했다. Source 976개 포함 입력 1639개 변화는 0이다. 네트워크는 기존 Player/Guardian 공격의 서버 승인 피해 fixture이며 모든 운영 보스 패턴/원격 예측 스킬/최종 포즈/화면 픽셀/Shipping 검증은 아니다. [공통 계획 §25](COMBAT_REACTION_AND_STATUS_EFFECT_PLAN.md#25-기간형-일반-경직--2026-10-27), `Saved/CombatStagger/Verification.proof.json`에 방법과 검증 범위를 기록한다.

## 115. 대표 공격의 경직 데이터와 부여 검증 (2026-10-27)

기존 대검 무기 부여 데이터에 기본 공격 경직 0.25초, Guardian 시작 데이터에 첫 근접 공격 경직 0.35초를 초기 조정값으로 저장했다. 다른 입력/능력/피해 설정을 보존하고 원래 GA를 사용한다. 공용 면역과 보스 기본 면역은 기존 판정을 따른다. 저작·재로드 도구는 `Scripts/Editor/ConfigureSharedStaggerAttacks.py`이며 기본 실행은 읽기 전용이다.

`-AuthoredStagger` 검사는 원래 장착/시작 데이터의 부여를 조회해 실제 기간과 소유자/관찰자의 몽타주 인스턴스를 확인한다. 승인 피해 이벤트를 반복할 수 있는 개발 fixture이며 짧은 모든 타격의 표시 보장은 아니다. native 91개와 실제 데이터 2인 검사, 별도의 80~160ms/5% 손실·늦은 참가 회귀를 같은 현재 프로젝트 검증 DLL `4C076D08EA921387927BE654F4A590BA4362E714F429C9B93FF4C59836CFDC19`에서 통과했다. 테스트 월드 EndPlay 누락은 공유 정리 함수로 수정했다.

이후 별도 세션/스킬 작업이 소스·자산·실행 DLL을 계속 갱신하므로 전체 최신 DLL 회귀와 소스 동일성은 주장하지 않는다. 전체 차이와 추가 DLL의 실제 데이터 검사 결과는 [공통 계획 §26](COMBAT_REACTION_AND_STATUS_EFFECT_PLAN.md#26-대표-공격의-경직-데이터-연결--2026-10-27), `Saved/CombatStaggerContent/Verification.proof.json`에 기록한다. 최종 밸런스·픽셀/전용 연출과 전체 보스 패턴 검증은 후속이다.

## 116. 에어본의 경사면·낭떠러지 실제 착지 검증 (2026-10-27)

공용 Player/NPC 에어본에 20도 경사면과 낮은 발판 착지 회귀를 추가했다. 이동 곡선이 끝나도 실제 착지 전에는 Airborne·중력 낙하와 이동 보안 승인이 유지되고, CMC의 실제 착지부터 다운/기상이 시작됨을 확인한다. 기존 서버 전투 규칙을 수정하지 않고 native 두 개와 `-AirborneTerrain` 개발 검사를 추가했다. 복제되는 임시 지형으로 양방향 승인 피해·소유자/관찰자 표시·입력 차단·회복/수렴을 검사하며 맵을 저장하지 않는다.

분리 DLL `FDA35EAEC294B885DE788DDEF8611363ACFFCF38B92DA2E250A1B4C2BFC0F3BB`에서 native 93개와 2인 Iris(80~160ms/5% 손실)가 통과했다. 경사면은 곡선 1.80702초보다 이른 1.72703초에 착지했고, 낭떠러지의 아래 바닥은 1.98985초에 착지했다. 이동 보안 위반은 0이다. 복사한 Source·Config·BP 입력 1379개는 유지했지만 다른 Content/Plugins는 공유하며 루트 소스/DLL도 동시 작업이 계속 갱신한다. 따라서 이번 결과는 분리 런타임 검증이고 최신 루트 DLL 전체 회귀 완료는 아니다.

방법·실패 설정 수정·기동 경고·증거는 [공통 계획 §27](COMBAT_REACTION_AND_STATUS_EFFECT_PLAN.md#27-에어본-경사면낭떠러지-착지-검증--2026-10-27), `Saved/CombatAirborneTerrain/Verification.proof.json`을 따른다. 에어본 중 늦은 참가·급경사/이동 발판·운영 보스 전체 전투·GPU 최종 연출/Shipping은 후속이다.

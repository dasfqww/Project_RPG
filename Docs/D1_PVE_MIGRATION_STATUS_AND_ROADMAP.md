# D1 PvE 시스템 이식 현황 및 전체 작업 로드맵

> 마지막 갱신: 2026-07-20
> 대상 프로젝트: `C:\UE5\Project_RPG`
> 참고 소스: `D1_SourceCode/`
> 현재 프로젝트 소스: `Source/Project_RPG/`
> 목표: 로스트아크 스타일의 PvE 액션 RPG 제작

## 1. 문서 목적

이 문서는 `D1_SourceCode/`의 시스템을 현재 `Project_RPG`에 이식하는 작업을 중단 없이 이어가기 위한 기준 문서다.

다음 내용을 한곳에서 관리한다.

- 지금까지 실제로 완료한 작업
- 부분적으로만 동작하거나 임시 처리된 작업
- 아직 이식하지 않은 D1 시스템
- 현재 확인된 기술 부채와 위험 요소
- 앞으로 진행할 전체 작업 순서
- 각 단계의 완료 조건과 검증 방법

이 문서에서 사용하는 상태는 다음과 같다.

| 상태 | 의미 |
|---|---|
| 완료 | 소스 구현, 빌드, 관련 에셋 로딩까지 확인됨 |
| 부분 완료 | 핵심 구조는 있으나 원본 기능 일부 또는 런타임 검증이 부족함 |
| 임시 호환 | D1 에셋을 당장 사용할 수 있도록 만든 대체 구현이며 추후 정식 이식으로 교체해야 함 |
| 검증 필요 | 빌드 또는 에셋 로딩은 됐지만 PIE/멀티플레이 실전 검증이 필요함 |
| 미착수 | 아직 현재 프로젝트용 구현이 없음 |

## 2. 최종 방향

최종 목표는 D1 파일을 폴더째 복사하는 것이 아니다. D1의 유용한 설계와 동작을 현재 프로젝트 구조에 맞게 흡수하는 것이다.

파일 또는 시스템마다 다음 세 가지 중 하나로 처리한다.

1. 현재 프로젝트에 같은 기능이 있으면 기존 코드에 통합한다.
2. Lyra 또는 D1 전용 의존성이 있으면 현재 RPG 타입으로 치환하여 이식한다.
3. 현재 PvE 목표에 필요하지 않거나 중복되는 코드는 제외한다.

작업 시 지켜야 할 핵심 원칙은 다음과 같다.

- 현재 프로젝트의 기존 시스템을 무시하고 D1 코드를 덮어쓰지 않는다.
- PvP보다 PvE 몬스터·보스 전투를 우선한다.
- 피해와 상태이상은 서버 권한을 기준으로 처리한다.
- 플레이어끼리의 아군 오폭은 기본적으로 차단한다.
- D1 Blueprint와 데이터 에셋의 직렬화 이름은 가능한 한 보존한다.
- 임시 호환 코드는 반드시 문서에 표시하고 정식 구현 후 제거한다.
- 각 계층은 빌드, 에셋 로딩, PIE 순서로 검증한다.

## 3. 현재 프로젝트에서 확인한 기반 구조

현재 프로젝트에는 다음 기반이 이미 존재한다.

- Gameplay Ability System
  - `URPGGameplayAbility`
  - `URPGAbilitySystemComponent`
  - `URPGAttributeSet`
- 플레이어 및 NPC 캐릭터
  - `ARPGBaseCharacter`
  - `ARPGPlayer`
  - NPC 파생 클래스
- 전투 컴포넌트
  - `UPawnCombatComponent`
  - `UPlayerCombatComponent`
  - `UNPCCombatComponent`
- 현재 무기 액터
  - `ARPGWeaponBase`
  - `ARPGPlayerWeapon`
  - `ARPGGreatSword`
- 장비 데이터 및 상태 컴포넌트
  - `URPGEquipmentComponent`
  - `URPGEquipComponent`
- 아이템과 Manifest/Fragment 구조
  - `URPGItemBase`
  - `FItemManifest`
  - `FEquipmentFragment`
- GameplayTag 기반 입력
  - `URPGInputComponent`
  - `UDataAsset_InputConfig`
- UI 및 HUD
  - `ARPGHUD`
  - 현재 프로젝트의 일반 UMG/Viewport 구조

이 기반은 D1/Lyra 구조와 완전히 같지 않다. 따라서 D1 계층마다 위 타입 중 무엇을 재사용할지 결정해야 한다.

## 4. 지금까지 완료한 작업

### 4.1 프로젝트 및 플러그인 호환 기반

상태: **부분 완료**

- Gladiator GameFeature 콘텐츠를 현재 프로젝트에서 마운트하고 읽을 수 있도록 구성했다.
- D1/Lyra 타입을 현재 프로젝트 타입으로 연결하는 Core Redirect를 추가했다.
- Experience, GameData, AbilitySet, 각종 데이터 에셋이 현재 프로젝트 클래스로 로드될 수 있도록 호환 클래스를 구현했다.
- 현재 프로젝트 입력 구조와 D1 입력 태그 사이의 리다이렉트를 추가했다.

주요 설정 파일:

- `Config/DefaultEngine.ini`
- `Config/DefaultGame.ini`
- `Config/DefaultGameplayTags.ini`
- `Project_RPG.uproject`

현재 등록된 주요 리다이렉트 범위:

- Lyra Experience 계열
- Lyra/D1 GameData 및 AbilitySet
- LyraGameplayAbility
- D1 직업 스킬 부모 클래스 7종
- D1 타기팅 액터
- D1 투사체 및 AOE 액터
- D1 Combat/Vital AttributeSet
- D1 클래스 선택 UI
- D1 데이터 에셋 및 관련 구조체/열거형
- D1 카메라 모드 일부

### 4.2 Experience 및 게임 데이터 호환

상태: **부분 완료**

구현한 현재 프로젝트 타입:

- `URPGExperienceDefinition`
- `URPGExperienceActionSet`
- `URPGUserFacingExperienceDefinition`
- `URPGLobbyBackground`
- `URPGGameData`

관련 파일:

- `Source/Project_RPG/Public/DataAsset/Definition/RPGExperienceDefinition.h`
- `Source/Project_RPG/Private/DataAsset/Definition/RPGExperienceDefinition.cpp`
- `Source/Project_RPG/Public/DataAsset/Definition/RPGExperienceActionSet.h`
- `Source/Project_RPG/Private/DataAsset/Definition/RPGExperienceActionSet.cpp`
- `Source/Project_RPG/Public/DataAsset/Definition/RPGGameData.h`
- `Source/Project_RPG/Private/DataAsset/Definition/RPGGameData.cpp`
- `Source/Project_RPG/Public/DataAsset/Definition/RPGLobbyBackground.h`
- `Source/Project_RPG/Private/DataAsset/Definition/RPGLobbyBackground.cpp`
- `Source/Project_RPG/Public/DataAsset/Definition/RPGUserFacingExperienceDefinition.h`
- `Source/Project_RPG/Private/DataAsset/Definition/RPGUserFacingExperienceDefinition.cpp`

남은 작업:

- Lyra Experience 전체 활성화/비활성화 수명주기를 현재 GameMode에 완전히 통합
- Experience Action의 실제 런타임 적용 범위 검증
- 맵 전환과 Experience 선택 흐름 정리

### 4.3 D1 데이터 에셋 호환

상태: **완료에 가까운 부분 완료**

구현한 타입:

- `URPGAssetData`
- `URPGCheatData`
- `URPGCharacterData`
- `URPGClassData`
- `URPGElectricFieldPhaseData`
- `URPGGladiatorItemData`
- `URPGMonsterData`
- `URPGUIData`
- 관련 구조체 및 열거형

관련 파일:

- `Source/Project_RPG/Public/DataAsset/Definition/RPGGladiatorData.h`
- `Source/Project_RPG/Private/DataAsset/Definition/RPGGladiatorData.cpp`
- `Source/Project_RPG/Public/DataTable/RPGItemData.h`

검증된 D1 데이터 에셋:

- `AssetData_GladiatorGame`
- `CharacterData_GladiatorGame`
- `CheatData_GladiatorGame`
- `ClassData_GladiatorGame`
- `ElectricFieldPhaseData_GladiatorGame`
- `GameData_GladiatorGame`
- `ItemData_GladiatorGame`
- `LobbyBackground_GladiatorGame`
- `MonsterData_GladiatorGame`
- `UIData_GladiatorGame`

남은 작업:

- 직업별 `DefaultItemEntries`를 실제 현재 장비/인벤토리 시스템에 적용
- D1 ItemTemplate 클래스의 런타임 생성과 현재 `URPGItemBase` 변환
- 몬스터 데이터와 현재 AI 스폰 흐름 연결

### 4.4 D1 AbilitySet 호환

상태: **완료**

구현한 기능:

- GameplayAbility 부여
- GameplayEffect 부여
- AttributeSet 부여
- 부여한 핸들 저장 및 회수
- 입력 태그를 AbilitySpec의 동적 소스 태그에 연결
- 직업 변경 시 이전 직업 스킬 제거
- 리스폰 시 선택한 직업 스킬 재부여

관련 파일:

- `Source/Project_RPG/Public/Ability/RPGAbilitySet.h`
- `Source/Project_RPG/Private/Ability/RPGAbilitySet.cpp`
- `Source/Project_RPG/Public/Player/RPGPlayerState.h`
- `Source/Project_RPG/Private/Player/RPGPlayerState.cpp`

### 4.5 직업 선택 및 스킬 부여

상태: **부분 완료 / PIE 검증 필요**

구현된 직업:

- Fighter
- Swordmaster
- Barbarian
- Wizard
- Archer

구현한 기능:

- 직업 선택 서버 RPC
- 선택 직업 복제
- 직업별 AbilitySet 부여
- 직업 변경 시 이전 AbilitySet 회수
- Pawn 변경 및 리스폰 시 재부여
- 직업 선택 UI에서 스킬 아이콘, 이름, 설명 표시
- 직업 미선택 상태에서 HUD가 선택 UI를 자동 표시

관련 파일:

- `Source/Project_RPG/Public/UI/Class/RPGClassSelectionWidget.h`
- `Source/Project_RPG/Private/UI/Class/RPGClassSelectionWidget.cpp`
- `Source/Project_RPG/Public/UI/HUD/RPGHUD.h`
- `Source/Project_RPG/Private/UI/HUD/RPGHUD.cpp`
- `Source/Project_RPG/Public/Player/RPGPlayerState.h`
- `Source/Project_RPG/Private/Player/RPGPlayerState.cpp`

현재 UI 처리 방식:

- 현재 프로젝트는 Lyra CommonGame UI Policy/RootLayout을 완전히 사용하지 않는다.
- 따라서 직업 선택 위젯은 현재 `ARPGHUD`의 Viewport 기반 UI 흐름에 맞춰 표시한다.
- 표시 중에는 UI 입력과 마우스 커서를 활성화하고, 종료 시 게임 입력으로 복귀한다.

남은 작업:

- PIE에서 실제 버튼 선택, 서버 반영, 재접속, 리스폰 검증
- 직업 재선택 정책 결정
- 선택한 직업의 기본 장비 및 외형 적용
- 저장 데이터에 선택 직업 기록

### 4.6 직업 스킬 입력 연결

상태: **완료**

검증 결과:

- 각 직업 첫 번째 스킬: `InputTag.Skill1`
- 각 직업 두 번째 스킬: `InputTag.Skill2`
- 현재 `DA_InputConfig`의 `IA_Skill1`, `IA_Skill2`와 일치

직업별 연결:

| 직업 | Skill1 | Skill2 |
|---|---|---|
| Fighter | Shield Bash | Swift Blade |
| Swordmaster | Whirlwind Slash | Berserk |
| Barbarian | Ground Breaker | Iron Will |
| Wizard | Tears of the Fallen | Soul Bind |
| Archer | Piercing Shot | Vampiric Focus |

### 4.7 AttributeSet 및 GameplayEffect 호환

상태: **부분 완료**

현재 `URPGAttributeSet`에 추가하거나 연결한 D1 계열 속성:

- Health/MaxHealth → CurrentHealth/MaxHealth
- Mana/MaxMana → CurrentMana/MaxMana
- Stamina/MaxStamina → CurrentStamina/MaxStamina
- BaseDamage/Strength → Attack
- Defense
- MoveSpeedPercent
- AttackSpeedPercent
- DrainLifePercent
- DamageReductionPercent
- ActiveEffectDuration

추가 구현:

- D1 Active Effect Duration Execution 호환
- D1 SetByCaller 피해 태그 호환
- D1 GameData의 피해 GameplayEffect 로딩

관련 파일:

- `Source/Project_RPG/Public/Attribute/RPGAttributeSet.h`
- `Source/Project_RPG/Private/Attribute/RPGAttributeSet.cpp`
- `Source/Project_RPG/Public/GameplayEffect/RPGActiveEffectDurationExecution.h`
- `Source/Project_RPG/Private/GameplayEffect/RPGActiveEffectDurationExecution.cpp`
- `Config/DefaultEngine.ini`
- `Config/DefaultGameplayTags.ini`

남은 작업:

- PvE용 최종 피해 공식
- 공격력, 방어력, 치명타, 피해 감소 적용 순서
- 보스 피해 감소와 상태이상 면역
- Drain Life의 실제 회복 처리
- 사망 이벤트와 보상/리스폰 흐름 통합

### 4.8 Health Component 호환

상태: **부분 완료**

구현한 기능:

- `URPGHealthComponent`
- `GetHealth`
- `GetMaxHealth`
- 정규화된 체력 조회
- D1/Lyra Blueprint가 요구하는 `IsDeadOrDying`
- `ARPGBaseCharacter`의 기본 서브오브젝트로 추가

관련 파일:

- `Source/Project_RPG/Public/Component/RPGHealthComponent.h`
- `Source/Project_RPG/Private/Component/RPGHealthComponent.cpp`
- `Source/Project_RPG/Public/Character/RPGBaseCharacter.h`
- `Source/Project_RPG/Private/Character/RPGBaseCharacter.cpp`

남은 작업:

- 사망 상태 전환과 부활 상태를 컴포넌트가 직접 관리하도록 정리
- 체력 변경 델리게이트와 UI 업데이트 중복 제거
- NPC 및 보스 사망 이벤트 통합

### 4.9 D1 직업 스킬 부모 클래스 호환

상태: **부분 완료 / 일부 임시 호환**

구현한 부모 클래스:

- `URPGGameplayAbility_Skill_Buff`
- `URPGGameplayAbility_Skill_ShieldBash`
- `URPGGameplayAbility_Skill_GroundBreaker`
- `URPGGameplayAbility_Skill_WhirlwindSlash`
- `URPGGameplayAbility_Skill_PiercingShot`
- `URPGGameplayAbility_Skill_Targeting`
- `URPGGameplayAbility_Skill_AOE`

관련 파일:

- `Source/Project_RPG/Public/Ability/Gladiator/RPGGladiatorSkillAbilities.h`
- `Source/Project_RPG/Private/Ability/Gladiator/RPGGladiatorSkillAbilities.cpp`

검증된 Blueprint 스킬 10종:

- `GA_Skill_ShieldBash`
- `GA_Skill_Buff_SwiftBlade`
- `GA_Skill_WhirlwindSlash`
- `GA_Skill_Buff_Berserk`
- `GA_Skill_GroundBreaker`
- `GA_Skill_Buff_IronWill`
- `GA_Skill_TearsOfTheFallen`
- `GA_Skill_SoulBind`
- `GA_Skill_PiercingShot`
- `GA_Skill_Buff_VampricFocus`

#### Fighter - Shield Bash

상태: **임시 호환 / 검증 필요**

- 전방 캡슐 범위 판정
- 서버 권한 피해 적용
- 적대 팀만 피해
- 피격 반응 이벤트
- Knockback Ability가 없을 때 직접 Launch 처리
- 이후 Stun 이벤트 또는 임시 스턴 처리

정식 이식 후 교체할 부분:

- D1 장비 요구 조건
- Shield 장비 액터
- 원본 몽타주 이벤트 타이밍
- 정식 D1 Knockback/Stun Ability

#### Swordmaster - Whirlwind Slash

상태: **임시 호환 / 검증 필요**

- TargetData가 있으면 원본 방식으로 HitResult 처리
- TargetData가 없으면 주변 Sphere 판정 사용
- 동일 타격 구간 중복 피해 방지
- `GameplayEvent.Reset`으로 타격 캐시 초기화
- 서버 권한 피해 처리

정식 이식 후 교체할 부분:

- `D1GameplayAbility_Weapon_Melee`의 TargetData 파싱
- 무기 액터 기반 트레이스
- 방어 판정
- 원본 몽타주 Notify

#### Barbarian - Ground Breaker

상태: **임시 호환 / 검증 필요**

- 전방 Box 범위 판정
- 다중 대상 서버 피해
- Stun 이벤트
- 정식 Stun Ability가 없을 때 이동 정지 기반 임시 스턴

정식 이식 후 교체할 부분:

- `ProcessHitResult`
- 방어 피해 감소
- GreatSword 장비 요구 조건
- 정식 상태이상 Ability

#### Wizard - Tears of the Fallen

상태: **부분 완료 / 검증 필요**

- GroundTrace 타기팅 클래스 연결
- 선택 위치에 AOE Spawner 생성
- 서버에서만 AOE 생성
- D1 AOE Spawner/Element 호환 클래스 로딩 확인

검증된 의존 클래스:

- `B_Spell_AOE_Spawner`
- `B_Spell_AOE_Element_Explosion`

남은 작업:

- 실제 조준 입력 흐름 PIE 검증
- 원본 카메라 모드 적용
- AOE 피해 주기와 중복 대상 정책 검증
- 보스 및 일반 몬스터 상태이상 차등 처리

#### Wizard - Soul Bind

상태: **부분 완료 / 검증 필요**

- D1 LineTraceHighlight 타기팅 액터 호환
- TargetData를 대상으로 GameplayEffect 적용
- 2개의 GameplayEffect 클래스 로딩 확인
- 기존 Blueprint의 HealthComponent 호출 오류 해결

남은 작업:

- 실제 마우스 조준과 Confirm/Cancel 검증
- 대상 하이라이트 머티리얼 정리
- 보스 대상 제어 효과 면역 규칙

#### Archer - Piercing Shot

상태: **부분 완료 / 검증 필요**

- 화살은 서버에서만 생성
- `ArrowSocket` 사용
- 카메라 시점을 이용한 Aim Assist
- 발사 시 가시성 트레이스로 목표점 계산
- 투사체의 피해, 속도, 충돌 처리 연결

검증 데이터:

- Projectile: `B_Projectile_Arrow_Skill`
- Damage: `35`
- Initial Speed: `4500`
- Max Speed: `8000`
- Spawn Socket: `ArrowSocket`
- Aim Assist Range: `100 ~ 10000`

남은 작업:

- ADS 카메라 모드 실제 적용
- 활 장비 액터 및 화살 표시
- 발사/취소/확정 입력 흐름
- 관통 가능한 대상 수와 피해 감소 규칙

#### 4종 Buff Skill

상태: **부분 완료 / 검증 필요**

검증된 GameplayEffect:

- `GE_Skill_SwiftBlade`
- `GE_Skill_Berserk`
- `GE_Skill_IronWill`
- `GE_Skill_VampricFocus`

구현 내용:

- 자신에게 Buff GameplayEffect 적용
- 추가 효과용 BlueprintNativeEvent 유지
- BuffEffect를 GameplayEffect Context의 SourceObject로 전달
- 원본 Buff Montage가 없을 경우 현재 프로젝트의 대체 몽타주 사용

남은 작업:

- 실제 버프 수치와 지속시간 검증
- 버프 VFX와 GameplayCue 연결
- 중첩/갱신/덮어쓰기 정책
- 사망 및 직업 변경 시 버프 정리

### 4.10 D1 타기팅 액터 호환

상태: **부분 완료**

구현한 클래스:

- `ARPGGameplayAbilityTargetActor_LineTraceHighlight`

구현 기능:

- 라인 트레이스 기반 대상 선택
- 최대 거리 제한
- Reticle 및 TargetData 생성에 필요한 D1 직렬화 이름 보존

관련 파일:

- `Source/Project_RPG/Public/Ability/Gladiator/RPGGladiatorTargetActors.h`
- `Source/Project_RPG/Private/Ability/Gladiator/RPGGladiatorTargetActors.cpp`

남은 작업:

- 실제 플레이 중 하이라이트 표시/해제 검증
- 장애물, 죽은 대상, 아군, 보스 필터
- 게임패드 타기팅 보조

### 4.11 D1 투사체 및 AOE 액터 호환

상태: **부분 완료**

구현한 클래스:

- `ARPGGladiatorProjectileBase`
- `ARPGGladiatorAOEBase`
- `ARPGGladiatorAOEElementBase`

구현 내용:

- D1 직렬화 속성 및 컴포넌트 이름 유지
- Hit/Overlap 충돌 방식
- ProjectileMovement
- Niagara Trail/Impact
- SetByCaller 피해
- 피해 GameplayEffect가 없을 때의 제한적 대체 피해
- AOE 요소 랜덤 생성
- AOE 투사 또는 폭발 처리
- 적대 팀 판정

관련 파일:

- `Source/Project_RPG/Public/Ability/Gladiator/RPGGladiatorEffectActors.h`
- `Source/Project_RPG/Private/Ability/Gladiator/RPGGladiatorEffectActors.cpp`

남은 작업:

- Object Pooling
- 관통/폭발/부착 정책 완성
- 네트워크 Relevancy와 Spawn 비용 측정
- 보스 대형 캡슐 및 다중 컴포넌트 충돌 중복 제거
- GameplayCue와 실제 VFX 타이밍 검증

### 4.12 카메라 클래스 호환

상태: **에셋 로딩만 완료 / 런타임 미연결**

리다이렉트된 타입:

- `LyraCameraMode`
- `LyraCameraMode_ThirdPerson`

검증된 스킬 카메라 에셋:

- `CM_Skill_ShieldBash`
- `CM_Skill_Bow_ADS`

중요한 현재 한계:

- 현재 `ARPGPlayer`는 일반 `UCameraComponent + USpringArmComponent` 구조를 사용한다.
- D1/Lyra CameraMode Stack을 현재 카메라가 소비하지 않는다.
- 따라서 카메라 클래스는 로드되지만 스킬 중 실제 카메라 전환은 아직 적용되지 않는다.

### 4.13 D1 AnimNotify 및 서버 무기 트레이스 호환

상태: **부분 완료 / 런타임 검증 필요**

구현한 타입:

- `URPGAnimNotify_SendGameplayEvent`
- `URPGANS_SendGameplayEvent`
- `URPGAnimNotifyState_PerformTrace`
- `FRPGWeaponTraceParams`
- `FRPGWeaponTraceDebugParams`

구현 내용:

- D1 `EventTag`, `EventData`, Begin/Tick/End 속성 이름 보존
- D1 Notify/NotifyState/Trace 구조체 Core Redirect 추가
- `GameplayEvent.Trace`에 `FGameplayAbilityTargetData_SingleTargetHit` 전달
- 현재 `URPGEquipComponent` 및 `UPawnCombatComponent`에서 손 타입별 무기 조회
- `ARPGWeaponBase::WeaponCollisionBox`를 이용한 회전 박스 스윕
- 이동 거리와 회전 각도를 함께 계산한 서브스텝으로 빠른 무기 이동 보완
- 공유 Notify 객체의 상태를 SkeletalMesh별로 분리하여 여러 Pawn의 트레이스 상태 혼선 방지
- 기본 `ROLE_Authority` 실행으로 서버가 직접 HitResult를 생성
- 기존 Melee 파이프라인의 적대 대상, 방어 방향, 대상당 중복 피해 검증 재사용
- Whirlwind Slash의 TargetData 미수신 Sphere Overlap 폴백 제거

소스 검증:

- `Project_RPGEditor Win64 Development -NoLink` UHT/C++ 빌드 성공
- `Project_RPG Win64 Development` 전체 컴파일 및 `Project_RPG.exe` 링크 성공

남은 작업:

- 원본 D1 몽타주를 로드한 PIE에서 Notify 직렬화 값과 실제 타격 프레임 검증
- Listen Server에서 두 캐릭터가 동시에 같은 Notify를 실행할 때 상태 분리 확인
- 클라이언트 예측 TargetData 전송 및 서버 재검증 프로토콜 설계
- Shield Bash Capsule 및 Ground Breaker Box 판정의 D1 원본 동작 정밀 검증

### 4.14 스킬·트라이포드 모듈화 기반

상태: **1단계 완료 / 네트워크 선택 상태 이식 예정**

구현 내용:

- `URPGSkillDefinition`을 신규 스킬 데이터의 단일 원본으로 확정
- 활성화 시 스킬 레벨, 선택 트라이포드, ASC 상태를 `FRPGSkillRuntimeSpec`으로 1회 해석
- 잘못된 인덱스와 레벨 미달 트라이포드 선택 정규화
- 동일 `StatTag` 배율의 곱셈 합성
- `TripodTag`, Action, Montage, VFX Override 적용
- SkillContainer와 ChargeAction이 활성화 스냅샷만 소비하도록 연결
- `URPGSkillDefinition` 전용 에디터 데이터 검증기 추가
- `URPGSkillConfig`는 기존 에셋 호환용으로 유지하되 신규 데이터 작성은 중단
- Editor `-NoLink` UHT/C++ 빌드 및 Game 타깃 전체 링크 성공

설계 문서:

- `Docs/SKILL_AND_TRIPOD_MODULAR_ARCHITECTURE.md`

다음 단계:

- `URPGPlayerSkillComponent`를 Player에 구성하고 서버 RPC/FastArray/Iris 복제 적용
- Target/Effect/Presentation Feature Module 인터페이스 정의
- Whirlwind Slash를 첫 D1 파일럿으로 모듈 실행 경로에 연결
- `RPGGladiatorSkillAbilities.cpp`를 스킬별 얇은 조정 클래스로 분리

## 5. 현재 사용하는 임시 호환 구현

다음 항목은 최종 구현이 아니며 정식 D1 계층 이식 후 제거하거나 교체해야 한다.

### 5.1 대체 몽타주

D1 직업 스킬 Blueprint에서 다음 원본 몽타주 참조가 현재 프로젝트에 존재하지 않는 것으로 확인됐다.

- Shield Bash Montage
- Ground Breaker Montage
- Whirlwind Slash Montage
- Buff Montage

현재 대체 에셋:

- 근접/발사 스킬: `/Game/Blueprints/Character/Player/Anim/Montage/AM_PlayerSkill`
- 버프 스킬: `/Game/Blueprints/Character/Player/Anim/Montage/AM_PlayerToggleSkill`

이 대체 몽타주는 기능 확인용이다. 직업별 무기와 원본 애니메이션이 준비되면 제거한다.

### 5.2 직접 범위 판정

- Shield Bash: Capsule Overlap
- Ground Breaker: Box Overlap

두 판정은 D1 원본 스킬 소스도 각각 Capsule Overlap과 Box Trace를 사용하므로 무기 트레이스의 임시 대체물이 아니다. Whirlwind Slash의 Sphere 폴백만 제거했으며, 무기 궤적형 공격은 AnimNotify TargetData를 `RPGGameplayAbility_Weapon_Melee` 계층에서 처리한다.

### 5.3 임시 Stun/Knockback

대상에게 정식 D1 Stun/Knockback Ability가 없을 때 다음 대체 처리를 사용한다.

- `LaunchCharacter`
- `Status.Stun` Loose GameplayTag
- CharacterMovement 일시 정지
- 타이머 후 이동 복구

정식 상태이상 Ability 이식 후 이 코드는 제거한다.

### 5.4 하드코딩된 콘텐츠 경로

현재 일부 호환 코드가 다음 경로를 직접 로드한다.

- Gladiator GameData
- 현재 프로젝트 대체 몽타주
- 기본 ClassData

최종적으로는 Project Settings, AssetManager 또는 Experience별 GameData 참조로 이동해야 한다.

## 6. 검증 완료 내역

### 6.1 C++ 빌드

Equipment Ability 구현 직후 전체 에디터 빌드 결과:

- Target: `Project_RPGEditor Win64 Development`
- Result: `Succeeded`

사용한 명령:

```powershell
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' Project_RPGEditor Win64 Development '-Project=C:\UE5\Project_RPG\Project_RPG.uproject' -WaitMutex -NoHotReloadFromIDE
```

손 타입 불일치 우회 방지 보강 후 최종 게임 타깃 빌드 결과:

- Target: `Project_RPG Win64 Development`
- Result: `Succeeded`
- `RPGGameplayAbility_Equipment.cpp`, `RPGEquipComponent.cpp`를 포함한 전체 게임 모듈 재컴파일 및 링크 성공

최종 에디터 타깃 증분 빌드에서는 변경된 두 C++ 파일의 컴파일까지 성공했으나, 실행 중인 `UnrealEditor.exe`가 `UnrealEditor-Project_RPG.dll`을 점유하여 마지막 DLL 재링크만 수행하지 못했다. 실행 중인 사용자 에디터는 종료하지 않았다.

Weapon Melee 기반 구현 후 에디터 타깃 증분 빌드 결과:

- UnrealHeaderTool 성공
- `RPGGameplayAbility_Weapon_Melee.cpp`, `RPGGladiatorSkillAbilities.cpp`, `RPGGladiatorEffectActors.cpp` 컴파일 성공
- 전체 37개 액션 중 컴파일 및 정적 라이브러리 링크 성공
- 실행 중인 `UnrealEditor.exe`의 DLL 점유로 최종 모듈 DLL 링크만 `LNK1104` 실패
- 에디터 종료 후 동일 명령으로 최종 링크 및 Blueprint 로딩을 다시 검증해야 함

EffectCauser 및 Shield Bash 공통화 보강 후 컴파일 전용 증분 빌드 결과:

- Target: `Project_RPGEditor Win64 Development -NoLink`
- Result: `Succeeded`
- UnrealHeaderTool 성공
- `RPGGameplayAbility_Weapon_Melee.cpp`, `RPGGladiatorEffectActors.cpp`, `RPGGladiatorSkillAbilities.cpp`, 관련 Unity 모듈 컴파일 성공

### 6.2 직업 스킬 Blueprint 로딩

검증 결과:

- 직업 스킬 10종 로딩 성공
- 관련 의존 클래스 로딩 성공
- Soul Bind의 기존 Blueprint 핀/부모 오류 해결
- 직업 스킬 범위에서 Blueprint Compile Error 0건
- 직업 스킬 10종의 `EquipmentInfos` 복원 성공
- Shield, One-Hand Sword, Two-Hand Sword, GreatSword, Staff, Bow 요구 타입 확인
- 모든 직업 스킬에서 Compatibility Fallback 활성 상태 확인

최종 런타임 데이터 검증 로그:

- `Saved/Logs/Codex_ValidateClassRuntimeFinal.log`

이전 전체 직업 로딩 검증 로그:

- `Saved/Logs/Codex_ValidateClassSkills4.log`

입력 검증 로그:

- `Saved/Logs/Codex_ValidateClassInputs2.log`

Equipment Ability 검증 로그:

- `Saved/Logs/Codex_ValidateEquipmentRequirements.log`

검증 스크립트:

- `Saved/Codex/validate_class_skills.py`
- `Saved/Codex/validate_gladiator_data.py`
- `Saved/Codex/inspect_melee_skill_montages.py`
- `Saved/Codex/validate_equipment_ability.py`

### 6.3 아직 수행하지 않은 검증

- 에디터 PIE에서 직업 선택부터 실제 전투까지 수동 통합 테스트
- Listen Server + Client 2인 테스트
- Dedicated Server 테스트
- 패킷 지연 환경의 Ability Prediction 테스트
- 사망/리스폰/맵 전환 후 직업 AbilitySet 유지 테스트
- 직업 변경 중 활성 Ability와 Buff 정리 테스트
- 장시간 전투 중 투사체/AOE 성능 테스트

## 7. 확인된 기존 프로젝트 문제

아래 문제는 D1 직업 이식과 직접 관련이 없으며 임의로 복구하지 않았다.

### 7.1 손상된 Behavior Tree 에셋

- 파일: `Content/Blueprints/Character/NPC/Gruntling/BT_Guardian.uasset`
- 파일 크기가 비정상적으로 작고 패키지 Name Table 오류가 발생한다.
- 명시적 복구 지시 전에는 삭제하거나 이전 커밋에서 복원하지 않는다.

### 7.2 누락된 UI 에셋

- `WBP_ItemDragVisual.uasset`이 현재 작업 트리에 없는 것으로 확인됐다.
- 과거 커밋에 유효한 버전이 있을 가능성이 있지만 명시적 승인 없이 복원하지 않는다.

### 7.3 매우 큰 Dirty Worktree

현재 저장소에는 D1 작업 외에도 다음 범위의 사용자 변경이 다수 존재한다.

- UnrealAgent
- 맵
- 프로젝트/Target/Plugin 설정
- 에디터 설정 및 생성 파일

작업 시 관련 파일만 수정하고 다른 변경을 Reset/Checkout 하지 않는다.

## 8. Equipment Ability 기반 이식 결과

### 8.1 `D1GameplayAbility_Equipment`

상태: **코드 및 Blueprint 로딩 완료 / PIE 검증 필요**

구현한 타입:

- `ERPGGladiatorEquipmentType`
- `ERPGGladiatorWeaponType`
- `ERPGGladiatorUtilityType`
- `FRPGGladiatorEquipmentInfo`
- `URPGGameplayAbility_Equipment`

구현한 기능:

- Armor/Weapon/Utility 요구 조건 검사
- 손 타입과 무기 타입 검사
- 장비 액터 캐시
- 장비 아이템 인스턴스 조회
- 장비 스탯 조회
- AttackSpeedPercent 기반 공격 속도 스냅샷
- `Ability.ActivateFail.MissingEquipment`, `Ability.ActivateFail.WrongEquipment` 실패 태그
- `URPGEquipComponent`의 슬롯별 SpawnedActor 조회와 손 타입 기반 활성 무기 조회
- `FEquipmentFragment`와 `ARPGWeaponBase`의 선택적 D1 장비 메타데이터
- `URPGEquipComponent`, `URPGEquipmentComponent`, `UPawnCombatComponent` 순서의 매핑
- 명시된 아이템/액터 타입이 틀리면 Compatibility Fallback으로 우회하지 않음

관련 파일:

- `Source/Project_RPG/Public/Ability/Gladiator/RPGGameplayAbility_Equipment.h`
- `Source/Project_RPG/Private/Ability/Gladiator/RPGGameplayAbility_Equipment.cpp`
- `Source/Project_RPG/Public/Component/Equipment/RPGEquipComponent.h`
- `Source/Project_RPG/Private/Component/Equipment/RPGEquipComponent.cpp`
- `Source/Project_RPG/Public/Item/Fragment/RPGItemFragment.h`
- `Source/Project_RPG/Public/Item/Weapon/RPGWeaponBase.h`
- `Source/Project_RPG/Public/Type/RPGEnumTypes.h`
- `Config/DefaultEngine.ini`

직업 스킬 요구 장비 복원 결과:

| 스킬 | 요구 장비 |
|---|---|
| Shield Bash | LeftHand Shield |
| Swift Blade | RightHand One-Hand Sword |
| Whirlwind Slash / Berserk | TwoHand Two-Hand Sword |
| Ground Breaker / Iron Will | TwoHand GreatSword |
| Tears of the Fallen / Soul Bind | TwoHand Staff |
| Piercing Shot / Vampiric Focus | TwoHand Bow |

현재 Compatibility Fallback:

- 아이템 또는 무기 액터의 타입 값이 `Count`이면 아직 메타데이터가 이식되지 않은 것으로 처리한다.
- 장비 액터가 아직 Spawn되지 않았으면 `UPawnCombatComponent`의 현재 등록 무기를 캐시한다.
- 명시적 타입 불일치는 항상 활성화 거부한다.
- `bAllowIncompleteEquipmentCompatibility`를 끄면 미완성 메타데이터 경로도 활성화 거부한다.
- 직업별 기본 장비 Spawn/Attach가 완료되면 이 Fallback을 제거한다.

남은 검증:

- PIE에서 호환 무기만 있는 캐릭터의 스킬 활성화
- PIE에서 명시적으로 잘못된 장비의 활성화 거부와 실패 태그 확인
- 장비 교체 직후 Ability 활성화 경합 확인
- Listen Server에서 장비 액터 캐시와 공격 속도 스냅샷 확인

### 8.2 `D1GameplayAbility_Weapon_Melee`

상태: **코드 구현 및 개별 컴파일 완료 / 최종 링크와 PIE 검증 필요**

구현한 기능:

- Ability 활성화 시 타격 캐시 초기화
- TargetData에서 Character Hit와 Block Hit 분리
- 장비 액터 Hit를 소유 캐릭터의 ASC Avatar로 정규화
- 같은 타격 구간의 중복 대상 제거
- 팀 적대 여부 필터
- 방어 중인 대상의 각도 계산
- 방어 시 피해 배율 적용
- GameplayCue Impact
- HitResult를 GameplayEffect Context에 저장
- D1/현재 프로젝트 SetByCaller 피해 태그 동시 적용
- 장비 액터를 EffectCauser로 보존
- 공통 HitReact 이벤트 전송
- 에디터 디버그 타격점

현재 프로젝트에 사용한 대응 구조:

- `URPGCombatFunctionLibrary::IsTargetPawnHostile`
- `RPGGameplayTags::Player_Status_Blocking`
- `Status.Block` 호환 태그
- `RPGGameplayTags::Shared_Event_HitReact`
- `RPGGladiatorEffectActors::ApplyDamage`
- 현재 `ARPGWeaponBase` 충돌 컴포넌트

교체한 임시 코드:

- Whirlwind의 로컬 `HitActors`
- Shield Bash/Ground Breaker의 직접 피해 적용
- Shield Bash/Ground Breaker/Whirlwind의 개별 HitReact 전송

관련 파일:

- `Source/Project_RPG/Public/Ability/Gladiator/RPGGameplayAbility_Weapon_Melee.h`
- `Source/Project_RPG/Private/Ability/Gladiator/RPGGameplayAbility_Weapon_Melee.cpp`
- `Source/Project_RPG/Public/Ability/Gladiator/RPGGladiatorSkillAbilities.h`
- `Source/Project_RPG/Private/Ability/Gladiator/RPGGladiatorSkillAbilities.cpp`
- `Source/Project_RPG/Public/Ability/Gladiator/RPGGladiatorEffectActors.h`
- `Source/Project_RPG/Private/Ability/Gladiator/RPGGladiatorEffectActors.cpp`

남은 검증:

- 에디터 종료 후 최종 DLL 링크
- 직업 스킬 Blueprint 10종 부모/함수 핀 로딩
- PIE에서 아군 제외, 중복 타격, 정면/후면 방어 피해 비교
- Listen Server에서 TargetData와 GameplayCue 중복 실행 여부 확인

## 9. 앞으로 진행할 전체 이식 로드맵

### Phase 1. D1 Equipment Ability 기반

상태: **코드 및 Blueprint 로딩 완료 / PIE 검증 필요**

- [x] D1 호환 EquipmentType enum
- [x] D1 호환 WeaponType enum
- [x] D1 호환 UtilityType enum
- [x] `FRPGGladiatorEquipmentInfo`
- [x] `URPGGameplayAbility_Equipment`
- [x] 장비 요구 조건 검사
- [x] 현재 Equip/Equipment/CombatComponent 매핑
- [x] 공격 속도 스냅샷
- [x] D1 Core Redirect 추가
- [x] 직업 스킬 부모 계층에 Equipment 기반 적용
- [x] 빌드 및 Blueprint 로딩 검증

완료 조건:

- [ ] 장비 기반 Ability가 현재 캐릭터에서 활성화 가능
- [ ] 잘못된 장비일 때 명확하게 활성화 거부
- [x] 장비 시스템 미완성 기간의 호환 경로가 별도 표시됨
- [x] 직업 스킬 10종이 계속 로드됨

### Phase 2. D1 Weapon Melee 기반

상태: **코드 구현 및 개별 컴파일 완료 / 최종 링크와 PIE 검증 필요**

- [x] `URPGGameplayAbility_Weapon_Melee`
- [x] TargetData 파싱
- [x] Character/Block Hit 분리
- [x] PvE 적대 팀 필터
- [x] 중복 타격 캐시
- [x] 방어 각도 판정
- [x] 방어 피해 배율
- [x] 공통 `ProcessHitResult`
- [x] Impact GameplayCue
- [x] HitResult Context
- [x] 디버그 타격점
- [x] Shield Bash, Ground Breaker, Whirlwind를 Melee 기반으로 리팩터링

완료 조건:

- [x] 근접 스킬이 공통 파이프라인으로 피해를 적용
- [ ] PIE에서 아군 피해가 발생하지 않음
- [ ] PIE에서 한 타격 구간에 대상당 한 번만 피해
- [ ] PIE에서 방어 방향과 피해 감소가 적용됨
- [ ] Listen Server에서 GameplayCue와 피해가 중복 실행되지 않음

### Phase 3. D1 AnimNotify 및 무기 트레이스

상태: **부분 완료 / 소스 빌드 성공 / 런타임 검증 필요**

- [x] D1 GameplayEvent AnimNotify 호환
- [x] `GameplayEvent.Montage.Begin`
- [x] `GameplayEvent.Montage.End`
- [x] `GameplayEvent.Trace`
- [x] `GameplayEvent.Reset`
- [x] 무기 소켓/충돌 박스 시작-끝 위치 트레이스
- [x] 서버 권한 TargetData 생성 및 Melee 검증 경로 연결
- [ ] 클라이언트 TargetData 생성
- [ ] 클라이언트 TargetData 서버 재검증
- [x] Whirlwind Slash 대체 Sphere 판정 제거
- [x] Shield Bash/Ground Breaker의 Capsule/Box는 D1 원본 스킬 고유 범위 판정으로 분류

완료 조건:

- 애니메이션의 실제 타격 프레임에만 피해 발생
- 빠른 무기 이동에서도 터널링 없이 타격
- 중복 콜리전과 중복 피해가 없음

### Phase 4. D1 Stun, Knockback, HitReact

상태: **미착수**

- [ ] `URPGGameplayAbility_Stun`
- [ ] `URPGGameplayAbility_Knockback`
- [ ] `URPGGameplayAbility_HitReact`
- [ ] 방향별 피격 몽타주
- [ ] Root Motion 또는 Motion Warping 기반 Knockback
- [ ] 상태이상 중 입력/AI 제어
- [ ] 상태이상 중첩과 갱신 정책
- [ ] 임시 타이머 기반 Stun/Launch 제거

PvE 추가 요구:

- [ ] 일반 몬스터 상태이상 허용
- [ ] 엘리트 상태이상 저항
- [ ] 보스 상태이상 면역 또는 무력화 게이지 연동
- [ ] 슈퍼아머 상태 처리

### Phase 5. D1 Equipment Actor 및 직업별 기본 장비

상태: **미착수**

- [ ] `D1EquipmentBase` 동작 이식
- [ ] 장비 액터 Spawn/Attach
- [ ] Primary/Secondary/Utility 상태
- [ ] 손 타입과 장비 슬롯 매핑
- [ ] WeaponType 메타데이터
- [ ] Shield의 Block Collision
- [ ] Bow의 Arrow Socket
- [ ] Staff의 Spell Socket
- [ ] ClassData의 `DefaultItemEntries` 적용
- [ ] 직업 변경 시 장비 제거/교체
- [ ] 장비 복제

직업별 목표 장비:

| 직업 | 목표 장비 |
|---|---|
| Fighter | One-Hand Sword + Shield |
| Swordmaster | Two-Hand Sword |
| Barbarian | GreatSword |
| Wizard | Staff |
| Archer | Bow |

### Phase 6. 직업별 애니메이션과 카메라

상태: **미착수**

- [ ] 직업별 Anim Layer
- [ ] 직업별 Locomotion
- [ ] 전용 Skill Montage
- [ ] 장비 전환 Montage
- [ ] Hit/Block/Knockback/Stun Montage
- [ ] 현재 SpringArm 카메라용 CameraMode Adapter
- [ ] Shield Bash 카메라
- [ ] Bow ADS 카메라
- [ ] AOE/Targeting 카메라
- [ ] 임시 대체 몽타주 제거

### Phase 7. Bow 및 Spell 공통 기반

상태: **부분 구현 후 정식 계층 미착수**

- [ ] `D1GameplayAbility_Weapon_Bow_Projectile` 이식
- [ ] Socket Spawn Transform
- [ ] Aim Assist 공통화
- [ ] 관통 규칙
- [ ] `D1GameplayAbility_Weapon_Spell_Projectile` 이식
- [ ] Targeting/Confirm/Cancel 공통화
- [ ] AOE Target Actor 공통화
- [ ] Cast Start/End/Spell Montage 흐름
- [ ] 카메라와 입력 안내 UI

### Phase 8. PvE 전투 규칙

상태: **미착수**

- [ ] 공격력/방어력 최종 공식
- [ ] 치명타
- [ ] 헤드어택
- [ ] 백어택
- [ ] 경직 단계
- [ ] 슈퍼아머
- [ ] 무력화 수치
- [ ] 보스 무력화 게이지
- [ ] 상태이상 저항
- [ ] 피격 면역 시간
- [ ] 피해 숫자 및 전투 로그
- [ ] 전투 중/비전투 상태

### Phase 9. AI, 몬스터, 보스

상태: **현재 프로젝트 AI는 존재하나 D1/PvE 통합 미착수**

- [ ] 어그로/Threat 시스템
- [ ] 파티원 타깃 선택
- [ ] 공격 예고 Telegraph
- [ ] 장판 및 안전 구역
- [ ] 보스 Phase 전환
- [ ] 패턴 쿨다운
- [ ] Enrage
- [ ] 소환 몬스터
- [ ] 무력화/카운터 패턴
- [ ] 난이도별 수치와 패턴 변화
- [ ] 서버 권한 AI 검증

### Phase 10. 아이템, 인벤토리, 보상

상태: **현재 프로젝트 기반은 있으나 D1 통합 미착수**

- [ ] D1 ItemTemplate → 현재 ItemManifest 변환
- [ ] 장비 스탯 Fragment
- [ ] 무기 공격력
- [ ] 방어구 방어력
- [ ] 옵션/희귀도
- [ ] 드롭 테이블
- [ ] 보스 보상
- [ ] 장비 교체 시 GAS 효과 갱신
- [ ] 저장/불러오기
- [ ] 직업 장비 제한

### Phase 11. UI 및 사용성

상태: **부분 완료**

- [ ] 직업 HUD
- [ ] 스킬 슬롯과 쿨다운
- [ ] 자원 게이지
- [ ] Buff/Debuff 아이콘
- [ ] 보스 체력과 무력화 게이지
- [ ] Targeting Confirm/Cancel 안내
- [ ] 파티 UI
- [ ] 전리품 UI
- [ ] 사망/부활 UI
- [ ] 직업 선택 저장 및 재선택 정책

### Phase 12. 네트워크, 성능, 테스트

상태: **미착수**

- [ ] Listen Server 테스트
- [ ] Dedicated Server 테스트
- [ ] Ability Prediction Key 검증
- [ ] Client TargetData 서버 검증
- [ ] 치트 방지용 거리/각도 재검증
- [ ] 투사체/AOE Replication 최적화
- [ ] Object Pooling
- [ ] 대규모 몬스터 전투 프로파일링
- [ ] 자동화 테스트
- [ ] 회귀 테스트 체크리스트

## 10. 권장 다음 작업 순서

현재 시점에서 바로 이어서 작업할 정확한 순서는 다음과 같다.

1. PIE에서 Equipment Compatibility Fallback 활성화 확인
2. 명시적으로 잘못된 장비의 Ability 활성화 거부 확인
3. 공격 속도 변경 후 Activation 시점 스냅샷 확인
4. Listen Server에서 장비 교체/활성화 경합 확인
5. 에디터 종료 후 Weapon Melee 변경의 최종 DLL 링크와 Blueprint 로딩 확인
6. PIE에서 아군 필터, 중복 타격, 방어 각도/피해 배율 확인
7. Listen Server에서 TargetData, GameplayCue, 피해 중복 여부 확인
8. Phase 3의 D1 AnimNotify 및 무기 트레이스 이식
9. 정식 Stun/Knockback Ability 이식
10. 직업별 기본 장비 Spawn/Attach

장비 시스템이 완성되기 전에는 현재 직업 스킬을 전부 막지 않도록 명시적인 Compatibility Fallback을 둔다. 직업별 기본 장비가 실제로 적용되면 해당 Fallback을 제거한다.

## 11. 작업 완료 정의

D1 시스템 하나를 현재 프로젝트에 이식 완료했다고 판단하려면 다음 조건을 모두 만족해야 한다.

- 현재 프로젝트 타입으로 컴파일된다.
- 기존 프로젝트 코드와 중복 책임이 정리됐다.
- D1 Blueprint/데이터 에셋이 부모 손실 없이 로드된다.
- 관련 GameplayTag가 유효하다.
- 서버 권한과 클라이언트 표시 책임이 구분된다.
- PvE 적대 팀 필터가 적용된다.
- PIE에서 실제 기능을 확인했다.
- Listen Server 환경에서 중복 실행이 없다.
- 임시 코드가 있으면 문서에 표시됐다.
- 관련 빌드/검증 로그가 남아 있다.

## 12. 유지보수 메모

- 전역 `git diff --check`는 UnrealAgent 등 다른 변경의 공백 문제로 오염될 수 있으므로 작업 파일 범위를 지정해서 실행한다.
- 새로 만든 Gladiator 호환 소스 중 일부는 아직 Git 기준 Untracked 상태다.
- 바이너리 에셋을 임의로 삭제하거나 과거 버전으로 복원하지 않는다.
- D1 소스의 클래스 이름과 UPROPERTY 이름은 Blueprint 직렬화 호환 때문에 가능한 한 유지한다.
- D1 원본의 동작이 현재 PvE 목표와 충돌하면 원본 복사보다 현재 프로젝트의 PvE 설계를 우선한다.
- 매 단계가 끝날 때 이 문서의 상태와 체크박스를 갱신한다.

## 13. 변경 기록

### 2026-07-20

- 지금까지의 D1/Gladiator 이식 현황을 최초로 통합 문서화했다.
- 완료, 부분 완료, 임시 호환, 미착수 범위를 구분했다.
- 다음 작업을 Equipment Ability 기반 이식으로 확정했다.
- `URPGGameplayAbility_Equipment`, D1 장비 enum/struct, Core Redirect를 구현했다.
- Equip/Equipment/CombatComponent 매핑과 명시적 Compatibility Fallback을 추가했다.
- 직업 스킬 10종의 장비 요구 정보 및 Blueprint 로딩을 재검증했다.
- Phase 1의 남은 항목을 PIE/Listen Server 런타임 검증으로 갱신했다.
- `URPGGameplayAbility_Weapon_Melee` 공통 파이프라인과 D1 Core Redirect를 구현했다.
- TargetData 분류, 적대 팀 필터, 중복 타격, 방어 각도/피해 배율, Impact Cue, HitResult Context를 통합했다.
- Shield Bash, Ground Breaker, Whirlwind를 공통 Melee 처리로 전환했다.
- Weapon Melee 관련 UHT와 C++ 컴파일은 성공했으며, 실행 중인 에디터의 DLL 점유로 최종 링크만 대기 상태다.

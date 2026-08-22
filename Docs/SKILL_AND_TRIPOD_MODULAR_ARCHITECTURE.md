# 스킬 및 트라이포드 모듈 아키텍처

> 기준일: 2026-07-20  
> 적용 대상: `Source/Project_RPG/` 및 `D1_SourceCode/` 이식 스킬

## 1. 목표

스킬 수와 트라이포드 조합이 늘어나도 다음을 유지한다.

- 타기팅, 피해, 상태이상, 연출을 서로 독립적으로 교체할 수 있다.
- 트라이포드 조합마다 GameplayAbility 서브클래스를 만들지 않는다.
- 서버와 클라이언트가 동일한 활성화 입력을 사용한다.
- DataAsset과 AnimNotify 같은 공유 객체에는 실행 중인 상태를 저장하지 않는다.
- D1 직렬화 호환 코드는 어댑터 경계에 두고 신규 스킬 코어로 퍼뜨리지 않는다.

## 2. 현재 확인된 문제

- `RPGGladiatorSkillAbilities.cpp` 하나가 여러 직업 스킬의 수명주기, 타기팅, 피해, 상태이상, 연출을 함께 소유한다.
- `URPGSkillDefinition`, `URPGSkillAction`, 트라이포드 UI가 존재하지만 D1 이식 Ability와 연결되지 않았다.
- `URPGSkillConfig`와 `URPGSkillDefinition`이 비슷한 데이터를 중복 정의한다.
- `FRPGSkillModifier`, `TripodTag`, Montage/VFX Override 데이터가 실행 경로에서 실제로 소비되지 않았다.
- `URPGPlayerSkillComponent`가 플레이어에 구성되거나 서버 권한으로 복제되지 않아 선택값을 신뢰할 수 없다.
- `OverrideActionClass`만으로 모든 변형을 처리하면 트라이포드 조합 수만큼 클래스가 증가한다.

## 3. 책임 경계

```text
PlayerSkillComponent (서버 권한 선택 상태)
                  |
                  v
SkillDefinition (공유 불변 데이터)
                  |
                  v
RuntimeSpec Resolver (활성화 시 1회 해석)
                  |
                  v
GameplayAbility Container (수명주기/예측/커밋)
        |                 |                  |
        v                 v                  v
 Target Module       Effect Module     Presentation Module
```

### GameplayAbility

- 활성화, 커밋, 취소, Prediction Key, AbilityTask 수명주기만 관리한다.
- 직접 Sphere/Box Trace나 상태이상 타이머를 계속 추가하지 않는다.

### SkillDefinition

- 기본 Action, 수치, 몽타주, VFX, 트라이포드 선택지를 보관한다.
- 실행 중 상태를 절대 저장하지 않는다.
- 신규 스킬의 단일 데이터 원본으로 사용한다.

### RuntimeSpec

- 스킬 활성화 시작 시 한 번 생성하는 불변 스냅샷이다.
- 스킬 레벨, 유효한 트라이포드 인덱스, 분기 태그, 합성 수치 배율, 최종 Action/Montage/VFX를 보관한다.
- 같은 StatTag의 배율은 티어 순서대로 곱한다.
- 잘못되었거나 레벨이 부족한 선택은 `INDEX_NONE`으로 정규화한다.

### Action 및 향후 Feature Module

- Action은 활성화별 UObject 인스턴스이므로 타이머와 임시 상태를 소유할 수 있다.
- 타기팅, 효과, 연출은 작은 Feature Module/AbilityTask로 추가 분리한다.
- 공유 Definition이나 Module CDO에는 HitActors, TimerHandle 같은 런타임 상태를 두지 않는다.

## 4. 트라이포드 구성 규칙

트라이포드는 우선 다음 세 수단을 조합한다.

1. `StatModifiers`: 피해, 범위, 쿨다운 같은 수치 배율
2. `TripodTag`: 관통, 폭발, 다단히트 같은 실행 분기
3. Montage/VFX Override: 표현 교체

전체 실행 방식이 완전히 달라질 때만 `OverrideActionClass`를 사용한다. 예를 들어 투사체 수 증가나 폭발 추가 때문에 새 Action 클래스를 만들지 않고, 태그와 Feature Module 조합으로 처리한다.

## 5. 네트워크 원칙

- 트라이포드 선택과 스킬 레벨은 서버 권한 상태로 관리하고 Iris/FastArray로 복제한다.
- Ability 활성화 시 서버와 예측 클라이언트는 복제된 선택값으로 각각 RuntimeSpec을 만든다.
- 피해, 상태이상, 드롭, 스폰은 서버만 확정한다.
- 클라이언트 TargetData를 사용하는 스킬은 Prediction Key와 서버 공간 검증을 반드시 통과한다.
- 활성화 도중 선택이 바뀌어도 이미 실행 중인 RuntimeSpec은 변경하지 않는다.

## 6. 단계별 전환

### 단계 A — RuntimeSpec 기반 정리

- [x] RuntimeSpec 타입 추가
- [x] 트라이포드 레벨/인덱스 검증 및 수치 배율 합성
- [x] TripodTag, Action, Montage, VFX 최종 해석
- [x] SkillContainer와 ChargeAction이 RuntimeSpec을 소비
- [x] `URPGSkillDefinition`을 신규 스킬의 단일 원본으로 확정
- [x] Editor `-NoLink` UHT/C++ 빌드 및 Game 타깃 전체 링크 성공

### 단계 B — 선택 상태 네트워크화

- [ ] `URPGPlayerSkillComponent`를 실제 플레이어에 구성
- [ ] 스킬 레벨/트라이포드 선택 서버 RPC
- [ ] FastArray/Iris 복제 및 OnRep UI 갱신
- [ ] 서버에서 티어 레벨과 OptionIndex 재검증

### 단계 C — 실행 Feature 분리

- [ ] Target Module: Self, WeaponTrace, Shape, Ground, Actor, Projectile
- [ ] Effect Module: Damage, Stun, Knockback, Buff, Spawn
- [ ] Presentation Module: Montage, Niagara, GameplayCue, Camera
- [ ] 공통 실행 Context와 단계별 훅 정의

### 단계 D — D1 이식 스킬 파일 분해

- [ ] Whirlwind Slash를 첫 파일럿으로 RuntimeSpec/Feature 구조에 연결
- [ ] Shield Bash와 Ground Breaker의 범위 판정 모듈화
- [ ] Projectile/Targeting/AOE 스킬을 공통 모듈로 전환
- [ ] `RPGGladiatorSkillAbilities.cpp`를 스킬별 얇은 조정 클래스로 분해

### 단계 E — 검증 자동화

- [x] Definition의 기본 필드, 티어/옵션 수, 수치 Modifier 구조 검증기 추가
- [ ] 모든 Definition 에셋에 대한 일괄 검증 실행 및 수정
- [ ] 서버/클라이언트 RuntimeSpec 해시 일치 확인
- [ ] 트라이포드 조합별 피해/범위/투사체 수 자동 테스트
- [ ] Listen Server에서 중복 피해와 GameplayCue 검증

## 7. 당장 지킬 규칙

- 신규 스킬 데이터는 `URPGSkillConfig`에 추가하지 않는다.
- 새 트라이포드를 만들기 전에 기존 StatTag 또는 Feature로 표현 가능한지 확인한다.
- 하나의 트라이포드 때문에 기존 Ability에 직접 `if`를 추가하지 않는다.
- 스킬 활성화 후에는 `URPGPlayerSkillComponent`를 다시 조회하지 않고 RuntimeSpec만 사용한다.
- D1 호환 속성명은 호환 Ability/Adapter에 보존하고 신규 모듈 API 이름과 분리한다.

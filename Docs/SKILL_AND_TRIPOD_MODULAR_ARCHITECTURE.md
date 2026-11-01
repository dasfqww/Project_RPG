# 스킬 및 트라이포드 모듈 아키텍처

> 갱신일: 2026-10-25 (초기 설계: 2026-07-20)
>
> 현재 작업 대상: `Source/Project_RPG/`의 신규 스킬 시스템과 플레이어/UI 연결
>
> 신규 스킬 설계의 제외 대상: `D1_SourceCode/` 및 D1 이식 스킬 전체의 분해·전환. 이후 사용자가 요청한 방향 정리의 제한된 제거 작업은 아래 기록을 따른다.

> 2026-10-25 방향 정리: 구형 5직업 선택 RPC·PlayerState 복제·HUD 선택창·직업 데이터 타입을 제거했다. 후속 정리에서는 현재 콘텐츠가 사용하지 않는 D1 스킬 7종과 구형 근접·투사체·장판 구현 및 관련 Redirect를 제거했다. 신규 스킬 코어와 별도 진행 중인 공통 피격·상태이상 구현은 이 제거 작업의 대상이 아니다. [D1 정리 기록](D1_PVE_MIGRATION_STATUS_AND_ROADMAP.md)의 최신 두 절을 현재 상태로 우선하며, 아래 10/12의 수동 선택창 유지 기록은 과거 상태다.

> 최소 컨텍스트 작업 재개: [스킬 시스템 SKILL.md](../Skills/project-rpg-skill-system/SKILL.md)와 연결된 진행 요약을 먼저 읽는다. 상세 이력은 필요한 주제만 이 문서에서 확인한다.

> 직업과 스킬 구성의 기준: [플레이어블 직업 설계 방향](PLAYABLE_CLASS_DESIGN_DIRECTION.md). 독립적인 전투와 상호 보조, 직업당 액티브 12~16개 중 8개 장착을 기본안으로 둔다. 신규 직업 확장을 위한 SOLID·모듈화와 책임·의존 경계는 해당 문서 8절의 필수 설계 원칙을 따른다. 현재 코드의 습득·8슬롯 발동 자격·부여 소유권·Definition 연결·복원과 설계 사이의 간극은 9절에서 확인한다.

## 1. 목표

> 2026-10-25 영속 성장 복원: 서버 성장 기록을 인증된 Pawn의 기존 성장 → 클래스 부여 → 8슬롯 경계에 공급하는 소스를 연결했다. 안정적인 트라이포드 ID, 고유 능력 해금과 전투 Pawn의 저장 구성 잠금을 반영한다. 기존 대검 안정 ID `GreatSwordWarrior`와 서버 초안 ID의 명시적 변환도 추가했다. UHT·소스 컴파일 `-NoLink`는 통과했으며 새 모듈 링크·실제 실행은 미실행이다. 실제 옵션·고유 능력 데이터와 실행/검증 범위는 [성장 API의 UE 계약](../Backend/CHARACTER_PROGRESSION_API.md#6-ue-서버-복원-연결--2026-10-25)을 따른다.

스킬 수와 트라이포드 조합이 늘어나도 다음을 유지한다.

- 타기팅, 피해, 상태이상, 연출을 서로 독립적으로 교체할 수 있다.
- 트라이포드 조합마다 GameplayAbility 서브클래스를 만들지 않는다.
- 서버와 클라이언트가 동일한 활성화 입력을 사용한다.
- DataAsset과 AnimNotify 같은 공유 객체에는 실행 중인 상태를 저장하지 않는다.
- D1 직렬화 호환 코드는 어댑터 경계에 두고 신규 스킬 코어로 퍼뜨리지 않는다.

## 2. 초기 설계 당시 확인된 문제 (2026-07-20)

아래는 초기 문제 목록이다. 현재 구현/검증 여부는 6절과 8절을 기준으로 판단한다. D1 정리 범위는 문서 상단의 최신 기록을 따른다.

- `RPGPlayerSkillAbilities.cpp` 하나가 여러 플레이어 직업 스킬의 수명주기, 타기팅, 피해, 상태이상, 연출을 함께 소유한다.
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

- 트라이포드 선택과 스킬 레벨은 서버 권한 상태로 관리한다. 현재 구현은 소유자 전용 `TArray`/revision 복제이며, FastArray 전환은 성능상 필요가 확인될 때 검토한다.
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

- [x] `URPGPlayerSkillComponent`를 `ARPGPlayer`의 기본 컴포넌트로 구성
- [x] 스킬 레벨/트라이포드 선택 서버 RPC 구현
- [x] 소유자 전용 선택/SP/revision 복제 및 OnRep UI 갱신 구현
- [x] 서버에서 SP, 티어 레벨과 OptionIndex 재검증 구현
- [x] 스킬창 생성 시 플레이어 컴포넌트와 공용 카탈로그 자동 연결
- [x] UI ViewModel 레벨 명령 → 서버 RPC → 소유자 복제 → 양쪽 실행 레벨 반영 → 초기화/SP 환급을 서버+2클라이언트에서 검증
- [x] ViewModel 트라이포드 선택/변경 및 거부된 낙관적 변경 복원을 서버+2클라이언트에서 검증
- [x] SP 부족 요청 거부 및 다른 소유자의 선택/SP/revision 비노출을 서버+2클라이언트에서 검증
- [ ] 실제 스킬창 버튼과 표시를 멀티클라이언트 PIE에서 검증

구현 체크와 실제 네트워크 검증 완료는 구분한다. `PlayerSelectionSnapshot`의 OnRep 직접 호출은 복제 전송을 검증하지 않으며, 레벨/SP 전송은 별도 `-VerifySkillProgression` 네트워크 테스트로 확인했다.

### 단계 C — 실행 Feature 분리

- [ ] Target Module: Self, WeaponTrace, Shape, Ground, Actor, Projectile
- [ ] Effect Module: Damage, Stun, Knockback, Buff, Spawn
- [ ] Presentation Module: Montage, Niagara, GameplayCue, Camera
- [ ] 공통 실행 Context와 단계별 훅 정의

### 단계 D — D1 이식 스킬 전환 (보류 / 현재 범위 제외)

사용자 요청에 따라 기존 D1 소스와 이식 스킬은 그대로 유지한다. 과거 계획의 Whirlwind Slash 파일럿, Shield Bash/Ground Breaker 모듈화, Projectile/AOE 전환, `RPGPlayerSkillAbilities.cpp` 분해는 현재 남은 작업에 포함하지 않는다.

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

## 8. 현재 작업 및 남은 필수 확인

### 이번 연결 수정

- `ARPGPlayer`가 복제 가능한 `URPGPlayerSkillComponent`를 직접 소유한다. UI와 `SkillContainer`의 `FindComponentByClass`가 같은 상태 모델을 조회한다.
- `URPGSkillWindow::NativeConstruct`에서 초기화를 시작한다. 창을 여는 쪽이 별도로 초기화 함수를 호출해야 했던 누락을 없앤다.
- `ProjectRPG.Skill.Integration.PlayerComposition`: 네이티브 플레이어 및 기존 `BP_Player`의 컴포넌트 상속 확인 테스트 추가.
- `ProjectRPG.Skill.Integration.PlayerSelectionSnapshot`: 실제 네이티브 플레이어/ASC/몽타주에서 선택 전달 → UI/실행 반영, 차지→즉발 트라이포드, 실행 중 스냅샷 고정, 다음 시전의 갱신값 사용을 확인하는 테스트 추가. 선택 전달은 테스트가 OnRep를 호출하며 RPC나 네트워크 전송은 모사하지 않는다. 트라이포드는 임시 Definition 복사본에만 설정한다.

2026-10-01에는 UHT와 C++ 구문 검사까지만 통과했고, 사용자 프로필의 UBT 설정 폴더 쓰기 권한 오류로 테스트 실행이 남아 있었다. 아래는 2026-10-04에 확인한 실행 결과다.

### 2026-10-04 실행 결과

- 2026-10-04 빌드된 Editor DLL로 `ProjectRPG.Skill.` 19개 테스트 모두 성공. 경고가 포함된 2개 테스트의 경고는 Google/EOS 외부 서비스 연결 실패이며, 스킬 assertion 실패는 없다.
  - 보고서: `Saved/Tests/Skill/Automation-30ff6f8c8f6e4871ab3c56b4c028bd2f/Report/index.json`
- 기존 `-VerifySkillInput` 서버+2클라이언트 성공. Enhanced Input 차지 입력/해제, 서버·클라이언트 공격 섹션 및 취소 없는 종료 확인.
  - 로그: `Saved/Logs/IrisMultiClient/20261004-012150/`
- `Run-IrisMultiClientTest.ps1`에 선택적 `-VerifySkillProgression` 검증 추가 및 서버+2클라이언트 성공.
  - 실제 ViewModel 레벨업 명령으로 1→4, SP 50→47.
  - 낙관적 로컬 맵이 아닌 복제 backing array의 레벨 4 도착을 확인한 뒤 입력 시작.
  - 서버와 각 소유 클라이언트의 실제 활성화 RuntimeSpec 레벨 4 확인.
  - ViewModel 초기화 명령으로 레벨 1 복원, 서버·클라이언트 SP 50 전액 환급 확인.
  - 로그: `Saved/Tests/Skill/ProgressionNetwork-20261004-01/Saved/Logs/IrisMultiClient/20261004-013236/`
- 새 네트워크 probe는 UHT 및 C++ 컴파일/링크 성공 후 `Saved/Tests/Skill/ProgressionNetwork-20261004-01/`의 격리 테스트 프로젝트에서 실행했다. 기존 UBT compiler/linker 응답 파일을 사용했으며, 원본 프로젝트 DLL 교체나 핫리로드는 하지 않았다. 원본에서 새 옵션을 재실행하려면 최신 소스로 Editor 모듈을 빌드해야 한다.
- 두 네트워크 실행 모두 기존 `ABP_Gladiator_AnimLayers_OneHandSword` 컴파일 오류와 ToolsetRegistry Python 초기화 오류가 로그에 있다. 이번 스킬 검증 경로는 성공했지만 전체 게임 연출/에셋이 정상이라는 의미는 아니다. D1 관련 소스/에셋은 수정하지 않았다.

원본 Editor 모듈 빌드 후 레벨/SP 네트워크 검증 명령:

```powershell
& .\Scripts\Network\Run-IrisMultiClientTest.ps1 -SkipBuild -VerifySkillProgression -ClientCount 2
```

이 옵션은 트라이포드 선택·거부 요청·물리 키/실제 스킬창 버튼·다른 클라이언트에 선택 정보가 노출되지 않는지까지 검증하지는 않는다. 일반 실행에서는 probe가 동작하지 않는다.

### 트라이포드/거부 요청 검증 추가 및 SP 복원 수정

- 별도 `-VerifySkillTripods` 옵션을 추가했다. 실제 카탈로그 에셋을 저장하지 않고 게임 인스턴스별 transient Definition을 사용한다. D1 소스/스킬 에셋은 수정하지 않는다.
- 소유 컴포넌트의 실제 RPC와 owner 복제 revision을 통해 정상 선택·선택 변경, 잘못된 티어/옵션 인덱스, 잠긴 티어, 알 수 없는 스킬, 서버 레벨 상한 거부를 확인하도록 구성했다. ViewModel의 낙관적 표시와 복제 backing array를 따로 비교한다. 레벨 하향 시 선택 해제·전액 환급도 포함한다.
- **수정 전 서버+2클라이언트 재현 완료:** 첫 6개 거부 요청은 복원됐다. 클라이언트 상한 10/서버 상한 4 상태에서 레벨 5를 예측한 뒤 서버가 거부하면, 레벨은 4로 복원되지만 남은 SP와 ViewModel SP가 2 대신 0으로 남았다.
  - 로그: `Saved/Tests/Skill/TripodsNetwork-20261004-01/Saved/Logs/IrisMultiClient/20261004-165549/`
- 원인은 클라이언트 예측이 복제 속성 `UsedSP` 자체를 변경하는 것이다. 거부 시 서버의 `UsedSP`는 변하지 않아 revision만 도착하므로, 기존 OnRep가 스킬 선택만 복원하고 SP 예측값을 유지했다.
- 수정: 서버 확정 `UsedSP`와 비복제 `PredictedUsedSP`를 분리했다. 예측 UI는 별도 값을 사용하고, OnRep가 UI 알림 전에 예측 SP를 폐기해 기존 서버 확정값으로 복원한다. 테스트는 예측 중 서버 확정 baseline을 변경하지 않는지도 검사한다.
- 이전 실행에서는 공유 폴더의 소스/adaptive unity 변경으로 링크가 완료되지 않았다. 아래 최신 DLL을 별도 프로젝트에 복사해 **수정 후 런타임 재검증을 완료했다.** 이번 검증에서 보안/인벤토리/컨트롤러/D1 소스 수정, 원본 DLL 교체, 실행 중인 에디터 종료나 핫리로드는 하지 않았다.

### 수정 후 통합 검증 결과 (2026-10-04)

검증에는 `2026-10-04 21:52:31` 빌드된 Editor DLL의 복사본을 사용했다. 원본과 복사본의 SHA-256은 모두 `411BE0C6BE01A7B21C05A0E871CEEB802E77A4ED326AB908557A8F47A99D633B`다. 격리 프로젝트는 `Saved/Tests/Skill/Verification-20261004-01/`이다.

- `-VerifySkillTripods` 서버+2클라이언트 성공.
  - 각 소유 클라이언트에서 7개 거부 요청 모두 복원 확인. 서버 상한으로 거부된 레벨 5 예측은 레벨 4/남은 SP 2로 복원됐으며, 예측 중 복제 `UsedSP` baseline도 보존됐다.
  - 정상 선택과 다른 옵션으로 변경, 서버·클라이언트 RuntimeSpec의 선택 배율 3, 레벨 3으로 하향 시 잠긴 티어 선택 해제/SP 환급, 레벨 1 초기화 시 SP 5 전액 환급 확인.
  - 로그: `Saved/Tests/Skill/Verification-20261004-01/Saved/Logs/IrisMultiClient/20261004-215644/`
- `ProjectRPG.Skill.` 자동 테스트 19개 모두 성공. 실패/미실행 0개. 2개 테스트의 경고는 Google/EOS 외부 연결 실패이며 스킬 assertion 오류는 없다.
  - 보고서: `Saved/Tests/Skill/Verification-20261004-01/Saved/Tests/Automation/Report/index.json`
- 기존 `-VerifySkillProgression` 서버+2클라이언트 재검증 성공. Enhanced Input 차지 입력/해제, ViewModel 레벨업, owner 복제 레벨 4, 실제 서버/클라이언트 활성화 snapshot 레벨 4, 초기화와 SP 전액 환급 확인.
  - 로그: `Saved/Tests/Skill/Verification-20261004-01/Saved/Logs/IrisMultiClient/20261004-215803/`

네트워크 로그에는 GladiatorCore/D1 애니메이션 Blueprint와 CommonUI 설정/바인딩 오류가 남아 있다. 이번 결과는 스킬 C++/ViewModel/RPC 및 특정 시전 경로의 검증이며, 전체 게임 연출이나 실제 스킬창/3인칭 조작의 PIE 검증 완료를 뜻하지 않는다. 해당 D1 에셋과 다른 UI 설정은 이번 작업에서 수정하지 않았다.

### SP 부족 / 소유자 전용 복제 검증 (2026-10-04)

`-VerifySkillTripods`를 8개 거부 요청과 실제 비소유자 pawn 복제 검사로 확장했다. 검증 코드는 임시 Definition/플레이어 상태에만 동작하며, D1/운영 스킬 소스와 에셋은 수정하지 않았다.

- 별도 임시 스킬은 서버/클라이언트 모두 최대 레벨 10이다. 남은 SP 2로 레벨 3까지만 올려 SP 0을 만든 뒤, 레벨 4 비용 1인 요청을 보냈다. UI의 로컬 요청 차단과 로컬 검증을 우회한 실제 소유 RPC의 서버 거부를 모두 확인했다. 상한 거부와 SP 부족 거부를 혼동하지 않는다.
- 각 클라이언트에서 실제 다른 플레이어 pawn/컴포넌트가 존재하는지 확인한다. 서버가 양쪽의 비기본 레벨/선택/SP를 유지하는 동안, 공개 PlayerState Score를 테스트 동기화 표식으로 사용해 검사 시점을 확인한다. 모든 소유자가 1초 이상 검사한 뒤에만 진행하며 Score는 완료 시 원래 값으로 복원한다.
- 비소유자 컴포넌트의 복제 backing array는 빈 상태, revision/UsedSP는 0, TotalSP는 해당 archetype의 기본값이다. 로컬 조회도 기본 레벨/미선택 배열을 유지한다. 첫 실행에서 미선택 기본 배열 `{-1,-1,-1}`을 빈 배열로 간주한 테스트 오류를 수정했고, 이후 아래 두 실행이 모두 성공했다.
- 일반 서버+2클라이언트 성공: `Saved/Tests/Skill/PrivacyNetwork-20261004-01/Saved/Logs/IrisMultiClient/20261004-222253/`
- 송신 방향별 100~150ms 지연/5% 패킷 손실 서버+2클라이언트 성공: `Saved/Tests/Skill/PrivacyNetwork-20261004-01/Saved/Logs/IrisMultiClient/20261004-222507/`
- 두 실행 모두 8개 요청 거부, 비소유자 선택/SP 비노출, 레벨 하향 시 선택 해제, 두 스킬 초기화와 SP 5 전액 환급을 확인했다. C++ 컴파일/링크는 격리 프로젝트의 DLL에만 반영했다. 원본 DLL 교체나 핫리로드는 하지 않았으므로, 원본에서 새 검증을 실행하려면 최신 소스로 Editor 모듈을 빌드해야 한다.
- 개인정보 검사에는 실제 상대 플레이어가 필요하므로 이 옵션은 최소 2클라이언트를 요구한다. 1클라이언트 요청이 UE 실행 전에 거부되는 것도 확인했다.

최신 소스로 Editor 모듈 빌드 후 재검증 명령:

```powershell
& .\Scripts\Network\Run-IrisMultiClientTest.ps1 -SkipBuild -VerifySkillTripods -ClientCount 2
```

이 옵션은 다른 입력/이동 probe와 함께 사용하지 않는다. SP 부족 거부와 PlayerSkillComponent 선택/SP 정보의 소유자 전용 복제는 확인했다. 실제 스킬창 버튼과 선택된 트라이포드의 실제 GAS 활성화를 결합한 멀티클라이언트 검증은 아직 남아 있다. 공개 공격 연출/피해로부터 스킬 특성을 추측할 수 없다는 의미도 아니다.

### 실제 스킬창 연결 수정 (2026-10-05)

- 에디터 MCP로 `/Game/Maps/testmap`의 실제 `WBP_SkillWindow`를 생성해 확인했다. ViewModel에는 네 스킬 이름과 SP 50이 있었지만 목록은 `Skill Name`/`Skill Type` 기본값을 표시했고, 레벨 조절기 네 개의 Main/Slot ViewModel은 모두 null이었다. 해당 WBP의 MVVM View도 없었다.
  - 수정 전 화면: `Saved/Tests/Skill/EditorPIE-20261005/SkillWindowActual.png`
- 목록 항목이 자신의 Slot ViewModel을 받아 이름·아이콘·합성된 시전 타입을 표시하고, 자식 레벨 조절기에 같은 Main/Slot ViewModel을 연결하도록 수정했다. 레벨/SP 변경 시 현재 레벨·비용·버튼 활성 상태를 갱신하고 SP 부족/최대 레벨에서 증가 버튼을 비활성화한다.
- 스킬창에 런타임 SP 표시를 추가하고, 목록 선택을 실제 상세창의 TripodBoard에 전달한다. 옵션이 없는 트라이포드 버튼은 숨기고 잠긴 옵션은 비활성화한다. 재사용되는 목록/조절기/보드의 이전 모델 구독도 정리한다. 서버 SP/선택 규칙이나 D1 소스/에셋은 변경하지 않았다.
- 변경한 C++와 생성 코드의 컴파일 및 격리 DLL 링크를 확인했다. 검증용 프로젝트는 `Saved/Tests/Skill/EditorUI-20261005-01/`이며 원본 DLL을 교체하거나 핫리로드하지 않았다.
- 수정된 격리 DLL로 `ProjectRPG.Skill.` 자동 테스트 19개를 재실행해 모두 성공했다(경고 포함 성공 1개, 실패/미실행 0개). 보고서: `Saved/Tests/Skill/EditorUI-20261005-01/Report/index.json`. 이 결과는 실제 화면/마우스 조작 검증의 성공을 의미하지 않는다.
- `Scripts/Editor/VerifySkillWindowPIE.py`는 실제 위젯의 레벨 버튼 핸들러·표시·SP 차감·상한/예산·전액 환급을 검사하고 테스트 상태를 복원한다. authority PIE용이며 실제 마우스 입력이나 네트워크 RPC 전송 검증을 대신하지 않는다. 10월 5일에는 Python 구문 검사까지만 통과했으며, 실제 위젯 실행 성공과 별도 마우스 검증은 아래 10월 6일 결과에 기록했다.
- 격리 실행의 한계: 첫 에디터는 DDC 쓰기 노드가 없어 초기화에서 종료됐다(`EditorUI.log`). 메모리 캐시/NullRHI 실행은 PIE까지 시작했지만 Slate 창의 크기가 0이어서 콘솔 입력과 실제 화면 검증이 성립하지 않았다(`EditorUI-MemoryDDC.log`). 렌더링 실행에서 셰이더 컴파일을 끈 시도는 엔진의 `AllowShaderCompiling()` assertion으로 종료됐다(`EditorUI-Rendered.log`). 이를 스킬 코드의 실패나 UI 검증 성공으로 분류하지 않는다. 캐시/렌더링 설정이나 D1 에셋을 수정하지 않고 검증용 실행만 종료했다.

정상적으로 렌더링되는 에디터에 최신 Editor 모듈을 빌드/로드하고 새 단일 플레이어 PIE(테스트할 첫 스킬 레벨 1)를 시작한 뒤, 에디터의 Cmd 입력에서 재검증한다:

```text
py "C:/UE5/Project_RPG/Scripts/Editor/VerifySkillWindowPIE.py"
```

`[RPG_SKILL_UI] PASS`가 기록되기 전에는 실제 위젯 검증을 완료로 표시하지 않는다. 현재 일반 게임플레이에서 스킬창을 여는 입력 경로와 네 기본 Definition의 실제 트라이포드 구성도 별도로 확인/연결해야 한다.

### 실제 레벨 조절기 WBP 회귀 검증 (2026-10-05)

- `ProjectRPG.Skill.UI.LevelAdjusterWidgetState`를 추가했다. 실제 `WBP_SkillLevelAdjuster`의 위젯 트리를 초기화해 네이티브 표시와 구독을 검사한다. 임시 Component/Definition/ViewModel에 복제 스냅샷을 넣고 실제 OnRep를 호출하므로 운영 플레이어, 에셋, 게임 인스턴스의 카탈로그는 변경하지 않는다.
- 검증 완료: 다른 스킬이 SP를 소모/환급할 때 현재 행의 증가 버튼 갱신, 다음 비용이 그대로인 레벨 변경의 표시 갱신, 최대 레벨의 비용 숨김/증가 차단, 초기화 표시, 다른 슬롯으로 재연결 및 이전 비용 구독 해제, null 모델로 해제한 뒤의 버튼 차단/표시 정리.
- 신규 테스트는 경고/오류 없이 성공했다. 수정한 테스트 코드의 컴파일·격리 DLL 링크 후 `ProjectRPG.Skill.` 전체 20개 성공(경고 포함 성공 3개, 실패/미실행 0개).
  - 보고서: `Saved/Tests/Skill/WidgetState-20261005-01/Report/index.json`
  - 로그: `Saved/Tests/Skill/WidgetState-20261005-01/Regression.log`
- 이는 렌더링/마우스 클릭, 목록 항목 자동 생성·연결, 실제 RPC 전송 검증이 아니다. `VerifySkillWindowPIE.py`와 별도 마우스 검증은 아래 10월 6일에 실행했으며, 멀티클라이언트 시전 결합 검증은 남아 있다. 캐시/렌더링 설정, 운영 스킬 코드, D1 및 원본 DLL은 이 회귀 테스트 추가에서 수정하지 않았다.

### 실제 렌더링 PIE·마우스 조작 검증 (2026-10-06)

- 정상 렌더링 중인 사용자 에디터에서 Unreal MCP로 `/Game/Maps/testmap`의 단일 플레이어 authority PIE를 시작했다. 이미 로드된 루트 Editor 모듈을 사용했으며, 이번 검증에서 C++ 변경·빌드·DLL 교체·핫리로드는 하지 않았다.
- 검증 스크립트의 Python API 오류를 수정했다. `RPGPlayerController.get_pawn()` 대신 `GameplayStatics.get_player_pawn(world, 0)`으로 플레이어와 authority를 확인한다. 실제 `WBP_SkillWindow` 및 네 목록 항목의 생성·연결, 이름·현재 레벨·SP 표시, 버튼 핸들러의 차감·환급 검증이 성공했다: `[RPG_SKILL_UI] PASS Names=4 ButtonLevel=1 SPDisplay=1 BudgetOrCap=1 FullRefund=1`.
- 스크립트 실행과 별도로 Slate의 실제 마우스 이벤트를 전달하고 ViewModel과 화면 상태를 함께 대조했다. 한 번 증가 시 레벨 1→2/SP 50→49, 한 번 감소 시 레벨 2→1/SP 49→50을 확인했다.
- 여러 스킬을 레벨 `{10,10,6,1}`로 올려 잔여 SP 1을 만들었다. 다음 비용이 6·6·2인 세 스킬의 증가 버튼은 비활성화되고 비용 1인 Whirl Wind만 활성화됐다. 비활성화된 Jump Smash 버튼을 클릭해도 레벨 6/SP 1이 유지됐다.
- Whirl Wind를 실제 클릭해 SP 1→0으로 만들자 네 증가 버튼이 모두 비활성화됐다. 실제 감소 버튼 클릭으로 SP 1/레벨 1을 복구했다.
- 네 기본 Definition의 실제 최대 레벨은 모두 **12**다. 별도 핸들러 호출로 Assult Blade를 레벨 12까지 올려 SP 17이 남아도 다음 비용 0/비용 표시 숨김/증가 버튼 비활성화를 확인했다. 해당 비활성 버튼의 실제 마우스 클릭도 레벨/SP를 변경하지 않았다. 레벨 10의 다음 비용 6은 상한 불일치가 아니라 정상적인 레벨 11 비용이다.
- 테스트 종료 시 버튼 핸들러로 네 스킬을 모두 레벨 1/SP 50으로 원복하고 임시 스킬창과 이번 검증의 Slate observer를 제거했다. 테스트용 PIE는 종료했으며 사용자 에디터와 MCP는 유지했다. Definition·WBP·D1 에셋은 저장하거나 수정하지 않았다.
  - 실행 로그: `Saved/Tests/Skill/EditorPIE-20261006/EditorSession.log` (`FINAL_RESTORE_PASS`, `TEMP_WINDOW_REMOVED` 포함)
  - 실제 표시 화면: `Saved/Tests/Skill/EditorPIE-20261006/SkillWindowAfter.png`
  - 잔여 SP 1 화면: `Saved/Tests/Skill/EditorPIE-20261006/SkillWindowBudget.png`
- 이번 결과는 레벨/SP 조작과 UI 연결의 완료 증거이며, 실제 트라이포드 선택·GAS 시전·멀티플레이 결합 검증은 아니다. 현재 WBP의 선택 배경/글자 대비와 빈 아이콘·버튼 기호 등 시각적 가독성은 별도 정리가 필요하다. 로그의 기존 D1 애니메이션, 공용 버튼 및 다른 UI 관련 오류/경고를 전체 게임 검증 성공으로 취급하지 않는다.

### 일반 스킬창 입력 경로 연결 (2026-10-09)

- `URPGSkillWindowComponent`에 로컬 스킬창 생성·닫기, 입력 바인딩, 커서/포커스와 이동·시점 입력 잠금의 수명을 분리했다. 컨트롤러는 입력 연결과 전투 입력 차단만 담당하며, GAS 시전 정책과 서버의 레벨/SP/트라이포드 규칙은 변경하지 않았다.
- `InputTag.ShowSkillWindow`를 추가했다. 입력 설정에 해당 액션이 있으면 사용하고, 없으면 런타임 전용 Enhanced Input 액션/컨텍스트로 기본 **K**를 제공한다. 기존 입력 옵션 창에 키 변경 항목을 연결했고, 저장된 키를 재사용한다. 새 입력 에셋이나 WBP/D1 에셋의 저장은 필요하지 않다.
- 토글은 `Started`에 바인딩해 키를 계속 누를 때 반복 개폐하지 않는다. 창은 ESC·현재 단축키·기존 닫기 버튼으로 닫히며 키 반복 이벤트는 소비한다. ESC로 닫은 뒤 같은 키를 누르고 있다는 이유로 메뉴가 다시 열리지 않도록 메뉴 토글도 `Started`를 사용한다.
- 창이 열릴 때 눌려 있던 GAS 입력을 해제하고 새 스킬/아이템/상호작용·줌·타깃 전환 입력을 차단한다. 스킬 키 해제는 계속 전달한다. 닫을 때 이 창이 추가한 이동·시점 잠금만 해제하고 이전 커서/포커스를 복원한다. 소유 pawn 변경·컨트롤러 종료 시에도 정리한다. 기존 입력 모드 복원은 이전 커서 표시 여부를 기준으로 하므로 모든 종류의 모달 UI 중첩 복원을 보장하는 것은 아니다.
- Unreal Header Tool, 변경된 C++ 및 생성 코드의 컴파일, 별도 검증 DLL 링크가 통과했다. 일반 UBT 실행은 사용자 AppData에 대한 쓰기 제한으로 중단되어 기존 컴파일/링크 응답 파일을 이용했다. 검증 결과를 원본 DLL 교체나 전체 프로젝트 UBT 빌드 성공으로 해석하지 않는다.
- 별도 검증 프로젝트의 `ProjectRPG.Skill.UI.` 테스트 **3개가 모두 성공**했다. 새 `CloseInput` 테스트는 실제 `WBP_SkillWindow`를 초기화하고 네이티브 Slate 키 이벤트로 단축키 반복 입력 유지, 새 키 누름으로 닫기, 닫기 키 변경, ESC 닫기를 확인한다. 기존 레벨 조절기 상태·반응형 초기화 테스트도 성공했다.
  - 검증 프로젝트/로그: `Saved/Tests/Skill/WindowInput-20261009/`, `CloseInputFinal.log`
  - 실행 중인 사용자 에디터는 유지했다. 원본 Editor DLL의 SHA-256은 검증 전후 `F75B263DB28249A3D41AE26E95E476C61F751B272668E136E7104D2E4751C48F`로 동일하다. 핫리로드·원본 DLL 교체·D1 소스/에셋 변경은 하지 않았다.
- **남은 확인:** 최신 소스로 정상 Editor 모듈을 빌드하고 에디터를 다시 시작한 뒤, 실제 PIE에서 K/변경 키로 열기·닫기, 커서와 입력 잠금 복원, 누른 스킬 키를 유지한 채 창 열기, 키 설정 저장/재시작 복원을 확인한다. 이번 테스트는 컨트롤러에서 창이 열리는 실제 게임 입력이나 렌더링·마우스·RPC 결합 검증이 아니다.

### 트라이포드 버튼 선택–시전 연결 검증 (2026-10-10)

- 실제 트라이포드 버튼이 `CommonButtonBase::SetIsSelected`와 슬롯 ViewModel의 선택 상태를 함께 사용하던 문제를 수정했다. CommonUI의 자동 선택/포커스 선택은 끄고, 모델이 알리는 선택 상태를 `SetSelectedInternal`로 반영한다. 옵션 변경·거절된 선택 복원·티어 재잠금에서 선택 표시가 남거나 어긋나지 않으며, 이미 선택한 옵션을 다시 눌러도 표시만 해제되지 않는다. 버튼 재초기화 시 이전 선택 상태도 정리한다. 선택 검증/RPC/GAS 정책의 책임은 기존 컴포넌트에 유지했다.
- `ProjectRPG.Skill.Integration.TripodButtonExecution`을 추가했다. 실제 `WBP_TripodBoard`/`WBP_TripodButton`과 임시 네이티브 플레이어·ASC·게임 인스턴스의 카탈로그를 사용한다. 버튼은 CommonUI의 `HandleButtonClicked`로 처리하고, 레벨/선택은 기존 ViewModel 명령으로 컴포넌트에 전달한다. GAS 입력 경계에서 시전한 실제 몽타주의 `Attack`/`Prepare` 섹션과 실행 정책을 확인한다.
- 신규 테스트가 확인하는 항목: 레벨 1의 선택 차단, 레벨 4의 티어 활성화, 없는 옵션 숨김, 즉발 선택과 시전, 다른 옵션으로 변경 후 다음 시전의 차지 복원, 현재 시전의 실행/레벨 스냅샷 유지, OnRep로 전달한 선택 복원, 레벨 하향 시 선택 해제/버튼 차단, SP 전액 환급 및 모델 해제.
- 비렌더링 테스트에서는 두 WBP 생성 클래스의 `bCanCallInitializedWithoutPlayerContext`를 실행 중에만 허용해 네이티브 초기화를 거치고, 종료 시 원래 값으로 복원한다. 임시 카탈로그의 Definition은 복제본이며, 에셋 파일이나 실제 게임 인스턴스의 카탈로그는 저장/변경하지 않는다. 이 테스트는 별도 자동화 실행용이며 활성 PIE 중 실행은 피한다.
- 수정 전 버튼 코드로 동일한 테스트를 실행했을 때 **네이티브 선택 상태 관련 assertion 5개가 실패**했고, 수정 후 신규 테스트는 성공했다. 초기 검증 실행의 위젯 컨텍스트 누락과 LocalPlayer 생성 오류는 테스트 환경 실패이며 기능의 재현 증거로 사용하지 않는다.
  - 재현 로그: `Saved/Tests/Skill/TripodUI-20261009/Reproduction.log`
  - 수정 후 신규 테스트 로그: `Saved/Tests/Skill/TripodUI-20261009/WidgetInitialization.log`
- 최종 관련 회귀 실행에서 `ProjectRPG.Skill.Integration.` 4개와 `ProjectRPG.Skill.UI.` 3개, **총 7개 테스트가 모두 성공**했다. 입력 해제/취소/쿨다운, 플레이어 구성·선택 스냅샷, 신규 트라이포드 버튼 연결, 창 닫기 입력, 레벨 조절기 상태와 반응형 초기화를 함께 확인했다. 로그: `Saved/Tests/Skill/TripodUI-20261009/Regression.log`.
- 변경한 C++ 컴파일과 별도 검증 DLL 링크가 통과했다. 사용자 프로젝트의 DLL을 이 검증으로 교체하거나 핫리로드하지 않았으며, D1 소스/에셋도 변경하지 않았다. 게임 인스턴스 초기화 중 기존 콘텐츠 로드/외부 서비스 연결 경고가 발생했으므로 전체 콘텐츠가 정상이라는 의미는 아니다.
- **남은 확인:** 실제 단축키로 연 스킬창의 렌더링·마우스 클릭 및 선택된 트라이포드의 멀티클라이언트 GAS 시전 결합. 위 OnRep 선택 복원은 스냅샷 전달로 검증했으며 실제 RPC 왕복 검증을 대신하지 않는다. 운영 Definition의 트라이포드 구성도 아직 별도 작업이다.

### 트라이포드 버튼–RPC–GAS 멀티클라이언트 연결 검증 (2026-10-10)

- 기존 `-VerifySkillTripods` 검증에 실제 `WBP_TripodBoard`/`WBP_TripodButton`의 CommonUI 클릭 핸들러를 연결했다. 로컬 플레이어 문맥으로 위젯을 생성하고 기존 Slot ViewModel 명령으로 실제 소유 RPC를 보낸다. 선택 승인/거절과 레벨 하향 뒤의 네이티브 버튼 선택·잠금 상태를 복제 데이터 및 ViewModel과 함께 검사한다.
- 전용 Iris 서버와 **실제 클라이언트 2개에서 최종 검증이 성공**했다. 각 소유자와 서버에서 옵션 0의 즉발 정책·레벨 4·배율 2·몽타주 `Attack`, 옵션 1의 차지 정책·레벨 4·배율 3·`Prepare`를 확인했다. 차지 입력 해제로 `Attack`에 진입하고 GAS 종료 이벤트의 `Cancelled=0`까지 확인했다. 즉발은 서버 시전 확인 후 검증용으로 취소했으므로 즉발의 정상 완주를 검증한 것은 아니다.
- 기존 8개 서버 거부 사례, 잘못 예측한 선택의 버튼 복원, 비소유자에게 선택/SP가 복제되지 않는지에 대한 양성 대조군, SP 부족 거부, 레벨 하향의 티어 해제, SP 전액 환급도 두 클라이언트에서 성공했다. 실행 스크립트는 양쪽 정책/섹션·정상 종료 증거가 없는 이전 완료 표식을 성공으로 인정하지 않는다.
- Definition과 트라이포드는 프로세스별 임시 복제본이다. 이미 생성된 비활성 능력 인스턴스만 같은 임시 Definition에 연결하고, 검증용 pawn의 공격 속도만 낮춰 서버 응답 사이에도 몽타주 구간을 관찰한다. 능력 CDO·Definition·WBP·몽타주·D1 에셋은 저장/수정하지 않는다. 종료 시 입력 해제와 버튼 모델/능력 종료 이벤트 구독을 정리한다.
- 재시전은 고정 벽시계 지연이 아니라 실제 GAS `CheckCooldown`을 기다린다. **기본/최소 1초 설정과 별개로 기존 능력 에셋의 더 긴 쿨타임이 유지되는 구조**도 확인했다. 최종 실행의 재시전 대기 로그는 약 23.9초였으며, 이를 지우거나 우회하지 않았다. 모든 기존 능력의 실제 쿨타임을 정확히 1초로 통일한 결과는 아니다.
- 변경 C++ 컴파일과 별도 검증 DLL 링크가 통과했다. 최종 DLL SHA-256: `6397F7435D5696D9EB368E899D8394F9801D11B88E2DF23A724C90F4F81FEC21`. 실행에는 기존 검증 스크립트의 사본에 로컬 DDC/UserDir과 초기 로드 여유만 적용했다. 원본 DLL 교체·핫리로드·사용자 에디터 실행은 하지 않았다.
  - 최종 성공 로그: `Saved/Tests/Skill/TripodActivation-20261010-01/Saved/Logs/IrisMultiClient/20261010-143320/`
  - 격리 실행 스크립트: `Saved/Tests/Skill/TripodActivation-20261010-01/RunTripodActivation.ps1`
  - 초기 고정 지연 재시전 실패(`20261010-040812`)와 종료 후 초기화된 활성 스냅샷을 검사한 실패(`20261010-142626`)는 검증 코드에서 수정했다. 초기화만 수행한 중단 실행과 서버 로드 시간 초과 실행도 성공 증거에서 제외한다.
- **검증 범위:** 비렌더링 위젯 클릭 핸들러 → ViewModel → 실제 RPC/소유자 복제 → ASC 입력 경계 → 실제 GAS 몽타주/종료다. 일반 K 단축키로 연 스킬창의 렌더링·마우스 라우팅, 키보드/Enhanced Input부터 이어지는 이번 시전 결합, 운영 트라이포드 데이터, 신규 결합 경로의 지연/손실 조건은 아직 별도 확인이다. 기존 CommonUI 뷰포트/콘텐츠 로드 경고도 남아 있으며 D1이나 공용 UI 설정을 이번 작업에서 변경하지 않았다.

### 신규 스킬 쿨타임 단일화 (2026-10-11)

- 신규 `SkillContainer`가 `GetCooldownGameplayEffect()`를 재정의해 상속받은 에셋 쿨타임 GE를 사용하지 않도록 했다. 기존 플레이어 기반 클래스의 Definition별 GAS 쿨타임 적용·확인·잔여 시간 조회는 유지한다. 기본/최소 1초이며, Definition의 명시적 긴 값과 트라이포드 합성은 보존한다. D1 스킬·에셋 및 일반 능력의 기존 쿨타임 구현은 변경하지 않았다.
- `ProjectRPG.Skill.Integration.DefinitionCooldownAuthority`를 추가했다. 에셋의 기존 쿨타임 클래스는 읽기만 하고 임시 능력 인스턴스에 주입한다. 수정 전 실제 24초 적용 및 1초 후 재시전 차단이 재현됐고, 수정 후 기본 1초, 취소 뒤 쿨타임 유지, 다른 능력의 긴 GE와 분리, 다음 활성화의 명시적 4초, 두 잔여 시간 API 일치 및 GE 중첩 방지가 통과했다.
- 쿨타임 1개·통합 5개·UI 3개, **관련 회귀 테스트 9개가 모두 성공**했다. 수정 C++와 격리 DLL 링크도 성공했다. 정상 프로젝트 전체 빌드·핫리로드·원본 DLL 교체를 수행한 것은 아니다.
- 서버 + 클라이언트 2개에서 기존 버튼→RPC→GAS 검증을 재실행해 성공했다. 서버의 즉발 확인 후 약 1.03초 뒤 두 소유자의 차지 재시전을 확인했으며, 차지 정상 종료·8개 거부·소유자 전용 복제·SP 환급도 유지됐다.
- **임시 Definition 검증의 한계:** 프로세스별 복제본은 네트워크에서 안정적으로 참조할 에셋 경로가 없다. GAS 예측 효과 정리 후 서버 효과의 SourceObject로 클라이언트 복제본을 조회할 수 없어, 클라이언트 확인만으로 재시전하면 서버의 1초 쿨타임보다 앞서 요청하는 실패가 발생했다. 테스트에 서버의 실제 `CheckCooldown` 통과 체크포인트를 추가했으며, 고정 시간 대기나 서버 쿨타임 우회는 하지 않았다. 이 체크포인트는 검증 전용이며, 운영 에셋을 사용한 클라이언트 예측 쿨타임/UI 복제 검증을 대신하지 않는다.
- 기록: `Saved/Tests/Skill/CooldownPolicy-20261011-01/Baseline.log`(수정 전 실패), `Regression.log`(9개 성공), `Saved/Logs/IrisMultiClient/20261011-135620/`(최종 네트워크 성공). `20261011-135217/`은 체크포인트 추가 전 실패이므로 성공 증거에서 제외한다. 최종 격리 DLL SHA-256: `E0E945A20E60A63D5084CAB722DE9244F01B44934A1D6DCE5F621B501C314D26`.

### 원본 Definition의 쿨타임 예측·복제 검증 (2026-10-11)

- 기존 트라이포드 검증 뒤에 원본 `/Game/Blueprints/GameData/Skill/DA_Skill_ChargeSlash`를 사용하는 별도 쿨타임 검증을 연결했다. 격리 프로세스의 비활성 능력 인스턴스에 Definition 참조만 복원하며, 에셋·CDO·카탈로그·몽타주는 수정하지 않는다. 이 원본 에셋의 안정적인 네트워크 참조를 통해 서버 GE의 SourceObject를 소유 클라이언트에서 실제로 조회했다.
- Iris 전용 서버 + 클라이언트 2개에서 **각 소유자의 로컬 예측 1초 GE → 서버에서 수신한 1초 GE → 취소 뒤 재입력 차단 → 만료 뒤 재활성화와 새 GE 수신**이 통과했다. 서버에서도 서로 다른 두 컨트롤러의 첫 시전·재시전을 확인했다. 복제 성공은 예측 효과만으로 판단하지 않고 `PredictionKey.WasReceived()`와 원본 SourceObject·효과 개수·지속 시간으로 확인한다.
- 클라이언트의 `CheckCooldown`, `GetCooldownTimeRemaining`, `GetCooldownTimeRemainingAndDuration`이 일치했고, 취소 후에도 쿨타임이 남으며 만료 후에는 효과/잔여 시간이 사라졌다. 재입력 차단과 조회 검증은 클라이언트의 실제 GAS 상태로 판단한다. 마지막 성공 시전만 서버 만료 체크포인트와 동기화해 시계 경계 경쟁을 피했다. **렌더링된 HUD의 쿨타임 표시나 키보드부터 이어지는 입력 검증은 아니다.** 원본 에셋 시전은 확인 후 테스트에서 취소하므로 원본 액션의 정상 완주도 이 검증 범위가 아니다.
- 첫 실행(`20261011-141353`)은 효과의 예측 키가 활성화 키와 같다고 가정해 시간 초과됐다. TargetData는 별도 scoped prediction key를 사용하며, 최종 로그는 로컬 예측 키/활성화 키 `9`, 수신된 효과 키 `10`을 확인했다. 검증 코드의 잘못된 동일성 조건을 수정했으며, 운영 예측/쿨타임 코드는 이번 검증을 위해 변경하지 않았다.
- 기존 버튼→RPC→즉발/차지 GAS 검증, 차지 해제 후 정상 종료, 거부 8건, 소유자 전용 선택/SP, 전액 환급도 같은 실행에서 유지됐다. 변경된 C++ 3개 컴파일과 격리 DLL 링크가 통과했다. 최종 성공 로그: `Saved/Tests/Skill/AssetCooldown-20261011-01/Saved/Logs/IrisMultiClient/20261011-141809/`. 실행 사본은 같은 격리 폴더의 `RunAssetCooldown.ps1`, 최종 링크 응답은 `LinkLatest.rsp`다. DLL SHA-256: `4E52EBD6A5A164F5C35C4410C278E7DCB4A53FC874892DC13E40F78AC7EBA12F`. 공유 빌드 산출물 갱신 중 발생한 링크 실패와 첫 시간 초과 실행은 성공 증거에서 제외한다. 정상 전체 빌드·원본 DLL 교체·핫리로드·D1 변경은 수행하지 않았다.

### MCP 연결 및 HUD 쿨타임 연결 점검 (2026-10-12)

- 정상 루트 프로젝트 에디터와 `http://127.0.0.1:8000/mcp`의 실제 MCP 초기화·도구 조회·호출을 확인했다. 현재 대화에 Unreal 전용 도구가 직접 등록돼 있지 않아 로컬 HTTP MCP 요청으로 호출했다. 에디터 콘솔 실행 명령은 엔진 플러그인 소스 기준 `ModelContextProtocol.StartServer`(선택적으로 포트 인자 지정)다. 서버가 켜져 있는 것과 대화의 도구 목록에 등록돼 있는 것은 구분한다.
- 처음에는 PIE가 없었으나, 맵 조회 도중 같은 에디터에서 다른 작업의 인벤토리 PIE 검증이 시작됐다. `SceneTools.load_level`은 내부 에셋 조회가 PIE 중 제한돼 디스크에 존재하는 testmap도 없다고 보고했다. 루트 프로젝트 경로와 `IsPIERunning=true`, 다른 작업의 `[RPG_INVENTORY_UI] PASS` 로그로 원인을 구분했다. 스킬 검증을 위해 다른 작업의 PIE를 종료하거나 키 입력을 이어가지 않았다.
- MCP로 기존 `TPWBP_AbilityIconSlot` 그래프를 읽었다. `AssignOnAbilityCooldownBegin`이 `OnAbilityCooldownBegin_Event`에 연결되고, 입력 태그 비교 뒤 `CountDown` → 쿨타임 텍스트/머티리얼 갱신으로 이어진다. 네이티브 소스에서 이 알림의 발행은 기존 `RPGPlayerGameplayAbility::CoolDown()`에만 있으며, 신규 컨테이너는 `CommitAbility()`를 직접 호출한다. 원본 `GA_Player_ChargeSlash` 그래프에서도 기존 CoolDown 호출은 발견되지 않았다. **신규 컨테이너의 HUD 시작 알림 누락 경로를 확인했지만, 실제 HUD 시전 재현·수정·플레이 통과로 보고하지 않는다.** 기존 `CoolDown()`을 단순 호출하면 중복 커밋이 생기므로 확인 없이 연결하지 않는다.
- 이번에는 운영 소스·D1·에셋을 수정하거나 빌드/핫리로드를 하지 않았다. 다음 단계는 다른 에디터 작업과 입력 제어를 조율한 뒤 스킬 시전 시 HUD 표시를 재현하고, 필요하다면 신규 경로만 대상으로 GAS 커밋과 UI 알림을 분리해 수정하는 것이다. 일반 K 입력·실제 클릭·커서/잠금 복원 검증도 여전히 미완료다.

### 시작 시 직업 선택창 자동 표시 제거 (2026-10-12)

- 사용자 요청으로 `ARPGHUD::BeginPlay`의 자동 표시 호출과 `TryDisplayInitialClassSelection` 재시도 함수를 제거했다. 수동 표시 API와 위젯/직업 데이터는 유지하며, 임의로 직업을 선택하거나 D1을 변경하지 않았다. 같은 HUD에 있던 다른 작업의 옵션 메뉴 provider 변경도 보존했다.
- 변경된 `RPGHUD.cpp`의 개별 컴파일은 종료 코드 0으로 통과했다. 격리 응답/오브젝트는 `Saved/Tests/Skill/StartupUI-20261012/`에 있다. 정상 모듈 빌드·DLL 교체·핫리로드는 하지 않았으므로 실행 중인 에디터에는 아직 이 수정이 반영되지 않았다.
- PIE가 종료된 것을 확인한 뒤 MCP로 스킬 입력 검증을 위한 PIE 시작을 요청했다. 로그에 기존 `ABP_Gladiator_AnimLayers_Base/Bow/OneHandSword` 컴파일 경고가 나온 이후 요청이 시간 초과됐고 후속 MCP 호출도 응답하지 않았다. 창의 상태와 경고가 원인인지는 미확정이며 사용자 확인을 요청했다. 에디터 강제 종료나 해당 애니메이션 에셋 수정은 하지 않았다. 이번 시도는 K 입력·마우스·커서 복원 또는 자동 표시 제거의 플레이 통과 증거가 아니다.

### Gladiator 참고 에셋과 기본 실행 연결 분리 (2026-10-12)

- 사용자 승인 범위는 Gladiator 원본을 참고용으로 유지하고 현재 실행에 불필요한 연결만 분리하는 것이다. MCP 참조 조회에서 `testmap` → `BP_GameModeBase` → `BP_Player` → `ABP_Player`의 기본 플레이어 경로를 확인했다. Gladiator 레이어는 기존 아이템 템플릿·방어구·다른 레이어가 참조하므로 삭제·이동·일괄 재부모화하지 않았다.
- `ARPGHUD` 생성자의 고정 Gladiator 직업창 soft class 경로를 제거했다. `ClassSelectionWidgetClass`는 이제 기본적으로 비어 있으며, 명시적으로 설정한 경우에만 수동 표시 API로 로드한다. 비어 있으면 위젯 생성 전에 반환한다. 앞서 제거한 시작 시 자동 표시도 유지했다. 스킬/무기 애니메이션이나 D1 코드, 다른 작업의 옵션 메뉴 provider 변경은 수정하지 않았다.
- 현재 경고는 열린 `Base/Bow/OneHandSword`의 깨진 캐스트·조준 변수·애니메이션 참조와 연결돼 있었다. UE `ResolveDirtyBlueprints`는 현재 맵의 참조 여부와 별개로 로드된 블루프린트를 검사하므로, HUD 의존성 제거만으로 이미 로드된 BP 오류가 해결됐다고 보고하지 않는다. 대상 에셋 편집기 3개만 닫았으며 MCP의 열린 에셋 목록이 빈 것을 확인했다. 저장·강제 언로드·전역 자동 컴파일 해제·에디터 종료는 수행하지 않았다.
- 수정한 HUD의 개별 컴파일은 종료 코드 0으로 통과했다. Gladiator 애니메이션 폴더의 11개 원본 파일 SHA-256이 작업 전후 모두 일치했다. 정상 모듈 빌드/재시작 후 시작 UI와 PIE 경고 여부를 확인해야 하며, 기존 AnimBP 자체의 컴파일 문제를 복구한 것은 아니다. MCP 연결은 다시 응답하고 있고 이번 작업에서 PIE를 재시작하지 않았다.

### 에디터 종료 후 정상 빌드 반영 확인 (2026-10-12)

- 사용자 에디터 종료를 확인하고 정상 빌드를 시도했다. 이 대화에서는 UBT의 AppData 쓰기 제한으로 컴파일 전에 중단됐고, 지원되는 프로젝트 설정 캐시·세션 옵션을 사용한 재시도도 UE 5.8 UBA의 재귀 실행 제한으로 실패했다. 엔진 코드나 권한은 변경하지 않았다.
- 그 사이 공유 작업 폴더의 다른 정상 빌드가 최신 HUD를 컴파일·링크했다. `Saved/Logs/RaidAreaStrikeBuild-20261012.log`에 `RPGHUD.cpp` 컴파일과 루트 DLL 링크가 있으며, 별도 플러그인 링크 오류 후 `Saved/Logs/RaidAreaStrikeLink-20261012.log`에서 최종 `Result: Succeeded`를 확인했다. HUD 오브젝트와 DLL은 수정한 소스보다 최신이며 타깃 메타데이터도 갱신됐다. 중복 빌드는 멈췄다. 실제 시작 UI·PIE 경고·K 입력 통과를 의미하지는 않는다.
- 최신 DLL로 `ProjectRPG.Skill.UI.CloseInput` 회귀를 실행했고 성공 결과·종료 코드 0을 확인했다. authored WBP의 네이티브 핸들러에서 K 반복 입력 유지, 새 K 닫기, J 재매핑, Escape 닫기가 통과했다. 로그는 `Saved/Tests/Skill/StartupUI-20261012/CloseInput.log`다. DLL SHA-256은 테스트 전후 `B73DCDD7A317086196F005A85CD1B646313467A5974B4D14C55F2F4550A8FB11`로 동일했다. 초기화 중 AutoSDK의 AppData 쓰기 오류와 기존 경고가 있었으므로 전체 로그가 깨끗한 것으로 보고하지 않는다. headless 테스트이며 실제 Enhanced Input K 열기·렌더링된 UI 클릭·입력 잠금/커서 복원은 미검증이다. 다음은 GUI 에디터를 다시 열고 새 MCP 세션에서 실제 PIE 입력을 확인하는 것이다. D1/Gladiator 원본은 변경하지 않았다.

### Gladiator 원본 오류 범위 점검 (2026-10-12)

- 사용자가 원본 오류 정리 필요성을 제기해 최신 `Saved/Logs/Project_RPG.log`를 읽기 전용으로 점검했다. 이 로그는 01:10:13에 새로 시작됐고 최신 루트 모듈을 로드했다. `Entry` 이후 `/GladiatorCore/Maps/DevMap`을 여는 기록이 있으며, 이 과정에서 구형 콘텐츠 오류가 다수 로드됐다. 기본 시작 맵이 DevMap이거나 신규 스킬이 이를 요구하는 것으로 해석하지 않는다.
- 로그의 Gladiator `Failed to load Class ... as Parent` 경로는 중복 제거 시 19종, `While trying to load package /GladiatorCore ... a dependent package ... was not available` 경로는 55종이었다. 문·상자·상호작용 능력 등 누락된 D1 기반 클래스와 원본 게임의 애니메이션·스켈레톤·FX 경로가 포함된다. 이 수치는 해당 실행 로그의 관측 범위이며 플러그인 전체 오류 개수는 아니다.
- 이후 `testmap`을 연 뒤에도 PIE 오류 목록에 `BTService_CheckTargetInValidRange`, `BTService_FindNearbyTarget`, `BTTask_ActivateAbilityByClass`, `ABP_Gladiator_AnimLayers_Base`가 남았다. 다른 맵으로 전환하는 것만으로 이미 로드된 구형 BP 오류가 제거되지는 않는다. HUD 연결 제거가 이 원본 오류들의 복구를 의미하지 않는다는 점을 재확인했다.
- 이번에는 원본 에셋·D1 코드·리다이렉트·플러그인 활성화 설정을 변경하지 않았다. 현재 사용할 에셋만 복구하고 미사용 콘텐츠는 보관·실행 분리하는 작업과, 누락된 원본 게임 기능 전체를 복원하는 작업은 범위가 다르므로 사용자와 복구 범위를 정한 뒤 진행한다. 다른 동작의 클래스에 임의 리다이렉트하거나 빈 호환 클래스를 추가해 오류 숫자만 줄이지 않는다.

### 미사용 Gladiator 플러그인의 실행 분리 (2026-10-12)

- 사용자가 현재 사용하는 에셋만 복구하고 미사용 원본은 보관·실행 분리하는 범위를 승인했다. `Project_RPG.uproject`에 `GladiatorCore`의 `Enabled: false`를 명시했다. 플러그인의 원본 descriptor·소스·콘텐츠와 D1 스킬 이식 코드·CoreRedirects는 수정/삭제하지 않았다. 로그 필터링이나 BP 컴파일 경고 무시는 사용하지 않았다.
- 읽기 전용 `Scripts/Editor/AuditGladiatorIsolation.py`로 `/Game` 에셋 6,892개를 점검했다. 직접 Gladiator 참조는 없었고, testmap의 743개 패키지 및 현재 스킬/UI/아이템/Experience 카탈로그의 599개 패키지 의존 경로에도 Gladiator가 없었다. 따라서 현재 사용 에셋의 임의 리다이렉트·재부모화·삭제를 추가하지 않았다. 분리 후 `AuditAfter.log`의 참조 점검은 종료 코드 0, 0 errors/5 warnings로 끝났다. 이전 `AuditBefore.log`는 metadata 점검은 끝났지만 DDC 메모리 fallback 오류로 전체 종료 코드 1이므로 전체 성공으로 보고하지 않는다. 후속 실행은 프로젝트 내부 DDC 경로를 사용했다.
- `Scripts/Editor/VerifyGladiatorIsolationPIE.py`를 별도 새 에디터에서 실행해 실제 `/Game/Maps/testmap` PIE를 요청했다. 최종 `StartupPIE-03.log`의 성공 JSON과 종료 코드 0에서 Gladiator 비활성·미마운트, BP_Player/BP_PlayerController/BP_RPGHUD 생성, 플레이어 ASC·스킬 컴포넌트·SP 50, 직업 선택창 0개, PIE 정상 종료를 확인했다. 해당 로그에 `/GladiatorCore/` 로드·구형 AnimBP·Gladiator 모듈/Feature 등록·BP 컴파일 실패는 없었다. 초기 두 실행은 검증 스크립트의 Python API/보호된 프로퍼티 접근 가정 때문에 실패했고, 엔진 소스로 확인한 공개 API로 수정한 뒤 통과했다. 이를 운영 코드 오류 수정으로 혼동하지 않는다.
- 증거는 `Saved/Tests/Skill/GladiatorIsolation-20261012/`에 있다. Native C++ 변경이 없어 새 빌드를 하지 않았고 정상 빌드된 기존 루트 DLL(`B73DCDD7A317086196F005A85CD1B646313467A5974B4D14C55F2F4550A8FB11`)을 사용했다. 원본 AnimBP 11개·플러그인 descriptor SHA-256은 작업 전후 동일했다. 이 검증은 NullRHI/-nosteam·격리 UserDir의 실제 PIE이며 일반 GUI 렌더링/물리 입력·실제 사용자 설정 복원을 검증한 것은 아니다. 실제 사용자 설정에 Gladiator `OpenAssetsAtExit` 자동 복원 항목은 없었고 MRU/탭 위치·보기 설정은 건드리지 않았다.
- SDK 확인용 UBT의 AppData 쓰기 제한과 기존 외부 연결/콘텐츠 경고는 남아 있다. Gladiator 오류 없음이 프로젝트 전체 무경고를 뜻하지 않는다. 기존 직업/효과 호환 함수에는 구형 콘텐츠 경로가 남아 있으므로 플러그인 전체 기능 복원으로 보고하지 않는다. 원본을 다시 사용하려면 참조 의존성을 복원하고 프로젝트의 명시적 비활성화를 해제해야 한다.

### 스킬창 Desired 레이아웃 (2026-10-12)

- 사용자 요청에 따라 `/Game/Blueprints/UI/Skill/WBP_SkillWindow`의 Designer screen mode를 Full Screen에서 Desired로 변경하고 해당 에셋만 저장했다. 실제 Slate 메뉴에서 Desired 체크를 확인했고 MCP의 `CompileWidgetBlueprint`는 성공했다. 기존 위젯 트리·바인딩과 root SizeBox의 WidthOverride=960, HeightOverride=640을 유지했다.
- Designer 미리보기 설정과 런타임 뷰포트 배치를 구분했다. inherited `BP_OnActivated`에 `SetAnchorsInViewport(0.5,0.5 → 0.5,0.5)` → `SetAlignmentInViewport(0.5,0.5)` → `SetDesiredSizeInViewport(0,0)`을 연결했다. 기존 컴포넌트의 AddToViewport 후 Activate 흐름에서 중앙 정렬·desired-size 배치를 적용하며, 기존 그래프 이벤트는 삭제/교체하지 않았다. C++/GAS/서버 규칙·D1/Gladiator는 이번 작업에서 변경하지 않았다.
- 에셋은 작업 전부터 dirty였으므로 기존 메모리 변경을 보존했으며 디스크로 강제 reload/revert하지 않았다. 수정 전 디스크 사본은 `Saved/Tests/Skill/DesiredLayout-20261012/WBP_SkillWindow.before.uasset`(SHA-256 `CEDA59C84AE58CCA496BCF4A65C23539461424A22FCD9BF0591F7C7CA634ECA2`)이고, 기존 unsaved 변경까지 포함한 백업은 아니다. 저장 후 에셋 SHA-256은 `80C1412DE6F6F67F4E6BCADD292E08F76357FFE4A834A6DE8F38C44629EADB94`다.
- 저장 로그는 회전된 `Saved/Logs/Project_RPG-backup-2026.10.12-04.33.22.log`의 `04:01:05 UTC` SAVEPACKAGE와 `04:14:20 UTC` `[RPG_SKILL_LAYOUT] DESIRED_ASSET_SAVED_AND_EDITOR_CLOSED`에 있다. 직접 열었던 해당 asset editor만 닫았다. 별도 네이티브 빌드/핫리로드는 하지 않았다.
- 실제 입력 검증은 미완료다. 별도 PIE `_C_3`에서 창 0개·커서 false·이동/시점 잠금 false의 baseline은 확인했으나, OS 입력 도우미가 전경 창 소유 확인에 실패해 키를 보내지 않았다. 이후 공유 에디터의 PIE가 외부에서 종료되고 `_C_4`로 교체됐다. `_C_4`의 `ShowWindow()` 직접 호출 성공은 실제 K/마우스나 geometry 통과 증거가 아니며 geometry 관측 전에 해당 PIE도 종료됐다. 다른 세션은 종료하지 않았고 이번 작업의 observer/Python 임시 참조는 정리했다. 실제 Desired 크기/작은 뷰포트·K·마우스·커서 복원은 단독 세션을 조율한 뒤 확인한다. 읽기 전용 관측 도우미는 `Scripts/Editor/InspectSkillWindowInputPIE.py`이며 열린 창 경로는 아직 미검증이다.

### 스킬창 Desired 배치 순서 수정 (2026-10-14)

- 최신 GUI PID 30164/DLL `DD2742F7CDDCE6DE63A5EF307190DBF7A8389DDCC7874D1DD45AB696A60A5096`의 자기 testmap PIE에서 Slate 클릭으로 게임 포커스를 확보한 뒤 K 입력으로 창을 열었다. 커서 표시·이동/시점 잠금은 적용됐지만, 창은 좌상단 검은 영역으로 보였고 캐시 geometry는 0이었다. 원하는 크기는 960×640이나 실제 뷰포트 앵커가 0이었다(`SkillWindow-Initial.png`, `DesiredLayout-Observations.log`). MCP 키 이벤트 주입이며 실제 하드웨어 입력·홀딩 검증은 아니다.
- 엔진 `UGameViewportSubsystem::SetWidgetSlotDesiredSize` 구현이 크기 설정과 함께 앵커를 `FAnchors(0,0)`으로 초기화하는 것을 확인했다. 앵커/정렬 뒤 크기를 설정하던 순서가 원인이며 구조체 상수 자체의 오류로 확정하지 않는다. 자기 PIE에서 앵커만 직접 보정한 진단으로 목록/버튼이 배치되는 것도 관측했다. 이 직접 호출은 저장된 BP나 실제 조작 검증의 통과 증거가 아니다.
- `WBP_SkillWindow`의 기존 이벤트·트리를 보존하고 활성화 체인만 `ForceLayoutPrepass` → `SetDesiredSizeInViewport(GetDesiredSize())` → 중앙 `MakeAnchors` → 중앙 정렬로 바꿨다. 중앙값은 `MakeVector2D(0.5,0.5)`를 재사용하며 크기를 그래프에 하드코딩하지 않는다. Blueprint 컴파일·해당 패키지만 저장·최종 실행 핀 순서 조회가 성공했고 dirty=false였다. 최종 에셋 SHA-256은 `028730EFCD877753C105193992A3DFF6C6349516E0863A30F1ABEFFF67AECB04`다. C++/D1/스킬 정책은 변경하지 않았다.
- 읽기 전용 관측 스크립트의 Python 클래스 이름을 실제 엔진 API인 `SlateLibrary`로 수정했다. `ProjectRPG.Skill.UI.CloseInput`은 passed=1/failed=0, errors/warnings=[]였다(`CloseInput-MCP.json`). 이는 authored 위젯의 네이티브 닫기 핸들러 회귀이며 수정 후 렌더링·Enhanced Input의 실제 조작 검증은 아니다.
- 최종 순서 적용 후 화면 재검증은 미완료다. 다른 MCP의 콘솔 입력으로 서버 포트가 8000→3000→8000으로 바뀌어 실제 입력 검증을 중단하고 단독 사용 조율을 요청했다. 자기 PIE/observer·임시 Python 참조만 정리했다. 결과 폴더는 `Saved/Tests/Skill/QuickSlot-20261014/`이며 `DesiredLayout-FinalGraph.json`은 최종 그래프, `WBP_SkillWindow.before.uasset`는 작업 전 디스크 사본이다. `SkillWindow-Desired.png`/`SkillWindow-BeforeOrderFix.png`는 실패한 중간 배치의 캡처로, 수정 완료 화면이 아니다. 다음은 수정 후 크기/중앙 배치·닫기/잠금 복원, 학습→드래그 등록→키 시전이다.

### 등록형 퀵슬롯의 단독 PIE 점검과 우클릭 누름 수정 (2026-10-14 오후)

- 새 에디터 PID 8540에서 수정된 스킬창/DLL의 해시가 유지됨을 확인하고 자기 testmap Standalone PIE를 시작했다. MCP/Slate 이벤트 주입으로 창을 열어 중앙의 비전체화면 배치를 실제 캡처했다(`QuickSlot-20261014-UI/SkillWindow-Open.png`). 뷰포트 슬롯은 중앙 앵커/정렬 0.5, Desired Size/크기 offset은 960×640이었다. 루트 UserWidget의 cached geometry size는 여전히 0으로 관측돼 실측 근거로 쓰지 않았으며, 실제 이미지와 자식 Slate 배치를 기준으로 렌더링을 확인했다. 닫기 버튼 이후 baseline, K 닫기, ESC 닫기 모두 창 0개·커서 false·이동/시점 잠금 false였다(`SkillWindow-Observations.log`). 작은 뷰포트/리사이즈·하드웨어 입력 검증까지 확장하지 않는다.
- 레벨 증가 버튼으로 AssultBlade/ChargeSlash의 레벨 1→2와 SP 50→49→48을 확인했다. 진행 데이터 변경이 Ability 부여를 의미하는 것은 아니다. 장착 전 ASC에는 기본 7개만 있었고 등록되지 않았으며, 실제 T 입력으로 대검을 장착한 뒤 기존 서버 무기 부여 경로에서 16개(스킬 컨테이너 포함)가 생겼다(`WeaponGrantedAbilities.json`). 별도 수동 GiveAbility·D1 전환은 하지 않았다.
- 실제 Slate Drag 경로에서 목록→Q AssultBlade/E Charge 등록, Q→빈 F 이동, 목록→Q WhirlWind 등록, F/Q 교환을 확인했다. 결과는 `[AssultBlade,Charge,None]` → `[None,Charge,AssultBlade]` → `[WhirlWind,Charge,AssultBlade]` → `[AssultBlade,Charge,WhirlWind]`였다(`QuickSlot-UI-Flow.jsonl`, 네 단계 기대값 비교 성공). 아이템 등록 상태는 변하지 않았다. 도구의 true 반환만으로 통과를 판정하지 않았고 실제 슬롯 태그를 조회했다. 일부 첫 입력이 상태를 바꾸지 않아 대상 PIE 창을 명시적으로 선택한 뒤 성공 경로를 기록했다.
- 창을 닫은 뒤 Q 키로 AssultBlade의 GAS 활성화·공격 애니메이션·마나 130→110·쿨타임 GE 1개/총 1초·HUD 숫자 0.7을 확인했다(`Q-Activation.json`, `Q-Activation.png`). JSON 잔여 시간은 약 0.667초였다. 실제 피해 대상/정상 종료 이벤트·E/F 차지/홀딩 유지·키 재설정·원격 RPC/복제는 이번 점검에서 검증하지 않았다. 키 누름/뗌은 같은 호출의 Slate 이벤트이며 키 유지나 하드웨어 입력을 대신하지 않는다.
- 우클릭 F 해제 시 슬롯이 그대로 남아 실패했다. `QuickSlot-UI-Flow.jsonl`의 `right_click_clear_f`/그 뒤 `restore_whirl_f`는 해제/복구 성공 증거가 아니다. 소스에서 우클릭 누름을 unhandled로 반환하고 해제 시에만 ClearSlot을 호출하는 경로를 확인했다. `RPGQuickSlotWidget.cpp`에서 스킬 슬롯의 우클릭 누름을 handled로 반환해 뷰포트 캡처로 넘어가지 않게 하고, 우클릭 더블클릭도 같은 경로로 처리했다. 기존 아이템 좌클릭/더블클릭 경로·등록/RPC 책임은 보존했다.
- `ProjectRPG.Skill.QuickSlot.RightClickInput` 회귀를 추가했다. 아이템 우클릭 누름의 기존 routing과 스킬 우클릭/더블클릭의 handled 응답을 검사하며 실제 Drop/Release UI 테스트는 아니다. 위젯 cpp/테스트 cpp의 **최종 개별 컴파일은 모두 종료 코드 0**이었다(`CompileWidget.log`, `CompileTests.log`, 각각의 rsp/obj). 첫 CMD 래퍼는 인용 구문 오류로 컴파일에 진입하지 못했으며 후속 완료 실행만 통과 근거로 삼는다. 실행 중 DLL은 여전히 `DD2742F7CDDCE6DE63A5EF307190DBF7A8389DDCC7874D1DD45AB696A60A5096`다. 정상 전체 빌드/새 회귀 실행/수정 후 실제 우클릭 해제는 미완료다.
- AssultBlade Definition의 SkillIcon이 null인 것을 확인했다. DT_Skill에는 AssultBlade/JumpSmash/WhirlWind의 기존 텍스처가 있으나 ChargeSlash 아이콘은 null이었다. 임의 아이콘 선택·운영 데이터/에셋 변경은 이번 점검에 포함하지 않았다. 따라서 드래그/시전 성공을 최종 아이콘 표시 완성으로 보고하지 않는다. 자기 PIE를 종료하고 자기 observer/관측 callback만 정리했으며 사용자 에디터는 열어 두었다. 다음은 에디터 종료·CMD 정상 빌드 후 새 회귀와 실제 우클릭 해제 재검증, 이후 아이콘 연결·E/F 유지/해제·재매핑·네트워크 점검이다. 콤보 노티파이 직전 해제 경계는 계속 사용자 보류 범위다.

### 우클릭 입력 수정의 정상 빌드·PIE 재검증 (2026-10-15)

- 사용자가 정상 빌드 성공을 알린 뒤 새 GUI PID 31984에 연결했다. 루트 DLL SHA-256은 `7634C5EE073A749279E33772C82C46EEE1C21D3601E4CEB7A9E5CC287B53E477`로 바뀌었으며 검증 전후 동일했다. `ProjectRPG.Skill.QuickSlot.RightClickInput`을 발견·실행해 passed=1/failed=0, errors/warnings=[]였다(`Saved/Tests/Skill/QuickSlot-20261015/RightClickInput-MCP.json`). 이전 회귀 3개를 불필요하게 재실행하지 않았다.
- 자기 testmap Standalone PIE를 시작하고 T 입력으로 대검을 장착한 뒤 스킬창에서 Q AssultBlade/E Charge/F WhirlWind를 드래그 등록했다. F 우클릭 후 `[AssultBlade,Charge,None]`, Q 우클릭 더블클릭 후 `[None,Charge,None]`을 조회했다. 다른 5개 스킬 칸과 아이템 등록 배열은 baseline과 같았다. `RightClick-UI-Flow.jsonl`의 네 단계 상태를 기대값과 비교해 성공했고 결과는 `RightClick-UI-Validation.json`에 있다.
- 도구의 true 반환만을 근거로 삼지 않고 실제 등록 상태를 확인했다. UI 입력은 MCP의 Slate MouseDown/Move/MouseUp/DoubleClick 경로이며 클릭 핸들러 직접 호출이 아니다. 다만 하드웨어 마우스 입력, 실제 두 번 클릭의 시간 간격, 원격 RPC/소유자 복제 검증까지 의미하지 않는다. 더블클릭 도구는 DoubleClick 이벤트와 해제를 보낸다.
- 이번 작업은 검증/기록만 수행했다. C++/에셋/D1·기존 아이템 클릭 구현을 추가 수정하거나 실행 중 DLL을 교체하지 않았다. 종료 직전 동일 에디터 PID·자기 PIE 창 ref를 확인하고 자기 세션을 종료했으며 마지막 IsPIERunning=false를 확인했다. 다른 Message Log 창·에디터 프로세스·관측 자료는 건드리지 않았다. 검증 결과 정리 명령의 첫 프로세스 생성이 일시적으로 거부돼 실행되지 않았으나, 짧게 나눈 재시도에서 상태 검증과 PIE 정리는 완료했다.
- 우클릭 해제 문제는 재검증 완료다. 남은 실제 연결 작업은 아이콘 데이터, E/F 유지·해제 및 UI/재매핑에서의 입력 해제, 퀵슬롯 등록의 원격 RPC/복제다. 사용자 보류인 콤보 해제–노티파이 직전 경계는 테스트하지 않았다.

### 스킬창·인벤토리 동시 표시와 공통 입력 수명주기 (2026-10-15)

- 원인은 `ToggleInventory()`의 스킬창 강제 닫기와, 창마다 별도로 GameOnly/커서를 복원하는 코드였다. 강제 닫기를 제거하고 일반 C++ `FRPGWindowInputState`를 컨트롤러와 스킬창 컴포넌트에 공유했다. 공유 객체는 창 목록·커서/포커스·카운트형 조작 잠금만 담당하며 GAS를 직접 참조하지 않는다. 첫 창 알림의 구독자가 기존 유지 입력을 해제한다.
- 첫 창에서 이전 커서/포커스를 보관하고 이동/시점 잠금을 한 번 추가한다. 한 창이 닫히면 남은 창으로 포커스를 넘기고, 마지막 창이 닫힐 때만 자기 잠금을 해제한다. 기존 외부 잠금은 유지한다. 이전 커서가 숨김이면 GameOnly, 표시 상태였다면 이전 포커스와 GameAndUI를 복원한다. 임의의 외부 UIOnly 모드까지 정확히 복원하는 전역 UI 관리자는 아니다.
- 스킬/인벤토리의 미리보기 키 처리에서 현재 Enhanced Input 매핑의 창 토글을 처리한다. 반복 키는 소비하고 새 키다운만 토글해 게임 입력으로 재전달되는 이중 토글을 방지한다. Escape는 포커스된 창을 닫는다. 인벤토리/장비/우편 탭은 계속 하나의 활성 projection이며, 스킬창과는 독립적이다. 탭 교체는 새 창 등록 후 기존 창을 해제해 공유 세션을 유지한다. 인벤토리 비활성화/제거와 컨트롤러 EndPlay도 연결했다.
- 신규 회귀 `ProjectRPG.Skill.UI.CoexistingWindows.InputLifetime`은 두 열기/닫기 순서와 이전 커서/이동/시점 잠금의 32개 조합, 중복 등록/닫기를 검사한다. `ReplacementAndCleanup`은 탭 교체·멱등 종료·첫 창 콜백의 재진입 취소를 검사한다. UHT와 production cpp 5개 및 테스트 cpp의 개별 컴파일이 종료 코드 0이다(`Saved/Tests/Skill/WindowCoexist-20261015/`). 테스트는 아직 실행하지 않았고 정상 모듈 링크/PIE 동시 표시·실제 키 라우팅도 미검증이다. 공유 GUI/DLL 교체·핫리로드·BP/D1 수정은 하지 않았다.

- 후속 재개 검증(같은 날 12:05 KST): 직접 정상 빌드는 AppData 설정/Trace 쓰기 제한으로 컴파일 전에 실패했다. 하지만 공유 루트 DLL은 이미 11:55:56 KST에 갱신돼 있었고, 새 `FRPGWindowInputState`/`HandlePlayerWindowKeyDown` export를 포함했다. 이 DLL(SHA-256 `289B98FE2091A3DC9CE9853A60C5B6295AFE5B13101BE4692B05DAC6FE337FAD`)을 그대로 사용해 격리 UserDir/프로젝트 내부 DDC/NullRHI로 두 신규 회귀를 실행했다. `InputLifetime`, `ReplacementAndCleanup` 모두 Success, errors/warnings=0, 실행 종료 코드 0이며 DLL 해시는 전후 동일하다. 증거는 `WindowCoexist-20261015/Automation-LocalDDC.log`와 `AutomationReport-LocalDDC/index.json`이다. 최초 기본 DDC 실행의 초기화 실패(코드 3), UBT AutoSDK/네트워크 초기화 환경 오류와 테스트 결과를 구분한다. 직접 빌드 성공이나 실제 UMG 두 창 렌더링·현재 매핑 키 입력까지 검증한 것은 아니다. 다음은 현재 DLL로 GUI PIE 확인이며 변경 없는 재빌드는 필요 없다.

### 동시 창의 GUI 검증과 공유 세션 중단 (2026-10-16)

- 새 GUI PID 11900/루트 DLL `44FD97DE79A5EF5366A266D929445DC9A712DB53C6BBC422337BF8F7D971FD2B`에서 자기 testmap Standalone PIE를 시작했다. 읽기 전용 `Scripts/Editor/InspectWindowCoexistPIE.py`로 자기 GI만 관측하며, baseline은 커서 숨김/이동·시점 잠금 해제였다. 실제 Slate 입력으로 K 스킬창 열기, X 버튼 닫기와 원래 상태 복원, I 인벤토리 열기와 UI 잠금을 확인했다. 키 확인은 현재 설정에서 I였으며 강제 매핑 변경은 하지 않았다.
- 입력 도우미 최초 시도는 뷰포트 미포커스, Python 도우미 첫 실행은 존재하지 않는 `controller.get_pawn()` 때문에 실패했다. 후자는 `GameplayStatics.get_player_pawn()`으로 수정했다. 두 창 사이 교차 단축키도 실패했으나, 엔진 SlateInspector `Windows(select)`가 `BringToFront()` 뒤 `SetAllUserFocus(VisibleWindows[Index])`로 **최상위 SWindow에 포커스를 강제로 옮기는 구현**임을 확인했다. 키마다 이 호출을 한 실패 기록은 UMG 키 라우팅의 제품 버그를 입증하지 않는다. 초기 뷰포트 클릭 이후에는 창을 다시 선택하지 않고 연속 입력해야 한다. 이 근거 없이 CommonUI All/Menu 정책이나 C++를 변경하지 않았다.
- 01:18:11 KST에 이 작업이 호출하지 않은 MCP StopPIE가 실행됐고, 01:19:50에는 GA_Player_DrawHUD 에셋 저장이 기록됐다. 공유 에디터 단독 사용이 깨졌으므로 추가 입력/PIE 재시작을 멈추고 사용자에게 조율을 요청했다. 자기 editor observer_2는 해제했고 game observer_3은 PIE 종료로 이미 사라졌다. 다른 에셋 저장을 취소하거나 원래 Entry 맵으로 강제 전환하지 않았다. 이 작업은 C++/BP/D1을 수정하거나 에셋을 저장하지 않았다. 검사 도우미/증거/작업 기록만 추가했다.
- 증거는 `Saved/Tests/Skill/WindowCoexist-20261016/Validation.json`, `UI-Flow.jsonl`, `Baseline-Slate.txt`, `Baseline.png`다. 실제 두 창 동시 표시·양방향 열기/닫기·교차 키 입력은 아직 미완료다. 다음은 단독 사용 확인 → 새 자기 PIE/GI와 관측 도우미 → 최초 클릭으로 뷰포트 포커스 → 창 선택 없이 K/I 연속 입력 검증이다. 기본 쿨타임/D1/운영 아이콘 데이터 등 다른 작업을 확대하지 않는다.

### 동시 창 입력의 PIE 재검증 완료 (2026-10-16)

- 사용자 재개 후 에디터가 PID 24928로 바뀌어 MCP/Slate 참조를 새로 얻고 자기 testmap Standalone PIE를 시작했다. 루트 DLL은 전후 `44FD97DE79A5EF5366A266D929445DC9A712DB53C6BBC422337BF8F7D971FD2B`로 같았다. 첫 뷰포트 클릭 이후에는 키 사이에 Windows(select)를 호출하지 않아 UMG 포커스를 유지했다. Native MCP Slate 입력이며 직접 Toggle/클릭 핸들러 호출은 아니다.
- K로 스킬창 → I로 인벤토리, I로 인벤토리 → K로 스킬창 양방향에서 두 창이 동시에 활성/Visible 상태가 됐다. 스킬 먼저 열기에서는 I로 인벤토리를 닫아 스킬을 남기는 순서와 X로 스킬을 닫아 인벤토리를 남기는 순서를 확인했다. 인벤토리 먼저 열기에서도 K로 스킬을 먼저 닫거나 I로 인벤토리를 먼저 닫는 두 순서가 통과했다. 남은 창이 있으면 커서/이동·시점 잠금이 유지됐고, 마지막 창을 닫으면 원래 커서 숨김/잠금 해제 상태로 돌아왔다. `Validation.json`의 15개 canonical check가 통과 기록이다.
- 중간에 다른 Windows(select) 호출 후 UI 포커스가 사라진 실패와 초기 닫기 버튼 재시도, 선택적 B2 시작 실패가 `UI-Flow.jsonl`에 함께 있다. 이 3건을 성공으로 바꾸거나 전체 실행 무실패라고 보고하지 않는다. 앞 창 재활성화/뷰포트 포커스 후 연속 입력한 canonical 경로로 기능을 검증했다. 스킬 먼저→인벤토리에서 앞선 스킬을 K로 닫는 세부 조합(버튼 닫기는 통과), 두 창의 Escape 연속 닫기, 하드웨어 입력/네트워크 복제는 별도 미검증이다.
- `BothOpen.png`는 현재 중앙 배치 때문에 스킬창이 아래 인벤토리를 가리는 화면이다. 객체의 동시 활성/Visible과 화면상 나란히 배치는 구분한다. 초기 위치 분리나 Z-order 개선은 이번에 구현하지 않았다. 나란히 이동한 스크린샷 시도 전에 01:42:37 KST 외부 StopPIE, 01:42:40 새 PIE가 실행됐다. 관측 도우미는 자기 세션 종료를 감지해 자동 해제됐으며 자기 editor observer_2만 해제했다(game observer_3은 이미 사라짐). 교체된 PIE는 종료/조작하지 않았다.
- 증거: `Saved/Tests/Skill/WindowCoexist-20261016-Verified/Validation.json`, `UI-Flow.jsonl`, `BothOpen.png`. 이번 재개에서는 C++/BP/D1/에셋/DLL을 변경하거나 빌드하지 않았다. 핵심 동시 열기/입력 수명주기 수정은 현재 DLL에서 검증했으므로 변경 없는 재빌드를 다시 요구하지 않는다. 배치 개선을 할 경우 중앙 겹침을 다음의 별도 UI 과제로 다룬다.

### 동시 창의 Desired 초기 배치 분리 (2026-10-16)

- `UI/RPGWindowPlacement.*`는 뷰포트/Desired 크기와 좌우 역할로 배치만 계산·적용한다. `FRPGWindowInputState`의 커서/포커스/조작 잠금 책임은 변경하지 않았다. 컨트롤러와 스킬창 컴포넌트는 이 공통 배치 helper를 호출하며 서로의 구체 창 크기나 인벤토리/스킬 데이터를 조회하지 않는다.
- 스킬창 활성화 이후 왼쪽, 인벤토리·장비·우편 projection 활성화 이후 오른쪽의 비-stretch 앵커/정렬과 기본 24 UI 단위 여백을 적용한다. 기존 BP 활성화 그래프의 중앙 설정보다 뒤에 실행하므로 에셋 수정이 필요 없다. 960×640 등의 운영 크기를 배치 코드에 하드코딩하지 않고 viewport extent 0의 자연 autosizing을 사용한다. UE 5.8의 DesiredSize/Position setter가 앵커를 초기화하므로 크기→위치→앵커/정렬 순서를 지킨다.
- AddToViewport에 맞춰 전체 뷰포트 픽셀 크기를 UI DPI로 한 번만 나눈다. 독립 창의 크기가 화면에 근접하면 여백부터 줄이며, 너무 높으면 제목이 위로 잘리지 않도록 보정한다. 뷰포트 크기가 아직 없으면 좌우 앵커와 기본 여백을 사용한다. 화면이 두 창과 여백의 합보다 좁을 때의 겹침은 자동 축소/창 크기 변경으로 해결하지 않는다. 화면 크기 변경을 매 프레임 추적하는 레이아웃 관리자도 추가하지 않았다.
- Z-order와 기존 render translation을 바꾸지 않아 제목 드래그 상태를 보존한다. 재열기에서는 현재 Desired 크기로 기본 슬롯 위치만 다시 적용한다. 스킬창 자체는 기존처럼 재생성되고 인벤토리 위젯은 기존처럼 재사용된다. 입력/등록/RPC·D1·BP·에셋은 이번 변경 대상이 아니다.
- `ProjectRPG.Skill.UI.WindowPlacement.Geometry`와 `DesiredSlot`을 추가했다. 전자는 논리 좌표의 간격/좌우 정렬·정확한 크기/거대 창·미확정 뷰포트를, 후자는 authored 스킬창의 활성화 이후 앵커/Desired autosizing·960×640 크기·Z-order/translation 보존을 검사한다. `RPGWindowPlacement.cpp`, `RPGWindowPlacementTests.cpp`, `RPGSkillWindowComponent.cpp`, `RPGPlayerController.cpp` 개별 컴파일이 종료 코드 0이고 테스트 최종 보강 후 재컴파일도 0이다. 증거는 `Saved/Tests/Skill/WindowPlacement-20261016/Compile.log`, `CompileTests-Final.log`다. 앞선 두 CMD wrapper 호출은 인용부호 문제로 컴파일 실행 전에 실패했으며 통과 근거에 포함하지 않는다.
- 신규 테스트는 아직 실행하지 않았으며 전체 DLL 빌드/핫리로드/렌더링 확인도 미완료다. 공유 UnrealEditor PID 8340 및 UnrealEditor-Cmd PID 1732를 읽기 전용으로 확인했을 뿐 조작/종료하지 않았다. 다음은 정상 빌드 후 신규 회귀 2개 실행과 자기 PIE에서 양쪽 열기 순서의 좌우 배치·드래그/재열기·작은 화면을 확인하는 것이다. 이전 동시 창 입력 검증을 새 배치 검증의 증거로 쓰지 않는다.

- 후속 사용자 빌드 요청으로 정상 `Build.bat`를 직접 실행했지만 UBT의 AppData/Roaming 기본 설정 디렉터리 생성이 `UnauthorizedAccessException`으로 거부됐다. 종료 정리에서도 AppData/Local `Trace.uba` 쓰기가 20초 재시도 후 거부됐고 최종 종료 코드는 6이다. `Saved/Logs/SkillEditorBuild-20261016-Console.log`에 기록했다. 컴파일/링크에 도달하지 않았으며 루트 DLL은 01:52:52 KST/10,318,336바이트 그대로다. GUI PID 8340/33708과 Cmd PID 1732가 프로젝트 DLL을 로드 중인 것도 확인했지만 종료하지 않았다. 새 소스 오류로 진단하거나 빌드 성공으로 보고하지 않는다. 이후에는 관련 프로세스를 종료하고 사용자 일반 CMD에서 정상 빌드해야 하며, AppData/UBA 제한 우회 실험을 다시 늘리지 않는다.

- 사용자 승인 후 DLL을 사용 중이던 PID 8340/1732의 이름·시작 시각·해당 모듈을 다시 확인해 종료했고 DLL 3개의 비변경 쓰기 열기 성공을 확인했다. 이후 사용자가 정상 빌드 성공/에디터 실행을 알렸다. 새 GUI PID 16092와 04:24:48 KST/10,345,472바이트의 루트 DLL SHA-256 `2A3DEF3F2675EC4700D0BE2B4C374AEED92624F22CC90DD57835CD9D98257E61`을 확인했다. 이 대화의 제한된 빌드 실행이 성공했다는 뜻은 아니다.
- 별도 자기 NullRHI/격리 UserDir·Local DDC에서 신규 2개만 실행했다. `Geometry` Success, `DesiredSlot` Fail이며 후자는 960×640 크기 검사 2건만 실패했다(`Saved/Tests/Skill/WindowPlacement-20261016/Runtime/Automation.log`, `Runtime/Report/index.json`: succeeded=1/failed=1). 종료 코드 0이라도 전체 통과가 아니다. 초기 AutoSDK UBT의 AppData/Trace.uba 및 EOS 연결 오류는 선택 테스트 결과와 구분한다. 자기 Cmd PID 6284는 정상 종료했고 사용자 GUI를 종료/조작하지 않았다.
- 실패 fixture는 `Window->TakeWidget()` 반환값을 즉시 버리고 있었다. UE의 `SObjectWidget` 소멸자는 NativeDestruct/ReleaseSlateResources를 실행하며 `ForceLayoutPrepass`/`GetDesiredSize`는 유효한 cached Slate widget을 필요로 한다. 실제 AddToViewport처럼 테스트 끝까지 `TSharedRef<SWidget>`를 유지하고 활성화 후 SlatePrepass를 수행하도록 테스트 cpp만 수정했다. `Runtime/CompileFixtureFix.log`에서 개별 컴파일 종료 코드 0이다. 운영 배치 구현·에셋·D1은 변경하지 않았으며 실행 중 DLL도 교체하지 않았다. 수정된 fixture의 전체 빌드/재실행은 아직 남아 있다.
- GUI의 MCP 8000 포트는 닫혀 있어 실제 배치 검증을 시작하지 못했다. 사용자에게 에디터 콘솔 `ModelContextProtocol.StartServer`와 검증 중 단독 사용을 요청했다. 다음은 MCP 연결 후 양방향 열기/Desired 좌우 배치·드래그/재열기·작은 화면 확인이고, 후속 정상 빌드에는 fixture 수정을 포함해 `DesiredSlot`을 다시 실행한다. 이미 성공한 Geometry를 변경 없이 반복하지 않는다.

- 이후 사용자가 실제 MCP 포트가 800번이며 아이템/인벤토리 작업이 같은 GUI를 사용 중이라고 알렸다. 800번 initialize 연결만 확인했고 에디터 tool/input/PIE 조작은 보내지 않았다. 설치 엔진 `FModelContextProtocolServer::StartServer`는 같은 인스턴스에서 다른 포트 시작 시 기존 서버를 중단하므로 포트 변경/재시작도 하지 않았다. 단독 UI 검증 대신 사용자 병행 요청 범위에서 전용 비렌더링 PIE를 사용했다.
- `Scripts/Editor/VerifySkillWindowPlacementIsolated.py`를 추가했다. 전용 `-RPGSkillWindowPlacementProbe` 없이는 실행을 거부하고 자기 testmap PIE만 시작/종료한다. 별도 UserDir/DDC·Hidden·NullRHI로 실행했으며 MCP 서버는 시작하지 않았다(만약 자동 시작이 설정돼 있어도 충돌하지 않도록 포트 override만 8001). 네이티브 ShowWindow/ShowInventoryProjection 경로의 두 열기 순서, 장비/우편 전환, 인벤토리 재열기 등 5개 샘플에서 좌우 앵커/정렬, 자연 autosizing, Z-order, Desired 크기와 재사용을 확인했다. 스킬 960×640, 인벤토리 720×640, 장비 560×620, 우편 720×540이며 인벤토리의 직접 할당한 translation (15,25)이 재열기 후 보존됐다. 실제 드래그 입력을 보냈다는 뜻은 아니다.
- `IsolatedPIE/Probe.log`의 `[RPG_SKILL_WINDOW_PLACEMENT]` 결과는 success=true/5샘플이다. 뷰포트 픽셀 크기가 0×0이라 화면상 비겹침/픽셀 위치/작은 창 렌더링은 검증하지 않았다. 증거는 `Saved/Tests/Skill/WindowPlacement-20261016/IsolatedPIE/Validation.json`에 정리했다. 자기 PID 6284(04:36:10 KST 시작)는 04:37:02 Exiting 기록 후 사라졌고 GUI PID 16092는 유지됐다. DLL 해시 `2A3DEF3F2675EC4700D0BE2B4C374AEED92624F22CC90DD57835CD9D98257E61`은 그대로다. 운영 코드/에셋/공유 800번 세션은 변경하지 않았다. 프로세스 종료 코드 자체는 수집하지 않았으며 성공 marker/검사 샘플과 실제 종료를 근거로 한다. C++ DesiredSlot fixture의 수정 후 재빌드/재실행은 아직 별도 남아 있다.

### 신규 스킬의 기존 BP HUD 쿨타임 알림 연결 (2026-10-13)

- MCP 읽기 전용 조회에서 `TPWBP_AbilityIconSlot`의 `EventOnOwningPlayerUIComponentInitialized` → `AssignOnAbilityCooldownBegin`, 입력 태그 비교 → `CountDown(RemainingCooldownTime)` → 텍스트/`CoolDownRemainingPercent` 갱신을 확인했다. 화면 구성/렌더링은 기존 BP이며 C++ HUD 재제작이나 에셋 수정은 하지 않았다.
- 신규 `URPGGameplayAbility_SkillContainer`의 정책/Action 시작 경로를 `CommitSkillAndNotifyCooldown()`으로 연결했다. GAS 커밋 성공 후 로컬 소유자에 한해 `IPawnUIInterface`의 기존 UI delegate로 실제 GE 조회의 전체/잔여 시간과 슬롯 태그를 전달한다. grant의 단일 입력 매핑을 우선하고, 매핑이 없으면 authored InputTag를 사용하며, 복수 매핑에 기본 태그가 포함되지 않으면 임의 슬롯을 고르지 않는다. UI 없는 Pawn은 그대로 실행한다. D1/일반 능력의 `CoolDown()`·기존 GE는 변경하지 않고 중복 커밋/GE/비용/RPC도 추가하지 않았다.
- 알림 뒤 UI 콜백이 스킬을 취소한 경우 활성 상태를 다시 확인해 이미 정리된 정책/Action을 시작하지 않도록 했다. 컨테이너는 커밋/알림 전달만 담당하며 쿨타임 화면 타이머·게이지/텍스트 로직은 BP에 남긴다.
- 최신 정상 응답 파일/PCH를 확인해 별도 `Saved/Tests/Skill/CooldownUI-20261012/Container.obj`로 컴파일했으며 종료 코드 0을 확인했다. 응답 파일은 `CompileContainer.rsp`, 로그는 `Compile.log`다. 사용자 인터럽트가 걸린 첫 컴파일은 통과 증거로 쓰지 않고 후속 완료 실행을 기준으로 한다. 루트 DLL 링크/교체·핫리로드는 하지 않았다. 검증 전후 루트 DLL SHA-256은 `8AFC2F3FD3EF393216EA44EDCBB1714DF70F2F5F91E5C7A9B4F987D1CB41D457`로 동일했다(이전 10/12 DLL 이후 공유 작업의 빌드가 반영된 상태).
- `Scripts/Editor/VerifySkillCooldownUIPIE.py`는 전용 프로세스 플래그를 요구하며 별도 PIE의 실제 UI delegate·Definition GE 1개·시간 일치·차단된 재입력의 알림/GE 중복 없음을 검사하도록 작성했다. Python 문법만 통과했고 실행 검증은 미완료다. 별도 probe PID 29772는 `Baseline.log`의 TurnkeySupport 모듈 초기화 기록 이후 진행되지 않아 Python/PIE에 도달하지 못했다. 멈춤 원인은 미확정이며 해당 신규 스킬의 실패로 해석하지 않는다. 180초 이상 로그 정체와 프로세스 시작 시각/이름을 확인한 뒤 직접 실행했던 이 프로세스만 정리했고 공유 GUI 에디터 PID 24524는 종료/입력 조작하지 않았다. 실제 알림 누락 재현·수정 후 HUD 렌더링·예측 거부/서버 보정 시 표시 복원은 아직 확인하지 못했다.

### 등록형 스킬 퀵슬롯의 책임 분리와 HUD 연결 (2026-10-13)

- `QuickSlotComponent`는 등록 상태·서버 검증·소유자 복제를 담당한다. 신규 스킬은 `SkillDefinition.SkillTag`로 식별하며, 현재 쿨타임/차단 여부로 등록 가능성을 판단하지 않는다. 같은 스킬을 다른 칸에 넣으면 이동/교환하고, 중복 grant나 미소유 스킬은 거부한다. 기존 RPC와 슬롯 구조를 유지했다.
- `IRPGSkillQuickSlotInput`/`IRPGSkillQuickSlotLookup`와 GAS 어댑터가 Spec 조회·입력·실제 GE 잔여 시간 조회를 분리한다. 유지 입력 라우터는 Press 당시 Handle을 기억하므로 시전 중 등록을 바꿔도 Release가 다른 스킬로 전달되지 않는다. 반복 Press, 공유 Handle의 마지막 Release, UI 열기·Pawn 교체·EndPlay의 일괄 해제를 처리한다.
- 스킬 목록과 퀵슬롯은 기존 `RPGDragDropOperation` 스냅샷을 사용한다. 드롭 helper는 동일 소유자·변경되지 않은 원본 칸·유효한 grant를 검사하고 등록을 요청한다. ViewModel은 등록된 스킬의 아이콘·실제 키·GAS 쿨타임을 표시한다. 셀은 0.1초마다 상태를 갱신한다. 원형 게이지와 실제 렌더링 검증은 미완료다.
- 입력은 `DA_InputConfig` 행을 순회한다. 동일 스킬 칸에 서로 다른 액션, 한 액션/키에 여러 스킬 칸이 겹치면 해당 스킬 행을 모두 거부한다. 런타임 재매핑 후에도 Press에서 충돌을 확인하며, 잘못된 퀵슬롯 설정이 구형 고정 스킬 입력을 다시 켜지 않도록 한다. 기존 아이템 F1/숫자 별칭은 유지한다.
- 기존 Q/E/F가 `IMC_GreatSword`에만 있는 것을 확인해 `IA_QuickSkill1/2/3`을 별도로 복제하고 `IMC_Default`에 같은 키를 추가했다. 컨트롤러는 설정/기본 컨텍스트를 소유자별 transient 복제본으로 사용한다. 여기서 스킬 매핑만 파생한 transient 컨텍스트를 우선순위 2로 적용한다. 기본 입력 0·무기 1·타깃 잠금 3을 유지하므로 T 장착/해제 등 다른 무기 입력의 우선순위는 바꾸지 않는다. 재매핑 시 파생 컨텍스트도 갱신하고 유지 입력을 해제하며 EndPlay에서 자기 컨텍스트만 제거한다. 기존 설정 태그는 유지하되 동일 액션의 신규 슬롯 설정을 우선하고, 키 라벨은 실제 로컬 매핑에서 읽는다.
- `WBP_QuickSlot` 원본은 변경하지 않고 `WBP_SkillQuickSlot`을 복제했다. HUD의 기존 `HorizontalBox_99`와 자식/그래프는 삭제하지 않고 Collapsed로 보존했다. 같은 Canvas 배치에 인덱스 0~2의 새 셀을 추가했으며 아이템 4칸·Identity 아이콘은 유지했다. `bCanDrag`는 창 자체 이동 옵션이므로 새 셀도 false로 두고 내용물 드래그는 네이티브 핸들러가 처리한다. 키 설정 메뉴의 구형 스킬 3개 항목만 숨기고 대응하는 신규 슬롯 항목을 노출했다. 기본 스킬 등록/8칸 확대/D1·무기 컨텍스트 수정은 하지 않았다.
- HUD는 작업 전 이미 dirty였으며 기존 메모리 변경을 보존해 저장했다. 수정 전 디스크 사본은 `Saved/Tests/Skill/QuickSlot-20261013/Assets/`에 있고, 당시 unsaved 상태까지 포함한 백업은 아니다. 기존 아이템 셀 에셋 SHA-256은 전후 `49DD5DA0B3CB0138256E99FD1693D59CCEF1DD1ACFBC012EE2573E902DE60585`로 동일했다.
- UHT 및 변경 production C++ 9개와 신규 테스트 파일의 개별 컴파일을 통과했다. 후속 컨트롤러 헤더 변경도 UHT로 갱신했다(`UHT-Resume.log`, 생성 파일 1개). `ProjectRPG.Skill.QuickSlot.HeldInputIdentity`, `DataDrivenBindings`, `RegistrationAndDrop`은 작성·컴파일했으나 아직 실행하지 않았다. 출력은 `Saved/Tests/Skill/QuickSlot-20261013/`에 있으며 전체 모듈 링크/핫리로드 성공으로 간주하지 않는다.
- MCP의 `CompileWidgetBlueprint`에서 새 셀/HUD 모두 true였고, 저장 후 native property 조회로 3칸 타입/인덱스·창 드래그 false·Q/E/F의 신규 액션 연결·4개 패키지 저장 상태를 확인했다(`AssetConfiguration.log`). 배치 감사의 첫 시도는 is_dirty에 UObject 경로를 전달한 검증 코드 오류로 실패했고, 후속 전체 스키마 반복 조회는 응답이 끝나지 않아 자기 HTTP 클라이언트만 중단했다. 이후 직접 native 조회가 완료됐다. 콜드 로드·실제 드래그·키 입력·RPC/복제 성공을 뜻하지 않는다.
- GUI PID 176은 11:26 빌드 DLL(`4929070DCC97A5747065E3F7EA423F87C5A9CAC664570A076B220F4ADA1BE9E3`) 이후 켜졌지만 마지막 입력 컨텍스트 코드는 그 뒤 수정됐다. 정상 빌드/재시작 후 3개 회귀 실행과 실제 드래그 등록→키 누름/유지/해제→이동/교환/우클릭 해제·쿨타임·키 재설정·클라이언트/서버 등록 검증이 필요하다. 공유 에디터/PIE를 강제 종료하거나 실행 중 DLL을 교체하지 않았다.

### 기본 스킬 아이콘의 조회 경로 일치 (2026-10-16)

- 시전의 BuildRuntimeSpec은 SkillDataHandle 행의 아이콘을 해석하지만 스킬창 ViewModel과 퀵슬롯 GAS 어댑터는 Definition.SkillIcon만 읽고 있었다. 따라서 행에 아이콘이 있어도 UI가 비는 경로를 확인했다. Definition에 GetBaseIcon과 공통 기본 아이콘 해석을 두고 UI 두 곳을 연결했다. 기존 데이터테이블 우선·Definition fallback 규칙과 상태별 시전 override는 유지했다. D1/아이템 코드·에셋·실행 중 GUI/DLL은 변경하지 않았다.
- 기존 ProjectRPG.Skill.QuickSlot.RegistrationAndDrop에 기본 필드만 있음, 테이블 우선, 직접 필드 누락, 테이블 아이콘 누락, 양쪽 누락의 5가지 기본 아이콘 경우를 추가했다. 스킬창 ViewModel·퀵슬롯 조회·RuntimeSpec이 같은 결과인지 비교한다. 새 테스트 도구/실행 환경은 만들지 않았다.
- production cpp 3개와 기존 테스트 cpp의 개별 컴파일이 종료 코드 0이다(`Saved/Tests/Skill/IconPresentation-20261016/Compile.log`). 수정 후 전체 DLL 빌드/테스트 실행/실제 아이콘 렌더링은 아직 확인하지 않았다. 다음 예정된 정상 빌드에 묶어 RegistrationAndDrop 및 수정된 DesiredSlot을 실행하고, 공유 800번 아이템 작업이 끝난 뒤 기존 PIE로 아이콘·배치/드래그를 확인한다. 검증만을 위한 별도 재시작/새 probe는 추가하지 않는다.

### 8000번 GUI에서 초기 배치와 제목 드래그 확인 (2026-10-16)

- 새 GUI PID 7944의 MCP 8000에 연결했다. 루트 DLL은 04:24 빌드 SHA-256 `2A3DEF3F2675EC4700D0BE2B4C374AEED92624F22CC90DD57835CD9D98257E61` 그대로이고 로드된 LiveCoding 패치도 없어 새 아이콘 수정/테스트 수명주기 수정은 미반영 상태다. 해당 회귀를 구형 DLL에서 반복하지 않았다. 깨끗한 Entry/PIE 없음 확인 후 testmap의 자기 Standalone Floating PIE만 시작했다.
- 기존 관측 도우미와 Native Slate 입력을 사용했다. K→I로 열린 두 창은 실제 1280×720 화면에서 좌우에 겹치지 않았다(`Saved/Tests/Skill/WindowPlacement-20261016/GUI/BothOpen.png`). 첫 Drag 호출은 true를 반환했지만 위치가 안 바뀌었다. 제목 Click 후 Drag에서는 INVENTORY 제목의 화면 좌표가 (1122,352)에서 (1034,332)로 변했다. 이는 실제 Native Slate 라우팅 결과이며 하드웨어 마우스 검증은 아니다.
- 드래그 뒤 I/I 입력 후 좌표는 같았지만 관측 로그에서 창 비활성화/재활성화는 확인되지 않았다. 따라서 재열기 위치 보존 성공으로 처리하지 않는다. 앞선 observed_03/04의 실제 닫기/재열기는 드래그 전에 일어난 것이다. 마지막 스크린샷은 0바이트여서 제외·제거했고, AfterReopen.png 역시 첫 드래그가 이동하지 않았던 경로의 화면이다. 증거/한계는 GUI/Validation.json에 정리했다. 자기 도우미와 observer_1, PIE를 정리했고 testmap dirty=false를 확인한 뒤 Entry로 복원했다. C++/BP/에셋/DLL은 변경하지 않았다. 다음 정상 빌드 후 아이콘 및 수정된 두 회귀, 드래그 후 실제 재열기만 묶어서 확인한다.

### 정상 빌드의 아이콘 회귀와 실제 드래그 후 재열기 확인 (2026-10-16)

- 사용자 정상 빌드 후 GUI PID 26880에서 18:47:26 KST 루트 DLL(SHA-256 `B11ADAC5F1E88A4381ED164A5B6BEBF2CA52620D04343670E80A4C12ABAED127`) 로드를 확인했다. MCP 8000의 Automation에서 변경된 `RegistrationAndDrop`·`WindowPlacement.DesiredSlot`만 실행해 passed=2/failed=0, errors=[]였다. 전자의 미소유/모호한 등록 거부 경고 1건은 기존 음성 테스트 출력이다. 이전의 전체 빌드·fixture 미반영 상태는 해소됐으며 이 대화가 정상 빌드를 실행해 성공시켰다는 뜻은 아니다.
- 자기 testmap Standalone PIE에서 기존 관측 도우미·Native Slate 입력을 사용했다. AssultBlade/JumpSmash/WhirlWind의 목록 아이콘이 실제 표시됐다. ChargeSlash는 기존 DT 행과 Definition 아이콘이 모두 None임을 읽기로 확인했으며 임의 아이콘이나 D1/데이터 에셋을 변경하지 않았다. 퀵슬롯 Drag가 true를 반환했지만 실제 등록/아이콘 렌더링은 확인하지 못했으므로 성공이나 제품 결함으로 판정하지 않는다.
- INVENTORY 제목이 드래그 전 (1122,352)에서 (929,386)으로 이동했고, I 입력의 실제 비활성화/숨김(`observed_04`)과 재활성화/표시(`observed_05`) 후에도 (929,386)이었다. 드래그 후 재열기 위치 보존은 이번에 확인했다. 증거/스크린샷은 `Saved/Tests/Skill/PostBuild-20261016/Validation.json`, `Icons-And-Windows.png`, `After-Drag-Reopen.png`다. 하드웨어 입력·모든 창/모든 해상도 검증을 뜻하지 않는다.
- 같은 콘솔에서 다른 아이템 작업의 read-only Check.py가 실행된 것을 10:02:33 UTC 로그로 감지했다. 추가 UI 입력을 중단하고 자기 observer_1/PIE만 정리했으며 기존 관측 도우미의 자동 정리 로그와 testmap dirty=false를 확인한 뒤 Entry로 복원했다. 사용자 에디터/MCP는 유지했다. C++/BP/에셋/DLL 변경·새 probe/별도 실행 환경 추가·추가 빌드 없이 이번 검증을 마쳤다. 다음 GUI 검증은 단독 입력 시간을 조율한 뒤 퀵슬롯 아이콘부터 진행하고 성공한 두 회귀·창 배치/드래그는 반복하지 않는다.

### 퀵슬롯의 실제 아이콘 렌더링 확인 (2026-10-16)

- 사용자가 기존 아이템용 인증 PIE 종료를 명시적으로 승인했다. GUI PID 26880/MCP 8000의 해당 PIE만 종료하고 같은 testmap에서 비인증 Standalone Floating PIE(`BP_RPGGameInstance_C_3`)를 시작했다. 에디터·별도 서버 프로세스·계정/백엔드 데이터는 변경하지 않았다.
- Native Slate의 T 입력으로 기존 무기 장착 경로를 실행해 ASC의 public GetAllAbilities가 16개를 반환함을 확인했다. 별도 GiveAbility/Definition 교체 없이 목록 아이콘을 Q/E/F로 드래그했다. 초기 true 반환만으로 등록을 판정하지 않았고, 게임 창/원본 아이콘에 포커스한 후 Q=AssultBlade/E=JumpSmash/F=WhirlWind 및 나머지 5칸 None을 읽기로 확인했다. `Registered-QEF.png`에서 세 퀵슬롯 아이콘이 실제 표시됐다. `Registered-Q.png`는 등록 확인 전 캡처이므로 성공 증거에서 제외한다.
- 증거는 `Saved/Tests/Skill/QuickSlotIcons-20261016/Validation.json`이다. 하드웨어 입력·원격 RPC/소유자 복제·유지 입력/재매핑 검증은 아니며 ChargeSlash의 원래 누락된 아이콘 데이터는 유지했다. Python의 라이브러리 별칭/보호된 spec 배열 접근 실패는 제품 결함이 아니며 public API와 기존 인스턴스 읽기로 확인했다. 새 도구/실행 환경·C++/에셋/D1 변경이나 재빌드/성공 회귀 반복은 없다. 자기 observer_3/PIE를 정리했고 원래 testmap은 dirty=false, 에디터/MCP/서버 프로세스와 DLL 해시 `B11ADAC5...`는 유지했다.

### 유지 입력 검증의 중단 체크포인트 (2026-10-16)

- 자기 Standalone PIE(`BP_RPGGameInstance_C_4`, 비인증)에서 E=Charge/F=WhirlWind를 등록하고 기존 API를 사용했다. MCP PressKey는 down/up을 한 번에 보내므로 유지 입력을 증명하지 못한다. Python에서 만든 FInputActionValue는 Break 결과가 0이라 해당 시도를 제외했다. 이후 엔진에 원래 있는 `Input.+key`/`Input.-key`와 메모리 안의 임시 틱 관측만 사용했으며 영구 검증 파일/프레임워크는 추가하지 않았다.
- 원본 ChargeSlash의 AbilityTags가 `Player.Ability.IdentitySkill`이고 기본 CanActivateAbility가 `Player.Status.Rage.Active`를 요구한다. 일반 상태에서 몽타주가 없는 것을 퀵슬롯 유지 입력 실패로 판단하거나 이를 통과시키기 위해 원본/D1/상태를 변경하지 않았다. WhirlWind는 Default→Toggle 재생과 마나 차감까지 관측했지만 입력 해제 후 종료·UI 열기/재매핑 시 해제를 확정하지 않았다. 첫 F 시도는 Zen 복구 정지로 예약 해제가 약 6초 늦어 시간 판정에서 제외했다. 보호된 GAS 필드 접근도 지원되지 않았으며 이를 제품 결함으로 보고하지 않는다.
- 사용자의 중단/재개 사이에 PIE가 종료됐다. 재개 시 MCP의 Windows=[]와 GUI PID 26880의 MainWindowHandle=0/PIE=false를 확인해 추가 입력을 중단하고 자기 observer_4만 해제했다. 마지막 임시 관측은 자동 해제 구간을 지났고 PIE도 종료돼 테스트 입력이 남은 플레이 세션은 없다. 별도 Cmd PID 9152와 GUI 프로세스는 임의 종료하지 않았다. 다른 작업의 `WBP_InputMappingRow` 저장 실패 로그도 별도 이슈로 유지했다. 소스/에셋·입력 설정 저장·재매핑·DLL 변경·추가 빌드는 없으며, 유지/해제와 재매핑은 여전히 미검증이다.

### WhirlWind 유지·해제와 UI/런타임 재매핑 해제 확인 (2026-10-17)

- GUI PID 25656/MCP 8000의 원래 Entry가 깨끗하고 PIE가 없음을 확인한 뒤 자기 비인증 testmap Standalone PIE를 사용했다. 02:08:52 KST 루트 DLL `95D55E1FAAF59FC0CC9286A9382D5D46E490298B03D607259E743BFA0066DBFE`는 03:04:59 에디터 시작 전에 빌드됐고 base 모듈만 로드돼 있었다. 정상 빌드는 이 대화가 실행한 것이 아니다. OS MainWindowHandle=0이어도 MCP Windows에는 에디터가 있었으므로 전자를 GUI 부재의 단독 근거로 사용하지 않았다.
- 기존 T 장착/드래그 경로로 WhirlWind를 F에 등록했다. Python FInputActionValue나 보호된 GAS 필드 대신 엔진의 기존 Input.+key/-key, 게임 시간 기준 임시 메모리 틱 콜백, InputKeyDown/몽타주/섹션 및 public GetActiveSkillTargetResult를 읽었다. 활성 타깃 스냅샷은 EndAbility에서 Reset되므로 시전 관측과 종료 확인을 구분할 수 있다. F 유지 시 Default/타깃 유효, 키 해제 뒤 몽타주 없음/타깃 무효를 확인했다.
- F를 계속 주입한 상태에서 기존 SkillWindowComponent.ShowWindow를 호출하자 End 섹션으로 이동한 뒤 스냅샷이 초기화됐다. UI가 열린 상태에서 키가 다시 down으로 관측돼도 스킬은 재활성화되지 않았다. 실제 K/옵션 위젯 제스처나 하드웨어 입력을 검증한 것으로 확대하지 않는다.
- CustomKeyMappings 원본 0행을 메모리에 보관하고 임시 F→F10 행을 통해 public UpdateInputMappings의 save=false 경로를 실행했다. F가 down인 채 바인딩이 F10으로 바뀌면서 End→종료됐고, 기존 F 해제 뒤 F10으로 별도 시전/해제가 성공했다. F로 원복하고 원래 배열을 복원했으며 SaveSettings는 호출하지 않았다. GameUserSettings.ini SHA-256은 전후 `1259F3127AE236A70AB5AB1D849214BF332902CAB601C0430208C887CA03FC76`로 동일했다.
- 증거는 `Saved/Tests/Skill/HeldInput-20261017/Validation.json`의 8개 상태 확인과 원시 타임라인이다. 새 자동화 테스트 8개를 실행했다는 의미는 아니다. 다른 시전 타입·ChargeSlash 폭주 조건·옵션 UI 저장·실제 물리 키·원격 RPC/소유자 복제는 별도 범위다. 임시 입력 F/F10·콜백·콘솔 전역변수·자기 observer_1/PIE를 정리하고 testmap dirty=false 확인 후 Entry로 복원했다. C++/BP/에셋/D1·DLL 변경이나 새 영구 검증 도구·추가 빌드·성공 회귀 반복은 없다. 다음은 퀵슬롯 등록의 소유자 RPC/복제 확인이다.

### 퀵슬롯의 캐릭터 상태별 아이콘 연결 (2026-10-19)

- `FRPGGASSkillQuickSlotAccess::Read`가 `GetBaseIcon`만 사용해 Definition의 상태별 `NewIcon`을 무시하던 연결 누락을 수정했다. ASC Avatar의 현재 스킬 저장 데이터를 조회하고 기존 `BuildRuntimeSpec`의 최종 아이콘을 사용한다. 위젯에 상태 태그 분기를 추가하거나 오버라이드 규칙을 중복 구현하지 않는다.
- 기존 퀵슬롯 0.1초 갱신 경로를 그대로 사용해 상태 진입/해제 후 현재 구성의 아이콘을 다시 읽는다. 실행 중인 Ability의 활성 스냅샷은 건드리지 않는다. 스킬 목록의 기본 아이콘 표시 및 BP/에셋/D1/아이템 구현은 변경하지 않았다.
- 사용자 방침에 따라 빌드·PIE·네트워크 검증은 후순위로 남겼다. 소스 연결을 구현한 것이며 실제 표시 검증 완료를 뜻하지 않는다.

### 스킬창 목록·소유자 재연결 수명 정리 (2026-10-19)

- 목록을 비워도 선택 해제가 상세 패널에 전달되지 않고, null 컴포넌트 초기화가 기존 ViewModel 연결을 유지하던 누락을 수정했다. 목록 선택 해제를 전달하고, 기존 슬롯의 소유자·컴포넌트 구독·목록·SP를 해제/초기화한다.
- 창 종료 때 상세 패널과 ViewModel을 정리한다. 카탈로그 비동기 로딩 완료 시 요청 당시 컴포넌트의 소유자와 현재 Pawn이 다르면 현재 Pawn 기준으로 다시 초기화하며, 현재 목록에 없는 선택은 상세 패널에 연결하지 않는다.
- 기존 책임 경계를 유지해 ViewModel/ListModule/Window cpp만 수정했다. BP/에셋/D1/아이템 구현 및 검증 도구는 변경하지 않았다. 빌드·PIE·네트워크 검증은 후순위로 남겼다.

### 스킬창 SP 표시의 WBP 배치 (2026-10-19)

- 사용자 요청에 따라 MCP 8000의 네이티브 UMG/Object 도구로 `WBP_SkillWindow`를 편집했다. 기존에는 런타임 C++ fallback만 생성하던 `SkillPointsText`를 `CommonTextBlock`으로 제목줄에 직접 배치했다. 제목 드래그 영역과 닫기 버튼 사이는 Auto 크기, 18pt, 수직 중앙 정렬과 좌우 12 패딩을 사용하며 텍스트는 마우스 입력을 가로채지 않는다.
- 기존 `GetWidgetFromName("SkillPointsText")` 및 ViewModel RemainingSP/TotalSP 구독을 그대로 사용한다. WBP는 표현과 배치를 담당하고 SP 계산·구독 로직을 그래프에 중복하지 않는다. 원래 Desired 크기 및 다른 위젯/그래프는 유지했고 C++은 설명 주석만 갱신했다.
- 편집 전 대상은 dirty=false였으며 디스크 백업은 `Saved/Backups/SkillUI-20261019-025116/WBP_SkillWindow.before.uasset`이다. WBP 컴파일 true, 해당 에셋만 저장 true, 저장 후 dirty=false를 확인했다. PIE/전체 C++ 빌드/플레이 검증은 실행하지 않았다. 스킬 퀵슬롯 WBP는 읽기만 했고 다른 작업의 에디터 조작·에셋 저장은 하지 않았다.

### 스킬 퀵슬롯 원형 쿨타임 표시 연결 (2026-10-24)

- 이미 읽고 있던 GAS 쿨타임 잔여 비율을 원형 게이지에 연결했다. ViewModel은 유한한 0~1 비율을 제공하고, 네이티브 위젯은 선택적인 `SkillCooldownImage`의 위젯별 MID에 `ProgressBarFillPercent`를 전달한다. 기존 0.1초 갱신을 사용하며 UI 카운트다운/타이머나 서버 쿨타임을 추가하지 않는다. 만료·등록 해제·스킬 grant 소실·위젯 바인딩 해제 때 게이지를 숨기고 마우스 입력은 가로채지 않는다.
- MCP의 UMG/Object/MaterialInstance 도구로 `WBP_SkillQuickSlot`에 아이콘 위·숫자와 키 라벨 아래의 게이지 이미지를 추가했다. 기존 `/Game/Assets/Textures/UI/Material/M_CircularProgress`를 부모로 `MI_SkillCooldown`을 만들어 FillPercent=0, AnimatedFill=0, Thickness=0.12, Color=(1,1,1,1)을 설정했다. 이미지의 기본 상태는 Collapsed이며 기존 아이콘과 같은 패딩/정렬을 사용한다. 외형은 WBP/MI가 담당하고 기존 공유 머티리얼과 HUD·아이템 WBP·D1은 수정하지 않는다.
- 편집 전 대상 dirty=false/PIE=false였으며 백업은 `Saved/Backups/SkillCooldownUI-20261024-010324/WBP_SkillQuickSlot.before.uasset`이다. WBP 컴파일 true, WBP/MI 두 에셋만 저장 true, 저장 후 각각 dirty=false를 확인했다. 변경 C++ 3파일은 아직 빌드/실행 반영 전이며 실제 게이지 렌더링·PIE·네트워크 검증은 후순위로 남긴다.

### 키 해제 시 조준 실패의 명시적 취소 연결 (2026-10-24)

- 조준 갱신이 필요한 정책에서 `PreReplicateAbilityInputReleased`가 실패하면 입력 전송만 생략하고 서버 실행은 남을 수 있는 경로를 확인했다. 기존 서버의 정책 스킬 자연 client End 무시 규칙은 유지하고, 실패한 해제 입력은 로컬 콜백에서 `CancelAbility(..., true)`로 명시적인 GAS 취소를 요청하도록 연결했다. 입력 준비 단계에서는 실패 표시와 이전 타깃 무효화만 수행하며, 실패 플래그는 콜백과 EndAbility에서 정리한다.
- 기존 `RequiresInputTargetRefresh` 및 즉발 정책의 추가 조준 생략은 유지한다. 취소 불가/서버 전용 종료 같은 기존 GAS 규칙을 강제로 우회하지 않으며 보안 RPC를 수정하지 않는다. 정상 타깃의 입력/시전 경로와 D1/WBP/아이템 코드는 변경하지 않았다.
- 컨테이너 cpp/h 구현만 변경했으며 빌드·플레이·멀티클라이언트 검증은 보류한다. 실제 원격 취소 성공을 검증한 것으로 보고하지 않는다.

### 시전 타입 표시의 정책 인터페이스 연결 (2026-10-24)

- `URPGSkillListSlot.cpp`에서 실행 Config 구조체 여섯 종류를 직접 구분하던 표시 분기를 제거했다. 정책의 읽기 전용 `GetExecutionDisplayName()`이 각 타입의 지역화 이름을 제공하고 위젯은 최종 RuntimeSpec의 ExecutionPolicyClass CDO에서 이름을 읽는다. 이름 조회는 실행 Host나 활성화 인스턴스를 요구하거나 상태를 변경하지 않는다.
- 기본 정책 여섯 개는 기존 지역화 namespace/key와 표시 문자열을 유지한다. 커스텀 정책은 자신의 표시 이름을 재정의할 수 있으며 기본값은 Custom이다. 새 정책 추가 시 목록 위젯에 타입 분기를 추가할 필요가 없고, 설정이 비어 있어도 유효한 즉발 정책은 Instant로 표시된다. 트라이포드/상태 오버라이드에 의한 정책 선택과 기존 목록 갱신 경로를 그대로 사용한다.
- 정책 cpp/h와 목록 위젯 cpp만 수정했다. 기존 WBP의 SkillTypeText 및 배치/입력 연결, 공유 소스의 RequiresInputTargetRefresh 예외, D1/아이템/실행 로직은 변경하지 않았다. 빌드·PIE·테스트는 후순위로 남긴다.

### ChargeSlash 운영 트라이포드와 WBP 표시 연결 (2026-10-24)

- 사용자 승인으로 `/Game/Blueprints/GameData/Skill/DA_Skill_ChargeSlash`의 비어 있던 TripodTiers에 레벨 4에서 여는 첫 티어/첫 옵션 `즉시 베기`를 추가했다. `RPGSkillExecutionPolicy_Instant`와 `RPGSkillInstantExecutionConfig.StartSection=Attack`만 덮어쓴다. 미선택 시 기본 Charge/Prepare→Attack을 유지하며 별도 피해 배율·쿨타임·조준·요구 상태를 추가하지 않는다. 기본 재사용 대기시간은 1초이고 기존 Ability의 폭주 요구도 그대로다.
- 기존 `AM_PlayerChargeSkill`을 읽어 Prepare/Loop/Attack 섹션과 0.35초부터 시작하는 공격 애니메이션을 확인했다. Attack 안에 `Player.Event.AOE` Notify(약 0.676844초), 나이아가라와 사운드 Notify가 이미 있어 재배치나 몽타주 복제를 하지 않았다. 몽타주와 `GA_Player_ChargeSlash`의 전후 SHA-256은 각각 `FFEDFB32F361D1A5850E7E258B3CB768A660CA4B485CD74912DAB1DA18BD7C4C`, `F7F4ACCA575B9A4E4927A433DB20E7DAEAD090D9F69E69FDE06C6E05F5B4F47F`로 동일하다.
- `WBP_TripodButton`에는 기존 TripodNameText/TripodImage가 있지만 표시 이벤트의 구현이 없었다. MCP 네이티브 그래프 편집으로 `BP_OnTripodInitialized`의 OptionName→텍스트, Description→툴팁, OptionIcon→소프트 텍스처를 연결했다. 아이콘 레퍼런스가 없으면 Hidden으로 숨겨 크기를 보존하고, 있으면 SelfHitTestInvisible로 표시한다. 선택 판단·RPC·SP·시전 방식 계산은 추가하지 않으며 기존 네이티브 모델/정책에 맡긴다. 기본 레이아웃·선택 이벤트·부모 클래스는 유지했다.
- 도구의 노드 핀 조회는 내부적으로 노드를 생성/제거해 WBP를 dirty로 만든다. 초기 비연결 기본 이벤트는 유지했고 실제 연결된 그래프가 없음을 확인한 뒤 필요한 표시 노드만 추가했다. DSL 편집은 한글 에디터의 이벤트 type ID를 처리하지 못해 저장하지 않았으며, 기존 네이티브 create_node/connect_pins API로 완성했다. 엔진 도구·전역 언어·새 도구/스크립트는 변경하지 않았다.
- MCP 8000/GUI PID 23620에서 WBP 컴파일 true와 **Definition/WBP 두 에셋만** 저장 true, 양쪽 dirty=false를 확인했다. 백업은 `Saved/Backups/ChargeSlashInstant-20261024-224158/`이다. 공유 보스 PIE는 중단하지 않았고 외부에서 종료된 후 수정·저장했으며 자기 Slate observer_1만 해제했다. D1/Gladiator·다른 에셋·C++는 변경하지 않았다. 재빌드는 필요 없으며 실제 WBP 클릭→서버 선택→즉발 시전/피해와 렌더링 검증은 사용자 방침대로 보류한다. ChargeSlash 자체 아이콘 선택과 다른 운영 트라이포드 구성도 남아 있다.

### 선택된 트라이포드 재클릭으로 해제 요청 (2026-10-24)

- ChargeSlash의 운영 옵션 추가 후, 기존 UI는 선택된 옵션을 다시 눌러도 같은 인덱스를 요청해 컴포넌트에서 중복 요청으로 무시되는 것을 확인했다. 단일 옵션에서는 스킬 레벨을 낮추지 않고 기본 실행 방식으로 돌아갈 클릭 경로가 없었다.
- `URPGTripodBoard::OnTripodBtnClicked`만 수정해 현재 모델상 선택된 옵션이면 `INDEX_NONE`, 아니면 클릭한 인덱스를 기존 `RequestTripodSelection`에 전달한다. 클릭 의도를 UI에서 명시적 명령으로 바꾸며, ViewModel/컴포넌트/서버의 선택 API를 토글로 바꾸지 않아 중복 명령이 선택을 다시 반전시키지 않는다.
- 기존 `IsTripodSelectionAllowed`는 유효한 티어의 `INDEX_NONE` 해제를 허용하며, 기존 RPC/소유자 복제와 모델 알림·시전 타입 표시 경로를 재사용한다. SP나 현재 시전 스냅샷은 수정하지 않는다. 이 변경이 반영된 후 해제 상태로 시작하는 다음 시전은 Definition의 기본 차지 정책을 사용한다.
- 변경은 Board cpp의 클릭 핸들러뿐이다. WBP/Definition/D1/서버 검증은 유지했고 diff --check만 통과했다. 빌드·핫리로드·PIE·자동화/네트워크 검증은 실행하지 않았으며 C++ 변경은 다음 정상 빌드에 묶는다.

### AssultBlade 즉발→차지 운영 구성 (2026-10-24)

- 다음 대상으로 제안한 AssultBlade의 역방향 전환을 구현했다. `/Game/Blueprints/GameData/Skill/DA_Skill_AssultBlade`의 첫 티어/첫 옵션 `차지 베기`는 레벨 4부터 선택 가능하며, Charge 정책과 Charge Config(단계당 0.5초, 3단계, 최대 차지 후 2초 유지, MinimumReleaseLevel=0, Prepare→Attack)를 적용한다. 기본 즉발 정책과 AssultBlade 시작 섹션, 원래 몽타주·피해량·1초 쿨타임·SoftTarget 조준은 바꾸지 않는다. 피해 보너스나 새 태그/Ability를 추가하지 않았다.
- 원래 `AM_PlayerSkill`은 AssultBlade와 JumpSmash가 공유하고 있어 직접 수정하지 않았다. 별도 `/Game/Blueprints/Character/Player/Anim/Montage/AM_AssultBlade_Charge`를 기존 `AM_PlayerChargeSkill`에서 복제해 Prepare/Loop/Attack 섹션 연결과 같은 스켈레톤/FullBodySlot을 유지했다. 앞의 두 준비/대기 세그먼트는 그대로 두고 Attack에는 원래 AssultBlade의 `Frank_RPG_GreatSword_Combo02_4` 전 구간(1.51666665초, 재생 속도 1)을 사용한다. Attack 시작은 0.35000002초, 전체 길이는 1.86666667초다.
- 템플릿의 공격 Notify는 새 몽타주에서만 제거하고 AssultBlade 첫 공격 구간의 Notify 4개를 옮겼다. Niagara 상태(0.5299083초), 사운드 2개(각 0.5353503초), 무기 충돌 상태(0.5399516초)이며 원래 공격 내 상대 시각과 상태 지속시간은 유지한다. 일반 Notify는 AnimationLibrary의 source 복사 API로 새 Outer에 복제하고, 상태 Notify는 기존 공개 설정을 새 상태에 복사했다. 상태 이벤트의 발동/필터 설정이 기본값인 것을 복사 전에 확인했으며 JumpSmash의 후반 Notify나 템플릿 ChargeSlash의 공격 Notify는 포함하지 않았다.
- 몽타주의 CompositeSections는 보호된 필드이고 SequenceLength는 읽기 전용이라 직접 변경하지 않았다. 트랙 구성 후 길이 쓰기 거부를 확인해, 엔진 SetCompositeLength와 같은 공식 AnimDataController.SetNumberOfFrames 경로를 사용했다. Controller의 모델 Outer가 신규 몽타주인지 확인해 원본 변경을 방지했고 최종 길이/섹션을 읽기로 확인했다. MCP 장문 콘솔 입력의 35초 타임아웃과 잘못된 Python 프로퍼티 별칭은 실패로 구분했으며 수정된 호출은 `RPG_ASSULT_CHARGE_NOTIFIES`의 count=4로 완료했다. 영구 스크립트/새 도구/엔진 수정이나 보호된 필드 접근 우회는 추가하지 않았다.
- MCP 8000/GUI PID 23620에서 Definition과 신규 몽타주 **두 에셋만** 저장 true, 각각 dirty=false를 확인했다. 백업은 `Saved/Backups/AssultBladeCharge-20261024-230317/DA_Skill_AssultBlade.before.uasset`이다. 원본 `AM_PlayerSkill` SHA-256 `598D6F8A365581683A74DAC3B54BB0E080BF87C49806CE32D2A53755E7AC0B6E`, `AM_PlayerChargeSkill` `FFEDFB32F361D1A5850E7E258B3CB768A660CA4B485CD74912DAB1DA18BD7C4C`, `GA_PlayerSkill_AssultBlade` `ECF5704E550E3F479CDF6AE25DB8CE62E4569D700C917AFC36D990EE4D43A6D6`는 전후 동일하다. 기존 WBP 데이터 표시를 재사용했고 WBP/C++/D1/Gladiator나 다른 작업의 에셋은 변경하지 않았다. 자기 observer_13을 해제했고 observer_17은 이미 없어 해제 false였으며, 다른 observer나 PIE는 종료하지 않았다.
- 이번 구성으로 ChargeSlash의 차지→즉발과 AssultBlade의 즉발→차지 운영 데이터가 각각 존재한다. 실제 클릭·유지/해제·서버 피해·연출/네트워크 검증은 하지 않았으므로 전체 시전 조합 완료나 플레이 성공으로 보고하지 않는다. 이전 Board 재클릭 해제 C++는 다음 정상 빌드에 포함해야 하며 이번 에셋 변경에 빌드를 새로 요구하지 않는다.

### AssultBlade 키 유지 2연속 콤보 운영 구성 (2026-10-25)

- 이전에 승인된 콤보 작업을 공유 PIE 종료 후 재개했다. `/Game/Blueprints/GameData/Skill/DA_Skill_AssultBlade` 첫 티어(요구 레벨 4)에 `2연속 베기`를 index 1로 추가하고 기존 `차지 베기` index 0은 보존했다. 추가 전후의 기존 옵션 JSON 필드가 동일한 것을 확인했다. 두 옵션은 같은 티어의 선택지이며 기본 즉발형·원래 피해 수치/타기팅·1초 쿨타임은 그대로다. 콤보의 추가 타격에는 기존 피해 계산을 사용하고 별도 피해 배율을 부여하지 않았다.
- 새 옵션은 기존 `RPGSkillExecutionPolicy_Combo`와 Combo Config를 사용한다. ComboSections는 [AssultBlade, JumpSmash], AdvanceEventTag는 GameplayEvent.Skill.Combo.Advance, bAdvanceWhileInputHeld/bAllowRepeatedPressBuffer는 true다. JumpSmash는 재사용한 몽타주의 두 번째 섹션 식별자이며 별도의 JumpSmash Ability나 Definition을 발동하지 않는다. 공용 WBP가 기존 데이터/모델 경로로 표시하므로 UI 타입 분기·Ability/RPC·C++를 추가하지 않았다.
- 전용 `/Game/Blueprints/Character/Player/Anim/Montage/AM_AssultBlade_Combo`를 `AM_PlayerSkill`에서 복제했다. 신규 에셋 편집기의 **몽타주 섹션 > 지우기**로 자동 섹션 연결을 해제했다. 이 공식 편집 동작만 사용했으며 보호된 CompositeSections 필드를 직접 수정하거나 원본 몽타주를 변경하지 않았다. 후속 입력 없이 첫 섹션이 두 번째로 자동 진행되지 않고 기존 Combo 정책의 전환 요청으로만 넘어가도록 구성한 것이다.
- 두 공격의 기존 Niagara/사운드/충돌·피해 Notify 9개를 유지하고 Combo 트랙에 `RPGAnimNotify_SendGameplayEvent`를 하나 추가했다. 시점은 첫 공격의 마지막 상태 Notify 종료(약 0.46847초) 다음 안전한 60fps 프레임인 0.5초이며, GameplayEvent.Skill.Combo.Advance를 보낸다. 네이티브 Notify의 bShouldFireInEditor=false를 유지했고 총 Notify 수 10개와 두 섹션 이름을 작성 과정에서 확인했다. 실사용 피해/렌더링·키 해제 경계의 플레이 검증으로 해석하지 않는다.
- MCP 8000에서 대상 dirty=false/PIE=false 후 편집했으며, Definition과 신규 몽타주 **두 에셋만** 저장 true/각각 dirty=false를 확인했다. 백업은 `Saved/Backups/AssultBladeCombo-20261025-005904/DA_Skill_AssultBlade.before.uasset`이다. 원본 `AM_PlayerSkill` SHA-256 `598D6F8A365581683A74DAC3B54BB0E080BF87C49806CE32D2A53755E7AC0B6E`, `AM_AssultBlade_Charge` `8242C286ABAD49B000F4EA681564570702F7FB41FF4D90092DC20A0FD10F9EB2`, `GA_PlayerSkill_AssultBlade` `ECF5704E550E3F479CDF6AE25DB8CE62E4569D700C917AFC36D990EE4D43A6D6`는 전후 동일하다. 자기 신규 몽타주의 편집기 1개와 observer_1만 닫았고 기존 빈 에셋 편집기 상태를 복원했다. 다른 세션의 PIE/에디터·D1/Gladiator는 변경하지 않았다.
- 빌드·PIE·자동화/네트워크 검증은 실행하지 않았다. 이번 에셋 추가에 C++ 재빌드를 요구하지 않지만 이전 Board 재클릭 해제 C++는 다음 정상 빌드에 포함해야 한다. 사용자가 미룬 콤보 해제–Notify 직전 경계의 실제 플레이 확인도 그대로 보류한다.

### 2026-10-25: 실제 클라이언트 퀵슬롯 RPC/OwnerOnly 관측

후속 완료: 사용자가 다시 Q/E에 JumpSmash/WhirlWind를 등록한 뒤 찬 슬롯끼리 스왑했고 08:01:39 UTC에 서버/소유자 모두 Q=WhirlWind, E=JumpSmash로 기록됐다. 이로써 등록 2개·빈 F로 이동·F/E 해제·찬 Q/E 스왑의 고유 6개 확인 항목을 마쳤고, owner 복제 업데이트 8회 모두 서버=소유자 및 다른 네 replica의 빈 배열 유지를 독립 비교했다. 아래 스왑 입력 대기는 끝났다. 최종 증거 `QuickSlotWire-20261025-1649/Validation.json`, 로그 `Project_RPG-backup-2026.10.25-08.05.08.log`의 SHA-256은 `8DEADD85CEFEB96DEBBC9AABEB1D0E54E017F8894A87608D254806B5960172B8`다.

원래 테스트 PIE는 08:02:14 UTC 이미 외부 종료돼 수동 cleanup의 IsValid가 만료된 Python wrapper 변환에서 실패했다. 다른 월드를 읽거나 종료하지 않았고 Entry/Standalone 1명/절전 true·observer_1 해제를 복원했다. 뒤이어 원 GUI PID 5384가 08:05:08 UTC 정상 종료돼 남은 Python 상태와 임시 관측 객체도 소멸했다. 현재 GUI PID 31512는 17:05:48 KST 시작해 실행 중이며 이 작업이 종료/재시작하지 않았다. 새 세션은 읽기만으로 Entry/PIE=false·기본 PlaySettings/절전 true와 GameUserSettings 해시 동일을 확인했다. 운영 코드/에셋 변경·새 빌드는 없고 지연·손실/예측 거부·Listen Server·모든 스킬별 원격 등록·비소유자 쓰기 공격으로 검증 범위를 확대하지 않는다.

- GUI PID 5384의 자기 dedicated server+실제 클라이언트 2개 PIE를 구성했다. 기존 무기 데이터 전체를 기준으로 서버 플레이어 둘에게 각각 9개를 부여했고 Client 1의 네 스킬 인스턴스가 유일하게 복제된 것을 확인했다. 슬롯 설정 RPC를 스크립트/MCP로 보내거나 서버 슬롯 배열/OnRep을 직접 바꾸지 않았다.
- 사용자의 실제 마우스 입력 뒤 owner 256의 JumpSmash Q 등록, WhirlWind E 등록, JumpSmash Q→빈 F 이동, F 해제, E 해제의 다섯 owner OnSkillSlotChanged 관측에서 서버/소유자 8칸 배열이 동일했다. Client 2의 owner 256 mirror와 변경하지 않은 owner 257의 서버/두 클라이언트 replica는 각각 8칸 빈 상태를 유지했다. read-only 스냅샷은 `Saved/Tests/Skill/QuickSlotWire-20261025-1649/Validation.json`에 보존했다. 입력 가드 안의 도구 호출로 받은 결과가 아니라 실제 클라이언트 입력의 서버 반영·소유자 복제 근거다.
- 요청했던 찬 Q/E 슬롯끼리 교환은 아직 미관측이다. 사용자에게 JumpSmash/WhirlWind를 Q/E에 등록한 뒤 Q→E 교환을 부탁했고, 자기 PIE와 읽기 전용 delegate 1개·observer_1을 의도적으로 유지 중이다. 상태 키 `__main__._rpg_quickslot_wire_20261025`의 월드 동일성부터 검사하고, 확인 뒤 자기 상태와 PlaySettings/절전/Entry 맵만 복원한다. 새 코드/에셋/빌드는 없다.
- 설정용 긴 MCP 요청은 HTTP 응답 60초 제한을 넘겼으나 에디터에서 완료돼 07:53:05 UTC 입력 준비 로그와 이후 정상 baseline을 확인했다. 중복 요청/엔진 수정/우회 도구는 만들지 않았다. 루프백 정상 조건의 이번 범위를 지연/손실·예측 거부나 Listen Server 피해/연출, 모든 스킬별 원격 등록/비소유자 쓰기 공격 검증으로 일반화하지 않는다.

### 2026-10-25: 발밑 지점의 로컬/서버 판정 정합성 수정

후속 완료: 공유 정상 빌드의 정책/테스트 obj가 각각 16:37:54/16:37:57 KST이며 링크 응답에 두 객체가 포함됐다. 루트 DLL/target은 16:40:31 KST(12,982,272바이트, SHA-256 `1C15E09A2EFD3CCD64DF4C51EF12A298FB9CE2A4C7A528670491C6B16B354CAC`), GUI PID 5384는 16:40:41 시작이다. 이 턴의 새 빌드나 핫리로드 없이 MCP에서 아래 새 테스트 두 개만 실행해 07:44:58 UTC에 모두 Success, passed=2/failed=0/skipped=0, errors=[]/warnings=[], 총 1.201초를 확인했다. 기존 `GroundTargeting-20261025-1614/Validation.json`과 `RuntimeResults.json`에 실제 결과를 보존했다. isolated world/ASC 통합 테스트이지 실제 마우스/키보드·원격 RPC·운영 지점 스킬 플레이 검증은 아니다. Entry/PIE=false와 기존 GameUserSettings 해시를 유지했고 GUI를 종료하지 않았다. 아래 개별 컴파일/정상 빌드 대기는 당시 상태이며 현재 추가 빌드는 필요 없다.

- `GroundPoint::ResolveTarget`은 타깃과 시전자의 수평 좌표가 같으면 시전자 전방을 보조 방향으로 사용하지만 `ValidateReplicatedTarget`은 바닥 재투영 뒤 같은 입력을 거부했다. 서버에 같은 전방 보조 규칙만 추가했다. 제출된 임의 벡터로 덮어쓰지 않고 서버 actor의 방향을 사용하며 기존 공통 사거리/조준/위치 오차 검사와 바닥 검사, 마지막 무효 방향 검사는 유지한다. SoftTarget 등 다른 정책은 변경하지 않는다.
- 기존 `RPGSkillRuntimeSpecTests.cpp`의 isolated world/ASC fixture를 재사용해 `ProjectRPG.Skill.Integration.GroundPointServerConsistency`(실제 충돌 바닥, 발밑 지점의 일치, 범위 초과 및 소실된 바닥 거부)와 `GroundReleaseFailureCancels`(충돌 바닥 위 실제 차지→바닥 제거→ASC release, GAS Cancelled 이벤트, 이전 조준/몽타주/타이머 정리 및 커밋된 쿨타임 유지)를 추가했다. 새 도구/프로브/테스트 프레임워크나 운영 API는 추가하지 않았다.
- 기존 Editor 응답 파일과 MSVC 14.44.35207로 정책/테스트 CPP 각각 격리 컴파일 종료 코드 0을 확인했다. 출력은 `Saved/Tests/Skill/GroundTargeting-20261025-1614/`에만 두었고 공유 Intermediate obj와 루트 DLL은 덮어쓰지 않았다. 루트 DLL SHA-256 `AA9520E7D918279A259B996CEA29FDE9BFED5EA96FF0A4B3E86880BFF4828332`는 변하지 않았다. **전체 빌드/핫리로드/새 테스트 실행이나 실제 원격 판정 통과를 뜻하지 않는다.** 다음 정상 Editor 빌드/재시작에 포함한 뒤 이 두 테스트만 실행한다.
- 앞선 GUI 준비는 임시 Definition의 EditDefaultsOnly 설정 거부, 기존 타입이 초기화된 InstancedStruct JSON import의 타입 보존, 메모리 Python 클래스의 Ability 인스턴스 기본값 전달 실패로 중단됐으며 플레이 검증 성공은 0개다. 운영 Definition·BP·몽타주를 고치거나 엔진 도구를 수정하지 않았다. 자기 PIE/상태/observer_2를 정리하고 Entry·절전 true·GameUserSettings 해시와 두 운영 Definition의 파일 해시 유지를 확인했다. 메모리 검사 클래스의 참조/태그/몽타주는 비웠고 현재 에디터에서 실행되지 않는다.
- 기록과 컴파일 근거는 `Saved/Tests/Skill/GroundTargeting-20261025-1614/Validation.json`, `Compile-Policy.log`, `Compile-Tests.log`다. D1/Gladiator·아이템/백엔드는 그대로이며 실제 네트워크/백뷰 플레이·아이콘/운영 기획과 사용자 보류 콤보 경계는 미완료 상태를 유지한다.

### 2026-10-25: 전체 무기 데이터 등록 확인과 잔여 검증의 중단 원인

- 기존 `BP_PlayerWeapon.PlayerWeaponData`의 기본/특수 능력 배열을 그대로 사용해 자기 Standalone PIE에 9개를 한 번 부여했다. 두 클래스만 별도 부여하던 이전 fixture는 반복하지 않았다. 네 특수 스킬의 고유 grant를 유지했고 서버 소유/모호성 검증이나 운영 데이터는 수정하지 않았다.
- 실제 Slate 드래그로 AssultBlade/JumpSmash/ChargeSlash 등록, WhirlWind로 덮어쓰기, JumpSmash↔AssultBlade 교환, AssultBlade↔WhirlWind 교환, WhirlWind 우클릭 해제, ChargeSlash/WhirlWind 재등록의 6개 체크포인트에서 8칸 배열을 읽어 기대값을 확인했다. 첫 도구 입력은 프리뷰 창 선택 전이라 모두 빈 상태였으며 무효 시도로 기록했다. 네트워크 RPC 증거가 아니라 Standalone의 렌더링된 UMG 입력 경로 확인이다.
- deferred Enhanced Input의 Q 누름/해제로 `GA_Player_JumpSmash_C`가 JumpSmash 섹션에서 실행되고 종료됐다. F 홀딩에서는 `GA_Player_WhirlWind_C`의 Default→Toggle을 관측했고 0.9초 후 해제 시 End로 진입한 뒤 타깃 상태가 정리됐다. 첫 관측기의 `get_mesh` API 실패는 제외하고 reflected Mesh를 사용한 완전한 후속 실행만 성공으로 기록했다. AssultBlade/ChargeSlash의 기존 시전 통과 기록을 불필요하게 재실행하지 않았다.
- 증거는 `Saved/Tests/Skill/AllSkillRegistration-20261025-1534/Validation.json`과 `Saved/Logs/Project_RPG-backup-2026.10.25-06.39.22.log`(SHA-256 `BD08C6E765764828226F04BED54C7B227635F5A00778ACBB084AD05C65F1B507`)다. 해당 GUI PID 9168은 06:39:22 UTC 정상 종료됐고 다음 GUI PID 10744에서는 Entry/PIE=false/절전 true 및 기존 GameUserSettings 해시 동일을 읽었다. 이전 세션의 actor/observer를 새 세션에서 재사용하지 않았다.
- 잔여 지연/손실 확인은 기존 `Run-IrisMultiClientTest.ps1`의 `-SkipBuild -ClientCount 2 -Port 17791 -TimeoutSeconds 120 -VerifySkillTripods -PacketLagMinMs 80 -PacketLagMaxMs 120 -PacketLossPercent 2`로 시도했다. 서버가 사용자 캐시 경로의 쓰기 거부와 writable DDC node 없음으로 초기화 중 종료됐고 클라이언트는 시작되지 않았다. 네트워크 통과는 0개이며 권한/캐시 우회나 도구 확장을 만들지 않았다. 로그 `Saved/Logs/IrisMultiClient/20261025-154900/Server.log`, SHA-256 `9D1CC93774CFAC11BAE9175E618C69068291B5ACBCFCC4BF92B25458E76EE706`다.
- GUI의 지점 조준/실패 해제 취소는 임시 Definition 인스턴스 설정까지만 시도했다. Python의 EditDefaultsOnly 설정 거부 뒤 06:52:29 UTC 공유 PIE가 외부에서 끝났고 복구 스크립트는 월드 존재 검사에서 멈췄다. 이후 native 설정은 객체 해석 단계에서 거부됐으므로 운영 에셋/종료된 actor 쓰기는 적용되지 않았다. 성공한 지점 테스트로 세지 않고 임시 상태/콜백도 export되지 않았다. 새 PIE를 재차 열지 않았다.
- `Saved/Tests/Skill/RemainingResume-20261025-1549/Validation.json`에 두 중단 원인을 기록한다. 다음은 공유 에디터 단독 사용 조율과 DDC 쓰기가 가능한 기존 실행 환경에서 미완료 범위만 이어가는 것이다. 원격 퀵슬롯 RPC/소유자 한정 복제·예측 거부 보정·Listen Server 피해/연출 중복, ChargeSlash 기본 아이콘 선택 및 추가 운영 기획, 사용자 보류 콤보 경계는 완료로 세지 않는다. 이 범위의 운영 코드/에셋/빌드 변경은 없다.

### 2026-10-25: 전체 스킬 등록 계약과 임시 부여 구성의 구분

- 사용자 직접 드래그 뒤 다른 스킬도 등록돼야 한다는 요청에 현재 코드를 확인했다. 드래그는 각 슬롯 ViewModel의 SkillTag를 사용하고 공통 조회는 Definition의 식별자와 유일한 부여 Spec을 찾는다. 특정 스킬 이름/슬롯/시전 타입별 등록 분기는 없다. 서버의 미부여·중복 grant 거부는 유지한다.
- MCP 읽기로 `BP_PlayerWeapon.PlayerWeaponData.SpecialWeaponAbilities`에 운영 AssultBlade, JumpSmash, ChargeSlash, WhirlWind 4개가 모두 있는 것을 확인했다. 직전 네트워크 fixture에서는 검증 준비용으로 AssultBlade/ChargeSlash만 직접 부여했으므로 다른 두 스킬은 등록 자격이 없었다. 이를 제품의 단일 스킬 하드코딩으로 오인해 C++ 분기나 무조건 등록 허용을 추가하지 않았다. 다음 런타임 부여 구성은 기존 무기 데이터에 맞춰야 하며, 4개 등록의 실제 입력 확인을 끝낸 것으로 보고하지 않는다.
- 사용자 입력 후 PIE가 이미 종료돼 현재 서버/비소유자 Actor 조회를 즉시 중단했다. 보존된 Python 순수 데이터인 owner 알림 16개에는 playerId 256의 첫 8개(Q=AssultBlade)와 다음 8개(Q=AssultBlade/E=Charge)가 기록됐다. 직접 OnRep/클라이언트 슬롯 대입을 하지 않은 상태에서 받은 소유자 알림 증거이지만, 사후 서버·비소유자 배열을 확보하지 못했으므로 전체 네트워크 검증 합격으로 올리지 않았다.
- `Saved/Tests/Skill/QuickSlotRPCResume-20261025-031315/Validation.json`에 이 구분과 캐시 알림·운영 부여 데이터를 기록했다. 자기 PIE는 사용자가 종료한 상태여서 새 실행/추가 입력을 하지 않았고, 자기 delegate/Python 상태/observer를 정리한 뒤 원래 Entry 맵·Standalone/1명·절전 true를 복원했다. GameUserSettings 해시도 기존과 동일하다. 이번에는 source/에셋/D1 변경이나 재빌드가 없다. 아래의 ‘사용자 입력 대기를 위해 PIE 유지’는 이전 상태다.

### 2026-10-25: RPC 방어 반영·회귀 통과와 MCP 공통 가드 제한

- 재개 시 최신 공유 산출물의 production 객체(02:59:07), 회귀 객체(03:03:06), 프로젝트 DLL 링크 응답의 두 객체 포함, 루트 DLL(03:04:00 KST, 13,032,960 bytes, SHA-256 `2260B6CDEAC63DD0C199B7AD2999ECA677BCD575B3C879E0178D645EE49E683B`), 이후 시작한 GUI PID 31408을 확인했다. UHT 헤더의 GENERATED_BODY도 최신 28행이다. 이 턴에서 새 빌드를 실행한 것은 아니며 같은 변경의 재빌드는 요구하지 않았다.
- 유휴 에디터에서 MCP Automation으로 추가 회귀를 포함한 `ProjectRPG.Skill.QuickSlot.RegistrationAndDrop`만 실행했다. Success/1 passed/0 failed/errors=[]/0.061초이며 미소유·모호한 스킬 거부 경고 1개는 기존 의도된 음성 케이스다. 이전 컴파일 차단/회귀 미실행 기록은 해소됐고 에디터 가드의 클라이언트 요청은 슬롯 보존·정상 반환했다. 실제 RPC 전송 검증과는 별개다.
- 자기 dedicated server+client 2개의 PIE에서 서버에 운영 AssultBlade/ChargeSlash만 부여하고 두 클라이언트의 grant 복제와 서버/소유자/비소유자 슬롯의 빈 baseline을 읽기로 확인했다. Python 클라이언트 상태 변경 요청을 사용하지 않고 네이티브 Slate Drag로 Client 1의 실제 SkillIcon→Q 입력을 보냈으나 상태는 변하지 않았다. 네트워크 성공으로 세지 않았다.
- 조사 결과 MCP 공통 `ModelContextProtocolToolLibrary.cpp:307` 및 비동기 `ModelContextProtocolToolAsyncAction.cpp:305`도 각 도구 호출에 FEditorScriptExecutionGuard를 적용한다. 따라서 네이티브 Slate 입력도 호출 스택 안에서 동기 실행되면 RPC를 로컬 처리하며 수정된 권한 검사가 이를 차단한다. 일반 게임플레이 드래그/네트워크 장애로 단정하거나, 다시 Python/OnRep/서버 직접 대입으로 검증을 대체하지 않는다. 엔진/프로덕션 도구·새 프레임워크는 추가하지 않았다.
- 실제 마우스 입력 한 번을 받아 관측하기 위해 자기 PIE를 유지했다. Client 1 `_C_1`/playerId 256/w7에서 AssultBlade→Q를 직접 드래그하고, Client 2 `_C_2`/playerId 257/w8의 비소유자 미노출과 서버 결과를 기존 read-only snapshot으로 비교한다. 상태 `__main__._rpg_quickslot_ui_rpc_20261025`, observer_1과 read-only delegate 바인딩만 남아 있고 tick callback/예약된 클라이언트 요청은 없다. Python record 함수는 상태를 읽고 기대값·결과를 기록할 뿐 RPC/OnRep을 호출하지 않는다.
- 증거는 `Saved/Tests/Skill/QuickSlotRPCResume-20261025-031315/Validation.json`/Client1-before.png다. 새 회귀는 통과했으나 이 실행의 네트워크 통과 항목은 아직 0개다. 입력 대기로 종료 정리도 pending이다. 완료/중단 시 자기 PIE의 동일성 확인 후 delegate/전역 상태/observer를 정리하고 Entry 맵·Standalone/1명·절전 true를 복원한다. 현재 테스트 설정은 Client/2명·절전 false이며 임의 전체 설정 파일 복원이나 다른 창/세션 종료는 하지 않는다.

### 2026-10-25: 퀵슬롯 RPC 에디터 로컬 실행의 재귀 방어

- 다음 작업으로 MCP 8000의 유휴 에디터 PID 2132에서 자기 dedicated-server PIE+클라이언트 2개를 시작했다. `_C_11/_C_12/_C_13`과 서버 Authority·두 소유 Pawn AutonomousProxy, 서로의 Simulated 복제본을 확인했다. 서버에 운영 AssultBlade/ChargeSlash만 fixture로 부여했고 서버/소유자/비소유자 슬롯의 빈 baseline까지 확인했다. 에셋/CDO/서버 보안 기본값은 변경하지 않았다.
- 첫 클라이언트 등록 요청에서 `EXCEPTION_STACK_OVERFLOW`가 발생해 에디터가 종료됐다. `SetSkillSlot→Server_SetSkillSlot→Server_SetSkillSlot_Implementation→SetSkillSlot`이 반복됐으며 등록·이동·교환·해제·거부 검증의 통과 수는 0개다. Python의 `PyUtil.cpp:644`는 ProcessEvent에 `FEditorScriptExecutionGuard`를 적용하고 UE `Actor.cpp:5467`는 이 가드에서 모든 RPC를 로컬 실행한다. 따라서 이번 경로는 실제 네트워크 RPC의 유효한 검증 수단이 아니었으며 일반 게임플레이 전송 오류로 확대 해석하지 않는다.
- 확인된 로컬 재진입 취약점만 최소 수정했다. 기존 세 RPC 구현에서 Owner/Authority를 확인한 다음 기존 요청 메서드를 호출한다. 서버의 미소유/모호한 grant 거부·교환/이동·아이템 ID 검증·OwnerOnly 복제는 바꾸지 않았다. 같은 구조의 스킬 등록·아이템 등록·해제를 모두 막되 기존 아이템 정책은 유지했다. 기존 RegistrationAndDrop 회귀에 EditorScriptExecutionGuard 아래 AutonomousProxy/SimulatedProxy의 요청이 기존 슬롯을 보존하는 검사를 추가했다. 새 테스트 프레임워크/도구/UCLASS나 엔진/생성 파일 변경은 없다.
- 기존 컴파일 응답의 출력만 Saved로 분리한 production `QuickSlotComponent.cpp` 컴파일은 종료 코드 0이다. 회귀 cpp는 현재 사용자 변경의 `RPGQuickSlotWidget.h` 28행과 오래된 생성 헤더의 26행 불일치 때문에 실패했으며 정상 UHT/전체 빌드가 필요하다. 이 부차적 컴파일 실패를 이번 권한 방어 코드의 실행 실패로 세거나, production 객체 컴파일을 전체 DLL/회귀 성공으로 보고하지 않는다. 기존 AppData/UBA 권한 실패 조합을 재시도하지 않았다.
- 결과는 `Saved/Tests/Skill/QuickSlotRPC-20261025-022412/Validation.json` 및 크래시 디렉터리 `UECC-Windows-8556BD964C27BD58D9FF6DBE9E6A179B_0000`에 있다. 에디터 강제 종료/재실행·에셋 저장은 하지 않았다. 프로세스 종료로 임시 Python/callback/fixture 메모리는 소멸했고 디스크 PlaySettings는 기존 Standalone/1명/one-process/별도 서버 false, GameUserSettings 해시는 이전과 동일하다. 새 에디터에서 Entry 맵/절전 설정을 다시 확인해야 한다.
- 다음은 정상 Editor 빌드/UHT 갱신과 추가 회귀 실행 후, Python의 클라이언트 UFUNCTION 호출이 아니라 에디터 스크립트 가드 밖의 실제 게임 입력으로 RPC를 확인하는 것이다. 직전 Standalone 12항목의 통과와 이번 미완료 네트워크 확인을 구분한다.

### 2026-10-25: 현재 빌드 반영·운영 변형 시전과 UI 가독성 확인

- 사용자가 잔여 목록의 계속 진행을 요청해 승인된 운영 변형과 최근 UI 변경의 필요한 확인을 재개했다. 이전 구현 우선 원칙을 폐기하거나 사용자 보류의 Notify 직전 키 해제 경계를 임의로 재개한 것은 아니다. D1/Gladiator·밸런스·임의 스킬/트라이포드 추가는 없다.
- 재클릭 해제 코드는 이미 공유 정상 빌드에 들어 있었다. `RPGTripodBoard.cpp`(10/24 22:51:51 KST)보다 최신인 객체(10/25 00:45:19), 프로젝트 DLL 링크 응답의 해당 객체 포함, 루트 DLL(00:45:57, 12,798,976 bytes, SHA-256 `45CA3CBF08D3E86275FD7457297ED2E9FBF75B839A73AA5CD29246FFB2AF0E2A`), 이후 시작한 GUI PID 2132를 확인했다. 새 빌드/핫리로드는 실행하지 않았다. 앞선 InventoryUI 로컬 빌드 로그의 recursive UBA 실패를 성공 증거로 사용하지 않는다.
- 실제 authored WBP에서 네이티브 CommonUI 클릭 핸들러→Board→ViewModel 경로를 확인했다. AssultBlade 레벨 4/SP 47에서 옵션 0 선택=Charge, 재클릭=-1/Instant, 옵션 1 선택=Combo, 재클릭=-1/Instant가 정상이며 선택/해제로 SP가 변하지 않았다. 이는 핸들러 호출이지 물리 마우스/RPC 전송 검증이 아니다.
- 실제 화면에서 선택 행의 흰 바탕과 흰 글자, 아이콘 없는 80px 이미지 아래 이름의 부족한 공간을 확인했다. 기존 `SkillListSlot_Style`의 선택/호버/눌림/비활성 배경을 어둡고 대비되는 색으로 조정하고, `WBP_TripodButton`의 이미지를 48×48로 줄이며 미지정 분기의 Hidden을 Collapsed로 바꿨다. 스타일의 참조자는 기존 스킬 목록 셀/트라이포드 버튼 두 곳뿐이다. 새 선택 강조 그래프나 로직은 없다. 두 대상만 컴파일·저장하고 백업 `Saved/Backups/SkillUIReadability-20261025-013109/`를 보존했다. `SkillWindow-before.png`/`SkillWindow-after.png`에서 선택 글자와 두 트라이포드 이름의 가독성을 확인했다.
- 자기 Standalone PIE `BP_RPGGameInstance_C_10`에서 정상 대검 장착 능력을 활성화해 부여된 운영 스킬을 사용했다. 슬롯 등록은 컴포넌트 요청으로 준비했고 입력은 엔진 `Input.+key`/`Input.-key` 경로다. AssultBlade 기본 즉발은 키 유지 중 한 번만 시전됐다. 차지 옵션은 Prepare→Loop→해제 후 Attack, 시전 중 옵션 해제 뒤에도 현재 차지 스냅샷은 유지되고 다음 시전은 기본 Instant로 돌아왔다.
- 운영 콤보는 키 유지 시 `AM_AssultBlade_Combo`의 AssultBlade→JumpSmash(관측 약 0.50초), 조기 해제 시 첫 섹션에서 종료, 해제 후 새 누름/해제의 버퍼 입력 시 2타가 정상이다. 마지막 타격 이후 키 유지가 전체 스킬을 재시전하지 않았다. 각 canonical AssultBlade 변형에서 1초 GE 하나만 관측했고 원형 게이지와 GAS 잔여 비율의 최대 오차는 0.097 미만으로 기존 0.1초 갱신 주기 안이었다. 만료 후 Collapsed였다. 조기 해제 케이스는 정밀 Notify 직전 경계 검증이 아니다.
- ChargeSlash는 Definition의 실제 태그 `Player.Ability.Skill.Charge`를 사용한다. 테스트 준비에서 이름을 추측한 `...ChargeSlash` 태그가 비어 시전하지 못했던 세 케이스는 준비 실패로 구분하며 제품 오류/통과로 세지 않는다. 실제 Definition 태그로 다시 준비해 기본 Prepare→Loop→Attack, 즉발 옵션의 Attack 직접 시작, 키 유지 시 반복 없음, 쿨타임 중 슬롯 해제의 즉시 게이지 숨김을 확인했다. 두 슬롯의 MID는 서로 다르고 E 시전은 Q 게이지를 표시하지 않았다. 폭주 Active 태그만 자기 일회용 authority PIE에 임시 제공했으므로 실제 폭주 자원/활성화 플레이 검증은 아니다.
- 운영 AssultBlade 차지 중 제어 yaw를 8.75°→68.75°로 변경한 뒤 키를 해제했다. 유효 타깃 결과의 조준/캐릭터 yaw는 68.75°로 갱신됐고 제어 yaw도 유지돼 공격이 카메라를 끌어돌리지 않았다. 이는 실제 SoftTarget의 타깃 없는 카메라 폴백 경로이며 타깃 잠금/지점 지정/원격 예측 검증으로 일반화하지 않는다.
- 첫 자기 PIE `_C_9`에서는 MCP 호출 없이 레벨/선택이 변경되고 세션도 외부 종료돼 결과를 정식 통과 근거에서 제외했다. 뒤의 `_C_10` canonical 결과와 구분한다. 네트워크용 DamageTarget은 명령행 opt-in 없이는 초기화하지 않으므로 이를 무리하게 재사용하거나 새 검증 프레임워크를 추가하지 않았다. 기존 네트워크 스크립트/Tripod probe도 퀵슬롯 등록 RPC를 직접 검사하지 않으므로 기존 합격을 새 등록 RPC의 합격으로 보고하지 않는다.
- 증거와 정리 상태는 `Saved/Tests/Skill/Completion-20261025-012146/Validation.json` 및 현재 로그의 `RPG_SKILL_UI_CASES`, `RPG_SKILL_RUNTIME`, `RPG_SKILL_CHARGE_VERIFIED`, `RPG_SKILL_BACKVIEW_AIM`에 기록한다. 추가 운영 기획/ChargeSlash 기본 아이콘 선택, 실제 퀵슬롯 소유자 RPC·비소유자 비노출, 지연/손실/예측 거부 보정·Listen Server 피해/연출 중복, 지점 지정과 타깃 해제 실패 취소는 아직 별도 미검증이다.

- 스킬창의 재초기화로 남은 이전 슬롯 VM과 창 닫기 뒤 이전 VM의 요청이 실제 레벨/SP/선택을 바꾸지 않았고, 다시 연 목록은 중복 없이 4개·SP 44/50으로 복원됐다. private 멤버 리플렉션 조회 실패는 제품 실패로 세지 않았으며 실제 요청의 무효 동작으로 확인했다. 최종 canonical 항목은 12개 모두 통과다.
- 정리 시 현재 game world와 저장한 자기 `_C_10`의 동일성을 확인하고 자기 PIE만 종료했다. 입력 Q/E/T/K 해제·임시 Rage 태그 제거·자기 callback 0개·Python 딕셔너리/참조 정리·observer 해제 후 원래 `/Engine/Maps/Entry`로 복원했다. 백그라운드 절전 설정도 원래 true이며 GameUserSettings SHA-256 `1259F3127AE236A70AB5AB1D849214BF332902CAB601C0430208C887CA03FC76`는 동일했다. 두 UI 대상 dirty=false와 원본 몽타주/Ability BP 해시 동일을 재확인했다. GUI PID 2132 및 다른 작업의 에셋 편집기는 종료하지 않았다.

### 다음 우선순위

2026-10-25 사용자 확정 마감: **확인된 오류만 고치고 스킬 시스템 작업을 종료한다.** 구체적 기획 없는 아이콘/추가 스킬·직업/트라이포드나 미실행 멀티 검증은 이번 작업을 계속 늘리는 필수 과제로 취급하지 않는다. 최종 TargetLock 수정은 탐색/위젯 단계의 취소 뒤 초기화·틱·전환이 계속되는 경로 차단, 실제 추가한 입력 서브시스템만 해제, 무효 종료/소멸 참조 방어다. 기존 CPP 응답 기반 격리 컴파일 exit 0과 diff check를 확인했다(`Saved/Tests/Skill/TargetLockCancelFix-20261025/`). 정상 모듈 반영/플레이 검증은 이번에 수행하지 않았으며 다음 예정된 빌드에 포함한다. 공유 에디터 종료·즉시 재빌드·새 검증을 요구하지 않고 작업을 마친다. 아래 우선순위는 이전 기록이다.

2026-10-25 원격 퀵슬롯 완료: 사용자 실제 입력 기반 등록·이동·스왑·해제와 소유자 한정 복제를 위 범위에서 확인했다. 자기 임시 세션/설정/참조도 정리됐으며 현재 에디터는 유지한다. 같은 빌드/검증이나 입력 대기를 반복하지 말고 남은 예측 예외/백뷰 플레이 및 확정 운영 기획만 후속으로 남긴다.

2026-10-25 지점 조준 후속 완료: 정상 모듈 반영과 새 두 테스트의 Success 확인을 마쳤다. 아래 빌드 실패/반영 대기는 이전 기록이며 같은 빌드·테스트를 반복하지 않는다. 남은 실제 원격 퀵슬롯/백뷰 플레이·예측 예외/Listen Server 범위와 확정 운영 기획만 후속으로 남긴다.

2026-10-25 정상 빌드 재시도: 공유 에디터 종료를 확인하고 CMD `Build.bat`를 실행했으나 컴파일 전에 UBT의 AppData/Roaming 설정 디렉터리 및 AppData/Local Trace.uba 쓰기가 거부돼 종료 코드 6이었다. 지정 빌드 로그는 생성되지 않았고 루트 DLL 해시도 이전과 같으므로 빌드 반영/새 테스트 통과로 세지 않는다. `GroundTargeting-20261025-1614/Validation.json`의 fullBuildAttempt에 명령과 실패를 기록했다. 현재 제한에서 재시도·권한 우회는 하지 않으며, 사용자 일반 CMD의 정상 빌드 완료 후 아래 두 테스트를 실행한다.

2026-10-25 지점 조준 수정 이후: 위 GroundPoint C++ 수정과 회귀 두 개의 개별 컴파일은 완료됐지만 정상 모듈 반영은 아직이다. 공유 에디터를 임의 종료하지 말고 다음 정상 Editor 빌드/재시작 후 `GroundPointServerConsistency`·`GroundReleaseFailureCancels`만 실행한다. 아래의 추가 빌드 불필요 기록은 이 소스 수정 이전 상태다.

2026-10-25 전체 스킬 등록 확인: 현재 자기 PIE/임시 설정은 정리 완료됐다. 공통 등록 경로와 기존 대검 데이터는 네 운영 스킬을 지원한다. 다음 실제 입력 검증은 임의의 두 스킬만 부여한 fixture 대신 기존 무기 데이터에 맞춘 부여를 전제로 진행한다. 사용자 직접 두 스킬 등록의 소유자 알림과 전체 네트워크 검증 완료는 구분하며 새 재빌드는 필요 없다.

2026-10-25 최신: RPC 방어 코드의 빌드/UHT 반영과 RegistrationAndDrop 회귀는 완료됐다. 새 재빌드가 아니라 **Client 1의 실제 마우스 등록 입력→서버/소유자/비소유자 복제 관측→자기 PIE/임시설정 정리**가 다음이다. MCP 자체의 동기 입력도 공통 에디터 가드 영향을 받으므로 네트워크 전송 증거로 세지 않는다.

2026-10-25 RPC 후속: 위 로컬 재귀 방어 C++를 추가했으므로 정상 UHT/Editor 빌드와 회귀 실행이 다시 필요하다. 현재 GUI는 검증 중 스택 오버플로 크래시로 종료된 상태이며 자동 재실행하지 않았다. Python 클라이언트 요청 기반의 네트워크 확인은 통과로 보고하지 않는다.

2026-10-25 최신: Board 빌드 반영과 위 운영 변형의 Standalone 시전 확인은 완료됐다. 아래의 과거 빌드 대기·콤보 승인 전·검증 미착수 기록은 당시 상태다. 다음 구현은 확정된 운영 스킬/아이콘 선택이나 재현된 누락에 한정하고 미검증 네트워크/지점 경로는 완료로 세지 않는다.

2026-10-25 콤보 구성 저장 완료: 위 승인 작업을 완료했으므로 아래의 공유 PIE 대기 기록은 이전 상태다. 추가 운영 데이터는 확정 기획에 따라 구성하고, 미룬 플레이 검증/아이콘 임의 선택/불필요한 리팩터링은 자동 재개하지 않는다. 이전 Board 재클릭 해제 C++의 다음 정상 빌드 반영만 남아 있다(다른 보류 검증과 별개).

2026-10-25 콤보 구성 승인/공유 PIE 대기: 사용자가 AssultBlade의 키 유지 2연속 콤보 트라이포드 구현을 승인했다. 기존 차지 옵션 인덱스는 보존하고 첫 티어의 다음 옵션으로 Combo 정책을 추가한다. AM_PlayerSkill의 두 공격을 전용 복제본에서 사용하며, 키를 놓았을 때 자동으로 후속 섹션에 진입하지 않도록 에디터의 Clear section ordering을 사용하고 1타 상태 Notify가 끝난 뒤 전환 Notify를 추가할 계획이다. 별도 Ability/RPC/위젯 분기나 수치 변경은 필요 없다. MCP 8000에서 보스 리허설 PIE=true가 유지돼 에셋 복제·편집·저장은 시작하지 않았다. 준비한 백업은 Saved/Backups/AssultBladeCombo-20261025-005459/에 있고 다른 작업의 PIE/observer는 건드리지 않았다. 실제 PIE 종료/편집 시점 조율 후 이 승인된 작업부터 재개한다. 이 기록 이전의 ‘후보/승인 전’ 상태는 더 이상 최신 상태가 아니다.

2026-10-25 선택 표시 연결 확인: MCP 읽기로 WBP_TripodButton의 기존 SkillListSlot_Style에 NormalBase(검정)/SelectedBase(흰색)가 있고 C++ SetSelectedInternal→SetButtonStyle 경로가 연결된 것을 확인했다. BP_OnSelectionChanged가 비어 있어도 이 기본 배경 표시는 네이티브 CommonUI 책임이므로 중복 강조 로직이나 스타일 수정은 하지 않았다. 실제 렌더링 확인·빌드·PIE/테스트는 실행하지 않았다. 다음 후보로 AssultBlade의 키 유지 2연속 콤보 트라이포드를 제안했으며 승인 전에는 에셋이나 수치를 추가하지 않는다. 이전 재클릭 해제 C++ 빌드 대기는 유지한다.

2026-10-24 AssultBlade 구성 저장 완료: 위 역방향 트라이포드와 전용 몽타주는 구현·저장됐고 이전 Board 재클릭 해제 C++만 빌드 반영 전 상태를 유지한다. 다른 스킬/트라이포드 기획·수치·아이콘을 임의로 정하거나 미룬 플레이 검증을 자동 재개하지 않는다.

2026-10-24 트라이포드 해제 입력: 위 재클릭 해제 요청은 소스 구현 완료/빌드 반영 전이다. 직전 Definition/WBP 에셋만 변경했을 때의 추가 빌드 불필요 상태와 구분한다. 이 한 파일 변경은 다음 예정된 정상 빌드에 포함하고, 이를 위해 공유 에디터를 중단하거나 같은 검증·빌드 실패를 반복하지 않는다.

2026-10-24 운영 구성 진행: 위 ChargeSlash의 첫 차지→즉발 트라이포드와 공용 WBP 데이터 표시를 저장했다. 최근 정상 C++ 빌드는 유지되며 이번 에셋 변경 때문에 추가 빌드를 요구하지 않는다. 다음 구현은 다른 실사용 스킬/트라이포드의 확정 기획 또는 확인된 누락에 한정하고, 검증·아이콘 임의 선택·추가 리팩터링을 자동으로 재개하지 않는다.

2026-10-24 재개 후 정상 빌드 확인: 별도 공유 작업의 `Saved/Logs/DungeonDepartureRecoveryBuild-20261024.log`에서 같은 Editor 타깃의 `Result: Succeeded`와 12개 작업 완료(37.67초)를 확인했다. 변수명 수정 cpp 재컴파일 및 프로젝트/Editor/OptionsUI DLL 링크·타깃 메타데이터 갱신이 포함되며 프로젝트 DLL은 20:20:24 KST/12,610,560바이트다. 최근 스킬 변경은 앞선 컴파일 결과와 이 링크에 포함돼 정상 빌드 반영 상태다. 이 대화의 사용자 요청 재시도는 동일 AppData 권한 제한으로 종료 코드 6이므로 자체 성공과 구분한다. 아래 실패 기록은 이전 시도의 한계이며 현재 Editor 빌드 미완료를 뜻하지 않는다. 추가 재빌드나 에디터/PIE/테스트 실행은 요구하지 않는다.

2026-10-24 빌드 시도: 사용자 요청으로 정상 Build.bat를 직접 실행했다. 에디터와 주요 DLL 점유는 없었고, 같은 타깃의 별도 빌드가 완료될 때까지 잠금을 정상 대기했다. 그 빌드의 C4458에 대해 `RPGInventoryProjectionItemSlotWidget.cpp:343` 지역 변수 Slot/참조만 LabelOverlaySlot으로 변경했다. 기존 빌드는 수정 전 오류로 실패했고, 이후 이 대화의 빌드는 AppData/Roaming UnrealBuildTool 디렉터리 생성 및 AppData/Local Trace.uba 쓰기 제한으로 컴파일 전에 종료 코드 6으로 실패했다. 루트 DLL 미갱신이며 전체 빌드 성공/최근 변경 반영은 아직이다. 강제 종료·잠금 삭제·권한 우회 없이 중단했으며, 다음 빌드는 해당 제한을 받지 않는 사용자 일반 CMD에서 진행한다. 지정 SkillEditorBuild 로그는 초기화 전에 실패했으므로 정상 생성됐다고 가정하지 않는다.

2026-10-19 사용자 방침: 구현에 집중하고 검증은 후순위로 미룬다. 아래의 과거 검증 우선 계획은 자동 재개하지 않는다. 확인된 필수 연결 누락과 신규 시스템의 실제 Definition/트라이포드 구성을 우선하며, 새 검증 도구나 임의 리팩터링을 추가하지 않는다.

10/13 공유 GUI 런타임 점검: 다음 작업 재개 시 이미 다른 인벤토리 검증의 `UEDPIE_0_testmap` 세션이 시작돼 있었다. 직전 IsPIERunning=false 조회 후 외부에서 PIE가 시작돼 AssetTools의 맵 dirty 조회가 play mode 오류로 실패했으며, 그 뒤의 이 작업 StartPIE는 실행되지 않았다. 실제 로그의 `VerifyInventoryWindowPIE` 및 `RPG_ITEM_UI_PREFLIGHT_JSON`에서 다른 작업의 사용을 확인했다. 해당 세션에서 native MCP 읽기만 수행해 컨트롤러의 설정/IMC가 transient 복제본이고 Q/E/F가 IA_QuickSkill1/2/3에 매핑됨을 확인했다. Screenshot에서는 새 스킬 3칸과 기존 아이템 4칸·키 라벨이 실제 렌더링됐다. 스킬 등록 8칸은 비어 있고 SP는 50이며 ASC grant는 기본 능력 7개였다. 출력은 `PIE-Readonly.png`, `PIE-LiveBindings.json`, `PIE-RuntimeBaseline.json`에 있다. 현재 단계는 UI 초기 연결/렌더링 확인이지 실제 학습·드래그·키 입력·서버 RPC 검증 성공이 아니다. 공유 PIE/다른 입력 작업을 중단하거나 조작하지 않았고, 자기 Slate observer_4만 해제했다. 다른 대화의 에디터 조작을 잠시 멈추도록 조율한 뒤 다음 입력 검증을 진행한다.

10/13 정상 빌드 후 회귀 실행: 사용자가 정상 빌드 성공과 에디터 재시작을 알렸고, 새 GUI PID 9548의 루트 DLL SHA-256은 `B371525AA894CA92CAD74660E19E808E72EF52C3A3A2E87EAFEB7CC635850A12`였다. MCP Automation에서 `ProjectRPG.Skill.QuickSlot` 3개를 실행해 passed=3/failed=0을 확인했다(`Saved/Tests/Skill/QuickSlot-20261013/Regression-MCP.log`, 현재 `Project_RPG.log`의 07:20 UTC 기록). 설정 충돌/파생 컨텍스트 갱신, 원래 Handle 해제/반복 입력 억제, 등록 이동·교환/미소유·중복 grant·오래된 드롭·타 Pawn 거부를 검증했다. 미소유 등록 거부 경고 1개는 의도된 음성 테스트 출력이며 테스트 errors는 없었다. 테스트 후 기존 DeathSound의 누락된 종속 패키지 로드 오류도 출력됐으나 이를 이번 퀵슬롯 테스트 실패나 수정 완료로 보고하지 않는다. 실제 드래그/키 입력·쿨타임 렌더링·RPC/소유자 복제는 아직 미검증이며, 공유 GUI 단독 사용을 확인한 뒤 testmap PIE에서 진행한다. 4칸 백뷰 배치/우클릭 방어 제안은 구현하지 않았다.

10/13 후속 빌드 오류 수정: `TStrongObjectPtr<UInputMappingContext>`에 전방 선언만 사용해 UHT 생성 vtable helper 생성자에서 완전 타입 검사가 실패했다. 컨트롤러 헤더에 `InputMappingContext.h`, 같은 패턴의 퀵슬롯 헤더에 `RPGQuickSlotViewModel.h`를 직접 포함했다. 엔진·생성 cpp를 수동 수정하거나 다른 포인터로 변경하지 않았다. UHT와 `RPGPlayerController.gen.cpp`, `RPGQuickSlotWidget.gen.cpp`, 실제 오류가 난 `Module.Project_RPG.2.cpp`의 Saved 격리 출력 컴파일이 모두 종료 코드 0으로 통과했다. `UHT-CompleteType.log` 및 `CompleteType-*.log`가 증거이며 전체 정상 링크/실행 성공은 사용자 재빌드로 확인한다.

우선 등록형 퀵슬롯의 정상 빌드/재시작과 위 회귀·실제 입력/RPC 검증을 진행한다. 아래 항목은 기존 스킬 시스템의 추가 검증 범위다.

1. 일반 스킬창 입력 경로와 네이티브 트라이포드 버튼→ViewModel→RPC→GAS 시전 연결은 위 항목과 같이 구현/검증했다. Desired 레이아웃도 저장/컴파일했으며, 에디터/PIE 단독 사용을 조율한 뒤 실제 창 크기와 게임 입력·커서/잠금 복원을 확인한다. 이전 마우스 검증의 창은 Python으로 열었고 신규 버튼/네트워크 테스트는 비렌더링 실행이므로 새 일반 입력 경로의 실제 렌더링·마우스·키보드 플레이 검증을 대신하지 않는다.
2. 신규 시스템의 스킬 Definition에 실제 사용할 트라이포드와 몽타주/이벤트를 구성한다. 현재 네 가지 기본 Definition 검증과 실행 정책 단위 테스트만으로 모든 시전 타입/조합의 플레이 검증이 끝난 것은 아니다.
3. 3인칭 조준/지점 지정과 시전 타입별 입력·취소를 PIE에서 확인하고, Listen Server의 피해/연출 중복을 검증한다.
4. 원본 Definition의 기본 네트워크 조건에서 쿨타임 예측·복제·조회·재입력은 위와 같이 확인했다. 기존 BP HUD로의 신규 알림 연결은 개별 컴파일까지 통과했으며 정상 빌드/반영 후 실제 알림 횟수·표시/갱신과 예측 거부/서버 보정·지연/손실 조건을 별도 확인한다. 신규 컨테이너의 쿨타임 단일화는 완료했으며, 기존 능력/D1의 개별 설정을 일괄 삭제하는 작업은 하지 않는다.

콤보 키 해제와 노티파이 직전 경계의 실제 플레이 확인은 사용자가 나중으로 미룬 항목이다. FastArray 최적화, 전체 효과/연출 모듈 분해, D1 코드 전환은 이번 연결 수정의 필수 작업이 아니다.

### 2026-10-25 직업 소속·습득·발동 자격 경계

사용자가 [직업 설계 방향](PLAYABLE_CLASS_DESIGN_DIRECTION.md)의 6절 순서와 8~9절 원칙에 따라 후속 구현을 요청했다. 이번 범위는 6.3 첫 단계의 C++ 기반이다. 직업 적용·저장·실제 콘텐츠 연결까지 완료한 기록은 아니다.

- `PlayerClass/RPGPlayerClassDefinition`은 안정적인 `ClassId`, 선택 가능한 스킬의 정확한 Definition/Ability BP 연결, 기존 장비 무기 조건, 최초 편성용 최대 8개 태그를 제공한다. 공통 캐릭터·컨테이너·UI에 직업별 분기를 추가하지 않았다. 무기 식별자는 기존 장비 enum을 사용하며, 새 직업 식별 enum을 만들지 않았다. 중복 SkillTag/Ability와 잘못된 기본 편성을 거절하고, 에디터의 `IsDataValid`에서 기존 스킬 실행·타기팅 검증을 재사용한다. 클래스 에셋의 스캔 경로는 `/Game/Blueprints/GameData/PlayerClass`다.
- 기존 `URPGPlayerSkillComponent`는 서버가 확인한 직업·명시적 습득 기록과 `Uninitialized/Loading/Ready/Failed` 상태를 소유한다. 카탈로그는 정의 탐색에만 사용한다. `BeginClassSkillRestore`의 요청 번호와 약한 컴포넌트 참조를 보관한 신뢰된 서버 호출자가 `CompleteClassSkillRestore`에 버전 1·안정적인 클래스 ID·명시적 습득 목록·서버 총 SP를 전달하는 계약이다. 클라이언트용 습득/직업 선택 RPC가 아니다.
- 복원은 전체 입력을 검증한 뒤 한 번에 반영한다. 중복·직업 외 스킬·무효한 레벨·잘못된 트라이포드·미지원 데이터 버전은 실패하고, 사용 SP는 유효한 레벨에서 다시 계산한다. 저장 실패/오류를 신규 기본 기록으로 덮지 않는다. 빈 습득 목록은 Ready가 될 수 있지만 스킬을 자동 생성하지 않는다. 기본 편성도 습득 근거가 아니다.
- 습득 기록의 요청 세대와 클래스 상태가 일치해야 조회·사용할 수 있다. 오래된 완료, 종료된 Pawn, 던전 권한이 회수된 소유자의 복원을 승인하지 않는다. Ready 상태에서 같은 Pawn의 전체 기록을 다시 덮어쓰는 API는 제공하지 않는다. 이는 실제 저장소나 사망/재접속 자원·쿨타임 복원 구현을 의미하지 않는다.
- `RPGClassSkillEligibility`가 Definition/Ability 연결과 습득을 공통 판정하고, 발동 시 기존 퀵슬롯의 현재 편성과 무기 조건을 더 확인한다. `SkillContainer::CanActivateAbility`와 활성화 진입부에 연결해 슬롯 외의 직접/이벤트 시전도 검사하며, 기존 GAS 비용·쿨타임·보안·생존·행동 제한 검사를 유지한다. 공통 기본 공격·회피·고유 능력에 8슬롯 조건을 일괄 적용하지 않는다.
- 성장 명령으로 습득 항목을 새로 만들지 않으며, 미습득 조회는 레벨 0을 반환한다. 클래스 스킬 실행 중 기존 편성·레벨·트라이포드 변경을 거절한다. 전투 판정과 구성 변경 장소/허용 정책은 아직 정하지 않았다. 슬롯 이동·교환은 같은 컴포넌트의 배열만 변경하고 Ability나 쿨타임을 제거하지 않는다. Press를 받은 원래 Spec으로 Release하는 기존 계약도 유지한다. 거절된 슬롯 요청도 owner 전용 revision을 보내 서버 상태로 화면을 갱신한다.
- UI는 검증된 클래스의 정확한 Definition 목록과 습득 상태를 표시한다. 클래스가 늦게 도착하면 목록을 다시 구성하고, 미습득 항목은 잠긴 상태/레벨 0으로 표시한다. 선택·레벨·복원 상태는 owner 전용 복제를 유지한다.

**현재 사용상의 제한:** 아직 실제 클래스 에셋이나 최초 습득 정책, 복원 API 호출부가 없다. 이 소스를 정상 빌드에 반영하면 검증된 직업·습득 기록을 연결하기 전까지 클래스 스킬 등록/성장/발동이 차단된다. 기존 시작/무기 경로의 GAS 부여만으로 이 조건을 통과시키지 않는다. 현재 코드를 대검 직업의 플레이 연결 완료로 해석하지 않는다.

다음 단계는 `URPGPlayerClassComponent`의 현재 Pawn/ASC 적용 수명과 부여 계획 검증, 시작·무기·직업 액티브·고유 능력 사이의 소유권 이관이다. 이번 구현은 Ability를 부여/회수하거나 `SourceObject`를 변경하지 않는다. 기존 소유 핸들 추적과 충돌/실패 시 처리를 먼저 구체화하고, 검증된 초기 습득·성장·편성을 해당 적용 경로에 연결한 뒤 대검 중심 스킬 몇 개를 연결한다. 고유 능력 API와 기존 `UActivateIdentity`의 부모 종료 계약도 이때 확인한다. 실제 영속 저장, 콘텐츠 변경의 변환/환급, 리스폰·재도전·재접속 자원/쿨타임 정책, 두 번째 직업은 남아 있다.

최소 컴파일: `Saved/BuildChecks/ClassSkillEligibility-20261025/Compile-Fixed.log`에서 UHT 성공 및 변경 CPP 9개 `-FileList -NoLink` 컴파일 종료 코드 0을 확인했다. 이후 UI의 목록 원본을 클래스 Definition으로 통일한 수정도 `Compile-ViewModel.log`의 개별 컴파일 종료 코드 0이다. 단독 컴파일에서 드러난 기존 헤더의 `Templates/SubclassOf.h` include와 `UDataTable` 전방 선언을 최소 보완했다. 모듈 링크·DLL 반영·핫리로드·에셋 변경·PIE·자동/네트워크 테스트는 수행하지 않았다. 과거 테스트 fixture는 명시적 직업/습득 준비를 추가해야 하므로 이전 통과 기록을 이번 자격 계약의 검증으로 사용하지 않는다. 새 검증 도구나 테스트 프레임워크는 추가하지 않았다.

### 2026-10-25 Pawn/ASC 적용과 부여 소유권

후속 요청으로 6.3 두 번째 단계의 소스 연결을 진행했다. `ARPGPlayer`의 기본 컴포넌트인 `URPGPlayerClassComponent`가 기존 성장 컴포넌트의 검증된 Ready 기록과 공통 시작 처리 완료를 기다린 뒤, 습득한 클래스 액티브와 설정된 고유 능력만 현재 Pawn/ASC에 부여한다. 성장·장비·편성·효과 실행의 원본은 기존 담당에 유지한다.

| 경로 | 현재 책임 |
|---|---|
| 공통 시작 | 이동·회피·장비 조작·상호작용 등 기존 공통 Ability와 시작 효과. `ARPGPlayer::PossessedBy`의 재진입/반복에 대해 같은 Pawn/ASC에는 한 번만 적용 |
| 직업 적용 | `SkillContainer` 액티브와 클래스 Definition에 명시한 선택적 고유 Ability. 자기 Spec 핸들의 단일 소유 목록을 추적 |
| 무기 | 기존 기본 공격과 실제 무기 고유 동작. 네이티브 로드아웃과 기존 BP용 `GrantPlayerWeaponAbilities` 모두 클래스 스킬/고유 능력 부여를 제외 |
| 성장·편성 | 기존 성장 컴포넌트와 `UQuickSlotComponent`. 직업 컴포넌트에 현재 스킬 레벨이나 현재 8슬롯을 복제하지 않음 |

시작 데이터의 기존 `IdentityData.IdentityAbility`는 클래스 소유 경로로 넘기며 신규 직업의 기본 고유 능력으로 자동 채택하지 않는다. 일반 공격의 부여 주체를 이름으로 변경하지 않았다. 무기 경로가 반환·보관하는 핸들에는 새 클래스 부여 핸들이 들어가지 않고, 기존 무기 해제 API도 직업 소유 핸들을 회수하지 않는다. 출처 불명의 기존 Spec과 클래스/SkillTag/입력 태그가 충돌하면 그 Spec을 임의로 압수하지 않고 적용을 거절한다. 이전 실행 중 Pawn을 핫리로드해 강제로 전환하는 기능은 제공하지 않는다.

부여 계획은 적용 전에 Definition 연결과 현재 ASC 충돌을 검증한다. 부여 중에는 발동을 막고, 각 핸들을 `GiveAbility` 콜백 전에 소유 목록에 기록한다. 콜백으로 Pawn/ASC 수명이 바뀌거나 부여가 실패하면 새로 부여한 자기 핸들만 취소·회수한다. 다른 시스템의 Ability나 효과를 일괄 제거하지 않는다. 같은 적용을 반복하거나 레벨·SP·트라이포드·슬롯·무기가 바뀌어도 클래스 Spec을 재생성하지 않는다. 한 번 적용한 핸들이 소실됐을 때도 자동 재부여하지 않는다.

발동 승인은 Definition/Ability 클래스뿐 아니라 직업 컴포넌트가 소유한 **정확한 Spec 핸들**과 전체 적용 완료를 요구한다. 기본 GAS 보안·비용·생존 검사를 유지하며, 고유 능력에는 8슬롯 조건을 적용하지 않는다. `SourceObject`는 클래스 부여에서 Pawn, 기존 네이티브 무기 부여에서 무기로 유지하고 Definition으로 교체하지 않았다. SkillContainer 쿨타임은 기존 Definition 기반 컨텍스트를 계속 사용한다. 직업 컴포넌트는 쿨타임·버프·고유 자원을 초기화하지 않는다.

ASC가 다른 Owner/Avatar로 전환되거나 ActorInfo를 비우는 경우, 컴포넌트 종료/해제 시에는 먼저 발동을 막은 뒤 자기 실행만 취소하고 자기 핸들만 회수한다. 해당 Pawn의 해제 상태에서는 다시 적용하지 않는다. 같은 Pawn의 일반적인 ActorInfo 갱신·재소유는 핸들을 유지한다. 새 Pawn은 별도 복원을 받아야 하며, 사망·재시도·재접속에 걸친 자원/쿨타임 보존은 아직 구현하지 않았다.

초기 슬롯 복원은 신뢰된 서버 C++ 호출인 `InitializeSkillSlotsFromRecord(ExpectedSkillRestoreGeneration, SlotTags, Error)`로 분리했다. 같은 성장 복원 요청인지, 직업 적용이 완료됐는지, 최대 슬롯 수·중복·습득·실제 클래스 소유 Spec을 확인한 뒤 전체 배열을 한 번에 반영한다. 복원 전에는 일반 슬롯 변경과 액티브 발동을 거절한다. 빈 배열/빈 위치는 그대로 보존하고 같은 Pawn에 두 번 복원하지 않는다. 최초 생성 호출자가 명시적으로 선택한 경우에만 `DefaultSkillSlots`를 이 API에 전달할 수 있다. 기존 캐릭터의 빈 기록이나 로딩/실패를 기본 편성으로 대체하지 않는다.

후속 저장/생성 연결부는 현재 Pawn과 요청 세대를 확인한 상태에서 **공통 시작 및 ASC 준비 → Definition 로드/검증과 명시적 성장 복원 → 클래스 적용 완료 확인 → 같은 요청의 슬롯 기록 1회 반영** 순서를 사용한다. 데이터가 먼저 도착했다면 호출자가 동일 Pawn의 준비 완료까지 기록을 보관해야 한다. 직업 컴포넌트가 저장 실패를 신규 기본값으로 전환하지 않는다.

클래스 Definition의 `IdentityAbility`/`IdentityInputTag`는 선택적 설정이다. 입력 태그를 비워 이벤트 기반으로 구성할 수 있지만 실제 고유 기능은 아직 없다. 현재 부여 트랜잭션은 InstancedPerActor/InstancedPerExecution, LocalPredicted/ServerOnly 및 명시적 트리거 구성을 지원하며 `OnGiven`/`OnSpawn` 자동 발동은 거절한다. 자동 고유 기능이 필요할 때 초기 적용 후 시작·실패·회수 계약을 별도로 구현한다. `UActivateIdentity::EndAbility`의 빠진 부모 종료 호출만 복구했으며, 기존 BP의 전체 효과/취소 동작을 검증했다는 뜻은 아니다.

다음 작업은 대검 클래스 Definition과 중심 스킬 몇 개에 실제 사용할 Ability/Definition을 연결하고, 명시적인 개발/생성용 습득·성장·슬롯 기록 공급 경로를 정해 위 순서를 사용하는 것이다. 초기 습득 정책과 저장소, 고유 능력 효과는 아직 미정이며 자동 학습이나 가짜 저장 복원으로 대체하지 않는다. 현재 소스만 정상 빌드하면 실제 기록 공급과 슬롯 초기화 전까지 클래스 액티브가 차단된다. 기본 공격·회피의 실제 에셋 연결과 대검 플레이, 저장·재접속·두 번째 직업은 검증/연결 완료로 간주하지 않는다.

검증: `Saved/BuildChecks/ClassAbilityOwnership-20261025/Compile-Fixed.log`에서 UHT와 변경 CPP 13개 `-FileList -NoLink` 컴파일 종료 코드 0을 확인했다. 첫 실행에서 확인한 기존 `RPGPlayer.cpp`의 SkeletalMesh include 누락을 보완한 뒤 성공했다. `git diff --check`도 통과했다. 모듈 링크·DLL 교체·핫리로드·에셋 변경·PIE·자동/네트워크 테스트는 수행하지 않았다. 새 테스트 도구/프레임워크를 추가하지 않았으며, 이전 테스트 fixture의 명시적 성장·클래스·슬롯 준비는 후속 검증 시 갱신해야 한다.

### 2026-10-25 대검 개발용 기록 공급과 콘텐츠 생성 준비

후속 요청으로 기존 대검 중심 스킬 4개를 재사용하는 클래스/개발 프로필의 초기화 코드와 생성 작업을 준비했다. 준비 당시에는 GUI PID 31512에 신규 PlayerClass 타입이 없어 실제 저장을 보류했다. 이후 사용자가 에디터를 종료하고 계속 진행하도록 지시해, 아래의 정상 빌드 반영과 에셋 생성까지 완료했다. GUI 강제 종료·재시작이나 핫리로드는 하지 않았다.

| 구분 | 구체적인 데이터 |
|---|---|
| 클래스 생성 대상 | `/Game/Blueprints/GameData/PlayerClass/DA_Class_GreatSwordWarrior`, 안정 ID `GreatSwordWarrior`, 기존 GreatSword 무기 조건 |
| 개발 프로필 생성 대상 | `/Game/Blueprints/GameData/PlayerClass/Development/DA_GreatSword_Development`, 데이터 버전 1, 개발용 총 SP 50 |
| 명시적인 습득 기록 | AssultBlade·JumpSmash·ChargeSlash·WhirlWind 4개, 각각 레벨 1, 트라이포드 미선택 |
| 명시적인 초기 편성 | 위 4개 SkillTag 순서, 나머지 슬롯은 빈 상태 |
| 고유 능력 | 미설정. 기존 IdentityData나 폭주 효과를 새 투지 해방으로 대체하지 않음 |

현재 GUI에서 기존 네 Ability CDO의 Definition 참조와 OnTriggered/OnInputTriggered, InstancedPerActor, LocalPredicted 설정을 읽어 확인했다. 활성화 Required/Blocked 태그도 네 개 모두 빈 배열이었다. 기존 Definition·Ability·몽타주를 새 데이터로 재작성하거나 재부모화하지 않는다. 이는 신규 직업 부여 경로의 플레이/네트워크 검증을 뜻하지 않는다.

`URPGPlayerClassDevelopmentProfile`은 개발용 습득·성장·편성 입력만 정의한다. `URPGPlayerClassDevelopmentComponent`는 **WITH_EDITOR 빌드의 권한 있는 게임 Pawn**에서 프로세스 명령행 `-RPGDevelopmentClassProfile=<asset object path>`가 명시된 경우에만 동작한다. 일반 실행과 Shipping의 자동 습득, 클라이언트 RPC, travel URL 또는 저장 실패 fallback 경로가 아니다. 다른 직업도 프로필 데이터로 선택할 수 있으며 공통 캐릭터/컨테이너/UI에 대검 이름 분기를 추가하지 않았다.

기존 직업 적용 컴포넌트에는 준비 여부 조회와 적용 상태 알림만 추가했다. 개발 공급자는 공통 시작/ASC 준비를 기다린 뒤 프로필을 비동기로 로드하며, 같은 World·Pawn·Controller·PlayerState와 요청 세대인지 다시 확인한다. 실제 성장 복원이 이미 시작됐거나 실패/완료된 Pawn에는 개발 기록을 덮어쓰지 않는다. 검증된 프로필을 기존 성장 복원 → 클래스 적용 → 같은 요청의 슬롯 기록 1회 반영 경로에 전달한다. 비용/SP 계산과 GAS 부여·슬롯 상태는 각 기존 소유자가 처리한다. 실패 시 기본값 재시도나 임의 습득을 하지 않는다.

프로필 로드가 동기 완료되는 StreamableManager 설정도 처리하고, 성장·부여·슬롯 알림 중 Pawn이 종료되면 이후 작업과 성공 로그를 중단한다. 종료/해제 때 자기 로드 핸들과 상태 알림 구독만 정리한다. 초기화 성공 뒤에는 성장/무기/슬롯 변경을 감시하며 개발 기록을 다시 적용하지 않는다.

생성 스크립트 `Scripts/Editor/CreateGreatSwordClassDevelopment.py`는 정상 모듈에서 위 에셋 2개만 새로 만든다. 기존 대상이 있으면 정확한 설정을 확인하고 보존하며, 사용자 설정이 다르거나 대상에 저장하지 않은 변경이 있으면 거절한다. 인자 없이 소스/기존 대상만 확인하고 `-RPGCreateGreatSwordClass`를 명시했을 때만 저장한다. 이것은 구현용 콘텐츠 생성기이며 새 플레이 검증 도구가 아니다.

정상 빌드 이후의 생성 명령:

```powershell
& 'D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'C:\UE5\Project_RPG\Project_RPG.uproject' -run=PythonScript -script='C:\UE5\Project_RPG\Scripts\Editor\CreateGreatSwordClassDevelopment.py' -RPGCreateGreatSwordClass -unattended -nop4 -nosplash -nullrhi -NoLiveCoding -multiprocess
```

생성 이후에는 `Scripts/Development/Open-PlayerClassDevelopment.ps1`로 기존 testmap을 명시적 개발 옵션과 함께 실행할 수 있다. `-PrintCommand`는 실행하지 않고 명령만 출력하며 에셋 생성 전에도 사용할 수 있다. 이 런처는 프로젝트 기본 클래스/맵이나 실제 캐릭터 저장 기록을 변경하지 않는다. 기존 Q/E/F 세 개 슬롯 입력·UI는 유지한다. 네 번째 편성 항목까지 동시에 입력/UI에 노출하거나 최대 8개 편성을 완성하는 작업은 후속 범위이며, 현재 세 칸 안에서 원하는 스킬을 교체하는 경로를 사용한다.

확인 범위: `Saved/BuildChecks/GreatSwordDevelopment-20261025/Compile.log`의 UHT 및 변경 CPP 4개 `-FileList -NoLink` 컴파일 성공. 로드/종료 콜백 보완은 `Compile-Lifetime.log`의 해당 CPP 재컴파일 성공이다. Python 생성기 AST 검사, PowerShell 런처 구문 분석과 `-PrintCommand`도 통과했다. 에셋 생성·모듈 링크·실제 개발 초기화·플레이/네트워크 검증은 아직 미실행이다. 초기 4개 습득/SP/편성은 명시적인 개발 데이터이며 출시 성장/초기 습득 정책이 아니다.

#### 정상 모듈 반영과 실제 에셋 생성 완료

`DA_Class_GreatSwordWarrior.uasset`와 `Development/DA_GreatSword_Development.uasset`를 실제로 생성·저장했다. 중심 스킬 네 개의 Definition/Ability BP를 그대로 참조하며 클래스 ID `GreatSwordWarrior`, GreatSword 조건, 명시적인 레벨 1 습득 네 개·트라이포드 미선택·총 SP 50·4슬롯을 기록했다. 고유 능력은 미설정이다.

진행 중이던 정상 `Project_RPGEditor Win64 Development -NoLiveCoding -NoHotReloadFromIDE -NoUBA` 빌드의 453개 작업과 링크 성공을 확인했다. 중복 빌드는 실행하지 않았다. 루트 런타임 DLL은 2026-10-25 18:41:36 KST, 12,631,552바이트, SHA-256 `9D7ED29176B62222672FFA35F882775ED86441E8F95DE9C5B8B01F7AB8F347C3`이며 Editor 모듈도 18:41:30에 갱신됐다. 전체 로그 복사본은 `Saved/BuildChecks/GreatSwordDevelopment-20261025/NormalEditorBuild.log`다. 앞 단계의 개별 컴파일만 완료/정상 모듈 반영 대기는 이 범위에서 해소됐다.

에셋 생성기는 UE Python의 GameplayTag 래퍼 객체 비교를 직렬화된 태그값 비교로 바꾸고, EditDefaultsOnly 구조체 필드는 독립 인스턴스의 setter 대신 네이티브 구조체 생성자 인자로 구성하도록 보완했다. 처음 두 실행은 저장 전 소스 확인 단계에서 멈췄다. 수정 후 `CreateAssets-StructInit.log`에서 두 대상만 `created`에 포함되고 명령렛 종료 코드 0을 확인했다. 추가 읽기 실행 `ReadbackAssets.log`도 종료 코드 0, `apply=false`, `created=[]`로 저장된 클래스/프로필의 타입·참조·습득·슬롯 값을 확인했다.

기존 네 Definition·네 Ability BP·공유 몽타주 세 개의 SHA-256은 생성 전후 11/11 일치한다. 새 에셋 두 개도 읽기 실행에서 재저장하지 않았다. 근거는 같은 폴더의 `SourceAssets-Comparison.json`, `CreatedAssets-Comparison.json`, `Validation.json`이다. 성공한 명령렛에는 기존 드라이브 저널·BT_Guardian 패키지·Python enum 이름 관련 경고 3개가 있으며 이번 범위에서 해당 콘텐츠를 수정하지 않았다.

실행은 `Scripts/Development/Open-PlayerClassDevelopment.ps1`를 사용한다. 실제 서버 초기화 성공/스킬 시전·PIE·네트워크 검증은 실행하지 않았으므로 에셋 저장/재로드 완료와 구분한다. 기본 게임 설정이나 실제 저장 캐릭터에 개발 기록을 적용하지 않았다. 남은 구현은 기존 3칸 입력/UI에서 네 번째 중심 스킬까지 동시에 노출하는 연결, 고유 능력의 구체화, 이후 최대 8개 편성의 점진적 완성이다. 영속 저장과 사건별 자원/쿨타임 복원은 별도 범위다.

### 2026-10-25 대검 투지 해방 런타임 연결

> 2026-10-26 후속: 현재 에디터의 새 타입 로드를 확인해 클래스 바인딩과 차지 4레벨 두 선택을 실제 에셋에 저장했다. 아래 클래스 미설정/에셋 적용 대기는 이 후속 이전 기록이다. 적용 범위와 남은 효과는 [최신 콘텐츠 기록](#2026-10-26-투지-해방-바인딩과-차지-4레벨-선택-저장)을 따른다.

[첫 성장 구간 v0.1](PLAYER_PROGRESSION_FIRST_SLICE_V01.md#34-고유-능력구성-변경회복)의 투지 규칙을 네이티브 `URPGGreatSwordResolveAbility`에 구현했다. 기존 빈 `UActivateIdentity`나 Rage 효과를 새 직업의 고유 능력으로 채택하지 않는다. 클래스의 `IdentityAbility`가 이 정확한 네이티브 타입이고 안정 ID가 `GreatSwordWarrior`일 때만 적용한다. 실제 클래스 에셋의 연결은 아래 별도 단계다.

#### 원본과 적중 확인

- 게이지 원본은 기존 GAS `CurrentIdentityGauge`/`MaxIdentityGauge`다. 최대 100을 확인하고 전용 Instant GE로 증감한다. 클래스 해금·소유 Spec·현재 Pawn/ASC·생존·권한과 대검 장비를 확인하며 임의 지급/소모 RPC는 추가하지 않았다. 기존 Full/None 태그가 동시에 남던 부분도 해당 게이지 분기에서 보완했다.
- 컨테이너는 서버 HitQuery와 기존 최종 피해 승인 경로가 성공한 첫 적중에 시전당 20을 준다. 지급 표시를 GE/UI 콜백 전에 확정하고 활성화 세대를 확인하므로 다중 대상·다단히트로 반복 지급하지 않는다. 보스의 기존 조우/컨트롤러·피해 권한 검증은 유지한다.
- 기본 공격은 기존 대검 약공격 태그의 `URPGComboSkillAbility`가 사용한다. 연속된 전체 몽타주 섹션 각각에 승인된 적중이 있고 마지막 섹션까지 완료했을 때 연계당 10을 준다. 빠른 개별 입력으로 이어지는 기존 연계도 지원한다. 중간 누락·취소·연계 초기화는 완료 보상이 아니며 같은 단계의 여러 적중은 한 번으로 센다.
- 위 고유 능력이 명시된 클래스에서는 기존 `GainIdentity()`와 피해 실행의 레거시 per-hit 획득을 억제해 별도 Rage/게이지 지급이 섞이지 않게 한다. 잠긴 상태도 레거시 경로로 해금을 우회하지 않는다. 다른 클래스의 기존 획득은 유지한다.

#### 소비와 강화의 수명

고유 능력 입력은 투지 100을 소비하고 10초짜리 전용 GE에 `Player.Status.Identity.GreatSword.Ready`를 부여한다. Ability 자체는 즉시 종료하므로 장시간 활성 Ability로 남기지 않는다. 남은 기회는 해당 인스턴스의 GE 핸들로 추적하며 클래스 Spec 회수 시 자기 핸들만 제거한다.

다음 차지/회전의 **서버 시전 확정 뒤** 기회를 한 번 소비하고 해당 활성화의 피해 배율 1.25를 고정한다. 핸들과 배율은 효과 제거 콜백 전에 확정해 다른 시전이 같은 기회를 빌려 쓰지 못한다. 회전은 해당 시전의 모든 타격에 적용한다. 차지는 네이티브 Charge 정책이 최대 충전 단계에서 실제로 해제됐음을 확인한 공격에만 적용하며 충전 중·부분 해제에는 강화 피해를 주지 않는다. 취소·헛침·부분 해제 후에도 이미 소비한 투지/기회를 환급하지 않는다. 원래 피해와 최종 계산 피해의 보안 상한을 강화 뒤에도 그대로 검사한다.

기회 사용 시 같은 Pawn/ASC·성장 복원 세대·전투 복구 revision·생명주기 epoch를 대조한다. 사망/재사용 뒤 이전 생명의 기회를 새 공격에 적용하지 않는다. 일반 재접속/로비 편집으로 게이지·쿨타임을 재설정하거나 강화 기회를 다시 부여하지 않는다. 새 전투 시도의 게이지·회복약·쿨타임 초기화 전체는 별도 계약이다.

#### 클래스 에셋 적용과 다음 콘텐츠

[ConfigureGreatSwordIdentity.py](../Scripts/Editor/ConfigureGreatSwordIdentity.py)는 정상 모듈에 새 타입이 로드된 뒤 기존 `DA_Class_GreatSwordWarrior`의 고유 능력/입력을 연결한다. 기본 실행은 읽기이며 `-RPGConfigureGreatSwordIdentity`가 있어야 `URPGGreatSwordResolveAbility`와 `InputTag.IdentitySkill`을 저장한다. 정확한 안정 ID·미설정 또는 동일 구성만 허용하고 다른 고유 능력/입력, 대상의 미저장 변경, PIE 중에는 거절한다. Definition·몽타주·개발 프로필은 수정하지 않는다. 기존 클래스 생성기의 읽기 검사도 이 명시적 후속 구성을 허용하도록 보완했다.

실행 예시는 정상 모듈이 준비되고 대상 에셋을 다른 에디터가 편집하지 않는 상태를 전제로 한다.

```powershell
& 'D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'C:\UE5\Project_RPG\Project_RPG.uproject' -run=PythonScript -script='C:\UE5\Project_RPG\Scripts\Editor\ConfigureGreatSwordIdentity.py' -RPGConfigureGreatSwordIdentity -unattended -nop4 -nosplash -nullrhi -NoLiveCoding
```

**이번 단계에서는 클래스 에셋을 저장하지 않았다.** 열린 공유 에디터에 새 네이티브 타입을 반영하거나 강제 종료/재시작하지 않았다. 따라서 현재 저장된 클래스의 고유 능력 미설정은 아직 해소되지 않았고, 해금된 운영 기록의 실제 복원 성공으로 세지 않는다. 정상 모듈 반영 뒤 위 명시적 바인딩이 필요하다. 개별 트라이포드의 안정 ID/수치/동작·무력화/부위 파괴 효과 작성도 아직 남아 있다.

검증은 UHT와 선택한 관련 CPP 8개의 `-FileList -NoLink` 최소 컴파일이다. 최초 실행의 태그/속성 CPP, include 보완 뒤 [성공 로그](../Saved/BuildChecks/GreatSwordResolve-20261025/Compile-Fixed.log)의 나머지 CPP와 생명주기 보완 뒤 [최종 증분 로그](../Saved/BuildChecks/GreatSwordResolve-20261025/Compile-Final.log)에서 모두 컴파일 성공을 확인했다. 초기 단독 컴파일에서 드러난 기존 승인 적중 컨텍스트의 전방 선언/WeakObjectPtr, 정책의 TimerHandle, 레거시 Player Ability의 World/Sound 의존성만 필요한 범위에서 보완했다. Python 두 작성 스크립트의 AST 구문 검사와 diff check도 통과했다.

열린 Live Coding 세션에서 산출물을 임시 위치로 컴파일했으며 정상 DLL 링크·교체·핫리로드·에셋 작성·실제 시전/GE/복제·PIE/네트워크 테스트는 수행하지 않았다. 이번 변경은 새로운 보스/트라이포드 조합이나 검증 프레임워크를 추가하지 않는다. 다음은 정상 모듈/클래스 바인딩과 트라이포드의 실제 데이터·실행 효과 연결이다.

### 2026-10-26 투지 해방 바인딩과 차지 4레벨 선택 저장

현재 공유 에디터에 `/Script/Project_RPG.RPGGreatSwordResolveAbility`가 로드된 것을 읽기로 확인했다. PIE가 아니고 두 대상 패키지에 미저장 변경이 없음을 확인한 뒤 기존 에디터의 Object/Asset Toolset으로 작성했다. 에디터 종료·재시작·핫리로드·별도 빌드 없이 다음 두 에셋만 저장했다.

- `DA_Class_GreatSwordWarrior`: 안정 ID와 네 스킬 참조를 유지하고 `IdentityAbility`를 새 네이티브 투지 해방, `IdentityInputTag`를 기존 `InputTag.IdentitySkill`에 연결했다. 실제로 저장된 클래스의 고유 능력 미설정은 이 범위에서 해소됐다.
- `DA_Skill_ChargeSlash`: 기존 4레벨 티어의 즉시 베기 옵션을 인덱스 0에 그대로 두고 `FullCharge`/`QuickCharge`를 인덱스 1/2에 추가했다. 저장 ID는 이름·순서로 추측하지 않고 각 옵션의 `PersistenceId`에 명시했다. 기존 저장/개발 선택 인덱스를 이동시키지 않았다.

| 새 4레벨 선택 | 최대 충전 시간 | 기존 기본 피해에 대한 배율 | 해제 조건 |
|---|---:|---:|---|
| `FullCharge` · 완충 강화 | 1.2초 | `3.2 / 2.8 ≈ 1.142857` | 최대 충전 단계에서만 공격 |
| `QuickCharge` · 빠른 충전 | 0.7초 | `2.6 / 2.8 ≈ 0.928571` | 최대 충전 단계에서만 공격 |

두 선택은 기존 Charge 정책과 준비/공격 섹션을 사용한다. 타입이 지정된 `FRPGSkillChargeExecutionConfig`에서 단계당 시간과 `MinimumReleaseLevel=MaxChargeLevel`을 작성했으며 최대 단계 전에 키를 놓으면 실행이 취소된다. 피해는 기존 `Shared.Stat.Damage` 배율로 합성하므로 새 BP 분기나 별도 피해 호출을 만들지 않았다. 투지 해방의 완충 판정·시전당 강화·피해 상한 검사도 기존 경로를 사용한다.

기본 피해 테이블·몽타주/Ability BP·기본 쿨타임 1초·기본 충전 1.5초·최대 스킬 레벨 12·타기팅/보안 상한은 바꾸지 않았다. 표의 피해는 기존 데이터에 대한 상대 배율이며 v0.1의 모든 기본 계수/쿨타임을 운영 데이터로 교체한 것이 아니다. backend 프로필의 최대 투자 7레벨 제한도 그대로다.

[ConfigureGreatSwordChargeTripods.py](../Scripts/Editor/ConfigureGreatSwordChargeTripods.py)에 동일 작성 규칙을 남겼다. 정확한 스킬·Charge 정책/설정·유일한 4레벨 티어를 확인하고 기존 옵션을 보존해 빠진 두 ID만 덧붙인다. 같은 ID에 다른 설정이 있으면 덮어쓰지 않는다. 기본은 읽기이며 `-RPGConfigureGreatSwordChargeTripods`가 있어야 저장한다. 이번 적용은 이 작성 함수로 만든 JSON을 기존 에디터 Toolset에 전달해 수행했으며, 이미 저장한 읽기 결과를 다시 입력하면 추가할 선택이 없음을 확인했다.

백업·전후 해시와 읽기 결과는 `Saved/BuildChecks/GreatSwordContent-20261026-133504/`에 있다. [ContentResult.json](../Saved/BuildChecks/GreatSwordContent-20261026-133504/ContentResult.json), `Before.json`/`After.json`과 `ChargeProperties-Readback.json`에 두 저장 성공, 저장 뒤 dirty=false, 기존 옵션/다른 필드 보존, 실제 float 값과 반복 적용 무변경을 기록했다. Python 구문·문서 링크·diff 확인을 수행했으며 C++를 바꾸지 않아 새 컴파일을 반복하지 않았다.

실제 입력·충전/취소·투지 획득/소모·GAS 피해·저장 캐릭터 복원·네트워크 플레이는 이번에 실행하지 않았다. 차지의 7레벨 `Stagger`/`PartBreak`, 돌진·회전·도약의 저장 ID/효과, 무력화/부위 파괴 대상과 첫 성장 구간 콘텐츠는 남아 있다. 효과가 없는 선택을 ID만 붙여 복원 가능 상태로 만들지 않았다. 다음은 나머지 선택을 실제 소비 경로와 함께 구현하는 것이다.

### 2026-10-26 범위 배율의 실제 HitQuery 연결

`Shared.Stat.Radius`는 등록되어 있고 선택별 배율을 RuntimeSpec에 합성하지만 기존 HitQuery는 원래 형상만 사용하고 있었다. `FRPGSkillRuntimeSpec::TryGetHitQueryProfile`에서 시전 시 확정한 배율과 프로필을 해석하고, 컨테이너의 `ExecuteActiveSkillHitQuery`가 이 결과를 사용하도록 연결했다. 기존 안전 피해 노드와 정책 몽타주 피해도 이 공통 판정을 사용한다. 새 피해 호출이나 클라이언트 범위 RPC는 추가하지 않았다.

- Sphere는 반경, Capsule은 반경·높이를 같은 배율로 변경한다.
- Sector/RingSector는 바깥/안쪽 반경만, Box는 XY 반길이만 변경한다. 공격 높이·각도·오프셋은 유지한다.
- LineSweep은 반경만 변경하고 선분 길이는 유지한다.
- 배율이 없거나 1이면 기존 형상을 유지한다. 0 이하·비유한 배율·잘못된 형상·보안 거리 한도를 넘는 확대는 거절한다. 정의의 데이터 검증에서도 0 이하의 반경 배율을 거절한다.

판정은 공유 Definition이나 시전 중 선택을 변경하지 않고 RuntimeSpec의 고정된 값에서 계산한다. 조준/이동 거리, 대상 필터·최대 대상/적중 수와 기존 서버 거리·최종 피해 상한은 늘리지 않는다. BP의 `GetActiveSkillHitQueryProfile`은 실제 쿼리와 같은 프로필을 반환하며 true일 때만 출력값을 사용한다. 비활성 시전이나 잘못된 설정에서는 false다.

#### 도약의 범위 선택 작성 준비

[ConfigureGreatSwordJumpAreaTripods.py](../Scripts/Editor/ConfigureGreatSwordJumpAreaTripods.py)에 도약의 4레벨 `WideArea`(×1.2)·7레벨 `ExtraArea`(×1.1) 작성 규칙을 추가했다. 현재 도약의 기본 반경 450에서 `WideArea`만 선택하면 540, `ExtraArea`만 선택하면 495, 두 선택 조합은 배율 곱셈으로 594가 된다. 기존 데이터·티어/옵션 인덱스를 보존하고 같은 ID의 다른 설정은 덮어쓰지 않는다. 이동·피해·정책·몽타주·기본 반경과 보안 한도는 변경하지 않는다.

이 소스 연결 단계에서 읽은 도약/회전 형상은 반경 450/375, 높이 250의 360도 Sector이며 두 스킬의 티어 배열은 비어 있었다. 회전도 공통 범위 처리 경로를 사용하지만 작성 준비는 도약 두 선택에 한정했다. 무력화·부위 파괴 기여를 받는 시스템은 아직 없어 해당 선택은 효과 없이 ID만 만들지 않았다.

스크립트는 기본적으로 읽기만 하며 `-RPGConfigureGreatSwordJumpAreaTripods`로 적용할 때 실행 중 모듈에 새 `GetActiveSkillHitQueryProfile` API가 있어야 저장한다. 소스 연결 당시 에디터에서는 기존 `ExecuteActiveSkillHitQuery` 함수가 조회되고 새 API는 조회되지 않았다. 따라서 **이 소스 연결 단계에서는 도약 에셋을 저장하지 않았다.** 실제 저장은 아래 후속에서 완료했다. 공유 에디터의 재시작·핫리로드는 수행하지 않았다.

확인 범위는 [Compile.log](../Saved/BuildChecks/SkillRadius-20261026/Compile.log)의 UHT와 관련 CPP 3개 `-FileList -NoLink` 성공, Python 구문 검사와 실제 도약 데이터로 작성 요청 생성까지다. 시전 기록 준비 전의 BP 조회 방어를 포함한 [최종 증분 컴파일](../Saved/BuildChecks/SkillRadius-20261026/Compile-Final.log)도 성공했다. 정상 DLL 링크·반영이나 자동/PIE/네트워크 테스트는 아니다. 준비 JSON·실제 모듈 기능 조회와 한계는 [ImplementationResult.json](../Saved/BuildChecks/SkillRadius-20261026/ImplementationResult.json)에 기록했다.

이 단계의 후속은 새 모듈을 반영한 뒤 도약 범위 선택을 저장하고, 돌진·회전 변형과 무력화/부위 파괴의 기여 수신 경로를 구현하는 것이었다. 도약 저장 완료와 남은 범위는 다음 기록을 따른다.

### 2026-10-26 도약 범위 선택 실제 저장

`DA_Skill_JumpSmash`에 준비한 두 선택을 실제 저장했다. 원래 비어 있던 티어 배열에 4레벨·7레벨 티어를 추가했으며 각 옵션 인덱스는 0이다. 저장 ID는 backend의 기존 허용 ID와 일치한다.

| 해금 레벨 | 저장 ID | 반경 배율 | 해당 선택만 적용한 반경 |
| --- | --- | --- | --- |
| 4 | `WideArea` | ×1.2 | 540 |
| 7 | `ExtraArea` | ×1.1 | 495 |

두 선택의 조합은 ×1.32로 기본 반경 450에서 약 594다. 기본 반경 450·높이 250·360도 Sector, 피해 테이블·쿨타임·조준/이동 거리·몽타주·실행/타기팅 정책·보안 한도 등 나머지 19개 속성을 저장 전후 비교해 보존했음을 확인했다. BP나 개발 프로필·기존 차지/클래스 에셋은 이번 저장 대상이 아니다.

공유 작업의 정상 Editor 빌드 성공을 읽기로 확인한 뒤, 현재 DLL을 사용하는 UE `PythonScript` commandlet에서 기존 작성 스크립트의 적용 모드를 한 번 실행했다. 로드된 네이티브 클래스가 새 `GetActiveSkillHitQueryProfile` API를 제공한다는 보호 조건을 통과했다. 공유 GUI의 MCP 서버가 비활성 상태여서 GUI 접속·설정 변경 없이 이 기존 에셋 작성 경로를 사용했으며 공유 에디터의 종료/재시작·핫리로드는 하지 않았다. 별도의 테스트 환경이나 PIE는 만들지 않았다.

[Apply.log](../Saved/BuildChecks/JumpAreaContent-20261026-141437/Apply.log)에 해당 패키지 저장·실제 uasset 이동과 Python 정상 종료가 기록돼 있다. [ContentResult.json](../Saved/BuildChecks/JumpAreaContent-20261026-141437/ContentResult.json)은 저장 후 메모리 읽기 일치·관련 없는 속성 보존·동일 작성 함수의 반복 변경 없음·dirty=false를 기록한다. [저장 전 속성](../Saved/BuildChecks/JumpAreaContent-20261026-141437/Properties-Before.json)·[저장 후 속성](../Saved/BuildChecks/JumpAreaContent-20261026-141437/Properties-After.json)과 원본 uasset 백업, 전후 파일 크기/SHA-256도 같은 폴더에 보존했다. 파일은 4,091→6,789바이트로 변경됐다.

이 확인은 옵션 작성과 저장의 범위다. 이미 열린 GUI의 메모리 데이터 갱신, 실제 시전/피해·HTTP 구성 왕복·Pawn 복원·네트워크 복제는 검증하지 않았다. 두 ID는 이제 해금 레벨과 옵션으로 해석할 수 있지만 첫 성장 구간 전체의 플레이 가능 상태를 뜻하지 않는다. 다음 구현은 차지 7레벨·돌진/회전 선택과 도약의 남은 대안, 무력화/부위 파괴 기여 수신 경로다.

### 2026-10-26 회전 베기 7레벨 범위 선택 저장

`DA_Skill_WhirlWind`에 7레벨 `WideArea`를 실제 저장했다. 옵션은 `Shared.Stat.Radius` ×1.2를 사용하므로 기존 시전 스냅샷과 공통 HitQuery 경로에서 반경 375→약 450으로 해석된다. 기본 프로필은 반경 375·높이 250·360도 Sector를 유지하며 확대 반경은 기존 서버 거리 한도 1,100 안에 들어간다. 피해·높이·조준/이동 거리·대상/적중/피해 상한은 변경하지 않는다.

| 해금 레벨 | 티어 인덱스 | 현재 옵션 |
| --- | --- | --- |
| 4 | 0 | 비어 있음. 이동 방식 선택의 작성 자리 |
| 7 | 1 | 인덱스 0의 `WideArea` |

원래 티어 배열은 비어 있었다. 4레벨 행을 먼저 확보해 후속 선택 작성으로 기존 7레벨 티어 인덱스를 옮길 필요가 없도록 했다. 4레벨 행에는 선택 ID나 효과가 없는 옵션을 추가하지 않았다. 기존 backend 허용표의 `WhirlWind`·7레벨 `WideArea`와 일치하며 API/스키마 변경은 없다.

[ConfigureGreatSwordWhirlwindAreaTripod.py](../Scripts/Editor/ConfigureGreatSwordWhirlwindAreaTripod.py)는 기본적으로 읽기만 하고 `-RPGConfigureGreatSwordWhirlwindAreaTripod`로 적용한다. 잘못된 스킬/정책·해금 레벨·반경·보안 한도와 중복 티어/ID를 거절하고, 기존 배열 인덱스를 유지하며 같은 ID의 사용자 설정을 덮어쓰지 않는다. 저장 전에 실행 모듈의 `GetActiveSkillHitQueryProfile` API를 요구한다.

UE `PythonScript` commandlet에서 스크립트 적용·네이티브 API 로드·저장 후 메모리 읽기·반복 작성 무변경·dirty=false를 확인했다. 비교한 나머지 19개 속성은 동일하다. 현재 Holding 정책의 4초 지속·`Default`/`End` 섹션·기본 쿨타임 1초, 피해 테이블·몽타주와 전투 반응 설정을 보존했다. 첫 구간 설계의 전체 기본 수치 조정은 후속 범위다. C++ 수정/새 빌드나 공유 GUI 재시작·핫리로드·PIE는 수행하지 않았다.

증거는 [ContentResult.json](../Saved/BuildChecks/WhirlwindAreaContent-20261026-155415/ContentResult.json)과 [Apply.log](../Saved/BuildChecks/WhirlwindAreaContent-20261026-155415/Apply.log)다. 같은 폴더에 원본 uasset 백업, 전후 속성 JSON과 파일 크기/SHA-256을 보존했다. uasset은 4,155→5,938바이트로 변경됐다. 실제 시전/피해·저장 구성 HTTP 왕복·Pawn 복원·복제와 열린 GUI의 메모리 갱신은 검증하지 않았다.

이 저장 단계의 다음 작업은 회전 4레벨 `Mobile`/`Stationary`의 이동 제약과 피해 차이였다. 당시 이동 속도 제한 API는 양수 제한만 지원하고 스킬 정책에는 이동 상태 관리가 없었다. 아래 후속에서 소스와 작성 규칙을 연결했으며 실제 옵션 저장은 정상 모듈 반영 뒤에 남아 있다. 돌진의 거리·회복 선택과 무력화/부위 파괴 기여 수신·마지막 타격 구분도 남아 있다.

### 2026-10-26 회전 4레벨 이동 선택 런타임 연결

Holding 설정에 선택적인 `bConstrainMovement`와 `MovementSpeedScalar`를 추가했다. 기본값 false/1은 기존 스킬 동작을 유지한다. 활성화 전 설정·몽타주와 시전자/월드·RPG 이동 컴포넌트를 검사하며 0~1 범위 밖이나 비유한 속도 배율은 거절한다. 서버와 예측 소유자의 정책이 각각 같은 고정 설정을 사용한다.

| 선택 | 속도 설정 | 피해 배율 |
| --- | --- | --- |
| `Mobile` | 현재 가용 속도 ×0.8 | 기존 기준 ×1 |
| `Stationary` | 자발적 평면 이동·점프 입력 차단 | 기존 기준 ×(2.8/2.4), 약 16.7% 증가 |

이 배율은 기존 피해 테이블에 대한 상대 차이다. 설계의 절대 계수나 기본 2초 지속으로 테이블·몽타주를 바꾼 것은 아니다. 작성 규칙은 현재 4초 Holding·기본 1초 쿨타임과 7레벨 `WideArea`를 보존한다.

`URPGCharacterMovementComponent`가 시전별 GUID/약한 소유자 참조로 제약을 관리한다. 동시 제약은 가장 작은 배율을 사용하고 현재 속도 속성·슬로우·기존 속도 제한을 계속 합성한다. `MaxWalkSpeed`나 MovementMode를 저장했다 되돌리지 않으며, 종료/취소/몽타주 중단·GC에서는 획득한 이동 컴포넌트의 자기 핸들만 해제한다. 키 해제 뒤의 종료 섹션이 끝날 때까지 제약을 유지한다. 삭제된 소유자는 이동 틱에서 정리한다.

제약 중에는 몽타주의 루트 모션을 IgnoreRootMotion으로 추출해 입력 기반 이동을 허용한다. 컴포넌트는 원래 애니메이션 모드를 기억하고 마지막 제약 해제 때 자신이 설정한 모드가 유지된 경우 복원한다. 다른 값으로 바뀐 모드는 덮어쓰지 않는다. 중력·사망의 이동 모드·넉백/띄우기의 이동 컴포넌트 루트 모션 소스는 유지한다. 원격 simulated proxy의 일반 위치 복제가 애니메이션 루트 모션에 가로막히지 않도록 활성 여부 한 비트만 `COND_SimulatedOnly`/PushModel로 복제한다. 속도/이동 잠금은 서버와 예측 소유자의 정책에서 처리한다. 이 복제 경로의 실제 네트워크 동작은 아직 검증하지 않았다.

타기팅의 `bFollowSourceForHitQuery`는 현재 서버/로컬 시전자 위치 변화만큼 판정 중심을 옮기고 확정된 조준 방향·쿼리 오프셋을 유지한다. 기본 false이며 두 이동 선택의 타기팅 설정에서만 true로 작성한다. 컨테이너는 타기팅 정책의 `ResolveHitQueryTransform` 결과를 공통 HitQuery에 전달한다. 새 클라이언트 좌표/범위 RPC나 서버 피해 허용 거리 증가는 없다.

[ConfigureGreatSwordWhirlwindMovementTripods.py](../Scripts/Editor/ConfigureGreatSwordWhirlwindMovementTripods.py)는 비어 있는 기존 4레벨 행에 `Mobile`/`Stationary`를 추가하도록 준비했다. 기존 티어/옵션 인덱스와 같은 ID의 사용자 설정을 보존하며 기본 실행/타기팅 설정을 복사해 필요한 필드만 덮는다. 기본 실행은 조회이며 `-RPGConfigureGreatSwordWhirlwindMovementTripods` 적용 모드에서는 실행 중 모듈에 새 Holding 속도 필드와 판정 추적 필드가 모두 있어야 저장한다.

이 소스 연결 당시 정상 DLL에는 두 필드가 모두 없었다. **이 단계에서는 에셋을 저장하지 않았고 4레벨 행을 비워 뒀다.** 기존 7레벨 `WideArea`와 파일 SHA-256을 유지했으며 당시 작성 완료 옵션은 5개였다. 실제 저장은 아래 2026-10-27 후속에서 완료했다. 공유 에디터를 재시작/핫리로드하거나 별도 테스트 환경을 만들지 않았다.

[Compile.log](../Saved/BuildChecks/WhirlwindMovement-20261026-161424/Compile.log)의 UHT/관련 CPP 4개 `-FileList -NoLink`와 종료 콜백 재진입 방어를 포함한 [최종 증분 컴파일](../Saved/BuildChecks/WhirlwindMovement-20261026-161424/Compile-Final.log)이 성공했다. 실제 Definition 읽기·작성 JSON 생성과 모듈 기능 조회는 UE PythonScript commandlet에서 수행했다. [ImplementationResult.json](../Saved/BuildChecks/WhirlwindMovement-20261026-161424/ImplementationResult.json), `Properties-Prepared.json`, 전후 속성과 원본 에셋 백업을 같은 폴더에 보존했다. 정상 DLL 링크/새 기능 실행·PIE/피해/네트워크 테스트는 미실행이다.

이 단계에서 남았던 정상 모듈 반영·회전 4레벨 두 옵션 저장은 아래 후속에서 해소했다. 이후 돌진의 거리/회복 선택과 무력화·부위 파괴 기여 효과를 연결한다. 기본 1초 쿨타임과 돌진의 초 단위 쿨감 가설의 정합성도 별도로 정리한다.

### 2026-10-27 회전 4레벨 이동 선택 실제 저장

현재 정상 DLL에서 Holding의 `MovementSpeedScalar`와 타기팅의 `bFollowSourceForHitQuery`가 모두 로드되는 것을 확인하고 `DA_Skill_WhirlWind`에 `Mobile`/`Stationary`를 실제 저장했다. 기존 작성 스크립트의 적용 모드를 UE PythonScript commandlet에서 실행했다. 이전 작업 이후 정상 DLL이 이미 갱신돼 있어 이번에는 C++ 수정/새 빌드나 공유 GUI 종료·재시작·핫리로드가 필요하지 않았다. 모듈 파일의 크기·시각·SHA-256은 증거 폴더의 `Module.json`에 기록했다.

| 4레벨 옵션 인덱스 | 저장 ID | 속도 배율 | 상대 피해 배율 |
| --- | --- | --- | --- |
| 0 | `Mobile` | 현재 가용 속도 ×0.8 | ×1 |
| 1 | `Stationary` | 자발적 이동 차단(×0) | ×(2.8/2.4), 약 16.7% 증가 |

두 선택은 이동 제약과 시전자를 따라가는 판정 중심을 켠 실행/타기팅 설정을 저장한다. 기존 4레벨 티어 인덱스 0과 7레벨 티어 인덱스 1, 7레벨 `WideArea`의 옵션 인덱스 0을 유지했다. 기본 Holding 4초·쿨타임 1초·반경 375, 기본 정책/몽타주·피해 테이블·보안 한도와 Slow/Burn/Stagger/ActivationImmunities를 포함한 나머지 23개 속성이 전후 일치했다. 기본 설정은 계속 이동 제약/판정 중심 추적이 꺼져 있으며 선택 시의 override에만 적용한다.

[ContentResult.json](../Saved/BuildChecks/WhirlwindMovementContent-20261027-011813/ContentResult.json)과 [Apply.log](../Saved/BuildChecks/WhirlwindMovementContent-20261027-011813/Apply.log)에 저장·읽기 일치·기존 인덱스 보존·반복 작성 무변경·dirty=false를 기록했다. 별도 프로세스에서 같은 uasset을 읽기만 하며 다시 로드해 24개 속성 전체 일치와 추가 작성 불필요를 확인했다([ReloadResult.json](../Saved/BuildChecks/WhirlwindMovementContent-20261027-011813/ReloadResult.json), `Reload.log`). 원본 백업·전후/재로드 속성과 파일 SHA-256도 보존했다. uasset은 5,938→8,545바이트로 변경됐다.

첫 구간 설계의 선택 16개 중 실제 에셋 작성 완료는 7개, 남은 선택은 9개다. 남은 것은 돌진 4개, 차지 7레벨 2개, 도약의 부위 파괴/무력화 2개, 회전의 마지막 타격 무력화 1개다. 이번 확인은 작성·저장·재로드 범위이며 실제 이동/종료/취소·피해·새 선택의 HTTP/Pawn 복원·멀티플레이 동작은 검증하지 않았다. 다음 구현은 돌진의 거리/회복 선택과 남은 기여 효과 연결이다.

### 2026-10-27 돌진 거리 선택 연결과 실제 저장

`DA_Skill_AssultBlade`에 4레벨 `LongReach`와 7레벨 `ExtraReach`를 실제 저장했다. 기존 4레벨 차지/콤보 옵션 인덱스 0/1과 내용을 보존하고 `LongReach`를 인덱스 2에 추가했다. 새 7레벨 티어 인덱스 1의 옵션 0은 `ExtraReach`다. backend의 기존 저장 ID와 해금 레벨을 사용하며 API/스키마 변경은 없다.

| 선택 | 몽타주 루트 이동 배율 |
| --- | --- |
| `LongReach` | ×1.2 |
| `ExtraReach` | ×1.1 |
| 두 선택 조합 | ×1.32 |

읽기 조사에서 돌진 BP의 활성화/종료 이벤트는 연결되지 않았고 기본 실행은 Instant/`AssultBlade` 섹션이었다. 기존 몽타주의 두 원본 시퀀스 모두 루트 모션이 활성화돼 있었다. 첫 시퀀스의 원본 평면 순변위는 약 132.954cm이고, 두 번째는 약 147.227cm/샘플 경로 길이 약 194.093cm였다. 이는 애니메이션 원본 조사이며 실제 캐릭터의 이동 거리 검증이 아니다. 메시 배율·충돌·블렌드·재생 섹션에 따른 실제 결과는 플레이에서 확인해야 한다. [RootTranslation.json](../Saved/BuildChecks/AssaultDistance-20261027-013019/RootTranslation.json)과 그래프/몽타주 조사 JSON을 보존했다.

`Shared.Stat.MovementDistance`를 시전 시 확정한 RuntimeSpec에서 합성하고 `TryGetMontageMovementScale`로 해석한다. 잘못된 배율과 이동 허용 설정을 활성화 전에 거절한다. 배율 1은 기존 GAS 몽타주 작업을 그대로 사용하고, 바뀐 배율은 새 `RPGAbilityTask_PlayScaledSkillMontageAndWait`에 전달한다. 이동은 기존 몽타주 루트 모션과 CharacterMovement의 충돌 처리를 사용한다. 조준 거리·판정 반경·피해·쿨타임·몽타주/BP는 변경하지 않았다. 두 선택은 기존 SoftTarget 설정을 복사해 시전자를 따라가는 판정 중심만 켠다.

선택의 이동을 위해 `SecurityProfile.AuthorizedMovement`를 활성화하고 기존 추가 거리 예산 600cm·reason·baseline 미초기화를 유지했으며 유효 시간을 상한 10초로 작성했다. 이 값은 이동 검사 예산이며 실제 이동 명령이나 캐릭터 이동 거리 상한이 아니다. 기본/레거시 선택에는 새 이동 배율이 없어 이 추가 작업이 시작되지 않는다. 바뀐 배율의 작업만 서버의 유한한 권한을 획득하고, 기존 이동 권한이 활성 상태라면 덮어쓰지 않는다. 입력·배율·예산을 받는 새 클라이언트 RPC는 없다.

작업은 최초 시전자·활성화 일련번호·보안 컴포넌트와 권한 핸들을 캡처한다. 늦게 도착한 이전 시전 작업은 새 시전의 취소 콜백을 호출하지 않는다. GAS의 강제 정지 경로는 blend-out delegate를 해제하므로, OnDestroy에서도 같은 시전자/시전/애니메이팅 Ability가 소유한 배율인 경우 기본 1로 돌린다. 다른 시전의 배율과 새 이동 권한을 이전 작업이 해제하지 않도록 검사한다. 정상 종료·취소·아바타 교체의 실제 실행 검증은 아직 하지 않았다.

[ConfigureGreatSwordAssaultDistanceTripods.py](../Scripts/Editor/ConfigureGreatSwordAssaultDistanceTripods.py)는 기본 조회, `-RPGConfigureGreatSwordAssaultDistanceTripods` 적용 모드다. 정상 모듈의 새 작업 클래스를 확인하고, 사용자 설정과 기존 인덱스를 보존한다. 작성 도구의 ArrayAdd 모호성을 피하기 위해 기존 티어 수정과 새 티어 추가를 나누고, 추가 단계의 기존 항목은 엔진에서 읽은 정확한 값(부동소수점 포함)을 사용한다. 초기 두 시도는 저장 전에 중단됐고 최종 작성에서만 uasset을 저장했다.

[Compile.log](../Saved/BuildChecks/AssaultDistance-20261027-013019/Compile.log)·`Compile-Final.log`의 UHT/선택 CPP 5개와 [정상 Editor 빌드](../Saved/BuildChecks/AssaultDistance-20261027-013019/Editor-Build.log), 소유권 검사까지 포함한 [최종 Editor 증분 빌드](../Saved/BuildChecks/AssaultDistance-20261027-013019/Editor-Final.log)가 성공했다. 새 DLL에서 저장 파일을 읽기 전용으로 재로드해 속성 일치·반복 작성 무변경·dirty=false를 확인했다([ReloadResult.json](../Saved/BuildChecks/AssaultDistance-20261027-013019/ReloadResult.json), `Reload-Final.log`). [ImplementationResult.json](../Saved/BuildChecks/AssaultDistance-20261027-013019/ImplementationResult.json)에 저장 결과·기존/다른 속성 22개와 적중 보안 한도 보존을 기록했다. 전후 파일 크기는 7,635→10,126바이트이며 원본 백업·해시·전후/재로드 JSON을 보존했다. 공유 에디터 종료/핫리로드나 자동/PIE/피해/네트워크 테스트는 이 작업에서 수행하지 않았다.

실제 작성 완료는 9/16개다. 남은 7개는 돌진 쿨감 관련 2개, 차지 기여 2개, 도약 기여 2개, 회전 마지막 타격 기여 1개다. `QuickApproach`/`ExtraRecovery`의 초 단위 쿨감은 현재 기본/최소 1초와 정합성을 정리해야 하므로 작성하지 않았다. 다음은 남은 기여 효과 연결과 쿨감 선택의 설계 정리다.

### 2026-10-27 저장 구성의 실제 적중과 투지 해방 연결

공용 무기 Actor의 미지 종류 때문에 투지 획득/해방이 막히던 경로를 수정했다.
`UPawnCombatComponent::GetCharacterCurrentEquippedWeaponType()`가 기존 장비 스냅샷과 현재
Actor를 함께 읽고, 일반 스킬과 대검 고유 능력이 이를 사용한다. 소유권이나 현재 장비
상태를 추가로 저장하지 않는다. `ServerOnly` 능력도 GAS가 클라이언트에서 사용 가능
여부를 먼저 검사하므로, 생존/사용 자격 조회의 서버 권한 요구를 제거했다. 실제 능력
실행·게이지 변경·강화 기회 소비는 명시적인 서버 권한 검사로 보호한다.

저장한 FullCharge/QuickCharge의 실제 티켓·HTTP 복원 뒤 작성된 몽타주/서버 피해 GE와
투지 5회 획득→100 소비→다음 차지 강화를 각각 실행했다. 정지 자세/복제 표적 fixture의
피해는 571→714와 464→580이며 취소·쿨타임 재입력/만료·아군/뒤쪽/범위 밖 제외와 소유
클라이언트의 체력/쿨타임/투지 수신도 확인했다. 마나 0·기본 쿨타임 1초와 기존 에셋은
유지했다. 이는 남은 트라이포드나 자연 전투/실물 입력 검증의 완료를 뜻하지 않는다.
집중 자동화·정상 빌드/별도 검증 DLL과 두 실행의 상세 근거/한계는
[성장 API 6.2](../Backend/CHARACTER_PROGRESSION_API.md#62-저장한-두-차지-구성의-적중쿨타임투지-순환--2026-10-27)에 모았다.

### 2026-10-27 대검 첫 전투 쿨타임과 돌진 회복 선택

[첫 성장 구간 3.2](PLAYER_PROGRESSION_FIRST_SLICE_V01.md#32-스킬과-트라이포드-가설)의 쿨타임을 네 실제 Definition에 반영했다. 이는 첫 플레이 조정을 위한 기준이며 출시 밸런스의 확정이 아니다.

| 실제 스킬 | 기본 쿨타임 | 마나 비용 |
|---|---:|---:|
| 돌진 베기 | 8초 | 0 |
| 도약 강타 | 10초 | 0 |
| 차지 슬래시 | 12초 | 0 |
| 회전 베기 | 10초 | 0 |

기존 BP 네 개는 네이티브 생성자의 설정과 달리 `bUseLegacyManualManaCost=true`를 저장하고 있었다. 테이블의 돌진/도약/회전 비용은 20/40/30, 차지는 0이었다. 따라서 단순히 표시만 고친 것으로 보지 않는다. `DT_Skill`의 해당 비용을 모두 0으로 맞춰 기존 수동 경로에서도 이 구간의 무마나 기준을 사용한다. BP·다른 행·피해·고유 자원 증가량·아이콘·툴팁은 유지했다. 양수 마나 비용의 새 GAS 처리나 모든 직업의 자원 체계를 구현한 것은 아니다.

돌진의 기존 티어/옵션을 유지하며 4레벨 `QuickApproach`(루트 이동 ×0.8·2초 감소)와 7레벨 `ExtraRecovery`(1초 감소)를 덧붙였다. 기존 `LongReach`/`ExtraReach`와의 네 조합은 8/7/6/5초, 이동 배율은 1.32/1.2/0.88/0.8이다. backend와 로비에 이미 있던 안정 ID를 사용한다. 이로써 첫 구간 선택 작성은 **11/16개**이며 차지 기여 2개·도약 기여 2개·회전 마지막 타격 기여 1개는 남아 있다.

공통 `FRPGSkillTripodOption::CooldownReductionSeconds`를 선택한 해금 티어에서만 합산해 시전별 `RuntimeSpec`에 복사한다. 계산은 `max(최소 1초, max(0, BaseCooldown - 감소 초 합) × Shared.Stat.Cooldown)`이다. 기존 배율의 곱셈 의미는 유지하며 초 단위 감소를 곱셈 배율로 흉내 내지 않는다. 음수/비유한 값은 편집 검증에서 거절하고 런타임 감소로 적용하지 않는다. 공유 Definition·플레이어 구성에 시전 중 실행 값을 되쓰지 않으며 새 상태 복제나 직업별 분기를 추가하지 않았다. 기존 성공 commit 시점과 취소 시 쿨타임 유지·효과 소유권도 그대로다.

[작성 도구](../Scripts/Editor/ConfigureGreatSwordCombatBaseline.py)는 기본 조회, `-RPGConfigureGreatSwordCombatBaseline` 적용 모드다. 네 Definition/Ability와 테이블을 모두 사전 검사하고 변경 전 5개 파일을 바이트 그대로 백업했다. 변경 대상은 기본 쿨타임·돌진의 새 선택·관련 마나 비용이며, 나머지 속성과 기존 옵션 인덱스·테이블 값을 저장 전후 비교한다. 정상 모듈이 새 필드를 제공해야 저장할 수 있다. 기존 선택 작성 도구 5개도 새 필드의 기본 0을 비교하도록 보완했다.

검증 근거는 `Saved/BuildChecks/GreatSwordCombatBaseline-20261027/`와 `Saved/ContentChecks/GreatSwordCombatBaseline/`이다.

- `Editor-Build.log`: 정상 Editor **267개 작업**, UHT·C++·게임/Editor DLL 링크·타깃 성공. 검증 모듈은 `Build.proof.json`에 기록했다.
- `Applied.json`/`Apply.log`: Definition 4개·테이블 1개 저장, 관련 없는 속성/행 보존, 반복 구성 일치와 dirty=false. 새 프로세스의 `ReadOnly.json`/`Reload.log`에서 추가 변경 0·저장 0·종료 0이다.
- `Native.log`/`Native.proof.json`: 집중 자동화 **5개 Success**, 종료 0, 빌드 모듈 일치. 실제 콘텐츠의 4개 쿨타임/0 비용과 돌진 4조합, 해금 조건·초 합산/배율/최소값·스냅샷 불변성·잘못된 감소 값 거절, 슬롯 입력의 5초 GE·취소 후 유지/1.1초 후 거절/만료 후 재사용·다음 시전만 새 값 적용을 확인했다. 기존 Definition/legacy 효과 분리 회귀도 포함한다. 기존 기동 `Condition failed` Error 15줄은 테스트 탐색 이전이며 이후 Error는 0이다.
- `Saved/Tests/CharacterProgressionRestore/665f9398c9fb47ba88dd06acec348d41/Verification.proof.json`: 개발 인증/Memory·실제 티켓/HTTP·bossmap/Iris/nullRHI에서 저장한 4레벨 FullCharge를 복원해 새 **12초 쿨타임**·서버 GE 수신·조기 취소 무적중/재입력 거절/만료, 실제 피해 571×5+714=3,569와 투지 100 획득/100 소비/다음 적중 후 20을 확인했다. 아군/뒤쪽/범위 밖은 무피해, 마나/원본 성장 구성/다른 캐릭터는 무변경이다. 같은 정상 빌드 모듈(`8EE8703F…7CD0D63`)과 선택 입력 48개가 전후 일치했고 서버/클라이언트/러너 종료 0·실제 실행 Error 0이다. `Combat.proof.json`과 이 작업의 `ImplementationResult.json`에 연결했다. 기동 중에만 fixture 할당 heartbeat 10회를 보냈고 운영 TTL이나 승인 정책을 바꾸지 않았다. 이 실행은 정지 자세의 복제 표적이며 자연 승리가 아니다. QuickCharge의 이전 1초 검증은 역사적 근거로 유지하며 새 기준으로 두 구성을 모두 재실행했다고 주장하지 않는다.

돌진 두 새 선택의 HTTP 복원/네트워크 이동·자연 전투와 실제 HUD/물리 입력 체감은 별도다. 피해 계수 전체·회전 2초 목표를 변경하지 않았고, 재접속/재도전의 자원·쿨타임 규칙을 새로 구현하지 않았다. 다음은 남은 기여 선택과 실제 전투에서 두 구성의 기회·운용 차이를 확인하는 것이다.

### 2026-10-27 차지와 도약의 기여 선택 및 공통 수신기

기존 `FRPGStaggerSettings`는 짧은 피격 경직의 시간 설정이다. 이번 누적 무력화/부위 파괴 단위는 별도 `FRPGCombatContributionAmounts`로 정의했다. Definition의 기본값과 선택한 `Shared.Stat.StaggerContribution`/`Shared.Stat.PartBreakContribution` 배율을 시전별 RuntimeSpec에서 확정한다. 피해·치명타·투지의 피해 배율은 기여 배율에 섞지 않는다. 음수·비유한 값·지원 상한 초과는 거절하며 초기값 0은 기존 스킬 동작을 유지한다.

| 실제 작성한 선택 | 해금 | 기본 기여 → 선택 후 |
|---|---:|---|
| 차지 `Stagger` | 7레벨 | 무력화 20 → 25, 파괴 20 유지 |
| 차지 `PartBreak` | 7레벨 | 파괴 20 → 25, 무력화 20 유지 |
| 도약 `PartBreak` | 4레벨 | 파괴 20 → 25, 무력화 20 유지 |
| 도약 `Stagger` | 7레벨 | 무력화 20 → 25, 파괴 20 유지 |

두 스킬은 1레벨부터 각각 기본 20단위로 참여하며 잠긴 선택은 강화하지 않는다. 기존 backend/로비의 저장 ID와 해금 레벨을 사용하고, 레벨·SP·부여·8슬롯의 원본은 변경하지 않았다. 첫 구간 선택 작성은 **15/16개**다. 회전의 `FinalStagger`는 실제 마지막 승인 타격을 구분하는 후속이 필요하므로 효과 없는 ID만 작성하지 않았다.

`URPGCombatContributionComponent`는 대상 소유의 선택적 수신기다. 일반 NPC의 기본 설정은 비활성이고 기존 `BP_Dummy_Gdn`에만 두 기준 100단위를 작성했다. 20/25단위와 100단위는 연결 검증용 임시값이며 기본 5회/강화 4회를 비교한다. 완료까지 누적한 값·기준·완료 여부·세대/리비전을 하나의 상태로 복제하고 읽기 전용 조회와 변경 이벤트를 제공한다. 현재 기능은 **게이지/완료 상태**이며 보스의 강제 중단·짧은 경직 연장·파괴 메시·방어력 감소·후속 공격 패턴이나 HUD를 구현한 것은 아니다. 보스 AI/기존 면역·경직 규칙을 변경하지 않았다.

기여 추가는 공통 컨테이너의 비공개 네이티브 승인 경로만 호출한다. 기존 일반/보스 피해 승인 성공 뒤에도 같은 활성 Ability·시전 번호·시전자/대상의 생존·ASC·제어 수명을 다시 대조한다. 수신기와 수신 세대도 피해 콜백 **이전**에 캡처해, 콜백 중 재사용/초기화된 새 대상에 이전 적중이 넘어가지 않게 한다. Blueprint/RPC로 대상·기여 수치를 제출하는 경로는 없다. 같은 Ability/시전은 대상마다 한 번만 기여하며 다른 파티원과 새 시전은 합산한다. 완료 값은 기준에서 포화되고, 유효한 예약을 지워 같은 시전을 다시 받지 않는다. 수신기의 명시적 초기화와 공통 CombatControl의 ASC 연결 교체·사망·아바타 재사용에서 자체 상태만 회수한다. 실행 중 임의 편성/클래스 갱신으로 다른 대상이나 Ability의 상태를 초기화하지 않는다.

[작성 도구](../Scripts/Editor/ConfigureGreatSwordContributionTripods.py)는 기본 조회이며 `-RPGConfigureGreatSwordContributionTripods`일 때만 저장한다. 네이티브 타입·두 스킬·훈련 표적의 자기 소유 수신기를 모두 먼저 확인하고 3개 원본을 백업한다. Definition의 기존 옵션/인덱스와 피해·쿨타임·타기팅·보안 등 다른 23개 속성을 보존하고, 훈련 표적은 수신 프로필만 작성한다. 증거는 `Saved/ContentChecks/GreatSwordContributions/Applied.json`·`ReadOnly.json`이며 3개 저장, 새 프로세스 재조회 변경/저장 0과 dirty=false·종료 0이다.

검증 근거는 `Saved/BuildChecks/GreatSwordContributions-20261027/ImplementationResult.json`에 연결했다.

- `Editor-Build.log`: 다른 빌드는 종료하지 않고 잠금을 기다린 뒤 정상 Editor 타깃 **18개 작업**, 관련 C++·게임/Editor 및 관련 모듈 DLL 링크·타깃 성공을 확인했다. 대기 중이던 자기 Build.bat 래퍼만 종료하고 같은 엔진의 정상 UBT `-WaitMutex` 진입점을 사용했다. 공유 빌드에서 발견한 복제 매크로 인자 이름 오류는 수정했고 최종 결과는 종료 0이다.
- `Native.log`/`Native.proof.json`: 새 집중 검사 3개 + 기존 승인 피해 15개 + 짧은 경직 6개, **24개 Success**·종료 0이다. 실제 네 선택/해금/스냅샷, 서버 쿼리와 악성 이벤트 payload 무시, 다단히트 중복 방지·두 source 합산·포화·클라이언트 초기화 거절, 피해 콜백 중 수신기 재사용/시전 종료·잘못된 값·사망을 확인했다. 기존 기동 `Condition failed` Error 15줄은 테스트 탐색 이전이며 이후 Error는 0이다.
- `Saved/Tests/CharacterProgressionRestore/a4d5ec76f7a140deb4f9ab7f966dd9b5/Verification.proof.json`: 개발 인증/Memory·실제 티켓/HTTP·bossmap/Iris/nullRHI의 저장 4레벨 FullCharge를 기존 슬롯 입력으로 시전했다. 취소 1회는 피해/기여 0, 승인 타격 6회에서 대상의 두 누적값 20→40→60→80→100/완료와 상한 유지를 서버·소유 클라이언트에서 확인했다. 아군/뒤쪽/범위 밖 3개는 피해/기여 0이다. 12초 쿨타임·피해 571→714·투지 100 소비·마나/저장본/다른 캐릭터 무변경도 유지됐다. 같은 정상 모듈(`E563DE70…5855C58`)과 선택 입력 61개가 전후 일치했고, 실제 실행 Error 0·서버/클라이언트/러너 종료 0이다. 기동 fixture heartbeat 4회이며 운영 승인/TTL 정책은 그대로다.

실제 네트워크 실행은 정지 자세의 복제 표적과 기본 기여를 확인한 것이다. 네 강화 선택의 실제 HTTP 저장/복원·전투 입력 비교, 훈련 표적의 화면/자연 타격, 2~4인 네트워크 기여 합산과 보스 기회/파괴 반응·최종 타격·HUD·Steam/PostgreSQL/물리 입력/재접속은 별도다. 다음은 회전의 실제 마지막 타격 구분과 수신 완료를 실제 전투 기회·표시에 연결하는 것이다.

검증 종료 뒤 선택 입력 중 스킬 관련 소스 6개에 `ContributionHitSection`/최종 타격 후속 코드가 추가됐다. 위 실행 중 입력 61개는 변하지 않았고 빌드/native/통합 실행의 DLL은 모두 `E563DE70…5855C58`로 같았다. 결과를 취합할 때도 DLL은 같았으나 이후 공유 소스는 별도 빌드/실행 전 상태다. 이번 증거를 후속 최종 타격 코드의 검증으로 확대하지 않는다. 변경 목록과 취합 시점은 `ImplementationResult.json`에 기록했다.

### 2026-10-27 회전 종료 타격 기여 소스 연결과 작성 준비

`AM_PlayerToggleSkill`을 읽어 조사한 결과 반복 구간에 AOE 이벤트가 하나 있었고, 마지막 End 애니메이션에는 피해 이벤트가 없었다. 마지막 타격을 관찰할 수 있도록 네이티브 `RPGAnimNotify_SkillFinisher`와 작성 도구를 추가했다. **이 단계에서는 Definition과 몽타주를 저장하지 않았다.** 실제 선택 작성은 위 후속을 포함한 15/16개이며, 마지막 `FinalStagger`는 소스/작성안 준비 상태다.

| 준비한 데이터 | 적용할 동작 |
|---|---|
| `ContributionHitSection = End` | 반복 적중은 기여를 예약하지 않고 실제 종료 타격만 대상마다 한 번 기여 |
| 기본 무력화 20 / 파괴 0 | 1레벨과 범위 선택도 종료 타격으로 무력화에 참여 |
| 7레벨 `FinalStagger`, 무력화 배율 ×1.25 | 종료 타격의 무력화 20→25. 피해나 짧은 경직 시간의 배율이 아님 |
| End 구간의 몽타주 시각 1.0초에 네이티브 타격 notify 하나 | 기존 반복 타격을 유지하고 종료 타격을 한 번 추가. 현재의 시전별 피해·타기팅·보안 한도 경로를 사용 |

종료 타격은 기존 타격과 같은 기준 피해를 사용하므로 전체 적중 피해가 한 타격만큼 늘어나는 작성안이다. 기존 반복 피해를 재분배하거나 전체 계수 2.4를 검증한 결과가 아니다. 기본 4초 Holding·10초 쿨타임·0 마나·이동/제자리·범위 선택과 기존 몽타주의 애니메이션/notify를 보존하도록 구성했다. 현행 Holding은 성공 구간에서 키를 놓으면 End로 넘어가므로, 4초를 모두 유지해야만 기여하는 규칙은 아니다. 종료 타격 전에 취소되거나 해당 타격이 빗나가면 피해/기여를 추가하지 않는다.

`ContributionHitSection`은 Definition에서 RuntimeSpec으로 복사하고 Reset에서 초기화한다. 활성화 전에 지원 정책·실제 몽타주 구간·해당 구간의 네이티브 notify 정확히 하나를 검사한다. Holding은 성공적으로 End에 진입한 뒤만 종료 타격을 허용하며 실패·취소·EndExecution은 허용을 회수한다. 일반 이벤트나 공개 BP 피해 함수로는 종료 기여를 제출할 수 없다.

notify는 서버의 branching point에서 전달된 몽타주 인스턴스 번호를 사용한다. 컨테이너가 재생 시작 때 캡처한 애니메이션 인스턴스/몽타주 번호·자기 ASC/메시·성공 종료 상태와 비교하고, 피해 콜백 전에 한 번의 종료 타격 시도를 예약한다. GAS의 기본 blend-out은 animating ability를 먼저 해제하고 일반 active-montage 조회도 종료 처리하므로, 종료 타격을 작성한 시전만 `bAllowInterruptAfterBlendOut`을 켜고 정확한 인스턴스를 조회한다. 일반/이동 배율 몽타주 작업에 같은 규칙을 적용했다. 이전 몽타주·새 애니메이션 인스턴스·다른 Ability가 종료 타격을 대신 승인하지 않는다. 기존 승인 피해/시전 번호/대상 수신 세대 확인과 중복 방지를 계속 사용하며, 반복/0 기여는 수신기의 시전 예약을 소비하지 않는다.

[작성 도구](../Scripts/Editor/ConfigureGreatSwordWhirlwindFinisher.py)는 기본 조회이며 `-RPGConfigureGreatSwordWhirlwindFinisher`에서만 적용한다. 실제 두 파일을 먼저 백업하고, 기존 선택 인덱스·기존 notify·피해/이동/쿨타임/보안 등 다른 속성을 비교한다. Definition의 네이티브 편집 검증을 거친 뒤, Definition 설정 없이는 동작하지 않는 notify부터 저장하도록 준비했다. 아직 적용 모드는 실행하지 않았으므로 이 절을 실제 저장/재로드 성공으로 보지 않는다.

근거는 [ImplementationResult.json](../Saved/BuildChecks/WhirlwindFinalContribution-20261027-233753/ImplementationResult.json)이다.

- `Compile.log`: UHT와 변경 CPP 5개 `-FileList -NoLink`, 종료 0.
- `Compile-Final.log`: blend-out/정확한 몽타주 인스턴스 보완 뒤 UHT와 영향 CPP 4개 증분 `-NoLink`, 종료 0. 실행 중 Live Coding 세션 때문에 UBT가 개별 컴파일 출력을 임시 위치에 두었으며 링크·패치·핫리로드를 수행한 것은 아니다.
- `Prepare.log`: 기존 정상 DLL에서 실제 콘텐츠를 읽어 작성안을 생성, 종료 0/`Supported=False`/`NoAssetsSaved=1`. [Prepared.json](../Saved/ContentChecks/GreatSwordWhirlwindFinisher/Prepared.json)에 기존 배열을 보존한 변경안을 기록했다. Python 구문과 diff 공백 검사를 통과했다.
- `Inspection.json`: 원래 몽타주의 애니메이션 구간과 notify 읽기 결과. 별도 `Inspect-Sections.log`의 비공개 섹션 속성 조회는 Python 오류로 종료했으며 저장하지 않았다. 이 실패를 성공 근거로 세지 않는다.

현재 정상 DLL에 새 타입/필드가 없고 공유 에디터가 사용 중이라 새 링크·에셋 저장·실제 종료/취소/헛침/피해/기여/HTTP/Pawn 복원/복제를 실행하지 않았다. 다른 에디터·검사 프로세스의 종료나 재시작은 하지 않았다. 다음 구현 단계는 예정된 정상 모듈 반영 뒤 작성 도구 적용과 파일 재로드이며, 보스의 실제 무력화/파괴 반응·표시와 첫 성장 구간의 콘텐츠/장비/보상 연결은 별도로 남아 있다.

### 2026-10-28 회전 종료 타격 선택 실제 저장

사용자의 다른 에이전트 작업 보존 지시에 따라 이번 변경을 `DA_Skill_WhirlWind`와 `AM_PlayerToggleSkill` 두 에셋 및 이 작업의 진행 기록으로 제한했다. 다른 작업의 2026-10-28 [정상 전체 빌드](../Saved/BuildChecks/RewardMailWBP-20261028/Normal-Build.log)에 종료 타격 C++도 포함돼 있었다. 현재 DLL SHA-256 `AA6C9CB08578832686CEB5CBC474A43CF7DB76F0121A335E78EF17AD00127810`에서 새 타입/필드를 읽었으며, 이번에는 소스 변경·새 빌드·핫리로드 없이 기존 작성 도구를 적용했다. 위 10월 27일의 DLL/저장 대기는 이 후속에서 해소됐다.

| 실제 저장 내용 | 결과 |
|---|---|
| 7레벨 티어 인덱스 1 / 옵션 인덱스 1 | `FinalStagger`, 무력화 배율 ×1.25 |
| 기본 기여 / 허용 구간 | 무력화 20·파괴 0 / `ContributionHitSection = End` |
| 몽타주의 `SkillFinisher` 트랙 | 1.0초에 네이티브 종료 타격 notify 하나 추가 |
| 기존 선택 인덱스 | 4레벨 `Mobile`/`Stationary` 0/1, 7레벨 `WideArea` 0 유지 |

기존 반복 타격과 같은 기준 피해로 종료 타격 한 번을 추가하는 앞선 작성안을 저장했다. 이 선택은 종료 타격의 무력화 기여를 20→25로 바꾸며, 기본 4초 Holding·10초 쿨타임·0 마나·기존 피해 테이블/보안 한도·이동/제자리/범위 설정을 보존한다. 실제 전체 피해·종료 동작·기여 수신을 플레이에서 검증한 결과는 아니다.

공유 GUI PID 1396의 MCP에서 PIE=false·열린 에셋 없음·대상 두 에셋 dirty=false를 먼저 읽었다. 원본 두 파일은 해시별로 백업한 뒤 PythonScript commandlet에서 대상만 저장했다. Definition의 다른 23개 속성, 기존 선택 인덱스와 기존 몽타주 notify/애니메이션/블렌드 속성을 비교했고, 네이티브 편집 검증은 Valid/경고 없음이었다. [Applied.json](../Saved/ContentChecks/GreatSwordWhirlwindFinisher/Applied.json)과 [Apply.log](../Saved/BuildChecks/WhirlwindFinisherContent-20261028-113535/Apply.log)에 저장 결과를 기록했다.

새 프로세스의 읽기 전용 재로드에서 같은 선택·20/0 기여·End 구간·notify 정확히 하나·추가 작성 불필요·dirty=false와 네이티브 검증 Valid를 확인했다. 네 중심 스킬의 실제 Definition을 읽어 계획한 4/7레벨 ID **16/16개가 각각 하나씩 존재**함을 확인했다. [ReloadResult.json](../Saved/BuildChecks/WhirlwindFinisherContent-20261028-113535/ReloadResult.json)·`Properties-Reloaded.json`·`Notifies-Reloaded.json`과 [Reload.log](../Saved/BuildChecks/WhirlwindFinisherContent-20261028-113535/Reload.log)가 근거이며 적용/재로드 프로세스는 모두 종료 0이다.

변경 전후에 기록한 19개 파일 중 위 두 에셋만 바뀌었다. 나머지 17개인 관련 소스/작성 도구 12개·다른 스킬 Definition 3개·공용 DT·DLL은 전후 해시가 동일하다. GUI의 PIE/열린 에셋/대상 dirty 상태도 전후 동일하게 유지됐다. 자세한 범위는 [ImplementationResult.json](../Saved/BuildChecks/WhirlwindFinisherContent-20261028-113535/ImplementationResult.json)에 있다. 다른 작업의 C++/보스 AI·기여 수신기·장비/보상·HUD는 수정하지 않았고, 공유 에디터 종료/재시작·메모리 강제 리로드·PIE/전투/HTTP/복제 검사도 수행하지 않았다. **16/16은 선택 에셋 작성 완료 수이며 전체 성장 구간의 플레이/밸런스 검증 완료 수가 아니다.**

### 2026-10-28 네 번째 스킬 입력과 HUD 슬롯 실제 연결

성장/편성 데이터는 8슬롯이고 첫 구간은 중심 스킬 4개인데, 실제 입력 액션과 HUD는 Q/E/F 3개만 작성돼 있었다. 네 번째 슬롯에 편성한 스킬도 같은 입력/표시 경로를 사용하도록 **기존 Q/E/F에 R을 추가**했다. 이 작업은 첫 구간의 네 스킬 입력을 연결한 것이며 5~8번 슬롯의 키/화면까지 확장한 것은 아니다. 기존 8슬롯 저장/해금/부여/발동 계약은 유지한다.

| 실제 변경한 에셋 | 내용 |
|---|---|
| `Actions/IA_QuickSkill4` | 기존 `IA_QuickSkill3`의 버튼/트리거/입력 설정을 복제한 새 액션 |
| `DA_InputConfig` | `InputTag.QuickSkill.4` → 새 액션, 키 설정 메뉴 노출 행 한 개 추가 |
| `IMC_Default` | 기존 매핑을 유지하며 새 액션의 기본 키 R 한 개 추가 |
| `WBP_PlayerHUD` | 기존 스킬 행에 `SkillQuickSlot_3`, 인덱스 3/스킬 셀을 추가하고 행 너비만 445로 조정 |

R은 Default/GreatSword/TargetLock 세 기본 컨텍스트에서 미사용인 것을 먼저 확인했다. 사용자 저장 키 설정 파일이나 기존 Q/E/F·아이템 1~4·무기/이동/고유 능력 매핑은 변경하지 않았다. 컨트롤러의 기존 데이터 순회가 새 행을 읽어 Started→Press, Completed/Canceled→Release를 연결하고, 기존 공통 서비스가 0기반 인덱스 3을 처리한다. 키 라벨도 기존의 실제 현재 키 조회를 사용한다. 새 Controller/Ability/서버 RPC나 네이티브 태그를 추가하지 않았다.

키 설정 메뉴의 새 표시명은 기존 `PostEditChangeProperty`가 태그에서 자동 생성하는 `QuickSkill.4`를 유지했다. 별도 한국어 이름으로 바꾸려던 조회/작성은 자동 생성 규칙과의 차이를 저장 전 비교에서 발견해 중단했고, 파일 해시가 그대로인 것을 확인했다(`MenuLabel.log`). 다른 작업의 입력 설정 클래스나 공통 메뉴 표시 정책을 수정해 우회하지 않았다.

새 셀은 기존 `WBP_SkillQuickSlot` 클래스를 그대로 사용하며 `bIsSkillSlot=true`, `SlotIndex=3`, `bCanDrag=false`로 작성했다. 기존 세 셀과 아이템 셀·고유 능력 아이콘 등 기존 위젯 22개를 슬롯 속성까지 비교했다. 의도한 변경은 스킬 행의 너비뿐이고 다른 기존 위젯은 일치했다. 100px 셀 네 개와 15px 간격 세 개에 맞춰 445px로 늘렸으며 기존 위치를 유지한다. 오른쪽 끝 973은 기존 고유 능력 아이콘 시작 1000보다 앞이다. 새 칸 왼쪽에만 15px 여백을 주어 기존 세 번째 셀도 수정하지 않았다. 스킬 편성/습득 기록을 자동으로 채우거나 잠긴 스킬을 발동시키는 경로는 없다.

[작성 도구](../Scripts/Editor/ConfigureGreatSwordFourthSkillSlot.py)는 기본 조회이며 `-RPGConfigureGreatSwordFourthSkillSlot`에서만 적용한다. 대상 사전 검사·원본 해시별 백업 뒤 네 에셋만 저장했다. [Applied.json](../Saved/ContentChecks/GreatSwordFourthSkillSlot/Applied.json)과 [Apply-Fixed.log](../Saved/BuildChecks/GreatSwordFourthSkillSlot-20261028-114829/Apply-Fixed.log)에 저장 4개/종료 0, 기존 입력 행·매핑·위젯 보존과 HUD `BS_UP_TO_DATE`를 기록했다. 새 프로세스의 [ReloadResult.json](../Saved/BuildChecks/GreatSwordFourthSkillSlot-20261028-114829/ReloadResult.json)·[Reload.log](../Saved/BuildChecks/GreatSwordFourthSkillSlot-20261028-114829/Reload.log)에서 네 셀 인덱스 0~3·R 매핑·추가 작성 불필요·대상 dirty=false, HUD 재컴파일 `BS_UP_TO_DATE`와 종료 0을 확인했다. 처음 조회의 Python 노출 방식 오류와 최초 작성의 FKey 생성자 오류는 저장 전에 중단됐고, 작성 도구의 네이티브 호출/직렬화만 수정했다. 실패 로그는 보존했으며 성공 근거로 합산하지 않았다.

공유 GUI의 PIE=false·대상 세 기존 에셋 dirty=false를 먼저 확인했다. 다른 작업의 우편 WBP 두 개가 열린 상태를 유지했고 이후 외부에서 열린 가방 WBP도 닫거나 되돌리지 않았다. 변경 전후 기록한 22개 파일 중 위 네 에셋만 바뀌었고 관련 C++ 10개·DLL·기존 액션/위젯 템플릿·다른 컨텍스트·우편 WBP 등 나머지 18개는 동일했다. [ImplementationResult.json](../Saved/BuildChecks/GreatSwordFourthSkillSlot-20261028-114829/ImplementationResult.json)에 범위를 기록했다. 네이티브 소스·타 작업 UI 로직·보스/장비/보상 코드는 수정하지 않았으며 새 빌드·GUI 종료/재시작/강제 패키지 리로드·PIE/실제 키 입력·서버 복제 검사는 수행하지 않았다. 실제 화면 배치·R의 누름/유지/해제와 새 칸의 드래그/등록은 후속 플레이 확인 범위다.

### 2026-10-29 선택 대상의 누적 기여 HUD

기존 대상 소유 `URPGCombatContributionComponent`의 복제 상태를 읽는 `URPGCombatContributionWidget`을 추가했다. `ARPGHUD`가 로컬 플레이어 화면에 한 번 생성하고 HUD 종료 시 회수한다. 기존 `GetSkillLockedTarget()`의 타깃 고정 대상을 표시하며 새 대상 선택/네트워크 요청 경로를 만들지 않는다. 직업·스킬별 분기, 기여 수신기·Controller·피해/AI 로직과 에셋 변경은 없다.

- 상단 중앙 340px 카드에 무력화/부위 파괴의 누적 수치·기준과 막대를 각각 표시한다. 임계값 0인 항목은 숨긴다. 기준 도달 시 색상과 `기여 완료` 문구를 함께 표시하며 그로기 발생·파괴 반응을 대신 판정하지 않는다.
- 현재 World의 유효한 대상에 활성 수신기가 있을 때만 표시한다. 로컬 소유 Pawn 부재/해제·대상 전환/파괴·기존 ASC 체력 0·World 종료/위젯 해제에서는 이전 숫자와 대상 참조를 지운다. 수신기 초기화 뒤의 0도 현재 상태에서 다시 읽는다.
- 루트는 입력을 가로채지 않고 계속 갱신하며, 내부 카드만 숨긴다. 대상이 없던 상태에서도 다음 타깃 고정을 감지할 수 있다. 별도 기여 상태나 완료 판정의 원본을 UI에 저장하지 않는다. 비활성·비유한/범위 밖 값·서로 불일치한 완료 상태는 표시하지 않는다.

작업 범위는 새 위젯 h/cpp·집중 검사 cpp와 `RPGHUD.h/.cpp` 다섯 소스다. 조율표에 먼저 예약했고 공유 DLL 읽기 고정·다른 담당의 UI/Guardian 에셋/입장 검사를 유지했다. `Saved/BuildChecks/ContributionHUD-20261029/ImplementationResult.json`이 최종 근거다. `Compile.log`의 UHT/관련 객체를 포함한 **75개 NoLink 작업 성공** 뒤, 정상 UBT 전체 응답의 OUT/PDB를 새 RuntimeProject로 바꾼 `Isolated-Link.rsp`로 별도 게임 DLL 링크를 성공시켰다. 정상 공유 Editor 전체 타깃 링크/적용과는 구분한다.

`Native.log`/`NativeReport`의 `ProjectRPG.Combat.ContributionUI` **3개 Success**를 확인했다. 실제 위젯 트리에서 두 게이지 비율·숫자/완료·한 항목 비활성·초기화, 두 대상 전환/해제·수신기 부재·대상 파괴·위젯 해제/재구성·소유 문맥 부재, 잘못된 복제 모형 상태와 실제 ASC 체력 0을 확인했다. 반영 전후 기여 값/리비전 불변도 검사했다. 테스트 전용 reflection으로 수신 상태를 공급한 UI 검사이며 실제 네트워크 `OnRep` 도착이나 승인 적중 검증으로 계산하지 않는다. 기존 기동 Error 15줄은 탐색 이전이고 이후 Error는 0이다. 엔진의 종료 상태 0 요청·호스트 종료 0·자체 프로세스 종료를 확인했으나 PowerShell의 프로세스 ExitCode 값은 null로 남았으므로 OS 종료 코드 직접 관측으로 표기하지 않았다. 입력 17개(담당 5/읽기 전용 12)와 분리 DLL은 전후 일치하며 원본 공유 DLL도 유지했다.

실제 전투 세션에서 HUD 자동 생성/타깃 고정/복제 도착, GPU 배치·실물 입력, 보스 그로기/파괴 반응과 Steam/Shipping은 이번에 실행하지 않았다. 다음은 조율된 정상 모듈 적용 뒤 실제 전투 화면에서의 표시 확인이며, 기존 기여 승인/네트워크 성공을 새 UI 실행 근거로 바꾸어 적지 않는다.

### 2026-10-29 누적 기여 HUD의 실제 전투 관측

앞선 표시 위젯을 실제 저장 캐릭터·티켓 입장·bossmap/Iris 전투에 연결해 관측했다. 기존 `Run-CharacterProgressionRestoreE2ETest.ps1 -VerifyCombat`에 선택적인 `-VerifyContributionHUD`를 추가하고 기존 combat probe h/cpp만 확장했다. 새 위젯을 검사에서 생성하거나 직접 갱신하지 않는다. 실제 `ARPGHUD`가 로컬 뷰포트에 생성한 위젯 한 개를 찾아 표시 대상·막대·숫자·완료 문구를 읽는다. 제품 HUD/수신기/스킬/에셋 수정은 없다.

- 기존 저장 4레벨 `FullCharge`와 슬롯 입력으로 조기 취소 1회/성공 적중 6회를 실행했다. 서버 승인 적중과 소유 클라이언트의 복제값에 따라 두 HUD 값이 **0→20→40→60→80→100→100**, 막대가 0→100%로 갱신됐다. 기준 도달 시 완료 문구도 일치한다.
- 첫 성공 적중 뒤 다른 대상의 0 표시 → 타깃 해제의 카드/숫자/참조 제거 → 원래 대상의 20 복원을 확인했다. 선택은 검사에서 기존 `SetSkillLockedTarget()` 게시 경로로 지정했다. 실제 타깃 고정 Ability의 키 입력/탐색 검증으로 계산하지 않는다.
- 위젯은 실제 NativeTick으로 갱신됐고, UI용 기여 상태 주입·reflection이나 강제 Refresh 호출은 없다. 기존 쿨타임/피해·투지/필터 검사는 같은 입력 드라이버를 유지했으며 저장 구성과 같은 계정의 다른 캐릭터도 그대로다.

숨긴 null RHI 클라이언트는 최초 관측에서 멈췄다(`754cad4a…`). `-RenderOffscreen`만 추가한 두 번째 실행(`8cafb98a…`)에서도 위젯 소유자/현재 World/뷰포트 한 개는 확인됐지만 geometry가 0이고 표시 대상이 없었다. 두 실행은 자체 stop gate로 정리했고 실패/미완료 기록을 보존했다. 엔진 Slate의 숨긴 창 처리와 관측을 근거로 HUD 관측 모드의 **클라이언트만 null RHI를 해제하고 D3D12 화면 밖 렌더링**을 사용했다. 서버는 null RHI를 유지한다. 화면에 사용자 조작 창을 띄우거나 위젯 Tick을 검사에서 대신 호출하지 않는다. 실제 그리기 경로에서 geometry가 양수·표시 대상이 유효해졌고 일곱 관측을 완료했다. 수동 시각 검수나 스크린샷 증거는 아니다.

`Saved/BuildChecks/ContributionHUDLive-20261029/ImplementationResult.json`이 최종 근거다. NoLink 2개 작업과 문맥 진단 추가 후 2개 작업, 별도 전체 게임 DLL 링크가 성공했다. 최종 실행 `Saved/Tests/CharacterProgressionRestore/ef0f84b525134ab994dc01a851c39988/`의 `Verification.proof.json`, `ContributionHUD.proof.json`, `Combat.proof.json`, server/client 로그에 HUD PASS·서버/클라이언트/호스트 종료 0·실행 Error 0이 있다. 선택 입력 67개와 분리 DLL/닫힌 로그 해시를 기록했고 담당 소스 3개/추가 읽기 고정 17개와 원본 공유 DLL도 유지됐다.

다른 backend 소스 작업과 겹치지 않도록 `-BackendBuildDirectory`로 이전 `a4d5ec76…`의 닫힌 검증 빌드를 자체 Memory TCP 3062에 복사해 사용했다. 자체 UE UDP 18862와 독립 UserDir/분리 RuntimeProject만 사용하고 다른 프로세스·GUI/MCP·DB·에셋은 유지했다. 현재 backend 소스 빌드나 정상 공유 Editor 타깃 적용을 뜻하지 않는다. 기동 중 자체 lease heartbeat 7회이며 운영 승인/TTL 정책 변경은 없다. 다음은 조율된 정상 모듈 반영과 수동 화면 배치/물리 입력 확인이고 보스 그로기/파괴 반응·Steam/Shipping은 별도다.

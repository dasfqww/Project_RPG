---
name: project-rpg-skill-system
description: Resume, implement, or review Project_RPG's GAS skill system, casting policies, tripod overrides, held-key combos, and third-person skill controls. Excludes D1 migration, boss AI, and online backend work.
---

# Project_RPG 스킬 시스템

## 최소 컨텍스트로 재개

프로젝트 루트는 `C:/UE5/Project_RPG`다. 이 파일을 모두 읽고, 재개·진행도 질문이면 [진행 요약](references/resume.md)을 읽는다. 이후 요청과 관련된 파일만 확인한다. 전체 소스·변경 목록·과거 로그를 매번 출력하지 않는다.

현재 소스와 기록이 다르면 소스를 기준으로 요약을 갱신한다. 완료한 검증은 관련 변경이나 실패 근거가 있을 때만 다시 실행한다. 스킬 적용은 새로운 에셋 변경이나 에디터 실행 권한을 뜻하지 않는다.

## 구현 우선, 검증 후순위

2026-10-19 사용자 확정 원칙이다. 이후 작업은 요청한 기능과 확인된 누락 구현에 집중한다. 기존 기록의 검증 우선 계획보다 이 원칙을 우선한다.

- 테스트·PIE·멀티클라이언트 검증과 검증용 도구 추가는 뒤로 미룬다. 사용자가 검증을 요청하거나 구현을 막는 오류 해결에 꼭 필요한 경우에만 해당 범위를 진행한다.
- 검증을 위한 에디터 단독 사용·MCP 연결·재시작을 구현 진행의 전제조건으로 삼지 않는다. 독립적으로 진행할 필수 구현이 없다면 임의 과제를 만들지 않고 남은 선택을 알린다.
- 필요시 변경 관련 최소 컴파일 확인만 수행한다. 구현 완료와 검증 완료를 구분하고, 미실행 검증은 후속 항목으로 남긴다.

## 불필요한 작업 금지

2026-10-16 사용자 확정 원칙이다. 요청한 기능 구현이나 재현된 오류 해결에 직접 필요한 최소 변경·검증만 수행한다.

- 필요성이 확인되지 않은 리팩터링·추상화·최적화, 새 검증 도구·테스트 프레임워크·별도 실행 환경을 추가하지 않는다. 기존 수단으로 충분하면 그대로 사용한다.
- 단순 UI 배치는 기존 PIE에서 필요한 동작을 확인하는 정도로 끝낸다. 검증 자체를 확대하거나 테스트 수정만을 위해 별도 빌드·에디터 재시작을 반복 요구하지 않는다. 필요한 재검증은 다음 예정된 빌드에 묶는다.
- 에디터 점유나 권한 제한으로 막혔다고 우회 도구나 새 과제를 만들어 작업을 늘리지 않는다. 독립적으로 진행할 필수 작업이 없다면 현재 상태와 남은 확인만 보고한다.
- 추가 작업이 실제로 필요하다면 해결할 문제와 기존 수단으로 부족한 이유를 먼저 짧게 설명한다. 요청 범위가 커지는 경우 사용자 확인 후 진행한다. 검증의 수행 시점은 위 구현 우선 원칙을 따른다.

## 작업 범위

- 대상은 `Source/Project_RPG/`의 신규 GAS 스킬 코어와 플레이어 입력/UI 연결이다.
- 즉발·홀딩·차지·지점·콤보·체인·캐스팅을 데이터와 정책으로 구성하고, 트라이포드로 시전 방식을 교체할 수 있게 한다.
- 콤보는 키 유지도 지원한다. 후속 세션 전환은 유효한 애니메이션 이벤트 구간에서 처리하며, 키 유지가 전체 스킬 재시전으로 이어지지 않게 한다.
- 기본 쿨타임은 1초다. 기존 긴 쿨타임과의 관계는 진행 요약을 확인한다. 기본값·최소값·실제 적용값을 구분한다.
- 3인칭 백뷰의 조준, 지점 지정, 입력 취소, UI 커서/입력 잠금 복원을 고려한다.
- 사용자가 미룬 콤보 키 해제–노티파이 직전 경계의 실제 플레이 확인은 임의로 재개하지 않는다.

## SOLID를 적용할 책임 경계

- `SkillDefinition`: 신규 스킬의 공유 데이터 원본. 실행 상태·타이머를 저장하지 않는다.
- `RuntimeSpec`: 활성화 시 레벨/트라이포드/수치/정책을 한 번 해석한 스냅샷. 현재 시전 중 선택 변경은 다음 시전에만 반영한다.
- `GameplayAbility_SkillContainer`: GAS 활성화·예측·커밋·종료를 조율한다. 새 기능의 타기팅/효과/연출까지 계속 직접 추가하지 않는다.
- 실행·타기팅 정책과 활성화별 `SkillAction`/AbilityTask에 각 동작을 맡긴다. 트라이포드 조합마다 Ability 클래스를 만들거나 UI에서 시전 타입을 하드코딩하지 않는다.
- 레벨·SP·선택은 서버가 검증하고 소유자에게 복제한다. UI/ViewModel은 요청·표시를 담당하며, 거부 시 서버 상태로 복원한다. 피해 확정은 서버 책임이다.
- 클래스 분리 자체를 SOLID 준수의 증거로 삼지 않는다. 책임 중복, 구체 타입 의존, 수정 범위로 판단한다. FastArray나 전체 효과/연출 분해는 필요가 확인될 때만 한다.

## 필요한 파일만 찾기

아래 경로는 `Source/Project_RPG/` 기준이다. 구현은 `Private/`, 공개 계약은 대응하는 `Public/`을 확인한다.

| 작업 | 시작점 |
| --- | --- |
| 시전 타입·트라이포드 합성 | `Skill/RPGSkillDefinition.*`, `RPGSkillRuntimeTypes.*`, `RPGSkillExecutionTypes.h` |
| GAS 수명주기·입력·정책 | `Skill/RPGGameplayAbility_SkillContainer.*`, `RPGSkillExecutionPolicy.*`, `RPGSkillAction*` |
| 조준·지점 지정 | `Skill/RPGSkillTargetingPolicy.*`, `RPGSkillTargetingTypes.*` |
| 키 입력·쿨타임·기존 콤보 | `Component/RPGAbilitySystemComponent.*`, `Ability/RPGPlayerGameplayAbility.*`, `Ability/RPGComboSkillAbility.*` |
| 레벨·SP·선택 RPC/복제 | `Component/Skill/RPGPlayerSkillComponent.*`, `Skill/RPGSkillCatalogSubsystem.*` |
| UI·일반 스킬창 입력 | `UI/MVVM/RPGSkillViewModel.*`, `UI/ViewModel/RPGSkillSlotViewModel.*`, `UI/Skill/RPGTripod*`, `Component/UI/RPGSkillWindowComponent.*` |
| 회귀 검증 | `Private/Tests/RPGSkillRuntimeSpecTests.cpp`, `RPGSkillWindowInputTests.cpp`, `RPGSkillTripodNetworkProbe.*` |

설계 이력이 필요할 때만 [아키텍처 기록](../../Docs/SKILL_AND_TRIPOD_MODULAR_ARCHITECTURE.md)에서 `rg -n`으로 주제를 찾는다. 시전 연결 검증은 `트라이포드 버튼–RPC–GAS`, 최신 쿨타임 검증은 `원본 Definition의 쿨타임 예측·복제 검증`, 남은 작업은 `다음 우선순위` 절에 있다.

## 검증과 인계

- 에디터 없이 소스 수정·컴파일·비렌더링 검증이 가능하다. 실제 키보드/마우스·커서 복원은 최신 정상 빌드로 PIE에서 확인한다.
- 사용자가 검증을 재개하도록 요청하거나 구현을 막는 오류 해결에 꼭 필요한 경우, `ProjectRPG.Skill` 테스트에서 관련 항목만 선택한다. RPC/복제 검증에는 `Scripts/Network/Run-IrisMultiClientTest.ps1`이 해당 항목을 실제 지원하는지 먼저 확인하고 사용한다.
- 네이티브 클릭 핸들러 호출은 실제 마우스 클릭이 아니며, OnRep 직접 호출은 실제 복제가 아니다. 격리 DLL 링크를 프로젝트 전체 빌드/핫리로드 완료로 보고하지 않는다.
- 임시 Definition 복제본 검증을 운영 트라이포드 데이터 검증으로 일반화하지 않는다. GAS 종료 후 활성 스냅샷이 초기화되는 것은 정상이며, 정상 종료는 종료 이벤트로 확인한다.
- 인계 시 진행 요약의 날짜·다음 작업·실제 성공 로그·미검증 범위를 짧게 갱신한다. 상세 기록은 기존 아키텍처 문서에만 추가하고 같은 이력을 여러 곳에 복제하지 않는다.

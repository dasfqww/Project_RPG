# Experience의 쿠킹 제외 참조 검사

추가일: 2026-10-16.

## 추가한 기능

에디터의 데이터 검사에서 공통 ExperienceDefinition·ActionSet의 저장된 게임 참조가
Packaging 제외 폴더에 도달하면 오류와 참조 경로를 표시한다.

- [에디터 검사 구현](../Source/Project_RPGEditor/Private/Experience/RPGExperienceCookReferenceValidator.cpp)
- [검사 클래스](../Source/Project_RPGEditor/Private/Experience/RPGExperienceCookReferenceValidator.h)
- [에디터 모듈 의존성](../Source/Project_RPGEditor/Project_RPGEditor.Build.cs)

URPGExperienceCookReferenceValidator는 기존 UEditorValidatorBase를 사용하는 상태 없는 에디터 검사 클래스다.
네이티브 검사 발견은 UEditorValidatorSubsystem이 담당한다.
새 Manager나 런타임 서비스, 별도 메뉴·등록 코드는 없다.

## 동작

1. 설정의 ProjectPackagingSettings.DirectoriesToNeverCook를 읽어 제외 폴더를 구한다.
2. 검사 에셋 자체가 제외 폴더에 있으면 이번 검사 대상에서 뺀다.
3. 저장된 Asset Registry의 Package 의존성 중 Game 플래그가 있는 hard·soft 참조를 따라간다.
4. 제외 폴더에 도달하면 원본 에셋부터 해당 패키지까지의 경로를 오류로 표시한다.
5. 다른 참조도 계속 확인하되 제외 폴더 내부의 의존성은 더 추적하지 않는다.

예를 들어 아래와 같은 저장된 게임 참조를 찾도록 구현했다.

~~~text
/Game/System/Experiences/DA_Game
 -> /Game/System/Experiences/ActionSets/DA_GameUI
 -> /Game/System/Experiences/Development/BP_ExperienceDevelopmentHUD
~~~

제외 경로를 소스에 하드코딩하지 않고 현재 Packaging 설정을 사용한다.
패키지 경계와 하위 폴더를 비교하므로 Development와 DevelopmentTools를 같은 폴더로 취급하지 않는다.
순환 참조와 공유 의존성은 패키지당 한 번 방문한다.
일정한 순서로 의존성을 정렬한 너비 우선 탐색으로 참조 경로를 구성한다.

참조된 콘텐츠를 LoadObject로 로드하거나 저장하지 않는다.
GameFeature 활성화와 런타임 Experience 선택도 수행하지 않는다.
다른 에셋을 수정하거나 이동하는 자동 교정 기능은 없다.

## 저장 상태와 불완전한 데이터

Asset Registry의 GetDependencies는 디스크에 저장된 패키지의 의존성을 반환한다.

- 원본 에셋이 변경된 상태이면 경고를 남기고 이번 검사를 건너뛴다.
- Asset Registry가 스캔 중이면 경고를 남기고 이번 검사를 건너뛴다.
- 탐색 중 이미 로드된 의존 패키지에 저장하지 않은 변경이 있으면 저장 후 재검사를 요구하는 오류를 표시한다.
- 필요한 패키지의 의존성 데이터가 없으면 콘텐츠 스캔·마운트 후 재검사를 요구하는 오류를 표시한다.

건너뛴 경우 이 검사기의 결과는 NotValidated이며 경계 검사를 통과한 결과로 해석하지 않는다.
같이 실행되는 기존 에셋 검사는 각자의 결과를 계속 반환한다.
개발 폴더 에셋을 대상으로 한 다른 데이터 검사도 유지된다.

## 사용 방법

일반 원본 Editor 빌드와 재실행으로 새 검사 클래스를 반영한 뒤 사용한다.

1. 검사할 Experience·ActionSet과 변경한 의존 에셋을 저장한다.
2. 콘텐츠 브라우저에서 대상 Experience·ActionSet을 선택한다.
3. 데이터 검사(Validate Assets)를 실행한다.
4. 출력에 표시된 참조 경로나 저장·스캔 상태를 확인한다.

검사 규칙은 공통 ExperienceDefinition·ActionSet 인스턴스에만 적용한다.
맵·HUD Blueprint 자체나 프로젝트 설정의 문자열·URL, GameFeature 이름으로 동적으로 찾는 콘텐츠까지 추적하는 기능은 아니다.
EditorOnly 참조·Manage 의존성도 이번 탐색에 포함하지 않는다.
이 검사는 실제 플랫폼 쿠킹 결과를 대신하지 않는다.

## 모듈과 빌드

새 헤더·구현은 Project_RPGEditor의 Private/Experience에만 추가했다.
에디터 모듈의 PrivateDependencyModuleNames에 AssetRegistry와 DeveloperToolSettings를 명시했다.
기존 DataValidation 의존성은 재사용한다.
Project_RPG 런타임 모듈과 GameFeature 모듈의 의존성·구현은 변경하지 않았다.

UE 5.8.1 / Project_RPGEditor / Win64 / Development 사본 빌드가 성공했다.
16개 작업, 69.22초, 종료 코드 0이며 새 소스 세 파일은 원본·사본 해시가 같다.
여기서 세 파일은 새 헤더·구현과 변경한 Build.cs를 뜻한다.
나머지 사본 입력은 기존 고정된 소스이므로 최신 원본 전체 빌드 결과는 아니다.
원본 DLL 교체·에디터 제어·실제 데이터 검사·자동화 테스트·쿠킹 실행은 수행하지 않았다.

기록: Saved/Logs/RPGExperienceCookValidation-20261016/.

- [변경 범위와 소스 해시](../Saved/Logs/RPGExperienceCookValidation-20261016/ChangesReport.json)
- [사본 빌드 결과](../Saved/Logs/RPGExperienceCookValidation-20261016/BuildResult.json)
- Build.log / BuildConsole.log / BuildStderr.log: 빌드 기록
- FilesBefore.json / Before / After / Diffs: 작업 전후 대조 자료

아이템·인벤토리·장비 창·스킬·던전·보상·백엔드·보스 소스와 에셋은 이 작업에서 수정하지 않았다.

## 2026-10-25: 프런트엔드의 명시적 PrimaryAssetId 참조

프런트엔드 정의도 검사 대상으로 포함했습니다.
MapID와 ExperienceID는 문자열 식별자이므로 일반 패키지 의존성 조회만으로 따라갈 수 없습니다.
두 ID를 Asset Manager에 등록된 경로로 해석하여 기존 제외 폴더·저장 상태·의존성 검사의 시작점에 추가합니다.
미등록 ID는 오류로 처리하며, 선택을 비운 ExperienceID는 기존 맵 선택 경로를 유지합니다.

이 검사는 명시적 ID와 저장된 의존성을 대상으로 하며 ExtraArgs URL이나 프로젝트 설정의 모든 동적 선택을 추론하지 않습니다.
에셋 저장, 실제 맵 로딩, 세션 생성, 게임 기능 활성화나 런타임 선택 정책을 수행하지 않습니다.
이번 변경의 통합 빌드·실행 결과는 Saved/Logs/RPGModularizationClosure-20261025/에 기록합니다.

## 2026-10-25: 명시적 ID와 ActionSet 참조 정책 실행 확인

- 정상 공통 ActionSet/Experience와 사본의 정상 프런트엔드 fixture가 데이터 검사를 통과했습니다.
- 미등록 ExperienceID는 등록 경로 오류로, 개발용 ExperienceID는 실제 제외 폴더 참조 경로로 거부됐습니다.
  에셋 검사 8개 케이스가 의도한 결과를 반환했습니다.
- DefaultGame.ini에 ActionSet 폴더 전용 RPGExperienceComposition 도메인을 추가했습니다.
  해당 폴더만 네 공통 GameFeature를 직접 참조하며, 프로젝트는 이 ActionSet 도메인을 참조할 수 있습니다.
  다른 프로젝트 폴더의 GameFeature 직접 참조 제한과 전역 검사 활성화는 유지합니다.
- Windows 공통 콘텐츠 쿠킹에서 네 GameFeatureData·공통 입력·ActionSet을 포함한 435개 패키지를 생성했습니다.
  Experience/ActionSet AlwaysCook를 유지한 상태에서 Development/Examples 패키지는 0개였습니다.

쿠킹은 고정 사본의 공통 기능 범위이며, 보호 대상 자동 쿠킹 규칙과 Landmass 시작 패키지를 확인 범위에서만 분리했습니다.
사본 설정은 복원했고 원본의 패키징 규칙·맵·에셋은 변경하지 않았습니다.
전체 게임 Shipping 패키징 성공을 의미하지 않습니다.
자세한 범위: EXPERIENCE_MODULARIZATION_INTEGRATION.md.
기록: Saved/Logs/RPGModularizationClosure-20261025/FocusedCook3/Report.json.

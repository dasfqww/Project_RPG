# 개발용 Experience와 배포 콘텐츠 경계

적용일: 2026-10-16.

## 적용한 설정

[DefaultGame.ini](../Config/DefaultGame.ini)의 ProjectPackagingSettings에 다음 폴더를 추가했다.

~~~ini
[/Script/UnrealEd.ProjectPackagingSettings]
+DirectoriesToNeverCook=(Path="/Game/System/Experiences/Development")
+DirectoriesToNeverCook=(Path="/Game/System/Experiences/Examples")
~~~

| 경로 | 용도 | 일반 일괄 쿠킹 정책 |
| --- | --- | --- |
| /Game/System/Experiences/Development | 공통 UI 개발용 Experience·HUD·GameMode·맵 | 폴더 전체 제외 |
| /Game/System/Experiences/Examples | 수명·선택·Ready 연동을 확인하는 예제 Blueprint와 맵 | 폴더 전체 제외 |
| /Game/System/Experiences/ActionSets | 공통 기능을 조합하는 ActionSet | 기존 규칙 유지 |
| /Game/System/Experiences/DA_* | 재사용 가능한 공통 Experience 구성 | 기존 규칙 유지 |
| /RPGClientPresentation, /RPGInputMapping, /RPGOptionsUI, /RPGClientUI | 실제 공통 GameFeature 구현과 콘텐츠 | 기존 규칙 유지 |

현재 제외 폴더의 에셋은 Development 4개, Examples 6개다.
같은 폴더에 추가하는 개발용 에셋에도 이 설정이 적용된다.
기존 에셋의 이동·재저장·이름 변경은 없다.

## 폴더를 기준으로 나눈 이유

AssetManager는 /Game/System/Experiences의 Experience를 AlwaysCook으로 스캔한다.
이 규칙은 에디터에서의 검색과 기존 런타임 구성에 계속 사용한다.
CookByTheBook의 Packaging 제외 목록으로 개발용 패키지를 구분했다.

UE 5.8.1의 AssetManager는 AssetBundle 항목을 직접 관리 참조로 등록한다.
Experience의 ActionSets도 Client·Server AssetBundle에 들어간다.
bApplyRecursively=False만 지정해도 직접 번들 참조는 관리 대상에 남는다.
따라서 개발용 Experience에 NeverCook 관리 규칙을 부여하는 방법은 공유 ActionSet에도 영향을 줄 수 있다.
이번 설정은 개발 폴더만 제외하므로 공유 ActionSet과 옵션 위젯, GameFeature 콘텐츠에 새 관리 규칙을 부여하지 않는다.

근거로 읽은 로컬 엔진 소스:

- Runtime/Engine/Private/AssetManager.cpp: UpdateManagementDatabase, ModifyCook, FPrimaryAssetCookRuleUnion::UnionWith.
- Editor/UnrealEd/Private/CookOnTheFlyServer.cpp: GetNeverCookPackageNames.
- Developer/DeveloperToolSettings/Classes/Settings/ProjectPackagingSettings.h: DirectoriesToNeverCook.

## 개발 실행과 배포 사용

Editor·PIE·UnrealEditor의 별도 -game 실행은 기존 개발용 콘텐츠를 계속 읽을 수 있다.
개발 실행 스크립트와 기존 관찰 코드를 변경하지 않았다.
이 폴더 제외는 Development·Shipping 구성의 일반 일괄 쿠킹에 적용된다.
패키징한 Development 게임에서도 이 개발 전용 맵과 Experience를 사용할 수 없도록 설정했다.

배포할 게임 맵과 Experience는 두 제외 폴더 밖에 두고 기존 ActionSet을 참조한다.
배포용 콘텐츠에서 제외 폴더의 개발 에셋을 참조하는 구성은 이 정책의 사용 범위에 포함하지 않는다.
공통 C++ 모듈과 GameFeature 바이너리의 배포 포함 여부는 이 콘텐츠 폴더 정책과 별개다.

UE 5.8.1은 Packaging의 이 목록을 CookByTheBook에서 사용한다.
CookOnTheFly나 별도의 DLC·커스텀 쿠킹 설정에 대한 제외 보장은 이번 범위가 아니다.
bOnlyCookProductionAssets, AssetManager 스캔 목록, 기존 시작 맵과 기본 Experience 선택은 유지했다.

## 반영과 확인 범위

C++ 변경이 없어 추가 컴파일은 필요하지 않다.
다음 별도 일괄 쿠킹 프로세스가 수정된 프로젝트 설정을 읽는다.
열려 있는 에디터의 Packaging 설정 화면에는 이미 읽어 둔 값이 표시될 수 있으므로
이 작업을 현재 에디터 화면의 즉시 갱신 결과로 해석하지 않는다.

설정 차이를 검토했고 기존 설정의 모든 바이트를 유지한 채 위 배열 항목과 설명만 추가했다.
공유 ActionSet·GameFeature 에셋과 Source 파일의 해시를 작업 전후 대조했다.
자동화 테스트 추가·실행, 에디터 실행 제어, 쿠킹·패키징 실행은 수행하지 않았다.
실제 산출물에서 개발 패키지가 제외됐는지는 후속 패키징 실행에서 확인할 항목이다.

기록: Saved/Logs/RPGExperienceCookPolicy-20261016/.

- [변경 파일·해시·작업 범위](../Saved/Logs/RPGExperienceCookPolicy-20261016/ChangesReport.json)
- [설정 차이](../Saved/Logs/RPGExperienceCookPolicy-20261016/DefaultGame.ini.diff)
- [제외 폴더의 현재 패키지 목록](../Saved/Logs/RPGExperienceCookPolicy-20261016/ExcludedPackages.json)
- FilesBefore.json / Before / After: 시작 시점 대조 자료와 이번 파일의 작업 전·후 내용

새 Manager와 C++ 클래스, 에셋은 추가하지 않았다.
아이템·인벤토리·장비 창·스킬·던전·보상·백엔드·보스 구현은 이 작업에서 수정하지 않았다.


## 2026-10-16: 배포 Experience의 제외 폴더 참조 검사

에디터 데이터 검사에 공통 ExperienceDefinition·ActionSet의 저장된 게임 의존성 검사를 추가했다.
Packaging 제외 폴더에 도달하는 참조 경로를 오류로 표시하며 개발 전용 에셋 자체는 검사 대상에서 제외한다.
순환 참조·공유 의존성을 처리하고 저장하지 않은 변경이나 불완전한 Registry 데이터는 통과로 처리하지 않는다.

새 에디터 전용 검사 클래스 하나를 추가했다. 런타임 코드·기능 모듈과 에셋은 변경하지 않았다.
사본 에디터 모듈 빌드는 16개 작업, 69.22초, 종료 코드 0이다.
실제 에셋 검사·자동화 테스트·쿠킹은 실행하지 않았다.

[대상·저장 상태 처리·사용 방법·빌드 기록](EXPERIENCE_COOK_REFERENCE_VALIDATION.md)에 정리했다.

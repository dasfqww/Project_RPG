# Manager 수명 리팩토링

대상은 `Source/Project_RPG`와 기존 `RPGOptionsUIRuntime`이다.
기존 Manager 네 개의 소유 범위를 정리했다. 새 Manager 클래스는 추가하지 않았다.

## 소유 범위

| 기존 클래스 | 변경 전 | 변경 후 |
|---|---|---|
| `USoundManager` | `UObject` 전역 싱글톤, `AddToRoot` | `UGameInstanceSubsystem`, GameInstance마다 하나 |
| `UUIManager` | `UObject` 전역 싱글톤, `AddToRoot` | `ULocalPlayerSubsystem`, 로컬 플레이어마다 하나 |
| `UPoolManager` | GameInstance Subsystem | `UWorldSubsystem`, 월드마다 하나 |
| `UObjectManager` | GameInstance Subsystem | `UWorldSubsystem`, 월드마다 하나 |

기존 reflected 클래스 이름과 자산 경로를 유지했다.
Pool·Object는 Game, PIE, GamePreview 월드에서 생성된다.
기존 DataManager·HttpWebManager·TeamManager와 ExperienceManagerComponent는 유지했다.

## 사운드

- 전역 `Instance`와 `AddToRoot()`를 제거했다.
- 사운드 캐시는 GameInstance의 수명 동안 유지하고 종료 시 해제한다.
- 월드 참조는 `TWeakObjectPtr`로 보유한다.
- 해당 GameInstance에 속한 월드만 `Init`에서 받는다.
- SoundMix는 같은 월드에서 중복 Push하지 않고, 월드 변경·정리·Subsystem 종료 시 자신이 Push한 항목을 Pop한다.
- GameInstance의 월드 초기화 콜백은 해당 GameInstance의 월드만 처리한다.
- GameInstance 종료 시 등록한 전역 Map·World 델리게이트를 해제한다.
- 공통 버튼·옵션 메뉴·메시지 박스·사운드 AnimNotify는 자신의 컨텍스트로 서비스를 조회한다.
- 기존 사운드 경로, 재생 인자 순서, 옵션 JSON 형식과 저장 데이터는 유지한다.

## UI와 GameFeature 종료

- 팝업 스택은 로컬 플레이어의 Subsystem이 보유한다.
- Push·Remove는 대상 위젯의 소유 플레이어에 해당하는 Manager로 전달한다.
- 같은 위젯의 중복 Push를 막고 `AddToPlayerScreen()`으로 표시한다.
- 위젯을 생성한 월드를 별도로 기록해 해당 월드 종료 시 스택을 정리한다.
- 컨트롤러 교체·로컬 플레이어 종료 시 팝업을 제거하고 참조를 해제한다.
- `URPGWidgetBase::NativeDestruct()`에서 `ForgetUI()`를 호출한다.
  기존 GameFeature의 위젯 제거 경로도 이 처리를 거쳐 팝업 스택 참조를 해제한다.
- 위젯 생성 중 NativeConstruct가 Feature를 해제하거나 소유자를 종료하는 경우를 처리한다.
- 아래쪽 팝업 제거와 다른 월드의 종료가 현재 팝업의 활성 상태를 불필요하게 바꾸지 않도록 했다.
- 옵션 탭과 공통 버튼의 이벤트 등록은 중복을 막는다. 옵션 탭 이벤트는 NativeDestruct에서 해제한다.

## Pool·Object

- Actor와 풀은 자신을 소유하는 월드의 Subsystem에서 관리한다.
- 월드·Actor 참조는 weak reference로 보유한다.
- 풀의 클래스는 native Pool 객체의 strong reference로 유지한다.
- 대여 가능 Actor를 별도로 기록한다. 대여된 Actor가 숨겨졌다는 이유로 다시 대여하지 않는다.
- 파괴된 Actor는 대여 목록에서 제외한다.
- 다른 풀·월드의 Actor 반환을 거부하고 중복 반환을 막는다.
- `UnregisterPool()`은 등록을 먼저 제거한 뒤 풀의 Actor를 정리한다.
- 콜백 중 등록이 해제되어도 현재 native Pool 호출이 유효하도록 shared pointer로 보유한다.
- ObjectManager는 Actor EndPlay 델리게이트로 추적 항목을 제거하고 종료 시 델리게이트와 목록을 해제한다.
- 풀 반환이 실패한 Actor는 기존 Despawn 경로에서 Destroy한다.

현재 GameFeature Action에 풀 등록을 자동으로 추가한 것은 아니다.
향후 기능이 풀을 등록한다면 해당 기능의 소유 범위에 맞춰 소비자 종료와 `UnregisterPool()` 호출 순서를 정해야 한다.

## 접근 API와 호환 경로 (2026-10-16)

공통 UI·사운드·풀·오브젝트 Manager는 게임 스레드에서 직접 호출한다.
`Get(Context)`는 항상 호출 가능한 수신자를 반환하므로 호출부의 Manager null 검사를 제거했다.

```cpp
USoundManager::Get(this)->Play(EffectSound);
UUIManager::Get(Widget)->PushUI(Widget);
UPoolManager::Get(this)->RegisterPool(PoolName, ActorClass);
UObjectManager::Get(this)->Despawn(PoolName, Actor);
```

실제 상태는 기존 엔진 Subsystem 인스턴스가 보유한다.

| Manager | 단일 인스턴스의 소유 범위 |
|---|---|
| `UUIManager` | 위젯·컨트롤러가 속한 `ULocalPlayer` |
| `USoundManager` | 컨텍스트가 속한 `UGameInstance` |
| `UPoolManager`·`UObjectManager` | 컨텍스트가 속한 `UWorld` |

UI의 위젯·컨트롤러·LocalPlayer 컨텍스트는 해당 플레이어를 선택한다.
GameInstance나 일반 월드 컨텍스트는 해당 GameInstance의 첫 번째 로컬 플레이어를 선택한다.
다른 로컬 플레이어를 지정할 때는 해당 위젯·컨트롤러·LocalPlayer를 전달한다.
Pool·Object의 새 API는 컨텍스트 인자가 필수이며, 다른 월드를 전역 검색하지 않는다.
기존 `Get<UPoolManager>(Context)`·`Get<UObjectManager>(Context)`는 각각 새 API로 위임한다.
다른 Subsystem 타입을 템플릿 인자로 주는 기존 범용 조회에는 non-null 보장을 추가하지 않았다.

### 소유자가 없거나 종료 중인 경우

- 실제 소유 Subsystem을 찾지 못하면 기존 클래스의 default object를 상태 없는 수신자로 반환한다.
- 수신자에 팝업·사운드 캐시·풀·Actor 참조를 저장하지 않는다.
- UI 수신자는 위젯 소유자를 찾을 수 있을 때만 실제 UIManager에 요청을 전달한다.
  내부 nullable 조회를 별도로 사용해 수신자가 자신에게 재귀 호출하지 않는다.
- 소유자가 없는 UI 요청, 사운드 재생·볼륨 변경, 풀 등록·반환, Actor 생성·제거 요청은 건너뛴다.
- 종료 중인 실제 인스턴스도 신규 재생·캐시 로드·Actor 변경을 차단한다.
  실제 종료 처리는 기존 Subsystem 정리 경로에서 수행한다.
- 실패 시 Actor·위젯 조회는 `nullptr`, 풀 반환은 `false`, 개수 조회는 `0`을 반환한다.
  호출자는 필요한 작업 결과를 확인한다.
- Manager를 전역 변수에 캐시하지 않고, 각 호출 시 현재 소유 컨텍스트로 조회한다.
- 이 API는 게임 스레드 전용이며 `Get`에서 이를 검사한다.

Sound·UI의 인자 없는 `Get()`는 기존 호출자를 위한 호환 경로다.
현재 게임 월드·게임 뷰포트·단일 GameInstance 순으로 소유자를 찾는다.
여러 PIE 월드나 로컬 플레이어 중 특정 소유자를 선택하는 신규 코드는 `Get(Context)`를 사용한다.
보호 범위의 던전 진입 UI·보스 게임 모드에 남은 인자 없는 호출도 기존 API와 호환된다.

공통 함수 `Manager/RPGManagerContext.h/.cpp`는 엔진 소유자를 조회한다.
새 Manager 클래스·전역 gameplay 인스턴스를 생성하거나 소유 상태를 이 함수에 보관하지 않는다.
아이템 캐시를 관리하는 DataManager와 백엔드 HttpWebManager, TeamManager의 계약은 기존대로 유지했다.

## 변경 범위와 기록 (2026-10-12)

- 기존 소스 20개 수정, 공통 함수 소스 2개 추가.
- 아이템·인벤토리·장비 UI·스킬·던전·보상·백엔드·보스 BT의 소스 파일은 이번 작업에서 수정하지 않았다.
- 공통 Manager와 WidgetBase의 변경은 기존 호출자가 사용하는 수명 관리 경로에 적용된다.
- 새 Experience·ActionSet·GameFeature 에셋을 생성하거나 기존 맵·에셋을 저장하지 않았다.
- 작업 중 다른 작업에서 변경된 소스는 현재 내용 그대로 사본에 반영했다.
- 작업 전 백업, 소스 목록과 SHA256은 `Saved/Logs/RPGManagerLifetimeRefactor-20261012/`에 있다.
- 이번 재개 시점의 보호 소스 기준은 `ProtectedSourcesAtResume.json`이다.
  이전 시점과 재개 사이의 다른 작업 변경은 별도로 기록한다.

## 빌드 결과와 적용 (2026-10-12)

- 원본 Editor 대상의 UHT는 통과했다. 당시 일반 빌드는 C++ 컴파일 시작 전에 Live Coding 때문에 중단됐다.
- 별도 사본: `Saved/ManagerLifetimeBuild-20261012/Project_RPG.uproject`.
- Editor 대상에서 `Project_RPG`·`RPGOptionsUIRuntime`과 필요한 의존 모듈의 빌드가 성공했다.
- 첫 재개 빌드: 430개 작업, 920.33초, 종료 코드 0.
- 마지막 UI 호환 경로를 반영한 증분 빌드: 8개 작업, 21.94초, 종료 코드 0.
- 작업에서 변경한 소스 22개는 원본과 최종 빌드 사본의 SHA256이 동일하다.
- 사본의 의존 소스는 재개 시점에 고정했다. 이후 다른 작업에서 바뀐 소스는 변경 기록에 별도로 남긴다.
- 원본 프로젝트의 객체 파일과 DLL을 사본 빌드에 공유하지 않았다. 이 작업에서 원본 DLL을 교체하지 않았다.
- 기존 `ReleaseMouseCapture` 호출의 C4996 경고가 남아 있으며 컴파일 오류는 없다.
- 최종 로그: `Saved/Logs/RPGManagerLifetimeRefactor-20261012/FinalIsolatedBuild.log`.
- 최종 결과: `Saved/Logs/RPGManagerLifetimeRefactor-20261012/FinalIsolatedBuildResult.json`.
- 변경 범위·에셋·사본 대조 기록: `Saved/Logs/RPGManagerLifetimeRefactor-20261012/ChangesReport.json`.

상속 기반과 native 인터페이스가 변경되었으므로 원본에 반영할 때는 에디터를 종료한 뒤
`Project_RPGEditor Win64 Development` 일반 빌드를 수행하고 에디터를 다시 실행한다.

자동화 테스트·PIE·GC·맵 전환·여러 로컬 플레이어·실제 사운드 실행은 이 작업에서 수행하지 않았다.
따라서 빌드 결과는 해당 실행 시나리오의 성공을 의미하지 않는다.

## 2026-10-13: 위젯 제거 요청 시 즉시 정리

GameFeature가 위젯을 제거할 때 팝업 스택 참조도 제거 요청 시점에 해제하도록
공통 WidgetBase와 UIManager를 보완했다. 기존 소스 3개를 수정했고 새 소스나 Manager 클래스는 추가하지 않았다.

### 제거 경로

`URPGWidgetBase::RemoveFromParent()`가 다음 순서로 처리한다.

1. 같은 위젯의 재귀적인 제거 요청을 막는다.
2. 제거 호출 중 위젯이 GC로 사라지지 않도록 일시적인 strong reference를 보유한다.
3. 드래그 상태와 마우스 캡처를 해제한다.
4. 소유 로컬 플레이어의 UIManager에서 팝업 스택 참조를 해제한다.
5. 기존 엔진의 부모·뷰포트 제거 함수를 호출한다.

UIManager의 `PushUI()`는 제거 중인 위젯을 다시 등록하지 않는다.
제거 함수가 반환한 뒤에는 기존처럼 위젯을 다시 Push할 수 있다.
BeginDestroy·이미 파괴 중인 객체·unreachable 객체는 기존 엔진 제거 경로를 유지한다.
`NativeDestruct()`의 정리도 유지해 직접적인 Slate·부모 해제 경로를 처리한다.

기존 Blueprint의 `RemoveFromParent` 함수 이름과 호출 형태는 유지했다.
이를 호출하는 기존 ClientUI·OptionsUI Feature도 동일한 공통 처리를 거친다.
개별 인벤토리·장비·스킬·던전·보상·백엔드·보스 소스와 Feature 에셋은 이 작업에서 수정하지 않았다.
콜백 중 같은 객체의 즉시 재등록은 Manager의 Push 경로에서 제한된다.
직접 AddToViewport/AddToPlayerScreen을 호출하는 별도 코드까지 차단하는 변경은 아니다.

### 빌드 기록

- Editor 대상에서 `Project_RPG`·`RPGOptionsUIRuntime`·`RPGClientUIRuntime`과 필요한 의존 모듈을 사본에서 빌드했다.
- 앞선 시도는 작업 중 갱신된 퀵슬롯 의존 파일과 테스트 코드 때문에 실패했다.
  원본에서 이미 수정된 최신 파일을 사본에 복사했으며 보호 영역의 원본 파일은 수정하지 않았다.
- 최종 사본의 증분 빌드는 3개 작업, 8.65초, 종료 코드 0으로 성공했다.
- 이 단계에서 변경한 소스 3개는 원본과 빌드 사본의 SHA256이 동일하다.
- 사본은 고정된 의존 소스를 사용하며 이후 다른 작업의 수정은 별도로 기록한다.
- 빌드 사본: `Saved/ManagerLifetimeBuild-20261012/`.
- 최종 로그: `Saved/Logs/RPGWidgetRemovalLifecycle-20261013/VerifiedBuild.log`.
- 결과·소스·에셋 대조: `Saved/Logs/RPGWidgetRemovalLifecycle-20261013/ChangesReport.json`.
- 원본 DLL을 교체하거나 에디터·PIE를 제어하지 않았다.
- 자동화 테스트·실제 UI 종료·Slate 참조 유지·GC·반복 열기 실행은 수행하지 않았다.
  빌드 성공은 해당 실행 시나리오의 성공을 의미하지 않는다.

사용자가 이전 Manager 리팩토링 후 원본 빌드와 에디터 재실행을 완료했다고 확인했다.
이 절은 그 이후 공통 위젯 제거 경로에 적용한 추가 소스 변경을 기록한다.


## 2026-10-16: 공통 Manager 직접 호출

공통 Manager 헤더·구현 8개, WidgetBase·버튼·메시지 박스·옵션 메뉴·사운드 AnimNotify·
GameInstance·OptionsUI 사운드 구현 7개, 소스 총 15개를 수정했다.
공통 UI·사운드 호출부의 Manager null 검사 16개를 제거했다.
같은 Manager를 여러 번 사용하는 옵션 복원에서는 유효한 포인터를 지역 변수로 재사용한다.

아이템·인벤토리·장비 UI·스킬·던전·보상·백엔드·보스 소스와 에셋은 이 작업에서 수정하지 않았다.
기존 Manager 4개의 소유 범위, reflected 클래스 이름과 에셋 경로를 유지했다.
실제 게임 상태는 플레이어·GameInstance·월드별 인스턴스에 보관한다.

- 백업·변경 diff·소스 SHA256: `Saved/Logs/RPGManagerDirectAccess-20261016/`.
- 격리 빌드 사본: `Saved/ManagerLifetimeBuild-20261012/`.
- 변경 소스 15개는 빌드 전 원본과 사본을 대조했다.
- 빌드 대상: Editor, `Project_RPG`·`RPGOptionsUIRuntime`·`RPGClientUIRuntime`·`RPGClientPresentationRuntime`.
- Editor 격리 빌드 성공: 24개 작업, 119.86초, 종료 코드 0.
- 빌드 결과: `BuildResult.json`·`Build.log`.
- 기존 사본의 보호 영역 의존 소스는 동결된 상태이며 원본의 병행 변경을 이번 빌드에 일괄 복사하지 않았다.
- 원본 DLL을 교체하거나 실행 중인 에디터를 제어하지 않았다.
- 자동화 테스트·PIE·실제 사운드·분할 화면·월드 종료 실행은 이번 작업에서 수행하지 않았다.

원본 실행에 적용할 때는 변경한 Manager 헤더가 포함되므로 에디터 종료 후 일반 Editor 빌드와 재실행을 권장한다.

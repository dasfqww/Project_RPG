# 공통 카메라와 Presentation 모듈 경계

## 2026-10-13: 카메라 조회와 컴포넌트 수명 정리

기존 Source의 카메라 소스 세 개를 수정했다. 새 Manager와 새 카메라 클래스는 추가하지 않았다.

### 변경한 코드

| 파일 | 변경 |
| --- | --- |
| Public/Camera/RPGUICameraManagerComponent.h | 등록·해제 override와 디버그 delegate handle 추가, 재진입 상태 초기값 명시 |
| Private/Camera/RPGUICameraManagerComponent.cpp | 정상 컨트롤러의 UI 카메라 반환, 대상 유효성·월드 검사, 디버그 연결과 해제 |
| Private/Camera/RPGPlayerCameraManager.cpp | UI 카메라 유효성 검사, 프로젝트 카메라 디버그 연결과 표시 이름 정리 |

GetComponent는 이전에 유효성 검사가 반대로 되어 있었다. null 컨트롤러를 역참조할 수 있고 정상 컨트롤러에서도 항상 null을 반환했다.
이제 컨트롤러와 카메라 매니저가 유효하고 파괴 중이 아닐 때 기존 GetUICameraComponent로 컴포넌트를 조회한다.

AHUD::OnShowDebugInfo는 전역 delegate다. 생성자에서 연결하던 처리를 OnRegister로 옮겼다.
게임 월드의 클라이언트 또는 리슨 서버에서만 연결하며 OnUnregister에서 해당 handle만 제거한다.
재등록 시 새 handle을 생성할 수 있다. 해제 시 저장한 ViewTarget 참조도 비운다.

showdebug UICamera 출력은 HUD의 컨트롤러와 해당 카메라 매니저가 일치하고 같은 월드에 있을 때만 그린다.
다른 로컬 플레이어나 다른 PIE 월드의 카메라를 같은 HUD에 출력하지 않는다.
이 출력은 마지막 요청 대상을 보여 주며 활성 UI 카메라 계산의 구현을 의미하지 않는다.

SetViewTarget은 재진입, 종료 중 월드·매니저, 파괴 중 대상 및 다른 월드의 대상을 거절한다.
null 대상은 기존 엔진의 컨트롤러 fallback에 전달한다.
ARPGPlayerCameraManager의 기존 Pawn 카메라 우선순위와 POV 계산 필드는 유지했다.

### 실제 호출 경로와 모듈 경계

Source와 GameFeature 소스에서 확인한 경로는 다음과 같다.

1. ARPGPlayerCameraManager가 UICamera를 기본 서브오브젝트로 생성한다.
2. Pawn의 URPGCameraComponent가 카메라 모드 스택을 계산한다.
3. ARPGPlayerCameraManager가 계산된 위치·회전·FOV·PostProcess를 적용한다.
4. UI 카메라의 GetComponent 또는 SetViewTarget을 호출하는 외부 C++ 소비자는 발견하지 못했다.

이 조사는 C++ 호출 경로를 대상으로 했다. Blueprint 에셋의 참조와 실제 게임에서 사용하는 카메라 클래스는 이번에 실행 확인하지 않았다.

모듈 경계는 기존 엔진 카메라 기반과 선택 가능한 연출 요청 사이로 정한다.
ARPGPlayerCameraManager·URPGCameraComponent·카메라 모드 스택 및 기존 reflected 타입은 공통 Source에 둔다.
메뉴·관전 등 실제 소비자가 생기면 해당 카메라의 선택·해제와 적용할 설정을 RPGClientPresentation의 Action/컴포넌트가 소유하도록 연결한다.
기능 종료 시 해당 기능이 요청한 카메라 상태를 해제하고 다른 기능의 요청을 보존하는 처리가 필요하다.

이번 단계에서 GameFeature에 카메라 구현을 이전하거나 새 Action을 추가하지 않았다.
NeedsToUpdateViewTarget과 UpdateViewTarget의 기존 비활성 UI 계산 경로도 유지했다.
보호 영역의 기능이나 에셋을 소비자로 임의 연결하지 않았다.

### 빌드 결과와 확인 범위

- UE 5.8.1 / Project_RPGEditor / Win64 / Development.
- 기존 사본 Saved/ManagerLifetimeBuild-20261012에서 Project_RPG와 RPGClientPresentationRuntime 컴파일·링크 성공.
- 10개 작업, 7.77초, 종료 코드 0.
- 이번 카메라 소스 세 개를 사본에 복사했으며 원본과 사본의 SHA256을 대조했다.
- 다른 의존 소스는 기존 사본의 고정된 입력을 사용한다. 최신 원본 프로젝트 전체 빌드 결과가 아니다.
- 원본 DLL 교체와 에디터·PIE 제어는 수행하지 않았다.
- 테스트 추가·실행, 실제 카메라 전환·재등록·분할 화면 디버그 출력 확인은 수행하지 않았다.
- 기존 reflected 클래스 이름과 Blueprint/맵/Experience 에셋은 변경하지 않았다.

빌드 로그와 변경·보호 영역·에셋 대조 기록: Saved/Logs/RPGCameraFoundation-20261013/.
아이템·인벤토리·장비·스킬·던전·보상·백엔드·보스 소스는 이번 작업에서 수정하지 않았다.


## 2026-10-24: ClientPresentation 소유의 선택적 카메라 요청

기존 URPGUICameraManagerComponent에 요청자별 카메라 요청을 추가하고,
RPGClientPresentation의 기존 컴포넌트가 해당 요청의 선택·해제를 소유하도록 연결했습니다.
새 Manager, 카메라 클래스, Action 클래스와 에셋은 추가하지 않았습니다.

### 공통 Source의 역할

- AddPresentationRequest(RequestOwner, CameraActor, Priority) / RemovePresentationRequest(RequestOwner)를 제공합니다.
  요청자와 CameraActor는 같은 월드에 있어야 하며, 현재 로컬 Controller의 ARPGPlayerCameraManager만 처리합니다.
  요청자와 대상은 약한 참조로 보관하므로 기능이나 월드의 수명을 늘리지 않습니다.
- 높은 우선순위를 선택하고, 같은 우선순위에서는 마지막으로 변경된 요청을 사용합니다.
  같은 요청자·대상·우선순위를 다시 전달해도 순서를 바꾸지 않습니다.
- 기존 NeedsToUpdateViewTarget은 유효한 요청이 있을 때만 활성화됩니다.
  UpdateViewTarget은 선택한 CameraActor의 CalcCamera 결과를 전체 POV에 반영합니다.
  엔진 ViewTarget, Controller의 입력 설정과 ControlRotation을 새 경로에서 직접 변경하지 않습니다.
- 요청 해제·요청자/대상 파괴·다른 월드로 이동·Manager 교체 시 유효하지 않은 요청은 계산에서 제외합니다.
  마지막 요청이 사라지면 기존 Pawn 카메라 계산 경로를 사용하므로 이전 POV를 저장해서 덮어쓰지 않습니다.
  다른 요청자가 보유한 요청은 그대로 남습니다.
- 카메라 계산 중 재진입을 막고, 계산 뒤 요청 세대·선택 대상·현재 로컬 Controller를 다시 확인합니다.
  계산 도중 기능이 해제되거나 요청이 바뀌면 그 계산 결과를 적용하지 않습니다.
- OnUnregister에서 요청을 모두 비우며 기존 SetViewTarget API의 본문은 유지했습니다.
  showdebug UICamera에는 선택된 PresentationTarget과 보관 중인 요청 수를 함께 표시합니다.

### GameFeature의 역할과 구성 방법

기존 URPGClientPresentationComponent를 Blueprint 파생 클래스로 구성할 수 있게 했습니다.

- CameraTargetActorTag: 선택적 Actor Tag. 기본값 None이면 기존 카메라 동작을 유지합니다.
- CameraPriority: 같은 플레이어의 Presentation 요청 간 우선순위입니다.
- 같은 월드에서 해당 태그를 가진 CameraActor가 정확히 하나일 때만 요청합니다.
  여러 개이면 임의로 선택하지 않고 해당 요청을 해제하며, 중복 상태 진입 시 경고를 한 번 남깁니다.
- 지정된 경우에만 0.25초 타이머를 사용하여 대상의 늦은 등장·제거, LocalPlayer 추가/제거,
  Controller/CameraManager 교체를 반영합니다.
  다른 플레이어 또는 이전 Manager의 요청을 새 Manager에서 해제하지 않습니다.
- EndPlay와 OnUnregister는 자기 요청과 자기 타이머만 정리합니다.
  이미 BeginPlay한 컴포넌트가 다시 등록되면 선택적 카메라 요청도 다시 시작합니다.
  기존 분할 화면 투표·복구 본문은 유지했으며 카메라 요청은 분할 화면 설정 여부와 독립적입니다.

Experience에서 사용하려면 URPGClientPresentationComponent의 Blueprint 파생 클래스에 태그와 우선순위를 설정하고,
UGameFeatureAction_AddComponents로 해당 클래스를 ARPGGameStateBase에 클라이언트 컴포넌트로 주입합니다.
그 ActionSet의 GameFeaturesToEnable에는 RPGClientPresentation을 지정합니다.
선택한 월드에는 같은 태그의 CameraActor를 하나 배치하고, 로컬 Controller는 ARPGPlayerCameraManager 계열을 사용해야 합니다.
기존 DA_ClientPresentation의 C++ 컴포넌트는 태그가 None이므로 이 카메라 요청을 자동으로 활성화하지 않습니다.

RPGClientPresentationRuntime은 Project_RPG에 대한 private 모듈 의존성으로 공통 카메라 API를 사용합니다.
Project_RPG에서 플러그인 모듈로의 컴파일 의존성은 추가하지 않았습니다.
카메라 계산 기반은 Source에 두고 요청의 선택·해제는 GameFeature가 맡습니다.

기존 소스/빌드 규칙 5개와 문서 2개를 변경했습니다.
Pawn 카메라·카메라 모드·PlayerController·보호 대상 시스템과 기존 맵/Experience/Blueprint 에셋은 수정하지 않았습니다.
공개 엔진 API와 변경 범위·공백 오류만 확인했습니다. C++ 빌드·에디터·PIE·게임·자동화 테스트는 실행하지 않았습니다.
새 속성과 구현을 사용하기 전 일반 C++ 빌드가 필요하며, 이번 작업을 카메라 전환의 실행 검증으로 보고하지 않습니다.
변경 전후 원본·diff·해시: Saved/Logs/RPGCameraPresentationRequests-20261024/.


## 2026-10-24: Experience에서 직접 구성하는 카메라 Action

기존 카메라 요청을 ActionSet에서 직접 설정할 수 있도록
UGameFeatureAction_RPGCameraPresentation을 RPGClientPresentationRuntime에 추가했습니다.

### 구성

1. Experience 또는 ActionSet의 Actions에 RPG Camera Presentation을 추가합니다.
2. GameFeaturesToEnable에 RPGClientPresentation을 포함합니다.
3. CameraTargetActorTag에 대상 CameraActor의 Actor Tag를 지정하고 CameraPriority를 설정합니다.
4. 해당 월드에 같은 태그의 CameraActor를 하나 배치합니다.
   로컬 Controller는 기존 조건대로 ARPGPlayerCameraManager 계열을 사용해야 합니다.

이 경로는 별도 컴포넌트 Blueprint 파생 클래스 없이 구성할 수 있습니다.
기존 AddComponents/Blueprint 컴포넌트 구성 경로도 유지합니다.
빈 태그는 에디터 데이터 검사 오류이며, 런타임에서도 경고 후 요청을 생성하지 않습니다.
카메라 태그는 GameplayTag가 아닌 AActor의 Actor Tag입니다.
태그와 우선순위는 활성화 시점의 값으로 고정해 같은 활성화에서 이후 월드에도 동일하게 적용합니다.

### 소유와 수명

- 해당 활성화 컨텍스트에 허용된 게임 월드만 처리하고 전용 서버는 제외합니다.
  기존 월드를 처리하고 이후 GameInstance 시작/월드 초기화도 관찰합니다.
- GameState가 늦게 설정되거나 교체되면 GameStateSetEvent로 현재 GameState에 연결합니다.
  월드 BeginPlay도 연결 기회로 사용하며, Action 자체에 반복 타이머는 추가하지 않습니다.
- Action/컨텍스트/월드마다 기존 URPGClientPresentationComponent의 native 인스턴스를 따로 생성합니다.
  기존 컴포넌트를 검색해서 빌리거나 덮어쓰지 않습니다.
  bDisableSplitscreen=false로 생성하므로 카메라 Action이 분할 화면 투표를 추가하지 않습니다.
- 카메라 선택·LocalPlayer/Controller/Manager 교체 대응과 요청 해제는 앞서 구현한 컴포넌트를 재사용합니다.
  다른 Action/기존 Presentation 컴포넌트의 요청은 별도로 유지됩니다.
- 컴포넌트 생성·등록 전에 소유 정보를 기록하고, 생명주기 콜백 뒤에는 현재 활성화 세대와 월드 소유를 다시 확인합니다.
  교체·해제가 재진입하면 이전 컴포넌트만 정리합니다.
- 기능 해제·월드 정리 시 먼저 해당 소유 정보를 분리한 뒤 자기 컴포넌트를 DestroyComponent합니다.
  기존 EndPlay/OnUnregister가 요청과 타이머를 해제하고 엔진이 Actor의 컴포넌트 참조를 제거합니다.
- 기능 등록 해제는 남은 컨텍스트와 delegate도 정리합니다.
  등록 해제 중의 재활성화를 차단하며, 다시 등록되면 정상 활성화를 허용합니다.

모듈의 공개 Action 헤더를 위해 GameFeatures public 의존성을 명시했습니다.
기존 Source 카메라 구현·PlayerController와 Presentation 컴포넌트 본문은 수정하지 않았습니다.
새 Action 소스 2개와 빌드 규칙 1개, 문서 2개를 변경했습니다. 새 Manager/Subsystem은 없습니다.
기존 맵·Blueprint·ActionSet·Experience 에셋과 보호 대상 구현은 수정하지 않았습니다.
새 native Action을 에디터에서 선택하려면 일반 C++ 빌드가 필요합니다.
엔진 API와 변경 범위·공백 오류를 확인했으며 빌드·에디터·게임/PIE·자동화 테스트는 실행하지 않았습니다.
변경 전후 원본·diff·해시: Saved/Logs/RPGCameraExperienceAction-20261024/.


## 2026-10-24: 카메라 Action을 연결한 개발용 에셋

전용 Action을 실제 ActionSet/Experience에 저장하고, 필요한 카메라 매니저를 개발용 Controller에 연결했습니다.
기존 공통 개발 구성을 상속/재사용하며 카메라 개발 맵은 별도로 생성했습니다.

- /Game/System/Experiences/Development/Camera/DA_CameraPresentation
- /Game/System/Experiences/Development/Camera/DA_CameraClientDevelopment
- /Game/System/Experiences/Development/Camera/BP_CameraDevelopmentController
- /Game/System/Experiences/Development/Camera/BP_CameraDevelopmentGameMode
- /Game/System/Experiences/Development/Camera/Maps/L_CameraExperienceDevelopment

ActionSet에는 RPGClientPresentation 의존성과 native 카메라 Action 하나를 지정했습니다.
CameraTargetActorTag=RPG.Development.PresentationCamera, CameraPriority=10이며 맵에 같은 태그의 CameraActor 하나를 배치했습니다.
Experience는 기존 화면 설정·옵션 단축키·안내 ActionSet 세 개와 이 ActionSet을 조합합니다.
새 Controller만 ARPGPlayerCameraManager를 지정하고 기존 입력 Config는 그대로 재사용합니다.
새 GameMode는 해당 Controller를 선택하며 기존 HUD·GameState·관전 Pawn 구성을 유지합니다.

이 태그 전용 Action은 추가 AssetBundle 에셋 없이 이미 로드된 CameraActor를 선택합니다.
에셋은 모두 기존 Development 폴더의 NeverCook 경계 안에 있고 프로젝트 쿠킹 설정은 변경하지 않았습니다.

CreateCameraExperienceDevelopment.py는 명시적인 생성 인수와 현재 native schema를 확인합니다.
다섯 허용 패키지의 누락분만 생성하고, 올바른 기존 출력은 보존합니다.
기존 공통 개발 에셋과 실제 게임 맵은 수정하지 않았습니다.
개발 실행 도구의 -CameraPresentation 옵션으로 전용 맵을 선택할 수 있습니다.

native Action 직렬화에 필요한 빌드를 수행한 뒤 별도 UnrealEditor-Cmd에서 에셋 저장과 새 Blueprint 컴파일을 완료했습니다.
이는 카메라 POV·우선순위 전환·종료 복구의 실행 검증이 아닙니다.
게임/PIE·자동화 테스트는 실행하지 않았으며 사용자 옵션 INI/JSON도 변경하지 않았습니다.
변경 전후 원본·생성 결과·빌드 로그: Saved/Logs/RPGCameraDevelopmentExperience-20261024/.


## 2026-10-24: Presentation 컴포넌트 등록 해제와 재등록 복구

임시 OnUnregister에서 카메라 요청만 해제하고 분할 화면 비활성화 투표는 EndPlay까지 보관하던 누락을 수정했습니다.

- 뷰포트 요청도 등록 수명에 맞춰 획득/해제합니다.
  OnUnregister와 EndPlay는 같은 해제 함수를 사용하고, 자기 AppliedViewport를 먼저 비워 중복 해제를 방지합니다.
  이미 BeginPlay한 컴포넌트의 OnRegister에서는 카메라와 뷰포트 요청을 다시 획득합니다.
- 한 컴포넌트는 같은 뷰포트에 한 번만 투표합니다.
  다른 월드/컴포넌트의 투표가 남아 있으면 유지하고 마지막 투표 해제 시 최초 설정으로 복구합니다.
  이미 비활성화되어 있던 분할 화면 설정과 외부에서 해제한 설정은 기존 정책대로 보존합니다.
- SetForceDisableSplitscreen은 virtual LayoutPlayers를 호출합니다.
  해제 시 기존 투표를 먼저 분리하고, 획득 시 소유를 먼저 기록한 뒤 엔진을 호출합니다.
  콜백 이후에는 map 참조를 재사용하지 않고 현재 소유와 투표 수를 다시 읽습니다.
  획득 세대를 비교하여 콜백 중 같은 뷰포트에 다시 생긴 새 요청을 이전 요청으로 해제하지 않습니다.
  설정값이 이미 목표 상태이면 엔진 호출을 생략합니다.
- 등록 해제 중 재진입은 별도 native guard로 막습니다.
  Blueprint BeginPlay 전에 종료 상태를 초기화하고, 이후 종료/등록 해제가 발생했다면 화면 설정을 다시 적용하지 않습니다.
  호출 중 명시적 GC에도 현재 요청을 처리하는 정상 컴포넌트/뷰포트의 참조를 유지합니다.
- 카메라 시작과 갱신에도 같은 등록·월드·소유자 유효성 조건을 사용합니다.
  카메라 선택·우선순위·자기 요청 제거의 본문과 기존 Action은 유지했습니다.

기존 컴포넌트 소스 2개와 문서 2개만 변경했습니다.
새 Manager/Subsystem/클래스·에셋·설정·테스트 파일은 추가하지 않았습니다.
Source 카메라/Controller와 보호 대상 시스템 및 기존 개발/게임 에셋은 수정하지 않았습니다.
엔진 호출 경로와 변경 범위·공백 오류만 확인했습니다.
C++ 빌드·에디터·게임/PIE·자동화 테스트는 실행하지 않았으며 이번 변경을 실제 재등록 동작의 검증으로 보고하지 않습니다.
변경 전후 원본·diff·해시: Saved/Logs/RPGPresentationRegistrationLifecycle-20261024/.


## 2026-10-24: 카메라 계산 실패 시 기본 시점 복귀와 후처리 연결

UI 카메라 계산 중 요청이 해제/변경되면 계산 결과를 버렸지만, 매니저는 void 호출 뒤 무조건 반환했습니다.
또한 기본 시점을 먼저 계산하고 후처리한 뒤 UI POV로 덮어써서, 카메라 효과/매니저 위치는 다른 시점을 기준으로 처리되고 있었습니다.

- 기존 void UpdateViewTarget 진입점은 유지하고 bool TryUpdateViewTarget을 추가했습니다.
  현재 요청·소유자가 유지된 계산 결과를 POV에 반영했을 때만 true를 반환합니다.
  요청이 사라지거나 바뀌면 자기 계산 결과를 반영하지 않고 false를 반환합니다.
- 매니저는 UI 시점을 먼저 계산하고, 적용되지 않았으면 같은 프레임의 기존 Pawn/엔진 경로로 진행합니다.
  UI를 시도하기 전에 기본 시점을 미리 계산하지 않으므로 기본 카메라의 한 프레임 FOV 오프셋과 모드 스택을 중복 소비하지 않습니다.
  기존 Pawn 카메라 계산 본문과 최종 엔진 fallback은 그대로 유지했습니다.
- UI 시점이 적용되면 엔진 CameraActor 경로와 같은 순서로 Modifier, 매니저 위치/회전 동기화,
  자동 직교 카메라 기준점과 렌즈 효과를 최종 UI POV에 처리합니다.
  이전처럼 폐기되는 기본 시점에 효과를 적용한 뒤 결과를 덮어쓰지 않습니다.
- 엔진의 outgoing ViewTarget 잠금 조건을 UI 계산 전에 확인합니다.
  계산/후처리 중 Controller·매니저·World가 바뀌거나 종료되면 이전 소유자의 계산을 계속하지 않습니다.
  ViewTarget 선택과 Controller의 ControlRotation을 이 경로에서 직접 변경하지 않습니다.

기존 카메라 소스 3개와 문서 2개를 변경했습니다. 새 클래스/Manager/Subsystem/에셋/테스트 파일은 없습니다.
Pawn 카메라 컴포넌트·모드 스택·Controller, GameFeature Action/Presentation 컴포넌트와 기존 에셋은 보존했습니다.
엔진 API·변경 범위·공백 오류만 확인했습니다.
이번 변경의 C++ 빌드·에디터·게임/PIE·자동화 테스트는 실행하지 않았으며,
계산 중 해제와 실제 카메라 효과/렌즈 위치의 실행 확인은 남아 있습니다.
변경 전후 원본·diff·해시: Saved/Logs/RPGPresentationCameraFallback-20261024/.

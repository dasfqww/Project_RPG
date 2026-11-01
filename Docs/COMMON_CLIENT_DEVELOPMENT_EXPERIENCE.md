# 공통 클라이언트 개발용 Experience

## 2026-10-13: 실제 HUD를 연결한 개발용 구성

기존 `DA_ClientPresentationAndOptionsHint`와 세 ActionSet은 이미 있었다.
기존 `L_ExperienceLoad`는 엔진 기본 HUD를 사용하므로 옵션 UI Action이
`ARPGHUD`를 찾지 못하고 옵션 창을 적용하지 않는 예제였다.

이번에는 기존 ActionSet들을 재사용한 개발용 Experience와 RPG HUD,
엔진 GameModeBase를 상속한 GameMode, 별도 개발 맵을 만들었다.
기능 구현을 복제하거나 새 Manager를 추가하지 않았다.

## 새 파일과 에셋

| 종류 | 경로 |
| --- | --- |
| Experience | /Game/System/Experiences/Development/DA_CommonClientDevelopment |
| HUD Blueprint | /Game/System/Experiences/Development/BP_ExperienceDevelopmentHUD |
| GameMode Blueprint | /Game/System/Experiences/Development/BP_ExperienceDevelopmentGameMode |
| 맵 | /Game/System/Experiences/Development/Maps/L_ExperienceDevelopment |
| 생성 스크립트 | Scripts/Editor/CreateExperienceDevelopment.py |
| 개발 실행 스크립트 | Scripts/Development/Open-ExperienceDevelopment.ps1 |

기존 C++ 소스, 프로젝트 설정, 기존 Experience·ActionSet·맵과 UI 에셋은 저장하지 않았다.
새 에셋은 `Content/System/Experiences/Development` 아래 네 파일이다.

## 구성과 선택

| 재사용 ActionSet | 활성화하는 GameFeature | 제공하는 기능 |
| --- | --- | --- |
| DA_ClientPresentation | RPGClientPresentation | 월드의 클라이언트 화면 설정 |
| DA_OptionsShortcut | RPGInputMapping, RPGOptionsUI | 추가 F10 매핑과 옵션 창 공급자·입력 바인딩 |
| DA_OptionsShortcutHint | RPGClientUI, RPGOptionsUI | 로컬 플레이어별 단축키 안내 |

세 ActionSet의 네 Action과 중복을 제거한 네 GameFeature를 사용한다.
Experience 자체에는 추가 Actions, GameFeaturesToEnable, DefaultPawnData가 없다.
맵의 Persistent Level에 활성 선택 액터 하나를 두고 새 Experience를 지정했다.
따라서 기존 URL → 개발용 명령줄 → 맵 액터 → 프로젝트 설정 우선순위가 적용된다.
새 전역 기본 Experience나 맵별 설정 항목은 추가하지 않았다.

HUD는 기존 `ARPGHUD`를 상속하고 OptionMenuWidgetClass에 기존
`/Game/Blueprints/UI/GameContents/Option/WBP_OptionMenu`를 지정한다.
MainMenu, Interaction, ClassSelection 클래스는 비워 두었다.
옵션 Feature는 같은 로컬 컨트롤러의 기존 옵션 창을 재사용하거나 새로 만들고
HUD 공급자 소유권과 F10 바인딩을 등록한다.

GameMode는 엔진 GameModeBase, GameState는 기존 RPGGameStateBase를 사용한다.
PlayerController와 PlayerState는 엔진 기본 클래스, Pawn은 SpectatorPawn이다.
맵에는 선택 액터·PlayerStart·엔진 기본 Cube 바닥·이동식 DirectionalLight를 배치했다.
전투 캐릭터나 던전·보상 액터는 배치하지 않았다.

공통 로딩 화면은 GameFeature 로딩 전부터 필요하므로 기존 Source 구현과
CommonLoadingScreen 설정을 사용한다. 별도의 로딩 Feature는 만들지 않았다.

## 실행 방법

일반 Editor 빌드가 반영된 상태에서 다음 스크립트를 실행한다.

```powershell
& 'C:/UE5/Project_RPG/Scripts/Development/Open-ExperienceDevelopment.ps1'
```

이 스크립트는 별도 게임 프로세스로 새 맵을 연다.
게임 시작은 이번 작업에서 실행하지 않았다.
F10으로 옵션 창을 열고 닫으며 단축키 안내와 로딩 화면을 확인할 수 있도록 구성했다.
실제 화면 표시, 포커스 복구, 네트워크 전달 결과는 아직 확인하지 않았다.

실행 명령만 보려면 `-PrintCommand`를 지정한다.

```powershell
& 'C:/UE5/Project_RPG/Scripts/Development/Open-ExperienceDevelopment.ps1' -PrintCommand
```

실행 인수는 해당 프로세스에서만 GameInstance를 엔진 기본 클래스로,
Viewport를 CommonGameViewportClient로, 로컬 플레이어를 엔진 기본 클래스로 지정한다.
PlayerInput과 InputComponent도 EnhancedInput 클래스로 지정하며 Steam을 사용하지 않는다.
프로젝트 ini 파일을 변경하지 않고 개발 맵을 직접 시작한다.
원본 프로젝트의 일반 PIE는 원래 GameInstance 설정을 사용하므로 이 실행과 같지 않다.

관전용 기본 컨트롤러는 RPGPlayerController의 키 재지정 API를 제공하지 않는다.
기존 옵션 창의 Controls 탭에서 RPG 입력 목록을 구성하는 기능은 이 맵의 범위에 포함하지 않는다.
F10 Feature 입력은 별도의 EnhancedInput 매핑과 컴포넌트를 사용한다.
그래픽·사운드 옵션은 기존 저장 서비스를 사용하므로 실제로 값을 저장하면
기존 사용자 설정 파일에 반영된다.

## 생성·보존 방식

별도 명령줄 에디터에서 다음 명령으로 생성한다.

```powershell
& 'D:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/UE5/Project_RPG/Project_RPG.uproject' '-run=PythonScript' '-Script=C:/UE5/Project_RPG/Scripts/Editor/CreateExperienceDevelopment.py' '-EnablePlugins=PythonScriptPlugin' -unattended -nop4 -nosplash -NullRHI -nosound -nosteam -NoLiveCoding -multiprocess
```

생성 전 기존 ActionSet의 기능 목록과 Action 개수, 의존 에셋을 확인한다.
없는 개발용 에셋만 생성·저장하고 새 Blueprint를 컴파일한다.
이미 있는 개발용 에셋은 구성 확인 후 보존하며 다른 구성일 때 오류를 반환한다.
기존 프로젝트 에셋을 자동 교정하거나 재저장하지 않는다.

## 완료 근거와 남은 확인

UE 5.8.1 원본 프로젝트의 별도 Python commandlet 실행을 종료 코드 0으로 마쳤다.
새 Blueprint 두 개는 경고 없는 컴파일 상태를 확인했다.
최종 실행은 먼저 저장된 Experience·HUD를 다시 읽어 확인하고,
GameMode·맵을 생성하여 `RPG_EXPERIENCE_DEVELOPMENT ASSETS_READY`를 기록했다.
생성 도중 UClass 비교와 Blueprint 부모 클래스 조회 API를 수정했으며
실패 실행 기록도 남겨 두었다.

실행 스크립트의 PowerShell 문법과 `-PrintCommand` 출력을 확인했다.
이 단계에는 C++ 변경·재빌드·PIE·게임 실행·자동화 테스트·멀티플레이·쿠킹 실행이 없다.
현재 AssetManager의 Experience 스캔 규칙은 AlwaysCook이므로
새 개발용 Experience에도 같은 규칙이 적용된다. 개발 에셋의 배포 포함 정책은
후속 패키징 단계에서 정리해야 한다. 이 단계에서는 쿠킹 설정을 바꾸지 않았다.

작업 전 소스·설정·관련 에셋 등 1,538개 파일과 원본 DLL 8개의 SHA256을 기록했다.
작업 후 비교 결과는 아래 기록에 남긴다.

- 생성 로그: Saved/Logs/RPGExperienceDevelopment-20261013/CreateAssets.log
- 실행 결과: Saved/Logs/RPGExperienceDevelopment-20261013/AssetCreationResult.json
- 변경 범위와 파일 해시: Saved/Logs/RPGExperienceDevelopment-20261013/ChangesReport.json


## 2026-10-14: 공통 UI 실행 확인 완료

별도 사본 게임 프로세스에서 이 맵의 로딩 화면·F10 안내·옵션 본문 렌더링을 확인했다.
F10 열기·닫기 두 회와 커서·입력 모드·실제 Slate 포커스 복구가 통과했다.
종료 시 네 Action과 UI·입력 매핑의 소유권 해제도 확인했다.
검증용 키 이벤트는 엔진 뷰포트 입력 경로에 주입했다.
물리 키보드 입력, 옵션 값의 적용·저장과 네트워크 동작은 아직 확인하지 않았다.

[실행 기록과 캡처](EXPERIENCE_CLIENT_UI_RUNTIME_VERIFICATION.md)를 참고한다.


## 2026-10-16: 개발용 콘텐츠의 쿠킹 경계

DefaultGame.ini의 Packaging 제외 폴더에 Experiences/Development와 Experiences/Examples를 추가했다.
개발 전용 Experience·HUD·GameMode·맵과 예제 콘텐츠를 일반 일괄 쿠킹에서 제외하도록 설정했다.
공유 ActionSet·GameFeature·재사용 Experience의 AssetManager 규칙은 유지했다.

Editor·PIE와 기존 별도 개발 실행은 계속 사용할 수 있다.
C++ 변경과 추가 빌드는 없으며 실제 쿠킹·패키징은 아직 실행하지 않았다.

[적용 경로·번들 관리 참조와의 관계·확인 범위](EXPERIENCE_DEVELOPMENT_COOK_POLICY.md)를 참고한다.



## 2026-10-24: 키 지정 옵션을 연결한 개발용 입력 구성

개발용 GameMode의 Controller를 기존 엔진 기본 클래스에서
BP_ExperienceDevelopmentInputController(ARPGPlayerController 상속)로 연결했습니다.
이 Controller는 개발 전용 빈 InputConfig/MappingContext를 사용하므로 기존 캐릭터·전투 입력 에셋을 수정하지 않습니다.
InputConfig와 Context는 Controller가 기존 코드로 플레이어별 사본을 생성할 수 있게 제공합니다.
관전 폰은 유지하며 이동은 엔진 관전 조작을 사용합니다.

공유 DA_OptionsShortcut의 IMC_OptionsShortcut 등록에 bUsePlayerOwnedCopy=true를 저장했습니다.
기존 F10·우선순위·InputAction·원본 MappingContext는 유지합니다.
이를 사용하는 Experience에서는 추가 입력을 플레이어 사본에서 변경·개별/일괄 복원·저장할 수 있는 구성이 됩니다.
최신 C++ 변경을 빌드한 뒤 해당 옵션 화면을 사용해야 합니다.

새 개발 에셋:
- /Game/System/Experiences/Development/IMC_ExperienceDevelopment
- /Game/System/Experiences/Development/DA_ExperienceDevelopmentInput
- /Game/System/Experiences/Development/BP_ExperienceDevelopmentInputController

기존 개발 맵·Experience·HUD는 재저장하지 않고 GameMode의 Controller 기본값만 갱신했습니다.
프로젝트 기본 Experience와 일반 게임 맵 설정은 변경하지 않았습니다.
개발용 실행 스크립트는 URPGInputComponent가 필요한 Controller의 기존 CastChecked 조건에 맞춰
해당 프로세스의 DefaultInputComponentClass를 /Script/Project_RPG.RPGInputComponent로 지정합니다.

Scripts/Editor/ConfigureExperienceDevelopmentInput.py는 기존 구성과 반영 가능한 reflected property를 먼저 확인합니다.
명시적인 -RPGConfigureDevelopmentInput 인수가 있어야 다섯 허용 패키지만 저장합니다.
기존 기본 생성 도구도 이미 연결된 개발용 Controller를 확인하고 재사용합니다.

별도 UnrealEditor-Cmd의 Python asset serialization과 필요한 Blueprint 컴파일로 적용했습니다.
C++ 빌드·게임 실행·PIE·키 변경/INI 저장의 실행 검증은 수행하지 않았습니다.
에셋 적용 프로세스에는 -nowrite -multiprocess를 사용해 INI 쓰기를 막았습니다.
구현 적용 기록: Saved/Logs/RPGExperienceInputAssets-20261024/.


## 2026-10-24: 분리된 카메라 개발용 Experience

공통 개발 맵을 유지하면서 카메라 Action을 조합한 개발용 구성 다섯 개를 추가했습니다.
모두 /Game/System/Experiences/Development/Camera 아래에 있으며 기존 Development 폴더의 NeverCook 경계를 따릅니다.

| 에셋 | 역할 |
| --- | --- |
| DA_CameraPresentation | RPG Camera Presentation Action 한 개, Actor Tag와 우선순위 지정 |
| DA_CameraClientDevelopment | 기존 공통 ActionSet 세 개와 카메라 ActionSet 조합 |
| BP_CameraDevelopmentController | 기존 개발용 입력 Controller 상속, RPGPlayerCameraManager 지정 |
| BP_CameraDevelopmentGameMode | 기존 개발 GameMode 상속, 카메라 Controller 연결 |
| Maps/L_CameraExperienceDevelopment | 전용 Experience 선택 액터와 태그가 일치하는 CameraActor 배치 |

카메라의 Actor Tag는 RPG.Development.PresentationCamera, 우선순위는 10입니다.
기존 개발용 빈 InputConfig를 재사용하고 HUD·관전 Pawn·옵션/F10 구성도 상속/재사용합니다.
기존 공통 맵·Experience·GameMode·Controller·공유 ActionSet은 재저장하지 않았습니다.

실행할 때 카메라 구성을 명시적으로 선택합니다.

```powershell
& .\Scripts\Development\Open-ExperienceDevelopment.ps1 -CameraPresentation
```

기존 인수 없는 실행은 공통 개발 맵을 계속 사용합니다.
-PrintCommand는 선택한 실행 명령만 표시합니다. 이번 작업에서는 게임을 실행하지 않았습니다.

Scripts/Editor/CreateCameraExperienceDevelopment.py는 명시적인 -RPGCreateCameraDevelopment 인수가 필요합니다.
native Action과 공유 구성/부모 클래스를 먼저 확인한 뒤 다섯 허용 패키지 중 없는 것만 생성합니다.
이미 올바른 에셋은 확인 후 보존하고, 예상 밖의 기존 구성을 자동으로 덮어쓰지 않습니다.

새 native Action을 에셋에 저장하는 데 필요한 모듈 빌드를 수행했습니다.
UBT가 공용 PCH와 의존 모듈도 다시 빌드했습니다.
별도 UnrealEditor-Cmd로 에셋을 저장하고 새 Blueprint 두 개를 컴파일했습니다.
INI 쓰기는 -nowrite -multiprocess로 막았습니다.
게임/PIE·카메라 화면 전환·옵션 조작·자동화 테스트·쿠킹/패키징은 실행하지 않았습니다.
생성 결과와 변경 기록: Saved/Logs/RPGCameraDevelopmentExperience-20261024/.

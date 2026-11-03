# 옵션 UI 입력과 포커스 소유

## 2026-11-04: 그래픽·사운드의 적용 결과와 로컬 저장 확정값

그래픽·사운드는 로컬 설정이다. 기존 `graphic_options.json`/`sound_options.json`과
백업 형식을 유지하며, 유효한 JSON을 옵션 복원의 기준으로 삼는다. JSON이 없거나
백업까지 읽을 수 없으면 기존 GameUserSettings INI 값을 사용한다. 서버 동기화와
키 설정 목록/매핑 정책은 이 변경 범위가 아니다.

- GameUserSettings는 현재 적용값과 저장 확정 스냅샷을 구분한다. INI를 읽은 값이
  최초 확정값이고, JSON을 성공적으로 복원하거나 저장한 뒤에만 해당 분야의 확정값을 갱신한다.
  사운드 슬라이더·음소거·기본값은 즉시 적용만 하며 저장 버튼으로 확정한다.
- 공용 `SaveSettings`는 확정된 화면/사운드 값으로 INI를 기록한 뒤 현재 적용값을 복구한다.
  다른 설정 저장이 시험 중인 음량이나 JSON 저장 실패 뒤의 화면 값을 저장하지 않는다.
  저장 콜백 중 사운드 적용/추가 확정을 차단하고 기존 화면 미리보기 취소 처리를 유지한다.
- SoundManager는 전체·배경음·효과음의 SoundClass/SoundMix 연결을 모두 검사한 뒤
  현재 값을 변경하고 믹스 명령을 보낸다. 누락은 적용 실패이며 성공 알림을 보내지 않는다.
  이는 필수 연결과 명령 제출의 확인이다. 개별 음원 에셋의 채널 분류나 실제 청취 결과를
  자동으로 보장하지 않으며, 에셋 연결·재생 검증은 별도로 수행한다.
- 네이티브 사운드 메뉴는 결과를 반환하는 값 기반 적용 API를 사용한다. 요청이 거절되면
  오류 문구를 표시하고 컨트롤을 현재 값으로 되돌린다. 다음 적용 성공 시 오류를 지운다.
  기존 C++ 호환용 void setter는 유지한다.
- JSON 저장 실패 시 적용된 값은 세션에 남고 오류를 표시한다. 확정 스냅샷은 이전 값이므로
  후속 INI 저장에도 실패한 변경이 섞이지 않는다. JSON 저장 성공 뒤 INI는 호환용 미러이며,
  엔진 `SaveSettings`의 반환값 없는 API로 INI 디스크 쓰기 성공까지 보장하지는 않는다.

화면 변경의 15초 확인/수명 종료 복구, 임시 JSON 쓰기와 백업 복원, 요청 중 소유자·값 재확인은
유지한다. 직접 회귀 검사는 별도 INI 경로와 사설 런타임을 사용하며, 고정 JSON 파일명에 쓰는
통합 사례는 `-RPGOptionsIsolatedTests`가 있는 전용 실행에서만 수행한다.

## 2026-10-13: 옵션 입력 구현을 RPGOptionsUIRuntime으로 이전

HUD의 옵션 Toggle 경로에서 직접 실행하던 입력 모드·커서 변경을 옵션 GameFeature 모듈로 옮겼다.
URPGOptionMenu가 열려 있는 동안의 상태를 보관하고 모듈 구현에 적용·해제를 요청한다.
기존 reflected 클래스, Blueprint 함수와 BindWidget 필드, 설정 데이터 형식은 유지했다.

### 변경 범위

기존 소스 5개:

- Source/Project_RPG/Public/UI/Option/RPGOptionMenu.h
- Source/Project_RPG/Private/UI/Option/RPGOptionMenu.cpp
- Source/Project_RPG/Private/UI/HUD/RPGHUD.cpp
- Plugins/GameFeatures/RPGOptionsUI/Source/RPGOptionsUIRuntime/Private/RPGOptionsUIRuntimeModule.cpp
- Plugins/GameFeatures/RPGOptionsUI/Source/RPGOptionsUIRuntime/RPGOptionsUIRuntime.Build.cs

새 계약·연결·구현 소스 4개:

- Source/Project_RPG/Public/UI/Option/RPGOptionsMenuRuntime.h
- Source/Project_RPG/Private/UI/Option/RPGOptionsMenuRuntime.cpp
- Plugins/GameFeatures/RPGOptionsUI/Source/RPGOptionsUIRuntime/Private/RPGOptionsMenuRuntime.h
- Plugins/GameFeatures/RPGOptionsUI/Source/RPGOptionsUIRuntime/Private/RPGOptionsMenuRuntime.cpp

새 Manager 또는 Subsystem은 추가하지 않았다.
프로젝트 모듈은 플러그인에 대한 Build.cs 의존성을 추가하지 않고 기존 ModularFeature 연결 방식으로 구현을 조회한다.
OptionsUI 모듈에는 GetDesiredFocusTarget 호출에 필요한 CommonUI 직접 링크 의존성을 추가했다.

### 창을 열 때

1. 로컬 컨트롤러, LocalPlayer, 월드, 뷰포트와 화면에 있는 옵션 위젯을 확인한다.
2. 위젯의 원래 IsFocusable 값을 보관하고 요청한 포커스를 받을 수 있게 한다.
3. 기존 GetDesiredFocusTarget의 Blueprint 지정 값을 조회한다. 닫기·제거가 재진입하면 세션 ID와 적용 여부로 요청을 중단한다.
4. 옵션 위젯 안에 있고 사용 가능한 포커스 대상이면 사용하며, 없으면 옵션 루트를 사용한다.
5. 현재 포커스·대기 중 Slate 포커스 요청·커서 표시·컨트롤러의 포커스 변경 시 키 flush 정책을 보관한다.
6. 같은 뷰포트에서 처음 열린 옵션 창이 CaptureMode·LockMode·IgnoreInput·HideCursorDuringCapture를 보관한다.
7. GameAndUI, DoNotLock, HideCursorDuringCapture=false를 적용하고 커서를 표시한다.

중복 표시 요청은 활성 세션의 이전 상태를 다시 저장하지 않는다.
입력 적용 전에 취소된 세션은 실제 입력 설정을 복구하는 동작 없이 자기 상태만 정리한다.
GetCurrentInputModeDebugString은 디버그 표시 이름 보관에만 조건부 사용한다.
Shipping에서도 복구 판단은 실제 공개 설정과 포커스를 사용한다.

### 창을 닫거나 제거할 때

아래 경로가 동일한 EndInputSession으로 연결된다.

- HUD HideOptionMenu / ToggleOptionMenu의 닫기.
- 옵션 위젯의 Hidden 또는 Collapsed 변경.
- 옵션 위젯의 RemoveFromParent 호출.
- NativeDestruct의 보조 정리.
- 기존 Feature 컴포넌트의 EndPlay에서 HUD 제공 창을 제거하는 경로.

먼저 세션을 비워 중복 해제와 콜백 재진입을 막는다.
위젯의 원래 IsFocusable 값도 돌려 놓는다.
컨트롤러·LocalPlayer·Slate user·월드·뷰포트가 이전 요청과 일치하는지 확인한다.
월드 종료, 컨트롤러 파괴·교체, GC 중에는 새 월드의 입력을 변경하지 않는다.

옵션의 포커스 경로 또는 자기 대기 포커스 요청이 유지되고, 적용한 공개 입력 설정이 일치할 때 이전 상태를 복구한다.
다른 창의 새 포커스 요청, 다른 창의 현재 포커스, 다른 입력 설정이 있으면 해당 상태를 보존한다.
필요하면 제거된 옵션을 향하는 자기 Slate 포커스 요청만 지운다.

복구 대상 포커스는 약한 참조이며 현재 보이는 Slate 경로에 있고 활성화된 대상인지 확인한다.
게임 뷰포트의 기존 상시 캡처 모드라면 뷰포트의 고정밀 마우스 캡처도 다시 요청한다.
이 구현은 이전의 임의 드래그 동작이나 다른 위젯의 일시적인 마우스 캡처 제스처를 복원하지 않는다.

### 공유 뷰포트와 구현 수명

옵션 위젯들이 같은 뷰포트를 사용할 때 처음 상태를 공유한다.
각 위젯은 자기 컨트롤러의 커서·포커스·flush 값을 따로 보관한다.
다른 옵션 창이 남아 있으면 현재 뷰포트의 설정을 유지하고, 마지막 창이 끝날 때 복구를 시도한다.
다른 로컬 플레이어의 UI 포커스가 있으면 공유 뷰포트 설정 복구를 생략한다.

공유 상태는 위젯들이 보유하는 native 값이며 UObject와 Slate 위젯 참조는 약한 참조다.
모듈의 조회 맵도 약한 참조만 보관한다. 루팅·전역 Manager·구현 포인터 보관은 추가하지 않았다.
모듈 시작·종료 시 기존 그래픽·사운드 구현과 함께 옵션 입력 구현을 등록·해제한다.
종료 처리의 조회는 모듈을 새로 로드하지 않는다.

Experience 없는 기존 맵은 표시 요청에서 OptionsUIRuntime 코드를 지연 로드할 수 있다.
이 조회는 GameFeature Action을 활성화하지 않는다.
살아 있는 HUD에서 Feature가 해제되면 기존 기본 창 복구 경로가 새 입력 세션을 사용한다.

### HUD와 보호 영역

기본 옵션 창은 소유 컨트롤러로 생성하고 AddToPlayerScreen(10)으로 배치한다.
표시 실패, 잘못된 소유자, 종료 중 월드에서는 입력을 적용하지 않는다.
외부 제거 후 다시 표시할 때 화면 등록을 다시 확인한다.
Provider 교체·해제 시 기존 입력 세션을 끝낸 뒤 창을 제거한다.

HUD의 메인 메뉴·상호작용·클래스 선택 처리와 옵션 탭·저장 로직은 기존 내용을 유지했다.
아이템·인벤토리·장비·스킬·던전·보상·백엔드·보스 원본 소스는 이 작업에서 수정하지 않았다.
스킬 창 등 다른 기능의 입력 처리를 새로운 계약으로 자동 이전하지 않았다.

복구 판단은 현재 공개 설정과 Slate 포커스에 근거한다.
다른 코드가 포커스와 공개 설정을 모두 동일하게 유지하면서 입력을 변경하는 경우에는 새로운 소유권을 식별할 수 없다.
임의의 사용자 정의 FInputModeDataBase 객체 전체나 CommonUI ActionRouter의 내부 상태를 복제하는 구현은 아니다.

### 빌드와 실행 확인 범위

- UE 5.8.1 / Project_RPGEditor / Win64 / Development.
- 기존 사본 Saved/ManagerLifetimeBuild-20261012에서 Project_RPG·RPGOptionsUIRuntime·RPGClientUIRuntime 대상 빌드 성공.
- 첫 빌드는 Visibility 매개변수의 기존 위젯 멤버 이름 가림 때문에 실패했다.
- 두 번째는 C++ 컴파일과 Core DLL 링크를 통과했지만 CommonUI 직접 링크 의존성 누락으로 OptionsUI 링크에 실패했다.
- 수정 후 최종 증분 빌드는 OptionsUI 10개 작업, 14.36초, 종료 코드 0이다. Core·ClientUI는 해당 최종 그래프에서 최신 상태다.
- 변경한 소스 9개는 원본과 사본의 SHA256을 대조했다. 다른 의존 소스는 기존 사본의 고정된 입력을 사용한다.
- 원본 DLL 교체, 에디터·PIE 제어, 테스트 추가·실행은 수행하지 않았다.
- 실제 창 표시·포커스 복구·공유 뷰포트·Feature 해제·패키징 실행은 아직 확인하지 않았다.

기록·백업·빌드 로그·소스/에셋 대조: Saved/Logs/RPGOptionsInputOwnership-20261013/.


## 2026-10-16: 옵션 UI Action의 월드 수명 관리

`UGameFeatureAction_RPGOptionsUI`의 기반을 엔진 `UGameFeatureAction`으로 바꾸고
옵션 기능의 활성화·월드 추적·해제를 RPGOptionsUIRuntime 안으로 옮겼다.
공통 게임플레이 Action 기반의 구현과 이를 사용하는 능력·입력·기존 UI Action은 수정하지 않았다.
OptionsUI 모듈은 기존 HUD·위젯 계약을 위해 Project_RPG에 계속 의존한다.

### 소유와 정리

- 활성화 시 GameInstance 시작·월드 초기화·월드 종료 이벤트를 구독한다.
- 현재 월드와 나중에 생성되는 월드에서 해당 활성화 컨텍스트의 게임 월드만 처리한다.
- 전용 서버와 종료 중인 월드는 제외한다.
- HUD별 요청을 기록해 같은 컨텍스트·월드·HUD의 중복 등록을 막는다.
- 공유 OptionsMenuComponent에는 요청마다 참조 하나를 추가하고 해제한다.
- 등록 당시의 ToggleInputAction을 HUD별로 기록해 같은 단축키 요청을 반환한다.
  종료 시점의 Action 필드를 다시 읽어 다른 단축키를 해제하는 경로를 제거했다.
- 월드 종료 시 그 월드의 Actor 생성 이벤트와 HUD 요청을 해제한다.
- Feature 비활성화 시 해당 컨텍스트를 먼저 제거하고 이벤트 구독·월드별 요청을 정리한다.
  월드 종료와 비활성화가 연속으로 와도 같은 요청을 다시 반환하지 않는다.
- 활성화마다 세대 번호를 부여한다. 종료·재활성화 후 이전 콜백과 기존 순회는 새 요청을 추가하지 않는다.
- 월드·HUD 목록은 약한 참조로 복사한 뒤 처리해 위젯 생성 콜백의 종료·재진입에 대응한다.
- 위젯 생성과 컴포넌트 등록 전에 정리 소유권을 기록한다.

기존 옵션 위젯의 생성·재사용, 입력·포커스 복구, 단축키 바인딩과
살아 있는 같은 월드에서의 기본 옵션 창 복구는 기존 HUD·OptionsMenuComponent가 처리한다.
Action 클래스 경로와 ToggleInputAction 필드 이름, Client AssetBundle 경로는 유지했다.
기존 에셋을 저장하거나 새 Manager를 추가하지 않았다.

### 빌드와 확인 범위

- 변경 소스: OptionsUI Action 헤더·구현 2개.
- Editor 격리 빌드: RPGOptionsUIRuntime·Project_RPGEditor 대상, 4개 작업, 29.72초, 종료 코드 0.
- 변경 소스 2개는 원본과 빌드 사본의 SHA256을 대조했다.
- 사본의 다른 의존 소스는 동결된 상태이며 최신 원본 전체 빌드 결과는 아니다.
- 기록: `Saved/Logs/RPGOptionsActionLifecycle-20261016/`.
- 원본 DLL 교체·에디터 제어·자동화 테스트·PIE·실제 맵 이동·에셋 로드 확인은 수행하지 않았다.

native 상속 구조가 변경되었으므로 원본 적용 시 에디터 종료 후 일반 Editor 빌드와 재실행이 필요하다.


## 2026-10-16: 키 지정 옵션의 바인딩별 변경

### 문제와 소유

기존 목록·키 선택기·ApplyKeyMapping·GameUserSettings 연결은 구현되어 있었다.
그러나 UnmapAllKeysFromAction 이후 MapKey를 호출하여 이동 방향·게임패드 매핑과
각 매핑의 Modifier·Trigger를 잃을 수 있었고, 적용 실패도 UI에 성공처럼 표시했다.

| 영역 | 책임 |
| --- | --- |
| Source의 IRPGInputOptionsRuntime | 목록·적용 정책 계약과 기존 모듈의 지연 조회 |
| RPGOptionsUIRuntime의 FRPGInputOptionsRuntime | 표시 행 구성, 키 유형·장치·충돌 검사 |
| ARPGPlayerController | 원본 InputConfig 보관, 소유한 런타임 IMC의 Key 변경·재구성 |
| URPGGameUserSettings | InputTag·DefaultKey·Key 저장과 기존 설정 호환 |
| InputOptionMenu·InputMappingRow | 실제 키 표시, 선택·오류·취소·기본값 복원 |

정책 객체는 기존 모듈 멤버이며 별도 Manager나 상태 저장소를 추가하지 않았다.
브리지의 코드 모듈 조회는 GameFeature Action을 활성화하지 않는다.
기존 모듈 등록·종료 경로에 정책을 연결했고 InputCore·GameplayTags 직접 의존성을 명시했다.

### 목록과 실제 적용

- 런타임에는 소유 컨트롤러의 원본 InputConfig를 사용한다. 위젯의 InputConfig는 디자이너 미리보기용이다.
- 표시 대상으로 설정된 Native·Ability 액션의 각 원본 키를 별도 행으로 만든다.
- 같은 Action·원본 키의 별칭은 한 행으로 합친다. Move [W]·Move [A]처럼 원본 키를 표시한다.
- 아날로그 축은 읽기 전용이다. 버튼은 같은 키보드/마우스 또는 게임패드 범위 안에서 변경한다.
- 원본 Action·DefaultKey로 대상 인덱스를 찾고 런타임 매핑의 Key만 수정한다.
  같은 Action·원본 키가 여러 번 있으면 해당 매핑들을 함께 변경한다.
- 컨트롤러가 소유한 런타임 Config·IMC인지 검사하며 공유 에셋은 수정하지 않는다.
- 기존 스킬 입력 해제·컨텍스트 재구성·스킬 창 닫기 키 갱신 연결을 보존했다.
- 콜백 이후 소유자·월드·매핑을 재확인하고 키 변경 중 재진입을 거절한다.
- 즉시 입력을 재구성하고 누르고 있던 키는 놓을 때까지 무시한다.

### 충돌·선택·기본값

1. 키 선택기를 선택한 뒤 단일 키를 입력한다. Ctrl+키 등의 조합은 오류로 거절한다.
2. Esc는 선택을 취소한다. Esc를 다른 명령에 새로 지정할 수는 없다.
3. 자기 런타임 기본 IMC의 다른 바인딩과 충돌하면 거절한다. 같은 이동 액션의 다른 방향도 포함한다.
4. EnhancedPlayerInput의 공개된 현재 유효 매핑 목록에서 다른 Action과의 충돌도 검사한다.
   비활성·가려진 모든 IMC를 원본 목록으로 전수 검사하는 정책은 아니다.
5. 실패하면 실제 현재 키·아이콘으로 복구하고 오류를 툴팁에 표시한다.
   MappingErrorText를 Blueprint에 선택적으로 연결하면 행 안에서도 표시된다.
6. 선택 중이 아닐 때 행 내부에 포커스를 두고 Backspace를 누르면 해당 원본 기본키로 복원한다.
   ResetToDefault도 노출했다. 기본키 복원도 같은 충돌 검사를 거친다.
7. 성공하면 다른 행의 표시도 갱신한다. 알려진 아이콘이 없으면 키 텍스트를 유지하고 이미지를 숨긴다.

생성·종료 시 선택기와 행 이벤트를 구독·해제한다.
프로그램의 SetSelectedKey 갱신이 다시 저장을 일으키지 않도록 막았다.
기존 에셋에는 MappingErrorText가 없을 수 있으므로 툴팁으로 오류를 제공한다.
별도 기본키 정보가 없는 기존 스킬 창 대체 행에는 기본값 복원이 제공되지 않는다.

### 저장과 복원

DefaultKey가 유효한 레코드는 InputTag와 원본 기본키로 개별 바인딩을 식별한다.
DefaultKey가 없는 기존 태그별 레코드는 주 바인딩 변경으로 읽어 호환한다.
복원 시 기존 태그별 레코드를 먼저, 바인딩별 레코드를 나중에 적용하며 기존 퀵슬롯 별칭 우선순위를 유지한다.
성공한 변경은 기존 GameUserSettings에 저장하고, 복원 중에는 다시 저장하지 않는다.
키 변경 때문에 그래픽·오디오 ApplySettings를 다시 실행하지 않는다.
런타임 IMC는 컨트롤러별이지만 저장 설정은 기존 프로세스 공통 환경설정이다. 사용자별 프로필 저장은 추가하지 않았다.

### 적용 범위와 남은 확인

ARPGPlayerController·InputConfig와 기존 RowWidgetClass·BindWidget 설정이 필요하다.
엔진 기본 PlayerController를 사용하는 공통 개발 맵에서 키 탭까지 자동 구성한 것은 아니다.
추가 GameFeature IMC는 충돌 검사에서 조회하며, 그 IMC 자체는 수정하지 않는다.
F10 등의 모든 Feature 명령을 이 목록에 자동 노출하지 않는다.
아이템·인벤토리·장비·스킬·던전·보상·백엔드·보스 원본의 별도 구현은 이번 키 옵션 작업 대상에 포함하지 않았다.

### 소스와 빌드 기록

- 키 옵션 소스 14개: 기존 10개 수정, Source 계약·브리지와 플러그인 정책 4개 추가.
- 최초 빌드는 보호된 엔진 API 접근에서 실패했다. 공개 GetEnhancedActionMappingsView로 수정했다.
- 다음 빌드는 InputCore 링크 의존성 누락으로 실패했다. 직접 의존성을 추가했다.
- 최종 격리 Editor 빌드: UE 5.8.1, Win64 Development, 6개 작업, 17.00초, 종료 코드 0.
- 결과: Saved/Logs/RPGInputKeyOptions-20261016/VerifiedBuildResult.json.
- 14개 중 12개 소스는 원본·사본 SHA256이 일치한다.
  컨트롤러 헤더·구현은 동결된 사본과 원본의 기존 코드가 달라 키 변경 부분만 각각 반영했다.
  사본 빌드는 최신 원본 전체의 통합 빌드를 대신하지 않는다.
- 실제 키 입력·다시 열기·재실행 복원·Blueprint 로드·PIE·자동화 테스트는 실행하지 않았다.
- 원본 DLL을 교체하거나 에디터를 제어하지 않았다.
- 변경 전후 백업·diff·해시·병행 변경 목록은 같은 기록 폴더에 보관했다.

원본에 적용하려면 에디터를 종료한 상태로 일반 Project_RPGEditor 빌드를 수행한 뒤 재실행한다.
네이티브 함수·설정 구조체 필드가 추가되어 Live Coding만으로 적용하는 단계는 아니다.

## 2026-10-16: 공통 위젯·입력 Action 정리

ClientUI·InputMapping에도 활성화 세대와 종료 전 소유 기록 제거를 적용했다.
ClientUI의 생성 재진입·일괄 제거, InputMapping의 갱신 재진입·동기 엔진 콜백·컨트롤러 교체를 처리한다.
요청한 위젯 ID와 실제 등록한 매핑만 해제하며 다른 기능의 요청을 일괄 삭제하지 않는다.
소스 6개, 격리 빌드 11개 작업·32.93초·종료 코드 0이다.
상세 백업·해시·diff: Saved/Logs/RPGClientInputActionLifecycle-20261016/.
이번 변경의 실제 활성화·해제·이동 동작은 실행하지 않았다.


## 2026-10-17: 실제 옵션 위젯의 네이티브 연결과 저장 완료

원본 C++ 빌드와 에디터 재실행은 앞서 완료된 상태다.
이번 단계는 옵션 위젯 두 개의 에셋 연결을 수정했다.

| 에셋 | 변경 |
| --- | --- |
| WBP_InputMappingRow | Image_490을 KeyIconImage로 바꾸고 변수로 노출하여 기존 C++ 아이콘 갱신에 연결했다. Visibility는 HitTestInvisible로 바꿔 아래 KeySelector의 마우스 입력을 가로막지 않도록 했다. |
| WBP_OptionMenu | 자식 WBP_InputOptionMenu의 이름을 InputOptionMenu로 바꾸고 변수로 노출했다. 기존 OnMenuShown의 RefreshMappings 호출에 연결된다. |

기존 레이아웃·부모 패널·순서·스타일과 행 클래스·InputConfig·KeyIconConfig 참조를 유지했다.
새 위젯을 추가하지 않고 기존 위젯을 네이티브 BindWidgetOptional 필드에 맞췄다.
키 목록 구성 정책·입력 매핑·아이템·스킬·던전·보상·백엔드·보스 구현은 수정하지 않았다.
원본 InputConfig에서 Move·Look의 기존 비표시 설정도 유지했다.
방향별 매핑 행 구성은 표시 대상으로 설정된 액션에 적용된다.

전날 저장은 별도 UnrealEditor-Cmd 프로세스의 파일 잠금(Windows 오류 32)으로 실패했다.
이전 백업과 저장 대기 패키지는 보존했다.
이번에는 잠금 프로세스가 종료된 상태에서 새 에디터에 두 변경을 다시 적용했다.
작업 중 PIE가 시작된 경우에는 편집하지 않고 종료를 기다렸다. PIE나 다른 프로세스를 종료하지 않았다.

두 Blueprint 컴파일과 원본 에셋 저장을 완료했다.
컴파일 후 위젯 트리에서 두 네이티브 연결과 아이콘 Visibility를 읽어 확인했고 저장 후 두 에셋의 Dirty 상태는 false다.
이 단계에서 C++ 소스 변경·재빌드·에디터 재실행·테스트 추가 및 실행은 하지 않았다.
실제 마우스·키 입력, 변경 저장 후 게임 재실행 복원은 이번 확인 범위에 포함하지 않는다.

기록: Saved/Logs/RPGInputOptionWidgetBindings-20261017/.
전후 에셋 사본·SHA256·위젯 트리·저장 결과는 ChangesReport.json에 있다.
이전 저장 실패 기록: Saved/Logs/RPGInputOptionWidgetBindings-20261016/.
이번 에셋 변경에는 추가 C++ 빌드가 필요하지 않다.


## 2026-10-17: 탭 클릭·키 선택 표시·재개방 포커스 보완

옵션 위젯 연결 이후 C++ UI 처리에서 남은 세 부분을 수정했다.

- 그래픽·사운드·입력 버튼의 OnClicked를 각 탭 전용 함수에 연결했다.
  네이티브 클릭 경로는 키보드 포커스로 클릭 대상을 추정하지 않는다.
  기존 OnTabButtonClicked 진입점은 호환용으로 유지하고 hovered·소유 플레이어 포커스를 순서대로 사용한다.
- 버튼 목록을 다시 연결할 때 기존 구독과 맵을 먼저 정리한다. 종료 시에도 같은 정리 경로를 사용한다.
- 키 선택 중에는 아이콘을 Hidden으로 바꿔 선택기의 안내 문구가 보이도록 했다.
  선택 완료·취소 후 기존 현재 키·아이콘 표시를 복구한다.
  다른 행의 표시 갱신이 선택 중인 KeySelector에 SetSelectedKey를 다시 보내지 않도록 했다.
- 옵션 창을 열 때 입력 목록을 먼저 갱신한 뒤 RPGOptionsUIRuntime에 포커스 획득을 요청한다.
  목록 재생성으로 방금 지정한 포커스를 제거하는 순서를 바꿨다.
  열기 중 재진입·중복 표시를 막고 갱신 콜백 이후 월드·소유자·표시 상태를 다시 확인한다.

변경 소스는 RPGOptionMenu 헤더·구현과 RPGInputMappingRow 구현, 총 3개다.
기존 클래스 경로·바인딩 위젯 이름·Runtime 정책 계약을 유지했으며 Manager나 에셋 변경은 없다.
아이템·인벤토리·장비·스킬·던전·보상·백엔드·보스 구현은 작업 범위에 포함하지 않았다.

격리 Editor 빌드는 504개 작업, 1462.27초, 종료 코드 0이다.
격리 Editor 빌드 결과와 소스 대조는 Saved/Logs/RPGOptionsInteraction-20261017/ChangesReport.json에 기록했다.
엔진 Build.version 갱신으로 격리 빌드 캐시가 무효화되어 의존 모듈도 다시 컴파일했다.
변경 소스 3개는 격리 사본에 동일하게 반영했고 나머지 사본 의존 코드는 동결된 입력을 사용한다.
이 결과는 최신 원본 전체의 통합 빌드 결과를 대신하지 않는다.
원본 DLL 교체·에디터 재시작·PIE·테스트 추가 및 실행은 수행하지 않았다.

이번에는 새 네이티브 클릭 처리 함수와 C++ 변경이 있어 원본 적용 시 에디터를 종료한 상태로
일반 Project_RPGEditor 빌드 후 재실행해야 한다.
실제 탭 클릭·키 선택·취소·포커스 복구·재실행 설정 복원은 이번 단계에서 실행하지 않았다.


## 2026-10-19: 키 옵션 행의 기본값 버튼과 상태 안내 구현

URPGInputMappingRow가 기존 Blueprint 위젯 트리를 감싸는 네이티브 Slate UI를 제공합니다.
각 행 오른쪽에 기본값 버튼을 표시하고, 복원은 기존 ResetToDefault → ApplySelectedKey →
RPGOptionsUIRuntime의 TryApply 경로를 사용합니다. 충돌 검사·런타임 매핑 갱신·설정 저장은 기존 정책을 따릅니다.

- 기본 키가 있고 변경 가능한 행에만 버튼을 표시합니다. 아날로그 표시 전용 행과 기본 키가 없는 레거시 행에는 표시하지 않습니다.
- 현재 키가 기본 키와 같거나 키 입력을 선택 중이면 버튼을 비활성화합니다.
  공개 CanResetToDefault는 Blueprint의 사용자 정의 복원 버튼에도 사용할 수 있습니다.
- 키 입력을 선택하는 동안 행 아래에 입력 대기 및 Esc 취소 안내를 표시합니다.
- 변경이 거절되면 행 아래에 오류를 표시합니다. 기존 MappingErrorText가 바인딩된 경우에는 해당 위젯의 오류 표시를 사용합니다.
- bShowNativeResetButton과 bShowNativeMappingFeedback은 기본적으로 켜져 있으며,
  Blueprint에서 별도 표시를 구현할 경우 클래스 기본값에서 끌 수 있습니다.
- Backspace 복원도 CanResetToDefault를 거칩니다. 복원 가능한 경우에만 키 이벤트를 소비합니다.
- Slate 상태 바인딩은 행의 약한 참조를 사용하며 별도의 Manager나 전역 상태를 만들지 않습니다.

구현 변경은 Source의 RPGInputMappingRow 헤더와 구현 두 파일입니다.
위젯 에셋·InputConfig·IMC·GameFeature 정책과 보호 중인 게임플레이 구현에는 변경이 없습니다.
사용자 요청에 따라 이번 단계의 빌드·에디터 실행·PIE·테스트는 수행하지 않았으며 검증은 후속 작업으로 미룹니다.
이번 기록은 구현 완료를 뜻하며 컴파일 또는 실행 성공을 뜻하지 않습니다.

백업과 변경 diff: Saved/Logs/RPGInputOptionRowControls-20261019/.


## 2026-10-19: 입력 명령 검색과 장치별 필터 구현

입력 옵션 목록에 네이티브 검색창, 전체/키보드·마우스/게임패드 버튼,
변경한 키만 보기, 필터 초기화와 결과 개수·빈 결과 안내를 추가했습니다.
기존 Blueprint 위젯 트리 바깥에 Slate 컨트롤을 배치하므로 에셋 편집은 필요하지 않습니다.
bShowNativeBindingFilters로 기본 표시를 끄고 공개 Blueprint 필터 API를 연결할 수도 있습니다.

- Source의 IRPGInputOptionsRuntime 계약에 장치 필터 열거형과 MatchesFilter를 추가했습니다.
  검색·장치 분류·변경 여부 판단은 RPGOptionsUIRuntime의 기존 구현이 담당합니다.
- 검색은 표시 이름, 입력 태그, 현재 키와 기본 키의 표시 이름·내부 이름을 대상으로 합니다.
  대소문자를 구분하지 않으며 공백으로 나눈 검색어가 모두 포함된 행을 표시합니다.
- 장치는 기본 키를 기준으로 구분하고, 기본 키가 없는 레거시 행은 현재 키를 사용합니다.
  변경한 키만 보기는 유효한 기본 키와 현재 키가 서로 다른 행에 적용합니다.
  기본 키가 없는 레거시 행은 변경 여부를 알 수 없어 이 필터에서는 제외합니다.
- 필터 조작은 기존 행의 Visibility만 변경합니다. 목록을 매번 재생성하거나 입력 매핑을 변경하지 않습니다.
  표시 복구 시에는 생성 당시의 행 Visibility를 사용합니다.
- 키 변경·기본값 복원 성공 후 현재 키와 필터·개수를 함께 갱신합니다.
  키 입력을 선택 중인 동안에는 필터 조작을 잠그고 선택 중인 행을 유지합니다.
- 변경한 키만 보기에서 기본값으로 복원하여 포커스가 있는 행이 숨겨지면,
  소유 LocalPlayer의 검색창으로 포커스를 옮깁니다. 다른 플레이어의 포커스는 변경하지 않습니다.
- 필터 상태는 위젯 인스턴스 안에 유지하며 사용자 키 설정 파일에는 저장하지 않습니다.

변경 구현은 옵션 Runtime 계약 1개, 기존 플러그인 구현 2개, 메뉴 2개, 행 2개로 총 7개입니다.
컨트롤러·GameUserSettings·위젯 에셋·입력 데이터 에셋과 보호 중인 게임플레이 영역은 수정하지 않았습니다.
사용자 지시에 따라 빌드·에디터 실행·PIE·테스트는 수행하지 않았습니다.
컴파일과 실제 입력·포커스 동작은 후속 검증 대상으로 남아 있습니다.

백업·diff·구현 기록: Saved/Logs/RPGInputOptionFilters-20261019/.


## 2026-10-19: 그래픽 옵션의 해상도 목록과 적용 UI 구현

고정된 해상도 세 개 대신 엔진의 지원 전체화면 해상도 목록을 사용하도록
기존 RPGOptionsUIRuntime의 그래픽 정책을 확장했습니다.

- 중복 해상도를 제거하고 화면 면적·가로·세로 순으로 정렬합니다.
  현재 엔진 설정의 사용자 지정 해상도도 선택 목록에 유지합니다.
- 옵션을 초기화할 때 GameUserSettings의 현재 해상도·화면 모드·VSync를 표시합니다.
  엔진 설정을 사용할 수 없을 때에만 유효한 기존 JSON 값을 사용하며,
  둘 다 없으면 1280x720 창 모드를 표시용 기본값으로 사용합니다.
- 목록을 다시 구성할 때 기존 옵션을 지워 재생성·재동기화에 따른 중복을 방지합니다.
- 공개 TryApplyMenu가 적용 실패 이유를 반환합니다. 양의 정수 해상도와 알려진 화면 모드를
  모두 검사한 뒤 엔진 설정을 변경하며, 숫자 변환 오버플로도 거절합니다.
- 기존 ApplyMenu 진입점과 창 모드 저장 형식, DataManager의 JSON 저장·로드 경로를 유지합니다.
  적용 후 엔진에서 정규화한 설정을 기존 JSON 경로로 전달합니다.
  JSON 쓰기 실패의 보고는 기존 DataManager 로그 방식을 따릅니다.
- GraphicOptionMenu는 네이티브 적용·변경 취소 버튼과 적용 결과·오류 문구를 제공합니다.
  변경 취소는 아직 적용하지 않은 선택을 현재 엔진 설정으로 되돌리는 기능입니다.
- bShowNativeGraphicsActions로 네이티브 컨트롤을 숨기고 기존 Blueprint UI를 사용할 수 있습니다.
  기존 OnApplyButtonClicked를 유지하고 InitializeOptions 및 LastApplyMessage도 Blueprint에 노출합니다.
- 설정 표시를 동기화하는 동안 재진입 적용을 막습니다.
  옵션 창을 다시 열 때는 부모 WidgetTree의 그래픽 메뉴를 찾아 현재 설정을 동기화합니다.
  이 연결에는 위젯 이름 변경이나 에셋 저장이 필요하지 않습니다.

구현 변경은 그래픽 Runtime 계약 1개, 플러그인 2개, 그래픽 메뉴 2개, 부모 옵션 메뉴 구현 1개로 총 6개입니다.
GameUserSettings·DataManager·데이터 구조·위젯 에셋·보호 중인 게임플레이 소스는 수정하지 않았습니다.
새 Manager는 추가하지 않았습니다. 사용자 요청에 따라 빌드·에디터 실행·PIE·테스트는 수행하지 않았습니다.
지원 해상도 열거, 화면 모드 전환, 실제 저장·재실행 복원 및 UI 동작은 후속 검증 대상입니다.

백업·diff·구현 기록: Saved/Logs/RPGGraphicsDisplayOptions-20261019/.


## 2026-10-19: 사운드 채널 연결과 기본값 복원 구현

사운드 옵션의 네이티브 슬라이더 이벤트를 BGM/Effect 전용 함수로 연결했습니다.
동일한 음량 값을 가진 슬라이더 중 하나를 추정해서 선택하던 네이티브 경로를 제거했습니다.
음소거 이벤트도 실제로 연결된 체크박스 객체로 채널을 식별합니다.

- Source의 Runtime 계약에 SetChannelVolume, SetChannelMute, RestoreDefaultSettings를 추가하고
  기존 RPGOptionsUIRuntime이 구현합니다. 재생과 믹스 소유권은 기존 SoundManager에 있습니다.
- 기존 OnVolumeChanged와 값 비교 Runtime 함수는 호환용으로 유지합니다.
  같은 값을 가진 채널이 여러 개면 호환 경로에서는 채널을 임의로 선택하지 않습니다.
- 메뉴 초기화는 현재 GameUserSettings를 우선 표시합니다. 없으면 기존 JSON을 사용하며,
  누락된 채널 음량은 1.0, 음소거는 false로 채웁니다.
- 저장·복원·기본값 정책에서 볼륨을 유한한 0~1 범위로 정리하고 알려진 BGM/Effect만 처리합니다.
  사용자 입력으로 전달되는 유한하지 않은 볼륨은 적용하지 않습니다.
- 메뉴 표시 동기화 중에는 네이티브 설정 변경·저장 콜백을 막습니다.
  기존 Blueprint 표시 갱신을 위한 값 알림은 유지하므로 기존 비율 텍스트 연결을 사용할 수 있습니다.
- 음량 변경 요청 중에도 재진입을 막고 메뉴와 전달 컨트롤의 수명을 해당 호출 동안 유지합니다.
- 네이티브 하단에 현재 음량 비율·음소거 상태와 저장·기본값 버튼을 추가했습니다.
  bShowNativeSoundActions로 숨길 수 있고 ResetToDefaults와 InitializeFromSavedSettings는 Blueprint에서도 호출할 수 있습니다.
- 기본값 복원은 모든 음량을 100%로 하고 음소거를 해제하여 즉시 소리에 반영합니다.
  기존 SoundManager의 GameUserSettings 반영 방식은 유지하며, 저장 버튼은 기존 sound_options.json 경로를 사용합니다.
- 부모 옵션 창을 다시 열 때 WidgetTree에서 사운드 메뉴를 찾아 현재 설정을 동기화합니다.
  에셋 이름 변경이나 추가 바인딩은 필요하지 않습니다.
- RPGCheckBox의 이벤트 등록을 AddUniqueDynamic으로 바꿔 속성 동기화 반복 시 중복 호출을 방지합니다.
- 기존 SoundManager::SetMasterMute에서 Master와 BGM/Effect 믹스를 함께 갱신합니다.
  전체 음소거 중 변경되어 0으로 남은 채널 믹스를 음소거 해제 시 복구합니다.

변경 구현은 사운드 Runtime 계약 1개, 플러그인 2개, 메뉴 2개,
부모 옵션 메뉴·RPGCheckBox·기존 SoundManager 각 1개로 총 8개입니다.
새 Manager, 에셋, 공유 저장 구조는 추가하지 않았습니다.
DataManager·GameUserSettings·보호 중인 게임플레이 소스는 수정하지 않았습니다.

사용자 지시에 따라 빌드·에디터 실행·PIE·테스트는 수행하지 않았습니다.
실제 채널별 재생, 전체 음소거 해제, 메뉴 재개방, 저장 후 재실행 복원은 후속 검증 대상입니다.
백업·diff·구현 기록: Saved/Logs/RPGSoundChannelOptions-20261019/.


## 2026-10-19: 오디오 전용 적용과 일괄 복원 경로 구현

기존 SoundManager의 볼륨·음소거 변경이 호출하던 GameUserSettings::ApplySettings(false)를 제거했습니다.
해당 엔진 함수는 해상도·렌더링 설정 재적용과 SaveSettings까지 수행하므로,
사운드 조작 경로를 즉시 오디오 반영과 명시적 저장으로 나눴습니다.

- 기존 SoundManager 헤더에 일반 C++ 값 구조체 FRPGSoundSettings와
  ApplySoundSettings를 추가했습니다. 새 Manager나 UObject 타입은 만들지 않았습니다.
- 전체·BGM·효과음의 음량과 음소거 여섯 값을 먼저 반영한 뒤 세 믹스를 갱신하고
  OnSoundSettingsChanged를 한 번 알립니다. 사운드 적용은 그래픽 설정이나 디스크를 건드리지 않습니다.
- 기존 SetMasterVolume/SetVolume/SetMasterMute/SetMute 진입점은 유지합니다.
  현재 상태에서 요청된 항목만 바꾼 스냅샷을 만들어 같은 적용 경로로 전달합니다.
- 유한하지 않은 일괄 요청과 유효하지 않은 채널을 거절하며 볼륨을 0~1로 정리합니다.
  기존 설정에서 스냅샷을 구성할 때 유효하지 않은 음량은 1로 보정합니다.
- 적용 중에는 매니저와 설정 객체를 보유하고, 변경 알림 콜백에서 발생하는 중첩 적용은 거절합니다.
  유효한 재생 월드가 없는 기본 수신 객체는 기존처럼 설정을 변경하지 않습니다.
- GameFeature의 기본값·JSON 복원은 여섯 번의 개별 변경 대신 스냅샷 한 번으로 적용합니다.
  JSON 키와 값 정리는 계속 GameFeature가 담당합니다.
- 사운드 저장 버튼은 컨트롤 값을 한 번에 적용한 뒤 기존 sound_options.json 경로로 전달하고
  GameUserSettings::SaveSettings로 INI를 저장합니다. 그래픽 설정을 다시 적용하지 않습니다.
  DataManager의 JSON 쓰기 실패 보고 방식은 기존 로그 처리를 유지합니다.

사운드 변경 경로에서는 더 이상 자동 저장을 요청하지 않습니다.
다른 설정 기능이 전체 GameUserSettings를 저장하면 현재 오디오 값도 함께 저장될 수 있으며,
설정이 프로세스 공용이라는 기존 범위는 유지합니다.

구현 변경은 SoundManager 헤더·구현과 RPGSoundOptionsRuntime 구현, 총 3개입니다.
GameUserSettings·DataManager·위젯·에셋·보호 중인 게임플레이 소스는 수정하지 않았습니다.
사용자 요청에 따라 빌드·에디터 실행·PIE·테스트는 수행하지 않았습니다.
실제 음량 변경, 알림 횟수, 기본값·저장값 복원 및 저장 후 재실행 동작은 후속 검증 대상입니다.

백업·diff·구현 기록: Saved/Logs/RPGSoundSettingsApply-20261019/.


## 2026-10-19: 옵션 파일 입출력의 GameFeature 이전과 저장 결과 표시

RPGOptionsUIRuntime 내부에 상태를 보관하지 않는 RPGOptionsPersistence 함수를 추가했습니다.
그래픽·사운드 Runtime의 JSON 저장·복원이 이 경로를 사용하며, 공유 DataManager에는 변경이 없습니다.

- 기존 graphic_options.json과 sound_options.json, FGraphicSaveData와 FSoundSaveData를 사용합니다.
  직렬화 방식은 기존 FJsonObjectConverter를 유지합니다.
- 읽기에서는 기존 속성 이름과 exporter의 camelCase 이름을 모두 받아들입니다.
  임시 구조체로 읽기를 완료한 뒤 호출자에게 전달하여 실패 시 부분 데이터가 전달되지 않게 합니다.
- 저장 시 직렬화·저장 폴더 생성·실제 파일 쓰기 결과를 확인하고 오류 문구를 반환합니다.
  새 Manager나 전역 상태는 만들지 않았습니다.
- 사운드 Runtime의 TrySaveMenu가 컨트롤·음량·적용 준비 상태와 JSON 쓰기 결과를 반환합니다.
  기존 SaveMenu는 호환용 진입점으로 유지하고 실패를 로그에 기록합니다.
- 사운드 메뉴는 저장 성공·실패를 네이티브 문구로 표시합니다.
  LastSaveMessage와 DidLastSaveFail도 Blueprint에서 사용할 수 있습니다.
  음량·음소거 조작, 기본값 복원, 재개방 시에는 이전 저장 문구를 지웁니다.
- JSON 저장이 실패하면 사운드 경로에서 INI 저장은 요청하지 않습니다.
  JSON 저장 성공 뒤 기존 SaveSettings를 호출하며, 엔진 INI API의 별도 성공 확인은 추가하지 않았습니다.
  사운드는 기존처럼 즉시 반영되므로 저장 실패가 음량 변경을 되돌리지는 않습니다.
- 그래픽 저장이 실패하면 화면에 이미 적용되었음을 오류 문구에 명시합니다.
  TryApplyMenu의 false는 입력 거절 또는 JSON 저장 실패를 뜻하며, 저장 실패 시 화면 설정이 적용된 상태일 수 있습니다.
- DataManager의 기존 호환 저장·로드 API는 남겨 두었습니다.
  옵션 GameFeature는 해당 매니저를 조회하거나 그 헤더에 의존하지 않습니다.

소스 9개(새 입출력 함수 헤더·구현 2개와 기존 파일 7개)를 작업했습니다.
공유 Manager·GameUserSettings·저장 구조·에셋·보호 중인 게임플레이 구현은 수정하지 않았습니다.
사용자 요청에 따라 빌드·에디터 실행·PIE·테스트는 수행하지 않았습니다.
기존 파일 읽기, 쓰기 실패와 성공 표시, INI 반영, 재실행 복원은 후속 검증 대상입니다.

백업·diff·구현 기록: Saved/Logs/RPGOptionsSaveFeedback-20261019/.


## 2026-10-19: Options GameFeature 활성화 시 저장 설정 초기화

활성화된 Options GameFeature가 저장 설정을 자동으로 가져오는 연결을 구현했습니다.
메뉴 동기화는 현재 값 표시를 유지하며, 시작 시 JSON 복원은 별도 InitializeForWorld가 담당합니다.

- 액션이 적용되는 클라이언트 게임 월드에서 초기화를 요청합니다.
  이미 열린 월드, 새 월드, GameInstance 시작과 월드 BeginPlay를 처리합니다.
  월드가 먼저 등록된 경우에도 GameInstance 시작 시 초기화를 다시 시도합니다.
- GameUserSettings에 Config/UPROPERTY가 아닌 메모리용 초기화 플래그 두 개를 추가했습니다.
  같은 설정 객체의 맵 전환·기능 재활성화·모듈 재로드에서 JSON을 반복 적용하지 않습니다.
  같은 프로세스의 PIE 인스턴스는 기존처럼 설정 객체를 공유합니다.
- 초기화 상태는 적용 콜백 전에 기록하여 재진입에 의한 중복 복원을 막습니다.
  설정 객체나 실제 SoundManager가 아직 없으면 완료로 기록하지 않고 다음 수명주기 요청을 기다립니다.
  사운드 일괄 적용이 거절되면 초기화 표시를 취소합니다.
- 파일이 없거나 손상되면 현재 엔진/INI 값을 유지합니다.
  사운드는 기존 정책으로 음량·채널을 정규화하여 한 번에 반영합니다.
  파일 실패 때문에 맵 전환마다 재로드하거나 현재 값을 기본값으로 바꾸지 않습니다.
- SoundManager::Init은 목적지 월드마다 현재 값으로 믹스를 연결합니다.
  JSON은 설정 객체당 한 번만 가져오므로 저장하지 않은 음량 조정도 맵 전환 시 유지됩니다.
  기존 SoundManager의 성공한 직접 설정 변경 역시 초기화 상태를 확정합니다.
- 그래픽 자동 복원은 일반 게임/Standalone에서 실행하며 PIE에서는 실행하지 않습니다.
  시작 해상도 적용은 명령행 덮어쓰기를 허용하고 ApplyResolutionSettings/ApplyNonResolutionSettings를 사용합니다.
  시작 경로에서 전체 ApplySettings나 SaveSettings를 직접 호출하지 않습니다.
  명시적 그래픽 적용·저장 경로는 유지합니다.
- 기존 GameInstance Blueprint 함수 InitializeGraphicOption/InitializeSoundOption도 같은 경로를 사용합니다.
  Experience가 없는 맵에서 해당 함수를 호출하는 호환 경로도 유지합니다.
  Runtime의 RestoreSavedSettings는 명시적 재로드용 C++ API로 남겼습니다.
- BeginPlay 등록은 월드 정리와 비활성화 시 해제합니다.
  설정 콜백 뒤에는 활성화 세대와 월드 등록을 다시 조회합니다.
  전용 서버·에디터 미리보기·정리 중인 월드는 초기화 대상에서 제외합니다.

소스 11개와 기존 문서 2개를 변경했습니다. 새 Manager·에셋은 추가하지 않았습니다.
공통 GameInstance의 옵션 위임, 기존 SoundManager와 GameUserSettings의 옵션 초기화 부분을 보완했으며
아이템·인벤토리·장비·스킬·던전·보상·백엔드·보스 구현은 변경하지 않았습니다.
사용자 요청에 따라 빌드·실행·PIE·테스트는 수행하지 않았습니다.
시작 복원, 명령행 설정, 맵 전환, 기능 재활성화와 여러 PIE 인스턴스 동작은 후속 검증 대상입니다.

백업·diff·구현 기록: Saved/Logs/RPGOptionsStartup-20261019/.


## 2026-10-19: 그래픽 화면 변경의 임시 적용·확인·복구

네이티브 그래픽 메뉴의 적용 버튼에 해상도·화면 모드 확인 단계를 추가했습니다.
GameFeature Runtime은 입력 검증과 JSON 저장을 맡고, 기존 GameUserSettings는 공유 화면의 임시 상태와 수명주기를 보유합니다.

- 해상도 또는 화면 모드가 바뀌면 현재 해상도·모드·선호 전체화면 모드·VSync를 메모리에 보관하고 임시 적용합니다.
  이 단계에서는 graphic_options.json과 INI 저장을 요청하지 않습니다.
  해상도 변경에 따른 렌더 해상도 비율의 자동 변경은 보존된 기존 비율로 제한합니다.
- 메뉴에 '이 설정 유지', '이전 설정으로'와 남은 초를 표시합니다.
  제한 시간은 15초이며 FPlatformTime과 CoreTicker를 사용하여 게임 일시정지·시간 배율과 분리했습니다.
  GameFeature Runtime 모듈이 사라져도 기존 설정 객체가 복구 시계를 유지합니다.
- 유지 확인 시 엔진 ConfirmVideoMode를 호출하고 JSON 저장 뒤 INI 저장을 요청합니다.
  JSON 쓰기 실패는 '화면은 유지했지만 저장하지 못함'으로 표시하고 적용 버튼을 통한 재시도를 안내합니다.
  엔진 INI API의 별도 쓰기 성공 확인은 추가하지 않았습니다.
- 시간 초과·소유 위젯 소멸·원래 월드 정리/변경 시에는 이전 상태로 복구합니다.
  옵션 창 닫기/숨기기/제거, 그래픽 메뉴 비활성화와 다른 탭 선택에도 미확정 화면 변경을 취소합니다.
  복구 자체는 JSON이나 INI 저장을 요청하지 않습니다.
- 동일 프로세스의 화면은 공유되므로 GameUserSettings당 임시 변경을 하나만 허용합니다.
  다른 그래픽 적용·명시적 복원 요청은 임시 변경 중 거절합니다.
  외부 코드가 화면 값을 바꾸거나 설정 객체를 교체했다면 해당 변경을 이전 스냅샷으로 덮어쓰지 않습니다.
- 사운드·입력 저장 등 다른 기능이 GameUserSettings::SaveSettings를 호출해도,
  진행 중인 임시 화면 값 대신 이전 화면 값만 INI에 직렬화하고 현재 표시 상태를 복원합니다.
  다른 옵션 값과 기존 저장 동작은 유지합니다. 직접 SaveConfig를 호출하는 외부 코드는 이 보호 범위 밖입니다.
- 화면 확인 중에는 해당 메뉴의 설정 컨트롤을 잠급니다.
  기본 그래픽 버튼을 숨기는 Blueprint 설정에서도 확인 버튼은 표시합니다.
  확인 버튼에는 해당 LocalPlayer의 Slate 포커스만 전달하며, 다른 UI가 가진 포커스는 가져오지 않습니다.
- Blueprint에 ConfirmDisplayChanges, RevertDisplayChanges, IsAwaitingDisplayConfirmation,
  GetDisplayConfirmationSecondsRemaining, GetApplyStatusMessage를 제공합니다.
  기존 OnApplyButtonClicked는 확인 흐름을 사용합니다.
  VSync만 바꾸거나 같은 화면 값을 다시 저장하는 경우에는 기존 즉시 적용·저장 경로를 사용합니다.
  C++ Runtime의 기존 ApplyMenu/TryApplyMenu는 즉시 적용·저장 호환 API로 유지합니다.

기존 소스 9개와 문서 2개를 수정했습니다. 새 Manager·에셋·별도 저장 형식은 추가하지 않았습니다.
GameUserSettings의 기존 사운드/키 매핑 데이터와 정책, 보호 중인 게임플레이·백엔드 소스는 변경하지 않았습니다.
사용자 지시에 따라 빌드·실행·PIE·테스트는 수행하지 않았습니다.
유지·시간 초과·창 종료·맵 전환·복수 메뉴·다른 옵션 저장 중 복구는 후속 검증 대상입니다.

백업·diff·구현 기록: Saved/Logs/RPGGraphicsConfirmation-20261019/.


## 2026-10-19: 옵션 재개방 포커스와 입력 목록 상태 유지

옵션을 숨겼다가 같은 위젯으로 다시 열 때 조작 위치를 이어갈 수 있도록 구현했습니다.
기능은 공통 옵션 위젯과 기존 Options GameFeature Runtime 안에 추가했으며 새 Manager나 저장 형식은 만들지 않았습니다.

- 입력 목록의 행 구성·표시 이름·편집 가능 여부·행 클래스·아이콘 설정·소유 컨트롤러가 같으면 행을 재사용합니다.
  현재 키와 필터 결과만 갱신하여 기존 Slate 포커스와 스크롤 위치가 유지되게 합니다.
  실제 구성이 달라 다시 만드는 경우에도 기존의 유한한 스크롤 오프셋을 복원합니다.
  스크롤 범위는 엔진이 현재 내용에 맞춰 제한합니다.
- 옵션이 닫히기 직전의 컨트롤 포커스를 약한 참조로 기억합니다.
  같은 LocalPlayer·월드·선택 탭에서, 현재 옵션 트리 안에 보이고 활성화된 포커스 경로가 남아 있을 때만 복원합니다.
  숨김/비활성 조상 아래나 제거된 행은 복원 대상으로 사용하지 않습니다.
- 마지막 포커스를 사용할 수 없으면 기존 Blueprint/기본 포커스, 현재 선택 탭 버튼, 옵션 루트 순서로 선택합니다.
  기존 탭 선택 상태를 유지하고 네이티브 기본 포커스를 현재 선택 탭에 연결했습니다.
  입력 모드 적용과 창을 닫은 뒤 외부 UI/게임으로 돌려주는 기존 소유권 처리는 유지합니다.
- 선택한 키를 확정하지 않은 채 창 종료·숨김·비활성화·탭 전환·행 재생성이 일어나면 키 입력 대기를 취소합니다.
  UInputKeySelector의 공개 취소 setter가 없어, 해당 Slate 컨트롤의 공개 OnFocusLost 처리로 대기 상태만 끝냅니다.
  이 과정에서 새 키를 선택하거나 매핑 저장을 요청하지 않으며 다른 Slate 사용자의 포커스를 변경하지 않습니다.
  키·필터 동기화 중 재진입을 막고, 파괴된 행에서 늦게 온 키 선택·기본값 변경을 적용하지 않습니다.
- 취소 시 목록의 관성 스크롤을 중단합니다.
  검색어·장치 필터·변경 항목 필터와 실제 키 매핑 값은 유지합니다.
- 부모 옵션 창은 선택적 BindWidget 이름뿐 아니라 WidgetTree에서 찾은 입력 메뉴도 갱신·정리합니다.
  에셋 이름 변경이나 추가 바인딩은 필요하지 않습니다.
- 기억한 포커스는 위젯 수명 내 메모리 상태입니다.
  다른 플레이어·월드 또는 새로 생성된 옵션 위젯으로 넘기거나 INI/JSON에 저장하지 않습니다.

기존 소스 7개와 문서 2개를 수정했습니다.
UIManager·GameUserSettings·저장 포맷과 보호 중인 게임플레이·백엔드 구현은 변경하지 않았습니다.
입력 메뉴에 이미 존재하는 스킬 창 항목 구성 분기도 그대로 유지했습니다.
사용자 요청에 따라 빌드·실행·PIE·테스트는 수행하지 않았습니다.
재개방·입력 대기 취소·필터와 스크롤 유지·여러 LocalPlayer의 포커스는 후속 검증 대상입니다.

백업·diff·구현 기록: Saved/Logs/RPGOptionsReopen-20261019/.


## 2026-10-19: 옵션 창의 뒤로 가기와 키보드·게임패드 탭 이동

Options GameFeature Runtime에 상태 없는 단축키 판정과 현재 입력 소유권 조회를 추가했습니다.
공통 옵션 위젯은 이 정책으로 자신의 탭 이동과 닫기를 처리하며 게임플레이 입력 매핑을 수정하지 않습니다.

- Ctrl+Tab은 다음 탭, Ctrl+Shift+Tab은 이전 탭을 선택합니다.
  게임패드 좌/우 범퍼도 이전/다음 탭을 선택합니다.
  사용 가능한 탭을 표시 순서로 순환하고 숨김·비활성 탭 버튼과 유효하지 않은 인덱스를 제외합니다.
  일반 Tab, 방향키, Alt/Command 조합 등은 기존 위젯의 입력 처리를 유지합니다.
- Esc와 게임패드 오른쪽 페이스 버튼은 뒤로 가기입니다.
  자식 컨트롤의 키다운 처리 이후에 동작하므로 텍스트 편집기의 자체 Esc 처리 등은 먼저 실행됩니다.
  키 지정 대기 중에는 네이티브 단축키가 캡처할 키를 가로채지 않습니다.
- 공통 RequestBack 경로는 키 지정 대기 취소, 미확정 화면 변경 복구, 옵션 창 닫기 순서로 한 단계만 처리합니다.
  기존 CommonUI Back 요청도 활성 옵션 입력 세션에서는 같은 경로를 사용합니다.
  해당 세션이 없는 CommonUI 사용 방식은 기존 Super 처리로 전달합니다.
- 탭 이동 후 같은 LocalPlayer의 선택 탭 버튼에 포커스를 전달합니다.
  버튼의 포커스 경로가 숨김/비활성/제거 상태이면 현재 옵션 루트로 전달합니다.
  콜백 뒤에는 세션 ID·소유자·스위처·선택 결과를 다시 확인합니다.
- 네이티브 단축키는 자기 옵션 트리에 포커스가 있고 현재 플레이어·컨트롤러·월드·뷰포트가
  원래 입력 세션과 일치하는 동안에만 동작합니다.
  다른 UI로 향하는 대기 중 포커스 요청과 입력 모드 변경도 확인합니다.
  초기 포커스 컨트롤이 제거되어도 현재 포커스가 옵션 내부에 있으면 소유권을 판단할 수 있게 했습니다.
- Slate 단축키의 반복 키다운에서는 같은 작업을 반복하지 않으며, 처리한 키의 키업도 같은 세션에서 소비합니다.
  세션 종료 시 해당 기록을 비웁니다.
- Blueprint에 RequestBack, CloseOptionsMenu, SelectAdjacentTab과 HasPendingKeySelection을 제공합니다.
  bEnableNativeOptionNavigation=false로 기본 단축키를 끄고 이 API로 직접 연결할 수 있습니다.
- 명시적 CloseOptionsMenu는 미확정 상태를 정리한 뒤 현재 HUD가 제공하는 위젯이 자신일 때만 HUD 닫기를 요청합니다.
  다른 위젯이 HUD 제공자가 된 경우에는 자신만 숨깁니다.
  다른 팝업을 전역 Pop하거나 임의로 GameOnly 입력 모드를 지정하지 않습니다.

기존 소스 7개와 문서 2개를 수정했습니다. 새 Manager·에셋·입력 액션·매핑 컨텍스트는 추가하지 않았습니다.
PlayerController·UIManager·GameUserSettings와 아이템·인벤토리·장비·스킬·던전·보상·백엔드·보스 구현은 변경하지 않았습니다.
사용자 지시에 따라 빌드·실행·PIE·테스트는 수행하지 않았습니다.
키 지정 중 단축키, 화면 변경 중 뒤로 가기, 범퍼/키보드 탭 이동과 여러 LocalPlayer의 입력 소유권은 후속 검증 대상입니다.

백업·diff·구현 기록: Saved/Logs/RPGOptionsNavigation-20261019/.


## 2026-10-20: 키 설정의 전체 기본값 복원과 확인 UI

- 입력 메뉴에 '모든 키 기본값 복원'과 개수 안내·취소·확인 UI를 추가했습니다.
  검색·장치·변경 항목 필터로 숨긴 바인딩도 대상에 포함됩니다.
  편집 가능한 원본 바인딩 중 현재 키가 기본키와 다른 항목만 복원하며,
  아날로그 축과 기본키 정보가 없는 기존 대체 행은 제외합니다.
- GameFeature의 BuildDefaultRestoration이 대상 목록을 결정하고 TryRestoreDefaults가 확인 시 목록과 현재 키를 다시 비교합니다.
  확인 대기 중에는 목록과 필터를 잠급니다. 다른 경로로 매핑이 바뀌면 다시 요청하도록 안내합니다.
- 컨트롤러의 TryRestoreDefaultKeyBindings는 모든 바인딩과 소유자를 먼저 확인한 뒤 런타임 IMC의 Key만 일괄 변경합니다.
  서로 자리를 바꾼 키는 최종 기본값 조합으로 검사합니다.
  원본에서 공유한 키는 유지하고, 복원 때문에 새로 생기는 자체 컨텍스트 충돌은 거절합니다.
- 다른 활성 컨텍스트는 엔진의 공개 QueryMapKeyInActiveContextSet으로 검사합니다.
  자신과 함께 재구성되는 파생 컨텍스트는 제외하고, 다른 Action의 차단·예약·가림 충돌을 거절합니다.
  두 Action 모두 입력을 소비하지 않아 엔진이 문제로 보고하지 않는 중복은 이 검사 대상이 아닙니다.
  적용되지 않은 모든 컨텍스트를 전수 검사하거나 공유 에셋을 수정하지 않습니다.
- 기존 입력 해제 훅 이후 소유자·원본/런타임 매핑·충돌을 재확인합니다.
  이 단계까지 실패하면 키와 저장 설정을 변경하지 않습니다.
  정상 적용 시 기존 파생 컨텍스트 재구성, 입력 재구성과 설정 저장을 각각 한 번 요청합니다.
  각 매핑의 Modifier·Trigger와 복원 대상 밖의 키는 유지합니다.
- 저장 시 복원한 Action의 설정된 태그 별칭에도 바인딩별 기본키를 기록합니다.
  기존 태그별 저장값이 재시작 때 복원 결과를 덮어쓰지 않도록 기존 두 단계 로드 순서를 활용합니다.
  저장 형식을 추가하거나 전체 CustomKeyMappings를 지우지 않습니다.
  INI SaveSettings는 성공 여부를 반환하지 않으므로 파일 저장 성공을 보장하는 메시지를 표시하지 않습니다.
- Esc/CommonUI 뒤로 가기는 전체 복원 요청부터 취소합니다.
  탭 전환·숨김·비활성화·종료·행 재생성도 확인 요청을 취소합니다.
  단순 취소는 매핑·저장값을 바꾸지 않습니다.
  기본 포커스는 같은 LocalPlayer의 취소 버튼이며, 다른 UI의 포커스를 가져오지 않습니다.
- Blueprint에 RequestRestoreAllDefaults, ConfirmRestoreAllDefaults, CancelRestoreAllDefaults,
  IsAwaitingDefaultRestoration, CanRestoreAllDefaults, 대상 개수·상태·실패 조회를 제공합니다.
  bShowNativeDefaultsActions=false로 기본 버튼을 숨길 수 있습니다.
  명시적으로 복원을 요청한 동안에는 확인 UI를 제공하여 취소·확정 경로를 유지합니다.

기존 소스 8개와 문서 2개를 수정했습니다. 새 Manager·에셋·저장 형식은 없습니다.
컨트롤러의 공통 입력 처리에 메서드를 추가했으며 기존 스킬·인벤토리·장비·던전·보상·백엔드·보스 구현은 수정하지 않았습니다.
사용자 지시에 따라 빌드·실행·PIE·테스트는 수행하지 않았습니다.
실제 키 복원·컨텍스트 충돌·확인 UI·저장 후 재실행 동작은 미검증입니다.

변경 전후 원본·diff·해시 기록: Saved/Logs/RPGInputDefaults-20261020/.


## 2026-10-20: 전체 복원의 충돌 정책을 GameFeature로 이전

전체 기본값 복원에 추가됐던 자체 컨텍스트 충돌 검사와 엔진 활성 컨텍스트 질의를
컨트롤러에서 RPGOptionsUIRuntime의 ValidateDefaultRestoration으로 이전했습니다.

- Source의 FRPGInputBindingRestoreView는 원본 매핑·최종 키·대상 인덱스의 읽기 전용 배열 뷰와
  질의할 컨텍스트·입력 서브시스템을 전달하는 일반 C++ 구조체입니다.
  배열은 요청 함수의 로컬 스냅샷을 참조하며, 콜백 밖에 보관할 수 없습니다.
  UObject나 Manager, 새 런타임 상태 저장소를 만들지 않습니다.
- 컨트롤러의 TryRestoreDefaultKeyBindings는 호출자가 제공하는 필수 정책 콜백을 동기 호출합니다.
  GameFeature가 기존 충돌 규칙과 오류 문구를 보유하고,
  컨트롤러는 플레이어·월드·설정 객체·원본/런타임 레이아웃 검사와 Key 일괄 반영·저장을 맡습니다.
- 기존 입력 해제 훅 전후 각각 정책을 조회합니다.
  정책 콜백 이후에도 소유자와 매핑을 재확인하여, 콜백 중 변경된 컨텍스트에 쓰지 않도록 했습니다.
  콜백은 뷰를 저장하거나 매핑을 수정하지 않는 동기 검사로 사용해야 합니다.
- C++ 전체 복원 메서드에는 정책 인자가 추가됐으며, 프로젝트에서 확인된 호출 지점인
  Options GameFeature를 함께 변경했습니다. 기존 Blueprint 복원 API는 같은 경로를 사용합니다.
- 원본의 의도된 공유 키, 최종 기본키 조합 검사, 파생 컨텍스트 제외와
  엔진의 차단·예약·가림 검사 규칙은 이전과 같습니다.

기존 소스 3개를 수정하고 공통 데이터 뷰 헤더 1개를 추가했습니다. 문서 2개를 갱신했습니다.
컨트롤러에는 Options GameFeature 헤더나 InputMappingQuery 의존성을 추가하지 않았습니다.
기존 스킬 입력 해제·파생 컨텍스트 재구성·닫기 키 갱신 훅과 저장 형식은 유지했습니다.
아이템·인벤토리·장비·스킬·던전·보상·백엔드·보스의 별도 구현은 수정하지 않았습니다.
사용자 지시에 따라 빌드·실행·PIE·테스트는 수행하지 않았습니다.
실제 콜백·컴파일·키 복원 동작은 후속 확인 대상입니다.

변경 전후 원본·diff·해시 기록: Saved/Logs/RPGInputPolicy-20261020/.


## 2026-10-20: 그래픽 옵션의 값 기반 계약과 Source UI 어댑터

그래픽 Runtime의 실제 처리 경로가 UMG 컨트롤을 직접 읽거나 수정하지 않도록 분리했습니다.

- Source 계약에 ERPGGraphicsWindowMode, FRPGGraphicsSettings, FRPGGraphicsOptionsModel을 추가했습니다.
  해상도는 FIntPoint, 화면 모드는 공통 열거형, VSync는 bool로 전달합니다.
  모델은 현재 설정과 선택 목록을 값으로 보유하며 위젯이나 설정 객체를 보관하지 않습니다.
- GameFeature는 BuildOptions, TryParseSelection, TryApplySettings, TryPreviewSettings로
  선택 목록·문자열 변환·검증·적용·저장을 처리합니다.
  그래픽 구현의 CheckBox/ComboBoxString include와 컨트롤 접근을 제거했습니다.
- 콤보박스 목록·선택값과 체크박스 상태의 동기화는 Source의 SynchronizeMenuControls가 맡습니다.
  Source의 ReadMenuSelection은 현재 선택을 문자열/불리언 값으로 복사한 뒤 GameFeature의 파서를 호출합니다.
  그래픽 메뉴는 모델 조회·컨트롤 동기화·설정 값 전달 경로를 직접 사용합니다.
- 기존 ConstructMenu, InitializeMenu, ApplyMenu, TryApplyMenu, TryPreviewMenu C++ 시그니처를 유지했습니다.
  구현은 Source의 기본 UI 어댑터가 제공하며, 같은 값 기반 Runtime 메서드로 연결됩니다.
- 저장 JSON의 해상도·화면 모드 문자열과 VSync 필드는 기존 형식으로 변환합니다.
  기존 WindowedFullscreen/Windowed Fullscreen 표기와 해상도 공백·대소문자 허용 규칙을 유지합니다.
  직접 전달된 설정 값도 해상도 양수·화면 모드 유효성을 확인합니다.
- 시작 복원과 명시적 저장값 복원도 문자열 파싱 후 같은 설정 값 적용 함수를 사용합니다.
  지원 해상도 정렬·중복 제거·현재 사용자 해상도 포함,
  15초 화면 확인·취소·수명주기 복구와 VSync 단독 적용 경로는 유지했습니다.
- 모델 동기화 뒤 메뉴가 제거됐는지 확인하고, 화면 변경 요청 동안 소유자를 강한 지역 참조로 유지합니다.

기존 소스 5개와 문서 2개를 수정했습니다.
새 Manager·에셋·저장 형식·테스트는 추가하지 않았습니다.
GraphicOptionMenu의 기존 CP949 헤더와 보호 중인 게임플레이·백엔드 구현은 수정하지 않았습니다.
사용자 지시에 따라 빌드·에디터 실행·PIE·테스트는 수행하지 않았습니다.
실제 메뉴·이전 C++ 호출·직접 값 요청·저장 복원 동작은 후속 확인 대상입니다.

변경 전후 원본·diff·해시 기록: Saved/Logs/RPGGraphicsValues-20261020/.


## 2026-10-20: 사운드 옵션의 값 기반 계약과 Source UI 어댑터

사운드 옵션의 컨트롤 접근을 Source UI로 옮기고, GameFeature가 설정 값으로 처리하도록 분리했습니다.

- 기존 FRPGSoundSettings의 이름·필드·기본값을 유지하여 Settings/RPGSoundSettings.h로 옮겼습니다.
  SoundManager와 옵션 계약이 같은 공통 헤더를 사용하며,
  기존 SoundManager.h를 포함한 호출 코드도 해당 구조체를 계속 사용할 수 있습니다.
- Runtime에 GetCurrentSettings, TryApplySettings, TrySaveSettings를 추가했습니다.
  전체/BGM/Effect 볼륨과 음소거 여섯 값을 한 번에 읽고 적용·저장합니다.
  직접 값 요청은 비정상 실수를 거절하고 정상 범위 밖의 볼륨은 0~1로 정리합니다.
- Source의 SynchronizeMenuControls가 컨트롤·채널 맵을 동기화합니다.
  기존 표시용 볼륨·음소거 콜백과 동기화 중 네이티브 쓰기 방지 동작을 유지합니다.
  ReadMenuSettings는 여섯 컨트롤을 확인한 뒤 값을 복사하며, GameFeature는 이 스냅샷만 받습니다.
- 기존 InitializeMenu, SaveMenu, TrySaveMenu, SetVolumeFromMenu, SetMuteFromMenu C++ API는
  Source 기본 어댑터로 유지했습니다. 값만 받은 채널 선택이 모호하면 거절하는 기존 동작도 유지합니다.
  사운드 GameFeature 구현에서 Slider·CheckBox·RPGCheckBox include와 컨트롤 접근을 제거했습니다.
- 네이티브 메뉴는 현재 값 조회·Source 컨트롤 동기화·값 기반 저장 경로를 사용합니다.
  저장 중 콜백으로 메뉴가 제거된 경우 완료 문구를 갱신하지 않도록 했습니다.
- GameFeature 내부에서 기존 JSON의 MasterVolume/bMasterMuted/Volumes/Mutes로 변환합니다.
  BGM/Effect 저장 키, 누락된 볼륨의 기본값 1, 음소거 기본값 false와 저장 파일 이름을 유지했습니다.
  기본값·시작 복원·명시적 저장값 복원도 같은 값 기반 적용 경로를 사용합니다.
- 실제 오디오 반영은 기존 SoundManager의 일괄 적용 메서드가 담당합니다.
  한 요청의 값들을 안정적으로 복사하고 설정 객체가 같은지 확인한 뒤 저장합니다.
  JSON 쓰기 실패 시 명시적 INI 저장 요청을 건너뛰며,
  INI API는 성공 여부를 반환하지 않아 파일 저장 성공을 별도로 검증한 것은 아닙니다.

기존 소스 6개를 수정하고 공통 설정 헤더 1개를 추가했습니다. 문서 2개를 갱신했습니다.
새 Manager·에셋·저장 형식·테스트는 추가하지 않았습니다.
SoundManager.cpp의 재생·캐시·믹스 구현, SoundOptionMenu의 CP949 헤더,
보호 중인 게임플레이·백엔드 구현은 수정하지 않았습니다.
사용자 지시에 따라 빌드·에디터 실행·PIE·테스트는 수행하지 않았습니다.
실제 채널별 반영·기존 C++ API·저장 실패·저장 후 재시작 복원은 미검증입니다.

변경 전후 원본·diff·해시 기록: Saved/Logs/RPGSoundValues-20261020/.


## 2026-10-20: 옵션 컴포넌트의 참조 기반 획득·해제와 HUD 제공자 교체

- OptionsMenuComponent의 BeginPlay와 UI 소유를 분리했습니다.
  미리 배치된 컴포넌트도 Feature 참조가 있을 때 UI를 획득하고,
  마지막 참조가 사라지면 UI 제공·단축키·대기 요청을 정리합니다.
  Feature가 생성한 컴포넌트는 참조가 없으면 제거하고, 미리 배치된 컴포넌트는 남겨 재활성화 시 다시 획득합니다.
  Feature 요청이 없는 HUD의 기본 옵션 위젯 경로는 유지했습니다.
- 획득·해제·입력 갱신 중 재진입과 종료 상태를 구분하고, 획득 시도에 세대를 부여했습니다.
  위젯 초기화·HUD 제공자 등록·화면 추가 뒤에 세대와 소유자를 다시 확인합니다.
  정리 콜백이 새 참조를 추가하면 종료된 획득 작업이 새 세션을 덮어쓰지 않습니다.
- 콜백 중 새 연결 요청과 seamless 이동 중 LocalPlayer 등록 대기는 월드의 다음 프레임 타이머로 재시도합니다.
  타이머는 약한 참조와 세대·월드 검사를 사용하며 마지막 참조 해제와 EndPlay에서 취소합니다.
  컴포넌트의 상시 Tick은 활성화하지 않았습니다.
- UI와 입력 컴포넌트의 소유 기록을 콜백 전에 비웁니다.
  입력 스택에서는 해당 컴포넌트만 Pop하고, 정확히 같은 HUD 제공자·위젯인 경우에만 제공을 해제합니다.
  HasShortcutBindings도 활성 세션과 실제 입력 스택 등록을 확인합니다.
- HUD에 IsOptionMenuProvider 조회와 제공자 변경 세대를 추가했습니다.
  새 제공자를 먼저 기록한 뒤 이전 위젯을 정리하며,
  제공 해제는 기존 기록을 먼저 비운 뒤 숨김·제거 콜백을 호출합니다.
  콜백에서 다른 제공자가 등록되거나 이전 위젯을 재사용하면 그 새 제공자를 제거·덮어쓰지 않습니다.
- 기본 옵션 위젯 생성도 생성·표시 콜백 뒤 제공자 세대·월드·플레이어를 확인합니다.
  HUD 종료 중에는 기본 창을 만들지 않고, 살아 있는 같은 월드의 기존 기본 창 복구는 유지했습니다.
  표시·숨김의 후속 처리도 제공자 세대가 바뀌면 중단합니다.
- GameFeature Action은 파괴 중인 컴포넌트를 새 요청에 재사용하지 않고,
  등록 콜백 동안 HUD·컴포넌트의 지역 강한 참조를 보유합니다.
  등록 전에 기능이 해제되어 컴포넌트가 파괴된 경우 등록을 건너뜁니다.

기존 소스 5개와 문서 2개를 수정했습니다.
새 Manager·에셋·저장 형식·테스트는 추가하지 않았습니다.
기존 옵션 위젯의 숨김·제거 경로로 키 선택·전체 기본값 확인·그래픽 미확정 변경과 입력 세션을 정리합니다.
아이템·인벤토리·장비·스킬·던전·보상·백엔드·보스 구현은 변경하지 않았습니다.
HUD의 메인 메뉴·상호작용·클래스 선택 구현도 유지했습니다.
사용자 지시에 따라 빌드·에디터 실행·PIE·테스트는 수행하지 않았습니다.
실제 참조 중첩·재활성화·콜백 재진입·월드 전환·입력 복구 동작은 미검증입니다.

변경 전후 원본·diff·해시 기록: Saved/Logs/RPGOptionsComponent-20261020/.


## 2026-10-20: 개별 키 변경의 GameFeature 정책 재확인

- 개별 키 변경의 키 종류·장치·중복 검사 코드를 Options GameFeature의
  ValidateKeyChange로 묶었습니다. 기존 충돌 규칙과 오류 문구를 유지합니다.
  런타임 IMC의 형제 바인딩과 EnhancedPlayerInput의 공개 유효 매핑 뷰를 조회합니다.
- 컨트롤러에 네이티브 TryApplyKeyBindingWithPolicy를 추가했습니다.
  GameFeature는 이 메서드에 동기 정책 콜백을 전달하며, 컨트롤러는 기존 입력 해제 훅 전후에 호출합니다.
  정책 콜백 전후에는 플레이어·월드·서브시스템·원본/런타임 설정·매핑의 Action/Key와
  요청 태그의 대상 Action이 처음과 같은지 확인합니다.
  저장 설정 객체가 교체된 경우에도 키를 반영하기 전에 중단합니다.
- 입력 해제 중 다른 경로에서 키나 매핑 구성을 바꾸면 이전 요청을 적용하거나 저장하지 않습니다.
  활성 유효 매핑이 바뀌어 새 충돌이 생긴 경우 GameFeature의 두 번째 검사에서 거절합니다.
  컨트롤러·설정·IMC·대상 Action은 동기 작업 동안 강한 참조로 유지합니다.
- 키가 성공적으로 반영되면 기존 파생 컨텍스트 재구성·입력 재구성·저장 경로를 한 번씩 사용합니다.
  Modifier·Trigger·형제 바인딩을 다시 만들지 않으며 대상 Key만 변경합니다.
- 기존 Blueprint TryApplyKeyBinding과 ApplyKeyMapping, 시작 시 저장 키 복원 경로는 유지합니다.
  새 C++ 정책 경로는 Options GameFeature가 호출합니다.
  기존 API와 시작 복원에 옵션 UI의 충돌 규칙을 강제하지 않습니다.
- 기존 스킬 창 대체 키 분기는 그대로 유지합니다.
  이 분기는 GameFeature의 최초 사전 검사를 거쳐 기존 처리로 진입하며,
  원본 IMC 바인딩의 입력 해제 훅을 사용하지 않으므로 이 훅 전후의 추가 검사 대상이 아닙니다.
- 정책은 동기 읽기 전용 콜백입니다. 보관하거나 매핑 변경에 사용하지 않습니다.
  새 Manager, UObject, 영구 상태, 저장 형식 또는 GameFeature에 대한 Source include는 추가하지 않았습니다.

기존 소스 3개와 문서 2개를 수정했습니다.
아이템·인벤토리·장비·스킬·던전·보상·백엔드·보스의 별도 구현과 에셋은 변경하지 않았습니다.
사용자 지시에 따라 빌드·에디터 실행·PIE·테스트는 수행하지 않았습니다.
실제 키 변경·입력 해제 중 매핑 변경·활성 입력 충돌·저장 동작은 미검증입니다.

변경 전후 원본·diff·내역: Saved/Logs/RPGInputChangePolicy-20261020/.


## 2026-10-20: 같은 Action의 태그별 키 저장 일치

- 개별 키 변경에서 요청 태그 하나만 저장하던 경로를 수정했습니다.
  원본 입력 설정에서 같은 Action으로 해석되는 모든 유효 태그에 같은 DefaultKey/NewKey를 기록합니다.
  다른 태그의 이전 바인딩별 저장값이 시작 복원 순서에 따라 최신 변경을 덮어쓰는 문제를 방지합니다.
- 기존 UDataAsset_InputConfig에 네이티브 읽기 전용 GetInputTagsForAction을 추가했습니다.
  NativeInputActions를 우선하고, 없으면 AbilityInputActions를 조회하는 기존 태그 해석 순서를 따릅니다.
  같은 태그가 다른 Action 항목에도 있으면 실제 조회에서 가려진 항목은 별칭으로 취급하지 않습니다.
  옵션 화면에 숨겨진 항목도 유효 별칭이면 포함하고, 중복 태그는 한 번만 반환합니다.
  새 reflected 함수·타입·Manager는 추가하지 않았습니다.
- 전체 기본값 복원도 같은 별칭 조회를 사용합니다.
  가려진 태그의 저장값을 다른 Action의 기본 키로 덮어쓰지 않습니다.
- 개별 변경과 전체 복원은 별칭 목록을 입력 해제 전에 스냅샷으로 보유하고
  정책 콜백 및 기존 입력 해제 훅 전후에 다시 조회해 비교합니다.
  요청 중 태그가 다른 Action으로 바뀌거나 별칭 목록이 달라지면 이전 요청을 적용·저장하지 않습니다.
- 성공한 개별 변경은 런타임 Key와 메모리의 모든 별칭 저장 항목을 먼저 갱신한 뒤
  기존 파생 컨텍스트·입력 재구성 훅을 호출합니다.
  재구성 콜백이 다른 옵션 저장을 요청해도 같은 키 상태를 보도록 순서를 맞췄습니다.
  작업 소유자와 설정 객체가 유지되면 명시적인 SaveSettings를 한 번 요청합니다.
- 기존 SetKeyBinding은 같은 InputTag/DefaultKey의 중복 레코드가 있으면 전부 갱신합니다.
  시작 시 뒤쪽의 이전 레코드가 최신 키를 덮어쓰지 않도록 하며,
  레코드를 삭제하거나 순서를 바꾸지 않습니다.
- 기존 CustomKeyMappings/FRPGKeyMapping 형식, DefaultKey가 없는 레거시 저장 항목,
  시작 시 레거시 → 바인딩별 복원 순서와 Blueprint API는 유지합니다.
  Modifier·Trigger·형제 키, 기존 스킬 창 대체 키 분기와 게임플레이 입력 훅은 변경하지 않았습니다.

기존 소스 4개와 문서 2개를 수정했습니다.
아이템·인벤토리·장비·스킬·던전·보상·백엔드·보스의 별도 구현과 에셋은 변경하지 않았습니다.
사용자 지시에 따라 빌드·에디터 실행·PIE·테스트는 수행하지 않았습니다.
별칭 태그·중복 저장 레코드·키 재지정·기본값 복원·재실행 복원 동작은 미검증입니다.

변경 전후 원본·diff·내역: Saved/Logs/RPGInputAliases-20261020/.


## 2026-10-20: 옵션 목록과 실제 Action의 일치

- 공통 입력 설정에 네이티브 FindInputActionByTag를 추가했습니다.
  기존 Native 우선 → Ability 조회 순서를 그대로 묶었으며,
  앞서 추가한 별칭 조회도 같은 함수를 사용합니다.
- Options GameFeature의 BuildRows는 표시 항목의 태그를 실제로 조회한 Action과
  항목에 지정된 Action이 일치할 때만 행을 만듭니다.
  같은 태그가 다른 Action에 가려진 항목은 목록에 추가하지 않습니다.
  Action 객체와 기본 키 조합으로 중복을 제거하여 별칭별 중복 행을 만들지 않습니다.
- 행 이름은 실제 선택한 표시 항목의 InputActionName을 사용하고,
  이름이 비어 있으면 해당 Action의 에셋 이름을 사용합니다.
  충돌 안내도 유효한 표시 항목을 우선하며, 없으면 유효한 숨김 항목 또는 에셋 이름을 사용합니다.
  가려진 항목의 이름을 실제 명령 이름으로 쓰지 않습니다.
- FRPGInputOptionBinding 값 모델에 원본 입력 설정과 Action의 약한 참조를 추가했습니다.
  구현 포인터나 강한 전역 참조를 보유하지 않습니다.
  새 TryApplyBinding은 원본 설정·Action·현재 키가 표시 모델과 같은지 확인한 뒤
  기존 컨트롤러의 정책 콜백 경로를 사용합니다.
  동기 입력 해제 훅 전후에도 표시 모델 검사와 기존 키·충돌 검사를 다시 수행합니다.
- 설정·태그의 Action·현재 키가 달라진 오래된 행의 변경 요청은 거절합니다.
  이미 만료된 약한 참조를 비어 있는 레거시 요청으로 취급하지 않습니다.
  두 식별 참조를 처음부터 비워 둔 기존 태그 기반 행은 종전 TryApply 경로를 사용합니다.
- Source 옵션 행은 InitializeOptionBinding으로 모델을 받고 값 기반 변경 API를 호출합니다.
  기존 InitializeRow·InitializeBindingRow C++ 서명과 Blueprint 키 변경/초기화 API는 유지합니다.
  변경 중 행이 재초기화·제거되면 이전 호출의 피드백·변경 알림이 새 행에 반영되지 않도록
  행의 초기화 세대를 확인합니다.
- 목록 재사용과 전체 기본값 복원 확인에도 원본 설정·Action 식별자를 비교합니다.
  태그·기본 키·현재 키가 우연히 같아도 다른 Action이나 설정의 이전 행을 재사용·확정하지 않습니다.

기존 소스 8개와 문서 2개를 수정했습니다.
새 Manager·에셋·저장 형식·reflected 타입은 추가하지 않았습니다.
스킬 창 대체 키 행 추가 분기와 Controller·GameUserSettings 구현은 이번에 변경하지 않았습니다.
아이템·인벤토리·장비·스킬·던전·보상·백엔드·보스의 별도 구현은 변경하지 않았습니다.
사용자 지시에 따라 빌드·에디터 실행·PIE·테스트는 수행하지 않았습니다.
실제 중복 태그·별칭 표시 이름·행 재사용·오래된 행의 변경 거절·기본값 확인 동작은 미검증입니다.

변경 전후 원본·diff·내역: Saved/Logs/RPGInputRows-20261020/.


## 2026-10-21: 실제 옵션 단축키를 표시하는 안내

- RPGOptionsShortcutHint의 고정 F10 문구를 현재 로컬 플레이어의 유효 매핑 조회로 교체했습니다.
  샘플 Experience의 F10 매핑은 계속 F10으로 표시하고, 다른 키로 구성된 단축키는 해당 키 이름을 표시합니다.
- 기존 OptionsMenuComponent에 BlueprintPure GetShortcutKeys를 추가했습니다.
  활성 UI 제공 세션·입력 컴포넌트 등록과 입력 스택 소유권을 확인하고,
  같은 월드의 LocalPlayer가 현재 단축키 컨트롤러를 가리키는 경우에만 키를 조회합니다.
  해당 컨트롤러의 현재 EnhancedPlayerInput이 가진 공개 읽기 전용 매핑 뷰를 사용합니다.
- Feature 참조가 남아 있는 ToggleAction의 유효한 버튼 키만 반환합니다.
  엔진 QueryKeysMappedToAction과 같이 가상 키를 해석하고 중복을 제거합니다.
  일반 키를 먼저, 게임패드 키를 뒤에 두며 각 그룹은 키 이름 순서로 정렬합니다.
  아날로그 축은 버튼 안내에서 제외합니다.
- ShortcutHint의 BlueprintPure GetShortcutText는 현재 HUD·컨트롤러·LocalPlayer·월드를 확인한 뒤
  조회한 키들의 표시 이름을 현지화 가능한 옵션 안내 문구에 연결합니다.
  Slate의 텍스트와 표시 여부도 같은 조회를 사용합니다.
- 단축키 InputComponent만 있고 현재 유효한 키 매핑은 없는 경우 안내를 숨깁니다.
  Feature 참조 해제·UI 세션 정리·입력 스택 해제·월드 전환 중에도 빈 문구를 반환합니다.
  매핑 재구성이 완료되면 다음 UI 조회에서 새 유효 키를 표시하며 매핑 객체나 Trigger/Modifier 참조를 캐시하지 않습니다.
- 키 조회와 안내는 입력을 등록·제거·저장하지 않습니다.
  별도 Manager·전역 상태·타이머·컴포넌트 Tick·GameFeature 간 의존성은 추가하지 않았습니다.
  FKey가 공개 API에 포함되므로 기존 InputCore 모듈 의존성을 Private에서 Public으로 옮겼습니다.

기존 소스 5개와 문서 2개를 수정했습니다.
Source의 Controller·옵션 위젯·GameUserSettings와 기존 단축키 획득·해제·토글 구현은 유지합니다.
아이템·인벤토리·장비·스킬·던전·보상·백엔드·보스의 별도 구현과 에셋은 변경하지 않았습니다.
사용자 지시에 따라 빌드·에디터 실행·PIE·테스트는 수행하지 않았습니다.
실제 안내 렌더링·복수 키·게임패드 이름·매핑 재구성·기능 해제와 맵 전환 중 표시 동작은 미검증입니다.

변경 전후 원본·diff·내역: Saved/Logs/RPGOptionsShortcutHint-20261021/.


## 2026-10-21: 옵션 JSON의 임시 저장과 이전 파일 복구

- 그래픽·사운드가 공유하는 RPGOptionsPersistence의 직접 덮어쓰기를 임시 파일 쓰기 → 이전 파일 백업 → 원본 교체 순서로 변경했습니다.
  기존 Saved/graphic_options.json과 Saved/sound_options.json의 파일명·JSON 구조·필드 이름 호환 처리는 유지합니다.
  각 파일 옆의 .bak는 직전 원본을 보관하는 별도 복구 파일입니다.
- 임시 파일은 같은 Saved 폴더 안에 GUID 이름으로 만들고 NoReplaceExisting으로 독점 생성합니다.
  생성에 성공한 파일만 정리 대상으로 기록하며, UTF-8 BOM 없이 쓴 뒤 Archive의 Close와 오류 상태를 확인합니다.
  이 단계에서 실패하면 원본과 백업을 변경하지 않습니다.
- 기존 원본을 읽고 JSON 파싱·UStruct 변환할 수 있으면 교체 전에 .bak로 복사합니다.
  백업 복사가 실패하면 원본 교체를 시작하지 않습니다.
  원본이 없거나 변환할 수 없는 경우에는 남아 있는 기존 .bak를 덮어쓰지 않습니다.
- 엔진 Move가 대상 파일을 먼저 삭제할 수 있으므로 백업 이후 교체합니다.
  교체 실패 시 변환 가능한 백업을 원본으로 복사하여 복구를 시도하고 실패를 반환합니다.
  복구 복사가 실패하더라도 백업 파일은 그대로 남겨 다음 로드의 복구 경로로 사용합니다.
- LoadGraphics/LoadSound는 원본을 먼저 읽고,
  읽기·JSON 파싱·필드 변환이 실패하면 같은 방식으로 .bak를 읽습니다.
  두 읽기가 모두 실패하면 호출자의 출력 값은 변경하지 않습니다.
  백업으로 읽는 과정에서 원본 파일을 자동 복사·재저장하지 않습니다.
  해상도·화면 모드·음량 등의 도메인 검증·정규화는 기존 Runtime이 계속 담당합니다.
- 성공·실패 반환 시 자신이 생성한 임시 파일만 정리하며,
  임시 파일 정리 실패는 로그에 기록합니다.
  파일 교체는 엔진의 지연 재시도 없이 결과를 받아 기존 옵션 저장 실패 피드백으로 전달합니다.
- 백업 읽기는 이 공통 저장 계층을 사용하는 옵션 경로에 적용합니다.
  DataManager와 INI 저장 구현, 그래픽 미확정 변경과 사운드 적용 경로는 변경하지 않았습니다.
  새 Manager·전역 상태·파일 형식·외부 라이브러리는 추가하지 않았습니다.

기존 소스 2개와 문서 2개를 수정했습니다.
아이템·인벤토리·장비·스킬·던전·보상·백엔드·보스의 별도 구현과 에셋은 변경하지 않았습니다.
사용자 지시에 따라 빌드·에디터 실행·PIE·테스트는 수행하지 않았습니다.
이번 코드 수정에서 실제 옵션 JSON/INI 파일을 저장·복구하지 않았으며,
실제 쓰기 실패·백업 복구·재실행 복원 동작은 미검증입니다.

변경 전후 원본·diff·내역: Saved/Logs/RPGOptionsFileRecovery-20261021/.


## 2026-10-21: 그래픽 적용과 JSON/INI 저장 순서

- 엔진 ApplySettings가 내부적으로 INI를 저장한 뒤 그래픽 Runtime이 INI를 다시 저장하던 중복 경로를 제거했습니다.
  일반 적용과 화면 확인 확정은 적용된 엔진 값 스냅샷 → JSON 저장 → 명시적인 INI SaveSettings 1회 순서를 사용합니다.
  JSON 저장 실패 시 해당 그래픽 요청의 명시적 INI 저장은 생략하고 기존 적용/유지 후 저장 실패 문구를 반환합니다.
- 기존 URPGGameUserSettings에 네이티브 ApplyGraphicsOptionsWithoutSaving을 추가했습니다.
  요청한 해상도·모드·VSync를 적용하고 엔진의 렌더 상태 일괄 갱신과 UI/사운드 알림을 유지하면서,
  이 함수 자체는 INI 저장을 요청하지 않습니다.
  시작 복원에서는 기존처럼 명령행 옵션을 허용하고 UI/사운드 알림을 추가하지 않습니다.
- 프로젝트 설정 객체는 동기 적용 중 이전 화면 값을 보관합니다.
  이때 사운드·입력 등의 SaveSettings 콜백은 기존 해상도·화면 모드·VSync·선호 모드로 INI를 기록한 뒤
  라이브 화면 값을 복원합니다. 다른 옵션 값의 저장 요청 자체는 유지합니다.
  이 마스킹은 동기 적용 중에만 사용하며, 일반 SaveSettings의 기존 동작은 유지합니다.
- 적용 중 기존 ApplySettings·새 적용 함수·화면 미리보기의 재진입을 차단합니다.
  Runtime도 동기 그래픽 요청 동안 적용·미리보기·확정·명시적 복원을 겹쳐 실행하지 않습니다.
  Runtime이 보유하는 추가 상태는 요청 중 여부뿐이며 월드·위젯·설정 객체를 장기 보관하지 않습니다.
- Runtime은 요청 시작 시 고정한 설정 객체를 적용·JSON 값 조회·INI 저장에 사용합니다.
  엔진 콜백 후 설정 객체가 교체되면 이전 요청의 후속 적용·저장을 중단합니다.
  JSON/INI 저장 전후에도 같은 객체와 화면 값인지 확인하고 값이 달라지면 변경 안내를 반환합니다.
- 화면 확인 확정도 같은 JSON/INI 저장 함수를 사용합니다.
  미리보기 생성 후 설정 객체나 소유자가 달라졌다면 기존 취소 경로를 거쳐 확정 대기를 시작하지 않습니다.
  기존 15초 확인·취소·시간 초과 복구와 게임 월드/PIE 시작 정책은 유지합니다.
- 명시적 저장값 복원은 JSON을 다시 쓰지 않고 적용 후 INI 저장을 한 번 요청합니다.
  기본 엔진 GameUserSettings 객체의 적용 호환 경로도 유지하며,
  다른 옵션 저장 시 이전 화면 값을 사용하는 마스킹은 프로젝트 GameUserSettings가 담당합니다.
- JSON/INI 저장은 별개의 API를 사용합니다.
  INI SaveSettings는 성공 여부를 반환하지 않으므로 이번 변경에서 디스크 저장 성공을 별도로 보장하거나 검증하지 않았습니다.
  입력 키 저장·사운드 값 적용·옵션 위젯과 JSON 백업/교체 구현은 이번에 변경하지 않았습니다.

기존 소스 4개와 문서 2개를 수정했습니다.
새 Manager·에셋·저장 형식·reflected 타입은 추가하지 않았습니다.
아이템·인벤토리·장비·스킬·던전·보상·백엔드·보스의 별도 구현은 변경하지 않았습니다.
사용자 지시에 따라 빌드·에디터 실행·PIE·테스트는 수행하지 않았으며 실제 옵션 파일 저장도 실행하지 않았습니다.
적용 콜백·설정 객체 교체·다른 옵션의 저장·JSON 실패 후 재시도·화면 확인 동작은 미검증입니다.

변경 전후 원본·diff·내역: Saved/Logs/RPGGraphicsSaveOrder-20261021/.



## 2026-10-22: 사운드 요청의 적용·저장 대상 고정

- Options GameFeature의 사운드 요청은 시작 시의 Context, GameInstance, 재생 World, 기존 SoundManager,
  GameUserSettings를 동기 호출 동안만 고정합니다. 명시한 Context의 월드와 재생 월드가 다르거나,
  월드 정리·Manager 종료·설정 객체 교체가 감지되면 후속 적용과 저장을 중단합니다.
  Context가 없는 기존 호출은 기존 Manager 탐색을 유지하며, 호출 도중 탐색 대상이 달라지면 중단합니다.
- 전체 적용, 저장, 슬라이더, 음소거, 기본값, 시작 복원, 명시적 저장값 복원은 같은 요청 경로를 사용합니다.
  동기 사운드 요청 도중 재진입한 변경·저장·초기화는 실행하지 않습니다.
  현재 값 조회는 허용하며, 요청 종료 후 새 변경은 정상 처리합니다.
- 저장은 요청 값 복사 → 같은 대상에 적용 → 실제 6개 값 확인 → JSON → 값·대상 재확인 → INI 1회
  → 최종 재확인 순서입니다. JSON 실패 시 이 요청의 INI 저장은 생략합니다.
  콜백에서 값이나 대상이 변경되면 성공으로 보고하지 않습니다.
- 기존 SoundManager는 호출자의 요청 값을 먼저 복사하고, 적용 중 재진입을 차단한 뒤 동일한 설정 객체에 반영합니다.
  믹스 갱신 사이와 알림 후 재생 월드·설정 객체를 확인하고, 알림 후 실제 값이 달라졌어도 실패를 반환합니다.
  기존 재생 월드 조회를 읽기 전용 native API로 공개했으며 다른 GameInstance에 속한 월드는 반환하지 않습니다.
- 월드별 믹스 초기화 후에도 같은 대상인지 확인합니다. JSON 시작 복원은 설정 객체마다 한 번만 수행하고,
  복원이 중단되면 해당 객체의 초기화 표시를 해제하여 이후 호출에서 재시도할 수 있게 합니다.
  시작·기본값·슬라이더·명시적 복원은 자체적으로 파일을 저장하지 않습니다.
- UI 컨트롤과 사운드 재생/믹스 소유권의 역할 분리는 유지합니다.
  새 Manager, reflected 타입, 전역 상태, 장기 보관 UObject, 타이머, 저장 형식은 추가하지 않았습니다.
  요청 중 상태가 달라졌을 때 이미 적용된 믹스나 기록된 JSON을 자동으로 되돌리지는 않습니다.
  JSON과 INI는 별도 저장이며, 다른 시스템이 직접 요청하는 INI 저장은 기존 동작을 유지합니다.
  INI SaveSettings는 결과를 반환하지 않으므로 디스크 영속성은 보장하지 않습니다.

기존 소스 5개와 문서 2개를 수정했습니다.
아이템·인벤토리·장착창·스킬·던전·보상·백엔드·보스의 별도 구현은 수정하지 않았습니다.
사용자 지시에 따라 빌드·에디터·PIE·테스트는 실행하지 않았고 실제 옵션 파일도 저장하지 않았습니다.
월드 전환·동기 콜백 재진입·설정 객체 교체·저장 실패 후 재시도 동작은 미검증입니다.

변경 전후 원본·diff·이력: Saved/Logs/RPGSoundRequestOwnership-20261022/.


## 2026-10-22: 키 변경·기본값 복원의 완료 상태 확인

- Controller가 소유한 단일 authored 바인딩 변경과 전체 기본값 복원에 적용 후 확인을 연결했습니다.
  적용 전에는 기존 런타임 키를, 적용 후에는 요청한 최종 키를 기준으로 같은 검사를 사용합니다.
  원본 Context의 Action·기본 키, 런타임 Context의 Action·키 배열과 태그 별칭이 바뀌면 중단합니다.
- 매핑 재구성·기존 창의 닫기 키 갱신·INI 저장 콜백 이후에도 같은 Controller·LocalPlayer·World,
  입력 Subsystem·Config·Context와 GameUserSettings인지 확인합니다.
  LocalPlayer가 해당 월드에서 다른 Controller를 가리키면 이전 요청은 저장하거나 성공을 반환하지 않습니다.
  저장이 필요한 authored 변경에서 설정 객체가 없는 경우도 준비되지 않은 요청으로 처리합니다.
- 기존 GameUserSettings에 읽기 전용 native MatchesKeyBinding을 추가했습니다.
  요청한 (태그, 기본 키)의 레코드가 있어야 하며, 중복 레코드도 모두 예상 키와 일치해야 합니다.
  적용한 Action의 유효한 별칭 각각에 이 검사를 수행합니다.
  다른 설정이나 관계없는 저장 키는 변경하거나 비교 대상으로 추가하지 않습니다.
- 재구성 이후 상태가 달라지면 이 요청의 INI 저장을 생략합니다.
  INI 저장 이후 상태가 달라져도 성공으로 보고하지 않습니다.
  전체 복원 입력 목록은 설정 객체 조회 이전에 복사하여 엔진 초기화 콜백이 원본 목록을 바꿔도 반복이 무효화되지 않게 했습니다.
- 기존 충돌 정책은 Options GameFeature가, 실제 키 적용·저장 완료 판정은 Controller와 GameUserSettings가 담당합니다.
  기존 스킬 입력 해제·파생 Context 재구성·닫기 키 갱신 호출의 구현과 매개변수는 바꾸지 않았습니다.
  태그별 legacy fallback 분기, 시작 복원 순서, 매핑 Trigger/Modifier, reflected API와 INI 형식도 유지합니다.

기존 소스 4개와 문서 2개를 수정했습니다. 새 Manager·전역 상태·에셋·타이머는 추가하지 않았습니다.
아이템·인벤토리·장착창·스킬·던전·보상·백엔드·보스의 별도 구현은 수정하지 않았습니다.
실패 시 이미 반영된 런타임 키나 저장 파일을 자동으로 되돌리지는 않습니다.
검사는 소유 객체와 authored/runtime 매핑 및 해당 저장 레코드를 대상으로 하며, 엔진의 최종 입력 캐시 전체를 검증하는 기능은 아닙니다.
INI API는 디스크 성공 결과를 반환하지 않습니다.
사용자 지시에 따라 빌드·에디터·PIE·테스트와 실제 옵션 파일 저장은 실행하지 않았습니다.
컨트롤러 교체·매핑 재구성 콜백·저장 값 변경·중복 레코드·저장 콜백 동작은 미검증입니다.

변경 전후 원본·diff·이력: Saved/Logs/RPGInputCommitState-20261022/.


## 2026-10-22: 키 변경 실패 후 목록과 필터 동기화

- 입력 행에 native OnBindingRefreshRequested 알림을 추가했습니다.
  같은 행·소유자의 키 변경 시도가 끝나면 적용 성공 여부와 별개로 현재 목록 조회를 요청합니다.
  기존 OnBindingChanged는 성공한 요청에만 알리는 계약을 유지합니다.
- 옵션 메뉴는 새 알림에서 RefreshMappings를 호출합니다.
  단일 키 변경이 재구성/저장 단계에서 실패해도 현재 행 모델·표시 키·필터·표시 개수를 다시 읽습니다.
  전체 기본값 복원도 결과를 표시한 뒤 같은 목록 조회를 사용합니다.
  Source Config·Action·행 구성이 같으면 위젯 재사용 경로를 사용하며, 구성이 바뀌면 행을 다시 만듭니다.
- 행의 키 적용과 표시 키 갱신에 재진입 및 요청 소유자 확인을 추가했습니다.
  위젯 세대·OwningPlayer·LocalPlayer·현재 Controller·World가 바뀌면 이전 요청의 표시와 알림을 중단합니다.
  오래된 authored 행은 교체된 Config/Action의 키를 기존 명령 이름 아래 읽지 않습니다.
  명시적으로 identity가 없는 기존 행은 종전 primary 바인딩 조회를 유지합니다.
- 메뉴의 목록 조회는 요청한 ScrollBox·행 클래스·아이콘·Config를 동기 호출 동안 고정합니다.
  행 생성/초기화 콜백 뒤 소유자나 구성이 바뀌면 이전 순회를 중단합니다.
  표시 키 일괄 갱신은 중첩 호출을 막고, 같은 행 목록·소유자·월드일 때 필터를 적용합니다.
- 새 알림과 조회는 설정을 적용하거나 저장하지 않습니다.
  실패한 요청의 오류는 행과 메뉴의 native 필터 영역에 유지합니다. 행이 필터에 숨겨져도 메뉴에서 확인할 수 있습니다.
  사용자 지정 UI는 GetLastBindingChangeError를 조회할 수 있습니다.
  새 개별 요청의 결과, 전체 복원 요청, 메뉴 생성/종료 시 메뉴의 이전 개별 오류를 갱신하거나 비웁니다.
  실패를 성공 알림으로 바꾸거나 적용된 키를 자동으로 되돌리지 않습니다.
  임의의 외부 입력 변경을 계속 구독하는 기능이나 별도 재시도 타이머는 추가하지 않았습니다.

기존 UI 소스 4개와 문서 2개를 수정했습니다.
새 Manager·전역 상태·에셋·저장 형식은 추가하지 않았습니다.
메뉴의 마지막 개별 키 변경 오류를 읽는 BlueprintPure GetLastBindingChangeError를 추가했습니다.
아이템·인벤토리·장착창·스킬·던전·보상·백엔드·보스 구현 및 Controller의 실제 키 적용·저장 코드는 수정하지 않았습니다.
기존 Skill Window fallback 행 생성 분기는 유지했습니다.
사용자 지시에 따라 빌드·에디터·PIE·테스트와 실제 옵션 파일 저장은 실행하지 않았습니다.
실패 후 필터/개수, 구성 교체, 행 생성 콜백, 재진입, 포커스·스크롤 동작은 미검증입니다.

변경 전후 원본·diff·이력: Saved/Logs/RPGInputResultRefresh-20261022/.


## 2026-10-23: GameFeature 추가 입력의 플레이어별 Context 사본

- RPGInputMapping의 FRPGFeatureInputMapping에 bUsePlayerOwnedCopy를 추가했습니다.
  기본값 false는 종전의 공유 원본 등록을 유지합니다.
  true로 지정한 항목은 대상 LocalPlayer·World·EnhancedPlayerInput·원본 Context 조합에 속한 transient 사본을 등록합니다.
  이 단계는 GameFeature 단축키 편집을 위한 사본 소유권 기반이며 기존 키 지정 UI에 추가 Context를 노출하는 연결은 아직 하지 않았습니다.
- 같은 플레이어·월드·PlayerInput에서 여러 Action이 같은 원본의 사본을 요청하면 활성 사본 하나를 공유합니다.
  사본도 원본과 같은 CountRegistrations 정책을 사용하므로 각 Action은 자신의 등록 1건만 해제합니다.
  다른 플레이어·월드·PlayerInput에는 별도 사본을 사용합니다.
  마지막 등록이 없어진 뒤에는 원본에서 새 사본을 생성하여 GC 시점에 따라 예전 사본을 재사용하는 동작을 피합니다.
- 엔진의 객체 복제로 Context의 기본/프로필 매핑과 소유한 하위 객체를 복사합니다.
  외부 InputAction 에셋 참조를 유지하며 원본 Context는 변경하지 않습니다.
  사본과 복제된 하위 객체에는 Transient를 적용하고 Public·Standalone 플래그와 외부 패키지 배정은 전파하지 않습니다.
- 등록 기록은 원본과 실제 적용 Context, 우선순위를 구분합니다.
  원본은 등록 수명 동안 보관하여 사본의 원본 식별을 유지합니다.
  월드·플레이어·Subsystem·PlayerInput 및 실제 적용 Context의 추적은 기존 약한 참조를 유지합니다.
  해제 시 원본을 추측하지 않고 기록한 실제 Context만 반환합니다.
- 같은 원본을 공유 에셋 모드와 사본 모드로 함께 등록하려는 요청은 중복 적용하지 않습니다.
  모드·우선순위 충돌 및 아직 로드되지 않은 매핑은 기존 0.25초 갱신 타이머에서 다시 시도합니다.
  이미 등록한 항목의 CountRegistrations는 재시도에서 늘리지 않습니다.
  영구 구성 오류인 CountRegistrations 미설정은 종전처럼 건너뛰고 데이터 검사에서 오류로 처리합니다.
- 복제·등록 콜백 뒤 활성화 세대, 월드·플레이어 소속과 현재 Controller·PlayerInput을 다시 확인합니다.
  원래 요청이 종료됐다면 새 등록을 이어가지 않습니다.
- native 읽기 전용 FindAppliedMappingContext(Player, SourceMapping)는 이 Action의 실제 활성 등록만 조회합니다.
  공유 모드는 원본을, 사본 모드는 플레이어 사본을 const 포인터로 반환하며, 소유자가 달라졌거나 비활성 상태이면 nullptr입니다.
  조회는 객체 생성·키 적용·입력 재구성·저장을 하지 않습니다.

기존 플러그인 소스 2개와 문서 2개를 수정했습니다.
새 Manager·Subsystem·전역 레지스트리·에셋·타이머·Project_RPG 모듈 의존성은 추가하지 않았습니다.
기존 샘플 에셋과 F10 설정은 저장/변경하지 않았으므로 사본 모드를 자동으로 켜지 않습니다.
아이템·인벤토리·장착창·스킬·던전·보상·백엔드·보스 구현과 Source의 옵션/Controller 코드는 수정하지 않았습니다.
UI의 추가 Context 행, 키 저장·재실행 복원, 전체 기본값 복원 연결은 다음 단계입니다.
등록된 항목의 설정 변경은 Action 재활성화 시 반영하며, 외부 코드의 무단 Context 제거를 상시 복구하는 기능은 아닙니다.
사용자 지시에 따라 빌드·에디터·PIE·테스트 및 실제 설정 파일 저장은 실행하지 않았습니다.
복수 플레이어, Action 공유/해제, 충돌 후 재시도, 복제 하위 객체와 GC 동작은 미검증입니다.

변경 전후 원본·diff·이력: Saved/Logs/RPGFeaturePlayerMappingCopies-20261023/.



## 2026-10-23: 추가 GameFeature 입력을 옵션 목록에서 조회

- Source의 native IRPGAdditionalInputProvider 계약으로 활성 Action의 입력을 조회합니다.
  기존 IModularFeatures 등록을 사용하며, 조회는 플러그인을 로드하거나 GameFeature를 활성화하지 않습니다.
  새 Manager나 Subsystem은 추가하지 않았습니다.
- RPGInputMapping Action은 활성화 Context가 있는 동안 공급자로 등록하고 마지막 비활성화 및 BeginDestroy에서 해제합니다.
  FRPGFeatureInputMapping.bShowInInputOptions(기본 true)로 각 Context의 표시 여부를 지정합니다.
  표시 설정은 등록 시 보관하므로 변경 반영에는 Action 재활성화가 필요합니다.
  조회는 현재 LocalPlayer/World/Controller/PlayerInput에 실제 적용된 등록만 반환합니다.
- 옵션 목록은 공급자별 Context를 안정적으로 정렬하고, 같은 원본/적용 Context의 공유 등록을 한 번만 표시합니다.
  원본 Action+기본 키를 기준으로 행을 만들며 엔진의 매핑 표시 이름을 우선 사용합니다.
  기존 기본 입력 Config가 이미 표시하는 Context는 추가 목록에서 제외합니다.
- 행에는 공급자, 원본 Context, 적용 Context, Action의 약한 참조를 보관합니다.
  현재 키 조회 시 활성 등록과 같은 식별 정보인지 다시 확인합니다.
  매핑 개수/Action 배열이 달라졌거나 동일 기본 바인딩의 실제 키가 일관되지 않으면 유효하지 않은 키를 반환합니다.
  목록 새로고침 시 소유자/Context가 달라진 행은 재생성하고 기존 검색·기기·변경 항목 필터를 사용합니다.
- 추가 입력은 이번 단계에서 표시 전용입니다. 사본 모드와 공유 모드 모두 원본/적용 Context를 수정하지 않습니다.
  키 선택·개별 초기화는 비활성화하고 일괄 기본값 복원의 편집 대상에서도 제외합니다.
  외부에서 추가 행의 변경 API를 호출해도 Controller의 기존 태그 경로로 넘기지 않습니다.
  추가 Context의 키 변경, 충돌 정책, 저장/재시작 복원, 일괄 기본값 복원 연결은 후속 구현입니다.
- 실제 Context의 변경이나 늦은 플레이어 등록을 지속 구독하는 기능은 추가하지 않았습니다.
  해당 변화는 메뉴 재개방 또는 기존 RefreshMappings/RefreshDisplayedKeys 호출에서 다시 조회합니다.

기존 소스 9개와 새 native 계약 헤더 1개, 기존 문서 2개를 변경했습니다.
RPGInputMappingRuntime은 공통 계약을 사용하기 위해 Project_RPG public 의존성을 추가했습니다.
RPGOptionsUIRuntime과 RPGInputMappingRuntime 사이의 직접 의존성은 추가하지 않았습니다.
기존 플레이어 사본 옵션의 기본 false, 샘플 에셋과 F10 바인딩, Controller/Settings 저장 경로는 유지합니다.
아이템·인벤토리·장비·스킬·던전·보상·백엔드·보스 구현은 수정하지 않았습니다.
구현에 필요한 엔진 공개 API 및 인터페이스 구현 위치만 확인했고 빌드·에디터/PIE·테스트는 실행하지 않았습니다.
실제 설정 파일과 에셋은 저장하지 않았습니다.

변경 전후 원본·diff·이력: Saved/Logs/RPGAdditionalInputRows-20261023/.



## 2026-10-23: 추가 입력의 개별 키 변경·저장·사본 재생성 복원

- 추가 Context의 편집 가능 여부와 단일 바인딩 변경을 native 공급자 계약으로 연결했습니다.
  RPGOptionsUI는 키 정책을 검사하고 RPGInputMapping Action이 실제 사본 수정·재구성·저장을 수행합니다.
  현재 활성 등록과 원본/적용 Context, Action, 표시된 현재 키가 모두 일치해야 요청을 받습니다.
  bUsePlayerOwnedCopy=true이고 bShowInInputOptions=true인 실제 플레이어 사본만 편집합니다.
  원본을 직접 공유하는 등록, 비영속 원본/Action, 아날로그 매핑은 표시 전용으로 유지합니다.
- 옵션 행의 키 선택과 개별 기본값 버튼/Backspace 복원을 연결했습니다.
  같은 Action+원본 기본 키의 중복 매핑은 함께 변경하고 Trigger·Modifier·다른 매핑은 수정하지 않습니다.
  같은 Context의 새 충돌과 엔진 공개 QueryMapKeyInActiveContextSet이 보고하는 다른 활성 명령의 충돌을 거부합니다.
  원본에 의도된 같은 기본 키 공유는 Context 내부 개별 기본값 복원에서 유지합니다.
  다른 기기 종류의 키, 아날로그 키, 취소 용도의 Esc 선택도 기존 정책대로 제한합니다.
- 기존 GameUserSettings에 별도 Config 배열 FeatureKeyMappings를 추가했습니다.
  (LocalPlayerIndex, 원본 Context 경로, Action 경로, 원본 기본 키)를 키로 사용합니다.
  같은 식별 정보의 중복 레코드는 모두 갱신하며, 기본값 복원도 명시적인 기본 키를 기록합니다.
  로컬 플레이어 인덱스는 GameInstance의 논리 슬롯이며 온라인 계정 식별 정보가 아닙니다.
  런타임 사본/World 경로나 GameplayTag를 저장 키로 사용하지 않고 에셋을 추가 로드하지 않습니다.
- 공유 Settings의 동기 요청 플래그로 같은 사본을 다루는 Action들의 재진입을 막습니다.
  Controller·LocalPlayer·World·PlayerInput·Settings, Context/Action 경로와 전체 키 배열을 요청 중 다시 확인합니다.
  사본 수정 후 즉시 입력을 재구성하고 누른 키는 해제까지 무시합니다.
  키/소유자/저장 레코드가 유지되는 경우에만 요청 자체의 INI 저장을 한 번 호출합니다.
  SaveSettings는 디스크 성공 결과를 반환하지 않으므로 파일 영속성 성공을 별도로 보장하지 않습니다.
  재구성/저장 콜백으로 상태가 달라진 실패 요청을 자동 롤백하지 않습니다.
- 새 플레이어 사본을 엔진에 등록하기 전에 저장 키를 복원합니다.
  재시작·Action 재활성화·PlayerInput 교체로 새 사본을 만들 때 같은 복원 경로를 사용합니다.
  이미 활성화되어 공유하는 플레이어 사본은 다시 복원하거나 덮어쓰지 않습니다.
  Settings 준비 지연/동기 저장 중에는 기존 등록 재시도를 사용합니다.
  잘못된 키/기기 또는 불일치 중복 레코드는 해당 원본 기본 키로 처리합니다.
  복원 계획에 Context 내부의 새 충돌 또는 현재 유효 입력과의 새 충돌이 있으면
  해당 Context의 저장 키 적용을 생략하고 원본 키를 유지하며 로그를 남깁니다. 저장 레코드는 보존합니다.
  이후 활성화되는 다른 Context와의 충돌을 지속 감시·자동 조정하는 기능은 추가하지 않았습니다.
  기존 구현처럼 기본 매핑 배열을 대상으로 하며 별도 Enhanced Input 키 프로필 전환 연동은 후속 범위입니다.
- 이번 단계의 일괄 복원은 기존 기본 입력 Config만 대상으로 유지합니다.
  추가 행이 태그 기반 일괄 복원으로 잘못 전달되지 않도록 목록/개수에서 제외했습니다.
  native 버튼·설명·확인 문구를 기본 조작 범위로 표시하고 추가 입력은 항목별 복원을 안내합니다.
  추가 입력을 포함하는 통합 일괄 복원은 다음 구현입니다.

기존 소스 10개와 문서 2개를 수정했습니다. 새 Manager·Subsystem·파일은 추가하지 않았습니다.
RPGInputMappingRuntime에 직접 사용하는 InputCore public 의존성을 명시했습니다.
기존 공급자 생성자/GetFeatureName의 out-of-line export 선언과 구현을 유지했습니다.
기존 태그 기반 키 저장/초기화, 그래픽/사운드 처리 본문과 Skill Window fallback 행은 유지했습니다.
아이템·인벤토리·장비·스킬·던전·보상·백엔드·보스 구현과 Controller는 수정하지 않았습니다.
bUsePlayerOwnedCopy 기본값은 false이며 기존 샘플 에셋의 모드를 저장/변경하지 않았습니다.
필요한 공개 엔진 API·선언/구현·변경 범위만 확인했습니다. 빌드·에디터/PIE·테스트는 실행하지 않았습니다.
이번 작업 중 실제 사용자 설정 파일과 에셋은 저장하지 않았습니다.

변경 전후 원본·diff·이력: Saved/Logs/RPGAdditionalInputRemap-20261023/.



## 2026-10-23: 기본 입력과 추가 GameFeature 입력의 통합 일괄 복원

- 옵션 메뉴의 일괄 복원 대상에 편집 가능한 추가 입력을 포함했습니다.
  기본 입력만 변경된 경우, 추가 입력만 변경된 경우, 두 종류가 함께 변경된 경우를 같은 경로로 처리합니다.
  검색·기기·변경 항목 필터로 숨긴 행도 포함하며, 공유 원본/아날로그/표시 전용 행은 제외합니다.
  버튼·설명·확인 문구와 개수를 전체 편집 가능한 입력 범위로 되돌렸습니다.
- 확인 요청에는 공급자·원본/적용 Context·Action·기본 키·현재 키·Config/Tag 식별 정보를 보관합니다.
  확인 시 전체 변경 목록과 비교하고 기능별 준비 과정 및 적용 직전에도 다시 조회합니다.
  요청 후 Context/공급자/바인딩 구성이나 현재 키가 달라지면 적용 전에 거부합니다.
- IRPGAdditionalInputRestorePlan은 요청 동안만 사용하는 native 스냅샷 계약입니다.
  각 Action이 자기 플레이어 사본의 전체 매핑과 복원할 인덱스, 저장 식별 정보를 준비합니다.
  준비/검사는 입력과 저장 값을 변경하지 않으며, Commit은 키와 메모리 설정만 반영합니다.
  새 Manager·Subsystem·UObject·타이머·전역 등록부는 추가하지 않았습니다.
  새 native 계약의 생성자/소멸자는 Project_RPG에서 out-of-line으로 export합니다.
- Controller에 추가 계획을 받는 동기 복원 경로를 연결했습니다.
  기존 TryRestoreDefaultKeyBindings 진입점은 유지하고 공통 내부 구현을 사용합니다.
  기본/추가 Context의 키 및 저장 값을 모두 준비된 값으로 반영한 뒤 입력 재구성 1회,
  요청 자체의 Settings.SaveSettings 1회를 수행합니다. 개별 변경 API를 반복 호출하지 않습니다.
  추가 입력만 복원하는 경우에도 기존 기본 입력 Config/런타임 사본이 준비되어 있어야 합니다.
  이 경우 기존 스킬 입력 해제/파생 Context 재구성/Skill Window 갱신 훅은 호출하지 않습니다.
  기본 입력도 복원하는 경우에는 해당 기존 훅과 매개변수를 유지합니다.
- 충돌 정책은 참여하는 모든 Context의 최종 키 배열을 함께 검사합니다.
  기본/추가 Context 사이 또는 추가 Context끼리 서로 바꿔 쓴 키를 동시에 원복할 수 있습니다.
  바꾸지 않는 같은 Context의 키도 검사하고 원본의 의도된 같은 기본 키 공유는 유지합니다.
  계획에 없는 활성 Context의 충돌은 엔진 공개 QueryMapKeyInActiveContextSet으로 검사합니다.
  참여 Context와 기본 입력에서 파생되는 Context의 현재 키는 계획의 최종 키로 평가합니다.
- Controller·LocalPlayer·World·PlayerInput·Settings, 원본/적용 Context, 전체 키 배열,
  Action 경로 및 대상 저장 레코드를 요청 중 확인합니다.
  확장 복원은 반영/입력 재구성/저장 후에도 최종 충돌 정책을 다시 검사합니다.
  커밋 후 새로 변경된 편집 가능 행이 생긴 경우에도 성공으로 처리하지 않습니다.
  공유 Settings의 기존 동기 플래그를 사용해 추가 입력 변경의 재진입을 막고,
  기본 입력 변경도 이 통합 요청 도중에는 거부합니다.
- 기존 단일 추가 입력 변경·개별 복원·사본 생성 시 저장 키 복원은 유지합니다.
  바인딩의 Key만 변경하며 Trigger·Modifier와 원본 에셋을 수정하지 않습니다.
  기본 입력의 유효 태그 별칭과 추가 입력의 로컬 슬롯/Context/Action/기본 키 저장 형식을 사용합니다.
  준비 단계의 거부는 키를 변경하지 않습니다. 반영 후 콜백이 상태를 바꾼 실패 요청은 자동 롤백하지 않습니다.
  INI API는 디스크 성공 여부를 반환하지 않으며, 다른 시스템의 독립적인 저장 요청을 묶는 트랜잭션은 아닙니다.

기존 소스 10개와 문서 2개를 수정했습니다. 소스 파일과 에셋을 새로 생성하지 않았습니다.
아이템·인벤토리·장비·스킬·던전·보상·백엔드·보스 구현 파일은 수정하지 않았습니다.
Controller 수정은 입력 복원 공통 경로와 통합 요청 중 기본 입력 변경 가드로 제한했습니다.
기존 Skill Window fallback 분기/행, Controller의 나머지 구현과 추가 입력 해제·에셋 검증은 유지했습니다.
사본 모드 기본값과 샘플 에셋은 변경하지 않았으며 bUsePlayerOwnedCopy=true인 추가 입력이 편집 대상입니다.
공개 엔진 API와 변경 범위만 확인했고 빌드·에디터/PIE·테스트는 실행하지 않았습니다.
실제 사용자 설정 파일과 에셋은 저장하지 않았습니다.

변경 전후 원본·diff·이력: Saved/Logs/RPGUnifiedInputDefaults-20261023/.



## 2026-10-23: 추가 입력의 변경 알림과 옵션 목록 자동 갱신

- 기존 IRPGAdditionalInputProvider 계약에 플레이어/월드별 native 변경 알림을 연결했습니다.
  delegate와 export 함수는 Project_RPG에 한 번만 정의하며 소유자 목록을 별도로 저장하지 않습니다.
  알림은 무효화 신호입니다. 구독자는 알림 안에서 조회·입력 변경을 하지 않고 후속 작업만 예약합니다.
- RPGInputMapping은 표시 대상 Context의 등록·해제와 개별 키 변경을 알립니다.
  등록 콜백에서 Context가 다시 제거된 경우도 알리고, 기존 등록 재시도/PlayerInput 교체 경로를 사용합니다.
  등록 기록에 OwnerPlayer/OwnerWorld의 약한 참조를 보관해 이전 입력 해제 시의 범위를 유지합니다.
  BeginDestroy로 공급자 등록이 끝나는 경우에도 남아 있던 표시 대상 플레이어를 스냅샷으로 알립니다.
  native 맵 엔트리를 보유한 채 알림 뒤에 재사용하지 않으며, 방송 동안만 플레이어/월드를 보호합니다.
- 개별 변경은 실제 키를 반영한 요청이 끝날 때 알립니다. 무변경 요청은 알리지 않습니다.
  일괄 복원은 계획의 Commit 안에서 방송하지 않고, Controller가 반환한 뒤 임시 계획이 해제될 때 알립니다.
  반영 후 실패한 요청도 실제 키가 바뀌었다면 알립니다. 저장/입력 재구성 횟수를 늘리지 않습니다.
- 입력 옵션 메뉴가 native 알림을 구독하고 자기 LocalPlayer/World의 변경만 처리합니다.
  next-tick 약한 delegate 한 개로 같은 프레임의 여러 알림을 합칩니다.
  예약에는 World/LocalPlayer/Controller와 세대 번호를 보관해 이전 소유자의 콜백을 구분합니다.
  바쁜 동기 갱신/복원 중에는 다음 틱으로 미루며, 반복적인 UI 입력 조회 폴링은 추가하지 않았습니다.
- 숨김/비활성화 시 예약을 취소하고 dirty 상태만 보관합니다.
  표시/재활성화 시 현재 목록을 다시 읽습니다.
  NativeDestruct/BeginDestroy에서는 구독과 예약을 해제합니다.
  수동 RefreshMappings가 이미 변경을 읽었으면 이전 예약을 취소하고, 조회 중 새 알림은 다음 예약으로 남깁니다.
- 행 정의가 그대로이면 위젯과 검색/기기/변경 항목 필터, 스크롤 위치를 재사용합니다.
  키 값만 바뀐 경우도 기존 행의 현재 키를 갱신합니다.
  실제 행 정의 또는 키가 바뀌면 진행 중인 키 선택을 취소하며,
  대기 중인 일괄 복원 확인도 취소하고 다시 요청하도록 안내합니다.
  자동 갱신으로 확인 패널이 닫히면 같은 사용자의 필터/메뉴 쪽으로 포커스를 복구합니다.
  공유 등록 횟수만 바뀌어 실제 행/키가 같으면 선택/확인 상태를 유지합니다.

기존 소스 6개와 문서 2개를 수정했습니다. 새 Manager/Subsystem/UObject 클래스나 에셋은 추가하지 않았습니다.
새 UI 예약은 변경 알림을 합치는 일회성 next-tick 작업이며 기존 입력 등록 재시도 타이머는 유지합니다.
입력 변경/저장/복원 정책과 Controller/Settings, 기존 Skill Window fallback 행은 수정하지 않았습니다.
아이템·인벤토리·장비·스킬·던전·보상·백엔드·보스 구현은 수정하지 않았습니다.
공급자 조회는 알림을 발생시키거나 에셋/플러그인을 로드하지 않습니다.
외부 코드가 직접 Context 내용을 바꾸거나 입력 Subsystem을 수정하는 모든 경우를 감시하는 기능은 아닙니다.
공급자 알림 없이 메뉴 설정/소유자를 변경하는 외부 코드는 기존처럼 적절한 RefreshMappings 호출이 필요합니다.
필요한 delegate/타이머/활성화 API와 변경 범위만 확인했고 빌드·에디터/PIE·테스트는 실행하지 않았습니다.
실제 사용자 설정 파일과 에셋은 저장하지 않았습니다.

변경 전후 원본·diff·이력: Saved/Logs/RPGInputLiveRefresh-20261023/.



## 2026-10-24: Experience 단축키 사본 모드와 개발용 입력 연결

- 공유 ActionSet DA_OptionsShortcut의 전용 IMC_OptionsShortcut 등록을 플레이어 사본 모드로 설정했습니다.
  기본 F10 키와 우선순위, InputAction/MappingContext 원본은 변경하지 않았습니다.
  기존 Experience 조합이 같은 ActionSet을 사용하므로 별도 Actions/기능 복제 없이 적용됩니다.
- 개발 GameMode에 ARPGPlayerController를 상속한 BP_ExperienceDevelopmentInputController를 연결했습니다.
  빈 기본 MappingContext와 InputConfig도 개발 폴더에 새로 만들었습니다.
  기존 캐릭터·아이템·스킬 입력 데이터를 연결/수정하지 않고 추가 입력 옵션을 사용할 수 있는 구성입니다.
  관전 Pawn은 유지합니다. 기본 관전 이동 조작을 RPG 전투 입력 재매핑의 실행 근거로 취급하지 않습니다.
- 기존 개발 맵·Experience·HUD·프로젝트 기본 선택은 저장/변경하지 않았습니다.
  일반 게임 맵과 아이템·장비·스킬·던전·보상·백엔드·보스 구현 파일은 수정하지 않았습니다.
- 개발 실행 스크립트의 프로세스별 InputComponentClass를 RPGInputComponent로 지정했습니다.
  이는 기존 Controller의 CastChecked 전제에 맞춘 설정이며 전역 DefaultInput.ini는 수정하지 않습니다.
- ConfigureExperienceDevelopmentInput.py는 기존 composition/클래스/속성 지원을 먼저 확인합니다.
  명시적인 -RPGConfigureDevelopmentInput 인수가 있을 때만 허용된 다섯 패키지를 저장합니다.
  이미 반영된 올바른 에셋은 보존하고 사용자 정의의 예상 밖 구성을 자동 덮어쓰지 않습니다.
  기본 생성 도구도 이 개발 Controller 구성을 확인해 재사용하도록 연결했습니다.
- 실제 에셋 저장에는 별도 UnrealEditor-Cmd를 사용하고 필요한 새/수정 Blueprint만 컴파일했습니다.
  C++ 빌드·게임/PIE·자동화 테스트는 실행하지 않았습니다.
  INI 저장은 -nowrite -multiprocess로 막았으며 옵션 값 변경이나 실제 키 입력을 실행하지 않았습니다.
  최신 C++ 구현은 빌드 후 사용해야 하며, 이번 에셋 반영을 입력 저장/복원의 실행 검증으로 보고하지 않습니다.

스크립트 3개와 기존 문서 3개를 변경하고, 개발 에셋 3개를 생성하며 기존 에셋 2개를 갱신했습니다.
변경 전후 원본·파일 해시와 적용 결과: Saved/Logs/RPGExperienceInputAssets-20261024/.


## 2026-10-24: 기본 입력과 추가 입력의 공통 변경 알림

기본 입력을 ApplyKeyMapping/TryApplyKeyBinding/UpdateInputMappings 같은 Controller API로 직접 변경하거나
옵션 행 밖에서 기본값을 복원하면, 추가 입력용 알림만 구독하던 메뉴가 현재 키와 필터를 갱신하지 못했습니다.

- 기존 IRPGInputOptionsRuntime 계약에 OnMappingsChanged/NotifyMappingsChanged를 추가했습니다.
  LocalPlayer/World 범위의 native 무효화 신호이며 이벤트 접근과 발행은 구현 모듈을 로드하거나 GameFeature를 활성화하지 않습니다.
  구독자는 콜백 안에서 목록을 조회하거나 키를 변경하지 않고 후속 갱신만 예약해야 합니다.
- Controller의 authored 기본 입력 변경·일괄 복원 경로는 실제 키 변경 여부를 기록합니다.
  요청의 재진입 방지 guard가 풀린 뒤 scope-exit에서 알립니다.
  저장·재구축 후 검사가 실패해도 이미 키가 바뀌었다면 알리며, 반영 전 거절과 동일 키 요청은 알리지 않습니다.
  저장값을 입력 사본에 복원하는 기존 UpdateInputMappings 경로도 같은 내부 변경 함수를 사용합니다.
- 추가 입력 제공자는 기존 feature-only 이벤트를 유지하고 공통 이벤트로도 전달합니다.
  기존 제공자 구독자에게 기본 입력 알림을 추가로 전달하거나 이벤트 계약을 바꾸지 않았습니다.
- 옵션 메뉴는 공통 계약을 구독하고 추가 입력 제공자 헤더에 대한 직접 의존을 제거했습니다.
  기존 플레이어/월드 확인, next-tick 병합, 숨김 중 dirty 보존, 재표시 갱신을 재사용합니다.
  실제 키가 바뀌면 진행 중인 선택·복원 확인을 정리하고, 같은 목록은 재사용하여 필터·스크롤을 유지합니다.
- Controller 변경은 authored 입력 변경 함수 두 곳과 include에 한정했습니다.
  Skill Window fallback 분기와 이후 Controller 구현, 아이템·스킬·던전·보상·백엔드·보스 코드는 보존했습니다.
  외부 코드가 API를 거치지 않고 Context 메모리를 직접 바꾸는 경우를 감시하는 기능은 추가하지 않았습니다.

기존 소스 7개와 문서 2개를 변경했습니다. 새 Manager/Subsystem/에셋/테스트 파일은 없습니다.
변경 범위와 공백 오류만 확인했으며 C++ 빌드·에디터·게임·PIE·자동화 테스트는 실행하지 않았습니다.
변경 전후 원본과 diff: Saved/Logs/RPGPrimaryInputLiveRefresh-20261024/.

## 2026-10-24: 옵션 컴포넌트의 등록 수명 연결

- 임시 OnUnregister에서도 기존 ReleaseOptions 경로로 자기 HUD 제공자와 옵션 위젯, 단축키 InputComponent를 반환합니다.
  단축키는 캡처한 Controller의 입력 스택에서 제거하며 다른 InputComponent는 변경하지 않습니다.
  살아 있는 같은 월드의 HUD는 기존 기본 옵션 창 복구 경로를 사용합니다.
  월드/HUD 종료 시 기본 창을 다시 만들지 않는 기존 조건은 유지합니다.
- 임시 등록 해제에서는 FeatureReferences와 Action별 단축키 참조 수를 보존합니다.
  OnRegister는 이전 예약과 세대를 무효화하고, 기존 next-tick 갱신으로 옵션 창과 단축키를 다시 연결합니다.
  최초 등록에서는 기존 BeginPlay 경로를 사용하며, 기능 참조가 없거나 종료 중이면 다시 획득하지 않습니다.
- 등록 해제 중에는 갱신·획득·예약을 차단합니다.
  엔진의 등록 해제를 먼저 완료한 뒤 제공자를 반환하여 제거 콜백에서 마지막 참조가 해제되고
  DestroyComponent가 호출돼도 같은 컴포넌트를 중복 등록 해제하지 않도록 합니다.
  EndPlay도 예약과 세대를 명시적으로 무효화합니다.
- Controller가 없거나 로컬 플레이어의 목적지 월드 연결이 아직 준비되지 않았다면 기존 next-tick 예약을 사용합니다.
  등록 해제/마지막 참조 해제/EndPlay는 이 대기를 취소하며, 알려진 원격 Controller에는 예약하지 않습니다.
  생성/제공자 콜백 뒤에는 현재 등록 상태와 동일 Controller·LocalPlayer·World 연결을 다시 확인합니다.
- RPGClientUI의 재등록 위젯 복원도 기존 준비 대기 타이머로 예약하도록 조정했습니다.
  OnRegister 안에서 NativeConstruct를 호출하면 기능 해제로 등록 상태가 바뀔 수 있으므로,
  엔진이 등록 완료를 확인한 뒤 위젯 생성 콜백을 실행합니다.

소스 3개와 문서 2개를 변경했습니다. 새 Manager·Subsystem·클래스·에셋은 추가하지 않았습니다.
HUD/Controller, 입력 변경·저장·복원 정책과 보호 대상 RPG 시스템은 수정하지 않았습니다.
엔진의 등록/등록 해제 API와 변경 범위·공백만 확인했습니다. C++ 빌드·에디터/게임·PIE·자동화 테스트는 실행하지 않았습니다.
변경 전후 파일·diff·보존 확인: Saved/Logs/RPGOptionsRegistrationLifecycle-20261024/.

## 2026-10-27: 기본 런타임 입력 복사본의 누락 보완

실제 인증된 던전에서 HUD를 숨겨도 W key value 1·이동 잠금/viewport 무시 0인데 이동이
없었다. 저장 IMC_Default는 26개였고 초기화 추적에서 컨텍스트 복사 직후 0개로 줄어드는
것을 확인했다. 사용자 키를 초기화하거나 이동 함수를 직접 호출하지 않고 이 누락을 수정했다.

`SetupInputComponent`는 기존 Config/IMC 복제 뒤, 사용자 키 복원 전에 private
`RPGInputMappingClone::RestoreMissingRecords`로 **빈 복사본만** 원본 레코드에서 보완한다.
Action/Key와 매핑 설정을 유지하고 modifier/trigger는 복사본 소유로 독립 복제한다.
원본 자체·이미 매핑이 있는 복사본·빈 원본은 바꾸지 않는다. 공유 입력 에셋/기존 바인딩별
설정·충돌/복원 순서·저장 정책과 GameFeature 입력 제공자 계약은 유지한다.

정상 Editor 타깃/링크·새 파일 포함 7개 작업과 집중 `ProjectRPG.Input.RuntimeMappingClone`
1개가 성공했다. 원본/기존 키와 modifier/trigger의 독립성을 확인했고 proof는
`Saved/BuildChecks/PartyHudInput-20261027-021618/RuntimeMappingClone-Native.proof.json`이다.
기동 기존 Error 15줄과 테스트 성공을 구분한다. 같은 DLL의 실제 2인 개발 인증/DB/Iris에서
기본 26개/유효 32개 매핑과 한 로컬 Pawn의 HUD 숨김/표시 이동·기본 공격·viewport 포인터/
포커스/커서 유지가 확인됐다. 원래 입력/에셋/모듈 해시는 보존했고 입력 전용 실행에서
재접속/보상은 하지 않았다. [실행 범위와 증거](DUNGEON_PARTY_COMBAT_HUD.md#런타임-바인딩-복사-보완과-입력-전용-성공).
옵션 창의 모든 재지정/기본값·물리 키/마우스·서버 이동/공격 승인·Steam/Shipping 검증으로
확대하지 않는다. 원인 확인 전 실패/진단 로그도 보존했다.

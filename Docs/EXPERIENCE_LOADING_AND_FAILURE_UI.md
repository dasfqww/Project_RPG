# Experience 로딩 화면과 실패 안내

## 2026-10-13: 공통 화면과 실패 전달 연결

CommonLoadingScreen의 LoadingScreenWidget 설정이 비어 있어 기존 실행에서 null 클래스 오류와 임시 스피너 대체가 발생했다.
에셋 없이 사용하는 native 로딩 화면을 설정하고, Experience 실패가 로컬 플레이어의 화면에 표시되도록 연결했다.
화면은 GameFeature를 준비하는 단계부터 필요하므로 공통 Source에 둔다.

### 변경 파일

| 범위 | 파일 |
| --- | --- |
| 기존 소스 수정 | Public/Manager/Component/RPGExperienceManagerComponent.h |
| 기존 소스 수정 | Private/Manager/Component/RPGExperienceManagerComponent.cpp |
| 기존 소스 수정 | Public/Settings/RPGExperienceSettings.h |
| 기존 설정 수정 | Config/DefaultGame.ini |
| 새 화면 소스 | Public/UI/Loading/RPGExperienceLoadingScreen.h |
| 새 화면 소스 | Private/UI/Loading/RPGExperienceLoadingScreen.cpp |
| 새 안내 소스 | Public/UI/Loading/RPGExperienceFailureNotice.h |
| 새 안내 소스 | Private/UI/Loading/RPGExperienceFailureNotice.cpp |

소스 경로는 Source/Project_RPG 기준이다.
기존 소스 3개와 설정 1개를 수정하고 화면 소스 4개를 추가했다.
새 Manager 또는 Subsystem은 추가하지 않았다.

### 기본 로딩 화면

DefaultGame.ini의 CommonLoadingScreen 설정에 다음 native 클래스를 지정했다.

```ini
[/Script/CommonLoadingScreen.CommonLoadingScreenSettings]
LoadingScreenWidget=/Script/Project_RPG.RPGExperienceLoadingScreen
```

URPGExperienceLoadingScreen은 Slate로 배경·스피너·안내 문구를 구성한다.
새 Blueprint, 텍스처, 맵 또는 Experience 에셋이 필요하지 않다.
기존 CommonLoadingScreen의 최소 표시 시간, 입력 차단, 성능 설정 및 다른 로딩 요청은 기존 관리자가 처리한다.
이 기본 클래스는 Experience 외의 CommonLoadingScreen 요청에서도 사용한다.
프로젝트에서 디자인한 화면으로 바꾸려면 같은 LoadingScreenWidget 설정을 변경할 수 있다.

현재 월드 GameState의 Experience 상태가 Failed이면 스피너를 감추고 실패 안내를 표시한다.
따라서 컨트롤러 도착이나 다른 로딩 요청 때문에 공통 화면이 계속 떠 있어도 실패 문구는 표시할 수 있다.
내부 에셋 경로·플러그인 URL·실패 진단 문자열은 화면에 표시하지 않는다.

### 실패 안내의 소유와 정리

ExperienceManager는 실패한 월드에만 안내를 요청한다.

1. Fail은 오류 문자열을 복사하고 Failed 상태로 전환한다.
2. 기존 Stop으로 Experience 로딩 요청·비동기 로딩·Action 소유를 정리한다.
3. 0.25초 간격의 월드 타이머로 로컬 플레이어와 컨트롤러, 로딩 화면 상태를 확인한다.
4. 공통 로딩 화면이 숨겨진 뒤 컨트롤러별 URPGExperienceFailureNotice를 AddToPlayerScreen(9000)으로 표시한다.
5. 늦게 도착한 컨트롤러에도 표시하며, 컨트롤러가 교체되거나 로컬 플레이어가 제거되면 기존 안내를 제거한다.
6. 다른 공통 로딩 화면이 다시 표시되면 안내를 감추고, 해당 화면이 닫히면 다시 표시한다.
7. Stop·EndPlay에서 자기 타이머와 안내 위젯만 정리한다.

안내 참조는 Transient UPROPERTY로 보관한다.
중복 실패와 종료 후 실패 콜백은 다시 안내를 생성하거나 Failed 알림을 반복하지 않는다.
정리 중 콜백이 실행돼도 멤버 배열을 먼저 비우며, 일반 종료에서는 처리할 위젯의 임시 강한 참조를 유지한다.
전용 서버·게임이 아닌 월드·종료 중 월드·GC에서는 안내를 생성하지 않는다.

안내는 표시용 UI다. 키보드 포커스·커서·입력 모드·이동/시점 잠금을 변경하지 않는다.
확인·재시도·게임 종료 버튼이나 자동 맵 이동을 제공하지 않는다.
기존 Experience는 월드당 한 번 선택하며 실패 상태에서 로딩을 재시작하는 API는 이번 단계에서 추가하지 않았다.

### 서버 실패의 클라이언트 전달

기존 SelectedExperiencePath는 유효한 선택 이후 복제된다.
서버의 선택 단계에서 실패하면 경로가 비어 있어 클라이언트 매니저가 Unassigned로 남을 수 있었다.
이 경로를 위해 bServerExperienceFailed를 추가로 복제한다.

- 서버가 Fail에 들어가면 실패 비트를 설정하고 GameState의 갱신을 요청한다.
- 클라이언트의 OnRep는 기존 Fail·Stop·Failed 알림 경로를 사용한다.
- BeginPlay 전에 실패 비트를 받았으면 BeginPlay에서 처리한다.
- 경로가 먼저 도착해 로딩 중이거나 이미 로컬 준비가 끝난 경우에도 이후 서버 실패를 처리한다.
- 실패 비트가 먼저 도착해 Failed로 전환되면 나중에 도착한 경로로 새 로딩을 시작하지 않는다.
- 서버의 내부 오류 내용은 복제하지 않는다. 클라이언트 로그는 일반적인 서버 준비 실패 사유를 사용한다.

이 처리는 Experience 선택과 기능 준비 실패를 대상으로 한다.
던전 접속·백엔드·보상 오류를 이 안내로 자동 통합하지 않았다.
새 비트의 실제 네트워크 전달과 순서별 실행은 아직 확인하지 않았다.

### 설정

Project Settings → RPG Experiences → Loading:

- Show Loading Failure Notice: 공통 로딩 화면 이후의 표시용 안내 사용. 기본 true.
- Loading Failure Message: 사용자에게 보여 줄 문구. 기본값은 게임 재실행 안내.
- Show Loading Screen While Loading, Load Timeout Seconds: 기존 설정과 동작 유지.

Show Loading Failure Notice를 끄면 실패 후 안내 위젯을 만들지 않는다.
기본 로딩 화면 자체의 실패 상태 표시는 계속 제공한다.
원래 로그와 GetLastError, OnExperienceLoadFailed는 상세 진단과 기존 소비자 연결에 사용한다.

### 빌드와 한계

- UE 5.8.1 / Project_RPGEditor / Win64 / Development 사본 빌드 성공.
- 선택 모듈: Project_RPG, RPGOptionsUIRuntime, RPGClientUIRuntime, RPGClientPresentationRuntime.
- 11개 작업, 47.19초, 종료 코드 0. 새 화면과 생성 코드·ExperienceManager·설정의 컴파일 및 Core DLL 링크 완료.
- 변경한 소스·설정 8개는 원본과 사본의 SHA256을 대조했다.
- 다른 소스는 기존 사본의 고정된 입력을 사용한다. 최신 원본 전체 Editor 빌드 결과가 아니다.
- 원본 DLL 교체·에디터/PIE 제어·테스트 추가 또는 실행은 수행하지 않았다.
- 실제 글꼴·화면 표시, 다른 로딩 요청과 공존, 늦은 컨트롤러·교체, 네트워크·패키징 실행은 미확인이다.
- 원본 에디터에 새 클래스와 복제 속성을 적용하려면 일반 Editor 빌드 후 재실행한다.

기존 아이템·인벤토리·장비·스킬·던전·보상·백엔드·보스 소스와 기존 에셋은 이 작업에서 수정하지 않았다.
Config/DefaultGame.ini의 기존 내용은 바이트 그대로 유지하고 로딩 화면 설정만 덧붙였다.
다른 설정 파일과 CommonLoadingScreen 플러그인 소스도 수정하지 않았다.

작업 전 백업·빌드·소스/설정/에셋 대조 기록: Saved/Logs/RPGExperiencePresentation-20261013/.


## 2026-10-14: 실패 UI 실행 검증과 표시 이벤트 보완

위 빌드와 한계 항목은 최초 구현 당시의 기록이다.
후속 작업에서 잘못된 ID·없는 에셋·안내 비활성화·별도 로딩 요청 유지의 네 케이스가 통과했다.
실제 한글 문구와 스피너 숨김, 안내 개수, 입력/실제 포커스 유지, EndPlay 정리를 확인했다.

공통 로딩 화면이 나타날 때 기존 안내가 다음 타이머 갱신까지 Visible로 남는 상태를 보완했다.
ExperienceManager는 표시 이벤트에서 안내를 즉시 숨기고 자기 구독을 Stop/EndPlay에서 해제한다.
화면 종료 후에는 기존 0.25초 타이머로 다시 표시한다.
네트워크 실패 전달·늦은 접속·컨트롤러 교체·패키징 확인은 여전히 남아 있다.

[최종 코드·검증 결과와 캡처](EXPERIENCE_FAILURE_UI_VERIFICATION.md)를 참조한다.


## 2026-10-14: 서버 실패 전달과 맵 이동 후 정리 실행 확인

개발용 전용 서버와 클라이언트 2개를 실제 Iris로 연결하여 초기 실패와 Ready 이후 실패를 확인했다.
늦은 접속에서도 실패 상태를 전달했고 내부 서버 진단을 클라이언트에 복제하지 않았다.
실패한 월드에서 정상 Experience로 맵을 다시 열고 한 번 더 재로드하여 이전 안내와 로딩 요청 정리를 확인했다.
최종 세 케이스와 게임 프로세스 9개가 모두 통과했다.
동일 개발 맵의 non-seamless 재로드 결과이며 임의의 복제 순서·seamless travel·로딩 중 이동은 미확인이다.

[검증 조건·로그·캡처·재현 방법](EXPERIENCE_NETWORK_TRAVEL_VERIFICATION.md)을 참조한다.


## 2026-10-15: 로딩 중 이동과 seamless 이동 확인

LoadingTravel, SeamlessTravel, LoadingSeamlessTravel의 세 케이스가 실제 Iris 전용 서버·클라이언트 실행에서 통과했다.
게임 프로세스 9개가 정상 종료됐고 Manager 정리 18건, 이전 완료 콜백 24개의 전달 후 새 Experience 유지가 확인됐다.
seamless 이동의 연결은 유지됐으며 GameModeBase가 교체한 컨트롤러와 HUD에 새 UI가 연결됐다.

공통 HUD의 옵션 종료 정리, 옵션 GameFeature의 원래 월드 확인, 공통 위젯 GameFeature의 컨트롤러 등록 확인을 보완했다.
동일 개발 맵과 개발용 엔진 컨트롤러를 사용한 결과이며 실제 플레이용 맵·컨트롤러와 패키징은 후속 범위다.

[변경 코드·최종 로그·화면·재현 방법](EXPERIENCE_TRAVEL_RACE_VERIFICATION.md)에 기록했다.

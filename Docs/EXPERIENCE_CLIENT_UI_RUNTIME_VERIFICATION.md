# Experience 공통 클라이언트 UI 실행 확인

## 2026-10-14: 개발용 맵의 실제 렌더링과 입력 복구 확인

개발용 L_ExperienceDevelopment를 사본 프로젝트의 별도 게임 프로세스에서 실행했다.
NullRHI 대신 실제 RHI와 Slate를 사용하는 RenderOffscreen 실행이며,
뷰포트에 F10 키 이벤트를 보내 기존 EnhancedInput 매핑과 옵션 바인딩을 거친다.
사용자의 에디터 또는 PIE를 제어하지 않았다.

## 결과

| 확인 항목 | 최종 결과 |
| --- | --- |
| 맵 선택 액터가 DA_CommonClientDevelopment 선택 | 통과 |
| 네 GameFeature와 네 Action 준비 완료 | 통과 |
| native CommonLoadingScreen 화면·스피너·한글 문구 표시 | 상태 검사와 화면 캡처 확인 |
| 로딩 완료 후 F10 안내 표시 | 화면 캡처 확인 |
| F10으로 옵션 창 열기·닫기 2회 | 통과 |
| 같은 옵션 위젯 재사용, 뷰포트 중복 인스턴스 없음 | 통과 |
| 옵션 본문의 실제 크기와 화면 내 위치 | 통과 |
| 열 때 커서·입력 모드·옵션 Slate 포커스 적용 | 통과 |
| 닫을 때 기존 커서·입력 모드·실제 Slate 포커스 복구 | 두 회 모두 통과 |
| 종료 시 UI·입력 매핑 및 네 Action 소유권 해제 | 같은 Session의 종료 로그 확인 |
| 최종 게임 프로세스 | 종료 코드 0 |
| 최종 로그의 fatal·assert·ensure | 발견되지 않음 |

닫힘 상태 비교는 커서, IgnoreInput, 마우스 캡처·잠금 모드,
캡처 중 커서 숨김, 포커스 변경 시 입력 플러시 정책과 실제 Slate 포커스를 비교한다.
검증 시작에는 GameOnly와 SViewport 포커스를 기준 상태로 명시한다.
다른 창의 포커스를 보존하는 경로, 여러 로컬 플레이어와 컨트롤러 교체는 이번 실행 범위가 아니다.

## 추가한 파일

- Source/Project_RPG/Private/Tests/RPGExperienceClientUIProbe.cpp
- Scripts/Tests/Run-ExperienceClientUIProof.ps1

native 검증 코드는 WITH_DEV_AUTOMATION_TESTS 범위에만 컴파일된다.
새 reflected 타입 또는 Manager는 추가하지 않았다.
Console command RPG.Experience.ClientUIProof는 -RPGExperienceUIProof를 지정한 경우에만 동작한다.
출력 위치는 실행 프로젝트의 Saved 디렉터리 안으로 제한한다.
FTSTicker가 단일 실행의 관찰 상태를 보관하고 완료·실패 후 자체 게임 프로세스를 종료한다.

기존 게임플레이 구현과 에셋을 변경하는 수정은 없다.
실제 옵션 UI·입력 경로를 검증하기 위한 전용 소스와 스크립트를 추가한 단계다.

## 표시 프레임을 기다리는 검사

첫 상태 검사에서는 Visible·입력·포커스가 정상이어도 옵션 본문이 없는 캡처가 나왔다.
당시 상태 검사 통과를 실제 본문 표시 확인으로 해석하지 않았다.
후속 실행에서 위젯의 위치·크기·투명도를 기록하고 본문 표시를 확인했다.
초기 캡처 타이밍 영향으로 보이며, 첫 실행의 정확한 원인을 독립적으로 확정한 것은 아니다.

최종 검증은 메뉴의 Visible 변경을 관찰한 뒤 최소 세 프레임을 기다린다.
TabSwitcher 본문이 양의 크기와 투명도를 갖고 뷰포트 안에 겹치는지도 확인한다.
옵션 캡처 파일이 저장된 뒤에 닫기 입력을 보낸다.
최종 로딩·준비·옵션·닫힘 캡처 네 장을 직접 확인했다.
본문 레이아웃 확인과 캡처 확인을 완료한 최종 실행을 이번 단계의 결과로 사용한다.

## 실행과 확인 범위

일반 Editor 빌드에 검증 소스가 포함된 뒤 실행한다.

```powershell
& 'C:/UE5/Project_RPG/Scripts/Tests/Run-ExperienceClientUIProof.ps1'
```

이번 실제 실행에는 사본 프로젝트 경로를 지정했다.

```powershell
& 'C:/UE5/Project_RPG/Scripts/Tests/Run-ExperienceClientUIProof.ps1' -ProjectRoot 'C:/UE5/Project_RPG/Saved/ManagerLifetimeBuild-20261012' -TimeoutSeconds 300
```

기본 엔진 GameInstance·LocalPlayer, CommonGameViewportClient,
EnhancedInput의 PlayerInput·InputComponent를 프로세스 인수로 지정한다.
Steam은 사용하지 않으며 기본 프로젝트 ini를 저장하지 않는다.
사본 Saved가 사용되므로 원본 사용자의 저장 설정을 검증용으로 공유하지 않는다.

키 이벤트는 GameViewportClient InputKey에 전달한다.
물리 키보드 → OS → Slate 키 이벤트 전달은 이 실행으로 검증하지 않았다.
옵션의 Apply·Back 버튼, 각 설정의 변경·저장, 마우스 클릭·드래그도 실행하지 않았다.
서버·클라이언트 연결, 실패 안내, 늦은 접속, 맵 이동과 쿠킹은 후속 작업이다.

## 사본 빌드와 런타임 준비

검증 native 소스는 처음에 입력 장치 조회의 직접 ApplicationCore 참조로 링크에 실패했다.
Engine의 UInputDeviceLibrary 공개 API로 수정하여 모듈 의존성을 추가하지 않았다.
최종 Core 사본 빌드는 3개 작업, 7.51초, 종료 코드 0이다.

실제 렌더링 시작에 CommonStartupLoadingScreen이 필요하여 사본에 해당 모듈을 빌드했다.
준비 빌드는 7개 작업, 14.13초, 종료 코드 0이었다.
모듈을 선택한 사본 빌드에는 런타임 .modules 목록이 없으므로
현재 엔진 BuildId 55116800과 일치하는 원본 목록과 누락 DLL을 사본에 준비했다.
이미 빌드한 사본 DLL은 덮어쓰지 않았다. BuildId를 바꾸지 않았다.
비활성·다른 플랫폼의 과거 엔진 모듈 목록은 사용하지 않았다.
이를 원본 전체 최신 빌드·패키징 확인으로 해석하지 않는다.

콘텐츠는 사본의 junction으로 원본을 참조한다.
게임 실행에서는 에셋 생성·컴파일·저장을 요청하지 않았다.
실행 중 원본 프로젝트의 별도 던전 빌드가 진행되어 사본 빌드가 잠금을 기다렸다.
그 원본 빌드의 변경은 이번 사본 빌드의 변경과 별도로 기록한다.

## 근거 파일

최종 원래 출력:
Saved/ManagerLifetimeBuild-20261012/Saved/Logs/ExperienceClientUI-20261013-201610-5f978f13/

보관본:
Saved/Logs/RPGExperienceClientUIProof-20261013/FinalRuntime/

- Report.json: 입력 스냅샷, 두 회의 결과, 실제 본문 레이아웃
- Game.log: 선택·준비·입력 적용·종료 소유권 로그
- ProcessResult.json: 최종 게임 프로세스 종료 코드
- Loading.png, Ready.png, OptionsOpen.png, Closed.png: 원본 캡처 그대로 보관
- ChangesReport.json: 별도 상위 기록의 변경 범위·해시·동시 변경 내역

![옵션 창 실제 실행 화면](../Saved/Logs/RPGExperienceClientUIProof-20261013/FinalRuntime/OptionsOpen.png)

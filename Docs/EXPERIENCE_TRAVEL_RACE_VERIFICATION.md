# Experience 로딩 중 이동과 seamless 이동 검증

최종 확인: 2026-10-15. 대상은 Source의 공통 Experience와 개발 맵이다.

## 실행 결과

전용 서버 1개와 클라이언트 2개를 별도 게임 프로세스로 연결하고 실제 Iris를 사용했다.
각 케이스에서 동일한 개발 맵을 다시 열었다.

| 케이스 | 이동 전 → 이동 후 | 결과 |
| --- | --- | --- |
| LoadingTravel | Loading → 일반 이동 → Ready → 이전 완료 콜백 전달 | 통과 |
| SeamlessTravel | Ready → seamless 이동 → Ready | 통과 |
| LoadingSeamlessTravel | Loading → seamless 이동 → Ready → 이전 완료 콜백 전달 | 통과 |

최종 실행의 게임 프로세스 9개가 모두 종료 코드 0이다.
Manager EndPlay 정리 18건, 정상 Ready Session 12개, Session 소유권 해제 18건을 확인했다.
두 로딩 이동 케이스에서 이전 완료 콜백 24개가 Current=0, Ending=1로 처리됐다.
새 Session이 Ready가 된 뒤 이전 Session의 Feature 소유권을 해제해도 네 GameFeature는 Active를 유지했다.
처음 Loading이었던 Session은 Ready로 바뀌지 않았고 등록된 Action도 0개였다.

새 월드의 클라이언트별 실패 안내는 0개, F10 안내와 옵션 위젯은 각각 1개다.
이전 월드에서 기록한 위젯은 뷰포트에 남지 않았고 Manager의 로딩 요청도 해제됐다.
일반 이동의 서버 Ready 기록은 클라이언트 재접속보다 앞서므로 연결 인원이 0이었다.
이후 Settled 기록과 두 클라이언트의 Ready 기록에서 인원 2명을 확인했다.

Fatal/assert/ensure, Iris/Net Error, AddToPlayerScreen 오류는 최종 로그에 없다.
PNG 16장을 저장했으며 대표 화면 6장을 직접 확인했다.
로딩 화면의 한글 문구와 진행 표시, 이동·이전 콜백 전달 후 F10 안내 한 개를 확인했다.

## 공통 UI 수정

- [RPGHUD](../Source/Project_RPG/Private/UI/HUD/RPGHUD.cpp): 컨트롤러가 해당 월드의 LocalPlayer에 등록된 뒤 기본 옵션 위젯을 만든다. EndPlay에서 옵션 입력 세션과 위젯을 정리하고 공급자 연결을 먼저 해제한다.
- [옵션 GameFeature 컴포넌트](../Plugins/GameFeatures/RPGOptionsUI/Source/RPGOptionsUIRuntime/Private/RPGOptionsMenuComponent.cpp): 옵션을 제공했던 월드를 약한 참조로 기록한다. 같은 월드에서 기능을 제거할 때 기본 위젯을 복원하며, 이동한 HUD에서는 복원하지 않는다.
- [공통 위젯 GameFeature 컴포넌트](../Plugins/GameFeatures/RPGClientUI/Source/RPGClientUIRuntime/Private/RPGClientUIWidgetComponent.cpp): seamless 이동 중 컨트롤러 목록 등록을 확인하고 UMG 소유자가 유효한 위젯을 화면에 추가한다.

seamless 이동은 HUD를 옮긴 뒤 새 월드의 컨트롤러 목록을 구성한다.
이 사이에 기존 Action이 새 월드의 HUD를 관찰할 수 있다.
기존 코드에서는 이 시점의 위젯 추가 오류와 이동한 HUD에서의 기본 옵션 복원·잔류가 발생했다.
최종 수정 후 두 seamless 케이스에서 오류와 중복이 사라졌다.

## seamless 객체 확인 기준

개발 맵은 UE 5.8.1의 GameModeBase를 사용한다.
로컬 엔진 소스의 HandleSeamlessTravelPlayer는 새 PlayerController를 생성하고 SwapPlayerControllers로 Player와 연결을 넘긴다.
검증은 이 엔진 동작에 맞춰 실제 객체를 비교한다.

- 두 seamless 케이스의 서버·클라이언트 모두 기존 NetDriver와 NetConnection이 유지됐다.
- 두 클라이언트의 PlayerController와 HUD는 교체됐다.
- 같은 LocalPlayer의 새 컨트롤러가 새 월드·실제 ServerConnection·HUD·옵션 위젯에 연결됐는지 확인했다.
- 서버에는 로컬 컨트롤러와 HUD가 없으므로 로컬 UI 관련 필드는 해당 역할에서 적용되지 않는다.

LocalControllerPreserved는 관찰값이며 클라이언트의 새 월드에서는 false다.
LocalControllerReady, LocalControllerReplaced, HUDReplaced가 교체와 연결 완료의 근거다.
객체 이름은 재사용될 수 있으므로 동일성 판정에는 약한 UObject 참조를 사용했다.
실제 플레이용 GameMode의 컨트롤러 유지 정책은 별도 적용 범위다.

## 이전 완료 콜백을 재현하는 방법

[ExperienceManager의 LoadFeatures](../Source/Project_RPG/Private/Manager/Component/RPGExperienceManagerComponent.cpp)에 개발 전용 전달 지연을 추가했다.
WITH_DEV_AUTOMATION_TESTS와 명시적인 RPGExperienceNetworkProof 실행 인수가 필요하며 개발 맵·케이스·역할·Saved 경로를 제한한다.
플러그인 로딩 성공 후 첫 Session의 완료 콜백 네 개를 보관한다.
새 월드 Ready를 확인한 뒤 실행 스크립트의 ReleaseDeferred 제어 파일로 전달한다.

실제 플러그인 로딩은 완료된 상태이고 Experience는 완료 처리 전 Loading 상태다.
따라서 확인한 경합 조건은 월드 종료 이후 도착하는 완료 콜백과 이전 소유권 해제다.
실제 디스크 IO 지연이나 임의의 복제 순서·패킷 손실은 후속 검증 범위다.

정상 완료 처리 본문은 작업 전과 공백을 제외한 내용이 같다.
생성자를 포함한 Manager 함수 24개 중 LoadFeatures만 변경됐고 Manager 헤더는 그대로다.
새 Manager, reflected 검증 클래스, 에셋과 설정 변경은 없다.
기존 [native 관찰 코드](../Source/Project_RPG/Private/Tests/RPGExperienceNetworkProbe.cpp)와 [실행 스크립트](../Scripts/Tests/Run-ExperienceNetworkTravelProof.ps1)를 확장했다.

## 빌드와 재현

UE 5.8.1 / Win64 / Development 사본 빌드가 성공했다.
최종 빌드는 Project_RPG, RPGOptionsUIRuntime, RPGClientUIRuntime의 18개 작업, 68.52초, 종료 코드 0이다.
변경한 소스 7개의 원본·사본 SHA256을 대조했다.
다른 입력은 기존 격리 사본의 고정된 소스를 사용하므로 최신 원본 전체 프로젝트 빌드 결과로 해석하지 않는다.

일반 Editor 빌드에 검증 코드가 포함된 뒤 다음처럼 실행한다.

~~~powershell
& 'C:/UE5/Project_RPG/Scripts/Tests/Run-ExperienceNetworkTravelProof.ps1' -ProjectRoot 'C:/UE5/Project_RPG/Saved/ManagerLifetimeBuild-20261012' -TimeoutSeconds 300 -Cases @('LoadingTravel','SeamlessTravel','LoadingSeamlessTravel')
~~~

서버는 NullRHI, 클라이언트는 RenderOffscreen으로 실행한다.
Null 온라인 서비스와 개발용 엔진 GameInstance·LocalPlayer 설정을 프로세스 인수로 사용했다.
DDC는 InstalledEnginePak·ZenLocal·Local을 사용한다.
이 실행에서는 UDP 19763을 사용했고 사용자 에디터나 PIE를 제어하지 않았다.
원본 DLL 교체는 이 작업에서 수행하지 않았다.
원본 에디터에 공통 UI 수명 변경을 적용할 때에는 일반 Editor 빌드와 재실행이 필요하다.

## 기록과 적용 범위

기록: Saved/Logs/RPGExperienceTravelRaces-20261014/.

- [최종 세 케이스 요약](../Saved/Logs/RPGExperienceTravelRaces-20261014/FinalRuntime/Summary.json)
- [프로세스·Session·객체·콜백·소스 대조](../Saved/Logs/RPGExperienceTravelRaces-20261014/VerificationChecks.json)
- [이번 변경 파일과 동시 작업 파일](../Saved/Logs/RPGExperienceTravelRaces-20261014/ChangesReport.json)
- FinalRuntime/케이스/역할: Report.json, 관찰 시점 JSON, Game.log, ProcessResult.json, PNG, 완료 콜백 식별 파일
- Build.log / BuildResult.json / RuntimeResult.json: 최종 빌드와 실행 상태
- Before / SourcesAfter: 이번에 저장한 파일의 작업 전·후 내용
- Attempt1~3: 초기 전달 지연 조건, 컨트롤러 유지 조건, UI 수명 보완 전 기록

설정과 기존 관련 에셋 650개는 시작 시점 해시와 일치한다.
이번에 저장한 소스는 공통 Experience·HUD·옵션·위젯·검증 코드에 한정된다.
아이템·인벤토리·장비 창·스킬·던전·보상·백엔드·보스 작업은 그대로 이어갈 수 있다.
동시 작업으로 바뀐 파일은 별도로 기록했고 덮어쓰지 않았다.

실행 범위는 로컬 IP 전용 서버, 프로세스당 LocalPlayer 1명, 같은 개발 맵, 성공한 GameFeature의 완료 전달 지연이다.
다른 맵 간 이동, Listen server, 여러 LocalPlayer, 실제 플레이용 컨트롤러 정책, F10 입력의 이동 후 재실행, 지연·패킷 손실, 쿠킹·패키징은 별도 확인이 필요하다.

![로딩 중 seamless 이동 전](../Saved/Logs/RPGExperienceTravelRaces-20261014/FinalRuntime/LoadingSeamlessTravel/Client/Loading-W1.png)

![이전 콜백 전달 후 F10 안내 한 개](../Saved/Logs/RPGExperienceTravelRaces-20261014/FinalRuntime/LoadingSeamlessTravel/LateClient/Settled-W2.png)

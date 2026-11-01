# Experience 네트워크 실패 전달과 맵 이동 실행 검증

검증일: 2026-10-14. 대상은 Source의 공통 Experience와 개발용 맵이다.

## 실제 실행 결과

각 케이스를 전용 서버 1개와 클라이언트 2개의 별도 프로세스로 실행했다.
서버와 클라이언트의 GameNetDriver가 실제 Iris를 사용하는지 확인했다.
클라이언트는 서버의 실패 비트와 네트워크 스냅샷을 복제로 받았다.

| 케이스 | 실행 흐름 | 결과 |
| --- | --- | --- |
| EarlySelectionFailure | 서버의 잘못된 ID 선택 → 첫 클라이언트 접속 → 늦은 접속 | 두 클라이언트 모두 Failed, Ready 없음 |
| PostReadyFailure | 첫 클라이언트 Ready → 서버에서 실패 주입 → 늦은 접속 | 첫 클라이언트 Ready→Failed, 늦은 클라이언트는 Ready 없음 |
| FailureToReadyTravel | 두 클라이언트 Failed → 정상 Experience로 맵 재로드 → 같은 맵 재로드 | 두 클라이언트 모두 두 번 Ready, 이전 월드 정리 통과 |

최종 실행의 세 케이스가 모두 통과했다.
게임 프로세스 9개의 종료 코드는 모두 0이며 Manager EndPlay 정리 15건을 확인했다.
Fatal/assert/ensure 및 Iris/Net Error 로그는 없다.

초기 선택 실패에서는 SelectedExperiencePath가 빈 상태에서도 실패 비트가 전달됐다.
준비 후 실패에서는 기존의 유효한 경로가 유지된 상태로 실패가 전달됐다.
그 서버에 늦게 접속한 클라이언트는 경로와 실패 상태를 함께 받아 새 준비를 시작하지 않았다.
클라이언트의 진단은 일반적인 서버 준비 실패 사유이며 서버의 잘못된 ID 문자열을 받지 않았다.

맵 이동은 동일한 L_ExperienceDevelopment의 non-seamless 재로드 두 번이다.
첫 재로드는 실패한 월드에서 DA_CommonClientDevelopment를 사용하는 새 월드로 이동한다.
두 번째는 정상 준비된 월드를 다시 연다.
각 프로세스에서 서로 다른 Manager 세 개를 관찰했다.
두 클라이언트 모두 재접속했고, 정상 월드의 스냅샷에 연결된 플레이어 2명이 기록됐다.

새 월드의 실패 비트는 false, 상세 오류는 빈 문자열이었다.
클라이언트별 실패 안내는 0개이고 F10 안내는 정확히 1개였다.
이전 월드의 안내·UI가 뷰포트에 남지 않았으며 Manager의 로딩 요청도 해제됐다.
각 정상 Session의 네 Action과 Feature 소유권 해제를 같은 Session ID의 로그에서 확인했다.
입력 매핑은 정상 월드에 적용되고 최종 종료 시 Remaining=0으로 해제됐다.
중간 재로드에서는 컨트롤러의 PlayerInput이 교체되므로 기존 Action이 교체된 입력 객체의 등록을 제거하지 않는 경로를 사용한다.

실제 클라이언트 캡처 11장을 보관했고 대표 다섯 장을 직접 확인했다.

## 추가한 코드와 실패 주입 방식

- [개발 전용 native 관찰 코드](../Source/Project_RPG/Private/Tests/RPGExperienceNetworkProbe.cpp)
- [서버·클라이언트 실행 스크립트](../Scripts/Tests/Run-ExperienceNetworkTravelProof.ps1)

새 reflected 타입, Manager, GameFeature Action 또는 에셋은 추가하지 않았다.
기존 ExperienceManager와 GameState의 기능 구현은 수정하지 않았다.
검증은 WITH_DEV_AUTOMATION_TESTS와 -RPGExperienceNetworkProof가 지정된 실행에서만 활성화된다.

초기 실패는 실제 서버의 잘못된 Experience ID 선택으로 발생시킨다.
준비 후 실패는 검증 코드가 서버에서 기존 실패 비트를 설정하고 기존 RepNotify→Fail 경로를 호출한다.
클라이언트의 비트나 상태를 직접 설정하거나 클라이언트 RepNotify를 수동 호출하지 않는다.
이 주입은 준비 후 서버 실패의 전달·정리를 확인하기 위한 제어된 조건이다.
실제 백엔드 오류나 특정 GameFeature 로딩 실패를 재현한 결과로 해석하지 않는다.

서버의 맵 이동 역시 이 개발 검증 코드가 요청한다.
현재 월드의 Experience를 재선택하는 API나 사용자용 재시도 기능을 추가한 작업은 아니다.

## 빌드와 재현

UE 5.8.1 / Project_RPGEditor / Win64 / Development 사본 빌드 성공.
최초 빌드는 3개 작업, 23.16초였다.
저장 경로 검사 보완 후 최종 빌드는 3개 작업, 6.40초, 종료 코드 0이다.
새 native 소스는 원본과 사본의 SHA256이 일치한다.
관찰 대상인 기존 Manager·GameState 네 소스 파일도 사본과 대조했다.
사본의 다른 소스는 기존 고정된 입력이므로 최신 원본 전체 빌드 결과는 아니다.

검증 소스를 포함한 일반 Editor 빌드 후 다음과 같이 실행한다.

```powershell
& 'C:/UE5/Project_RPG/Scripts/Tests/Run-ExperienceNetworkTravelProof.ps1'
```

이번 실제 실행에는 기존 사본 프로젝트를 지정했다.

```powershell
& 'C:/UE5/Project_RPG/Scripts/Tests/Run-ExperienceNetworkTravelProof.ps1' -ProjectRoot 'C:/UE5/Project_RPG/Saved/ManagerLifetimeBuild-20261012' -TimeoutSeconds 300
```

-Cases PostReadyFailure처럼 선택 실행할 수 있다.
기본 UDP 포트 19763부터 비어 있는 포트를 선택한다.
클라이언트는 RenderOffscreen, 전용 서버는 NullRHI로 실행한다.
Steam 대신 Null 서비스를 사용하고 개발용 엔진 GameInstance/LocalPlayer 설정을 프로세스 인수로 지정한다.
사용자 경로를 프로세스별로 분리하며 사용자의 에디터·PIE는 제어하지 않는다.
원본 DLL을 교체하지 않았다.

초기 실행에서 프로세스별 UserDir 변경 때문에 보고서의 Saved 경로 검사가 거부됐다.
검증 코드의 검사 기준을 프로젝트 Saved로 보완했다.
그 다음 실행은 새 로컬 DDC의 셰이더 컴파일로 클라이언트가 접속하기 전에 시간 제한에 걸렸다.
설치 엔진 pak과 기존 Zen 캐시를 읽도록 실행 설정을 보완한 최종 실행에서 세 케이스가 연속 통과했다.
두 초기 실패를 기능 검증 성공으로 포함하지 않는다.

## 기록과 남은 범위

기록: Saved/Logs/RPGExperienceNetworkTravel-20261014/

- [최종 세 케이스 요약](../Saved/Logs/RPGExperienceNetworkTravel-20261014/FinalRuntime/Summary.json)
- [프로세스·월드·Session 대조](../Saved/Logs/RPGExperienceNetworkTravel-20261014/VerificationChecks.json)
- [파일 변경과 해시](../Saved/Logs/RPGExperienceNetworkTravel-20261014/ChangesReport.json)
- FinalRuntime/케이스/역할: native Report.json, 관찰 시점 JSON, Game.log, ProcessResult.json, 원본 PNG
- Build.log / BuildResult.json / RuntimeResult.json: 최종 빌드·실행 상태
- Attempt1/Attempt2 기록: 초기 실패와 보완 전 실행
- FilesBefore.json: 작업 시작 시 소스·설정·관련 에셋 대조 기준

아이템·인벤토리·장비·스킬·던전·보상·백엔드·보스 코드는 이 작업에서 저장하지 않았다.
동시 작업의 기존 변경과 새 파일은 ChangesReport에 별도로 기록하며 유지한다.
이 단계의 추가 파일은 검증 소스·스크립트·문서이며 기존 문서 두 개를 갱신했다.

이번 검증은 로컬 IP 전용 서버, 프로세스당 로컬 플레이어 1명, 같은 개발 맵의 non-seamless 재로드다.
Listen server, seamless travel, 여러 로컬 플레이어/컨트롤러 교체, 준비 중 맵 이동,
복제 속성 도착 순서를 인위적으로 바꾼 실행, 지연·패킷 손실, 쿠킹·패키징은 아직 확인하지 않았다.

![서버 실패를 받은 늦은 클라이언트](../Saved/Logs/RPGExperienceNetworkTravel-20261014/FinalRuntime/EarlySelectionFailure/LateClient/Failed-W1.png)

![두 번째 맵 재로드 후 F10 안내 한 개](../Saved/Logs/RPGExperienceNetworkTravel-20261014/FinalRuntime/FailureToReadyTravel/LateClient/Ready-W3.png)


## 2026-10-15: 로딩 중 이동과 seamless 이동 확인

LoadingTravel, SeamlessTravel, LoadingSeamlessTravel의 세 케이스가 실제 Iris 전용 서버·클라이언트 실행에서 통과했다.
게임 프로세스 9개가 정상 종료됐고 Manager 정리 18건, 이전 완료 콜백 24개의 전달 후 새 Experience 유지가 확인됐다.
seamless 이동의 연결은 유지됐으며 GameModeBase가 교체한 컨트롤러와 HUD에 새 UI가 연결됐다.

공통 HUD의 옵션 종료 정리, 옵션 GameFeature의 원래 월드 확인, 공통 위젯 GameFeature의 컨트롤러 등록 확인을 보완했다.
동일 개발 맵과 개발용 엔진 컨트롤러를 사용한 결과이며 실제 플레이용 맵·컨트롤러와 패키징은 후속 범위다.

[변경 코드·최종 로그·화면·재현 방법](EXPERIENCE_TRAVEL_RACE_VERIFICATION.md)에 기록했다.

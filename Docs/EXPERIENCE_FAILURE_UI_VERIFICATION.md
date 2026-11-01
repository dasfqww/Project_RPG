# Experience 실패 UI 실행 검증

검증일: 2026-10-14. 대상은 Source의 공통 Experience/UI 기반이다.

## 코드 보완

선택 실패 직후에는 공통 로딩 화면이 아직 표시되지 않아 실패 안내가 먼저 생성될 수 있었다.
이후 로딩 화면이 나타나면 기존 0.25초 갱신 전까지 안내의 Visible 상태가 남았다.
최초 검증이 이 상태를 검출했다. 실제 화면에서 문구가 겹쳐 보였다는 의미는 아니다.

기존 RPGExperienceManagerComponent가 CommonLoadingScreen의 표시 이벤트를 구독하도록 보완했다.
Show 이벤트에서 자기 안내를 즉시 Collapsed로 바꾸고, 화면이 닫힌 뒤에는 기존 타이머로 다시 표시한다.
Hide 이벤트는 로딩 관리자의 표시 상태 갱신보다 먼저 전달되므로 해당 콜백에서 안내를 다시 표시하지 않는다.
Stop/EndPlay 정리 경로에서 자기 구독 핸들, 타이머, 안내를 해제한다.
별도 Manager는 추가하지 않았다.

이번 단계의 파일 변경:

- [기존 Manager 헤더](../Source/Project_RPG/Public/Manager/Component/RPGExperienceManagerComponent.h)
- [기존 Manager 구현](../Source/Project_RPG/Private/Manager/Component/RPGExperienceManagerComponent.cpp)
- [기존 개발 검증 소스 확장](../Source/Project_RPG/Private/Tests/RPGExperienceClientUIProbe.cpp)
- [새 실패 UI 실행 스크립트](../Scripts/Tests/Run-ExperienceFailureUIProof.ps1)

정상 UI 검증 본문과 명령은 이전 파일과 일치한다.
선택·비동기 로딩·Feature/Action 준비·실패 복제의 기존 함수도 변경하지 않았다.
검증 코드는 WITH_DEV_AUTOMATION_TESTS와 명시적 실행 인수로 제한한다.

## 네 시나리오 결과

| 시나리오 | 주입 조건 | 결과 |
| --- | --- | --- |
| InvalidId | 등록되지 않은 PrimaryAssetId | 실패 화면 → 안내 1개, 통과 |
| MissingAsset | 없는 Experience 에셋 경로 | 실패 화면 → 안내 1개, 통과 |
| NoticeDisabled | 없는 경로 + 안내 설정 false | 실패 화면 유지, 이후 안내 0개, 통과 |
| IndependentLoadingHold | 없는 경로 + 별도 LoadingProcessTask | 별도 요청이 유지되는 3초 동안 로딩 유지, 해제 후 안내 1개, 통과 |

각 게임 프로세스의 종료 코드는 0이다.
실패 제목과 설정 문구를 실제 Slate 트리에서 확인했고 표시 중인 스피너가 없는지 검사했다.
내부 에셋 경로와 진단 문자열은 사용자 문구에 노출되지 않았다.
실패 사유는 개발용 보고서와 로그에만 기록한다.

GameOnly·숨긴 커서·SViewport 포커스를 기준으로 입력 상태를 비교했다.
로딩 화면이 닫힌 뒤 안내가 커서, 뷰포트 입력/마우스 설정, 실제 Slate 포커스를 바꾸지 않았다.
안내는 HitTestInvisible이고 포커스를 받지 않았다.
추가 1.5초 갱신 이후에도 개수와 입력 상태가 유지됐다.
모든 케이스의 Manager EndPlay에서 안내 0개를 확인했다.
Fatal/assert/ensure 및 예상하지 않은 Ready 로그는 없다.

표시 후 최소 세 프레임과 캡처 저장을 기다린다.
로딩 화면 종료 직후 안내가 아직 Collapsed인 경우에는 기존 타이머가 다시 표시할 때까지 기다린다.
대표 캡처 다섯 장을 직접 확인했다.

## 빌드와 실행

UE 5.8.1 / Project_RPGEditor / Win64 / Development 사본 빌드 성공.
최종 검증 코드 보완 빌드는 3개 작업, 7.01초, 종료 코드 0이다.
표시 이벤트 보완을 포함한 직전 빌드도 8개 작업, 23.69초, 종료 코드 0이었다.
변경한 소스 세 파일은 원본과 사본의 SHA256이 일치한다.

일반 Editor 빌드에 변경 사항이 포함된 뒤 전체 검증을 실행한다.

```powershell
& 'C:/UE5/Project_RPG/Scripts/Tests/Run-ExperienceFailureUIProof.ps1' -TimeoutSeconds 300
```

이번에는 기존 사본을 지정했다. 에디터/PIE 제어 없이 별도의 숨긴 게임 프로세스를 순차 실행했다.

```powershell
& 'C:/UE5/Project_RPG/Scripts/Tests/Run-ExperienceFailureUIProof.ps1' -ProjectRoot 'C:/UE5/Project_RPG/Saved/ManagerLifetimeBuild-20261012' -TimeoutSeconds 300
```

선택 실행은 -Cases NoticeDisabled,IndependentLoadingHold처럼 지정한다.
프로세스 인수로 테스트용 GameInstance/LocalPlayer/뷰포트 및 안내 설정을 지정하며 기본 ini를 저장하지 않는다.

최초 두 케이스 통과 이후 실행이 세 번째 케이스의 엔진 초기화에서 중단됐다.
기존 성공 결과를 보관하고 같은 사본 빌드로 나머지 두 케이스만 재개했다.
최종 Summary는 두 실행의 성공 케이스를 합친 결과이며 단일 연속 실행으로 기록하지 않는다.

## 근거와 남은 범위

기록: Saved/Logs/RPGExperienceFailureUI-20261014/

- [최종 네 케이스 요약](../Saved/Logs/RPGExperienceFailureUI-20261014/FinalRuntime/Summary.json)
- [변경·해시·동시 변경 내역](../Saved/Logs/RPGExperienceFailureUI-20261014/ChangesReport.json)
- FinalRuntime/각 케이스: Report.json, Game.log, ProcessResult.json, 원본 PNG
- Build.log / BuildResult.json: 사본 빌드
- Attempt1/Attempt2 기록: 보완 전 실패와 재빌드 이력
- InterruptedRuntime.json: 중단과 재개 내역

이번 단계는 기존 소스 3개 수정, 스크립트 1개 추가, 문서 1개 추가 및 기존 문서 2개 갱신이다.
기존 에셋·설정·GameFeature 소스 및 보호 중인 게임플레이 코드는 이 작업에서 저장하지 않았다.
파일 대조에서 별도 퀵슬롯·던전 검증 파일과 신규 보스 소스의 동시 변경을 발견해 보고서에 기록했으며 해당 변경은 유지했다.

실행은 단일 로컬 플레이어의 독립 게임 프로세스다.
서버 실패 비트의 실제 네트워크 전달·도착 순서, 늦은 접속, 다중 로컬 플레이어/컨트롤러 교체,
맵 이동, 패키징은 아직 검증하지 않았다.
원본 DLL은 이 사본 빌드로 교체하지 않았다. 원본 적용에는 일반 Editor 빌드와 재실행이 필요하다.

![실패 로딩 화면](../Saved/Logs/RPGExperienceFailureUI-20261014/FinalRuntime/InvalidId/FailedLoading.png)

![로딩 종료 후 표시용 안내](../Saved/Logs/RPGExperienceFailureUI-20261014/FinalRuntime/IndependentLoadingHold/FailureNotice.png)

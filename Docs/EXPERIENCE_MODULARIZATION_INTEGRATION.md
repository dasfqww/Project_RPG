# GameFeature / Experience 통합 결과

2026-10-25 기준. Source와 네 공통 GameFeature의 구현·연결 및 개발용 구성 확인 결과입니다.

## 반영한 연결

- 프런트엔드의 전체 Experience ID 전달과 에디터 ID 형식 검사.
- 프런트엔드 MapID·ExperienceID의 등록 경로 및 배포 제외 참조 검사.
- RPGOptionsUI의 CommonUI 플러그인 의존성.
- /Game/System/Experiences/ActionSets 폴더에 한정한 네 공통 GameFeature 참조 정책.
  프로젝트 콘텐츠가 ActionSet을 참조하는 경로도 명시했습니다. 전역 참조 제한은 유지합니다.
- 현재 개발 Controller에 맞춘 RPGInputComponent 실행 설정과 opt-in 통합 확인 코드.
  새 Manager·Subsystem·런타임 기능 클래스는 추가하지 않았습니다.

## 확인 결과

| 확인 | 결과 |
| --- | --- |
| 고정 사본의 UE 5.8 Editor 빌드 | 통과 |
| 프런트엔드 전체 ID·URL 선택 우선순위 | 통과 |
| ClientUI 등록 해제·재등록과 위젯 중복 방지 | 통과 |
| 옵션 입력 반환·기본 창 재사용·재바인딩 | 통과 |
| 카메라 기본 시점 복귀·재등록·요청 우선순위·독립 해제 | 통과 |
| F10→F8 변경, 실제 뷰포트 키 전달·커서 복구 | 통과 |
| 새 프로세스의 F8 복원→통합 기본값 복원→다음 프로세스의 F10 복원 | 통과 |
| 두 LocalPlayer의 UIManager·입력 사본 분리, 두 번째 제거 후 첫 번째 유지 | 통과 |
| 로딩 화면·옵션 본문 렌더링과 두 차례 F10 열기/닫기·실제 Slate 포커스 복구 | 통과 |
| 서버 준비 후 실패 전달·늦은 접속, 로딩 중 seamless 이동 | 서버 1·클라이언트 2의 두 시나리오 통과 |
| 에셋 데이터 검사 | 정상 6개와 의도한 미등록/배포 제외 ID 거부 2개 통과 |
| Windows 공통 콘텐츠 쿠킹 | 종료 0, 패키지 435개, 필수 패키지 누락 0, 개발/예제 패키지 0 |

쿠킹 산출물에 네 GameFeatureData와 공통 ActionSet·Experience·입력 에셋이 포함되어 있습니다.
키 입력은 엔진 뷰포트 API로 전달했습니다. 물리 키보드나 두 플레이어의 동시 화면/장치 입력을 확인한 결과는 아닙니다.

## 최신 소스 통합 마무리

2026-10-25 03:16에 캡처한 소스·설정·스크립트·플러그인 입력 1,308개를 기준으로 확인했습니다.

- 원본의 새 파일 28개를 포함해 사본 입력 55개를 갱신했습니다.
- 원본에서 이미 제거된 RPGHealthComponent 소스 2개는 사본의 컴파일 대상에서 제외하고 기록 폴더에 보관했습니다.
- Project_RPGEditor Win64 Development 빌드: 종료 코드 0, 205개 작업 통과.
- 원본에서 캡처한 소스를 그대로 빌드했습니다. 사본의 소스 보정 없이 입력 해시가 모두 유지됐습니다.
- 맵 적용은 사용자 요청으로 보류했습니다. 이 단계의 원본 변경 파일은 이 통합 문서입니다.
- 마무리 시점의 원본에는 스냅샷 이후 수정 14개·추가 12개·삭제 0개가 관찰됐습니다.
  이 변경들은 보존했으며, 위 빌드 결과는 캡처 시점의 사본에 해당합니다.

기존 기능·네트워크·쿠킹 결과는 위 표와 이전 검증 기록을 따릅니다.
이번 빌드와 변경 관찰 기록: Saved/Logs/RPGCurrentIntegration-20261025-031631/IntegrationReport.json.

## 재현

최신 소스를 일반 Editor 빌드한 별도 사본을 지정합니다.

```powershell
& .\Scripts\Tests\Run-ExperienceModularizationProof.ps1 -ProjectRoot 'C:\UE5\Project_RPG\Saved\ManagerLifetimeBuild-20261012'
```

기본 실행은 Write, Restore, VerifyDefault, Players 네 모드를 순서대로 실행합니다.
Players만 확인하려면 -Modes Players를 지정합니다.
Write/Restore/VerifyDefault는 같은 실행 폴더의 전용 GAMEUSERSETTINGSINI를 사용합니다.
native 코드는 WITH_DEV_AUTOMATION_TESTS와 명시적 실행 인수가 있을 때만 동작합니다.

렌더링 UI와 네트워크 확인은 기존 Run-ExperienceClientUIProof.ps1,
Run-ExperienceNetworkTravelProof.ps1을 같은 사본에서 실행했습니다.
에셋 검사 스크립트·빌드·쿠킹 인수와 결과는 아래 기록에 보관했습니다.

## 확인 범위

- 기능·쿠킹 결과와 최신 소스 통합 빌드는 각각 고정한 사본 기준입니다. 원본 에디터를 종료하거나 원본 DLL을 교체하지 않았습니다.
  각 빌드 시작 후 원본에서 진행된 별도 변경은 해당 사본에 자동 합치지 않았습니다.
- 초기 빌드의 기존 던전 테스트 구문 오류는 사본에만 보정했습니다.
  이 작업에서 보호 대상 원본 코드는 수정하지 않았습니다. 이후 원본의 동시 변경도 보존했습니다.
- 쿠킹은 공통 기능 범위입니다. 사본의 아이템·스킬·던전 자동 쿠킹 규칙을 일시 제외하고 종료 후 복원했습니다.
  Experience/ActionSet의 AlwaysCook와 Development/Examples의 NeverCook 규칙은 유지했습니다.
- 첫 쿠킹 두 번은 기존 Landmass 시작 패키지의 에디터 리소스 참조 오류로 실패했습니다.
  최종 확인에서는 /Landmass를 해당 프로세스의 NeverCook 대상으로 추가하여 공통 콘텐츠를 분리했습니다.
  원본의 Landmass나 전체 게임 패키징 설정은 변경하지 않았습니다.
- 원본 맵·게임플레이 에셋은 저장하지 않았습니다. 에셋 검사 fixture는 사본 안의 전용 콘텐츠에만 저장했습니다.
- 실제 게임 맵 적용은 사용자 요청으로 보류했습니다. 이 통합 단계에서 MapExperiences·전역 기본 Experience·맵 에셋을 변경하지 않았습니다.
  추후 대상 맵이 정해지면 기존 MapExperiences/선택 액터로 연결할 수 있습니다.
  전체 게임 Shipping 실행 파일 생성·실기기 배포 확인은 별도 작업입니다.

기록: Saved/Logs/RPGModularizationClosure-20261025/ChangesReport.json.

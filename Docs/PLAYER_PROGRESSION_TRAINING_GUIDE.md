# 첫 훈련의 조작 안내 HUD

작성일: 2026-10-29

상태: **안내 위젯/HUD 생성·회수 연결 구현·분리 native 3개 통과. 실제 훈련 콘텐츠 배치는 후속.**

[서버 조작 목표 컴포넌트](PLAYER_PROGRESSION_TRAINING.md)의 소유자 진행을 읽는 [URPGTrainingGuideWidget](../Source/Project_RPG/Public/UI/Content/RPGTrainingGuideWidget.h)을 추가했다. 목표 인정·스킬 부여·영속 성장·던전 완료 보고는 기존 소유자가 담당한다. UI에 목표 완료 설정 함수나 RPC를 추가하지 않았다.

## 표시와 조작 안내

- 기본 공격·회피·돌진 베기·끝까지 모은 차지의 네 행과 완료 개수 `0/4`~`4/4`를 표시한다. 각 행의 `[대기]`/`[완료]` 문구로 색상 없이도 상태를 구분한다.
- 첫 미완료 동작을 추천하지만 완료 순서를 제한하지 않는다. 이미 다른 동작을 마쳤다면 서버가 보낸 해당 행을 바로 완료로 표시한다.
- 기본 공격/회피의 키는 기존 Controller 입력 조회를 사용한다. 돌진/차지는 현재 Pawn의 기존 스킬 슬롯에서 해당 SkillTag를 찾은 뒤 공통 슬롯 키 표시 경로로 읽는다. 고정 Q/E/F나 별도 키 설정 목록을 만들지 않는다. 키 재지정·슬롯 이동은 다음 표시 갱신에 반영한다.
- 키/스킬 배치를 확인할 수 없으면 입력 설정이나 슬롯 배치를 확인하도록 안내한다. 새 스킬을 부여하거나 빈 슬롯을 채우지 않는다.
- 네 목표가 모두 완료돼도 `조작 실습 완료! 다음 안내를 기다려 주세요.`라고 표시한다. 경험치 지급·레벨 상승·파티 던전 완료가 확정됐다는 문구는 표시하지 않는다.

## HUD 연결과 수명

[ARPGHUD](../Source/Project_RPG/Private/UI/HUD/RPGHUD.cpp)가 로컬 플레이어 화면에 안내 위젯 한 개를 생성하고 EndPlay에서 회수한다. 공통 HUD에는 직업 이름이나 훈련 판정 분기를 추가하지 않았다. 안내는 기존 훈련 컴포넌트가 있는 현재 소유 Pawn의 `bActive` 상태에서만 나타난다.

위젯의 루트는 입력을 가로채지 않고 갱신을 유지한다. 내부 카드만 숨겨 늦은 컴포넌트/복제 도착이나 Pawn 교체를 다시 발견한다. 소유 Controller/현재 World·Pawn 연결이 달라지거나, 진행이 비활성화되거나, 컴포넌트 제거·사망·World 종료·위젯 해제가 발생하면 이전 참조와 문구를 지운다. 위젯은 영속 목표 상태를 별도로 소유하지 않는다.

## 검증 범위

근거는 `Saved/BuildChecks/TrainingGuideHUD-20261029/`에 보존한다. 새 UCLASS와 HUD 변경의 UHT 생성 7개, 개별 컴파일에서 확인한 두 오류를 보완한 뒤 NoLink 105개·종료 0, 정상 UBT의 실행 없는 링크 응답 생성·종료 0을 확인했다. 링크 입력 451개를 자체 사본으로 고정하고 OUT/PDB를 별도 RuntimeProject로 바꿔 DLL 링크를 성공시켰다. 공유 DLL 교체나 정상 Editor 전체 타깃 링크 성공을 뜻하지 않는다.

최종 실행은 `Native-593124c572d243f0bf44ceb1aec69d3f/Native.proof.json`이며 **3개 Success·UE/호스트 종료 0**이다. 실제 로드된 분리 DLL 경로/해시를 관측했다. 기존 기동 Error 15줄은 테스트 탐색 이전이고 탐색 이후 Error는 0이다. 검사 결과는 다음과 같다.

- `ProgressAndCompletion`: 비활성/늦은 활성·완료 개수·순서 밖 완료·다음 목표 추천·완료 문구·비활성 전환 시 이전 문구 제거. UI가 진행을 변경하거나 경험치 지급을 주장하지 않음을 확인했다.
- `PawnAndWidgetLifetime`: 실제 로컬 Controller의 소유 Pawn 전환/해제·컴포넌트 지연 추가/제거·위젯 해제/재구성·ASC 체력 0에서 이전 대상과 문구를 정리했다.
- `CurrentInputAndSlots`: 실제 입력 설정에서 J→K 키 재지정을 반영하고, 스킬을 세 번째 슬롯에 둔 P 입력을 읽었다. 차지는 같은 실제 슬롯 키와 길게 누르기/완충 후 놓기 안내를 표시하고 슬롯을 수정하지 않았다. 조회 불가 상태의 안내도 확인했다.

집중 native 검사는 실제 로컬 Controller/Pawn·UMG 트리와 입력 설정/스킬 슬롯을 사용하지만 진행 상태는 테스트에서만 소유자 복제 모형으로 주입한다. 서버 GAS 정상 종료 인정이나 실제 OwnerOnly 복제 전송·실물 입력·GPU 배치를 검증하는 검사가 아니다. HUD의 화면 자동 생성/배치는 이번에는 소스·컴파일 연결로 확인했으며 실제 훈련 맵에서의 화면 실행을 대신하지 않는다.

담당 소스 5개/읽기 보존 14개와 고정한 링크 입력 451개·분리 DLL이 검사 전후 같다. 원본 게임 DLL은 유지했고 자체 UE 프로세스만 자연 종료했다. 공유 빌드 자원은 입력 고정 후 사용 해제했다. 이후 시작된 다른 담당의 로비 뒤로가기 소스 변경은 이 고정 빌드의 검증 대상이 아니다. 상세 집계는 같은 폴더의 `ImplementationResult.json`에 연결한다.

초기 개별 컴파일의 `UWidget::Slot` 이름 충돌/전처리 매크로 문제와, 링크 입력 고정 도구가 플랫폼의 상대 라이브러리 이름까지 절대 경로로 요구했던 실패는 별도 로그에 남겼다. 해당 실패를 성공 결과에 합산하지 않는다.

## 실제 콘텐츠에 남은 연결

현재 플레이어 BP·맵에 목표 컴포넌트를 자동 부착하지 않았다. 이 HUD만 추가해도 기존 보스 맵이 훈련장으로 바뀌지 않는다. 실제 훈련 Pawn/맵·서버 정의를 준비하고 인증/성장 복원이 완료된 `Progression.Training`/Normal에서 목표를 관찰하도록 배치해야 한다.

서버의 파티별 완료 조건·기존 완료 보고·[경험치 전용 보상](DUNGEON_REWARD_INVENTORY_FIRST.md#훈련-경험치-전용-보상-에셋-저장-2026-10-29) 연결은 별도다. 네 목표의 표시 완료를 경험치 정산 증거로 사용하지 않는다. 정상 공유 모듈 반영·재실행과 실제 훈련/네트워크·수동 화면 배치 확인도 남아 있다.

## 2026-10-30 실제 HUD 생성·회수와 검증 후 자동 정리

[RPGTrainingGuideViewportTests.cpp](../Source/Project_RPG/Private/Tests/RPGTrainingGuideViewportTests.cpp)에 `NativeViewportLifecycle`을 추가했다. 기본 로컬 Controller·가상 창과 실제 엔진 플레이어 레이어를 준비하고 **제품 ARPGHUD의 BeginPlay/EndPlay**를 실행한다. 검사에서 가이드 위젯을 직접 생성하거나 강제로 갱신하지 않는다.

- HUD가 소유 플레이어의 가이드를 정확히 한 개 생성하고 실제 Slate 트리에 부착한다. 훈련 문맥이 없는 기본 Controller에서는 안내 카드가 숨겨져 있고 입력/포커스를 가로채지 않는다.
- HUD를 파괴하면 기존 가이드는 뷰포트 등록과 Slate 트리에서 제거된다. 새 HUD로 교체해도 한 개만 존재하고 이전 위젯을 재사용하지 않는다. 마지막 HUD 종료 뒤에는 가이드가 남지 않는다.
- 활성 진행/입력·소유 Pawn 상태의 기존 3개 검사는 반복하지 않았다. 이번에는 기본 Controller로 HUD 수명만 확인하며, 서버 GAS 목표 승인·OwnerOnly 복제·실제 훈련 맵/화면 렌더링·키/패드 조작·정산을 증명하지 않는다. 제품 코드와 다른 담당의 훈련장/전용 Pawn 에셋은 수정하지 않았다.

공유 UHT/UBT·Intermediate를 쓰지 않았다. 당시 완료된 TrainingGuideHUD 링크 입력 451개를 읽기 고정하고, 자체 PCH/소스/생성 헤더 사본에서 새 검사 CPP 한 개만 CL 컴파일해 분리 링크했다. 최종 실행은 `Saved/Tests/TrainingGuideAutomation/f9d548fbd3a44f09b5354c2f52ff2df4/Verification.proof.json`으로 **새 native 1개 Success·UE/호스트 종료 0**, 기동 Error 15줄/탐색 이후 0이다. 실제 로드 DLL 해시와 선택 소스 7개의 불변을 확인했다. 집계는 `Saved/BuildChecks/TrainingGuideViewport-20261030/ImplementationResult.json`에 남겼다.

최초 게임 전용 Controller의 에셋 로딩 시간 초과, 플레이어 레이어가 없는 시험 환경의 0개 판정 실패, 조건부 엔진 World API의 개별 컴파일 실패는 보존했다. 기본 Controller·실제 SGameLayerManager와 엔진 옵션 가드를 준비한 최종 검사만 성공으로 기록한다. 가상 창 어댑터의 구형 엔진 연결 API는 검사 내부에서만 한정 사용한다.

[Run-TrainingGuideAutomationTest.ps1](../Scripts/Tests/Run-TrainingGuideAutomationTest.ps1)은 기본적으로 새 뷰포트 사례 하나를 실행한다. 정상 모듈에 새 검사가 포함된 뒤 사용할 수 있고 `-All`은 기존 진행/입력 검사까지 명시적으로 포함한다. 원본 모듈·Config를 자체 임시 프로젝트에 복사하고 소유 UE의 종료/파일 사용 해제 후 **성공·실패 모두 finally에서 Temp·DLL·UserDir를 자동 폐기**한다. 유지되는 것은 결과·명령·모듈 해시·필요 로그다. 기본 범위 밖 경로/재분석 지점 경유 거절, 사용 중 파일의 삭제 전 거절, Junction 대상 보존·삭제 용량·반복 정리는 [작은 PowerShell 검사](../Scripts/Tests/Test-TrainingGuideArtifacts.ps1)로 확인했다.

새 지침에 따라 이전 본인 검증 7개 폴더의 입력 사본·OBJ/PCH·분리 DLL/PDB·RuntimeProject/캐시도 정리했다. **이전 사본 3.571GiB**, 이번 성공/실패의 자동 폐기까지 **누적 14.257GiB**를 삭제했다. 재생성 후 다시 폐기한 파일도 포함한 누적 삭제량으로 디스크 순증가량과 다르다. 훈련 콘텐츠 담당 `progression-training-content-20261029`가 읽기 예약한 기존 `TrainingGuideHUD-20261029/RuntimeProject` 한 곳은 예외 보존했다. 해당 예약 종료/인계 확인 직후 또는 확인 가능한 다음 재개에 정리한다. 상세 대상·용량·예외는 `Saved/Maintenance/TrainingGuideCleanup-20261030/Result.json`이다.

정리한 고정 입력과 분리 바이너리의 해시는 **당시 검증 증거**다. 같은 DLL 재실행/입력 재감사가 계속 가능하다는 뜻이 아니며 과거 Saved의 빌드 레시피는 폐기한 입력을 먼저 재생성해야 한다. 원본 Source/Content/Binaries/Intermediate·공유 DDC와 다른 담당의 임시 사본은 이번 정리 범위 밖이다.

2026-10-30 후속: 훈련 콘텐츠 담당의 완료·인계와 읽기 예약 해제를 확인해, 위 예외 RuntimeProject도 정리했다. `Saved/Maintenance/TrainingGuideCleanup-20261030/Deferred-Result.json`에 추가 삭제 용량/링크 해제와 잔여 없음이 기록돼 있다. 이전 정리의 대형 예외는 해소됐으며 그 모듈 해시도 당시 증거로만 남는다.

# 훈련 개발 실행기

프로젝트 폴더의 CMD에서 다음 명령을 실행한다. 에디터 안의 콘솔 명령이 아니다.

```cmd
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\Start-TrainingDevelopment.ps1
```

정상 빌드된 게임 DLL과 `D:\UE_5.8`의 `UnrealEditor-Cmd.exe`, .NET SDK가 필요하다. 엔진 위치가 다르면 `-EngineRoot`를 지정한다. 기본 포트는 backend TCP **3076**, 게임 UDP **18896**이며 점유 중이면 시작을 거절한다. 기존 에디터/백엔드/DB/할당기는 종료하지 않는다.

## 직접 플레이

2026-11-01부터 아래 메뉴 흐름을 사용한다. 이전 절차의 방 생성·출발·준비된 던전 입장
버튼을 따로 누를 필요가 없다.

1. 실행기가 비공개 Memory 백엔드를 빌드·시작하고 별도 게임 창의 로비를 연다.
2. 개발 로그인 화면에서 개발용 Steam ID(예: `76561198000000001`)로 로그인한다. 실제 Steam 인증은 아니다.
3. **서버 선택**을 누르고 대검 캐릭터를 생성·선택한 뒤 **접속**을 누른다. 메뉴형 로비가 열린다.
4. **튜토리얼 입장**을 한 번 누른다. 기존 회원 조회·생성·출발·서버 준비·입장권 요청을 자동으로 진행하며, 방 ID/주소/자격은 입력하지 않는다.
5. 훈련 맵으로 이동하면 기본 공격·회피·돌진·완충 차지의 네 조작을 수행한다. 완료 후 결과의 로비 복귀 버튼으로 돌아오면 같은 캐릭터의 스킬 구성을 확인할 수 있다.
6. 게임 창을 닫으면 실행기가 자기 게임 서버와 백엔드를 종료하고 임시 실행 파일을 정리한다. PowerShell을 정상 중단하는 경우에도 `finally`가 같은 정리를 수행한다.

완료 후에는 **스킬 구성**에서 SP와 새 스킬을 확인하라는 안내가 표시된다. 캐릭터를 다시
선택하면 저장된 진행 정보를 읽어 완료 표시를 복원한다. 새 캐릭터는 자신의 진행 정보로
판단하며, 조회가 끝나기 전에는 튜토리얼 입장을 잠근다. 조회 실패 시 **진행 정보 다시 확인**을
사용한다. 미저장 스킬 편집이나 미확정 저장이 있으면 이 재조회로 덮어쓰지 않는다.

새로 얻은 스킬을 빠르게 편성하려면 **스킬 구성 → 빈 슬롯에 습득 스킬 넣기 → 구성 저장**을
사용한다. 이미 찬 슬롯과 SP 투자는 유지하고 비어 있는 슬롯에만 넣는다. 저장 전에는
편집본만 바뀌므로 **변경 취소**로 되돌릴 수 있다. 편성/저장/취소 버튼은 목록 위쪽에 있다.
정상 모듈의 실제 저장/재조회 확인은 `Saved/BuildChecks/AcquiredSkillLoadout-20261101/Verification.proof.json`이다.

같은 클라이언트의 완료→형제 선택→원래 캐릭터 재선택 검증은
`Saved/BuildChecks/TutorialProgressMenu-20261101/Verification.proof.json`에 기록했다.

**Memory에 생성한 계정/캐릭터/성장은 실행 종료 시 사라진다.** 기존 PostgreSQL 저장 자료에는 접속하지 않는다. 같은 실행에서는 훈련 완료 후 로비로 돌아가 캐릭터를 변경하고 다른 캐릭터의 **튜토리얼 입장**을 계속 사용할 수 있다. 실행기는 이전 세션의 확정 종료와 해당 클라이언트의 실제 훈련 맵→로비 이동을 모두 확인한 뒤 자기 서버만 회수하고, 같은 백엔드/포트에서 다음 출발 요청을 처리한다. 결과 화면에 머무르는 동안에는 서버를 유지한다. 같은 클라이언트의 실제 캐릭터 변경 조작은 아래 자동 검사의 범위와 구분한다.

최신 실제 UI 검사는 `Saved/TrainingDevelopment/1478f40454c9462dbddbe506687a89a9/Result.json`
이다. 로그인·서버 선택·캐릭터 생성/선택·접속·메뉴 로비·튜토리얼 버튼 **1회**로 실제
훈련 맵의 0/4 가이드까지 통과했다. 완료/보상/로비 복귀는 같은 모듈의 별도 능력 입력
검사로 통과했다. 검증 전용 런처 사본은 다른 담당이 수정 중인 PlayerHUD를 별도 Content
overlay로 분리했다. 유지할 실행기의 기본 Content/Plugins 링크 정책 변경은 없다.

## 실행 경계

- 2026-11-01 연속 훈련: 회수 판단은 정확한 세션/서버 ID·Training/Normal·1인·Cleared/Failed/Closed와 **배정 이후** 클라이언트의 훈련 맵→로비 로그를 함께 요구한다. 진행 중/정산 대기, 조회 실패, 초기 로비나 이전 세션의 이동 기록만으로 서버를 끄지 않는다. 자기 PID/경로/시작 시각을 대조하고 종료 확인 후 소유 journal에서도 해당 자식을 제거한다. 회원·보상 상태를 실행기가 변경하지 않는다.

- backend는 기존 `DungeonContent__DevelopmentProfile=Training`을 선택한다. Development/Memory/개발 인증·훈련 Normal/실제 맵/1인 정책을 사용하며 기본 운영 카탈로그를 편집하지 않는다.
- 클라이언트는 기존 `-RPGDungeonProfile=Training -RPGDevelopmentLogin -nosteam`을 사용한다. 이 선택이 티켓이나 멤버십 승인을 대신하지 않는다.
- 출발 요청은 기존 `Backend/Tools/Start-LocalDungeonServer.ps1`의 관리자 claim·맵 검증·제한된 서버 자격 발급/환경 전달로 처리한다. 새로운 할당 API나 클라이언트 관리자 권한을 만들지 않는다.
- 관리자 자격은 게임 프로세스에 전달하지 않는다. 게임 서버에는 기존 세션 한정 자격만 환경 변수로 전달하며, 자격을 명령행/결과 JSON에 남기지 않는다.
- 원본 `.uproject`/Config/Binaries를 해시 대조한 자기 사본과 Content/Plugins 링크를 사용한다. 공유 Source/UHT/UBT/Intermediate·원본 DLL/에셋을 쓰지 않는다. 이후 원본 모듈 교체는 이미 고정한 사본의 증거에 포함되지 않는다.
- 긴 임시 프로젝트 경로가 접속 journal의 260자 경계를 넘지 않도록 서버 실행기에 선택적 `RuntimeUserDirectory`를 추가했다. 기존 호출의 기본 경로는 유지하고, 명시한 디렉터리는 상태에 기록해 다른 디렉터리의 살아 있는 서버를 잘못 재사용하지 않는다. Editor 서버의 MCP 자동 시작도 해제해 기존 GUI 엔드포인트를 보존한다.

연속 배정 자동 검사는 다음 명령이다. 같은 Memory 백엔드/UDP 포트를 유지하면서 검사용
클라이언트를 캐릭터마다 새로 띄워 네 능력 입력·결과 버튼 복귀를 실행한다. 클리어 API나
직접 성장 주입을 사용하지 않는다. 같은 창에서의 물리 캐릭터 변경·Steam/PG 검사는 아니다.

```powershell
.\Scripts\Start-TrainingDevelopment.ps1 -SessionLoopSmoke
```

집중 PS 28사례와 실제 두 세션의 완료/각 XP100·레벨2·SP3/동일 신원 복귀/각 회수 당시
클라이언트 생존/첫 캐릭터 성장 불변을 확인했다. 실제 결과는
`Saved/TrainingDevelopment/f558f965f5e24bf6b4434a09955f8eba/Result.json`이다.

실행별 경로는 `Saved/TrainingDevelopment/<실행GUID>/Temp`와 짧은 `Saved/Tests/TP/<자기12hex>/U`다. 자기 PID·시작 시각·실행 경로를 대조한 뒤 프로세스를 정리하고, 링크를 따라가지 않는 기존 정리 helper로 임시 DLL/backend 소스·빌드/UserDir/캐시·원본 로그를 폐기한다. 작은 결과·명령에 해당하는 실행 스크립트·입력 해시·최소 로그만 남긴다. PowerShell 프로세스 자체를 강제 종료하면 `finally` 실행이 보장되지 않는다.

## 실행기가 강제 종료된 경우의 복구 — 2026-10-31

새 실행은 게임을 띄우기 전에 이전 실행기의 소유 기록을 확인한다. 강제 종료된
실행기의 부모 PID/시작 시각/경로와 등록된 자식 신원을 대조하고, 살아 있는 부모가
있으면 그대로 둔다. 부모가 종료된 경우에도 각 자식의 실제 실행 인자가 해당 실행의
정확한 임시 프로젝트 또는 backend DLL을 참조하는지 다시 확인한다. 관리자 자격,
접속 토큰, 티켓, 실행 명령행은 소유 기록에 저장하지 않는다.

복구만 실행하려면 프로젝트 폴더의 CMD에서 다음을 사용한다. 이 모드는 엔진 설치나
backend 빌드를 요구하지 않고 새 게임/서버를 시작하지 않는다.

```cmd
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\Start-TrainingDevelopment.ps1 -RecoverOnly
```

- 소유 기록은 `Saved/TrainingDevelopment/<GUID>/Lifecycle.json`, 복구 요약은 같은
  폴더의 `Recovery.json`이다. 정상 종료 후에는 완료로 표시하며 반복 복구는 삭제 0이다.
- 현재 다른 담당이 실행 경로나 이 실행기/임시 실행 경로 전체를 예약한 경우, PID/시작 시각/경로가 달라진 경우,
  실행 인자가 다른 프로젝트를 가리키는 경우에는 자동 종료·삭제를 거절한다.
  `Temp` 자체/상위 경로가 junction이면 거절하고, 내부 Content/Plugins 링크는 대상에
  들어가지 않고 링크만 해제한다. 열려 있는 일반 파일도 삭제 전에 검사한다.
- 프로세스 생성과 소유 기록 저장 사이에 강제 종료돼 등록되지 않은 프로세스가 남으면
  추측해 종료하지 않고 복구 파일을 보존한다. 기존 할당기가 별도로 저장한 정확한 서버
  신원은 대조해 회수할 수 있다. 이 기능 이전의 소유 기록 없는 산출물은 일괄 삭제하지 않는다.
- 복구는 실행기의 프로세스·임시 파일 정리다. Memory의 캐릭터/성장을 되살리는 기능이나
  실행 중 게임의 저장/재접속 복구가 아니다. 기본 Memory 수명과 기존 게임 권한은 유지한다.

유지 검사는 다음과 같다. 실제 UE/프로젝트 backend 대신 별도로 만든 작은 .NET 대기
프로그램과 PowerShell 부모만 사용한다.

```cmd
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\Tests\Test-TrainingDevelopmentRecovery.ps1
```

실제 부모 강제 종료/자식 회수·반복 실행, 살아 있는 부모·변경된 시작 시각·다른 실행
인자·등록 공백/소유 기록/경로/예약 보호, junction 대상 보존과 UserDir 정리,
`-RecoverOnly`/잘못된 모드의 시작 전 거절·자격 없는 journal을 포함한 13그룹이 통과했다.
PowerShell 5의 `File.Replace`에 일반 `$null`을 넘기던 최초 기록 교체 실패는
`[NullString]::Value`로 보완했고, 당시 남은 자기 자식/임시 파일도 정확한 기록 대조 후
회수했다. 초기 실패와 최종 성공·정리량은
[검증 요약](../Saved/BuildChecks/TrainingInterruptionRecovery-20261031/Verification.proof.json)에
구분해 남긴다. 새 게임 플레이나 기존 PIE 로비 수정의 실행 성공을 뜻하지 않는다.
반복 보완을 포함해 자기 fixture의 DLL/obj·임시 실행/UserDir **4,813,916 bytes(4.59MiB)**를
폐기했고, 남은 자기 프로세스·임시 경로·정리 예외는 없다. 유지하는 것은 작은 소유 기록,
검사 결과/실행 스크립트·해시와 최소 빌드 로그이며 사본 DLL은 재실행할 수 없다.

## 2026-10-31 검증

```cmd
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\Tests\Test-TrainingDevelopmentLauncher.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\Start-TrainingDevelopment.ps1 -SmokeTest
```

스크립트의 구문/잘못된 포트의 시작 전 거절/기존 할당기 mock/정리 경로·junction 보호 **4그룹 통과**다. 할당기 mock은 기존 선택·맵/자격·보상적 실패 범위와 새 짧은 UserDir의 정확한 전달/메타데이터·잘못된 문맥/상대 경로/인용부호 경로 거절을 포함해 **17사례**다. 실제 UE를 띄운 검사와 혼동하지 않는다.

실제 headless smoke는 [Result.json](../Saved/TrainingDevelopment/f08709abf137470baf6c356a56afec48/Result.json)과 같은 폴더의 `Inputs.json`·`Build.log`·최소 로그가 근거다.

- 기존 Training backend 프로필의 실제 서버 claim이 `/Game/Maps/Progression/L_ProgressionTraining`/Normal을 반환했고, backend InProgress와 일치하는 `RPG_DUNGEON_SERVER_READY`를 확인했다. 별도 카탈로그 인덱스 override는 사용하지 않았다.
- 실제 로드한 서버/클라이언트 DLL 모두 자기 사본 `9968DDA8…EA32034`이며 입력 사본 **110개**가 실행 종료까지 불변이었다. backend Release 빌드 경고/오류 **0/0**이다.
- 개발 클라이언트 옵션을 함께 사용한 기존 훈련 probe가 실제 입장 티켓과 네 조작의 입력을 수행해 목표 **0→1→3→7→15**, 세션 **Cleared**, XP **100**/레벨 **2**/SP **3**/투지 해방 해금을 확인했다. 클라이언트 정상 종료 **0**이다. 서버는 런처 정리로 종료했으므로 서버 자연 종료 성공은 아니다.
- smoke는 검사 준비를 위해 캐릭터/세션 생성·출발을 HTTP로 실행한 뒤 기존 클라이언트 probe로 재개/입장한다. **로비 버튼 클릭 → 출발 → 결과 화면/로비 복귀를 수동 또는 UMG로 전부 조작한 성공이 아니다.** GPU/물리 입력·Steam/PG·다인·재시작 영속도 검증하지 않았다.
- 자기 임시 실행/백엔드/캐시/로그/UserDir **22,903,966 bytes**와 mock fixture **4,706 bytes**, 합계 **22,908,672 bytes(21.85MiB)**를 자동 폐기했다. 잔여 임시 경로·정리 예외는 없다. Content/Plugins 원본과 다른 담당자의 정상 빌드 출력/GUI는 보존했다. 폐기한 사본은 해시만 남은 당시 증거이므로 재실행에는 새 사본이 필요하다.

로비 입장 UI의 자동 확인은 아래 선택 모드로 이어졌다. 훈련 완료·결과/복귀 UI와 물리 입력은 별도 후속이다. [로비 프로필 계약](ONLINE_LOBBY_UI.md#개발-클라이언트의-훈련-선택-목록-2026-10-31)과 [훈련 조작/정산 계약](PLAYER_PROGRESSION_TRAINING_PLAYTEST.md)을 따른다.

## Slate 로비 로그인부터 훈련 입장까지 — 2026-10-31

```cmd
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\Tests\Test-TrainingLobbyUIObservation.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\Start-TrainingDevelopment.ps1 -LobbyUISmoke
```

기본 직접 플레이 모드와 기존 `-SmokeTest`를 유지하고, 선택적인 `-LobbyUISmoke`를 추가했다. 이 모드는 자기 full Editor의 별도 PIE 창에서 Slate의 Type·Tab/Enter·SelectOption·Click으로 **개발 로그인 → 캐릭터 생성·선택 → 훈련 세션 생성 → 출발 → 준비된 던전 입장**을 수행한다. 캐릭터/세션 생성·출발 HTTP를 준비 단계에서 대신 호출하지 않는다. UI 로그인 후 별도 개발 토큰으로 소유 계정의 목록·세션·성장을 읽어 결과를 대조하며, 관측 토큰을 클라이언트에 주입하지 않는다.

MCP 기본 포트는 **8014**이며 loopback 리스너와 자기 에디터 PID를 확인한다. 다른 GUI/MCP와 기존 Memory3076/UDP18896 점유는 재사용하지 않는다. 입력은 합성 Slate 이벤트이므로 실물 키보드·마우스나 픽셀 외형의 수동 검증과 구분한다. 이 경로는 에디터 전용 Slate toolset이 필요하며 D3D12 offscreen floating PIE로 실행한다.

초기 2026-10-31 검사는 **검증 사본에만** `RPGOnlineLobbySubsystem.LobbyMapPackage=/Game/Maps/UEDPIE_0_LobbyMap`을 지정했다. **2026-11-01 정상 모듈의 PIE 정규화 반영 뒤 이 override를 실행기에서 제거했다.** 현재는 기본 로비 설정으로 로그인·생성·출발·훈련 입장까지 확인했다. 초기 결과와 새 모듈의 결과는 아래 후속으로 구분한다.

최종 [실행 결과](../Saved/TrainingDevelopment/29fd67be749348b2af129c648694f048/Result.json)와 [입장 가이드 Slate](../Saved/TrainingDevelopment/29fd67be749348b2af129c648694f048/training-guide-on-entry.slate.txt), [전체 요약](../Saved/BuildChecks/TrainingLobbyUI-20261031/Verification.proof.json)이 근거다.

- UI로 생성한 캐릭터 1개의 이름/대검 직업을 서버 목록과 대조했고, 실제 할당기의 Training/Normal claim·서버 준비 후 입장했다. 서버·클라이언트 모두 DLL `9968DDA8…EA32034` 사본을 실제 로드했으며 고정 입력 **111개**가 종료까지 동일했다. backend Release 빌드 경고/오류 **0/0**이다.
- 입장 직후 **기본 조작 훈련 0/4**, 기본 공격·회피·돌진 베기·차지 공격 안내를 실제 Slate에서 확인했다. 세션 **InProgress**, XP **0**/레벨 **1**도 대조해 훈련 전에 보상이 지급되지 않았음을 확인했다. 그 뒤 StopPIE와 중지 조회를 통과했고 자기 프로세스만 회수했다. 자연스러운 서버 종료나 결과/복귀 성공을 뜻하지 않는다.
- 도구 검사는 비활성/영점 크기 컨트롤 배제·가까운 필드 문맥·중복 버튼 거절·JSON 목록 0/1/2개·두 smoke 동시 요청의 시작 전 거절을 통과했다. 로딩 중 콤보박스 대신 에디터의 다른 컨트롤을 고르지 않고 기다린다. Windows PowerShell의 빈 JSON 배열을 1개로 세던 관측기 오류도 보완했다.
- 초기 시작 시간 초과·PIE 맵 이름·창 크기/포커스·Editor 전용 toolset·목록 집계/로딩 경쟁의 실패 기록은 최종 성공과 구분해 보존했다. UE 전체 런타임 오류 수는 이 검사에서 감사하지 않았으므로 오류 0으로 표시하지 않는다. Steam 실인증·PG 저장·다인·훈련 완료/결과/복귀 UI는 이번 범위 밖이다.
- 성공/실패 8회에서 자기 임시 실행/캐시/UserDir **891,196,923 bytes**, 중복/빈 Slate **94,828 bytes**, 합계 **891,291,751 bytes(850.00MiB)**를 폐기했다. 임시 잔여·정리 예외는 없고 사용자 GUI2584/원본 출력·에셋은 유지했다. 성공 시 가이드와 같은 진단 스냅샷을 중복 저장하지 않도록 최종 로그 조건만 추가 보완했다. 사본 해시는 당시 증거이며 재실행 가능한 DLL은 남기지 않았다.

## 2026-11-01 훈련 완료 후 결과·복귀 반영

기존 “다음 안내를 기다려 주세요”에서 진행되지 않던 결과 UI 연결을 정상 DLL에 반영했다.
새 실행에서 네 조작을 완료하면 결과 위젯이 표시되고, 서버 정산 확인 뒤 **Return to lobby**로
돌아갈 수 있다. 실제 입력15→Cleared/XP100/Lv2/SP3→결과 위젯1개/복귀 버튼→같은 캐릭터의
로비 도착·UE0/0을 확인했다. 기본 Memory 저장 수명과 일회성 서버 실행 정책은 유지한다.

설치 DLL은 `F1A20269…1AC5654`이며 이미 실행 중인 사본에는 적용되지 않는다.
게임 창을 닫고 이 문서 첫 명령으로 새로 실행한다. 같은 모듈의 native7과
**PIE 패키지 설정 override 없는** 실제 Slate 로그인/생성/출발/훈련 입장도 통과했다.
[완료 검증·초기 실패/계획 단절 오류의 구분과 정리 기록](ONLINE_LOBBY_UI.md#훈련-44-이후-결과로비-복귀의-원본-반영-2026-11-01)을 따른다.

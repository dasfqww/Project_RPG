# 제작 파이프라인 구현

게임/백엔드와 독립된 .NET 도구다. [전체 계획](../../Docs/CONTINUOUS_GAME_PRODUCTION_PLAN.md)의 첫 구현이며 자동 제작 가동을 의미하지 않는다.

## 작업 계약 (PROD-01)

`Contracts`의 실제 C# wire type에서 `Production/Contracts/*.schema.json`을 생성한다. JSON schema는 구조를 검사하며, 필드 간 경로·한도·중복·버전 규칙은 `ContractRules`가 추가로 검사한다. 실행 가능한 입력에는 양쪽 검사가 필요하다. snake_case 필드, 문자열 enum, 필수 nullable 필드, 알 수 없는 필드/중복 JSON 거절을 적용한다.

작업은 고정 커밋/입력 해시, 명세 revision, 의존성, 쓰기 경로, 산출물/완료 조건/검사, 담당과 다음 역할, 실행 한도, 임시/증거 경로를 갖는다. 경로는 저장소 또는 해당 작업 공간 기준의 상대 경로이며 폴더는 하위 전체를 포함한다. 와일드카드·상위 경로·Windows 장치/ADS·`.git`은 허용하지 않는다. 실제 OS 경계와 reparse point 검사는 실행 어댑터 책임이며 문자열 검사만으로 파일 접근을 허가하지 않는다.

`Production/Contracts/Examples/seal-planner.json`은 등록 형식 예제다. `Production/Briefs/seal-stone.json`은 시범 명세 초안이며 아트 채택·성능 기준·유료 생성 범위는 미정이다. 예제 파일을 읽는 동작은 작업 실행을 시작하지 않는다.

추가 사용비는 정수 USD micro 단위다. 기본 정책은 구독 인증, API fallback/추가 지출 금지, 동시 Codex/무거운 작업/Git 각각 1이다. 이는 실제 계정의 추가 크레딧 과금을 확인했다는 의미가 아니다. 알 수 없는 실제 비용은 0으로 보고하지 않는다. 검토 시각 null은 미정이며 예약 작업을 만들지 않는다.

## 실행과 검증

```powershell
./Scripts/Production/Install-ProductionSdk.ps1
./Scripts/Production/Test-Production.ps1 -Group contracts
```

SDK 10.0.403은 Microsoft 배포 archive의 SHA-512를 대조해 `Saved/ProductionToolchain/`에만 설치한다. 시스템 SDK나 게임 프로젝트를 변경하지 않는다. 이는 유지 도구 의존성이며 임시 빌드와 구분한다. [.NET 지원 정책](https://dotnet.microsoft.com/en-us/platform/support/policy/dotnet-core)에 따라 새 제작 도구는 .NET 10 LTS로 분리한다.

`Contracts.Tool` 명령은 `schemas <출력 폴더>` 또는 `validate <task|result|policy> <파일>`이다. SDK로 프로젝트를 빌드한 뒤 DLL에 인자를 전달한다. 회귀 실행기는 전용 경로에서 빌드하고 종료 시 임시 출력/DB를 정리하며 작은 결과만 Saved에 남긴다. 게임 빌드·에디터·AI 요청은 실행하지 않는다.

## 영속 작업 큐 (PROD-02)

`Coordinator`는 전용 SQLite DB와 상태 변경/이벤트를 같은 트랜잭션에 기록한다. 전용 파일 잠금은 관리자 프로세스 1개만 허용하며 SQLite WAL/FULL과 짧은 쓰기 트랜잭션을 사용한다. 작업자는 이 라이브러리/DB에 직접 접근하도록 설계하지 않는다. 현재 제공되는 명령은 운영자 로컬 CLI이고 작업자 전송 계층은 후속이다. [SQLite 트랜잭션](https://learn.microsoft.com/en-us/dotnet/standard/data/sqlite/transactions)과 [연결 설정](https://learn.microsoft.com/en-us/dotnet/standard/data/sqlite/connection-strings)을 따른다.

새 DB는 pause 상태다. 카드 등록은 Proposed이며 명시 `approve` 후 Ready가 된다. 동일 ID/같은 카드는 재시도 시 기존 결과를 반환하고, 내용이 다른 카드는 거절한다. 카드는 불변이며 새 revision은 새 ID로 등록한다. 선행 작업은 먼저 등록되어야 하므로 순환 의존성을 만들 수 없다. 선행 작업의 기술 완료와 필요한 채택까지 끝나야 다음 작업이 배정 후보가 된다. 잘못된 DB/지원하지 않는 버전/실행 중 정책 변경은 덮어쓰지 않고 거절한다.

```powershell
./Scripts/Production/Invoke-Production.ps1 -Command init
./Scripts/Production/Invoke-Production.ps1 -Command enqueue -TaskFile Production/Contracts/Examples/seal-planner.json
./Scripts/Production/Invoke-Production.ps1 -Command approve -TaskId seal-planner-v1
./Scripts/Production/Invoke-Production.ps1 -Command status
./Scripts/Production/Invoke-Production.ps1 -Command events
./Scripts/Production/Test-Production.ps1 -Group queue
```

기본 DB는 `Saved/Production/state.db`이고 `-Database`로 전용 로컬 경로를 지정할 수 있다. `pause/resume`은 배정 상태만 바꾸며 사용자/작업자 프로세스를 종료하지 않는다. 현재 CLI에는 실행 시작이나 Done 강제 지정 명령이 없다. `Saved/Production/ToolBuild`는 운영 명령이 사용하는 빌드이며 회귀 검사의 임시 경로와 다르다. DB와 작은 이벤트는 유지 데이터다. 큐 회귀는 별도 DB에서 동시 재등록, 관리자 배타 잠금, 프로세스 강제 종료 후 커밋/미커밋 복구를 검증한다.

## 자원 임대와 복구 (PROD-03 기반)

큐 DB v2는 기존 v1 작업/이벤트를 보존하면서 시도, heartbeat/deadline, 자원/경로 임대, 누적 실패 횟수를 추가한다. 관리자는 `TryClaim`으로 카드와 임대를 같은 트랜잭션에 받는다. Codex/무거운 작업/Git/외부 3D 슬롯은 각각 1개이며 읽기 입력과 쓰기 폴더의 대소문자·상하위 경로 충돌을 검사한다. 작업 공간과 임시/증거 경로도 예약한다. 막힌 고우선 작업을 건너뛰어 독립된 후보를 진행할 수 있다.

임대는 task/attempt/worker/generation에 묶인다. 시작 때 소유 프로세스의 PID·시작 UTC ticks·호스트를 기록한다. heartbeat 30초/만료 후보 180초를 기본으로 하고, heartbeat가 와도 전체 실행 제한 시간은 늘리지 않는다. 만료 후 heartbeat/결과로 임대를 되살릴 수 없다. 만료 검사는 해당 작업을 Blocked로 표시하며 자원을 즉시 넘기지 않는다.

`Recover`는 기존 프로세스 종료를 실제 OS에서 관측하고, 관리자의 실행 트리·외부 작업·부분 파일 조정 근거를 받은 뒤에만 실행 슬롯을 반환한다. 경로/작업 공간은 같은 작업의 재시도 동안 보존한다. 새 세대에서 이전 시도의 결과/heartbeat를 거절하며, 재시작해도 시도 수·일시 오류 60/300/900초·동일 결함 추가2회 한도를 유지한다. 예산 미설정 작업에는 유료 배정을 하지 않는다. 소유 프로세스 자체를 임의로 종료하는 API는 없다.

`RecoveryClearance`는 **신뢰하는 관리자 어댑터의 입력 계약**이다. bool이나 근거 경로 문자열 자체가 외부 서비스/실행 트리/실제 diff를 검사하지 않는다. 이 값은 AI 작업자의 자기보고로 채우면 안 된다. Codex 실행 관리자와 파일/외부 task 검증 어댑터가 후속 구현에서 실제 증거를 확인해야 한다. 시작 응답 유실로 PID가 없거나 다른 호스트/권한 문제로 관측이 Unknown이면 예약을 유지하고 인계 대기한다. 종료한 부모 PID만으로 자식 프로세스 전체 종료를 추정하지 않는다.

작업자 결과가 입력 revision/hash/attempt/허용 경로에 맞아도 상태는 Checking이다. 실제 파일 내용, QA, 정리, Git 원격 확인은 후속 어댑터의 책임이며 현재 Done으로 올리는 API는 제공하지 않는다. `SettleExecution`은 확인된 종료 뒤 실행 슬롯만 반환하고 제품 경로는 유지한다. 재시도 불가/인계 대상은 관리자 `HandOffRecovered`가 실제 인계 근거와 현재 세대를 확인한 후 NeedsDirector로 바꾸며 경로를 반환한다.

```powershell
./Scripts/Production/Test-Production.ps1 -Group leases
./Scripts/Production/Invoke-Production.ps1 -Command attempts -TaskId seal-planner-v1
./Scripts/Production/Invoke-Production.ps1 -Command sweep
```

`sweep`은 만료 후보 표시만 수행하며 프로세스 종료/재실행 명령이 아니다. `attempts`와 `events`로 관리자 재시작 후 인계 상태를 다시 읽는다. 자동 배정 루프·Codex 실행·OS 쓰기 격리·외부 서비스 조정·QA/Git 게시·일일 검토 화면은 아직 연결되지 않았다. 실제 24시간 운영 또는 M0~M5 전체 완료로 해석하지 않는다.

회귀는 동시 claim, 경로/슬롯 충돌, 실제 소유 자식 프로세스의 생존/종료 관측, 모의 시간/외부 증거를 사용한 backoff·부분 변경 예약·세대 차단을 구분한다. 각 실행은 입력 파일 해시의 전후 일치와 작은 결과를 저장한다. SDK 설치물과 운영 도구 빌드는 유지하고 회귀 실행기의 자기 임시만 폐기한다.

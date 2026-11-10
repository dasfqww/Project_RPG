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

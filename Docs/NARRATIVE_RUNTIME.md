# 시나리오·퀘스트·대화 런타임

기준: [통합 기획안](SCENARIO_QUEST_DIALOGUE_DESIGN.md). 최초 구현일: 2026-11-04.

## 서버의 책임

| 클래스 | 책임 |
|---|---|
| `QuestManager` | 선행·수락·대화 목표·보고·완료 판정 |
| `ScenarioManager` | 확정된 퀘스트 결과에서 챕터 완료와 이야기 해금 계산 |
| `DialogueManager` | 현재 자격에 맞는 노드·선택지 조회와 선택 검증 |
| `NarrativeCoordinator` | 매니저 연결, 저장 명령 실행, 승인된 결과 반환 |

매니저는 캐릭터 상태를 보관하지 않는다. 변경 가능한 상태는 캐릭터별 저장소에 있고, 정의는 불변 카탈로그다. `INarrativeRepository`의 Memory/PostgreSQL 구현이 소유권 확인·요청 영수증·리비전 확인·변경·상태/영수증 저장을 같은 잠금 또는 트랜잭션에서 처리한다. 같은 요청 ID와 같은 입력은 최초 결과를 반환한다. 같은 ID의 다른 입력은 충돌이다. 서로 다른 요청이 같은 리비전을 사용하면 하나만 반영된다.

클라이언트는 작성된 대화·선택지 ID만 제출한다. 목표 수량·완료 플래그·해금 ID·XP·아이템 지급을 제출할 수 없다. 첫 목표 유형은 승인된 개인 대화 확인이며, 전투·조사·파티 증거는 후속 서버 사건 연결 대상이다. 보고가 승인되면 퀘스트 완료·챕터 완료·이야기 해금이 함께 반영된다. 전투 성장·정산은 기존 소유 경로를 따른다.

## API와 개발 콘텐츠

- `GET /api/characters/{characterId}/narrative`: 본인 캐릭터의 상태와 현재 가능한 대화.
- `POST /api/characters/{characterId}/narrative/choices`: `requestId`, 문자열 `expectedRevision`, `catalogVersion`, `dialogueId`, `choiceId`.
- 응답: `catalogVersion`, `characterId`, `schemaVersion`, 문자열 `revision`, `quests`(ID·정의 버전·상태·확인 목표), `completedChapters`, `storyUnlocks`, `availableDialogues`.

기존 Bearer 인증을 사용한다. Player의 본인 캐릭터만 읽기/쓰기를 허용하며 관리자·게임 서버 자격으로 대신 선택하지 않는다. GET은 영속 레코드를 생성하지 않는다. 재전송 결과가 현재 진도보다 오래된 리비전일 수 있으므로 표시 소비자는 리비전을 비교해야 한다. 결과 미확정 선택은 원래 요청 ID·입력으로 재시도한다.

정의 버전/저장 형식이 맞지 않거나 저장 상태가 모순이면 자동 초기화하지 않고 충돌을 반환한다. PostgreSQL은 기존 v1/v2 SQL을 유지하고 `0003_character_narrative.sql`을 추가한다. 운영 적용은 기존 명시적 마이그레이션 절차를 따른다.

기본 카탈로그는 비어 있다. Development/Testing 환경에서 `Narrative:EnableDevelopmentContent=true`를 명시했을 때만 `DEV.Introduction`을 제공한다. Contact의 수락 → Contact/Instructor 각각 확인 → Contact 보고의 네 명령으로 `DEV.Prologue`와 `DEV.Story.Next`를 확정한다. 추가 성장 보상은0이다. 이는 기술 검증용 의뢰이며 CH01 세계관의 채택이나 실제 NPC 배치를 뜻하지 않는다. 첫 UI 연결도 이 계약을 사용한다.

2026-11-05부터 외부 JSON을 `Narrative:CatalogPath`로 선택해 같은 매니저/UI 계약을 사용할 수 있다. 시작 전 형식·참조·순환·진행 경로·UE 응답 한도를 검사하고, 오류가 있으면 빈 데이터로 대체하지 않고 시작을 거절한다. 개발 예제도 같은 형식의 내장 JSON으로 옮겼으며 ID/대사/진도 계약은 유지한다. `--validate-narrative-catalog`는 DB/호스트를 시작하지 않고 검사 후 종료한다. 파일 형식·설정·버전/저장 호환 경계는 [콘텐츠 저작 안내](NARRATIVE_CONTENT_AUTHORING.md)를 따른다.

## 검증

`Scripts/Tests/Run-NarrativeManagerTests.ps1 -Postgres`는 독립 입력/출력에서 빌드하고, 라벨로 소유한 임시 PostgreSQL을 사용한 뒤 임시 파일·컨테이너를 정리한다. 소스 해시를 전후 대조하고 작은 로그·요약만 로컬에 보존한다. PostgreSQL 실행에는 Docker와 캐시된 `postgres:17-alpine`이 필요하다.

2026-11-04 확인: 경고/오류0 빌드, Memory·실제 HTTP Bearer 인증·PostgreSQL에서 소유권/다른 캐릭터 분리, 중복 재전송·동시 요청 충돌, 잘못된 선택·버전·리비전 거절, 완료/해금 동시 반영, 새 Coordinator/DB 연결 복원, 실패 롤백·취소, 기존 성장 불변. 마이그레이션 v1~v3·기존 마이그레이션/장비 영속 회귀도 통과했다. 게임 클라이언트 연결·NPC 화면·실제 다인 사건은 별도 검증이다.

## UE Subsystem 소유 구조

2026-11-04 사용자 지시에 따라 최초 UObject 후보를 아래 구조로 원본 구현했다. 서버 매니저와 클라이언트 조회 서비스의 책임은 구분한다.

| UE 클래스 | 엔진 소유자 | 책임 |
|---|---|---|
| `URPGQuestManager` | GameInstance / `UGameInstanceSubsystem` | 캐릭터 ID별 승인된 퀘스트 조회 캐시 |
| `URPGScenarioManager` | GameInstance / `UGameInstanceSubsystem` | 캐릭터 ID별 챕터·이야기 해금 조회 캐시 |
| `URPGDialogueManager` | LocalPlayer / `ULocalPlayerSubsystem` | 해당 플레이어의 가능한 대화와 현재 열린 대화 |
| `URPGNarrativeCoordinator` | LocalPlayer / `ULocalPlayerSubsystem` | 서비스 참조·캐릭터 선택·조회/선택 요청·재시도·결과 조율 |

엔진이 Subsystem을 생성·초기화·종료한다. Coordinator는 `GetSubsystem`으로 기존 인스턴스를 참조하며 매니저를 `NewObject`로 생성하지 않는다. LocalPlayer의 대화 의존성과 GameInstance의 HTTP 의존성은 각 Subsystem의 초기화에서 지정한다. GameInstance 서비스는 Coordinator를 참조하지 않고 독립적으로 조회할 수 있다.

GameInstance 캐시는 현재 엔드포인트/계정에 묶고 캐릭터 ID로 나눈다. 조회에도 캐릭터 ID를 명시한다. 다른 계정의 캐시는 숨기고 새 계정의 승인된 결과 적용 시 비운다. 캐릭터 변경이나 한 LocalPlayer의 종료로 다른 캐릭터/LocalPlayer가 사용하는 GameInstance 캐시를 지우지 않는다. GameInstance 종료 시에는 해당 캐시를 정리한다. 캐시는 영속 저장을 대신하지 않는다.

`SelectCharacter` → `Refresh` → `BeginDialogue` → `Choose`/`CloseDialogue`를 사용한다. `OnChanged` 후 각 매니저를 조회하고 `OnRequestFinished`로 실패/결과 미확정을 표시할 수 있다. 선택 결과가 미확정이면 `RetryPendingChoice`가 같은 요청 ID·리비전·입력을 재사용한다. 대화를 닫아도 수락/완료를 요청하지 않는다.

서버 응답의 캐릭터/버전/형식과 계정·엔드포인트·연결 수명을 확인한다. 오래된 결과로 GameInstance 캐시를 되돌리지 않으며 퀘스트·시나리오·대화 리비전이 일치할 때 현재 대화를 노출한다. 다른 LocalPlayer가 같은 캐릭터 캐시를 갱신한 경우에는 이전 대화가 숨겨지고 해당 LocalPlayer가 다시 조회한다.

`Scripts/Tests/Run-NarrativeFrontendTests.ps1`은 현재 DLL을 독립 런타임에 복사해 `ProjectRPG.Narrative.Codec`, `.Coordinator`, `.PlayerTransport`, `.SubsystemOwnership`, `.DialogueWidget`과 기존 로비/내비게이션/뒤로가기 검사4를 실행한다. 엔진의 실제 Subsystem 소유권·캐릭터/계정 분리·종료·재시도·늦은 응답과 위젯의 선택 이벤트를 확인한다. HTTP/DB를 사용하는 선택 검사는 아래 별도 러너로 실행한다.

2026-11-04 원본 반영 확인: UHT·핵심 CPP6/신규 생성5/기존 생성 묶음4 컴파일과 정상 게임 모듈 집중 링크를 완료했다. 같은 `5557C6E1…1BB16EB9` DLL의 독립 런타임에서 위 native4 모두 Success·프로세스 종료0·검사 구간 오류0이다. 캐릭터별 캐시, 다중 GameInstance/LocalPlayer의 엔진 소유권과 분리, LocalPlayer 제거 후 GameInstance 캐시 보존, 계정 전환·재시도·오래된 응답을 확인했다. 기존 에셋/환경 경고는 남으며 GPU·실제 백엔드 HTTP 플레이 검증은 아니다.

전체 Editor 타깃 시도는 다른 사설 UBT의 전역 잠금 대기 중 자기 프로세스만 종료했으며, 위 결과를 전체 타깃 성공으로 표시하지 않는다. 기존 공유 출력의 0-byte probe OBJ1·누락 의존 import6을 복구한 뒤 집중 링크했다. 원본 제품 소스와 검증 사본을 구분하며, 사본 취득 직후 공유 입력/출력 예약을 해제했다. 자기 런타임/DLL/UserDir·캐시와 연결11개를 안전하게 정리했고 원본 `Binaries`/`Intermediate`와 유지할 소스/검사는 보존했다.

## 의뢰 진행 표시

2026-11-05부터 JSON 의뢰 정의의 선택 사항 `title`·`objectiveTexts`를 저널 표시 정보로 사용한다. 표시 문구는 개인 영속 상태나 완료 영수증에 복사하지 않으며, 조회 시 현재 정의와 서버 확정 진도를 연결한다. 기존 상태 스키마·선택 명령·의뢰 완료/성장 효과는 유지한다.

기존 `quests` 응답의 ID·정의 버전·상태·완료 목표를 유지하면서 `title`과 `objectives`(ID·문구·완료 bool)를 추가한다. bool은 저장된 완료 목표에서만 계산한다. 이전 클라이언트는 추가 필드를 무시할 수 있고 새 클라이언트는 표시 필드가 없는 이전 응답도 읽는다. 새 메타데이터가 있으면 목표 중복·완료 목록/상태와의 모순·문자/개수 한도를 검사한다. 작성한 목표 설명도 응답 1MiB 예산에 포함한다.

이야기 창의 스크롤 저널은 의뢰명과 진행 중/보고 가능/완료 상태, 진행 중 의뢰의 목표별 설명·완료 표식을 표시한다. 완료한 의뢰는 제목/상태를 남긴다. 표시 정보가 없으면 번역 가능한 일반 의뢰명·목표 번호/완료 수로 안내하며 내부 ID를 이름처럼 노출하지 않는다. 로컬 클릭으로 목표를 미리 완료 표시하지 않는다. 조회 문맥이 바뀌면 저널도 비운다. 추적 고정·지도 표식·전투 목표 사건은 이 표시 기능과 구분한다.

2026-11-05 저널 확인: 원본 UHT·관련 CPP/생성 코드 집중 컴파일·게임 모듈 링크와 같은 D511937B…5B6A0FB4의 native9/UE0/검사 오류0, 실제 game 클라이언트2의 HTTP/PG 완료·새 UE/백엔드 복원이 통과했다. 실제 응답의 제목/목표 설명이 화면에 반영되는지 확인하며, 영수증4·다른 캐릭터 진도0·성장 불변도 유지했다. 백엔드는 운영툴의 공통 호스트 동시 작업을 피해 Git3d388a4 사본에 본인 narrative8파일만 얹은 독립 입력으로 검증했다. 그 사본에서 빌드 경고/오류0·저작/기존 Memory·인증 HTTP 회귀도 통과했다. 운영툴이 합쳐진 최신 호스트 전체의 검증으로 표시하지 않는다.

본인 임시334,105,976 bytes/연결33·DB/자식 프로세스와 Git 사본을 폐기했고 Temp/대형 예외0이다. 해시는 당시 증거이며 사본 재실행 가능성을 뜻하지 않는다. 근거: `Saved/BuildChecks/NarrativeJournal-20261105/BackendSnapshot.json`, `943a0679756b45e9b8fae7e4e07afb8c/Result.json`, `7980d9e747c64ecb8def6ab8ce6b489a/Result.json`. `Run-NarrativeFrontendTests.ps1`과 `Run-NarrativeDialogueE2E.ps1`의 `TaskId`/`ProofName`으로 검사 예약/출력을 구분하고, 후자는 `BackendRoot`로 독립 서버 입력을 선택할 수 있다. 운영툴 담당의 의존성 인계에 맞춰 Core 프로젝트와 Razor 원본도 선택적으로 복사하도록 보완했으며, 해당 추가 복사 분기는 정적으로 확인했다. GPU·물리 마우스 검증과 전체 Editor 타깃 빌드는 이 결과에 포함하지 않는다.

## 로비의 이야기 창

캐릭터로 로비에 접속한 뒤 **인물과 이야기**를 누르면 해당 LocalPlayer의 `URPGNarrativeDialogueWidget`을 연다. 화면은 서버의 현재 가능한 대화, 인물·본문·선택지, 완료한 의뢰 수와 진행 저널을 표시한다. 진행할 대화가 없으면 빈 상태를 안내한다. 이 화면은 네이티브 UMG로 구성하며 기존 대화 Blueprint나 NPC 에셋을 교체하지 않는다.

대화 열기·닫기는 로컬 표시만 바꾼다. 수락·확인·보고는 표시된 선택지를 눌렀을 때만 Coordinator를 통해 서버로 전송한다. 응답 전에는 의뢰 진도나 해금을 먼저 바꾸지 않는다. 결과 미확정 시 **선택 결과 다시 확인**이 원래 명령을 재전송하며, 창을 닫았다 다시 열어도 미확정 명령을 보존한다. 접속 오류는 사용자용 문구로 안내하고 내부 ID·리비전·오류 코드는 화면에 노출하지 않는다.

인증·본인 캐릭터·로비/세션 상태를 다시 검사한 뒤 창을 열며, 캐릭터/접속 문맥이 바뀌면 이전 창을 닫거나 내용을 숨긴다. 조회/미확정 선택 중에는 캐릭터 변경과 새 던전 이동을 차단한다. 새 창에서 기존 미확정 선택을 복구할 수 있다. 전투 성장, 입장 조건, 부캐 이야기 건너뛰기 정책을 이 UI가 대신 판정하지 않는다.

`Scripts/Tests/Run-NarrativeDialogueE2E.ps1`은 캐시된 `postgres:17-alpine`과 Docker를 사용한다. 임시 DB에 명시적으로 마이그레이션하고, 독립 백엔드의 개발 인증으로 새 캐릭터 둘을 만든다. UE에서는 실제 플레이어 인증·로비 메뉴·UMG 버튼 이벤트·HTTP를 거쳐 개발 의뢰를 수락/확인/보고한다. 이어 같은 임시 DB를 유지한 채 백엔드와 UE를 새 프로세스로 시작해 복원한다. 완료/해금, 명령 영수증4건, 다른 캐릭터의 초기 진도, 성장 저장 불변을 대조한다. 개발 콘텐츠는 이 전용 백엔드에서만 켜며 원본 설정을 수정하지 않는다. 토큰·DB 비밀번호는 프로세스 메모리/자식 환경에서만 사용하고 결과 파일에 남기지 않는다.

두 러너는 각자의 `Saved/BuildChecks/NarrativeDialogue-20261104/<GUID>/Temp`와 자신이 시작한 프로세스/라벨 컨테이너만 정리한다. Content/Resources 연결은 원본을 따라가지 않고 해제하며 작은 로그·해시·결과만 보존한다. HTTP 검사는 독립 `-game -nullrhi` 클라이언트의 엔진 소유 GameInstance·LocalPlayer·Controller와 실제 화면에 추가된 UMG의 클릭 델리게이트를 사용한다. 물리 마우스·GPU 외형·실제 NPC 배치·다인 전투 사건 검증과는 구분한다.

2026-11-04 확인: UHT·로비 헤더 소비 코드를 포함한 집중 컴파일/게임 모듈 링크를 완료했다. `0DBE6945…552A6BC2`의 native9가 모두 Success·프로세스 종료0·검사 구간 오류0이다. 이후 제품6소스의 SHA는 그대로 유지하고 HTTP 검사 어댑터만 정상 게임 월드를 사용하도록 바꿨다. 최종 `4709F09A…4AAB4EC7`의 서로 다른 게임 클라이언트 두 개가 실제 HTTP 완료/새 프로세스 복원을 각각 통과했다. PostgreSQL 진도4·완료/해금·영수증4건·다른 캐릭터 진도0·성장 저장 불변을 확인했다. 로컬 근거는 `Saved/BuildChecks/NarrativeDialogue-20261104/CompletionResult.json`과 `d1ab4fb0e3504cd880c2b96172a57ee4/Result.json`이다.

초기 미보유 Slate 수명/검사 소유자 오류와 최소 RPC 월드의 비동기 컨트롤러 소멸, 냉시작 제한시간 초과는 실패 기록으로 남겼다. 최종 HTTP 검사는 직접 만든 월드·컨트롤러를 사용하지 않는다. 전체 Editor 타깃 빌드나 GPU 외형 확인을 이 결과로 대신하지 않는다. 모든 시도에서 본인 임시 2,019,439,774 bytes와 연결132개·자기 DB 컨테이너/프로세스를 정리했으며 Temp/대형 예외0이다. 원본 Binaries/Intermediate·다른 담당 변경은 보존했다. 폐기한 검사 사본의 해시는 당시 증거로만 사용한다.

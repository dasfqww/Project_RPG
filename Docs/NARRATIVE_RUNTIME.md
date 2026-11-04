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

`Scripts/Tests/Run-NarrativeFrontendTests.ps1`은 현재 DLL을 독립 런타임에 복사해 `ProjectRPG.Narrative.Codec`, `.Coordinator`, `.PlayerTransport`, `.SubsystemOwnership`을 실행한다. 엔진의 실제 Subsystem 생성/소유자·다중 GameInstance/LocalPlayer·캐릭터 분리·종료·계정 변경·재시도·늦은 응답을 확인하는 검사다. 실제 NPC 배치·선택지 위젯 에셋·다인 사건·클라이언트와 백엔드의 전체 HTTP 플레이는 별도 범위다.

2026-11-04 원본 반영 확인: UHT·핵심 CPP6/신규 생성5/기존 생성 묶음4 컴파일과 정상 게임 모듈 집중 링크를 완료했다. 같은 `5557C6E1…1BB16EB9` DLL의 독립 런타임에서 위 native4 모두 Success·프로세스 종료0·검사 구간 오류0이다. 캐릭터별 캐시, 다중 GameInstance/LocalPlayer의 엔진 소유권과 분리, LocalPlayer 제거 후 GameInstance 캐시 보존, 계정 전환·재시도·오래된 응답을 확인했다. 기존 에셋/환경 경고는 남으며 GPU·실제 백엔드 HTTP 플레이 검증은 아니다.

전체 Editor 타깃 시도는 다른 사설 UBT의 전역 잠금 대기 중 자기 프로세스만 종료했으며, 위 결과를 전체 타깃 성공으로 표시하지 않는다. 기존 공유 출력의 0-byte probe OBJ1·누락 의존 import6을 복구한 뒤 집중 링크했다. 원본 제품 소스와 검증 사본을 구분하며, 사본 취득 직후 공유 입력/출력 예약을 해제했다. 자기 런타임/DLL/UserDir·캐시와 연결11개를 안전하게 정리했고 원본 `Binaries`/`Intermediate`와 유지할 소스/검사는 보존했다.

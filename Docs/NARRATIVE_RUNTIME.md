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

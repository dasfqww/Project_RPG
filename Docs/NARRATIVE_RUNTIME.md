# 시나리오·퀘스트·대화 런타임

기준: [통합 기획안](SCENARIO_QUEST_DIALOGUE_DESIGN.md). 최초 구현일: 2026-11-04.

## 서버의 책임

| 클래스 | 책임 |
|---|---|
| `QuestManager` | 선행·수락·대화/던전/개인 상호작용 목표·보고·완료 판정 |
| `ScenarioManager` | 확정된 퀘스트 결과에서 챕터 완료와 이야기 해금 계산 |
| `DialogueManager` | 현재 자격에 맞는 노드·선택지 조회와 선택 검증 |
| `NarrativeCoordinator` | 매니저 연결, 저장 명령 실행, 승인된 결과 반환 |
| `DungeonObjectiveCoordinator` | 본인 캐릭터의 확정 클리어 증거를 검증하고 던전 목표 기록 |
| `NarrativeInteractionCoordinator` | 세션 서버의 개인 승인 사건을 원자 처리하고 당시 적용 결과 반환 |

## 메인·서브 의뢰와 선택 진행

`QuestDefinition.Category`는 기본 `Unspecified`인 `Main`/`Side` 분류다. 기존 JSON·코드 생성 정의는 기본값으로 호환한다. 카탈로그는 퀘스트의 순환/참조 검사가 끝난 뒤 선행 그래프를 검사해, 명시 메인 의뢰와 이야기 챕터가 선택 서브를 직접·간접 필수화하는 정의를 거절한다. 서브→메인 선행과 독립 서브 체인은 허용한다. 분류를 추가하는 것만으로 수락·완료·해금·보상을 실행하지 않는다.

서버 `QuestJournalEntry.From`이 현재 카탈로그의 분류를 문자열로 반환하며 개인 `NarrativeState`나 명령 fingerprint에는 저장하지 않는다. 기존 저장/리비전·영수증·스킵 정의 해시 형식은 그대로다. UE `FRPGNarrativeCodec`은 생략/null을 미분류로 읽고 정규 문자열 세 값만 허용하며 잘못된 응답은 기존 뷰를 보존한다. `FRPGNarrativePresentation::QuestHeading`이 NPC/로비 공용 저널의 메인·선택 서브/기존 미분류 제목과 상태를 구성한다. 실제 콘텐츠 저작은 [분류 계약](NARRATIVE_CONTENT_AUTHORING.md#메인선택-서브-의뢰-분류)을 따른다.

검사 진입점은 `Scripts/Tests/Run-NarrativeCategoryTests.ps1`과 `Run-NarrativeCategoryUETests.ps1 -HttpWirePath <앞 검사의 HttpWire.json>`이다. 후자는 인증 HTTP에서 얻은 실제 스냅샷을 UE 파서/표시에 전달하고 기존 목표 수량 회귀를 함께 실행한다. 작은 native 모듈의 링크/실행과 전체 UHT·관련 실제 CPP 컴파일을 구분하며 전체 게임 링크·GPU/물리 입력·Steam 검증을 뜻하지 않는다.

2026-11-13 확인: 게시 `2132b0b1`+고정 입력의 backend `0022cd57be934d099b48a2fb966f9fea`에서 Release3종 경고/오류0, 정의/직접·간접 선행·미분류 호환·서브 미수행 메인 완료/해금·기존 저장/스킵 자격·실제 인증 HTTP와 기존 이야기/스킵 회귀가 통과했다. 그 실제 HTTP 바이트를 사용한 UE `6a8cd2fcc91c4bf5b8e543a8d413a45f`의 `C6D3CC60…2A572D43`에서 native8 Success/검사 오류·경고0/종료0, 전체 UHT/실제 CPP5 컴파일0을 확인했다. 제품/검사·러너 입력 SHA와 생산자/소비자 HTTP SHA가 일치한다. 초기 검사 코드의 예외 접근/생성자 오류와 분류의 대소문자 비교 문제는 수정 후 결과와 구분하며 기동 Error15/Warning4는 검사 구간과 별도다. 자기 backend2/UE2회221,675,712 bytes(211.41MiB)를 폐기해 Temp/소유 PID/새 예외0이다. 작은 근거 `Saved/BuildChecks/NarrativeCategory-20261113/CompletionAudit.json`을 유지한다. 사본 DLL은 폐기되어 해시는 당시 증거이며 실제 월드 화면/PG/Steam/다인 검증을 뜻하지 않는다.

## 던전 클리어 증거 반영

`POST /api/characters/{characterId}/narrative/dungeon-objectives`는 본인 플레이어 인증으로 아래 입력을 받는다. 클라이언트는 기존 클리어 기록을 참조하며 결과·참가자·발생 시각·완료 수치를 제출하지 않는다. 관리자·게임 서버 토큰은 이 개인 요청을 대행할 수 없다. 기존 세션 종료 후 서버 토큰 권한은 그대로 유지한다.

```json
{
  "requestId": "00000000-0000-0000-0000-000000000001",
  "expectedRevision": "2", "catalogVersion": 7,
  "dungeonSessionId": "00000000-0000-0000-0000-000000000002",
  "questId": "SAMPLE.Clear", "objectiveId": "clear"
}
```

Memory/PostgreSQL의 `INarrativeDungeonEvidenceRepository`가 기존 정산의 불변 참가자 목록·최초 접수 시각과 던전/난이도를 조회한다. 정산 접수는 기존 권위 서버가 성공을 확정한 시점이므로 재화 전달 대기 중에도 증거가 유효하다. 현재 세션의 가변 멤버 목록이나 보상 전달 완료 시각으로 대체하지 않는다. 참가 자격이 없는 캐릭터/부캐, 임의 세션 ID, 미수락/비활성 목표, 목표 활성화 전 클리어, 다른 던전/난이도를 거절한다. 순차 목표는 마지막 선행 목표가 기록된 순간부터 활성화된다. 동일 퀘스트는 한 시도의 증거를 한 목표에만 사용한다.

목표의 활성화 시각·사용한 시도 ID는 기존 캐릭터 진도 JSON에 저장한다. 상태와 영수증은 기존 캐릭터 잠금/트랜잭션으로 원자 저장한다. 서버 증거 요청은 대화 요청과 다른 fingerprint를 사용하며 기존 대화 영수증은 그대로 재생된다. 같은 요청 ID·입력의 재전송은 당시 결과를 반환한다. 다른 입력 재사용·낡은 리비전은 409, 증거/소유권 불일치는 403이다. 성공 응답은 기존 이야기 저널 형식과 문자열 리비전, `no-store`를 유지한다. 목표 달성만으로 보고·해금·보상을 대신 처리하지 않는다.

UE의 던전 결과 자동 반영은 아래 절의 읽기·별도 POST 절차를 따른다. 개인 조사/상호작용과 처치 목표의 서버 처리 및 명시 저작한 UE 사망 연결은 각각의 절을 따르며, 프로젝트 전체 참여 정책·실제 맵/다인 검증은 후속 단위다.

## 개인 처치 목표 서버 사건

`POST /api/dungeon-sessions/{sessionId}/narrative/kills/{characterId}`는 해당 세션에 배정된 전체 GameServer 인증만 받는다. 플레이어·관리자·다른 세션·종료 후 제한 인증은 쓸 수 없다. 본문은 `eventId`, `enemyId`, 양수 int64 문자열 `connectionGeneration`, UTC `observedAtUtc`다. 수신한 수량/완료 플래그는 사용하지 않는다. 서버는 승인된 적의 한 번의 사망에 고정된 사건 ID를 발급하고, 개인별 자격 판정 뒤 같은 ID/적/세대/시각으로 재전송해야 한다. 재사용된 몬스터의 새 생명은 새 사건이다.

`NarrativeKillCoordinator`는 기존 개인 현장 사건 저장의 세션→멤버/lease→캐릭터 잠금·최종 입장/접속 세대·소유권 검사를 재사용한다. 저장 어댑터의 `IsKillEvent`는 내부 전용이며 공개 조사 API가 선택할 수 없다. fingerprint의 `personal_kill_v1` 도메인으로 조사/대화와 분리하고 기존 `character_narrative_commands`의 캐릭터/사건 키를 함께 사용한다. 스키마 변경은 없다. 과거 조사 fingerprint는 유지한다. 사건 시각은 활성화 시각 이상이며 미래가 아니고 현재로부터5분 이내여야 한다. 기존 영수증 재전송은 새 사건의 자격/시각 검사보다 먼저 복구한다.

사건 이전 스냅샷에서 현재 수락·선행 조건과 정확한 던전/난이도/적이 맞는 목표마다 1회를 누적한다. 여러 의뢰/병렬 목표를 한 리비전으로 반영하지만 같은 사건이 연 순차 목표를 연달아 달성시키지 않는다. 이미 달성한 목표는 올리지 않는다. 미수락·선행 미충족·다른 적 등 무효과 사건도 리비전을 유지한 영수증으로 저장해 수락 후 소급 재사용을 막는다.

진행은 활성화 시각·필요 횟수·현재 횟수·마지막 사건을 저장한다. 음수/상한 초과·달성 플래그 모순·필수 메타데이터 누락·선행/후속에 같은 마지막 사건을 사용한 상태는 읽기/새 전이에서 거절하고 초기화하지 않는다. 저널은 `currentCount`/`requiredCount`를 제공한다. 응답 영수증은 사건/캐릭터, 문자열 `revision`, 적용 목표별 `questId`/`objectiveId`/`count`/`requiredCount`이며 `no-store`다. 영수증은 당시 저장 스냅샷에서 렌더링하여 뒤의 처치·보고·카탈로그 변경으로 과거 부분 횟수가 바뀌지 않는다.

`invalid_kill_event`는400, 현재 멤버/세대 불일치 등은403, `kill_event_out_of_window`·사건 ID 내용 변경/다른 명령과의 충돌은409다. 자동 보고·아이템/XP·해금·파티 전파는 하지 않는다. UE의 적별 ID/생명 사건 생성, 사망 확정과 개인 자격/기여자 선정, 전송은 후속 연결이며 저널 수량 표시는 아래 절을 따른다. 이 API의 GameServer 인증만으로 실제 몬스터 사망/참여 정책의 완성을 주장하지 않는다.

검증 진입점은 `Scripts/Tests/Run-NarrativeKillTests.ps1 -Postgres`다. 게시 Git Backend와 명시한 본인 입력만 고정하여 실제 Memory/PG·Bearer HTTP의 수량 누적/부분 저널·동시 재전송·무효과 영수증·타입 충돌·순차 목표·세대/종료·부캐/성장·손상 상태와 기존 조사·이야기 회귀를 확인한다. UE/전투 실행은 검사하지 않는다. 사본·빌드 산출물·전용 DB는 종료 시 정리하고 작은 결과/입력 해시만 남긴다.

2026-11-09 검증: 게시 `cd8182dd`+본인11입력의 Release 빌드3종 모두0경고/0오류, 새 처치 목표의 Memory/실제PG·인증 HTTP와 기존 조사 Memory/PG·이야기 Memory/HTTP 회귀가 통과했다. 잘못된 적·던전·난이도, 부분 누적과 영수증 복원/카탈로그 변경, 순차/병렬·역할/세대/종료·부캐/성장 불변도 확인했고 최종 입력 SHA가 유지됐다. 추가 손상 검사 fixture의 잘못된 초기 리비전은 거절된 원인을 확인하고 주입 리비전을 수정하여 재실행했다. 자기3회 임시87,249,524 bytes/전용 DB 폐기·Temp/예외0. 근거 `Saved/BuildChecks/NarrativeKill-20261109/56fb7aeb52714409b80ed81f5245681c/Result.json`과 `CompletionAudit.json`이며 폐기한 입력/빌드 해시는 당시 증거다. 실제 UE 처치 연결 검증을 뜻하지 않는다.

## 계정의 직접 완료 챕터 이력

`GET /api/characters/{characterId}/narrative/account-history`는 소유 캐릭터를 기준으로 같은 계정의 직접 완료 이력을 조회한다. 응답은 `characterId`, `catalogVersion`, `chapters`이며 각 항목은 `chapterId`, 선택형 `title`, `completedOnSelectedCharacter`, `completedOnOtherCharacter`다. 완료가 확인된 챕터만 ID 오름차순으로 한 번씩 반환한다. 선택 캐릭터와 다른 캐릭터가 둘 다 완료했으면 두 플래그가 모두 true다. 직접 진행 전의 새 캐릭터는 자기 진도 행을 생성하지 않고 다른 캐릭터의 완료 이력만 읽을 수 있다. 제목은 챕터 정의의 선택형 표시 이름으로 진행/해금 의미를 바꾸지 않으며 null인 기존 데이터도 유지한다.

플레이어 인증과 선택 캐릭터 소유권이 필요하다. 익명은401, 관리자/게임 서버와 다른 계정·없는 캐릭터는403이다. 처리된 조회/거절/저장 충돌 응답에는 `no-store`를 적용한다. 다른 캐릭터의 ID·이름·Steam ID·퀘스트별 진도·이야기 해금은 응답에 넣지 않는다. 외부 계정의 완료/손상된 상태는 조회에 포함하지 않는다.

Memory는 기존 캐릭터 잠금 안에서 소유권과 모든 해당 캐릭터의 상태를 함께 읽는다. PostgreSQL은 선택 캐릭터의 소유 계정을 CTE로 확인하고 같은 계정의 캐릭터/진도 행을 LEFT JOIN하는 단일 SELECT의 MVCC 스냅샷을 사용한다. 상태가 없는 캐릭터는 빈 읽기 모델로 취급한다. 조회를 위해 상태·명령 영수증·성장을 삽입하거나 FOR UPDATE 잠금을 추가하지 않으며 스키마 변경도 없다.

`NarrativeAccountHistory`는 각 상태를 기존 카탈로그/저장 검증으로 확인한 뒤, 해당 챕터의 필수 의뢰를 모두 `Completed`로 보고한 사실에서 직접 완료를 산출한다. `ReadyToReport`, 단독 챕터/해금 플래그와 로컬 UI 추정은 근거가 아니다. 같은 계정 안에 카탈로그 비호환·손상 상태가 있으면409로 실패하며, 그 부분을 누락한 정상 이력이나 빈 이력으로 바꾸지 않는다. 원본 상태를 초기화/수정하지도 않는다.

이 응답은 **완료 이력**이다. 스킵 실행 가능 여부·비용·보상·현재 입장 자격을 뜻하지 않으며, 다른 캐릭터의 이력을 읽어도 선택 캐릭터의 퀘스트/챕터/이야기 해금과 성장은 변하지 않는다. 스킵 실행은 아래 별도 미리보기/명령 계약을 사용하며 이 조회를 권한 영수증으로 사용할 수 없다. UE 완료 이력 패널은 계속 읽기 전용이다.

검증 진입점은 `Scripts/Tests/Run-NarrativeHistoryTests.ps1 -Postgres`다. 게시 Backend와 명시 입력만 고정한 사본에서 Memory·실제 PG/재개방·인증 HTTP의 계정 격리/미보고 제외/정렬·중복/현재 캐릭터 구분/동시 조회/취소/손상 거절을 검사한다. 선택 캐릭터의 진도·해금·성장 불변과 PG 진도/영수증/성장 테이블의 읽기 전후 일치, 기존 이야기·처치 회귀도 확인한다. 자기 DB/빌드 사본은 종료 시 폐기하며 실제 UE UI/스킵 실행 검증과 구분한다.

2026-11-10 확인: 게시 `e8edb9bd`+본인8입력의 최종 `bfe0b9e324d74e759e8eef30d5907081`에서 Release3종 경고·오류0, 새 이력 Memory/실제PG/재개방·인증 HTTP와 기존 이야기 Memory/HTTP·처치 Memory/PG/HTTP 회귀가 통과했다. 24개 조회 작업과 보고 작업을 별도 스레드로 실행하고 같은 의뢰에 의존하는 챕터의 일관성·순서/중복·자기/다른 캐릭터 플래그·미보고 제외·타 계정/손상 격리·카탈로그 비호환/취소를 확인했다. 선택 캐릭터의 진도/해금/성장과 PG 진도·영수증·성장 테이블의 읽기 전후 내용이 같으며 최종 입력/러너 SHA가 유지됐다. 최초 HTTP fixture의 미활성 GameServer 자격이401로 거절된 원인을 확인하고 세션 배정/시작 후403 역할 거절을 검증했다. 자기3회 임시90,037,036 bytes/전용 DB를 폐기했고 프로세스/Temp/컨테이너/예외0이다. 작은 근거 `Saved/BuildChecks/NarrativeHistory-20261110/CompletionAudit.json`을 남겼다. UE UI·스킵 실행/비용/보상을 구현하거나 검증한 결과는 아니다.

## 챕터 이야기 스킵 서버

2026-11-12 사용자 확정 C7~C9: 무료·한 챕터 단위, 스킵한 의뢰 보상 미지급, 기존 훈련·입문 해금은 직접 수행한다. `NarrativeStorySkip`은 본인 플레이어의 계정 직접 완료를 검사하고 미리보기와 명령을 분리한다. 챕터의 `allowStorySkip`은 기본 false이며 검토한 이야기 의뢰에만 명시한다. 개발/기본 카탈로그를 자동 활성화하지 않는다.

- `GET /api/characters/{id}/narrative/story-skip-preview?chapterId=...`: 종료할 남은 의뢰 ID/제목, 새 이야기 해금, 문자열 리비전·카탈로그 버전·미리보기 해시, 비용0/의뢰 보상 미지급/기존 훈련 유지. 계정 원본 캐릭터 ID는 노출하지 않는다.
- `POST /api/characters/{id}/narrative/story-skips`: `requestId`, 문자열 `expectedRevision`, `catalogVersion`, `chapterId`, `previewHash`, `confirmed:true`. 본인 소유·직접 완료 근거·현재 선행/진도·던전 참가를 저장 잠금 안에서 재검사한다. 보지 않은 변경 결과는409로 거절해 새 미리보기를 요구한다. 관리/서버 대행은403, 미인증은401, 처리한 응답은 `no-store`다.

`INarrativeStorySkipRepository`의 Memory는 dungeon→character 잠금, PG는 선택 캐릭터의 소유 행 FOR UPDATE와 현재 계정 상태 조회를 사용한다. 회원/lease FK 삽입도 이 소유 잠금과 직렬화된다. Waiting/Loading/InProgress/SettlementPending 참가자는 새 적용이 거절된다. 동일 요청의 확정 영수증은 이후 참가/진도 변경에도 원래 상태를 복구하며 새 효과를 만들지 않는다. 스킵·기존 대화·서버 사건은 같은 캐릭터/요청 키를 사용하되 fingerprint 도메인을 분리한다. 상태와 영수증은 같은 트랜잭션에서 저장한다.

`storySkips`에는 챕터·당시 카탈로그 버전·UTC 시각·실제로 생략한 의뢰 ID·정의 해시를 저장한다. 미수락 의뢰를 가짜 `Completed`로 생성하지 않으며 진행/보고 대기 의뢰의 이미 저장한 목표 사실도 보존한다. 생략 의뢰는 저널/대화/던전 후보에서 제외되고 처치·조사·클리어로 다시 진행하지 않는다. 직접 완료한 의뢰와 서브는 유지한다. 다음 의뢰의 이야기 선행은 스킵으로 충족하지만 정보형 대화의 실제 완료 조건과 계정 직접 완료 이력은 바꾸지 않는다. 이야기 챕터/해금과 스킵 기록은 함께 반영되고 응답은 `storySkippedChapters`를 별도로 제공한다. 기존 성장·스킬·실습 milestone·재화·보상 저장은 호출하지 않는다.

예전 저장에 `storySkips`가 없으면 빈 배열로 읽는다. 스킵 정의 해시는 챕터 의뢰·해금·의뢰 버전/선행에 묶어 무관한 카탈로그 버전 증가를 허용한다. 관련 정의/실제 완료와 모순되거나 손상된 기록은 거절하고 초기화하지 않는다. 관련 콘텐츠 변경은 사전 호환 검토/명시 이행이 필요하다. 이전 서버로의 저장 롤백 호환을 보장하지 않는다.

검증 진입점 `Scripts/Tests/Run-NarrativeSkipTests.ps1 -Postgres`: Memory·실제 PG·인증 HTTP의 미리보기/확인·변경 경합·재전송·성장 불변·소유권·참가 차단·기록/영수증 실패 롤백·재개방, 기존 이력/이야기/처치 회귀를 확인한다. 저장소에서 참가 세대를 확정한 서버 처치/조사 사건도 스킵된 목표에는 무효과이며 클리어 요청/후보도 차단한다. UE의 미리보기/확인 연결은 아래 절을 따른다.

2026-11-12 확인: 게시 `965e47ab`+고정17입력의 최종 `d24d225c8b1d4bba9a66ef6b724a394a`에서 Release4종 경고/오류0, 새 스킵 Memory/실제PG·새 저장소/인증HTTP와 기존 이력/이야기/처치 회귀가 통과했다. 실제 PG의 소유 행/FK 대기 경합에서 참가가 선행하면 스킵을 거절했고, 훈련으로 얻은100XP/스킬 보존·남은 실습 미완료 유지와 영수증 INSERT 실패의 전체 롤백을 확인했다. 초기 namespace/검사 fixture 오류·Docker 미기동과 정리 복구는 최종 성공과 구분한다. 별도 미접속 pipe를 쓰는 자식 검증으로 공유 Docker를 바꾸지 않고 미기동 실패 시 임시 사본 정리도 확인했다. 자기7회142,610,679 bytes(136.00MiB)/사설 PG를 폐기했고 Temp/소유 PID/컨테이너/예외0이다. 작은 근거 `Saved/BuildChecks/NarrativeSkip-20261112/CompletionAudit.json`을 유지한다. 서버 단위이며 UE 실행 UI·Steam·실제 전투/다중PC 검증을 포함하지 않는다.

## UE 계정 완료 이력 패널

`URPGNarrativeHistoryPanel`을 기존 이야기 창의 저널 스크롤 하단에 포함한다. 창 열기는 요청을 보내지 않으며 ‘완료 이력 다시 확인’ 버튼으로 조회한다. 챕터 제목과 이 캐릭터/다른 캐릭터/양쪽 완료를 구분해 표시하고, 제목이 없는 기존 챕터는 읽을 수 있는 대체 이름을 쓴다. 내부 챕터 ID를 화면 이름으로 쓰지 않는다. 진짜 빈 이력·조회 중·조회 실패를 구분하고, 실패 시 재조회 안내를 표시한다. 스킵 버튼·실행 가능 표시·횟수/해금 수정은 없다.

LocalPlayer의 `URPGNarrativeCoordinator`가 순수 조회 클라이언트 `FRPGNarrativeHistoryClient`를 소유한다. 기존 대화/던전 명령과는 별도인 `FRPGHttpNarrativeTransport` 인스턴스가 플레이어 GET 요청을 만들고 경로만 `account-history`로 지정한다. 원래 `UHttpWebManager`의 플레이어 전용 자격/갱신 문맥 검사를 재사용하며 GameServer 자격으로 대체하지 않는다. 공용 전송 인터페이스는 구체 HTTP 구현과 분리해 순수 읽기/모의 전송 검사에 사용할 수 있다.

조회는 캐릭터·계정·endpoint와 요청 세대에 묶인다. Coordinator의 캐릭터 및 실제 선택 캐릭터가 맞아야 요청/표시할 수 있으며 계정/endpoint/인증 문맥이 바뀌면 예전 이력을 숨긴다. 캐릭터 전환·승인된 개인 진행 변경·LocalPlayer 종료는 자기 조회를 취소/초기화한다. 이전 요청/다른 캐릭터/낡은 자격의 응답은 적용하지 않는다. 조회 중 중복 클릭은 같은 요청을 교체하지 않으며 실패한 조회를 빈 정상 이력으로 취급하지 않는다. 다시 조회할 때 이전 화면은 숨기고 응답 전체를 검증한 뒤 한 번에 적용한다.

코덱은 소유 캐릭터 GUID·양수 int32 카탈로그 버전·최대256개 챕터·정렬/중복 없는 ASCII 논리 ID·실제 bool 두 개와 적어도 하나의 완료 여부·선택 제목의 타입/1~256자 비공백을 검사한다. 제목의 누락/null은 기존 응답으로 허용한다. 숫자 문자열·불완전 항목·잘린 JSON은 거절하고 기존 출력 객체를 부분 덮어쓰지 않는다. UI는 현재 선택 문맥을 다시 확인하며, 닫힐 때 자기 이벤트 구독만 해제한다. 늦은 응답은 닫힌 위젯을 붙잡거나 다른 계정에 노출하지 않는다. 별도 읽기는 대화/던전의 미확정 본문·요청 ID·영수증을 교체하거나 재전송하지 않는다.

검증 진입점은 `Scripts/Tests/Run-NarrativeHistoryUETests.ps1`이다. 실제 코덱/조회 클라이언트와 작은 UMG 패널을 사설 native 모듈에서 검사하고, 게시 Source/Config/플러그인에 명시 입력만 얹은 사본에서 전체 UHT와 실제 이력/패널/HTTP 전송/Coordinator/기존 이야기 창 CPP를 컴파일한다. 제목 저작·HTTP 응답과 저장 불변은 기존 Backend 이력 러너의 전용 TaskId/ProofName으로 확인한다. 전체 게임 모듈 링크·실제 플레이어 자격 HTTP 왕복/GPU·기존 이야기 창 입력의 E2E 증거와 구분한다.

2026-11-10 확인: 게시 `f3a1799d`+명시 입력의 최종 `8753fc44d1f448478fe7b469da6e4721`에서 작은 native6종 Success/검사 오류·경고0/UE 종료0, 전체 UHT/실제 CPP6 컴파일0을 확인했다. 버튼의 실제 OnClicked 구독을 통해 조회·이름/완료 주체·빈 이력/실패·계정 변경 시 표시 해제·닫기 구독 해제를 검사했다. 순수 클라이언트는 이전 세대/캐릭터/자격 응답 차단과 알림 중 선택/소유자 해제 재진입도 통과했다. 초기 작은 모듈의 구체 HTTP export 의존성 링크 실패는 전송 계약 헤더 분리로 보완했고, 실제 로비 선택 검사와 콜백 중 수명 보강 뒤 최종 사본으로 다시 확인했다. Backend `e5be94275285485aaddf5917ba0cb6c1`은 제목 유효성/한글 HTTP 응답·Memory/PG·기존 이야기/처치 회귀의 Release3종0경고/0오류로 통과했다. 최종 소스/러너 SHA 동일·엔진 기동 진단 별도, 자기 UE4회/Backend1회 임시604,983,396 bytes·중복 proof339,388 bytes를 폐기하고 프로세스/DB/Temp/예외0을 확인했다. 작은 근거 `Saved/BuildChecks/NarrativeHistoryUI-20261110/CompletionAudit.json`을 남겼다. 전체 게임 링크·설치 DLL/실제 플레이어 HTTP·GPU/물리 입력 검증을 실행한 결과는 아니다.

## UE 챕터 스킵 미리보기와 확인

기존 이야기 창의 저널 스크롤에서 완료 이력을 명시적으로 조회한 뒤 ‘부캐 이야기 건너뛰기’의 챕터를 선택한다. `URPGNarrativeStorySkipPanel`은 다른 캐릭터의 직접 완료 후보를 표시하며 현재 캐릭터가 이미 끝낸 챕터는 제외한다. 같은 제목은 순번으로 구분하고 내부 ID를 제목으로 쓰지 않는다. 창 열기·선택·재열기는 스킵 POST를 보내지 않는다. 후보는 권한 영수증이 아니므로 ‘건너뛸 내용 미리보기’에서 서버 자격/정책을 확인한다. NPC 대화 창에서는 이 패널을 숨긴다.

미리보기는 챕터/남은 의뢰 제목, 새 이야기 해금 개수, 무료·스킵 의뢰 보상 미지급·남은 훈련 직접 수행/완료한 훈련·스킬 유지·성장 점핑 없음 정책을 표시한다. ‘이 내용으로 건너뛰기’ 버튼을 누를 때만 고정된 요청 ID·문자열 리비전·카탈로그 버전·해시·`confirmed:true`를 전송한다. 다른 챕터를 선택한 직후의 오래된 확인 이벤트도 재검사해 차단한다. 캐릭터/계정/엔드포인트/진도 변경, 대화·던전 미확정 명령, 던전 참가/진행 요청 중에는 새 스킵을 시작하지 않는다. 미리보기 닫기는 저장 명령을 만들지 않는다.

LocalPlayer의 `URPGNarrativeCoordinator`가 `FRPGNarrativeSkipClient`를 소유하고 기존 `IsBusy`/`HasPendingChoice`/재시도 경계에 포함한다. 별도 `FRPGHttpNarrativeTransport` 인스턴스는 기존 플레이어 전용 build-only 요청/자격 검사를 사용하며 경로를 미리보기 GET 또는 스킵 POST로 바꾼다. GameServer 자격으로 대체하지 않는다. 서버 확정 응답만 기존 매니저의 리비전 검사 후 적용하고 필요한 던전 목표 조회를 예약한다. 계정 완료 이력은 계속 별도 읽기다.

`FRPGNarrativeSkipCodec`은 대상 캐릭터/챕터·정수/문자열 리비전·카탈로그·순서/중복/한도·제목·해시와 확정 정책 세 값을 검사한다. 확정 응답에는 원래 리비전+1, 원래 카탈로그와 해당 챕터의 `storySkippedChapters`/`completedChapters` 일치가 필요하다. 응답 유실·5xx·401/403·형식 오류·불명확한 오류는 원래 본문을 유지하며 로컬 성공으로 바꾸지 않는다. 저장/호환성 오류는 이미 확정된 영수증을 읽다가 발생할 수도 있어409여도 미확정으로 유지한다. 명확한 정책/리비전/미리보기 거절만 새 검토 상태로 돌아간다.

창을 닫아도 미확정 명령은 같은 LocalPlayer 서비스에 남고, 재열기 후 ‘스킵 결과 다시 확인’ 또는 기존 진행 결과 재시도는 같은 요청 ID/본문을 사용한다. 다른 계정·엔드포인트로 넘기지 않는다. 이 명령의 로컬 영속 저널은 없으며 애플리케이션 재시작 후에는 서버 진도를 다시 읽는다. 서버의 일회성 챕터 완료/영수증이 중복 적용을 차단하며 새 챕터를 자동 적용하지 않는다.

검증 진입점은 `Scripts/Tests/Run-NarrativeStorySkipUETests.ps1`이다. 순수 클라이언트와 실제 UMG 콤보/버튼의 codec·명시 확인·재시도·불명확한 영수증 오류·계정/진도 격리·수명/재진입·창 재열기를 검사하고 기존 완료 이력 회귀를 함께 실행한다. 독립 작은 모듈의 링크/네이티브 실행과 전체 프로젝트 UHT/관련 실제 CPP 집중 컴파일을 구분한다. 전체 게임 링크·실제 플레이어 HTTP·GPU/물리 입력/Steam/다인 검증은 포함하지 않는다.

2026-11-13 확인: 게시 `c30a5698` 기반 native `4fbe64891d2740d293f2fc9a3992e4cb`의 실제 `FB8D1E3A…255FC693E`에서 native13 Success/검사 오류·경고0/종료0이 통과했다. 최초 성공 후 영수증 조회의409 호환성/저장 오류를 미확정으로 유지하는 분류를 보완해 재검사했다. 이후 조율자의 보류된 던전 진도 재개를 보완한 `c0b5c631c55e4be8984e3c5a3958cfae`에서 전체 프로젝트 UHT/실제 CPP10 집중 컴파일0을 확인했다. 네이티브 모듈에 실제 복사·컴파일한13입력은 그대로이며 최신 호출부/전체19입력과 러너 SHA도 고정했다. 자기3회645,275,821 bytes(615.38MiB)·중복 로그를 폐기해 Temp/소유 PID/예외0이다. 작은 근거 `Saved/BuildChecks/NarrativeSkipUI-20261113/CompletionAudit.json`에 두 검증 범위와 입력을 구분해 유지한다. 사본 DLL은 폐기되어 위 해시는 당시 증거다.

## UE 사망 사건과 개인 진도 갱신

`URPGNarrativeKillComponent`를 콘텐츠의 `ARPGNonPlayerCharacter`에 명시적으로 붙인다. 기본 정책은 `Disabled`다. `ConfirmedMembersInRadius`를 선택한 NPC만 서버의 초기화 완료·양수 HP에서 준비되며, 실제 HP 0과 GAS 사망 태그가 함께 확인된 때 한 번 사건을 만든다. 처음부터 죽은 NPC·단순 Destroy/EndPlay·정리 중인 소환수는 근거가 아니다. 한 Actor를 회복/태그 제거로 재사용해도 다시 인정하지 않으며 새 생명은 새 Actor로 생성한다. 기존 Attribute/NPC/드롭·무력화 코드를 수정하지 않는다.

이 명시 저작 정책은 사망 순간 같은 확정 세션의 현재 Pawn이 반경 안에 있는 멤버를 선택한다. 캐릭터·계정·세션·현재 입장 승인·연결 세대·적대 팀·실제 위치를 서버에서 확인하고 캐릭터를 중복 제거한다. 살아 있어야 한다거나 피해/막타·시야를 요구하지 않으며, 사망한 멤버도 Pawn과 자격/반경을 만족하면 포함된다. 연결이 끊겨 현재 Pawn/승인 근거가 없는 멤버의 과거 전투 참여를 추정하지 않는다. 반경 방식은 선택 가능한 구현이며 프로젝트 전체 인정 정책의 확정이 아니다. 피해/지원 참여 기록 방식과 단절 전 참여 보존은 별도 정책·증거 연결이 필요하다.

`URPGNarrativeKillSubsystem : UGameInstanceSubsystem`이 사건 ID·관측 UTC·적 ID·개인 캐릭터/연결 세대와 전송 본문을 사망 순간 고정한다. 같은 사망 ID를 각 개인 명령에 쓰며 NPC 제거 후에도 소유 대기열이 유지된다. 클라이언트의 처치 제출 RPC는 없다. `UHttpWebManager::CreateServerNarrativeKillRequest`는 배정 GameServer 자격으로만 기존 kills API를 호출한다. 동시 요청은 최대4, 미해결 개인 사건은 최대1024이며 초과 시 해당 사망 전체를 큐에 넣지 않고 서버 오류를 기록한다. 자동 재시도는 1/2/4/8/16/30초 뒤 동일 본문으로 계속하며 소유 계정/대상을 다시 선택하지 않는다.

정확한 event/character/revision·중복 없는 목표와 정수 count/requiredCount가 맞는 200만 성공이다. 유실/잘린 200·권한 실패·5xx·문맥이 바뀐 응답은 미확정으로 유지한다. 저장 영수증 검사가 시간 창보다 먼저 실행되는 서버 계약에 따라 typed `kill_event_out_of_window`만 안전하게 종결 거절한다. 요청 세대로 늦은 이전 콜백을 제외한다. 월드/서버/세션/endpoint가 바뀌거나 GameInstance가 종료되면 자기 요청을 취소하며, 이미 저장된 결과를 되돌리지 않는다. 대기열은 메모리에 있으므로 서버 프로세스 중단 전에 전송하지 못한 사건의 디스크 복구는 지원하지 않는다.

성공 영수증 뒤 현재 원래 계정/캐릭터·세션의 소유 Controller에 `URPGNarrativeProgressComponent`가 갱신 알림을 보낸다. 클라이언트는 알림 수량을 적용하지 않고 기존 인증 GET으로 재조회한다. 계정/endpoint·선택 캐릭터·PlayerState/세션을 다시 확인하며, 진행 중 조회나 미확정 대화/던전 선택은 유지하고 끝난 뒤 재조회한다. 가까운 알림은 합치며 NPC 수명이나 방장에게 개인 결과를 맡기지 않는다. 재접속 후 기존 조회로 저장된 진도를 복원한다.

검증 진입점은 `Scripts/Tests/Run-NarrativeKillUETests.ps1`이다. 실제 codec/재시도/사망 생명 경계를 작은 native 모듈에서 검사하고, 게시 Git에 본인 입력만 얹은 사설 사본에서 전체 UHT와 실제 NPC component/outbox/알림/HTTP/Controller CPP를 컴파일한다. 전체 게임 링크·사망/파티/HTTP/PG 네트워크 실행·기존 맵 배치 검증과 구분한다.

2026-11-09 확인: 게시 `16ff2be9`+명시 입력의 작은 native4종 Success/검사 오류·경고0/UE 종료0, 최종 `7b81f2b03ecf4988ba2feef0ab730083`의 전체 UHT/실제 CPP8 컴파일0을 확인했다. 초기 테스트 NaN API·UE5.8 Pawn 포인터 형식의 컴파일 실패를 보완했으며, 최종 수정은 outbox CPP 한 곳이고 native 입력5개/러너는 통과본과 SHA 동일하다. 엔진 기동 Error15/Warning4는 검사 결과와 별도다. 자기3회 임시292,061,917 bytes와 중복 proof215,910 bytes를 폐기했고 프로세스/Temp/예외0이다. 작은 근거 `Saved/BuildChecks/NarrativeKillUE-20261109/CompletionAudit.json`·`FinalSourceManifest.json`을 남겼으며 폐기한 사본/DLL 해시는 당시 증거다. 실제 NPC·다인/RPC·HTTP/PG 전송 및 전체 게임 링크를 실행한 결과는 아니다.

## UE 목표 수량 검증과 저널

`FRPGQuestObjectiveView`는 `bHasCount`, `CurrentCount`, `RequiredCount`를 읽기 모델로 제공한다. 코덱은 두 필드가 함께 없는 옛 응답과 둘 다 null인 비수량 목표를 허용한다. 수량 목표는 두 필드 모두 실제 JSON 숫자여야 하고 정수·필요량1~10000·현재량0~필요량·`completed`와의 일치를 검사한다. 한쪽 누락/null, 숫자 문자열, 음수·소수·범위 초과·완료 모순은 응답 전체를 거절하며 부분 파싱으로 기존 출력 모델을 덮어쓰지 않는다.

`FRPGNarrativePresentation::ObjectiveLine`을 기존 대화창 저널에서 사용해 `[ ] 늑대 처치 (1/3)`과 완료 시 `[완료] 늑대 처치 (3/3)`을 표시한다. 제목이 없는 목표는 위치에 따른 기존 대체 이름을 사용하고, 비수량 목표에는 수량을 만들지 않는다. Blueprint 읽기도 같은 서버 진도 모델을 사용한다. 화면 표시를 위해 로컬 횟수를 증가시키거나 목표를 완료하지 않는다. NPC/선택 캐릭터·계정/endpoint 검증과 진행 조회/보고는 기존 Coordinator 경로를 유지한다.

검증 진입점은 `Scripts/Tests/Run-NarrativeCountTests.ps1`이다. 닫힌 사설 작은 UE 모듈에서 실제 코덱과 저널 문자열 함수를 native 검사하고, 게시 Git 전체 Source/Config/플러그인에 본인 입력만 얹어 전체 UHT와 실제 위젯/타입/표시 CPP를 집중 컴파일한다. 원본 DLL을 교체하지 않으며 전체 게임 모듈 링크·렌더링/GPU·UE 사망 사건 전송 검증과 구분한다.

2026-11-09 확인: 게시 `7a35731c`+본인6입력의 작은 모듈 빌드/링크와 native5 Success·검사 오류/경고0·실행0이 통과했다. 위젯의 기존 Pawn 전방 선언 의존성이 NoPCH 집중 컴파일에서 드러나 명시 헤더를 추가했고, 이후 `-IntegrationOnly`의 전체 UHT/실제 CPP4 컴파일0이 통과했다. native에 포함된5입력은 불변이며 최종6입력/러너 SHA를 기록했다. Engine 기동 Error15/Warning4와 최초 위젯 컴파일 실패는 검사 성공과 구분한다. 자기2회 임시181,284,920 bytes 폐기·Saved/NC 잔여/정리 예외0, 근거 `Saved/BuildChecks/NarrativeCount-20261109/CompletionAudit.json`이다. 폐기한 DLL 해시는 당시 근거이며 GPU/물리 입력/실제 처치 전송 결과로 확대하지 않는다.

## 개인 조사/상호작용 서버 사건

`POST /api/dungeon-sessions/{sessionId}/narrative/interactions/{characterId}`는 해당 세션에 묶인 현재 GameServer 자격만 받는다. Player·Administrator·다른 세션 및 종료 후 제한된 서버 자격으로는 제출할 수 없다. 서버는 승인된 개인 현장 행동마다 하나의 사건 ID를 만들고 재전송에는 그대로 사용한다.

```json
{
  "eventId": "00000000-0000-0000-0000-000000000001",
  "targetId": "Scene.Seal01",
  "connectionGeneration": "3",
  "observedAtUtc": "2026-11-08T10:00:00Z"
}
```

새 사건은 `InProgress`·세션/멤버/캐릭터 lease 유효·본인 멤버·현재 접속 세대·입장 확인 세대 일치·연결 끊김 없음 조건을 모두 확인한다. 시각은 서버가 실제 행동 완료 때 관측한 UTC이며 접수 시점의 과거 5분 이내이고 미래가 아니어야 한다. 멤버 입장 이전 사건은 거절한다. 클라이언트의 완료 플래그나 수량을 증거로 받지 않는다. 서버가 거리·대상 존재·허용 행동·완료 조건을 검사한 뒤 보내는 승인 사건을 신뢰하는 경계이며 백엔드가 UE 공간 좌표를 대신 검사하지 않는다.

서버 세션 → 멤버/lease → 캐릭터 잠금 안에서 검사·퀘스트 변경·사건 영수증을 처리한다. 개인 진도의 수락·선행 조건·목표 활성화 시각·정확한 던전/난이도/대상을 대조한다. 사건 이전 스냅샷에서 해당하는 목표만 모아 한 번에 반영하므로 순차 단계를 같은 사건으로 건너뛰지 않는다. 변화가 있으면 리비전을 한 번 올리며, 대상이 없거나 미수락/선행 미충족이면 리비전을 유지한 영수증을 저장한다. 그 무효과 사건을 수락 후 다시 보내도 당시 결과를 돌려준다.

기존 `character_narrative_commands`를 별도 fingerprint 도메인으로 재사용하며 스키마 마이그레이션은 추가하지 않는다. 사건 ID는 캐릭터별 명령 영수증 키다. 같은 ID의 대상/세대/시각/세션 변경 및 대화 명령과의 ID 충돌은 409다. 정확한 재전송은 현재 퀘스트 자격을 다시 적용하지 않고 당시 결과를 반환한다. 현재 세션의 유효한 서버 인증이 있어야 HTTP 재전송이 가능하며, 이미 종료된 세션은 서버 쓰기 권한을 다시 열지 않는다. 저장된 개인 진도는 기존 Player 이야기 조회로 복원한다.

응답은 `eventId`, `characterId`, 문자열 `revision`, `appliedObjectives`(퀘스트/목표 ID)와 `no-store`이며 대화/전체 저널을 서버 사건 응답에 노출하지 않는다. 이 기록은 목표 달성까지만 처리하고 보고·보상·해금은 기존 흐름을 따른다. 전체 파티로 사건을 복제하지 않는다.

서버 저장 단위의 검증 러너는 `Scripts/Tests/Run-NarrativeInteractionTests.ps1 -Postgres`이며 전용 DB·실제 Bearer HTTP·동시 재전송·무효과 영수증·세대/종료/시점·기존 이야기 회귀를 확인한다. 이어지는 UE 연결의 범위와 검증은 아래 절을 따른다.

2026-11-08 검증: 게시 `fbb7d5ab`와 본인13입력의 독립 Release 빌드·기존 이야기 빌드가 모두 경고/오류0이다. Memory/실제 PostgreSQL의 신규/무효과 사건·동시 재전송·여러 개인 의뢰·순차 단계/시점·입장 확인/접속 세대·소유권·카탈로그 변경 후 영수증·손상 저장 거절과 실제 Bearer HTTP 역할/종료 후 제한을 통과했다. 기존 저작·대화·정보·던전 목표·입장·소유권/리비전 회귀도 통과했다. 근거는 `Saved/BuildChecks/NarrativeInteraction-20261108/2f33eb09bce9483a83daf235826eed3c/Result.json`과 `CompletionAudit.json`이다. 초기 시각 fixture의 거절 분류 오류는 수정 후 결과와 구분해 보존했다. 모든 시도 자기 임시100,609,409 bytes/전용 DB를 정리했고 Temp·예외0이다. 폐기한 사본의 해시는 당시 증거로만 사용한다.

## UE 개인 조사 승인과 결과 복구

`ARPGNarrativeInvestigation`은 기존 WorldDynamic 상호작용으로 배치하는 즉시 조사 액터다. 클라이언트는 소유 Controller의 복제 컴포넌트에 액터 참조와 입력 상관 ID만 보낸다. 서버는 현재 Controller/Pawn/PlayerState 관계·생존 체력·최종 입장 확인·개인 인증과 양수 접속 세대·거리/Visibility 시야를 검사한다. 승인 뒤 서버가 사건 GUID와 UTC 관측 시각을 만들고 서버의 논리 대상 ID를 고정한다. 대화의 목표 기록이나 클라이언트 타이머를 승인 근거로 사용하지 않는다.

`UHttpWebManager::CreateServerNarrativeInteractionRequest`는 배정된 세션의 서버 인증만 사용한다. 세션·서버·endpoint·인증 값이 바뀐 콜백은 적용하지 않는다. 접속 세대는 int64 문자열, 응답은256KiB/목표256개 이하로 검사하고 성공 영수증의 사건/캐릭터·문자열 리비전·목표 중복도 대조한다. 사건은 Controller당 하나만 미해결 상태로 유지한다. 응답 유실·5xx·인증 오류·잘못된200은 성공 여부 미확정으로 남기며 최초 요청과 1/2/4초 후 재시도까지 최대4회 자동 전송한다. 이후 같은 액터 재상호작용 또는 `RetryPendingInvestigation()`은 같은 사건/대상/세대/시각/본문을 재전송한다. 다른 대상의 새 사건으로 대체하지 않는다.

승인 이후 재전송은 Pawn의 이동·사망·대상 소멸로 관측 내용을 바꾸지 않는다. 원래의 PlayerState/개인 인증·세션/접속 세대·서버 문맥은 계속 일치해야 한다. 정확한 영수증 조회보다 뒤에서 검사하는409 `interaction_event_out_of_window`만 확정 거절로 처리하여 오래된 미저장 사건을 해제한다. 불확실한 요청의 시각/사건 ID를 새로 만들어 강행하지 않는다. 완료/확정 거절의 최근16개 입력 상관 ID를 보관해 중복 RPC가 새 사건을 만들지 않게 한다. 서버 프로세스/Controller 종료 시 미해결 전송 자체를 영속 복구하지는 않는다. 이미 저장된 진도는 기존 Player 이야기 GET으로 복원하며 저장되지 않은 조사는 새로 수행한다.

완료 RPC는 해당 소유 Controller로만 보낸다. 클라이언트의 계정/endpoint/선택 캐릭터·PlayerState/세션을 확인하고 LocalPlayer의 `URPGNarrativeCoordinator`로 새 진도를 조회한다. 진행 중 대화/자동 던전 목표 명령이 있으면 완료될 때까지 기다린다. 부분 영수증을 전체 진도로 덮어쓰거나 다른 플레이어로 전파하지 않는다. 액터 이름표에 조사 중/완료/재확인 상태를 표시하고 `OnStatus`로 별도 HUD 연결도 지원한다. 목표가 없거나 이미 달성한 경우도 정상 조사 완료이며 자동 보고·보상·해금은 하지 않는다.

`Scripts/Tests/Run-NarrativeInvestigationTests.ps1`은 본인 입력만 고정한 작은 UE 모듈에서 사건 인코딩·정밀도·영수증/무효과·잘못된 응답·동일 본문 재시도·상한/확정 거절을 검사하고, 게시 Git의 전체 Source/Config/플러그인 사본에 본인 입력만 얹어 실제 Controller/액터/RPC/HTTP CPP와 전체 UHT를 컴파일한다. 공유 DLL은 교체하지 않는다. 게임 모듈 전체 링크·실제 네트워크 RPC/맵·Steam/물리 입력·이번 UE에서 HTTP/PG까지 연결한 검증과는 구분한다. 자기 사본/OBJ/DLL/PDB/cache/UserDir는 종료 시 정리하며 작은 결과와 당시 입력 해시만 남긴다.

문맥 변경으로 중단한 입력도 최근 입력 기록에 남겨 같은 입력을 새 사건으로 재승인하지 않는다. 중단은 이전 쓰기의 취소/미저장 확정을 뜻하지 않으며 원래 캐릭터의 저장 조회로 복구한다. Player 인증 갱신 중에도 계정/endpoint/캐릭터가 같으면 완료 알림과 조회 대기를 유지한다.

2026-11-08 검증: 게시 `fa700fe8`+본인10입력의 작은 모듈 빌드/링크와 native5가 통과했다(검사 오류/경고0·실행0). 실제 전체 UHT와 관련 CPP6 집중 컴파일도 성공했고 입력/러너 SHA가 유지됐다. 기동 중 Engine 자동 검사 Error15/Warning4는 검사 구간과 분리했다. 최초 헤더 누락, 사설 대상 조회 준비 전 공유 Build.bat 대기, 숫자 리비전 자동 문자열 변환을 거절하는 검사 실패는 보완 후 결과와 구분해 보존한다. 자기4회 임시195,337,667 bytes를 정리했고 Saved/NI 잔여/정리 예외0이다. 근거 `Saved/BuildChecks/NarrativeInvestigation-20261108/d3f1364faae14dd9abc0b345147df868/Result.json`과 `CompletionAudit.json`; 폐기된 DLL 해시는 당시 증거다. 이 결과를 실제 RPC/맵 또는 UE→HTTP→PG 실행 검증으로 확대하지 않는다.

## 던전 목표 자동 반영과 복구

본인의 이야기 `GET` 응답에는 `dungeonObjectiveCandidates` 배열이 추가된다. 서버가 현재 활성 목표별로 목표 활성화 이후의 유효한 첫 클리어를 찾고 `questId`, `objectiveId`, `dungeonSessionId`만 반환한다. 수령 자격·정확한 던전/난이도·같은 퀘스트에서 이미 사용한 증거를 대조하며 최대 256개다. Memory/PostgreSQL 모두 기존 영속 정산 기록을 조회하므로 현재 활성 세션이 없어도 복구할 수 있다. 조회는 진도·리비전·영수증을 변경하지 않는다. PostgreSQL은 한 번의 쿼리로 후보를 찾으며, 저장 시각의 마이크로초와 진도 JSON의 .NET tick 정밀도 차이 때문에 활성화 전 사건을 후보로 내놓지 않도록 경계를 비교한다. 카탈로그 응답 크기 검사도 이 배열을 포함한다.

`URPGNarrativeCoordinator`는 기본 로컬 플레이어의 기존 `DungeonSessionSubsystem` 이벤트를 소비한다. 캐릭터 선택 후 세션 복원 완료, 새 종료 결과 관측, 이야기 창의 진도 조회가 자동 반영을 시작한다. `Failed`도 재조회하되, 실제 전투 실패에는 성공 정산 증거가 없으므로 목표를 인정하지 않는다. 성공 접수 뒤 재화 전달만 실패한 경우는 기존 성공 증거를 인정한다. 현재 세션을 한 번 더 정산하거나 완료 버튼을 대신 누르지 않는다. 조회한 후보 하나를 문자열 리비전·새 요청 ID와 함께 기존 던전 목표 POST로 보낸 뒤 승인된 진도를 적용하고 다시 읽는다. 자동 보고·보상·이야기 해금은 수행하지 않는다.

대화와 던전 목표는 같은 Coordinator/전송 직렬화를 사용한다. 기존 대화 선택이 미확정이면 목표 반영도 기다린다. 목표 요청의 전송 실패·응답 손상·인증 문맥 변경 등으로 결과가 불명확하면 원래 요청 ID·본문·캐릭터/계정을 유지하며 기존 **진행 결과 다시 확인** 동작으로 재전송한다. 리비전 충돌이 확정되면 미확정 명령을 해제하고 새 진도를 읽는다. 자동 쓰기는 한 번의 시작당 최대 256회로 제한한다. 지도 이동은 LocalPlayer Subsystem의 미확정 요청을 보존하고, 종료 시 타이머/이벤트/HTTP를 해제한다.

애플리케이션 재시작 후에는 서버 진도와 아직 사용하지 않은 정산 증거를 다시 읽는다. 이미 승인된 목표는 후보에서 빠지고, 승인되지 않은 목표만 새 요청으로 반영한다. 요청 ID를 재시작 후까지 저장한다고 가정하지 않으며 로컬 파일을 권위 진도로 사용하지 않는다. 보상·횟수 증가와 달리 이 목표는 기존 목표 완료 상태 및 증거 사용 기록으로 중복 완료를 차단한다. 다른 계정/캐릭터로 미확정 명령을 넘기지 않는다. 기존 UI 표시와 캐릭터 변경 가드는 던전 목표의 미확정 요청에도 적용된다.

자동 반영 검증에는 `Run-NarrativeDungeonObjectiveTests.ps1 -Postgres -TaskId narrative-dungeon-sync-20261106 -ProofName NarrativeDungeonSync-20261106`, native `ProjectRPG.Narrative.DungeonObjectives`, `Run-NarrativeDialogueE2E.ps1 -IncludeDungeonObjectives`를 사용한다. 마지막 검사는 전용 DB의 불변 성공 fixture, 실제 UE 게임 프로세스/플레이어 HTTP/자동 호출, 새 백엔드·게임 프로세스의 복원과 영수증 수를 대조한다. 실제 전투로 던전을 완료했다는 증거와는 구분한다.

2026-11-07 실행·11-08 게시 점검: UE는 게시 `e93a4e1d`의 독립 사본과 본인 명시 소스에서 전체 Editor 타깃·최종 증분 빌드에 성공했다. 최종 `E31C8EAA…822C9FC9` 모듈의 native12가 모두 Success이며, 같은 모듈의 실제 게임 프로세스4개가 기존 대화 완료/복원과 던전 목표 자동 반영/복원을 통과했다. 완료 세션이 현재 활성 목록에 없어도 복구되고, 진도6·영수증6·보고 가능 상태를 유지하며 재시작으로 명령이 추가되지 않았다. 다른 캐릭터 진도0·성장 저장 불변도 확인했다. Backend는 게시 `40a7045a`와 본인 변경의 Release0경고/0오류·Memory/실제 PostgreSQL·인증 HTTP·기존 이야기/입장 회귀·시각 정밀도 경계를 통과했다. 이후 다른 담당이 게시한 최신 전체 UE 타깃을 재빌드했다는 의미는 아니다.

근거는 `Saved/BuildChecks/NarrativeDungeonSync-20261106/CompletionAudit.json`, 최종 실행 `db361d47cf6448b3b15fcc90882d8889`, native `ef07b0dbdc494445b954f16d1ab8160c`, HTTP/PG `9f9dfc81a91e41ad9cb67a0b9e392ff4`, Backend `3d294c9868804df7a8149a925c591d06`이다. 검사 중 JSON 공백 의존 assertion, 수동/자동 조회 경합, NPC 진입의 대화 전용 재시도 제한을 수정했고 초기 실패는 최종 성공과 구분해 보존했다. 본인 소스18개의 검증 후 SHA가 일치한다. 모든 시도의 자기 임시7,313,425,245 bytes·연결102·전용 DB를 정리했고 Temp·정리 예외0이다. 폐기한 바이너리 해시는 당시 증거로만 사용한다.

초기 서버 단위 검증(2026-11-06): `Scripts/Tests/Run-NarrativeDungeonObjectiveTests.ps1 -Postgres`. 게시 Git의 Backend 고정 사본과 본인 명시 소스만 사용해 전체 narrative 회귀·실제 Bearer HTTP·전용 PostgreSQL을 검사하고, 자기 사본/빌드/DB를 종료 후 폐기했다. 작은 결과와 입력 해시는 `Saved/BuildChecks/NarrativeDungeonObjective-20261106`에 남긴다. 이 초기 실행은 UE 자동 반영·게임 소켓 전투를 검사하지 않았다.

2026-11-06: 게시 `ff036751`과 본인 소스12개로 독립 Release 빌드 경고/오류0, Memory/실제 PostgreSQL 목표 처리·동시 요청·영수증·재접속 복원, 실제 Bearer HTTP 소유권/역할 거절, 기존 이야기/입장 회귀와 v1~v10 마이그레이션 검사를 통과했다. 최종 근거는 `5c672617e8264d59a50d100d9c718c76/Result.json`과 `CompletionAudit.json`이다. 실패한 초기 검사 fixture 두 시도를 포함해 자기 임시94,786,877 bytes/DB를 정리했고 Temp·잔여 컨테이너·예외0이다. 해시는 폐기한 당시 입력의 증거다.

매니저는 캐릭터 상태를 보관하지 않는다. 변경 가능한 상태는 캐릭터별 저장소에 있고, 정의는 불변 카탈로그다. `INarrativeRepository`의 Memory/PostgreSQL 구현이 소유권 확인·요청 영수증·리비전 확인·변경·상태/영수증 저장을 같은 잠금 또는 트랜잭션에서 처리한다. 같은 요청 ID와 같은 입력은 최초 결과를 반환한다. 같은 ID의 다른 입력은 충돌이다. 서로 다른 요청이 같은 리비전을 사용하면 하나만 반영된다.

플레이어 클라이언트는 작성된 대화·선택지 ID 또는 서버가 저장한 던전 결과 ID를 참조하는 명령을 제출한다. 목표 수량·완료 플래그·해금 ID·XP·아이템 지급을 제출할 수 없다. 개인 조사/상호작용과 처치 사건은 세션 GameServer 전용 경로를 사용한다. UE 처치는 위의 명시 저작 옵션으로 연결하며 프로젝트 전체 참여 정책·실제 맵/다인 검증과 구분한다. 보고가 승인되면 퀘스트 완료·챕터 완료·이야기 해금이 함께 반영된다. 전투 성장·정산은 기존 소유 경로를 따른다.

## API와 개발 콘텐츠

- `GET /api/characters/{characterId}/narrative`: 본인 캐릭터의 상태와 현재 가능한 대화.
- `POST /api/characters/{characterId}/narrative/choices`: `requestId`, 문자열 `expectedRevision`, `catalogVersion`, `dialogueId`, `choiceId`.
- `POST /api/dungeon-sessions/{sessionId}/narrative/interactions/{characterId}`: 세션 GameServer의 승인된 개인 사건. 입력/응답/재전송은 위 사건 계약을 따른다.
- 응답: `catalogVersion`, `characterId`, `schemaVersion`, 문자열 `revision`, `quests`(ID·정의 버전·상태·확인 목표), `completedChapters`, `storyUnlocks`, `availableDialogues`, `dungeonObjectiveCandidates`(조회 시의 미반영 클리어 후보).

대화·개인 진도 API는 기존 Bearer 인증으로 Player의 본인 캐릭터만 읽기/쓰기를 허용하며 관리자·게임 서버 자격으로 대신 선택하지 않는다. 서버 사건 API는 별도로 해당 세션의 GameServer 자격을 요구한다. GET은 영속 레코드를 생성하지 않는다. 재전송 결과가 현재 진도보다 오래된 리비전일 수 있으므로 표시 소비자는 리비전을 비교해야 한다. 결과 미확정 선택은 원래 요청 ID·입력으로 재시도한다.

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

`Scripts/Tests/Run-NarrativeFrontendTests.ps1`은 현재 DLL을 독립 런타임에 복사해 `ProjectRPG.Narrative.Codec`, `.Coordinator`, `.PlayerTransport`, `.SubsystemOwnership`, `.DialogueWidget`, `.InformationDialogue`과 기존 로비/내비게이션/뒤로가기 검사4를 실행한다. 엔진의 실제 Subsystem 소유권·캐릭터/계정 분리·종료·재시도·늦은 응답과 위젯의 선택 이벤트를 확인한다. HTTP/DB를 사용하는 선택 검사는 아래 별도 러너로 실행한다.

2026-11-04 원본 반영 확인: UHT·핵심 CPP6/신규 생성5/기존 생성 묶음4 컴파일과 정상 게임 모듈 집중 링크를 완료했다. 같은 `5557C6E1…1BB16EB9` DLL의 독립 런타임에서 위 native4 모두 Success·프로세스 종료0·검사 구간 오류0이다. 캐릭터별 캐시, 다중 GameInstance/LocalPlayer의 엔진 소유권과 분리, LocalPlayer 제거 후 GameInstance 캐시 보존, 계정 전환·재시도·오래된 응답을 확인했다. 기존 에셋/환경 경고는 남으며 GPU·실제 백엔드 HTTP 플레이 검증은 아니다.

전체 Editor 타깃 시도는 다른 사설 UBT의 전역 잠금 대기 중 자기 프로세스만 종료했으며, 위 결과를 전체 타깃 성공으로 표시하지 않는다. 기존 공유 출력의 0-byte probe OBJ1·누락 의존 import6을 복구한 뒤 집중 링크했다. 원본 제품 소스와 검증 사본을 구분하며, 사본 취득 직후 공유 입력/출력 예약을 해제했다. 자기 런타임/DLL/UserDir·캐시와 연결11개를 안전하게 정리했고 원본 `Binaries`/`Intermediate`와 유지할 소스/검사는 보존했다.

## 월드 NPC 대화 연결

`ARPGNarrativeNPC`는 기존 `IInteractionInterface`와 WorldDynamic 주변 조회를 사용하는 정적 배치 NPC다. 공통 Controller/HttpWebManager/WidgetBase 원본을 바꾸지 않고 소유 LocalPlayer의 UIManager에 기존 대화창을 연다. NPC별 `DialogueEntryIds`와 서버의 현재 가능한 진입점을 교차해 표시하며, 선택된 정보 분기의 승인된 링크는 계속 탐색한다. [배치·저작·권위 경계](NARRATIVE_CONTENT_AUTHORING.md#월드-npc-대화-진입점).

NPC 대화창은 원래 Pawn/캐릭터/계정/엔드포인트·거리·월드를 확인하며 문맥이 바뀌면 자기 창/자기가 연 로컬 대화만 정리한다. SkillWindowComponent의 공유 `FRPGWindowInputState`에 창을 등록해 커서/포커스·이동/시선 잠금·Esc 닫기를 연결하고, 해제 시 외부 입력 잠금을 보존한다. 일반 Controller의 단독 창도 같은 수명 규칙을 사용한다. 결과 미확정 명령은 보존하고 같은 NPC의 진입점에서 원래 요청을 재전송할 수 있다. 다른 NPC/다른 창의 선택·명령으로 바꾸지 않는다. 기존 로비 창과 전역 서버 대화의 권한·진도·저장 계약을 유지한다. NPC의 로컬 거리 판정을 서버 전투/조사 증명으로 사용하지 않는다.

`Run-NarrativeFrontendTests.ps1`과 `Run-NarrativeDialogueE2E.ps1`은 `RuntimeProjectRoot`로 사설 모듈/설정/플러그인 입력을 선택하고 `IncludeNPC`로 NPC native 및 실제 HTTP의 월드 NPC 인터페이스 경로를 검사한다. 기본 호출은 기존 로비 경로를 유지한다. 기존 맵/에셋과 공유 DLL은 이 기능의 원본 변경 대상에서 제외한다.

2026-11-06 확인: Gitd0c0cc2+본인 UE7파일의 사설 UHT/전체 Editor 타깃 빌드가 성공했다. 이후 검사 전용 가상 뷰포트와 최신 위젯 본문을 집중 컴파일/재링크한 최종 `2915F1AE…72336C2`에서 native11 모두 Success·UE0·검사 발견 뒤 오류0이다. NPC 인터페이스/인물별 진입점·반복 열기/다른 NPC 선택 차단·거리/캐릭터/소유 Pawn/소멸/로그아웃 정리·동일 요청 복구·정보형 링크·공유/일반 Controller 입력·재바인딩/외부 잠금 보존/Esc를 확인했다. 같은 모듈의 실제 게임 프로세스2/HTTP/PG에서 월드 NPC→UMG 수락/확인/보고→리비전4·챕터/해금과 새 백엔드/게임 복원·영수증4·부캐 진도0·성장 불변도 통과했다. 기존 로비 경로 회귀를 함께 유지했다.

검증 중의 UE5.8 TObjectPtr 추론·가상 화면 없는 fixture·SDK UBT 대기·복사 시각으로 남은 이전 객체·NPC/로비 로그 표식 차이를 보완했고, 동일 모듈의 성공한 native는 HTTP 표식 보완 때 재사용했다. SDK 확인은 이미 빌드된 모듈의 검증 자식 프로세스에서만 엔진 지원 `UE_SKIP_UBT_SDK_SETUP=1`로 생략하고 부모 값을 즉시 복원한다. 초기/빌드 경고와 검사 발견 전 콘텐츠 로드 로그는 성공 구간과 구분해 보존했다. 마지막으로 자기 사본/OBJ/PCH/DLL/PDB·UserDir·DB·자식과 연결84개, 총7,024,255,873 bytes를 정리했고 Temp/대형 예외0이다. 근거: `Saved/BuildChecks/NarrativeNPC-20261105/8537e8310e684d54bb068d079d47e380/Result.json`, `35a35fb26b4142aeb9bbafcd3d9bbeff/Result.json`, `6c715f41b783495fa13e7c56d3c9b38e/Result.json`, `CleanupSummary.json`. 폐기한 바이너리의 해시는 당시 근거다. 실제 키/마우스·GPU 외형·Steam/다인 네트워크·최신 다른 담당의 미완료 플러그인 통합과 구분하며, 정적 NPC 배치/메시/진입점 설정은 저작 안내를 따른다.

## NPC의 개인 의뢰 상태 표식

`DialogueManager.GetAvailable`은 기존 자격/선행/선택지 검사를 통과한 노드에만 `questId`/`questAction`을 제공한다. `Accept`는 수락 전, `Progress`는 Active, `Report`는 ReadyToReport다. 정보형 노드는 두 필드가 null이다. 메타데이터는 현재 뷰에서 계산하며 개인 저장·명령 DTO/fingerprint·영수증·보상·훈련/스킵 판정을 바꾸지 않는다. 응답 크기 예산에도 이 필드들을 포함한다.

UE는 두 필드의 부재/null을 구 서버와 호환하고, 새 필드가 있으면 쌍·문자열 종류/값·의뢰 ID와 현재 저널 상태 일치를 검사한다. 불완전하거나 모순된 응답은 뷰 전체를 거절하고 기존 스냅샷을 보존한다. `FRPGNarrativeNPCMarker`가 NPC의 명시 진입점과 허용 노드를 정확히 교차해 보고/수락/진행/일반 대화 우선순위를 계산한다. 개인 `URPGNarrativeNPCMarkerWidget`은 기존 매니저 캐시만 읽으며 표시나 사라짐이 완료·입장·지급 근거가 되지 않는다.

`ARPGNarrativeNPC`는 LocalPlayer별 screen-space 컴포넌트에 OwnerPlayer를 지정한다. 인증·선택 GUID·확정 캐시·Pawn/월드/거리 조건이 없으면 숨기고 같은 조건의 새 진도에서 갱신한다. 조회를 자동 시작하지 않고 공용 `Nameplate`의 이름/포커스 표시와 개인 표식을 분리한다. Controller 교체/로컬 플레이어 제거·NPC 종료에서는 자기 화면 컴포넌트만 제거/재연결한다. 실제 배치 옵션은 [저작 안내](NARRATIVE_CONTENT_AUTHORING.md#npc-개인-의뢰-표식)를 따른다.

`Run-NarrativeMarkerTests.ps1`은 Memory와 실제 bearer HTTP의 상태별 메타데이터·소유권·무변경 및 기존 이야기/스킵 회귀를 확인한다. `Run-NarrativeMarkerUETests.ps1 -HttpWirePath <앞 검사의 HttpWire.json>`은 그 실제 응답을 native codec/표식 판정에 공급하고 개별 UMG 제공자의 분리/숨김/갱신을 검사한다. 전체 UHT/관련 실제 CPP 컴파일과 작은 모듈의 링크/native 실행을 구분하며 실제 NPC 맵 렌더·화면 위치/가림·split-screen/Controller 교체·GPU/물리 입력·Steam/다인 실행 증거는 별도다.

2026-11-13 확인: 게시 `617abb25`+명시 입력의 backend `18e4a0899bbc4cf7b300005c0ba01c93`에서 Release3종0경고/0오류, 실제 인증 HTTP의 상태별 Accept/Progress/Report·완료·소유권/no-store·읽기 전후 진도 불변과 기존 이야기/스킵 회귀가 통과했다. 같은 HTTP 바이트를 사용한 `5ec6e74455e842308bc465d383ca25aa`의 실제 `98589538…59163B4E`에서 native4 Success/검사 오류·경고0/UE0을 확인했다. NPC 호출부의 GetPawn/TObjectPtr 추론을 명시 포인터로 보완한 `2882e8dc4a7b472eb1616c5438f16a20`에서 전체 UHT/실제 CPP5 컴파일0을 확인했으며, native에 실제 복사/컴파일한7입력은 그대로이고 최신 호출부9입력/러너·생산자/소비자 HTTP SHA도 일치한다. 기동Error15/Warning4와 첫 호출부 컴파일 실패를 최종 성공과 구분한다. 자기backend1/UE2회287,908,464 bytes(274.57MiB)/Temp/소유PID를 폐기해 새 예외0이다. 작은 근거 `Saved/BuildChecks/NarrativeMarker-20261113/CompletionAudit.json`에 두 검증 범위를 구분해 유지한다. 사본 DLL 해시는 당시 증거이며 실제 NPC 배치/OwnerPlayer 화면·Controller 전환/거리·GPU·PG·Steam 증거는 별도다.

## 목표의 선행 조건

2026-11-05: `QuestDefinition.ObjectivePrerequisites`로 같은 의뢰 안의 순차/동시 목표 연결을 작성한다. 생략/null/빈 매핑은 기존 동시 진행이며, 지정된 선행은 모두 확인해야 한다. 카탈로그는 잘못된 참조·중복·자기 참조/순환·null 배열을 거절한다. `QuestManager.CanRecordObjective`를 선택지 조회/선택 승인·실제 전이에서 사용하고 Coordinator의 저장 검사에도 선행 일관성을 적용한다. 가능한 선택지가 없는 노드는 응답에서 빠지며, 저널/UE 응답과 저장 스키마는 유지한다. [작성 규칙](NARRATIVE_CONTENT_AUTHORING.md#순차-목표와-동시-목표).

Git3d0903c+본인7 소스의 닫힌 사본에서 빌드 경고/오류0·기존 저작/정보 대화/입장 자격/Memory·인증 HTTP 회귀와 새 목표 검사가 통과했다. 순차/동시·여러 선행 AND와 합류/연쇄, 잘못된 참조/중복/자기/순환/null 거절, 실제 HTTP의 막힌 노드 제외·위조 요청 거절/영수증 미생성·동일 요청 재생/오래된 리비전 거절·전체 목표→보고→챕터 해금/새 Coordinator 복원·비정상 저장 거절·부캐/성장 분리를 확인했다. 외부 JSON v7 실제 호스트 의뢰/검증 CLI·기동 거절 회귀도 통과했다. 검증 뒤 게시된64b082c는 픽업 전용 어댑터/DI 추가이며 본인 입력7/러너 SHA 불변을 대조했다. 이 결과는 UE/GPU·NPC/전투 사건·PG 영속 재검사 결과가 아니다.

`Run-NarrativeAuthoringTests.ps1`의 `TaskId`/`ProofName`/`BackendRoot`로 원본 공통 호스트 편집과 겹치지 않는 Git 사본을 검증했다. 실행/소스 사본 임시17,044,765 bytes·자식 정리·Temp/예외0. 작은 근거: `Saved/BuildChecks/NarrativeObjectiveOrder-20261105/b2f7c9c0066a4eccbb1cfb0adfaac1b0/Result.json`, `Source-69777c871c05462386ddfac90d695d7c/Result.json`, `CleanupSummary.json`. 폐기한 사본의 해시는 당시 근거로만 사용한다. NPC 원본은 전체 Source 고정 해제와 Controller 담당 추가 인계 뒤 별도 예약한다.

## 이야기 해금 입장 검사

`DungeonStoryAdmission`은 `DungeonContent:Entries[].RequiredStoryUnlocks`와 현재 이야기 카탈로그를 고정해 서버의 캐릭터별 저장에서 입장 자격을 읽는다. 모든 해금 AND·미완료/보고 대기/타 캐릭터 불인정이며 기존 Coordinator의 상태/정의 버전·챕터 투영 검사를 공유한다. 설정 ID/중복/개수·미정의 해금 참조는 시작 시 거절한다. 조건 생략은 기존 콘텐츠의 동작을 유지한다. [작성·운영 규칙](NARRATIVE_CONTENT_AUTHORING.md#이야기-해금과-던전-입장).

방 생성과 파티 참가는 기존 소유권 확인 후 각각 본인 이야기 자격과 기존 성장 조건을 검사한다. 입장권 발급과 배정된 서버의 최종 승인은 해당 세션의 던전/멤버에서 다시 검사한다. 잠긴 멤버를 과거 세션/입장권으로 등록해도 최종 승인되지 않는다. 소비/영수증 조회/봉인/재시도는 기존 소유 경로를 유지하며 자격 부족을 미확정 소비의 롤백 근거로 삼지 않는다. 자격 검사 자체는 진도/해금/보상/성장을 변경하지 않는다. 서버의 고정 정의에서 이야기 해금은 정상 명령으로 제거되지 않으며, 기존 세션·멤버십/연결 세대의 원자 판정은 저장소가 계속 담당한다.

`Scripts/Tests/Run-NarrativeAdmissionTests.ps1 -Postgres`는 게시 Git Backend와 본인17파일만 겹친 독립 사본, 소유 라벨의 임시 PostgreSQL, 동적 loopback HTTP에서 검사한다. 빌드/Memory·PG 이야기 검사와 기존 입장권 발급/소비/근거·봉인/연결 관측 HTTP 회귀, 실제 호스트의 생성·참가·출발·입장권·승인을 수행한다. `finally`에서 자기 프로세스/컨테이너/Temp를 정리하고 작은 결과·로그·해시만 남긴다. UE 소스/공유 출력·기존 서비스/DB·NPC 배치는 변경하지 않는다.

2026-11-05 확인: Gitc960b47+본인17파일의 최종 d859609e 사본에서 두 빌드 경고/오류0, Memory·실제 PG의 상태/AND/방장·부캐 분리/모순 저장 거절/과거 입장권·최종 승인·소비 재전송/성장 불변과 기존 이야기 회귀가 통과했다. 실제 호스트 HTTP에서 미해금 생성/참가 거절·거절 후 명단/리비전 불변, 이야기 완료 후에도 실습 미충족 거절, 자격을 가진 파티2명의 출발→입장권→최종 승인, 미정의 해금 참조 기동 실패를 확인했다. PG v1~v6 마이그레이션/복원 및 기존 입장권·소비 근거/봉인·연결 관측5그룹도 통과했다. 초기 정적 전환 컴파일1·검사 호스트의 빈 설정 제공자2 실패를 보완한 최종 입력은 SHA 불변이며 초기 실패의 최소 로그도 보존했다. 자기4회 임시57,483,310 bytes·컨테이너/프로세스 폐기·Temp/예외0. 근거: `Saved/BuildChecks/NarrativeAdmission-20261105/d859609ed1c9487a8079758532e09662/Result.json`, `CleanupSummary.json`. 폐기한 사본의 해시는 당시 근거이며 UE/GPU/실제 NPC 검증 결과가 아니다. NPC 연결은 타 담당의 UE Source 고정 해제 후 별도 예약 대상이다.

## 조건부 정보 대화

2026-11-05: 정보형 정의의 선택 사항 `requiredCompletedQuests`를 서버의 캐릭터별 완료 진도와 연결했다. 조건은 모두 충족해야 하며 Active/ReadyToReport·다른 캐릭터 완료로 대신하지 않는다. 연결된 정보 노드의 조건 집합을 동일하게 제한해 공개 시점과 링크의 일관성을 유지한다. 잘못된 참조·중복·서로 다른 조건의 연결·진행형 조건 오용은 카탈로그 검사에서 거절한다.

조건 미충족 노드는 `availableDialogues`에서 문구/선택지/연결 정보를 포함해 제외한다. 선택을 위조해도 같은 서버 조건으로 거절한다. 공개된 정보 대화의 읽기 전용 동작, 기존 상태 스키마·완료/성장·영수증 계약과 무조건 정보 대화는 유지한다. UE 응답 형식/코드는 바꾸지 않는다. [작성 규칙](NARRATIVE_CONTENT_AUTHORING.md#완료-조건이-있는-안내후일담)을 따른다.

2026-11-05 확인: Git52a9a5b+본인5파일 독립 사본에서 빌드 경고/오류0·저작/기존 Memory·인증 HTTP 회귀와 조건 검사가 통과했다. 실제 HTTP 본문에서 미자격 진입점/설명 전체 제외, 위조 선택 거절과 영수증 미생성, Active/ReadyToReport 비공개·Completed 공개, 다중 조건 AND·연결 조건 불일치 거절·다른 캐릭터 분리·새 Coordinator의 완료 자격 재조회·리비전 불변을 확인했다. 기존 실제 호스트 FILE 의뢰·사전 검사 CLI도 통과했다. UE/GPU/PG 영속 검사를 다시 수행한 결과는 아니다. 자기 임시17,030,946 bytes·사본/자식 정리·Temp/예외0, 근거: `Saved/BuildChecks/NarrativeDialogueGates-20261105/Backend-c223070bf0c84cab85bc9c09d98c653b/Result.json`. 폐기한 입력 사본의 해시는 당시 근거로만 사용한다.

## 정보형 대화 분기

2026-11-05: 정의의 `informational`/`entryPoint`와 선택지의 `nextDialogueId`를 추가했다. 기존 노드는 진행형·직접 진입으로 읽어 기존 JSON/클라이언트 계약을 유지한다. `DialogueEffect.None`은 정보형에서만 허용하고 진행형 연결/목표 효과와 섞지 않는다. 모든 정보형 노드가 진입점에서 도달 가능한지 검사하며, 이전 주제로 돌아가는 정보형 순환은 허용한다.

조회 응답은 노드의 두 플래그와 선택지의 다음 ID를 포함한다. 새 클라이언트는 진입점만 목록에 노출하고, 정보 선택은 Coordinator가 현재 승인된 스냅샷의 정보 노드로 이동하거나 닫는다. HTTP 명령·미확정 요청·서버 리비전/진도/영수증을 만들지 않는다. 다음 정보 노드로 이동한 뒤 창을 닫을 때도 해당 창이 소유한 현재 대화를 닫는다. 일반 질문을 POST로 보낸 구형/위조 클라이언트는 서버에서 거절하며 진도와 영수증은 보존한다. 기존 진행 선택의 서버 승인 경로는 유지한다. [작성 규칙](NARRATIVE_CONTENT_AUTHORING.md#정보형-질문돌아가기닫기)을 따른다.

2026-11-05 확인: UHT·관련 CPP/생성 코드 집중 컴파일·게임 링크와 같은 EB806654…AED1A29의 native10/UE0/검사 오류0이 통과했다. 실제 game 클라이언트2가 서버에서 읽은 주제→추가 설명→돌아가기→닫기를 저장 명령 없이 실행했고, 기존 의뢰의 HTTP/PG 완료·새 UE/백엔드 복원·영수증4/다른 캐릭터 진도0/성장 불변도 유지했다. `Run-NarrativeDialogueE2E.ps1 -IncludeInformation`은 기본 개발 의뢰와 검증용 정보 그래프를 임시 카탈로그로 합쳐 이 경로를 검사한다. 기본 개발/운영 카탈로그를 바꾸지 않는다.

서버는 Git66c7e7d+본인9파일의 닫힌 사본에서 저작·Memory·인증 HTTP/위조 정보 POST 거절·영수증 미생성을 확인했고, 게시된 운영툴 통합 Gitc11feba+동일9파일 사본에서도 빌드0경고/0오류·같은 서버 회귀와 실제 호스트 FILE 의뢰 완료/해금·검증 CLI가 통과했다. UE/PG 전체 흐름은 전자 서버 기준이며 후자를 포함한 전체 프로젝트/GPU/물리 입력/실제 OAuth 검증으로 확대하지 않는다. 최초 C# nullable 분석 오류는 검증 함수의 실패 시 비복귀 계약을 명시해 보완했다. 모든 시도 본인 임시351,636,726 bytes/연결33·사본/DB/자식 정리·Temp/예외0·작은 근거만 유지한다. 근거는 `Saved/BuildChecks/NarrativeInformation-20261105/3f5bef3e8eb64487a8a43d9d03e6e4e2/Result.json`, `42dbcdb6b4e141d8a60b0303f5aacec8/Result.json`, `Combined-2b5b0d3bc11044ddab5005b04784784f/Result.json`이며 폐기한 사본의 해시는 당시 증거다.

## 의뢰 진행 표시

2026-11-05부터 JSON 의뢰 정의의 선택 사항 `title`·`objectiveTexts`를 저널 표시 정보로 사용한다. 표시 문구는 개인 영속 상태나 완료 영수증에 복사하지 않으며, 조회 시 현재 정의와 서버 확정 진도를 연결한다. 기존 상태 스키마·선택 명령·의뢰 완료/성장 효과는 유지한다.

기존 `quests` 응답의 ID·정의 버전·상태·완료 목표를 유지하면서 `title`과 `objectives`(ID·문구·완료 bool)를 추가한다. bool은 저장된 완료 목표에서만 계산한다. 이전 클라이언트는 추가 필드를 무시할 수 있고 새 클라이언트는 표시 필드가 없는 이전 응답도 읽는다. 새 메타데이터가 있으면 목표 중복·완료 목록/상태와의 모순·문자/개수 한도를 검사한다. 작성한 목표 설명도 응답 1MiB 예산에 포함한다.

이야기 창의 스크롤 저널은 의뢰명과 진행 중/보고 가능/완료 상태, 진행 중 의뢰의 목표별 설명·완료 표식을 표시한다. 완료한 의뢰는 제목/상태를 남긴다. 표시 정보가 없으면 번역 가능한 일반 의뢰명·목표 번호/완료 수로 안내하며 내부 ID를 이름처럼 노출하지 않는다. 로컬 클릭으로 목표를 미리 완료 표시하지 않는다. 조회 문맥이 바뀌면 저널도 비운다. 추적 고정·지도 표식·전투 목표 사건은 이 표시 기능과 구분한다.

2026-11-05 저널 확인: 원본 UHT·관련 CPP/생성 코드 집중 컴파일·게임 모듈 링크와 같은 D511937B…5B6A0FB4의 native9/UE0/검사 오류0, 실제 game 클라이언트2의 HTTP/PG 완료·새 UE/백엔드 복원이 통과했다. 실제 응답의 제목/목표 설명이 화면에 반영되는지 확인하며, 영수증4·다른 캐릭터 진도0·성장 불변도 유지했다. 백엔드는 운영툴의 공통 호스트 동시 작업을 피해 Git3d388a4 사본에 본인 narrative8파일만 얹은 독립 입력으로 검증했다. 그 사본에서 빌드 경고/오류0·저작/기존 Memory·인증 HTTP 회귀도 통과했다. 운영툴이 합쳐진 최신 호스트 전체의 검증으로 표시하지 않는다.

본인 임시334,105,976 bytes/연결33·DB/자식 프로세스와 Git 사본을 폐기했고 Temp/대형 예외0이다. 해시는 당시 증거이며 사본 재실행 가능성을 뜻하지 않는다. 근거: `Saved/BuildChecks/NarrativeJournal-20261105/BackendSnapshot.json`, `943a0679756b45e9b8fae7e4e07afb8c/Result.json`, `7980d9e747c64ecb8def6ab8ce6b489a/Result.json`. `Run-NarrativeFrontendTests.ps1`과 `Run-NarrativeDialogueE2E.ps1`의 `TaskId`/`ProofName`으로 검사 예약/출력을 구분하고, 후자는 `BackendRoot`로 독립 서버 입력을 선택할 수 있다. 운영툴 담당의 의존성 인계에 맞춰 Core 프로젝트와 Razor 원본도 선택적으로 복사하도록 보완했으며, 해당 추가 복사 분기는 정적으로 확인했다. GPU·물리 마우스 검증과 전체 Editor 타깃 빌드는 이 결과에 포함하지 않는다.

## 개인 의뢰 추적과 HUD

NPC/로비 이야기 창의 저널에서 진행 중·보고 가능한 의뢰 하나를 골라 **선택한 의뢰 추적**을 누른다. **의뢰 추적 해제**는 표시 고정을 지우며 수락·진행·포기 명령이 아니다. 메인·선택 서브를 모두 고를 수 있고 같은 제목은 순번으로 구분한다. 완료한 의뢰는 후보에서 제외하며 내부 ID를 표시 이름으로 사용하지 않는다. 창 열기·후보 선택만으로 자동 추적하지 않는다.

LocalPlayer의 `URPGNarrativeCoordinator`가 순수 `FRPGNarrativeQuestTracking` 모델과 별도 `URPGNarrativeQuestTrackerWidget` HUD를 소유한다. 저널은 같은 위젯의 조작 모드를 사용한다. 모델은 선택 GUID·계정·endpoint가 일치하고 기존 매니저의 스냅샷이 읽을 수 있을 때만 진행 상태를 반환한다. 추적/해제는 HTTP·저장·성장·입장 상태를 바꾸지 않는다. 추적 고정은 해당 LocalPlayer 세션의 한 의뢰이며 별도 영속 저장이나 캐릭터별 여러 고정 목록을 추가하지 않는다. 다른 캐릭터/계정에서는 이전 고정이 보이지 않으며, 다른 고정으로 교체하지 않았다면 원래 문맥의 진도를 다시 읽은 뒤 복원된다.

HUD는 기존 공용 제목·목표/수량 표시와 보고 가능 안내를 사용한다. 응답 대기 중 마지막 확정 뷰가 계속 유효하면 그 값만 표시하며, 인증/선택/읽기 문맥이 유효하지 않으면 숨긴다. 조회 실패·일시적인 읽기 불가는 고정을 삭제하는 근거가 아니다. 읽을 수 있는 새 확정 스냅샷에서 보고 완료 또는 의뢰 제거/스킵을 확인했을 때만 고정을 해제하며 다음 의뢰를 자동 선택하지 않는다. 이야기를 닫아도 HUD는 유지되고, LocalPlayer의 짧은 ticker가 Controller 교체/뷰포트 제거 후 현재 소유자로 다시 연결한다. HUD는 hit-test-invisible/포커스 없음이며 입력 잠금·공용 HUD/Controller·기존 에셋을 교체하지 않는다. LocalPlayer 종료 때 자기 ticker·HUD·고정만 정리한다.

`Scripts/Tests/Run-NarrativeTrackingUETests.ps1`은 별도 모듈에서 추적 선택/문맥·확정 진도와 실제 UMG 버튼·닫기/재열기·HUD 표시를 검사하고 전체 UHT/관련 실제 CPP를 집중 컴파일한다. 모형 provider는 서버가 이미 승인한 캐시 입력을 대신하며 실제 HTTP·레벨 이동·GPU/물리 입력·Steam 검증은 별도다.

2026-11-13 확인: 게시 `9929a505`+고정13입력의 `d76c8188bb484a59a53d18c752d9f704`에서 작은 모듈 링크0·실제 `03C08B31…00CB6DDD`의 native4 Success/검사 오류·경고0/UE0, 전체 UHT/실제 CPP7 컴파일0이 통과했다. 추적/해제와 exact ID, 메인·서브/동일 제목 후보, 이전 캐릭터의 늦은 조작·계정/endpoint·인증/조회 실패, 보고 완료·스킵의 고정 제거, 닫기/재열기·목표 수량·보고 안내·HUD 비입력 모드를 확인했다. 초기 지역/검사 상수 이름 가림 컴파일 오류는 수정본과 구분하고 기동 Error15/Warning4도 검사 구간과 별도다. 본인 입력/러너 SHA는 동일하며 자기2회222,476,083 bytes(212.17MiB)/Temp·소유 PID를 폐기해 새 예외0이다. 작은 근거 `Saved/BuildChecks/NarrativeTracking-20261113/CompletionAudit.json`을 유지한다. 사본 DLL 해시는 당시 증거이며 전체 게임 링크·실제 Controller 전환/레벨 이동·GPU 외형·물리 입력·HTTP/Steam 검증을 뜻하지 않는다.

## 이번 접속에서 읽은 대사 기록

NPC/로비 이야기 창의 **읽은 대사 보기 (이번 접속)**는 현재 캐릭터로 표시한 화자·대사만 다시 읽는 로컬 텍스트 기록이다. 접기/다시 열기와 지우기는 선택지·분기·수락/보고·보상·입장 권한을 실행하지 않는다. 선택 결과나 서버 영수증의 재생 화면으로 취급하지 않으며 컷신·음성 다시보기나 서버 영속 대본 보관을 추가하지 않는다.

LocalPlayer의 `URPGNarrativeCoordinator`가 `FRPGNarrativeTranscript`를 소유한다. 이야기 창은 뷰포트에 보이고 현재 캐릭터/NPC 문맥·확정 뷰가 유효하며 화면의 화자/본문이 실제 활성 대사와 일치하는 tick에서만 기록한다. 선택 가능한 진입점 목록·미래 대사·열기 실패만으로 수집하지 않는다. 창의 표시 상태와 연속 같은 대사 중복을 구분하므로 일반 재렌더·기록 지우기가 현재 대사를 매 틱 다시 추가하지 않는다. 다른 활성 대사로 이동하거나 창을 새로 열면 표시 검사를 다시 한다.

기록은 선택 캐릭터 GUID·인증 계정·endpoint로 구분하며 현재 문맥의 줄만 읽고 지울 수 있다. 인증/선택 문맥이 무효하면 패널을 접고 이전 내용을 숨긴다. 같은 문맥이 복원되면 명시적으로 다시 열 수 있으며, 활성 대사가 닫히거나 진도 조회가 실패해도 이미 읽은 기록을 퀘스트 진도로 대신하거나 초기화하지 않는다. 다른 캐릭터의 기록을 지우지 않고 LocalPlayer 종료 때 전체 기록을 버린다. 접속 중 전체 보관량은 최대64건·화자/본문 합계32,768 UTF-16 코드 단위이며 오래된 항목부터 제거한다. 화자256/본문4096/노드128/계정128/endpoint2048 한도를 확인하고 내부 노드 ID·계정·서버 주소는 기록 패널에 표시하지 않는다.

`Run-NarrativeTranscriptUETests.ps1`은 순수 기록 모델의 활성 대사/문맥·용량·삭제와 실제 UMG 버튼/접기/재열기/격리를 확인하고 전체 프로젝트 UHT/관련 실제 CPP를 집중 컴파일한다. native 검사는 활성 대사 provider를 주입하며 실제 게임 뷰포트의 수집 시점·NPC/서버 HTTP·레벨 이동·GPU/물리 입력·Steam 실행은 별도다.

2026-11-13 확인: 게시 `24e0a801`+고정 입력의 `86e89fa51ff44a748133c6a6a0f45089`에서 실제 `EDC3DA2A…9C8CDDFC` native4 모두 Success/검사 오류·경고0/UE0을 확인했다. 실행기의 예상 검사 이름 접두사에 남은 이전 기능명을 고쳤으며, 실행 명령·native6입력은 그대로이고 옛/새 실행기 SHA와 실제 검사 이름을 따로 확인했다. `f4501063401e41a99adb0304d9d0c181`은 수정된 실행기의 최신10입력 전체 UHT/실제 CPP5 컴파일0이다. 활성 노드 외 수집 거절·연속 중복·문맥별 읽기/삭제·인증 숨김·64건/문자 예산·실제 UMG 열기/접기/재열기/삭제가 통과했다. 기동Error15/Warning4와 첫 실행기의 이름 판정 실패는 실제 native 성공과 구분한다. 자기2회169,374,067 bytes(161.53MiB)/Temp/소유PID를 폐기해 새 예외0이며 작은 근거 `Saved/BuildChecks/NarrativeTranscript-20261113/CompletionAudit.json`과 `NativeRunnerCorrection.json`에 범위/해시를 구분해 유지한다. DLL은 폐기되어 해시는 당시 증거이며 실제 뷰포트 수집/HTTP/GPU/물리 입력/Steam 실행을 뜻하지 않는다.

## 로비의 이야기 창

캐릭터로 로비에 접속한 뒤 **인물과 이야기**를 누르면 해당 LocalPlayer의 `URPGNarrativeDialogueWidget`을 연다. 화면은 서버의 현재 가능한 대화, 인물·본문·선택지, 완료한 의뢰 수와 진행 저널을 표시한다. 진행할 대화가 없으면 빈 상태를 안내한다. 이 화면은 네이티브 UMG로 구성하며 기존 대화 Blueprint나 NPC 에셋을 교체하지 않는다.

대화 열기·닫기는 로컬 표시만 바꾼다. 수락·확인·보고는 표시된 선택지를 눌렀을 때만 Coordinator를 통해 서버로 전송한다. 응답 전에는 의뢰 진도나 해금을 먼저 바꾸지 않는다. 결과 미확정 시 **진행 결과 다시 확인**이 원래 명령을 재전송하며, 창을 닫았다 다시 열어도 미확정 명령을 보존한다. 접속 오류는 사용자용 문구로 안내하고 내부 ID·리비전·오류 코드는 화면에 노출하지 않는다.

인증·본인 캐릭터·로비/세션 상태를 다시 검사한 뒤 창을 열며, 캐릭터/접속 문맥이 바뀌면 이전 창을 닫거나 내용을 숨긴다. 조회/미확정 선택 중에는 캐릭터 변경과 새 던전 이동을 차단한다. 새 창에서 기존 미확정 선택을 복구할 수 있다. 전투 성장, 입장 조건, 부캐 이야기 건너뛰기 정책을 이 UI가 대신 판정하지 않는다.

이야기 창 자체도 `DungeonSessionSubsystem`의 현재 선택 GUID를 열기·각 입력·표시 시점에 대조한다. 로비가 이전 창을 정리하기 전 같은 프레임에 발생한 선택·재조회·재시도도 다른 캐릭터의 요청을 전송하지 않는다. 늦게 온 이전 캐릭터의 조회 응답은 그 캐릭터의 캐시에만 남으며 현재 창의 대사·선택지·저널에는 표시하지 않는다. 이 차단은 이미 전송한 명령을 취소하거나 미확정 명령을 삭제하지 않는다. 원래 캐릭터로 돌아오면 같은 명령 ID와 본문으로 결과를 복구한다. 창 열기가 전역 캐릭터 선택을 대신 변경하지 않으며 GUID 표기의 대소문자는 같은 캐릭터로 취급한다.

2026-11-12 확인: 게시 `c9552ea6`+본인2소스의 독립 Editor 전체949단계/UHT/링크와 실제 `06867D20…1E3CB36` 모듈의 native12가 통과했다. 같은 최종 입력의 개별 CPP 컴파일, 실제 UMG의 전환 전 버튼 콜백·늦은 조회·숨김·미확정 보존·동일 본문 재시도와 기존 정보/NPC 대화·로비 회귀를 확인했다. 검사 구간 오류0이며 기동 오류15·기존 에셋 경고280은 별도 기록했다. 초기 검사 코드의 GUID 열거값/명시 include 오류와 첫 전체 빌드 후 로컬 실행기의 결과 폴더 인자 실패는 최종 성공과 구분한다. 자기4회 빌드/런타임·중복 로그13,732,986,629 bytes(12.79GiB)/링크39를 폐기했고 잔여 임시 경로·프로세스·예외0이다. 작은 근거 `Saved/BuildChecks/NarrativeSelection-20261112/CompletionAudit.json`을 유지한다. 이 검사는 headless UMG·주입 전송 기준이며 HTTP/Steam/물리 입력/GPU 검증을 포함하지 않는다. 위 모듈은 정리되어 해시는 당시 증거다.

`Scripts/Tests/Run-NarrativeDialogueE2E.ps1`은 캐시된 `postgres:17-alpine`과 Docker를 사용한다. 임시 DB에 명시적으로 마이그레이션하고, 독립 백엔드의 개발 인증으로 새 캐릭터 둘을 만든다. UE에서는 실제 플레이어 인증·로비 메뉴·UMG 버튼 이벤트·HTTP를 거쳐 개발 의뢰를 수락/확인/보고한다. 이어 같은 임시 DB를 유지한 채 백엔드와 UE를 새 프로세스로 시작해 복원한다. 완료/해금, 명령 영수증4건, 다른 캐릭터의 초기 진도, 성장 저장 불변을 대조한다. 개발 콘텐츠는 이 전용 백엔드에서만 켜며 원본 설정을 수정하지 않는다. 토큰·DB 비밀번호는 프로세스 메모리/자식 환경에서만 사용하고 결과 파일에 남기지 않는다.

두 러너는 각자의 `Saved/BuildChecks/NarrativeDialogue-20261104/<GUID>/Temp`와 자신이 시작한 프로세스/라벨 컨테이너만 정리한다. Content/Resources 연결은 원본을 따라가지 않고 해제하며 작은 로그·해시·결과만 보존한다. HTTP 검사는 독립 `-game -nullrhi` 클라이언트의 엔진 소유 GameInstance·LocalPlayer·Controller와 실제 화면에 추가된 UMG의 클릭 델리게이트를 사용한다. 물리 마우스·GPU 외형·실제 NPC 배치·다인 전투 사건 검증과는 구분한다.

2026-11-04 확인: UHT·로비 헤더 소비 코드를 포함한 집중 컴파일/게임 모듈 링크를 완료했다. `0DBE6945…552A6BC2`의 native9가 모두 Success·프로세스 종료0·검사 구간 오류0이다. 이후 제품6소스의 SHA는 그대로 유지하고 HTTP 검사 어댑터만 정상 게임 월드를 사용하도록 바꿨다. 최종 `4709F09A…4AAB4EC7`의 서로 다른 게임 클라이언트 두 개가 실제 HTTP 완료/새 프로세스 복원을 각각 통과했다. PostgreSQL 진도4·완료/해금·영수증4건·다른 캐릭터 진도0·성장 저장 불변을 확인했다. 로컬 근거는 `Saved/BuildChecks/NarrativeDialogue-20261104/CompletionResult.json`과 `d1ab4fb0e3504cd880c2b96172a57ee4/Result.json`이다.

초기 미보유 Slate 수명/검사 소유자 오류와 최소 RPC 월드의 비동기 컨트롤러 소멸, 냉시작 제한시간 초과는 실패 기록으로 남겼다. 최종 HTTP 검사는 직접 만든 월드·컨트롤러를 사용하지 않는다. 전체 Editor 타깃 빌드나 GPU 외형 확인을 이 결과로 대신하지 않는다. 모든 시도에서 본인 임시 2,019,439,774 bytes와 연결132개·자기 DB 컨테이너/프로세스를 정리했으며 Temp/대형 예외0이다. 원본 Binaries/Intermediate·다른 담당 변경은 보존했다. 폐기한 검사 사본의 해시는 당시 증거로만 사용한다.

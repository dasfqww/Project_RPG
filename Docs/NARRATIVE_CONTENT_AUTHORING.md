# 시나리오·퀘스트·대화 JSON 저작

현재 서버 매니저와 로비 이야기 창이 사용하는 정의를 외부 JSON 파일로 작성한다. 파일을 바꾼 뒤 서버를 재시작하면 적용되며, 외부 파일 때문에 게임/서버 코드를 다시 컴파일할 필요는 없다. 실행 중인 카탈로그는 불변이며 자동 감시·부분 교체는 하지 않는다.

이 형식은 개인 대화로 수락 → 작성된 목표를 각각 확인 → 보고하는 의뢰, 완료 의뢰에 따른 챕터/이야기 해금, 진도를 바꾸지 않는 정보형 대화 분기를 지원한다. 전투·조사·지역 사건, 보상 지급, 부캐 스킵은 아직 이 형식의 효과가 아니다. 아래 SAMPLE은 기술 예시이며 실제 세계관·출시 퀘스트 채택을 뜻하지 않는다.

## 파일 선택과 사전 검사

서버 설정 예시:

```json
{
  "Narrative": {
    "CatalogPath": "Content/story.json",
    "EnableDevelopmentContent": false
  }
}
```

상대 경로는 ASP.NET의 `ContentRootPath` 기준이다. 배포 시 파일을 명시한 위치에 함께 설치한다. 배포 작업 디렉터리가 달라질 수 있으면 절대 경로를 사용한다. 운영 파일을 저장소의 개발 예제로 자동 대체하지 않는다.

DB 연결·서비스 시작 없이 검사하려면 다음 명령을 사용한다.

```powershell
dotnet run --project Backend/ProjectRpg.Backend -- --validate-narrative-catalog --Narrative:CatalogPath="C:/RPGContent/story.json"
```

성공하면 카탈로그 버전과 의뢰/챕터/대화 개수를 출력하고 종료0, 실패하면 오류 코드와 가능한 JSON 경로를 반환하며 비정상 종료한다. 검증 모드와 `--migrate-schema`는 동시에 지정할 수 없다. 일반 서버 시작과 마이그레이션 모드에서도 선택한 콘텐츠를 먼저 검증한다.

파일 선택도 개발 플래그도 없으면 기존처럼 빈 카탈로그를 사용한다. 개발 예제는 `EnableDevelopmentContent=true`일 때 Development/Testing에서만 제공한다. 개발 플래그와 파일을 동시에 지정하면 모호한 설정으로 거절한다. `DEV.` 정의/이야기 해금 ID는 개발 예제용 예약 접두사이며 운영 환경의 외부 파일에서도 거절한다. 기존 개발 의뢰의 ID·대사·진도·성장 보상0 계약은 유지한다.

## 최소 작성 예시

```json
{
  "schemaVersion": 1,
  "catalogVersion": 1,
  "quests": [
    {
      "id": "SAMPLE.Intro", "version": 1, "prerequisites": [], "objectives": ["briefing"],
      "title": "안내 확인",
      "objectiveTexts": { "briefing": "연락책에게 안내 듣기" }
    }
  ],
  "chapters": [
    { "id": "SAMPLE.Prologue", "requiredQuests": ["SAMPLE.Intro"], "storyUnlocks": ["SAMPLE.Next"] }
  ],
  "dialogues": [
    {
      "id": "SAMPLE.Offer", "speaker": "연락책", "text": "안내를 확인하시겠습니까?",
      "questId": "SAMPLE.Intro", "requiredStatus": null,
      "choices": [{ "id": "accept", "text": "확인하겠습니다.", "effect": "AcceptQuest" }]
    },
    {
      "id": "SAMPLE.Briefing", "speaker": "연락책", "text": "이 대화는 시스템 연결을 확인하는 예시입니다.",
      "questId": "SAMPLE.Intro", "requiredStatus": "Active",
      "choices": [{ "id": "confirm", "text": "확인했습니다.", "effect": "RecordObjective", "objectiveId": "briefing" }]
    },
    {
      "id": "SAMPLE.Report", "speaker": "연락책", "text": "확인을 마쳤군요.",
      "questId": "SAMPLE.Intro", "requiredStatus": "ReadyToReport",
      "choices": [{ "id": "report", "text": "보고를 마칩니다.", "effect": "ReportQuest" }]
    }
  ]
}
```

## 형식과 연결 규칙

| 요소 | 규칙 |
|---|---|
| 최상위 | `schemaVersion=1`, 양의 정수 `catalogVersion`, 배열 `quests`/`chapters`/`dialogues` 필수 |
| 의뢰 | 양의 정수 `version`, `prerequisites` 배열, 비어 있지 않은 `objectives` 배열. 모든 선행 의뢰를 완료해야 수락 가능 |
| 의뢰 표시 | 선택 사항 `title`과 `objectiveTexts`(목표 ID → 설명). 기존 파일은 생략 가능. 지정한 설명의 키는 해당 의뢰의 목표여야 함 |
| 챕터 | 비어 있지 않은 `requiredQuests`, `storyUnlocks` 배열. 필수 의뢰 전부 완료 시 계산 |
| 대화 | ID·화자·본문·대상 의뢰·`requiredStatus`·선택지 필수. 새 의뢰 제안의 상태도 생략하지 않고 명시적 `null` 사용 |
| 수락 | `effect=AcceptQuest`, 대화의 `requiredStatus=null`, `objectiveId` 없음 |
| 목표 확인 | `effect=RecordObjective`, 대화의 `requiredStatus=Active`, 해당 의뢰에 작성한 `objectiveId` 필수 |
| 보고 | `effect=ReportQuest`, 대화의 `requiredStatus=ReadyToReport`, `objectiveId` 없음 |
| 정보형 대화 | 노드의 `informational=true`, `questId=null`, `requiredStatus=null`. 선택지는 `effect=None`, `objectiveId` 없음 |

모든 목표는 한 번씩 확인하는 AND 목표다. 완료한 목표의 선택지는 다시 노출하지 않으며, 전부 확인하면 보고 가능 상태가 된다. 의뢰마다 수락·보고와 모든 목표를 기록할 수 있는 대화 경로가 있어야 한다. 선행 참조 누락·자기 참조/순환·정의/선택지/목표 중복을 거절한다. 상태와 효과의 조합도 검사한다.

ID는 영문 ASCII 문자·숫자·마침표·밑줄·하이픈만 사용하며 최대128자다. 화자·본문·선택지 문장은 ID와 분리된 UTF-8 텍스트다. 문장의 변경이 영속 ID를 바꾸는 이유가 되지 않는다.

| 현재 UE 계약에 맞춘 상한 | 값 |
|---|---|
| UTF-8 파일 | 1MiB, BOM 허용, JSON 깊이16 |
| 의뢰/챕터/대화 정의 | 각각256개 |
| 의뢰별 목표·선행 / 전체 고유 이야기 해금 | 각각256개 |
| 대화별 선택지 | 64개 |
| 화자 / 본문 / 선택지 문장 | 각각256 / 4096 / 1024 UTF-16 코드 단위 |
| 의뢰명 / 목표 설명 | 각각256 / 1024 UTF-16 코드 단위. 지정한 값은 공백만으로 작성할 수 없음 |

모든 노드·선택지·진도 ID를 포함한 응답 크기를 과대 추정해, JSON 이스케이프 후에도 UE의 1MiB 응답 한도를 넘지 않는지 검사한다. 이 상한은 이번 첫 클라이언트 계약의 한도이며 대형 카탈로그 분할·페이지 조회를 구현했다는 뜻은 아니다.

누락된 필수 속성, 알 수 없는 속성·상태·효과, 숫자로 쓴 enum, JSON 속성 중복, 잘못된 참조는 거절한다. 예를 들어 `xp`, `itemReward`, `GrantXp`를 작성해도 보상 효과가 추가되지 않는다. 세 진행 효과와 정보형 대화의 `None` 외의 효과는 별도 서버 계약과 구현이 필요하다.

## 정보형 질문·돌아가기·닫기

정보형 노드에는 `informational=true`를 지정하고, `questId`와 `requiredStatus`는 명시적 `null`로 작성한다. 선택지는 `effect=None`을 사용한다. `nextDialogueId`가 있으면 해당 정보형 노드로 이동하고, 없거나 `null`이면 대화를 닫는다. `objectiveId`와 진행 효과를 섞을 수 없다. 기존 진행형 노드는 `informational`을 생략하거나 false로 두며 다음 노드 링크를 가질 수 없다.

`entryPoint`는 기본 true다. 주제 목록에 직접 보여줄 시작 노드만 true로 두고, 이어지는 설명 노드는 false로 작성한다. 새 클라이언트는 시작 노드만 목록에 표시하며 선택지 링크로 설명 노드에 들어간다. 돌아가기는 이전 주제 노드를 가리키는 링크로 작성한다. 정보형 순환은 허용하지만 모든 정보형 노드는 하나 이상의 시작점에서 도달 가능해야 한다. 없는 대상·진행형 대상 링크·고립된 설명 노드는 검사에서 거절한다. 진행형 노드를 숨기는 `entryPoint=false`도 거절한다.

완전한 3노드 작성 예시는 [검증용 정보 대화 파일](../Backend/ProjectRpg.Backend.NarrativeTests/Fixtures/information.json)에 있다. 새 캐릭터용 정보 전용 카탈로그로 읽거나, 운영 중인 정의를 보존한 채 해당 `dialogues` 항목을 기존 카탈로그에 합친다. 기존 캐릭터의 의뢰 정의를 지우면서 이 예제만 배포하면 기존 저장 호환 규칙에 의해 거절될 수 있다.

서버 조회는 소유 캐릭터의 대화 그래프를 반환하고, 질문·뒤로가기·닫기는 그 스냅샷 안에서 LocalPlayer의 현재 대화만 바꾼다. 쓰기 요청/명령 ID·영수증을 만들지 않으며 퀘스트 목표·리비전·해금·보상·성장을 변경하지 않는다. 정보 선택을 POST로 위조하면 서버도 `informational_dialogue_not_mutable`로 거절한다. 이 방식은 일반 질문 재열람용이며 영구 분기·관계도 저장·NPC 거리/월드 상호작용 판정을 대신하지 않는다.

## 완료 조건이 있는 안내·후일담

정보형 대화에 선택 사항 `requiredCompletedQuests`를 작성하면, 소유 캐릭터가 지정한 의뢰를 전부 완료한 뒤에만 그 노드가 조회 응답에 포함된다. 생략하거나 빈 배열이면 기존처럼 추가 완료 조건이 없다. 수락·진행 중·보고 가능은 완료로 인정하지 않으며 다른 캐릭터의 완료 이력도 대신 사용하지 않는다.

```json
{ "requiredCompletedQuests": ["SAMPLE.Intro"] }
```

이 필드를 해당 정보 대화의 각 노드에 추가한다. 연결된 노드들은 순서와 무관하게 동일한 조건 집합을 가져야 한다. 조건이 있는 시작점에서 조건 없는 설명으로 연결하는 경우도 거절한다. 따라서 대화 전체가 함께 공개되고, 공개된 링크가 숨겨진 노드로 향하지 않는다. 별도 대화는 서로 다른 조건을 가질 수 있다. 의뢰 참조 누락·중복·잘못된 ID·256개 초과를 검사하며, 진행형 대화에 이 조건을 붙일 수 없다. 의뢰 수락의 선행 조건은 기존 `prerequisites`를 사용한다.

조건 미충족 노드는 진입점/설명/선택지/문구 전체가 응답에서 제외된다. ID를 알고 POST해도 서버가 같은 자격을 재검사해 거절하며 영수증을 만들지 않는다. 공개된 정보 선택도 기존처럼 쓰기로 처리하지 않는다. 이 조건은 표시할 대화의 자격이며 던전 입장 해금이나 보상·성장 지급을 추가하지 않는다. 조건을 바꾸는 배포는 기존 카탈로그 버전/저장 호환 규칙과 클라이언트 재조회를 함께 고려한다.

## 버전과 기존 저장

`schemaVersion`은 파일 형식 버전, `catalogVersion`은 선택 요청이 바라보는 콘텐츠 묶음 버전, 의뢰의 `version`은 해당 저장 진도의 정의 버전이다. 서버는 선택 요청의 카탈로그 버전과 저장된 의뢰 정의 버전을 대조한다. 기존 ID 삭제·버전 변경·챕터 필수 조건/해금 변경이 기존 저장을 자동 변환하지 않는다. 저장과 맞지 않으면 기존 런타임 계약에 따라 충돌을 반환하며 새 진도로 초기화하지 않는다.

운영 교체 전에는 실제 저장과의 호환 또는 별도 마이그레이션을 검토하고, 사전 검사 후 같은 파일을 모든 해당 서버에 배포한다. 진행 데이터·완료 영수증·성장 상태는 카탈로그 파일에 작성하지 않는다.

## 검증 진입점

`Scripts/Tests/Run-NarrativeAuthoringTests.ps1`은 독립 입력/출력에서 빌드한 뒤 형식·참조·순환·누락 경로·문자/개수/응답 크기·환경 설정과 기존 Memory/인증 HTTP 회귀를 실행한다. 별도 파일의 ID와 카탈로그 버전을 바꿔 실제 서버를 시작하고, 해당 정의로 수락→목표 확인→보고·챕터/해금을 확인한다. 검증 전용 CLI가 DB 접근보다 먼저 종료하고, 잘못되거나 없는 파일이 시작 단계에서 거절되는지도 확인한다. 기존 서비스/DB/UE는 사용하지 않는다. 자기 `Saved/BuildChecks/NarrativeAuthoring-20261105/<GUID>/Temp`와 자식 프로세스는 성공·실패 시 정리한다.

2026-11-05 확인: 독립 빌드 경고/오류0, 위 저작 검사와 기존 Memory/인증 HTTP 회귀, 외부 파일 카탈로그7·FILE 의뢰의 실제 호스트/HTTP 완료·챕터/해금, DB 접근 전 사전 검사 종료, 잘못된 파일/누락 파일/시작 모드 충돌 거절이 통과했다. 최종 입력 SHA 불변이다. 첫 검증 호스트는 작업 디렉터리에 기본 설정이 없어 기동에 실패했으며, 전용 입력의 서버 프로젝트를 작업 디렉터리로 고쳐 확인했다. 모든 시도 자기 임시31,468,029 bytes와 자식 프로세스를 정리했고 Temp/예외0이다. 로컬 근거: `Saved/BuildChecks/NarrativeAuthoring-20261105/47c5d203bda441628cc9accb48a9566d/Result.json`. 이 작업에서 UE/GPU/DB 영속 검사를 다시 수행하지 않았다.

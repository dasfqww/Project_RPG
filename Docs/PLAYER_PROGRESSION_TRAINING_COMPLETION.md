# 훈련 파티의 완료 집계

## 입문1과의 완료 gate 재사용 — 2026-11-02

`ARPGTrainingGameMode`는 훈련 ID·4개 목표·기존 delegate를 기본값으로 유지한다.
입문1의 `ARPGIntroApproachGameMode`가 콘텐츠 ID/목표 조회·상태 읽기/구독·해제의 좁은
protected hook만 override하도록 확장했다. 명부·신원/세대·현재 수명·확정 제외·완료 후
신선한 멤버십 GET·단일 부모 제출 알고리즘과 영속 정산은 복제하지 않았다.
기존 훈련 완료 로그도 유지한다. [입문1 연결/검사 경계](PLAYER_PROGRESSION_INTRO1.md#기존-파티-완료-gate의-입문1-적용--2026-11-02)를 따른다.

원본 적용/정상9작업·종료0·입력1,381 불변과 같은 모듈 native3 Success/사례 오류·경고0을
확인했다. 훈련16상태/입문1의8상태 구분·타입별 delegate/미인증 완료 거절 범위이며
기동Error15/발견 이후0을 구분한다. 이전 아래 훈련 실행 증거는 당시 모듈의
증거이며 새 adapter의 실제 훈련/입문1 HTTP·멀티플레이 정산 성공으로 승격하지 않는다.

작성일: 2026-10-30  
상태: **GameMode/BP 연결·정상 모듈 반영 후 실제 1인 조작 완료 → 서버 정산 → XP 저장 통과.** 2~4인 명부/전원 완료·재접속/포기와 운영 Steam/DB 검증은 후속이다.

[개인 조작 목표와 훈련 콘텐츠](PLAYER_PROGRESSION_TRAINING.md)의 다음 단계다. 새 [ARPGTrainingGameMode](../Source/Project_RPG/Public/GameMode/RPGTrainingGameMode.h)가 개인 목표를 읽고, 기존 서버 멤버십과 대조한 뒤 기존 `ReportConfiguredDungeonClear()` 경로를 호출한다. 개인 목표의 차지 판정은 다른 담당의 작업을 그대로 사용한다.

## 완료 조건

| 입력 | 집계 규칙 |
|---|---|
| 콘텐츠 | 실제 Dedicated Server·현재 AuthGameMode·인증된 `Progression.Training`/Normal·보상 활성 설정만 허용 |
| 최초 명부 | 공통 GameMode의 시작 요청 전에 기존 `OnDungeonSessionUpdated`를 구독해 성공한 시작/진행 응답의 참가자를 기억. 아직 입장하지 않은 사람도 포함 |
| 개인 완료 | 서버 목표 delegate에서 새로 완성된 네 목표를 관찰해야 함. 이미 완료된 컴포넌트를 뒤늦게 발견해 완료 이벤트를 만들지 않음 |
| 현재 수명 | 같은 Pawn/Controller/PlayerState·세션/캐릭터/Steam·연결 세대·영속 스킬 복원 세대·살아 있는 ASC/전투 수명이어야 함 |
| 현재 명부 | 기존 검증된 멤버십 캐시가 같은 서버/세션이며 신선해야 함. 현재 대상은 1~4명이며 각자 명확한 현재 관찰 기록이 필요 |
| 미입장·단절 | Pawn/목표 관찰이 없거나 연결 세대/접속 메타데이터가 불명확하면 대기. 재접속 유예 중인 대상도 완료를 막음 |
| 제외 | 기억한 참가자의 단순 누락으로 대상을 줄이지 않음. 같은 서버/세션/캐릭터/Steam의 유효한 확정 포기 또는 단절 만료 제외 영수증만 허용 |
| 마지막 확인 | 현재 대상 전원의 완료 이벤트 **이후에 시작된** 성공한 기존 멤버십 GET 필요. 진행 중/종료 상태와 요청 시계도 검사 |

최초 세션 응답은 SteamID를 제공하지 않으므로 참가자의 캐릭터 ID만 먼저 보존한다. SteamID는 이후 인증된 멤버십 조회의 멤버 또는 검증된 제외 영수증에서 채운다. 이로써 첫 GET 전에 빠진 참가자도 영수증 없이 잊지 않는다. 신원 충돌이나 빈/과도한 최초 명부는 완료를 허용하지 않는다. 멤버십 표시용 접속 세대 자체를 입장 권한으로 사용하지 않으며 현재 인증 Pawn·목표 이벤트와 함께 대조한다.

## 관찰과 보고 수명

`RestartPlayer` 직후와 0.25초 간격의 재발견에서 목표 delegate를 연결한다. 서버의 목표 변경 이벤트에서 미완료→완료 전환을 기록하며 GAS 종료 알림 스택 안에서 정산하지 않는다. 새 연결·Pawn/ASC 교체·사망/전투 세대 또는 성장 복원 세대 변경이 있으면 이전 완료 기록을 인정하지 않는다. 비어 있고 활성화된 새 목표 상태에서 새 관찰 수명을 잡는다. 재접속 시 이전 Pawn의 실습을 영속 복원하는 기능은 추가하지 않았다.

목표 delegate에는 발신자가 없으므로 연결한 컴포넌트를 읽어 새 완료 전환을 찾는다. 각 관찰의 기존 수명을 현재 수명과 대조하며 다른 이벤트를 계기로 오래된 완료 bool을 새 Pawn의 완료로 바꾸지 않는다. 처음부터 네 목표가 완료된 객체의 소급 인정은 없으므로 이 GameMode를 실행 중에 뒤늦게 붙이는 방식은 지원하지 않는다.

타이머는 기존 멤버십 캐시만 읽는다. 추가 HTTP 조회·새 명부 저장소·클라이언트 완료 RPC는 없다. 명부에 누락이나 실패가 있으면 다음 기존 조회를 기다리며, 표준 조회 주기만큼 완료 보고가 늦어질 수 있다.

최종 조건과 기존 연결 변경 복구의 전송 허용 상태가 성립하면 자기 제출 상태를 먼저 고정하고 타이머를 해제한 뒤 `Super::ReportConfiguredDungeonClear()`를 한 번 호출한다. 이 클래스의 동일 공개 메서드를 다른 서버 BP가 호출해도 같은 집계 조건을 거친다. 실제 요청 중복 방지·원래 버전/payload의 재시도·보상 수령 명부 확정·XP/SP/해금 영속은 기존 GameMode/HTTP/backend가 맡는다. 완료 로그는 지급 영수증이 아니다.

## 훈련 BP 전환

[기존 콘텐츠 작성기](../Scripts/Editor/CreateProgressionTrainingContent.py)에 `-RPGConfigureProgressionTrainingCompletion`을 추가했다. 새 타입을 포함한 모듈을 사용하고 에디터/다른 담당의 해당 에셋 예약을 확인한 뒤 다음처럼 적용한다. 이 문서 아래의 후속 회차에서 분리 모듈로 실제 적용했다.

```text
RPG_TRAINING_CONTENT_REPORT=<결과 JSON 절대 경로>
UnrealEditor-Cmd <조율된 프로젝트> -run=pythonscript
  -Script=<프로젝트>/Scripts/Editor/CreateProgressionTrainingContent.py
  -RPGConfigureProgressionTrainingCompletion -unattended -nop4 -nullrhi
```

이 옵션은 이미 작성된 네 훈련 에셋이 모두 있고 소유 마커·기존 역할/보상/맵 설정이 일치할 때만 `BP_TrainingGameMode`의 부모를 `ARPGGameModeBase`에서 새 `ARPGTrainingGameMode`로 바꾼다. BP 컴파일과 역할/맵 재확인 뒤 GameMode 한 개만 저장하며 이미 전환됐으면 저장하지 않는다. 다른 세 에셋의 해시 변경은 허용하지 않는다. 생성/재로드 옵션과 혼용할 수 없고 기본 호출은 계속 읽기 전용이다.

**현재 디스크의 BP 부모는 `ARPGTrainingGameMode`다.** 최초 소스 구현 회차에서는 에셋을 저장하지 않았고 아래 적용 회차에서 GameMode 한 개만 변경했다. 이전 네 에셋의 최초 작성 증거와 이번 부모 전환 증거를 구분한다. 작성기의 `party_completion_connected`는 확인한 부모 연결 여부이고 실제 완료/XP 정산 검증 여부는 별도 `party_completion_runtime_verified=false`로 남긴다.

## 최초 소스 구현에서 확인한 범위

[최초 소스 ImplementationResult.json](../Saved/BuildChecks/ProgressionTrainingCompletion-20261030/ImplementationResult.json)에 소스/작성기 해시와 정적 확인 범위를 기록했다. 당시 Python 구문과 수정 경로의 공백 오류·문서 링크를 확인했고 게임 빌드/실행은 수행하지 않았다. 보호한 공통 코드·설정·기존 에셋/DLL 17개 해시가 같았다. 수정 중이던 목표/정책 소스를 고정 입력으로 예약하거나 편집하지 않았다.

당시 차지 판정 담당의 공유 헤더 변경이 진행 중이어서 컴파일/부모 전환을 미뤘다. 그 소스 회차의 임시 실행 프로젝트·DLL/PCH/OBJ 사본·캐시는 **생성 0/정리 대상 0 bytes**였으며 후속 회차의 실행·정리 용량과 합산하지 않는다.

## 2026-10-30 컴파일·BP 전환과 독립 재로드 완료

차지 판정·HUD 담당의 완료/예약 해제를 확인한 뒤 공유 빌드/입력을 예약했다. 사용자 에디터 PID 27576/LiveCoding은 유지했으며 쓰기 전 MCP 읽기에서 PIE 비실행·훈련 GameMode 미개방과 네 훈련 에셋의 dirty=false를 확인했다. GUI에 대한 에셋 로드/편집·핫리로드·종료 요청은 없었다.

[이번 ImplementationResult.json](../Saved/BuildChecks/ProgressionTrainingActivation-20261030/ImplementationResult.json)과 같은 폴더의 `SourceAudit.json`·필수 로그·작성 JSON이 근거다.

- `Project_RPGEditor Win64 Development -NoLink -Module=Project_RPG`의 컴파일 **102개/종료 0**, 171.81초다. 새 GameMode·완료된 차지/목표·HUD와 생성 묶음을 포함한다. 현재 생성 코드를 사용했으며 이 실행에서 새 UHT 생성 성공을 별도로 주장하지 않는다. 기존 로비 포커스 시험 코드의 `SetViewportOverlayWidget` 폐기 예정 경고 1건을 보존했다.
- 원본 응답의 OBJ/LIB/RES를 복사하지 않고 예약/해시 고정한 **462개 입력**으로 별도 DLL을 링크해 종료 0이다. 입력/타입을 제외한 축약 링크가 아니지만 원본 타깃의 정상 DLL 링크/교체 성공으로 계산하지 않는다.
- 별도 Cmd의 실제 로드 DLL 경로와 SHA256 `5FDFA42C…240EF7C`를 Windows 모듈 조회로 기록했다. **부모 전환·반복 적용·독립 재로드 모두 종료 0, 저장 1/0/0**이며 네 에셋의 보고 속성과 해시는 세 단계에서 같다. GameMode만 이전 파일에서 변경됐다.
- BP는 `BS_UP_TO_DATE`, GameMode CDO·서버 정의·XP 전용 보상은 `Valid`/검증 오류·경고 없음이다. 각 Cmd 로그 Error/Fatal 0이며 기존 경고 8줄(요약 재출력 포함)은 따로 남겼다. 맵의 같은 BP 참조·PlayerStart 4개·기존 Pawn/서버/보상 설정을 유지했다.
- 공유 입력 해제 직전 **게임 소스 1007개·보호 파일 16개 불변**과 링크 입력 전후 일치를 확인했다. 공통 코드/원본 DLL·기존 Pawn/맵/서버/보상·Config/backend/타 담당을 편집하지 않았다. 본인 GameMode/작성기 소스도 이번 실행에서 수정할 필요가 없었다.

원본 DLL `AA6C9CB0…0127810`과 실행 중 GUI에는 새 타입이 반영되지 않았다. 현재 디스크의 훈련 BP를 사용하려면 해당 타입이 포함된 호환 모듈이 필요하다. 기존 에디터의 메모리 상태나 정상 원본 DLL의 실행 성공으로 표시하지 않는다.

자체 분리 DLL/PDB·RuntimeProject/UserDir·캐시는 finally에서 **212,896,156 bytes** 폐기하고 링크 대상 원본을 보존했다. 중복/상세 로그 931,544 bytes도 축소/폐기해 **합계 213,827,700 bytes(약 203.92MiB)**를 정리했다. 잔여 Temp/정리 예외 없음이며 작은 결과·명령·해시·필수 로그만 남겼다. 이 해시는 당시 증거이며 폐기 DLL의 재실행/고정 입력 재감사에는 새 빌드가 필요하다.

2026-10-30 03:14 KST의 소스 감사 뒤 공유 UHT/UBT/Intermediate·Source/Policy/링크 입력 예약을 해제했다. 이후 다른 담당의 정책/테스트 변경은 이 DLL의 검증 결과에 포함하지 않는다. 실제 조작/파티/XP E2E는 로드맵의 `training-playtest-20261030` 담당이 별도로 예약했으므로 중복 실행하지 않는다.

남은 연결은 조율된 정상 원본 모듈 반영과 실제 훈련 목표/OwnerOnly 복제·미입장자/재접속 대기·2~4인 전원 완료·단절/포기·중복 보고 및 XP 정산 확인이다. 최초 훈련의 XP 100/레벨 2/3 SP·해금이 영수증 기준 한 번만 반영되는지 확인한 뒤 로비/backend 공개를 별도 조율한다.

## 2026-10-30 1인 실제 완료 보고·XP 저장 통과

위 소스/부모 전환 기록 이후 정상 Editor DLL이 설치됐다. 다른 HUD 소스/공유 빌드 예약과 겹치지 않게 설치된 `4BCAB2FB…7B764E7`의 독립 사본을 사용했다. 기본 공격·회피의 에셋 분류 태그 불일치를 보완한 뒤, 실제 훈련 맵/개발 인증·Memory·HTTP 티켓/Iris에서 기존 입력으로 네 목표를 완료했다. 서버와 소유자 복제 모두 15에 도달한 후 이 GameMode가 기존 정산 경로로 세션을 Cleared 처리했다. 검사에서 직접 완료 보고·정산 POST·진행 설정을 호출하지 않았다.

성장 조회로 XP100/레벨2/SP3·회전 베기/투지 해방 해금을 확인했다. 이후 3회 읽기는 동일했고 다른 캐릭터도 불변이다. 서버/클라이언트 종료0/0·probe 이후 Error0/0이며 [실행 proof](../Saved/Tests/TrainingPlaytest/68ed2a36ccc2485d94c4f80f7223df44/Verification.proof.json)를 보존했다. 자기 임시 모듈/입력/캐시/로그/복구 사본 92.68MiB 정리·잔여/예외0이다.

이 결과는 **1인 정상 완료 경로**의 확인이다. 2~4인 전원 완료·미입장/재접속 유예/확정 포기·중복 완료 전송·프로세스 재시작, Steam/PostgreSQL·GPU/물리 입력·로비 공개는 아직 검증하거나 적용하지 않았다. [태그 보완과 검사 경계](PLAYER_PROGRESSION_TRAINING_PLAYTEST.md#2026-10-30-기본-공격회피-태그-보완과-1인-전체-흐름-통과)를 따른다.

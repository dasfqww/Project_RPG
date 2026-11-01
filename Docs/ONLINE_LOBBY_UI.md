# 온라인 로비 UI와 던전 입장

## 습득 스킬의 명시적 빈 슬롯 편성

스킬 구성의 원본 C++에 **빈 슬롯에 습득 스킬 넣기**를 추가했다. 현재 문맥에서 편집이
허용될 때만 기존 DraftSkills의 습득 태그를 확인하고, 기존 `SetCombatSlot`으로 아직 편성하지
않은 스킬을 첫 빈 슬롯부터 넣는다. 이미 찬 슬롯·스킬 레벨·트라이포드·SP·확인된 Saved
기록은 바꾸지 않는다. 자동 실행/저장은 없으며 사용자가 **구성 저장**을 눌러야 기존
플레이어 구성 API의 revision/권한 검사 뒤 반영된다. **변경 취소**는 기존 Saved로 되돌린다.
편성/저장/취소 버튼은 긴 스킬·슬롯 목록 위에 둔다.

기존 native 문맥 검사에 기본/사용자 편성 보존·중복 편성 방지·SP 불변·요청 중/인증 만료
거절·취소 사례를 추가했다. 별도 opt-in `-RPGTrainingEquipNewSkill`은 기존
`-RPGTrainingProgressionLobby` 검사 뒤 실제 편성 버튼→편집본만 변경 확인→저장 버튼→
확인된 새 revision/슬롯→다시 읽기를 검사하도록 작성했다. 기존 기본 훈련 probe는 유지한다.
2026-11-02 정상 반영/실행 확인을 완료했다. 장비 GAS7의 해제와 장비 명령12의 고정 읽기
인계를 확인하고 정상 타깃을 순차 호출했다. 기존 빌드가 끝난 뒤 본인 호출은 up-to-date/
0 actions/종료0이었고, 같은 시각 장비 명령 담당의 정상 출력 예약을 유지하며 본인 공유
입력/출력 예약은 즉시 해제했다. 신규 probe 문자열과 Source4 SHA가 일치하는 정상 모듈
`859E1D3D…12AC3BF6`의 닫힌 사본을 재사용했다.

native2는 모두 Success(기존 기동 Error15/발견 이후0)다. 실제 Memory/티켓/Iris 훈련을
완료한 동일 클라이언트에서 로비 복귀/캐릭터 재선택→스킬 구성 열기→편성 버튼의
draft-only 확인→저장 버튼의 실제 HTTP→다시 읽기를 통과했다. 최종 revision3의 세 번째
슬롯은 WhirlWind, 기존 두 슬롯 유지·XP100/레벨2·총SP3/사용SP0·형제 성장 불변이다.
runtime 예상외 Error0/0, 계획된 로비 단절/ConnectionLost1은 별도 집계했다. 물리 입력과
다음 던전에서 새 스킬의 실제 사용을 검증한 것은 아니다. 저장/편성 API나 신규 습득·입장
권한을 추가하지 않았고 원본 backend/에셋/기존 DB·타 담당 소스/프로세스도 변경하지 않았다.

최종 근거는 `Saved/BuildChecks/AcquiredSkillLoadout-20261101/Verification.proof.json`,
실제 실행은 `Saved/Tests/TrainingPlaytest/78880887d35a4271b03f4f98a1d6d8dd/Verification.proof.json`
및 `client.proof.log`다. 2026-11-01의 Handoff.proof.json은 당시 미빌드 이력으로 보존한다.
검증 사본/DLL/UserDir/캐시·중복 로그 202,671,053 bytes(193.28MiB)를 폐기·Temp/정리 예외0이며
작은 명령/해시/근거만 남겼다. 삭제한 모듈 해시는 당시 증거다.
PG·운영 Steam·다인·물리 입력/Shipping은 별도다.

## 캐릭터별 저장 진행과 튜토리얼 안내 — 2026-11-01

원본 구현의 정상 모듈 반영과 집중 native/실제 로비 검증을 완료했다. 훈련 메뉴는 기존
`URPGCharacterConfigurationWidget`의 확인된 backend Snapshot을 읽는다. 새 완료 저장소나
별도의 선택 캐릭터 상태를 만들지 않는다. 같은 Steam/캐릭터/World/Controller·유효 인증·
확인된 조회의 조건을 만족한 `Milestones`의 훈련 비트로 완료를 표시한다. 직전 훈련 세션의
Cleared 표시는 해당 멤버의 즉시 복귀 안내에만 보조적으로 사용한다.

Training 프로필은 로비 진입 후 현재 문맥의 첫 성장 조회를 한 번 시도한다. 캐릭터 변경은
이 조회 문맥을 비우며, 실패 후 자동 반복 요청을 보내지 않는다. 미조회/요청 중/실패·
손상/다른 캐릭터·인증 만료/미확정 저장의 정보로 튜토리얼 입장을 허용하지 않는다.
**진행 정보 다시 확인**은 기존 읽기 요청을 사용하고, 미저장 편집이나 미확정 저장이 있으면
기존 편집 내용을 보존한다. 일반 프로필의 명시적 저장본 읽기 흐름은 유지한다.

완료한 캐릭터를 다시 선택해 직전 세션이 비워져도 저장된 완료를 표시하며, 로비의 다음
안내는 스킬 구성에서 SP와 새로 습득한 스킬을 확인하도록 연결한다. 이 UI 판정은 backend의
입장/티켓 권한을 대체하거나 완료/보상·성장 기록을 변경하지 않는다. 새 opt-in native 입력
probe는 실제 완료→결과 버튼→로비→형제 캐릭터→원래 캐릭터 재선택을 검증하며, 저장 값이나
클리어 상태를 주입하지 않는다.

다른 담당의 Normal3 정상 타깃에 고정한 Source8을 순차 포함한 모듈
`46083497…2DEADA9`를 재사용했고 본인 공유 빌드/정상 출력 쓰기는 없었다. native2는 모두
Success, 기존 기동 Error15/발견 이후 Error0이다. 실제 같은 클라이언트에서 네 능력 입력·
훈련 완료/XP100·레벨2·SP3·결과 버튼 복귀 후 완료 표시→새 캐릭터의 미완료/입장 가능→
원래 캐릭터 재선택의 완료 유지·세션 캐시 없음·완료 버튼 강제 디스패치의 새 요청/세션0을
확인했다. 조회 전 입장 잠금과 형제 성장 불변도 통과했다. 미저장 편집 보존·503/인증 만료·
문맥 분리는 native에서 검사했다. runtime 예상외 Error0/0이며 계획된 로비 단절1과 그 사이
client ConnectionLost1을 별도 집계했다.

근거는 `Saved/BuildChecks/TutorialProgressMenu-20261101/Verification.proof.json`, 실제
`Saved/Tests/TrainingPlaytest/9af28f036f03485f937c957333c441b9/Verification.proof.json`과
`client.proof.log`다. 초기 공유 컴파일의 인자 누락/C2660과 PS5 검사기 열거 오류는 수정 후
성공과 분리했다. HUD가 수정 중인 러너는 닫힌 사본에 새 opt-in 플래그만 연결했고 원본
러너/에셋/backend/DB는 수정하지 않았다. 자기 사본/캐시/UserDir·중복 로그
202,342,870 bytes(192.97MiB)를 폐기·Temp/예외0·정상 출력 유지이며 삭제한 모듈의 해시는
당시 증거만 남긴다. 운영 Steam·PG/게임 재시작 영속·다인·물리 입력/Shipping은 별도다.

## 서버 선택과 메뉴형 로비 — 2026-11-01

화면 순서는 **로그인 → 서버 선택 → 캐릭터 선택/생성 → 접속 → 메뉴형 로비**다.
현재 연결된 backend 하나를 실제 서버 항목으로 표시한다. 여러 지역/월드가 있는 것처럼
가짜 선택지를 만들지 않는다. 같은 `LobbyMap` 안에서 UMG 화면을 전환하며, 서버 선택은
화면 이동이다. 캐릭터의 소유권·선택 원본은 기존 backend/`URPGDungeonSessionSubsystem`이다.

훈련 개발 프로필의 로비에는 **튜토리얼 입장**과 **스킬 구성**을 제공한다. 튜토리얼 버튼은
현재 캐릭터의 회원 정보를 먼저 조회하고, 기존 Flow의 생성 → 출발 → 서버 준비 확인 →
티켓 요청/접속을 순차 호출한다. 방 ID/주소 입력이나 생성·출발·입장 버튼의 별도 조작은
필요 없다. 중복 클릭/요청 중 재생성, Loading 주소의 조기 접속, 다른 캐릭터/던전/파티의
세션 교체를 허용하지 않는다. 오류·불확정 출발은 기존 상태를 보존하며, 사용자의 재시도로
기존 명령을 재확인한다. 새 API·클라이언트 관리자 권한·입장 승인 우회는 없다.

일반 프로필은 로비의 **던전 · 파티**에서 기존 준비 화면을 연다. 스킬 편집기도 선택해서
열며, 개발 세션 GUID 입력은 추가 `-RPGAdvancedLobby`를 명시한 상세 화면에만 보인다.
완료 후 같은 캐릭터로 로비에 돌아오면 보존된 완료 상태를 표시한다. 훈련 실행기는
확정 종료와 실제 로비 복귀를 확인한 자기 서버를 회수하고, 다른 캐릭터의 다음 튜토리얼
출발 요청을 같은 Memory 백엔드/포트에서 연속 처리한다. 실행을 끝내면 Memory 자료는
사라진다. 여러 서버/월드 라우팅,
운영 Steam 인증, 튜토리얼 영속 완료 여부와 다음 콘텐츠의 통합은 별도 범위다.

정상 게임 모듈 빌드와 집중 native 5개가 통과했다(기존 기동 Error 15, 검사 발견 이후
Error 0). 같은 설치 모듈 `94A66943…524B9742`의 사본에서 실제 Slate 로그인·서버 선택·
생성/선택·접속·메뉴 로비·튜토리얼 버튼 1회·기존 할당/입장권·훈련 맵의 0/4 표시를
확인했다. 별도 실행은 네 실제 능력 입력·서버 완료/XP100·레벨2·SP3·기존 결과 버튼의
동일 캐릭터 로비 복귀를 확인했다. 물리 키보드/마우스 검사와 운영 Steam 검증은 아니다.
화면 검사에서는 Tab으로 이름/생성 버튼에 포커스를 이동하고 실제 backend의 이름/소유
목록으로 전달을 확인한다. Slate 접근성 이름은 입력값 대신 힌트일 수 있어 값 증거로
사용하지 않는다. 초기 fixture/대소문자 정책/화면 입력 검사의 실패와 최종 성공은 분리했다.

최종 근거는 `Saved/BuildChecks/MenuFrontend-20261101/Verification.proof.json`, 실제 UI는
`Saved/TrainingDevelopment/1478f40454c9462dbddbe506687a89a9/Result.json`, 완료/복귀는
`Saved/Tests/TrainingPlaytest/3442a063838242f7ad5977d1180fbb6a/Verification.proof.json`이다.
수정 중인 HUD는 파일 집합/해시 대조를 거친 별도 overlay, 모듈/플러그인은 닫힌 사본으로
검사했다. 자기 임시 입력/사본/캐시·덤프·중복 로그 1,321,474,624 bytes(1,260.26MiB)를
폐기했고 잔여 Temp/정리 예외는 없다. 정상 Binaries/Intermediate는 유지했다. 해시는 당시
증거이며, 이후 다른 담당의 소스 변경은 이 검증에 포함하지 않는다.

연속 배정 후속은 `Saved/TrainingDevelopment/f558f965f5e24bf6b4434a09955f8eba/Result.json`
에서 두 캐릭터의 각 4조작·완료/보상·기존 결과 버튼 복귀, 이전 클라이언트가 살아 있는
상태의 서버 회수, 새 배정과 첫 캐릭터 성장 불변을 확인했다. 같은 백엔드를 유지하지만
자동 검사 클라이언트는 캐릭터마다 새로 시작한다. 같은 창의 실제 캐릭터 변경/물리 입력은
미검증이다. 신규 게임 코드·입장 API·회원/보상 권한은 추가하지 않았다.

> 2026-10-27 전투 후속: [성장 API 6.2](../Backend/CHARACTER_PROGRESSION_API.md#62-저장한-두-차지-구성의-적중쿨타임투지-순환--2026-10-27)에서 저장한 두 차지 구성의 서버 적중·투지 100 획득/소비·다음 차지 강화와 쿨타임/복제 수신을 확인했다. 개발 인증/Memory·정지 자세/복제 표적 검사이며 실제 입력/HUD·자연 보스전·Steam/PostgreSQL은 별도다.

> 2026-10-27 후속: [성장 API 6.1](../Backend/CHARACTER_PROGRESSION_API.md#61-저장-구성의-실제-서버-복원과-차지-시전--2026-10-27)에서 저장 구성의 서버/소유 클라이언트 복원과 기존 퀵슬롯 입력의 실제 차지 시전·종료를 확인했다. 영속 장비 종류 판정과 차지의 잘못된 고유 능력 분류를 수정했다. 개발 인증/Memory·실제 입장 티켓/Iris/nullRHI 범위이며 로비 편집부터 자연 전투까지의 단일 실제 입력/GPU·Steam·PostgreSQL 검증은 별도다. 아래 기록의 전투 Pawn 미검증/다음 복원 표시는 이번 후속 이전의 범위다.

> 2026-10-26 성장 구성 후속: 조회 403/404 뒤 보존한 요약을 이전 조회 정보로 표시하고 저장 잠금/재조회 안내를 보완했다. 개발 인증/Memory에서 생성 직업 목록의 503·빈 목록·재시도 중 이름/소유 목록 유지·생성 POST 0, 실제 백엔드 403/404의 편집/선택 보존·저장 잠금·재조회 후 저장을 확인했다. UE/러너 종료 0·기동/실행 Error 0·모듈/선택 소스 24개 전후 일치다. [성장 API의 최신 실행 범위와 장애 제공 방식](../Backend/CHARACTER_PROGRESSION_API.md#74-성장-조회-거절과-생성-직업-목록-실패재시도--2026-10-26)을 따른다. 실제 Steam·PostgreSQL·GPU/실물 입력·전투 Pawn 적용은 별도다. 다음은 저장 구성을 실제 전투 캐릭터로 복원하는 경로다. 아래 이전 실행은 각 당시 버전의 근거로 유지한다.

## 정상 DLL 반영과 로비 집중 실행 (2026-10-26)

누적한 로그인/캐릭터 화면을 정상 `Project_RPGEditor Win64 Development` 빌드의
266개 작업·게임/관련 Editor DLL 링크·타깃 메타데이터 기록까지 반영했다. 종료 0이며 로그는
`Saved/BuildChecks/LobbyFrontendVerification-20261026/Editor-Build.log`, 모듈 기록은
`NormalBuild.json`이다. 아래 소스 구현 항목의 첫 DLL/native 실행 대기는 이 결과로 해소됐다.
뒤에서 발견한 로그아웃 수정의 추가 반영 상태와 구분한다.

- `Scripts/Tests/Run-LobbyFrontendAutomationTest.ps1`의 집중 native 7개가 모두 Success,
  실행/러너 종료 0이었다. 실제 설정의 직업 이름 Import, 위젯 전환·선택 보존·생성 목록 및
  성장 요약 해석/잠금·안내를 검사했다. 첫 검사 전 기존 기동 Error 15줄, 검사 이후 Error 0줄로
  strict 오류 0 감사는 통과하지 못했다. 모듈/선택 소스는 실행 전후 일치했다.
  근거: `Saved/Tests/LobbyFrontend/f9edfcd3157f4633bd42b3c1fe0baf9a/Frontend.proof.json`.
- 기존 로비 러너의 `-CreationOnly`와 probe 선택 모드를 추가했다. 빈 개발 계정에서 실제
  `LobbyMap`의 로그인 버튼 → 서버 직업 조회 → 생성 버튼(이름/중복 디스패처 잠금) → 소유 목록
  갱신·선택 → 원정 준비 → 선택으로 돌아가기·다시 준비를 실행했다. 서버가 선택 직업/레벨 1을
  저장하고 다른 계정은 비어 있는지 확인했다. 세션 생성·출발·전투는 하지 않고 종료한다.
  UE/러너 종료 0, 입력/모듈 전후 일치, 기동/실행 Error 0이었다.
  근거: `Saved/Tests/LobbyCreation/958727e6f80044dabcd1b97253276838/Verification.proof.json`.
- 이어 실행한 로그아웃 회귀는 요청 직후 잠금 검사에서 실패했다. 위젯이 요청 승인 전에
  `bLoggingOut`을 켜고, coordinator가 그 위젯의 busy를 보고 요청 자체를 거절하는 순서 문제를
  확인했다. coordinator가 접수한 뒤 실제 HTTP pending 여부로 UI를 잠그도록 고쳤다. 기존의
  확인된 토큰 폐기·동일 컨텍스트·중복 입력·다른 계정 전환 제한은 그대로 적용한다.
  실패 로그: `Saved/Tests/LobbyLogout/ca4c97e0460e418fbd412cf7d66b84d2/client.log`.

로그아웃 수정의 첫 CPP 컴파일은 성공했으나 다시 열린 에디터의 모듈 잠금으로 추가 링크가
`LNK1104`였다(`Editor-LogoutFix.log`). 이후 에디터 종료를 확인하고 정상 Editor 타깃의
263개 작업·게임/관련 Editor DLL 링크·메타데이터 기록을 종료 0으로 완료했다.
근거는 같은 빌드 폴더의 `Editor-LogoutFix-Retry.log`와 `LogoutFixBuild.json`이며,
게임 DLL SHA-256은 `6428E480…E41BE1F6`이다. 실행 중 에디터를 강제 종료하지 않았다.

수정 DLL에서 `Run-LobbyLogoutE2ETest.ps1 -BackendPort 3049`도 UE/러너 종료 0으로
통과했다. 실제 로비 버튼으로 로그인/소유 캐릭터 선택 → 로그아웃의 중복/동시 로그인 잠금 →
이전 UE 토큰의 HTTP 401 → 계정·선택·목록·입력 초기화 → 같은 프로세스에서 다른 개발 계정
로그인/소유 캐릭터 선택을 확인했다. 별도 기기 토큰과 두 계정의 원래 목록도 유지됐다.
기동/로그아웃 이후 Error는 각각 0이며 DLL/선택 소스 전후 해시가 일치했다.
근거: `Saved/Tests/LobbyLogout/5b761492cfff4896be9490e3b2753fbb/Verification.proof.json`.
앞선 native/생성 결과는 각 실행 당시 모듈의 근거로 유지한다. 실제 HTTP 로그아웃의
응답 유실·재시도는 이번 재실행의 범위에 포함되지 않는다.

이 실행은 실제 UMG 버튼의 OnClicked/OnSelectionChanged와 UE HTTP를 사용하는
Development/Memory 검사다. null RHI이므로 GPU 배치·실물 입력·외형 미리보기·실제 Steam/
다른 PC·PostgreSQL·Shipping·던전 플레이의 검증이 아니다. 새 러너는 변경 소스보다 오래된
DLL의 실행을 거절하고 자신이 시작한 프로세스만 정리한다.

## 서버가 허용한 직업의 캐릭터 생성 (2026-10-26)

새 캐릭터 영역에 직업 선택·설명·직업 목록 새로고침을 연결했다. 로그인/로비 진입 뒤
인증된 `GET /api/character-creation-options`를 읽으며 현재 구현된 대검 전사만 표시한다.
기존 캐릭터 선택과 이 목록의 조회는 독립적이다. 생성 버튼은 기존 세션/요청 잠금과 함께
성공적으로 조회한 직업의 유효한 선택을 요구한다. 이름이나 표시용 직업 이름 설정만으로
생성 가능한 직업을 만들지 않는다.

- 조회 실패·잘못된 목록·빈 목록에서는 선택과 생성을 잠그고 재조회 안내를 표시한다.
  단 하나의 직업이면 그 항목을 표시 선택하고, 복수라면 직접 선택한다. 새로고침 뒤에도
  이전 저장 ID가 여전히 허용된 경우에만 선택을 유지한다. 캐릭터를 자동 생성하지 않는다.
- 생성 시 서버 목록의 정확한 `classId`를 이름과 함께 보낸다. 서버는 허용 ID인지 먼저
  검사하고, 불일치·빈 ID·초안 별칭·대소문자 차이는 400으로 거절한다. 현재 허용 ID는
  기존 대검 초기 성장/시작 장비 생성 경로와 일치한다.
- 이전 조회 응답은 세대로 구분하고 인증 토큰의 기존 응답 상관 검사를 따른다. 재인증으로
  최신 요청 자체가 무효화돼도 실패 완료해 재조회를 제공한다. 해제 시 자기 이벤트만 정리한다.
- 구형 이름만 받는 호출은 기존 서버 기본 생성 계약을 유지한다. 새 로비 UI는 이 호환
  경로로 실패한 직업 조회를 우회하지 않는다. 새 백엔드를 먼저 반영해야 한다.

API·호환 범위는 [생성 계약](../Backend/CHARACTER_PROGRESSION_API.md#10-생성-가능한-직업과-선택-생성--2026-10-26)에 있다.
다른 직업의 성장/장비 생성, 기존 캐릭터의 직업 변경과 외형 설정/미리보기는 이번 범위가 아니다.

backend Release 빌드 경고/오류 0, 기존 시작 장비/성장 검사와 허용 ID 검사가 종료 0이었다.
실제 HTTP/Memory/개발 인증 17개에서 목록, 선택 생성의 저장 직업, 구형 호출 호환,
다섯 잘못된 ID의 400·소유 목록 무변경·이름 미소모, 인증/역할 경계를 확인했다.
근거는 `Saved/BuildChecks/CharacterCreation-20261026/HttpChecks.json`이다.
UE UHT와 C++ 7개 NoLink, 마지막 인증 경계 보완의 C++ 2개 최소 컴파일이 성공했다.
같은 폴더의 `Compile.log`, `Compile-Final.log`에 기록했다. 위젯/파서 기대값은 보완했으나
native 실행·정상 DLL 링크·실제 화면/입력·PostgreSQL 실행·실제 Steam 인증은 별도다.

## 캐릭터별 저장 직업·레벨 표시 (2026-10-26)

캐릭터 선택 화면의 선택 이름 아래와 원정 준비 화면에 서버가 저장한 직업·레벨 요약을
표시하도록 연결했다. [캐릭터 목록의 성장 요약 계약](../Backend/CHARACTER_PROGRESSION_API.md#8-캐릭터-목록의-저장-성장-요약--2026-10-26)에 따라
기존 `GET /api/characters` 응답에서 받으며 캐릭터마다 추가 HTTP 요청을 만들지 않는다.

이 요약은 마지막 목록 조회 시점의 저장 정보다. 목록 조회가 경험치·해금·마이그레이션을
적용하지 않으므로, 정산 뒤 아직 반영하지 않은 성장은 기존 상세 성장 조회에서 확정된다.
요약을 Pawn 복원, GAS 부여나 구성 저장의 입력으로 사용하지 않는다.

`Missing`에는 저장 기록 없음, 누락/손상/미지원 요약에는 확인 불가를 표시한다. 레벨 1이나
현재 개발 프로필로 대체하지 않으며, 표시 실패만으로 캐릭터 자체를 목록에서 지우지 않는다.
직업 이름은 `RPGOnlineLobbyWidget.CharacterClassNames`의 정확한 저장 ID → `FText` 설정으로
표시한다. 이름이 등록되지 않은 직업은 확인 불가 안내를 쓰며 공통 UI에 대검 직업 분기를
추가하지 않는다. 현재 설정에는 대검 전사 이름만 등록돼 있다.

백엔드 Release 빌드는 경고/오류 0, 기존 시작 장비/성장 저장소 실행과 신규 요약 사례가
종료 0이었다. 동일 계정 두 캐릭터의 독립 레벨, 다른 계정 제외, 누락/손상/불일치 기록,
목록 조회의 미지급/변환 무변경을 검사했다. 실제 loopback HTTP/Memory/개발 인증 6개
검사는 `Saved/BuildChecks/CharacterSummary-20261026/HttpChecks.json`에 기록했다.
UE는 UHT와 직접 영향 C++ 5개 `-FileList -NoLink` 컴파일이 성공했고 로그는 같은 폴더의
`Compile.log`다. 표시/수신 검사를 보완했지만 native 실행·새 DLL 링크·실제 화면/입력·
PostgreSQL 실행·Steam 운영 인증은 아직 수행하지 않았다. 직업 선택 생성과 외형 미리보기도
이번 범위에 포함하지 않았다.

## 캐릭터 선택과 원정 준비 화면 분리 (2026-10-25)

Steam 계정 아래의 캐릭터들을 각각 생성·선택하는 흐름에 맞춰 네이티브 화면을
`게임 시작 / Steam 인증 → 캐릭터 선택·생성 → 선택한 캐릭터의 원정 준비`로 나눴다.
캐릭터 선택 화면에는 내 목록·선택 이름·`이 캐릭터로 원정 준비`와 별도 생성 영역을,
준비 화면에는 현재 캐릭터 이름·스킬 구성·던전/파티 메뉴를 표시한다.

- 처음 로그인한 뒤 목록 조회나 캐릭터 생성만으로 첫 캐릭터를 자동 선택하지 않는다.
  목록 선택은 기존 고유 ID 선택과 활성 멤버십 GET을 사용한다. 원정 준비 버튼은 화면만
  전환하며 세션 생성·파티 출발·던전 이동을 하지 않는다. 던전에서 돌아온 동일 GameInstance는
  기존 선택 캐릭터를 조회한 뒤 준비 화면을 복원한다.
- 캐릭터 이름만 선택지에 표시하고 내부 ID는 저장/권한 확인에 사용한다. 현재 서버의 이름
  중복 금지 계약을 사용하되, 같은 표시 이름이 여러 번 들어온 응답은 첫 항목으로 임의 선택하지
  않는다. 거절된 콤보 선택은 기존 선택으로 되돌린다.
- 미조회·조회 중·실패·서버가 확인한 빈 목록을 구분한다. 갱신 실패 시 지난 조회가 있으면
  이전 목록임을 안내한다. 생성 응답을 확인하지 못하면 생성 성공으로 처리하지 않고 목록에서
  확인하도록 안내한다. 이름 입력과 기존 목록을 새 캐릭터 저장본으로 추정하지 않는다.
- 캐릭터 선택으로 돌아가기는 기존 세션/파티·요청/미확정 저장 잠금을 따른다. 저장하지 않은
  스킬 구성이 있으면 저장 또는 되돌리기를 먼저 요구하고, 콤보 강제 디스패처도 전환을 거절한다.
  화면 이동만으로 캐릭터 선택·완료 세션·성장 저장본을 지우지 않는다.
- 같은 Steam 계정의 재로그인 시작 시 소유 목록을 비우지 않도록 보완했다. 이로 인해
  성장 구성의 현재 캐릭터가 일시적으로 사라져 초안/미확정 저장이 초기화되던 경로를 막는다.
  인증 성공 뒤에는 기존대로 목록을 새로 조회하며 확인된 로그아웃만 계정 선택을 정리한다.

생성 API와 직업별 성장 데이터는 기존 서버 경로를 사용한다. 이번 변경은 대검 이외 직업의
생성을 열거나, 클라이언트에 직업/레벨/장비를 추정해 표시하는 기능이 아니다. 직업 선택과
캐릭터별 실제 직업·레벨·외형 상세 표시는 후속 작업이다.

UHT 갱신과 직접 영향 C++ 8개 `-FileList -NoLink` 컴파일이 성공했다. 마지막 전환 가드 보완의
`RPGOnlineLobbyWidget.cpp`도 최소 재컴파일이 성공했다. 로그는
`Saved/BuildChecks/CharacterSelection-20261025/Compile.log`와 `Compile-Final.log`다.
기존 위젯/안내 검사의 화면 전환·독립 ID/이름·재인증 표시 보존 기대값을 보완하고 기존
E2E probe에 원정 준비 버튼 단계를 연결했지만, 이 단계에서는 검사를 실행하지 않았다.
실행 중 에디터와 DLL을 보존했으며 정상 DLL 링크·실제 UI/입력·HTTP/Steam 성공은 아직 별도다.

## 기본 시작점과 로그인 화면 분리 (2026-10-25)

플레이 가능한 대검 개발 실행은 `/Game/Maps/testmap`을 명시해 접속/캐릭터 선택을 건너뛴다.
프로젝트 `GameDefaultMap`도 같은 전투 맵이어서, 별도 맵을 지정하지 않은 게임 실행이
기존 로비의 인증 UI를 보여 주지 않았다. 기본값을 `/Game/Maps/LobbyMap`으로 변경했다.
에디터 시작 맵과 명시적인 개발 전투 실행 스크립트의 맵 선택은 그대로 사용한다.

기존 `URPGOnlineLobbyWidget` 안에 로그인 페이지와 인증 후 준비 페이지를 분리했다.
미인증/인증 만료에는 로그인 안내·인증 버튼을, 인증 성공에는 기존 캐릭터 생성/선택·성장
구성·던전/파티 메뉴를 표시한다. 로그인 중 안내와 실패 시 재시도를 제공하며 페이지를
전환할 때 위젯/미확정 저장·캐릭터·세션을 새로 만들거나 지우지 않는다. 기존 확인된
로그아웃만 계정 선택을 정리하며, 만료된 자격의 로그아웃도 로그인 페이지에서 접근할 수 있다.
기존에 화면이 포커스를 소유했을 때만 새 페이지의 유효 컨트롤로 포커스를 이어 준다.

개발 로그인은 기존 비 Shipping `-RPGDevelopmentLogin -nosteam` 조건을 유지한다.
이 모드에서는 Steam 버튼을 숨기고 17자리 개발 ID 입력과 개발 인증 버튼을 표시한다.
개발 모드 없이 `-nosteam`만 전달하면 Steam 로그인은 비활성화하고 재실행 안내를 표시한다.
새 인증/계정 서비스나 자동 로그인·캐릭터 선택·던전 이동은 추가하지 않았다.

정상 Editor 빌드 반영 후 PowerShell에서 다음으로 로비를 실행할 수 있다.
백엔드는 미리 실행돼 있어야 하고, 개발 로그인은 백엔드에서도 명시적으로 허용해야 한다.

```powershell
.\Scripts\Development\Open-OnlineLobby.ps1
.\Scripts\Development\Open-OnlineLobby.ps1 -DevelopmentLogin
```

`-PrintCommand`는 실행하지 않고 인자만 출력한다. 로그인 실행은 대검 개발 프로필을
주입하지 않으며, 캐릭터 데이터는 기존 백엔드의 조회/생성/복원 경로를 사용한다.

검증은 UHT 갱신, 직접 영향 C++ 3개의 `-FileList -NoLink` 개별 컴파일과 두 실행 모드의
스크립트 파싱/인자 출력까지다. 기존 위젯 검사의 페이지 표시·인증 전환 후 편집 상태 보존
기대값을 보완했고, 개별 컴파일에서 드러난 테스트의 누락 `UObject/Package.h`를 추가했다.
로그는 `Saved/BuildChecks/LoginEntry-20261025/Compile.log`와 `Compile-Final.log`다.
실행 중 에디터/게임을 종료하거나 DLL을 교체하지 않았다. 새 DLL 링크·자동화 실행·실제
로그인 화면/HTTP·Steam 인증/입력 확인은 아직 수행하지 않았다.

## 구현 범위

`URPGOnlineLobbySubsystem`이 로컬 Standalone `/Game/Maps/LobbyMap`에서
`URPGOnlineLobbyWidget`을 자동 생성한다. 전용 서버·던전 클라이언트에는 표시하지 않으며,
던전 이동 시 해제하고 같은 GameInstance로 로비에 돌아오면 다시 생성한다.
기존 맵/Blueprint/PlayerController 에셋을 수정하지 않는 네이티브 기본 화면이다.

화면은 기존 인증·HTTP·던전·Steam 서브시스템을 호출하는 프런트엔드다.
새로운 DB 접근, 보상 지급 RPC, 서버 할당 관리자 권한은 추가하지 않는다.

- Steam 로그인, 인증된 내 캐릭터 목록 조회, 캐릭터 생성 및 선택.
- 캐릭터 선택 시 활성 던전 세션 복원. 소유 목록에 없는 임의 ID는 선택하지 않는다.
- 대기 세션 생성, 복원/새로고침, 대기 세션 나가기, 준비된 던전 입장.
- 활성 던전의 파티원 목록/인원과 방장·본인 표시. 서버 조회 결과의 읽기 전용 표시이며 접속/준비 상태는 추정하지 않는다.
- 새 세션의 Easy/Normal/Hard/Hell 선택. 생성 이후에는 백엔드 난이도를 표시하며 변경하지 않는다.
- Steam 파티 생성/검색/참가, 수락한 초대 참가, 친구 초대 오버레이, 서버 정보 자동 게시/수동 재시도 및 파티 나가기.
- 세션 상태·인원·난이도와 요청 실패 코드 표시. 요청/이동 중 중복 입력 차단.
- 복귀 시 인증·캐릭터 선택·완료 세션을 보존하고 소유 목록을 다시 읽어 선택 표시 복구.
- 안전한 로비의 명시적 백엔드 로그아웃. 현재 토큰 폐기 확인 뒤 로비의 계정/캐릭터 선택과 목록을 정리한다.

기본 던전은 등록된 `DA_Dungeon_RaidSwordMan`의 `Dungeon.RaidSwordMan`, 초기 난이도 선택은 `Hard`다. 난이도는 생성 전에
네 값 중 선택할 수 있다. 선택형 정의 에셋 목록을 등록하면 던전 종류와 지원 난이도를 선택할 수 있다.
출시용 디자인은 아니며,
사용자 입력 없이 캐릭터 선택/새 세션 생성/이동을 하지 않는다.

## 상태와 권한 경계

| 상태 | 허용되는 로비 작업 |
| --- | --- |
| 미인증 | 로그인 |
| 인증됨, 활성 세션 없음 | 내 캐릭터 선택/생성, 세션 생성/복원, Steam 파티 검색/참가 |
| `Waiting` | 세션 조회, 대기 세션 나가기, 현재 세션의 Steam 파티 생성 |
| `Loading` | 세션 조회. 할당 주소가 있더라도 입장은 잠금 |
| `InProgress` | 선택 캐릭터의 멤버십·서버 ID·주소가 확인된 경우 입장 |
| `SettlementPending` | 세션 조회. 캐릭터/새 세션 전환과 입장은 잠금 |
| `Cleared` / `Failed` / `Closed` | 새 캐릭터 선택 및 새 세션 생성/복원 가능 |
| HTTP/Steam 요청 중, `Connecting` / `ReturningToLobby` | 추가 작업 잠금 |

알 수 없는 비어 있지 않은 세션 상태도 새 세션/캐릭터 전환을 허용하지 않는다.
상태·캐릭터·멤버십 비교는 대소문자를 구분한다. 클릭 핸들러도 최신 상태를 다시 확인한다.
Steam 파티에 속한 동안에는 캐릭터 변경과 별도 백엔드 세션 생성/탈퇴를 잠근다.

사용자가 서버 주소를 직접 입력하는 UI는 없다. Steam 파티 입장은 기존
`ConnectToDungeonServer`의 메타데이터/백엔드 대조를, 파티 밖의 복원 세션은 현재
백엔드 주소로 `ConnectToDungeon`을 호출한다. 기존 흐름에서 새 Join Ticket을 받은 뒤
이동한다. UI가 인증·서버 준비·티켓 소비 검증을 대체하지 않는다.

활성 세션은 2초 간격, 최대 120초간 자동 조회한다. 요청은 겹치지 않으며 새 세션이나
수동 새로고침은 조회 기한을 갱신한다. 기한 이후에도 수동 새로고침은 가능하다.
화면 해제 시 타이머·자신의 버튼 및 서브시스템 이벤트 바인딩을 제거한다.

## 난이도 선택 UI와 BP 연결 (2026-10-19)

기본 로비의 `DungeonDifficulty` ComboBox는 정확한 `Easy` / `Normal` / `Hard` /
`Hell`만 제공한다. 선택은 현재 위젯의 새 세션 생성 선호도일 뿐이며, 그 자체로 HTTP,
GameInstance의 서버 난이도 설정, 보스 초기화 또는 보상 변경을 하지 않는다.
생성 버튼을 누를 때만 기존 인증된 `CreateDungeonSession`에 선택값을 전달한다.

- 실제 Standalone 로비·인증·내 캐릭터 선택이 준비되고 미완료 세션/미확정 포기가 없어야
  선택할 수 있다. UI/인증/Steam/던전 요청과 이동 중, Steam 파티 소속 중에는 잠근다.
  콤보와 버튼의 강제 디스패처 및 BP 함수도 최신 상태를 다시 검사한다.
- Waiting/Loading/InProgress/SettlementPending 및 알 수 없는 미완료 상태에서는
  서버 스냅샷의 난이도를 읽기 전용으로 표시한다. 다른 난이도의 파티에 참가해도 로컬
  기본값으로 덮어쓰지 않는다. 완료 세션 뒤에는 새 세션용 선호도를 다시 사용한다.
- 잘못된 설정/응답·대소문자/공백 차이·알 수 없는 enum을 Hard로 바꾸지 않는다.
  유효하지 않은 표시 값은 선택을 비우고 생성은 차단한다. 인증/소유 선택이 준비되면
  지원되는 값을 직접 선택해 설정을 교정할 수 있다.
- 선호도는 디스크에 저장하지 않는다. 위젯을 새로 만들면 설정의 기본값에서 시작하되,
  미완료 세션이 있다면 그 백엔드 값이 우선한다. 세션 생성 뒤 난이도 변경 API는 없다.

커스텀 BP 연결:

1. GameInstance의 `RPGOnlineLobbySubsystem` → BP Pure `GetLobbyWidget`으로 현재
   네이티브 화면 참조를 얻는다. 자동 생성 전/해제 후 null일 수 있으므로 유효성을 확인한다.
2. `GetDungeonDifficultyOptions`의 enum 목록으로 선택지를 만들고,
   `GetSelectedDungeonDifficulty`의 **bool 결과가 true일 때만** 출력 enum을 사용한다.
3. 선택 변경 시 `CanSelectDungeonDifficulty`를 확인하고 `SelectDungeonDifficulty`를
   호출한다. false는 컨텍스트 잠금 또는 잘못된 값이며 기존 선호도/세션을 보존한다.
4. 생성 버튼은 `CanSelectDungeonDifficulty`와 유효한 선택을 모두 확인한 뒤
   `CreateSelectedDungeonSession`을 호출한다. true는 요청 시작이지 백엔드 생성 완료가
   아니다. 기존 `RPGDungeonSessionSubsystem.OnDungeonSessionChanged` /
   `OnFlowStateChanged`로 완료 값·잠금 상태를 갱신한다.

이 프런트엔드 선택은 서버의 최종 난이도 권한을 대체하지 않는다. 인증된 배정 응답에서
확정한 난이도로 생성·보상 선택을 수행하는 기존 서버 경계를 유지한다. 이번 작업은
저장된 BP/맵/보스 수치표나 백엔드 API/스키마를 수정하지 않는다.

검증: Editor 전체 타깃 증분 34개 작업/게임 DLL 링크와 집중 native 6개가 Success·종료 0이었다.
기존 로비 정책·이름 검사·실제 위젯/강제 디스패처/재생성 해제, 신규 BP 계약과 미완료/
잘못된 응답·설정/완료 뒤 선호도 표시를 검사했다. 같은 DLL의 실제 로비 두 클라이언트에서
네 값의 로컬 선택·잘못된 값 거절→방장 Normal 생성→실제 HTTP/PostgreSQL Waiting 세션→
다른 기본 선호도의 참가자도 Normal 표시·잠금→요청/세션 중 강제 선택/중복 생성 거절을
확인했다. 러너 종료 0, 결과:
`Saved/Tests/DungeonBossReturn/cc659f35dfbf4c54b07d3bbfe21e9751/result.json`.

선택 입력 11개와 native/HTTP 실행의 DLL은 일치했다. native 시작 전 Error 15줄,
HTTP 클라이언트 로그인 마커 전 Error 각 3줄(총 6줄)이 있어 엄격한 오류 0 감사는 실패다.
검사 시작/로그인 이후 Error와 Fatal/Assert/Ensure는 0이며 기동 오류를 해결하거나 새로
허용 처리한 것은 아니다. 통합 증거:
`Saved/Tests/LobbyDifficulty-mvf9xu3j/Verification.proof.json`.
새 개발 계정·캐릭터·세션과 로그는 보존하고 기존 DB 데이터는 삭제하지 않았다.
실제 BP 이벤트 그래프 실행/자산 저장, Steam 파티·GPU/게임패드·Shipping, 네 난이도 전부의
실제 HTTP/전투·Normal의 입장/보스/정산은 이번 검사 범위가 아니다.

기존 보스 통합 러너의 선택형
`-LobbyUI -LobbyDifficultyOnly -LobbyDifficulty Normal`은 실제 로비/선택/생성/파티 참가까지만
실행한다. 기존 Hard 전투·보상 기대값을 다른 난이도에 재사용하거나, 이 모드를 보스 전투
전체 검증으로 표시하지 않는다. 다음은 선택 난이도의 실제 입장·보스 초기화·보상까지
이어지는 흐름을 연결/확인하는 단계다.

## 안전한 로비의 백엔드 로그아웃 (2026-10-18)

기본 로비의 `LogoutBackendButton`과 `URPGOnlineLobbySubsystem`의 BP 노드
`CanLogoutFromBackend` / `LogoutFromBackend`, `OnLogoutCompleted`를 연결했다.
Steam 자체를 로그아웃하거나 다른 Steam 계정으로 강제 전환하는 기능은 아니다.
개발 인증의 다른 ID는 명시적 로그아웃이 확인된 뒤 다시 입력할 수 있고, 실제 Steam
로그인은 계속 현재 Steam 클라이언트 계정의 티켓을 사용한다.

- 자기 GameInstance의 Standalone 로비 컨트롤러여야 한다. 활성/알 수 없는 던전 상태,
  미확정 포기, Steam 파티, 로그인·캐릭터 조회/생성·파티 요청·이동 중에는 허용하지 않는다.
  파티나 던전을 자동으로 포기하지 않으며, 필요한 나가기/복귀를 먼저 명시적으로 수행한다.
- 백엔드의 본문 없는 `POST /api/auth/logout`은 인증된 플레이어의 **현재 요청 토큰**만
  폐기한다. 요청 본문의 계정/다른 토큰은 대상으로 쓰지 않는다. Memory는 동일 레코드의
  조건부 제거, PostgreSQL은 hash+SteamID 조건의 원자적 DELETE를 사용한다. 다른 기기의
  로그인, 서버 자격 증명·Join Ticket·던전 멤버십·캐릭터/아이템/보상은 삭제하지 않는다.
- UE는 원래 요청 객체/자격 증명이 일치하고 실제 수신한 204 또는 401일 때만 로그아웃을
  확인한다. 401은 이미 폐기/만료돼 사용할 수 없는 같은 토큰의 재시도 완료다. 로컬 시계
  만료만으로 계정 고정을 풀지 않으며, 보관한 마지막 토큰으로 폐기를 확인한다.
- 전송 실패·응답 유실·403·429·5xx·구형 서버 404는 계정 정보를 유지한다. 다시 눌렀을 때
  서버에서 앞선 폐기가 커밋됐다면 401로 완료할 수 있다. 새 계정의 자격 증명이나 다른
  요청의 늦은 응답은 이전 로그아웃으로 지우지 않는다.
- 요청 중 플레이어 인증 사용·로그인·던전/Steam 파티 작업을 잠근다. 완료는 게임 스레드에서
  처리하고 원래 로비/컨트롤러·계정·선택·세션을 다시 확인한다. 컨텍스트가 바뀌었는데
  서버 폐기만 확인됐다면 토큰만 무효화하고 계정 고정·선택은 유지한다.
- 정상 완료에서만 로비의 선택 캐릭터/완료 세션·목록·입력과 메모리 인증을 정리한다.
  이전 인증/던전 응답을 무효화하며, 이미 제출한 아이템/보상 트랜잭션을 롤백하지 않는다.

백엔드를 먼저 배포해야 한다. 이 UI는 게임 서버의 접속자 로그아웃이나 운영자 제재,
계정 전체/모든 기기의 토큰 폐기와는 별개다. 저장 BP 자산이나 DB 스키마 변경은 없다.
집중 백엔드 실행은 `dotnet run --project Backend/ProjectRpg.Backend.DungeonAbandonmentTests -c Release -- --logout-only`다.

### 커스텀 로비 BP 연결

기본 네이티브 화면은 이미 연결돼 있다. 별도 WBP에서는 자신의 GameInstance에서
`Get Game Instance Subsystem (RPGOnlineLobbySubsystem)`을 얻고 다음처럼 사용한다.

1. 버튼 활성 여부는 `CanLogoutFromBackend(Get Owning Player)`로 갱신한다.
2. 클릭 시 `LogoutFromBackend(Get Owning Player)`를 호출한다. 반환값은 **요청 접수**이며
   로그아웃 성공이 아니다. false이면 기존 선택/목록을 지우지 않는다.
3. Construct에서 `OnLogoutCompleted`를 바인딩하고 Destruct에서 자기 바인딩만 제거한다.
   `bSuccess=true`에서만 자기 WBP의 목록/입력을 비운다. 인증·선택 캐릭터·세션 초기화는
   서브시스템이 담당한다. false이면 실패 안내와 재시도를 제공하고 기존 표시를 유지한다.

WBP가 직접 HTTP를 보내거나 토큰을 비우고 성공 화면으로 이동하지 않는다. 개발 계정 ID를
바꾸더라도 완료 이벤트 뒤 기존 로그인 경로를 사용한다. 실제 Steam 계정 전환은 Steam
클라이언트의 별도 작업이며 이 노드가 플랫폼 인증을 바꾸지 않는다.

### 집중 검증과 한계

- 백엔드 Release 빌드 경고/오류 0, Memory 저장소와 실제 loopback HTTP의 5개 검사 그룹이
  통과했다. 다른 계정/같은 계정의 다른 기기 토큰 보존, 관리자·서버 자격 증명 거절,
  위조 본문 무시, 취소, 재시도/만료를 포함한다. PostgreSQL 구현/검사는 컴파일했으나
  테스트 DB 미설정·Docker daemon 미실행으로 PostgreSQL 런타임 검사는 하지 않았다.
- 처음 Editor 빌드는 공유 단절 journal의 이전 obj 서명 때문에 링크 실패했다. 현재 공유
  빌드 완료를 기다린 최종 자체 타깃 확인은 `Succeeded / 0 action`이다. 자체 클린 재빌드나
  독점 링크라고 표시하지 않는다. 해당 DLL에서 `DungeonLogout.Transport`,
  `DungeonLogout.Policy`, `DungeonLobbyWidget`, `DungeonAuthenticationTransport`,
  `DungeonJoinTicketFailure.MembershipRecovery` 5개가 Success, 프로세스 종료 0이었다.
- 테스트 시작 전 `Condition failed` Error 15개가 있어 엄격 로그 감사는 통과하지 못했다.
  선택 테스트 시작 이후 Error·실패·Fatal/Assert/Ensure는 0개이고, 선택 소스 19개와 실행
  전후 DLL 해시는 일치했다. 기동 오류 원인은 미확정이며 이를 무시하거나 수정하지 않았다.
  증거: `Saved/Tests/PlayerLogout-mvdw2l3i/Verification.proof.json`.

이 단계의 검증은 실제 UE HTTP/로비 버튼 통합 실행을 포함하지 않았다. 아래 후속 실행과
구분한다. 진행 중 던전·파티의 강제 로그아웃 정책으로 확대하지 않는다.

### 실제 로비 로그아웃·다른 개발 계정 재로그인 (2026-10-19)

`Scripts/Tests/Run-LobbyLogoutE2ETest.ps1`은 기존 던전 흐름 probe의 선택 모드를 사용해
실제 `/Game/Maps/LobbyMap`의 기본 위젯과 HTTP 경로를 실행한다. 전용 서버·전투·정산
전체 실행을 반복하지 않으며, 기존 서버/DB를 재사용하지 않는 별도 Development/Memory
백엔드와 고유한 테스트 계정 두 개만 사용한다.

```powershell
& .\Scripts\Tests\Run-LobbyLogoutE2ETest.ps1
```

첫 계정의 로그인 버튼→소유 캐릭터 선택/활성 멤버십 조회→로그아웃 버튼→확인 후 UI
초기화→같은 프로세스의 다른 개발 계정 로그인/선택을 실행한다. 강제 중복 클릭/동시
로그인 가드를 확인하며, 첫 UE 토큰은 메모리의 별도 GET 요청으로만 보관해 로그아웃 후
실제 HTTP 401을 확인한다. 토큰·응답 본문은 파일/콘솔/UE 실행 인자로 전달하지 않는다.
러너가 별도로 발급받은 두 계정의 토큰과 원래 캐릭터 목록은 종료 직전에 다시 조회한다.

기본 포트 3038이 사용 중이면 중단한다. 실행별 `Saved/Tests/LobbyLogout/<RunId>/`에
격리 백엔드 출력, UE 로그, 소스/DLL 해시와 proof를 남기고 자신이 시작한 두 프로세스만
종료한다. 테스트 호스트 종료로 해당 실행의 메모리 계정이 사라지며 기존 DB/계정/에디터는
변경하지 않는다. 실제 Steam·PostgreSQL·Shipping, 물리 마우스/GPU 배치와 실제 응답 유실
조건은 별도다.

실행 기록:

- 최초 빌드는 캐시된 소스 목록에 새 probe CPP가 없어 새 메서드 두 개의 링크에 실패했다.
  소스 목록을 다시 수집한 `-NoUBTMakefiles` 증분 빌드는 UHT·23개 작업·게임 DLL 링크와
  Editor 타깃을 성공했다. `Saved/Tests/LobbyLogoutE2E-mvdz20qw/Build-Regather.log`에 기록했다.
- 실제 실행 `Saved/Tests/LobbyLogout/73b53bc4ea584b3a85034fe383cbb6df/`에서
  REQUEST / CONFIRMED / PASS 세 마커, 첫 UE 토큰의 HTTP 401, 계정 고정·캐릭터 선택·목록/
  입력 초기화, 강제 중복/로그인 잠금, 다른 계정의 소유 목록/캐릭터 선택을 관측했다.
  UE 프로세스는 종료 0이며, 러너의 두 별도 토큰과 원래 캐릭터 목록 재조회도 통과했다.
- 단 UE 기동 중 다른 공유 빌드가 DLL을 교체해 러너는 해시 가드에서 **종료 1**이었다.
  실제 로드된 DLL을 실행 전/후 해시 중 하나로 확정할 수 없으므로 최신 공유 DLL의
  테스트 통과로 계산하지 않는다. 실행 중 선택 소스 9개는 유지됐으나 빌드 시작 이후
  공용 HTTP 소스 두 개의 별도 변경도 있었으므로 전체 소스의 독점/클린 빌드 증거가 아니다.
- 로그아웃 요청 전 `LogUIActionRouter` / `LogTemp` Error 각 1개가 있고 원인은 이번 범위에서
  확정하지 않았다. 요청 이후 Error·Fatal/Assert/Ensure·probe 실패는 0개다. 이전 native
  automation의 `Condition failed` 15개와 다른 `-game` 실행이며 오류가 해결됐다고 표시하지 않는다.
  관측 증거는 `Execution.observed.proof.json`에 보존했다. 자신의 두 프로세스만 종료했고
  기존 DB는 변경하지 않았다.

해시 불일치에서도 기능 관측을 잃지 않도록 러너가 먼저 proof를 저장한 뒤 실패하도록
보완했다. 이 후속 PS 수정은 구문 검사만 했으며 다시 실행하지 않았다. 이번 결과는 실제
로비/HTTP 동작의 관측이며, 엄격 로그 감사·버전 귀속·Steam/Shipping 통과와는 구분한다.

## 로컬 아이템 UI 검증용 백엔드 연결 (2026-10-14)

- MCP는 에디터 조작용 `8000`, 게임 백엔드는 별도 ASP.NET Core 프로세스의
  `localhost:3000`이다. MCP의 포트를 `3000`으로 바꾸는 것은 백엔드 실행이 아니다.
  최초 복원 때 UE의 미사용 IPv4 리스너가 남아 IPv6 loopback으로 실행했으며,
  에디터 재시작 뒤에는 IPv4/IPv6 loopback 양쪽의 백엔드 readiness HTTP 200을 확인했다.
- `Saved/Tests/ItemUIBackend/8e0f93edb4c84443b9e8ea8b2918757b/Start-Backend.ps1`은
  기존 호스트를 덮어쓰지 않고 Development/Memory로만 시작한다. PostgreSQL 연결
  환경값은 자식에서 제거하며 기존 DB/프로젝트 설정은 수정하지 않는다. 암호화된
  로컬 DPAPI 자격 증명을 재사용하고 시작마다 새 로그를 만들어 이전 로그를 보존한다.
  메모리 데이터는 종료/재시작 뒤 보존되지 않는다. 재개 시 종료된 호스트를 복구했으며
  이 단계에서 UE나 백엔드를 재빌드하거나 이전 전체 테스트를 반복하지 않았다.
- PowerShell의 상태 확인/개발 인증/소유 캐릭터 조회에 이어 실제 `UHttpWebManager`
  HTTP 로그인과 인증된 빈 목록 조회도 PASS다. Native 검사는 기존 플레이어의
  서브시스템이 아닌 detached GameInstance 소유 객체에서 실행하고 delegate/tick/
  임시 토큰을 정리했다. 기존 PIE의 world/Controller/미인증 상태는 전후 동일했다.
  초기 MCP console focus 실패는 창 선택/console click으로 해결했으며 엔진 코드는
  수정하지 않았다. 자신의 observer만 해제하고 에디터와 백엔드는 유지했다.
- 증거는 위 디렉터리의 `Connection.proof.json`, `native-http.observed.log`,
  `NativeHttpProbe.py`, 시작별 backend 로그다. 범위는 **실제 UE↔로컬 HTTP 인증/조회**다.
  개발용 Memory 인증은 실제 Steam·PostgreSQL 영속성·기존 플레이어 로그인·게임 서버
  입장·인벤토리 Ready·실제 아이템 등록/소비나 Shipping 완료 증거가 아니다.
  다음은 별도 테스트 게임 서버/캐릭터와 인증 입장을 준비한 뒤 아이템 UI를 검증한다.

## 방장의 서버 정보 자동 게시 (2026-10-11)

`URPGSteamLobbySubsystem`이 백엔드 세션 조회 완료와 Steam 작업 완료 뒤 서버 정보를
자동 게시한다. 파티 생성 완료 전에 서버가 준비됐거나 다른 OSS 요청 중 준비 상태가
도착한 경우도 작업 완료 뒤 다시 확인한다. 파티/백엔드 세션 생성 자체는 아직 별도
사용자 작업이며, 운영 서버 할당이나 자동 입장을 추가한 것은 아니다.

- 현재 플레이어 인증, 선택 캐릭터의 정확한 멤버십, Steam 로비 방장 여부와
  로비/백엔드 세션 ID 일치를 모두 요구한다. 준비 확인은 정확히 `InProgress`만 허용한다.
- 백엔드/Steam 요청 중, 이동/복귀·오류 상태 또는 미확정 포기가 있으면 자동 게시하지 않는다.
  정보는 현재 백엔드 스냅샷에서 가져오며 기존 게시 API의 방장/동일 세션 검증을 재사용한다.
- 세션·상태·서버 ID·정규화 주소가 같은 연속 스냅샷은 자동으로 한 번만 시도한다.
  요청 직전에 시도 기록을 남기므로 즉시 거절이나 완료 실패도 폴링 재시도를 만들지 않는다.
  OSS가 실패한 Update의 로컬 설정을 먼저 바꿔도 성공 영수증으로 취급하지 않는다.
- 게시 실패는 기존 오류 상태로 표시하고 파티·백엔드 세션을 삭제하지 않는다.
  기존 게시 버튼은 명시적 재시도 경로로 유지한다. 새 서버 배정은 새 자동 시도가 가능하고,
  파티 해제/재생성 시 이전 시도 기록을 초기화한다.
- 멤버십·Join Ticket·실제 서버 준비/주소 대조와 인벤토리·보상·DB 권한은 변경하지 않았다.

첫 조건/완료 선택 검증은 `RPGSteamLobbyPublicationTests.cpp`의
`ProjectRPG.Online.SteamPublication.Guards`, `Deduplication`, `CallbacksAndReset`이다.
이 세 fixture는 운영 eligibility/시도 기록/완료 callback을 실행하지만
OSS 전송·백엔드 HTTP·실제 Steam 메타데이터 전파를 실행하지 않는다.

운영 구현 파일은 Development/Shipping의 `-SingleFile -NoLink` 컴파일을 통과했다.
로그: `Saved/Logs/SteamServerAutoPublish-Compile-20261011.log`,
`Saved/Logs/SteamServerAutoPublish-ShippingCompile-20261011.log`.
이 로그의 테스트 파일 컴파일은 아래 GameInstance fixture 수정 전이며,
수정된 fixture는 후속 Development 모듈 링크와 네이티브 실행으로 검증했다.
Shipping 모듈 링크·패키징·실제 Steam 성공을 뜻하지 않는다.

후속 게임 모듈 검증은 `-Module=Project_RPG -NoHotReload`로 제한했다.
최초 35개 컴파일 뒤 다른 UE 게임/테스트 프로세스의 DLL 잠금으로 `LNK1104`가
발생했으며 해당 프로세스를 종료하지 않았다. 잠금 해제 뒤 재시도에서 발견된
공유 NPC fixture의 컴파일 오류는 다른 작업에서 이미 수정된 것을 확인했다.
이 실패 이력은 `SteamServerAutoPublish-ModuleLink-20261011.log`와
`SteamServerAutoPublish-ModuleLink-Retry-20261011.log`에 보존한다.
공유 수정 후 링크는 `SteamServerAutoPublish-ModuleLink-AfterSharedFix-20261011.log`,
최종 fixture 수정 후 3개 작업(테스트 컴파일·lib/DLL 링크)의 성공은
`Saved/Logs/SteamServerAutoPublish-FixtureOuter-ModuleLink-20261011.log`에 기록했다.
모듈 링크 성공이며 전체 Editor 타깃 빌드는 아니다.

첫 선택 자동화는 신규 fixture의 GameInstance 서브시스템을 Package 부모에 생성해
Ensure가 발생했다. 세 fixture의 부모를 `UGameInstance`로 수정했으며 운영 코드는
변경하지 않았다. 실패 로그는
`Saved/Tests/SteamLobbyPublication/Automation-0e270337cec84e148883c93b80cd1452/automation.log`에 남긴다.

검증 시점의 DLL에서 신규 자동 게시 3개와 직접 인접한 `DungeonLobbyPolicy`/
`DungeonLobbyWidget` 2개가 모두 성공했고 Ensure/Fatal/Assertion은 없었다.
실행 전후 관련 소스 10개와 DLL 해시가 같으며, 전체 회귀는 반복하지 않았다.
선택 경로·결과·소스/DLL/로그 해시와 범위는
[네이티브 검증 proof](../Saved/Tests/SteamLobbyPublication/Automation-520485ec675b4411a921a8a10299bdd0/SteamLobbyPublication.proof.json),
[실행 로그](../Saved/Tests/SteamLobbyPublication/Automation-520485ec675b4411a921a8a10299bdd0/automation.log)에 있다.
검증 완료 뒤 공유 DLL이 다시 갱신됐으므로 더 새 바이너리의 통과로 표시하지 않는다.
이 결과는 실제 `UpdateSession` 전송·HTTP·Steam 전파·서버 할당·네트워크/Shipping
검증이 아니며, 파티 생성 통합과 실제 Steam 2계정 검증은 별도 작업이다.

## 자동 게시의 OSS 호출 연결 검증 (2026-10-11)

같은 테스트 파일에 `ProjectRPG.Online.SteamPublication.Dispatch`,
`DeferredDispatch`, `ManualRetryDispatch`를 추가했다. 실제 GameInstance의
HTTP/던전/Steam 로비 서브시스템을 초기화하고, 세션 인터페이스만 기록용
`IOnlineSession`으로 교체한다. 인증/백엔드 스냅샷은 로컬 fixture이며,
HTTP 요청이나 실제 Steam 전송은 하지 않는다. 운영 로직은 변경하지 않았고
두 서브시스템의 friend 접근 선언과 개발 자동화 테스트만 추가했다.

- 실제 던전 흐름 delegate → 자동 게시 → public 게시 함수 → `UpdateSession` 호출,
  대상 로비·서버 정보·주소 정규화·온라인 광고 옵션·기존 파티 설정 보존을 검사한다.
  실제 인증 상태·백엔드 요청 중·미확정 포기도 운영 getter에서 검사한다.
- 로비 생성 중 도착한 준비 이벤트가 등록된 생성 완료 callback 뒤 게시되는지,
  pending Update 중 새 배정이 도착하면 이전 실패 완료 뒤 한 번 게시되는지 검사한다.
  생성 요청 자체는 실행하지 않는다.
- 동기 거절/비동기 실패와 OSS의 로컬 설정 변경 뒤 자동 반복 호출은 막으면서,
  기존 public 수동 재시도가 실제 모의 OSS 호출을 다시 만드는지 검사한다.
  파티·백엔드 멤버십과 오류 표시를 보존하고 성공 완료 뒤 중복 호출도 막는다.
- fixture 해제 시 GameInstance 초기화가 바꾼 전역 암호화 delegate 3개를 복원한다.
  모의 OSS에서 게시 외 예상하지 않은 작업도 없음을 확인한다.

최초 단일 컴파일의 pending operation 이름 오류는 `Refresh`로 수정했다.
로그: `Saved/Logs/SteamPublicationDispatch-Compile-20261011.log`.
게임 모듈 56개 작업의 컴파일/링크와 중간 DLL의 신규 3개 실행이 통과했고,
정리 코드 보완 뒤 공유 obj 2개의 접근 거절은
`Saved/Logs/SteamPublicationDispatch-Cleanup-ModuleLink-20261011.log`에 보존했다.
다른 빌드 프로세스가 없음을 확인한 후 5개 증분 작업의 링크가 성공했다.
최종 로그: `Saved/Logs/SteamPublicationDispatch-Cleanup-ModuleLink-Retry-20261011.log`.
전체 Editor 타깃 성공이나 공유 보스/스킬 변경의 검증으로 계산하지 않는다.

최종 검증 시점 DLL에서 신규 3개만 모두 통과했고 Ensure/Fatal/Assertion이 없었다.
실행 전후 관련 소스 9개와 DLL 해시도 같았다. 이전 전체 회귀와 인접 로비 검사는
반복하지 않았다. 경로·소스/DLL/로그·모듈 빌드 로그 해시는
[연결 검증 proof](../Saved/Tests/SteamLobbyPublication/Dispatch-74aff7f29fa14328870feda49531e658/SteamPublicationDispatch.proof.json),
[실행 로그](../Saved/Tests/SteamLobbyPublication/Dispatch-74aff7f29fa14328870feda49531e658/automation.log)에 있다.
이 단계는 실제 Steam 구현·두 계정 간 메타데이터 전파·HTTP·할당·네트워크·Shipping
검증이 아니다. 파티 생성 UI도 아직 별도다. 실제 Steam 2계정/서로 다른 PC에서의
전파 확인은 해당 환경이 준비된 뒤 진행한다.

## 설정과 개발 UI

```ini
[/Script/Project_RPG.RPGOnlineLobbySubsystem]
bAutoShowOnlineLobby=True
LobbyMapPackage=/Game/Maps/LobbyMap

[/Script/Project_RPG.RPGOnlineLobbyWidget]
DungeonId=Dungeon.RaidSwordMan
Difficulty=Hard
```

맵 변경 시 `RPGDungeonSessionSubsystem.LobbyMapPackage`도 같은 값으로 맞춘다.
현재 판정은 실제 맵 패키지 이름을 사용한다. PIE 접두사 월드는 별도 검증하지 않았으며
이번 검증은 독립 `-game` 프로세스를 사용한다.

개발용 로그인과 세션 GUID 참가 입력은 비 Shipping에서
`-RPGDevelopmentLogin -nosteam`을 둘 다 지정한 경우에만 생성한다.
로그인은 17자리 개발 SteamID를, 참가는 GUID를 받는다. 백엔드도 개발 인증을 허용해야 한다.
GUID는 참가 요청의 식별자이지 소유권/입장 티켓이 아니다.

## 검증 (2026-10-06)

Editor 증분 빌드: `Saved/Logs/OnlineLobbyBuild-20261006.log` 성공.
클린 빌드·Cook·Shipping 결과가 아니다.

- 던전 8종: `Saved/Tests/DungeonFlow/Automation-2c26a6efe842485abc3b4fddae56464d/automation.log`.
- 보스 10종: `Saved/Tests/BossState/Automation-743a6a37f6c248cb935beeb9585f4ad2/automation.log`.
- 보상 스냅샷 3종: `Saved/Tests/RewardSnapshot/Automation-77e398994ba44dd688e585667a97de40/automation.log`.

성공 마커 8+10+3개, 실패/Assertion/Ensure 0건을 별도로 재확인했다.
새 `ProjectRPG.Online.DungeonLobbyPolicy`는 소유 선택·멤버십·준비 상태·중복 입력·
알 수 없는 상태 잠금을 검사한다. `DungeonLobbyWidget`은 실제 위젯 트리/Slate,
버튼 바인딩·미인증 클릭 가드·해제/재생성·기본 개발 UI 제외를 검사한다.
격리 자동화만으로 HTTP/Steam/GPU 동작을 통과한 것은 아니다.

```powershell
& .\Scripts\Tests\Run-DungeonClientFlowAutomationTest.ps1
& .\Scripts\Network\Run-DungeonBossReturnE2ETest.ps1 -ClientCount 2 -LobbyUI -SkipPostgresStart -NoBackendRestore
```

`-SkipPostgresStart`는 개발 PostgreSQL이 이미 기동된 경우 사용한다.
UI E2E: `Saved/Tests/DungeonBossReturn/e4cf8876c60a4c0f9248f357af07547f/result.json` 성공.

1. 두 클라이언트가 실제 로비에서 개발 로그인 버튼을 실행하고 HTTP로 내 캐릭터 목록을 읽는다.
2. 실제 캐릭터 콤보 선택 → 활성 세션 조회 후 리더가 생성 버튼으로 `Waiting` 세션을 만든다.
3. 참가자는 개발 전용 GUID 참가 버튼으로 같은 세션에 들어간다.
4. 하네스가 관리자 권한으로 서버를 할당/기동한다. 최초 서버 틱·Iris와 백엔드
   `InProgress` 확인 후 클라이언트가 실제 입장 버튼을 실행한다.
5. 실제 보스 맵의 인증 입장 → 서버 GAS 처치 → 정상 Dead/Victory → PostgreSQL 정산 →
   살아 있는 결과 복귀 버튼 → 실제 로비 이동 및 새 UI의 선택 표시 복원을 확인한다.

각자 골드 500·인벤토리 수량 4(물약 3·투구 1)·우편 0을 받았다.
`LobbyUIVerified`, `WaitingSessionCreatedInUE`, `BackendPartyJoinFromUI`가 모두 true다.
클라이언트의 private 토큰/세션 상태를 주입하지 않는다. 공유 파일은 세션 GUID와 진행
장벽만 전달하며 입장/보상 권한은 기존 백엔드·전용 서버가 검증한다.

기본 Entry 경로도 같은 바이너리로 순차 회귀했다:
`Saved/Tests/DungeonBossReturn/7aad3c1d43834f1b86e174eb26124ec5/result.json` 성공.
두 실행의 서버/클라이언트 로그에서 네트워크 Error·Fatal/Assertion/Ensure·fixture 실패·
입장 우회 패턴 0건을 확인했고 테스트 프로세스와 `3027`/`18827` 포트는 종료됐다.
개발 DB·생성 데이터·로그와 다른 작업의 프로세스는 보존했다.

## 남은 작업과 검증 한계

- 실제 Steam 2계정/다른 PC의 로그인·파티 검색/초대/입장·재접속 검증.
  버튼은 기존 Steam API에 연결했지만 이번 실행은 Steam 전송을 사용하지 않았다.
- 운영 서버 할당/기동 서비스. UI에 관리자 토큰/activate 기능을 넣지 않았다.
  이번 할당과 서버 기동은 외부 테스트 하네스가 담당한다.
- 안전한 로비의 현재 토큰 로그아웃 이외의 계정 전환/강제 로그아웃과
  인증 서버 접속 불가/연결 단절의 강제 탈출 정책. 위 2026-10-18 구현과 구분한다.
  로비의 같은 계정 재인증은 아래 후속 작업 범위다.
  던전 입장 후 만료의 결과 화면 복구는 `DUNGEON_CLIENT_RETURN_FLOW.md`의 2026-10-08 항목을 따른다.
  2026-10-06 모드는 계정·캐릭터를 사전 생성한다. 캐릭터 생성 UI는 아래 후속 모드를 따른다.
- 자연 플레이어 공격/회피 기반 협동 전투와 패배 후 포기 정책.
  처치는 기존 Brain 정지/서버 GAS 치명타 fixture이며 자연 전투 검증이 아니다.
- GPU 배치·폰트·번역 수집·게임패드·Shipping·지연/손실 조건.
  NullRHI 실제 위젯/클릭 검증이지 시각 품질 검증이 아니다.
- 포화 우편·Claim·재시작은 기존 보상 E2E의 범위이며 이번에 재실행하지 않았다.
- 기존 FX/Blueprint 경고와 공유 DB 동시 DDL 초기화 교착은 별도 과제로 남는다.

기존 처치/결과/복귀 fixture: [DUNGEON_BOSS_RETURN_E2E.md](DUNGEON_BOSS_RETURN_E2E.md).

## 캐릭터 생성 UI 후속 작업 (2026-10-07)

이름을 trim한 뒤 UTF-16 길이 2~20과 C0/C1 제어문자 제외를 검사한다.
생성 요청/목록 재조회 중에는 이름 입력과 버튼을 잠그며, 성공하면 입력칸을 비우고
인증된 내 캐릭터 목록을 다시 읽는다. 생성은 미인증·요청 중·활성/알 수 없는 세션에서
허용하지 않는다. 빈 계정은 캐릭터 선택 없이 첫 캐릭터를 만들 수 있다.
새 이름 검증 자동화와 기존 로비 정책 검사를 보강했다.

```powershell
& .\Scripts\Network\Run-DungeonBossReturnE2ETest.ps1 -ClientCount 2 -LobbyUI -CreateCharactersInUI -SkipPostgresStart -NoBackendRestore
```

`-CreateCharactersInUI`는 `-LobbyUI`가 필요하다. 하네스는 개발 계정만 준비하고
빈 소유 목록을 확인한다. 클라이언트는 실제 생성 버튼으로 너무 짧은 이름·21자 이름·
내부 탭을 거절하고, 앞뒤 공백을 붙인 정상 이름으로 생성한다. 처리 중 실제 디스패처를
강제로 한 번 더 호출해 중복 요청 가드를 검사한다. 응답의 GUID로 재조회된 소유 목록을
확인하고 그 옵션을 선택한다. 캐릭터의 private 상태나 예상 GUID는 주입하지 않는다.

PowerShell은 응답 GUID를 확인하고 플레이어 권한으로 소유 목록의 캐릭터가 정확히
하나인지 재검사한다. 이후 같은 캐릭터로 세션 생성/참가·입장·보상·복귀를 검증한다.
결과에 `CharacterCreationInUIVerified`/`CharacterCreationGuardsVerified`를 기록하며,
기존 모드에서는 false다. 두 값이 Steam/자연 전투 검증을 뜻하지 않는다.

- 초기 증분 빌드: `Saved/Logs/OnlineLobbyCharacterBuild-20261007.log` 성공.
- 초기 2인 생성/전체 흐름: `Saved/Tests/DungeonBossReturn/2078c5f2ca8347e59e10b245da3c25ef/result.json` 성공.
  두 신규 캐릭터가 각자 골드 500·인벤토리 4·우편 0을 받고 새 로비 UI의 선택 표시까지 복구했다.
  서버/클라이언트 실패 경계 재검사 0건 및 테스트 포트 종료를 확인했다.
- 첫 하네스 실행 `Saved/Tests/DungeonBossReturn/97350321ab1342a6898c125ad3baa100/`는
  PowerShell 5의 빈 JSON 배열 파이프라인 표현을 1개 캐릭터로 잘못 해석해 UE 기동 전에
  중단됐다. 해당 계정의 DB 캐릭터 수 0을 확인하고 응답을 변수로 받아 배열화하도록
  수정했다. 이 실패 실행을 성공 근거로 사용하지 않으며 계정/로그는 보존했다.
- 첫 자동화 `Saved/Tests/DungeonFlow/Automation-dbacd4c378704c429f888a86a88e30d3/automation.log`는
  새 이름 검사에서 실패했다. `FString::InsertAt`이 NUL 삽입을 무시해 정상 `ab`를 잘못된
  입력으로 검사한 fixture 문제였다. 실제 제어문자를 가진 배열을 구성하고 길이 검사를
  추가했다. 첫 자동화 실행은 통과 근거가 아니다.
- fixture 수정 후 증분 빌드: `Saved/Logs/OnlineLobbyCharacterBuild-Verified-20261007.log` 성공.
- 최종 던전 자동화 9종: `Saved/Tests/DungeonFlow/Automation-230d4e1f8b1f4921bb789f57fed6db4e/automation.log` 성공.
  `DungeonLobbyCharacterName`은 길이 경계·한글·공백 정규화·NUL/C0/C1 제어문자 거절과
  실패 출력 초기화를 검사한다. NUL fixture의 실제 길이도 확인한다.
- 최종 보스 10종: `Saved/Tests/BossState/Automation-a968b0b0d3354d5cbeec3fa06eb97ed5/automation.log` 성공.
- 최종 보상 스냅샷 3종: `Saved/Tests/RewardSnapshot/Automation-583156457dbb4d2dbeca065000df20d1/automation.log` 성공.
  같은 바이너리의 성공 마커 9+10+3개와 실패/Assertion/Ensure 0건을 별도로 재확인했다.
- 최종 바이너리 4인 생성/전체 흐름:
  `Saved/Tests/DungeonBossReturn/0168ed5cac9c4075bbaee31c10100e18/result.json` 성공.
  네 계정 모두 내 목록에 캐릭터 하나만 생성됐고 같은 대기 세션에 참가했다.
  전원 인증 입장·각자 골드 500/인벤토리 4/우편 0·실제 복귀 버튼·로비와 선택 표시 복원 통과.
  Steam/자연 전투 검증 플래그는 false를 유지한다.
- 최종 바이너리 기본 Entry 2인 회귀:
  `Saved/Tests/DungeonBossReturn/bdcd637e124d4af19d086aa873a4639f/result.json` 성공.
  사전 생성된 캐릭터의 인증/세션 복원·입장·정산·실제 결과 버튼·로비 복귀를 유지한다.
  생성 UI 검증 플래그는 false이며 새 생성 모드와 구분한다.
- 최종 4인/기본 2인 실행의 모든 서버/클라이언트 로그를 재검사해 네트워크 Error·
  Fatal/Assertion/Ensure·fixture 실패·입장 우회 패턴 0건을 확인했다.
  테스트의 `3027`/`18827` 엔드포인트는 종료됐으며 DB·생성 데이터·실패 로그와
  다른 작업의 프로세스는 보존했다. 프로젝트 전체 에셋/Blueprint 경고 0건 판정은 아니다.

## 로비 인증 만료와 재로그인 (2026-10-07)

당시 백엔드 로그인 응답의 `accessToken`뿐 아니라 `steamId`와 `expiresAt`도 검증했다.
이 단계의 만료 시각은 UTC로 해석하며 현재 시각이 만료 시각 이상이면 로그인되지 않은 것으로
판정한다. 잘못된 토큰/계정 ID, 시간대가 없는 시각, 이미 만료된 응답은 수락하지 않는다.
현재 플레이어 자격 증명으로 보낸 요청의 HTTP 401도 인증을 해제한다.
403·통신 실패는 인증을 해제하지 않으며 전용 서버의 서비스 자격 증명과 분리한다.

2026-10-08부터는 `issuedAt`을 추가하고 서버 상대 유효 기간과 단조 증가 deadline을
사용한다. 현재 시계 오차/변경 계약은 [PLAYER_AUTHENTICATION_CLOCK.md](PLAYER_AUTHENTICATION_CLOCK.md)를
따른다. 아래 UTC 비교 및 검증 기록은 2026-10-07 단계의 이력이다.

만료는 계정 전환이나 던전 탈퇴가 아니다. SteamID·선택 캐릭터·현재 세션은 유지하며,
로비는 같은 계정으로 다시 로그인하도록 안내한다. 개발 ID 입력도 기존 계정으로 고정한다.
로그인 성공 후 내 캐릭터 목록을 다시 읽고 선택 표시를 복구한다. 세션 새로고침/입장은
기존 UI와 백엔드 권한 검증을 따른다. 다른 계정의 성공 응답으로 기존 상태를 섞지 않는다.
이 단계에서는 로그아웃/계정 전환을 제공하지 않았다. 2026-10-18의 안전한 로비 로그아웃은
위 후속 계약을 따르며, 인증 만료만으로 계정이 바뀌는 것은 여전히 허용하지 않는다.

중복 로그인 호출은 진행 중 요청을 해제하거나 실패 완료시키지 않는다. 이전 로그인
요청의 늦은 응답은 무시하며, 이전 토큰의 401/성공 응답도 새 인증 상태에 적용하지 않는다.
401과 겹친 현재 토큰의 다른 요청은 실패 완료시켜 UI 잠금을 풀되 데이터를 적용하지 않는다.
요청 검사와 헤더 작성 사이에 만료된 무인증 요청도 실패 완료한다. 재로그인 후 도착한
이전 무인증 응답으로 새 인증을 해제하지 않는다.
토큰/요청 상관 정보는 메모리에만 두며 설정/로그에 저장하지 않는다.
기존 `SetAccessToken` 호환 API는 만료 메타데이터가 없으므로 정상 로그인 경로로 사용하지 않는다.

```powershell
& .\Scripts\Network\Run-DungeonBossReturnE2ETest.ps1 -ClientCount 2 -LobbyUI -CreateCharactersInUI -ReauthenticateInLobby -SkipPostgresStart -NoBackendRestore
```

`-ReauthenticateInLobby`는 `-LobbyUI` 및 기본 로컬 개발 PostgreSQL 컨테이너/연결만 허용한다.
하네스는 이번 실행에서 생성한 고유 개발 계정의 `auth_sessions.expires_at`만 과거로 바꾼다.
기존 대기 파티를 그대로 두고 실제 세션 조회의 HTTP 401 → 로그인 버튼 재실행 →
소유 목록/선택/세션 복원을 확인한 뒤 서버를 할당한다. 이후 새 티켓 입장·처치·보상·
결과 버튼·실제 로비 복귀까지 검증한다. UE에 토큰이나 private 인증 상태를 주입하지 않는다.
결과의 `BackendAuthenticationExpiryVerified`/`LobbyReauthenticationVerified`는 기존 모드에서 false다.

로컬 시각 만료 경계·잘못된 메타데이터·다른 계정 거절·403 보존·이전 응답 무시·
동시 요청 실패 완료·중복 로그인 잠금은 새 `DungeonAuthentication` 및
`DungeonAuthenticationTransport` 자동화로 검사한다. DB 만료/실제 HTTP 401 경로와 구분한다.
실제 Steam 재인증, 60분을 자연 경과시키는 실행, 클라이언트 시계 오차/변경 보정, 던전 플레이 중 재인증,
로그아웃/계정 전환·Shipping·GPU 검증은 이 범위에 포함하지 않는다.

검증 기록:

- 최초 빌드 `Saved/Logs/OnlineLobbyAuthenticationBuild-20261007.log`는 새 테스트의
  존재하지 않는 `FTimespan::FromTicks` 호출로 실패했다. ticks 생성자로 수정했다.
- 수정 증분 빌드 `Saved/Logs/OnlineLobbyAuthenticationBuild-Verified-20261007.log` 성공.
- 첫 던전 자동화 `Saved/Tests/DungeonFlow/Automation-588631c46138447d8b908c41e2f4f236/automation.log`는
  10개 성공/1개 실패다. 새 transport fixture의 서브시스템을 GameInstance 밖에 만들어
  `ClassWithin` Ensure가 발생했다. 실제 GameInstance를 Outer로 사용하도록 fixture만 수정했다.
  첫 실행/빌드 실패를 통과 근거로 쓰지 않으며 로그는 보존했다.
- fixture 수정 후 증분 빌드 `Saved/Logs/OnlineLobbyAuthenticationBuild-TestFixture-20261007.log` 성공.
- fixture 수정 후 던전 11종: `Saved/Tests/DungeonFlow/Automation-27fad5a9f1c249c18066e3ee37b1979a/automation.log` 성공.
- 같은 바이너리 보스 10종: `Saved/Tests/BossState/Automation-69797f3643cf468da74ce6e68cc59f76/automation.log` 성공.
- 같은 바이너리 보상 스냅샷 3종: `Saved/Tests/RewardSnapshot/Automation-e56d44c3dc5c47ccb0eefcc246bcbcd1/automation.log` 성공.
  성공 마커 11+10+3개, 실패/Fatal/Assertion/Ensure 0건을 별도로 확인했다.
- 2인 생성·인증 만료·재로그인·입장/보상/복귀:
  `Saved/Tests/DungeonBossReturn/0fb4e478619f438faffe60169fc0dfcf/result.json` 성공.
  두 클라이언트 모두 실제 HTTP 401과 재로그인/소유 목록/세션 보존 마커가 확인됐다.
  전원 새 티켓으로 입장해 각자 골드 500·인벤토리 4·우편 0을 받고 로비로 돌아왔다.
  서버/클라이언트의 네트워크 Error·Fatal/Assertion/Ensure·fixture 실패·입장 우회 0건이다.
- 초기 4인 생성·재인증/전체 흐름:
  `Saved/Tests/DungeonBossReturn/17b0abf55d0841bbbe065da69e1c25e4/result.json` 성공.
  네 계정 모두 실제 401/재로그인·기존 선택과 세션 보존·입장/보상/복귀를 통과했다.
- 최종 보완은 검사/헤더 작성 사이의 만료가 무인증 요청을 만들었을 때 실패 완료하도록
  처리하고, 재로그인 뒤 이전 무인증 응답은 무시하는 것이다. transport 자동화도 추가했다.
- 보완 재빌드 `Saved/Logs/OnlineLobbyAuthenticationBuild-Final-20261007.log`는 별도 진행된
  `RPGGameplayAbility_SkillContainer.cpp`의 `GetNetMode` 호출에서 실패했다. 확인 시 이미
  `Source->GetNetMode`로 수정돼 있었으며 이 작업에서 해당 파일을 추가 수정하지 않았다.
- 갱신된 작업공간 최종 증분 빌드:
  `Saved/Logs/OnlineLobbyAuthenticationBuild-FinalVerified-20261007.log` 성공.
- 최종 던전 11종: `Saved/Tests/DungeonFlow/Automation-9e7b860ba1884b0695a2bc02d9c7e347/automation.log` 성공.
- 최종 보스 10종: `Saved/Tests/BossState/Automation-c19697e4d81e4046bbf95b0f7b3f68ac/automation.log` 성공.
- 최종 보상 스냅샷 3종: `Saved/Tests/RewardSnapshot/Automation-9c91e9b0e0e8435f928d0ef4a4ea9bf2/automation.log` 성공.
  같은 최종 바이너리의 성공 마커 24개, 실패/Fatal/Assertion/Ensure 0건을 재확인했다.
- 최종 바이너리 4인 생성·인증 만료/재로그인·전체 흐름:
  `Saved/Tests/DungeonBossReturn/d68d65fa6ae3492c8e6103e502bd3cb1/result.json` 성공.
  네 클라이언트 모두 실제 401·같은 계정 재로그인·소유 목록/선택/세션 보존을 확인했다.
  전원 새 티켓 입장·각자 골드 500/인벤토리 4/우편 0·실제 결과 버튼/로비 복귀 통과.
- 최종 바이너리 기본 Entry 2인 회귀:
  `Saved/Tests/DungeonBossReturn/2d35ceb4a72248dc82c3f1e68e287e3f/result.json` 성공.
  기존 인증/활성 세션 복원·입장·정산·결과 버튼·실제 로비 복귀가 유지됐다.
  재인증/캐릭터 생성 UI 검증 플래그는 false이며 새 모드의 증거와 구분한다.
- 위 2인·초기/최종 4인·기본 Entry 실행의 서버/클라이언트 로그를 모두 재검사했다.
  네트워크 Error·Fatal/Assertion/Ensure·fixture 실패·입장 우회 0건이며,
  테스트의 `3027`/`18827` 엔드포인트와 소유 프로세스는 종료됐다.
  개발 DB·생성 데이터·로그와 다른 작업의 프로세스는 보존했다.
  전체 에셋/Blueprint 경고 0건이나 클린 빌드/Cook/Shipping 통과를 뜻하지 않는다.

## 결과/로비 입력 경로와 초기 목록 조회 (2026-10-21)

전역 `DefaultEngine.ini`의 `GameViewportClientClassName`을 엔진의
`/Script/CommonUI.CommonGameViewportClient`로 지정했다. 테스트만의 viewport override나
빈 프로젝트 하위 클래스는 추가하지 않는다. `-RPGRequireIris`는 실제 네트워크 월드에서
계속 검사하되, 같은 프로세스가 standalone 로비로 돌아온 경우에는 드라이버 부재를 오류로
판정하지 않는다.

로비 생성 시 다른 Flow 요청(예: 도착 직후 포기 POST)이 진행 중이면 초기 캐릭터 GET을
보류했다가 기존 2초 폴링에서 인증·로컬 로비·요청 잠금을 다시 검사해 한 번 시작한다.
시작 전에 보류 표시를 소비하므로 실패/빈 목록을 자동 반복하지 않는다. 이후 재조회는
기존 Reload 버튼/로그인 경로를 사용한다. 화면 해제와 성공한 로그아웃은 보류를 해제한다.
포기 POST·신원 고정·보상·기본/Shipping 노출 가드와 BP 에셋은 바꾸지 않았다.

검증: `Run-DungeonBossReturnE2ETest.ps1 -VerifyResultAbandonment
-VerifyResultAbandonmentResponseLoss -VerifyResultAbandonmentReauthentication -SlatePointerInput`.
클라이언트는 기존 개발 DDC를 사용하는 GPU offscreen 창, 서버는 기존 nullrhi 경로다.
실제 창의 hit-test grid에 버튼이 포함되는지 확인하고 Slate move/down/up으로 클릭한다.
클립된 로비 행은 ScrollBox로 먼저 드러낸다. 취소/확인·남은 파티 재도전/정산/복귀·응답 유실
후 실제 401/로그인/동일 영수증 복구의 클릭 11회와 HTTP/PostgreSQL 단언을 통과했다.
강제 중복 디스패처는 별도 부정 검사로 남긴다. 선택 입력 20개/DLL 불변, CommonUI 오류·
Iris 오진단·Fatal/Assert/Ensure 0건이며 클라이언트별 예정된 ConnectionLost 1건은 구분한다.
proof: `Saved/Tests/OnlinePointer-522b15b1/Verification.proof.json`.

물리 마우스·OS 이벤트·키보드/패드 라우팅, 시각 품질, Steam, 재시작, Shipping의 증거는
아니다. 최초 별도 DDC의 셰이더 준비 timeout, nullrhi에서 Slate 그리기를 강제한 엔진 단언,
수정 전 초기 목록 대기 중단은 보존했다. nullrhi Slate 강제 모드는 최종 코드에서 제거했다.

## 포기 확인창의 키보드/패드 탐색 (2026-10-21)

결과창과 로비의 기존 포기 확인 UI를 열면 ‘취소’ 버튼에 로컬 사용자 포커스를 둔다.
6방향(상하좌우·Next/Previous) 탐색은 확인/취소 사이를 순환하고, 로비 ScrollBox는
포커스된 행을 즉시 드러낸다. 열기/명시적 취소 때만 포커스를 변경하므로 폴링이 사용자의
선택을 반복해서 빼앗지 않는다. 기존 네이티브 화면에 적용되며 BP 에셋/바인딩은 바꾸지 않는다.

확인창의 preview key 경로에서 해당 Slate 사용자의 플랫폼 Back 매핑을 처리한다.
Esc/패드 Back은 창의 동의만 닫고 열기 버튼으로 포커스를 돌린다. 반복 Back은 소비하되
새 창을 닫지 않으며, 창이 닫힌 상태의 입력은 기존 상위 동작으로 전달한다. Enter/accept를
확정 요청으로 가로채지 않고 포커스된 버튼의 기존 클릭 동작을 사용한다. 이미 제출/대기 중인
POST나 미확정 신원을 취소·초기화하지 않는다. 신원·Revision·로비 전용 POST·기본/Shipping
노출 제한과 서버/백엔드 권한 검사는 그대로다. 다른 창 전체의 모달 입력 잠금은 추가하지 않았다.

검증은 이전 GPU runner 옵션에 `-ConfirmationNavigation`을 추가한다(`-SlatePointerInput`
필수). 테스트 측 포커스 대입 없이 실제 Open 클릭부터 Tab/Shift+Tab·패드 D-pad·반복 Back·
새 Back 이벤트를 Slate에 전달하고 확인/취소 사이 이동, 취소 후 포커스와 세션 보존, 재열기의
안전 포커스를 검사한다. 결과창 1회·로비 2회, 합성 포인터 14회와 2인 HTTP/PostgreSQL의
응답 유실·남은 파티 재도전/정산·실제 401/재로그인·동일 영수증 복구가 통과했다.
자체 Editor 타깃 84개 작업 성공 뒤 공유 DLL이 갱신돼 구형 native 성공과 구분했다.
집중 native 확인창 2개를 최신 DLL에서 확인했으며 최종 native/네트워크가 같은 DLL을 사용했다.
프록시 4개는 별도 Node 검사다.
proof: `Saved/Tests/ConfirmationInput-a2f66f51/Verification.proof.json`.

실제 전송에서 자동 재시도가 0건일 수 있으므로 runner는 그 발생 자체를 요구하지 않는다.
수동 release 전 추가 전달 0·최초 커밋/200 유실 각 1·최종 동일 영수증 1·401 1 조건은 유지한다.
강제로 재전송했을 때의 hold 차단은 프록시 단위 검사에서 별도로 검증하고, 실패 스냅샷도
단언 전에 저장한다. 앞선 프록시 상태 단언 실패 2개를 보존했으며 최종 실행은 자동 재전송 0건이다.

실물 키보드/패드·OS 입력, 아날로그 스틱, 번역/시각 품질, Steam·다중 PC·Shipping 검증은
아니다. 기존 native 기동 오류와 테스트 결과를 구분하며 전체 프로젝트 오류 0을 주장하지 않는다.

## 로비/결과 화면의 기본 포커스 (2026-10-21)

기존 PC/BP의 화면 루트 포커스 호출은 엔진 `DesiredFocusWidget`을 통해 현재 사용할 수
있는 자식 컨트롤로 연결한다. 결과창은 복귀·재도전·조회·재로그인 순서의 활성 후보,
로비는 개발 로그인 입력/로그인·내 캐릭터·이름 입력·입장/생성·조회 순서의 활성 후보를 쓴다.
포기·확정 포기·연결 종료·로그아웃은 기본 후보에 넣지 않는다. 포기 확인창 안에서는 기존
‘취소’ 기본 포커스를 유지한다. 이것은 선택 정책이지 작업 실행이나 권한 승인 경로가 아니다.

화면 자신과 후보/조상이 활성·표시 상태이고, 키보드 포커스를 지원하며 해당 화면의
WidgetTree 소속이어야 한다. 후보가 없으면 `NAME_None`으로 이전 기본값을 지운다.
목록/세션 데이터가 늦게 도착해도 **로컬 소유자의 포커스가 이 화면 루트에 그대로 남았을 때만**
새 후보로 이동한다. 이미 고른 자식 버튼·텍스트 입력·다른 창·게임플레이의 포커스는 빼앗지
않는다. 기존 자식 포커스가 사라진 모든 경우를 강제로 복구하는 전역 포커스 관리자는 아니다.

클릭/키 입력 없이 로그인·캐릭터 선택·입장·포기를 자동 실행하지 않으며 기존 소유권·인증·
서버 정산·미확정 요청·기본/Shipping 노출 가드는 바꾸지 않았다. BP 에셋 수정이나 새 입력
모드/타이머 없이 기존 화면의 presentation 갱신과 엔진 포커스 전달 경로를 사용한다.

검증: Editor 타깃 62개 작업 및 최종 증분 확인 성공. 기본 후보 선택/상태 변경 native 2개와
기존 확인창 native 2개가 통과했다. 같은 DLL의 기존 GPU offscreen 2인 runner를
`-SlatePointerInput -ConfirmationNavigation -VerifyResultAbandonment
-VerifyResultAbandonmentResponseLoss -VerifyResultAbandonmentReauthentication`으로 실행해
기본 후보가 유효하고 실제 자식 컨트롤에 포커스가 있는지 클릭 전 확인했다. 테스트의 포커스
재대입 없이 같은 갱신 함수를 호출해 기존 선택 보존도 검사했다. 14곳 검사/클릭, 확인창 탐색
3회와 남은 파티 재도전·정산/복귀·응답 유실 후 401/재로그인/동일 영수증 복구가 통과했다.
선택 입력 22개/native 8개와 DLL 일치, 최종 UE 로그 Fatal/Assert/Ensure 0건이다.
proof: `Saved/Tests/MenuFocus-82e93d1f/Verification.proof.json`.

최초 native 실행은 테스트 시작 전 180초 기동 timeout으로 미완료다. 최초 통합 실행은
UI 검사에 도달하기 전 `ResumeIdentityOrStateMismatch`로 중단됐다. 종료 후 해당 실행의
개발 세션이 Closed인 것을 조회했지만, 이것만으로 실패 전 종료 시점이나 원인을 확정하지
않았다. 둘 다 보존했고 lease/인증 만료·서버 가드를 바꾸지 않은 재실행이 통과했다.
기존 native 기동 오류와 테스트 오류를 구분한다. 실물 입력·OS 이벤트·시각 품질·Steam·
Shipping·다중 PC, 모든 종류의 포커스 상실 자동 복구를 완료했다는 증거는 아니다.

## 정의 에셋 기반 던전 목록과 난이도 선택 (2026-10-23)

`URPGDungeonDefinition`은 새 세션 선택에 필요한 정확한 `DungeonId`, 번역 가능한
`DisplayName`, `Difficulties`, `DefaultDifficulty`를 갖는 Primary Data Asset이다.
공통 로비/인증/세션 생성 코드를 던전마다 복사하지 않고 정의 목록을 교체한다.
이번 단계의 소비자는 로비이며, 이 에셋을 서버 할당·입장 조건·보상 권한으로 취급하지 않는다.

에디터에서 위 클래스의 Data Asset을 만들고 각 던전의 ID/이름/지원 난이도/기본 난이도를
작성한 뒤 `RPGOnlineLobbyWidget`의 config 배열 `DungeonDefinitions`에 등록한다.
아래는 작성 예시이며, 해당 이름의 에셋을 자동 생성하거나 실제 새 콘텐츠로 추가한 것은 아니다.

```ini
[/Script/Project_RPG.RPGOnlineLobbyWidget]
+DungeonDefinitions=/Game/System/Dungeons/DA_SwordRaid.DA_SwordRaid
+DungeonDefinitions=/Game/System/Dungeons/DA_Forest.DA_Forest
DungeonId=Dungeon.RaidSwordMan
Difficulty=Hard
```

Config의 soft reference만으로 패키징 포함을 보장하지 않는다. 해당 경로에 실제 에셋을
작성하고 사용할 때는 기존 Asset Manager 설정에 다음 스캔/cook 규칙도 등록한다.
이 예시를 실제 패키징 검증 완료로 계산하지 않는다.

```ini
[/Script/Engine.AssetManagerSettings]
+PrimaryAssetTypesToScan=(PrimaryAssetType="RPGDungeonDefinition",AssetBaseClass="/Script/Project_RPG.RPGDungeonDefinition",bHasBlueprintClasses=False,bIsEditorOnly=False,Directories=((Path="/Game/System/Dungeons")),SpecificAssets=,Rules=(Priority=-1,ChunkId=-1,bApplyRecursively=True,CookRule=AlwaysCook))
```

동작과 경계:

- 목록을 비워 두면 기존 `DungeonId`와 네 난이도를 그대로 사용한다. 기존 Config/BP/세션
  프로토콜은 바꾸지 않으며 기본 프로젝트에 가상의 새 던전을 등록하지 않았다.
- 목록을 설정했다면 최대 64개, 모든 참조의 로드·정의 검증·정확한 ID의 유일성이 필요하다.
  잘못된 ID/빈 이름/빈·중복·알 수 없는 난이도/지원하지 않는 기본 난이도/누락 에셋 중 하나라도
  있으면 전체 새 생성 선택을 막는다. 조용히 기본 던전으로 되돌리거나 잘못된 행만 건너뛰지 않는다.
- `DungeonSelectionOptions` 콤보는 이름과 ID를 함께 보여 주고 ID로 구별한다. 난이도 콤보는
  현재 던전의 지원 목록을 사용한다. 사용자 던전 선택 후 기존 난이도가 새 정의에서도 유효하면
  유지하고, 아니면 **그 명시적 선택 시점에만** 정의의 기본 난이도를 사용한다. 로드/갱신 중에
  잘못된 Config 선택을 임의로 교정하거나 HTTP/이동을 시작하지 않는다.
- 선택과 생성 모두 기존 로컬 로비/인증/소유 캐릭터/요청·이동/Steam 파티/미확정 포기 가드를
  다시 검사한다. 강제 콤보 이벤트나 BP 호출도 가드를 우회하지 못한다.
- 기존 미완료/알 수 없는 상태의 세션은 백엔드의 던전 ID·난이도가 우선이다. 해당 ID가 현재
  로컬 목록에 없거나 목록이 잘못됐어도 읽기 전용 표시·기존 복원 경로를 보존한다. 새 선택으로
  덮어쓰지 않으며 완료 후에만 기존 새 세션 선호도를 다시 사용한다.
- 일반 갱신은 콤보의 옵션이 바뀐 경우에만 목록을 재구성해 열린 드롭다운/포커스를 불필요하게
  초기화하지 않는다. 새 콤보의 이벤트는 위젯 해제 시 제거하고 재생성 시 다시 바인딩한다.

커스텀 UI는 `GetDungeonOptions`의 **ID 목록**과 `GetDungeonDisplayName`으로 표시하고,
`GetSelectedDungeonId`의 bool 결과를 확인한다. `CanSelectDungeon`/`SelectDungeon`으로
변경하며 난이도는 `GetAvailableDungeonDifficultyOptions`를 사용한다. 기존 static
`GetDungeonDifficultyOptions`는 네 값의 공통 codec/호환 API이므로 던전별 제한 목록으로
오해하지 않는다. 생성은 기존 `CreateSelectedDungeonSession`을 사용한다.

실제 선택된 ID/난이도만 기존 생성 요청에 전달한다. 맵 URL·서버 주소·보상 manifest나
입장 인원을 클라이언트 에셋에서 새로 보내지 않는다. 다음 연결 단위는 서버가 검증하는
콘텐츠 정의와 맵/배정·보상 설정의 대응이다. 보스 AI나 새 콘텐츠의 제작 완료는 아니다.

직접 영향 검증은 `Scripts/Tests/Run-DungeonSelectionAutomationTest.ps1`로 신규 정의/
목록/실제 위젯 이벤트/BP 계약과 기존 로비 5개를 선택한다. 결과와 한계는 아래에 기록한다.

검증: 신규 4개와 기존 5개 **native 9개 Success·러너 exit 0**.
`Saved/Tests/DungeonSelection/650b86731c5148a093a287abf780676e/Verification.proof.json`.
선택 입력 13개·실행 DLL·닫힌 로그 해시가 실행/사후 검사에서 일치했다. 테스트 시작 전
기존 기동 Error 15줄, 시작 후 Error 0·Fatal/Assert/Ensure 0이므로 로그 전체 오류 0으로
표현하지 않는다. 이 검사는 메모리 정의와 실제 네이티브 위젯/강제 이벤트의 격리 검사다.
작성된 디스크 에셋의 로드·인증된 새 ID 생성 HTTP/DB·맵 배정·Steam·GPU·Shipping은 별도다.

초기 25개 작업 빌드에서 신규 정의/선택/위젯/테스트는 컴파일됐지만, 동시 변경된 별도 입력
플러그인의 새 헤더 참조로 전체 타깃은 실패했다. 최신 의존성이 반영된 후에는 헤더 전용
`PROJECT_RPG_API` 인터페이스의 생성자/이름 함수 import 심볼 두 개에서 링크가 실패했다.
입력 동작/등록·조회 계약은 유지하고 `IRPGAdditionalInputProvider` 생성자와 기존 이름 함수의
같은 구현만 별도 `.cpp`의 내보내기 정의로 분리했다. 추가 파일이 이전 빌드 목록에 없던 실패도
보존했으며, 공유 게임/플러그인 DLL 갱신 뒤 자체 `-NoUBTMakefiles` 전체 타깃 확인은
**0개 작업/up-to-date/Success**였다. 이를 자체 전체 DLL 링크 실행으로 바꾸어 기록하지 않는다.
로그는 `Saved/Logs/DungeonSelectionBuild{,-final,-linked,-gather}-20261023.log`에 남겼다.

맵/Blueprint/실제 콘텐츠 에셋·기본 Config·백엔드 API/DB는 바꾸지 않았다. 가상의 새 던전을
기본 목록에 넣지 않았고 기존 단일 던전 동작을 유지한다. 소유 테스트 프로세스만 종료했으며
다른 에디터/작업은 유지했다. `project-rpg-online`에 따라 로컬 선택과 서버의 입장·보상 권한,
기존 활성/미확정 세션을 분리했고 광역 테스트·과거 2인 정산/10분 유예는 반복하지 않았다.

## 기본 정의 에셋의 실제 등록과 2인 로비 생성 (2026-10-23)

`/Game/System/Dungeons/DA_Dungeon_RaidSwordMan`을 실제 저장하고 기본 로비의
`DungeonDefinitions`에 연결했다. 표시 이름은 `검사 레이드`, ID는 기존
`Dungeon.RaidSwordMan`, 난이도 네 값과 Hard 기본값은 그대로다. 가상의 새 던전이나
새 맵/보스를 추가한 것이 아니다. Asset Manager에 이 디렉터리의 `RPGDungeonDefinition`
스캔·AlwaysCook 규칙을 등록했지만 실제 Shipping cook/패키징 성공을 뜻하지 않는다.

`Scripts/Editor/CreateDungeonDefinitionAssets.py`는 이 기본 에셋만 누락 시 생성한다.
기존 에셋이 있으면 타입/ID/이름·기존 네 난이도/Hard 기본값을 검사하고 저장하지 않는다.
불일치·타깃 미저장 변경은 덮어쓰지 않고 중단한다. 다른 맵/BP/보상·더티 패키지나 Config를
일괄 저장하지 않는다. 실제 생성 proof:
`Saved/Tests/DungeonDefinitionSetup/d454b554fde44087a0b656e9e65ec0a4/Setup.proof.json`.

직접 영향 검증:

- 자체 Editor 증분 **5개 작업/타깃·lib/DLL 링크 성공**
  (`Saved/Logs/DungeonDefinitionRegisteredBuild-20261023.log`).
- `Run-DungeonSelectionAutomationTest.ps1 -RegisteredAssetOnly`의 신규 실제 디스크 정의/
  Config/Asset Manager 등록·cook 규칙/네이티브 위젯 로드와 기존 위젯·세션 표시 **3개 Success,
  러너 exit 0**. 정의/캐시를 테스트에서 주입하지 않고 운영 `NativeConstruct`의 soft reference
  로더가 저장된 객체를 읽었음을 확인했다. 입력 16개/DLL·닫힌 로그 일치. 기존 기동 Error
  15줄·검사 시작 후 Error 0이며 광역 회귀나 이전 9개의 현 DLL 재실행으로 계산하지 않는다.
  `Saved/Tests/DungeonSelection/77872085e3c74d639580c41f24764912/Verification.proof.json`.
- 기존 로비 통합 러너의 `-LobbyUI -LobbyDifficultyOnly -VerifyRegisteredDungeonDefinition
  -LobbyDifficulty Normal -ClientCount 2 -Port 18873 -BackendUrl http://127.0.0.1:3073
  -SkipPostgresStart -NoBackendRestore -TimeoutSeconds 600`으로 실제 개발 인증의 두 로비에서
  저장된 정의의 이름/목록→공개 선택 API→기존 생성 버튼/HTTP→두 번째 캐릭터 참가→서버
  던전 ID/난이도 표시와 변경 차단까지 **러너 exit 0**. 잘못된 옵션 조합 2개는 기동 전 거절했다.
  `Saved/Tests/DungeonBossReturn/b74240a792ac4bea9e60bca68dff8dd4/result.json`.
- 종료된 소유 프로세스의 로그 4개와 입력 27개·DLL이 일치했다
  (`registered-definition.proof.json`). 독립 읽기 전용 SQL도 동일 세션의
  `Dungeon.RaidSwordMan / Normal / Waiting`, 멤버 정확히 2명·배정 서버 없음·정산 0을 확인했다
  (`registered-definition-durable.proof.json`). 두 클라이언트/백엔드 로그 Error 및
  Fatal/Assert/Ensure/흐름 실패 0, 실행별 백엔드 빌드 경고/오류 0이다.

이번 통합은 기존 ID의 정의 기반 세션 생성/참가이며 **새 콘텐츠 ID·전용 서버 배정·입장/
전투/보상/복귀·Steam·실물 입력/GPU·Shipping**은 검증하지 않았다. 서버의 현재 세션/난이도
검증·맵/BP 보상 설정은 바꾸지 않았고, 다음은 서버가 검증하는 던전 ID→맵/배정·보상 정의의
대응이다. 로컬 정의를 입장 허가나 보상 manifest로 신뢰하지 않는다.
`project-rpg-online`의 권한 분리를 유지했으며 DB 초기화/삭제 없이 새 테스트 계정·캐릭터·
대기 세션과 로그를 보존했다. 소유 프로세스만 종료하고 기존 DB 서비스/다른 작업은 유지했다.

## 로비 생성에서 자동 할당·실제 2인 입장까지 (2026-10-24)

`Run-LocalDungeonAllocatorStartupTest.ps1 -LobbyEntry`가 실제 로비의 기존 버튼 경로를
할당기와 연결한다. 계정/캐릭터만 미리 준비하고 첫 클라이언트가 로비의 생성 버튼으로
세션을 만든다. 두 번째 클라이언트는 같은 ID의 참가 버튼을 사용한다. 파티가 실제
`Dungeon.RaidSwordMan / Normal / Waiting`에 모인 뒤 supervisor가 claim하고, 카탈로그의
맵으로 UE 서버를 실행한다. 하네스가 activate/start를 대신 호출하거나 서버 토큰·맵 주소를
클라이언트에 전달하지 않는다. 클라이언트 자식 환경에서도 관리자·게임 서버 자격을 제거한다.

실제 서버 준비 뒤 파일 장벽은 테스트 버튼을 누를 시점만 알린다. 입장 허가가 아니며 기존
로비의 최신 세션/신원·요청 잠금, 새 Join Ticket 발급, 서버의 소비/멤버십·접속 세대·신원
확인 경로를 그대로 사용한다. 두 클라이언트가 실제 bossmap에 이동해 각자의 세션·캐릭터·
SteamID 복제, Iris와 Pawn/ASC/input 및 인벤토리 투영 Ready를 확인해야 입장 완료다.

새 `RPGFlowE2EAdmissionOnly`는 non-Shipping opt-in 클라이언트 probe에서만 이 지점에
관측을 끝낸다. 실제 로비/보스 맵을 요구하고 전투 결과·보상·복귀를 자동 수행하지 않는다.
서버에는 이 probe나 강제 처치/Brain 중지·전투 상태 주입을 켜지 않았다.

로컬 no-Steam 검증을 위해 할당기/launcher의 명시적 `-DevelopmentIdentityTesting`은
기존 non-Shipping Steam UniqueId 비교 예외 두 플래그만 전달한다. 정확한 EditorProject와
literal loopback 백엔드/공개 호스트가 필요하고, 실제 게임 소켓도 `-MULTIHOME`으로
루프백에 바인딩한다. 기본 Editor/패키지 Server에 예외를 자동 적용하지 않으며, 패키지
프로파일/원격 주소의 사용은 배정 전에 거절한다. 재사용 시 이 옵션이 기존 상태와 달라도
기존 프로세스를 보존하며 거절한다. 전체 `RPGNetTestMode` 입장 우회는 켜지 않는다.
이는 실제 Steam 인증·플랫폼 신원 일치 검증의 대체 구현이 아니다.

```powershell
Scripts/Network/Run-LocalDungeonAllocatorStartupTest.ps1 `
  -LobbyEntry -ClientCount 2 -Port 18886 -TimeoutSeconds 300
```

최종 `Saved/Tests/LocalDungeonAllocator/fc6ba9de6dcd418cac6ce2e6f5a04736/`의
`Verification.proof.json`과 `RuntimeLogAudit.proof.json`이 runner exit 0이다. 실제 로비
생성/참가→자동 claim/서버 준비→입장 버튼/새 티켓→정확한 신원의 두 클라이언트 완료,
서버 Iris Connected=2와 실제 `127.0.0.1:18886` 소켓 바인딩을 확인했다. 같은 서버 재사용과
잘못된 맵 요청 뒤 파티 보존도 통과했다. 이전 중간 실행의 0.0.0.0 바인딩을 관측해 위
루프백 제한을 추가했고, 중간 proof는 그 당시의 결과로 보존했다.

자체 Editor 4개 작업(클라이언트 probe TU/lib/DLL/metadata)·전체 타깃 Success,
격리 백엔드 Release 경고/오류 0, 모의 할당기 13종/readiness 7종과 스크립트 파싱을 통과했다.
선택 입력 32개/Project DLL·닫힌 로그 15개(실제 서버/클라이언트/백엔드 5개 포함)가 일치했고
실제 로그 5개의 Error/fail/Unhandled 및 Fatal/Assert/Ensure는 0이다. 자기 클라이언트·서버·
Memory 백엔드만 종료했으며 기존 DB/에셋/에디터는 유지했다. PostgreSQL·자연 전투/정산·
결과 복귀·4인·물리/Slate 입력·Steam/다른 PC/Shipping 성공은 아니며 기존 광역 E2E는
반복하지 않았다.

다음 기능 경계는 **모집 중과 출발 요청의 분리**다. 현재 저장소는 Waiting에서만 신규
멤버 참가를 허용하고 일반 claim은 즉시 Loading으로 바꾼다. 이번 연결은 참가 완료 뒤
supervisor를 실행한 경우이며, 상시 실행 중인 할당기가 아직 모집 중인 방을 먼저 가져가는
문제를 해결했다고 계산하지 않는다. 출발 의사/권한·취소/최소 인원/재시도 정책을 별도
상태로 연결한 뒤 상시 자동 할당을 열어야 한다. 클라이언트에 관리자 claim 권한을 주거나
Loading 중 무조건 참가를 허용해 이 경계를 우회하지 않는다.

## 모집·방장 출발 요청 분리 (2026-10-24)

`Waiting`은 유지하고 서버가 저장하는 `leaderCharacterId`, `lobbyRevision`,
`departureRequestId`, `departureRequestRevision`, `departureRequestedAt`으로 모집과
배정 대기를 구분한다. 새 방의 생성자가 방장이며, Waiting에서 방장이 나가면 남은 멤버의
참가 시각/정확한 GUID 순으로 승계한다. 멤버가 나가면 배정 대기를 해제한다. 새 멤버는
출발 요청이 없는 Waiting에서만 참가할 수 있다. 기존 진행 중 세션/보상 권한은 바꾸지 않는다.

플레이어 전용 `POST /api/dungeon-sessions/{id}/departure`와 `/departure/cancel`은
`{ characterId, requestId, expectedLobbyRevision }`을 받는다. 실제 소유 신원과 현재
방장·멤버십, 원래 명령의 요청 ID+버전을 저장소의 동일 gate/행 잠금에서 검사한다.
출발은 최신 방 버전을 요구한다. 취소에는 **출발을 처음 요청한 버전**을 보내며, 응답의
`departureRequestRevision`에도 같은 값이 실린다. UUID만 같아도 버전이 다르면 다른 명령이다.

- 같은 출발/취소의 응답 유실 재시도는 멤버/임대/방 버전을 다시 바꾸지 않는다. 취소한
  명령의 늦은 출발 재전송과 새 명령을 향한 오래된 취소는 409이며 새 UUID를 강제로
  만들어 우회하지 않는다. 이미 claim된 같은 출발의 조회성 재시도는 현재 Loading을 반환한다.
- HTTP claim/activate는 명시적으로 출발을 요청한 파티만 배정한다. 취소가 먼저 잠금을
  잡으면 claim하지 못하고, claim이 먼저면 취소는 409이며 Loading을 되돌리지 않는다.
  구형 내부 저장소 호출의 선택 인자 호환을 HTTP에 우회 권한으로 노출하지 않는다.
- `DungeonSessions:MinimumDeparturePartySize`는 서버 설정이고 기본 1로 기존 솔로 흐름을
  보존한다. 최대 인원 이하로 제한하며, 요청과 배정 양쪽에서 현재 유효 멤버 수를 검사한다.
  2인 이상 전용 정책은 설정을 2로 바꾸며 클라이언트가 값을 정하지 않는다.
- 기존 DB에 다섯 열을 추가하되 방장/출발을 추측해 채우지 않는다. 구형 Waiting은 조회/
  나가기·재생성이 가능하고 자동 배정하지 않는다. 기존 Loading/InProgress의 서버 시작/
  heartbeat·정산과 데이터는 보존한다. 새 제약은 잘못된 출발 명령 모양을 거절한다.

백엔드 증거는 `Saved/Tests/DungeonDeparture/9f7f05efd1ac4432abb75fb09e90f036/`다.
격리 Release 빌드·Memory/실제 private PG의 방장/소유권/최소 인원, 취소·재시도/ABA,
멤버 이탈·승계, claim/취소 경합, 저장소 재생성 복원/구형 방 보존과 실제 Program HTTP가
runner exit 0이었다. private schema만 생성·정리했고 public DB/기존 세션은 변경하지 않았다.
기존 만료 정리를 제외하고 API 거절이 멤버/임대·다른 파티를 새로 변경하지 않는다. 기존 테스트 하네스의 관리자 배정
앞에는 방장 **플레이어** 자격의 출발 helper를 명시적으로 연결했다(파싱 검증이며 광역
E2E 재실행 성공은 아니다).

UE 기본 로비에 `RequestDungeonDepartureButton`/`CancelDungeonDepartureButton`과 모집 중/
배정 대기·구형 세션 안내, 키보드/패드 포커스 후보를 연결했다. 실제 Standalone 로비/인증·
현재 방장/소유 선택·요청 잠금을 확인한 뒤 공통 HTTP 경로로 보낸다. Blueprint는
`URPGDungeonSessionSubsystem::CanSetDeparture`/`SetDeparture`를 사용할 수 있다. 사용자
정의 로비는 동일 이름 버튼을 연결하거나 이 API를 사용해야 하며 BP 에셋을 일괄 수정하지 않는다.

미확정 요청은 메모리에서 원래 세션/캐릭터/계정·요청 ID/버전·출발 또는 취소 의도를 유지하고
같은 명령만 재시도한다. 같은 활성 대기열 조회만으로 취소가 실패했다고 추측하지 않는다.
정확한 멤버의 authoritative snapshot이 취소 완료/배정 시작/다음 소유 세션/현재 세션 없음
등을 확인했을 때 해제한다. 잘못된 멤버 응답으로 신원을 버리거나 미확정 취소가 다음 방을
영구 잠그지 않도록 했다. 토큰을 저장하거나 클라이언트 추측으로 Waiting을 복원하지 않는다.

최종 검증:

- 첫 `DungeonDepartureBuild-20261024.log`의 Live Coding 차단과
  `DungeonDepartureResumeBuild-20261024.log`의 대규모 재컴파일 중 공유 인벤토리 `Slot`
  이름 가림 실패를 보존했다. 해당 지역 변수는 다른 작업에서 이미 `LabelOverlaySlot`으로
  수정된 상태였으므로 중복 수정하지 않았다. `DungeonDepartureRecoveryBuild-20261024.log`는
  자체 증분 **12개 작업/lib·Project 및 관련 플러그인 DLL 링크·전체 타깃 Success**다.
  이 인벤토리 기능의 동작 검증까지 완료했다고 계산하지 않는다.
- `Saved/Tests/DungeonDepartureNative/30d54ce4afc44c818326aa8230e81383/Verification.proof.json`:
  `ProjectRPG.Online.DungeonDepartureRecovery` 단일 native 검사(8개 복원 조건), runner exit 0.
  미확정 취소 유지·claim 시작/다음 방 해제·취소/출발 확인·미커밋 동일 재시도·잘못된 멤버
  보존·현재 세션 없음 복원이다. 입력 5개/Project DLL·닫힌 로그 일치. 기존 기동 Error 15줄과
  검사 이후 Error 0을 구분하며 HTTP/네트워크 증거로 사용하지 않는다.
- `Saved/Tests/LocalDungeonAllocator/32f44e610bb14b4e8d8b622819be5d6f/Verification.proof.json`:
  실제 2인 로비에서 일반 멤버의 출발/취소 차단, 모집 중 HTTP claim **204/미배정 보존**,
  방장의 실제 버튼 출발→취소→새 요청→자동 서버 기동→정확한 두 멤버의 Iris/가방·GAS/input
  입장이 runner exit 0이다. backend는 격리 Memory/Release 경고·오류 0이며 강제 처치/정산을
  하지 않았다. 입력 48개/Project DLL·닫힌 로그 15개 일치, 실제 서버/클라이언트/백엔드
  5개 로그의 Error/fail/Unhandled/Fatal/Assert/Ensure 0이다. 모의 할당기 13/readiness 7종은
  실제 UE 연결과 별개로 보존했다.
- 중간 `755239df35374f0aaf1d29cc860f5a16` 실행은 HTTP 204의 PowerShell 반환값을 null로
  가정하여 실패했다. 같은 backend DLL의 독립 Memory 진단은 204/빈 `System.String`,
  Waiting·서버 배정 없음/출발 요청 없음을 확인했다(`204-body-diagnostic.proof.json`).
  운영 코드는 바꾸지 않고 러너를 상태 코드+실제 스냅샷 검사로 수정해 위 최종 흐름만
  재실행했다. 원래 실패를 성공으로 승격하거나 기존 PG/native 검사를 반복하지 않았다.

위 스킬 기준대로 자기 프로세스만 종료했고 기존 public DB/에디터·맵/BP/보상 에셋은 유지했다.
백엔드의 새 스키마/API와 로비 클라이언트를 함께 배포해야 한다. 이전 클라이언트는 출발
요청을 보낼 수 없으며, 이미 진행 중인 세션의 입장·정산은 소급 폐기하지 않는다.
이 단계의 증거는 기본 출발/취소/재요청 연결이다. 다음 소절에서 같은 프로세스의 취소
응답 유실·401 재로그인을 추가 검증한다. 최초 출발 응답 유실/클라이언트 재시작의 UX,
실제 PG+2~4인 전체 흐름·자연 전투/정산/복귀, 물리/Slate 입력과 Steam·배포용 Server/
Shipping은 별도다. 기본 연결 증거만으로 이 남은 범위를 완료 처리하지 않는다.

## 미확정 출발·취소 재확인 UX (2026-10-24)

`HasUnresolvedDeparture`는 자격 증명/명령 내용을 노출하지 않는 상태 조회이고,
`RetryDeparture`는 캡처한 세션·캐릭터·계정/요청 ID/원래 버전·취소 여부를 그대로 사용한다.
캡처가 없으면 새 출발 요청을 만들지 않는다. 실제 로비/인증/일치하는 문맥/진행 중 요청
가드는 기존 `SetDeparture`가 재검사한다. 인증이 없으면 재확인도 잠그고 같은 계정 로그인을
요구한다. 미확정 상태에서 일반 출발/취소를 잠그고 `RetryDungeonDepartureButton`과 안내를
연결했다. 조회만으로 이미 해결된 경우에는 기존 owner-scoped 복원 규칙으로 해제한다.

새 `-DepartureResponseLoss`는 정확한 2인 `-LobbyEntry`와만 함께 사용하는 로컬 개발
검증 모드다. 정상 입장/전투를 반복하지 않고 로비에 머문다. 방장의 실제 취소 200이
백엔드에서 커밋된 뒤 그 응답만 유실시키고, 해당 세션 읽기를 일시 보류해 미확정을 관측한다.
보호된 로컬 제어 포트가 그 요청의 자격만 **실제 `/api/auth/logout`**으로 폐기한다.
클라이언트 재확인 요청의 401을 실제 백엔드에서 전달하고, 실제 로그인 버튼의 같은 계정
인증 뒤 동일 명령/동일 취소 응답을 재확인하도록 했다. 인증 만료 시각이나 401을 위조하지
않으며 토큰은 프록시 메모리에만 두고 로그/상태 파일/클라이언트 환경에 기록하지 않는다.

검증 결과와 범위:

- 자체 `Saved/Logs/DungeonDepartureRetryBuild-20261024.log`: Editor 414개 작업·전체
  링크/타깃 Success. 프록시 Node 구문/러너 파싱과 잘못된 옵션 조합 2개 거절도 통과했다.
- 집중 native `DungeonDepartureNative/2337af4819514924b821c93891fb5208`:
  `ProjectRPG.Online.DungeonDepartureRecovery` 1개 통과. 캡처 없는 재시도의 새 명령
  생성 거절·월드 없는 거절 후 원래 명령 보존을 추가 확인했다. 입력 5개/실행 DLL은
  검사 중 불변이며, 기존 기동 Error 15줄과 검사 시작 이후 Error 0줄을 구분한다.
- 실제 2인 Memory HTTP `LocalDungeonAllocator/ae3c28d193c947989f99de769501a234`:
  `Verification.proof.json` Passed/runner exit 0. 실제 방장 취소 200 유실 1건,
  백엔드 401 1건, 실제 같은 계정 재로그인과 원래 취소 replay 1건이다. 최초/최종 응답
  SHA256이 같고 다른 자격으로 재인증했음을 확인했다. 새 출발은 0건이며 복구 전후 전체
  세션 스냅샷의 파티/방 버전/lease가 같고 다른 참가자의 읽기도 유지된다.
  읽기/조기 replay 보류 기능은 켜져 있었지만 해당 구간의 실제 차단 관측은 각각 0건이다.
  선택 입력 51개/실행 DLL·닫힌 로그 16개 불변, 실제 로그 6개 Error/Fatal/Assert/Ensure
  0이다. 게임 서버/할당/입장·전투·보상을 반복하지 않고 로비에서 종료했다.
- 최초 native `35c3b00b5b3f463db15c9b6862af28aa`와 통합
  `22cd5ffe0ca84755ad966b3274318516`는 UE 초기화에서 timeout됐고, native
  `d71dd0462ca04196a2bdc1f43fcce8b0`는 같은 대기 중 자체 기동을 중단했다. 테스트
  성공으로 계산하지 않는다. 다른 Editor 빌드와 동시에 SDK 검사 UBT를 기다리는 경로를
  엔진 `TargetPlatformManagerModule.cpp`에서 확인했다. 성공 실행은
  `UE_SKIP_UBT_SDK_SETUP=1`을 실행 PowerShell 환경에만 설정하고 종료 시 원래 값으로
  복구했다. 패키징/SDK 준비 검사가 아니며 엔진 파일·다른 빌드/에디터를 변경하지 않았다.
  실패 실행의 정확한 SDK 로그 경로에 묶인 대기 셸/자식 PING만 정리했다. 같은 통합 루트의
  `DepartureRetryPostCheck.proof.json`에 native/통합의 동일 DLL, 입력/닫힌 로그 변경 0과
  이 기동 환경·자체 프로세스 정리를 기록했다.

새 UI의 검증은 네이티브 버튼 디스패처이며 물리/Slate 포인터·키 입력 증거는 아니다.
이 절의 증거는 같은 UE 프로세스의 **취소** 응답 유실 복구다. 최초 출발 응답 유실은 다음
절에서 추가 확인한다. 프로세스 재시작 후 복원, 실제 Steam 인증·PostgreSQL·4인/Shipping은
별도 범위로 남긴다.

## 최초 출발 응답 유실 복구 연결 (2026-10-24)

원래 `SetDeparture`/`RetryDeparture`는 최초 출발과 취소의 명령을 모두 보존한다.
이번 변경은 이 운영 로직을 중복 구현하지 않고 실제 클라이언트 검증 경로의 두 의도를
분리한다. `Run-LocalDungeonAllocatorStartupTest.ps1 -LobbyEntry -DepartureResponseLoss
-DepartureLossOperation Request -ClientCount 2`가 최초 출발 200 유실 경로다. 기존 옵션만
사용하면 `Cancel`이며 정상 입장/전투 모드와 혼합하지 않는다.

프록시의 보호된 `/arm`에서 `Request`/`Cancel`을 고정하고 정확한 원래 명령만 replay한다.
최초 출발 유실 중 다른 요청 ID/반대 의도 취소는 전달하지 않는다. 클라이언트 probe는
출발 직후의 미확정부터 재확인/실제 401/같은 계정 로그인/동일 요청 복구를 거치고, 복구한
Waiting이 출발 대기 상태이며 새 출발 비활성·취소 활성인지를 확인하도록 연결했다.
취소 유실을 복구한 모집 상태와 혼동하지 않는다. 이 모드는 실제 서버를 할당하지 않는다.

프록시 집중 3개는 HTTP stub의 두 의도/읽기·조기 replay 보류·401 전달/동일 응답과
새 명령·반대 의도 차단, 로그/상태의 자격 비노출을 확인했다. 이는 실제 백엔드/UE 인증
증거가 아니다. 결과는 `Saved/Tests/DungeonDepartureRequestRecovery/
49a7457f2a2e4fc4a669984aa714d9fa/Verification.proof.json`에 보존했다. Node/PowerShell
구문 및 독립 소문자 `request` 옵션 거절도 통과했으며, 러너가 대소문자를 wire 값으로
정규화한다.

실제 연결 결과:

- 자체 `DungeonDepartureRequestRecoveryAlignedBuild-20261024.log`: Editor 415개 작업,
  모듈 링크·전체 타깃 Success. 최초 `DungeonDepartureRequestRecoveryBuild-20261024.log`
  실행은 공유 빌드와 다른 Live Coding 옵션으로 광역 재컴파일을 시작해 자체 UBT/컴파일러만
  중단했다. `-NoLiveCoding`을 맞춘 뒤 위 빌드로 완료했으며 중간 기록을 성공으로 승격하지
  않는다. 다른 작업·기존 에디터/DB·운영 코드/에셋은 변경하지 않았다.
- `Saved/Tests/LocalDungeonAllocator/f1dc7ab35b974f08ab74e754f5dd7b94/
  Verification.proof.json`: 실제 2인 개발 로비/HTTP Memory, runner exit 0. 출발 200
  유실 1건·실제 백엔드 401 1건·같은 계정 로그인 후 원래 요청 replay 1건이다. 명령과
  최초/최종 응답 SHA256이 일치하며 추가 출발/반대 의도 취소는 각각 0건이다. 복구 전후
  전체 세션 스냅샷의 파티·방 버전·lease가 같고 다른 참가자의 읽기도 유지된다.
- 최종 Waiting/2인/서버 배정 없음/출발 대기 상태와 실제 위젯의 새 출발 비활성·취소
  활성 표식을 확인했다. 읽기·조기 replay 보류의 실제 관측은 0건이며 해당 경계는 위 stub
  집중 검사와 구분한다. 입력 52개/실행 DLL·닫힌 로그 16개 불변, 실제 로그 6개
  Error/Fatal/Assert/Ensure 0이다. SDK 초기화 UBT 대기를 피하는 환경 설정은 이전 절처럼
  테스트 PowerShell에만 적용하고 원래 값으로 복구했다.

이번 검증은 기존 운영 복구 로직과 버튼 디스패처를 재사용한다. 정상 입장/전투·보상,
native 전체·PostgreSQL/Steam·물리 입력·Shipping을 반복 검증한 것이 아니다. 이 단계까지는
명령이 메모리에만 남았으며, 다음 절에서 로컬 영속 복원 기능을 추가했다.

## 미확정 출발 명령의 로컬 영속 복원 (2026-10-24)

`FRPGDungeonDepartureJournal`은 `Saved/Online/DepartureCommands` 아래에 백엔드 API URL,
Steam 계정, 캐릭터의 정확한 조합을 해시한 파일을 둔다. 저장 필드는 버전·그 조합·세션 ID·
원래 요청 ID·원래 lobby revision(문자열)·취소 여부뿐이다. 토큰, JoinTicket, 서버 주소,
멤버십/보상 영수증, 인증 만료 시각은 저장하지 않는다. URL의 query/fragment/userinfo와
비정규 GUID·안전한 JSON 정수 범위 밖 버전·초과/알 수 없는 필드는 받아들이지 않는다.
이 파일은 사용자 변경이 가능한 복구 힌트이지 권한이나 서버 커밋의 증거가 아니다.

전송·복원 계약:

- 최초 출발/취소 POST 전에 원래 명령을 `.tmp`에 쓰고 flush한 뒤 최종 파일로 옮긴다.
  백엔드/계정/캐릭터별 명명 mutex와 exact-match 비교로 다른 명령 덮어쓰기를 막는다.
  완전한 tmp는 복원하고 부분/충돌 tmp·읽기/쓰기 실패는 새 nonce를 만들거나 빈 기록으로
  취급하지 않는다. 기존 요청 재시도는 ID·버전·의도를 바꾸지 않는다.
- 재실행 뒤에는 새 로그인과 소유 캐릭터 선택이 필요하다. 그때 일치하는 파일만 불러오며
  인증/현재 세션은 복원하지 않는다. 기존 캐릭터 선택 UI가 `ResumeDungeonSession`으로
  실제 active-session GET을 보낸다. 조회 전 자동 mutation/이동은 없다.
- 서버가 원래 Waiting 명령의 완료/취소, Loading 이후, 다음 소유 세션, 확정 이탈 또는
  인증된 no-active-session을 확인하면 exact-match 기록을 정리한다. 변경 없는 precommit
  Waiting이면 원래 명령을 유지하고 기존 결과 재확인 버튼으로만 재전송한다. 실패/401/
  nonmember 응답·로컬 Reset·종료는 기록을 삭제하지 않는다.
- 복원된 미확정 요청은 캐릭터 변경/새 방 생성·참가/입장/로그아웃을 잠근다. 기존 세션의
  조회·확정 이탈·같은 계정 재인증은 유지한다. 파일 정리에 실패해도 기록을 잃지 않는다.
  저장소 오류는 별도로 표시하고 복원/조회/재확인을 통해 다시 시도한다. 손상 파일은 자동
  삭제하지 않으므로 실제 저장 공간/권한/파일 복구가 필요할 수 있다. 세션을 아직 읽지
  못했으면 활성 세션 복원 버튼을 먼저 사용하도록 안내한다.

검증과 한계:

- `DungeonDeparturePersistenceBuild-20261024.log`: 자체 Editor 83개 작업·전체 링크/타깃
  Success. `Run-DungeonDepartureAutomationTest.ps1 -Persistence`의 native 3개가
  `Saved/Tests/DungeonDepartureNative/94a95e5f5e7944fc943ec70fd630c3d7/Verification.proof.json`
  에서 통과했다. 실제 파일의 재열기/완전한 tmp/부분 tmp/다른 nonce·의도·버전·계정·endpoint,
  손상 저장소의 dispatch 차단·복구, 토큰 없는 새 manager/GameInstance의 두 의도 복원과
  owner-scoped 스냅샷에 따른 디스크 정리를 확인했다. 입력 11개/DLL 불변, 기동 Error
  15줄/검사 시작 이후 Error 0·Fatal/Assert/Ensure 0이다. 파일 fixture는 고유 Tests 디렉터리에
  보존하며 기존 플레이어 기록을 조작하거나 광역 삭제하지 않았다.
- 같은 DLL의 `LocalDungeonAllocator/8189d46fbf2b4115a910bcd1f6f05137/Verification.proof.json`:
  실제 2인 개발 로비·Memory HTTP의 최초 출발 응답 유실→실제 401/같은 계정 로그인→
  원래 요청 복구가 runner exit 0이다. 원래 명령/응답, 파티·버전·lease 유지와 추가 mutation
  0건, 입력 56개·닫힌 로그 16개/DLL 불변 및 실제 로그 6개 Error/Fatal/Assert/Ensure 0을
  확인했다. 이는 새 기록 저장/정리의 기존 실제 호출 경로 확인이며 cold-start 검사가 아니다.
- 그 뒤 조회 전 복원 안내 **문구만** `DungeonDeparturePersistenceUIBuild-20261024.log`의
  4개 작업/링크·전체 Success로 보완했다. 따라서 위 런타임 증거의 DLL은 최종 문구 DLL과
  구분하며, 문구/DLL 변경만을 이유로 native/2인·광역 검사를 반복하지 않았다.

위 증거의 객체 재생성+디스크 복원과 실제 프로세스 종료·재실행은 다른 검증 단계다.
다음 절에서 커밋 후 두 의도의 실제 UE cold-start UI/HTTP를 추가 연결했다. 커밋 전 복구,
Steam·PostgreSQL·4인·물리 입력·Shipping·동일 계정 다중 프로세스 경합/하드 전원 차단
검증은 위 증거에 포함하지 않는다.

## 커밋 후 출발·취소의 실제 프로세스 재시작 (2026-10-24)

`Run-LocalDungeonAllocatorStartupTest.ps1 -LobbyEntry -DepartureResponseLoss -DepartureRestart
-DepartureLossOperation Request -ClientCount 2`가 새 cold-start 모드다. `Cancel`로 바꾸면
취소 커밋 후 응답 유실을 확인한다. 기존 동일 프로세스 401/재로그인 모드와 분리되며
게임 서버를 할당하거나 전투/정산을 진행하지 않는다.

실제 경로와 경계:

- 기존 방장의 실제 요청이 커밋된 200만 유실시키고 미확정 UI에서 대기한다. 그 프로세스가
  사용한 비밀 없는 journal 경로/8개 필드를 확인하고, 저장된 원래 요청의 정규 본문 SHA256을
  프록시가 관측한 실제 HTTP 명령과 비교한다. 기록을 주입·수정하지 않는다.
- 실행 파일/PID/start ticks가 일치하는 자기 방장만 강제 종료한다. 같은 UserDir/API URL로
  새 UE 프로세스를 띄우고, 인증·계정·선택·세션·pending이 모두 빈 초기 상태임을 확인한다.
  실제 로그인 버튼과 소유 목록 조회 뒤에도 원래 파일 SHA256이 그대로인지 확인한다.
- 새 프로세스의 실제 캐릭터 선택이 기존 journal 복원/active-session GET을 호출한다.
  프록시의 보호된 `/cold-read`는 해당 소유 GET만 보류 해제하고 mutation replay를 열지
  않는다. 정상적인 새 자격의 실제 200을 그대로 전달하며 DB·만료 시각·조회 응답을 만들지
  않는다. 원래 자격을 폐기하거나 401을 관측한 모드는 아니므로 이전 401 증거와 구분한다.
- 실제 조회 결과로 같은 세션/원래 요청 ID·버전·의도와 멤버십을 확인한 뒤 파일이 사라지고
  출발은 queued/출발 비활성·취소 활성, 취소는 recruiting/출발 활성·취소 비활성이 되는지
  확인한다. 두 경우 모두 재확인 버튼은 비활성이다. 새 프로세스에 flow private 상태나
  토큰을 주입하지 않고 기존 버튼/선택 디스패처를 사용한다.

검증 결과:

- 자체 `DungeonDepartureColdStartBuild-20261024.log`: Editor 11개 작업·전체 링크/타깃
  Success. 프록시 집중 5개는 기존 두 의도의 응답 유실/401 replay와 새 cold-read의
  자격 교체·mutation 잠금 유지/모드 혼합 거절을 HTTP stub으로 확인했다. 구문 검사와
  `DepartureResponseLoss` 없는 재시작 옵션 거절도 통과했다.
- 실제 출발: `Saved/Tests/LocalDungeonAllocator/a616b7d065734ce2b66c37576ce13052/Verification.proof.json`.
  실제 취소: `Saved/Tests/LocalDungeonAllocator/7efe630661d24f1ea21bc1992c87c55a/Verification.proof.json`.
  두 runner 모두 exit 0이며 old/new 프로세스 신원을 기록했다. 실행별 실제 커밋 응답 유실
  1건·새 자격의 소유 GET 1건, POST replay/조기 replay 시도/추가 출발·취소 각각 0건이다.
  서버 전체 스냅샷의 파티·방 버전·lease가 전후 같고 남은 참가자의 읽기도 유지된다.
  원래 파일의 재시작/로그인 전후 불변과 원래 명령 SHA 일치·조회 후 파일 정리를 확인했다.
- 실행별 입력 57개/동일 DLL·닫힌 로그 17개 불변, 실제 로그 7개 Error/Fatal/Assert/Ensure
  0이다. SDK 자동 설정 생략은 테스트 PowerShell에만 적용하고 복구했다. 운영 C++/백엔드
  정책·자산·기존 플레이어/DB를 바꾸지 않았으며 native·정상 입장/전투·보상·PG를 반복하지
  않았다. 실제 cold-start 클라이언트/HTTP는 개발 인증의 로컬 Memory 범위다.

완료 범위는 **커밋 후** 결과를 소유 GET만으로 복구한 두 경우다. 커밋 전 요청이 아직
백엔드에 도달하지 않은 상태의 재시작/원래 명령 POST, 서버 배정과 경합한 재시작,
Steam·PostgreSQL·4인/Shipping·실물 입력 검증은 다음 범위로 남긴다.

## 커밋 전 복구 연결과 의도별 안내 (2026-10-25, 소스 구현/검증 보류)

사용자의 구현 우선 요청에 따라 에디터 종료·빌드/실행 검증을 요구하지 않고 소스를
마무리했다. 아래는 구현된 코드 경로이며 새 DLL이나 실행 성공의 증거가 아니다.

실제 로비 UI에는 `IsPendingDepartureCancellation`으로 자격/명령 내용을 노출하지 않고
저장된 의도만 조회하도록 연결했다. 재확인 버튼은 `원래 출발 요청 재확인` 또는
`원래 취소 요청 재확인`으로 표시한다. 세션이 아직 없으면 먼저 세션 복원, 원래 요청을
확인할 수 있으면 재확인, 현재 문맥에서 재전송할 수 없으면 새로고침을 안내한다.
기존 인증/문맥/소유 검사와 새 요청 잠금·동일 ID/버전 재사용은 유지한다. 서버 반영 여부를
로컬 안내만으로 단정하거나 자동 POST하지 않는다.

이미 구현된 `RetryDeparture`/journal 복원을 사용하는 개발 연결 모드는 기존 cold-start
옵션에 `-DepartureLossPoint BeforeCommit`을 추가한다. 이 값은 `DepartureRestart`와
`DepartureResponseLoss`를 함께 요구한다. 기본 `AfterCommit`은 유지한다.

- 최초 대상 요청의 본문/의도를 캡처한 뒤 백엔드에 전달하지 않고 연결을 끊는다. 원래
  커밋 응답을 만들지 않으며 대상 전송 수 0인 상태로 재시작하도록 연결했다.
- 새 프로세스의 인증·소유 목록/캐릭터 선택과 실제 소유 GET 뒤에도 원래 요청·파일이
  미확정으로 유지되는지 확인하도록 작성했다. 일반 출발/취소는 잠그고 재확인만 연다.
- 별도 게이트와 보호된 `/cold-retry`는 새 자격의 소유 조회 뒤에만 원래 명령 전달을
  허용한다. 실제 재확인 버튼 디스패처가 기존 재전송 본체를 호출한다. 조회 자체가 요청을
  전송하거나 해결된 것으로 파일을 지우지 않도록 경계를 나눴다.
- 원래 명령과 동일한 실제 POST 1건·성공 1건, 방 revision 정확히 1 증가, 요청 ID/원래
  버전·최종 의도, 파티/lease 및 나머지 세션 필드 보존, 커밋 뒤 파일 정리를 검사하도록
  연결했다. 읽기만으로 복구하는 기존 커밋 후 모드와 proof 필드를 구분한다.

이 절의 UI/새 개발 연결 소스와 추가 프록시 사례는 아직 빌드·실행하지 않았다. 기존
커밋 후/동일 프로세스/객체 복원의 성공 기록을 새 precommit 코드 결과로 승격하지 않는다.
백엔드/저장 계약·맵/BP/보상 에셋은 변경하지 않았다. 이후에도 기능 구현을 우선하고
이 경계의 빌드/실제 UE 연결 확인은 별도 대기 항목으로 남긴다.

## 던전 파티원 목록과 역할 표시 (2026-10-25, 소스 구현/검증 보류)

기존 세션의 인원수 텍스트에 더해 `OnlineLobbyPartyRosterHeader`와
`OnlineLobbyPartyRoster`를 기본 로비 화면에 연결했다. 별도 화면·입력 버튼을 생성하지
않고 기존 새로고침/세션·소유 목록 delegate가 갱신하는 읽기 전용 텍스트다.

- 기존 인증 계정 바인딩·소유 캐릭터 선택과 활성 세션의 자기 멤버십이 있을 때만 표시한다.
  로컬 journal만 있거나 종료된 세션·멤버십/소유 선택이 없는 경우에는 숨긴다. 계정 만료,
  조회 처리 중/오류에는 남아 있는 스냅샷을 `마지막 조회 정보`로 구분한다.
- 방장 우선, 유효한 참가 시각, 정확한 ID 순서로 정렬하고 `[방장 · 나]`, `[방장]`, `[나]`,
  `[파티원]`을 표시한다. 방장 정보가 없으면 임의 방장을 만들지 않는다. 잘못되거나 중복된
  멤버 ID는 일부 멤버/인원으로 꾸미지 않고 목록을 표시하지 않는다.
- 이미 가진 소유 목록에서 알 수 있는 이름만 사용한다. 타인의 이름은 현재 세션 DTO에
  없으므로 `캐릭터 <축약 ID>`로 표시하며, view에는 안정적인 전체 ID와 `bHasKnownName`을
  별도로 둔다. 다른 계정 조회나 Steam 프로필/비공개 캐릭터 API 호출은 추가하지 않는다.
- 인원은 해당 서버 스냅샷의 멤버 수다. `LeaseExpiresAt`이나 PC 시계로 접속 여부/자동
  탈퇴·10분 유예를 계산하지 않고, 준비/보상 자격이나 최대 인원도 표시에서 추정하지 않는다.
  Steam 로비 인원과 던전 멤버십을 혼합하지 않는다.

`RPGOnlineLobbyPartyView.h/.cpp`의 `FRPGOnlineLobbyPartyMemberView`와 공통 순수
formatter를 분리했다. 커스텀 WBP는 기존 로비 위젯 인스턴스에서 다음 조회를 사용할 수 있다.

- `GetDungeonPartyMembers`: 안정적인 캐릭터 ID·표시 이름/이름 가용성·방장/본인 역할.
- `GetDungeonPartySummaryText`: 멤버 수와 캐시 여부를 포함하는 제목.
- `GetDungeonPartyRosterText`: 기본 화면과 같은 다국어 역할/이름 줄 목록.

표시 모델의 역할은 권한이 아니며 기존 출발/취소/입장/이탈 검사는 그대로 유지한다. 읽기
전용 텍스트는 포커스를 받지 않아 기존 키보드·패드 탐색 대상도 바꾸지 않는다. 백엔드/API/
스키마·맵/BP·보상 에셋과 기존 미확정 요청 복원/잠금 정책은 수정하지 않았다.

사용자 요청에 따라 이번에는 소스/API·공백 검토만 했다. UHT/C++ 빌드·native/실제 화면/
MCP·Steam/Shipping 검증은 하지 않았으며 화면 반영은 이후 정상 빌드/재실행이 필요하다.
이 단계에서는 타인의 이름이 없었으며, 아래 소절에서 같은 세션 응답의 표시용 이름을 연결했다.

## 같은 던전 파티원의 캐릭터 이름 (2026-10-25, 소스 구현/검증 보류)

기존 세션 응답의 `members[]`에 nullable `characterName`을 추가했다. 원래의
`characterId`/`joinedAt`/`leaseExpiresAt`은 유지하고, 멤버 요청에는 이름 입력을 추가하지
않는다. 같은 세션 응답을 받을 기존 권한을 통과한 뒤 제공하는 표시 정보다. 타 계정의
캐릭터 조회 API를 열거나 Steam 프로필 조회/계정 정보·아이템·경제 상태를 덧붙이지 않는다.

- Memory는 캐릭터 생성 시 저장한 서버 기록의 ID/Steam 소유자가 정확히 일치하는 경우에만
  세션 생성·참가 시 이름을 채운다. 기존 `with` 갱신은 이를 유지한다. 현재 캐릭터 이름은
  생성 후 불변이며 이름 변경 기능이 추가되면 이 표시 스냅샷의 갱신 정책도 함께 연결해야 한다.
- PostgreSQL은 기존 세션의 멤버 SELECT에서 캐릭터 ID와 Steam 소유자를 함께 맞춘
  `LEFT JOIN characters`로 이름을 읽는다. 별도 열/테이블·추가 멤버별 DB 조회를 만들지
  않으며 누락·불일치 기록은 이름만 null로 둔다. 이름 조회가 멤버를 제거/추가하지 않는다.
- 공통 `DungeonSessionResponses`가 표시명만 DTO에 전달한다. `DungeonSessionMember`의
  새 필드는 표시 전용이며 소유권·입장 티켓·리더 판정·회수/정산에 사용하지 않는다.
- UE의 `FRPGDungeonSessionMember.CharacterName`은 선택적인 읽기 필드다. 구버전 응답의
  누락/null/비문자열 이름은 기존 멤버십을 거절하지 않고 빈 이름으로 다룬다. 파티 view는
  표시 가능한 서버 이름, 이미 가진 소유 목록의 이름, 축약 ID 순서로 fallback한다.
  이름은 일반 텍스트이며 빈 값/제어 문자/표시 상한 초과 값은 실제 이름으로 표시하지 않는다.

정렬·안정적인 행 ID·방장/본인 표시는 여전히 캐릭터 ID와 서버 멤버 스냅샷 기준이다.
표시명으로 인증/멤버십을 해결하지 않고 이름이 같아도 서로 다른 캐릭터를 합치지 않는다.
구 UE는 추가 JSON 필드를 무시하고, 새 UE는 구 backend의 축약 ID 표시를 유지한다.
이름을 보려면 갱신된 backend와 UE 모듈을 각각 정상 빌드/재실행해야 한다.

이번 변경은 소스 구현/읽기 검토와 공백 확인에 한정했다. backend Release·UE UHT/C++/
실제 HTTP/PostgreSQL·native/UI·Steam/Shipping 검증은 실행하지 않았다. 기존 proof는
이 새 필드/조회 소스의 성공 증거가 아니며 맵/BP/보상 에셋·DB 데이터/스키마는 변경하지 않았다.

## 로비 영역별 배치와 고정 상태 안내 (2026-10-25, 소스 구현/검증 보류)

네이티브 로비의 긴 단일 목록을 계정·캐릭터·던전/파티 준비·Steam 파티 카드로 나눴다.
제목과 기존 상태 안내는 스크롤 밖의 상단 행에 두고, 나머지 영역만 하나의 ScrollBox에서
탐색한다. 기존 화면 너비 60% 고정을 제거하고 뷰포트 앵커/여백을 사용한다. 세션 정보와
파티 목록은 던전 카드 안에 함께 표시하며 새로운 서버/접속 상태를 추정하지 않는다.

- 공통 `RPGOnlineLobbyLayout` helper가 카드·본문·버튼·입력창 배치를 담당한다.
  제목 26/영역 제목 18/본문·버튼 14의 글자 크기, 카드/행 여백과 입장 버튼 강조를 적용했다.
  기존 폰트/fallback 자산과 콤보/입력창의 폰트는 유지하며 새 폰트 에셋을 만들지 않는다.
- 버튼·입력창·선택창은 최소 높이 44의 wrapper를 사용한다. 높이를 고정하지 않아
  줄바꿈이 필요한 버튼 문구가 늘어날 수 있고, 버튼의 직접 TextBlock 자식은 유지해
  기존 원래 출발/취소 재확인 문구 갱신과 호환된다.
- 개발 로그인과 개발 세션 참가는 별도 개발 카드로 분리하되 기존
  `IsDevelopmentUIEnabled` 조건을 그대로 사용한다. 포기 버튼도 기존 노출 가드를 유지하며
  숨길 때 최소 높이 wrapper까지 함께 접어 비어 있는 조작 영역을 남기지 않는다.
- 기존 컨트롤 이름·클릭 바인딩·인증/소유권·중복 입력/미확정 명령 가드는 유지한다.
  기존 기본 포커스 후보와 확인창 취소 포커스, 초점 이동 시 ScrollBox의 IntoView도 유지한다.
  새 탭/접기 동작·자동 실행·입력 모드·HTTP 요청이나 BP/맵 변경은 추가하지 않는다.

기존 `DungeonLobbyWidget` 집중 사례에 고정 상태 행/영역 구조, 최소 높이, 직접 버튼 문구,
개발 카드 노출과 포기 wrapper 표시 일치 검사를 추가했다. 사용자 구현 우선순위에 따라
이번에는 소스/API와 공백만 검토했으며 테스트 실행·UHT/C++ 빌드·GPU 화면/실물 입력·
Steam/Shipping 검증은 보류했다. 이전 DLL/기존 proof의 성공은 이번 배치의 성공 증거가 아니다.
정상 빌드/재실행 뒤 해상도·DPI별 배치/한글 폰트·긴 문구·포커스 스크롤을 확인해야 한다.
이는 기본 사용성 배치 단계이며 최종 아트/전체 게임패드 탐색·번역 수집 완료는 아니다.

## 상태별 다음 단계 안내와 기존 버튼 강조 (2026-10-25, 소스 구현/검증 보류)

고정 상태 안내 아래에 `OnlineLobbyNextStep` 문구를 추가했다. 기존 상세 상태/오류 문구는
유지하며 로그인·동일 계정 재인증·캐릭터 선택/이름 수정·세션 생성·파티 모집·서버 준비·
입장·정산 대기·미확정 요청 복구에 따라 다음에 확인할 조작을 안내한다. 활성화된 기존
버튼이 추천 대상일 때만 해당 버튼의 배경색을 강조한다. 입력/선택창은 문구로 안내한다.

- 공통 순수 `FRPGOnlineLobbyNextStepPresentation`이 상태 범주와 사용 가능한 컨트롤
  이름 집합을 받아 문구/대상을 고른다. 위젯은 기존 `CurrentPresentation`과 서브시스템의
  상태를 읽고, 해당 컨트롤과 조상이 모두 표시/활성 상태인 경우에만 후보로 제공한다.
  추천은 서버 준비·접속 상태·최소 인원·지급 성공·남은 재접속 시간을 새로 추정하지 않는다.
- 미확정 출발/취소는 활성화된 원래 명령 재확인, 현재 세션 새로고침, 세션 복원 순으로
  안내한다. 세션이 없으면 기존 새로고침 버튼이 잠겨 복원이 선택된다. 저장 복구 오류는
  저장 공간/권한 확인과 조회만 안내한다. 새 출발/반대 의도 요청으로 우회하지 않는다.
- 참가 자격 차단·조회/요청 오류·알 수 없는 상태는 읽기 작업을 안내하며, 서버가 실행
  중이어도 기존 입장 버튼이 잠겨 있으면 입장을 추천하지 않는다. 정산 대기를 지급 완료로
  표시하지 않고, 포기 확인창/진행 중 요청/미확정 포기는 일반 추천 대상을 비운다.
- 추천 allowlist에는 로그아웃·세션 탈퇴·출발 취소·포기/확정 포기를 넣지 않는다.
  확인창의 기본 취소 포커스는 기존 정책을 유지하며, 안내 갱신은 포커스/스크롤을 옮기거나
  클릭 delegate·HTTP·로그인·출발·이동을 실행하지 않는다. 기존 버튼 핸들러가 실행 시점에
  자격/문맥을 다시 검사하는 구조도 바꾸지 않는다.
- 커스텀 UI는 BlueprintPure `GetNextStepText`와 `GetNextStepControlName`을 읽을 수 있다.
  이름은 네이티브 로비의 표시 대상일 뿐 실행 허가/서버 영수증이 아니다. 타 UI의 동일 이름
  컨트롤에 자동 전달하거나 이 조회값만으로 작업을 실행하지 않는다.

새 순수 집중 사례에 상태별 우선순위·잠긴 조작 fallback·정산/입장 차단·원래 명령 복구와
위험 조작만 남았을 때 빈 추천을 추가했다. 기존 위젯/Blueprint 계약 사례에는 고정 안내와
조회 일치·사용 불가 문맥의 빈 대상·read-only 함수를 추가했다. 사용자 우선순위대로
소스/API·공백만 검토했으며 UHT/C++ 빌드·native/HTTP·실제 화면/입력·Steam/Shipping 검증은
실행하지 않았다. 맵/BP·폰트/보상 에셋·backend/DB 정책은 변경하지 않았다. 다음 정상 빌드
후 상태 전환별 문구/강조 해제·긴 문구 배치를 확인하며, 이 소스 추가를 실행 성공으로 계산하지 않는다.

## 비활성 버튼의 사유 표시 (2026-10-25, 소스 구현/검증 보류)

네이티브 로비 버튼 바로 아래에 비활성 사유 행을 추가했다. 비활성 버튼의 hover/툴팁이나
키보드 포커스를 요구하지 않는 일반 텍스트이며, 버튼이 활성화되거나 버튼/조상이 숨겨지면
사유도 비우고 접는다. 개발용 포기 버튼·닫힌 확인창의 숨김 조건도 그대로 따른다.
버튼의 직접 TextBlock 자식과 최소 높이 wrapper는 유지해 재확인 문구와 기존 조작을 보존한다.

- 공통 순수 `FRPGOnlineLobbyActionAvailability`는 기존 활성화 결과를 먼저 받아,
  비활성인 경우에만 공통 차단 또는 알려진 미충족 조건의 첫 사유를 선택한다.
  기존 `Enable`의 boolean 식과 클릭 시점 가드는 유지하며 설명이 활성/권한 판정을 대신하지 않는다.
- 서비스/로비 문맥·열린 확인창·요청/이동 중 상태를 공통 안내하고, 로그인/같은 계정 재인증,
  소유 캐릭터·현재 멤버 세션·Steam 파티·대기 상태·방장/로비 버전·출발 여부를 조작별로 구분한다.
  출발 저장 복구/미확정 요청, 참가 자격·서버 준비, 던전/난이도·파티 검색 선택/초대도 안내한다.
  최소 출발 인원이나 서버의 세부 거절 원인·새 보상 자격을 추정하지 않는다.
- 로그아웃은 일반 작업의 토큰 유효성 안내를 그대로 적용하지 않고 기존 로그아웃 자격 조회를
  사용한다. 만료된 플레이어 자격의 안전한 로그아웃 가능 여부와 기존 복구 잠금을 바꾸지 않는다.
  세부 차단 조건을 확인하지 못하면 원인을 단정하지 않는 일반 상태 확인 문구를 표시한다.
- BlueprintPure `GetActionUnavailableReason(ButtonName)`은 마지막 presentation의 설명을 읽는다.
  알 수 없는 이름·활성/숨긴 버튼은 빈 값이며, 빈 값이 실행 허가를 의미하지 않는다.
  새 tooltip/포커스 이동·클릭 delegate·HTTP·원래 요청 변경·타이머는 추가하지 않는다.

집중 사례 소스에 활성 결과 우선·공통/조건 우선순위·확인 불가 fallback·전체 사유 문구를
추가했다. 기존 위젯/Blueprint 사례에는 버튼별 사유 행/조회 일치·개발 숨김/미등록 이름과
read-only 계약을 보완했다. 사용자 구현 우선순위대로 소스/API·공백만 검토했으며 이번
UHT/C++ 빌드·native/실제 화면/입력·HTTP·Steam/Shipping 검증은 실행하지 않았다.
backend/DB·맵/BP·에셋은 변경하지 않았다. 정상 빌드 뒤 활성/숨김 전환의 사유 제거,
긴 한글 안내와 스크롤/기존 포커스 배치를 확인해야 하며 이전 DLL proof로 대체하지 않는다.

## 키보드·게임패드 순환 탐색 (2026-10-29)

기본 로비의 표시·활성 버튼/입력창/선택창을 화면 트리 순서로 연결했다. `Tab`/`Shift+Tab`과
위/아래 탐색은 같은 순환 경로를 사용하며, 숨김/비활성 조작과 그 조상·중첩 UserWidget의
가용 상태를 함께 검사한다. 상태 변경 시 이전 연결을 정리하고, 한 개만 남으면 외부로
벗어나거나 자기 자신을 가리키는 대신 탐색을 멈춘다. 이름 선택·난이도/스킬 구성·Steam
조작도 기존 화면에 있는 활성 컨트롤일 때만 포함한다. 표시/활성 상태가 권한을 대신하지 않는다.

- 새 private `RPGOnlineLobbyNavigation`은 탐색 메타데이터만 바꾼다. 기존 presentation 갱신에
  연결했으며 새 타이머/HTTP/클릭 dispatch·인증/세션/저장 상태 변경을 추가하지 않는다.
  변하지 않은 규칙은 재작성하지 않고 기본 포커스 후보·실제 포커스/입력 모드도 강제 변경하지 않는다.
- 닫힌 ComboBox는 UE의 `EnableGamepadNavigationMode`를 사용해 결정키로 열기 전에는
  위/아래가 조작 사이 탐색에 사용되게 한다. 열린 목록의 선택/결정/뒤로는 엔진에 맡긴다.
  일반 입력창의 좌/우 caret·공간 탐색 규칙과 선택값/문자열은 그대로 둔다.
- 포기 확인창이 열리면 활성 확인/취소만 탐색 범위에 넣고 여섯 방향을 그 안에 연결한다.
  취소만 가능하면 외부로 나가지 않게 멈춘다. 알려진 닫힌 확인창은 일반 경로에서 제외하되
  기존 두 선택지의 확인 정책을 보존한다. 기본 취소 포커스와 플랫폼 Back 처리·동의/재검사는 유지한다.
- 네이티브 레이아웃의 소유 필드가 없는 커스텀 Blueprint root에는 이 경로를 적용하지 않는다.
  내부 ID/컨트롤 이름은 권한이나 자동 실행 명령으로 사용하지 않는다. 실제로 포커스된 조작이
  비활성화될 때의 Slate 동작과 다양한 화면에서의 사용감은 아래 실물 입력 범위에서 확인한다.

변경 CPP 3개 개별 컴파일/분리 링크와 실행 전 테스트 busy 조회 기대 보완의 테스트 CPP
한 개 재컴파일/분리 재링크가 성공했다. 최종 같은 별도 DLL에서 새 `DungeonLobbyNavigation`
3개와 기존 `MenuFocus` 2개·`DungeonLobbyWidget`/`DungeonAbandonmentConfirmation` 2개,
**7개 모두 Success·UE/호스트 종료 0**이다. 탐색 순서·중첩/부모 가용성·단일 항목·빈 범위·
확인창/외부 scope·선택/문자열/기존 busy 상태 보존·실제 네이티브 연결과 커스텀 root를 다룬다.
테스트는 UI 트리 상태만 설정하며 실제 인증·클릭/게임플레이 포기나 HTTP를 대신하지 않는다.

proof: `Saved/BuildChecks/LobbyInputNavigation-20261029/Verification.proof.json`,
`Native-a30d9a5385d34f6a808b455c574c3cb6/Native.proof.json`.
실제 로드한 분리 DLL·선택 소스 13개/고정 입력 456개는 전후 불변이다. 기존 기동 Error
15줄과 검사 이후/종료 Error 0을 구분한다. 정상 던전 native 러너의 전체 모드에 새 3개
필수 이름/입력 경로를 추가하고 PS AST 오류 0을 확인했으며 전체 suite를 실행하지 않았다.
원본 DLL/Intermediate·열린 GUI/MCP·에셋/Config·backend/DB·다른 담당은 유지했다.

이 결과는 UMG/Slate 탐색 메타데이터와 위젯 상태 검사다. 실제 키보드/패드 버튼·열린
드롭다운 입력·GPU/한글 폰트/해상도·DPI·스크롤/포커스 회복·Steam/Shipping과 정상 공유
모듈 반영은 남는다. 이 단계로 기존 P1 전체나 게임패드 전체 사용성 완료를 주장하지 않는다.

## 비활성 자식 포커스의 제한적 복구 (2026-10-29)

기존 `RPGMenuFocus`는 화면 루트가 포커스를 가진 경우에만 기존 기본 대상으로 이동한다.
요청으로 자식이 비활성/숨김이 된 뒤에도 자식에 포커스가 남는 경우를 위해, 네이티브 로비의
기존 presentation/탐색 갱신 끝에 제한적 복구를 추가했다. 공통 MenuFocus/입력 모드나
캐릭터/세션·클릭/서버 요청 코드는 변경하지 않았다.

- `FindFocusRecoveryTarget`은 실제 캐시 Slate 자식 트리의 경로로 현재 로비 소유 여부를
  확인하는 읽기 전용 조회다. 정상 조작/텍스트 편집, 열린 콤보, 다른 트리·누락/분리된
  위젯·이미 루트인 포커스에는 대상을 반환하지 않는다. 숨김/비활성 부모도 확인한다.
- 부적격한 현재 조작에 대해서만 기존 `GetDesiredFocusWidget`의 활성·표시·범위 내
  대상을 선택한다. 기본 대상도 없다면 조작을 추측하지 않고 inert 화면 루트를 사용한다.
  이후 기존 루트 포커스 정책이 안전한 기본 대상이 준비됐을 때 이어받을 수 있다.
  확인창에서는 기존 취소 우선 정책을 재사용하고 취소가 막혀도 확정을 자동 선택하지 않는다.
- 실제 `SetUserFocus`는 같은 로컬 PC/Slate 사용자·뷰포트에 추가된 네이티브 화면·활성 앱/
  현재 최상위 창에서만 수행한다. 후보 계산 뒤에도 실제 사용자 포커스가 같은지 재확인한다.
  다른 창으로 이동한 사용자나 비활성 앱에서 새 포커스를 만들지 않는다. 기존 로비 생성
  호출은 `RPGOnlineLobbySubsystem`의 `CreateWidget(PC)` → `AddToViewport(120)` 경로를 사용한다.
- 동적으로 제거돼 더 이상 현재 화면 소유임을 증명할 수 없는 조작은 추측 복구하지 않는다.
  장면 전환의 기존 기본 포커스 처리는 유지하며 새 영속 상태·타이머/HTTP·클릭 dispatch를
  추가하지 않는다. 커스텀 Blueprint root에 기존 가드를 우회 적용하지 않는다.

CPP 2개 개별 컴파일/분리 DLL 링크 종료 0. 새 `DungeonLobbyFocusRecovery` 3개와 기존
`MenuFocus` 2개/`DungeonLobbyNavigation.NativeConnection` 1개, **최종 한 DLL의 6개가 모두
Success·UE/호스트 종료 0**이다. 실제 캐시 Slate 조작/내부 `SEditableText` 포인터로 복구
후보를 조회해 가용성/안전 기본·루트 fallback·다른 트리/분리된 위젯·확인창 범위를 검사했다.
이 조회 검사는 실제 사용자 포커스를 설정하는 양성 실행이 아니다. native 연결 사례도
소유 PC/뷰포트가 없는 범위의 가드이며 활성 창에서의 실제 handoff 성공으로 계산하지 않는다.

proof: `Saved/BuildChecks/LobbyStaleFocus-20261029/Verification.proof.json`,
`Native-3ab1d5748237489e848f838a368917b6/Native.proof.json`.
선택 소스 11개/고정 입력 455개·실제 로드한 분리 DLL은 전후 불변이다. 기동 Error 15줄/
검사 이후 Error 0이며 전체 러너의 새 필수 이름/입력 경로와 PS AST 오류 0을 확인했다.
전체 suite·앞선 탐색 7개 전체·서버/보상/600초 검사는 반복하지 않았다. 원본 DLL·
Intermediate·열린 GUI/MCP·에셋/Config/backend/DB와 다른 담당은 유지했다.

실제 로컬 플레이어의 활성 창/뷰포트에서 비활성 조작 → 기본 대상/루트 → 요청 종료 뒤
기본 대상의 포커스 이동, 실물 키/패드·열린 콤보·GPU·Steam/Shipping과 정상 공유 모듈
반영은 남는다. 이번 결과만으로 포커스/게임패드의 모든 실사용 문제를 해결했다고 표시하지 않는다.

## 로컬 뷰포트의 실제 포커스 handoff (2026-10-29)

위 후보 조회 검사에 이어 `DungeonLobbyFocusHandoff.LocalViewport` 사례를 추가했다.
자체 프로세스의 로컬 Player/Controller와 `UGameViewportClient`·UMG 뷰포트 서브시스템에
실제 로비를 추가하고, 기존 `RefreshInputNavigation` → `SetUserFocus`를 호출한 전후의
실제 Slate 사용자 포커스를 대조한다. 제품 함수/가드·인증/요청/입력 모드는 변경하지 않았고
헤더에는 시험 friend만 추가했다. 전체 던전 러너에도 입력 해시 대상과 필수 이름을 등록했다.

- 비활성/숨김 조작 → 기존 안전 기본 대상, 기본 대상 부재 → inert 화면 루트,
  이후 기존 `RPGMenuFocus::Update` → 활성 기본 대상의 실제 이동을 확인했다.
- 정상 텍스트 편집기의 현재 포커스, 다른 창에 있는 포커스와 **현재 조작은 로비 소유지만
  활성 최상위 창은 다른 경우**, 비활성 앱, 뷰포트에서 제거된 로비의 비간섭을 확인했다.
- 확인창의 비활성 확정 → 취소로 실제 이동하고 확인창은 열린 채 유지된다.
  확인/클릭·동의 dispatch나 HTTP는 실행하지 않는다.

창은 `SVirtualWindow` 기반이며 엔진의 inert `FGenericWindow` stub을 사용한다.
OS 핸들이 없음을 검사하고 시험용 regular-window 분류/합성 활성화 이벤트만 사용한다.
시험용 월드는 BeginPlay 없이 만든 최소 RPC 월드이므로 소유 컨트롤러를 엔진
`AddController`로 등록한다. 렌더링용 `FSceneViewport`·OS 창 생성/표시·실물 입력은 없다.
조작의 표시/활성 상태도 UI fixture로 구성하므로 실제 인증/요청의 전체 상태 전이 검사는 아니다.

최초 창 정리 어설션(UE 종료 3), 다음의 소유 PC 조회 실패, 이후 활성 창 이벤트 순서의
기대 실패를 각각 별도 DLL/소스 사본/로그/proof로 보존했다. Slate의 `SetUserFocus` 자체가
활성 최상위 창도 갱신하는 점을 반영해 시험의 이벤트 순서를 바로잡았다. 이 세 보완은
시험 환경에만 적용했고 제품 객체는 바꾸지 않았다. 최종 시험 CPP 개별 CL/분리 링크와
**해당 native 1개 Success·UE/호스트 종료 0**이다. 전체 440개 링크 객체 중 수정된 것은
새 시험 객체 하나뿐이며 선택 소스 7개/고정 입력 451개·실제 분리 DLL은 불변이다.
기동 Error 15줄/검사 이후 Error 0이고, fixture의 구형 `SetViewportOverlayWidget` 사용에
따른 C4996 경고 1개와 기존 에셋 경고는 남는다. 전체 러너 PS AST 오류 0·새 필수 이름/
입력 각 1개만 확인했으며 앞선 전체/native·서버/보상/600초 검사는 반복하지 않았다.

proof: `Saved/BuildChecks/LobbyFocusHandoff-20261029/Verification.proof.json`,
최종 실행: `ActivationOrderFix/Native-5eaac1e153f44e5e9ba694e52e129b4a/Native.proof.json`.
원본 DLL/Intermediate·열린 GUI/MCP·에셋/Config/backend/DB와 다른 담당을 유지했다.
정상 공유 모듈 적용 후 실제 화면에서의 키보드·패드/OS 창 전환·열린 콤보 사용감과
실제 요청 중/완료 전이 확인은 남는다. 이번 결과는 합성 활성화 환경의 실제 Slate 포커스
실행 증거이지 GPU·실물 입력·Steam/Shipping 또는 현재 전체 소스의 검증은 아니다.

## 원정 준비 화면의 키보드·패드 뒤로가기 (2026-10-29)

네이티브 원정 준비 화면에 `NativeOnKeyDown`의 Back 처리를 추가했다. 기존 확인창은
preview 단계에서 먼저 취소하며, 일반 뒤로가기는 **bubble 단계**에서만 처리한다.
텍스트 편집/열린 콤보가 먼저 키를 처리할 수 있고, Blueprint의 기존 `OnKeyDown`이
Handled를 반환하면 그대로 존중한다. 플랫폼 Back 매핑은 기존 `RPGConfirmationInput`을
재사용한다. 반복 키는 화면을 전환하지 않으며, 표시 중인 네이티브 준비 화면 밖에서는
새 Back 동작을 적용하지 않는다. 캐릭터 선택/로그인 화면에서 자동 로그아웃하지 않는다.

키는 기존 `ChangeCharacter`를 호출한다. 이 함수가 매번 다시 계산하는 버튼의 인증·
미저장 구성·미확정 저장·요청·파티/미완료 멤버십 잠금을 그대로 따른다. 잠긴 상태에서는
준비 화면을 유지하고 기존 비활성 사유를 표시한다. 허용된 화면 전환도 선택 캐릭터/
로그인·세션을 지우지 않으며 저장·출발/취소·탈퇴·HTTP·맵 이동을 만들지 않는다.
새 reflected 필드/타이머·저장 형식은 없고 기존 클래스의 입력 virtual override만 추가했다.

`DungeonLobbyBackInput.Preparation`은 소유 로컬 PC/최소 월드와 표시 전용 인증/선택을
구성하고 실제 `SObjectWidget::OnKeyDown`에서 새 virtual override로 전달되는 경로를
확인한다. Escape/패드 Back·repeat·Accept 비간섭·선택/로그인 보존·요청/멤버십 잠금·
확인창 우선·인증 만료로 숨은 준비 화면을 검사했다. 미저장/미확정 저장·파티는 기존
정책을 재사용하지만 이 새 사례에서 각각 독립 seed한 검증은 아니다. 자식 편집기/콤보의
실제 popup 키 처리와 뷰포트·실제 Slate 사용자/물리 장치·GPU 경로도 별도로 남는다.

위젯/시험/생성 묶음 개별 컴파일은 성공했지만 첫 분리 링크는 현재 생성 묶음에 들어온
새 훈련 타입 두 개가 이전 baseline에 없어 LNK1120(참조 11개)으로 실패했다. native는
시작하지 않았고 그 로그를 보존했다. **자체 생성 코드 사본만** baseline의 등록 범위에
맞춰 두 타입을 제외하고 생성 묶음만 재컴파일/분리 링크했다. 전후 등록 정의 132개가
같으며 다른 담당의 훈련 구현/원본 UHT 생성물은 수정하거나 합쳐 검증했다고 표시하지 않는다.
최종 집중 native **1개 Success·UE/호스트 종료 0**, 선택 소스 10개/선택 고정 입력 871개·
실제 분리 DLL 불변, 컴파일 경고 0·기동 Error 15줄/검사 이후 Error 0이다.
전체 러너의 입력/필수 이름 각 1개·PS AST 오류 0을 확인했다. 정상 전체 타깃/공유 DLL
반영은 아니며 완료된 전체/native·보상/600초 검사를 반복하지 않았다.

proof: `Saved/BuildChecks/LobbyPreparationBack-20261029/Verification.proof.json`,
최종 실행: `BaselineRegistrationFix/Native-fda39259889a41799221b2870e6fe8ec/Native.proof.json`.
원본 DLL/Intermediate·열린 GUI/MCP·HUD/훈련·에셋/Config/backend/DB와 타 담당을 유지했다.
다음은 조율된 정상 모듈 반영과 실제 키보드·패드에서 편집기/열린 콤보의 취소 우선 및
잠금 사유 안내를 포함한 사용성 확인이다.

## 검증 임시 사본 폐기 (2026-10-30)

새 공통 정리 지침에 따라 본인 `LobbyInputNavigation`/`LobbyStaleFocus`/
`LobbyFocusHandoff`/`LobbyPreparationBack`의 `-20261029` 검증 디렉터리 안에 남았던
입력·객체/PCH 사본과 분리 DLL/PDB·RuntimeProject/UserDir·캐시를 폐기했다.
소스·유지할 검증 스크립트·작은 proof/해시·필수 로그는 보존했다. 위의 소스/모듈 해시와
성공/실패 기록은 **당시 실행의 역사적 증거**이며 삭제된 바이너리 재실행이나 고정 입력의
재감사가 현재도 가능하다는 뜻이 아니다. 향후 실행에는 새 빌드/입력 준비가 필요하다.
원본 게임 모듈/Intermediate·에셋·다른 담당의 입력은 정리 대상에서 제외했다.

다른 본인 검증을 포함한 10개 디렉터리의 합계 27,233,618,745 bytes(25.363 GiB),
일반 파일 3,258개를 삭제하고 링크 34개는 대상 미추적으로 해제했다. 작은 보존 파일
233개와 원본 보호 파일 7개의 SHA256은 동일하며 잔여 후보/실패·보존 예외는 없다.
결과: `Saved/Maintenance/DungeonPartyTempCleanup-20261030/Result.json`.
게임 빌드/실행은 추가하지 않았다. 정상 통합 빌드는 현재 공유 UBT/Intermediate 담당의
예약 종료·인계 확인 뒤 진행한다.

## 정상 Editor 모듈의 로비·결과 입력 연결 (2026-10-30)

정상 빌드 예약이 에어본 담당과 겹친 것을 착수 전 재조회에서 확인해 본인 공유 예약을
해제하고 순차 인계했다. 해당 담당의 정상 타깃 성공(CPP 0/링크 6/경고 0)·native/네트워크
검사 종료와 전체 예약 해제를 확인한 뒤 **빌드를 반복하지 않고** 정상 DLL을 사용했다.

- 실제 설치 경로 `Binaries/Win64/UnrealEditor-Project_RPG.dll`의 SHA256은
  `C428B66FA0BC1A164ACDCD86F6D590278D2BBC80F8F2BDC4BF408AA8981070ED`다.
  자체 Cmd에서 이 원본 경로를 실제 로드했고 native 프로세스는 종료 0이다.
- `DungeonLobbyBackInput.Preparation`과 `DungeonResultNavigation.NativeControls` 두 사례가
  같은 실행의 JSON/완료 로그에서 모두 Success/사례 Error 0이다. UI 관련 기존 전체 suite나
  완료된 전투/보상/600초 검사를 반복하지 않았다.
- 결과는 **순수 성공 1개 + 경고 포함 성공 1개**다. 로비 fixture의 GI/콘텐츠 로딩 동안
  프로젝트 에셋 경고 140개가 기록됐다(LogBlueprint 12/LogUObjectGlobals 1/LogLinker 95/
  LogEnhancedInput 1/LoadErrors 31). 누락 리소스·데모 BP의 입력/컴파일·외부 클래스 로드
  경고 등을 보존했으며 이를 해소하거나 경고 없는 화면 성공으로 표시하지 않는다.
  기동 Error 15개/시험 발견 이후 Error 0, assertion/ensure/fatal 없음이다.
- 최초 실행기는 경고 성공을 실패로 분류했고 콘솔 리디렉션 핸들 때문에 정리도 중단됐다.
  최초 `Result.json`을 유지하고 경고 성공/실패를 구분하도록 실행기를 보완했다.
  이미 종료한 프로세스의 관측/JSON/로그와 같은 소스·모듈을 **읽기 전용 재감사**했으며
  게임 코드 수정/두 사례 재실행은 없다. 재감사 첫 정리의 동적 함수 경로 문제도 별도
  `InitialAudit.proof.json`에 남겼고 경계를 명시해 최종 정리를 마쳤다.
- 선택 입력 1,317개 및 정상 모듈 해시는 실행 뒤/재감사 시 같았다. 자기 Temp의 UserDir/
  에셋 레지스트리 캐시·보고서/중복 콘솔 로그 **140,018,147 bytes(133.53MiB)**를 폐기해
  잔여/예외 0이다. 작은 JSON 보고서·원본 로그·해시/명령·실패/최종 감사만 보존했다.
  원본 정상 DLL/Intermediate·Source/Config/Content/Plugins는 유지했다.

근거: `Saved/BuildChecks/OnlineEditorIntegration-20261030/1a15b6d60a16410e87c53b45d868a8a0/`
의 `Result.json`(최초 실행기 실패), `NativeReport.json`, `Native.log`, `Inputs.json`,
`InitialAudit.proof.json`(첫 정리 실패), `Audit.proof.json`(최종 재감사/정리 통과).
프로젝트 제품 소스와 에셋 저장은 없고 이번 검사 후 본인 예약을 해제했다.
정상 모듈은 유지하지만 해시 기록은 해당 시점 증거이므로 이후 교체된 DLL로 이 결과를
확장하지 않는다. 실제 화면/GPU·물리 키보드/패드와 서버 요청 전이·Steam은 별도다.

## 훈련 전용 개발 백엔드 프로필 (2026-10-31)

`DungeonContent:DevelopmentProfile=Training`(환경 변수 `DungeonContent__DevelopmentProfile=Training`)
선택 시 Development/Testing에서만 별도 `DevelopmentProfiles/Training.json`을 사용한다.
파일은 build/publish 출력 복사 대상으로 지정했다. 실제 publish 실행/운영 배포는 별도다.

- 훈련 `Progression.Training / Normal` 하나를 `/Game/Maps/Progression/L_ProgressionTraining`에
  연결하며 아직 검증하지 않은 다인 훈련은 해당 프로세스의 최소 출발/최대 인원 1로 제한한다.
- 기본 목록과 인덱스 배열을 병합하지 않는 완전 카탈로그 스냅샷이다. 기본 Easy/Hard/Hell·
  보스/미구현 콘텐츠가 섞이지 않고 실행 중 파일 변경으로 카탈로그가 바뀌지 않는다.
- 키 미지정은 기존 동작이다. 빈 값/오타/경로형 이름·잘못된 문서와 Production/Staging의
  일반 기동은 실패하며 자동 fallback하지 않는다. 이름을 임의 파일 경로로 해석하지 않는다.
- 전체 검증 뒤 파티 설정 두 개만 프로세스 안에서 적용한다. 인증/Steam·저장소/연결 문자열·
  티켓/멤버십/성장/정산 정책은 덮어쓰지 않으며 Testing 허용도 개발 인증 제한을 완화하지 않는다.

별도 개발 PowerShell에서의 Memory 예시(재시작 시 데이터 소멸, 기존 backend 중지 없음):

```powershell
$env:ASPNETCORE_ENVIRONMENT = 'Development'
if ($env:POSTGRES_CONNECTION_STRING) { throw 'Use a separate shell without an inherited PostgreSQL override for this Memory example.' }
dotnet run --no-launch-profile --project Backend/ProjectRpg.Backend/ProjectRpg.Backend.csproj -- --Storage:Provider=Memory --DungeonContent:DevelopmentProfile=Training --Kestrel:Endpoints:Http:Url=http://127.0.0.1:3075
```

기존 PostgreSQL 환경 override 우선순위는 유지한다. 운영/기존 게임 DB에 이 개발 프로필을
배포하지 않는다. 예시 포트도 비어 있어야 하며 기존 프로세스/포트를 강제로 종료하지 않는다.

자체 Release 빌드 경고/오류 0·새 집중 7그룹/검사 종료 0이다. 기본/선택/잘못된 문서·환경
경계와 실제 동적 loopback/Memory/개발 인증 HTTP에서 방 생성 → 방장 출발 전 claim 204 →
출발 후 같은 훈련 맵/Normal 할당·서버 자격 발급을 확인했다. 다른 콘텐츠/난이도·비로그인·
두 번째 참가자/원래 방 변경·플레이어의 관리자 할당은 거절됐고 실제 Production 기동도
리스너 전에 거절됐다. 성공 개발 호스트는 시험 소유 프로세스로 계획 종료했으며 drain 검증은 아니다.

첫 시험의 참가 URL `/join` 오타 404는 시험만 기존 `/members`로 고쳤다. 첫 실패/최종 성공은
분리 보존했다. 입력 98개/검증 DLL 2개 해시·원본 불변, 두 Temp 소스/Release 사본·DLL/캐시
17,972,346 bytes(17.14MiB) 자동 폐기·잔여/정리 예외 0이다. 작은 기록만 보존했으므로 폐기한
DLL 재실행에는 새 빌드가 필요하다. 근거는 `Saved/BuildChecks/TrainingDevelopmentProfile-20261031/`
아래 `2848242d869744efb101120616bd6742/{Result.json,Build.log,Tests.log}`와 앞선 실패 디렉터리다.

**기본 UE 로비는 기존 보스만 선택하므로 훈련 backend와 사용할 때는 아래 개발 로비 옵션도 필요하다.**
아래 후속에서 선택 목록을 연결했으며 실제 맵/목표/XP·결과/복귀의 통합 확인은 남는다.
위 backend 단계에서는 UE 실행/빌드·Source/Config/Content/정상 DLL 변경,
게임 DB/PG·Steam·기존 게임 suite 반복을 하지 않았으며 HTTP 성공을 전체 플레이 성공으로 계산하지 않는다.

## 개발 훈련의 단일 실행기 (2026-10-31)

[Start-TrainingDevelopment.ps1](../Scripts/Start-TrainingDevelopment.ps1)로 private Memory backend·기존 Training 프로필/로비 옵션·출발 후 기존 할당기를 함께 실행할 수 있다. 기본 모드는 별도 게임 창에서 직접 조작하며, 원본 Config/에셋/정상 DLL을 편집하지 않는다. 한 번의 개발 훈련용 임시 저장으로, 종료하면 해당 Memory 계정/캐릭터/성장은 소멸한다.

새 스크립트의 headless smoke에서 프로필→실제 서버 claim/훈련 맵 readiness→기존 클라이언트 입력 네 목표→Cleared/XP100·Lv2·SP3를 확인했다. 생성/출발은 이 검사에서 HTTP로 준비했으므로 로비 UMG 클릭·결과/복귀 UI 전체 성공으로 계산하지 않는다. [실행 명령·자격/프로세스 경계·증거와 정리](TRAINING_DEVELOPMENT_LAUNCHER.md)를 따른다.

## 개발 클라이언트의 훈련 선택 목록 (2026-10-31)

일반 실행의 Config/CDO/에셋을 바꾸지 않고 클라이언트의 명시적
`-RPGDungeonProfile=Training -RPGDevelopmentLogin -nosteam` 조합에서만 저장된
`DA_Dungeon_Training`을 로비 인스턴스의 유일한 정의로 읽는다. 기본 생성 선택은
`Progression.Training / Normal`이며 실제 기본 드롭다운에도 같은 단일 목록을 연결했다.
backend는 위 `DungeonContent:DevelopmentProfile=Training` 프로필을 별도로 선택해야 한다.

- 기존 `IsDevelopmentUIEnabled()`를 사용하므로 Shipping에서는 이 전환을 허용하지 않는다.
  프로필 누락은 원래 목록, 오타/빈 값/중복·사용 불가 문맥은 명시적 목록 오류다.
  잘못된 프로필을 기존 보스나 이전 훈련 목록으로 자동 대체하지 않는다.
- 실제 정의가 훈련 ID·Normal 전용/기본값과 다르거나 로드되지 않으면 기존 검증으로 거절한다.
  목록 변경은 인스턴스의 생성 선호일 뿐 기존 백엔드 세션의 ID/난이도를 덮어쓰지 않는다.
- 인증·소유 캐릭터·출발·입장/티켓/멤버십·보상/재접속 판정은 기존 경로 그대로다. 프로필은
  인증이나 입장을 승인하지 않고, 권한 없는 `CreateSelectedDungeonSession()`도 계속 실패한다.

수동 개발 실행에서는 `/Game/Maps/LobbyMap -game`과 위 세 옵션을 사용한다. backend 예시가
3075라면 클라이언트/서버의 기존 `HttpWebManager`, `RPGItemBackendSubsystem`,
`RPGEconomyBackendSubsystem`의 `ApiUrl`도 모두 `http://127.0.0.1:3075/api`에 맞춰야 한다.
기존 실행기의 `-ini:Game:[/Script/Project_RPG.HttpWebManager]:ApiUrl=...` 등과 같은 방식이며
원본 INI를 저장할 필요가 없다. 실제 서버 할당·기동은 기존 allocator의 관리자 경로를 사용한다.

정상 Editor 증분 빌드 **CPP 3/링크 2/메타데이터 1, 종료 0·컴파일 경고 0**으로 설치했다.
실제 원본 DLL SHA256은 `4EDC2193B0D1B25213AE8EA35DD125445CEDE5BAC2E569364C99B4E899493637`이다.
같은 원본 경로를 로드한 Cmd의 새 native `DungeonDevelopmentProfile.Policy`/`.Widget`
**2개 모두 경고 없이 Success/종료 0**, 기동 Error15/발견 이후 Error0이다. 옵션/중복/사용 불가
정책과 실제 NativeConstruct·저장 에셋 로드·콤보/Normal·반복 로드·미완료 세션 보존·잘못된
프로필 오류·원래 보스 목록/CDO 불변을 확인했다. 기존 광역 suite나 완료된 보상/전투 검사는 반복하지 않았다.

선택 입력 1,321개 및 실행 모듈 해시가 같았다. 자체 Temp/UserDir/캐시/보고서
**139,938,072 bytes(133.46MiB)**를 자동 폐기해 잔여/예외 0이며 작은 결과/JSON 보고서·로그/
명령/해시와 정상 빌드 출력은 유지한다. 근거는
`Saved/BuildChecks/TrainingLobbyProfile-20261031/9449a5bf1f4d488dbeb5422f78316879/`
의 `Result.json`, `Build.log`, `Native.log`, `NativeReport.json`, `Inputs.json`이다.

이 결과는 실제 HTTP 입장·훈련 입력·정산·결과/복귀를 이 옵션으로 끝까지 실행했다는 증거가 아니다.
다음은 이 목록과 backend 프로필을 사용한 로비→티켓 입장→목표/XP→결과/복귀 연결이다.
실물 키/패드·GPU·Shipping 패키지·Steam/PG·운영 공개는 별도다. 본인 소스/공유 빌드·입력 예약은
해제했으며 소비 담당은 이후 최신 해시를 새로 확인해 공유 빌드를 인계받을 수 있다.

## 훈련 완료 결과·복귀 UI의 누락과 독립 패치 (2026-10-31, 미적용)

이 절은 후보 준비 당시의 기록이다. **2026-11-01 원본 적용·정상 DLL 반영과 실제 훈련 후 복귀 검증을 완료했다.** 아래의 [원본 반영 후속](#훈련-44-이후-결과로비-복귀의-원본-반영-2026-11-01)을 현재 상태로 본다.

현재 `ARPGTrainingGameMode`는 기존 정산을 보고하지만, Controller의
`EnsureDungeonResultActions`는 BossResultWidget/보스 스냅샷을 전제로 한다. 따라서
훈련 guide의 네 목표가 완료돼도 안내 문구만 바뀌고 결과/복귀 조작이 생성되지 않는다.
직접 맵 이동이나 별도 보상 요청으로 우회하지 않고 기존 `URPGDungeonResultActionsWidget`을
훈련 안내의 완료 수명에 연결하는 후보를 준비했다.

후보는 로컬 클라이언트·살아 있는 현재 소유 Pawn·활성 안내, 정확한 계정/선택 캐릭터/
PlayerState/훈련 Normal 세션·멤버십을 대조한다. 개인 실습이 완료되면 기존 비패배 결과
조작을 한 번 표시하고, 실제 정산 상태 GET/인증 만료·재로그인/복귀 클릭의 fresh GET과
terminal 판정은 기존 위젯/Flow에 맡긴다. 개인 완료를 보상 지급/파티 전체 성공으로
판정하지 않고 새로운 clear/정산/RPC/abandon API를 추가하지 않는다. Pawn/신원/세션/
안내 수명이 끝나면 자기 결과 위젯의 구독/타이머를 정리하고 자기 포커스일 때만 입력 모드를
복구하도록 작성했다. 후보는 게임에서 실행된 코드가 아니다.

현재 platform-player-security 담당이 전체 Source/공유 빌드/검증 입력을 예약했고 소비
접합도 선행 대기 중이므로 **원본 두 Source 파일은 수정하지 않았다.** 최종 인계물은
`Saved/BuildChecks/TrainingCompletionActions-20261031/`의 다음 작은 파일이다.

- `TrainingCompletionActions.patch`: 원본 guide h/cpp 두 파일 대상 후보(100줄 추가/1줄 제거).
- `Candidate/Source/.../RPGTrainingGuideWidget.{h,cpp}`: 검토 가능한 후보 원고.
- `OriginalHashes.json`, `Prepared.proof.json`, `Package.ps1`: 원본/후보 해시와 적용 문맥 점검.

원본 두 파일의 SHA256 불변과 `git apply --check --whitespace=error` 종료 0을 확인했다.
이는 패치의 문맥/공백 확인일 뿐 C++/UHT/링크/native/실제 UI 성공이 아니다.
첫 포장 도구의 PS5 UTF-8 문자열 조건은 원본 변경 전에 실패했고 코드포인트 조건으로 보완했다.
대형 실행 사본/캐시/프로세스를 만들거나 정리하지 않았으며 작은 후보/패치는 최종 인계물로
보존한다(약 29KB). 본인 원본 Source/공유 예약은 없고 준비 경로 예약도 완료 시 해제했다.

선행 담당 종료 후 원본 해시/문맥이 그대로인지 재확인하고 별도 예약해 적용해야 한다.
변경됐다면 강제 적용하지 않고 재기준화한다. 다음 검증은 완료 이후 진행 복제/성장 세대 변화,
인증·HTTP 실패/다른 계정·세션, Pawn 사망/교체·HUD 종료/포커스 수명과 정산 전 복귀 차단이다.
그 뒤 실제 로비/티켓 입장→훈련 입력/XP→이 결과 조작으로 로비 복귀를 연결한다.

## PIE 로비 패키지 판별 보완 (2026-10-31, 실행 확인 대기)

이 절의 초기 대기는 **2026-11-01 정상 DLL/native3 및 설정 override 없는 실제 PIE 로그인·훈련 입장 확인으로 해소됐다.** 아래 원본 반영 후속의 모듈/증거를 사용한다. 초기 실패·인계 기록은 그대로 보존한다.

로비 UI 자동 표시의 `URPGOnlineLobbySubsystem::IsLobbyController`가 맵 패키지명을
그대로 비교해, 에디터 플레이의 `/Game/Maps/UEDPIE_0_LobbyMap`을 기본
`/Game/Maps/LobbyMap`과 다른 맵으로 판정했다. 앞선 `-LobbyUISmoke`는 검증 사본의
설정만 바꿔 이 조건을 맞췄으며 제품 코드가 PIE를 지원한다는 증거는 아니었다.

현재 원본 판별은 **WorldType이 PIE일 때만** 엔진 `UWorld::RemovePIEPrefix`로
실행 중 패키지명을 정규화하도록 보완했다. 일반 Game/GamePreview의 패키지,
전체 디렉터리·대소문자 일치, 같은 GameInstance·로컬 컨트롤러·게임 월드·Standalone
조건은 유지한다. 로그인/로그아웃·인증·세션/티켓·이동 권한을 새로 부여하지 않는다.

신규 `RPGOnlineLobbyWorldTests.cpp`는 일반/PIE 정확한 맵과 인스턴스0/7,
던전·다른 디렉터리/대소문자·PIE처럼 이름 붙인 일반 맵·다른 GI·Editor·네트워크
client/listen-server 거절을 검사하도록 작성했다. **아직 컴파일/실행된 검사가 아니다.**
테스트의 프로퍼티 조회 한 줄 보완도 공유 읽기 인계 뒤 적용해야 한다.

본인 예약 등록 뒤 HUD 취소 담당이 전체 Source 읽기/공유 NoLink·분리 링크 범위로
확장한 것을 확인해, 두 원본은 2026-10-30 17:22:59 UTC의 변경 이후 고정했다.
본인의 공유 빌드/추가 Source 쓰기는 중지했고, 새 테스트의 정적 점검 오류와 순차
보완 요청을 조율 표에 기록했다. 다른 담당의 프로세스·에디터/원본 DLL은 종료하거나
교체하지 않았다. `Saved/BuildChecks/LobbyPIEMap-20261031/Verify.ps1`는 구문 확인만
끝냈으며, 해당 담당의 명시적 해제 후 보완·재예약하여 native3개와 **패키지 설정
override 없는** 별도 PIE의 로그인 화면을 확인할 계획이다. 기존 실행기의 검증용
override와 설치 DLL9968…은 현재 유지한다.

## 훈련 4/4 이후 결과·로비 복귀의 원본 반영 (2026-11-01)

사용자의 실제 훈련에서 네 조작을 끝낸 뒤 다음 화면이 나오지 않았다. 당시 guide의
완료 분기는 “조작 실습 완료! 다음 안내를 기다려 주세요”라는 문구만 바꿨고,
훈련에는 보스 결과 스냅샷이 없어 기존 Controller의 결과 UI 생성 경로도 실행되지 않았다.
기다리는 시간이나 사용자의 조작 문제가 아니라 **완료 이후 화면 연결의 누락**이었다.

`training-completion-actions-20261031` 담당의 준비된 guide h/cpp 패치를 원본 해시/
문맥 대조 후 그대로 인계·적용했다. 현재 클라이언트의 활성 guide·살아 있는 소유 Pawn,
인증된 Steam/선택 캐릭터/PlayerState, Training/Normal 세션·멤버십을 대조해 개인 실습
4/4에서 기존 `URPGDungeonResultActionsWidget`을 한 번 표시한다. 기존 위젯이 세션을
조회하고, **서버의 terminal 상태를 fresh GET으로 확인한 뒤** Return to lobby를
허용한다. 클라이언트의 실습 완료로 XP/clear를 요청하거나 직접 OpenLevel하지 않는다.
신원/세션·Pawn/위젯 수명이 끝나면 자기 결과 UI의 구독/타이머와 자기 포커스만 회수한다.

정상 Project_RPGEditor 모듈 빌드 종료0(첫 CPP7/링크2, 로비 fixture 보완 CPP1/링크2)
후 설치 DLL은 `F1A202697D2FC64E36045B5B01C7B17FFC37D699A580CBB83A017242E1AC5654`다.
사용자 UE가 기동하지 않은 상태에서 반영했고 다른 담당의 독립 사본/실행을 종료하지 않았다.
전체 Source/Config/Plugins·공유 컴파일/링크 입력은 자기 실행 사본을 닫은 뒤 먼저 해제했다.

- 같은 DLL의 guide4/로비 월드2/로그아웃 정책1, **native7 성공**(순수5/경고 포함2,
  기동 Error15/발견 후 Error0). 처음 로비 검사2 실패는 fixture의 로컬 컨트롤러 설정
  누락으로 구분하고 `SetAsLocalPlayerController`/등록을 보완했다. 별도 실습 완료 표시
  검사는 처음부터 통과했으며, 일반 월드/인증 없는 표시만으로 결과 UI를 생성하지 않는다.
- 실제 Memory/HTTP 티켓/Iris 훈련은 네 동작 입력으로 서버/클라이언트 Mask15,
  Cleared·XP100/Lv2/SP3·고유 능력 해금을 확인했다. 결과 위젯 **1개**, 활성화된 복귀
  버튼의 **기존 UMG 핸들러**를 한 번 호출해 같은 계정/선택 캐릭터의 실제 LobbyMap에
  도착했다. 세 번 성장 조회 불변·다른 캐릭터 불변·UE 서버/클라이언트 종료0/0이다.
  물리 마우스 입력이나 Steam/PG·다인 검증은 아니다.
- 복귀 때 기존 프로토콜은 서버가 소켓을 닫으므로 엔진이 client ConnectionLost를 Error로
  기록한다. 서버의 `ErrorContext="dungeon_planned_lobby_disconnect"` **1건**과 결과 버튼
  호출~동일 신원 로비 도착 **사이**의 정확한 client 오류1건만 별도 집계했다.
  예상 밖 오류는 양쪽0이며, 다른 오류/중복/범위 밖/서버 증거 없음은 계속 실패한다.
  이 경계는 CRLF 포함12사례로 확인했다. 전체 오류0으로 숨기지 않는다.
- nullRHI 첫 시도는 입력15까지 통과했으나 UI 갱신 관측에서 시간 초과했다. 완료 UI
  모드는 실제 Slate 갱신이 있는 D3D12 offscreen 게임 viewport를 사용하도록 보완했다.
  설정 변경 전 자기 gate로 중단한 시도, 기능상 복귀 성공 뒤 오류 집계로 실패한 시도와
  마지막 통과를 구분해 보존했다. 기본/차지 취소 검사의 모드와 권한은 유지한다.
- 같은 모듈의 full Editor/Slate 로그인→캐릭터 생성/선택→훈련 방/출발/입장→가이드0/4도
  통과했다. PIE 패키지명 임시 override를 런처에서 제거했고 기본 설정으로 검증했다.
  이것은 실제 훈련 완료 후 복귀 검사와 구분된 로비 진입 검사다.

증거는 `Saved/BuildChecks/TrainingCompletionInstall-20261031/Verification.proof.json`,
실제 복귀는 `Saved/Tests/TrainingPlaytest/c4368ebc069e40fcab499e1727d1c987/Verification.proof.json`,
native는 같은 폴더 계열의 `7bef121438e64b9eb78c3c414535f9e7/NativeReport.json`,
기본 PIE 로비는 `Saved/TrainingDevelopment/88a5f69ef2724dcf9b5536e6d72241f4/Result.json`이다.
자기 Temp DLL/실행 프로젝트/UserDir·캐시/중복 Console 합계 **797,262,832 bytes
(760.33MiB)**를 자동 폐기했고 잔여/정리 예외는 없다. 정상 설치 DLL/PDB는 유지할 출력이며,
폐기한 검사 사본은 당시 해시/명령 증거만 남아 새 실행에는 새 고정 사본이 필요하다.

이미 켜 둔 개발 게임은 이전 사본을 사용한다. 게임 창을 닫고 훈련 실행기를 새로 시작해야
이 변경이 적용된다. 기본 Memory 수명은 유지되며, 일회성 실행기의 연속 서버 재할당이나
후속 던전/장비 지급·강화 명령까지 구현된 결과는 아니다.

## 카탈로그 제거 뒤 확정 출발 요청의 복구 (2026-11-01)

운영 카탈로그에서 던전/난이도가 빠지면 이미 확정된 출발의 같은 요청도 HTTP에서 409로
거절되던 결함을 수정했다. 새 출발의 선택 제한은 유지하되, 저장된 요청 ID+원래 버전·출발
시각이 있고 현재 상태가 Waiting/Loading/InProgress인 정확한 재전송은 복구로 분류한다.
`DungeonDeparturePolicy.IsCommittedRequestReplay`를 API와 기존 저장 정책이 함께 사용한다.

이 판별은 권한 승인이 아니다. API가 조회한 스냅샷을 그대로 성공 반환하지 않고 기존
저장소의 gate/트랜잭션 안에서 현재 방장/계정·멤버/임대·요청을 다시 확인한다. 조회 뒤 취소나
명단 변경이 일어나도 원래 버전 검사로 새 출발을 만들지 않는다. 새 UUID/다른 원래 버전,
취소된 요청과 terminal 세션은 카탈로그를 우회하지 못하며 기존 취소 경로는 유지한다.
카탈로그 snapshot/claim·맵 선택·입장/정산·저장 형식/UE 코드는 바꾸지 않았다.

`--departure-catalog-recovery-only`는 같은 Memory 저장소에 서로 다른 불변 카탈로그의
실제 Kestrel 호스트를 연결해 설정 교체 상황을 모사한다. 실제 Bearer/HTTP 집중7그룹과
직접 영향 Memory 정책4그룹이 통과했고 Release 빌드 종료0·경고/오류0, 입력124개 불변이다.
새 카탈로그의 최소 인원 변경에도 확정 재전송은 상태·전체 멤버/임대·버전·배정을 그대로
보존하며, 잘못된 신원/요청과 취소·terminal 거절을 대조했다. 실제 프로세스 재시작의
PostgreSQL 복원이나 UE/Steam 통합·운영 배포의 성공은 아니다.

초기 테스트 호스트의 설정 provider/진단 누락 실패2회, 이후 수정 전 실제409 재현과 최종
통과를 별도로 보존했다. 네 시도의 자기 artifacts/bin/obj 총38,399,668 bytes(36.62MiB)를
각 finally에서 자동 폐기해 임시 실행 경로/정리 예외0이며, 원본 공유 출력/서비스·DB·타 담당은
유지했다. 작은 명령/입력 해시/실패·최종 로그는
`Saved/BuildChecks/DepartureCatalogReplay-20261101/Summary.proof.json`을 따른다. 삭제한
검사 DLL 해시는 당시 증거만 남아 있고 같은 바이너리 재실행 가능 상태는 아니다.

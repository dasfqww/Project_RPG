# 입문2: 공격 기회 실습

첫 챕터 M05의 전투 기반이다. M05의 다온 발견·증언·개인 의뢰 카탈로그는 후속 작업이다. 상위 기준은 [첫 챕터 기획](SCENARIO_FIRST_CHAPTER_PROPOSAL.md), [확정 정책 C1~C9](SCENARIO_QUEST_DIALOGUE_DESIGN.md)이며 기존 훈련·입문1과 실제 정산 조건을 보존한다.

## 플레이

`Scripts/Start-TrainingDevelopment.ps1 -DungeonProfile Intro2`로 실행한다. Development/Testing의 명시 개발 프로필만 Training/Intro1/Intro2 Normal을 제공한다. 기존 Training/Intro1 프로필의 범위는 유지된다. 캐릭터 자신의 확정 훈련 기록으로 입문1, 확정 입문1 기록으로 입문2를 선택한다. 클라이언트 표시와 별도로 백엔드가 입장 조건을 재검사한다. 챕터 의뢰 개발 옵션 `-ChapterOne`은 현재 M01~M04용 Intro1 프로필을 유지한다.

전용 맵 `/Game/Maps/Progression/L_ProgressionIntro2`에서 정면의 고정 실습 표적에 접근한다. 안내 카드의 **예고·대기 → 공격 기회 → 대기**를 확인하고 열린 기회에 기본 공격 또는 기존 피해 스킬을 적중한다. 예고 2초, 기회 3초, 회복 3초이며 서버 시각을 따른다. 서로 다른 기회에서 총 2회 인정하면 기존 파티 완료 gate와 정산·결과·로비 귀환 UI로 이어진다. 이 실습 표적은 이동·공격 AI와 사망 드롭을 사용하지 않는다.

## 승인과 저장 경계

- 기존 GAS의 검증된 TakeDamage 처리에서 실제 체력이 감소한 적대 적중만 관찰한다. 공간 진입·애니메이션·클라이언트 요청으로 완료하지 않는다. 새 완료 RPC는 없다.
- 현재 입문2 서버·Normal 할당, 인증된 참가 캐릭터·Controller/Pawn·연결 세대·불변 스킬 복원·살아 있는 전투 수명이 일치해야 한다. 다른 표적·현재 회차가 아닌 오래된 적중·닫히거나 만료된 창·사망·권한 철회는 인정하지 않는다.
- 같은 표적의 같은 회차는 개인별 1회만 인정한다. 파티원 진도는 각자의 소유자에게만 복제한다. 기존 파티 gate가 현재 참가자 전원의 새 완료 관찰을 확인한 뒤 정산한다.
- 전투 대응은 **이번 시도**의 사실이다. 재접속·새 Pawn·스킬 복원 세대 변경·사망 등 수명 변경 때 0회로 초기화한다. 다온 발견의 개인 영속 저장과 혼합하지 않는다.
- 성공은 기존 Intro2 영수증으로 저장한다. XP100·광석2, 정상 순차 최초 진행 후 누적 XP300·SP9를 유지한다. 새 스킬 자동 편성·성장 보정·스토리 스킵 지급은 추가하지 않는다. 반복 조회는 추가 성장 지급을 만들지 않는다.

## 작성과 검증

`Scripts/Editor/CreateProgressionIntro2Content.py`는 호환 모듈에서 읽기 검사한다. `RPG_INTRO2_REPORT`에 절대 결과 경로를 지정하고 `-RPGCreateProgressionIntro2Content`를 추가한 경우에만 없는 소유 에셋 5개를 생성한다. 기존 보상·선택 정의·다른 맵을 변경하지 않는다. 소유 표식이 다른 기존 에셋이나 미저장 변경은 보존한다.

`Scripts/Tests/Run-IntroOpportunityTests.ps1 -RuntimeProjectRoot <호환 프로젝트>`는 미인증/위조 진도·공간만의 창 개방·입장 프로필 경계를 native automation으로 검사한다. `Scripts/Tests/Run-ChapterOnePlaytest.ps1 -RuntimeProjectRoot <호환 프로젝트> -OpportunityPractice`는 별도 Memory 백엔드와 실제 Iris 서버/클라이언트에서 훈련 입력, M03 재접속, M04 수락·입문1 실습, 입문2의 닫힌 창·동일 회차·재접속 초기화·서로 다른 회차 적중과 실제 정산을 확인한다. 진행 수치·정산을 주입하지 않고 정상 입력과 입장·조회 API를 사용한다. 자기 런타임 UserDir·백엔드 출력은 종료 후 정리한다.

로컬 최소 근거는 `Saved/BuildChecks/Intro2Opportunity-20261117`에 둔다. 개발 인증·Memory·합성 입력 검증은 실제 Steam 인증, PostgreSQL, 여러 사람의 물리 입력, Shipping/cook 검증과 구분한다. 실행 중인 다른 담당의 에디터/DLL을 교체하지 않는다.

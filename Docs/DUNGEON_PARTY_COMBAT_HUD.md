# 던전 파티 전투 정보 HUD

## 현재 범위 (2026-10-27, 실제 2인 전투·접속 표시와 로컬 Slate 입력 관측)

파티 기반 던전/레이드에서 같은 세션의 명단·재접속 대기와 서버가 관측한 체력·생존·전투 불능을
확인하는 네이티브 표시 모델과 WBP 기반의 읽기 전용 HUD다. 로비 화면이 아니라 인증된 던전 PlayerController의
컴포넌트와 소유 클라이언트 화면에 연결했다. 기존 GAS 값과 사망 태그를 읽으며 전투·처치·
부활·전멸/재도전·정산·멤버십을 변경하는 기능은 아니다.

연결·저장된 좌측 중앙형 WBP로 실제 개발 인증/PostgreSQL/Iris의 2인 명단·초기 체력,
서버 승인 피해의 체력 감소→0/전투 불능과 다른 멤버 150 유지, 소유 클라이언트 강제
종료 후 유예 행 유지/체력 바 숨김, 새 티켓 재접속 후 표시 복귀를 관측했다. 의도적 서버
피해 fixture이며 자연 전투·확정 탈퇴/10분 자연 만료는 아직 별도다. 후속 입력 전용 실행에서
HUD 숨김/표시의 로컬 Slate 이동·기본 공격과 포커스/커서 유지도 확인했다. 물리 입력·서버의
이동/공격 승인이나 모든 스킬/메뉴 조합까지 확인한 것은 아니다.
PIE 로비 완료 판정 수정 후 최종 실행은 보상·양쪽 로비/Idle 복귀까지 통과했다. 아래 첫
관측의 부분 성공/실패 기록과 최종 실행을 구분하며 입력·자연 만료·Steam 성공으로 확대하지 않는다.

## 서버와 복제 경계

- `URPGDungeonPartyCombatComponent`는 Controller의 기본 서브오브젝트다. 서버에서
  0.25초 간격으로 기존 멤버십 조회 캐시와 GameState의 PlayerArray를 읽고 스냅샷이 바뀐 경우에만 갱신한다.
  서버 전용 상태 수집과 로컬 화면 갱신을 분리했으며 새 HTTP/RPC/DB 요청은 없다.
- 수신자의 인증된 PlayerState·현재 Controller/소유 관계·활성 상태를 확인한다. 같은 월드의
  동일한 세션 ID, 현재 Controller/PlayerState 관계와 유효한 인증을 가진 참가자만 체력 샘플에 포함한다.
  다른 세션·미인증·inactive·Controller/PlayerState/Pawn의 권한 회수 상태는 활성 샘플에서 제외한다.
  접속이 끊긴 명단은 아래 계약에 따라 별도 유지한다. 수신자 범위가 유효하지 않거나 같은
  캐릭터 ID의 모호한 중복/소유자 불일치·128행 초과가 있으면 전체 표시를 비운다.
- 전체 스냅샷은 `COND_OwnerOnly`다. 수신자 자신의 세션/캐릭터 ID로 표시 범위를 고정하고
  읽을 때도 현재 소유자와 정확히 일치해야 한다. 기존 PlayerState의 캐릭터/Steam/세션·재화
  owner-only 필드를 공개 복제로 바꾸지 않는다. 각 파티원 행에는 공개 PlayerState 이름,
  런타임 번호/수신자 내 안정적인 행 번호·본인 여부·체력/재접속 표시 상태만 담고 타인의
  계정/캐릭터 ID·아이템·보상은 넣지 않는다. 서버의 명단 결합용 ID/계정 캐시는 복제하지 않는다.
- 이름은 복제 전에 trim/64자 상한/제어 문자 제외를 적용한다. 기존 서버 전용 멤버십 API의
  backend 캐릭터 이름을 우선하고 없으면 공개 `PlayerState.GetPlayerName()`의 마지막 확인 이름을
  사용한다. 이름이 없으면 수신자 내 행 번호를 사용하며 이름으로 신원·입장·보상을 판정하지 않는다.

## 표시 규칙

현재 Pawn이 해당 PlayerState와 연결되고 ASC의 Avatar와 일치할 때만 GAS 체력/최대 체력과
사망 태그를 읽는다. 유한한 현재 체력 0 이상·최대 체력 양수인 경우만 체력 수치를 표시한다.
서버의 사망 태그 또는 확인된 체력 0은 `전투 불능`, 양수는 `생존`이다. Pawn/ASC/체력 정보가
없거나 잘못되면 `상태 확인 중`이며, 이때 체력 바도 접어 사망/빈 체력으로 오해하지 않게 한다.
표시 수치는 최대 체력 범위로 제한하지만 GAS/서버 전투 값은 수정하지 않는다.

로컬 Controller가 0.25초 간격으로 동일 범위의 스냅샷을 읽고 좌측의 수동 조작 없는 HUD를
표시한다. 본인 우선·수신자 내 안정적인 행 번호 순서여서 재접속 시 같은 행이 유지된다.
현재 신원이 달라지거나 표시할 수 있는 범위가 사라지면
창을 제거하며 Controller EndPlay에서도 정리한다. 기존 보스/결과·가방/우편 UI의 입력 모드,
기본 포커스·전투 입력을 변경하지 않고 전체 HUD는 HitTestInvisible이다. Native BP/폰트 에셋
저장 없이 동작하도록 작성했으며 서버와 화면의 공통 presentation 형식을 분리했다.

## 멤버십 명단 유지와 재접속 표시 (2026-10-25, 구현/링크·실행 대기)

기존 서버 전용 membership-snapshot GET에 표시용 `characterName`·`disconnectedAt`·
`reconnectExpiresAt`·문자열 `connectionGeneration`/`disconnectedGeneration`과 응답 `observedAt`을 추가했다. 정확한
게임 서버/세션 인증과 기존 멤버·포기/자동 탈퇴 영수증 계약은 유지한다. 이름은 기존 생성
정책의 20자 상한을 넘으면 생략하고, 기한은 저장된 최초 단절 시각 + 기존 10분 정책이다.
연결 정책과 같은 저장소 시계를 관측 시각으로 사용한다. 기한을 표현할 수 없는 저장 시각이면
선택 기한만 생략해 필수 명단·영수증 조회는 유지한다. 이 mapper는 조회 시점에 멤버를
만료/삭제하거나 새 영수증을 만들지 않는다. 스키마 변경은 없다.

UE codec은 새 표시 필드를 선택적으로 읽는다. 구형/잘못된 표시 메타데이터만으로 기존 유효한
멤버십·불변 영수증을 버리지 않는다. 큰 입장 세대는 문자열로 읽어 int64 정밀도를 보존하며
반올림된 JSON 숫자나 잘못된 쌍/10분이 아닌 기한은 표시에서만 제외한다.

GameMode는 기존 monitor가 수락한 같은 세션/서버의 GET만 별도 표시 캐시에 보관한다.
실패·오래된 응답은 현재 명단/영수증을 덮어쓰지 않는다. 컴포넌트는 이 명단을 수신자별
서버 메모리 캐시에 결합해, 처음부터 Controller가 없는 멤버와 연결이 사라진 멤버도 행을
유지한다. 최신 명단에서 누락됐지만 회수 영수증이 없으면 체력 없는 `상태 확인 중`으로
남기며, 단순 부재/HTTP 실패/inactive/클라이언트 시계만으로 탈퇴시키지 않는다.

- 서버가 단절을 확인한 행은 `재접속 대기`로 표시하고 이전 체력/사망 표시를 유지하지 않는다.
  남은 시간은 backend 관측/기한의 차이에서 전체 GET 요청의 단조 시계 경과를 차감해
  GameState 서버 시간의 표시 기한으로 옮긴다. 클라이언트 UTC는 사용하지 않으며 `약 mm:ss`다.
- 마지막 성공 조회가 기본 10초 또는 설정된 조회 주기의 두 배를 넘기거나 요청 시계/서버
  관측이 없으면 숫자를 숨긴다. 헤더도 마지막 확인 정보 포함을 구분한다. 숫자가 0이 돼도
  행을 지우거나 보상 제외를 승인하지 않고 `유예 경과 · 서버 확인 대기`로 남긴다.
- 같은 세션의 명시적 포기/자동 탈퇴 불변 기록이 기존 monitor에 확인된 경우에만 명단
  캐시에서 제거한다. 현재 살아 있는 PlayerState의 권한 회수 장벽도 전투 행을 숨기지만
  이 화면이 회수/퇴장 작업을 실행하지는 않는다. 남은 참가자의 권한은 변경하지 않는다.
- 새 입장이 서버에서 확인된 더 높은 연결 세대는 이전 조회의 단절 상태를 무시한다.
  티켓 소비가 이미 세대를 높인 조회라도 단절 자체의 `disconnectedGeneration`과 비교하므로
  확인 전 조회가 확인 후 도착했을 때 이전 단절을 새 연결의 단절로 오인하지 않는다. 새 연결
  자신의 단절은 계속 표시하며, 구형 응답의 단절 세대가 없으면 기존 연결 세대를 사용한다.
  그 뒤 다시 연결이 없어져도 이전 세대의 타이머를 재사용하지 않고 새 서버 관측을 기다린다.
  이 최고 관측 세대는 서버 메모리의 표시 정보이며 backend 입장 확인을 대신하지 않는다.

명단/HUD는 전멸·Threat·재입장·정산 수령인을 결정하는 자료가 아니다. 기존 10분 유예·
자동 탈퇴·미정산 보상 제외 정책과 전투/포기 처리에는 새 승인 경로를 추가하지 않았다.
다음은 새 DLL 링크 후 실제 2인 강제 종료/유예 내 재접속·자연 만료/남은 파티 보상에서 이
표시 경로를 확인하는 것이다. 서버 전체 장애/운영 재개·다중 프로세스 fencing은 별도 남은 범위다.

## 파티원 접속 변경 안내 (2026-10-25, 소스 구현/개별 컴파일 완료)

파티 HUD에 `연결 끊김 확인 · 재접속 대기`, `재접속 확인`, `서버에서 탈퇴 확인`의
짧은 안내를 추가했다. 최초 명단 불러오기는 과거 접속 변경의 알림으로 재생하지 않는다.
이후 연결 끊김은 기존 서버 확인 메타데이터, 재접속은 기존 인증/입장 확인을 마친 활성
Controller/PlayerState, 탈퇴는 같은 신원의 기존 불변 포기/만료 기록이 있어야 표시한다.
Actor/조회 누락·정보 미확인·HTTP 실패·표시 타이머 0초·native 격리만으로 탈퇴 안내를
만들지 않는다. 정보가 잠시 미확인이 돼도 이미 확인한 재접속 대기는 유지하므로 같은 단절의
반복 알림이나 단순 Pawn 준비 완료의 잘못된 재접속 안내를 피한다.

- 서버의 수신자별 메모리 이력에서 표시 전환만 수집하고 기존 owner-only 스냅샷에 담는다.
  공개 이름/수신자 내 행 번호·안내 종류·서버 표시 기한만 복제하며 타인의 신원은 담지 않는다.
- 같은 파티원의 새 안내는 이전 안내를 교체한다. 전체 최대 3개·서버 게임 시간 기준 8초이며
  서버와 클라이언트 모두 표시 기한을 확인한다. 재접속 이후에도 옛 대기 안내가 같이 남거나,
  다음 복제가 없어서 안내가 계속 남지 않게 했다. 정보 안내의 만료는 멤버십 만료가 아니다.
- 신원/세션 변경·수신자 회수·모호한 명단에서는 안내도 지운다. 새 HTTP/RPC·영속 이력·
  자동 재시도·입력/포커스·사운드는 없고 기존 전투/정산·10분 정책도 변경하지 않는다.

순수 전환/중복 억제·이름/시간·최대 개수·같은 멤버 교체 사례를 `NoticeTransitions`에
추가했다. 기존 Widget/ServerScope/RosterRecovery 사례에도 표시/만료·회수 즉시 읽기 차단·
조회 실패/초기 로드 무알림·확인된 재접속/단절·영수증 탈퇴·반복 조회를 보완했다.
타입/컴포넌트/HUD/테스트 4개 TU의 `-NoLink` 개별 컴파일은 exit 0이다. 로그:
`Saved/Tests/PartyNoticeCompile/916608189a0b4d96a2675b185d220948/`.
현재 에디터/Live Coding·공유 DLL은 보존했다. 새 모듈 링크·5개 native 사례 실행·실제
2인 Iris/화면 검증은 아직 하지 않았으며, 이전 backend mapper 결과를 이 안내의 검증으로
대체하지 않는다.

## 커스텀 WBP 교체 지점 (2026-10-25, 소스 구현/개별 컴파일 완료)

Controller 기본값의 `UI|Dungeon Party → DungeonPartyCombatWidgetClass`에
`URPGDungeonPartyCombatWidget`을 부모로 하는 WBP를 지정할 수 있다. 미지정·추상/폐기
클래스·커스텀 생성 실패는 기존 네이티브 HUD로 돌아간다. Controller/GameMode 프로필을
달리해 같은 서버 명단·접속/체력 수집을 여러 화면에서 재사용하는 연결부이며, DungeonId에
따른 자동 스킨 선택이나 런타임 클래스 변경 기능을 추가한 것은 아니다.

WBP에 이미 디자이너 Root가 있으면 네이티브 기본 레이아웃을 삽입/교체하지 않는다. 첫
Construct 전에 현재 표시 데이터를 넣고, Construct 뒤와 기존 0.25초 로컬 갱신에서
`OnPartyPresentationUpdated`를 호출한다. WBP는 이 이벤트에서 다음 읽기 전용 조회를
사용하면 되며 별도 Tick/HTTP/RPC를 만들 필요가 없다.

- `GetPartyMembers`: 공개 파티원 행의 사본. 자기/체력 유효 여부·접속 상태와 안정적인 행 번호.
- `GetPartyHeaderText` / `IsLastKnownPartyRoster`: 같은 네이티브 헤더·오래된 조회 구분.
- `GetPartyMemberText` / `GetPartyHealthFraction`: 같은 체력/사망·서버 상대 재접속 문구와 비율.
  체력 바는 `bHasHealth`이고 `Presence != ReconnectGrace`일 때만 표시한다.
- `GetActivePartyNotices` / `GetPartyNoticeText`: 최대 3개·최신순의 아직 유효한 안내와 공통 문구.

이 API는 타인의 계정/캐릭터 ID나 원본 멤버십 스냅샷을 노출하지 않으며 명령/권한 변경
경로가 아니다. 기본 HUD도 같은 조회를 써 포맷/만료 규칙을 공유한다. Construct/갱신 뒤
비포커스·HitTestInvisible을 유지하고 detach에서는 표시 캐시를 비운다. 커스텀 BP에서도
전투 입력 모드·포커스나 별도 요청/멤버십 판정을 추가하지 않는 수동 HUD 계약을 지켜야 한다.

`DesignerPresentation` 사례에 디자이너 소유 Root 보존·첫 Construct 전 조회·공통 포맷/
안내 만료·반환 사본·범위 비우기·수동 입력 상태를 추가했다. 위젯/Controller 연결/기존
Controller/테스트 4개 TU의 `-NoLink` 개별 컴파일은 exit 0이다. 로그:
`Saved/Tests/PartyDesignerCompile/6ced664f9a6141ed94cbe13fa9a42809/`.
새 모듈 링크·집중 native 6개 실행·실제 WBP Blueprint 이벤트/화면은 아직 검증하지 않았다.
새 WBP/BP·맵·폰트 에셋을 생성/저장하거나 서버/복제/API 정책을 변경하지 않았다.

## 정상 Editor 타깃 빌드 (2026-10-25)

사용자의 빌드 요청 뒤 에디터 종료를 확인하고 `Project_RPGEditor Win64 Development`의
정상 타깃을 `-NoLiveCoding -NoHotReloadFromIDE -NoUBA`로 빌드했다. `-NoLink`나
개별 파일 선택이 아닌 453개 작업이 모두 완료됐고, 프로젝트 런타임/에디터·관련 플러그인
DLL 링크와 타깃 메타데이터 기록, 최종 `Result: Succeeded`/프로세스 exit 0을 확인했다.
이전 절들의 링크 대기는 당시 기록이며 이번 빌드로 해소됐다. 별도 UHT 재실행이나 native/
게임 실행 성공을 이 결과로 추정하지 않는다.

로그: `Saved/BuildLogs/PartyHUD-f78ae20395eb4e29a30ea672b1b8c9f2/Editor-Build.log`.
실행 시간 1349.52초, 런타임 DLL 갱신 18:41:36, SHA-256:
`9D7ED29176B62222672FFA35F882775ED86441E8F95DE9C5B8B01F7AB8F347C3`.
`RPGWidgetBase`의 mouse capture API와 외부 meshy 플러그인의 font 생성자에 C4996 경고
2건이 남으며 StructUtils 폐기 예정 안내도 있다. 경고 0건 빌드로 표시하거나 이번 범위에서
관련 소스를 변경하지 않았다.

이번 작업에서는 에디터 재실행·native 6개·WBP 생성/지정·실제 UI/Iris를 실행하지 않았다.
후속으로 관측한 다른 작업의 `UnrealEditor-Cmd` 프로세스도 종료하지 않았다. 다음은 새
모듈에서 집중 사례와 WBP 이벤트/화면을 확인하고, 실제 2인 단절/재접속 표시를 연결하는 것이다.

## 집중 native 실행 및 WBP 연결 대기 (2026-10-25)

`Scripts/Tests/Run-DungeonPartyCombatAutomationTest.ps1`에서 현재 테스트 소스의 정확한
케이스 목록을 추출해 이 범위만 실행했다. 공유 DLL은 앞선 자체 빌드 이후 갱신된
`923A0679…41D3A1`이며, 이 실행 전후 동일 DLL/선택 소스 20개 입력의 해시와 예상 결과를
대조했다. 별도 UserDir·MCP 자동 시작 차단·숨김 NullRHI 프로세스를 사용했고 우리가 만든
프로세스만 정리했다. 기존 에디터/타 테스트 프로세스·에셋·C++ 코드는 변경하지 않았다.

`DesignerPresentation`, `NoticeTransitions`, `Presentation`, `RosterRecovery`, `ServerScope`,
`Widget` 6개 모두 Success, 에디터/러너 exit 0이다. 디자이너 소유 Root·표시용 조회/사본·
안내 만료·수동 입력 상태, 초기/반복 알림 방지와 잘못된 명단/Actor 누락의 경계, 새 연결 세대·
명단 유지/불변 기록 제거를 native 범위에서 확인했다. 집중 실행 전 프레임 0의
`LogAutomationTest: Error: Condition failed` 15줄은 남으므로 기동 오류 0건으로 표시하지 않는다.
각 대상 테스트는 그 뒤 실행됐으며 Fatal/Assert/Ensure는 없었다.

proof: `Saved/Tests/DungeonPartyCombat/86029f582ec7447a8ae6d862095d06ce/PartyCombat.proof.json`.
같은 폴더의 `automation.log`가 실행 로그다. 실제 WBP 에셋·Blueprint 이벤트 override·
GPU/실물 입력·Iris·HTTP/DB·Steam 검증을 이 결과로 대체하지 않는다.

후속 MCP 8000의 `IsPIERunning=true`를 확인해 사용자의 실행 중인 플레이를 보존했다.
플레이 중지나 에셋 수정/생성·저장·Controller 기본값 변경은 하지 않았다. 다음은 사용자가
플레이를 중지하고 변경사항을 저장한 뒤 확인용 WBP 생성/지정과 실제 표시를 연결하는 것이다.
이를 위해 에디터 자체를 종료하거나 정상 빌드/성공한 집중 검사를 반복할 필요는 없다.

## 실제 WBP 연결과 좌측 중앙형 변형 (2026-10-25~26)

2026-10-25에는 `/Game/Blueprints/UI/Party/WBP_DungeonPartyCombat`을 실제 WidgetBlueprint로
생성·컴파일·저장하고 `BP_PlayerController.DungeonPartyCombatWidgetClass`에 연결했다.
부모는 `URPGDungeonPartyCombatWidget`이며 디자이너 Root를 두지 않아 기존 네이티브
레이아웃을 사용한다. 실제 디자이너 미리보기까지 확인했으나 Blueprint 이벤트 override나
인증된 파티 플레이의 검증은 아니었다. 위의 WBP 생성/연결 대기는 이 후속 이전 기록이다.

2026-10-26에는 별도 변형
`/Game/Blueprints/UI/Party/WBP_DungeonPartyCombat_LeftCenter`를 제작·컴파일·저장했다.
당시 기존 WBP/Controller를 미저장으로 보고 별도 변형으로 작성했으며 기존 에셋은
덮어쓰거나 함께 저장하지 않았다. 그때의 미저장 판정은 아래 경로 처리 정정과 구분한다.

- 디자이너 소유 Canvas/4행 카드·체력바·안내 3칸, 폭 280 Slate 단위. `PartyPanel`의
  앵커 `(0, .5)`·정렬 `(0, .5)`·왼쪽 여백 24·자동 크기로 화면 좌측 중앙에 배치한다.
  행이 줄면 전체 패널 높이에 맞춰 중앙 정렬되며, 4인을 넘는 예외 명단에는 4인용 화면이라는
  안내를 표시한다. 임의 규모 공격대용 가변 행 구현으로 간주하지 않는다.
- 실제 `OnPartyPresentationUpdated` override에서 `RefreshPartyLayout`을 호출한다.
  기존 네이티브 파티원/헤더/문구/체력 비율/유효 안내 API만 읽으며 새 Tick/HTTP/RPC는 없다.
  확인된 체력만 표시하고 재접속 대기는 오래된 체력이 있어도 바를 접는다. 전투 불능의
  붉은 바·재접속의 황색 문구를 구분하고, 빠진 행의 이름/체력/래퍼는 즉시 비운다.
- 루트는 HitTestInvisible이고 버튼·포커스/입력 모드 변경·멤버십/보상 변경은 없다.
  미리보기용 이름/체력은 디자이너 기본값이며 런타임 갱신에 실제 명단으로 대체된다.
  API를 시험하려고 비공개 snapshot setter를 Blueprint/Python 쓰기 가능으로 바꾸지 않았다.

저장된 실제 WBP의 격리 NullRHI 인스턴스에서 Blueprint 이벤트를 호출해 **빈 snapshot 처리
24개 확인**과 commandlet/러너 exit 0을 확인했다. 클래스 로드·디자이너 변수 연결·4행의
미리보기 이름/체력/래퍼 제거·안내/초과 표시 정리·공통 헤더 갱신 범위다.
파티원이 있는 상태의 체력/재접속 전환·뷰포트 입력·2인 Iris/HTTP/Steam 검증은 아니다.

- 검사: `Scripts/Tests/TestDungeonPartyCombatLeftCenter.py`
- 결과: `Saved/Tests/PartyLeftCenter/9fd6223c96224a718af428e5ab435b61/Result.json`
- 미리보기 개요: `Saved/Tests/PartyLeftCenter/Designer-Large.png`
- 에셋 SHA-256: `88A3976D09F9C2C0D6B1617998639C142BFFE8A07631485F9E2214712785B62A`

초기 Python fixture의 잘못된 라이브러리 이름과 비공개 Members 쓰기 거절 로그는 각각
`PartyLeftCenter/f9eaa2843c8041109f4e82373d5b17f2/`,
`PartyLeftCenter/56cb7ef0df324d0f837c516414655d44/`에 보존했다. 성공한 검사는 이후의
빈 명단 전용 검사이며 초기의 populated snapshot 계획 전체가 통과했다는 뜻이 아니다.
한국어 실행 핀을 인식하지 못하는 그래프 DSL 시도는 해당 변형의 미연결 노드만 제거하고
명시적 핀 연결로 교체했다. 최종 함수 노드 150개·이벤트 연결·UMG 컴파일을 확인했으며
에디터 언어/엔진 도구 소스·C++/DLL은 바꾸지 않았다. 작성 도구는
`Scripts/Editor/BuildDungeonPartyCombatLeftCenter.py`, `WireDungeonPartyCombatLeftCenter.py`다.

이 변형 제작 직후의 연결은 기존 `WBP_DungeonPartyCombat`이었다. 새 변형으로 바꾸려던 직전 PIE가
다시 실행 중이어서 가드에서 중단했다. **새 클래스 지정·Controller 저장·PIE 중지는 하지
않았다.** 기존 미저장 WBP와 실행 중 플레이도 보존한다. Controller 디스크 백업은
`Saved/Backups/PartyLeftCenter/9e3b5934d55247c5b3cbb03f75ed4aea/`이며 전후 해시는 동일하다.
그 단계의 연결 대기는 아래 후속에서 해소했다. 실제 파티원 표시 확인은 여전히 별도이며
에디터 종료나 전체 C++ 빌드·성공한 기존 집중 검사를 반복할 필요는 없다.

## 좌측 중앙형 Controller 연결 완료 (2026-10-26)

사용자의 PIE 종료 안내 후 실제 중지 상태를 확인하고,
`BP_PlayerController.DungeonPartyCombatWidgetClass`를
`WBP_DungeonPartyCombat_LeftCenter_C`로 지정·컴파일·저장했다. 이전 선택이 기존 기본
WBP인지 검사한 뒤 이 한 기본값만 바꿨으며 다른 선택을 덮어쓰는 경로는 거절한다.
일반 `BP_GameModeBase`와 `BP_BossBattleGameMode`는 기존 Controller를 그대로 사용한다.
전체 저장이나 GameMode/맵/기존 WBP·새 변형/C++/DLL 변경은 하지 않았다.

Controller 변경 전 디스크 파일과 현재 에디터 내용을 같은 이전 선택으로 저장한 파일을
각각 백업했다. 두 해시는 동일한 `32A7778E…06D31E`였고 기존 내용은 보존했다.
백업: `Saved/Backups/PartyHudConnection/84266370644c457e9e208757fa5e7a3f/`.
연결 후 Controller SHA-256은 `FB57736E1F152F9904023ABBADDBEC2538B9ACCB4C4D3CFE0E380A7B3523264C`다.
WBP는 앞선 `88A3976D…85B62A`와 동일하다.

별도 숨김 NullRHI commandlet에서 **저장 에셋 재로드·선택 검증 6개/exit 0**을 확인했다.
Controller 클래스 로드, 새 WBP 선택, 네이티브 부모 호환, 두 GameMode의 기존 Controller
유지와 선택된 클래스로 위젯 생성의 범위다. 에디터 메모리의 읽기 결과만으로 저장 성공을
추정하지 않는다. 이 검사는 뷰포트에 붙이지 않은 위젯이며 인증된 파티 플레이·실제 체력/
재접속/탈퇴 표시·입력 비간섭·Iris/HTTP/Steam 성공은 아니다.

- 검사: `Scripts/Tests/TestDungeonPartyCombatHudSelection.py`
- 증거: `Saved/Tests/PartyHudConnection/84266370644c457e9e208757fa5e7a3f/Selection.proof.json`
- 같은 폴더의 `Selection.log`가 실행 로그다. 기존 native 6개·WBP 빈 명단 24개·backend
  변환이나 전체 빌드는 반복하지 않았다. GUI 에디터와 다른 프로세스는 보존하고 본 검사
  프로세스만 종료했으며 PIE를 시작하거나 중지하지 않았다.

에셋 상태 조회 정정: 현 엔진 도구의 `AssetTools.is_dirty`는 full object path의 존재 확인과
package path 기준의 dirty/on-disk 확인을 섞어, object path에는 저장 후에도 `true`를,
package path에는 없는 에셋 오류를 반환할 수 있다. 이전 이 경로로 얻은 `dirty=true`를
실제 사용자 미저장 변경의 증거로 사용하지 않는다. 이 도구/엔진은 수정하지 않았으며 이번
저장 여부는 제한한 저장 호출·파일 변경과 별도 프로세스의 재로드로 확인했다.

이 연결 이후 실제 2인 명단·초기 체력과 접속 단절/복귀는 아래 후속에서 관측했다.
체력 변화·전투 불능·확정 탈퇴 표시와 입력 비간섭은 남아 있다.

## 실제 2인 HUD와 단절·재접속 관측 (2026-10-26)

기존 멤버십 E2E에 선택 항목 `-ObservePartyCombatHud`를 추가했다. 기존 전용 서버 역할
프로세스와 소유한 두 렌더링 Editor/PIE 클라이언트로 개발 인증·PostgreSQL·실제 Join Ticket/
Iris 입장을 실행하고, 80~160ms 지연·5% 손실 조건에서 화면의 Slate와 PNG를 수집했다.
일반 headless 경로는 유지하며 실제 화면을 읽는 데 필요한 로컬 MCP는 소유 프로세스와
127.0.0.1 포트 일치를 검사한다. 사용자 에디터/PIE는 조작하지 않고, 본 실행의 프로세스와
관찰자만 정리했다. 저장 Controller/WBP는 변경하지 않았고 신원·스냅샷·체력을 주입하거나
서버/DB 시계·10분 정책을 바꾸지 않았다.

확인한 화면은 총 5개다.

- 최초 두 클라이언트: 각각 본인 표시와 실제 공개 이름의 2인 명단, 체력 150/150과 바 표시.
- 소유 원본 클라이언트 강제 종료 후 남은 클라이언트: 서버의 단절 확인을 거쳐 2명 명단을
  유지하며 해당 행은 황색 재접속 대기/약 09:59와 체력 바 숨김, 남은 멤버의 체력 표시 유지.
- 새 티켓 입장 후 남은 클라이언트와 교체 클라이언트: 모두 2인 명단과 정상 체력 표시 복귀.
  실제 서버 확인 세대 1→2, 옛 세대의 단절/입장 확인 POST 거절 403, 파티 유지와 추방 0건도
  원래 재접속 검사에서 확인했다. 확정 탈퇴 영수증을 만든 시나리오는 아니다.

화면별 `.txt`/`.png`와 해시는 다음 실행 폴더에 보존했다.

- 실행: `Saved/Tests/DungeonMembershipBackend/e026bbb0ac394b05820974adcc7acc23/`
- HUD 한정 성공 증거: 같은 폴더의 `PartyHudObservation.proof.json` (`passed=true`, 관측 5개).
- 대표 화면: `party-hud-initial-client-2.png`, `party-hud-grace-client-2.png`,
  `party-hud-reconnected-client-2.png`.
- 관측 도구: `Scripts/Network/DungeonPartyHudObservation.ps1`와 기존 러너/재접속 시나리오의
  opt-in 연결. 파서의 정상/유예·중복/미확인·잘못된 수치 거절 13개는
  `Scripts/Tests/Test-DungeonPartyHudObservation.ps1`에서 별도 통과했다.
- `Scripts/Tests/Test-DungeonPartyHudCapturedEvidence.ps1`는 기존 실행의 실제 종료/새 입장과
  세대·403·보존 증거, DLL/원래 입력 해시, 새 WBP 고유 문구와 5개 저장 화면을 검사해 위
  HUD 증거를 봉인했다. 당시 입력 일치는 아래 검사기 소스 수정 **이전 시점**의 사실이다.
  현재 소스의 전체 런타임 성공으로 승격하거나 봉인 파일을 덮어쓰지 않는다.

**전체 던전 통합 검사는 성공하지 않았다.** 부모 `Failed.proof.json`의
`DUNGEON_FLOW_E2E_LOBBY_COMPLETE` 미관측 실패를 그대로 남겼고, HUD 증거에도
`fullLifecyclePassed=false`를 명시했다. 보상 뒤 두 클라이언트가 실제
`/Game/Maps/UEDPIE_0_LobbyMap.LobbyMap`을 로드했지만 기존 검사기는 일반 실행의 정확한
패키지 이름만 비교해 PIE 복귀를 인식하지 못했다. 실제 travel 로그만으로 나머지 로비 상태
검사까지 통과했다고 간주하지 않는다.

이후 `RPGDungeonFlowE2EProbe.cpp`의 주 흐름 Stage 4/50에서 **PIE 월드에만** 엔진의
`UWorld::RemovePIEPrefix`를 적용하도록 수정했다. 원래 맵/Standalone·인증/선택/세션·요청/
티켓 검사는 유지하고 별도 포기 흐름이나 운영 게임 코드는 바꾸지 않았다. 해당 CPP 1개
`-NoLink` 컴파일은 `Result: Succeeded`이며 로그는 위 폴더의 `FlowProbe-PIE-NoLink.log`다.
새 DLL 링크/교체나 수정 검사기의 통합 재실행은 하지 않았으며 이전 실제 HUD 증거와 구분한다.

앞선 세 번의 기동 시도에서 MCP 요청/PIE 시작 감시 시간과 cold shader stall에 따른 기존
네트워크 초기 접속 시간 초과가 있었다. 실패 폴더와 로그를 보존했고 모든 시도가 성공했다고
계산하지 않는다. 서버 접속 제한이나 유예/티켓 보안 정책을 늘려 통과시키지 않았다.

다음은 실제 체력 변화·전투 불능과 입력 비간섭, 불변 기록 후 해당 행 제거/10분 자연 만료다.
이번 관측은 체력 변화량/바 비율의 정밀 검증·물리 입력·화면 최종 품질 승인·남은 파티 정산/
전멸 회귀·Steam/다중 PC·Shipping 성공을 뜻하지 않는다. 새 DLL이 필요한 검사기 수정은
다음 정상 모듈 반영과 묶으며, 이번 문서 정리를 위해 빌드/네트워크 전체 검사를 반복하지 않는다.

## 피격·전투 불능의 선택형 관측 경로 (2026-10-27, 구현/개별 컴파일 완료)

기존 2인 재접속 관측에 `-ObservePartyCombatVitals`를 추가했다. 이 구현 단계에서는 실제
화면 실행 전이었으며, 아래 후속에서 새 실행 9개 화면을 확인했다. 앞선 5개 화면을 이
기능의 실행 증거로 재사용하지 않는다.

- 기존 `-ObservePartyCombatHud -VerifyRemainingPartySettlement -VerifyUnexpectedDisconnect
  -VerifyReconnectWithinGrace`와 함께 사용하며, 미확정 재입장 장애 주입과의 혼합은 거절한다.
  옵션이 없으면 기존 경로에 피해를 추가하지 않는다.
- 새 `RPGDungeonPartyVitalsProbe.cpp`는 기존 opt-in 서버 fixture의 일부다. 개발/`-nosteam`·
  전용 서버·실제 인증된 동일한 두 접속/Pawn/세션/연결 세대·진행 중 전투 범위를 검사한다.
  소유 실행의 파일 게이트를 기다린 뒤 보스→대상 한 명의
  `ApplyAuthorizedServerDamage` → 기존 피해 실행/AttributeSet/사망 태그 경로를 사용한다.
  첫 타격은 비치명, 다음 타격은 치명으로 나누며 거리/적대/피해 상한 검사를 우회하지 않는다.
  다른 파티원의 체력과 전투 진행을 보존하고 직접 HP/태그/신원/스냅샷을 쓰지 않는다.
- 양쪽 초기 화면이 없으면 피해 게이트를 열지 않는다. 비치명 피해 후 양쪽 화면에서 서버의
  실제 감소 수치와 다른 멤버의 기존 체력을 확인한 다음에만 치명 게이트를 연다. 이후 양쪽의
  `전투 불능`·체력 0/표시 바·생존 멤버를 관측한 뒤 기존 강제 종료/새 티켓 재접속을 이어간다.
  이는 의도적으로 서버 피해를 발생시키는 fixture이며 자연 전투·물리 입력 검증은 아니다.
- 화면 파서는 초기 생존/유예와 명시적 전투 불능을 구분하고, 이전 체력·잘못된 본인/중복 행·
  바 누락·서버 관측 수치 불일치를 거절한다. 실제 저장 화면 검증기도 vitals 기록이 있을 때만
  피해/전투 불능 4개 화면을 추가해 총 9개를 요구한다. 이전 5개 증거 파일은 덮어쓰지 않는다.
  러너의 의도적 피해 표시와 직접 속성 쓰기/자연 전투/입력 확인 여부도 분리했다.

확인한 범위는 순수 파서 **26개**, 모의 화면/서버 로그를 사용하는 순서/게이트 검사 **13개**,
변경 PowerShell 5개 파일의 AST 파싱과 직접 영향 C++ **2개 TU의 NoLink 컴파일 성공**이다.
순수 검사는 `Test-DungeonPartyHudObservation.ps1`, `Test-DungeonPartyHudVitalsScenario.ps1`이며
각 출력의 `actualGameRun=false`를 유지한다. 컴파일 로그는
`Saved/BuildChecks/PartyHudVitals-20261027-012356/`의
`RPGDungeonPartyVitalsProbe-NoLink.log`, `RPGDungeonFlowE2EProbe-NoLink.log`다.

사용자 에디터는 종료/조작하지 않았고 새 DLL 링크·적용이나 이 옵션의 네트워크/화면 실행은
하지 않았다. 다음 정상 Editor 모듈 반영 후 이 새 피해 시나리오와 앞선 PIE 로비 판정 수정을
같이 확인한다. 입력 비간섭·정밀 바 비율/색상·확정 탈퇴/10분 자연 만료·Steam/Shipping은
여전히 별도이며 기존 정상 접속만의 검사를 반복해 완료 처리하지 않는다.

## 실제 피격·전투 불능 관측과 PIE 복귀 후속 (2026-10-27)

정상 Editor DLL 링크/타깃 성공 후 위 새 시나리오를 한 번 실행했다. 개발 인증·격리된
PostgreSQL 스키마·실제 티켓/Iris와 80~160ms·5% 손실에서 다음 **실제 화면 9개**를 확인했다.

- 양쪽 초기 150/150 → 서버 승인 피해 후 대상 135/150 → 다음 승인 피해 후 대상 0/150·
  `전투 불능`을 각각 양쪽에서 관측했다. 다른 파티원은 150/150을 유지했고 서버 전투는
  InProgress였다. 피해/전투 불능의 원격 클라이언트 PNG도 직접 열어 확인했다.
- 그 뒤 소유한 원본 클라이언트만 강제 종료했다. 남은 화면의 유예 행/바 숨김과 새 티켓
  재접속 뒤 두 화면의 정상 복귀를 관측했다. 실제 연결 세대 1→2와 옛 세대 POST 403·
  전체 파티 유지도 확인했다. 일반 생존자의 단절만 반복한 실행이 아니라 피격/사망 이후
  재접속을 연결한 새 시나리오다.
- 스냅샷/HP/사망 태그를 직접 쓰지 않았고 운영 전투·정산·10분 정책과 저장 WBP/Controller는
  변경하지 않았다. 이는 의도적 서버 피해 fixture이며 자연 조작으로 보스에게 맞은 증거나
  입력 비간섭·정밀 바 비율/색상·죽음 모션·최종 레이아웃 승인은 아니다.

실행/증거: `Saved/Tests/DungeonMembershipBackend/9ea100aaba814757a6538ca6b2dc3183/`의
`PartyHudObservation.proof.json`은 HUD 한정 `passed=true`, 관측 9개,
`healthChangesVerified=true`, `incapacitatedStateVerified=true`다. 같은 폴더의 실제
`party-hud-damaged-client-2.png`, `party-hud-incapacitated-client-2.png`와 나머지 텍스트/PNG의
해시를 보존했다. 검증기는 원래 DLL/입력 해시 일치도 확인해 **아래 복귀 소스 수정 전에**
봉인했다. 기존 5개/이번 9개 증거를 덮어쓰거나 새 소스의 실행 성공으로 승격하지 않는다.

**전체 검사 성공은 아니다.** 보상 표시/복귀 버튼과 PIE 로비 로드는 실행됐지만 두 클라이언트가
`LobbyReturnStateMismatch`로 실패해 부모 `Failed.proof.json`과
`fullLifecyclePassed=false`를 유지했다. 앞선 검사기 접두사 수정 덕분에 맵 도착 다음의 실제
상태 검사까지 진행됐고, 운영 `HandleLobbyMapLoaded`도 raw package 이름만 비교해
`UEDPIE_0_LobbyMap` 도착에서 ReturningToLobby→Idle을 누락하는 경로를 찾았다.

`RPGDungeonSessionSubsystem.cpp`의 완료 판정에서 **PIE 월드만** 접두사를 정규화하도록
수정했다. 같은 GameInstance·정확한 목적지·ReturningToLobby 상태와 일반 게임 맵의 비교는
유지했다. 기존 friend fixture의 새 `ProjectRPG.Online.DungeonPIELobbyCompletion`은
잘못된 월드/다른 인스턴스·맵·일반 Game 월드의 PIE 이름 거절, 기존 FString의 패키지
대소문자 비교 유지, 올바른 PIE 완료와 선택/완료 세션 보존을 다룬다.

빌드 기록은 `Saved/BuildChecks/PartyHudVitalsRuntime-20261027-014438/`에 보존했다.
최초 빌드는 다른 성장 검사 코드의 엔진 로그 도우미 심벌에서 실패했다. 그 파일은 다른
작업에서 이미 수정돼 추가로 건드리지 않았고 `Editor-Build-CurrentSource.log`에서 정상
3개 작업·DLL 링크/타깃 성공을 확인해 위 화면 실행에 사용했다. 이후 복귀 수정의
`Editor-Build-PIEReturn.log`는 관련 CPP 2개 컴파일 뒤 다른 UE 검사 프로세스의 DLL 잠금으로
LNK1104였다. 그 프로세스들은 보존했으며 이후 자연 종료로 잠금이 해소됐다.

### 복귀 수정 반영과 최종 통합 성공

다른 작업에서 갱신한 모듈로 자체 정상 타깃 0 action 성공을 확인했다. 첫 native 실행은
패키지 대소문자 비교를 잘못 가정한 사례가 실패했다. 운영 비교 규칙을 바꾸지 않고 기존
FString 비교를 보존하는 사례로 정정했으며 원래 실패 로그/증거는 유지했다. 최종
`Editor-Build-PIEReturn-Casing.log`에서 4개 작업·CPP 컴파일/정상 DLL 링크/타깃 성공을,
`PIELobbyCompletion-Final.proof.json`에서 집중 native 1개 Success·exit 0·DLL/입력 전후
일치를 확인했다. 기동의 기존 Condition failed Error 15줄은 남고 사례 시작 이후 Error는
0이므로 에디터 전체 오류 0건으로 기록하지 않는다.

수정 DLL `36A2E5FF…1CFD8372`의 새 실행
`Saved/Tests/DungeonMembershipBackend/b0fdfdb55fb9441fba4f7e4a9f6d05de/`는 **전체 시나리오와
러너 exit 0**이다. 이번에는 양쪽 체력 150→120→0/전투 불능과 다른 멤버 150 유지,
사망 후 실제 단절/유예·새 티켓 세대 1→2와 옛 세대 POST 403을 확인하고, 양쪽 결과 버튼→
실제 PIE 로비·Standalone·Idle·인증/선택 보존·Join Ticket 제거까지 완료했다. 원래 시작
무기를 보존하며 두 캐릭터 각각 골드 500·가방 보상 수량 4·우편 0을 독립 HTTP/SQL로
확인했다. DB 스키마는 감사용으로 보존하고 본 실행의 프로세스/관찰자만 정리했다.

- 전체 증거: 같은 폴더의 `DungeonReconnectWithinGrace.proof.json`.
- 실제 화면 한정 증거: `PartyHudObservation.proof.json` (`passed=true`, 관측 9개,
  체력 변화/전투 불능 확인 true, `fullLifecyclePassed=true`). 수정된 DLL/선택 입력과 저장
  화면의 해시를 확인해 새로 봉인했으며 앞선 실패/부분 성공 증거는 덮어쓰지 않았다.
- 최종 실행은 소스 수정 전 단순 반복이 아니라 실패한 운영 로비 완료 판정의 수정 검증이다.
  기존 순수 26/13개나 다른 광역 native/백엔드 회귀는 다시 실행하지 않았다.

남은 항목은 입력 비간섭·정밀 바 비율/색상·자연 전투/죽음 모션·확정 탈퇴/10분 자연 만료·
Steam/다중 PC·Shipping이다. 이번 로컬 fixture 통과가 그 성공을 의미하지는 않는다.

## 입력 비간섭 전용 경로와 기준 입력 조사 (2026-10-27)

`Run-DungeonMembershipBackendE2ETest.ps1`에 `-ObservePartyCombatInput`을 추가했다.
`-ObservePartyCombatHud -VerifyRemainingPartySettlement`와 정확히 두 클라이언트를 사용하되
입력 확인 뒤 종료하며 기존 단절/재접속·서버 승리 타격·정산/로비 복귀는 실행하지 않는다.
다른 수명주기 옵션과 혼합은 사전 거절한다. 앞선 성공 9개/피격·보상 흐름을 반복하는 모드가 아니다.

새 개발용 `RPGDungeonPartyHudInputProbe.cpp`는 실제 인증된 로컬 Pawn/세션과 저장된
좌측 중앙 WBP를 확인한다. HUD를 접은 기준/보인 상태에서 현재 활성 이동 액션의 키를
Slate key down/up으로 전달하고, 실제 파티 카드 좌표의 hittest 경로가 게임 viewport에
도달하는지와 기본 공격 활성·동일 Pawn/세션·포커스/커서 유지 여부를 비교하도록 구현했다.
초기 포커스는 일반 viewport 클릭으로만 얻고 `SetUserFocus`/입력 모드 강제 변경·직접
이동/공격 함수 호출·Enhanced Input action injection·키 설정/신원/스냅샷 쓰기는 하지 않는다.
자동 Slate 이벤트이며 물리 키/마우스나 서버의 이동/공격 승인 증거로 확대하지 않는다.

초기 조사 단계는 입력 비간섭 성공이 아니다. 처음 두 실행은 HUD를 접은 기준 이동부터 실패했다.
`5497a79da45f4c5f94d9b470d5df8d71/client-1.log`에서 W key value 1, 이동/viewport 무시 0,
이동 거리/속도 0과 W의 활성 매핑 부재를 확인했다. W 고정 가정을 없애고 실제 이동 액션의
활성 키를 찾도록 바꾼 뒤 `260e065ec7bd4a7da1a1e5acdff14063/client-1.log`에서는
Configured=1·PrimaryApplied=1·ActiveMappings=10·MoveAction=IA_Move·SelectedKey=None로
`PartyInputMovementMappingMissing`을 기록했다. 이 실패를 HUD가 입력을 차단한 증거로
취급하지 않고 저장 설정과 실행 복사본/최종 적용 목록을 구분해 조사한다.

초기 실행들의 부모 `Failed.proof.json`과 실제 파티 화면은
`Saved/Tests/DungeonMembershipBackend/`의 각 run 디렉터리에 보존했다. 성공 proof는 만들지
않았고 소유 프로세스/관찰자만 정리했다. 읽기 전용 UE commandlet은 저장 Controller의
DA_InputConfig/IMC_Default 연결과 WASD·방향키·Gamepad_Left2D→IA_Move를 확인했다.
입력 에셋/사용자 키 설정은 저장·초기화하지 않았다.

구현 PS AST/잘못된 옵션 조합의 사전 거절, 새 CPP의 NoLink 컴파일, 공유 모듈 갱신 후 정상
타깃 성공을 확인했다. 최신 진단 코드는 `Saved/BuildChecks/PartyHudInput-20261027-021618/`의
`Editor-Build-MappingDetails.log`에서 정상 4개 작업·DLL 링크/타깃 성공이다. 중간 다른 검사
소스 오류/타 프로세스 DLL 잠금, 자체 진단 include 누락과 UHT 행 번호 캐시 불일치 로그도
남겼고, include 보완·`-ForceHeaderGeneration`의 UHT/15개 작업 성공으로 해소했다.
`InspectInputAssets-Keys.log`는 저장 매핑을 읽은 결과이며 실행 중 매핑의 정상성을 증명하지 않는다.

이 조사 단계에서는 운영 입력 로직을 바꾸지 않았다. 이후 아래 추적에서 누락 지점을 확정하고
런타임 입력 복사 경로를 수정했다.

### 런타임 바인딩 복사 보완과 입력 전용 성공

`7aac01742cc942c499ef15622b212d49/client-1.log`에서 초기화 단계를 추적해 원본 26개가
컨텍스트 복사 **직후** 0개가 되고 등록/저장 키 복원/스킬 컨텍스트 추가 뒤에도 빈 상태인
것을 확인했다. HUD 차단이나 저장 키 변경 때문으로 단정하지 않고 이 복사 누락만 보완했다.
추적 로그는 `-RPGPartyHudInputE2E`의 개발 실행에서만 출력하며 신원/토큰은 포함하지 않는다.

Controller의 새 private `RPGInputMappingClone::RestoreMissingRecords`는 원본이 있고 복사본이
비어 있을 때만 매핑 레코드 전체를 보완한다. Action/Key/나머지 레코드 설정을 유지하고
modifier/trigger는 복사본 소유로 독립 복사한다. 원본 자체·이미 내용이 있는 복사본·빈 원본은
건드리지 않는다. 기존 사용자 키 복원 **전에** 실행하므로 저장 키 변경을 초기화하거나 공유
에셋을 쓰지 않는다. 컨텍스트 메타데이터와 기존 옵션/저장/충돌 정책도 유지한다.

정상 4개 작업의 수정 반영 후 새 native 파일을 포함한 정상 타깃 7개 작업·DLL 링크/종료 0과
집중 `ProjectRPG.Input.RuntimeMappingClone` 1개 Success·종료 0을 확인했다. 원본/기존 키 보존,
modifier/trigger 독립 소유와 원본 영향 없음 사례다. 로그/입력·DLL 전후 일치 proof는 위 빌드
폴더의 `Editor-Build-CloneTestGather.log`, `RuntimeMappingClone-Native.proof.json`이다.
기동 기존 Condition failed Error 15줄은 별도이며 사례 시작 이후 Error는 0이다.

최종 실제 입력 전용 실행은
`Saved/Tests/DungeonMembershipBackend/9b3046f32be242cba09077970a12c988/PartyHudInput.proof.json`
의 **passed=true·러너 종료 0**이다. 같은 DLL `FD1978D2…D021D137`에서 복사/등록/저장 키 복원
뒤 기본 매핑 26개를 유지했고 유효 매핑 32개·실제 이동 키 W를 확인했다.

- 실제 개발 인증/PostgreSQL/티켓·Iris의 두 클라이언트에서 명단을 표시한 뒤 한 로컬 클라이언트의
  같은 Pawn을 비교했다. HUD 숨김/표시에서 이동 거리 약 225/259, 각각 기본 공격 활성과
  파티 카드 좌표→실제 viewport hittest, 포커스/커서 유지가 확인됐다. 거리 동등성/성능 비교는 아니다.
- 직접 입력 함수/Enhanced Input action injection·강제 포커스/입력 모드·사용자 키 초기화는
  하지 않았다. 초기 포커스도 정상 Slate 포인터 경로로 얻었다. 자동 Slate 이벤트이며 물리
  장치 입력이나 서버의 이동/공격 승인 검증은 아니다.
- 명단 2명·InProgress·포기 기록 0을 유지하고 초기 두 화면/입력 후 화면의 텍스트·PNG 3개를
  보존했다. `party-hud-input-passed-client-1.png`도 직접 확인했다. 소스/러너/에셋과 DLL은
  실행 전후 일치했고 소유 프로세스/관찰자만 정리했다. 스키마/앞선 실패 증거는 보존했다.
- 기존 피격·재접속·보상·로비 복귀/다른 광역 회귀는 실행하지 않았다. 이번 입력 proof도
  reconnect/rewardSettlement/physicalInput/serverMovementOrAbilityApprovalVerified를 false로
  명시한다. 앞선 별도 전체 흐름 성공 증거와 혼합하지 않는다.

명시적 탈퇴 후 행 제거는 아래 후속에서 확인했다. 10분 자연 만료와 입력의 물리 장치·다른
스킬/메뉴 조합 확인은 별도다.

### 명시적 탈퇴 후 실제 HUD 행 제거 (2026-10-27)

기존 불변 영수증 기반 행 제거와 저장된 WBP를 재구현하지 않고, 기존 2인 러너에
`-ObservePartyCombatHud -ObservePartyCombatDeparture -VerifyRemainingPartySettlement`의
탈퇴 관측 전용 경로를 추가했다. 마지막 옵션은 기존 파티 fixture 선택이며 이 모드에서는
승리 게이트를 열지 않고 보상 정산/로비 복귀 전에 종료한다. 다른 생명주기/입력/피격 모드와
혼합은 실행 전에 거절한다. 게임 C++·에셋/10분 정책 변경이나 새 UE 빌드는 없다.

관측 파서는 남은 1명/기존 2명과 제거할 공개 이름을 구분한다. 파티 영역 안의 인원수·행·
체력바 수를 대조하며, 탈퇴 알림에 남은 이름은 허용하되 옛 체력/유예/상태 미확인 행·고아
체력바·중복 viewport는 거절한다. 플레이어/보스의 별도 체력바는 세지 않는다. 기존 사례를
포함한 순수 파서 35개와 PS 구문·잘못된 모드 거절 확인이 통과했다.

최종 실제 실행은
`Saved/Tests/DungeonMembershipBackend/426a21a8301d455b9054af030a20ae7d/PartyHudDeparture.proof.json`
의 **passed=true·러너 종료 0**이다. 개발 인증/PostgreSQL·실제 티켓/Iris와 기존
80–160ms·5% 손실에서 다음을 확인했다.

- 두 실제 초기 HUD의 2명/각 HP 150 → 본인 HTTP 포기 커밋 후 남은 화면의 **1명/HP 150**.
  제거된 행/체력바가 없고 탈퇴 알림은 유지되는 실제 Slate·PNG 3개를 보존했으며 최종 PNG도
  직접 확인했다. 스냅샷/영수증/시계 주입은 없다.
- 같은 불변 영수증을 API 재조회와 독립 PostgreSQL 조회에서 대조했다. 서버의 대상 Actor
  제거·같은 남은 참가자/전투·InProgress 유지와 탈퇴자의 미정산 reward-snapshot 403을 확인했다.
- DLL `E563DE70…5855C58`과 선택 소스/러너/에셋·닫힌 로그 5개의 해시가 사후에도 일치했다.
  자신이 시작한 프로세스/observer만 정리했고 사용자 에디터와 기존 데이터는 유지했다.

첫 관측 `36649c7f9044468b86a3928bc3ca0718`은 행 제거를 확인했지만 종료 순서 때문에
종료 중 fixture 실패가 남았다. 다음 `c3e6ee466ebe4190878eacea631cda5e`는 행 제거 후
로그 잠금으로 결과 해시 저장에서 종료 1이었다. 둘 다 최종 정상 종료 증거로 승격하지 않는다.
조기 종료 모드만 소유 서버부터 일괄 종료하고 모든 종료를 기다리며, 닫힌 로그의 일시적
파일 잠금에는 최대 5초만 대기하도록 보완했다. 최종 proof의 `closedLogsFailureFree`는
설정된 실패 마커 부재를 뜻한다. 전체 Error 0은 아니며, 최종 로그에는 기존 기동
AutomationTest Condition failed 30줄과 의도한 퇴장의 ConnectionLost Error 포함 2줄이 있다.

이번 검사는 명시적 플레이어 HTTP 포기의 HUD 반영 범위다. 게임 내 확인창 조작·10분
자연 만료·실제 지급/정산/복귀·Steam/Shipping 완료로 확대하지 않는다. 기존 통과한
피격/재접속/보상 흐름은 반복하지 않았다. 자연 만료의 후속 관측은 아래 기록을 따른다.

### 자연 10분 만료의 실제 HUD 관측 (2026-10-28, 공유 소스 변경으로 전체 러너는 실패)

기존 러너에 `-ObservePartyCombatExpiry`를 추가했다. 렌더링 2인·
`-ObservePartyCombatHud -VerifyRemainingPartySettlement -VerifyUnexpectedDisconnect`에서만
허용하고 재접속/명시적 포기/피격/입력 모드와 혼합을 거절한다. 기존 실제 소켓 단절·운영
폴러·독립 SQL 검사를 재사용하며 유예 시작과 만료 직전의 2인 HUD, 확정 만료 뒤의 1인
HUD를 관측한다. 만료 직전 상태 관측의 대기만 짧게 제한하고 운영 600초는 바꾸지 않는다.
이 모드도 승리 게이트를 열지 않고 정산/복귀 전에 종료한다. PS 구문과 새 모드의 잘못된
조합 거절 3개를 확인했고 기존 파서/native/보상 회귀는 반복하지 않았다.

실행 폴더는 `Saved/Tests/DungeonMembershipBackend/3d7dfa0692ff44a59e3647ea50f43c54/`다.
스킬/보스 AI/아이템 UI·게임 C++/WBP·정책·공유 DLL을 수정하거나 새 UE 빌드를 하지 않았다.
개발 인증/PostgreSQL·티켓/Iris·기존 80–160ms/5% 손실에서 다음을 실제 관측했다.

- 소유 테스트 클라이언트 하나만 강제 종료했다. 원래 단절은 KST 00:09:41.405,
  만료는 00:19:41.405로 정확히 **600초**다. 독립 DB 유예 표본은 110개이며 마지막 표본은
  만료 약 2.9초 전이다. 시계/DB 변경·가짜 단절 보고·명시적 포기·러너의 만료 POST는 없다.
- 양쪽 초기 2인 화면, 유예 시작/만료 약 14초 전의 2인 대기 행과 숨겨진 대상 체력바,
  확정 만료 뒤 1인/남은 HP 150·탈퇴 알림의 실제 Slate/PNG **5개**를 보존했다.
  최종 PNG도 직접 확인했다. API/DB의 같은 불변 영수증·보상 제외, 새 티켓/파티 재참가
  409와 서버의 같은 남은 참가자/전투·InProgress 유지까지 관측했다.
- 테스트만 정리했고 사용자 에디터·다른 프로세스/작업은 유지했다. 닫힌 로그 5개와 실행
  DLL `E563DE70…5855C58`은 사후에도 일치했다. 기존 기동 AutomationTest Error 30줄은
  남으므로 전체 Error 0으로 표시하지 않는다.

**원본 `Failed.proof.json`은 passed=false·러너 종료 1이다.** 실행 중 다른 작업에서 공용
`RPGPlayerController.cpp/.h`가 변경돼 최종 입력 일치 가드가 거절했다. 이 파일을 되돌리거나
가드를 완화하지 않았다. 런타임 DLL이 같다는 이유로 새 Controller 소스까지 검증했다고
계산하지 않으며, `PartyHudExpiry.proof.json`의 전체 PASS는 생성되지 않았다.

`ReadOnlyHudObservation.json`은 닫힌 5개 관측의 파서/파일 해시·유예 전후 시각과 기존
DLL/로그 해시를 읽기 전용으로 재대조한 기록이다. `observationChecksPassed=true`지만
`originalRunnerPassed=false`, `promotedToRunnerPass=false`, `currentSourceVerified=false`와
변경 입력 2개를 명시한다. 새 실행·DB/게임 변경이나 원본 결과의 성공 승격은 아니다.
기존 DLL의 자연 만료 HUD 근거로 유지하고 새 소스 통합은 해당 작업의 빌드 후 구분한다.
동일한 10분/기존 재접속·보상 정산 검사는 이 변경만을 이유로 다시 실행하지 않았다.

## 초기 검토/검증 기록

순수 presentation의 체력/사망/정보 누락·잘못된 수치·이름/범위 일치, 합성 서버 신원의 같은
세션/다른 세션·미확인·중복·inactive·회수/수신자 즉시 읽기 차단, 수동 HUD의 초기 데이터/
불명 체력 바·늦은 갱신/행 정리를 다루는 집중 사례를 소스로 추가했다. 이것은 실제
Join Ticket/HTTP/네트워크 입장·Iris 복제 성공 증거가 아니다.

초기 HUD 구현은 소스/API·공백만 검토했고, 이후 직접 영향 C++ 10개 TU의 `-NoLink`
개별 컴파일을 완료했다. 최초 GameMode/테스트의 불완전한 `UPackage` 타입 오류는 명시적
include로 수정해 해당 TU가 통과했으며 실패 로그도 보존했다. 로그 묶음:
`Saved/Tests/PartyRosterCompile/b0ba613603c94b4889397b867a81d084/`,
`b72e37da54d9427fba870cb55c6ab622/`, `cd716b827d554541a79e237b82d21261/`,
`1ce1305028d24f329f503f179f339392/`.

후속 명단/재접속 연결의 backend 격리 Release 빌드와 `--party-presentation-only`의 변환
21개 집중 확인은 exit 0이다. 기한 경과 조회가 멤버를 삭제하지 않는 점·정밀도/이름 상한·
저장소 시계·새 티켓/이전 단절 세대 분리·기한 overflow 시 영수증 보존을 확인했다.
HTTP/DB/실제 만료를 실행하지 않았다. 최신 로그:
`Saved/Tests/PartyRosterPresentation/23d468709ef04a1d85bf25e6dbc25bfb/backend-presentation.log`.
UE 집중 사례에는 선택 메타데이터의 권한 증거 보존·명단/시간/누락 유지·재단절과 불변 영수증
제거를 추가했다. 단절 세대 후속의 codec/컴포넌트/집중 테스트 2개 TU도 `-NoLink` 컴파일
exit 0이다: `Saved/Tests/PartyRosterCompile/c7674c60d237409d938d145b22e86f31/`.

새 모듈 링크·전체 타깃 성공·native 실행·2~4인 Iris·실제 체력 변화/전멸/재도전·GPU 배치/
입력·Steam/Shipping은 별도다. 현재 실행 중인 에디터/Live Coding을 종료하지 않고 DLL도
교체하지 않았다. 다음 정상 링크/재실행 후 집중 사례와 실제 2인 흐름에서 owner-only 수신·
다른 세션 제외·체력/사망·접속/재도전 전환을 확인해야 한다. 기존 DLL/proof로 대체하지 않는다.

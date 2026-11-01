# 던전 보상: 인벤토리 우선, 초과 수량만 우편

## 서버 던전 ID·맵·보상 정의와 스냅샷 고정 (2026-10-23)

`URPGDungeonServerDefinition`은 서버가 관리하는 던전 ID, 대상 World의 soft reference,
난이도별 기존 `URPGDungeonRewardDefinition` 참조를 묶는다. 클라이언트의
`URPGDungeonDefinition`(이름/선택 난이도)과 별개이며, 클라이언트 요청에 맵이나 보상 정의
참조를 새로 받지 않는다. 실제 권위 GameMode의 `DungeonServerDefinitions` 목록만 조회한다.

- 서버 목록은 최대 64개이며 null/잘못된 정의·중복 ID·잘못된 맵 경로/난이도/보상 payload는
  일부 행을 건너뛰지 않고 거절한다. ID/맵 비교는 정확한 문자열을 사용하고, 실제 World의
  PIE 접두어만 엔진 함수로 제거한다. 맵 조회/배정 검사 자체는 World를 로드하지 않는다.
- 인증된 배정 세션/서버의 `InProgress` 응답과 정확한 난이도라는 기존 전제조건을 먼저
  검사한다. 목록에 해당 ID가 있고 현재 실행 중인 맵/지원 난이도가 맞을 때만 입장을 준비한다.
  잘못된 설정을 기본 던전/보상으로 바꾸지 않으며 준비 전 티켓 소비 차단은 기존 경로를 따른다.
- 첫 확정에서 선택한 정의/보상 참조·ID·보상 버전과 재화/아이템 전송 payload의 복사본을
  고정한다. `ReportConfiguredDungeonClear`는 이 복사본으로 기존 정산 경로를 호출한다.
  동일 버전 자산의 수량을 나중에 수정해도 실행 중인 세션의 보상을 재계산하지 않는다.
  정의/보상 참조나 버전 변경·목록 제거로 실행 중 설정을 교체할 수 없고, 충돌 응답은 기존
  준비 상태/멤버/보상 스냅샷을 보존한다. EndPlay는 해당 GameMode의 스냅샷만 정리한다.
- 서버 정의 목록이 비어 있는 미전환 GameMode는 기존 난이도별 보상 설정을 유지한다.
  변환된 레이드에서는 목록이 설정되어 있으므로 알 수 없는 ID를 legacy로 우회하지 못한다.
  기존 `bGiveReward`와 서버 전용 저수준 특수 정산 API의 의미·백엔드 지급/우편 계약은 유지한다.

콘텐츠 변형은 새 서버 정의의 ID/맵/난이도별 보상을 작성하고 해당 맵 GameMode의 서버
목록에 등록한다. 같은 공통 GameMode 코드와 입장/재접속/정산을 재사용하며, 클라이언트
선택 정의에는 같은 ID와 표시/지원 난이도만 작성한다. 기존 세션은 고정된 값을 사용하므로
정상적인 보상 개정에는 새 버전과 새 세션을 사용한다.

기본 연결은 기존 `bossmap`과 `BP_BossBattleGameMode`의 현재 네 보상 참조를 복사하는
`DA_DungeonServer_RaidSwordMan`이다. `CreateDungeonServerDefinitionAssets.py`는 누락된
정의만 생성하고 기존 정의/보상·비어 있지 않은 다른 서버 목록을 덮어쓰지 않는다. GameMode
원본 백업 후 새 프로세스에서 `-RPGDungeonBindServerDefinition`으로 빈 서버 목록만 연결한다.
Python에서 soft World 속성을 직접 읽으면 맵을 로드하므로 기존 정의 확인은 native
`GetDungeonMapPackage`의 메타데이터 조회를 사용한다. 맵/AI 그래프/보상 에셋은 저장하지 않는다.

이 구현은 **잘못된 맵의 서버가 입장을 승인하지 못하게 검사하는 경로**다. 백엔드/운영 할당기가
ID에 맞는 서버 프로세스와 맵을 자동 기동하는 기능은 아직 별도이며, GameMode가 임의의
맵으로 자동 travel하지 않는다. 실제 Steam·Shipping/cook·새 콘텐츠/4인 검증과도 구분한다.

직접 영향 검증은 `Run-DungeonSelectionAutomationTest.ps1 -ServerContentOnly`, 실제 기본
레이드의 로비/입장/정산/복귀는 기존 보스 러너의 `-LobbyUI -VerifyServerDungeonContent`를
사용한다. 이후 보상 개정은 서버 정의의 참조/버전으로 작성한다. 과거 BP의
`DungeonClearRewardsByDifficulty`는 호환을 위해 보존했으며 서버 목록이 있는 온라인
세션의 선택 근거는 아니다. 초기 생성 스크립트는 충돌을 자동 동기화하는 도구가 아니다.

실제 연결과 검증:

- 기존 GameMode 원본과 맵/네 보상 에셋의 초기 해시를
  `Saved/Backups/DungeonServerContent/51b5ce46f1de4c8a9419c8d46ad36b68/Before.proof.json`
  및 같은 폴더의 원본 `.uasset`에 보존했다. GameMode는 서버 목록만 추가 저장했고 맵/네
  보상 에셋 5개의 해시는 그대로다. 새 정의와 BP 연결 결과는
  `Saved/Tests/DungeonServerContentSetup/061a43224c424c8f8017f730bed1cc45/Setup.proof.json`.
- 최초 설정 `.../7e8d74727c394e86a361c18bd14a3c1a/`는 정의 생성 후 BP 파일 이동/저장에
  실패했으며 원본 BP는 바뀌지 않았다. Python의 soft World 속성 접근이 실제 맵을 로드하므로
  메타데이터 조회 함수와 생성/연결 프로세스를 분리했다. 새 프로세스의 기존 정의 검증·
  빈 목록 연결은 exit 0이며 첫 실패 로그도 보존했다. 외부 프로세스를 강제 종료하거나
  파일 잠금을 강제로 해제하지 않았다.
- 다른 빌드를 기다린 뒤 자체 Editor **4개 작업/타깃·lib/DLL 링크**, 메타데이터 함수 추가의
  **8개 작업/전체 링크**가 성공했다. 로그:
  `Saved/Logs/DungeonServerContent{Build,MetadataBuild}-20261023.log`.
- 최초 집중 native 4개 중 Catalog/GameMode/AuthoredDefault 3개는 Success였고 Definition은
  `NAME_None`이 잘못된 버전이라는 테스트 가정으로 실패했다. 기존 보상 codec은 문자열
  `None`을 단순 식별자로 허용하므로 운영 규칙을 바꾸지 않고 공백을 포함한 명확한 오류
  문자열로 테스트만 수정했다. 원본 러너 실패를 PASS로 바꾸지 않았다:
  `Saved/Tests/DungeonSelection/c583608ca67d4b25818fc0158a03cd4c/Verification.proof.json`.
- 테스트 전용 **4개 작업/전체 링크** 뒤 `-ServerContentOnly -ExactTest
  ProjectRPG.Online.DungeonServerContent.Definition`의 단일 native가 Success·러너 exit 0이다.
  `Saved/Tests/DungeonSelection/6fc636cc14ea41bdaa43070fb7aa6b83/Verification.proof.json`.
  입력 16개/실행 DLL·닫힌 로그 일치, 기존 기동 Error 15줄/검사 후 Error 0이다. 통과한
  다른 3개를 반복하거나 단일 실행의 native 4개 전체 PASS로 표현하지 않는다.
- `Run-DungeonBossReturnE2ETest.ps1 -LobbyUI -VerifyServerDungeonContent -LobbyDifficulty
  Normal -ClientCount 2 -Port 18874 -BackendUrl http://127.0.0.1:3074 -SkipPostgresStart
  -NoBackendRestore -TimeoutSeconds 600`의 실제 **2인 로비→배정/티켓 입장→서버 정의의
  맵/Normal 보상 버전 확정→기존 시험용 GAS 처치→정산→두 결과 버튼/로비 복귀**가 exit 0이다.
  `Saved/Tests/DungeonBossReturn/98de5b0b3bac4ebdbae42164cf518e20/result.json` 및
  `server-content.proof.json`. 잘못된 옵션 조합 3건도 실행 전 거절했다.
- 독립 읽기 전용 SQL의 동일 `Dungeon.RaidSwordMan / Normal / Cleared`와 `Completed`
  수령인 정확히 2명·버전 `pve_normal_v1`을 확인했다(`server-content-durable.proof.json`).
  각 골드 250·가방 물약 2·우편 0·기본 대검 보존이다. 실행 당시 입력 33개/DLL·종료 로그
  5개 해시가 일치했다. 서버/백엔드 Error 0, 각 클라이언트는 계획된 복귀 ConnectionLost
  Error 1줄이며 Fatal/Assert/Ensure/흐름 실패 0이다. 이후 단일 테스트 수정의 DLL과는
  구분한다. 마지막 사후 검사에서는 통합 종료 후 다른 작업의 `RPGPlayerController.cpp`
  수정 1개를 관측했다(나머지 32개·종료 로그 불변). 서버 정의/GameMode/보상 코드는 그대로이며
  기존 통합은 실행 당시 소스/DLL의 증거다. 후속 Controller 수정까지 검증했다고 표시하거나
  DLL 변경만으로 이 통합을 반복하지 않았다.

실제 서버 자동 기동/할당기의 콘텐츠 허용 목록, 새 콘텐츠 ID/4인/자연 전투·Steam·Shipping은
별도다. 원래 보스 AI/맵/보상 에셋과 DB 스키마·지급 정책은 바꾸지 않았고 테스트 DB
초기화/삭제 없이 새 개발 기록을 보존했다. `project-rpg-online`에 따라 배정/맵 확인과
클라이언트 선택·서버 정산 권한을 분리하고 자기 GameMode 스냅샷만 정리했다.

## 지급 계약

새 UE 보상 정의는 아이템 에셋의 `MaxStackSize`를 서버에서 추출해 정산 요청에
`maxStackSize`로 보낸다. 플레이어 클라이언트가 지급 위치나 스택 제한을 결정하지 않는다.
현재 한 보상 항목의 수량은 해당 스택 제한 이하이며, 한 정산은 최대 16항목이다.

백엔드는 캐릭터마다 다음 순서로 지급한다.

1. 현재 인벤토리의 호환되는 스택을 슬롯 번호 순서로 채운다.
2. 남은 수량을 가장 낮은 빈 인벤토리 슬롯에 넣는다.
3. 빈 슬롯이 없으면 **남은 수량만** `DungeonReward.<sessionId>` 우편에 보관한다.

예: 물약 8/10이 있고 가방 슬롯이 모두 찬 상태에서 물약 5개를 보상받으면,
기존 물약은 10개가 되고 우편에는 3개만 남는다. 빈 슬롯이 있으면 그 3개도 가방에 넣는다.

인벤토리 ID는 캐릭터 GUID 문자열이며 용량은 80슬롯이다. 백엔드의
`DungeonRewardDeliveryPlanner.InventorySlotCapacity`와 UE의
`RPGItemInventoryPolicy::SlotCapacity`를 함께 변경해야 한다. 가방 확장은 아직 지원하지 않는다.

스택 병합은 정의/버전, 태그, 능력치, 귀속, 내구도, 만료 시각이 같은 경우에만 허용한다.
잠긴 스택은 병합하지 않는다. 태그/능력치의 배열 순서는 영향을 주지 않는다.
기존 스택의 GUID·시드·생성 출처는 보존하고 리비전은 정산당 한 번 올린다.

## 서버 배정 난이도의 확정 (2026-10-19)

온라인 GameMode의 난이도/보상 정의는 로컬 GameInstance 기본값이나 클라이언트 입력이
아니라, 인증된 서버 시작/heartbeat 응답의 배정 난이도를 따른다. 기존 E2E의 `Hard` 강제
설정만으로 이 운영 연결이 구현됐다고 보지 않는다.

- 현재 권위 GameMode와 정확한 배정 세션·서버의 `InProgress` 응답에서만 확정한다.
  문자열은 대소문자를 포함해 정확히 `Easy` / `Normal` / `Hard` / `Hell`이어야 한다.
  알 수 없는 값·실패/다른 배정 응답을 기본 난이도로 바꾸거나 멤버 삭제 근거로 쓰지 않는다.
- 첫 확정에서 `CurrentGameDifficulty`와 서버 GameInstance의 pending 값, 보상 멤버 스냅샷을
  반영한 뒤 `OnBackendDungeonDifficultyConfirmed`를 한 번 알린다. 같은 난이도의 반복
  heartbeat는 멤버 스냅샷만 갱신하며, 다른 난이도 응답은 기존 설정/멤버를 보존하고 무시한다.
- 배정된 온라인 서버에서는 기존 BP `SetGameDifficulty`로 설정을 덮어쓰지 못한다.
  배정이 없는 오프라인/로컬 설정 경로는 유지한다. EndPlay 뒤 늦은 응답으로 준비 상태를
  되살리지 않는다.
- 확정 전 새 입장은 `dungeon_configuration_not_ready`로 **티켓 소비 전에** 거절한다.
  `ReportConfiguredDungeonClear`도 기본값으로 보상 정의를 먼저 선택하지 않는다.
  이미 제출된 트랜잭션을 취소하거나 남은 파티를 퇴장시키는 동작은 추가하지 않았다.

서버 GameMode BP 적용:

1. BeginPlay에서 `OnBackendDungeonDifficultyConfirmed`에 자신의 초기화 이벤트를 연결한다.
2. 전달된 `Difficulty` 또는 BP Pure `GetCurrentGameDifficulty`를 이용해 난이도별 스포너/
   전투 설정을 초기화한다. 온라인에서 `SetGameDifficulty`를 다시 호출하지 않는다.
3. 늦게 생성된 BP는 `IsBackendDungeonDifficultyReady`를 확인해 이미 확정됐으면 즉시
   같은 초기화를 수행한다. 초기화 함수 자체도 중복 실행에 안전하게 만든다.
4. 던전 클리어는 기존 서버 `ReportConfiguredDungeonClear`/보상 정의 경로를 사용한다.
   이 이벤트에서 직접 보상을 지급하거나 클라이언트가 보낸 난이도를 신뢰하지 않는다.

준비 노드는 난이도 확정 여부이며 모든 BP 비동기 로드/스포너 준비 완료를 뜻하지 않는다.
Delay/비동기 로드가 필요한 콘텐츠는 기존 콘텐츠 준비 가드도 함께 사용해야 한다.

저장 BP 자산/보스 능력치 테이블/백엔드 DB 스키마는 이번 코드 변경으로 수정하지 않았다.
맵의 BeginPlay에서 이미 생성된 적의 모든 수치를 자동 재설정했다는 뜻이 아니며,
난이도 의존 콘텐츠 초기화는 위 이벤트/준비 노드에 연결해야 한다. 입장 시점과 기존
네이티브 보상 정의 선택은 GameMode에서 보호한다.

검증: 최초 빌드는 별도 단절 순서 테스트의 불완전 타입 오류로 실패했지만 해당 소스의
include는 재확인 전에 이미 보완돼 있어 중복 수정하지 않았다. 난이도 파서 출력의 초기화
경고만 정리한 후 최종 Editor 증분 6개 작업/게임 DLL 링크가 성공했다. 신규
`DungeonDifficulty.Codec` / `Configuration` / `BlueprintContract`와 기존 보상 정의 3개,
총 native 6개가 종료 0·Success였다. 잘못된 배정/상태/대소문자·다른 난이도 heartbeat,
온라인 setter 차단·GameInstance 동기화·EndPlay 뒤 무효화와 BP 반영/보상 자산을 검사했다.

선택 소스 3개와 실행 전후 DLL은 일치했다. 단 테스트 시작 전 기존 `Condition failed`
Error 15개가 남아 엄격 로그 감사는 실패이며 원인/오류 없는 기동을 해결한 것은 아니다.
테스트 이후 Error·Fatal/Assert/Ensure는 0개다. 증거:
`Saved/Tests/DungeonDifficulty-mve2mmpo/Verification.proof.json`.
실제 HTTP 시작/heartbeat→PreLogin 티켓 순서의 다인 통합, 보스 BP 수치/스포너 자산 연결과
이벤트 수신 게임플레이, PostgreSQL·Steam·Shipping 검증은 별도다.

### 후속: 기존 보스 BP의 생성/초기화 연결 (2026-10-19)

기존 `BP_BossBattleGameMode`의 보스 생성은 그래프가 아니라 부모
`ARPGBossBattleGameMode::BeginPlay` → `SpawnBoss`가 소유한다. NPC는 빙의 시점에
GameMode 난이도를 읽어 시작 데이터의 GAS ApplyLevel을 `Easy=1`, `Normal=2`,
`Hard=3`, `Hell=4`로 고정한다. 따라서 초기 효과를 적용한 보스를 나중에 재설정하지 않고,
그 **최초 생성/빙의가 난이도 확정 뒤** 일어나도록 연결했다.

- 부모 BeginPlay 전에 난이도 확정 이벤트를 구독하고, 이미 확정된 경우도 기존 생성
  호출에서 처리한다. 온라인/미확정 상태의 직접 `SpawnBoss` 호출은 생성하지 않는다.
- 늦게 확정되면 서버 BP의 확정 알림 처리가 끝난 다음 월드 틱에 생성한다. 원래 월드의
  권위 GameMode·확정 여부·난이도를 재확인하며 예약은 하나만 유지한다.
- 생성 전에는 보스전이 Waiting을 유지한다. 생성 뒤에도 기존 NPC 시작 데이터 Ready와
  생존 참가자 검사를 거쳐야 전투를 시작한다. 이미 부여된 GAS 효과를 재부여하거나
  진행 중 전투의 수치를 덮어쓰지 않는다.
- EndPlay는 자기 이벤트 바인딩과 생성 예약을 해제한다. 종료 후 지연 이벤트/직접 생성은
  무시한다. 오프라인 초기 생성과 기존 전멸/재시도 경로는 유지한다.

이 공통 부모 기반 BP의 **기본 보스 생성**에는 추가 그래프 바인딩이 필요 없다. 위 BP
연결 안내는 별도 커스텀 스포너/기믹 초기화에도 사용할 수 있다. 저장 BP·BossClass·시작
데이터·보스 수치표·보상·DB는 이번 작업에서 수정하지 않았다.

검증: 새 검사 코드의 컴파일 오류를 보완한 뒤 최종 Editor 증분 4개 작업/게임 DLL 링크가
성공했다. 난이도 5개와 기존 `Boss.NPCStartup`, `Boss.BattleRetry`,
`DungeonReward.ConfiguredAssetsAreValid`, 총 native 8개가 Success·종료 0이었다.
실제 저장 BP의 부모/보스 클래스 참조와 직접 SpawnActor 노드가 없음을 읽기 전용으로
확인하고, production 생성 경로의 fixture에서 미확정/다른 배정 차단·확정 뒤 다음 틱 생성·
Hard의 실제 NPC GAS ApplyLevel 3·단일 부여·조기 전투 방지·종료 취소를 검사했다.

선택 입력 10개(BP 포함)는 유지됐고 테스트 실행 전/종료 시 DLL 해시도 일치했다.
종료 13초 뒤 공유 DLL이 재링크돼 이후 바이너리는 이 실행의 검증 대상으로 표시하지
않는다. 시작 전 기존 `Condition failed` Error 15개로 엄격 로그 감사는 실패이며,
테스트 시작 이후 Error·Fatal/Assert/Ensure는 0개다. 증거:
`Saved/Tests/BackendBossStartup-mve3l07m/Verification.proof.json`.
실제 HTTP 배정/PreLogin 티켓 순서, 실제 보스 BP의 다인 전투, PostgreSQL·Steam·Shipping은
이번 집중 검사에 포함하지 않았다. 다음은 실제 배정 응답으로 기존 보스 맵을 시작해
미확정 대기→확정 난이도 초기화→입장/전투가 이어지는지 확인하는 통합 단계다.

### 후속: 실제 배정에서 보스 맵 전투까지 (2026-10-19)

기존 `RPGDungeonFlowE2EProbe`가 서버 GameMode와 GameInstance에 Hard를 직접 쓰던
부분을 제거했다. 공통 probe를 쓰는 다른 난이도 실행도 자신에게 배정된 난이도를 따른다.
명시적 개발 fixture의 전투 제한 시간/입장 장벽은 유지하되 난이도·초기 HP·Ability는
테스트에서 주입하지 않는다.

서버는 시작 HTTP 전 미확정·보스 없음·Waiting을 관측하고, 실제 확정 뒤 GameInstance
동기화·NPC 시작 데이터 Ready·살아 있는 초기 HP와 서버 NPC Ability spec 레벨을
검사한 뒤 준비를 알린다. 레벨은 배정 난이도의 1/2/3/4와 비교한다. 모든 초기 Effect의
개별 적용 성공이나 비동기 Ability 완료까지 보증한다는 뜻은 아니다.

기존 `Run-DungeonBossReturnE2ETest.ps1`은 이 대기→확정→보스 준비 순서를 요구하고,
백엔드 GET의 정확한 InProgress·Hard·배정 서버를 확인한 뒤 클라이언트를 입장시킨다.
기존 영속 대검/실제 플레이어 입력·보스 AI·DB 지급·결과 버튼·로비 복귀 경로를 재사용한다.
실행 전후 모듈 해시를 기록하며, 도중 공유 DLL이 교체되면 기능 관측 결과를 보존하되
현재 바이너리 통과로 표시하지 않는다. 저장 BP/수치표/DB 스키마는 수정하지 않았다.

실제 첫 실행에서는 엔진/맵 콜드 부팅 중 배정의 기존 90초 임대가 먼저 만료됐다.
DB는 Loading/Hard였고, 서버 토큰 자체는 12시간 중 대부분이 남아 있었지만 시작 요청은
401이었다. 이는 만료된 세션의 자격 증명을 거절하는 기존 인증 경계가 동작한 결과다.
이 실패를 우회하기 위해 인증/만료 검사를 풀거나 DB deadline을 직접 늘리지 않았다.

백엔드의 서버 배정 `claim`/`activate`에 별도
`DungeonSessions:ServerStartupLeaseSeconds`(기본 300초, 30~900초 clamp)를 연결했다.
`start`/`heartbeat`는 기존 `ActiveLeaseSeconds` 기본 90초와 기존 제한을 그대로 사용한다.
이 준비 시간은 클라이언트 요청 값이 아니라 백엔드 운영 설정이다. 토큰 만료·정확한
세션/서버 대조·유예 정각의 인증 거절은 유지하며 만료된 배정을 부활시키지 않는다.

Release 백엔드/검사 빌드와 정책 경계 1개·메모리/실제 PostgreSQL 각 3개, 총 7개 집중
검사 그룹을 통과했다. 120초 콜드 부팅 후 시작 가능, 시작 뒤 90초 활성 임대로 전환,
활성/준비 유예 정각의 인증 거절과 늦은 start 차단을 검사했다. 테스트 전용 PG 스키마만
정리했고 public 데이터는 유지했다.

최종 Editor 전체 타깃 95개 작업/게임 DLL 링크 성공 뒤 수정한 백엔드로 2인 재실행을
통과했다. 실제 저장 보스 BP가 미확정 대기→HTTP Hard 확정→NPC Ability 4개의 레벨 3·
초기 HP 444→Iris 입장으로 이어졌으며, 영속 대검의 실제 입력·보스 AI 가동/공격 종료→
Dead/Victory→PostgreSQL 각자 골드 500·물약 3·투구 1 및 장비 대검 보존→결과 버튼→
두 로컬 로비 맵 복귀를 확인했다. 러너 종료 0, 결과:
`Saved/Tests/DungeonBossReturn/d9d736fe624e434fb58dae7161f3dc5f/result.json`.
이 실행에서는 보스 공격에 의한 플레이어 피격을 관측하지 않았으므로 피격 검증으로
확대하지 않는다.

실행 전후 DLL과 선택 UE/스크립트/BP 입력 10개·백엔드/검사 입력 5개가 일치했다.
첫 만료 실패는 `a1e38922d3d745b6b3a2ed8653d0c783`에 보존했다. 최종 로그에 Error 표시
12줄(정확한 계획 복귀 ConnectionLost 2줄 포함)이 있어 엄격한 오류 0 감사는 실패다.
그 밖의 Error 10줄을 별도 오류 해결/허용 처리하지 않았고 Fatal/Assert/Ensure는 0이다.
통합 증거와 범위:
`Saved/Tests/BackendDifficultyFlow-mvf8n4h8/Verification.proof.json`.
실제 네트워크의 확정 전 거절/티켓 미소비 부정 검사, 네 난이도 전부·4인·Steam·다른 PC·
Shipping/GPU·지연/손실은 포함하지 않았다. 다음 기능은 로비의 난이도 선택 UI/BP를
실제 세션 생성에 연결해 Hard 외의 배정도 같은 확정 경로를 사용하게 하는 것이다.

## 원자성·동시성·재시도

지급 계획은 정산 워커가 사전에 조회해서 만들지 않고, 저장소 트랜잭션 안에서 만든다.
PostgreSQL의 캐릭터 소유자 잠금은 일반 아이템 변경과 보상 지급 양쪽에서 공유한다.
파티 전체 소유자 잠금을 같은 순서로 획득한 뒤, 재화와 아이템을 하나의 트랜잭션으로
저장한다. 메모리 저장소도 동일한 트랜잭션 잠금과 롤백 스냅샷을 사용한다.

기존 지급 영수증이 있으면 가방 상태를 다시 계산하지 않는다. 지급 후 아이템을
소비하거나 이동해도 같은 정산을 재시도해서 추가 지급하지 않는다. 다른 명령 내용으로
같은 요청 ID를 사용하면 거절한다. 지급 예정 GUID의 충돌도 병합으로 숨기지 않는다.

스택 병합으로 기존 80개 스택과 신규 최대 16개를 함께 변경할 수 있으므로,
**내부 정산 경로만** 최대 96개 변경을 허용한다. 외부 일반 아이템 Commit API의
16개 변경 제한은 유지한다. 우편 목록의 페이지 크기가 인벤토리 지급 계획을 잘라내지 않는다.

## 이전 데이터 호환

`maxStackSize`가 없거나 0인 이전 보상은 기존 우편 전용 지급을 유지한다.
스택 제한을 모르는 이미 저장된 정산을 임의의 제한으로 변환하지 않는다.
0일 때 해당 필드는 JSON 직렬화에서 생략해 이전 명령 지문도 유지한다.
이미 지급된 우편을 자동으로 가방에 옮기거나 새로 지급하지 않는다.
스키마 변경은 필요 없다.

기존 우편 화면의 Claim은 첨부 전체를 첫 빈 슬롯으로 옮기는 기능을 유지한다.
우편 수령 자체의 자동 스택 병합/부분 수령은 이번 작업에 포함하지 않는다.

## 검증

```powershell
dotnet run --project Backend/ProjectRpg.Backend.RewardDeliveryTests/ProjectRpg.Backend.RewardDeliveryTests.csproj

# 실제 개발용 PostgreSQL에서도 실행. 운영 DB에는 실행하지 않는다.
$env:PROJECT_RPG_REWARD_TEST_POSTGRES = $env:POSTGRES_CONNECTION_STRING
dotnet run --project Backend/ProjectRpg.Backend.RewardDeliveryTests/ProjectRpg.Backend.RewardDeliveryTests.csproj

# HTTP 요청·영속 정산 워커·백엔드 재시작까지 확인
Backend/postgres-integration-test.ps1 -KeepArtifacts

# 새 UE 바이너리를 빌드한 뒤 기본 실행은 인벤토리 직접 지급·재접속 검증
Scripts/Network/Run-DungeonRewardPersistenceE2ETest.ps1 -ClientCount 2
```

저장소 테스트는 실행마다 별도 테스트 캐릭터/아이템/재화 코드를 생성한다.
개발 DB의 테스트 데이터는 보존한다. 정산 원자성, 빈/부분/가득 찬 가방, 여러 보상 항목,
메타데이터 불일치, 다수 스택 병합, 지급 후 소비·재시도, 동시 정산/일반 아이템 변경,
기존 우편 데이터 호환을 검사한다.

2026-10-05: 메모리 25개와 PostgreSQL 25개 테스트 통과.
로그: `Saved/Logs/RewardInventoryFirst-Tests-20261005.log`.
HTTP 통합 검증은 네 난이도의 인벤토리 우선 지급과 네 난이도의 이전 우편 전용
지급을 모두 검사하고, 백엔드 재시작 후 같은 수량·위치·리비전이 유지되는 것을 확인했다.
HTTP 로그: `Saved/Logs/RewardInventoryFirst-HttpIntegration-20261005.log`.

에디터 종료 후 UE 전체 빌드와 2인 인벤토리 직접 지급·이전 우편 동시 Claim 회귀가 통과했다.
최초의 DLL 잠금/옵션 UI 헤더 오류 로그는 실패 이력이며 현재 빌드 상태를 뜻하지 않는다.
기준선 빌드: `Saved/Logs/RewardInventoryFirstBuild-ClosedEditor-20261005.log`.
기준선 결과: `Saved/Logs/DungeonRewardE2E/20261005-034838-7e66975f8aa7/result.json`,
`Saved/Logs/DungeonRewardE2E/20261005-035259-76a37f1a078e/result.json`.

이후 재접속 없는 지갑·인벤토리·우편 갱신과 열린 화면의 지급 안내를 연결했다.
2인 직접 지급의 실시간 UI 및 재시작 복구도 통과했다. 세션에 묶인 읽기 전용 API,
조회 응답의 세대 검사와 최신 검증 로그는
[재접속 없는 보상 갱신](DUNGEON_REWARD_LIVE_REFRESH.md)을 참고한다.

실제 80슬롯 포화/부분 스택의 2인 초과 우편 지급→열린 화면 갱신→수령→재시작
통합 E2E도 통과했다. 준비 데이터, 수령 권한과 결과 로그는
[포화 가방 E2E](DUNGEON_REWARD_OVERFLOW_E2E.md)를 참고한다.

## 아직 남은 작업

- 지급 안내를 실제 보스 결과 화면에 연결하고 시각적 배치/한국어 지역화 검증.
- 실제 협동 보스전 → 정산 → 화면 갱신 → 로비 복귀 전체 E2E.

현재 완료 범위는 DB 지급 위치 결정·영속성, UE 정산 페이로드와 재접속 없는 소유자 화면 갱신이다.
실제 협동 보스전과 로비 복귀까지 전체 게임 흐름의 검증 완료를 뜻하지 않는다.

## 경험치 전용 완료의 작성 경계 (2026-10-29)

첫 훈련의 경험치 100 보상을 연결하기 위해 `URPGDungeonRewardDefinition::BuildSettlement`가 기존 진행 계약의 정확한 `FirstSlice.v1`을 명시한 빈 재화/아이템 구성을 허용한다. 기존 보상 버전을 명시적 opt-in으로 사용하고 새 플래그·가짜 골드/아이템·경험치 지갑을 추가하지 않았다. 정규화된 버전은 대소문자까지 기존 backend의 `FirstSliceProgression.RewardVersion`과 같아야 한다. 일반 빈 구성·다른 버전·`no_reward`는 계속 거절하며 일반 보상의 수량/식별자/항목 수 검사는 유지한다.

작성 시 보상 정의에 `RewardVersion=FirstSlice.v1`, 빈 `CurrencyChanges`/`ItemRewards`를 두고 기존 GameMode의 보상 보고 경로(`bGiveReward=true`)를 사용한다. `bGiveReward=false`의 `no_reward` 완료를 훈련 경험치로 바꾸지 않았다. 경험치량·수령 자격·완료 이정표·중복 방지는 backend 진행/정산 경로가 소유하며 UE 정의가 로컬 경험치나 SP를 지급하지 않는다. 이 변경만으로 새 콘텐츠 ID나 습득 권한이 생기지 않는다.

기존 서버 정의의 ValidateDefinition/BuildAssignedSelection이 같은 builder를 호출하므로 `Progression.Training`/Normal의 후보 선택에 정확한 버전과 빈 transport 배열을 복사할 수 있다. 기존 GameMode의 확정 payload 복사 → ReportConfiguredDungeonClear → 정산 보고 호출도 버전/빈 배열을 보존하는 것을 소스로 확인했다. GameMode/HTTP/backend/API/스키마·기존 보상 에셋은 수정하지 않았다.

근거는 `Saved/BuildChecks/ProgressionOnlyReward-20261029/ImplementationResult.json`이다. 공유 Intermediate/UBT/UHT를 쓰지 않고 완료된 ContributionHUDLive의 링크 입력 obj/lib/res와 PCH **480개를 자체 복사본으로 고정**했다. 수정 reward cpp/기존 검사 cpp 두 개를 원래 컴파일 설정으로 분리 CL 컴파일하고 해당 객체만 교체해 별도 DLL 링크를 성공시켰다. 다른 공용 소스의 최신 변경 전체를 빌드한 타깃 성공으로 계산하지 않는다.

별도 RuntimeProject의 `Native.log`/`NativeReport`에서 **5개 Success·프로세스 종료 0**이다. 새 2개는 정확한 빈 payload/에디터 검증·일반/다른 버전/무보상/비정규 버전 거절과 실패 출력 초기화, 기존 서버 배정 선택의 버전/빈 배열·복사본 보존·다음 무효 선택/잘못된 맵/지원하지 않는 난이도 거절을 확인했다. 기존 3개는 일반 재화/아이템 payload·무효 항목 거절과 실제 네 난이도 보상/BP 설정 보존이다. 기동의 기존 Condition failed Error 15줄은 탐색 이전이고 이후 Error는 0이다. 담당 2개/읽기 고정 15개, 사본 480개/원래 baseline 입력, 검증 DLL/원본 공유 DLL은 전후 일치했다. 다른 담당의 에디터·입장/소비 검사·backend·DB·에셋과 프로세스를 유지했다.

이번 결과는 UE 작성/서버 선택 경계의 구현·집중 검증이다. 실제 훈련 목표 성공 → GameMode 완료 → UE HTTP 정산 → 경험치 영속 완료를 실행한 결과가 아니다. 훈련 보상/로비/서버 에셋·맵/목표·카탈로그 등록과 정상 공유 모듈 적용은 후속이다. 기존 개발 fixture가 `FirstSlice.v1`/빈 정산으로 경험치를 받은 과거 결과를 새 GameMode 완료 경로의 검증으로 바꿔 기록하지 않는다.

## 훈련 경험치 전용 보상 에셋 저장 (2026-10-29)

[DA_DungeonReward_ProgressionTraining](../Content/Blueprints/GameData/DungeonReward/DA_DungeonReward_ProgressionTraining.uasset)을 새로 작성·저장했다. 기존 `URPGDungeonRewardDefinition`이며 `RewardVersion=FirstSlice.v1`, 재화/아이템 배열은 모두 빈 값이다. 경험치 100·수령 자격·중복 방지는 기존 backend 진행/정산 계약의 책임으로 유지한다. 에셋에 가짜 재화나 별도 경험치 수치를 저장하지 않는다. 이 에셋은 아직 GameMode/서버 정의·로비 목록에 연결하지 않았다.

작성 중 성공한 보상 검증이 부모의 `NotValidated`를 그대로 반환하던 오류를 확인했다. `IsDataValid`가 자체 payload 검사를 성공하면 `Valid`를 명시하고 부모/자체 `Invalid`는 유지하도록 보완했다. 기존 native 검사도 일반 보상과 XP 전용 보상의 정확한 `Valid` 반환을 요구하도록 강화했다. 보상 수량·버전 허용 범위·API/스키마는 바꾸지 않았다.

[CreateProgressionTrainingRewardAsset.py](../Scripts/Editor/CreateProgressionTrainingRewardAsset.py)는 기본 실행에서 읽기만 한다. `-RPGCreateProgressionTrainingReward`일 때 누락된 대상 한 개만 생성하며, 기존 대상은 클래스·세 필드·검증/dirty 상태를 확인하고 저장하지 않는다. 사용자 지정/잘못된 클래스·registry/디스크 불일치·dirty 대상은 보존을 위해 중단한다. 실제 에셋 객체가 네이티브 검증을 통과한 뒤에만 디스크에 저장하므로 옛 모듈로 빈 보상을 저장하지 않는다. `-RPGProgressionTrainingRewardReload`는 새 프로세스의 읽기 전용 확인이다.

`Saved/BuildChecks/ProgressionTrainingReward-20261029/ImplementationResult.json`이 최종 근거다. 이전 ProgressionOnlyReward의 고정 입력 480개를 읽는 독립 CL 두 개/별도 DLL 링크가 성공했고, native **5개 Success·종료 0**으로 강화한 반환/기존 보상·서버 선택 회귀를 확인했다. 기동 Error 15줄은 탐색 이전이며 이후 Error는 0이다. 공유 UBT/UHT/Intermediate·DLL은 갱신하지 않았다.

`Applied.json`은 저장 **1개**, 같은 commandlet에서 재실행한 `Repeat.json`은 저장 **0개**다. 새 프로세스 `Reload.json`도 저장 0/동일 파일 해시·정확한 버전/빈 배열·Valid·오류/경고 없음·dirty=false다. 작성/재로드 프로세스는 종료 0·각 로그 Error 0이며 대상은 착수 전에 없었다. 초기 일반 transient probe는 EditDefaultsOnly 속성 편집에 막혀 종료 1/파일 생성 0이었다. 이 실패를 `Apply-TransientProbe*`에 남기고 실제 메모리 에셋의 저장 전 검증으로 고쳤다. 성공 근거로 합산하지 않는다.

담당 소스 3개/읽기 보존 18개, 고정 입력 480개, 검증 DLL과 원본 공유 DLL은 전후 일치한다. 보존 범위에는 기존 네 보상·레이드/서버 정의·다른 담당자의 로비 정의 5개·두 맵/보스 GameMode·Config/backend 기본 설정이 포함된다. 다른 담당자의 훈련 목표 C++·공유 GUI/MCP·검사 프로세스는 건드리지 않았다.

이는 보상 데이터 준비·검증 결과다. 다른 담당의 훈련 목표 컴포넌트와 실제 맵/서버 완료·이 보상 참조의 연결, 로비/backend 카탈로그 공개와 실제 경험치 정산은 후속이다. 현재 공유 모듈에는 XP 전용 경계/Valid 반환이 반영되지 않았으므로 적용 전에는 이 에셋을 기존 모듈의 검증·실제 훈련 완료 근거로 사용하지 않는다.

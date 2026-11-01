# 보상 우편 조회·수령

던전 보상으로 생성된 캐릭터 소유 `Mail` 아이템을 조회하고,
서버 검증을 거쳐 인벤토리로 이동한다. 별도의 아이템을 생성하지 않는다.

새 보상 정의는 인벤토리에 먼저 지급하며 들어가지 못한 수량만 우편으로 보관한다.
이전 우편 전용 보상도 계속 조회·수령할 수 있다. 지급 정책과 검증 범위는
[인벤토리 우선 지급](DUNGEON_REWARD_INVENTORY_FIRST.md)을 참고한다.

## 사용

플레이어 HUD 오른쪽 상단의 **우편** 버튼을 누른다. 우편은 가방/장비와 동시에
열 수 있는 독립 창이며, X는 우편창만 닫는다. 이전 창 내부 탭은 더 이상 표시하지 않는다.
목록의 **Claim** 버튼은 현재 가방의 첫 번째 빈 슬롯으로 첨부 아이템
전체를 수령한다. 화면은 `WBP_RewardMail`, 첨부 행은 `WBP_RewardMailEntry`이며 기존
native 부모/공유 ViewModel이 수령을 처리한다. 상단 진입 버튼은 기존 PlayerHUD WBP에
작성되어 있다. `ARPGPlayerController::ShowRewardMail()`로 직접 열거나 앞으로 올릴 수도 있다.
수령 요청을 기다리는 동안 해당 버튼은 비활성화된다. 목록이 새로 그려져도
요청 상태를 유지하며, 서버가 거절하면 임시 슬롯 점유를 풀고 다시 시도할 수 있다.
성공 응답이나 우편 항목 삭제가 인벤토리 항목보다 먼저 도착해도, 두 투영이
수령 완료를 보여줄 때까지 슬롯 점유를 유지한다.

현재 가방 크기는 `RPGItemInventoryPolicy::SlotCapacity = 80`이다.
가방이 가득 차면 수령하지 않고 우편을 그대로 보존한다. 잠긴/만료된
아이템은 수령할 수 없다. 성공/충돌/거절 결과는 화면 하단에 표시된다.

## 우편 WBP 연결 준비 (2026-10-28, 구현·개별 컴파일)

기능을 새 Blueprint 그래프로 복제하지 않고, 기존 native 부모와 공유 ViewModel 위에
편집 가능한 화면을 올리도록 준비했다. 에셋은 `/Game/Blueprints/UI/Inventory/Projection/`의
`WBP_RewardMail`과 `WBP_RewardMailEntry`이며 아래 MCP 제작 단계에서 컴파일·저장을 완료했다.

| 에셋 / native 부모 | 위젯 이름 | 타입 / 역할 |
| --- | --- | --- |
| WBP_RewardMail / RPGRewardMailWidget | LoadStateText | TextBlock, 필수 조회 상태 |
| 같은 화면 | CommandResultText | TextBlock, 필수 명령 결과·복구 안내 |
| 같은 화면 | MailRows | VerticalBox, 필수 첨부 행 목록 |
| 같은 화면 | TitleText / CloseWindowButton | TextBlock / Button, 기존 제목줄 드래그·X 닫기 |
| WBP_RewardMailEntry / RPGRewardMailEntryWidget | MailItemName | TextBlock, 필수 이름·수량 |
| 같은 행 | MailClaimFeedback | TextBlock, 필수 수령 불가 이유 |
| 같은 행 | ClaimButton / ClaimButtonText | Button / TextBlock, 필수 수령 버튼·상태별 문구 |

화면의 `MailEntryWidgetClass`에는 작성한 행 WBP 클래스를 지정한다. 수령 버튼 문구는
버튼의 직접 자식 캐스팅 대신 `ClaimButtonText` 바인딩으로 갱신하므로 아이콘/SizeBox 등을
넣어도 기존 대기·포화/복구 이유와 Available 검사·요청 경로를 유지한다.
컨트롤러의 `RewardMailWidgetClass`는 화면 WBP를 선택하며, 미작성/누락 에셋은 기존
native 화면으로 대체한다. 두 레이아웃 모두 동일 수령·공유 모델 로직을 사용한다.

UHT 생성물의 새 바인딩/클래스 속성을 확인했고, 변경 CPP 2개의 `-FileList -NoLink`
개별 컴파일은 exit 0이다. 소스 4개와 공유 DLL은 검사 중 불변이었고 Live Coding 임시
출력이다. 로그: `Saved/BuildChecks/RewardMailWBP-20261028/Compile.log`.
준비 당시 MCP 8000 응답·PIE=false·열린 에셋 없음은 확인했지만 미저장 전체 패키지는
확인하지 못해 에디터를 유지했다. 이후 사용자의 종료 알림과 프로세스 종료를 확인하고
`Project_RPGEditor Win64 Development` 정상 빌드를 실행했다. 엔진 Build.version 변경으로
527개 작업이 수행됐고 UHT·게임/에디터 DLL 링크·전체 타깃 **Succeeded / exit 0**이다.
`Source/Project_RPG` 입력 975개는 검사 중 변경/추가가 없었고 빌드 후 게임 모듈 SHA256은
`AA6C9CB08578832686CEB5CBC474A43CF7DB76F0121A335E78EF17AD00127810`이다.
로그: `Saved/BuildChecks/RewardMailWBP-20261028/Normal-Build.log`. API 폐기 예정 경고는
남아 있으며 이번 빌드를 경고 0 또는 Shipping/클린 빌드로 표시하지 않는다.

### 실제 WBP 제작과 디자이너 확인 (2026-10-28)

새 에디터 PID 1396의 모듈 경로/위 SHA와 화면·행 클래스 선택 속성의 로드를 확인한 뒤,
새 MCP 8000 세션에서 두 WBP를 작성했다. PIE=false에서 작업했고 기존 에셋은 덮어쓰지 않았다.
화면은 720×540, 제목줄/닫기·안내·상태·높이 Fill 스크롤 목록·명령 결과를 갖는 17개 위젯이다.
행은 이름/수량·상태 안내·수령 버튼의 9개 위젯이며, `ClaimButtonText`를 장식 Border 아래에
배치해 버튼의 직접 자식에 의존하지 않는 바인딩을 사용한다. 화면 CDO의 `MailEntryWidgetClass`가
행 WBP를 가리키고, 필수 바인딩 8개의 실제 위젯 참조/변수 설정·720×540·Fill·제목을 읽어 확인했다.

두 에셋 모두 MCP 컴파일 true·명시적 저장 true·최종 dirty=false다. 디자이너의 Desired 표시와
화면/행 이미지를 확인했으며, 스크린샷의 `보상 아이템 ×1`은 편집용 문구이지 실제 우편 데이터가 아니다.
기존 가방/장비/HUD 3개 에셋과 DLL SHA는 전후 동일하다. C++·RPC/API/DB·소유권/CAS·예약/복구
정책 변경이나 추가 빌드·native/HTTP E2E 반복은 없다. 초기 도구 경로/상속 자리표시자 처리 실패는
실제 참조 재조회로 정리했고 엔진/플러그인을 수정하지 않았다.

증거: `Saved/BuildChecks/RewardMailWBP-20261028/Authoring.proof.json`, 같은 폴더의
`Designer-Window-Fit.png`와 `Designer-Entry.png`. 에셋 편집기만 열어두고 자체 observer 2개는 해제했다.
**게임에서 새 우편창을 열고 실제 첨부 수령·복구 상태를 확인한 결과는 아직 아니다.** 기존 native/HTTP
성공을 새 WBP 실행 증거로 계산하지 않는다. 다음은 새 화면의 최소 실제 표시/입력 확인과 가방·장비·
임시 HUD 버튼의 외형 정리이며, 고급 우편 기능이나 완료한 서버 검사를 새로 확대하지 않는다.

### 실제 수령 표시와 하단 바인딩 보완·UI 마무리 (2026-10-29)

2026-10-28 후속 실행에서 실제 화면 하단에 `텍스트 블록`이 남는 문제를 발견했다. 앞선
필수 바인딩 확인 기록과 달리 실제 에셋에는 `CommandResultText` 대신 `TextBlock_4`가
있었다. 원본 WBP를 백업하고 해당 위젯의 이름·변수 바인딩을 바로잡았으며, 빈 초기 문구·
Roboto Regular 11·줄바꿈을 적용했다. 위젯 17개/기존 native 부모·행 클래스·수령 로직은
유지했다. 컴파일/저장 true·dirty=false 뒤 새 인증 클라이언트에서 수령 버튼 1회로 같은
첨부 ID/수량 2의 Mail→가방 2번 칸·revision 1→2, 목록 비움·pending 0과 실제 하단의
`Claim reward: Confirmed by the server.`를 확인했다. HTTP/Iris·개발 인증·Memory 범위이며
PostgreSQL/Steam/Shipping이나 응답 유실 복구의 새 검증으로 확대하지 않는다.

2026-10-29 재개에서는 수정 후 WBP/DLL 일치를 먼저 확인해 위 수령을 반복하지 않았다.
기존 `testmap`의 UI-only/Uninitialized 세션으로 제목줄 좌클릭 이동·X 닫기와 다른 창
유지·최종 입력 복구를 확인했다. 자세한 조작/제한은
[UI 마무리 계약](ITEM_INVENTORY_PROJECTION.md#새-wbp-표시입력-마무리-2026-10-29-사용자-지정-23번-완료)을 따른다.
원래 Entry 맵/dirty 0으로 복귀하고 자체 PIE/관찰자를 해제했으며 에디터는 유지했다.
사용자가 제외한 HUD 외형·고급 우편 기능을 추가하지 않고 지정된 UI 작업을 종료했다.

원본 백업: `Saved/Tests/ItemUIFinal-20261028/Backups/WBP_RewardMail.uasset`.
수정 전 SHA `705EE750…7F4A8`, 수정 후 `563186D9…8BC22`; 실행 근거는 같은 작업 루트의
`Final/Client-Claimed.json`, `Final/Backend-Claimed.json`, `Final/UI-Claimed.png`, 최종 정리는
`Finish-20261029/Verification.proof.json`이다. 이전 중단/포커스 오류와 전투 실패 모달의
실패 구간은 보존하며 최종 통과 구간과 구분한다. 새 C++ 빌드·API/DB/권한 변경은 없다.

## 경계와 계약

- `URPGInventoryProjectionComponent::MailProjection`은 `COND_OwnerOnly`
  FastArray다. `GetMailItems()`/`FindMailItem()`으로 조회한다. 사용 가능한
  인벤토리/장비 Projection에는 수령 전 우편을 포함하지 않는다.
- 같은 첨부 슬롯 번호라도 `DungeonReward.<sessionId>` 컨테이너가 다르면
  별개의 우편이다. 동일 컨테이너·슬롯 충돌은 캐시 갱신 전에 거절한다.
- ViewModel의 `MailItems`, `RequestClaimMail`,
  `RequestClaimMailToFirstFreeSlot`이 UI 경계다. UI는 DB나 HTTP API에
  직접 쓰지 않는다.
- `ServerClaimMailItem(RequestId, ItemId, ExpectedRevision, InventorySlotIndex)`
  RPC에서 인증된 PlayerState로 소유자와 목적 가방 ID를 결정한다.
  기존 보안 제한 게이트도 그대로 거친다.
- `Item.ClaimMail` 계획은 소유자, Active 상태, 잠금, 만료, 리비전,
  Mail 출발 위치, 정의 버전/스택 크기, 슬롯 범위/점유를 검증한다.
  재료처럼 장착/소비 정책이 없는 아이템도 수령할 수 있다.
- 기존 비동기 Item V2 Commit의 CAS와 DB 위치 유일성 제약을 사용한다.
  성공 응답 후 서버 캐시 및 인벤토리·우편 Projection을 갱신한다.
  GUID, 수량, 생성 시드, 스탯, 태그, 귀속, 내구도, 만료, 생성 출처를
  유지하고 리비전만 한 번 증가한다.
- 동일 서버 명령 캐시에서 같은 RequestId/인자는 `AlreadyApplied`,
  같은 ID의 다른 인자는 충돌이다. 캐시가 없더라도 리비전과 Mail 출발
  위치 검증 때문에 이미 수령한 아이템을 다시 지급하지 않는다.

현재는 아이템 레코드별 첨부 목록이다. 우편 제목/발신자/본문, 일괄 수령,
자동 스택 병합/분할, 가방 확장, 대량 우편 페이지네이션은 후속 범위다.
기존 캐릭터 전체 아이템 로드 한도(500개)는 그대로다.

## 수령 응답 유실의 원래 요청 복구 (2026-10-26, 실제 1인 응답 유실 연결 통과)

기존 가방 명령의 서버 저널/수동 복구 경로에 `Item.ClaimMail`을 추가했다.
이전의 우편 수령 성공·재입장 검증과 이번 미확정 결과 복구 구현은 별개다.

- 서버가 계획한 원래 RequestId/fingerprint·전체 첨부 ID/수량·정확한 int64 revision·
  목적 칸을 HTTP 전 선저장한다. 지원 동작 판정은 저널의 같은 목록을 사용한다.
  수령은 기존 아이템 하나의 전체 스택을 자기 가방으로 옮기는 단일 mutation만 허용하며,
  새 ID 생성/부분 수령·다른 소유자·다른 컨테이너를 저널 복구로 허가하지 않는다.
- 응답 유실/불일치는 확정 실패가 아닌 Uncertain으로 보존한다. 같은 배정/세션/캐릭터/
  계정 범위의 파일만 복원하고 자동 재전송하지 않는다. 기존 owner-only 복구 상태와
  `RequestRetryInventoryCommand()`/수동 복구 버튼·현재 인증·3초 간격을 재사용한다.
  다른 슬롯/새 ID로 재계획하거나 실패한 재시도의 401/충돌·잘못된 영수증으로 해소하지 않는다.
- 공유 ViewModel도 Uncertain에서 원래 첨부/가방 칸/요청 상관관계를 유지한다.
  원래 복구가 남아 있으면 바뀐 재전송의 거절만으로 예약을 풀지 않는다.
  성공 ACK 하나가 아니라 Mail 제거와 Inventory 추가 양쪽을 확인한 뒤 예약/상관관계를
  정리하며, 처음부터 확인된 확정 거절은 해당 예약만 해제하는 기존 동작을 유지한다.
- 서버 저널의 권한/손상 차단/회수 quarantine·전체 영수증 대조·CAS는 그대로다.
  파일 schema나 백엔드 API/DB 스키마·WBP를 바꾸지 않았다. 이전 바이너리의 미지원
  ClaimMail 파일 거절을 빈 저널로 취급하거나 파일을 지우는 다운그레이드 우회도 하지 않는다.
  소비의 일회 효과와 장착/해제의 새 복구는 이번 범위가 아니다.

새 native 사례는 원래 바이트의 파일 복원·큰 revision·수동 재시도/영수증 불일치·
소유권/새 요청 우회 거절, 저널의 잘못된 수량/복수 mutation/새 ID/메일 목적지 거절이다.
기존 우편 예약 사례에는 Uncertain·바뀐 재전송 거절·양쪽 투영 도착 순서를 추가했고,
`Run-ItemAutomationTest.ps1 -InventoryRecoveryOnly`가 우편 사례도 선택하도록 연결했다.
러너 PS 구문·38개 고유 테스트 이름 탐색과 공백 검사는 통과했지만 native 실행은 아니다.

영향받은 CPP 5개만 `-FileList -NoLink`로 컴파일하려 했으나 UBT가 workspace 밖
사용자 설정/trace 경로 쓰기에서 접근 거부되어 컴파일 전에 실패했다. 경로/권한을
우회하거나 에디터를 종료하지 않았다. 준비 기록은
`Saved/BuildChecks/MailClaimRecovery-mvoxhgp3/Preparation.json`이다.
후속 정상 공유 Editor 263개 작업/전체 타깃 성공의 DLL `6428E480…`에서 기존
`-InventoryRecoveryOnly -SkipSDKSetup`을 한 번 실행해 **38/38 성공·러너 종료 0**을
확인했다. 새 우편 저널/원래 계획 2종과 우편 예약의 미확정·거절/투영 순서가 포함되며,
선택 소스 51개/닫힌 로그·DLL 일치를 확인했다. 기존 기동 Error 15줄은 보존하고 검사
중/이후 Error 0·전체 Fatal/Assert/Ensure 0과 구분한다. 증거:
`Saved/Tests/Item/Automation-afd0b61220534f6f90ed20f27b73621f/Item.proof.json`.
실제 HTTP/DB 응답 유실·Iris·재기동/Steam/Shipping 통과는 아니며 이전 정상 장착·사용·
수령 결과를 이 새 복구의 실통합 증거로 재사용하지 않는다.

### 우편 창의 실제 응답 유실 연결 경로 (2026-10-26, 1인 실연결 통과)

우편은 공통 화면을 상속해 기존 수동 복구 버튼이 이미 연결돼 있었다. 새 WBP/운영 RPC를
추가하지 않고 기존 통합 러너의 `-InventoryRecoveryOperation ClaimMail`에 실제 입력 경로를
연결했다. 한 명의 새 배정 멤버에게 기존 서버 API로 첨부 7개와 대조 아이템을 준비하고,
native 우편의 실제 Claim 버튼을 hit-test/합성 Slate로 클릭한다. 이미 작성된 가방 WBP와
같은 ViewModel을 쓰는지, 응답 유실 중 원래 첨부/예약·우편 창 재오픈 후 복구 버튼 유지,
원래 요청의 수동 replay/AlreadyApplied 뒤 Mail 제거와 Inventory 추가 양쪽·예약 해소를 본다.
새 ID/다른 슬롯 재계획이나 UI/Projection 주입은 하지 않는다.

프록시는 현재 실행의 서비스 자격 해시·소유자/첨부 ID·수량/리비전·첫 빈 칸의 단일
mutation에 한정한다. 전체 레코드/원래 영수증 행 SHA·가방/시작 장비 보존과 ClaimMail 1/
다른 일반 커밋 0, 별도 서버 소유 저널의 원래 바이트/세대 1→2 해소를 확인하도록 연결했다.
이 모드는 InventoryRecovery·1인 전용이며 합성 실패/만료/회수 혼용은 사전 거절한다.
기본 실행은 세션 내 복구이며, 실제 프로세스 재기동은 아래 별도 모드로 선택한다.

프록시 24개(기존 20+우편 4), PS 3개 구문 오류 0, 잘못된 모드 6종/미연결 DLL 거절,
전체 레코드 정상 1·변조 8종 거절을 확인했다. 준비 당시 새 C++ probe는 미연결이었으며
공유 빌드의 별도 전투 헤더 오류/중단 이력은 보존했다. 해당 코드/다른 프로세스는 수정하거나
종료하지 않았다. 준비: `Saved/Tests/MailClaimRecovery-4d730c36/Preparation.proof.json`.

후속 공유 정상 Editor **60개 작업/전체 타깃 성공** 뒤 새 DLL `69DAEA54…`의 우편
flag/PASS marker를 확인해 재사용했다. 자체 빌드나 기존 native 38개·가방 네 동작 반복은
없다. 아래 모드의 첫 실제 실행은 **exit 0/Passed=true**였다. 실제 로그인/새 티켓/Iris
입장→native Claim hit-test/합성 클릭 1→HTTP 200 저장 응답 유실 1/예전 Mail 7개·예약 유지→
우편 창 teardown/재오픈 후 같은 공유 ViewModel/예약·기존 복구 버튼 유지→버튼 합성 클릭
1/동일 frozen 바이트 replay 1/AlreadyApplied→같은 ID의 Inventory 0번 칸 7개/rev 2·Mail
제거와 두 투영 확인 후 예약 해소를 관측했다. 새 RPC/ID·슬롯 재계획이나 상태 주입은 없다.

ClaimMail 1건/다른 일반 아이템 커밋 0, 원래 전체 레코드/가방·시작 장비와 전체 영수증 행
SHA 불변, 저널 세대 1→2/명령 0·pending/resolved 사본/live 파일 해시를 확인했다. 종료 후
독립 DB 조회도 같은 ID/7개/rev 2·Inventory 0번 칸/잠금 없음과 같은 영수증 행 SHA·추가
작업 0이다. 선택 입력 55개/검증 DLL·backend 어셈블리·Config 3개/WBP/닫힌 로그 6개 일치,
runtime/backend Error/Fatal/Assert/Ensure 0이며 경고까지 없다는 뜻은 아니다. 자기 UE/
프록시/백엔드만 정리하고 Docker/기존 PG와 다른 작업은 유지했다. lease/인증·원래 자격을
연장하거나 재발급하지 않았다. 실제 수령 입력은 native 우편이며 기존 가방 WBP와 공유
ViewModel로 연결되고, 에셋 저장/운영 권한/API/DB 스키마 변경은 없다.

성공 결과 저장은 **2026-10-26 13:22:02 UTC**, 다른 공유 빌드의 DLL `53509761…`
갱신은 **13:22:35 UTC**였다. 기록된 `69DAEA54…` 실행의 성공은 유지하며 이후 DLL을
이번 실통합 통과로 승격하거나 완료된 검사를 반복하지 않는다. native 38개는 앞선
`6428E480…`의 직접 영향부 증거이므로 서로 다른 두 DLL의 범위를 구분한다.

확정: `Saved/Tests/MailClaimRecovery-4d730c36/Verification.proof.json`;
실행: `Saved/Tests/DungeonBossReturn/5a4b31522306473781a574d92d622e4e/result.json`.
이 실행의 완료 범위는 세션 내 1인 응답 유실 복구이며, 후속 실제 서버 재기동은 아래
별도 항목의 증거로 구분한다. 다인/지연·손실,
실제 자격 만료/회수 교차·운영 초기 부팅 원인 해결·실물 입력/Steam/Shipping도 별도다.

```powershell
.\Scripts\Network\Run-DungeonBossReturnE2ETest.ps1 -InventoryRecovery `
  -InventoryRecoveryOperation ClaimMail -ClientCount 1 -BackendUrl http://127.0.0.1:3057 -Port 18867 `
  -SkipPostgresStart -NoBackendRestore
```

### 같은 배정의 우편 명령 서버 재기동 복구 (2026-10-26, 1인 실연결 통과)

기존 `ClaimMail` 모드에 `-InventoryRecoveryRestart`를 연결했다. 원래 수령 응답 유실/
우편 창 재오픈·예약과 서버 저널을 확인한 뒤 자기 서버·클라이언트 2개만 종료하고,
같은 배정/서비스 자격/서버 UserDir·저널의 새 서버와 새 UserDir 클라이언트로 재입장했다.
실제 로그인/새 티켓/Iris 입장 뒤 새 화면은 Mail이 비어 있고 같은 ID의 가방 0번 칸
7개/rev 2를 보인다. 원래 요청의 복구 잠금은 가방·우편 공용 상태에 남으며 가방의 두
아이템 모두 새 이동이 차단된다. 이전 클라이언트의 낙관적 우편/예약 상태를 주입하거나
수령할 수 없는 가짜 첨부를 만들지 않는다.

복원 중 자동 HTTP replay 0·새 Claim 클릭 0을 확인한 뒤, 비어 있는 native 우편 창의
기존 복구 버튼을 hit-test/합성 클릭 1회로 눌렀다. 동일 frozen 본문 1회/AlreadyApplied 뒤
원래 ID/7개/rev 2·Mail 부재를 유지하고 가방 조작 잠금을 해제했다. ClaimMail 1건/다른
일반 아이템 작업 0·추가 ID/리비전 증가 0, 전체 레코드/가방·시작 장비와 원래 전체
영수증 행 SHA 불변, 저널 세대 1→2/명령 0·pending/resolved 사본/live 해시를 확인했다.
정리 후 독립 DB 조회에서도 같은 아이템/위치/잠금·수량/리비전·영수증 SHA/작업 수가 같다.

자체 정상 Editor **4개 작업/compile·게임 DLL 링크·전체 타깃 성공/exit 0** 뒤 새 DLL
`5766648B…`에서 첫 실제 실행을 **exit 0/Passed=true**로 마쳤다. 입력 55개(중복 없이)/
DLL·backend 어셈블리/닫힌 로그 8개·Config 3개/WBP 일치와 runtime/backend 오류 0이다.
PS 3개 구문 오류 0·잘못된 혼용 5종·새 marker가 없는 DLL 거절 2종(대소문자 정규화 포함)을
확인했고, 변경 없는 기존 native 38개·프록시 24개/가방 네 동작·세션 내 우편 검사는 반복하지 않았다.
연결 도구/관측 코드만 바꿨고 운영 저널/RPC/권한·WBP/API/스키마·lease/인증은 바꾸지 않았다.
자기 프로세스만 정리하고 기존 Docker/PG와 다른 작업은 유지했다.

확정: `Saved/Tests/MailClaimRestart-0fcc666a/Verification.proof.json`;
실행: `Saved/Tests/DungeonBossReturn/36128b8dc5ac4084a63d9c4f4887e9c8/result.json`.
동일 호스트/배정·1인 개발 인증·합성 입력이다. 자동 재접속 UX·다인/지연·손실, 실제
만료/회수 교차·운영 초기 부팅 원인 해결·소비/장착 효과 복구·호스트 이관/fencing·
실물 입력/Steam/Shipping 완료로 확대하지 않는다.

```powershell
.\Scripts\Network\Run-DungeonBossReturnE2ETest.ps1 -InventoryRecovery -InventoryRecoveryRestart `
  -InventoryRecoveryOperation ClaimMail -ClientCount 1 -BackendUrl http://127.0.0.1:3059 -Port 18869 `
  -SkipPostgresStart -NoBackendRestore
```

## 우편 아이템의 이름·상세 표시 (2026-10-24, 구현·Editor 빌드 대기)

우편 첨부 행도 가방/장비와 같은 ViewModel presentation을 사용한다. 기존 owner-only
`MailItems`를 표시 조회 범위에 추가했으며 인벤토리/장비 배열에 우편을 섞거나 수령하지는
않는다. ID/버전이 일치하는 정의의 이름·설명과 기존 공통 tooltip의 수량/귀속·잠금·내구도·
UTC 만료·옵션을 읽기 전용으로 표시한다. 정의가 없으면 전체 PrimaryAssetName을 유지하고
실제 첨부 수량을 별도 xN으로 표시한다. 에셋 로드나 새 아이템/서버 RPC는 발생하지 않는다.

수령할 수 없을 때는 이름을 오류 문구로 덮어쓰지 않고 `MailClaimFeedback` 행에 기존 안내를
표시한다. `GetFeedbackText()`는 해당 안내만 반환하며 기존 포화 UI probe의 문구 계약을
유지한다. 재초기화/성공 제출 시 이전 안내를 지우고 이름/상세/버튼 상태를 다시 갱신한다.
`ClaimButton` 이름과 기존 요청·예약/소유권·만료/잠금 검증은 유지했다. 우편 아이콘·일괄
수령·정렬·별도 WBP/에셋 편집은 이번 범위가 아니다.

기존 definition presentation 테스트에 우편 이름/설명·수량과 읽기 후 우편 잔존 검사를
추가했다. 소스/공백 검토를 수행했으며 아직 자동화/실제 화면을 실행한 것은 아니다.
2026-10-25 사용자가 요청한 정상 `Project_RPGEditor Win64 Development` 빌드를 시도했다.
에디터/Live Coding 종료는 확인했으나 실행 환경이 사용자 AppData의 UBT 설정·Trace.uba
쓰기를 거절해 최초 명령은 컴파일 전에 종료 6으로 실패했다. 프로젝트의 기존 XML 캐시와
로컬 로그/UBA 디렉터리·`-Session`으로 재시도했지만 이 엔진은 재귀 세션에서 UBA executor를
시작할 수 없어 다시 종료 6이다. 재시도 로그는
`Saved/BuildLogs/InventoryUI-20261025-d63f35c2/Editor-Build-Local.log`다. 새 UHT/C++ 컴파일·
DLL 링크/타깃 성공은 확인하지 못했으며 사용자 설정/엔진 소스/게임 코드는 변경하지 않았다.
이후 사용자가 안내한 CMD 명령의 빌드 완료를 보고했고 게임 DLL의 2026-10-25 00:45:57
갱신을 확인했다. 기본 UBT 로그는 AppData 읽기 제한으로 직접 확인하지 못했으므로 이 보고를
자체 컴파일/링크·타깃 성공 증거로 승격하지 않는다. 다음은 새 에디터에서 우편 이름·수량/
상세와 이름을 보존하는 수령 오류 행 확인이다. 실제 UI/자동화는 계속 미검증이다.

## 수령 버튼의 가용 상태 안내 (2026-10-25, 구현·빌드/실행 미검증)

수령 전부터 버튼의 문구/활성 여부와 별도 사유 행을 같은 ViewModel presentation으로
표시한다. BlueprintPure `GetMailClaimPresentation(ItemId)`는 `FRPGMailClaimPresentation`
(State/ActionLabel/Reason)을 반환하며 native 행도 이를 사용한다.

- Available만 일반 수령 시도를 허용한다. Pending은 기존 우편 수령 예약이며 성공 ACK나
  우편 삭제만으로 가방 도착 전에 예약을 해제하지 않는다. 새 타임아웃 해제는 없다.
- Locked, InventoryFull, InventoryReserved는 각각 owner 잠금·실제 포화·남은 빈 칸 전체
  예약으로 구분한다. 마지막 경우를 가방 포화로 단정하지 않는다. 이전처럼 실패할 때까지
  클릭해야 이유를 알게 하지 않고, 비활성 버튼 밖의 사유 행은 계속 표시한다.
- 미확정 가방 복구/복구 저장 차단을 각각 RecoveryRequired/RecoveryStorageBlocked로
  구분한다. 신원 회수·조회 미완료·잘못되었거나 사라진 아이템·명령 컴포넌트 부재는
  Unavailable이다. 기존 InventoryItems/LoadState/복구/예약 알림의 우편 갱신을 재사용한다.
- 행의 클릭 핸들러도 최신 presentation을 다시 확인해 비활성 버튼의 합성 디스패치로 새
  요청을 보내지 않는다. 이 가드는 UI 사전 안내이며 Available이 서버 권한/최종 수령 성공을
  보장하지 않는다. 기존 RequestClaimMail/서버 소유권·위치·Revision/CAS 검사와 예약 정책은
  수정하지 않았다. 구체적인 서버 거절은 기존 화면 하단 명령 결과로 표시한다.
- 기존 행의 `UtcNow()` 만료 비교를 제거했다. 만료 시각은 공통 tooltip의 UTC 값으로
  표시하고 실제 수령 가능 만료 판정은 기존 서버 정책을 따른다. 클라이언트 시계 오차로
  정상 보상을 영구 비활성화하거나 서버의 만료 검사를 우회하지 않는다.

기존 포화 UI probe의 활성 버튼 가정을 비활성/Full 안내와 직접 디스패치 무요청 확인으로
맞췄다. 기존 interaction 테스트 파일에 8개 상태/우선순위·예약 해제 뒤 재가용·읽기 전용
경계를 보는 집중 사례를 추가했으나 실행하지 않았다. 사용자 우선순위대로 구현·직접 영향부
소스/공백 검토만 수행했다. 2026-10-25 앞서 보고된 사용자 빌드에는 이번 변경이 포함되지
않으며 새 enum/struct/조회의 UHT·C++ 빌드와 native/실제 UI 실행은 미검증이다. 우편은
native 화면이므로 WBP/MCP 에셋 편집이나 백엔드/복제/저장 스키마 변경은 하지 않았다.

## 검증

```powershell
# 아래 Claim 실행은 이전 우편 전용 페이로드를 사용하는 호환/UI 수령 테스트다.
# 실제 포화/부분 스택은 아래 InventoryOverflow 모드로 별도 검증한다.
& .\Scripts\Network\Run-DungeonRewardPersistenceE2ETest.ps1 -ClaimMail

# 파티원별 지급·수령·재시작 복구
& .\Scripts\Network\Run-DungeonRewardPersistenceE2ETest.ps1 -ClientCount 2 -ClaimMail
& .\Scripts\Network\Run-DungeonRewardPersistenceE2ETest.ps1 -ClientCount 4 -ClaimMail -ConcurrentClaims
& .\Scripts\Network\Run-DungeonRewardPersistenceE2ETest.ps1 -ClientCount 2 -InventoryOverflow
& .\Scripts\Network\Run-DungeonRewardPersistenceE2ETest.ps1 -ClientCount 2 -InventoryOverflow -PartialStackOverflow
```

`ProjectRPG.Item.Mail` 자동화 테스트는 수령 시 데이터 보존, 다른 소유자,
잠금/만료/종료 상태, 리비전/슬롯/정의 검증, 복수 우편의 슬롯 재사용,
캐시의 원자적 거절, 중복 요청, 투영 갱신 순서와 거절 후 슬롯 해제를 검사한다.

통합 테스트는 실제 dedicated+Iris와 PostgreSQL에서 보상 지급→재시작→
네이티브 우편 화면 생성/바인딩→실제 Claim 버튼 클릭·재그리기 시
중복 입력 방지→수령/중복 재요청/잘못된 요청 거절→
전체 프로세스 재시작→수령된 동일 GUID의 인벤토리 복구를 확인한다.
로컬 개발 인증과 `-nosteam`을 사용한다. 시각적 배치, 실제 Steam 인증과
배포 패키지 검증은 별도로 필요하다.

파티 실행에서는 각 계정의 지갑·아이템 수량과 GUID를 독립적으로 검사한다.
각 클라이언트는 자기 첨부 두 개만 조회해야 하며, 다른 파티원의 지갑·아이템을
HTTP로 조회하면 403으로 거절돼야 한다.

2026-10-02 확인: Editor 전체 빌드 성공, `ProjectRPG.Item` 자동화 테스트
22개 성공, `-ClaimMail` PostgreSQL/전용 서버/Iris 통합 테스트 성공.

2026-10-04 확인: 네이티브 `ClaimButton.OnClicked` 이벤트로 두 첨부를
수령했고, 화면 재그리기 중 중복 입력 차단과 백엔드·서버 재시작 복구를
다시 검증했다.

2026-10-04 추가 확인: 같은 프레임의 두 우편 수령 E2E와 우편 삭제·인벤토리
추가 사이의 투영 간격, 명시적 거절 후 임시 슬롯 해제 자동화 테스트를 통과했다.

2026-10-04 파티 검증: 2인 순차 수령·중복 요청 검증과 4인 동시 수령 E2E가
통과했다. 전원의 동일 GUID·수량·revision 2 인벤토리 복구와 계정 간 조회 거절을 확인했다.

2026-10-05 추가 확인: 2인 이전 우편 전용 호환 fixture에서 정산 직후 열린 우편 창의
목록·지급 안내·골드가 재접속 없이 갱신됐고, 동시 Claim과 수령 후 재시작 복구도 통과했다.
결과: `Saved/Logs/DungeonRewardE2E/20261005-133834-3f0609964e86/result.json`.
이 Claim fixture는 가방 포화로 발생한 초과 우편 테스트가 아니다. 실제 80슬롯 포화와
부분 스택의 2인 화면 갱신·수령 차단·물약 소비 후 수령·중복 재시도·재시작 복구는
`-InventoryOverflow`로 별도 통과했다. [포화 가방 E2E](DUNGEON_REWARD_OVERFLOW_E2E.md)를 참고한다.
새 조회 API와 캐시 적용 경계는
[재접속 없는 보상 갱신](DUNGEON_REWARD_LIVE_REFRESH.md)을 참고한다.

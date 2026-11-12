# Item Inventory Projection

## 장비·소비·기타 각 80칸과 탭 정렬 — 2026-11-08

현재 가방은 장비/소비/기타 세 탭에 **각각 기본 80칸**, 합계 240칸을 제공한다.
기존 화면 필터가 아니라 실제 저장 위치와 용량을 분리한다. `Inventory` 컨테이너의
전역 슬롯 0..79/80..159/160..239가 각 탭의 로컬 1..80칸에 대응한다. 기존
소유자/컨테이너/슬롯 유일성, 아이템 GUID, 퀵슬롯의 ID 참조를 재사용한다.

- 서버 정의의 정규 이름 `GameItem.Equipment.*`는 장비, `GameItem.Consume.*`는 소비,
  나머지는 기타다. UE `RPGItemInventoryPolicy`와 backend `InventoryBagLayout`에 같은
  규칙을 둔다. 표시 이름/클라이언트가 보낸 태그로 분류하지 않는다. 신규 정의 작성 시
  이 이름 공간과 `ItemCategory`를 일치시킨다.
- 픽업/우편 수령/장비 해제/보상/거래 수령은 해당 탭만 찾는다. 탭이 가득 차면 다른 탭을
  빌리지 않는다. 보상은 해당 탭에서 호환 스택을 먼저 채운 후 초과분만 기존 우편으로
  보낸다. 일반 이동/교환/분할의 다른 탭 목적지는 UE 계획과 backend의 새 저장에서 거절한다.
  과거 배치의 커밋 재요청은 먼저 원래 영수증을 대조하므로 전환 후에도 과거 영수증 조회를
  새 배치 규칙으로 차단하지 않으며, 옛 응답이 현재 저장 위치를 되돌리지 않는다.
- native 가방은 장비/소비/기타 탭과 정렬 버튼을 기존 WBP 부모에 추가하고, 현재 탭의
  80개 셀/점유·예약·빈칸만 표시한다. 기존 별도 장비/우편 창 및 공용 ViewModel은 유지한다.
- 정렬은 현재 탭에서만 명시적으로 실행한다. 기존 슬롯 순서를 먼저 확정한 뒤 정의 ID
  (이름 공간의 세부 유형/아이템), 정의 버전, 강화 단계 내림차순, 수량 내림차순으로
  `Algo::StableSort`한다. 비교 키가 같으면 기존 순서를 유지하며 잠긴 아이템의 칸은 건너뛴다.
  스택 합치기/분할/새 아이템 생성/옵션 변경은 하지 않는다. 이미 같은 배치이면 커밋 없이
  `NoChange`로 안내한다.
- 서버 소유 `Item.Sort` 전체 계획을 하나의 CAS/영수증에 저장한다. 빈 칸이 없는 80개
  순환 배치도 원자적이다. 일반 Commit은 계속 최대 16개이며 Sort만 최대 80개를 허용한다.
  backend는 기존 레코드의 소유자/수량/옵션/귀속/메타데이터를 대조해 위치 외 변경을 거절한다.
- 기존 명령/픽업이 진행 중이면 새 정렬을 받지 않는다. 정렬 동안 서버와 UI에서 새 변경을
  막고, 성공 ACK와 전체 대상의 복제 리비전이 모두 도착한 뒤 UI 예약을 해제한다. 미확정
  정렬은 기존 디스크 journal/수동 복구로 같은 전체 바이트를 재전송한다. 새 계획으로
  재정렬하거나 응답 유실을 실패로 단정해 잠금을 해제하지 않는다.

배포에는 backend migration **v11 `0011_inventory_tabs.sql`**과 같은 버전의 UE 서버/클라이언트가
필요하다. 게임 서버를 Drain하고 기존 미확정 명령을 먼저 해소한 뒤 적용한다. migration은
구형 배치가 있는 캐릭터 가방만 유형별로 옮기고 ID/수량/옵션/옛 영수증을 보존한다. 실제
이동한 레코드의 리비전만 증가한다. 같은 초기화 재실행은 이미 옮긴 가방을 변경하지 않는다.
80개 초과/리비전 한계/살아 있는 거래 예약은 전체 롤백하고 원인 수정을 요구한다.
실행 중인 기존 서비스·사용자 DB·정상 UE DLL은 이 소스 변경만으로 교체하지 않는다.

검증 진입점은 `Run-InventoryTabsBackendTests.ps1`와 `Run-InventoryTabsNativeTests.ps1`이다.
실제 수정/검증 입력은 `Scripts/Tests/InventoryTabsInputs.json`으로 고정하며, 게시 HEAD와 본인
overlay의 사설 출력만 사용하고 자기 임시 빌드/DB는 종료 시 폐기한다. backend Release
0경고/0오류·메모리/실제 loopback HTTP/PostgreSQL **103개 확인 통과**. HTTP는 실제 Item
endpoint/JSON이지만 principal 주입 fixture이며 실제 플랫폼 인증 증명은 아니다.
UE는 전체 Editor 컴파일 뒤 수정본9단계/게임·플러그인 링크/타깃 종료0, 동일
`9CC1D85F…D5CCFF7` 게임 DLL에서 native **7/7 Success·검사 오류/경고0·프로세스 종료0**이다.
80칸 순환 배치/안정 순서/잠긴 칸·다른 탭 보존/전체 wire·journal/응답 유실의 frozen 재시도/
세 탭 버튼·80셀/ACK 전후 UI 예약/픽업 포화·예약 분리를 확인했다. 초기 헤더 순서·검사 API
이름 컴파일 실패와 기동 진단은 별도 로그로 보존한다. 실제 저장 WBP의 렌더·물리 입력,
다인 Iris·Steam·Shipping 및 실행 중 서비스의 배포를 확인한 것은 아니다.
소유 사본/DB **6,571,303,957 bytes(약6.12GiB)**·읽기 junction7을 정리했고 임시/컨테이너/예외0,
원본 Content는 보존했다. 로컬 근거는 `Saved/BuildChecks/InventoryTabs-20261108/Completion.json`.
폐기한 DLL 해시는 당시 증거다. 아래 공용80칸/정렬 미구현 기록은 당시의 이력이다.

## 목적

권위 있는 `FRPGItemRecord` 전체를 클라이언트에 노출하지 않고, 현재 캐릭터의
인벤토리 UI에 필요한 읽기 모델만 소유자에게 델타 복제한다.

```text
Item V2 API
  -> URPGItemBackendSubsystem
     -> FRPGItemRecord[]
        -> FRPGInventoryProjectionMapper
           -> URPGInventoryProjectionComponent
              -> FRPGInventoryProjectionList (FFastArraySerializer)
                 -> owning client UI
```

`URPGInventoryProjectionComponent`는 `ARPGPlayerController`의 기본 서브오브젝트다.
Dedicated Server의 참가 티켓 검증과 캐릭터 ID 설정이 끝난 뒤 초기 조회를 시작한다.
기존 `URPGInventoryComponent`는 자산 연결용 호환 파사드만 남긴다. 별도의 아이템
목록·아이템 서브오브젝트 복제·UI 생성·웹 저장은 수행하지 않는다.

## 복제 계약

- `Projection`과 `LoadState`는 `COND_OwnerOnly`로 복제한다.
- 배열 항목은 `ItemId`를 안정 식별자로 사용한다.
- 동일 스냅샷은 델타를 만들지 않는다.
- 추가·수정 항목은 `MarkItemDirty`, 제거는 `MarkArrayDirty`로 전송한다.
- 클라이언트는 `GetProjectedItems`, `FindProjectedItem`으로만 읽는다.
- `OnProjectionChanged`, `OnLoadStateChanged`로 UI 갱신 시점을 알린다.

복제하는 필드는 다음과 같다.

- Item ID, Definition ID/Version
- 슬롯, 수량, `int64` Revision
- 귀속, 내구도, 만료 시각, 잠금 여부
- 인스턴스 태그와 롤된 스탯

Owner ID, Container ID, 생성 시드, 생명주기, 생성 출처 같은 서버 영속성 정보는
복제하지 않는다.

## 보안과 무결성

Mapper는 스냅샷을 적용하기 전에 전체 응답을 검증한다.

1. 예상 소유자는 유효한 Character여야 한다.
2. 모든 Record는 구조적으로 유효하고 예상 소유자와 일치해야 한다.
3. 다른 소유자의 Record가 하나라도 섞이면 전체 스냅샷을 거부한다.
4. Active + Inventory Record만 투영하고 Trade/Storage/Equipment/Terminal 등은 숨긴다.
5. 중복 Item ID 또는 중복 슬롯이 있으면 전체 스냅샷을 거부한다.
6. 슬롯과 Item ID 순으로 정렬해 결과를 결정적으로 유지한다.

비동기 조회 콜백은 로드 세대와 캐릭터 GUID를 다시 확인한다. 재접속이나 캐릭터
전환 뒤 도착한 오래된 응답은 적용하지 않는다. GUID는 소문자 하이픈 형식으로
정규화해 표기 차이로 인한 잘못된 소유자 불일치를 막는다.

## 상태 변경 규칙

이 Projection은 읽기 모델이다. 클라이언트 RPC나 UI 입력으로 Record를 직접
변경하지 않는다. 아이템 이동·스택·장착·소비는 서버 트랜잭션과 Item V2 Commit을
통과해야 하며, 성공 응답의 Record로 Projection을 다시 조정해야 한다.

인증 직후의 초기 Load는 `ApplyAuthoritativeRecords`로 전체 캐시를 교체한다.
이후 성공한 Item V2 Commit은 `ApplyAuthoritativeCommitResult`로 검증한 뒤 변경된
Record만 `FRPGInventoryProjectionStore`에 병합한다. 전체 캐시에서 read model을
다시 만들기 때문에 Commit에 포함되지 않은 Inventory 아이템은 유지된다. 더 낮은
Revision의 늦은 응답은 거부한다. 다음 구현에서는 비동기 명령 orchestrator가 이
API를 호출하고 Equipment/GAS read model도 같은 방식으로 조정한다.

### 소모품 영수증의 원래 계획 대조 (2026-10-27, 정상 빌드·집중 native 완료)

비동기 명령의 전체 레코드 대조를 저널 지원 동작에만 제한하던 조건을 제거했다.
Consume도 원래 계획의 mutation 수·각 ItemId·전체 필드·정확한 expected revision+1·
처리 수량이 모두 일치해야 Projection/일회 효과 sink에 도달한다. 헤더의 요청 ID/
동작/지문/Actor만 같은 변경 응답은 ProtocolRejected로 끝나며 로컬 아이템이나 효과를
반영하지 않는다. 이미 저장됐을 수 있는 backend 트랜잭션을 롤백하는 처리는 아니다.

기본 sink의 첫 Succeeded 효과 전달/AlreadyApplied 무재적용 규칙과
최종 소진의 terminal tombstone은 유지했다. 별도 scoped consume sink의 효과 복구는 이
보완의 범위가 아니다. 소비의 응답 유실/프로세스 재시작에 걸친
일회 효과 복구 프로토콜을 추가한 것은 아니며, 기존 우편/장비 복구 완료와도 구분한다.

기존 async fixture에 Succeeded/AlreadyApplied 각각의 잘못된 수량·revision·아이템 ID·
슬롯·메타데이터·처리 수량·누락/추가 레코드 거절 사례와, 부분 사용/최종 소진·큰 int64/
이미 처리된 응답의 정상 사례를 추가했다. 기존 2개와 신규 2개의 정확한 테스트 이름을
확인했다. 당시 최소 컴파일 단계와 후속 native 실행은 아래와 같이 구분한다.

새 실행 권한에서 영향 CPP 2개를 기존 UBT `-FileList -NoLink`로 컴파일해 2개 작업/
exit 0·`Result: Succeeded`를 확인했다. 에디터의 활성 Live Coding 세션 때문에 출력은
UBT 임시 위치이며 실행 DLL은 교체하지 않았다. 로그는
`Saved/BuildChecks/ConsumeReceipt-mvpgtmhd/Compile.log`다.
소스/공백·러너 PS 구문 확인은 마쳤다. 컴파일 뒤 같은 CPP/헤더에 별도 scoped consume
복구 변경이 들어왔으며 원래 검증 블록과 해당 변경을 모두 보존했다. 따라서 위 성공은
컴파일 당시 두 파일의 결과이지 현재 결합 소스 전체의 검증이 아니다.
`Preparation.json`에 당시 입력과 후속 변경을 구분했다. 정상 DLL 링크/집중 native·실제
HTTP/GAS 실행은 별도다. 기존 UI/우편/장비 검사를 단순 반복하지 않았다.

후속 MCP에서 PIE=false와 미저장 content/map 0을 확인한 뒤 GUI 에디터만 정상 종료했다.
독립 프로젝트 사본의 테스트 서버/클라이언트는 대상 DLL을 사용하지 않음을 확인해 보존했다.
자체 정상 Editor 빌드의 **4개 작업·게임 DLL 링크/전체 타깃 exit 0**과 빌드 전후 runtime
C++ 입력 956개 불변을 확인했다. 로그/입력은
`Saved/BuildChecks/ItemReceiptFinal-mvph8aby/`에 있으며 DLL은
`54B66E6D79B4598E106E2E2634016E6E59A1C061DE972811286425951093CFD3`다.

같은 모듈에서 기존 `Run-ItemAutomationTest.ps1 -InventoryRecoveryOnly -SkipSDKSetup`을
실행해 신규 영수증 2종과 직접 영향 회귀/별도 scoped consume 복구 사례를 포함한
**47/47 성공·러너 exit 0**을 확인했다. 선택 입력 61개/닫힌 로그와 DLL 해시가 일치하며
기동 Error 15줄·검사 중/이후 Error 0·전체 Fatal/Assert/Ensure 0을 구분한다.
native proof는
`Saved/Tests/Item/Automation-e7895667439645b0a1934bdf266f0ad2/Item.proof.json`이다.
여전히 제어된 native sink/로컬 파일의 범위이며 실제 HTTP 응답 유실·GAS/네트워크·서버
재기동에 걸친 소비 효과 복구·Steam/Shipping 완료로 확대하지 않는다. 에디터는 빌드 후
닫아 둔 상태이며 다른 프로세스/DB는 종료하지 않았다.

검증 완료 뒤 별도 재확인에서는 공유 DLL이 `B74E4A0D…40B607`로 달라져 있었다.
선택 소스 61개는 그대로지만 변경 원인/교체 시각은 추정하지 않는다. 파일 수정 시각만으로
이 교체의 순서를 단정하지 않으며, 앞서 해시 일치로 마친 `54B66E6D…` 실행의 성공은
그 범위로 보존한다. 후속 DLL을 이번 native 통과로 승격하거나 완료된 검사를 단순 반복/
다른 DLL 덮어쓰기는 하지 않았다. 관측은 빌드 폴더의 `SubsequentModuleObservation.json`에 있다.

### 가방 내부 빈 슬롯으로 드래그 이동 (2026-10-19, 구현·실행 미검증)

이 절의 미검증 표시는 2026-10-19 구현 당시 상태다. 2026-10-23 실제 화면/저장 연결은
아래 [이동·자리 바꾸기 실연동](#가방-이동자리-바꾸기의-실제-화면저장-연결-2026-10-23)에 기록했다.

가방의 아이템을 좌클릭 드래그해 같은 가방의 빈 칸에 놓는 경로를 연결했다.
80칸 모두 네이티브 셀로 구성하고, 드래그 중 이름/수량 표시와 유효한 목적지 강조를 제공한다.
기존 퀵슬롯 등록은 같은 `ItemId` payload를 계속 사용한다. 장비 칸으로 드롭,
아이템 맞바꾸기·스택 합치기/분할·월드 버리기는 이번 범위에 포함하지 않는다.

- 드래그 시작 시 소스 ViewModel과 Revision을 캡처한다. 다른 ViewModel, 잠긴 항목,
  변경된 Revision, 원래 위치, 점유/예약된 목적지에는 이동 요청을 보내지 않는다.
- `ServerMoveItem(RequestId, ItemId, ExpectedRevision, InventorySlotIndex)`의 소유자와
  Container ID는 서버가 현재 인증 캐릭터에서 도출한다. 클라이언트가 제출하지 않는다.
- `FRPGItemActionCommandPlanner`의 `Item.Move`는 소유권·활성·잠금·만료·Revision,
  같은 Inventory 컨테이너·슬롯 범위·빈 목적지를 검사한다. 정의의 장착/소비 정책 없이
  재료도 이동할 수 있으며, 기존 비동기 CAS Commit과 영수증/Projection 조정 경로를 재사용한다.
- UI는 전체 스택의 위치를 미리 변경하지 않는다. 요청 중인 아이템/목적지를 해당 ViewModel에서
  예약하고, 실패 응답 또는 새 authoritative Revision/제거 반영 시 해제한다.
  성공 RPC가 먼저 와도 복제 상태가 갱신될 때까지 예약을 유지한다.
- ItemId와 수량을 유지하므로 기존 퀵슬롯 참조는 유지된다. 실제 위치/Revision은
  백엔드 Commit 결과가 소유자에게 복제된 뒤에만 바뀐다.

2026-10-19에는 사용자 우선순위에 따라 구현에 집중했다. 소스 검토와 `git diff --check`만 수행했으며,
신규 RPC의 UHT/Editor 빌드·네이티브 테스트·실제 드래그/HTTP/Iris 실행 검증은 보류했다.
당시 실행 중이던 에디터/DLL에는 반영하지 않았다. 이후 빌드/실연동 결과는 아래 후속 절을 따른다.

### 가방 내부 자리 바꾸기와 WBP 안내 (2026-10-19, 구현·C++ 실행 미검증)

이 절은 2026-10-19 당시 기록이며 2026-10-23의 실제 교환/원자 저장 결과는 아래 후속 절에 있다.

아이템이 있는 가방 칸에 드롭하면 `Item.Swap`으로 두 아이템의 위치를 교환한다.
출발 Revision은 드래그 시작 시 캡처하고, 목적 아이템의 현재 Revision은 드롭 시 읽는다.
양쪽 모두 같은 인증 캐릭터의 Inventory여야 하며 소유권·활성·잠금·만료·Revision과
80칸 범위를 서버에서 검사한다. 서버가 도출한 Container ID만 사용한다.

- `ServerSwapInventoryItems` → 공통 planner/orchestrator → 기존 Commit 경로로
  두 mutation을 **한 요청**에 넣는다. 기존 PostgreSQL 트랜잭션/위치 제약과 Memory
  저장소의 후보 스냅샷을 재사용하며, 두 번의 단독 이동이나 임시 빈 슬롯은 만들지 않는다.
- ItemId·수량·메타데이터를 유지하고 위치만 교환한다. 같은 종류의 아이템도 자동 병합하지
  않으며 기존 퀵슬롯 참조를 유지한다. 빈 칸 드롭은 앞서 구현한 `Item.Move`를 사용한다.
- UI는 두 ItemId와 두 슬롯을 예약한다. 성공 RPC가 먼저 오거나 한쪽 Revision만 먼저
  복제되어도 예약을 해제하지 않고, 양쪽 스냅샷의 갱신/제거를 기다린다. 실패 응답은
  해당 요청의 예약을 해제한다. 위치는 낙관적으로 바꾸지 않는다.
- MCP `8000`으로 기존 가방/장비 WBP와 사용 중인 native 부모를 확인했다. PIE 미실행·대상
  미저장 변경 없음·편집 가능 상태에서 가방 원본을 백업한 뒤, 가방 WBP의 InventoryColumn에
  `InventoryInteractionHint`를 추가했다. 11pt 자동 줄바꿈·입력 비차단 안내이며 다른 레이아웃/
  그래프나 장비 WBP는 변경하지 않았다. 가방 WBP 하나만 컴파일/저장했고 저장 상태를 확인했다.
  백업: `Saved/Backups/InventoryUI/Swap-6d97ad3fe62e4a17be67711c8c271657/WBP_InventoryProjection.uasset`.

2026-10-19에는 C++ 소스 검토와 공백 검사만 수행했다. 신규 RPC의 UHT/Editor 빌드·네이티브/실제 HTTP·
드래그 실행 검증은 사용자 우선순위에 따라 보류했다. WBP 저장 성공은 새 C++ 기능의
실행 성공을 의미하지 않는다. 이후 이동/자리 바꾸기의 빌드·실제 드래그 결과는 아래 절을 따른다.
자동 정렬·스택 합치기/분할·월드 버리기는 이번 범위에 포함하지 않는다.

### 가방 이동·자리 바꾸기의 실제 화면·저장 연결 (2026-10-23)

`Scripts/Network/Run-DungeonBossReturnE2ETest.ps1 -InventoryMoveSwap -ClientCount 1`은
기존 개발 인증/실제 Join Ticket/Iris 입장 뒤 저장된 WBP의 평범한 좌클릭 드래그를 사용한다.
UI 요청 함수/아이템 RPC를 직접 호출하지 않고 전투/정산 gate도 열지 않는다.

- 새 캐릭터 가방에 물약 7개(0번 칸)와 투구 1개(1번 칸)를 서버 권한으로 준비한다.
  다른 정의를 사용해 합치기와 교환을 구분한다. 같은 종류의 호환 안 되는 스택을
  교환으로 우회하도록 규칙을 바꾸지 않는다. 물약의 정상 퀵슬롯 참조 2개를 준비하고
  비소모품 투구의 퀵슬롯은 등록하지 않는다.
- 첫 드래그는 물약 `0→3`: 같은 ID/수량 7, rev 1→2와 원래 칸 비움·열린 셀/Projection/
  퀵슬롯을 확인한다. 실제 HTTP 영수증 1건/단일 레코드와 DB의 Move 1/Swap 0을
  확인해야 교환 gate를 연다. 투구는 1번 칸/수량 1/rev 1 그대로다.
- 두 번째는 물약 `3→1` 드롭: `Item.Swap`의 한 요청/영수증에 두 레코드를 저장하고,
  물약 1번 칸/rev 3, 투구 3번 칸/rev 2가 된다. 양쪽 갱신 후 조작 잠금이 해제되고
  ID/수량/정의/시드/태그/스탯/귀속/내구도/만료/출처 등은 보존된다. 위치 선반영은 없다.
- 세 번째는 자기 칸 `1→1` 드롭: 거절·추가 요청/저장 없음. 최종 PostgreSQL 영수증은
  Move 1건/Swap 1건이며 합치기/분할/소비/장착·해제/우편 수령으로 전환하지 않았다.
  원래 이동 영수증과 시작 장비도 불변이며 물약의 두 퀵슬롯 참조가 유지된다.

최종 실행: `Saved/Tests/DungeonBossReturn/f4911e516b6a4e718676ca00189866b2/result.json`.
자체 Editor 빌드 77개 작업/모듈 링크/타깃과 전용 러너는 종료 0이다. 잘못된 조합 4종을
사전 거절하고 평범한 합성 드래그 3회, 선택 입력 44개(WBP/정의 에셋 포함)/DLL 전후·종료
후 일치와 닫힌 UE 로그 2개 Error/Fatal/Assert/Ensure 0을 확인했다. 자체 UE/백엔드
프로세스만 정리했고 테스트 레코드/증거와 기존 Docker DB는 보존했다.
이번 변경은 opt-in probe/러너/helper이며 운영 아이템/UI·BP/API/스키마·권한 규칙을
바꾸지 않았다. 기존 native 19개·합치기/분할 E2E는 반복하지 않았다.
proof: `Saved/Tests/InventoryMoveSwap-3d719ac4/Verification.proof.json`.

이는 1인 로컬 개발 인증·GPU offscreen 합성 입력의 성공이다. 이후 2인 소유권/동시 목적지
경합의 RPC 검사는 아래 절을 따른다. 실제 Steam/물리 입력·다인 UI 입력·지연/손실·
재시작/응답 유실 복구까지 확인한 결과는 아니다.

### 두 클라이언트의 아이템 소유권·동시 목적지 충돌 (2026-10-23)

`Scripts/Network/Run-DungeonBossReturnE2ETest.ps1 -InventoryConcurrency -ClientCount 2`는
기존 개발 인증/실제 입장 티켓/Iris/HTTP/PostgreSQL을 사용하는 독립 headless 검사다.
UI 예약을 우회한 변조 클라이언트도 거절해야 하므로, 각 소유 클라이언트의 기존
`ServerMoveItem` RPC를 직접 호출한다. Projection·인증·서버 처리 결과를 주입하지 않는다.
운영 아이템 처리 규칙/API/스키마/BP는 그대로이며, 서버에는 non-Shipping opt-in 결과 로그만 추가했다.

- 각 클라이언트가 다른 소유자의 아이템 ID를 보내면 거절된다. 전원의 거절 후, 두 가방의
  전체 원본 레코드와 동작 영수증 0건을 확인해야 경합 gate를 연다. 상대 아이템은 어느
  클라이언트의 자기 Projection에도 나타나지 않는다.
- 첫 소유자의 물약 7개(0번 칸)와 투구 1개(1번 칸)를 같은 프레임에 빈 3번 칸으로 보낸다.
  정확히 성공 1건/충돌 1건이며, 패자의 요청 ID로 서버의 **BackendRejected/LocationConflict**를
  확인한다. UI/계획 단계 거절만으로 DB 경합 통과를 선언하지 않는다. 승자만 rev 1→2,
  패자는 원래 칸/rev 1을 유지하며 어느 아이템이 이겨도 같은 조건을 적용한다.
- 두 번째 소유자의 물약 3개는 자기 가방의 3번 칸으로 정상 이동한다. 같은 칸 번호를
  소유자 전체의 공통 슬롯으로 취급하지 않는다. 두 소유자 각각 Move 영수증이 정확히 1건이다.
- 재전송 전 성공 영수증/DB 상태를 보관한 뒤, 첫 소유자가 성공한 **같은 요청 ID·아이템·
  expected revision 1·목적지 3**을 재전송한다. `AlreadyApplied` ACK, 원래 영수증 불변,
  추가 Commit/Revision 증가 없음과 거절 요청의 영수증 부재를 확인한다.
- 위치/Revision 외 모든 Record 필드·시작 장비를 유지하며, 전투/정산 gate는 열지 않는다.
  기본 로컬 DB 외 연결, 다른 fixture 혼용, 2인 외 실행은 시작 전에 거절한다.

최종 실행: `Saved/Tests/DungeonBossReturn/d77cb0992a184b03b545fb16c24e3508/result.json`.
자체 Editor 54개 작업/모듈 링크/전체 타깃과 전용 러너 종료 0, 잘못된 실행 조합 5종·
SQL에 쓰는 잘못된 GUID 4종 거절, 선택 입력 42개/실행 DLL/닫힌 UE 로그 3개 해시 일치와
Error/Fatal/Assert/Ensure 0을 확인했다. own UE 3개/백엔드만 종료하고 DB·증거는 보존했다.
기존 합치기/분할/native 회귀는 반복하지 않았다.
proof: `Saved/Tests/InventoryConcurrency-dcf32e63/Verification.proof.json`.

이는 로컬 개발 인증의 RPC 보안 경계 검사다. 서버의 기존 비검증 플랫폼 identity 개발
fixture를 사용하지만 입장 티켓/캐릭터 소유권 검사를 우회하지 않는다. 실제 Steam 인증,
WBP/물리 입력, 인위적 지연/패킷 손실, 재시작과 응답 유실 복구/Shipping은 별도다.
이후 같은 서버 세션의 미확정 명령/UI 복구 구현은 다음 절을 따른다. 실제 HTTP 응답 유실
주입·다인 지연/손실 검증과 서버 재시작 복구까지 완료했다는 뜻은 아니다.

### 가방 명령의 미확정 결과와 수동 복구 (2026-10-23)

가방 이동/교환/스택 합치기/분할의 HTTP 재시도 소진은 ‘저장되지 않음’과 다르다.
`ERPGItemCommandResultCode::Uncertain`은 일반 거절과 구분하며, UI 예약을 즉시 풀지 않는다.
기존 성공 ACK가 Projection보다 먼저 도착하면 양쪽 스냅샷을 기다리는 규칙도 유지한다.

- 서버 orchestrator는 이미 계획한 전체 Commit을 세션 메모리에 동결한다. 원래 RequestId/
  fingerprint/Actor/아이템·생성된 분할 ID/양쪽 int64 Revision/목적지/수량·메타데이터를
  유지한다. 복구 때 현재 가방으로 새 계획을 만들거나 다른 슬롯·새 ID로 바꾸지 않는다.
- 미확정 건이 남으면 해당 컨트롤러의 새 아이템 변경과 보상 캐시 덮어쓰기를 보류한다.
  기존 동시 요청 한도 안에서 보관되며 다른 소유자의 가방을 잠그지 않는다.
- 소유자 전용 `InventoryRecovery`에는 요청 ID/동작/재시도 중 여부만 복제한다. 전체 저장
  본문·서버 자격 증명은 클라이언트에 보내지 않는다. ViewModel을 다시 열어도 이 상태를 읽는다.
- BP의 `bHasInventoryRecovery`/`bCanRetryInventoryRecovery`와
  `RequestRetryInventoryCommand()`가 UI 경계다. 기존 WBP의 native 부모는
  `Recover pending item command` 버튼·미확정 안내를 런타임에 추가한다. WBP 저장은 필요 없다.
  서버의 `ServerRetryUncertainInventoryCommand(RequestId)`는 현재 인증 소유자·기존 보호
  동작 게이트를 재검사하고 동시 중복/알 수 없는 ID/다른 소유자를 허용하지 않는다.
  클라이언트와 서버 모두 3초 재요청 제한이 있으며 자동 무한 재시도는 추가하지 않는다.
- 동일 원래 Commit의 정상 성공/멱등 영수증만 적용한다. HTTP 200과 요청의 상관관계뿐 아니라
  레코드 개수·각 ID·전체 필드·expected revision+1·affected quantity를 대조한다.
  오류 HTTP 상태에 성공 모양 본문이 붙어 와도 완료로 인정하지 않는다.
- gateway는 내부 wire 재시도의 앞선 유실 가능성을 보존한다. 첫 응답을 잃고 마지막에
  401을 받아도 이를 일반 확정 실패로 낮추지 않는다. 이미 미확정이었던 복구 요청의
  401/403/404·Revision/위치 충돌·불일치 영수증은 원래 명령/잠금을 유지한다.
  처음부터 상관관계가 확인된 명확한 거절은 기존 실패 처리로 남는다.
- 종료/신원 회수 후 콜백은 기존 weak orchestrator·권한 장벽을 따른다. 상태 해제를
  위한 관리자 우회/시간 초과 강제 해제나 새 권한은 추가하지 않았다.

이 절의 최초 구현 범위는 **같은 서버·같은 인증 컨트롤러 세션의 네 가방 동작**이다. Consume의 일회 효과,
우편/장착, 디스크 journal·프로세스 재시작 복구, 영수증 부재 건의 운영 해소 정책은
새 복구 기능에 포함하지 않는다. 성공을 확인할 수 없는 건은 자동 해제하지 않으므로
원래 영수증을 복구할 수 없으면 보류가 계속될 수 있다.
후속 디스크 보존 구현과 아직 확인하지 않은 재기동 경계는 아래 「가방 명령의 서버 디스크 보존」을 따른다.

검증: 자체 Editor 전체 타깃/최종 48개 작업·모듈 링크 종료 0 뒤
`Scripts/Tests/Run-ItemAutomationTest.ps1 -InventoryRecoveryOnly -SkipSDKSetup`의
집중 native 22개(신규 복구 5 + 신규 gateway 2 + 직접 회귀 15)/러너 종료 0이다.
이동과 두 레코드 동작의 동일 직렬화 바이트·정확한 큰 int64 Revision/분할 ID,
같은 ID의 바뀐 인자·다른 소유자·새 ID 우회·동시 재시도 거절, 종료 후 콜백 무효화,
유실 뒤 인증 오류/불일치 전체 레코드·수량의 보류, ViewModel 재바인딩을 검사했다.
선택 입력 44개/DLL/닫힌 로그 해시 일치, 테스트 중·이후 Error 0 및 전체
Fatal/Assert/Ensure 0이다. 기존 기동 Error 15줄은 별도로 남는다. 1차 41개 작업 빌드도
성공했지만 마지막 보완까지 포함한 native 증거는 최종 48개 작업 DLL에만 대응한다.
proof: `Saved/Tests/InventoryRecovery-99562725/Verification.proof.json`;
native: `Saved/Tests/Item/Automation-37bf368c605649e2b20cec13c41115e2/Item.proof.json`.
이는 제어된 transport/영수증·UI 상태 fixture이며 실제 HTTP/PostgreSQL 응답 유실,
Iris 복제/버튼 입력·GPU/물리 입력·Steam/Shipping 성공으로 계산하지 않는다.
이후 이 새 복구 버튼/서버 동결 경로의 실제 HTTP 응답 유실 결과는 다음 절을 따른다.

### 복구 버튼의 실제 커밋 후 HTTP 응답 유실 연결 (2026-10-23)

`Scripts/Network/Run-DungeonBossReturnE2ETest.ps1 -InventoryRecovery -ClientCount 1`은
정상 개발 인증/실제 입장 티켓/Iris와 저장된 가방 WBP를 사용한다. 서버의 Item API만
실행별 loopback 프록시로 보내고 인증/던전 API는 그대로 둔다. 프록시는 무작위 제어 토큰과
해당 배정 서버의 Authorization 해시·캐릭터·물약 ID에 한정해 주입한다. 제어 토큰은 Node
프로세스 환경으로만 전달하고 실제 자격 증명/본문은 증거에 쓰지 않는다.

- 실제 WBP의 합성 Slate 드래그 1회로 물약 7개를 0→3번 칸으로 보낸다. 프록시는 실제
  백엔드가 전체 레코드와 일치하는 `Committed`/HTTP 200을 반환해야 UE 응답만 끊는다.
  DB에 쓰기 전 오류를 돌려주는 것을 ‘커밋 후 유실’로 계산하지 않는다.
- 유실 1회 뒤 gateway의 재전송 1회는 503으로 막아 DB에 전달하지 않았다. 클라이언트는
  `Uncertain`, 물약 0번 칸/7개/rev 1의 예전 Projection·화면과 새 조작 잠금을 유지했다.
  실제 WBP를 프로그래밍 방식으로 teardown/재부착하고 컨트롤러 경로로 다시 열어도
  같은 요청의 복구 상태와 사용 가능한 버튼이 나타났다. Projection/대기 상태 주입은 없다.
- 이 잠금 구간에서 독립 HTTP GET와 PostgreSQL로 물약은 이미 3번 칸/7개/rev 2이고,
  영수증은 Move 1건·다른 가방 동작 0건임을 확인했다. 전체 DB 영수증/활성 목록을 보관해야
  재전송을 허용하고 UI gate를 연다. 투구 1개/rev 1/1번 칸과 시작 장비는 그대로다.
- 실제 복구 버튼을 hit-test/합성 마우스 down/up으로 1회 눌렀다. UI 함수/RPC를 직접
  호출하지 않았다. 서버가 원래 직렬화 바이트 그대로 전송한 1회는 `AlreadyCommitted`를
  받고, 서버/소유 클라이언트는 `AlreadyApplied`로 완료했다. 새 ID/슬롯/Revision 변경은 없다.
- 첫 요청과 재전송의 전체 레코드/요청 상관 필드가 같고, DB GET의 전체 원래 영수증과
  활성 목록은 복구 전후 불변이다. HTTP 본문의 상태는 정상적으로 Committed→AlreadyCommitted로
  달라지므로 원본 HTTP 응답 전체가 같은 바이트라고 표시하지 않는다. 최종 영수증 1건,
  추가 Commit/Revision 증가 없음·화면의 0번 칸 비움/3번 칸 7개/rev 2·잠금 해제를 확인했다.

최종 실행: `Saved/Tests/DungeonBossReturn/fb55d2b17e5748ebbaa3a2355e42d6de/result.json`.
자체 Editor 6개 작업/모듈 링크/전체 타깃·러너 종료 0, 프록시 단위 5개/시작 전 잘못된
실행 조합 5종 거절, 선택 입력 48개(WBP/정의 에셋 포함)/DLL/닫힌 UE 2개·프록시 2개 로그
해시 일치 및 Error/Fatal/Assert/Ensure 0이다. 자신의 UE 2개·프록시·백엔드만 종료했고
Docker/기존 DB·실행별 증거는 보존했다. 운영 복구 코드/BP/백엔드 API·스키마는 바꾸지 않고
opt-in probe/러너·주입/판정기만 추가했다. 기존 native 22개와 다른 가방 E2E는 반복하지 않았다.
proof: `Saved/Tests/InventoryRecoveryLive-e9fec9bb/Verification.proof.json`.

범위는 1인 로컬 개발 인증·GPU offscreen 합성 입력·같은 서버 세션의 이동 복구다.
기존 비검증 플랫폼 identity fixture를 사용하지만 입장/아이템 소유권 검사는 유지한다.
실물 입력/Steam·다인·패킷 지연/손실·서버 재시작/Consume 효과/Shipping은 별도다.
이후 실패한 복구 요청의 UI 대기 상태는 다음 절을 따른다. 실제 자격 만료·권한 회수의
실연결과 서버 재시작 복구까지 확인한 결과는 아니다.

### 실패한 복구 요청의 진행 표시와 재요청 간격 (2026-10-23)

복구 클릭 직후 `bInventoryRecoveryInFlight`를 로컬에서 설정해 즉시 버튼을 비활성화한다.
서버 복제의 `bRetrying=false→true→false`가 한 번의 갱신으로 합쳐져도 같은 요청의
Client ACK가 로컬 진행 표시를 끝낸다. 실패 ACK는 원래 `InventoryRecovery`나 아이템
예약을 해제하는 근거가 아니며, 다른 요청 ID의 ACK로 이 진행을 종료하지 않는다.

- `bCanRetryInventoryRecovery`는 로컬 요청 진행·서버의 재시도 상태와 클릭 후 3초 간격을
  함께 반영한다. 버튼/진행 중·재요청 대기 문구는 native 부모와 BP FieldNotify로 연결한다.
- Core ticker는 3초가 지난 뒤 **복구 버튼의 가용성만** 갱신한다. 아이템 잠금/미확정 기록을
  시간으로 해제하거나 HTTP/RPC를 자동 전송하지 않는다. ViewModel 해제 시 ticker를 제거한다.
- 서버는 현재 인증된 소유자의 이미 보관된 요청만 확인한 뒤 3초 간격을 검사한다.
  간격 미달이면 추가 저장 없이 `Uncertain` ACK를 보내 로컬 진행 표시가 무응답으로
  남지 않게 한다. 알 수 없는 ID/다른 소유자에 새 조회·재전송 권한을 주지 않는다.
- 실패 주입 모드는 `-InventoryRecovery -InventoryRecoveryRetryFailure -ClientCount 1`이다.
  첫 커밋 응답 유실은 기존 실제 DB 경로를 쓰고, 첫 수동 복구 요청에만 범위를 제한한
  **합성 HTTP 401**을 반환한다. 실제 자격 증명을 만료·회수하거나 백엔드 인증을 바꾸지 않는다.
  원래 요청/예전 Projection·잠금과 DB 전체 레코드/영수증을 확인한 뒤 다음 수동 재시도를 허용한다.

검증 결과: 자체 최종 Editor 24개 작업/모듈 링크/전체 타깃·집중 native 22개/러너 종료 0,
프록시 단위 6개·잘못된 실행 조합 5종 거절이다. 동일 요청의 실패 ACK가 복제 값 변화 없이
도착하는 조건과 타이머가 재요청 가용성만 복원하는 조건은 native UI fixture에서 검사했다.
최초 빌드의 ticker 핸들 C2679/C2664는 설치 엔진의 `FTSTicker::FDelegateHandle`로 수정했고
실패 로그/종료 6은 보존했다. native 입력 44개/실행 DLL·닫힌 로그 일치, 검사 중·이후
Error 0·전체 Fatal/Assert/Ensure 0이며 기존 기동 Error 15줄은 별도다.

실연결은 `Saved/Tests/DungeonBossReturn/3bfd6a46e78a45b08865a81fd58d516f/result.json`이다.
합성 드래그 1회→실제 커밋 후 응답 유실 1회/gateway 503 보류 1회→WBP 재오픈→첫 복구
클릭의 합성 401 1회→같은 요청의 새 Uncertain ACK·원래 화면/잠금 유지→버튼 재활성→
두 번째 복구 클릭의 동일 바이트 재전송 1회/AlreadyCommitted·AlreadyApplied를 확인했다.
클릭부터 재활성까지 로그 시각 차이는 약 3.135초다. 401 뒤에도 독립 GET/PG의 전체 영수증과
활성 아이템은 불변이고 최종 Move 1건/추가 저장·Revision 증가 0·시작 장비 유지다.
실행별 입력 48개(WBP 포함)/DLL/닫힌 UE·프록시 로그 4개 해시 일치와 실행 Error/Fatal/Assert/
Ensure 0이다. 자기 native/UE·프록시·백엔드만 종료하고 DB/증거·Docker는 보존했다.
proof: `Saved/Tests/InventoryRecoveryRetry-031df467/Verification.proof.json`;
native: `Saved/Tests/Item/Automation-9a04b343b8f1485a8a4a30f74d8f1d77/Item.proof.json`.

이는 **주입된 HTTP 인증 오류 응답**에서의 보류 유지/재시도 UX다. 실제 자격 만료·신원 회수,
복구 중 열린 연결의 퇴장·서버 재시작·Steam/Shipping 검증은 아니다. 기존 보호 동작/소유권/
동결·CAS 규칙을 유지하며 WBP 저장·백엔드 API/스키마/인증 정책 변경은 없다.
배정 서버 자격 만료의 후속 실연결은 다음 절을 따른다. 플레이어 신원·권한 회수와의
교차, 실제 자격 갱신·재발급 정책은 별도다.

### 신원 회수 후 가방 UI의 종료 상태 (2026-10-24, native 실행 확인)

기존 `ARPGPlayerState::RevokeAuthenticatedIdentity`의 정확한 소유 Controller 회수에
아이템 명령 컴포넌트의 **owner-only 종료 신호**를 연결했다.
`IsItemCommandAuthorityRevoked()`와 ViewModel의 BP FieldNotify
`bInventoryAuthorityRevoked`로 읽으며 클라이언트가 이 상태를 서버에 쓰는 API는 없다.
멤버십 조회 실패나 일반 인증/서비스 자격 만료가 이 신호를 만들지는 않는다.

- 컴포넌트는 기존 명령/효과/미확정 계획을 무효화한 다음 알린다. 재진입하는 BP 관찰자도
  무효화가 끝난 상태를 보며 이미 시작한 DB 커밋은 취소·롤백하지 않는다.
- ViewModel은 종료 신호를 관측한 뒤 같은 바인딩에서 이를 유지한다. 복구 목록이 비거나
  늦은 `AlreadyApplied` ACK/예전 Ready Projection/보상 알림이 와도 정상 복구로 표시하지 않는다.
  가방·장비·우편 표시와 로컬 예약/진행 핸들만 정리하고 재시도·재조회·아이템 조작을 막는다.
  이 UI 정리는 미확정 영수증 해소 또는 서버 재시작 복구의 증거가 아니다.
- native 부모를 사용하는 기존 WBP는 종료 안내를 표시하고 복구 버튼/성공 결과를 숨긴다.
  다시 열어도 컴포넌트의 종료 상태를 다시 읽는다. WBP 자산 저장은 필요 없으며 커스텀 BP는
  위 FieldNotify로 종료 상태를 구분할 수 있다. 이 표시는 서버 권한 검사를 대신하지 않는다.

신규 native 2종은 실제 PlayerState 회수 함수→미확정 컴포넌트/진행 중 frozen 재시도→늦은
성공 콜백 무시·남은 신원 보존과, 복제 알림 순서를 모사한 예전 목록/ACK/재오픈의 종료
우선 처리를 검사하도록 추가했다. 저장 횟수는 fixture 카운터이며 실제 HTTP/PG/Iris가 아니다.
`Run-ItemAutomationTest.ps1 -InventoryRecoveryOnly`에 기존 회귀와 함께 선택되고 PlayerState
입력 해시도 포함한다. 영향받는 C++ 8개는 집중 `-File/-NoLink`로 컴파일했다. 첫 집중
빌드의 6개 성공 뒤 테스트의 Package 및 컴포넌트의 World/GameInstance 직접 include 누락을
수정했고, 최종 2개 컴파일/선택 빌드 종료 0을 확인했다. 그 전 전체 NoLink 시도는 공유
E2E 헤더 변경과 생성 코드 불일치로 자체 빌드만 중단했으며 성공으로 계산하지 않는다.
당시 에디터와 기존 DLL은 유지했으며 이 컴파일만으로 native 실행을 주장하지 않았다.
당시 증거: `Saved/Tests/InventoryAuthority-a196856d/Implementation.proof.json`.

후속으로 **이미 연결돼 있던 공유 DLL**에서 `-InventoryRecoveryOnly -SkipSDKSetup`의
소스 유래 24종(위 신규 2종 포함)을 실행해 모두 성공/러너 종료 0을 확인했다. 입력 46개와
실행 전후 DLL·닫힌 로그 해시가 일치한다. 기존 기동 Error 15줄과 검사 중/이후 Error 0,
전체 Fatal/Assert/Ensure 0을 구분한다. Config 3개와 저장된 WBP도 해시가 유지됐다.
native: `Saved/Tests/Item/Automation-a17481f6d02a4e51ab74ff8531678a19/Item.proof.json`.

이번 자체 전체 빌드는 실행 중 바뀐 공유 E2E 헤더/생성 위치 불일치로 자체 프로세스만
중단했다. 생성 코드는 다른 빌드에서 갱신됐고 제 재시도는 공유 빌드 잠금에서 대기하다
종료했다. **자체 전체 Editor 타깃/새 DLL 링크 성공은 아니다.** 최초 native 기동도 SDK
재탐색이 같은 잠금을 기다려 제 프로세스만 중단했고, 기존 `SkipSDKSetup` 옵션으로 이미
확인된 SDK의 재탐색만 생략했다. 인증/권한 검사를 우회하지 않는다. 다른 작업의 빌드는
종료하지 않았으며 native 실행 뒤 공유 DLL이 바뀌면 이 결과는 기록한 DLL의 증거로 남는다.
후속 증거: `Saved/Tests/InventoryAuthorityVerify-4bddb2c0/Verification.proof.json`.

실제 포기 영수증·멤버십 polling·Iris 퇴장과 미확정 명령의 교차는 아직 별도다.
다음은 실제 2인 연결에서 원래 커밋/영수증과 남은 파티를 보존하며 이 경계를 연결하는 작업이다.

### 미확정 가방 명령과 2인 포기·퇴장 (2026-10-24)

독립 `-InventoryRecovery -InventoryRecoveryRevocation -ClientCount 2` 경로를 추가했다.
범위는 이전 명령이 실제 저장됐지만 응답 유실로 클라이언트에 미확정인 상태에서,
본인의 기존 포기 API와 서버의 불변 영수증 조회·Iris 퇴장이 만나는 경계다.

- 첫 클라이언트는 저장된 WBP에서 드래그·재오픈을 수행하고 예전 슬롯/Revision·원래
  요청의 잠금을 유지한다. 두 번째 클라이언트는 headless 입장하며 같은 서버가 선언된
  두 번째 소유자의 초기 GET만 통과시킨다. 해당 소유자의 커밋을 프록시가 허용하지 않는다.
- 본인 플레이어 토큰으로 기존 포기 POST를 호출하고 같은 세션/서버/캐릭터/Steam의
  멤버십 영수증을 확인한다. 멤버 누락이나 인증 실패를 포기 성공으로 해석하지 않는다.
- 기존 서버 파티 관측기를 재사용해 원래 미확정 명령의 무효화→신원 회수 알림→연결/액터
  정리와, 남은 플레이어의 같은 Pawn/연결/신원/전투 세대를 관측한다. 클라이언트의 추가
  network-failure 관측은 읽기 전용이며 테스트 GameMode/입장 우회를 활성화하지 않는다.
  관측기의 기존 조건인 서버 송신 지연 80~160ms/손실 5%를 명시하고 실제 설정을 확인한다.
  클라이언트 송신 시뮬레이션을 확인한 결과로 확대하지 않는다.
- 원래 아이템 영수증 DB 전체 행 해시와 양쪽 플레이어의 전체 활성 목록을 전후 비교한다.
  포기 후 소유 플레이어 조회를 사용하며 서버의 영수증 조회 권한을 넓히지 않는다.
  명령 재전송·DB 롤백·정산을 하지 않고 미확정 명령을 성공 복구로 표시하지 않는다.

자체 Editor 90개 작업과 관측 종료 gate 보완 뒤 4개 작업 모두 모듈 링크/전체 타깃 종료 0이다.
프록시 8종/실행 조합 거절 5종/잘못된 SQL GUID 거절 4종과 대상 연결 종료 진단 정책 6종도
확인했다. 정확한 영수증을 확인한 뒤 대상 클라이언트의 정해진 연결 종료 진단만 분류하며,
다른 플레이어 오류와 Fatal/Assert/Ensure를 허용하지 않는다. live 검증과 DB/프록시 비교가
끝난 뒤 관측 종료 gate를 확인하고 프로세스를 정리한다. 종료 후 DB 영수증·멤버십/정산도 읽는다.

실행 `Saved/Tests/DungeonBossReturn/01603a2a610b4b7f8d2dc5792a7142a0/result.json`에서
실제 WBP 드래그/재오픈·미확정 잠금, Move 1건/추가 커밋·Revision 0, 본인 포기 영수증과
production polling/Iris 퇴장, 남은 Pawn/연결/전투 유지 및 양쪽 전체 활성 목록/시작 장비 불변을
관측했다. DB 영수증 전체 행 해시와 정리 후 멤버십/정산도 유지됐고, 예상 대상 연결 종료
진단 1건 외 닫힌 UE/프록시 로그의 비예상 Error/Fatal/Assert/Ensure는 0이다. DLL/닫힌 로그와
Config 3개/WBP 해시는 일치했다.

위 이전 실행의 **최종 러너 판정은 실패**다. 실행 도중 다른 작업에서 로비·세션/HTTP 소스 4개가
바뀌어 입력 59개의 종료 일치 검사가 실패했다. 당시 기록은 고정된 DLL의 기능 관측으로 보존하고
최신 소스 전체의 통과로 계산하지 않았다. 다른 작업의 소스를 되돌리지 않고 수정이
안정된 뒤 현재 소스의 빌드·최종 실행을 기다렸다. 앞선 서버 관측 옵션 누락/시뮬레이션 조건
불일치·정리 중 관측 계속에 따른 세 번의 실패도 보존했다.
증거: `Saved/Tests/InventoryRevocation-ec5ef33e/Verification.proof.json`.

후속 확정 실행은 `Saved/Tests/DungeonBossReturn/b8fc2064bf4a4f90aff6ecb46d3ef70b/result.json`이다.
동시 수정 종료 뒤 현재 입력을 고정하고 자체 Editor 타깃 6개 작업/종료 0을 확인했다.
이번 빌드는 최신 공유 게임 DLL을 사용했으며 새 게임 DLL 링크로 계산하지 않는다.
실제 2인 HTTP/PostgreSQL/Iris 시나리오는 **러너 종료 0/Passed=true**이며 입력 59개와
DLL·닫힌 UE/프록시 로그, Config 3개/WBP 해시가 일치했다. 본인 포기·원래 미확정 명령의
무효화/퇴장·남은 같은 Pawn/연결/전투와 원래 아이템 보존을 다시 확인했다. Move 영수증 1건,
추가 커밋/Revision 0, 정리 후 DB 불변이며 예상 대상 연결 종료 진단 1건 외 비예상
Error/Fatal/Assert/Ensure는 0이다. 자체 UE/프록시/백엔드만 종료하고 DB·Docker와 실패
기록은 보존했다. 기존 native/다른 E2E를 반복하지 않았고 구현 코드 변경은 없다.
확정 증거: `Saved/Tests/InventoryRevocationConfirm-9d4d3b0e/Verification.proof.json`.

이 시나리오는 포기 UI의 Slate 입력, 퇴장 전에 owner-only UI 종료 신호가 반드시 전달됨,
진행 중 수동 복구의 늦은 응답, 클라이언트 송신 지연/손실·Steam/Shipping·서버 재시작을 증명하지 않는다.

### 배정 서버 자격 만료와 미확정 아이템 복구의 교차 (2026-10-24)

`Run-DungeonBossReturnE2ETest.ps1 -InventoryRecovery -InventoryRecoveryCredentialExpiry -ClientCount 1`은
저장 성공 후 응답 유실로 이미 미확정인 이동 명령에서, **그 실행의 배정 게임 서버 자격
1건만** 만료시킨다. 원래 token hash·세션·서버 ID를 모두 고정한 UPDATE의 변경 행 수 1을
요구하고, `expires_at`만 DB 현재 시각보다 1초 전으로 옮긴다. 자연 TTL 경과나 플레이어
토큰 만료를 검증한 것으로 표시하지 않는다. 원래 자격을 복원·재발급하지 않는다.

- 만료 전에는 실제 Move 커밋/응답 유실·WBP 재오픈·예전 0번 칸/7개/rev 1·잠금과,
  DB의 3번 칸/rev 2·영수증 1건을 확인한다. 일반 gateway 재전송 1회는 기존 503 gate로 보류한다.
- 만료 뒤 실제 백엔드의 영수증 GET이 401인지 먼저 확인한다. WBP 복구 버튼의 합성 클릭
  1회가 원래 바이트로 보낸 POST도 실제 `ApiAuthenticationMiddleware`/`BearerAuthenticator`/
  PostgreSQL 만료 검사에서 401이다. 프록시는 그 빈 401 응답을 변경 없이 전달하며 합성 401은 0건이다.
- 서버는 `Uncertain/Unauthorized/Pending=1`, 클라이언트는 같은 요청·예전 화면/잠금을 유지한다.
  재요청 간격이 끝나도 아이템은 열리지 않으며 성공/AlreadyApplied를 만들지 않는다.
- 영수증 GET의 서버 전용 권한을 완화하지 않는다. 만료 후에는 원래 영수증 **DB 전체 행**의
  SHA256을 전후 대조하고 활성 아이템은 아직 유효한 소유 플레이어 토큰으로 읽는다.
  영수증 1건/추가 저장·Revision 증가 0·활성 목록/시작 장비 불변이다.
- 멤버 GUID 목록과 포기/자동 퇴장/정산 기록, InProgress·서버 배정이 유지된다. 소유 클라이언트의
  인증 캐릭터/비회수 상태도 확인한다. 서비스 자격 만료를 개인 포기나 신원 회수로 바꾸지 않는다.

실행: `Saved/Tests/DungeonBossReturn/d9006177ffcb46dea0823a1c2bc0988a/result.json`.
자체 Editor 6개 작업/모듈 링크/전체 타깃·실연결 러너 종료 0, 프록시 단위 7개(빈/JSON 401
전달 포함)·잘못된 실행 조합 6종/SQL 식별자 4종 거절, 선택 입력 54개/DLL/닫힌 UE·프록시
로그 4개 해시 일치와 Error/Fatal/Assert/Ensure 0이다. opt-in probe·진단/주입·판정기만
보완했고 운영 인증/아이템 복구 규칙·BP/백엔드 API·스키마는 바꾸지 않았다. 기존 native 22개나
정상/합성 401 E2E는 반복하지 않았다. 자기 UE 2개·프록시·백엔드만 종료했으며 만료 자격/DB·
증거와 Docker는 보존했다. proof: `Saved/Tests/InventoryCredentialExpiry-42348552/Verification.proof.json`.

이 결과는 **성공 복구가 아니라 같은 서버 세션에서의 안전한 미확정 보류**다. 관측 종료 시까지
원래 명령/잠금이 남고 자격은 계속 만료되어 있었다. 프로세스 종료 뒤 메모리 요청 복원,
새 서비스 자격 발급/갱신 후 재개, 실제 플레이어 로그인 만료/신원·권한 회수·다인/지연·
Steam/Shipping은 별도다. 다음은 기존 신원 회수 경로와 미확정 복구 요청의 교차다.

### 가방 명령의 서버 디스크 보존 (2026-10-24 구현, 2026-10-25 native 확인)

`FRPGInventoryCommandJournal`을 기존 네 가방 동작의 비동기 경로에 연결했다.
HTTP 호출 **전에** 서버가 계획한 Commit 전체를 파일에 쓰고 flush·교체한다. 원래 요청
ID/fingerprint/두 레코드·정확한 int64 revision/분할 ID/슬롯·수량을 그대로 보존하며,
유실 결과를 새 명령으로 재계획하거나 DB 트랜잭션을 롤백하지 않는다.

- 범위는 backend URL·배정 server ID·던전 session·character·Steam ID 모두에 묶인다.
  현재 인증된 PlayerState에서만 열며, 복원된 파일 자체는 권한이 아니다. 같은 캐릭터라도
  세션/계정이 바뀌면 이전 파이프라인·늦은 응답을 적용하지 않는다. 기존 백엔드 인증·CAS와
  영수증 전체 필드 대조를 유지하고 파일에는 bearer/service token을 저장하지 않는다.
- 새 컨트롤러의 authoritative source 준비 시 원래 ID/동작을 기존 owner-only
  `InventoryRecovery`에 노출한다. 자동 HTTP replay는 없고 기존 BP/WBP 수동 복구를 쓴다.
  성공/AlreadyApplied를 검증한 뒤에만 디스크 항목을 해소한다. 최초 명확한 거절은 해소할 수
  있지만, 이미 미확정인 명령의 401/404/충돌·불일치 응답으로는 해소하지 않는다.
- 정상 EndPlay/단절은 파일을 지우지 않는다. 로드된 저널의 정확한 신원 회수는 원래 본문을
  남긴 채 영구 quarantine한다. 일반 자격 만료는 신원 회수로 바꾸지 않는다. 파일을 열기 전
  회수된 신원은 기존 입장/멤버십 권한 장벽이 차단하며 저널만으로 재입장을 허가하지 않는다.
- 완성된 임시 snapshot만 검증 후 승격한다. 손상·서로 다른 범위·세대 충돌·quarantine 해제
  시도·쓰기/정리 실패는 원본을 보존하고 새 변경을 차단한다. 실패 시 BP getter
  `IsInventoryRecoveryStorageBlocked()`와 ViewModel FieldNotify
  `bInventoryRecoveryStorageBlocked`, 기존 native/WBP 안내를 사용한다. 시간 경과로 강제
  해제하거나 실패 파일을 빈 저널로 간주하지 않는다.
- `[/Script/Project_RPG.RPGItemBackendSubsystem]`의 서버 설정
  `InventoryJournalDirectory`가 비어 있으면 `Saved/ItemInventoryJournal`을 사용한다.
  운영에서는 재기동 후에도 유지되는 **서버 소유 볼륨/ACL**이 필요하다. 경로의 해시는 범위
  구분일 뿐 서명이 아니며, 클라이언트 파일 업로드·손상된 서버 파일시스템을 신뢰하지 않는다.
  한 배정의 단일 서버 writer가 전제이며 호스트 이관/다중 서버 fencing을 구현한 것은 아니다.

검증: 앞선 C++ 7개 `-File/-NoLink`에 이어 2026-10-25 자체 Editor 전체 타깃 재빌드
283개 작업/종료 0과 새 DLL 연결을 확인했다. 최초 빌드는 동시 로비 헤더 변경으로
생성 위치가 불일치해 자기 빌드만 중단하고 현재 헤더로 재시도했으며 타 작업 소스는 보존했다.
최초 집중 native는 31개 성공/1개 실패였다. 기존 `Swap`은 수량을 옮기지 않아
`AffectedQuantity=0`인데 저널이 양수만 허용하던 오류를 수정했다. 기존 frozen 본문이나
백엔드 계약을 바꾸지 않고 Swap은 정확히 0, 나머지 세 동작은 양수인 규칙을 적용했다.
수정 후 자체 Editor 21개 작업/모듈 링크/전체 타깃 종료 0, 기존 러너
`Run-ItemAutomationTest.ps1 -InventoryRecoveryOnly -SkipSDKSetup`의 소스 유래
**32개 전부 성공**이다. 그중 저널 8종은 실제 임시 파일을 사용해 HTTP 호출 전 선저장,
새 객체의 파일 복원·네 동작의 동일 바이트/큰 int64/분할 ID·merge 종료 레코드,
범위 격리·부분 파일/세대 충돌·회수 기록·IO/정리 실패·부정 재시도 보류를 검사했다.

입력 50개/자체 최종 DLL/닫힌 로그 해시 일치, 테스트 중·이후 Error 0 및 전체
Fatal/Assert/Ensure 0이다. 기존 기동 Error 15줄과 최초 실패는 별도로 보존했다.
Config 3개/WBP는 실행 전후 같고 자기 native 프로세스만 정리했다. 러너 proof에는
`localInventoryJournalFiles=true`로 실제 로컬 파일과 모의 backend를 구분했다.
proof: `Saved/Tests/InventoryJournalVerify-c262c41e/Verification.proof.json`;
native: `Saved/Tests/Item/Automation-732a9c7a520b4d30bfba6a2b4ea03ba7/Item.proof.json`.

이는 **한 native 프로세스 안의 객체 재구성과 파일 복원**의 증거다. 실제 서버 프로세스
재기동/HTTP/PostgreSQL/Iris는 별도 범위이며 이동 1건의 후속 실연결은 다음 절을 따른다.
Consume의 일회 효과·장착/우편의 새 영속 복구·영수증 부재 운영 해소·Steam/Shipping은
계속 별도다. 저장 BP/백엔드 API·스키마·인증 정책은 바꾸지 않았다.

### 장착·해제 명령의 디스크 보존과 수동 복구 (2026-10-26, 구현·native 확인)

기존 서버 저널과 비동기 명령 경로에 `Item.Equip`/`Item.Unequip`을 연결했다.
HTTP 전에 원래 요청 ID·fingerprint·정확한 int64 revision·단일 아이템 전체 레코드를
보존하며, 재구성된 파이프라인은 자동 replay 없이 기존 owner-only 복구 목록/BP 버튼에
원래 명령을 제공한다. 현재 인증 신원·배정 범위와 백엔드 소유권/CAS 검사는 그대로다.

- 장착은 본인 Equipment의 유효한 부위, 해제는 본인 80칸 Inventory의 원래 목적지만
  허용한다. 두 동작 모두 기존 ID·수량/처리량 1·활성/비잠금 레코드 한 개이며 신규 지급,
  타인 컨테이너·부위 범위 초과·종료 레코드·복수 mutation은 저널에 저장하지 않는다.
- 최신 조회에 이미 장착/해제 결과가 있어도 원래 영수증 확인 전에는 새 변경을 차단한다.
  재시도 401/404/충돌·변조된 목적지/수량으로 보류를 해소하지 않고, 전체 영수증 대조 후
  원래 레코드를 기존 Projection → 장비 상태 재조정 경로로 전달한다. 소비의 일회성 GAS
  효과는 이 상태 복구와 구분하며 `Item.Consume`은 여전히 저널 replay 대상이 아니다.
- 공유 ViewModel에서 원래 복구 항목이 남은 재시도의 거절은 장착/해제 슬롯 예약을
  지우지 않는다. 최초 확정 거절은 기존처럼 해제하며 성공/AlreadyApplied ACK만으로는
  예약을 풀지 않고 새 권위 revision/제거를 기다린다. 기존 가방·장비 창/BP 조회·공통
  복구 버튼을 사용하고 새 RPC·WBP/에셋·백엔드 API/스키마는 추가하지 않았다.

자체 정상 `Project_RPGEditor Win64 Development` 빌드 9개 작업·게임 DLL 링크/전체
타깃 exit 0(48.90초) 뒤 기존 `Run-ItemAutomationTest.ps1 -InventoryRecoveryOnly
-SkipSDKSetup`으로 소스 유래 **40/40**을 확인했다. 새 장비 2종은 양쪽 동작의 실제
임시 파일 선저장/객체 재구성·동일 바이트·큰 revision·귀속/내구도/전체 레코드 보존과
잘못된 mutation 11종씩을 포함한다. 기존 UI 사례에도 양쪽 동작의 재시도 거절/슬롯 예약·
ACK 선행/후속 Projection 해소를 추가했다. 검증 모듈 `82021ADC…7301B1`·선택 입력
51개/닫힌 로그의 전후가 일치했고 러너 exit 0이다. 기동 전 기존 Error 15줄은 남으며
검사 중/이후 Error 0·전체 Fatal/Assert/Ensure 0이다.

빌드: `Saved/Tests/EquipmentRecovery-4da581c9/Build.log`;
native: `Saved/Tests/Item/Automation-36d09d12d14b4f148872293d83aaf1df/Item.proof.json`.
이는 모의 backend·실제 로컬 파일/UE 객체·ViewModel의 검증이다. 실제 서버 프로세스
재시작·HTTP/PostgreSQL/Iris·실물 입력/Steam/Shipping 성공으로 계산하지 않는다.
기존 가방/우편 실통합은 반복하지 않았다. 당시 다음 장비 단계였던 실제 응답 유실과 새
서버의 장비 재조정 연결은 아래 후속에서 확인했으며 소비 효과 복구는 별도로 남는다.

### 장착·해제의 실제 서버 재기동 복구 (2026-10-26, 두 경로 실연결 통과)

기존 복구 러너에 별도 `Equip`/`Unequip` 모드를 연결했다. 각 새 배정의 실제 개발 인증·
티켓/Iris 입장 뒤 저장된 가방/장비 WBP의 **우클릭 1회**로 원래 명령을 제출한다. 실제
커밋 응답을 끊은 동안 옛 Projection·슬롯 예약·장비 창 teardown/reopen의 잠금을 확인한
뒤 자기 서버/클라이언트만 교체했다. 같은 배정·서비스 자격·서버 UserDir/저널을 유지하고
새 클라이언트 UserDir·로그인/티켓으로 입장한다. 복구 상태나 아이템을 주입하지 않는다.

- 새 조회는 같은 ID·수량 1·revision 2의 확정 DB 상태지만 원래 복구 항목으로 조작을
  잠근다. 자동 replay/새 장착·해제 클릭은 0회이며 기존 **장비 창 복구 버튼 1회**가 원래
  전체 바이트를 재전송해 `AlreadyApplied`로 해소한다. 장착은 Head 0번 칸, 해제는 원래
  가방 0번 칸이고 장착 시 정상 `BindOnEquip` → `CharacterBound`만 허용한다.
- `Item.Equip`/`Item.Unequip` 영수증은 각 1개·추가 커밋/ID/revision은 0이다. 전체 영수증
  DB 행 SHA·전체 활성 가방/시작 장비·다른 아이템, 원래 전체 레코드와 저널 1→2/명령 0·
  pending/resolved 사본/live 파일을 대조했다. 종료 후 독립 DB 조회도 동일했다.
- 서버 실제 장비 재조정과 소유 클라이언트 장비 Snapshot/셀을 함께 확인했다. 장착은 옛
  서버 Head 적용 0/새 서버 초기 적용 1, 해제는 옛 서버 1/새 서버 0이며 복구 replay가 Head
  자원을 추가 적용/제거하지 않는다. 종료 정리의 정상 해제는 실행 중 동작과 구분한다.
  종료 전에 검증한 활성 로그 접두부 길이/SHA를 고정해 닫힌 로그와 다시 대조하고, 오류는
  정리 구간까지 포함한 전체 로그에서 검사한다. 모든 장비 종류/임의 GAS 효과의 검증은 아니다.

첫 정상 빌드 시도는 검사 코드의 private API 접근으로 실패했고 기존 공개 표시 API로
수정한 뒤 자체 정상 Editor 4개 작업·게임 DLL 링크/전체 타깃 exit 0(17.79초)을 확인했다.
첫 장착 러너도 종료 정리의 정상 REMOVE를 검사기가 오판해 실패한 채 보존했다. 활성
경계를 고정하도록 검사기만 수정한 **새 장착 실행**과 첫 해제 실행은 각각 exit 0/PASS다.
앞선 실패를 사후 PASS로 승격하지 않았고 인증/lease·운영 복구 정책을 바꾸지 않았다.

프록시 32개·PS 4개 파싱/잘못된 모드 12종·미연결 DLL 4종, 전체 레코드 정상 2/변조 거절
16종·잘못된 활성 로그 경계 4종을 확인했다. 두 성공 실행 각각 입력 58개(정규화 중복 0)·
DLL `ACDF7D10…CD3390`/backend·닫힌 로그 8개가 일치하고 Error/Fatal/Assert/Ensure 0이다.
Config 3개와 가방/장비 WBP는 불변이며 기존 native 40개와 가방/우편 E2E는 반복하지 않았다.
전용 비Shipping 읽기 전용 관측 외 운영 장비 적용·권한·RPC/API/스키마·BP/에셋 변경은 없다.

확정: `Saved/Tests/EquipmentRestartLive-f4176a39/Verification.proof.json`;
장착: `Saved/Tests/DungeonBossReturn/4bfba3f01f2643529e7500c64062cb53/result.json`;
해제: `Saved/Tests/DungeonBossReturn/bc12aa617391473698d2aeeadc9486b8/result.json`.
실행은 기존 러너의 `-InventoryRecovery -InventoryRecoveryRestart -ClientCount 1`에
`-InventoryRecoveryOperation Equip` 또는 `Unequip`을 지정한다. 로컬 개발 인증·합성 Slate
입력·단일 배정/호스트 범위이며 소비의 일회 효과 복구, 다인/손실·Steam/Shipping·호스트
이관/fencing은 별도다. 다음 기능 구현은 소비 저장과 GAS 효과 사이의 중복/누락 복구다.

### 실제 서버 재기동의 가방 수동 복구 연결 (2026-10-25, 이동 1건 실연결 통과)

기존 `Run-DungeonBossReturnE2ETest.ps1`에 `-InventoryRecoveryRestart`를 추가했다.
먼저 **이동 1건**으로 공통 저널의 실제 프로세스 수명을 확인하는 경로이며, 교환/합치기/
분할의 실재기동까지 완료한 것으로 계산하지 않는다. `-InventoryRecovery -ClientCount 1`
전용이고 기존 인증 만료/회수/합성 401 모드와 혼용하지 않는다.

- 기존 WBP 드래그→실제 커밋 응답 유실·예전 화면 잠금 경계에서 서버 저널의 정확한
  backend/배정/세션/캐릭터/Steam 범위와 원래 wire SHA256을 대조한다. 전체 DB 영수증
  행 해시·활성 아이템·리비전/커밋 수를 보존하고 pending 파일을 별도 증거로 남긴다.
- 자신의 클라이언트/서버 2개만 종료하고 backend·프록시·배정 자격·서버 UserDir와 명시적
  저널 경로는 유지한다. 재기동 로그는 별도 파일이고, 새 클라이언트는 새로운 UserDir에서
  실제 로그인/새 입장 티켓을 사용한다. 자동 재접속 UX나 호스트 이관을 구현한 것은 아니다.
- opt-in C++ 입력 probe는 새 DB Projection(이미 이동된 3번 칸/rev 2)과 복원된 원래
  요청/잠금·복구 버튼을 함께 기다린다. 상태나 영수증을 주입하지 않고, 복원 중 HTTP
  재전송이 없었는지 확인한 뒤에만 기존 WBP 버튼의 합성 Slate 클릭을 허용한다.
- 원래 frozen 바이트의 AlreadyApplied, 잠금 해제·동일 저널의 한 세대 해소, 전체 영수증/
  가방/장비 불변·추가 커밋 0을 확인하도록 연결했다. old/new PID와 양쪽 새 인증/입장,
  닫힌 로그·선택 입력/DLL 해시를 따로 기록한다. SQL은 검증된 canonical GUID의 조회만
  추가하며 DB 롤백·수령인/슬롯 교체·서비스 자격 재발급·권한 우회는 없다.

점검: 기존 프록시 단위 8개, 잘못된 재기동 옵션 5종·SQL GUID 4종 거절, 변경 PS 3개
구문 검사 성공. C++ 2개 NoLink 시도에서 드러난 `FWeakObjectPtr`의 간접 include 의존은
probe 헤더의 명시적 `UObject/WeakObjectPtr.h`로 보완했다. 첫 실패 로그는 보존했고,
수정 후 재시도는 타 작업 Build.bat 대기 상태에서 자기 대기 프로세스만 종료했다.
초기 준비 기록은 `Saved/Tests/InventoryRestart-abe1d63f/Preparation.proof.json`에 보존했다.
후속 공유 게임 DLL에는 해당 probe가 이미 연결되어 있어, 이번에는 이 최신 DLL을 고정해
아래 모드를 실제 실행했다. 자체 C++ 재빌드/링크나 지난 native 32개 반복은 하지 않았다.

**실행 결과: runner 종료 0/Passed=true.** 실제 WBP 합성 드래그 1회→Move 커밋 응답
유실·원래 요청/예전 Projection 잠금→서버 저널 선저장/DB 단일 커밋을 확인했다.
원래 UE 2개를 종료한 뒤 같은 배정·자격·경로의 새 서버/새 UserDir 클라이언트 2개로
교체했고, 실제 로그인/새 티켓/Iris 입장 후 최신 3번 칸/rev 2와 원래 요청 잠금을 함께
복원했다. 자동 replay/새 드래그는 0건이다. 복구 버튼 합성 클릭 1회가 동일 frozen
바이트의 AlreadyApplied→잠금 해제와 저널 generation 1→2/명령 해소를 완료했다.
PostgreSQL 전체 영수증 행 SHA256/활성 가방·장비/멤버십·배정 유지, Move 영수증 1건/
추가 커밋·리비전 증가 0, 이전 pending 사본과 해소 파일 사본의 해시도 확인했다.

선택 입력 53개/DLL·닫힌 UE/프록시 로그 6개 일치, 추가로 닫힌 백엔드 로그 2개까지
총 8개를 고정했다. runtime Error/Fatal/Assert/Ensure 및 backend fail/fatal 0이다.
Config 3개/WBP는 전후 동일하고 자기 UE/프록시/백엔드만 종료했다. Docker를 기동해
기존 PostgreSQL 컨테이너를 사용했으며 둘은 유지하고 사용자 에디터는 그대로 두었다.
이번 실행에서 C++/BP/설정·운영 API/스키마·저널 프로토콜 변경은 없다.
실행: `Saved/Tests/DungeonBossReturn/4092f3e2b7984e6ba5838ebbce00fb27/result.json`;
확정 증거: `Saved/Tests/InventoryRestartLive-29f8c486/Verification.proof.json`.

사용한 실행 명령:

```powershell
.\Scripts\Network\Run-DungeonBossReturnE2ETest.ps1 -InventoryRecovery -InventoryRecoveryRestart `
  -ClientCount 1 -BackendUrl http://127.0.0.1:3047 -Port 18857 -SkipPostgresStart -NoBackendRestore
```

이는 **동일 호스트/배정의 이동 1건·개발 인증·합성 Slate 입력** 검증이다. 다인/지연·손실·
교환/합치기/분할의 실재기동·백엔드 재시작·자동 재접속 UX·호스트 이관·실물 입력·
Steam/Shipping은 별도다. 다음은 나머지 가방 동작의 해당 재기동 경계다.

### 자리 바꾸기의 서버 재기동 복구 연결 (2026-10-25, 두 레코드 실연결 통과)

기존 재기동 모드에 `-InventoryRecoveryOperation Swap`을 추가했다. 기본값은 Move이며
Swap은 `-InventoryRecovery -InventoryRecoveryRestart -ClientCount 1`에서만 허용한다.
두 아이템의 원래 ID/리비전 1/서로 바뀐 0·1번 슬롯과 **처리 수량 0**을 그대로 사용한다.
다른 작업이나 실패 주입 모드로 자동 확장하지 않는다.

- 기존 WBP의 0→1 plain drag가 실제 Swap을 만들고, 프록시는 명시적으로 고정한 두 ID와
  동일 소유자·슬롯·수량/리비전·두 mutation만 허용한다. 두 번째 소유자/ID 변조·mutation
  누락·양수 처리 수량은 upstream 전송 전에 차단하며 전체 frozen 바이트 대조도 유지한다.
- 성공 응답은 두 레코드의 모든 필드를 검사한다. 부분/변조 영수증으로 응답 유실 경계를
  통과하지 않는다. 이동과 마찬가지로 DB 영수증 1건·전체 활성 목록을 고정하며, Swap은
  Move 0/Swap 1/일반 아이템 작업 합계 1을 요구한다.
- 새 서버·클라이언트의 최신 두 레코드(rev 2/서로 교환된 위치)와 원래 요청/잠금을 함께
  기다린다. 복구 버튼 뒤 같은 Swap 영수증으로 두 아이템 모두 조작 가능해져야 성공한다.
  재계획·다른 슬롯/요청 ID·새 권한·부분 완료/강제 잠금 해제는 추가하지 않았다.
- 현재 DLL에 새 Swap probe가 연결되지 않았으면 DB fixture/프로세스 생성 전에 중단한다.
  문자열 존재 검사는 검증 도구의 빌드 선행 조건일 뿐 게임 권한이나 코드 진위 서명이 아니다.

초기 준비 확인: 프록시 12개(기존 Move 8 + 신규 Swap 4), 잘못된 옵션 8종, 링크되지 않은 DLL의
선행 거절, 변경 PS 3개 구문 검사 성공. 변경 C++ 2개의 `-File/-NoLink` 재컴파일도 종료
0/2개 작업 성공이다. split 입력 probe 헤더의 간접 weak pointer include 의존만 명시적
include로 보완했으며 최초 컴파일 실패 로그는 보존했다. 운영 트랜잭션/RPC·저널·API/
스키마·BP/설정은 준비 작업에서 바꾸지 않았다. 당시 사용자 에디터가 실행 중이라 공유 DLL은
교체하지 않았고 새 링크·실제 재기동은 대기 상태였다.
기록: `Saved/Tests/InventorySwapRestart-ca1dbfca/Preparation.proof.json`.

후속 실행에서는 에디터가 종료된 것을 확인하고, 타 작업이 진행하던 전체 Editor 빌드의
453개 작업/`Result: Succeeded`와 새 DLL의 Swap probe 포함을 확인했다. 이번 작업의 자체
빌드/링크로 표시하지 않으며, 해당 공유 DLL을 고정해 아래 모드를 실행했다. 앞 절의 Move
성공이나 초기 NoLink 결과를 새 Swap의 실행 증거로 재사용하지 않았다.

**실행 결과: 재시도 runner 종료 0/Passed=true.** 기존 WBP의 합성 0→1 드래그가 두
아이템의 실제 Swap을 만들고, HTTP 200 응답 유실 후 예전 Projection·두 항목 잠금과
재열기/복구 버튼을 보존했다. 원래 UE 2개만 종료하고 같은 배정/서비스 자격/서버 UserDir·
명시적 저널 경로의 새 UE 2개를 시작했다. 새 클라이언트는 새 UserDir에서 실제 로그인/
새 티켓/Iris 입장 뒤 서로 바뀐 1·0번 칸/rev 2/수량 7·1과 원래 미확정 요청 잠금을 함께
복원했다. 복원 시 자동 replay/새 드래그는 0건이다.

기존 복구 버튼 합성 클릭 1회가 원래 전체 frozen 바이트를 정확히 1회 재전송해
AlreadyApplied를 받고 **두 아이템 모두** 조작 가능해졌다. 처리 수량 0/원래 두 레코드의
전체 영수증 일치, 저널 generation 1→2/명령 해소와 pending/final 사본 해시 일치를 확인했다.
PostgreSQL 전체 영수증 행·활성 가방/기본 장비 불변, Move 0/Swap 1/일반 작업 합계 1과
추가 커밋·리비전 증가 0을 확인했고, 프로세스 정리 후 독립 조회에서도 영수증 행 해시와
커밋 수가 같았다. 선택 입력 53개/DLL·백엔드 어셈블리/닫힌 UE·프록시 6개와 backend 2개
로그, Config 3개/WBP가 전후 동일하다. runtime Error/Fatal/Assert/Ensure 및 backend
fail/fatal 0이며 로딩 경고까지 없다는 뜻은 아니다. 자신의 UE/프록시/백엔드만 정리하고
Docker/기존 PostgreSQL은 유지했다. 이번 후속 작업은 C++/BP/설정·운영 API/스키마 변경이나
지난 native 32개/프록시 12개 반복 없이 실제 통합 경계를 확인했다.

첫 실행은 느린 초기 로딩·heartbeat 401 뒤 클라이언트의 `ResumeIdentityOrStateMismatch`로
**아이템 조작 전에 실패**했다. DB의 Closed/관련 아이템 커밋 0을 확인해 별도 보존했다.
활성 lease 소진은 시간 관측에 따른 추정이며 원인 추적 완료로 계산하지 않는다. 이 실패를
성공으로 승격하거나 lease/인증 규칙을 늘리지 않고, 새 격리 fixture의 재시도가 통과했다.
실패: `Saved/Tests/InventorySwapRestartLive-1341ea57/Failure.proof.json`;
실행: `Saved/Tests/DungeonBossReturn/9f1878fcf5ca43fdabe8208fa66b22c9/result.json`;
확정 증거: `Saved/Tests/InventorySwapRestartLive-1341ea57/Verification.proof.json`.

이는 동일 호스트/배정·개발 인증·합성 Slate 입력의 두 레코드 Swap 복구다. 합치기/분할의
실재기동, 다인·지연/손실·백엔드 재시작·자동 재접속 UX·호스트 이관·실물 입력·Steam/
Shipping은 별도다. 다음 가방 저널 작업은 합치기/분할의 원래 두 레코드·수량·분할 ID가
같은 재기동 경계에서 보존되는지 연결 확인하는 것이다.

```powershell
.\Scripts\Network\Run-DungeonBossReturnE2ETest.ps1 -InventoryRecovery -InventoryRecoveryRestart `
  -InventoryRecoveryOperation Swap -ClientCount 1 -BackendUrl http://127.0.0.1:3051 -Port 18861 `
  -SkipPostgresStart -NoBackendRestore
```

### 완전 합치기의 서버 재기동 복구 연결 (2026-10-26, 두 레코드 실연결 통과)

기존 재기동 도구에 `-InventoryRecoveryOperation Merge`를 추가했다. Move/Swap 기본
동작은 유지하며 Merge는 `-InventoryRecovery -InventoryRecoveryRestart -ClientCount 1`
전용이다. 다른 실패/인증 만료/회수 모드와 혼용하지 않으며, 새 probe가 DLL에 연결되지
않았으면 테스트 DB fixture·프로세스 생성 전에 거절한다. 빌드 문자열 검사는 도구의 선행
조건일 뿐 권한 부여나 코드 진위 서명이 아니다.

- 새 테스트 캐릭터에 기존 배정 서버 권한으로 호환 물약 7·1개를 준비한다. 기존 WBP의
  plain 0→1 드래그가 `Item.StackTransfer` 1건을 만들며, 원래 두 ID·처리 수량 7과 전체
  바이트를 고정한다. 목적 스택은 8/rev 2, 출발 스택은 0/rev 2·Consumed·잠긴 Terminal/
  빈 container ID/-1번 칸이다. 소모 기록을 활성 슬롯이나 빈 GUID 문자열로 바꾸지 않는다.
- 프록시는 명시한 소유자/두 ID·수량/슬롯/리비전·소모 상태/잠금·정의와 두 mutation을
  확인한다. 부활/잠금 해제/수량·소유자·ID·정의 변조/레코드 누락은 upstream 전에 거절하고,
  응답 유실과 정확한 replay 모두 두 레코드의 모든 영수증 필드를 대조한다.
- 새 서버/클라이언트가 원래 요청과 목적 스택 잠금을 복원하고 자동 replay 없이 기존
  복구 버튼을 기다리도록 연결했다. 출발 ID가 Projection에 없는 것은 정상 소모이지 복구
  실패나 새 아이템 생성 사유가 아니다. 버튼 뒤 AlreadyApplied와 목적 스택만의 정상 조작,
  출발 ID의 계속된 부재를 요구한다. 새 RPC/권한 우회/강제 잠금 해제는 추가하지 않았다.
- 활성 목록만으로 소모 기록 보존을 판단하지 않는다. 재기동/수동 복구 전후와 소유 UE
  정리 후에도 출발 Terminal·목적 레코드 전체를 별도 조회하고 원래 영수증 행/전체 활성
  가방·장비/저널 및 Move 0·Swap 0·일반 아이템 작업 합계 1을 검사하도록 연결했다.

확인: 프록시 16개(기존 12 + 신규 Merge 4) 종료 0, 변경 PS 4개 구문 오류 0, 잘못된
옵션 8종·미연결 DLL 선행 거절, 레코드 대조 helper의 정상 소모 1/변조 거절 6종을 확인했다.
운영 C++/RPC·저널·backend API/스키마·BP/설정은 바꾸지 않았으며 Config 3개/WBP는
전후 동일하다. 기존 native 32개나 지난 이동/자리 바꾸기 E2E를 반복하지 않았다.

**초기 준비 당시 C++ 컴파일·새 DLL 링크·실제 Merge 재기동/HTTP/PG/Iris는 미검증**이었다. 선행 격리
빌드 실패의 오류는 원본에서 이미 수정돼 있어 직접 고치지 않았다. 자기 정상 Editor 빌드는
누적 변경 318개 중 13개 작업 뒤 중단해 Incomplete/exit -1로 보존했고, 이후 변경 probe/
호출부 2개 NoLink 시도는 다른 격리 빌드들이 차지한 Batch 대기에서 자기 대기 프로세스만
정리했다. NoLink는 UBT를 시작하지 못했으므로 컴파일 성공으로 계산하지 않는다. 다른
작업의 빌드/소스는 유지했다. 기록: `Saved/Tests/InventoryMergeRestart-45a00e97/Preparation.proof.json`.

후속에서는 새 공유 DLL에 Merge probe가 이미 연결돼 있고 준비한 소스 8개가 같은 것을
확인해 해당 DLL을 재사용했다. 자기 C++ 빌드/링크나 이전 native 32개·프록시 16개를
반복하지 않았다. Docker를 기동해 기존 PostgreSQL을 유지하고 아래 모드를 실행했다.

첫 실행은 느린 초기 로딩·heartbeat 401 뒤 `ResumeIdentityOrStateMismatch`로 입장 전에
실패했다. DB의 Closed·관련 아이템 커밋 0/드래그 0을 확인해 별도 보존했다. 활성 lease
소진은 시간 관측에 따른 추정이며 원인 추적/수정 완료가 아니다. lease/인증을 늘리거나
원래 세션의 자격을 재발급하지 않고 새 격리 테스트 세션으로 한 번 재시도했다.

재시도에서는 실제 WBP 0→1 드래그/커밋 응답 유실·옛 화면 잠금→원래 저널→같은 배정/
서비스 자격/경로의 새 서버·새 클라이언트→실제 로그인/새 티켓/Iris 입장을 관측했다.
출발 ID는 Projection에서 부재하고 목적 8/rev 2와 원래 요청/잠금·기존 버튼을 복원했다.
복원 중 자동 replay 0, 버튼 합성 클릭 1→원래 frozen 바이트 replay 1/AlreadyApplied와
목적 잠금 해제·출발 소모 유지, 저널 세대 1→2/명령 0을 확인했다. 러너의 전체 레코드/
활성 목록·영수증 불변 검사도 소스 검사 지점 전까지 통과했다. 종료 후 독립 DB 조회에서도
출발 0/rev 2·Consumed·잠긴 Terminal/빈 container ID/-1번 칸, 목적 8/rev 2·활성/잠금
없음, Move 0·Swap 0·일반 아이템 커밋 1과 원래 전체 영수증 행 해시 일치를 확인했다.
pending/final 사본·동일 live 저널 해시, DLL·Config 3개/WBP가 같고 닫힌 UE/프록시 6개와
backend 2개의 runtime/backend Error/Fatal/Assert/Ensure는 0이다. 로딩 경고까지 없다는 뜻은 아니다.

**전체 러너는 exit 1이며 현재 소스 기준 최종 PASS가 아니다.** 성공 동작 후 최종 소스
무결성 검사에서 입력 53개 중 `Public/Manager/HttpWebManager.h`와
`Private/Component/UI/QuickSlotComponent.cpp`의 동시 수정을 확인했다(경로는
`Source/Project_RPG/` 기준). DLL은 실행 전후 같지만 현재 소스와 대응하는 통과 증거를
확정할 수 없어 `result.json`이 생성되지 않았다. 다른 작업을 되돌리거나 무결성 검사를
우회하지 않았으며 두 실행의 실패를 성공으로 승격하지 않는다. 자기 UE/프록시/백엔드는
정리하고 Docker/기존 PostgreSQL과 다른 작업은 유지했다.

입장 실패: `Saved/Tests/InventoryMergeRestartLive-a7cf4a78/Failure.proof.json`;
재시도: `Saved/Tests/DungeonBossReturn/cacddf3e7fab495db80c89077cba0228/failure.json`;
복구 관측/동시 수정 기록: `Saved/Tests/InventoryMergeRestartLive-a7cf4a78/RecoveryObservation.proof.json`.
2026-10-26에는 새 공유 DLL `BB5A5642…`와 준비한 복구 코드 8개 불변을 확인해 자체
빌드/기존 native·프록시 반복 없이 한 번 재개했다. 서버 준비는 04:31:35 UTC였지만
멤버 lease는 04:31:14 UTC에 끝났고, 직후 배정 조회가 Closed여서 **입장/아이템 fixture 전
exit 1·관련 커밋 0**으로 종료했다. 초기 기동 지연과 만료의 시간 관계만 확인했으며
전체 원인 추적/수정 완료나 Merge 복구 성공을 뜻하지 않는다. 실행 중 인증 관리자
두 파일/로비 헤더·DefaultGame도 바뀌었다. 원본과 실패를 유지하고 자기 프로세스는
러너가 정리했으며 사용자 에디터/Docker/기존 PG는 보존했다. 증거:
`Saved/Tests/InventoryMergeRestartFinal-e8a5f6ad/Failure.proof.json`.

같은 날 후속에서는 에디터가 닫혀 있었고 공유 정상 Editor 빌드의 **263개 작업/전체 타깃
성공**을 기다린 뒤 새 DLL `6428E480…`을 재사용했다. 자체 UBT 빌드나 기존 native/
프록시·다른 세 동작의 반복은 없다. 첫 실행은 느린 서버/클라이언트 초기화 뒤 입장 전에
`ResumeIdentityOrStateMismatch`로 실패했다. DB Closed·관련 커밋 0과 소스 53개/DLL 불변을
확인해 `Saved/Tests/InventoryMergeRestartComplete-66d576f7/Attempt1Failure.proof.json`에
보존했다. 원래 실패를 성공으로 승격하지 않았으며, 초기 로딩 지연과 lease 만료의 시간
관계만 확인했지 운영 초기 부팅 원인 해결을 주장하지 않는다.

다른 UE 검증 프로세스 종료 뒤 새 격리 세션으로 한 번 재시도해 **러너 exit 0/Passed=true**를
확인했다. 실제 WBP 0→1 드래그의 7+1 합치기/응답 유실·원래 요청/예전 화면 잠금→서버
저널 선저장→동일 배정/서비스 자격/서버 UserDir·저널의 새 서버와 새 UserDir 클라이언트→
실제 로그인/새 티켓/Iris 입장→목적 8/rev 2와 원래 요청/잠금 복원이다. 자동 replay는 0이며
기존 복구 버튼의 합성 클릭 1회가 동일 frozen 바이트 1회/AlreadyApplied·목적 잠금 해제를
만들었다. 출발 ID는 계속 Projection에서 없고 DB에는 0/rev 2·Consumed/잠긴 Terminal/
빈 container ID/-1로 남았다. 목적은 8/rev 2·활성 Inventory 1번 칸/잠금 없음이다.

전체 영수증 행 SHA/활성 가방·시작 장비 보존과 Move 0·Swap 0·일반 아이템 커밋 합계 1,
추가 ID·리비전 증가 0을 확인했다. 자기 프로세스 정리 후 독립 DB 조회에서도 같은 전체
영수증 행 SHA와 두 레코드의 소모/수량/위치/잠금·버전이 유지됐다. 저널 세대 1→2/명령 0,
pending/resolved 사본과 원래 live 파일 해시, 입력 53개/DLL·backend 어셈블리/닫힌 로그
8개·Config 3개/WBP 일치와 runtime/backend 진단 오류 0을 확인했다. 경고까지 0이라는
뜻은 아니다. 이 작업에서 운영 C++/BP/설정/API/스키마는 변경하지 않았고 자기 프로세스만
정리했으며 기동한 Docker/기존 PG와 다른 작업은 유지했다. lease/인증·원래 자격을 늘리거나
재발급하지 않았고 소스 검사를 우회하지 않았다.

확정 증거: `Saved/Tests/InventoryMergeRestartComplete-66d576f7/Verification.proof.json`;
실행: `Saved/Tests/DungeonBossReturn/adb28e1274434d558315c12e66a900f2/result.json`.
네 가방 명령의 기본 1인 재기동 복구는 각 실행 범위로 완료했지만, 하나의 최신 DLL로
네 동작을 모두 다시 검증한 결과는 아니다. 부분 합치기·다인/지연/손실·자동 재접속 UX·
호스트 이관/fencing·우편/소비/장착 복구·Steam/Shipping·실물 입력은 별도다.

```powershell
.\Scripts\Network\Run-DungeonBossReturnE2ETest.ps1 -InventoryRecovery -InventoryRecoveryRestart `
  -InventoryRecoveryOperation Merge -ClientCount 1 -BackendUrl http://127.0.0.1:3053 -Port 18863 `
  -SkipPostgresStart -NoBackendRestore
```

### 분할의 서버 재기동 복구 연결 (2026-10-25, 원래 생성 ID 실연결 통과)

기존 재기동 도구에 `-InventoryRecoveryOperation Split`을 추가했다. 단독 1인
`-InventoryRecovery -InventoryRecoveryRestart`에서만 허용하고 다른 실패/인증 만료/회수/
분할 fixture와 혼용하지 않는다. 새 Split probe가 DLL에 없으면 DB fixture·프로세스를
만들기 전에 거절한다. 빌드 문자열은 도구의 선행 조건일 뿐 인증/권한 검사가 아니다.

- 기존 WBP의 Ctrl 0→3 드래그·실제 수량 입력의 Slate 문자 경로로 `2`를 입력하고 기존
  Split 버튼을 누르도록 연결했다. 위젯/Record/Projection을 직접 설정하거나 RPC를 직접
  호출하지 않는다. 원본 7→5/rev 2와 새 스택 2/rev 1, 무관한 기존 1개 항목/기본 장비를
  함께 검사한다. Shift 절반 분할의 동작은 변경하지 않았다.
- 생성 ID는 기존 `MakeSplitItemId(RequestId, SourceItemId)`로 도출한 바로 그 값이어야
  한다. 관측 probe는 같은 순수 함수를 사용하며 생성/소유 권한을 부여하지 않는다. 저널의
  원래 ID/전체 wire 바이트를 재사용하고 재시도마다 새 ID나 다른 슬롯을 선택하지 않는다.
- 프록시는 명시된 소유자/원본·생성 ID의 형식/중복·수량/빈 3번 칸을 검사한다. 원본은
  expected/new revision 1, 새 스택은 0이며, 성공 영수증은 각각 2/1이어야 한다. 두 레코드의
  ID·수량·리비전·슬롯을 제외한 모든 필드가 같아야 하며, 재시도의 생성 ID 교체·소유자/
  메타데이터/수량 변조·부분 영수증·새 스택의 잘못된 리비전을 성공으로 계산하지 않는다.
- 러너는 원본 seed 전체를 기준으로 생성 레코드도 비교한다. 같은 배정/서비스 자격/저널
  경로의 새 서버와 새 로그인/티켓 클라이언트가 원래 요청과 정확한 생성 ID·두 스택 잠금을
  복원하고, 자동 replay 없이 기존 복구 버튼을 기다리도록 연결했다. 버튼 뒤 동일 요청의
  AlreadyApplied/양쪽 조작 가능·전체 영수증/가방·장비/무관한 항목 불변·커밋 합계 1/
  원본+새 스택 총수량 7·저널 한 세대 해소를 확인해야 통과한다.

확인: 프록시 20개(기존 16 + 신규 Split 4) 종료 0, 변경 PS 4개 구문 오류 0, 잘못된
옵션 9종/미연결 DLL 선행 거절, 생성 레코드 비교 helper의 정상 1/변조 거절 6종을 확인했다.
자체 `-File/-NoLink`로 변경 복구 probe와 호출부 2개 TU를 컴파일해 2개 작업/종료 0/
`Result: Succeeded`를 확인했다. 다른 성장 기능의 빌드를 유지한 채 기다린 뒤 실행했으며
그 빌드의 넉백 헤더 오류를 직접 수정하지 않았다. 이전 native 32개나 이동/교환/합치기의
HTTP/PG 시나리오는 반복하지 않았고 운영 C++/RPC·저널·API/스키마·BP/설정은 바꾸지 않았다.
Config 3개/WBP는 전후 같았다.

초기 준비 당시 새 DLL 링크·전체 Editor 타깃·실제 Split 재기동/HTTP/PG/Iris·수량 UI 입력은
미검증이었다. NoLink 결과를 실행 성공으로 확대하지 않았고 새 연결 전 실행 거절도 유지했다.
기록: `Saved/Tests/InventorySplitRestart-35580bd0/Preparation.proof.json`.

후속에서는 에디터/다른 UE 빌드가 종료된 것을 확인해 자체 Editor 전체 타깃 37개 작업/
링크를 성공했다. 첫 실행은 실제 분할 재기동 복구 동작까지 관측했으나 실행 도중
`HttpWebManager.h/.cpp`가 수정돼 최종 소스 무결성 검사에서 exit 1이 됐다. 이 기록은
`Saved/Tests/InventorySplitRestartLive-8b19f134/Attempt1Observation.proof.json`에 보존했고
성공으로 승격하지 않았다. 두 파일의 추가 변경이 멈춘 것을 확인한 뒤 최신 인증 코드를
포함해 자체 Editor 전체 타깃 102개 작업/링크를 다시 성공했고 같은 모드를 재실행했다.

**최종 실행은 runner 종료 0/Passed=true.** 기존 WBP의 Ctrl 0→3 드래그·Slate 수량 `2`
입력/확인이 실제 Split을 만들었다. 커밋 응답 유실 후 옛 원본 7개/투영·잠금·재열기와
기존 복구 버튼을 확인하고 원래 서버 저널/단일 DB 커밋을 대조했다. 자기 UE 2개를 종료한
뒤 같은 배정/서비스 자격/서버 UserDir·저널 경로의 새 서버와 새 UserDir 클라이언트 2개를
기동했다. 실제 로그인/새 티켓/Iris 입장 뒤 원본 5/rev 2·원래 생성 ID의 2/rev 1과 원래
요청·두 스택 잠금/복구 버튼을 함께 복원했다. 복원 중 자동 replay/새 드래그는 0건이다.

복구 버튼 합성 클릭 1회가 원래 전체 frozen 바이트를 1회 재전송해 AlreadyApplied를 받고
양쪽 조작이 가능해졌다. 원래 생성 ID·두 레코드 전체 영수증/활성 가방·장비/무관한 항목
보존과 저널 generation 1→2/명령 해소를 확인했다. 프로세스 정리 후 독립 DB 조회도 원본
5/rev 2·0번 칸/활성/잠금 없음, 새 ID 2/rev 1·3번 칸/활성/잠금 없음, Item.Split 영수증
정확히 1건·다른 일반 아이템 커밋 0과 원래 전체 영수증 행 해시 일치다. 새 ID 재발급이나
추가 커밋·리비전 증가는 없었다.

입력 소스 54개/DLL·backend 어셈블리·pending/final 저널 사본과 live 해소 파일, Config
3개/WBP가 전후 같았다. 닫힌 UE/프록시 6개와 backend 2개 로그의 runtime/backend
Error/Fatal/Assert/Ensure는 0이며 로딩 경고까지 없다는 뜻은 아니다. 자기 UE/프록시/
백엔드만 정리하고 기존 Docker/PostgreSQL과 다른 작업은 유지했다. 이번 후속은 운영
C++/RPC·저널/API/스키마·BP/설정을 직접 수정하거나 과거 native 32개/프록시 20개를
반복하지 않고 빌드·실통합에 한정했다.
실행: `Saved/Tests/DungeonBossReturn/8a7f2f41b16c4b0d81966c3922da6db3/result.json`;
확정 증거: `Saved/Tests/InventorySplitRestartLive-8b19f134/Verification.proof.json`.

전달 직전에는 결과 파일 작성 **후** 로비 UI 헤더/CPP와 `Config/DefaultEngine.ini`가
수정된 것도 확인했다. 각 수정 시각이 성공 결과 저장 뒤임을 별도 기록했으므로 위 실행의
PASS는 유지하되, 이 후속 소스/설정까지 같은 DLL에서 검증됐다고 표시하지 않는다.

이는 동일 호스트/배정·개발 인증·합성 Slate 입력의 분할 1건이다. 다인/지연·손실·백엔드
재시작·자동 재접속 UX·호스트 이관·실물 입력·Steam/Shipping은 별도다. 다음은 앞 절의
완전 합치기 최종 소스 정합 확인이며, 서로 다른 실행 DLL의 이동/교환/분할 증거를
네 가방 동작 모두의 같은 최신 DLL 회귀로 확대하지 않는다.

```powershell
.\Scripts\Network\Run-DungeonBossReturnE2ETest.ps1 -InventoryRecovery -InventoryRecoveryRestart `
  -InventoryRecoveryOperation Split -ClientCount 1 -BackendUrl http://127.0.0.1:3055 -Port 18865 `
  -SkipPostgresStart -NoBackendRestore
```

### 가방의 같은 스택 합치기 (2026-10-21)

같은 종류의 스택 가능 아이템을 겹쳐 놓으면 `Item.StackTransfer`로 목적 스택의
최대 수량까지만 합친다. 초과분은 출발 슬롯에 남고, 전량 이전된 출발 아이템은 같은 ID의
Consumed/Terminal 기록으로 남는다. 목적 ID/슬롯은 유지하며 새 아이템을 발급하지 않는다.
다른 종류나 스택 불가 장비는 기존 자리 바꾸기를 사용한다. 같은 스택이 가득 찼거나
속성이 다르면 드롭을 거절하며 자리 바꾸기로 조용히 전환하지 않는다.

- 기존 native 셀을 부모로 사용하는 WBP에도 드래그 경로가 적용된다. 커스텀 BP는
  ViewModel의 `RequestMergeItems(SourceItemId, ExpectedSourceRevision, DestinationItemId,
  ExpectedDestinationRevision, OutRequestId)`를 호출한다. false이면 요청이 제출되지 않았다.
  기존 이름/수량/결과 delegate와 셀 tooltip을 재사용하며 BP 에셋을 다시 저장하지 않았다.
- `ServerMergeInventoryItems`는 BlueprintCallable owning-client RPC다. 소유자/가방 ID는
  인증된 서버 상태에서 도출하고 수량은 고정된 merge-all 의도로 처리한다. 클라이언트가
  소유권·수량·최대 스택·Record를 정하지 못한다. RequestId와 양쪽 int64 Revision을 고정한다.
- 서버 planner는 양쪽 소유권·Active·잠금·만료·Revision·현재 캐릭터의 가방/0~79 슬롯과
  정의 ID/버전/최대 수량을 검사한다. 귀속·내구도·만료 시각·인스턴스 태그·롤 스탯이 달라도
  거절한다. 생성 시드/출처는 각 원래 Record의 감사 정보로 유지하며 만료/귀속을 세탁하지 않는다.
- 출발 감소/소비와 목적 증가를 하나의 기존 비동기 CAS Commit에 넣고 수량을 보존한다.
  백엔드 API/스키마·인증·정산 권한은 변경하지 않았다. 기존 영수증/Projection 반영 경로를 사용한다.
- UI는 두 ID/슬롯을 기존 이중 예약으로 잠그고 낙관적으로 위치나 수량을 바꾸지 않는다.
  성공 RPC 또는 한쪽 갱신만으로 예약을 풀지 않으며, 양쪽 새 Revision/제거를 기다린다.
  명시적 실패는 해당 예약을 해제한다. 전량 소비된 출발 ID의 퀵슬롯은 기존 소진 정리 경로를
  따르며 목적 ID로 자동 재지정하지 않는다. 부분 잔량과 목적 ID의 기존 참조는 유지한다.
- 같은 완료 요청의 재시도는 기존 orchestrator의 제한된 완료 캐시/영수증 경로를 사용한다.
  새 자동 재시도·프로세스 재시작 복구·스택 분할·전체 자동 정렬은 추가하지 않았다.

집중 검증: `Scripts/Tests/Run-ItemAutomationTest.ps1 -StackMergeOnly`.
신규 스택 planner/CAS·권한/메타데이터·비동기 재시도·드롭 선택·이중 예약 5개와 기존
비동기/셀 조작·표시 6개, 총 11/11 성공·러너 종료 0을 확인했다. 선택 입력 28개와 실행 DLL은
전후/전달 시점에 일치했다. 기존 기동 Error 15줄과 구분해 테스트 시작 이후 Error 및
Fatal/Assert/Ensure는 0건이다. 초기 자체 Editor 빌드는 공유 던전 probe의 C2181로 실패했고,
그 파일이 다른 작업에서 이미 수정된 것을 확인해 중복 수정하지 않았다. 이후 공유 링크된
DLL에서 최종 Editor 타깃은 0개 작업/up-to-date 성공이며 독립적인 자체 전체 링크라고
표시하지 않는다. 최초 실패 로그와 native proof를 보존했다.

증거: `Saved/Tests/StackMerge-28c40bd1/Verification.proof.json`.
이 단계 당시 실제 HTTP/PostgreSQL·Iris 드래그·물리 입력·Steam·Shipping은 미검증이었다.
실제 가방/저장/복제 연결은 아래 2026-10-22 후속 검증에서 구분해 확인했다.

### 실제 WBP 스택 드래그와 PostgreSQL/Iris 연결 (2026-10-22)

`Scripts/Network/Run-DungeonBossReturnE2ETest.ps1 -InventoryStackMerge -ClientCount 1`은
기존 개발 인증→소유 캐릭터/세션 복원→실제 Join Ticket→dedicated/Iris 입장을 재사용한다.
전투 시작 gate는 열지 않고 가방 조작만 검사한다. 다른 전투/포기/재인증 모드와 함께
실행하거나 임의 DB를 대상으로 실행할 수 없다. 잘못된 옵션 3개를 사전 거절했다.

- 할당 서버의 기존 Commit 권한으로 새 테스트 캐릭터에 물약 7/8/3개를 준비한다.
  UE에 토큰·신원·Ready·아이템 Record를 주입하지 않는다. 클라이언트는 Controller의
  `ShowInventoryProjection`으로 저장된 WBP를 열고 실제 셀의 hit-test 경로를 확인한다.
  Slate 좌클릭/이동/드롭을 사용하며 셀의 NativeOnDrop/ViewModel/RPC를 직접 호출하지 않는다.
- 첫 드래그는 0→1 슬롯: `7+8 → 5+10`, 양쪽 Revision 1→2와 나머지 3개 보존.
  열린 ViewModel/셀/퀵슬롯을 확인한 뒤, 러너가 실제 HTTP 영수증·DB 레코드를 읽어
  검증해야 두 번째 드래그를 허용한다. UI의 낙관적 수량 변경은 없다.
- 두 번째는 0→2 슬롯: `5+3 → 0+8`. 출발 ID는 수량 0/Revision 3의 잠긴
  Consumed/Terminal 기록, 목적 ID는 수량 8/Revision 2로 유지된다. 기존 가득 찬 스택은
  10/Revision 2 그대로다. 출발 퀵슬롯만 해제되고 다른 두 참조는 유지된다.
- 세 번째는 8개 스택→가득 찬 10개 스택: 드롭 거절, 자리 바꾸기·추가 Commit 없음.
  PostgreSQL의 해당 캐릭터 `Item.StackTransfer` 영수증은 정확히 2건(수량 2/5)이며,
  활성 물약 총수량 18개·원래 영수증 불변·시작 대검의 모든 Record 필드를 보존했다.

자체 Editor 증분 35개 작업·게임/Editor 모듈 링크·최종 타깃 성공 뒤 같은 DLL에서
러너 종료 0을 확인했다. 선택 입력 37개(WBP 포함)/모듈 해시는 전후·전달 시점에 같고,
소유 UE 프로세스 종료 후 해시한 서버/클라이언트 로그의 Error/Fatal/Assert/Ensure는 0이다.
기존 native 11개는 이번에 반복하지 않았다. 검증 전용 코드/러너만 추가했으며 운영 아이템
코드·BP 에셋·백엔드 API/스키마·권한/lease/정산 규칙은 변경하지 않았다.

초기 Pawn 대신 Controller에서 퀵슬롯을 찾던 probe 실패와 후속 러너 실패 2회를 보존했다.
마지막 재현의 구조화된 실패 기록은 소비 아이템의 잠금을 잘못 거절한 조건을 가리켰다.
기존 `TryMarkConsumed`는 재사용 방지를 위해 tombstone을 잠근다. 이 규칙을 바꾸지 않고
러너가 잠긴 tombstone을 요구하도록 고쳤으며, 잠금 해제된 tombstone 거절도 별도 검사했다.
실패 실행의 UI/읽기 전용 DB 관측을 최종 러너 PASS로 소급 계산하지 않았다.

proof: `Saved/Tests/StackMergeLive-7c29a501/Verification.proof.json`.
최종 실행: `Saved/Tests/DungeonBossReturn/1876926308db4070be979a8a36e71ecc/result.json`.
이는 GPU offscreen의 합성 Slate 입력·로컬 개발 인증 1인 검증이다. 실물 입력/육안 배치,
다인·지연/손실·프로세스 재시작 복구·Steam·Shipping은 별도다. 당시 다음 기능 후보였던
스택 분할의 구현/native 범위는 다음 절에 기록한다. 이 합치기 결과를 분할이나 빈 슬롯
이동/자리 바꾸기의 실제 드래그 성공으로 확대하지 않는다.

### Shift 드래그 스택 분할 (2026-10-22)

기존 가방 셀에서 **Shift를 누른 채 드래그를 시작해 빈 칸에 놓으면** 캡처한 수량의
절반(내림)을 `Item.Split`으로 분리한다. 7개는 원본 4개/새 스택 3개가 된다. 1개짜리는
분할 드래그를 만들지 않으며 점유된 칸·장비·퀵슬롯에는 분할할 수 없다. 부적절한 목적지에서
일반 이동·합치기·자리 바꾸기·퀵슬롯 등록으로 전환하지 않는다. 일반 드래그 동작은 유지한다.

커스텀 BP는 기존 Inventory ViewModel에서 다음 함수를 사용한다.

- `CanSplitItem(ItemId, ExpectedRevision, InventorySlotIndex, Quantity)`는 제출 가능 여부를 읽는다.
- `RequestSplitItem(ItemId, ExpectedRevision, InventorySlotIndex, Quantity, OutRequestId)`는
  지정 수량을 제출한다. true는 요청 제출이며 저장 성공이 아니다. 기존 완료 delegate와
  authoritative InventoryItems 갱신을 사용한다. 수량은 **1 이상/현재 수량 미만**이어야 한다.
- 기대 Revision은 선택한 projection 항목에서 가져온다. 드래그는 시작 시 Revision/절반 수량을
  고정하고 변경된 아이템을 최신 Revision으로 바꿔서 재요청하지 않는다. 드래그 시각 요소와
  tooltip 안내는 native 셀에 적용되므로 기존 WBP 에셋 재저장이 필요 없다.

서버의 BlueprintCallable `ServerSplitInventoryItem`은 현재 인증된 캐릭터의 소유자/가방을
도출한다. 클라이언트는 새 ID·정의·스탯·귀속·Record를 제출할 수 없다. 서버 planner는
소유권/Active/잠금/만료/Revision·같은 가방과 0~79 슬롯·빈 목적지·정의 ID/버전/스택 상한을
검사한다. 수량 감소와 생성은 하나의 CAS Commit이며, 원본은 기존 ExpectedRevision,
새 스택은 expected/new revision 0으로 제출하고 저장소가 각각 다음 Revision/1을 부여한다.

새 ID는 버전 있는 namespace와 원본 ID·RequestId에서 결정적으로 도출한다. 같은 요청의
전송 재시도에서 매번 새 ID를 뽑지 않는다. 기존 ID(소비 tombstone 포함) 충돌은 거절하고
다른 ID/목적지로 재발급하지 않는다. 원본 ID/슬롯/퀵슬롯 참조는 유지하며 두 스택의 총수량,
정의/버전·시드·태그/롤 스탯·귀속·내구도·만료·생성 출처를 보존한다. 새 퀵슬롯을 자동 등록하거나
소모품의 사용 효과를 실행하지 않는다. 새 ID를 예측할 수 있는 것은 소유권/지급 권한이 아니다.

UI는 원본 ID/출발 칸과 예상 새 ID/목적지를 예약한다. 성공 ACK 또는 원본 갱신만으로
예약을 풀지 않고, **정확한 새 ID가 목적지에 복제되고 원본 Revision도 갱신될 때** 해제한다.
새 스택이 먼저 와도 원본을 기다리고, 같은 목적지의 다른 ID만으로는 해제하지 않는다.
명시적 실패는 해당 요청의 예약을 해제한다. 화면 수량/위치를 낙관적으로 쓰지 않는다.

집중 검사: `Scripts/Tests/Run-ItemAutomationTest.ps1 -StackSplitOnly`.
분할 planner/메타데이터·CAS·비동기 재실행·이중 예약·native Shift 입력과 직접 영향
합치기/비동기/퀵슬롯 검사를 선택한다. 자체 Editor 재빌드 40개 작업은 모듈 링크/타깃
생성까지 종료 0이고, 선택 검사 **16개(분할 6 + 직접 회귀 10)**가 모두 통과했다.
검사 중 선택 소스 35개/DLL 불변과 종료 후 해시 일치를 확인했다. 테스트 시작 전 Error
15개는 별도이며 테스트 중/이후 Error 0, Fatal/Assert/Ensure 0이다. native 입력 검사는
그리지 않는 transient 셀의 핸들러 호출이며 인증 없는 RPC 거절 fixture를 포함한다.
이는 실제 인증·렌더링·DB 성공을 입증하지 않는다. 최초 빌드의 별도 키 설정 함수
`MatchesKeyBinding` 링크 실패도 보존했고, 현재 소스에 구현이 있음을 확인한 뒤 해당
설정 파일을 수정하지 않고 재빌드했다.
proof: `Saved/Tests/StackSplit-53ab901e/Verification.proof.json`.
native proof: `Saved/Tests/Item/Automation-105255881c4941be8c70ac21fa6a8205/Item.proof.json`.
수량 선택 팝업·자동 재시도/재시작 복구·가방 확장·일괄 정렬·월드 버리기는 추가하지 않았다.
이 native 단계만으로 실제 렌더링된 Shift 입력/HTTP/PostgreSQL/Iris 분할을 입증하지 않는다.
후속 로컬 연동 결과는 다음 절에 기록한다. 실물 입력·Steam·Shipping은 별도다.

### Shift 분할의 로컬 화면 연동 경로 (2026-10-22)

`Scripts/Network/Run-DungeonBossReturnE2ETest.ps1 -InventoryStackSplit -ClientCount 1`을
추가했다. 코드/Editor 빌드 뒤 Docker 대기를 해소하고, **실행 버전 기준 실제 WBP/
HTTP/PostgreSQL/Iris 분할의 전용 러너 종료 0**을 확인했다. 아래는 로컬 개발 인증
1인·GPU offscreen 합성 Slate 입력에서 관측한 범위다.

- 기존 개발 인증/소유 캐릭터/세션 복원과 실제 Join Ticket 입장을 사용한다. 서버의 기존
  권한으로 새 테스트 캐릭터에 물약 7개만 준비하며 클라이언트 토큰/신원/Record/Ready를
  주입하지 않는다. 전투 gate는 열지 않고 일반 정산·권한·lease를 변경하지 않는다.
- 저장된 가방 WBP와 실제 셀의 hit-test 경로에 Shift 수정 키를 가진 Slate 드래그를 전달한다.
  셀의 NativeOnDrop, ViewModel 요청 또는 RPC를 직접 호출해 분할하지 않는다.
  0→3 칸 드롭으로 7→4+3, 원본 rev 1→2/새 ID rev 1과 셀/ViewModel/Projection을 확인한다.
- 요청 ID에서 도출한 정확한 새 ID, 원본 퀵슬롯 유지/새 퀵슬롯 미등록, 두 아이템의
  공개 UI 명령 가능 여부 회복을 확인한 뒤 점유된 3번 칸에 다시 Shift 드롭한다.
  합치기/자리 바꾸기/이동/소비로 전환하거나 새 명령을 발급하면 실패한다.
- 기존 HTTP 조회와 PostgreSQL의 생성 캐릭터 범위 조회로 두 레코드 영수증 1건,
  총수량 7/원본 ID·메타데이터·시작 장비 보존과 다른 동작 영수증 없음/영수증 불변을
  검사한다. 입력/WBP/DLL 해시와 자체 종료한 UE 로그를 보존한다.
- 합치기와 동시 선택, 다인 또는 다른 시나리오 병용, 지정된 로컬 개발 DB 외 연결은
  시작 전에 거절한다. 직접 확인한 잘못된 조합 3개와 스크립트 문법 검사는 통과했다.

신규 probe의 최초 private 함수 조회 컴파일 실패를 보존하고, 운영 API를 공개하거나
friend를 추가하지 않고 공개 `CanSplitItem`/`CanMoveItemToSlot`을 쓰도록 수정했다.
자체 증분 재빌드 4개 작업/모듈 링크/Editor 타깃은 종료 0이다. 전체 DTO 비교 helper는
원본 잔여/새 스택 수용과 변조 메타데이터 거절을 확인했다. 기존 native 16개는 반복하지
않았고 당시 선택 소스 35개가 현재와 같은 것만 확인했다. 새 DLL의 native 실행 결과로
소급하지 않는다. 운영 아이템/BP 에셋/API/스키마는 이번에 변경하지 않았다.
proof: `Saved/Tests/StackSplitLive-6a18c0e2/Verification.proof.json`.

최종 실행은 `Saved/Tests/DungeonBossReturn/f0061a59252d487192c7fcf13f0a15e6/result.json`이다.
두 레코드를 함께 저장한 `Item.Split` 영수증은 정확히 1건(분리 수량 3), 원본 4/새 스택 3으로
총수량 7을 유지했다. 귀속/내구도/시드/태그/롤/만료/생성 출처를 포함한 전체 DTO와 시작
장비를 비교했고, 출발 퀵슬롯 유지/새 퀵슬롯 미등록·두 아이템의 조작 잠금 해제를 확인했다.
두 번째 Shift 드롭은 점유된 칸에서 거절됐고 Move/Swap/StackTransfer/Consume 영수증이
생기지 않았다. 기존 영수증은 변하지 않았으며 전투 gate와 정산은 시작하지 않았다.

실행 중 선택 입력 41개(WBP 포함)/DLL 일치, 닫힌 서버·클라이언트 로그 2개 해시와
Error/Fatal/Assert/Ensure 0, 자체 UE 프로세스 2개 종료를 확인했다. 최초 실행
`c9f1073b178a40dda8ae10943d400dd4`는 같은 기능/DB 관측 뒤 공용 입장 probe 두 파일의
동시 수정 때문에 소스 일치 검사에서 실패했다. 실패 결과를 보존하고, 변경을 되돌리지
않은 채 공유 새 DLL `54FE895C…`에서 전용 모드만 다시 실행했다. 이번 작업에서는
재빌드/전체 native 반복·운영 아이템/BP/API/스키마·권한/lease 변경을 하지 않았다.

최종 PASS 기록 뒤 공용 `RPGDungeonFlowE2EProbe.h/.cpp`가 다시 수정되고 공유 DLL도 교체됐다. 결과 파일
기록 시각 14:28:03 UTC보다 두 파일 수정 시각 14:28:50 UTC가 늦다. 이는 **실행한
버전의 성공**이며 이후 수정된 현재 전체 소스 트리/새 공유 DLL의 성공으로 소급하지 않는다.
실행 후 변경 2개를 보존하고 추가 반복 실행은 하지 않았다.
통합 proof: `Saved/Tests/StackSplitNetwork-5e7f28aa/Verification.proof.json`.

다음 기능 작업에서는 이미 제공한 BP 지정 수량 요청에 수량 선택 UI를 연결할 수 있다.
실물 입력·다인/지연·프로세스 재시작·Steam/Shipping과 현재 공용 probe 후속 수정의
검증은 이번 완료 범위가 아니다.

### 지정 수량 분할 입력 (2026-10-23)

기존 **Shift+드래그의 절반 분할**은 유지하고, **Ctrl+드래그로 빈 가방 칸에 놓으면**
가방 안에 수량 입력/확인/취소 영역을 연다. Ctrl+Shift도 수량 선택으로 처리한다.
일반 우클릭 사용/장착·일반 드래그는 바꾸지 않는다. 점유 칸·장비·퀵슬롯이나 1개짜리
아이템은 수량 선택 대신 다른 동작으로 전환하지 않는다.

- 열기/타이핑/취소는 아이템 RPC·슬롯 예약·낙관적 수량 변경을 만들지 않는다.
  Enter 또는 Split 버튼 확인 시에만 기존 ViewModel `RequestSplitItem`을 호출한다.
  true는 요청 제출이며 저장 성공이 아니다. 확인 전에 선택을 닫아 중복 확인을 막는다.
- 드래그의 ID/전체 int64 Revision/목적지를 유지하고 원본 수량을 캡처한다. 소유 Projection의
  원본/Revision/수량·빈 목적지·명령 가능 상태가 달라지면 선택을 취소한다. 최신 Revision이나
  다른 빈 칸으로 자동 재요청하지 않는다. 가방 비활성화/해제와 Esc/Cancel도 요청 없이 닫는다.
- 입력은 1 이상/원본 수량 미만의 ASCII 십진 정수(최대 10자리)다. 음수·부호·소수·지수·
  구분자·공백·범위 초과는 확인 불가다. float 슬라이더/반올림/수량 자동 보정은 사용하지 않는다.
- 기존 WBP의 가방 grid와 상태/명령 결과 VerticalBox에 native 영역을 붙이며 에셋을 재저장하지
  않는다. 셀의 Border/Label 구조는 유지한다. 지원하지 않는 커스텀 BP 배치는 실패로 끝내며
  전체 스택 이동/절반 분할을 대신 보내지 않는다. 예전 UObject 아이템용 팝업은 재사용하지 않는다.
- BP에서 `OpenSplitQuantitySelection(ItemId, ExpectedRevision, DestinationSlot)`,
  `CanConfirmSplitQuantitySelection`, `ConfirmSplitQuantitySelection(OutRequestId)`,
  `CancelSplitQuantitySelection`, `IsSplitQuantitySelectionOpen`을 사용할 수 있다.
  서버 권한/소유권·정의 상한·CAS/멱등성 규칙과 API/스키마는 바꾸지 않는다.

집중 검사 경로는 `Scripts/Tests/Run-ItemAutomationTest.ps1 -SplitQuantityOnly`다.
신규 입력/취소/무효화/정밀도/Ctrl 핸들러/WBP 템플릿과 직접 영향 Shift 분할·기본 상호작용·
퀵슬롯 검사를 선택한다. 신규 6개+직접 영향 기존 13개 **19/19 native 통과**, 선택 입력
38개(WBP 포함)/DLL 전후 불변과 종료 후 해시 일치를 확인했다. 실제 WBP의 read-only
템플릿에서 grid와 입력 영역을 붙일 VerticalBox도 확인했다. 입력/취소 검사는 그리지 않는
transient 위젯이며 제출 fixture는 인증 없는 RPC 거절까지다. 실제 인증/저장 성공으로
계산하지 않는다. 테스트 시작 전 Error 15개는 별도이고 테스트 중/이후 Error 0,
Fatal/Assert/Ensure 0이다.

첫 자체 빌드는 다른 Build.bat의 종료를 기다린 뒤 위젯의 지역 `Slot`이 상속 멤버를
가리는 C4458로 실패했다. 현재 파일은 이미 다른 작업에서 `DestinationSlot`으로
수정돼 있어 중복 변경하지 않았다. 공유 링크된 DLL `FA3B54A0…`에서 Editor 타깃
up-to-date/종료 0과 위 19개 결과를 확인했으며 자체 독립 모듈 링크로 표시하지 않는다.
proof: `Saved/Tests/SplitQuantityUI-8bd491af/Verification.proof.json`.
native proof: `Saved/Tests/Item/Automation-514f734e69d44da098fc25d62fe7c6ae/Item.proof.json`.

이 native 결과를 실제 렌더링된 Ctrl 입력/선택 수량 저장 성공으로 대체하지 않는다.
후속 실제 WBP의 Ctrl 드롭·취소·지정 수량 저장 결과는 다음 절에 기록한다.
물리 입력과 Steam/Shipping은 별도다.

### 지정 수량 UI의 실제 화면·저장 연결 (2026-10-23)

`Scripts/Network/Run-DungeonBossReturnE2ETest.ps1 -InventorySplitQuantity -ClientCount 1`은
기존 개발 인증/Join Ticket/전용 서버 입장과 저장된 WBP를 재사용한다. 전투 gate는
열지 않고 합성 Ctrl 드래그·hit-test 포인터 클릭·포커스된 입력의 Ctrl+A/문자 이벤트로
기존 수량 영역을 조작한다. probe에서 `SetText`/UI 확인 함수/아이템 RPC를 직접 호출하지 않는다.

- 첫 Ctrl 드롭에서 영역을 열고 Cancel 버튼을 실제 hit-test 경로로 클릭했다. 클라이언트의
  무명령/수량 보존과 HTTP 원본 7/rev 1·활성 아이템 개수, 해당 캐릭터의 아이템 동작
  영수증 `0|0`을 DB에서 확인해야 다음 드롭의 확인 gate를 열도록 했다.
- 두 번째 Ctrl 드롭에서 전체 수량 `7`을 키보드 문자 경로로 입력해 확인 불가/버튼 비활성화를
  확인했다. 다시 `2`를 입력하고 Split 버튼을 클릭해 **7→원본 5+새 스택 2**를 저장했다.
  절반 기본값 3을 저장하거나 드롭만으로 저장하는 경우는 통과하지 않는다.
- 정확한 새 ID와 원본 rev 2/새 ID rev 1이 열린 셀/ViewModel/소유 Projection·HTTP의
  두 레코드 영수증에서 일치했다. 원본 퀵슬롯 유지/새 퀵슬롯 미등록과 양쪽 조작 잠금
  해제를 확인했다. 세 번째 Ctrl 드롭은 점유된 칸에서 거절되고 영역도 열리지 않았다.
- PostgreSQL의 분할 영수증은 정확히 1건(분리 수량 2)이며 Move/Swap/StackTransfer/Consume
  영수증은 없다. 총수량 7, 귀속/내구도/시드/태그/스탯/만료/출처를 포함한 전체 DTO와
  시작 장비/원래 영수증을 보존했다. 세션은 InProgress이며 전투/정산을 시작하지 않았다.

최종 실행: `Saved/Tests/DungeonBossReturn/99a1fdb67b30429b95997316b557f477/result.json`.
러너 종료 0, Ctrl 드래그 3회·포인터 클릭 4회·문자 입력 2회, 선택 입력 42개(WBP 포함)/
DLL 전후·종료 후 해시 일치와 닫힌 UE 로그 2개 Error/Fatal/Assert/Ensure 0을 확인했다.
자체 UE 2개와 백엔드는 정리하고 Docker Desktop/개발 DB는 유지했다. 꺼져 있던 Docker
Desktop은 기존 사용자 설치 경로에서 백그라운드로 시작했으며 설정이나 기존 데이터를
초기화하지 않았다. 잘못된 모드 병용/인원/DB 연결 조합 4개는 실행 전에 거절했다.

첫 자체 빌드는 입력 인터페이스의 DLL 심볼 누락으로 실패했다. 현재 소스에 다른 작업이
추가한 out-of-line 정의가 이미 있어 중복 수정하지 않았고, 자체 재빌드 6개 작업/모듈
링크/Editor 타깃 종료 0을 확인했다. 운영 아이템/UI·BP 에셋/API/스키마·권한/lease 규칙은
변경하지 않았고, 관련 소스 38개가 같은 기존 native 19개는 반복하지 않았다. 이전 native
결과를 이번 새 DLL의 native 실행으로 표시하지 않는다.
proof: `Saved/Tests/SplitQuantityLive-42fb6701/Verification.proof.json`.

이는 1인 로컬 개발 인증·GPU offscreen 합성 입력의 관측이다. 실물 입력·다인/지연·재시작·
Steam/Shipping, 실제 Ctrl 선택 도중 원본/목적지가 경합하는 경우는 별도다.
다음 작업은 이미 구현된 빈 칸 이동/자리 바꾸기의 실제 조작·저장 연결을 마무리하는 것이다.

### 실패한 초기 아이템 조회의 수동 재시도 (2026-10-18)

인증 입장 뒤 서버의 첫 Item V2 조회가 실패하면 소유자 인벤토리/장비 창은 `Failed`
상태에서만 `Retry item load` 버튼을 보인다. 버튼은 ViewModel을 통해 인자 없는
owner-controller RPC를 보내며, 서버가 **현재 인증 캐릭터·미회수 신원·Failed 상태**와
3초 재시도 간격을 확인하고 기존 `LoadAuthenticatedCharacterItems`를 다시 실행한다.
클라이언트가 아이템 목록/소유자 ID를 제출하거나 DB Record를 직접 바꾸지는 않는다.
`Loading`/`Ready`에서는 버튼을 숨기고 서버도 중복 요청을 거부한다. 성공하면 기존
authoritative 스냅샷→장비 Reconcile→무기 로드아웃 경로로 이어진다.

UHT 생성과 변경 C++ 3개의 `Project_RPGEditor` 개별 `-NoLink` 컴파일은 통과했다.
공유 DLL 링크·네이티브/실제 인증 입장 중 백엔드 장애/회복·UI 클릭·무기 공격까지의
실행 확인은 별도다. 이 재시도는 기존 캐릭터에 새 시작 무기를 지급하거나
실패한 장비 정의/에셋을 자동 수정하지 않는다.

## 퀵슬롯 전환

`UQuickSlotComponent`의 아이템 슬롯은 `ItemId`만 저장한다. 수량, Definition,
현재 Revision은 소유 컨트롤러의 Projection에서 조회한다. 로컬 소비 입력은
`ARPGPlayerController::TryConsumeInventoryItem` → 컨트롤러 소유 공유 ViewModel의
`RequestConsume` → `URPGItemCommandComponent::ServerConsumeItem`으로 전달한다. 구형 인벤토리의
소비 RPC나 아이템 객체의 수량을 사용하는 경로는 퀵슬롯에서 제거했다.

- 등록 시 서버가 소유 Inventory 항목과 소모 정책의 Definition 버전을 검사한다.
- Projection 갱신 시 Widget과 ViewModel을 다시 갱신한다.
- 마지막 소비 또는 Inventory 밖으로의 이동으로 항목이 사라지면 서버가 해당
  ID를 참조하는 모든 퀵슬롯을 해제한다. 클라이언트는 복제 도착 순서 때문에
  항목이 잠시 보이지 않더라도 등록 상태를 직접 삭제하지 않는다.
- 스킬 슬롯의 태그와 Press/Release 처리는 기존 동작을 유지한다.

Blueprint의 신규 등록은 `SetItemSlotById`, 클릭 사용은 `UseAssignedItem`을
사용한다. 기존 `SetItemSlot`/`UseSlotItem`은 ID를 전달하는 Deprecated 호환
진입점이며, 별도의 구형 상태를 읽거나 수정하지 않는다. 기존 객체 포인터와
수량 이벤트 필드는 자산 연결을 변경할 때까지 보존하지만 새 경로에서는 사용하지
않는다. 기존 인벤토리 데이터가 새 Record 저장소로 자동 이전되는 것은 아니다.

인벤토리 ViewModel의 `AssignItemToQuickSlot(ItemId, SlotIndex)`은 Blueprint가
선택한 Projection 항목을 퀵슬롯 등록 요청으로 전달한다. 등록 성공 여부는 서버가
검증한다. 인벤토리 격자의 점유 칸은 ItemId만 담아 퀵슬롯 아이템 칸으로
드래그할 수 있다. 스킬 칸 드롭은 기존 처리에 맡긴다. I키의 인벤토리 전환은
Projection 창만 사용한다.

`ProjectRPG.Item.QuickSlot` 테스트는 등록 검증, 수량/Revision 갱신, 최종 소비 후
복수 슬롯 해제를 검증한다.

2026-10-03 검증: `Project_RPGEditor Win64 Development` 빌드 성공. 화면 없는
자동화 실행에서 QuickSlot 2개와 Projection 회귀 테스트 3개, 총 5개가 통과했다.
ItemId 드래그 경로는 컴파일만 검증했으며 실제 UI/멀티플레이 조작은 별도 단계다.
픽업·장비 경로의 구형 상태 제거까지 완료한 것은 아니다.

### 퀵슬롯 소비의 공유 대기·복구 표시 (2026-10-27)

- 키보드·위젯 클릭·기존 호환 진입점의 소비는 가방과 같은 컨트롤러 소유 모델을 사용한다.
  가방을 닫아도 같은 ItemId의 예약이 유지되고, 여러 퀵슬롯에 같은 항목을 등록했거나
  가방 명령이 대기 중이면 새 소비 요청을 만들지 않는다. 공유 모델/로컬 컨트롤러를
  사용할 수 없을 때 직접 RPC로 우회하지 않는다. 컴포넌트가 UI ViewModel을 직접 소유하지는 않는다.
- 아이템 슬롯은 기존 FieldNotify를 구독해 Pending/복구/잠금/사용 불가를 아이콘 투명도와
  상태 툴팁으로 표시한다. 상태 변경과 비동기 아이콘 완료 모두 같은 표시 함수를 사용하며
  누르고 있던 클릭은 차단 상태가 되면 취소한다. 위젯 자체를 비활성화하지 않아 기존 드롭/
  해제 입력을 유지하고, 해제/파괴 시 구독을 제거한다. 새 Tick·로딩·자동 재시도는 없다.
- 최신 소스의 `Project_RPGEditor Win64 Development` 정상 타깃 확인은 exit 0/Up to date다.
  이 실행은 새 링크를 수행하지 않았다. 로그: `Saved/BuildChecks/QuickSlotSharedConsume-20261027-0220/Build.log`.
  초기 공유 빌드에서 발견된 테스트의 서로 다른 포인터 타입 비교를 고친 뒤, 같은 DLL의
  퀵슬롯 **7/7**, 화면/상호작용 **8/8**이 통과했다(각 러너 exit 0, 검사 입력·DLL/닫힌 로그 불변).
  증거: `Saved/Tests/Item/Automation-3e2e64f454a94c42a7206fbad5c4720a/Item.proof.json`,
  `Saved/Tests/Item/Automation-f3fbe53837ca452db4d87c043eb8a49b/Item.proof.json`.
  마무리 재확인에서는 공유 DLL 해시가 달라졌고 선택 소스/닫힌 로그는 같았다.
  따라서 15개 성공은 두 proof에 기록된 검증 당시 DLL의 결과이며 이후 DLL의 재검증으로 계산하지 않는다.
- WBP/백엔드/API·스키마·서버 권한/CAS·소비 효과 복구 정책은 변경하지 않았다.
  합성 native 입력/통지 검사이며 실제 WBP 화면·인증 HTTP 응답 유실·PostgreSQL/GAS 실연결,
  다중 PC/Steam/Shipping 검증 완료가 아니다. 그 실연결은 기존 준비 도구를 사용하는 후속이다.

### 차단 상태에서 시작한 퀵슬롯 클릭의 소비 방지 (2026-10-27, 구현·개별 컴파일)

이전 구현은 상태가 차단으로 바뀔 때 누르던 클릭을 취소했지만, 이미 로딩/복구 중인 슬롯을
새로 누르면 소비 클릭이 다시 준비될 수 있었다. 해제 전에 상태가 Idle로 돌아오면 그 클릭이
새 소비로 이어질 수 있어, 이제 `NativeOnMouseButtonDown`에서도 공유 모델의 Idle을 요구한다.
Pending/복구/잠금/사용 불가 또는 모델 부재에서 시작한 누르기는 소비를 준비하거나 마우스를
캡처하지 않는다. UI의 drag 감지/드롭 경로는 유지하며, 상태 해제 뒤에는 새 클릭이 필요하다.
서버 RPC/검증·아이템 예약·수량/효과 처리와 스킬 입력은 바꾸지 않았다.

기존 `SharesRecoveryPresentationWithoutDisablingDrag` 검사에 복구 중 누르기→복구 해제→놓기,
로딩 중 누르기→Ready→놓기의 무소비와 이후 정상 새 클릭을 추가했다. 운영 CPP의 개별 컴파일은
통과했으며 테스트 CPP에서 드러난 `UPackage` 불완전 타입은 `UObject/Package.h` 직접 include로
보완했다. 최종 증분 `-NoLink`는 exit 0, 두 변경 소스와 공유 DLL은 검사 중 불변이었다.
로그: `Saved/BuildChecks/QuickSlotBlockedPress-20261027/Compile.log`(초기 실패 포함),
`Compile-Final.log`(최종 성공). 실행 중 에디터를 유지했고 Live Coding의 임시 개별 출력이다.
**정상 DLL 링크·에디터 반영·추가한 assertion의 native 실행은 아직 하지 않았다.** 이전의
퀵슬롯 7개/native 및 별도 소비 HTTP/GAS 실연결 성공을 이 입력 변경의 검증으로 계산하지 않는다.

### 퀵슬롯 에셋의 구형 drag visual 참조 정리 (2026-10-13)

- MCP로 `WBP_QuickSlot`의 그래프/기본값/의존성을 먼저 확인했다. 이벤트 그래프는
  실행 연결 없는 PreConstruct/Construct/Tick뿐이고 `DragVisualClass`는 이미 `None`이었다.
  현재 C++ ItemId 드롭 경로는 이 클래스를 사용하지 않지만 저장 파일에는 사라진
  `/Game/Blueprints/UI/Inventory/WBP_ItemDragVisual` 의존성이 남아 로드 경고가 발생했다.
- 원본을 백업한 뒤 이 속성만 명시적으로 `None`으로 설정하고, 경고를 오류로 취급한
  MCP 컴파일 후 `WBP_QuickSlot` 하나만 저장했다. 그래프 DSL과 기본 `bCanDrag=false`,
  `SlotIndex=0`, `bIsSkillSlot=false`는 전후 동일하다. 의존성은 6개→5개로 줄었고
  native 부모/기존 RPGSizeBox 의존성을 유지했다. 구형 비주얼 에셋을 새로 만들지 않았다.
- 원본 백업:
  `Saved/Backups/InventoryUI/QuickSlot-1e4cbcfd9b424429aab91395ddb3e903/WBP_QuickSlot.uasset`.
  원본 SHA256 `7BF28C7F5E9008F24E2CB66115E7B1B540C913B826FF8B3A985115C74A55AABC`,
  변경 후 `49DD5DA0B3CB0138256E99FD1693D59CCEF1DD1ACFBC012EE2573E902DE60585`.
- 읽기 전용 `Scripts/Editor/VerifyQuickSlotAsset.py`를 추가했다. 대상 BP 하나만 로드/메모리
  컴파일하고 native 타입·기본값·저장된 의존성을 확인하며 에셋을 저장하거나 PIE를 시작하지 않는다.
  `UnrealEditor-Cmd -ExecutePythonScript=<스크립트 절대 경로>`로 실행한다.
- 별도 숨김 프로세스의 cold-load/compile 검사는 **PASS, exit 0**이다.
  `Saved/Tests/QuickSlotAsset/1bb761e049e84148a2dcc091ff7bb1ad/cold-load.log`의
  `QUICKSLOT_ASSET PASS NoAssetsSaved=1 dependency_count=5`를 확인했고,
  삭제된 비주얼의 로드 오류가 없으며 실행 전후 에셋/DLL 해시가 같았다.
  DLL SHA256은 `8AFC2F3FD3EF393216EA44EDCBB1714DF70F2F5F91E5C7A9B4F987D1CB41D457`이다.
- 최초 MCP reset 도구의 `get_super_class` 바인딩 오류와 최초 검사기의 `parent_class`
  읽기 바인딩 오류를 보존했다. 초기 cold-load는 exit 0이었지만 PASS marker가 없어 실패로 처리했다.
  검사기는 native CDO의 타입 검사로 보완했으며 초기 실패 로그는
  `Saved/Tests/QuickSlotAsset/447bbc922082489183e68b08091ac7de/cold-load.log`에 남긴다.
- 다른 작업의 미저장 `TPWBP_AbilityIconSlot`은 저장하지 않았다. 이후 dirty 목록의
  `WBP_PlayerHUD`도 저장하지 않았다. C++/DLL 교체, 다른 에셋 저장, 백엔드/DB 변경은 하지 않았다.
  이 결과는 에셋 연결 검증이며 인증된 아이템 등록/소비나 실제 입력 성공의 증거가 아니다.
- 당시 확인한 다음 입력 수정은 클릭→`UseAssignedItem` 연결이었다. 아래 후속 소스 구현과
  빌드/실행 대기 상태를 구분한다.

### 아이템 퀵슬롯 좌클릭 입력 연결 (2026-10-13, 링크/native 실행 대기)

- `URPGQuickSlotWidget`에서 같은 아이템 칸의 좌클릭 누르기→해제로 기존
  `UseAssignedItem`/`UseItemSlot` 소비 요청을 연결했다. 누르기 자체는 사용하지 않고,
  요청 전 누름 상태를 지워 중복 해제가 다시 요청하지 못하게 한다. 더블클릭의 두 번째
  누르기도 해제 때만 처리한다. UI는 수량/Revision을 직접 수정하지 않는다.
- 누르기 때의 ItemId·컴포넌트·슬롯·사용자/포인터를 확인한다. 영역 이탈/바깥 해제,
  Slate 드래그 임계값 이동, 드래그 시작/취소/드롭, capture 상실, 비활성화/파괴/재바인딩,
  누르는 동안 등록 아이템 교체는 취소한다. 자신이 소유한 해당 포인터 capture만 해제한다.
  빈 칸/disabled/다른 버튼·포인터/스킬 칸은 아이템 사용을 시작하지 않는다.
- 기존 ItemId 드롭 등록, 스킬 슬롯 입력 및 단축키 표시 변경을 유지했다. 공개
  `UseAssignedItem`도 표시 ID와 현재 등록 ID/소유 Pawn이 일치하는 경우만 전달한다.
  기존 서버 소유권/정책/Revision/인증 검증을 변경하거나 우회하지 않았다.
- `RPGQuickSlotClickTests.cpp`에 3종을 추가했다: matching release 1회/더블클릭,
  영역·드래그·capture·수명 취소, 등록 교체/드롭/스킬 칸 경계. fixture는 백엔드를
  시작하지 않고 소비 요청의 인증 거절과 수량 불변을 관찰한다. 인증된 실제 소비 증거가 아니다.
  `Run-ItemAutomationTest.ps1 -QuickSlotOnly`는 신규 3종과 기존 Projection 2종만 선택한다.
  러너 구문 검사와 5종 선택 확인은 통과했지만 **native 테스트는 아직 실행하지 않았다**.
- 자체 UBT `-NoLink`는 exit 6이다. workspace `-UBARootDir`를 사용해도 makefile의
  Roaming 설정 폴더 및 기본 AppData trace 접근이 거절돼 컴파일 단계에 도달하지 못했다.
  로그: `Saved/Build/QuickSlotClick/c834f6e9cc314b199b9855f6e36568fb/NoLink.log`.
  별도 MSVC 개별 컴파일의 초기 실패에서는 `GetIsEnabled` 등 UE 5.8 API를 보완했고,
  공유 `QuickSlotComponent.h`와 이전 generated header의 행 번호 불일치도 확인했다.
- 공유 빌드가 끝난 뒤 UHT만 별도 실행했다. 첫 실행의 이중 log writer 오류를 보존하고,
  `-NoLog`로 UHT의 추가 writer만 생략한 후 기존 글로벌 workspace log를 유지했다.
  최종 UHT는 warnings-as-errors/exit 0, 기록상 새 출력 0개이며 당시 갱신된 공유 생성
  헤더의 행 번호 일치를 확인했다. 생성 헤더 갱신을 자체 전체 빌드 성공으로 표시하지 않는다.
- 이후 기존 MSVC 14.44 응답 파일과 엔진 Source 작업 폴더를 사용한 두 파일의 개별
  컴파일이 **모두 exit 0**이다. 출력 obj/응답 파일/각 초기 실패를 별도 경로에 보존했고,
  공유 obj/DLL은 교체하지 않았다. 증거:
  `Saved/Build/QuickSlotClick/Direct-1791820876352/Compilation.proof.json` 및 같은 폴더의
  `RPGQuickSlotWidget.Attempt3.log`, `RPGQuickSlotClickTests.Attempt3.log`, `UHT2.log`.
  선택 소스/생성 헤더/응답 파일 해시와 불변 DLL SHA256
  `30D4C8FF2C0004788CB23978A222137D131A12889AF02955C0DFCFE69262A1D7`을 기록했다.
  이 결과는 **개별 컴파일**이며 UBT NoLink 성공/모듈 링크/native 실행/인증 소비가 아니다.
- 다른 프로세스/GUI 에디터/미저장 에셋/백엔드를 종료·저장·수정하지 않았다.
  당시 다음 단계는 Editor 빌드/모듈 링크→선택 native 5종→인증된 실제 UI 등록/사용·수량·
  최종 소비 검증이었다. 아래 후속 기록에서 별도 DLL의 native 아이템 5종은 통과했으며,
  실제 UI/인증 소비 검증과 공유 DLL에 수정된 테스트를 반영하는 단계는 남는다.
  열린 에디터 DLL의 강제 교체나 unsafe hot reload는 하지 않는다.

### 후속 빌드의 스킬 퀵슬롯 테스트 링크 차단 수정 (2026-10-13)

- 사용자가 수행한 Editor 빌드는 게임 모듈 링크에서
  `RPGSkillQuickSlotTests.cpp.obj`의 `FGameplayAbilitySpecHandle::GenerateNewHandle`
  미해결 참조(LNK2019/LNK1120)로 실패했다. 해당 함수는 엔진 헤더에 외부 모듈용
  export가 없고, `FGameplayAbilitySpec` 생성자는 export되어 GAS 내부에서 핸들을 생성한다.
- `HeldInputIdentity` fixture에서 직접 생성 호출을 제거하고 export된 spec 생성자의
  `Handle`을 사용하도록 수정했다. 두 핸들의 유효성/서로 다른 identity도 확인한다.
  능력을 실제 grant/activate하거나 fake 정수 핸들을 쓰지 않는다. 테스트 파일 하나만
  바꿨고 운영 스킬/아이템/입력/엔진 코드·Build.cs·에셋은 변경하지 않았다.
- 실행 중인 에디터/빌드가 없는 상태에서 사용자의 기존 컴파일/링크 응답 파일을 보존해
  해당 테스트만 재컴파일하고 기존 게임 모듈을 링크했다. **컴파일 exit 0, 모듈 DLL 링크
  exit 0**이다. 수정 전/후 obj의 undefined `GenerateNewHandle` 참조는 1개→0개다.
  실패한 사용자 링크 뒤 루트 DLL은 없었으며, 열린 DLL을 강제 교체하지 않았다.
- 같은 DLL에서 별도 숨김 `UnrealEditor-Cmd`로
  `ProjectRPG.Skill.QuickSlot.HeldInputIdentity` **1종 성공/exit 0**을 확인했다.
  요청 scope와 성공 marker, 로그의 fail/fatal/assert/ensure 부재, 실행 전후 소스/DLL
  불변을 확인했다. 새 유효 핸들 생성, held/repeat의 원래 spec 유지, 공유 spec의 마지막
  release와 release-all 동작을 검사한다. 다른 skill/item 전체 suite는 실행하지 않았다.
- 증거는 `Saved/Build/SkillQuickSlotLinkFix/95af0e9819c14b9c8d2943e5a3723451/`의
  `LinkFix.proof.json`, `Compile.log`, `Link.result.json`, `HeldInput-automation.log`, 보고서와
  Before/After obj·링크 응답·검증 DLL 사본이다. 성공한 링크의 출력은 비어 있어
  `Tee-Object`가 `Link.log`를 생성하지 않았으며 반환 결과를 `Link.result.json`으로 기록했다.
  DLL SHA256은 `4929070DCC97A5747065E3F7EA423F87C5A9CAC664570A076B220F4ADA1BE9E3`이다.
  **전체 Editor 타깃 완료·아이템 퀵슬롯 5종 실행·인증 소비/HTTP/복제/실물 입력/Shipping**은
  이 증거의 범위가 아니다. 이후 아이템 선택 5종은 아래 별도 테스트 DLL에서 확인했다.

### 퀵슬롯 선택 5종과 테스트 컨텍스트 검증 (2026-10-13)

- 사용자의 후속 빌드 성공 보고 뒤 공유 DLL의 테스트 5종 존재를 확인하고
  `Run-ItemAutomationTest.ps1 -QuickSlotOnly -SkipSDKSetup`으로 실행했다. 최초 결과는
  성공 3/실패 2다. 프로세스 exit 0에도 러너는 성공 marker 부족을 실패로 판정했다.
  증거: `Saved/Tests/Item/Automation-7b3575ed867744b58b251d6f121e4284/`.
- `RPGQuickSlotClickTests.cpp` fixture만 수정했다. Slate 참조를 유지하지 않으면
  `SObjectWidget` 파괴가 `NativeDestruct`/바인딩 해제를 호출하므로 참조를 유지하고
  월드 파괴 전에 해제한다. 로컬 컨트롤러를 명시하고 `World->AddController`로
  조회 목록에 등록한다. 위젯 `PlayerContext`는 이 목록을 통해 Controller를 찾는다.
  Controller 일치와 pending press 양성 검사도 추가해 초기 취소 검사의 무동작 통과를
  완료 증거로 쓰지 않는다. 등록 목록 누락을 드러낸 중간 실패 2회도 보존한다.
- GUI 에디터/다른 검증 프로세스가 실행 중임을 재확인해 종료나 공유 DLL 교체를 하지
  않았다. Saved 아래 프로젝트 사본의 Binaries/Config/러너와 Content/Plugins/Source
  junction을 이용해 테스트 obj와 DLL/PDB 출력만 별도로 지정했다. 원본 에셋을 저장하거나
  운영 입력/소유권/인증/엔진/백엔드 코드를 바꾸지 않았다.
- 수정 테스트 컴파일/별도 모듈 링크 **exit 0**, 최종 native 선택 **5/5 성공,
  러너 exit 0**이다. matching release/더블클릭, 드래그·영역·capture·수명 취소,
  등록 변경/ItemId 드롭/스킬 칸 경계와 기존 소유·정책 등록/수량·int64 Revision·최종
  tombstone의 복수 슬롯 해제를 확인했다. 성공 marker/오류 부재, 전후 소스/DLL 해시와
  실제 로드한 별도 DLL 경로도 확인했다. 초기 실행 이후 변경된 관련 소스는 fixture 하나다.
- 증거 루트: `Saved/Tests/QuickSlotClickFixture/9674e48fb88044779782b4b25d2f5edc/`의
  `FixtureFix.proof.json`, 컴파일/링크 응답, 검증 DLL 및 초기 사본들.
  최종 로그/native proof: 그 아래
  `Project/Saved/Tests/Item/Automation-11e1c628ce4c4680a4b2b0205f95d703/`.
  중간 실패는 `Automation-7038cf1b0da741d383bf82250580f8ed`,
  `Automation-1da338cf9e70410f831bf24f1502de66`에 보존한다.
  검증 DLL SHA256 `E3D45265E20808EF671199690EC9C76719811162C4704B45453C94E6F5ACA252`,
  불변 공유 DLL SHA256 `4929070DCC97A5747065E3F7EA423F87C5A9CAC664570A076B220F4ADA1BE9E3`.
  수정 fixture는 다음 정상 빌드에 공유 DLL로 반영하며, 현재 공유 DLL의 테스트 통과로
  이 별도 DLL 결과를 표시하지 않는다. junction/원본 폴더의 재귀 정리는 하지 않았다.
- 범위는 **native handler + 모의 권위 Projection + 인증 거절 facade**다.
  실제 Slate/OS 마우스 라우팅·인증된 소비 성공·HTTP/DB·네트워크·Shipping은 아니다.
  다음은 기존 에디터/MCP에서 실제 초기 로드 상태와 인증된 항목의 등록/사용·수량·
  최종 소비를 확인하는 것이다. 애니메이션 오류/다른 기능은 수정하지 않는다.

### MCP UI 준비 상태와 GUID 비교 보완 (2026-10-13, 기록 재개 2026-10-14)

- 기존 에디터의 standalone `testmap` PIE에서 실제 HUD 아이템 퀵슬롯 4개의
  인덱스 `0~3`, 키 표시 `1~4`, 빈 ItemId/수량 표시와 Pawn 소유 컴포넌트를 읽었다.
  캐릭터는 미인증, Projection은 `Uninitialized`, 아이템/우편은 0개였다.
  이는 로드된 빈 인벤토리나 아이템 등록·소비 성공이 아니다. 위젯의 private weak
  binding은 Python에서 읽을 수 없어 직접 바인딩 동일성 검증도 주장하지 않는다.
- `VerifyInventoryWindowPIE.py`의 ItemId/명령 RequestId 비교를 `str(Guid)`에서
  `GuidLibrary.conv_guid_to_string`으로 바꿨다. UE 5.8의 `str(Guid)`는 GUID 값이
  아니라 wrapper 주소를 포함해 같은 ID를 서로 다르다고 판단할 수 있었다.
  실제 UE GUID wrapper와 detached 모의 항목으로 동일 ID/정렬·int64 Revision 보존,
  ID·수량·Revision·위치·요청 결과 변경 감지 **12개 검사 통과**를 확인했다.
  이 검사는 진단 helper 검증이며 실제 아이템/권위 Projection에 데이터를 주입하지 않는다.
- 증거: `Saved/Tests/InventoryUIPreflight/c4bd9c7dc1a34afdb989c1485642fd15/`의
  `Readiness.proof.json`, `observations.log`, `GuidFingerprintProbe.py`.
  당시 GUI 로드 DLL은 `B371525AA894CA92CAD74660E19E808E72EF52C3A3A2E87EAFEB7CC635850A12`로
  검사 전후 동일했다. 자신이 시작한 PIE만 world/Controller 동일성 확인 후 종료하고
  GUI·게임 코드·에셋·다른 검증을 유지했다. 초기 Python binding/긴 Type 전송 실패도 남겼다.
- 2026-10-14 재개 시 진단 소스 해시는 위 proof와 같지만 공유 DLL은 더 새 버전이다.
  에디터가 종료돼 MCP가 응답하지 않고, 설정된 `localhost:3000/health`도 제한 시간 내
  응답하지 않았다. 다른 `dotnet` 프로세스를 종료/재설정하거나 새 서버를 띄우지 않았다.
  다음은 기존 인증 입장 경로로 `Ready`와 실제 소모품을 확보한 뒤 UI 등록·좌클릭
  사용·수량 갱신·최종 소비 해제를 검증하는 것이다. OS 입력·인증 소비·HTTP/DB·Steam·
  Shipping은 미검증이며 준비 확인/진단 검사만으로 완료 표시하지 않는다.

### 실제 인증 입장과 Ready 가방 표시 (2026-10-14)

- 별도 Development/Memory 백엔드의 새 테스트 계정·캐릭터·세션과 소유 UE Editor
  `-server` 프로세스를 준비했다. 현재 GUI의 PIE가 꺼져 있음을 확인한 뒤 자체 PIE를
  시작했고, 기존 native 개발 로그인 → 인증된 소유 캐릭터 조회 → public 세션 복원 →
  Join Ticket 발급/소비 → 실제 네트워크 입장 경로를 사용했다. 토큰·PlayerState 신원·
  Projection `Ready`/레코드를 직접 주입하지 않았다.
- owning client는 authority가 없으며 실제 인증 CharacterId/세션 ID와 `Ready`를 확인했다.
  포션 3개·장비 1개는 백엔드, Iris 복제 Projection, 실제 가방 ViewModel에서 같은
  ItemId·수량·Revision `1`·슬롯 `0/1`이었다. MCP 합성 I 키로 가방을 열어 80칸·분리
  레이아웃·동일 Ready 상태와 두 항목 표시를 확인했다. 아이템 명령은 제출하지 않았고,
  UI 확인 후에도 백엔드 수량/Revision과 현재 멤버 lease가 유지됐다.
- 증거: `Saved/Tests/ItemUIAdmission/d4154b7dedcd40888c2dd5e7079e8e3d/`의
  `Admission.proof.json`, `Fixture.json`, `observations.log`, `inventory-ready-ui.png`,
  준비/입장/읽기 전용 UI 진단 스크립트와 시작별 서버 로그다. GUI/서버가 실제 로드한
  공유 DLL 경로와 해시는 시작/확인 시 일치했고 이번 단계에서 재빌드·C++/BP/설정
  수정이나 이전 전체 suite 반복은 없었다. 최초 조회 배열 필드 파서 오류의 서버만
  종료하고 원래 로그를 보존했다. Python 비노출 API 오류는 로그인 전 준비 단계에서
  해당 PIE GameInstance 소유의 이미 생성된 서브시스템을 읽는 방식으로 수정했다.
- **현재 인증 입장 클라이언트의 `RPGQuickSlotWidget`은 0개다.** 따라서 실제 퀵슬롯
  등록·좌클릭 사용·수량 갱신·최종 소비 해제는 아직 검증하지 않았다. 다음은 travel 후
  owning-client HUD 생성 경로 확인/수정이며 위젯이나 ItemId를 임의 생성/등록해
  실제 UI 검증으로 대신하지 않는다. 자체 observer는 해제하고 자체 PIE/게임 서버와
  에디터/백엔드는 후속 UI 확인을 위해 유지했다. 기존 DB와 다른 작업은 변경하지 않았다.
- 이는 1인 로컬 개발 인증/HTTP/Iris/합성 I 입력 범위다. 설치된 Editor의 개발 서버
  fixture는 플랫폼 연결 신원 비교만 기존 개발 플래그로 우회하고 Join Ticket·배정
  세션·소유권 검증은 유지한다. 실제 Steam 연결 신원·물리 OS 입력·PostgreSQL 영속성·
  packaged dedicated server/Shipping·소모품 UI 사용 성공으로 확대 해석하지 않는다.

### 입장 후 HUD 시작 순서 보완 (2026-10-15, 에셋 저장/빌드 대기)

- 현재 바이너리의 새 개발 인증 입장에서도 `Ready`와 두 아이템 복제는 정상이나,
  HUD 루트/퀵슬롯은 0개였다. 클라이언트 로그에 `GA_Player_DrawHUD`의
  `LocalPredicted` 실행이 아직 로컬 제어 상태가 아니어서 거절된 기록이 있고,
  이후 Pawn은 로컬 제어 상태가 됐지만 부여된 HUD spec은 비활성이었다.
  기존 HUD/Controller/GameMode의 BeginPlay가 직접 생성하는 구조가 아니라
  `DA_Player`의 OnGiven 어빌리티가 `WBP_PlayerHUD`를 생성하는 경로임을 MCP로 확인했다.
- `URPGAbilitySystemComponent`는 **실제 서버 요청을 받은 기존 LocalOnly spec만**
  로컬 Controller/Pawn 준비까지 보류한다. 누락 spec은 엔진 큐를 유지하고,
  예측 전투의 요청/서버 OnGiven 부여·제거 규칙은 변경하지 않았다. 동일 핸들 요청을
  합치며 Owner/Avatar/World scope·활성/제거된 spec을 다시 확인하고 actor info 변경/clear
  시 과거 요청을 버린다. `PawnClientRestart`에서 기존 context는 refresh만 수행해
  불필요한 montage context 재초기화를 피하고 준비된 요청을 처리한다.
- mock 준비 전 중복/clear, 제거 spec, Avatar 교체, 엔진 missing-spec 큐 보존의
  native 회귀 4종을 추가했다. 변경 구현 2개와 테스트 1개를 기존 응답 파일로
  **각각 컴파일 exit 0** 확인했으며 출력 obj/의존성/로그는 Saved 아래로 격리했다.
  전체 타깃/새 DLL 링크와 네이티브 테스트 실행·수정 후 실제 HUD 성공은 아직 아니다.
- `GA_Player_DrawHUD`만 LocalOnly로 바꾸는 opt-in 에디터 helper와 원본 백업을
  준비했지만 대상에 미저장 변경이 감지돼 정책 변경/저장을 거절했다.
  현재 에셋은 여전히 LocalPredicted이며 디스크 에셋/DLL 해시는 재개 당시와 동일하다.
  현재 미저장 상태를 함께 저장할지 사용자 확인 → 정책 적용 → 새 빌드/로드 →
  위 회귀와 실제 인증 입장/HUD 1개·퀵슬롯·등록/소비 검증 순서로 이어간다.
- 증거: `Saved/Tests/ItemHUDStartup/8d1f23729cb44c2f8ca1da8e0021ce45/`의
  `Startup.proof.json`, `observations.log`, `Before/Fixture.json`, baseline 서버 로그와
  `Compile/`의 응답/개별 컴파일 로그. 백업은
  `Saved/Backups/ItemHUDStartup/8d1f23729cb44c2f8ca1da8e0021ce45/GA_Player_DrawHUD.Before.uasset`이다.
  자체 PIE/서버/observer만 종료하고 현재 에디터·백엔드·다른 프로세스·미저장 변경은
  유지했다. 게임 데이터·UI 명령/신원 주입, 다른 에셋 저장, 엔진/애니메이션 수정은 없다.

### HUD 시작 회귀 실행 (2026-10-16, 정책 적용/실제 UI 검증 대기)

- 재개 시 에디터와 공유 DLL·대상 HUD 에셋은 이전 실행 이후 갱신돼 있었다.
  MCP로 현재 `GA_Player_DrawHUD` 정책이 여전히 `LocalPredicted`이고 기존 PIE가
  실행 중임을 확인했다. 기존 PIE·미저장 상태는 건드리지 않았고 정책/에셋 저장은
  사용자 확인 대기로 유지했다. 이번 단계에서 빌드·DLL 교체는 하지 않았다.
- `Run-ItemAutomationTest.ps1 -LocalAbilityStartupOnly`를 추가하고 기존
  `-QuickSlotOnly`도 소스에서 추출한 **정확한 테스트 이름**으로 선택하게 했다.
  두 범위는 각각 신규 4종/기존 5종이며, 같은 QuickSlot 접두사 때문에 의도하지 않은
  다른 범위를 실행하지 않는다. 선택 스위치의 중복은 실행 전에 거절한다.
  별도 UserDir와 MCP 자동 시작 비활성 옵션으로 현재 에디터/PIE를 유지했다.
- 현재 공유 DLL에서 신규 mock 시작 순서 **4/4**, 기존 퀵슬롯 **5/5**를 별도
  화면 없는 프로세스로 실행해 두 러너 모두 exit 0을 확인했다. 소스·로그·DLL 해시가
  proof와 일치하며 자체 테스트 프로세스는 종료됐다. DLL SHA256은
  `44FD97DE79A5EF5366A266D929445DC9A712DB53C6BBC422337BF8F7D971FD2B`다.
  전체 Editor 타깃 빌드 성공이나 실제 RPC/HTTP/Iris·HUD 표시·아이템 소비 성공은 아니다.
- 증거: `Saved/Tests/Item/Automation-4c198c508d284c63a5c6fa73b3d25994/`
  (신규 4종)과 `Saved/Tests/Item/Automation-67dfb48e5d8c476aac134da38dd60a1f/`
  (기존 5종)의 `Item.proof.json`/`automation.log`다. 실행기 구문 검사,
  선택 개수 4/5 및 세 쌍의 중복 선택 거절도 확인했다.
- 다음은 기존 PIE 종료 및 대상의 미저장 변경 보존/저장에 대한 사용자 확인 후
  **해당 HUD 에셋만** LocalOnly 적용/컴파일/저장하고, 실제 인증 입장으로 HUD 1개·
  퀵슬롯·등록·소비를 검증하는 것이다. 애니메이션·다른 에셋·기존 플레이는 변경하지 않았다.

### HUD LocalOnly와 실제 인증 입장 표시 확인 (2026-10-16)

- 사용자 후속 진행 요청에 따라 기존 PIE를 종료하고 현재 디스크의
  `GA_Player_DrawHUD`를 별도 백업했다. 재확인 시 dirty 에셋은 0개였으며,
  opt-in helper로 **해당 에셋의 NetExecutionPolicy만 LocalOnly**로 변경했다.
  그래프·다른 실행 기본값의 동일성과 BP 컴파일 상태를 확인한 뒤 해당 에셋만 저장했다.
  다른 에셋 저장·게임 코드 변경·재빌드·공유 DLL 교체는 없었다.
- 새 Development/Memory 백엔드와 별도 개발 게임 서버/캐릭터를 준비하고 기존
  native 로그인 → 인증 소유 목록 → public 캐릭터 선택/세션 복원 → Join Ticket
  발급/소비 → 실제 owning-client 입장을 통과했다. 신원/Ready/위젯을 주입하거나
  HUD Ability를 직접 강제 실행하지 않았다. 포션 3개·장비 1개는 테스트용 서버 Grant
  API로 준비한 fixture이며 자연 드롭 검증은 아니다.
- 실제 원격 클라이언트에서 **HUD 루트 1개·아이템 퀵슬롯 4칸(전체 슬롯 7개)**,
  로컬 제어 Pawn과 owner-only Iris Projection `Ready`를 확인했다. MCP 합성 I 키의
  가방 열기 → 닫기 → 재열기 후에도 같은 HUD 루트 1개·80칸 가방을 유지했고,
  두 ItemId·수량 3/1·Revision 1·슬롯 0/1이 같았다. ViewModel에는 제출한 아이템
  명령 결과가 없고 네 아이템 퀵슬롯 ID도 비어 있다. 이번 실행 구간에는 DrawHUD의
  로컬 제어 준비 전 실행 거절 로그가 없었다. 지연 큐 분기 자체가 실네트워크에서
  실행됐는지 계측한 것은 아니며 앞선 mock 회귀와 구분한다.
- 증거: `Saved/Tests/ItemHUDStartup/b42021e01b084b44bf2e5c02bcc5b26f/`의
  `HUD.proof.json`, `observations.log`, `Fixture.json`, 관측/준비/최종화 스크립트와
  `hud-ready.png`/`hud-inventory-ready.png`다. 16개 artifact 해시와 기존 4/5종 native
  proof의 소스·로그·동일 DLL 해시를 확인하고 기존 suite는 반복하지 않았다.
  GUI/서버가 로드한 DLL은 앞 항목의 `44FD97DE...D2B`이며 새 HUD 에셋 SHA256은
  `99FB03EF4C75421CBAB489A940D292C8612309949FE106151C554425C6A23110`이다.
  백업은 `Saved/Backups/ItemHUDStartup/b42021e01b084b44bf2e5c02bcc5b26f/GA_Player_DrawHUD.BeforeDisk.uasset`이다.
- 콘솔의 import/스크립트 로컬 변수 범위 오류는 module import로 해결했고, 최종화의
  콘솔 echo 시작 마커 의존도 실제 관측 마커로 보완했다. 최초 실패 관측은 유지했다.
  자신의 observer만 해제하고 에디터와 자체 PIE/서버/백엔드는 후속 아이템 UI 검증용으로
  유지했다. 실행 중 전체 GUI/서버 로그 대신 민감한 travel URL을 제외한 관측 사본을 해싱했다.
- **다음은 실제 가방→퀵슬롯 드래그 등록·좌클릭 소비·수량 감소·최종 소비 후 해제다.**
  해당 조작/소비와 실제 Steam 연결 신원·PostgreSQL 영속성·물리 OS 입력·packaged
  dedicated server/Shipping은 아직 검증하지 않았다. 전체 아이템 UI P1은 미완료다.

### Ability의 현재 ActorInfo 연결 (2026-10-19)

이전 Normal 2인 로비→입장→전투→복귀 실행의 AttributeSet 캐시 오류 6건은 모두
`GA_Player_DrawHUD`에서 발생했다. UE의 per-execution Ability는 부여 시 공용 CDO가
`OnAvatarSet`을 받고, 실제 실행 인스턴스는 활성화 시 생성되므로 부여 때의 캐시를
실행 준비 완료로 간주하지 않는다.

- 공통 부모는 아바타 연결 및 활성화 시 전달된 현재 ActorInfo의 ASC에서 속성을 읽는다.
  실행 인스턴스에만 transient 캐시를 저장하고 CDO에는 소유자의 AttributeSet을 남기지 않는다.
  ActorInfo·ASC·아바타가 없으면 이전 속성 참조를 비운다.
- 플레이어 Ability는 BP 활성화 콜백 전에 현재 속성과 공격 속도를 준비한다. 정상적인
  첫 캐시 연결을 오류로 기록하지 않지만 실제 속성 누락의
  `RPG_ABILITY_ATTRIBUTES_UNAVAILABLE` Error는 유지한다.
- Player·Controller BP getter는 이전 객체가 아직 살아 있어도 현재 ActorInfo를 따른다.
  연결이 없는 전투 컴포넌트 조회는 null을 반환하며, 서버의 기존 마나 차감도 ASC가
  없으면 안전하게 거절한다. 다른 Pawn의 속성·마나를 대신 사용하지 않는다.
- 기존 LocalOnly 서버 요청/준비 대기·OnGiven/OnSpawn·예측 실행·피해/마나 서버 권한은
  유지한다. BP 그래프·실행 정책·시작 데이터의 저장이나 Ability 재부여는 하지 않는다.

회귀는 per-execution의 두 소유자 분리, 늦은 속성 연결/무효 컨텍스트, 동기 OnSpawn
진입 전 준비, Player/Controller 교체와 null BP 조회를 집중 검사한다. 실제 입장/복귀는
기존 2인 Normal 로비·보스 전투·정산 흐름으로 별도 확인한다.

확인된 결과:

- Editor 전체 타깃 85개 작업, 게임/에디터 DLL 링크 및 빌드 종료 0. 신규 위 4종과
  기존 `ProjectRPG.Item.QuickSlot.LocalStartup` 4종이 모두 Success/종료 0이었다.
  실제 저장 HUD의 per-execution 정책도 읽어 확인했고 BP를 저장하지 않았다.
- 같은 DLL의 실제 로비→Normal 2인 인증 입장→기존 보스 AI/대검 입력 전투→정산→
  두 결과 버튼→로비 복귀가 종료 0이었다. 캐시 오류는 이전 실행의 6건에서 0건으로
  줄었고 실제 속성 누락 진단도 0건이었다. 플레이어 피격이 관측됐으며 PostgreSQL의
  각자 골드 250·물약 2·투구 없음과 장비 대검 보존을 다시 확인했다.
- 통합 결과는 `Saved/Tests/DungeonBossReturn/4b0ba15c681b4425bb6fe5ab569c214d/result.json`,
  증거 요약은 `Saved/Tests/AbilityActorInfo-33c31e19/Verification.proof.json`이다.
  선택 입력 13개와 native/통합 실행 전후 DLL
  `D6715E3F7CD0F40D41328462B7475965B9F0E9FA8F17E9E0A2087A4147021747`은 일치했다.
- 최초 통합 실행은 게임 BeginPlay 전 SDK 자동 확인이 별도 전체 빌드의 배치 파일
  잠금을 기다리다 시간 초과했다. 첫 실행과 실패 로그를 보존했고, 자기 서버의 남은
  SDK 조회 대기 자식만 정리했다. 엔진의 기존 `UE_SKIP_UBT_SDK_SETUP=1`을 재시도의
  프로세스 환경에서만 사용/복원해 이미 빌드된 런타임의 SDK 재확인을 생략했다.
  다른 빌드·엔진 파일·게임 인증/티켓/난이도/정산 검사는 변경하지 않았다.
- native 시작 전 Error 15줄과 통합의 다른 Error 8줄(UIActionRouter 2·Standalone
  로비의 테스트용 Iris 요구 4·계획된 ConnectionLost 2)은 남는다. 서버 Error와 전체
  Fatal/Assert/Ensure는 0이지만 엄격 오류 0 감사는 실패다. 이번 결과를 실제 GPU HUD
  배치, Steam/다른 PC, Shipping/Cook 또는 전멸/재도전 검증으로 확대하지 않는다.

다음 기능 작업은 패배 결과의 재도전/포기 경계를 연결하는 것이다. 이미 정산된 승리
세션을 재사용하지 않는 규칙과 기존 P0 운영 연결은 유지한다.

### 실제 퀵슬롯 등록·클릭 소비와 연결 시점 보완 (2026-10-16)

- 실제 드래그의 서버 승인/owner 복제까지는 성공했지만 화면 슬롯은 비어 있는
  후속 문제를 재현했다. 등록 배열에는 ItemId가 있고 `FindAssignedItem`은 실패했다.
  엔진 `ClientRestart`는 Pawn을 직접 설정해 `OnRep_Pawn`을 건너뛸 수 있으며,
  `AcknowledgePossession` 뒤에야 Pawn.Controller를 설정한다. 컨트롤러의
  `ClientRestart_Implementation`에서 Super 완료 후 기존
  `RefreshControlledPawnReferences`를 호출하도록 보완했다. 신원/정책/서버 부여·소비
  권한을 바꾸거나 클라이언트에 Record/등록 ID를 직접 주입하지 않았다.
- 연결 해제 상태 → 실제 restart callback → 동일 수량/Revision 조회 복구·재호출 보존의
  mock 회귀를 추가했다. Controller/test와 관련 생성 TU를 별도 컴파일·링크해
  신규 1종/기존 5종, **6/6 성공·러너 exit 0**을 확인했다. 최초 생성 TU 미재컴파일
  후보의 신규 회귀 실패는 보존했다. 일반 UBT는 외부 AppData 설정/trace 권한으로
  실패했으므로 **전체 Editor 타깃 성공이 아니다**. 에디터의 dirty 에셋/맵 0개 확인 후
  자체 PIE/서버를 정리하고, 원래 DLL/PDB를 백업해 테스트와 동일한 DLL/PDB만 반영했다.
- 새 Development/Memory 캐릭터·서버의 기존 native 로그인/소유 목록/public 세션 복원/
  Join Ticket 입장 뒤, MCP 합성 마우스로 포션을 **두 아이템 퀵슬롯에 드래그 등록**했다.
  화면 ItemId/수량과 실제 복제 등록 배열이 일치했다. 세 번의 좌클릭이 각각 서로 다른
  RequestId의 `Item.Consume: Succeeded` 1개씩을 만들었고, 백엔드 인증 GET·Iris
  Projection·두 화면 카운터가 **3 → 2 → 1 → 소진**으로 일치했다. 남아 있는 수량의
  Revision은 1 → 2 → 3이며, 최종에는 활성 GET/Projection에서 항목이 사라지고 해당 ID를
  가리키던 두 슬롯을 포함해 등록 배열 8칸/화면 4칸이 모두 비었다. 빈 칸 재클릭 뒤에도
  명령은 총 3개였고 장비 1개의 ItemId·수량 1·Revision 1·슬롯 1은 유지됐다.
  terminal 행은 플레이어 조회 권한 밖이므로 직접 조회 성공/최종 DB Revision을 주장하지 않는다.
- 증거: `Saved/Tests/ItemUIConsumption/b5bf0d3b611143d0910bc8485093dbf3/`의
  `Consumption.proof.json`, `Before-Registration.proof.json`, 별도 프로젝트의 native
  성공/최초 실패 proof·닫힌 로그, `Build/` 응답/컴파일·링크 기록과 `After/`의 인증 fixture,
  시점별 `Backend-*.json`, 읽기 전용 관측, `registered.png`/`consumed.png`다.
  최종 31개 artifact 해시는 일치했다. 테스트/GUI/서버/반영 DLL SHA256은
  `3ED9ED89654BA83225E3AA67AC8A839E5C4EB68BD313C892BB2394A37DDE7A37`이다.
  DLL/PDB 백업은 `Saved/Backups/ItemUIConsumption/b5bf0d3b611143d0910bc8485093dbf3/SharedModuleBefore/`다.
- **검증 뒤 Controller.cpp에 별도 창 배치 수정이 추가됐다.** 이번 restart/binding hook은
  유지됨을 확인하고 해당 작업을 보존했다. proof는 실행 DLL의 UI 성공과 이후 변경된
  현재 소스 트리를 구분하며 `currentSourceTreeMatchesTestedInputs=false`를 기록한다.
  현재 트리 전체 빌드/새 배치 코드 검증으로 표시하거나 그 이유만으로 전체 suite를 반복하지 않았다.
- 별도 창의 포커스/최초 Python 바인딩 실패와 에디터 시작의 캐시/셰이더 경로 권한 실패도
  보존했다. 설치 엔진 pak·작업 폴더 내 DDC/UserDir/ShaderWorkingDir 실행 옵션으로
  에디터를 복구했으며 엔진/전역 설정/애니메이션/에셋을 수정하지 않았다. 자신의 관측
  delegate/Slate observer만 해제하고 실제 에디터·PIE·테스트 서버·Memory 백엔드는 유지했다.
  이는 1인 로컬 개발 인증/HTTP/Iris/합성 UI 범위이며 실제 Steam/물리 OS 입력/
  PostgreSQL 영속성/packaged server/Shipping은 별도다. 다음은 실제 장비 UI의 장착/해제다.

## 분리된 창의 아이템 조작

인벤토리/장비 창은 기존 분리 레이아웃을 유지한다. 각 창의 점유 칸은 동일한
네이티브 셀 위젯을 사용하며, 셀은 ItemId와 ViewModel만 참조한다.

- 인벤토리 칸 우클릭: 현재 Definition 버전의 액션 정책으로 소모품 사용 또는
  빈 호환 장비 슬롯에 장착을 요청한다. 호환 슬롯은 enum 순서로 결정하며,
  이미 장착된 장비를 자동으로 교체하지 않는다.
- 장비 칸 우클릭: 첫 빈 인벤토리 슬롯으로 해제를 요청한다. 인벤토리 용량과
  진행 중인 보상 우편의 예약 슬롯도 고려한다. 가방이 가득 찼으면 요청하지 않는다.
- 인벤토리의 왼쪽 드래그는 기존 ItemId 기반 퀵슬롯 등록을 유지한다.
  장비 칸에서는 이 드래그를 시작하지 않는다.
- 초기 로드 완료 전, 알 수 없는 ItemId, 잘못된 컨테이너/슬롯, 정책 버전 불일치,
  소비 수량 부족은 요청 전에 거부한다. 서버의 소유권·Revision·정책 검증은 유지한다.
- 셀 위젯은 RPC나 구형 아이템 객체를 직접 다루지 않는다. ViewModel이 명령을
  제출하며, 저장소의 성공 영수증 전에는 UI가 인벤토리/장비 위치를 미리 수정하지 않는다.

`ProjectRPG.Item.Interaction`의 테스트 2개는 정책에 맞는 명령 전달, 빈 슬롯 선택,
점유/용량/버전/로드 상태 거부, 장비 셀의 해제 전달 및 낙관적 변경 방지를 검증한다.
이 테스트의 서버 명령은 백엔드가 없는 fixture에서 거부되므로 실제 장착 효과의
성공이나 물리적인 마우스 입력/멀티플레이 조작을 검증한 것은 아니다.

2026-10-04 검증: Editor Development 빌드 성공. Interaction 2개, PickupGrant 5개,
QuickSlot 2개, Projection 3개, Mail 4개, 총 16개 자동화 테스트가 통과했다.
로그: `Saved/Logs/ItemProjectionInteractionBuildRetry-20261004.log`,
`Saved/Tests/ItemInteraction/Run-20261004/automation.log`.

### 실제 장비 UI 장착·해제 검증 (2026-10-16)

- 이번 에디터의 MCP는 **800번 포트**다. 이전 8000번 기록은 당시 실행의 설정이며,
  게임 백엔드 `localhost:3000`과 구분한다. 현재 에디터와 기존 Memory 백엔드를 유지하고,
  종료된 이전 검증 서버 대신 별도 개발 계정/캐릭터/서버를 준비했다. 미저장 에셋/맵이
  0개이고 PIE가 없는 것을 확인한 뒤 테스트 맵과 자체 PIE만 시작했다.
- 기존 native 개발 로그인 → 소유 캐릭터 조회/선택 → public 세션 복원 → Join Ticket
  입장으로 authority 없는 owning client의 Ready를 확인했다. 검증 아이템은 할당된
  개발 서버의 Grant API로 준비한 포션 3개/투구 1개이며 자연 플레이 획득 증거는 아니다.
- MCP 합성 마우스로 실제 가방의 투구 칸을 **우클릭 장착**하고 분리된 장비 창의
  `HEAD` 표시를 확인한 뒤, 해당 칸을 **우클릭 해제**했다. 같은 ItemId/수량 1의
  위치/Revision은 `Inventory slot 1/rev 1 → Equipment Head(0)/rev 2 → Inventory slot 1/rev 3`이다.
  백엔드 인증 GET, Iris Projection, 두 창의 실제 ViewModel과 부착된 아이템 셀이 일치했다.
  owning client의 장비 snapshot도 장착 시 같은 ID/rev 2로 생겼다가 해제 후 사라졌다.
- 각 조작은 서로 다른 RequestId의 `Item.Equip`/`Item.Unequip: Succeeded` 영수증을
  하나씩 만들었다. 빈 `HEAD` 재우클릭과 가방 재표시 후에도 총 2개였고, 포션의
  ID/수량 3/rev 1/slot 0은 전후 유지됐다. UI나 진단에서 아이템 RPC를 직접 호출하거나
  Record/신원/Ready/위젯을 주입하지 않았다. 최초 private Python property 조회 실패는
  별도 소스/로그로 보존하고, 실제 부착 셀의 하위 텍스트를 읽도록 진단만 보완했다.
- 증거: `Saved/Tests/ItemUIEquipment/7a3a0971f2c241c893fd89dc8d8c2b69/`의
  `Equipment.proof.json`, `Client-*.json`/`Backend-*.json`, `Rpc-*.json`,
  `Observations.log`와 전후 스크린샷이다. 5개 관측 상태/2개 고유 성공 명령을 검증했고
  고정 artifact 52개와 선택 입력 13개의 해시를 기록했다. 에디터/서버가 로드한 동일 DLL의
  SHA256은 `2A3DEF3F2675EC4700D0BE2B4C374AEED92624F22CC90DD57835CD9D98257E61`이다.
  운영 C++/BP/기본 설정/애니메이션 수정, DLL 교체/재빌드나 전체 회귀 반복은 없었다.
  선택 입력의 불변은 소스 트리 전체의 빌드 출처를 증명하지 않는다.
- 자신의 delegate/Slate observer만 해제하고 에디터·자체 PIE/서버·백엔드는 유지했다.
  이 결과는 1인 로컬 개발 인증/Memory/HTTP/Iris/합성 UI 범위다. 서버 GAS 능력치 적용,
  장비 외형/캐릭터 프리뷰, 실물 OS 입력, Steam/다인 격리, PostgreSQL 영속성/Shipping은
  별도다. 다음은 가방 포화 시 해제 차단 등 실패 경계의 실제 UI 검증이다.

### 실제 이름·설명 표시 연결 (2026-10-16, 소스/native 완료·에디터 반영 대기)

- 사용자 요청에 따라 이미 자동화된 가방 포화의 추가 UI 재검증은 보류했다.
  실제 화면/호출부에서 가방과 장비가 Definition ID 앞 12글자만 표시해 같은 종류의
  아이템을 구분하기 어려운 문제를 확인하고 이 사용성 누락부터 수정했다.
- 기존 ViewModel이 기존 `IRPGItemDefinitionViewCatalog`에서 이름·설명을 읽는다.
  인벤토리/장비 셀은 같은 읽기 경계를 사용하며, tooltip은 이름·사용 가능한 설명과
  기존 우클릭/드래그 안내를 표시한다. `FText`를 유지하고 레이아웃/아이콘/프리뷰나
  구형 tooltip 클래스를 확장하지 않았다. 동기 에셋 로드·추가 서비스·서버 RPC/상태
  변경도 없다. 메타데이터가 없거나 ID/버전/유효성이 맞지 않으면 설명을 숨기고
  잘리지 않은 전체 Definition 이름을 표시하며, 알 수 없는 ItemId는 빈 정보를 반환한다.
- 신규 presentation mock 2종과 직접 영향받는 기존 셀 interaction 2종, **4/4 통과·러너 exit 0**.
  native/legacy 출처의 정의 텍스트, 공통 ID 접두어의 구분, 누락·잘못된 버전/ID/유효성,
  셀 tooltip의 fallback·입력 안내·재사용 시 이전 정보 제거와 읽기 전용 동작을 검사했다.
  변경한 UI 구현 3개와 테스트 TU를 별도 컴파일·모듈 링크했으며 처음 테스트의 catalog
  include 누락은 보존/수정한 뒤 해당 TU만 재컴파일했다. reflected 필드/RPC는 변경하지 않았다.
- 증거: `Saved/Tests/ItemPresentation/643d268f0e7945a9b3e6968d2c2a87df/`의
  `Presentation.proof.json`, `Build/` 응답/컴파일 로그와 별도 프로젝트의
  `Automation-f9b03d72c7df40fabdacbf973a8f317f/Item.proof.json`/닫힌 `automation.log`다.
  테스트 DLL은 `089407D49523B7E0652240171375B81EA544210A8CC50BBF4045BDE637801D90`이며
  기존 에디터/서버의 공유 DLL `2A3DEF3F2675EC4700D0BE2B4C374AEED92624F22CC90DD57835CD9D98257E61`은
  교체하지 않았다. quiet link에 없는 로그를 참조한 최초 artifact manifest도 별도 보존했다.
- **당시 GUI는 이전 표시 코드였다.** 이 native 실행만으로 실제 새 이름/tooltip의 화면 검증, 전체 Editor 타깃 빌드나
  새 HTTP/Iris/Steam/DB/Shipping 성공으로 계산하지 않는다. 기존 에디터/서버/PIE·에셋은
  유지했고 후속 빌드·재실행의 표시 확인은 아래에 별도로 기록한다. 이 검증 프로젝트에는 junction이
  있으므로 재귀 삭제하지 않는다.

### 툴팁의 아이템 상태·옵션 표시 (2026-10-24, 구현·빌드/실행 미검증)

가방/장비 셀의 기존 tooltip에 수량·귀속 상태와, 존재하는 경우 잠금·내구도·UTC 만료 시각·
개별 rolled stat을 연결했다. 최대 중첩 수는 ID/버전/유효성이 맞는 정의 view가 있을 때만
사용하며, 정의가 없어도 owner Projection의 상태는 표시한다. 알 수 없거나 비어 있는 ItemId는
빈 상세 정보를 반환하고 셀 재사용 시 이전 tooltip을 지운다.

- `FRPGItemTooltipFormatter`는 복제된 안전한 읽기 모델만 문자열로 바꾼다. 정의/에셋 로드,
  HTTP/RPC·서버 상태 변경, 능력치 적용이나 인벤토리 정렬을 수행하지 않는다.
- 옵션은 전체 StatTag 순서로 안정적으로 표시한다. 현재 별도 표시명/단위 사전이 없으므로
  태그를 자르거나 퍼센트·최종 캐릭터 능력치로 추정하지 않는다. 옵션 수치는 최대 소수
  6자리의 표시용 포맷이며 원본 값은 변경하지 않는다. 유효하지 않은 태그/비유한 값은 숨긴다.
- 만료는 서버 UTC 시각 자체를 표시하며 클라이언트 시계로 만료 여부/조작 권한을 결정하지 않는다.
  `Unbound`를 거래 가능 보장으로 해석하지 않는다. 저장/조작 검사는 기존 서버 정책이 담당한다.
- 기존 `FItemPresentation`에 InstanceDetails를 추가하고 native 셀은 공통 상세 텍스트를 사용한다.
  WBP도 ViewModel의 BlueprintPure `GetItemDetailsText(ItemId)`로 같은 내용을 조회할 수 있다.
  WBP 레이아웃/에셋을 수정하거나 구형 tooltip 클래스를 재사용하지 않았다.

사용자 우선순위에 따라 소스 구현과 직접 영향부 검토/공백 검사만 수행했다. 신규 조회 함수의
UHT/Editor 빌드·native 테스트·MCP/실제 tooltip 실행은 보류했으며, 기존 실행/에셋 증거를
이번 변경의 실행 성공으로 계산하지 않는다. 다음 정상 빌드·에디터 재실행 후 표시 반영이 필요하다.

### 명시적 성장 단계의 기존 툴팁 연결 (2026-11-01)

저장된 `EquipmentProgression`이 명시되어 있고 지원 구간/규칙·0~3 단계 검사를 통과한 경우만
기존 `FRPGItemTooltipFormatter::BuildInstanceDetails`에 `Enhancement: +k`를 추가한다.
누락 상태를 +0으로 추정하거나 이름/StatValues에서 단계를 역산하지 않는다. 이 표시는 강화
가능 여부·새 명령/비용·공격력/최대 체력 또는 GAS 적용을 뜻하지 않는다.

기존 `GetItemPresentation`/`InstanceDetails`→native 셀 `SetToolTipText`와 WBP의
BlueprintPure `GetItemDetailsText` 경로를 그대로 사용한다. 가방/장비/우편 모두 같은
owner read model을 읽고, 새 Revision의 갱신/셀 비우기에서 예전 단계 텍스트를 남기지 않는다.
새 화면·Blueprint 그래프/에셋·RPC·저장/예약/권한·계산된 능력치의 변경은 없다.

원본 formatter1/기존 fixture와 새 표시 사례의 테스트 CPP1만 변경했다. 정상 Editor5작업
(CPP2/링크2/metadata1)·종료0·C++ 경고0·입력1,344 불변, 모듈 `552DB6D7…0C421F19`다.
사설 사본1,167 입력 불변과 실제 모듈 로드를 확인해 native3개(새2/기존 identity fallback1)
각 경고/오류0·Success/UE0을 확인했다. 0~3과 옛 미설정 상태, 실제 native 셀의
가방/장비/우편 +0→같은 ItemId의 새 Revision/+3·비우기 및 Int64 Revision 보존을 검사했다.
기동 Error16/발견 이후0을 구분한다. 검증은 WBP가 사용하는 native 셀의 ToolTipText
속성까지이며 새 단계의 실제 Slate popup/실물 hover 또는 HTTP/GAS 검사가 아니다.

근거는 [ImplementationResult.json](../Saved/BuildChecks/ProgressionTooltip-20261101/ImplementationResult.json),
BuildResult와 `98a6a7c94d524b07b2a47465be681a5c/Verification.proof.json`이다.
소유 UE 종료/파일 사용 해제 뒤 자기 사본/DLL/캐시 **188,473,831 bytes(179.74MiB)**를
자동 폐기했고 임시 경로/정리 예외0이다. 작은 로그/명령·소스/모듈 해시만 남기며 폐기한
사본 해시는 당시 근거다. 정상 출력/원본 소스/유지할 테스트·다른 담당의 작업은 유지하고
본인 Source2/공유 빌드·정상 출력/입력·문서 예약을 해제했다.

### 아이템 아이콘과 수량 배지 (2026-10-24, 구현·빌드/실행 미검증)

기존 가방/장비 native 셀에 아이콘·이름 fallback·수량 레이어를 연결했다. 두 창의 WBP와
61픽셀 기준 가방 격자/84×56 장비 외곽은 유지하며, 아이콘은 42×42 영역 안에서 비율을
유지한다. 중첩 수량이 2 이상이면 오른쪽 아래 `xN` 배지, 1개/빈 칸이면 배지를 숨긴다.
드래그 표시에는 이름/수량을 유지하고 Shift/Ctrl 분할 및 기존 명령 경로는 바꾸지 않는다.

- ViewModel은 ID/버전/유효성이 맞는 기존 definition view의 soft icon 참조만 제공한다.
  Quantity는 기존 owner Projection에서 읽으며 이름/상세 tooltip과 같은 item presentation을
  사용한다. WBP는 BlueprintPure `GetItemIcon(ItemId)`로 같은 참조를 조회할 수 있다.
- 로드는 셀의 StreamableManager 비동기 요청이 담당한다. 이미 로드된 텍스처는 재사용하고
  동기 로드는 하지 않는다. 로딩 중·참조 누락·실패 시 이름을 계속 보여준다.
- 셀 재사용/NativeDestruct/BeginDestroy에서 이전 요청을 취소하고 generation을 갱신한다.
  약한 셀 참조와 generation이 맞는 완료만 표시하므로 늦은 이전 아이콘을 적용하지 않는다.
  다시 부착된 셀은 취소된 로드를 재개하며 빈 칸은 이전 그림/이름/수량을 지운다.
- native 셀 트리의 `ItemIcon`, `ItemFallbackName`, `ItemQuantityText`는 입력을 가로채지 않는다.
  단일 Border→TextBlock을 가정하던 기존 merge/split/move-swap/recovery probe의 읽기 부분만
  새 이름/수량 레이어를 확인하도록 맞췄고 실행·테스트 추가나 새 검증 도구는 만들지 않았다.

사용자 우선순위에 따라 소스 구현/직접 영향부 검토·공백 검사만 수행했다. C++/UHT 빌드와
실제 이미지 로딩·화면/입력 검증은 보류했다. WBP/아이콘 에셋 저장·MCP 조작, 백엔드/복제/
정책 변경은 없으며 다음 정상 빌드·에디터 재실행 후 새 표시가 반영된다. 기존 퀵슬롯의
별도 아이콘 로딩 구현은 이번 범위에서 수정하지 않았다.

### 슬롯의 잠금·처리 중·복구 표시 (2026-10-24, 구현·빌드/실행 미검증)

가방/장비 native 셀에 `ItemStatusText` 상태 배지와 명암/배경색·사유 tooltip을 연결했다.
`LOCK`은 기존 owner Projection의 잠금, `WAIT`는 기존 가방 명령/우편 수령의 로컬 예약,
`CHECK`는 컨트롤러의 미확정 가방 복구 상태다. 우편으로 예약되거나 이동/분할을 기다리는
빈 목적지에도 WAIT를 표시하며, 복구 중 빈 칸에는 CHECK를 반복 표시하지 않는다.

- ViewModel의 BlueprintPure `GetItemSlotPresentationState(ItemId, ContainerType, SlotIndex)`는
  Idle/Locked/Pending/RecoveryRequired/Unavailable을 반환한다. Idle은 특정 장착·소비 정책의
  허용 보장이 아니다. 초기 조회 미완료·신원 회수·잘못된 item/container/slot은 사용 불가다.
- 잠금/예약/복구/사용 불가 셀에서는 우클릭 기본 동작과 새 드래그·드롭을 UI에서 시작하지
  않는다. Widget 자체를 비활성화하지 않아 이름/상태 tooltip은 계속 읽을 수 있다.
  서버 소유권·Revision·정책 검사와 복구 권한을 바꾸거나 잠금 토글을 추가하지 않았다.
- `PendingItemCommandCount` FieldNotify는 기존 이동/교환/합치기/분할과 우편 예약의 추가·
  확정 실패·authoritative 투영 정리·바인딩/권한 종료 시 갱신된다. 성공 ACK만으로 예약을
  해제하지 않는 기존 규칙을 유지한다. 새 소비/장착 요청 추적이나 만료 타이머는 추가하지 않는다.
- 예약 수 알림은 기존 셀의 상태/색상/tooltip만 갱신해 격자·아이콘 비동기 로드·분할 입력을
  재생성하지 않는다. 별도 polling/Tick은 없고, 우편 화면은 기존 행/버튼 갱신 경로를 따른다.
  예약은 기존 ViewModel 범위이며 재오픈된 미확정 상태는 기존 owner 복구 신호를 이용한다.

사용자 우선순위에 따라 구현·직접 영향부 소스 검토/공백 검사만 수행했다. 신규 enum/조회/
FieldNotify의 UHT·C++ 빌드와 native/실제 입력 검증은 보류했다. WBP/BP 에셋 저장·MCP 조작,
백엔드/복제 스키마·명령/영수증/예약 해제 정책 변경은 없으며 다음 정상 빌드 후 반영된다.

### 아이템 명령의 사용자용 결과 안내 (2026-10-24, 구현·빌드/실행 미검증)

가방/장비/우편의 공통 `CommandResultText`는 내부 Operation/enum/축약 RequestId 대신
작업 이름과 사용자용 결과 문구를 표시한다. 요청 전에는 빈 문구이며 긴 안내는 줄바꿈한다.

- `FRPGItemCommandFeedbackFormatter`는 기존 owner ACK의 10개 결과 코드를 순수하게
  표시용 FText로 바꾼다. 이동·교환·스택 합치기·분할·장착·해제·사용·우편 수령·픽업을
  구분하며, 모르는 작업/결과는 내부 문자열을 그대로 출력하지 않고 일반 안내로 표시한다.
- 성공은 **서버 확인**으로 표현하며 클라이언트 투영 반영 완료를 보장하지 않는다.
  AlreadyApplied는 이미 처리된 요청을 중복 적용하지 않았다고 안내한다. Conflict는 현재
  아이템/목적지를 확인하도록 안내하며, InvalidRequest/Forbidden/BackendRejected만으로
  가방 포화·잠금·귀속·자격 만료 같은 세부 원인을 임의로 확정하지 않는다.
- ViewModel의 BlueprintPure `GetCommandFeedbackText()`를 native 창과 작성된 WBP에서
  공통으로 조회할 수 있다. 신원 회수는 예전 ACK 문구를 지우고, 복구 저장 차단/미확정
  가방 명령은 다른 요청의 성공/실패보다 우선한다. 복구 진행 중에는 원래 요청을 확인
  중이라고 표시하며, 미확정 결과를 확정 실패나 새 요청 재전송 안내로 바꾸지 않는다.
- 기존 `LastCommandResult`/RequestId와 FieldNotify는 진단/호출자용으로 유지한다.
  RPC·복제·서버 권한·커밋/영수증/예약 해제·자동 재시도 규칙은 변경하지 않았다.
  이 단계는 서버 ACK와 기존 복구 상태의 표시만 바꾸며, RPC를 보내기 전 로컬에서 거절된
  모든 조작에 새 오류 이벤트를 만드는 작업은 포함하지 않는다.

사용자 요청대로 구현과 직접 영향부 소스/공백 검토에 집중했다. 신규 Blueprint 조회의 UHT와
C++ 빌드·native/실제 화면 검증은 보류했다. 기존 native 부모/`CommandResultText` 바인딩을
사용하므로 WBP/BP 에셋 저장이나 MCP 조작은 하지 않았다. 다음 정상 빌드 후 반영된다.

### 우클릭 조작의 로컬 거절 안내 (2026-10-25, 구현·빌드/실행 미검증)

가방/장비의 기존 우클릭 경로가 RPC 제출 전에 거절한 경우를 공통 하단 안내에 연결했다.
호환 장비 칸 없음, 1회 사용 수량 부족, 사용/장착 동작 없음, 누락/버전 불일치 액션 정의,
해제 목적지의 실제 가방 포화와 빈 칸 전체 예약을 구분한다. 정의/수량/목적지는 기존
정책과 owner Projection/예약으로 판단하며 동기 에셋 로드나 자동 장비 교체를 추가하지 않는다.

- `LocalActionFeedback`는 UI 전용 FieldNotify다. 로컬 거절은 RequestId/예약을 만들거나
  `LastCommandResult`/서버 영수증을 덮어쓰지 않으며 RPC를 보내지 않는다.
- `GetCommandFeedbackText()`는 권한 종료 → 복구 저장 차단/미확정 복구 → 로컬 거절 →
  최근 서버 ACK 순으로 표시한다. 새 로컬 오류가 오래된 성공 문구 뒤에 가려지지 않는다.
- 다음 실제 명령 제출, Projection 갱신, 서버 결과 수신, 명시적 소유자 재바인딩/권한 종료는
  이전 로컬 안내를 지운다. 표시를 지우는 작업이 예약/미확정 명령을 해제하는 것은 아니다.
- native 창은 이 필드의 알림으로 기존 하단/상태만 갱신해 셀/아이콘을 다시 만들지 않는다.
  기존 WBP의 `CommandResultText`를 재사용하며 이번 에셋 수정/MCP 저장은 없다.
  조회 전/잠금/대기/복구 상태의 기존 입력 가드와 tooltip은 유지한다.

기존 interaction 테스트에 정책/수량 거절 안내를 보완하고, 실제 포화와 예약의 구분·서버
결과 보존·안내 우선순위/스냅샷 및 ACK 정리·소유자 종료 사례를 추가했다. 사용자 요청대로
구현과 소스/공백 검토만 수행했으며 새 UHT/C++ 빌드·native/실제 입력 검증은 하지 않았다.
앞선 사용자 빌드/WBP 저장과 구분하고, 미반영 부위명 수정과 함께 다음 빌드에 반영한다.
서버/API/저장/소비 효과·CAS/복구 및 퀵슬롯의 별도 소비 경로는 변경하지 않았다.

### 최신 모듈의 표시·조작 집중 확인 (2026-10-25, 8개 통과)

헤더 경로 수정 후 공유 DLL의 15:46:58 갱신과 재실행한 에디터/MCP의
`LocalActionFeedback` 반영을 확인했다. 자체 UBT 실행/전체 Editor 타깃 성공을 뜻하지 않는다.
기존 `Run-ItemAutomationTest.ps1 -PresentationOnly -SkipSDKSetup`으로 직접 영향부 8개만
별도 프로세스에서 실행했다. 기본 사용/장착·해제, 우편 가용 상태, 빈 장비 부위명, 로컬
거절 안내와 정의 표시/fallback의 7개가 성공하고 공유 소유자/예약 1개가 실패했다.

실패 fixture는 ViewModel만 Ready로 설정하고 투영 컴포넌트는 Uninitialized로 남겨
새 Revision의 예약 정리를 막고 있었으며, 별도로 생성한 RPG Controller의 로컬 역할도
준비하지 않았다. 테스트에만 컴포넌트 Ready/기존 상태 알림과 로컬 역할 설정을 추가했다.
운영 가드/서버 코드·WBP는 변경하지 않았다.

첫 실패와 닫힌 로그는 `Saved/Tests/Item/Automation-6355b15cf928411194623ac45fc4cc6f/`에
보존했다. 첫 검사 직후에는 DLL 일치와 선택 입력 중 테스트 파일만 후속 변경임을 확인했다.
첫 러너 결과는 실패로 유지하고 아래 후속 성공과 구분한다.

후속 공유 DLL의 16:40:31 갱신 뒤 같은 집중 범위를 1회 실행해 **8/8 성공·러너 종료 0**을
확인했다. 공유 소유자/예약과 수정 fixture도 통과했다. 결과는
`Saved/Tests/Item/Automation-0c626a73d4e64889bae2accc7d852eb7/Item.proof.json`과 닫힌
`automation.log`에 있으며 검사/현재 DLL 일치를 확인했다. 자체 UBT 빌드 성공은 아니고,
기동/DDC 진단과 실제 화면/물리 입력·HTTP/DB·재기동 복구/Steam/Shipping은 별도다.
사용자 에디터는 유지했고 자체 검사 프로세스만 종료됐다. 이 테스트 준비 수정에 대한
추가 빌드/재검사 대기는 해소됐다.

### 인증 전 PIE의 실제 가방·장비·우편 표시 확인 (2026-10-25)

기존 사용자 `testmap` Standalone PIE와 최신 모듈을 유지한 채 MCP로 표시 연결을 확인했다.
실제 Controller의 기존 `ShowInventoryProjection`으로 가방을 열고, Slate의 장비 탭 클릭으로
분리 장비 창을 표시했다. 가방 조작 안내/용량 조회 불가, 장비의 14개 부위명/새 안내가
실제 위젯 트리에 나타났으며 장비 제목줄 좌클릭 드래그 후 제목/슬롯 위치가 함께 이동했다.
에디터 콘솔 확인 뒤에는 프리뷰를 전면으로 전환해야 합성 클릭이 해당 창에 전달됐다.

합성 I의 반환값만으로 창 열기를 확인하지 못했으므로 물리 I 입력 성공으로 계산하지 않는다.
당시 Projection은 Uninitialized이며 보유 아이템 0개/Ready나 인증·아이템 조작 성공의
증거가 아니다. 아이템/신원/Ready를 주입하거나 사용·장착·수령 요청을 보내지 않았다.
우편 탭 입력 뒤 상태를 읽기 전에 PIE가 종료돼 당시 우편 표시/창 전환 확인은 미완료였다.
이 첫 관측에서는 StopPIE를 호출하지 않았고 에셋/맵을 저장하지 않았다. 자체 관찰자
`observer_3`는 종료 후 목록에 남지 않은 것을 확인했으며 다른 관찰자는 건드리지 않았다.

남은 재시작 복구 경로는 기존 PostgreSQL 전용 러너를 유지한다. 로컬 3000/3047 health가
응답하지 않고 Docker CLI를 PATH/기본 설치 위치에서 확인하지 못해 실행하지 않았다.
새 DB 생성/저장소 교체·서비스 설치 없이 기존 백엔드/DB 실행 정보가 필요하다.
수정 fixture의 후속 8개 native 성공은 위 절을 참고하며, 인증된 Ready 상태의 실제 조작·
서버 재기동 복구는 아직 별도다.

후속 재개에서 clean인 `/Engine/Maps/Entry`를 확인하고 기존 `testmap`으로 임시 전환했다.
PIE 시작 요청은 시간 초과였으나 뒤 조회에서 Client 1/2 실행과 별도 퀵슬롯 준비 로그를
확인했다. 다른 작업과의 세션 충돌을 피하려고 반복 시작·플레이 중지·Entry 복귀는 하지
않았다. 맵/에셋 저장은 없었고, 그때까지 우편 UI 확인은 미완료였다.

다시 PIE 종료가 확인된 후 clean인 Entry에서 기존 testmap의 Standalone PIE를 시작했다.
게임의 마우스 캡처를 Shift+F1로 해제한 뒤 기존 Controller `ShowRewardMail`로 실제 우편
창을 열었다. `Dungeon Reward Mail`, `Projection: Uninitialized`, `Reward mail is not
ready yet.` 표시를 읽었고 실제 Inventory 탭 → 가방 → Reward Mail 탭 → 동일 조회 전
안내까지 확인했다. 정상 조회의 보상 없음으로 잘못 표시하지 않으며 Ready/첨부 수령
성공은 아니다. 아이템/신원 주입이나 수령 명령·새 빌드/에셋 저장은 없었다.
이번에 시작한 PIE만 종료하고 clean testmap에서 원래 `/Engine/Maps/Entry`로 복귀했다.
자체 `observer_2`/`observer_3` 제거 true와 PIE=false를 확인했으며 에디터는 유지했다.

### 가방 용량과 예약된 빈 칸 표시 (2026-10-24, WBP 저장·C++ 빌드/실행 미검증)

가방 WBP의 `InventoryColumn` 끝에 `InventoryCapacityText`를 추가했다. 기본 글꼴은 기존
Roboto Regular 11, 자동 줄바꿈/입력 비간섭이며 장비/우편 WBP는 수정하지 않았다. 새 native
부모의 optional binding과 ViewModel 조회로 사용 칸/전체 칸·남은 칸·예약된 빈 칸을 표시한다.

- BlueprintPure `GetInventoryCapacity()`는 `FRPGInventoryCapacityPresentation`을 반환한다.
  전체 칸은 기존 서버/UI 공용 `SlotCapacity=80`을 사용하며 가방 확장은 추가하지 않는다.
  Ready owner 스냅샷의 유효한 Inventory 슬롯만 중복 없이 세고 장비/우편은 제외한다.
  스택 수량과 무관하게 한 칸이며 잠금/만료된 항목도 점유 칸으로 남는다.
- 기존 이동/교환/합치기/분할·우편 수령 예약을 그대로 조회한다. 빈 칸에 걸린 예약만
  `ReservedEmptySlots`로 세므로 이미 점유된 칸은 이중 계산하지 않는다. 스냅샷이 있으면
  `OccupiedSlots + ReservedEmptySlots + FreeSlots == TotalSlots`이며, 이동 원본 칸을
  미리 비우거나 성공 ACK만으로 예약을 해제하지 않는다.
- 조회 전/실패/신원 회수는 `bHasSnapshot=false`와 용량 확인 불가 문구로 표시해
  `0/80` 또는 가방 포화로 오해하지 않게 한다. 스냅샷이 있어도 복구/저장 차단·명령 컴포넌트
  부재 때는 조작 불가 안내/회색을 우선한다. 실제 포화는 빨강, 남은 빈 칸이 모두 예약된
  경우는 주황으로 구분한다. `FreeSlots`는 표시용 빈 칸 수이며 새 지급/조작 허가가 아니다.
- 기존 InventoryItems/LoadState/복구 알림과 `PendingItemCommandCount`로 갱신한다.
  예약 수 변화 시 격자/아이콘/수량 입력을 재생성하지 않고 용량 문구만 함께 갱신한다.
  추가 polling·서버 명령·저장/복제 스키마·예약 해제 정책 변경은 없다.

MCP 8000에서 실제 부모/자식과 PIE 중지·에셋 clean/editable을 확인한 뒤 가방 에셋만
변경했다. 수정 전 백업은 `Saved/Backups/InventoryUI/Capacity-f406e7bde1cb48cdb2cff1d43db9bfd3/`
에 보존했다. 현재 로드된 클래스 기준 WBP 컴파일/해당 에셋 저장은 true, 표시명/폰트/
줄바꿈/입력 비간섭 readback과 dirty=false를 확인했다. 사용자 우선순위에 따라 신규 USTRUCT/
조회/binding의 UHT·C++ 빌드와 실제 수치/색상/입력 검증은 보류했다. 에디터의 새 C++ 부모
반영은 다음 정상 빌드·재실행이 필요하며 WBP 컴파일을 새 C++ 검증으로 계산하지 않는다.

### 창 사이 공유 상태·기본 동작 예약·갱신 분리 (2026-10-25, 구현·새 빌드/실행 미검증)

가방·장비·우편 창은 같은 로컬 `ARPGPlayerController`의
`GetInventoryProjectionViewModel()`을 사용한다. 창을 추가로 열거나 다시 생성해도 동일
모델의 예약/최근 결과/복구 상태를 유지하며, 창 하나의 해제가 다른 창의 모델을 초기화하지
않는다. 현재 Controller/Projection/Command component/PlayerState 바인딩이 달라지면
재연결하고, Controller EndPlay에서 로컬 핸들/구독을 정리한다. 종료 뒤 getter로 모델을
다시 만들지 않는다. 일반 APlayerController를 사용하는 테스트/독립 화면은 기존 개별 모델
fallback을 유지한다. 이는 클라이언트 UI 수명 관리이며 서버 journal/DB 커밋 취소가 아니다.

- ViewModel을 통하는 사용·장착·해제도 RPC 전 원래 ItemId/int64 Revision·출발/목적 칸을
  예약한다. 같은 아이템의 재사용/이동/장착·중복 해제와 다른 아이템의 같은 장비 칸 선점을
  막는다. 장착 정책의 첫 빈 칸 선택, 가방 목적지 선택/용량과 우편 사전 안내도 기존 예약과
  함께 계산한다. 기본 세 동작의 예약은 `PendingItemCommandCount`에 포함된다.
- 성공/AlreadyApplied ACK만으로 새 예약을 풀지 않는다. 단일 아이템의 새 authoritative
  Revision 또는 제거가 원래 스냅샷을 폐기하면 해제하며, 확정 거절은 해당 요청만 정리한다.
  Uncertain은 확정 거절로 취급하지 않는다. 신규 장착/소비 서버 재전송·영속 복구 프로토콜이나
  타임아웃 해제는 추가하지 않았다. 퀵슬롯의 별도 소비 제출 경로를 이 UI 예약으로 바꾸거나
  서버 CAS/효과 멱등성 검사를 대체한 것은 아니다.
- 빈 장비 칸도 native 셀로 구성하고 실제 장비 SlotIndex를 전달한다. 예약된 빈 장비 칸은
  WAIT, 빈 칸은 기존 부위명을 유지한다. 장비 칸 드롭이나 자동 장비 교체는 추가하지 않는다.
- InventoryItems/EquipmentItems/MailItems 알림만 내용을 다시 만든다. 조회/최근 결과·보상
  안내·예약/복구 버튼 가용성 알림은 기존 셀의 상태·툴팁·용량/버튼만 갱신해 아이콘 로드,
  우편 행/포커스를 재생성하지 않는다. 수량 선택 입력은 원본/목적지가 무효해졌을 때만
  기존 검증으로 취소하며 상태 알림 때문에 다시 만들지 않는다. 추가 Tick/polling은 없다.
- 우편의 빈 목록은 Ready에서만 보상 없음으로 표시하고 조회 전/실패/권한 종료를 구분한다.
  UI 사전 상태가 최종 서버 허가를 뜻하지 않는 기존 계약은 유지한다.

기존 interaction 테스트 파일에 공유 getter의 예약 보존/명시적 소유자 해제와 사용·장착·
해제의 아이템/장비/가방 예약·중복 거절·ACK 선도착·확정 거절·새 Revision 정리를 보는
집중 사례를 추가했다. 소스/직접 영향부·공백만 검토했으며 UHT/C++·native/UI 실행은 하지
않았다. 앞선 사용자 빌드의 DLL과 구분하며 정상 빌드 후 반영해야 한다. 서버/API/복제·저장
스키마와 기존 journal 권한/영수증은 수정하지 않았다.

### 퀵슬롯 아이콘의 비동기 읽기 (2026-10-25, 구현·새 빌드/실행 미검증)

아이템 퀵슬롯의 수량/항목 갱신에서 `LoadSynchronous()`를 제거했다. 기존 definition view의
유효성과 ID/버전이 일치할 때만 soft icon을 사용하며, 로드된 텍스처/같은 항목의 진행 중인
요청은 재사용한다. 누락/실패/로딩 중에는 그림을 투명하게 두고 기존 수량·키 표시는 유지한다.
새 아이콘/아트/위젯 자산을 만들거나 스킬 아이콘·쿨다운 경로를 이관하지 않는다.

슬롯 ID/soft 참조가 바뀌면 이전 로드를 취소하고 generation을 갱신한다. 약한 위젯과
generation/ItemId가 일치하고 아이템 슬롯인 콜백만 그림을 적용한다. 바인딩 해제/빈 슬롯/
NativeDestruct/BeginDestroy에서도 취소하며, 재부착 시 기존 assignment로 다시 읽는다.
이미지 알파 갱신도 실제 brush가 없을 때 빈 그림을 노출하지 않는다. 소비/등록/드래그 RPC·
아이템 상태는 변경하지 않았고 실제 비동기 완료/슬롯 교체·GC·입력 검증은 다음 빌드 후다.

MCP 8000의 첫 확인 때는 PIE 실행 중이라 플레이/미저장 에셋을 보존했다. 2026-10-25
후속 재개에서 PIE 종료·가방 에셋 clean/editable을 확인하고 `InventoryInteractionHint`만
세 줄로 갱신했다. 좌클릭 드래그 이동/교환/동일 스택 합치기, Shift 반분/Ctrl 지정 수량,
우클릭 사용/장착·퀵슬롯 등록을 현재 native 입력에 맞춰 안내한다. 글꼴/자동 줄바꿈/격자와
다른 에셋은 수정하지 않았다. 수정 전 사본은
`Saved/Backups/InventoryUI/InteractionHint-2740fdb1/WBP_InventoryProjection.uasset`에 보존했다.
가방 WBP 컴파일/해당 에셋만 저장은 true, 문구 원문 일치·실제 세 줄/autoWrap=true와
dirty=false를 확인했다. 이것은 현재 로드된 부모 기준 WBP 저장 확인이며, 새 공유 모델/
예약·갱신/아이콘 C++의 UHT·링크나 실제 화면/입력 검증으로 계산하지 않는다. 다음은
에디터 저장·종료 후 정상 Editor 빌드와 필요한 실제 화면 확인이다. 실행 중 DLL 교체나
중복 빌드/광역 테스트는 하지 않았다.

### 장비 빈 칸의 부위명 보존 (2026-10-25, 소스 수정·새 빌드 대기)

빈 장비 칸을 native 셀로 전환한 뒤 `SetItem`의 빈 ItemId 분기가 전달된 부위명까지
지우고 있었다. 장비 칸은 HEAD/CHEST 등 전달된 이름을 보존하고, 빈 가방 칸은 이전
이름을 계속 지우도록 수정했다. 빈 칸의 조작 허용, WAIT/예약, 아이콘 로드나 서버 명령은
변경하지 않았다. 기존 interaction 테스트 파일에 빈 장비 부위명/재사용·빈 가방 전환과
명령 미제출 사례를 추가했으며, 소스/공백만 검토하고 새 빌드/실행은 하지 않았다.

앞선 공유 모델/예약·비동기 아이콘 묶음은 사용자가 후속 빌드 성공을 보고했고 공유 DLL의
2026-10-25 03:04:00 갱신을 확인했다. 이번 부위명 수정은 그 뒤의 소스 변경이다. 재실행한
에디터/MCP 8000에서 가방/장비 WBP의 분리 구조와 기존 안내·용량 행을 읽었다. 장비 창에
제목줄 좌클릭 드래그/우클릭 해제 안내를 추가하려 했으나, 변경 직전 PIE 실행을 확인해
당시 에셋 수정/저장을 하지 않고 기존 플레이를 유지했다.

후속 사용자 PIE 종료 안내 후 MCP에서 PIE=false·장비 WBP clean/editable을 확인하고,
`MainColumn`의 상태 행과 장비 본문 사이에 `EquipmentInteractionHint`를 추가했다.
제목줄 좌클릭 드래그 이동/우클릭 장비 해제(가방 빈 칸 필요)를 두 줄로 안내한다. 가방
안내의 Regular 11 글꼴/색상·자동 줄바꿈을 재사용하고 HitTestInvisible로 입력을 가로채지
않는다. 장비 외곽 크기·슬롯/미리보기와 가방 에셋은 변경하지 않았다. 수정 전 사본은
`Saved/Backups/InventoryUI/EquipmentHints-57a464c2/WBP_EquipmentProjection.uasset`에 보존했다.
장비 WBP만 컴파일/저장 true, 문구/두 줄/배치 순서 readback과 dirty=false를 확인했다.
이것은 WBP 저장 확인이며 앞선 부위명 C++ 수정의 새 빌드나 실제 화면/입력 검증은 아니다.

### 새 빌드의 이름 표시·설명 데이터 확인 (2026-10-16, 툴팁 팝업 미검증)

- 사용자 빌드/재실행 후 GUI와 별도 개발 서버가 같은 게임 모듈
  `B11ADAC5F1E88A4381ED164A5B6BEBF2CA52620D04343670E80A4C12ABAED127`을 로드한 것을 확인했다.
  MCP는 `8000`, Development/Memory 게임 백엔드는 `localhost:3000`이다.
- 최초 검증 호스트의 구형 백엔드는 새 티켓 입장 계약과 맞지 않아 HTTP 200 뒤
  `invalid_join_ticket`으로 거절됐다. 실패/Connecting timeout 기록을 보존하고, 자신이
  시작한 Memory 백엔드/서버만 현재 이미 빌드된 백엔드 사본으로 교체했다. 사용자 DB나
  다른 프로세스는 변경하지 않았으며 인증 정책을 완화하거나 새 빌드를 수행하지 않았다.
- 기존 native 개발 로그인·소유 조회·public 세션 복원·Join Ticket 입장으로 remote
  owning client의 Ready를 확인했다. 실제 가방에 `Large Red Health Potion`과
  `Leather Helm`이 표시되고, 부착 셀의 `tool_tip_text`에 이름·정의 에셋과 일치하는
  설명·기존 입력 안내가 전달됐다. 아이템 명령은 0개이며 인증 GET/Projection의
  포션 3개·투구 1개/rev 1/원래 슬롯은 전후 동일했다. 아이템은 개발 서버 Grant로
  준비한 fixture이며 자연 획득 증거는 아니다.
- 이번 창 열기는 기존 public Controller 동작을 사용했다. 합성 I 입력의 창 열기와
  합성 hover 뒤 실제 tooltip 팝업 렌더링은 관측하지 못했으므로 성공으로 표시하지 않는다.
  장비 창 표시/장착·소비·포화 검증과 이전 native 4종을 반복하지 않았다.
- 증거: `Saved/Tests/ItemPresentationUI/c6f9e47257984e859b1fd163c0af5775/Current/`의
  `NameUI.proof.json`(`PASS_NameUIAndTooltipData_PopupUnverified`), `Presentation-*.json`,
  전후 `Backend-*.json`, `named-inventory-image.png`, `Observations.log`다.
  고정 artifact 44개의 해시를 재확인했고 자신의 관측만 해제했다. 실행 환경은 유지했다.
  이번 확인에서 운영 C++/BP/설정 변경은 없으며 전체 타깃 자체 빌드·실물 OS 입력·
  Steam/PostgreSQL 영속성/Shipping 성공은 아니다. 다음은 실제 마우스 hover의 팝업 확인이다.

### 툴팁 팝업의 분리 관측 (2026-10-17, 사용자 실물 hover 확인)

- MCP `8000`/공유 DLL `95D55E1F…`, 기존 PIE 없음·dirty 패키지 0개를 확인했다.
  인증/아이템 서버를 재준비하지 않고 자체 Standalone Entry PIE에 동일한 네이티브 셀
  클래스의 **UI-only 임시 미리보기**를 만들었다. 기존 포션 에셋의 이름·설명을 tooltip에
  설정했으며 `SetItem`/ViewModel/Projection/신원/Ready/아이템 명령은 사용하지 않았다.
- 합성 hover 뒤 실제 셀의 `is_hovered() == true`와 cursor 표시를 관측했다.
  `Slate.EnableTooltips=true`/summon delay 약 0.15초를 읽었으나 설정은 바꾸지 않았다.
  팝업은 snapshot/이미지에서 관측되지 않았고 에디터 기본 저장 버튼의 비교 hover에서도
  관측되지 않았다. 이 결과만으로 셀 코드 결함이나 정상 팝업 표시를 단정하지 않는다.
- Python의 internal-use Create 바인딩 실패는 보존하고 `new_object`/owning player와
  엔진의 정상 Rebuild/Initialize 경로를 사용하도록 임시 스크립트만 보완했다.
  운영 코드·BP·설정 수정/빌드·백엔드 시작·기존 인증/조작 회귀 반복은 없었다.
- 증거: `Saved/Tests/ItemTooltipPopup/d60a9f9ba04e43fc81378db02f14ece1/`의
  `PopupDiagnostic.proof.json`, `Preview.py`, `Observations.log`, hover RPC와 두 비교 이미지다.
  고정 artifact 16개와 관측 해제를 확인했다. 자신의 Slate observer 두 개만 제거하고,
  사용자 실물 마우스 확인을 위해 `UI-ONLY TOOLTIP TEST` 셀/자체 PIE는 임시 유지했다.
  이후 사용자가 실제 마우스 확인 요청에 **“팝업뜨는데?”**라고 응답해 임시 셀의 팝업
  표시를 확인했다. MCP 자동 관측 성공이나 실제 인증 인벤토리 전체 검증으로 확대하지 않는다.
  이 증거 범위에 따라 게임 코드/설정 수정은 하지 않았다. `UserConfirmation.json`에 사용자
  보고를 따로 기록하고 기존 미관측 proof는 보존한다. `Cleanup.py`로 동일 world/Controller/
  셀 경로를 대조한 뒤 `Preview.stop()`과 해당 자체 PIE만 종료했으며 MCP에서 PIE 종료를
  확인했다. 에디터는 유지하고 정리용 자신의 observer 하나도 해제했다.

### 가방·장비 WBP 외형 통일 (2026-10-28, 에셋 작성·디자이너 확인)

MCP 8000으로 기존 `WBP_InventoryProjection`·`WBP_EquipmentProjection` 두 에셋만
편집·컴파일·저장했다. 수정 전 바이너리는
`Saved/Backups/InventoryUI/BagEquipmentStyle-20261028/`에 보존했다.

- 가방 720×640·장비 560×620, 기존 native 부모/필수 변수/위젯 계층을 유지했다.
  어두운 창 배경·금색 20 Bold 제목·X 버튼·11포인트 안내/상태 글꼴과 여백을 통일하고
  기존 조작 안내를 한국어로 정리했다. Blueprint 그래프 작성은 하지 않았다.
- 가방은 런타임 10열에 맞춰 61×61 최소 셀과 사방 2 패딩을 유지한다. 비활성 장비
  열은 계속 Collapsed이고 Fill 0으로 디자이너의 불필요한 폭을 없앴다. 숨겨진 탭/열의
  디자이너 표시만 별도로 정리했으며 런타임에서 다시 활성화하지 않았다.
- 장비는 좌우 7개씩의 기존 슬롯/중앙 미리보기 영역을 유지하고 기본 편집용 문구를
  정리했다. 중앙의 캐릭터 미리보기는 **준비 중인 자리표시자**이며 3D 구현은 아니다.
- 두 에셋 컴파일 true·저장 true·dirty=false, 전후 위젯 계층 서명 일치와 최종 디자이너
  이미지를 확인했다. I/P·제목줄 좌클릭 드래그·앞 레이어·닫기 및 아이템 명령의 native
  구현은 수정하지 않았다. 실제 인증 게임의 표시/입력·80칸 조작은 이번에 재검증하지 않았다.
- C++ 빌드·native/서버 E2E 반복·API/DB 변경 없이 기존 DLL을 유지했다. 우편/HUD는
  이번 에셋 편집 대상이 아니며, 이 구간 시작 시 해시를 수집하지 않아 전후 동일성을 주장하지 않는다.

근거: `Saved/BuildChecks/BagEquipmentStyle-20261028/Authoring.proof.json`,
`Inventory-Designer-Layout.png`, `Equipment-Designer-Final.png`. 그래프 DSL은 전후 빈
내보내기여서 전체 직렬화 그래프의 바이트 동일성 근거로 확대하지 않는다.

### 새 WBP 표시·입력 마무리 (2026-10-29, 사용자 지정 2·3번 완료)

사용자가 선택한 실제 표시/입력 확인과 발견된 UI 문제 보완만 마무리했다. 상단 우편 버튼
외형 변경·3D 캐릭터 프리뷰·고급 우편은 이번 범위에서 제외했다.

- 2026-10-28의 인증된 owning client/HTTP·Iris·Memory 실행에서 실제 가방·장비·우편 WBP와
  행 클래스를 확인했다. 우편 수령 버튼 클릭 1회 뒤 원래 첨부 ID/수량 2를 보존해 가방
  2번 칸으로 이동하고 revision 1→2·우편 1→0·pending 0과 서버 확인 결과를 관측했다.
  같은 실행의 겹친 가방/우편 제목 클릭 전후 GPU 이미지로 앞 레이어 변경도 확인했다.
- 그 실행에서 발견한 우편 하단의 `텍스트 블록`은 `TextBlock_4`의 native 결과 안내
  바인딩 누락이었다. 해당 에셋만 백업 후 `CommandResultText`/변수 바인딩과 11 Regular·
  빈 초기 문구/줄바꿈으로 보완해 컴파일·저장 true·dirty=false 및 실제 수령 결과 표시를
  확인했다. C++·서버 권한·수령/복구 정책이나 HUD 외형은 수정하지 않았다.
- 2026-10-29 재개 시 DLL과 관련 에셋 5개의 해시가 위 수정 후 관측과 일치했다. 인증/수령은
  반복하지 않고 기존 `testmap`의 **Uninitialized·UI-only Standalone**에서 남은 조작을
  분리했다. I/P 열기·재토글, 우편 버튼, 세 창의 제목줄 좌클릭 드래그, Esc로 가장 앞의
  가방만 닫기, 각 X와 최종 커서/이동·시점 잠금 복구를 16개 상태/드래그 3개로 확인했다.
  기존 readonly 확인으로 공통 모델·80칸/14칸·동일 창/투영/명령 결과 보존과 새 아이템 명령
  0을 확인했다. 이는 새 Ready/실아이템·PostgreSQL/Steam/물리 입력 검증은 아니다.
- 초기 Entry의 일반 Controller와 콘솔 포커스 때문에 실패한 자동 입력, 이전 중단/전투 실패
  모달 구간은 성공으로 바꾸지 않았다. 실제 제목줄 클릭 뒤 통과한 구간과 구분해 보존했다.
  자체 PIE/관찰자는 정리하고 원래 `/Engine/Maps/Entry.Entry`·dirty 패키지 0으로 복귀했으며
  사용자 에디터와 다른 담당의 프로세스는 유지했다. 새 빌드/native·서버 E2E 반복은 없다.

근거: `Saved/Tests/ItemUIFinal-20261028/Finish-20261029/Verification.proof.json`과 같은 폴더의
상태/드래그 JSON·PNG. 실제 수령과 레이어 전환의 기존 근거는 `Final/Client-Claimed.json`,
`Final/Backend-Claimed.json`, `Final/UI-Claimed.png`, `Final/UI-{Bag,Mail}Front.png`다.
사용자 지정 UI 마무리 범위는 종료하며 선택 기능을 자동으로 다음 필수 작업으로 올리지 않는다.

### 독립 창·P키·상단 우편 버튼 (2026-10-25, 구현·집중 검증)

가방·장비·우편을 하나의 활성 탭으로 취급하던 Controller 상태를 제거했다. 기존 세 창과
공유 ViewModel은 유지하며 각 창을 동시에 열고 개별적으로 닫는다. I의 기존 가방 입력은
가방만 토글하고, 별도 P 입력은 장비만 토글한다. P는 현재 고정 단축키이며 새 키 설정
화면/사용자 리매핑 기능은 추가하지 않았다. 창 내부에 포커스가 있어도 preview에서 P/I/Esc를
처리하고 반복 이벤트와 게임 입력으로의 중복 토글을 막는다.

- 가방 오른쪽·장비 왼쪽·우편 가운데에 최초 배치하며 제목줄 좌클릭 이동을 유지한다.
  이미 열린 창을 다시 선택할 때 배치·드래그 이동량이나 아이템 컨트롤을 초기화하지 않는다.
- 기존 공통 `FRPGWindowInputState`에 선택 순서와 레이어를 연결했다. 패널의 preview mouse
  down은 자식 버튼/슬롯 클릭을 소비하지 않고 해당 창을 앞으로 올린다. UE 5.8의
  `SetWidgetSlot`만으로 바뀌지 않는 실제 Slate viewport overlay 슬롯도 같은 순서로 갱신하며
  위젯 제거/재생성이나 무한 증가 Z-order를 사용하지 않는다. 스킬 창도 같은 순서를 따른다.
- X는 해당 창만, Esc는 가장 앞의 창 하나만 닫는다. 나머지 창이 있으면 커서/입력 잠금을
  유지하고 마지막 창이 닫힐 때 기존 counted-lock/이전 포커스 복구를 적용한다. 개별 창을
  닫아도 공유 모델의 미확정 명령/예약을 초기화하거나 서버 작업을 취소하지 않는다.
- MCP 8000으로 가방·장비 WBP 제목줄에 X를 추가하고 기존 TabRow를 접었다. native 우편도
  탭 대신 같은 제목줄/닫기 구조를 사용한다. 초기 조회 재시도 버튼은 숨겨진 TabRow 밖에
  배치해 실패 상태에서도 접근할 수 있다.
- 기존 `WBP_PlayerHUD` 오른쪽 위에 116×36 임시 `우편` 버튼을 추가했다. OnClicked →
  GetOwningPlayer → RPGPlayerController 캐스트 → 기존 ShowRewardMail 호출만 연결한다.
  다른 창을 닫지 않고 우편을 열거나 앞으로 올린다. 기존 게임 커서 표시 정책은 바꾸지 않았다.

에셋 수정 전 PIE=false/clean/editable 확인과 `Saved/Backups/InventoryUI/IndependentWindows-3acf095d`
백업을 수행했다. 세 WBP 각각 컴파일/명시적 저장 true와 dirty=false, 제목줄/레이아웃·
버튼 그래프 연결을 읽어 확인했다. `VerifyInventoryWindowPIE.py`의 단일 창 가정을 제거하고
세 창의 공통 모델·각 배치·닫기 버튼을 확인하도록 수정했으며 로컬 Python 구문 검사를 통과했다.
독립 창의 선택 순서/실제 overlay 재정렬, P 토글·창별 닫기/최종 입력 복구 집중 native 사례를
추가하고 기존 제목줄 검사에 X를 포함했다. 후속 2026-10-25 23:22:27 공유 DLL에서 기존
`Run-InventoryWindowDragAutomationTest.ps1 -IncludeIndependentWindows`를 실행해 새 2개와
제목줄 관련 3개, 합계 5/5 성공·러너 종료 0·실행 전후 DLL 해시 일치를 확인했다.
증거는 `Saved/Tests/InventoryWindowDrag/e26a4ae45b714932bca3b9b26606f6e5/`의 닫힌 로그와
리포트다. 해당 DLL SHA256은 `923A0679319CEC12E28B2387FE411F54982E43B2CC2265C4AA513BEEDB41D3A1`이며
자체 전체 Editor 빌드 성공이나 이후 교체된 DLL의 재검사로 확대하지 않는다.

같은 GUI의 Standalone에서 상단 우편 버튼 클릭과 P/I 합성 입력의 독립 창 표시, 우편 제목·
가방 본문 클릭에 따른 실제 겹침 순서 변경을 확인했다. 위 폴더의 `UI-bag-front.png`,
`UI-mail-front.png`, `UI-bag-body-front.png`가 GPU 관측이다. 기존 readonly Python 확인도
세 창의 공통 모델/owner snapshot 일치·가방 80칸/장비 14칸·배치/X를 통과했다.
이는 Uninitialized/인증 전 화면이며 물리 키보드나 실제 아이템 장착·사용·우편 수령의
새 창 통합 성공이 아니다. 서버/API/아이템 권한·저장/복구 프로토콜은 변경하지 않았다.

2026-10-26 후속 실아이템 확인 준비에서는 MCP 8000의 기존 에디터/Entry·PIE=false를
확인했다. 기존 3053 검증 백엔드는 건드리지 않고 별도 3000 백엔드를 준비했으며,
자체 .NET 빌드 경고/오류 0과 기존 PostgreSQL 스키마 준비 로그를 확인했다. 그러나 HTTP
준비 조회가 타임아웃되어 계정/세션 생성·아이템 seed·UE 서버 기동·클라이언트 입장 전
준비를 중단했고 자체 백엔드는 종료했다. 추가 기동 진단도 실행 정책에서 거절돼 우회하지
않았다. `Saved/Tests/ItemWindowLive-mvorkxk3/`의 준비 스크립트/빌드·기동 로그는 실패
기록이며 실연결 PASS가 아니다. 에디터/다른 프로세스는 유지하고 새 PIE나 에셋 저장·
기존 native/UI 검사 반복은 하지 않았다. 다음은 접속 가능한 기존 개발 백엔드 환경에서
새 독립 창의 실제 아이템 조작·동시 갱신을 확인하는 것이다.

#### 인증 입장과 실아이템 표시 (2026-10-26, 조작 전 단계 확인)

사용자 요청으로 실행한 기존 PostgreSQL 백엔드 `localhost:3000`의 readiness 200/ready를
재사용해 별도 개발 계정·캐릭터·배정 서버를 준비했다. native 개발 로그인 → 인증된
소유 캐릭터 조회/선택 → 기존 세션 복원 → Join Ticket/실제 클라이언트 이동으로 authority
없는 owning client의 Ready를 확인했다. 서버 API로 준비한 가방 물약 3개/투구 1개·우편
물약 2개와 기존 시작 장비 1개를 읽었으며 자연 플레이 획득이나 Steam 인증 증거는 아니다.

가방·장비·우편을 동시에 연 상태에서 기존 readonly 확인의 공통 모델·Ready/배치·80칸/
14칸 검사를 통과했다. 별도 플레이어 인증 GET `/api/items`로 읽은 4개 레코드의 ID/수량/
Revision/컨테이너/슬롯/정의가 클라이언트 Projection과 일치했고 기본 pending/명령 수는
각각 0이었다. **장착·해제·사용·수령 명령은 아직 발생하지 않았으므로 조작 뒤 동시 갱신
검증을 완료했다고 계산하지 않는다.**

장착 입력 직전에 GUI 에디터가 정상 종료되었고 MCP 8000 리스너도 종료됐다. 종료 원인은
추정하지 않으며 자체 종료 호출은 하지 않았다. 실행 중인 백엔드/DB와 다른 프로세스를
유지하고 PID/시작 시각이 일치하는 자체 UE 서버만 정리했다. 원래 에디터가 종료되어
맵 복귀/관찰자 해제 호출은 하지 않았다. 다음은 에디터 재실행 뒤 기존 테스트 대기 모드로
보스 전투가 UI 조작에 간섭하지 않게 준비하고 실제 장착·해제부터 확인하는 것이다.

증거는 `Saved/Tests/ItemWindowLinked-mvosakj5-ddc-api/`의 `Fixture.json`,
`Client-Initial.json`, `Backend-Initial.json`, `ReadyObservation.json`과 서버 로그다.
`ReadyObservation`은 단계 관측이지 전체 PASS가 아니다. DLL 해시는 fixture와 일치했지만
소스 전체 빌드 출처로 확대하지 않는다. 앞선 엔진 DDC 기동 실패와 오래된 조회 URL의
404는 별도 디렉터리에 보존했고, 테스트 서버의 임시 메모리 DDC/보조 스크립트 조회만
보완했다. UI 관측의 Python `get_pawn` 미노출도 기존 GameplayStatics 조회로 수정했다.
운영 C++/BP/API·권한/아이템 복구 규칙은 바꾸지 않았고 과거 집중 검사는 반복하지 않았다.

### 독립 창의 실아이템 장착·해제·사용·수령 (2026-10-26)

앞선 조작 전 관측의 후속으로, 기존 PostgreSQL 백엔드와 새 검증용 계정/배정 서버를
사용해 실제 native 개발 로그인·소유 캐릭터 조회/선택·세션 복원·Join Ticket/Iris 입장을
진행했다. 서버의 기존 opt-in 전투 대기 fixture만 사용하고 gate 파일을 만들지 않아
보스 전투 간섭을 제외했다. 운영 AI/맵/타이머·입장 인증 정책은 변경하지 않았다.

가방·장비·우편을 모두 연 채 실제 부착 셀/버튼에 MCP Slate 입력으로
장착 → 해제 → 포션 1회 사용 → 우편 1건 수령을 수행했다.

- 투구의 동일 ID/수량 1이 가방 → HEAD → 원래 빈 가방 칸으로 이동하고
  Revision 1→2→3, owning client의 HEAD snapshot 생성/제거가 일치했다.
- 포션은 같은 ID에서 수량 3→2/Revision 1→2로, 우편은 같은 ID/수량 2를
  보존한 채 Mail → 가방 빈 칸/Revision 1→2로 바뀌었다. 우편 목록은 비었다.
- Initial/Equipped/Unequipped/Consumed/Claimed의 5단계에서 세 창의 같은 ViewModel,
  Ready·80/14칸·pending 0과 플레이어 인증 GET/Projection의 4개 레코드가 일치했다.
  네 고유 RequestId의 Committed 영수증을 배정 서버 자격으로 별도 조회해 결과 레코드와
  대조했고, 시작 대검 및 각 아이템의 정의·소유·시드·옵션·메타데이터가 보존됐다.

증거는 `Saved/Tests/ItemWindowsActions-mvowpaag/`의 `Verification.proof.json`,
5쌍의 Client/Backend 관측, `Receipt-0.json`부터 `Receipt-3.json`,
`EditorObservation.json`, `UI-Final.png`, 서버 로그 및 `CleanupObservation.json`이다.
실행 DLL `2CC7545E…BCFB95`의 시작/종료 해시가 일치했다. 로컬 준비의 Python 미노출
조회와 정리 스크립트의 실행 scope/이미 파괴된 컴포넌트 참조 오류는 별도로 보완했으며
이를 제품 오류 또는 전체 오류 0건으로 계산하지 않는다.

자체 PIE/서버만 종료하고 Python 참조/관측을 정리한 뒤 원래 clean bossmap으로 복귀했다.
에디터·기존 백엔드/DB는 유지했다. 집중 검증 범위에 맞춰 과거 native/UI 검사를
반복하지 않았고 운영 C++/WBP/API 수정이나 재빌드는 없었다. 개발 인증·로컬 PostgreSQL/
합성 UI 입력 범위이며 실제 Steam·물리 입력·다인/손실·자연 전투·이번 창 변경의 재기동
영속성·Shipping 완료로 확대하지 않는다.

## 구형 인벤토리 실행 경로 제거

`URPGInventoryComponent`는 기존 Blueprint의 부모 클래스와 RPC 이름을 보존하는
호환 파사드다. 두 인벤토리를 병행 실행하는 구현은 아니다.

- 구형 FastArray 목록, 아이템 서브오브젝트 복제, 별도 위젯 생성, 로그인 시 구형
  JSON 조회, 지연 저장/종료 저장 코드를 제거했다. GameMode는 새 Projection만 로드한다.
- 구형 픽업 호출은 `ServerPickupItem`으로 전달한다. 구형 소모 호출은 기존 객체의
  ID가 서버의 현재 Inventory Record에 있을 때만 그 Record의 Revision으로
  `ServerConsumeItem`을 호출한다. 객체의 수량·Manifest·스탯은 판정에 사용하지 않는다.
- 구형 드롭·이동·분할·스택 전송·정렬은 상태를 변경하지 않고 거부한다. 이 기능은
  새 비동기 서버 명령/UI에 아직 연결되지 않았다. 특히 Manifest를 월드에 스폰하고
  픽업 때 새 ID를 발급하는 우회 경로를 차단했다.
- `GetAllItems`는 빈 배열, `GetInventoryMenu`는 null을 반환한다. 구형 토글은
  PlayerController의 새 Projection 창을 사용한다.
- 기존 DB의 구형 저장 데이터와 Blueprint 자산은 삭제하거나 자동 변환하지 않는다.
  구형 저장 아이템만 있던 캐릭터는 검증된 데이터 마이그레이션 전까지 새 UI에서
  그 아이템을 사용할 수 없다. 기존 저장 데이터가 새 Record로 자동 승격되는 것은 아니다.

`ProjectRPG.Item.LegacyRetirement`의 테스트 2개는 별도 목록/복제 상태 부재,
구형 쓰기의 전체 native Record 불변, 미이전 ID 거부 및 기존 ID의 새 명령 전달을
검증한다. 소모 테스트의 서버에는 인증·보안 컴포넌트·백엔드가 없으므로 실제 소모
성공을 검증하는 테스트는 아니다.

후속 장비 조회 전환에서 스킬과 무기 타격 검사는 새 장비 Snapshot을 사용하도록 옮겼다.
2026-10-05 구형 장비 변경 함수도 native 명령으로 전달하거나 raw 쓰기를 거부하도록
정리했다. 두 구형 장비 컴포넌트는 별도 목록·효과·복제가 없는 호환 파사드다.
반영 타입·기본 컴포넌트·자산 참조는 아직 유지하며 자동 데이터 이전은 하지 않는다.
상세 계약은 `Docs/ITEM_ASYNC_COMMANDS_AND_EQUIPMENT.md`의 장비 조회 전환 절에 있다.

2026-10-05 후속 검증: 최신 Editor Development DLL 링크 성공, Item 39개와 Skill
19개(신규 구형 장비 호환 테스트 3개 포함), 총 58/58 통과. 관련 Blueprint 15개
메모리 컴파일과 자산 SHA-256 불변도 확인했다. 별도 RPGClientUI의 GameFeatureData
누락은 해결하지 않았으며 최종 검증 실행에서만 `-DisablePlugins=RPGClientUI`로
제외했다. 기본 설정의 전체 UI 초기화나 실제 입력/멀티플레이 성공을 주장하지 않는다.
로그와 검증 범위는 위 상세 계약 문서의 2026-10-05 검증 기록에 있다.

2026-10-04 검증: Editor Development 빌드 성공(exit 0), `ProjectRPG.Item` 전체
자동화 테스트 32/32 통과(exit 0). 관련 Blueprint 6개(기존 Component/Controller/
Spatial UI/Grid와 새 Inventory/Equipment 창)의 메모리 내 컴파일이 성공했다.
Blueprint 검증 전후 6개 자산의 SHA-256은 동일하다. 실제 UI 입력·멀티플레이·
기존 DB 데이터 마이그레이션은 이 검증 범위가 아니다.

로그: `Saved/Logs/LegacyInventoryRetirementBuild-20261004.log`,
`Saved/Tests/LegacyInventoryRetirement/Automation/automation.log`,
`Saved/Tests/LegacyInventoryRetirement/Blueprints/blueprints.log`.
자산을 저장하지 않는 재검증 스크립트: `Scripts/Editor/ValidateInventoryCompatibility.py`.

### 미사용 HTTP 함수 정리 (2026-10-11)

프로젝트 C++ 참조가 없는 `UHttpWebManager::SaveInventoryToWeb`,
`LoadInventoryFromWeb`와 두 응답 처리 함수의 선언·구현만 제거했다.
이 함수들은 `UFUNCTION`이 아니며 새 Item V2/Projection 경로에서 사용하지 않는다.
Blueprint에 공개된 기존 인벤토리 이벤트 4개와 `FItemSaveData`는 호환성을 위해 유지한다.
백엔드 `/api/loadInventory`, `/api/saveInventory`, 저장소·스모크 테스트와
`inventory_items` 테이블·기존 데이터는 변경하거나 자동 이전하지 않았다.

검증은 제거 심볼의 C++ 참조 0건, 변경 범위 확인 및
`HttpWebManager.cpp`의 `-SingleFile -NoLink` 컴파일 1개 작업 성공이다.
첫 단일 컴파일에서 간접 include 의존이 드러나 HTTP 헤더에 `Templates/SubclassOf.h`,
구현에 `Subsystems/SubsystemCollection.h`를 명시했다. 아이템 타입 자체는 변경하지 않았다.
실패 로그: `Saved/Logs/LegacyInventoryHttpRetirement-Compile-20261011-090454.log`.
성공 로그: `Saved/Logs/LegacyInventoryHttpRetirement-Compile-Retry-20261011-090556.log`.
DLL 링크·전체 Editor 빌드·런타임 자동화·Blueprint 로드 검증은 이번에 실행하지 않았다.

### 동기 참조 구현의 테스트 전용 경계 (2026-10-11)

`FRPGItemTransactionService`와 `FRPGInMemoryItemRepository`의 선언·구현을
`WITH_DEV_AUTOMATION_TESTS`로 제한했다. 현재 사용처는 기존 자동화 파일 4개이며,
온라인 실행은 `FRPGItemAsyncCommandOrchestrator`와 백엔드 Commit을 사용한다.
메모리 저장소를 운영·오프라인 서버 저장 경로로 오해하지 않도록 주석도 수정했다.

공용 `IRPGItemRepository`/RecordSource, 요청·결과 타입, Action/Pickup Planner와
온라인 명령 로직은 변경하지 않았다. 동기 서비스의 장착·소비 검증도 기존 공통
Action Planner 재사용을 유지한다. 별도 테스트 모듈, 백엔드·DB 변경은 없다.

검증 중 직접 사용하는 테스트 3개에서 누락된 `UObject/Package.h`, 대검 테스트에서
`Engine/StaticMesh.h` 의존이 드러나 해당 테스트의 include만 명시했다.
테스트 본문은 변경 전 해시와 비교해 줄바꿈 차이를 제외하고 불변임을 확인했다.

- Development Editor: 두 구현과 직접 사용하는 테스트 파일 4개 모두
  `-SingleFile -NoLink` 개별 컴파일 성공. 최초 6개 실행에서 테스트 3개 실패,
  실패 파일 3개 재실행에서 대검 테스트만 실패한 뒤 그 1개만 다시 컴파일했다.
  최종 6개 오브젝트가 해당 소스와 두 경계 헤더보다 최신임을 확인했다.
- Shipping: 같은 6개 파일의 `-SingleFile -NoLink` 컴파일 성공.
  두 구현의 COFF 심볼 검사에서 두 클래스 관련 심볼은 Development에서 존재하고
  Shipping에서는 0개였다. 최종 테스트 조건 안의 include 보완 뒤 대검 파일
  1개도 같은 Shipping 조건으로 다시 컴파일해 성공했다.
- 공용 저장소 계약·요청 타입·Action Planner·Async Orchestrator와 변경하지 않은
  Pickup Planner 테스트의 SHA-256은 변경 전과 동일했다.

로그: `Saved/Logs/ItemTestBoundary-Development-20261011.log`,
`Saved/Logs/ItemTestBoundary-Development-Retry-20261011.log`,
`Saved/Logs/ItemTestBoundary-Development-PickupRetry-20261011.log`,
`Saved/Logs/ItemTestBoundary-Shipping-20261011.log`,
`Saved/Logs/ItemTestBoundary-Shipping-PickupRefresh-20261011.log`.
이는 조건별 개별 컴파일/심볼 검증이며 DLL 링크·전체 타깃 빌드·자동화 실행·
HTTP/DB/Steam E2E 또는 Shipping 패키징 성공을 뜻하지 않는다.

## 월드 픽업 연결

`FRPGItemPickupGrantPlanner`는 서버가 이미 확정한 요청 ID와 ItemId로 단일
Inventory Record의 신규 발급 커밋을 만든다. 소유자, 등록된 네이티브 정의 버전,
최대 스택 수량, 빈 슬롯, 중복 ItemId를 검사하고 생성 시드까지 요청 지문에 넣는다.
월드 픽업의 상호작용은 다음 경로를 사용한다.

```text
ARPGPickUpBase::TakePickup
  -> URPGItemCommandComponent::ServerPickupItem
     -> FRPGItemPickupGrantPlanner
        -> URPGPickupGrantComponent (claim + frozen commit + retry)
           -> Item Backend
              -> FRPGPickupGrantReceiptPolicy
                 -> Inventory Projection -> pickup retirement
```

- 서버가 인증된 소유자, 보안 게이트, 거리, 월드, 픽업 가용성, 네이티브 정의를 검증한다.
  클라이언트는 픽업 액터만 전달하며 수량·ItemId·생성 시드·목적지는 지정하지 않는다.
- 픽업 액터가 요청을 소유한다. 응답 유실·잘못된 성공 영수증·인증/HTTP 오류는
  같은 요청 ID, ItemId, 목적지, 생성 시드의 커밋으로 재시도한다. 처리 중인 픽업은
  다른 플레이어가 획득하거나 외부 코드가 잠금을 해제·초기화·제거할 수 없다.
- 이전 일반 커밋 방식은 요청과 일치하는 슬롯/Revision 충돌 또는 검증 실패에서 잠금을
  해제했다. 아래 journal v2 예약 방식은 확정 거절에도 원래 예약과 claim을 보존하며
  같은 명령만 재시도한다. 인증 오류만으로 앞선 요청의 저장 여부를 확정하지 않는다.
- 성공 영수증의 전체 Record와 수량을 동결된 요청과 비교하고 Projection 적용 후
  픽업을 제거한다. 이미 더 최신 상태로 반영된 Record도 확인한다. 소유자가 접속을
  종료하거나 캐릭터를 전환했다면 다른 캐릭터의 Projection에는 적용하지 않고,
  저장된 원래 캐릭터의 아이템은 다음 로그인에서 불러온다.
- `URPGInventoryComponent::TryAddItem`과 기존 픽업 RPC는 신규 명령을 전달하는
  호환 진입점이다. 픽업에서 구형 인벤토리의 스택/목록을 별도로 수정하는 경로는 제거했다.

2026-10-04 당시 네이티브 정의 자산은 `DA_Item_Potion_Red_Large`, `DA_Item_Helm_Leather` 두 개였다.
다른 구형 `FItemManifest` 픽업은 정의를 이전하기 전까지 획득을 거부하고 월드에 남긴다.
2026-10-04 읽기 전용 자산 확인에서 실제 미이전 항목은
`/Game/Blueprints/Item/Consume/1`의 `GameItem.Consume.Potion.Blue.Small`이었다.
이 자산에는 아이콘과 최대 스택 10만 있고 사용 효과/회복량 정의는 없어 이전을 보류한다.
이름이나 색만으로 효과와 밸런스 수치를 추측해 네이티브 정책을 생성하지 않는다.
확인 도구는 `Scripts/Editor/AuditItemDefinitions.py`, 로그는
`Saved/Tests/ItemDefinitionAudit/audit.log`에 있다. 기존 자산은 변경하지 않았다.
미인증 상태, 백엔드/초기 로드 준비 전, 빈 슬롯 없음, 최대 스택 초과도 획득하지 않는다.
이 단계는 단일 스택 신규 발급이며 기존 스택 자동 병합·여러 스택 분할·구형 저장 데이터
자동 이전은 포함하지 않는다. 런타임 재시도는 서버 세션 내 기능이며 서버 재시작을 넘는
월드 드롭/미완료 요청의 영속 복구까지 구현한 것은 아니다.

2026-10-04 검증: `Project_RPGEditor Win64 Development` 빌드 성공.
화면 없는 자동화에서 PickupGrant 5개(계획 2개, 런타임 3개), QuickSlot 2개,
Projection 3개, 총 10개가 통과했다. 런타임 테스트는 지연 응답을 제어하는 가짜
백엔드로 동결 요청 재시도, 잠금 보호, 성공 반영 후 제거, 슬롯 충돌 후 해제,
캐릭터 전환 격리를 확인한다. 실제 HTTP/멀티클라이언트 픽업 및 UI 조작 검증은 별도다.
장비·드롭·구형 저장 경로 전체의 제거가 완료된 것은 아니다.

검증 로그: `Saved/Logs/PickupRuntimeBuildFinal-20261004.log`,
`Saved/Tests/PickupRuntime/RunFinal-20261004/automation.log`.

### 월드 픽업의 진행 상태 안내 (2026-10-27, 구현·집중 native 완료)

`URPGPickupGrantComponent::BeginGrant`의 실제 claim 뒤부터 완료까지 서버가 요청/목적칸을
추적하고, `URPGItemCommandComponent::GetPendingPickupGrantCount`로 건수만 소유자에게
`COND_OwnerOnly` 복제한다. 클라이언트가 상태/원래 명령을 지정하는 RPC는 추가하지 않았다.
같은 픽업의 거절된 중복 시작은 건수를 늘리지 않는다.

공유 ViewModel은 기존 상태 알림을 구독해 `PendingItemCommandCount`와 가방 하단의
지역화된 `PickupPending` 안내를 갱신한다. 기존 WBP의 FieldNotify 경로를 재사용하므로
에셋 수정은 없으며, 창/모델을 다시 연결해도 Controller의 현재 건수를 읽는다. 다른 동작의
로컬 거절 사유와 저장소 실패·기존 미확정 가방 복구 안내가 우선한다.

- 응답 유실·401·잘못된 영수증 및 prepared 예약의 충돌은 기존 claim/원래 명령과 진행 건수를
  유지한다. 성공 영수증 반영 또는 이전 비예약 방식의 확정 거절에서 해당 요청 건수만 정리한다.
- 픽업 건수는 정보 표시이지 가방 전체 잠금·빈 칸 예약·사용 허가가 아니다. 기존 가방/장비/
  우편의 예약과 서버 검증을 바꾸지 않으며 `InventoryRecovery` 목록/수동 복구 버튼도 만들지
  않는다. 모델 재연결로 새 요청을 보내거나 새 ItemId/슬롯을 고르지 않는다.
- 신원 회수/소유 Controller 종료의 UI 정리는 DB 취소·claim 해제·성공 확정이 아니다. 기존 journal과
  원래 지급 결과의 안전 경계는 유지하며 재시작 복구·수동 충돌 해소 정책은 확장하지 않았다.

정상 Editor 타깃/DLL 링크 exit 0(최종 6개 작업, 31.22초) 뒤 픽업 런타임 **6/6**·러너 exit 0,
선택 입력 31개/검증 모듈 `3C8039FF…7F107F1` 불변이다. 최초 새 사례는 임시 Projection의
로드 Ready 준비 누락으로 5/6이었으며 실패를 보존하고 테스트 준비만 수정했다. 운영의
미준비 차단은 완화하지 않았다. 앞선 모듈 `2FF6F464…A2378C8`에서는 직접 영향 표시/조작
**8/8**·입력 14개·러너 exit 0을 확인했다. 후속 변경은 테스트 준비 및 다른 작업의 probe여서
표시 검사는 반복하지 않았으며 두 결과를 같은 DLL의 통과로 계산하지 않는다. 각 실행의
기동 기존 Error 15줄은 별도, 검사 중/이후 Error 0·전체 Fatal/Assert/Ensure 0이다.

증거: `Saved/Tests/PickupPendingPresentation-f1af6019/Verification.proof.json`;
native: `Saved/Tests/Item/Automation-78cdb416f880495f89da91a63505970a/Item.proof.json`와
`Automation-4b145f16d2b843cfbbcaa172af6d0cf6/Item.proof.json`.
이는 임시 월드·모의 committer/ViewModel 검사이며 실제 HTTP/DB/Iris 화면·물리 입력/
Steam/Shipping 증명이 아니다. 이전 소비/픽업/가방·우편·장비 HTTP E2E를 반복하지 않았다.
다음 실제 플레이에서는 기존 가방의 진행 안내를 확인할 수 있으며, 이 표시 연결 때문에
Revision 충돌 실통합·권한 있는 미소비 replay/월드 복원 등 기존 별도 범위를 완료로 바꾸지 않는다.

### 연속 픽업의 서버 목적칸 보호 (2026-10-27, 구현·집중 native 완료)

진행 중인 픽업은 아직 Projection에 없으므로, 원래 계획기는 연속 요청에 같은 빈 칸을
선택할 수 있었다. prepared 예약은 한 번 정한 목적지를 바꾸지 않기 때문에 나중 요청이
충돌하면 가방에 여유가 있어도 그 칸에서 보류될 수 있다. 다음 신규 획득의 **최초 계획**에만
서버의 기존 픽업 목적칸을 제외하도록 연결했다.

- 실제 `ServerPickupItem`은 `TryPlanPickupGrant`를 통해 현재 인증 소유자의 Controller가
  보유한 목적칸을 제외한다. `ReservedInventorySlots`는 서버 내부 계획 입력일 뿐이며,
  helper가 현재 추적으로 다시 구성한다. 기존 HTTP 본문/지문·RPC 인자에는 추가하지 않는다.
- claim 뒤 HTTP 전에 요청/목적칸을 등록한다. 같은 요청 ID·이미 잡힌 칸·잘못된 소유자/
  위치/신규 생성 revision은 거절한다. 이 등록에 실패한 **아직 전송하지 않은** 새 claim만
  되돌리고, 다른 원래 요청의 추적/예약이나 journal은 지우지 않는다.
- 응답 유실·401·prepared 충돌에서는 원래 요청/칸을 보존하고 다른 새 픽업만 다음 빈 칸을
  선택한다. 응답 완료 순서가 달라도 완료된 요청만 정리하며, 재시도는 원래 전체 본문이다.
  모든 칸이 점유/예약됐으면 새 발급 계획을 만들지 않는다.
- 픽업 액터의 미확정 EndPlay는 그 칸이 비었다는 증거가 아니다. 현재 Controller 안에서는
  목적칸과 대기 건수를 유지한다. 이는 앞선 표시 연결의 액터 종료 시 건수 정리보다 강화된
  후속이며 성공/취소나 replay 허가가 아니다. 권한 회수/Controller 종료의 로컬 정리는 기존
  DB·journal 보존 및 권한 차단을 유지한다.

기존 BP 픽업 호출을 그대로 사용하며 WBP/에셋·API/DB 스키마 변경이나 가방 전체 잠금은
없다. 이 보호는 **픽업끼리의 최초 계획** 범위다. 가방의 다른 조작과 교차하는 슬롯 예약
전파, 실제 HTTP/DB/Iris 연속 획득·자연 전투/물리 입력, 재시작 replay·월드 복원/Steam/
Shipping의 완료를 뜻하지 않는다.

정상 Editor 타깃 3개 작업·자체 DLL 링크/exit 0(UBT 18.03초·선행 빌드 대기는 별도,
소스 컴파일 객체는 선행 공유 빌드),
픽업 계획 3개+런타임 8개 **11/11**·러너 exit 0이다. 원래 무필터 계획의 중복 칸 재현,
다른 칸으로 동시 획득/응답 역순·거절된 claim의 무전송/원복·유실 재시도의 전체 본문 불변,
미확정 액터 종료의 목적칸 보존과 기존 권한/표시 사례를 확인했다. 선택 입력 41개(중복 0)/
모듈 `8EE8703F…7CD0D63` 불변이며 기동 기존 Error 15줄은 별도, 검사 중/이후 Error 0·전체
Fatal/Assert/Ensure 0이다. 기존 HTTP E2E나 별도 표시 8개 검사는 반복하지 않았다.

증거: `Saved/Tests/PickupSlotScheduling-c002458f/Verification.proof.json`;
native: `Saved/Tests/Item/Automation-abfb41fa2d1244b0a1316988270df5ec/Item.proof.json`.
다음 실제 플레이 확인은 기존 BP 입력으로 여러 드롭을 연속 획득하는 경로이며, 이 결과로
기존 Revision 충돌 실통합·미소비 replay/운영 복구 항목을 완료로 바꾸지 않는다.

### 픽업과 가방 조작의 교차 목적칸 보호 (2026-10-27, 구현·집중 native 완료)

픽업끼리만 보호하던 목적칸을 기존 가방 이동·분할·해제·우편 수령 등의 공통 전송 경로와
연결했다. `MakeSlotAwareCommitter`가 실제 backend adapter를 감싸 HTTP 전에 모든 mutation의
목적칸을 검사한다. 먼저 진행 중인 가방 명령의 칸도 신규 픽업 최초 계획에서 제외하고,
픽업 claim 등록에서도 양쪽 예약을 다시 확인한다. 기존 RPC/본문·DB 계약은 바꾸지 않았다.

- 로컬 충돌은 `ValidationFailed` 거절이며 실제 upstream `LocationConflict`나 성공 영수증으로
  표시하지 않는다. 저널 보존/정리와 성공·미확정 판정은 기존 오케스트레이터가 계속 담당한다.
  로컬 거절 전에 생성된 저널의 정리에 실패해도 기존 저장소 차단/미확정 처리를 우회하지 않는다.
- 미확정 명령의 목적칸은 유지하며 같은 요청 ID의 다른 전체 본문으로 교체할 수 없다.
  검증된 완료/확정 거절만 해당 추적을 정리한다. 완료 캐시 응답은 HTTP 경로를 다시 거치지
  않으므로 과거 목적칸의 현재 상태 때문에 멱등 응답이 차단되지 않는다.
- 픽업 목적칸 번호만 `COND_OwnerOnly`로 전달한다. 기존 공유 모델의 빈 칸 WAIT·용량/가용
  칸 계산·드롭/분할/수령 후보 선택과 퀵슬롯 상태 갱신에 연결하며 새 복구 버튼은 없다.
  다른 칸은 계속 사용할 수 있고 이 UI 정보 자체가 서버 권한/취소·replay 허가는 아니다.

실제 공통 adapter·오케스트레이터·서버 예약/공유 모델과 모의 backend를 사용하는 직접 영향부
**14/14**·러너 exit 0이다. 새 3개는 픽업의 빈 칸 WAIT/용량·이동/분할 후보/창 재연결,
가방 이동→픽업의 반대 순서와 미확정 동일 본문 복구·완료 캐시, 동일 ID 본문 교체/다른
요청 예약 침해 거절을 확인한다. 선택 입력 49개(중복 0)/모듈 `E563DE70…5855C58`은 실행 중
불변이다. 기동 기존 Error 15줄은 별도이며 검사 중/이후 Error 0·전체 Fatal/Assert/Ensure 0이다.

최초 정상 Editor 빌드는 244개 작업 중 범위 밖 `RPGCombatContributionComponent.cpp`의
`OutLifetimeProps` 오류로 exit 6이었다. 이번 서버 명령·공유 모델·픽업 테스트의 C++ 컴파일은
통과했으며 실패 로그를 보존했다. 해당 전투 소스는 다른 작업에서 이미 수정돼 건드리지 않았다.
이후 다른 빌드가 DLL을 반영한 상태에서 자체 정상 타깃 재확인은 exit 0/Up to date(0개 작업,
7.81초)다. 이를 자체 전체 빌드/새 링크 성공으로 계산하지 않으며 사용자 에디터를 종료하지 않았다.

증거: `Saved/Tests/PickupCrossSlot-61cc8c62/Verification.proof.json`;
native: `Saved/Tests/Item/Automation-d3fb65aa999b4756b1514addd767d4d2/Item.proof.json`.
실제 HTTP/PostgreSQL/Iris 조작·물리 입력·Steam/Shipping, Controller/서버 재시작을 넘는
예약 복원과 운영 해소 정책은 별도다. 각 RPC의 실제 화면/서비스 통합을 14개 native로
대체하지 않는다. 이전 소비/우편/장비/픽업 E2E나 별도 표시 suite는 반복하지 않았다.
다음 실제 플레이 확인은 픽업 대기 중 가방 이동/분할·해제·우편 수령을 함께 조작하는 경로다.

### 일반 NPC 드롭→영속 픽업 통합 검증 준비 (2026-10-11)

`Scripts/Network/Run-NPCPickupBackendE2ETest.ps1`과 개발 전용
`FRPGNPCPickupNetworkProbe`를 추가했다. 아래 최초 기록은 **검증 경로 준비 완료**이며,
최초 준비 검증 당시 Docker Desktop Linux 엔진이 준비되지 않아 실제 HTTP/PostgreSQL E2E는 실행하지 못했다.
초기 준비 proof만으로 DB 지급·영수증·재시작 복구의 성공을 계산하지 않는다.
후속 실제 실행의 통과 범위는 아래 별도 절에 기록한다.

- 저장된 `BP_Dummy_Gdn`, 실제 공유 피해 GE·Guardian 사망/드롭 Ability·대검 픽업을
  사용한다. 드롭 Ability의 실행 인스턴스에만 확률 1·수량 1의 임시 행을 넣는다.
  저장된 드롭 BP/DT와 CDO는 수정하지 않으며 Guardian의 저장된 드롭/보상 목록은 여전히 비어 있다.
  기존 디졸브 종료→드롭→제거 순서도 유지한다.
- 기존 개발 로그인→캐릭터/세션 조회→Join Ticket→실제 서버 입장 경로를 재사용한다.
  `RPGNetTestMode` 입장 우회는 사용하지 않는다. `-nosteam` 신원 fixture이므로 실제 Steam 증거는 아니다.
- 인증된 원격 클라이언트가 기존 `IInteractionInterface::Interact`를 두 번 호출한다.
  새 지급 RPC/API는 없으며 수량·ItemId·생성 시드·슬롯은 기존 서버 코드가 결정한다.
  정지한 NPC, 네이티브 서버 치명타, 서버가 배치한 플레이어/픽업과 상호작용 호출은
  통합 fixture다. 자연 전투·물리 입력·이동 성공으로 표시하지 않는다.
- 러너는 실제 서버 드롭 관측 뒤에만 클라이언트 획득 gate를 연다. NPC/픽업 Iris ID와
  요청/아이템 ID를 연결하고, 두 요청 중 성공 1개·거절 1개, 소유자 Projection 수량 1/Revision 1,
  픽업 제거 및 PostgreSQL의 전체 Record/영수증 일치를 검사하도록 작성했다.
  성공 요청의 정확한 HTTP 재전송과 이 실행이 소유한 백엔드 재시작 뒤 동일 저장 결과도 검사한다.
  준비 기록만으로 이 검사들의 전체 통과를 확인하지 않으며 HTTP 재전송은 UE 응답 유실 재시도 증거가 아니다.
- 개발 fixture는 명시적 `-RPGNPCDropPickupE2E`에서만 동작한다. Shipping에는 새 옵션/로그 문자열이
  남지 않도록 분기까지 제외했다. 러너는 loopback·기존 로컬 개발 DB만 허용하고, 공유 컨테이너나
  다른 실행의 프로세스/DB 행을 삭제하지 않는다. 실행한 계정·아이템·영수증은 보존한다.

확인한 범위: Editor 모듈 증분 링크 성공 후 최종 소스의 공유 타깃 up-to-date 확인(0개 작업),
관련 C++ 3개 Shipping `-SingleFile -NoLink` 실제 컴파일 성공, Shipping helper의 네 함수가
모두 즉시 return하고 새 개발 옵션/로그 문자열이 세 object에 없는 것, PowerShell 구문 검사.
전체 Shipping 링크·패키징·런타임이나 새 네이티브 자동화 실행을 뜻하지 않는다.
증거: `Saved/Tests/NPCPickupIntegration-20261011-Prepared/Preparation.proof.json`
(`postgresRuntimeVerified=false`), 보관된 소스/object 및 빌드 로그.
마지막 확인에서 보관된 소스 6개의 해시는 그대로지만 공유 DLL은 다시 갱신됐다.
이 proof의 빌드 근거를 더 새 DLL의 런타임 통과로 표시하지 않는다.

재검증은 Docker 엔진이 준비된 뒤 프로젝트 루트에서
`.\Scripts\Network\Run-NPCPickupBackendE2ETest.ps1 -TimeoutSeconds 600`으로 실행한다.
성공 시에만 `Saved/Tests/NPCPickupBackend/<runId>/NPCPickupBackend.proof.json`을 남긴다.
다중 수령자 경합·UE 응답 유실·UE 서버 재시작을 넘는 월드 드롭/미완료 요청 복구·
자연 스포너/부활의 새 처치 identity·운영 Steam/Shipping 검증은 별도 후속 범위다.

#### 첫 실제 실행의 중단 기록 (2026-10-11)

Docker/PostgreSQL 준비 후 `-SkipPostgresStart -NoBackendRestore`로 실행했다.
격리 백엔드 빌드는 경고/오류 0개였고, 클라이언트 로그의 실제 로그인·티켓 입장과
서버 로그의 NPC 사망을 각각 1회 확인했다. 하지만 드롭/획득 완료·최종 proof·
러너 종료 결과 없이 실행이 중단돼 있었으며 원인은 확인되지 않았다.
재개 시 두 UE 프로세스와 해당 백엔드는 실행되지 않았고 Docker 엔진/DB도 사용할 수 없었다.
통합 성공이나 타임아웃 실패로 추정하지 않는다. DB 저장/영수증·중복 거절·HTTP 재전송·
백엔드 재시작 복구는 미검증이다. 초기 로드 DLL 해시가 실행 전에 영구 기록되지 않아
재개 시 DLL 해시만 별도로 기록했으며, 현재 바이너리의 런타임 통과로 간주하지 않는다.

로그/해시와 미완료 관측 기록:
`Saved/Tests/NPCPickupBackend/604a9d7fdd4e4f4f9b9642b4ef8dadc7/Interrupted.proof.json`
(`passed=false`, `outcome=Incomplete`). 생성된 개발 계정/세션/DB 행과 기존 로그는 삭제하지 않았다.
이 중단 기록은 아래 새 실행 ID의 통과와 구분한다.

#### 전용 서버 디졸브 복구와 1인 영속 픽업 E2E (2026-10-11)

실제 실행에서 NPC 사망 뒤 드롭이 시작되지 않는 결함을 확인했다. 저장된
`BP_RPGNPCBase`의 전용 서버 분기는 Niagara async load를 건너뛰지만,
디졸브 타임라인 시작도 Niagara 설정 노드 뒤에 있어 서버에서 도달할 수 없었다.
`ConfigureNPCDeathPresentation.py`를 보완해 전용 서버 분기를 기존 `SetPlayRate`→
`DissolveTimeline.PlayFromStart`에 연결했다. 원래 클라이언트 Niagara→타임라인 연결을
검사하고 보존하며, 타임라인 종료→드롭→제거 순서/시간이나 저장된 보상표는 바꾸지 않았다.
저장 변경은 NPC 부모 BP 하나이며 드롭 BP/DT/CDO는 그대로다.

원본 BP 백업은 `Saved/Backups/NPCDeathLifecycle-a7b5230a330f46358d19680834ebecd5/`에 있다.
부모 BP 해시는 `7D1BF29F…`→`12633ADD…`, Blueprint 컴파일/저장과 원래 연결 보존 기록은
`Saved/Tests/NPCDeathLifecycleRepair-20261011-Verified/BlueprintRepair.proof.json`에 보관했다.

러너도 다음 검증 경계를 보완했다.

- 실행 전에 `Attempt.proof.json`에 DLL·러너·소스/자산 해시를 기록한다. 중단된 시도에
  나중의 공유 DLL을 대신 붙이지 않는다.
- UE의 BeginPlay/`InProgress`가 실제 tick/입장 준비보다 앞서 발생할 수 있으므로,
  살아 있는 자기 프로세스의 할당만 기존 GameServer 토큰/heartbeat API로 유지한다.
  이 allocator fixture는 실제 원격 입장 완료까지만 동작하고 이후에는 UE heartbeat를 사용한다.
  기본 90초 lease·만료 검사·서버/세션 권한은 변경하지 않았다.
  초기 부팅을 기본 lease 안에 끝내는 운영 할당 검증으로 계산하지 않는다.
- 기존 네트워크 러너와 같이 `-NoLoadingScreen`을 사용한다. 모든 기능 검사/늦은 로그 확인 뒤
  자기 UE 프로세스만 종료해 로그를 닫고 전체 로그를 다시 검사한다. 해시가 없으면 PASS를 거절한다.
  다른 프로세스·공유 DB/볼륨을 종료하거나 삭제하지 않는다.

최종 실행 `d8fd74a92a374732a97350762e36b835`은 exit 0으로 통과했다.
인증된 원격 클라이언트 1개가 실제 저장된 NPC의 서버 GAS 사망→디졸브→대검 드롭→
기존 상호작용/RPC를 실행했다. 요청 두 개 중 성공 1개·거절 1개, 수량 1/Revision 1의
소유자 Projection 및 픽업 제거를 관측했다. 실제 PostgreSQL의 단일 Item/전체 영수증 일치,
거절 요청 영수증 404, 동일 성공 명령 재전송의 `AlreadyCommitted`/추가 지급 없음,
소유 백엔드 재시작 뒤 동일 아이템/영수증 유지와 거절 영수증 404도 확인했다.
기동 heartbeat fixture는 3회였고, 테스트 종료 후 로그 해시·소스/자산 해시도 일치했다.

증거: `Saved/Tests/NPCPickupBackend/d8fd74a92a374732a97350762e36b835/NPCPickupBackend.proof.json`
(`passed=true`, `proofIntegrityVerified=true`), 같은 폴더의 두 UE 로그와 격리 백엔드 로그/바이너리.
검증 DLL은 `2BB3C23E…`다. 공유 DLL이 준비 중 사라졌던 실패는 별도 보존했고,
자체 마지막 빌드 확인은 0 action up-to-date로서 자체 재링크로 표현하지 않는다.
백엔드 빌드는 경고/오류 0개였으며 새 전체 UE 자동화·Shipping 링크/패키징은 반복하지 않았다.

실패 이력도 `Saved/Tests/NPCPickupBackend/`에 보존한다: `b13c2b9a…`/`f0e1e613…`은
기동 lease 만료, `d7df4c1f…`는 실제 입장/사망 뒤 드롭 미기동 및 기동 유지 종료 경계,
`3af69ba4…`는 기능 검사 후 열린 로그 해시 실패, `0b28f9de…`는 공유 DLL 누락이다.
해시 없는 첫 성공 산출물은 `NPCPickupBackend.invalid-proof.json`으로 분리했으며
유효한 완료 proof로 사용하지 않는다. 개발 계정/세션/아이템/영수증은 보존했다.

이 1인 통과는 정지한 NPC·서버 피해/배치·확률 1의 임시 행·스크립트 상호작용·개발 신원/기동
heartbeat fixture다. 자연 전투/스포너·실제 입력·저장된 Guardian 보상 콘텐츠·다중 수령자 경합·
UE 응답 유실 재시도·UE 서버 재시작을 넘는 월드 드롭 복구·실제 Steam·Shipping 실행은 미검증이다.

#### 2인 동시 픽업 경합과 소유자 격리 (2026-10-11)

기존 러너에 `-ClientCount 1..4`를 추가했다(기본 1, 이번 실제 경합 검증은 2인).
운영 RPC/지급 코드·BP/드롭 표/CDO·API는 바꾸지 않았다. 개발 probe는 명시한 캐릭터 목록을
검증하고 같은 세션의 인증된 전원이 Pawn/Projection 준비를 마쳐야 NPC 하나를 생성한다.
첫 멤버만 사망/드롭을 관측하며 다른 Controller가 별도 NPC를 생성하지 않는다.

경합 모드는 각 클라이언트가 같은 살아 있는 Iris 픽업·빈 인벤토리·획득 거리를 확인한
`CLIENT_READY` 뒤에만 공통 send gate를 연다. 실제 기존 상호작용을 두 번씩 호출하며
클라이언트에 `PktLag=250`, `PktLoss=0`을 적용한다. 준비/전송/완료의 캐릭터·NPC·픽업·
아이템/요청 ID와 승자 1명·전원 요청 2개·서로 다른 거절 ID를 별도 parser에서 검증한다.
다른 관측/요청을 빌려 통과시키는 경우와 패자 Projection 유출 등을 포함한 parser 검사
14개는 통과했지만, 이것만으로 네트워크/DB 성공을 계산하지 않는다.

실제 2인 실행 `3956b3cefd924ed5bf37759cf4c42c34`은 exit 0으로 통과했다.
두 인증 클라이언트가 같은 픽업을 준비하고 약 3ms 간격으로 요청을 큐에 넣었다.
총 4개 RPC에서 성공 1개·거절 3개, 파티 전체 Projection/실제 PostgreSQL 아이템 합계 1개,
수량 1/Revision 1·픽업 제거·전체 영수증 일치와 거절 영수증 404를 확인했다.
패자의 자기 인벤토리는 비어 있고 승자의 Item/인벤토리 조회는 403이다.
승자/패자 모두 플레이어 토큰으로 서버 영수증을 조회할 수 없으며,
동일 성공 HTTP 명령 재전송과 소유 백엔드 재시작 뒤에도 같은 경계를 확인했다.
공통 probe/러너 변경의 직접 영향인 기본 1인 모드도 같은 DLL에서 다시 통과했다:
`Saved/Tests/NPCPickupBackend/4a0d0dde70bc4105bb844265f86c1d66/NPCPickupBackend.proof.json`
(exit 0, 성공 1/거절 1·정확한 HTTP 재전송·소유 백엔드 재시작·최종 해시 확인).

최종 증거는 `Saved/Tests/NPCPickupBackend/3956b3cefd924ed5bf37759cf4c42c34/NPCPickupBackend.proof.json`
(`multipleClaimantsVerified=true`, `ownerIsolationVerified=true`, `proofIntegrityVerified=true`)다.
세 UE 로그와 소스/자산 해시는 종료 뒤 일치했다. 검증 DLL은 `1369FF13…`이며
Development 게임 모듈의 compile/lib/DLL 3개 작업 성공으로, 전체 Editor 타깃 검증과 구분한다.
변경된 helper의 Shipping 단일 파일 `NoLink` 컴파일도 성공했고 네 함수가 즉시 return하며
개발 옵션/관측 문자열이 없는 것을 확인했다. 빌드/parser/object 증거는
`Saved/Tests/NPCPickupParty-20261011-Verification/BuildAndParser.proof.json`에 보관했다.
최초 컴파일의 로그 매크로 분기 오류와 최초 런타임의 쉼표 참가자 목록 해석 오류는 수정했고,
실패 실행 `8e68baf096b948a88e90b80161cde4d6/Failure.proof.json`과 당시 소스를 보존했다.

기존 개발 신원·정지 NPC/서버 피해·배치·임시 행·공통 파일 gate·입장까지의 heartbeat fixture다.
기본 lease/권한·데이터는 유지하고 자기 프로세스만 정리했다. 4인·패킷 손실 회귀,
각 거절이 DB 응답 전 pending 상태에서 발생했는지의 정확한 시점, UE 응답 유실 재시도,
게임 서버 재시작을 넘는 드롭 복구·자연 전투/콘텐츠·Steam·Shipping 실행은 해당 경합 실행에서 검증하지 않았다.

#### DB 커밋 후 HTTP 응답 유실과 UE 재시도 복구 (2026-10-12)

`Run-NPCPickupBackendE2ETest.ps1 -ClientCount 2 -LosePickupAck`는 UE 서버↔백엔드의
아이템 HTTP 응답 유실을 검증한다. 로컬 프록시는 이 실행의 서버 토큰 해시/캐릭터 두 명에
범위를 묶고 실제 PostgreSQL 커밋의 완전한 200 응답을 받은 뒤 UE 연결만 끊는다.
이후 커밋 재시도를 잠시 차단하며, 인증된 직접 GET으로 저장된 아이템/영수증을 확인한 뒤
서버의 `Pending=1/Available=0/Retired=0/TotalItems=0`과 살아 있는 픽업에서의 거절을 관측한다.
그 후 기존 UE 재시도만 허용한다. 새 지급 RPC·운영 API/지급 정책·BP/드롭 표/CDO는 변경하지 않았다.

리뷰의 두 문제도 수정했다. 선택된 첫 지급 이후에는 같은 테스트 서버 토큰의 모든 커밋을
operation/actor 필터보다 먼저 동결 본문과 비교해, 변경된 operation/소유자 타입/범위가
검사를 우회해 전달되지 않게 한다. 경과 시간은 `process.hrtime.bigint()`에서 얻고 UTC는
기록에만 사용한다. 미래/과거 UTC·짧은 실제 간격·누락/비유한/역행 카운터를 검증했다.
재시도 묶음 크기는 설정 파일의 4회 가정 대신 살아 있는 UE가 보고한 횟수로 계산한다.
HTTP 프록시 회귀 6개와 관측/타이머 파서 34개는 통과했으며 이는 실제 DB 실행과 구분한다.

Windows 시작 로그의 빈 값/공유 핸들 문제는 빈 문자열 가드와 `FileShare.ReadWrite`로
수정하고 별도의 살아 있는 프록시 시작 검사로 확인했다. 화면 없는 사전 빌드 런타임은
기존 엔진/자동화의 `UE_SKIP_UBT_SDK_SETUP=1`로 빌드 뒤에 대기하는 SDK 검사만 건너뛴다.
환경은 실행 뒤 복원하며 SDK 설치·패키징 검증을 뜻하지 않는다. 기본 90초 lease/권한은 그대로다.

최종 실행 `353e81f6813f498aa59c17fce12c029e`은 exit 0으로 통과했다.
실제 NPC 사망/디졸브/드롭→인증된 2인 같은 픽업의 RPC 네 개에서 단일 DB 저장 후
첫 성공 HTTP 응답을 1회 유실시켰다. 재시도들은 동일 요청 ID·직렬화 본문 SHA256을 유지했고,
실효 게이트웨이 시도 수 2회의 다음 묶음까지 단조 증가 시간 약 2.04초를 확인했다.
응답 대기 중 전원의 Projection은 비어 있고 픽업은 잠긴 채 살아 있었으며 거절 세 개를 관측했다.
복구 응답은 `AlreadyCommitted`, 클라이언트 완료는 `AlreadyApplied`였다.
최종 수량 1/Revision 1·파티 전체 아이템 1개·픽업 제거·전체 영수증 일치/거절 영수증 404,
패자의 타인 조회 403·플레이어 영수증 조회 403과 정확한 추가 HTTP 재전송/소유 백엔드 재시작
후 같은 결과/권한을 확인했다. 첫 멤버가 아닌 두 번째 클라이언트가 수령한 실행이다.

증거: `Saved/Tests/NPCPickupBackend/353e81f6813f498aa59c17fce12c029e/NPCPickupBackend.proof.json`
(`unknownAckRetryVerified=true`, `nativeGrantTimerRetryVerified=true`, `proofIntegrityVerified=true`).
기본 1인 모드도 같은 DLL에서 exit 0으로 통과했다:
`Saved/Tests/NPCPickupBackend/f59353b95c1641f5b269115d089ae492/NPCPickupBackend.proof.json`.
로그·소스/자산·러너/파서 해시를 독립적으로 재확인했다. 검증 DLL은 `E353CAC5…`이며
선택 게임 모듈 빌드는 199개 작업으로 성공했다. 전체 Editor 타깃/클린 빌드·전체 회귀·새 Shipping
실행으로 계산하지 않는다. 빌드/회귀/Windows 시작 검사/소스 보관은
`Saved/Tests/NPCPickupLostAck-Final-43a1a337ca2648f2aa4a113f1f17fa0e/Verification.proof.json`에 있다.

최초 빈 시작 로그(`d11e38e3…`), 공유 핸들(`8a67d810…`), SDK 대기(`0656caef…`),
실효 시도 수 가정 오류(`b51fedf7…`)는 각각 실패 proof로 남겼고 통과로 합치지 않았다.
자기 프로세스/멈춘 시도의 SDK 자식만 정리했으며 공유 빌드·DB 행/볼륨은 삭제하지 않았다.
기존 개발 신원·정지 NPC/서버 피해/배치·임시 드롭 행·파일 gate·입장까지의 heartbeat fixture다.
클라이언트↔UE RPC 응답 유실 자체, 서버측 거절의 정확한 DB 커밋 상대 시점, 4인/패킷 손실,
게임 서버 재시작을 넘는 월드 드롭/미확정 명령 복구·자연 전투/콘텐츠·Steam·Shipping은 별도다.

#### 미확정 픽업 요청의 서버 로컬 기록과 읽기 전용 복원 (2026-10-12)

`project-rpg-online`의 기존 호출 재사용/권한 범위 원칙에 따라 BP나 새 RPC를 추가하지 않고
`URPGItemBackendSubsystem`의 기존 `Item.Pickup` 커밋 경계에
`FRPGPickupGrantJournal`을 연결했다. 기존 액터의 claim·동결 요청·동일 요청 재시도와
Projection의 신원/세대 검사는 유지한다. 월드 드롭 액터의 재생성 기능은 아니다.

- 첫 HTTP POST 전에 전체 커밋 본문과 배정 server/session을 파일에 기록한다.
  `.tmp` UTF-8 기록 → `Flush(true)` → 같은 디렉터리의 최종 파일로 이동이 성공해야 전송한다.
  동일 RequestId의 본문 변경은 지문이 같아도 거절하며, 저장 실패 시 POST를 보내지 않는다.
- 저장 위치는 `FPaths::ProjectSavedDir()/ItemPickupJournal/<session GUID>/<request GUID>.json`이다.
  서버 ID와 세션 GUID를 환경 설정에서 읽고, 토큰·Join Ticket·클라이언트 인증은 저장하지 않는다.
  서버 ID가 다른 파일, 손상/과대 파일, 파일명/요청 불일치, 다른 본문인 main/tmp는
  부분 복원 없이 가용성을 닫고 원본을 보존한다. 파일당 64 KiB, 요청 1,024개로 제한한다.
  읽은 본문을 기존 Codec으로 복원·재직렬화해 동일한 본문인지 확인한다.
- 재초기화에서 남은 요청을 복원하되 **쓰기 권한으로 취급하지 않는다**.
  해당 캐릭터의 기존 인벤토리 로드 시 `GET item-transactions/<원래 RequestId>`만 보낸다.
  HTTP 200과 요청 ID/operation/지문/소유자, 수량·전체 revision 1 Record가 모두 맞는
  성공 영수증만 pending 기록을 정리한다. 이후 최신 인벤토리를 별도 GET으로 읽으므로
  이미 소비/이동된 아이템을 오래된 영수증으로 되살리지 않는다.
- 404·401/403·응답 유실·잘못된 성공·비-200에 실린 성공 본문은 원래 기록을 유지한다.
  미저장을 확정할 수 없으므로 자동 커밋/새 RequestId 재발급은 하지 않는다.
  복원된 요청이 미확정인 캐릭터의 Item V2 커밋과 Ready 투영 로드는 차단하며,
  정상 journal의 다른 파티원은 자기 인벤토리를 계속 읽을 수 있다.
  이후 인벤토리 로드를 다시 요청하면 영수증을 다시 확인한다. 자동 폴링/UI 복구는 추가하지 않았다.
- 현재 프로세스의 기존 픽업 재시도는 같은 본문으로 계속한다. 확정 성공/기존 receipt policy의
  확정 거절은 기록을 정리한다. 종료·바뀐 subsystem/journal의 늦은 복원 응답은 파일을 삭제하지 않는다.

이 디렉터리는 서버 전용 ACL과 재시작 이후에도 유지되는 저장 볼륨을 전제로 한다.
`-UserDir`를 쓰면 실제 Saved 경로도 바뀌므로 재시작 때 같은 경로를 넘겨야 한다.
배정 하나의 디렉터리를 한 프로세스만 소유해야 하며, 다중 프로세스 fencing/호스트 교체·
서버 ID 변경 이관·디스크/전원 장애까지의 보장은 이번 단계에 포함하지 않는다.
영수증이 없는 요청의 안전한 재전송은 현재 세션/멤버십 권한과 드롭 identity를 영속 연결한
후속 계약이 필요하다. 기록을 임의 삭제해서 차단을 해제하는 복구는 제공하지 않는다.

최종 Development 게임 모듈 6개 작업(컴파일 4개·lib/DLL 링크 2개)이 성공했다.
DLL은 `06B8E5832C404A5529ED18D7E891D758D7796683AD6DF3A558394184B5404291`이며,
최종 빌드 로그는 `Saved/Logs/PickupGrantJournal-EditorModule-Final-20261012.log`이다.
신규 journal 7개와 직접 영향 Codec/Gateway 2개·픽업 runtime 4개, 총 13개가 exit 0으로 통과했다.
디스크 기록이 Send보다 먼저 생기는지, 새 journal/subsystem의 읽기 전용 복원,
미확정 원래 소유자만 차단, 저장 실패의 무전송, 손상·인증·수량·늦은 응답 보존을 검증했다.
모의 HTTP 응답과 실제 격리 파일을 사용하는 **새 인스턴스 검사**이며 실제 서버 프로세스 재시작은 아니다.
네이티브 proof: `Saved/Tests/Item/Automation-27757cfac0f14eb38c2a9fadfe4ee562/Item.proof.json`.

같은 DLL의 실제 1인 BP NPC/사망/드롭 → 기존 픽업 RPC → HTTP/PostgreSQL 단일 지급,
중복 거절·소유자 격리·정확한 HTTP 재전송·소유 백엔드만 재시작 회귀도 exit 0이다.
`Saved/Tests/NPCPickupBackend/30f25606fa4d47f782ecbb85f8e4d336/NPCPickupBackend.proof.json`에 기록했다.
실제 서버의 `server-User/Saved/ItemPickupJournal/7dd0cf93-2e11-490f-9b75-357467cee4c5/`가
만들어졌고 성공 후 미확정 파일이 남지 않는 것도 확인했다. 이 실행은 응답 유실/UE 재기동 검사가 아니다.
기존 개발 신원·정지 NPC/서버 피해·임시 행·배치·스크립트 입력·입장까지 heartbeat fixture를 유지했다.
BP/드롭 콘텐츠·운영 API·기본 lease·기존 DB 데이터는 변경/삭제하지 않았다.

러너의 복수 `RunTests` queue 제한(`ae7e692d…`)과 종료 로그 공유 핸들(`57d84387…`)은
실패 proof와 당시 러너를 보존했다. 단일 `+` 선택식/명시적 공유 읽기로 보완한 최종 실행과 구분한다.
선택 소스 30개/E2E 소스·자산 38개와 모든 최종 로그/DLL 해시를 다시 확인하고, 수정 파일 11개를 보관했다.
통합 검증 기록은 `Saved/Tests/PickupGrantJournal-20261012-159b8652f5974dedab496eef65542da02/Verification.proof.json`이다.
전체 Editor 타깃/Shipping·Steam, 실제 UE 강제 종료/같은 볼륨 재시작+HTTP/DB 영수증 복원,
미저장 요청의 권한 있는 replay·월드 드롭/처치 identity 복구는 아직 완료하지 않았다.

#### 실제 UE 서버 재기동 검증 경로 준비 (2026-10-13, 실행 미완료)

이 절은 준비/중단 당시 기록이다. 이후 실제 완료 결과는 아래 `실제 2인 UE 서버 재기동 복구 통과` 절에 있다.

기존 `Run-NPCPickupBackendE2ETest.ps1`에 `-RestartServerAfterLostAck`를 연결했다.
`-LosePickupAck -ClientCount 2`가 필요하며 기존 BP 사망/드롭·픽업 RPC·실제 인증/티켓 경로를 재사용한다.
운영 지급 코드·BP/보상 콘텐츠·백엔드 API는 변경하지 않았다. 별도 서버 배정이나 신원을 주입하지 않는다.

1. 실제 DB 커밋 후 HTTP 응답 유실, pending claim/전원 빈 Projection 및 거절 3개를 확인한다.
2. 서버 Saved 경로의 원래 journal 본문 SHA256을 실제 HTTP 본문/요청·배정 scope와 비교한다.
   자기 UE 프로세스 3개만 종료하고, 종료 뒤 journal 불변/원래 로그를 검증·보관한다.
3. 같은 server/session·포트·`server-User` 경로로 새 전용 서버를 띄운다. 백엔드/PG는 재시작하지 않는다.
   기존 heartbeat fixture로 재입장 때까지만 배정을 유지하고 기본 90초 lease 정책은 바꾸지 않는다.
4. 새 클라이언트 프로세스 두 개가 기존 로그인·세션 복원·새 Join Ticket으로 입장한다.
   복구 probe는 읽기/관측만 하며 NPC 생성·픽업 RPC·지급을 실행하지 않는다.
5. 원래 영수증 GET·전원 최신 인벤토리 GET, 소유자 1개/나머지 0개 투영과 DB 전체 Record/영수증 불변,
   journal 해소를 함께 검증한다. 최종 자기 UE 로그 6개와 원래 pending 파일/소스·자산·DLL 해시가 있어야 완료다.

HTTP fixture의 `restart-read-only` 모드는 해당 서버 토큰의 모든 비-GET/본문 있는 요청을
백엔드에 전달하기 전에 차단한다. 원래 RequestId 영수증/배정 캐릭터 인벤토리 조회만 허용하며
전체 영수증 Record를 첫 DB 성공 응답과 비교한다. Node 제어 토큰은 Node에만 전달한 뒤
부모 환경에서 제거하며 UE 프로세스의 제어 토큰/클라이언트의 게임 서버 토큰 상속도 거절한다.
원래 서버 ID와 요청 GUID는 복구 probe의 비교값일 뿐 인증 상태나 아이템을 주입하는 값이 아니다.

프록시의 기존/신규 로컬 HTTP mock 9개와 새 경계/복구 판정기 20개가 통과했다.
다른 소유자/ItemId, 성공 ack 선행, 중복 거절/클라이언트, 패자 투영 유출, 쓰기 시도,
누락/잘못된/비-200 영수증과 과거/비정상 단조 시각을 거절한다. 스크립트 3개의 구문 검사도 성공했다.
자체 선택 게임 모듈 확인은 `NPCPickupServerRestart-EditorModule-20261013.log`의
up-to-date/0 action이며 새 compile/lib/DLL 링크나 전체 Editor/Shipping 성공으로 계산하지 않는다.
공유 DLL에 두 복구 관측 문자열이 있는 것을 확인했지만 실제 재기동 성공을 뜻하지 않는다.

실제 2인 시도 `Saved/Tests/NPCPickupBackend/037bfadb1806479eb4a891f56de6ff32/`는
양쪽 초기 입장과 NPC 사망까지만 남아 있고 이후 소유 프로세스/완료 proof가 없다.
중단 원인은 확정하지 않았으며 `Interrupted.proof.json`으로 남겼다.
드롭·DB 미확정 경계·서버 재기동은 관측하지 않았으므로 실패/중단을 통과로 바꾸지 않는다.
이 준비 기록을 저장할 때는 Docker daemon이 꺼져 있어 실검증을 다시 실행하지 못했다.
준비 proof/모의 테스트 로그/수정 소스 7개는
`Saved/Tests/NPCPickupServerRestart-20261013-0395c92edb5143818c56e8a815f8e674/Preparation.proof.json`에 보관한다.

Docker 준비 후 실행:

```powershell
.\Scripts\Network\Run-NPCPickupBackendE2ETest.ps1 -ClientCount 2 -LosePickupAck -RestartServerAfterLostAck
```

이 검증도 자동 재접속 UX, 미저장 명령의 권한 있는 replay, 월드 드롭/처치 identity 재생성,
호스트 이관/동시 프로세스 fencing, Steam·Shipping을 완료하지 않는다.

#### 실제 2인 UE 서버 재기동 복구 통과 (2026-10-13)

Docker 준비 이후 같은 러너의 실제 2인 실행이 **exit 0**으로 완료됐다.
`project-rpg-online`의 기존 호출/권한 경계 재사용 원칙에 따라 새 지급 API나 RPC를 만들지 않고
저장된 NPC BP/사망/드롭 → 기존 픽업 RPC → HTTP/PostgreSQL → 로컬 journal →
실제 서버 강제 종료 → 같은 배정/포트/서버 UserDir 재기동 → 새 로그인/Join Ticket 입장을 검증했다.
자연 전투가 아닌 정지 NPC/서버 피해·임시 드롭 행·배치·스크립트 입력의 개발 fixture다.

- 원래 NPC/픽업은 net ID `74`/`96`이었다. DB 커밋 뒤 응답을 한 번 끊고 전원 빈 Projection,
  미확정 pickup claim/살아 있는 픽업 거절 3개와 실효 gateway 2회 이후 약 2.028초 재시도를 확인했다.
  종료 전에 journal 요청/배정과 실제 HTTP 본문 SHA256이 같았고, 종료 후 파일 해시도 그대로였다.
- 원래 UE PID 3개 `30084/30320/30644`가 종료된 뒤 새 PID `7024/14036/29984`로 실행했다.
  백엔드/공유 PostgreSQL은 살아 있는 채 유지했으며 서버 ID/세션·Saved 경로는 바꾸지 않았다.
  재입장까지 기존 배정 heartbeat fixture 11회로 유지했고 기본 lease/권한 정책은 변경하지 않았다.
- 소유자는 원래 두 번째 클라이언트 `82a0068c-0c38-4074-8f55-53edf5a01db4`였다.
  원래 요청 `2be93ef6-42d4-1da6-02f1-9a9cb3f772f6`, 아이템
  `34bd517b-4d5a-3366-8933-fe82f22eb2d4`를 같은 수량 1/revision 1로 복구했다.
  새 서버/두 클라이언트의 native 관측, DB 전체 Record/영수증 불변, 소유자 1개/다른 멤버 0개 및
  타인 아이템·인벤토리/플레이어 영수증 조회 거절을 확인했다. 성공 후 원래 pending journal은 비었다.
- HTTP fixture는 원래 요청의 **영수증 GET 1회·두 멤버 최신 인벤토리 GET 각 1회**를 관측했다.
  첫 DB 성공 응답과 같은 전체 Record 해시였으며, 복구 이후 아이템 쓰기는 없었다.
  프로세스 안의 재시도 성공/옛 클라이언트 ack를 재기동 복구로 계산하지 않는다.
- 자기 UE 종료 로그 6개, 원래 pending 파일 사본, 소스·자산 41개, 러너/파서/백엔드 모듈 해시를
  최종 proof에 저장했다. 소스·자산/로그/파일 사본 해시를 독립 재확인했고 이전 중단 proof도 유지했다.
  본 작업은 BP/콘텐츠·운영 지급 코드/API·기존 DB 데이터를 삭제/변경하지 않았다.

완료 proof는
`Saved/Tests/NPCPickupBackend/fbda81d660cd4309b6e4ed306717fc41/NPCPickupServerRestart.proof.json`이다.
검증 DLL은 `B371525AA894CA92CAD74660E19E808E72EF52C3A3A2E87EAFEB7CC635850A12`이며,
완료 proof 저장 시각 14:05:05 UTC 뒤 14:05:40 UTC에 공유 DLL이 `B5589623…`로 갱신됐다.
이 결과를 더 새 DLL의 통과로 표시하거나 그 해시 변경만을 이유로 전체 검사를 반복하지 않았다.
자체 빌드는 선택 모듈의 0 action up-to-date 확인이고 새 전체 타깃/자체 compile·DLL 링크 성공은 아니다.
모의 HTTP 9개·오프라인 판정기 20개와 수정 소스 7개, 완료 proof/독립 해시 검사 및 DLL 범위 차이는
`Saved/Tests/NPCPickupServerRestart-20261013-0395c92edb5143818c56e8a815f8e674/Verification.proof.json`에 있다.

이번 완료는 **DB에 이미 저장된 미확정 요청의 같은 호스트/배정 재기동 복구**다.
새 클라이언트를 명시적으로 실행했으므로 자동 재접속 UX 검증은 아니다.
미저장 명령의 현재 멤버십/권한 있는 replay, 월드 드롭/처치 identity 재생성, 호스트 이관/다중 프로세스
fencing, 실제 Steam·Shipping·4인/패킷 손실은 남는다. 공유 PostgreSQL/볼륨·개발 행은 보존했다.

#### 서버 Drop ID와 예약·지급 journal v2 (2026-10-14)

기록 마무리: 2026-10-15. 기존 `ARPGPickUpBase` BP 상호작용/`ServerPickupItem` RPC를
유지하고 네이티브 지급 경계만 새 예약 API에 연결했다. 새 BP/RPC·클라이언트 지급 매개변수와
저장 에셋 변경은 없다. 현재 생산 호출은 `FPickupBackendCommitter`와 prepared claim을 사용한다.

```text
서버 Drop ID + journal v2 저장
  → 예약 POST / scope·전체 원래 명령 대조
  → 빈 본문 소비 POST / 전체 지급 영수증 대조
  → journal 해소 · Projection 반영 · 월드 픽업 정리
```

- `PickupDropId`는 spawned authority의 `PostInitializeComponents`에서 만들고 읽기 전용으로
  복제한다. 새 manifest/풀 재사용에는 새 ID를 만들지만 pending 동안 재초기화/외부 해제는
  ID와 claim을 바꿀 수 없다. 클라이언트는 계속 액터만 보내며 Drop ID·ItemId·수량·목적지를
  RPC 입력으로 지정하지 않는다. 이 ID는 런타임 논리 드롭의 식별자이며 월드/처치 영속 레지스트리가 아니다.
- `CommitPickup`은 유효 Drop ID와 원래 명령을 schema v2 journal에 기록/flush/rename한 뒤만
  전송한다. 동일 Request ID의 본문/Drop ID 변경, 같은 Drop ID의 다른 요청과 v1→v2 소급 채택을
  거절한다. main/tmp의 Drop ID 충돌·v2 descriptor의 버전만 낮춘 파일도 전체 복원을 닫고 보존한다.
- 준비 응답의 서버·세션·드롭·요청·캐릭터·아이템·시각과 전체 정규화 명령을 대조한다.
  같은 fingerprint의 seed 변경도 소비 POST를 허용하지 않는다. 소비 본문은 비우고 200의
  완전히 일치하는 성공 영수증만 적용한다. 비-200에 실린 성공 본문은 거절한다.
- 각 단계의 순간 오류 재시도는 같은 경로/본문을 사용한다. game thread에서 현재 subsystem/
  journal과 인증 claimant를 다시 확인하며 권한 상실/teardown 뒤 두 번째 POST와 후속 재시도를
  막는다. 기존 진행 중 커밋을 취소/롤백했다고 주장하지 않는다.
- 슬롯/Revision 충돌에도 prepared journal과 frozen claim을 보존한다. 다른 수령인·새 ID·
  새 슬롯으로 풀어주지 않고 기존 타이머는 같은 원래 명령을 재시도한다. 원래 슬롯이 비워지면
  동일 요청으로 성공할 수 있지만 취소/목적지 변경·수동 복구 UX는 별도다.
- 일반 `URPGItemBackendSubsystem::Commit(Item.Pickup)`은 Drop ID 없는 신규 지급을 거절한다.
  장비/소비 등 일반 명령은 기존 경로를 유지한다. 백엔드의 일반 Commit API 자체를 삭제하거나
  전역 예약 검사로 바꾼 것은 아니다.

복원된 v1 journal은 이전의 원래 영수증 GET만 사용한다. v2는 먼저 현재 scope의 예약 GET과
전체 명령/Drop ID·`consumedAt`를 확인한 뒤 원래 영수증 GET, 최신 인벤토리 GET으로 진행한다.
아직 소비되지 않은 예약·404·권한/통신/프로토콜 실패에는 자동 POST나 일반 영수증으로의
우회가 없다. 원래 캐릭터의 Ready/변경 장벽과 다른 파티원의 읽기 권한을 유지한다.

검증: 자체 Development Editor 게임 모듈의 UHT 및 23개 compile/lib/DLL 작업 성공,
`-PickupJournalOnly` 신규 7개+직접 영향 기존 13개, 총 20개/러너 exit 0.
실제 격리 파일과 제어된 transport 응답으로 v2 동결/호환/손상, 단계별 동일 재시도,
scope/본문 위조·비-200 성공, 권한/수명 상실, 미소비 예약의 무 replay, 읽기 전용 복구와
prepared 런타임 충돌/풀 identity를 확인했다. 에셋 cold-load/실제 BP·Iris/HTTP/PostgreSQL/Steam/
Shipping·전체 Editor 타깃/전체 Item 회귀 검증은 아니다.

네이티브 증거: `Saved/Tests/Item/Automation-e25e8aee055147b3b7c7a5b07dcda6cb/Item.proof.json`.
검증 DLL `FC98595D…`는 실행 전후 같았으며 완료 후 공유 DLL이 `09C5DAF0…`로 갱신됐다.
더 새 DLL의 통과로 표시하지 않고 소스/종료 로그/당시 DLL 해시를 보존한다.
통합 기록: `Saved/Tests/PreparedPickup-20261014-12fccd365a8b4c5eae633fb75c54f991/Verification.proof.json`.

실적용에는 새 예약 API와 `consumed_at` schema를 지원하는 백엔드가 필요하다. 이 단계에서
기존 실행 중 백엔드나 public DB를 재시작/이관하지 않았으며 구 API로의 자동 fallback도 없다.
다음은 기존 NPC 픽업 E2E 러너/프록시/판정기를 prepare+빈 본문 consume/v2 journal에 맞춰
갱신하고 실제 저장 BP→RPC→HTTP/PostgreSQL 단일 지급·거절·유실/재기동을 검증하는 것이다.
이전 일반 커밋/재기동 성공 proof를 새 프로토콜 성공으로 재사용하지 않는다.
미소비 예약의 권한 있는 replay·월드 드롭/처치 복원·호스트 fencing은 아직 남는다.

#### 예약·소비 실제 E2E 경로 준비와 동시 수정 중단 (2026-10-15)

`Run-NPCPickupBackendE2ETest.ps1`의 기본 경로를 prepare → 빈 본문 consume으로 갱신했다.
기존 저장 BP·액터만 전달하는 RPC는 유지하고 개발용 `SERVER_DROP` 표식에 서버 Drop ID를
추가했다. 관측 프록시는 첫 전송 전에 실제 journal v2의 전체 명령/할당/Drop ID를 확인하고,
전달한 원래 바이트와 예약 응답·전체 성공 영수증을 대조한다. 일반 커밋 우회·소비 본문·
변조된 journal/응답은 전달 또는 성공 인정 전에 거절하며, 자격 증명/명령 본문은 로그하지 않는다.

러너는 현재 수령인과 요청·아이템·드롭을 네이티브 로그/HTTP/DB에 걸쳐 연결한다. PostgreSQL의
예약/소비 표시·아이템·영수증을 한 읽기 전용 쿼리로 확인하고 동일 소비 재전송과 소유 백엔드
재시작 전후 전체 행의 불변을 검사한다. 플레이어의 예약 조회/소비 403, 거절된 요청의 예약/
영수증 404, 수량 1/revision 1과 journal 해소도 포함한다. DB 영수증은 도메인 직렬화의
계산된 `itemId` 별칭을 갖기 때문에 원래 PK/state ID와 일치하는지 검사한 뒤 그 필드만 Web
계약으로 투영한다. 첫 실제 실행에서 이 차이로 발생한 판정 실패와 수정 회귀를 보존했다.

선택 검증: 프록시 22개(기존 9+신규 13), 신규 판정기 44개, 기존 판정기 34개 및 PowerShell
구문 검사 통과. 최초 자체 게임 모듈 compile/lib/DLL 3개 작업과, 다른 작업의 공유 소스 변경을
반영한 선택 게임 모듈 6개 작업이 성공했다. 전체 Editor 타깃·Shipping 빌드/실행은 아니다.
관련 없는 기존 네이티브/백엔드 전체 테스트를 반복한 결과로 표시하지 않는다.

실제 인증 1인 BP → RPC → prepare/consume → 단일 지급, 동일 요청 재전송·플레이어 권한 차단·
소유 백엔드 재시작 후 DB 불변과 닫힌 네이티브/프록시 관측까지 실행했다. 그러나 **최종 러너
통과는 아니다**. 실행 도중 다른 작업이 공유 입장 probe, 다음 실행에서는 NPC 본체/헤더를
변경해 마지막 소스/자산 해시 검사에서 두 번 거절됐다. 실행 DLL 불변과 기능 관측을 별도로
보존하고 다른 작업을 되돌리거나 해시 검사를 완화하지 않았다. 이후 재실행은 조율 전까지 중단한다.

- 최초 본문 판정 실패: `Saved/Tests/NPCPickupBackend/cc5ec7652a3645d2b310190dca7fe43d/Failure.proof.json`.
- 재개 전 중단: `Saved/Tests/NPCPickupBackend/ff3ee84b3f8d49ad8916f1c2b999e737/Interrupted.proof.json`.
- 공유 입장 probe 변경: `Saved/Tests/NPCPickupBackend/aeb86a3f2bf0443a91982cf84fe05e70/Failure.proof.json`.
- 공유 NPC 변경: `Saved/Tests/NPCPickupBackend/c9324b97bd13406c895ae91da419ca3e/Failure.proof.json`.

마지막 두 실행의 `PreparedPickup.database.proof.json`/`PreparedPickup.proxy.proof.json`은
그 경계의 증거이며 최종 PASS 파일을 대신하지 않는다. 모듈 `C5AA9FBC…`와 실행/소스 범위,
선택 테스트·실패/중단을 통합한 기록은
`Saved/Tests/PreparedPickupE2E-20261015-26515810d31541c2a640dad511d890cd/Verification.proof.json`이다.

재현 명령은 아래와 같다. 실행 중 관련 C++/자산/DLL 변경이 없어야 하고 현재 선택 소스가
게임 모듈보다 새로우면 먼저 모듈을 빌드한다. 로컬 Docker/PostgreSQL과 개발 인증·기동 입장까지의
할당 heartbeat, 정지 NPC/서버 피해·배치/임시 드롭 표를 사용하는 fixture다. 개발 DB의 새 fixture
행과 기존 데이터/볼륨은 보존하며 새 API/schema 초기화는 소유 로컬 백엔드의 기존 기동 경로다.

```powershell
& Scripts/Network/Run-NPCPickupBackendE2ETest.ps1 -ClientCount 1 -NoBackendRestore
```

`-LegacyProtocol`은 과거 일반 커밋에 맞는 바이너리/fixture용 명시적 선택이며 현재 native 코드를
구형 지급으로 바꾸는 fallback이 아니다. 이 시점에는 새 경로의 응답 유실/UE 재기동이
미지원이었다. 이후 `-LosePrepareAck`/`-LosePickupAck`는 아래 2026-10-16 기록에서 연결했고,
새 경로의 `-RestartServerAfterLostAck`도 아래 journal v2 재기동 절에서 후속 연결했다. 기존 구형 성공 proof나
이번 준비 시점의 기능 관측을 후속 통과로 계산하지 않는다.
자연 전투·Steam/멀티 PC·Shipping, 미소비 예약 replay·월드/처치 복원·호스트 fencing은 별도다.

#### 예약·소비 기본 1인 E2E 통과와 백엔드 입력 격리 (2026-10-16)

이전 동시 수정 중단의 후속으로 새 프로토콜 기본 1인 러너가 최종 **exit 0/PASS**했다.
실제 저장된 NPC 사망/드롭 BP → 기존 픽업 RPC → journal v2 선저장 → 예약/빈 본문 소비 →
클라이언트 Projection 단일 지급/월드 픽업 정리를 검증했다. 서버 Drop ID·전체 원래 명령/영수증,
성공 1/중복 거절 1·수량 1/revision 1, 플레이어의 예약 조회/소비 403과 거절된 요청의 예약/
영수증 404·journal 해소, 동일 소비 재전송과 소유 백엔드 재시작 후 DB 전체 행 불변도 확인했다.

이번 첫 시도 역시 공유 백엔드 소스/schema가 실행 중 바뀌어 마지막 해시 검사에서 거절됐다.
`NPCPickupBackendSnapshot.ps1`로 빌드 입력을 실행별 `backend-source`에 고정하는 보완을 했다.
현재 프로젝트의 전체 소스·csproj·설정·schema와 `-NoBackendRestore`의 기존 restore metadata를
복사 전후 해시/입력 목록으로 검증한 뒤 그 사본의 프로젝트만 소유 출력 폴더로 빌드한다.
실행 시 schema는 기존과 같이 그 빌드 출력의 `Database/schema.sql`을 읽는다. 공유 원본이
그 뒤 바뀌더라도 원래 경로/캡처 해시와 실제 사용한 사본을 구분하며, **사본·DLL·UE/BP/선택
네이티브 소스의 최종 변경 검사는 유지**한다. 검사 대상을 단순 제외하거나 구형 API로 우회하지 않는다.

외부 ProjectReference/Import/명시 Compile·지원하지 않는 Content/부모 MSBuild 설정은 입력
포착을 확장하기 전까지 거절한다. 캡처 중 변경/입력 추가·삭제, 경로 이탈·reparse point와 기존
사본 덮어쓰기도 거절한다. SDK/전역 패키지 캐시까지 밀봉한 빌드나 운영 다중 호스트 fencing은 아니다.
새 격리 테스트 15개와 실제 격리 백엔드 빌드(경고/오류 0), 새 경로의 실제 E2E가 통과했다.
기존 프록시/판정기와 전체 네이티브/백엔드 회귀를 반복한 결과로 표시하지 않는다.

최종 증거:
`Saved/Tests/NPCPickupBackend/fcc5c948eb2e48e9929604de778bfb5a/NPCPickupBackend.proof.json`.
같은 폴더의 `BackendSnapshot.proof.json`, `PreparedPickup.database.proof.json`,
`PreparedPickup.proxy.proof.json`, `BackendSnapshotTests.log`와 `Verification.proof.json`에
입력 115개(격리 백엔드 67개·restore metadata 3개 포함), 닫힌 서버/클라이언트 로그 및
실행 DLL `3ED9ED89…`의 독립 해시 확인을 기록했다. `VerifiedInputs`/실행 DLL 사본도 보존했다.
첫 공유 백엔드 변경 시도는 `Saved/Tests/NPCPickupBackend/91f41c5f8c7d4bdd91a4ddbe8c7f674e/Failure.proof.json`에 남겼다.

기존 게임 모듈을 사용했고 이번 자체 UE 재빌드는 없었다. 원래 에디터/다른 Cmd 프로세스를
종료하지 않았으며 저장 BP·운영 지급 코드는 수정하지 않았다. fixture 행과 소유 백엔드의 기존
schema 초기화 경로를 사용했고 기존 데이터/볼륨을 삭제·초기화하지 않았다. 개발 인증,
정지 NPC/서버 피해·배치·임시 드롭 표·입장 완료까지만 할당을 유지하는 heartbeat fixture이며
기본 lease/운영 초기 부팅·자연 전투/Steam/Shipping 통과는 아니다. 다음은 새 방식의 실제
2인 경합/패자 격리다. 새 방식의 응답 유실·읽기 전용 UE 재기동/거절 보존, 미소비 replay·월드/
처치 복원·호스트 fencing은 여전히 별도다.

#### 예약·소비 2인 경합·패자 격리 E2E 통과 (2026-10-16)

새 prepare/빈 본문 consume 경로의 실제 2인 E2E가 최종 **exit 0/PASS**했다.
저장된 NPC 사망/드롭 BP와 기존 상호작용 RPC를 사용해, 전원이 같은 Iris 픽업·획득 거리·
빈 Projection을 확인한 뒤 공통 파일 gate로 두 요청씩 보냈다. 총 4개 중 성공 1·거절 3,
승자 아이템 1개/수량 1/revision 1·패자 Projection/인벤토리 0개·픽업 정리를 확인했다.
패자의 타인 아이템/인벤토리 조회와 전원 플레이어의 예약 조회/소비·서버 영수증 조회는 403,
서로 다른 거절 요청 3개의 예약/영수증은 404다.

서버 Drop ID → 전송 전 journal v2 → 원래 전체 명령의 예약 응답 → 빈 본문 소비의 전체
성공 영수증을 대조했다. 프록시의 신규 예약/소비는 각각 1회이며 journal이 해소됐다.
DB의 소비된 예약·아이템·영수증, 정확한 소비 재전송의 `AlreadyCommitted`와 소유 백엔드
재시작 뒤 전체 행 불변/소유자 격리도 통과했다. 원래 UE 게임 서버/클라이언트 재기동이 아니다.

실행 및 최종 독립 검증:
`Saved/Tests/NPCPickupBackend/3c79524be3214344984ea14f8a76b2a0/NPCPickupBackend.proof.json`,
같은 폴더의 `Verification.proof.json`.
닫힌 서버 로그 1개/클라이언트 로그 2개를 기존 파서로 다시 대조하고 승자/패자·세 거절 ID와
Drop ID/요청/아이템을 DB/프록시 기록에 연결했다. 입력 115개(격리 백엔드 입력 67개·restore
metadata 3개 포함)와 실행 DLL `2A3DEF3F…`, 격리 백엔드 DLL/출력 schema의 해시를 확인하고
`VerifiedInputs`/실행 DLL 사본을 보존했다. 기존 1인 증거는 `3ED9ED89…`의 별도 실행이며
이번 DLL의 전체 네이티브/백엔드 자동화 통과로 확장하지 않는다.

자체 UE 재빌드/운영 코드·BP 변경은 없고, 기존 러너/프록시/판정기가 변하지 않아 단위 회귀를
반복하지 않았다. 실행별 백엔드 사본은 경고/오류 0으로 빌드했다. 기존 에디터와 다른 Cmd
프로세스를 종료하지 않았으며, fixture 행/소유 백엔드의 기존 schema 초기화 경로를 사용하고
기존 데이터/볼륨은 삭제·초기화하지 않았다.

클라이언트 지연 250ms/손실 0%, 개발 인증·정지 NPC/서버 피해·배치·임시 드롭 행·기동 입장까지의
할당 heartbeat fixture다. 각 거절이 DB 커밋 전 pending에서 발생했는지의 정확한 시점,
슬롯/Revision 거절 보존의 실통합·새 방식의 응답 유실/재시도·읽기 전용 UE 재기동, 4인/패킷 손실,
자연 전투·운영 Steam/멀티 PC·Shipping 및 미소비 replay·월드/처치 복원·호스트 fencing은 별도다.
다음은 새 예약/소비 단계의 응답 유실을 주입하는 fixture/판정기를 연결해, 원래 frozen 요청과
빈 본문 소비 재시도가 단일 지급만 유지하는지 검증하는 것이다.

#### 예약·소비 응답 유실과 동일 요청 복구 E2E (2026-10-16)

새 프로토콜의 두 실제 2인 E2E가 최종 **exit 0/PASS**했다. 기존 저장 BP/RPC와 native
관측을 재사용했고 변경은 개발용 프록시·러너·판정기/테스트다. C++/운영 API·지급 정책/BP
변경이나 자체 UE 재빌드는 없었으며 실행 DLL은 `2A3DEF3F…`다.

- `-LosePrepareAck`: 완전히 대조한 첫 `Prepared` 응답만 1회 끊는다. 같은 서버 Drop ID/
  Request ID·전체 직렬화 바이트·journal v2/준비 시각을 유지한 재시도가 `AlreadyPrepared`를
  받고, 이후 빈 본문 소비가 한 번만 `Committed`가 됐다. native 예약 요청 2/소비 요청 1이다.
  이 경우는 단계 내 재시도이며 장기 native pending/타이머 검증으로 표시하지 않는다.
- `-LosePickupAck`: 첫 소비의 전체 `Committed` 응답을 받은 뒤 UE 응답만 끊고 같은 빈 본문
  소비 재시도를 일시 차단한다. 직접 인증 GET/단일 DB snapshot으로 이미 소비된 예약·아이템/
  전체 영수증을 확인하고 원래 journal v2 사본/해시를 보존했다. native 픽업 잠금·전원 빈
  Projection·살아 있는 픽업에서 거절 3개를 관측한 뒤 재시도를 허용했다. `AlreadyPrepared`의
  원래 명령과 빈 본문 `AlreadyCommitted`/클라이언트 `AlreadyApplied`로 복구했다. native 경로의
  예약 3/전달된 소비 2·차단된 소비 wire 재시도 5회이며 실제 추가 지급은 없다.

소비 유실 첫 실행은 기존의 **설정된 게이트웨이 횟수 = 빈 POST wire 횟수** 가정으로 타이머
판정에 실패했다. DB 저장/잠금·거절 관측과 실패 proof/당시 입력을 보존했다. 실제 성공 실행에서도
설정은 2였지만 첫 두 차단 wire 간격은 약 16ms여서 그 인덱스가 native 타이머 경계가 아니었다.
소비 실패 묶음 뒤 처음 다시 온 `AlreadyPrepared` 요청의 **도착 단조 시간**과 직전 실패 소비를
대조해 약 2.063초를 확인하도록 수정했다. UTC나 지연된 HTTP 응답 시각으로 타이머를 추정하지
않고, 원래 소비됨·명령/본문/journal/영수증 일치와 최소 1.5초 조건을 유지한다.

두 경우 모두 4개 RPC에서 성공 1/거절 3·패자 인벤토리 0·타인 조회/플레이어의 예약/소비/
영수증 403, 거절 예약/영수증 404·journal 해소를 확인했다. 러너의 별도 정확한 소비 재전송과
소유 백엔드 재시작 뒤에도 DB 전체 행과 소유 경계는 같았다. 소비 유실은 첫 미확정 DB snapshot과
복구 뒤 snapshot도 완전히 같고, 보존한 journal의 정규화 전체 명령과 원래 DB 예약을 대조했다.
원래 HTTP 전체 영수증 hash는 DB 도메인 레코드의 `itemId` 별칭을 검증/투영한 전체 레코드와 같다.

프록시 32개(기존 9+prepared 기본 13+새 유실 10), prepared 판정/타이머 87개,
기존 파티/타이머 파서 34개 및 PowerShell 구문 검사가 통과했다. native/backend 전체 자동화
재실행이나 전체 Editor/Shipping 빌드로 계산하지 않는다. 두 격리 백엔드 빌드는 경고/오류 0이다.

증거:

- 소비 유실: `Saved/Tests/NPCPickupBackend/2c54dfd1308b46d5a55b5d6b6198d267/NPCPickupBackend.proof.json`.
- 예약 유실: `Saved/Tests/NPCPickupBackend/e92ed2147e394952a1b3f4cfaca50638/NPCPickupBackend.proof.json`.
- 최초 타이머 판정 실패: `Saved/Tests/NPCPickupBackend/38aec538aa17481398b2151fb9a0eeb8/Failure.proof.json`.
- 두 실행/입력·닫힌 로그·DB·프록시·원래 journal의 독립 대조와 선택 테스트/소스 사본은 소비
  유실 폴더의 `Verification.proof.json`, `VerifiedInputs`/`TestSources`에 있다. 각 실행 입력은
  117개이고 원래 에디터/다른 Cmd 프로세스·기존 데이터/볼륨은 보존했다.

```powershell
& Scripts/Network/Run-NPCPickupBackendE2ETest.ps1 -ClientCount 2 -LosePrepareAck -NoBackendRestore
& Scripts/Network/Run-NPCPickupBackendE2ETest.ps1 -ClientCount 2 -LosePickupAck -NoBackendRestore
```

두 유실 옵션은 서로 배타적이다. 이 두 실행 당시 미지원이던 새 방식의 UE 재기동은 다음 절에서 검증했다. 개발 인증·정지 NPC/
서버 피해·배치·임시 드롭 행·기동 입장까지의 heartbeat 및 클라이언트 지연 250ms/손실 0% fixture다.
슬롯/Revision 확정 거절의 실통합·정확한 DB 커밋 상대 거절 시점, 4인/패킷 손실·자연 전투/
운영 Steam/멀티 PC·Shipping·미소비 replay·월드/처치 복원/호스트 fencing은 별도다.
후속 검증은 실제 UE 서버를 종료/재기동한 뒤 v2 journal의 원래 소비된 예약→영수증→최신
인벤토리를 읽기만 해서 복원하고 추가 아이템 쓰기 0을 확인하는 것이다.

#### journal v2 실제 UE 재기동과 읽기 전용 복구 통과 (2026-10-16)

`-ClientCount 2 -LosePickupAck -RestartServerAfterLostAck -NoBackendRestore`가 새 예약/소비
프로토콜에서 최종 **exit 0/PASS**했다. 기존 native v2 복구 경로를 재사용하고 구형 재기동
fixture/프록시/판정기만 확장했다. C++·BP·운영 API/지급 정책 수정이나 자체 UE 빌드는 없고,
실행 게임 모듈은 `2A3DEF3F…`다. 구형 journal v1 검사도 명시적 legacy 분기로 유지한다.

- 저장된 NPC의 서버 피해→실제 드롭→2인 각 2회 RPC에서 원래 예약/소비 1건만 DB에 저장했다.
  첫 소비 응답을 끊고 재시도를 차단한 동안 native 잠금·전원 빈 Projection·거절 3개와 원래
  journal v2를 확인했다. 같은 본문 재예약 도착과 직전 실패 소비 사이 단조 간격은 약 2.024초다.
- 이 실행이 소유한 UE 서버/클라이언트 3개만 종료하고 journal 해시를 다시 확인했다. 같은 서버
  배정·세션·서버 Saved 경로로 서로 다른 새 PID 3개를 시작하고, 두 클라이언트가 새 로그인/
  Join Ticket의 실제 `Iris=1 Identity=1 PawnGASInput=1` 입장을 완료했다.
- 재기동 후 지정 서버의 아이템 HTTP는 원래 소비된 예약 GET→원래 전체 영수증 GET→소유자의
  최신 인벤토리 GET만 허용한다. 패자 인벤토리 GET은 이 체인과 독립적으로 가능하다. 실제
  관측은 예약→패자 인벤토리 0→영수증→승자 인벤토리 1의 GET 4개이며 모두 200/전달 완료다.
  원래 Drop ID/전체 명령·준비/소비 시각·journal·전체 영수증 레코드를 대조하고, 미소비/변조/
  다른 scope·영수증 선조회·소유자 인벤토리 선조회 및 모든 쓰기는 전달 전에 거절한다.
- 재기동 이후 추가 준비/소비/차단 재시도는 **0개**다. 전체 실행의 준비 2/전달된 소비 1/
  차단된 소비 wire 5개가 복구 전후 그대로다. 일반 커밋 fallback이나 지급 재전송 없이 승자만
  같은 Item ID·수량 1·Revision 1을 복원하고 패자는 0개다. 새 fixture 드롭은 생성하지 않고,
  완전한 영수증 확인 후 journal이 비었다.
- 최초 미확정 상태와 재기동 후 PostgreSQL 예약/아이템/영수증 전체 행이 같다. 보존한 journal의
  전체 명령과 DB 예약, 원래 HTTP 전체 레코드 hash와 DB 영수증을 별도로 대조했다. 플레이어의
  예약/소비/영수증 403·타인 조회 403·거절 요청의 예약/영수증 404도 유지됐다.

프록시 51개(기존 32+새 재기동 19), 재기동 판정 43개(기존 20+새 23), 직접 사용하는 prepared
판정/타이머 87개와 PowerShell 구문 검사가 통과했다. 격리 백엔드 빌드는 경고/오류 0이다.
최종 선택 입력 118개와 러너/파서 2개의 사본, 실행 DLL 사본, 종료된 로그 6개·원래 journal/
DB 전체 snapshot·테스트 로그/소스 해시를 확인했다. 최초 Docker 미기동은 DB/런타임 시작 전
중단이었고 설치된 Desktop 정상 기동 후 기존 PostgreSQL 데이터/볼륨을 유지해 실행했다.

증거: `Saved/Tests/NPCPickupBackend/64f7e8b3bfaf4ceb94f3d4c5250f1941/`의
`NPCPickupServerRestart.proof.json`, `Verification.proof.json`, `ServerRestart.boundary.proof.json`,
`PendingPickup.before-restart.json`, `UnknownAck.blocked.database.proof.json`,
`ServerRestart.database.proof.json`, `VerifiedInputs`/`TestSources`와 종료된 이전/새 UE 로그다.

개발 인증·정지 NPC/서버 피해·배치·임시 드롭 행·입장까지의 heartbeat fixture다. 이번 재기동은
소유 UE 프로세스 재시작이며 이미 별도 검증된 백엔드 재시작이나 자동 재접속 UX와 구분한다.
이 실행 다음으로 남았던 슬롯 충돌은 아래 후속 절에서 검증했다. Revision 충돌·4인/패킷 손실·
커밋 상대 거절 시점/자연 전투·운영 Steam/멀티 PC/Shipping·미소비 요청 replay·월드/처치 복원·
호스트 fencing은 남는다.

#### 실제 슬롯 충돌의 예약 보존과 원래 슬롯 재시도 통과 (2026-10-16)

`-ClientCount 2 -LosePickupAck -SlotConflict -NoBackendRestore`가 최종 **exit 0/PASS**했다.
기존 native prepared claim/409 처리·타이머·pending/`AlreadyApplied` 관측을 재사용하고,
개발용 프록시/러너·판정기만 확장했다. C++·BP·운영 API/지급 정책 변경이나 자체 UE 빌드는
없다. 이전 재기동 실행과 다른 공유 후속 DLL `B11ADAC5…`에서 이번 통합 범위만 확인했다.

- 실제 NPC 드롭/2인 RPC가 정한 원래 슬롯과 journal v2를 먼저 동결했다. 테스트 준비 동안
  native 빈 소비 wire 3개를 백엔드에 보내지 않고 끊은 뒤, 별도 하네스의 정상 서버 커밋 API로
  **이 실행의 새 점유 아이템 하나만** 원래 슬롯에 만들었다. UE Projection이나 native 원래
  명령·기존 사용자 DB 행은 주입/수정하지 않았다. 이는 자연스러운 다른 게임 조작의 경합이
  아니라, 빈 Projection 이후 DB 상태가 바뀌는 조건을 만드는 개발 fixture다.
- 이후 실제 PostgreSQL 소비 API가 `LocationConflict` 409를 두 번 반환/전달했다. 다음 요청의
  도착 단조 시간과 직전 409 응답 사이 약 4.069초를 확인했다. 원래 예약/전체 명령/준비 시각은
  불변·`consumed_at=null`, 원래 픽업 아이템/영수증은 없었다. 별도 점유 아이템은 DB에만 있고
  native 픽업 pending/사용 불가/Projection 합계 0과 살아 있는 픽업의 거절 3개를 확인했다.
  기존 `SERVER_UNCERTAIN` 마커는 이때 잠금/빈 Projection 관측에 재사용했으며 DB 결과는
  **확정된 미지급**이다. 이 마커만으로 커밋 여부가 불명확하다고 해석하지 않는다.
- 같은 Drop ID에 새 요청, 같은 요청의 새 슬롯, 다른 파티 수령인으로 재예약한 실제 HTTP
  세 시도가 모두 409였고 새 예약/영수증도 없었다. 모든 시도 뒤 원래 전체 DB 행과 journal
  hash를 다시 대조했다. 다른 Request ID/슬롯으로 자동 재지급하거나 예약을 해제하지 않았다.
- 정상 서버 커밋 API로 **점유 아이템만** `Destroyed`/Terminal·수량 0·Revision 2로 전이해
  원래 슬롯을 비웠다. SQL 삭제/수정이나 원래 픽업 ID의 교체 없이 같은 native 요청을 재개했다.
  점유 아이템 행과 생성/종료 영수증은 삭제하지 않았으며 독립 SQL 조회에서도 확인했다.
- 같은 원래 명령의 첫 `Committed` 뒤 응답만 1회 유실하고, 기존 유실 fixture로 추가 빈 소비
  재시도를 잠시 차단했다. 지급 후 다음 native 재예약 경계 약 16.048초와 같은 전체 명령/
  journal·`AlreadyCommitted`/`AlreadyApplied`를 확인했다. 소비 전 물리 재예약과 지급 후 재예약을
  구분해 판정하며, 원시 요청 기록은 필터링 없이 모두 보존/대조한다.
- 최종 성공 1/거절 3·승자만 수량 1/Revision 1·패자 0/타인 권한 거절·journal 해소가 유지됐다.
  최초 예약과 최종 예약은 `consumed_at`만 바뀌고, 원래 전체 명령과 DB/HTTP 전체 영수증이
  일치했다. 별도 정확한 소비 재전송과 소유 백엔드 재시작 뒤에도 최종 전체 행은 같았다.
  물리 예약 요청은 6개, 성공 본문을 받은 소비는 2개(최초/멱등), 확정 409는 2개이며 지급 후
  차단 wire는 5개다. 추가 지급은 없고 설정된 게이트웨이 시도 수와 wire 횟수를 동일시하지 않는다.

프록시 58개(기존 51+새 슬롯 7), prepared 판정 114개(기존 87+새 27), 기존 재기동 판정 43개와
PowerShell 구문 검사 4개가 통과했다. 격리 백엔드 빌드는 경고/오류 0이다. 선택 입력 118개+
러너/파서 2개 사본, 종료된 UE 로그 3개·실행 DLL 사본과 DB/원래 journal/테스트 증거 hash를
확인했다. 기존 GUI 에디터와 다른 작업의 프로세스·기존 DB/볼륨은 유지했다. native 전체 자동화/
전체 Editor 타깃·Shipping 빌드의 재통과로 계산하지 않는다.

증거: `Saved/Tests/NPCPickupBackend/552a4cd96aa84b50842e83ee7ca69d2b/`의
`NPCPickupBackend.proof.json`, `Verification.proof.json`, `SlotConflict.boundary.proof.json`,
`SlotConflict.original.database.proof.json`, `SlotConflict.known.database.proof.json`,
`SlotConflict.known.proxy.proof.json`, `SlotConflict.blocker.database.proof.json`,
`SlotConflict.journal-v2.json`, 최종 `PreparedPickup.database.proof.json` 및 `VerifiedInputs`/`TestSources`.

이번 옵션은 기존 pending/멱등 복구 관측을 쓰기 위해 **슬롯 충돌→점유 해제→성공 응답 유실 복구**를
이어 실행한다. 충돌 해제 후 첫 성공 응답을 직접 전달하는 독립 시나리오나 Revision 충돌로
확대하지 않는다. 다음은 기존 Item ID/Revision 충돌에도 기존 아이템 소유권·버전과 원래 예약/
journal이 보존되는지 실통합 검증이다. 개발 인증·서버 피해/배치·기동 heartbeat/클라이언트 지연
250ms·손실 0% fixture이며 4인/손실·자연 전투·운영 Steam/멀티 PC/Shipping·수동 충돌 해소 UX/
미소비 요청 replay·월드/처치 복원·호스트 fencing은 별도다.

#### 실제 Item ID/Revision 충돌의 원래 예약·소유권 보존 (2026-11-01)

기존 prepared 정책을 바꾸지 않고 `Run-NPCPickupBackendE2ETest.ps1`에 opt-in
`-RevisionConflict -LosePickupAck -ClientCount 2`를 연결했다. 이 경로는 슬롯 해제 뒤
성공으로 전환하는 검사가 아니다. 같은 원래 Item ID의 다른 소유자/Revision1 행이 있어
실제 `RevisionConflict`가 나면 지급을 계속 미소비로 보존하고 억지로 해소하지 않는다.

- 실제 NPC 죽음/작성된 드롭 능력·서버 Drop ID와 두 인증 클라이언트의 기존 상호작용 RPC가
  정한 원래 journal v2/명령을 먼저 보존했다. 하네스는 정상 서버 Commit API로 **이번 실행의
  새 Item ID만** 다른 자기 fixture 멤버/빈 가방79번 칸에 생성했다. 기존 사용자 행/SQL이나
  UE Projection은 수정하지 않았다. 생성 시점은 원래 예약 뒤·소비 전인 개발 경합 fixture다.
- 실제 PostgreSQL의 원래 빈 소비 POST가 `RevisionConflict`/409를 두 번 반환/전달했고
  native grant 타이머 경계는 **4.0746069초**다. 프록시는 이를 합성하지 않고 관측했다.
  원래 요청/Drop ID·전체 명령/준비 시각·journal SHA와 `consumed_at=null`이 유지됐으며
  원래 지급 영수증은 끝까지 없고 기존 충돌 아이템의 소유권·수량1·Revision1은 그대로다.
- 같은 Drop ID의 새 요청·새 슬롯·새 아이템 ID·다른 수령인 재예약은 모두 실제 HTTP409,
  새 요청의 예약/영수증 조회는404다. 원래 수령인의 다른 소유자 아이템 조회는403,
  시작 장비를 포함한 양쪽 기존 아이템 전체는 보존됐다. 별도 아이템 삭제·환불·ID 교체는 없다.
- 기존 `SERVER_UNCERTAIN`을 pending/사용 불가/픽업 생존/가방0 관측에만 재사용했다.
  이때 DB 결과는 불확정이 아니라 **확정 미지급**이다. 세 개의 서로 다른 중복/경쟁 RPC는
  살아 있는 픽업 앞에서 거절됐고, 서버/두 클라이언트의 동일 사설 DLL 경로·SHA를 관측했다.

이전 실행기의 세 문제도 실제 실행에서 구분해 보완했다. 긴 UserDir의 소비/단절 journal
디렉터리270자 때문에 안전 가드가 닫힌 사례는 이 모드의 짧은 UserDir215자로 해결했다.
다음은 신규 캐릭터의 기본 장착 대검까지 가방 수량으로 세던 오래된 관측 전제였다.
`RPGNPCPickupNetworkProbe.cpp` 한 파일에서 Inventory 컨테이너만 관측하도록 분리했고
기본 장비는 그대로 두었다. 마지막은 아직 실행 중인 프록시 stderr의 독점 해시 읽기였으며
최종 상태 취득→자기 프록시 종료→닫힌 로그 해시 순서로 보정했다. 세 실패 기록을 최종
성공으로 승격하거나 검사를 느슨하게 만들지 않았다. 생산 지급/RPC·BP 정책 변경은 없다.

직접 영향 집중 검사: 새 판정/옵션41개, v2 SQL 사본을 포함한 backend snapshot16개,
수정한 prepared proxy57개가 통과했다. 기존 native/슬롯 성공/응답 유실 suite는 반복하지 않았다.
defense 담당의 명시적 전체 해제 뒤 정상 Editor 증분 CPP1/라이브러리·DLL 링크2/metadata1,
종료0·입력1,341 불변과 새 관측 마커를 확인했다. 실제 사용 모듈은
`7CD047BACCB43C23F3EAF98CFE953FEF5CF51112263BE08071055539DDD104E7`이다.
최종 UE/HTTP/PostgreSQL은 닫힌 실행 사본에서 통과했으며 같은 DLL의 다른 시험 통과를
빌리거나 Source 관측 해시만으로 바이너리 컴파일 대응을 주장하지 않았다.

요약: `Saved/BuildChecks/NPCPickupRevisionConflict-20261101/Summary.proof.json`.
최종 실행 `75aa21fa6395451c8571460348b2f506/E2E-0bd23141741148c9830aea67be8a5637/`의
`NPCPickupRevisionConflict.proof.json`과 before/after/closed DB·원래 journal·proxy 기록,
상위 `LoadedModules.json`을 보존했다. 모든 경로는 같은 요약 폴더 아래다.
닫힌 원문 로그 해시와 Join Ticket 마스킹 보존본 해시는 `RetainedFiles.json`에서 구분한다.

네 번의 자기 backend/runtime/DLL/캐시·UserDir와 작은 fixture를 합쳐 계측한 임시
**294,410,150 bytes(280.771MiB)**를 자동 폐기했다. 잔여 임시 프로젝트/정리 예외0이며
정상 Binaries/Intermediate·공유 DDC·기존 복구 백업/DB fixture와 다른 담당 프로세스는
유지했다. 폐기한 사본 해시는 당시 증거이며 동일한 사본의 재실행 가능성을 뜻하지 않는다.
미소비 요청의 권한 있는 replay/수동 해소·월드/처치 identity 영속화·4인/패킷 손실·
실제 Steam/멀티 PC/Shipping은 이 결과의 범위가 아니다.

## 검증

- UnrealHeaderTool 성공
- Projection 구현, PlayerController 생성, GameMode 인증 연결 컴파일 성공
- `UnrealEditor-Project_RPG.dll` 링크 성공
- `ProjectRPG.Item` 자동화 테스트 15개 성공
  - 다른 소유자 응답 전체 거부와 Inventory 전용 필터 검증
  - `2^53`보다 큰 Revision의 정확한 유지 검증
  - 안정 Item ID 기반 추가·무변경·수정·제거 델타 검증
  - 부분 Commit 병합 시 미변경 아이템 유지, 장착·소모 제거, 낮은 Revision 거부 검증

### GUIParts 가방 아트 적용 (2026-11-13)

사용자가 선택한 두 번째 세트 `Content/Assets/GUIParts/Gui_parts`의 어두운 질감 배경·청동색
프레임·붉은 버튼과 세 번째 폴더의 투명 `T_UI_Item_Border` 슬롯 테두리를 실제
`WBP_InventoryProjection`에 적용했다. 가방 크기는
720×640이며 10열/탭당 80칸, 제목줄 드래그·닫기와 기존 바인딩을 유지한다.

- `InventoryArtBackground`와 프레임을 분리해 배경이 컨트롤을 가로채지 않도록 했다.
  프레임 장식과 제목이 겹치지 않도록 제목줄 여백을 조정했다.
- 접힌 `InventorySlotFrameStyle` 이미지와 `InventoryActionButtonStyle` 버튼을
  WBP에 저장한다. 아트는 이 템플릿의 실제 텍스처 참조를 사용하므로 새 파일 경로나
  패키징용 하드코딩 로더가 필요하지 않다.
- 런타임에서 생성하는 가방 셀과 카테고리/정렬·재조회/복구·분할 버튼도 템플릿을
  사용한다. 슬롯 테두리는 별도 HitTestInvisible 이미지이며 기존 상태 색상·아이콘·
  수량·툴팁·드롭 판정과 아이템 명령을 유지한다. 중복 장식 추가를 막는다.
- 템플릿이 없는 장비/우편·기존 native 대체 화면에는 새 스킨을 강제하지 않는다.
  장비/우편/HUD 에셋과 서버·ViewModel·RPC/API/DB는 이 변경의 편집 대상이 아니다.
- `Scripts/Editor/ConfigureInventoryArt.py`는 기본 읽기 전용인 엔진 에셋 작성 스크립트다.
  명시적인 APPLY=True에서만 가방 에셋 하나를 컴파일/저장하고 크기·필수 위젯·기존
  변수 바인딩·허용한 프레임 래핑 외 부모 계층을 확인한다.

검증과 작은 근거는 `Saved/BuildChecks/InventoryArt-20261113/`에 기록한다.
위젯 컴파일/저장과 실제 UI CPP의 고정 입력/UHT·개별 컴파일을 확인했다.
아트 헤더의 별도 native 실행은 기존 게임 DLL에 저장 WBP를 올리는 UI 전용 환경이며,
새 전체 게임 DLL 링크나 최신 전체 소스의 인증된 게임 실행 성공과 구분한다.
무거운 전체 사본 빌드의 중단과 이전 입력 재사용 실패를 후속 성공으로 바꾸지 않는다.
최종 확인: WBP 컴파일/저장·dirty=false와 필수 변수 유지, 전체 프로젝트 UHT 및 실제
가방 CPP 1개의 `-NoPCH` 개별 컴파일 exit 0이다. 새 아트 헤더는 독립 native 6단계
컴파일/링크 exit 0 뒤 기존 게임 DLL과 저장 WBP의 GPU/UI 전용 PIE에서 실행했다.
10열/80칸 각각의 아트·HitTestInvisible, 소비/기타 탭 클릭 후 80칸 재생성, 제목줄
좌클릭 드래그와 X 닫기, 숨긴 스타일 템플릿·pending 명령 0을 확인했다.
`Uninitialized` 상태이며 인증된 빈 가방/실아이템 조작의 증명이 아니다. 작은 근거는
`Completion.json`, `VisualChecks.json`, `Inventory-Bag.png`다. 전체 새 게임 DLL에서의
NativeConstruct/FillInventoryGrid 연결은 별도 컴파일 범위이며 이번 화면에서 직접
실행한 아트 헤더와 구분한다. 원본 DLL을 교체하지 않았다.

검증 후 자기 사본·OBJ/PCH·native DLL/PDB·캐시와 에셋 백업을 finally 정리했다.
계측된 최소 8,250,236,824 bytes(7.684GiB) 폐기·자기 임시 프로젝트/PID·정리 예외 0이다.
초기 짧은 probe 재시도의 미집계 폐기량은 이 수치에 더하지 않았다. 폐기한 모듈/입력의
기록은 당시 증거이며 같은 검증 바이너리를 다시 실행할 수 있다는 뜻이 아니다.
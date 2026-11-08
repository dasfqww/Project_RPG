# 유저 거래·경매장 개발 계약

작성: 2026-11-06 · 담당: trading-foundation-20261106

## 1. 목표와 결정 수준

로비의 1:1 거래와 정가 즉시 구매 경매장을 기존 ASP.NET Core/PostgreSQL에 추가한다.
두 기능은 자산 예약·이전·영수증을 공유하되 제안/동의와 등록/가격/만료는 각각 소유한다.
이 문서는 사용자의 설계 정리·개발 착수 요청에 따른 구현 기준이다. 기존 콘텐츠의 귀속이나
재화 가치는 자동 변경하지 않는다. 실제 출시 허용 목록과 아래 조정 항목은 콘텐츠 결정으로 남긴다.

| 구분 | 개발 기본값/방향 | 출시 전 조정 항목 |
|---|---|---|
| 일반 거래 | 로비, 서로 다른 계정의 캐릭터 2명, 한 캐릭터당 활성 거래 1개 | 초대 TTL·거리/동일 방·차단/초대 제한 |
| 물품 | 서버 정의 ID+버전별 명시 허용, 미등록 정의 거절 | 거래할 재료·소모품·미귀속 제작 장비 목록 |
| 귀속 | Unbound/미장착 BindOnEquip만 일반 거래 후보 | 핵심 장비의 획득 파티 제한 교환 여부 |
| 재화 | 거래당 서버가 선택한 재화 1종, 정수 최소 단위 | 실제 통화 코드·상한·수수료·등록 보증금 |
| 경매장 | 정가 판매, 판매자 오프라인 허용, 본인 계정 구매 금지 | 기간·등록 수·가격/검색 범위 |
| 초기 규모 | 기존 백엔드/동일 DB, 별도 분산 거래 서비스 없음 | 부하 측정 뒤 인덱스/검색 개선 |

실시간 입찰, 통합 서버 경제, 현금 거래, 유저 간 우편 송금은 초기 범위에 포함하지 않는다.
파티 전리품 교환은 일반 거래의 귀속 검사 우회로 만들지 않고 T6의 별도 권한으로 구현한다.

## 2. 기존 근거와 확장 경계

- [아이템 모델](../Backend/ProjectRpg.Backend/Domain/ItemModels.cs): 안정 ID, 소유자, 위치,
  Revision, 귀속/만료/사용자 잠금과 Trade/Auction 위치가 존재한다. 위치 enum만으로 거래가 구현된 것은 아니다.
- [아이템 계약](../Backend/ITEM_API_V2.md): CAS, 위치 유일성, 다중 변경/영수증 원자 저장을 재사용한다.
- [재화 계약](../Backend/ECONOMY_API.md): Account/Roster/Character 지갑 범위와 잔액 상한을 존중한다.
  일부 보상 설명은 과거 우편 우선 흐름이며 현재 배달 정책은 아래 최신 계약을 우선한다.
- [보상 배달](DUNGEON_REWARD_INVENTORY_FIRST.md), [우편](REWARD_MAIL.md): 가방 우선·초과분 우편,
  기존 우편은 아이템 첨부 중심이다. 판매대금을 기존 우편 기능으로 처리할 수 있다고 가정하지 않는다.
- PostgresItemRepository.CommitInTransactionAsync와 PostgresEconomyRepository.CommitBatchInTransactionAsync는
  외부 트랜잭션 참여 경계가 있다. 던전 문맥 권한을 거래에 그대로 재사용하지 말고 T2에서 공통 저장 연산을 분리한다.
- UE 인벤토리 플러그인 이동과 독립적으로 Backend/Domain/Trading에서 시작한다.
  UE 연결은 당시 최신 플러그인 계약/호출자를 다시 확인한 후 예약한다.

## 3. 책임과 호출

| 구성 | 책임 |
|---|---|
| UE ViewModel/화면 | 제안 표시·명령 제출·결과 확인 중 표시·확정 데이터 새로고침 |
| Trade application | 인증된 두 참여자, 로비/제재/세션 검사, 동의와 예약의 수명 |
| Auction application | 등록·검색·구매·취소·만료 및 정산 수령 |
| Trading domain | 정의별 거래 정책, 제안 버전, 준비/확인 상태 전이, 변경 계획 |
| Asset transaction store | 예약·아이템·지갑·수령 대기금·영수증의 단일 원자 저장 |

UI는 소유자 변경 후 Record나 잔액 delta를 제출하지 않는다. API는 본인 제안/확인/구매 같은 의도만 받는다.
인증 계정에서 캐릭터 소유권을 조회하며, 상대방의 확인을 대신 제출할 수 없다.
일반 아이템/재화 commit의 기존 게임 서버 전용 권한은 완화하지 않는다.
같은 프로세스 안의 역할 분리이며 역할마다 별도 서버를 배포하지 않는다.

## 4. 직거래 상태와 자산 예약

`Invited → Negotiating → 양측 Ready → 양측 Confirm → 원자 교환/Completed`

- Session Revision은 모든 변경의 CAS 기준이다. OfferRevision은 어느 쪽이든 제안을 수정할 때 증가한다.
- 초대는 지정된 상대만 수락한다. 본인 계정의 부캐와 거래하지 않으며 제안은 본인 측만 바꾼다.
- 최초 양측 제안은 비어 있다. 양쪽 모두 빈 거래는 Ready할 수 없지만 한쪽만 자산을 주는 증여는 허용한다.
- 제안은 아이템 ID/예상 Revision/수량과 재화 금액이다. T1은 한쪽 최대 8개, 중복 ID/음수 금액을 거절한다.
  이는 제안 상한이며 분할/병합 후 DB mutation 수 제한과 동일하지 않다. T2는 최대 변경 수를 별도 산정한다.
- 제안 수정은 양측 Ready/Confirm를 해제하고 OfferRevision을 증가시킨다. 이전 버전의 확인은 거절한다.
- 준비 철회는 양측 Ready/Confirm와 예약을 해제한다. 내용은 유지하므로 OfferRevision은 같고 Session Revision이 증가한다.
- Ready는 해당 자산의 DB 예약과 함께 저장한다. Confirm는 양측 Ready와 같은 OfferRevision을 요구한다.
- 마지막 Confirm의 도메인 결과 `Agreed`는 양측 동의만 의미한다. 실제 완료 영수증이 아니다.
  T2는 마지막 동의·재검증·자산 이동·Completed 영수증을 한 트랜잭션에 확정하며 Agreed만 따로 성공 저장하지 않는다.
- 직거래 아이템은 원래 가방 칸에 남겨두고 예약한다. 사용자 보호 잠금 IsLocked와 거래 예약은 별개다.
- 아이템은 초기에는 스택 전체에 독점 예약을 걸고, 제안 수량만 최종 분할한다. 재화는 실제 지갑 소유 단위로 금액을 예약한다.
- 사용 가능 잔액은 실제 잔액에서 활성 예약 합계를 뺀 값이다. 본 거래 확정 시 자신의 예약을 인식하여 이중 차감하지 않는다.
- 소비/분해/강화/장착/슬롯 이동/다른 등록/운영 회수와 모든 재화 지출도 같은 예약 규칙을 따른다.
  모든 쓰기 경로에 연결되기 전에는 거래 API를 공개하지 않는다.
- 제안 수정/취소/만료는 상태 변경과 예약 해제를 함께 저장한다. DB 잠금은 명령 처리 동안만 유지한다.
- Ready 유지 중 아이템 자연 만료, 계정 제재, 던전 입장 경합은 최종 확정 때 다시 검사한다.
  거래와 입장 잠금 순서를 기존 계정/캐릭터/리스 경계와 통합한다. UI의 로비 표시만 신뢰하지 않는다.

## 5. 원자 저장·복구 계약

추가 저장 개념: trade_sessions/offers, 자산 예약, auction_listings, seller_payables, trading_receipts.
T2a에서 자산 예약은 v8의 `trade_asset_reservations`와 `trade_reserved_items`로 구현했다.
PostgreSQL 세션 동의/실제 교환 영수증은 v10의 `trade_sessions`/`trade_session_receipts`로 구현했다. 경매장 테이블은 후속이다.

1. 인증/소유 범위를 확인한 후 요청 ID와 전체 명령 지문으로 기존 영수증을 찾는다.
2. 거래/판매글과 관련 계정·캐릭터·리스·아이템 소유자·지갑을 일관된 순서로 잠근다.
   기존 쓰기 경로의 순서와 맞춰야 하며 새 기능만 임의로 정한 순서로 잠그지 않는다.
3. 현재 상태/제안 버전/쌍방 동의/예약/소유/리비전/귀속/만료/잔액/한도를 재검사한다.
4. 교환 후 가방을 계획한다. 나가는 물품이 비우는 칸을 고려한다. 초기 직거래는 수령 공간 부족 시
   전부 거절하고 준비/예약을 해제한다. 거래 수령을 몰래 우편으로 바꾸지 않는다.
5. 양측 아이템·재화와 수수료·예약 해제·완료 상태·영수증을 같은 DB 트랜잭션에 확정한다.
6. DB 커밋 뒤 화면/UE 캐시를 갱신한다. 알림 유실은 지급 유실이 아니며 재조회로 복구한다.

아이템 통째 이전은 ID/옵션을 보존한다. 분할은 새 ID와 출처 연결을 영수증에 저장하고,
병합은 정의 버전·귀속·옵션·만료 등 기존 스택 호환성을 지키며 소멸 ID를 tombstone으로 남긴다.
재화는 checked 정수 연산과 최소/최대 잔액 검사를 사용한다. 수수료를 제외한 총재화와 총수량 보존을 검증한다.

요청 ID는 재전송에도 유지하고 다른 내용 재사용은 충돌로 처리한다. Session Revision과 영수증은 서로 대체하지 않는다.
응답 타임아웃에는 `결과 확인 중`을 표시하고 같은 요청/거래를 조회한다. 임의 환불/다른 ID 재지급을 하지 않는다.
프로세스 재시작 후 DB에서 상태/예약/영수증을 복원한다. 만료 작업도 정상 명령과 같은 잠금/트랜잭션 경계를 사용한다.
재시도 가능한 DB 경합은 같은 명령으로 제한 재시도하고, 이미 커밋됐는지 불확실하면 영수증으로 판별한다.

## 6. 정가 경매장

- 등록: 서버 정의/귀속/가격/기간/수량 검사 후 물품을 Auction 보관 위치로 이동하고 판매글과 함께 저장한다.
- 구매: 판매글 Active/미만료와 본인 계정 구매 금지를 검사하고, 결제·구매자 물품 귀속·판매 대기금·수수료·Sold를 함께 저장한다.
- 구매/취소/만료는 같은 판매글을 잠가 하나만 성립한다. 검색 결과는 참고이며 구매 시 가격/버전을 재검사한다.
- 구매품/취소품은 가방 우선·초과분 우편으로 전달한다. 우편 누적·기존 500개 조회 한도를 처리하기 전 무제한 등록하지 않는다.
- 판매대금은 별도 수령 대기금이다. 수령 가능한 잔액 범위에서 대기금 차감+입금을 원자 처리한다.
  오프라인/잔액 상한 때문에 확정된 판매나 환불을 잃지 않는다.
- 초기에는 부분 구매/가격 수정 없이 등록 단위 전체 구매, 가격 수정은 취소 후 재등록한다.
- 검색은 정렬 키+ID 기반 페이지네이션과 제한된 필터를 제공한다. 거래 이력/상대 계정의 비공개 정보는 공개하지 않는다.
- 경매장 수수료는 편의 기능 이용료로 시작하는 제안이다. 직거래 무료 여부와 경제 전체 재화 회수 정책은 출시 결정 항목이다.

## 7. 파티 전리품 제한 교환

T6에서 보상 생성 시 확정한 eligible character/account 목록, 원래 획득자, 던전/정산 ID,
교환 기한과 장착/강화 후 자격 소멸 규칙을 저장한다. 현재 파티만 검사하거나 문자열 태그로 귀속을 우회하지 않는다.
일반 거래/경매 정책은 기존 CharacterBound/AccountBound를 계속 거절한다.
핵심 장비 정책이 확정되고 출처 증거가 영속화되기 전에는 이 예외를 켜지 않는다.

## 8. 단계별 구현과 종료 기준

| 단계 | 구현 범위 | 완료 기준 |
|---|---|---|
| T0 | 본 문서/조율 기록 | 현재 호출·계약 확인, 정책 기본값과 미결 분리 |
| T1 | 서버 정의별 허용 정책, 불변 제안, 양측 동의 상태 전이 | 실제 Backend 빌드와 정책/위조 참여자/오래된 버전/변경 시 동의 해제/만료 회귀 |
| T2a | Memory/PG 자산 예약·해제·만료와 기존 변이/지출 차단 | 실제 PG 병렬 예약/지출/아이템 변경·취소 봉인·중간 실패 롤백·저장소 재생성 |
| T2b | 교환 계획(11절)·PostgreSQL 원자 저장(12절)·Memory 원자 저장/내부 DI(13절) 완료 | 실제 PG 양측 교환/취소·입장 경합, 주입 실패 전체 롤백, 재시작/응답 유실 복구; Memory 동등 계약 |
| T3 | 인증된 직거래 API/조회/만료 처리 구현(14절), 기본 비활성 | 실제 HTTP 두 계정·세 번째 계정 거절·요청 충돌·응답 유실·재시작, 제한/영수증 감사 |
| T4 | 상세 API·UE 수명/조회/상세·확인한 내용에 대한 준비/확정 구현(15~19절), 상대 선택/제안 편집·Projection 후속 | 실제 두 클라이언트 제안·준비·확정·취소·재접속, 기존 인벤토리 명령과 충돌 검사 |
| T5 | 경매장 등록/검색/구매/취소/만료/대금 수령/API/UI | 동시 구매 1명 성공·취소 경합·오프라인 판매·포화/잔액 상한·재시작 실연결 |
| T6 | 파티 전리품 자격/제한 교환 | 획득 당시 참여자만 허용·시간/장착/강화/재이전 규칙·정산 출처 보존 |

각 단계는 구현→범위에 맞는 검증→자기 임시 정리→기능별 커밋/푸시→원격 해시 확인 순서로 끝낸다.
T1은 보안/DB 거래 완성이 아니다. T2/T3 전에는 기능 노출·잔액 변경·새 귀속 정책 적용을 하지 않는다.
Memory 테스트를 PG 동시성 증거로, 도메인 테스트를 실제 HTTP/UE 증거로 보고하지 않는다.

## 9. 현재 진행과 재개 지점

- T0/T1과 T2a 구현·검증 완료. T1에서 서버 허용 목록·아이템 소유/상태/귀속/수량 검사, 불변 제안,
  초대 수락·준비/철회·양측 확인·제안 변경 시 동의 해제·취소/만료를 구현했다.
  T2a는 10절, 교환 계획은 11절, PostgreSQL 원자 저장은 12절, Memory 동등 저장과 내부 DI는 13절,
  인증 application/API·초대 제한·만료 작업은 14절에 기록한다. 실제 콘텐츠 허용과 UE 화면은 후속이다.
- T1 당시 게시 Backend 96081fe7+본인 거래 코드의 Release 빌드 경고0/오류0,
  도메인 6그룹/105 assertion 통과. 첫 실행의 잘못된 귀속 enum 기대 오류 코드를 기존
  ItemRecordRules의 invalid_trade_item으로 보정했으며 제품 검증을 완화하지 않았다.
  러너의 Windows PowerShell stderr 처리는 종료 코드/전체 실패 로그를 보존하도록 수정했다.
- 자기 임시 2회 합계 32,373,680 bytes 폐기, 잔여 Temp/예외0. 도메인 검증만 수행했으며
  실제 PostgreSQL·HTTP·UE·운영 배포 성공을 의미하지 않는다. 폐기 모듈 해시는 당시 증거다.
  로컬 근거: Saved/BuildChecks/TradingFoundation-20261106/e45fa8a8542f412aae4635bd6ad50d59/Result.json.
- T1 경로: Backend/ProjectRpg.Backend/Domain/Trading, Backend/ProjectRpg.Backend.TradingTests.
- 검증: `powershell -NoProfile -File Scripts/Tests/Run-TradingTests.ps1`.
  게시된 Backend 사본에 자기 거래 코드만 덧씌워 빌드하며 Temp는 finally 폐기한다.
- T4의 참여자 전용 상세 물품 미리보기 API는 15절까지 구현했다. 다음은 UE 표시/거래 화면 연결이다. T3 API는 기본 비활성이며 실제 콘텐츠 허용 목록은 비어 있다.
  출시 허용 정책·초대 상대 선택/차단·거리 또는 같은 방 조건과 최신 조율 표의 UE 담당 범위를 재확인한다.

## 10. T2a 자산 예약 저장·소비 차단 (2026-11-06)

### 구현과 불변식

- 내부 `ITradeReservationRepository`의 Reserve/Release/Find와 Memory/PG 구현을 추가했다.
  `ReservationId`는 한 참여자의 한 제안 세대를 나타내며 같은 ID의 내용 변경은 충돌이다.
  ID/TradeId/OfferRevision/참여자/통화/정렬된 아이템 ID·revision·수량/금액/UTC 만료를 지문에 포함한다.
- Reserve는 서버 정의 정책·현재 아이템/지갑을 검사한 뒤 아이템 전체 스택과 해당 금액을 함께 예약한다.
  한 캐릭터의 활성 예약은 하나이며 Account/Roster/Character 실제 지갑 범위별로 금액을 합산한다.
  예약 자체는 소유권·아이템 리비전·실제 잔액·사용자 잠금을 바꾸지 않는다.
- Release는 예약과 무관하게 새로운 아이템/금액 지급을 하지 않는다. 먼저 도착한 취소도 Released 기록으로
  봉인하므로 늦은 Reserve나 동일 ID 재시도는 예약을 되살리지 못한다. 해제/만료 뒤 새 제안은 새 ID를 사용한다.
- Memory는 기존 item/economy와 같은 InMemoryTransactionGate를 강제한다. PostgreSQL은
  캐릭터 KEY SHARE → 예약 ID advisory → 아이템 owner advisory → 재화 정의 SHARE/지갑 UPDATE → 아이템 행 순서다.
  기존 보상 정산이 owner를 지갑보다 먼저 잠그므로 이 순서를 따른다. 캐릭터 KEY SHARE는 이후 FK 검사와
  장비의 character→owner 잠금 순서가 역전되지 않게 먼저 취득한다.
- PG의 활성 판정은 잠금 이후 `clock_timestamp()`, Memory는 공유 gate의 서버 TimeProvider를 사용한다.
  외부 명령의 committedAt을 미래로 바꿔도 예약을 우회하지 못한다. 만료 작업이 지연되어도 만료된 예약은 소비를 막지 않는다.
- 기존 확정 아이템/재화 영수증 재생은 예약보다 먼저 처리한다. 이미 끝난 명령을 재실행하거나
  새 예약 때문에 과거 성공을 실패로 바꾸지 않는다.
- 새 예약 생성/해제에는 DB 트랜잭션을 사용한다. 임의 Operation 이름이나 ReservationId 문자열을
  일반 Commit에 넣어도 예약 우회 권한은 생기지 않는다.

### 현재 연결한 쓰기 경로

| 경로 | 공통 검사/현재 동작 |
|---|---|
| 일반 아이템 Commit, 우편 수령/소비/이동/장착 | Memory CommitCore와 PG 일반 Commit에서 예약된 ID의 변이 전체 거절(기존 ValidationFailed) |
| 제작/분해/강화/이전, 픽업, 운영 보상 | 기존 Memory 공통 Commit 및 PG CommitInTransactionAsync의 같은 검사 사용 |
| 캐릭터 시작 장비 | 서버가 새 ID만 생성하는 기존 직접 삽입; 기존 예약 아이템 변경 경로 없음 |
| 단일/배치 재화 변경과 정산 내부 배치 | 현재 실제 잔액-활성 예약까지만 지출 가능(기존 InsufficientBalance), 배치 실패 시 전체 롤백 |
| 던전 보상 스택 합치기 | 예약 스택은 병합 후보에서 제외하지만 원래 칸은 점유 상태 유지; 가방 포화 시 기존 우편 정책 유지 |

v1~v7 SQL은 수정하지 않고 `0008_trade_reservations.sql`을 카탈로그와 배포 출력에 추가했다.
운영 DB 적용/서비스 재배포는 수행하지 않았다. 이 코드의 운영 기동에는 정식 마이그레이션 경로로 v8이 필요하다.

### 확인한 범위

- 최종 dbe7e392+본인 사본 Release 빌드 경고0/오류0, T1 105 / 예약 Memory 69 / 실제 PostgreSQL 81 assertion 통과.
- PG v7→v8만 적용·반복 적용0·기존 캐릭터 보존, 동일 요청 경합·아이템/지출 경합·취소 봉인,
  계정/원정대 부캐 지갑, 미래 committedAt 거절, 중간 삽입 실패 전체 롤백, 새 연결 풀의 복원,
  실제 지갑 행 잠금 대기 중 만료, 가방 포화 우편/확정 보상 재생을 확인했다.
- 기존 제작/분해 Memory·PG와 보상 Memory/실제 bearer HTTP·PG 28그룹 통과.
  이후 변경은 Memory 외부 소유 아이템의 오류를 PG와 같은 NotFound로 통일한 부분과 신규 검사뿐이며,
  이 변경과 포화 경계는 최종 예약 집중 검사에서 재검증했다. 기존 공통 저장소/계획/스키마 SHA는 동일하다.
- 첫 확대 실행은 Docker 초기화용 임시 소켓 서버의 기동 완료 오인으로 중단됐다.
  러너를 TCP 기동 확인으로 수정해 성공했고 실패분을 포함한 자기 임시4회 합계66,941,577 bytes/전용 PG를 폐기했다.
  잔여 Temp·자기 DB·정리 예외0. 작은 명령/해시/로그만 유지하며 폐기 사본의 재실행 가능성을 주장하지 않는다.
- 최종 proof: Saved/BuildChecks/TradeReservations-20261106/4ccd0490566f49cbbe2fe12e7e53495d/Result.json.
  기존 기능 회귀 proof: 같은 루트/571cad88886a42809c992c2d2cb3c171/Result.json.
- 실행: `powershell -NoProfile -File Scripts/Tests/Run-TradeReservationTests.ps1`.
  `-SkipRegression`은 거래 집중 검사만 실행하며 기존 제작/보상 재검증으로 보고하지 않는다.

### T2b 인계 조건

T2a 당시에는 단독 예약 저장만 구현했다. PostgreSQL의 T1 Ready와 동일 트랜잭션 저장, 양측 자산 이전,
완료 영수증과 제재/던전 입장 재검사는 이후 12절에서 연결했다. 현재 public HTTP/UE 거래 노출은 없고
DI의 실제 물품 허용 목록도 비어 있다. T2b/T3는 인증/로비/제재/서버 TTL과 실제 콘텐츠 허용 목록을 연결해야 한다.

T2b에서는 단독 ReserveAsync를 두 번 호출해 원자 교환으로 간주하지 않는다. 동일 connection/transaction에 참여하는
내부 예약 연산과 상태 CAS를 추가하고, Memory도 예약 사전까지 포함한 snapshot/rollback을 사용한다.
완료 영수증과 해제는 함께 저장하여 중간 Release 때문에 예약 자산을 먼저 소비하는 틈이 없게 해야 한다.
기존 원정대/계정 지갑과 owner 잠금 순서를 유지하며 참여자 2명의 키를 정렬하고, lease 경합은 기존 NOWAIT 계약을 검토한다.

## 11. T2b 양측 교환 계산기 (2026-11-06)

당시 운영툴 서비스 제어 담당이 공통 입장/계정 잠금과 마이그레이션 카탈로그·Backend csproj를 수정 중이었으므로,
저장소 통합에 필요한 순수 `TradeExchangePlanner`를 독립적으로 구현했다. T2b 전체 완료는 아니다.

### 입력과 계산 결과

- 서버가 같은 트랜잭션 안에서 로드한 양측 전체 가방(각 80칸), 실제 범위의 지갑과 다른 거래의 예약 합계/아이템 ID,
  마지막 Confirm으로 만든 Agreed, 양측의 실제 활성 예약을 받는다. 플레이어가 제출할 DTO가 아니다.
- 동의/만료·예약 지문/참여자/제안 세대/내용/통화/실제 지갑을 대조하고, 제안 아이템의 소유·리비전·귀속·잠금·만료를 재검사한다.
  재화는 Account/Roster/Character의 실제 소유자를 사용하며, 차액이 0이어도 양측 제시 금액 전체가 다른 예약을 제외하고 충당되어야 한다.
- 양쪽에서 나가는 물품을 먼저 제거한 뒤 도착분을 계산한다. 전체 가방 맞교환은 나가는 칸을 재사용하며,
  공간이 부족하면 계획 전체를 거절한다. 우편으로 우회하거나 입력 상태를 수정하지 않는다.
- 통째 이전은 기존 ID/seed/옵션을 유지한다. 부분 이전은 원본 수량을 줄이고 trade ID·offer revision·원본 ID로 결정한
  새 ID와 분할 출처/수량을 결과에 남긴다. 같은 입력 재시도는 같은 결과를 만든다.
- 병합은 정의/버전·귀속·내구도·만료·seed·태그·옵션이 같고 성장 상태가 없는 스택만 허용한다.
  보호 잠금·다른 예약·자기 측 부분 출고 잔여 스택·증가할 수 없는 리비전은 병합 대상에서 제외한다.
  먼저 도착한 호환 스택에도 병합할 수 있으며 완전히 합쳐진 원본/분할 ID는 Consumed tombstone을 생성한다.
- 아이템 변경은 expected revision을 그대로 보존한 읽기 전용 스냅샷, 재화 변경은 실제 소유자별 이전/이후 잔액과 expected revision이다.
  정수 overflow·잔액 상한을 거절한다. 가방 160개와 신규 분할 최대 16개를 포함한 내부 계획 상한은 176개다.
  공용 아이템 API의 16개 제한을 완화하거나 DungeonReward 명령으로 위장하지 않았다.

### 검증과 저장소 인계

- 게시9a991435+본인 명시 입력 사본 Release 빌드 경고0/오류0. 교환 6그룹 8,985 assertion,
  T1 105·기존 Memory 예약69 통과. 고정 난수 80회 양측 최대8개 제안의 수량/CAS/슬롯 유일성/입력 순서 독립성을 포함한다.
  실제 Memory 예약 저장소가 만든 예약 지문·소유자 호환과 계산 시 잔액/예약 불변도 확인했다.
- 첫 실행은 검사 fixture의 캐릭터 이름 중복으로 마지막 Memory 연동 그룹에서 실패해 이름을 구분했다.
  마지막 한 칸의 다중 도착 스택 병합 경계도 보완하여 재검증했다. 실패 포함 자기 임시 2회 33,699,386 bytes 폐기,
  Temp/정리 예외0. 작은 로그/해시만 보존한다.
- 실행: `powershell -NoProfile -File Scripts/Tests/Run-TradeExchangePlanTests.ps1`.
  근거: Saved/BuildChecks/TradeExchangePlan-20261106/3c1b802fcf9b45eb90170735fbe2a4cd/Result.json.
- 계획은 자산 이전/인증 성공/완료 영수증이 아니다. 이 단계에 DB migration, 저장소 write 연결, HTTP/UE 노출은 없다.
  다음 저장소는 운영툴의 계정/서비스/입장 잠금 인계를 확인하고, 전체 입력을 같은 잠금/트랜잭션에서 읽어야 한다.
  분할 ID는 두 가방 밖의 기록까지 전역 충돌 검사하고, 최대176개 CAS·교환 후 슬롯 유일성을 한 번에 적용해야 한다.
  가방이 꽉 찬 맞교환은 중간 쓰기 순서의 슬롯 UNIQUE 충돌도 처리해야 한다.
  최종 동의·계획 적용·예약 해제·완료 영수증을 함께 저장하고, 용량/정책 거절은 준비 철회/예약 해제와 함께 저장한다.
  다른 예약 합계에서 이 거래를 정확히 제외해야 하며 내부 전용 저장 경계 외에는 예약 우회를 허용하지 않는다.
  이 인계의 PostgreSQL 통합과 동시 확정/취소/입장·롤백·재시작 검증은 아래 12절에서 수행했다.

## 12. PostgreSQL 동의·예약·자산 교환 원자 저장 (2026-11-06)

운영툴 서비스 제어 9c8e4022 인계 후 `ITradeSessionRepository`와 `PostgresTradeSessionRepository`를 구현했다.
이 단계에서는 내부 포트를 DI/HTTP/UE에 공개하지 않았다. 이후 13절에서 Memory 구현과 내부 DI를 연결했다.
Actor는 이후 application이 인증 결과에서 만들어야 하며, Expire는 서버 만료 작업 전용이다.

### 저장과 수명

- 새 v10 `trade_sessions`/`trade_session_receipts`를 배포 카탈로그에 추가했다. 기존 v1~v9 SQL은 변경하지 않았다.
  운영 DB migration/서비스 재배포는 수행하지 않았으며 새 백엔드 배포에는 정식 v10 적용이 필요하다.
- 초대 생성 시 서버 DB 시간 기준 5분 TTL과 캐릭터당 활성 거래 1개를 적용한다. 요청 ID와 전체 명령 지문으로
  정확한 결과를 재생하고, 같은 요청 ID의 다른 내용은 충돌로 거절한다. 세 번째 참여자 조회/명령은 거절한다.
- 초대 수락·제안 변경·Ready·철회·Confirm·취소·만료를 저장한다. Ready와 자산 예약은 같은 트랜잭션이고,
  수정/철회/취소/만료는 양측 동의 상태와 예약을 함께 갱신한다. 상태 CAS가 오래된 명령은 새 동의를 철회하지 않는다.
- 마지막 Confirm은 교환 계산 결과를 재검사하고 아이템/재화·예약 해제·Completed·명령 영수증을 함께 저장한다.
  Agreed만 따로 커밋하지 않는다. 결과의 Exchange는 expected revision을 담은 감사용 계획이며 실제 아이템/지갑은
  그 리비전에서 1 증가한다. 호출자는 완료 후 권위 인벤토리/지갑을 다시 조회해야 한다.
- 양측 전체 가방을 잠그고 신규 분할 ID를 전역 검사한다. 바뀌는 기존 아이템을 트랜잭션 내부 Terminal 상태로
  잠시 옮겨 슬롯 UNIQUE를 유지한 채 꽉 찬 가방 맞교환을 저장한다. 모든 CAS 확인 후 최종 위치/수량을 기록한다.
- Ready/Confirm의 용량·잔액·예약·정책·접근 거절은 savepoint로 해당 시도를 되돌린 뒤 준비 철회/양측 예약 해제와
  거절 영수증을 함께 저장한다. SQL/접속/취소 예외는 전체 트랜잭션을 롤백하며 동일 요청으로 재시도한다.

### 공통 잠금과 접근 경계

잠금 순서는 서비스 공유 gate → 정렬한 양측 account gate → 요청 ID → 거래 ID → 정렬한 character KEY SHARE →
예약 ID → 기존 정렬 규칙의 양측 item owner → 재화 정의 SHARE/정렬 지갑 → 아이템 행이다.
서비스 Open/계정 제재/활성 character lease를 DB 서버 시간으로 재검사한다. 기존 입장 경로가 같은 계정 gate를
사용하므로 입장 트랜잭션 뒤에 대기한 확정은 커밋된 리스를 보고 거절한다. 잠금 대기 후 거래 만료도 다시 확인한다.
이미 확정된 영수증 재생과 취소/철회는 Drain/제재 중에도 가능하다. 이것이 HTTP 인증 생략을 허용하지는 않는다.
만료는 조회만으로 자동 저장하지 않으며 Expire 명령 또는 다음 변경 명령에서 원자적으로 닫는다.

### 검증과 후속

- 게시 d546e392+본인 명시 overlay 사본 Release 빌드 경고0/오류0. 실제 PG 세션 5그룹318 assertion,
  별도 dotnet 프로세스 재시작/동일 영수증 재생4, 기존 T1 105·계획8985·예약 Memory69/PG81 통과.
  v9→v10/반복 적용0/기존 캐릭터 보존, 가방 맞교환, 분할/병합, Ready 저장 실패 및 마지막 영수증 실패의 전체 롤백,
  요청 재전송/ID 충돌·외부 조회 거절·제안 수정/준비 철회/취소/만료·공간 부족·제재/Drain을 확인했다.
- 동시 Ready 1예약, 최종 Confirm/Cancel 1승자, 계정 잠금 대기 후 리스 관찰, 실제 GameRepository 입장 후 교환 거절,
  계정 잠금 대기 중 만료를 확인했다. 계정 잠금 경합 검사는 사설 TX로 동일 gate/리스 SQL을 사용했고,
  실제 입장 저장소 검사는 별도로 실행했다. HTTP/UE/다중 PC 거래 검증과 구분한다.
- 초기 DB 복원에서 ReadOnlyCollection 역직렬화 실패를 고쳐 IReadOnlyList 계약/읽기 전용 생성 결과를 사용한다.
  첫 맞교환 fixture는 호환 스택 병합으로 ID가 소멸했으므로 양측 seed를 구분해 슬롯 교환 자체를 검증했다.
  검사를 약화하지 않고 병합은 별도 실제 DB 사례로 유지했다.
- 마이그레이션 패키지의 v9 고정 기대를 v10으로 갱신하고 마이그레이션 순수/실제 배포 명령 검사7개를 별도 실행했다.
  이 후속에는 제품 소스 변경이 없다. 검증 SQL·DB와 실패 포함 자기 임시4회 79,267,119 bytes를 폐기했고 잔여/예외0이다.
- 실행: `powershell -NoProfile -File Scripts/Tests/Run-TradeSessionTests.ps1`.
  `-MigrationOnly`는 패키지/배포 명령 검사만 실행한다. 전체 근거는 Saved/BuildChecks/TradeSession-20261106/2cb5fefd78544f8b8ffc4f252f5b1195/Result.json,
  마이그레이션 후속은 같은 루트/ca99daba959341e6afa18126feb6c378/Result.json. 작은 로그/해시만 보존한다.
- 당시 후속이었던 Memory의 동일 세션/예약/자산 rollback과 서비스/계정/리스 직렬화·내부 DI는 13절에서 구현했다.
  인증 application·서버 초대 제한·만료 worker/API, 실제 거래 허용 콘텐츠와 UE 화면은 남아 있다. API나 기본 허용 목록은 열지 않았다.

## 13. Memory 원자 거래와 공통 저장소 계약 (2026-11-06)

`InMemoryTradeSessionRepository`를 구현하고 Memory/PostgreSQL 모두 `ITradeSessionRepository`로 내부 DI에 등록했다.
명령 검증·참여자 판정·요청 지문·준비 예약 ID는 `TradeSessionRules`를 공유한다. HTTP/UE 호출은 아직 없다.

### 저장·잠금·롤백

- Memory는 실제 게임 저장소의 dungeon gate → 기존 item/economy 공통 transaction gate 순서로 실행한다.
  생성 시 게임/아이템/재화 저장소의 gate 일치를 강제하고, 기존 입장·서비스 Drain·계정 제재와 같은 경계에서 접근을 재검사한다.
  이 경계 안에서는 동기적으로 끝나는 구체 Memory 저장소만 호출하며 외부 I/O나 await를 넣지 않는다.
- 초대/수락/제안/준비/확정/철회/취소/만료에 PG와 같은 5분 TTL·캐릭터당 활성 거래 1개·상태 CAS·요청 재생을 적용한다.
  세션/영수증을 직렬화해 보관하므로 반환 결과 수정이 저장 상태를 바꾸지 않는다. 같은 gate로 저장소를 재생성해도 재생 가능하지만,
  Memory 데이터는 프로세스 종료 시 사라진다. 프로세스 재시작 복구는 PostgreSQL의 별도 검증이다.
- 마지막 확정은 전체 가방과 최신 지갑/다른 예약을 읽고 교환 계산 후 아이템·재화·예약 해제·완료·영수증을 한 번에 반영한다.
  내부 전용 적용은 전역 아이템 ID/CAS·최종 슬롯 유일성·재화 상한을 다시 확인한다. 꽉 찬 양측 가방의 맞교환과 분할/병합을 지원한다.
- 아이템/재화와 각각의 기존 영수증·거래 예약·세션·명령 영수증을 함께 snapshot/복원한다. Ready/Confirm의 정책/접근 거절은
  해당 시도만 되돌리고 양측 준비/예약 해제와 거절 영수증을 저장한다. 취소 예외나 예기치 않은 실패는 전체 명령을 롤백해 같은 요청으로 재시도한다.
- Memory 지갑에 저장됐던 재화 정의가 오래되어 변경된 잔액 상한을 놓치는 문제도 수정했다. 지갑 조회 시 현재 정의를 사용하고,
  교환 적용 시 다시 검사한다. 일반 아이템/재화 명령의 예약 차단은 유지한다.

### 검증·정리·다음 연결

- 게시 e93a4e1d와 본인 명시 overlay의 분리 Release 빌드 두 개 모두 경고0/오류0. Memory 세션 6그룹320 assertion,
  실제 PostgreSQL 세션 5그룹320·별도 프로세스 영수증 재생4, 양쪽에 동일한 공통 세션 계약을 실행했다.
  요청 재생/CAS/소유권/반환 결과 격리, 동시 Ready·확정/취소, 실제 게임 저장소 입장 경합, Drain/제재/만료,
  꽉 찬 가방 맞교환·분할/병합·상한 변경·중간 실패/취소 전체 롤백을 확인했다.
- 기존 T1 105·교환 계획8985·예약 Memory69/PG81, 제작 Memory9그룹·보상 Memory7그룹도 통과했다.
  제작/보상 검사는 실제 bearer HTTP를 포함하며 거래 HTTP 검증은 아니다. 검증 후 제품/검사 소스11개의 해시가 동일하다.
- 실행: `powershell -NoProfile -File Scripts/Tests/Run-TradeMemorySessionTests.ps1` (dotnet·Docker·Node 필요).
  근거는 Saved/BuildChecks/TradeMemorySession-20261106/4ac60af9cfbd4654a789b7b0745aad00/의 Result.json·CleanupRecovery.json·CompletionAudit.json이다.
  최초 러너 결과는 모든 동작 검사를 통과한 뒤 Windows 긴 경로 정리에서 실패했다. 전용 정리 함수를 보완해 남은 실제 경로를 삭제했고,
  초기 실패 기록은 보존했다. 복구에서 확인한 삭제량은 7,067,029 bytes이며 그 전 부분 삭제량은 미집계다.
  자기 DB/프로세스/Temp 잔여와 정리 예외는0, 작은 로그/해시만 남긴다. 폐기한 바이너리는 재실행 가능한 증거로 취급하지 않는다.
- 당시 후속이던 인증 application/API·초대 제한·서버 만료 작업은 14절에서 구현했다.
  운영 거래 허용 목록은 계속 비어 있으며 실제 콘텐츠 허용·UE 화면·다중 PC 거래 검증은 후속이다.

## 14. 인증된 직거래 API·초대 제한·서버 만료 (2026-11-07)

실제 Player bearer와 캐릭터 소유권으로 Actor를 만들고 상대 캐릭터의 계정은 저장소에서 조회한다.
관리자/게임 서버 토큰으로 플레이어 거래를 대신 실행할 수 없다. 계정 ID·재화 종류·만료 시간·예약 ID·교환 후 Record는 요청으로 받지 않는다.
기존 일반 아이템 commit의 게임 서버 전용 권한은 유지한다.

### 활성화와 조회

- `Trading:Enabled` 기본값은 false, `Trading:CurrencyCode` 기본값은 `Gold`다. 잘못된 재화 코드 설정은 기동 시 거절한다.
  이 변경은 설정/운영 DB/실행 중 백엔드를 활성화하거나 재배포하지 않았다. 기존 아이템 허용 목록도 비어 있다.
- 비활성 상태에서도 인증된 본인 조회·취소·준비 철회와 서버 만료 작업은 가능하다. 나머지 명령은 404 `trading_disabled`다.
  금전만 주는 거래도 활성화 설정의 영향을 받는다. 활성화 여부는 기동 시 읽으며 변경 시 프로세스 재기동이 필요하다.
- 기본 경로는 `/api/characters/{characterId}/trades`다. GET은 미완료·미만료인 본인 진행 거래를 `{ session }`으로 반환하며 없으면 null이다.
  초대받은 캐릭터도 이 경로로 초대를 발견한다. GET `/{tradeId}`는 완료/취소/만료된 거래도 참여자에게 반환한다.
  제삼자의 거래와 없는 거래는 동일한 404다. 소유하지 않은 캐릭터 경로는 403이며 응답은 `no-store`다.
- 응답은 양측 캐릭터 ID·제안·준비/확인·버전·만료·완료 상태다. 계정 ID·예약 ID·지갑 소유자·내부 교환 계획은 노출하지 않는다.
  완료 시 상태 문자열은 `Completed`다. 기존 명령 응답의 제안 물품은 ID/수량/예상 버전이며 상세 미리보기는 15절의 별도 GET으로 조회한다.
  완료 후 기존 권위 인벤토리/지갑 GET으로 최신 자산을 읽는다.

### 명령과 오류

모든 변경은 POST `/{tradeId}/{action}`이며 클라이언트는 불확정 응답의 `requestId`·`tradeId`·본문을 유지해 재전송한다.
요청/응답의 리비전·재화 금액은 64비트 정밀도를 보존하는 10진 문자열이다. 선행 0·부호·공백·숫자 JSON 타입은 거절한다.

| action | requestId 외 필드 |
|---|---|
| invite | inviteeCharacterId |
| accept / withdraw / cancel | expectedRevision |
| offer | expectedRevision, offer: { items: [{ itemId, expectedRevision, quantity }], currencyAmount } |
| ready / confirm | expectedRevision, expectedOfferRevision |

예를 들어 offer의 재화 20은 `"currencyAmount":"20"`, 리비전 2는 `"expectedRevision":"2"`로 전송한다.
items는 최대 8개이며 quantity는 양의 JSON 정수다. 서버 전용 Expire HTTP 경로는 없다.

- 요청은 JSON 콘텐츠 타입/최대 16KiB/깊이 12를 적용한다. chunked 요청도 읽기 상한을 지킨다.
  알 수 없는 필드·중복 JSON 키·누락 필수값·동작과 맞지 않는 필드는 400, 콘텐츠 타입은 415, 크기 초과는 413이다.
- 성공 및 기존 영수증 재생은 200 `{ requestId, session, rejection, committedAt }`다.
  Ready/Confirm의 저장된 정책/접근 거절은 **409이면서 같은 결과 형식**을 반환한다. 이때 session에는 준비/예약 해제가 반영돼 있으므로
  HTTP 오류라는 이유로 상태를 버리지 않는다. CAS/요청 ID 충돌 등 저장되지 않은 거절은 409 `{ error }`다.
- 서비스 Drain에서 새 초대/수락은 503, 계정 제재에 따른 인증 세션 회수는 기존 401 규칙을 따른다.
  유효한 상대방의 Ready/Confirm에서 제재를 발견하면 409 거절 영수증과 양측 준비 해제를 저장한다.
  취소·조회·기존 영수증 재생은 저장소의 기존 계약을 따른다. 다만 API 비활성/요청 제한은 재전송에도 먼저 적용된다.
- 초대는 보내는 계정당 5회/분, 받는 계정당 10회/분, 그 외 변경은 계정당 120회/분이다.
  부캐·재로그인 토큰도 같은 계정 예산이며 실패/재전송 시도도 소모한다. 429와 `Retry-After: 60` 뒤 같은 요청으로 재시도한다.
  이 제한은 기존 ingress/player 제한에 추가한 **백엔드 프로세스 단위** 보호다. 재시작 시 초기화되며 다중 인스턴스 공용 제한은 아니다.
  초대 차단 목록/거리·같은 방 검사는 아직 없다.

### 만료·검증·후속

- `TradeExpiryWorker`는 API 비활성 여부와 무관하게 기동 시/5초마다 만료 후보를 최대 100개 조회한다.
  Memory는 공유 gate의 서버 시간, PostgreSQL은 DB 시간을 사용한다. 조회는 자산을 바꾸지 않으며 실제 만료는 기존 ExecuteAsync의
  잠금/CAS/영수증/예약 해제 트랜잭션으로 실행한다. 거래 ID와 관측 리비전에서 만든 동일 요청 ID로 여러 worker/재시작의 중복을 처리한다.
  중간 CAS 경합은 다음 조회로, 저장 실패는 다음 sweep으로 재시도한다. 캐릭터 조회는 만료 대기 중인 거래를 진행 거래로 표시하지 않는다.
- 분리 Release 빌드 경고0/오류0. 실제 bearer HTTP에서 Memory209·PostgreSQL213 assertion으로 두 계정 교환/분할·정확한 금액·
  제삼자/관리자/실제 서버 자격 거절·큰/중복/위조 요청·초대/부캐/수신 제한·입장/Drain/제재·취소·동시 만료 worker를 확인했다.
  기존 세션 Memory320/PG320·저장소 별도 프로세스4도 통과했다. 신규 실제 Program 별도 프로세스에서 HTTP 완료 영수증의 정확한 재생과
  등록된 hosted worker의 자동 만료를 5 assertion으로 확인했다. 실제 Steam/UE/다중 PC/운영 배포 검증은 아니다.
- 첫 빌드는 검사 코드의 ItemRecord 수량 접근을 State.Quantity로 수정했다. 후속 자식 프로세스 검사의 DB 연결 실패와
  appsettings의 고정 3000 포트 충돌을 확인하고, 사설 DB 원본 연결값/설정 우선순위와 명시 동적 Kestrel 포트를 보완했다.
  기존 서버/에디터를 종료하지 않았으며 실패 기록도 남겼다. 제품 10개 입력은 검증 시도 사이 동일하다.
- 실행: `powershell -NoProfile -File Scripts/Tests/Run-TradeApiTests.ps1`. `-ApiOnly`는 API 검사만,
  `-RestartOnly`는 새 사설 PG의 HTTP 데이터 생성과 실제 Program 재시작 검사를 실행한다.
  최종 PG/Program 근거는 Saved/BuildChecks/TradeApi-20261106/36c58f2d6f814351b90ff9b959868347/Result.json,
  Memory API는 17d3999f67b447298fe379f57fa26e36, 기존 저장소 회귀는 0200fb5c288e47fa99ce3d9368a00e8c의 해당 PASS 로그다.
  앞선 두 전체 결과는 마지막 자식 프로세스 검사 실패를 포함하므로 전체 PASS로 표시하지 않는다.
- 실패 포함 자기 임시4회 77,426,695 bytes와 전용 DB/프로세스를 정리했다. 잔여/정리 예외0, 작은 로그/해시만 로컬에 보존한다.
  종합 근거는 같은 루트의 CompletionAudit.json이며 폐기한 바이너리 해시는 당시 증거다.
- 상세 제안 물품 조회는 15절에서 구현했다. 다음은 실제 거래 허용 정책과 UE 거래 화면/재접속·불확정 요청 복구다.
  경매장/파티 전리품 교환은 각각 T5/T6로 남아 있다.

## 15. 참여자 전용 상세 물품 미리보기 (2026-11-08)

`GET /api/characters/{characterId}/trades/{tradeId}/preview`를 추가했다. 기존 Player bearer/본인 캐릭터 검사를 사용하고
거래 참여자에게만 응답한다. 제삼자의 거래와 없는 거래는 동일한 404, 다른 사람의 캐릭터 경로/관리자 역할은 403이다.
기존 조회처럼 API 신규 거래가 비활성이어도 조회 가능하며 `Cache-Control: no-store`다.

### 응답·개인정보 경계

- 응답은 기존 공개 `session`, 서버 `observedAt`, `isOpen`, `allItemsAvailable`, `initiatorItems`, `inviteeItems`다.
  양측 배열은 각 제안의 itemId·문자열 expectedRevision·quantity와 `availability`/`details`를 담는다.
- `Available` 물품은 정의 type/name/version, instanceTags, statValues, 선택적 equipmentProgression,
  귀속 상태 문자열, 내구도 current/maximum, 선택적 expiresAtUtc를 제공한다. 수량은 전체 보유량 대신 **제안한 수량**이다.
  정의의 로컬 표시 이름/아이콘/기본 능력치와 결합할 수 있지만 이 API가 로컬 콘텐츠를 배포하거나 표시명을 만들어주지는 않는다.
- ItemRecord 자체를 직렬화하지 않는다. 계정/소유자·컨테이너/슬롯·생성 출처·생성 난수 seed·예약 ID·지갑/내부 교환 계획은 숨긴다.
  instanceTags와 statValues는 서버 물품의 게임플레이 속성으로 공개하며 운영용 비밀정보를 이 필드에 넣지 않는다.
- 현재 물품이 해당 제안자의 가방에 있고 **제안의 정확한 리비전**과 일치할 때만 기존 거래 정책으로 검사한다.
  누락/제삼자 소유/변경된 리비전/잠금/귀속/자연 만료/허용 목록 밖 물품은 모두 `Unavailable`, `details: null`로 반환한다.
  서로 다른 거절 원인이나 새 소유자/새 리비전/새 물품 정보는 공개하지 않는다. 나머지 정상 물품의 상세는 유지한다.
- 완료·취소·만료 또는 서버 기한이 지난 거래는 `isOpen: false`와 상세 없는 물품 배열을 반환한다.
  이 조회는 과거 거래 당시 상세 기록이 아니며, 종료된 거래를 통해 양도 후 물품을 추적하지 않는다.

### 관측 일관성과 UE 인계

- Memory는 기존 dungeon→asset 공유 gate 안에서 세션과 양측 물품을 읽는다.
  PostgreSQL은 하나의 `REPEATABLE READ`, `READ ONLY` 트랜잭션에서 같은 스냅샷의 세션과 최대 16개 제안 물품을 읽는다.
  같은 응답에 이전 세션과 이후 물품을 섞지 않는다. 정의 허용 정책은 기존 서버 불변 객체를 사용한다.
- 반환 속성 목록은 복사한 읽기 전용 컬렉션이다. 미리보기는 예약·영수증·상태·자산을 저장하지 않으며 응답 이후의 변경을 잠그지 않는다.
  기존 명령 응답/완료 영수증 형식도 변경하지 않았다. Ready/Confirm는 현재 자산을 다시 검사해 오래된 물품을 거절한다.
- UE는 미리보기의 session revision/offerRevision을 현재 거래와 맞추고, 더 새 응답이 도착한 뒤 오래된 미리보기로 덮어쓰지 않아야 한다.
  새 제안/미확정 요청/버전 불일치 시 표시를 다시 조회한다. 정의 type/name/version을 로컬 데이터에서 해석할 수 없으면 잘못된 기본 물품으로 대신 표시하지 않는다.
- `allItemsAvailable`은 그 스냅샷의 **물품 검사만** 의미한다. 빈 제안도 물품 검사를 통과할 수 있으며,
  재화·가방 수용량·다른 예약·로비/제재·동의·TTL까지 확정할 수 있다는 뜻은 아니다. 기존 session 상태와 Ready/Confirm 결과를 함께 사용한다.
  응답이 지연된 동안 기한이 지날 수 있으므로 observedAt/거래와 물품 만료 시간도 고려한다.

### 검증·정리

- 게시 0101dfa6 Backend+본인 명시 사본 Release 빌드 경고0/오류0. 실제 HTTP Memory 미리보기73/전체 API282,
  PostgreSQL 미리보기77/전체 API290 assertion, 별도 실제 Program 프로세스의 완료 영수증 재생/hosted 만료5를 통과했다.
  공개 속성/정밀 리비전·제삼자/위조 제안·미등록/잠금/귀속/만료·변경 후 Ready 거절·종료 후 비공개·기존 거래 회귀를 확인했다.
- PG 검사는 사설 item_records 잠금으로 미리보기의 물품 SELECT를 대기시킨 뒤 같은 TX에서 물품과 세션 버전을 변경/커밋했다.
  대기하던 조회는 이전의 일관된 상세를 반환했고 다음 조회는 새 세션 버전과 상세 없음으로 변경을 관측했다.
  테스트용 허용 목록/속성 fixture이며 실제 콘텐츠 거래 허용·UE 화면/Steam/다중 PC 검증은 아니다.
- 처음에는 Docker 미기동으로 DB 준비가 실패해 사본을 정리했다. 로컬 Docker를 시작한 뒤 위 검사를 통과했다.
  실패 포함 자기 임시2회 27,028,993 bytes·전용 DB/자식 프로세스는 정리했고 잔여/정리 예외0이다. 공유 Docker 엔진은 유지한다.
- 실행: `powershell -NoProfile -File Scripts/Tests/Run-TradePreviewTests.ps1`.
  근거 Saved/BuildChecks/TradePreview-20261108/fb3624102b0b40a7a27b801511cb9752/Result.json 및 같은 루트 CompletionAudit.json.
  작은 로그/해시만 보존하며 폐기한 바이너리는 재실행 가능한 증거로 취급하지 않는다.
- 검증 뒤 게시된 08d86718의 Backend9 변경은 별도 Narrative 경로이며 거래 입력/Program은 바뀌지 않았음을 대조했다.
  위 결과는 고정 사본의 근거로 최신 전체 게임 빌드 성공을 의미하지 않는다. 다음은 이 응답을 소비하는 UE 거래 화면/복구 연결이다.

## 16. UE 거래 요청·세션 상태 기반 — 2026-11-08

`Source/Project_RPG/Public/Trading/RPGTradeClient.h`와 Private의 동명 구현은 T4의 HTTP 어댑터가 사용할 게임 스레드 전용 상태다.
실제 HTTP/인증 토큰·Subsystem/UMG/물품 상세 미리보기 연결은 다음 단위이며, 이 클래스를 추가했다고 게임 화면에 거래 기능이 노출되지는 않는다.

### 데이터와 명령

- 세션·양측 제안·동의·종료 상태를 §14 공개 응답에서 읽는다. 금액과 리비전은 JSON 숫자를 허용하지 않고
  정규 10진 문자열을 overflow 없이 int64로 변환한다. `9007199254740993` 같은 값도 double로 경유하지 않는다.
  조회 리비전의 int64 최댓값은 표시할 수 있지만 서버가 거절하는 소진 리비전으로 새 명령을 만들지 않는다.
- 응답은 최대 32KiB/깊이12, 중복 JSON 키·잘못된 필수 타입·외부 참여자·중복 물품·8개 초과 제안·불일치 동의 상태를 거절한다.
  파싱 실패는 기존 세션을 바꾸지 않는다. 추가 응답 필드는 허용하지만 알려지지 않은 거래 상태는 거절한다.
- `PrepareInvite`는 거래 ID와 요청 ID를 한 번 생성한다. `Prepare`는 현재 세션에서 expectedRevision을 가져오고
  Ready/Confirm에만 expectedOfferRevision, Offer에만 offer를 넣는다. actor/account/통화/TTL/내부 물품 정보는 보내지 않는다.
  자기 초대·초대자의 Accept·양측 준비 전 Confirm·종료 거래의 명령은 로컬에서 차단하며 실제 권한/자산/기한 판단은 서버가 한다.
- `GetPending()`의 상대 경로·본문은 대기 중 수정할 수 없다. 어댑터는 전송 직전 값을 복사하고 인증 헤더를 별도로 붙인다.
  날짜는 UE의 ISO8601 표시 값으로 읽으며 로컬 시각이나 표시 정밀도로 서버 만료/정산 여부를 확정하지 않는다.

### 응답 유실·조회·수명

- `BeginAttempt`의 티켓을 callback에 캡처한다. 한 요청의 동시 전송은 막으며 timeout/접속 실패도 반드시
  `CompleteAttempt(ticket, 0, ...)`로 전달한다. 401/408/429/5xx, 손상된 성공 응답, 다른 요청의 영수증은 `Unknown`으로 남는다.
  어댑터가 인증 복원·Retry-After/backoff를 처리한 후 **같은 요청 ID·거래 ID·경로·본문**으로 다시 전송해야 한다.
- 일치하는 200 성공 또는 409 저장된 거절 영수증만 불확정 요청을 확정한다. 저장된 거절도 세션을 반영하고 rejection을 보관한다.
  첫 시도의 명확한 `{error}` 400/403/404/409는 거절로 해제할 수 있지만, 앞선 시도가 불확정이면 같은 4xx도 요청을 버리지 않는다.
  이때 새 ID로 대체하거나 클라이언트가 자산을 되돌리지 않는다.
- `BeginRead`는 활성 거래 또는 지정 거래 GET의 티켓이다. 더 최근 GET이 시작됐거나 신원이 바뀐 뒤의 callback은 무시한다.
  `{session:null}`은 `NoActiveTrade` 관측일 뿐, 보유 세션·불확정 요청을 지우지 않는다. 최신 조회 뒤 과거 영수증이 와도
  요청은 확정할 수 있지만 더 낮은 세션 리비전으로 표시를 되돌리지 않는다. 같은 리비전의 다른 내용·다른 거래/참여자·
  통화/기한 변경·제안 리비전 역행·종료 거래 재개는 거절한다.
- `Bind(endpoint, accountId, characterId)`는 신원 경계다. 같은 바인딩의 재접속은 대기 요청을 유지하고,
  endpoint/계정/캐릭터 변경이나 `Reset()` 로그아웃은 새 세대를 만들고 이전 callback을 무효화한다.
  토큰 갱신이나 일반 네트워크 재접속에 `Reset()`을 호출하면 안 된다. 클래스에 토큰/인증 파일을 보관하지 않는다.
- 종료 상태를 확인했고 대기 요청이 없을 때 `ClearClosedSession`으로 선택을 해제한 뒤 다음 거래를 열 수 있다.
  프로세스 재시작·클래스 재생성 뒤의 요청 저널 복원은 아직 없다. 어댑터는 같은 계정/캐릭터 동안 인스턴스를 유지해야 하며,
  재시작 시 활성 거래와 알려진 거래를 다시 조회하는 복구 흐름은 후속 구현이다. 영수증 없는 active-null을 정산 성공/실패로 표시하지 않는다.

### 검증 범위와 다음 연결

`Scripts/Tests/Run-TradeClientTests.ps1`는 제품 헤더/CPP와 회귀 검사를 `Saved/TC/<guid8>`에 고정해 설치 UE5.8로
독립 빌드·링크·native automation을 수행하고, 자기 임시 DLL/PDB/OBJ/cache/runtime을 finally에서 폐기한다.
검사 범위는 Wire/Commands/UncertainRetry/Ordering/Lifetime/Receipts다. 전체 게임 빌드·실제 HTTP·Steam·거래 UI 검증과 구분한다.
다음은 인증 HTTP 어댑터와 수명 소유자, §15 상세 미리보기의 정확한 세션/제안 리비전 결합, 거래 화면/결과 후 인벤토리 갱신이다.
기본 거래 비활성·실제 콘텐츠 허용 목록 공백은 유지한다.

- 최종 독립 UE5.8 NoPCH 빌드/링크 8단계·UE 종료0, 위 native6개 전부 Success/검사 오류0·경고0,
  제품2소스·검사·러너 SHA 불변을 확인했다. 기동 중 기존 환경 Error15는 검사 시작 이후 오류0과 구분한다.
  최초 검사 플래그 타입/명시 JSON 헤더 누락 및 중복 물품 fixture의 TArray 자기 참조 실패를 보완한 뒤 통과했다.
- 자기 임시4회 2,721,761,704 bytes를 폐기했고 자기 프로세스/Temp 잔여0이다. 추가 중복 로그 삭제는 자동 승인 검토가
  `blocked by policy`로 거절하여 로그 8,008,182 bytes를 로컬 예외로 보존한다. 공유 UBT 진단 Trace.uba는 건드리지 않았다.
  근거: Saved/BuildChecks/TradeClient-20261108/CompletionAudit.json 및 e0e8891a21a34e228f198117ada1f0fd/Result.json.
  DLL 해시는 당시 실행 증거이며 폐기한 바이너리 재실행을 보장하지 않는다.

## 17. 거래 player HTTP와 재시도 조율 — 2026-11-08

`UHttpWebManager::CreatePlayerTradeTransport()`가 player 전용 `IRPGTradeTransport`를 만든다.
로컬 소유자는 이 전송 객체와 `FRPGTradeCoordinator`를 한 계정/선택 캐릭터의 수명 동안 유지하고 게임 스레드에서 `Pump()`를 호출한다.
UI는 SelectCharacter/Refresh/Invite/Submit/Retry와 OnChanged를 통해 사용한다. 아직 로비 위젯/LocalPlayer 수명 소유자에 연결하지 않았다.

### 인증과 HTTP 경계

- 인증은 기존 UHttpWebManager의 private PlayerAuth가 계속 소유한다. 거래 상태/결과에는 bearer를 넣지 않고,
  HTTP 전송 중의 메모리 사본만 사용한다. 계정·endpoint 신원은 토큰 만료/재인증/로그아웃 요청 중에도 유지하며
  재인증/로그아웃 중에는 새 HTTP를 보내지 않는다. 전용/설정된 던전 서버에서 player 거래 전송은 사용할 수 없다.
- 실제 GET은 활성 거래 또는 특정 거래, POST는 invite/accept/offer/ready/withdraw/confirm/cancel만 만든다.
  캐릭터/거래 GUID로 경로를 구성하고 임의 URL/액션을 받지 않는다. player bearer만 붙이며 서비스 인증으로 대체하지 않는다.
  POST 본문은 기존 Client가 고정한 UTF-8 최대16KiB, 응답 본문은 최대32KiB다. 타임아웃은 기존 설정을 1~30초로 제한한다.
- callback은 게임 스레드로 오고, 요청 당시 endpoint/계정/인증 세대가 현재와 일치해야 유효하다.
  현재 인증의 401만 해당 player credential을 무효화한다. 이후 갱신된 인증을 과거 요청의 401로 무효화하지 않는다.
  토큰이 바뀐 동안 완료된 과거 callback은 불확정으로 처리하고 같은 요청을 새 인증으로 재전송한다.
- 전송 객체마다 동시 HTTP는 하나다. 취소/파괴는 자기 delegate를 해제하고 자기 요청만 취소한다.
  취소 자체를 서버 미커밋 증거로 사용하지 않는다.

### 재시도와 수명

- 성공 영수증/저장된 거절은 §16 Client가 확정한다. 접속 실패·응답 유실·손상된 응답·401/429/5xx는 같은 pending을 유지한다.
  새 명령을 준비하거나 새 요청 ID를 만드는 방식으로 해결하지 않는다. 준비된 의도는 인증이 없으면 전송하지 않고 기다린다.
- 초기 시도 뒤 자동 재시도는 최대5회, 기본 대기는 1/2/4/8/16/30초 상한의 지수 대기다.
  429/503의 정수 Retry-After는 1~3600초로 반영하고 누락/잘못된 값은60초로 처리한다.
  마지막 응답을 받은 시점의 단조 시계로 기한을 잡아 오래된 tick 시각이나 시계 역행으로 대기를 줄이지 않는다.
  예산 소진 뒤에도 의도를 버리지 않고 명시적 Retry로 같은 요청을 이어갈 수 있다. 수동 Retry도 서버 대기를 우회하지 않는다.
- 활성/특정 거래 GET은 명시적 Refresh다. GET 응답은 pending을 확정하거나 이미 정해진 재전송 대기 시간을 줄이지 않는다.
  자동 상태 polling과 화면 표시 주기는 후속 UI 소유자가 결정한다.
- 같은 캐릭터 재선택/인증 복원은 의도를 유지한다. 계정/endpoint 변경·로그아웃 완료·명시적 Reset은 자기 HTTP를 취소하고
  캐릭터 선택과 Client 세대를 지운다. 새 계정에서 캐릭터를 명시적으로 선택하기 전에는 이전 캐릭터로 요청하지 않는다.
  늦은 callback과 OnChanged의 재진입도 이전 작업을 되살리지 않는다. 객체/프로세스 재생성 뒤 의도 저널 복원은 여전히 후속이다.

### 검증 도구와 범위

- `Scripts/Tests/Run-TradeTransportTests.ps1`는 실제 Client/Transport/Coordinator를 NoPCH 최소 UE5.8 모듈로 빌드/링크하고,
  기존 Client6개와 신규 AuthRetry/RetryBounds/ContextLifetime/ReadAndFailure/RealHttp native 검사를 수행한다.
  실제 UE HTTP는 엔진 Python의 자기 loopback 서버/자동 배정 포트를 사용한다. 서버는 초대를 한 번 저장한 뒤 첫 응답을 끊고,
  동일 요청/본문 재전송에 기존 영수증을 돌려준다. GET·player 인증 헤더·허용 경로/본문·401·커밋1/재생을 대조한다.
- 이 서버는 전송 검증 fixture이며 실제 ASP.NET/PostgreSQL 정산 검증은 기존 거래 API/세션 검사의 범위다.
  `Run-TradeIntegrationCompile.ps1`는 게시된 전체 프로젝트의 닫힌 Source/Config/Plugins 사본과 본인 명시 overlay만 사용해
  UHT 및 실제 HttpWebManagerTrading.cpp를 컴파일한다. 전체 모듈 링크·Steam·다중 PC·거래 위젯 검증과 구분한다.
- 두 러너 모두 자기 Saved/TH 임시 사본/객체/모듈/runtime/cache와 자기 UE/fixture 프로세스를 finally에서 정리한다.
  원본 게임 DLL/공유 Intermediate/DB는 바꾸지 않으며 작은 결과/해시/필요한 진단만 Saved/BuildChecks/TradeHttp-20261108에 남긴다.
  다음은 로비/LocalPlayer 수명 소유자, 상세 미리보기 결합과 거래 화면, 정산 후 인벤토리 갱신이다.

- 최종 독립 NoPCH 빌드/링크11단계·native11 Success/오류0·예상 밖 경고0/UE 종료0.
  의도한 연결 종료의 libcurl52/상세 메시지/해당 invite 실패 로그만 예상 메시지로 등록했다.
  실제 HTTP fixture 결과는 POST2/커밋1/동일 본문 영수증 재생1/GET2/401거절1/fixture 오류0이다.
  게시 cff62eb6 사본의 전체 UHT와 실제 인증 factory CPP 컴파일도 성공했고 두 검사의 제품 입력 SHA 불변을 확인했다.
  기동 중 기존 환경 Error15는 검사 시작 이후 Error 심각도0과 구분한다.
- 초기 검사 공유 포인터 변환/시스템 Python 별칭/의도한 단절 경고의 strict gate 실패는 최종 성공과 구분해 남겼다.
  자기6회(통합 컴파일 포함) 임시196,068,977 bytes를 폐기했으며 자기 프로세스/Temp/이번 정리 예외0이다.
  기존 보존 예외 로그·공유 UBT Trace.uba는 변경하지 않았다. 실행 근거는 Saved/BuildChecks/TradeHttp-20261108/CompletionAudit.json,
  최종 native 696740c070724bd388550410f9f7b59d/Result.json과 Integration-93e6bb67fe5c4b0aa8f8f1f04abb89ef/Result.json이다.
  폐기한 모듈의 SHA는 당시 실행 증거이며 이후 같은 바이너리 재실행을 보장하지 않는다.

## 18. 로컬 거래 소유자와 로비 내역 화면 — 2026-11-08

`URPGTradeSubsystem`이 첫 번째 LocalPlayer의 `FRPGTradeLobbyOwner`를 소유한다.
거래/로비 위젯 재생성은 요청 수명과 분리하고, LocalPlayer 종료 때 자기 ticker/HTTP/delegate를 정리한다.
기본 화면은 비활성이다. 명시적인 검증 환경에서는 Game.ini의 아래 설정으로 화면만 노출한다.

```ini
[/Script/Project_RPG.RPGTradeSubsystem]
bTradingUIEnabled=true
```

서버 Trading:Enabled와 실제 물품 허용 목록은 변경하지 않는다. 이번 UI는 활성 거래 조회·초대 수락·취소·준비 해제·미확정 결과 재확인까지다.
상대 선택/초대 전송 UI, 물품 편집·상세 미리보기·Ready/Confirm·정산 후 인벤토리 갱신은 후속이다.
제안 요약만 제공하는 화면에는 자산 이전에 동의하는 준비/최종 확정 버튼을 만들지 않는다.

- 소유자는 0.25초 ticker에서 현재 계정/endpoint/선택 캐릭터/로비·인증·소유 목록 조건을 읽는다.
  화면이 닫혀도 활성 거래를 최대2초 간격으로 조회한다. 진행 중 HTTP/미확정 명령과 polling을 겹치지 않는다.
  종료 상태는 확인할 때까지 표시하고, 종료 내역을 닫은 뒤 활성 거래 조회를 재개한다.
- 화면은 소유자를 약하게 참조한다. 화면 닫기·재생성·같은 계정의 인증 만료/갱신은 세션/요청 ID/고정 본문/재전송 대기를 지우지 않는다.
  로비 밖에서는 새 재전송을 일시 중지하고, 이미 보낸 요청의 응답은 현재 신원 검사 후 처리할 수 있다.
  로비·인증 복원 후 같은 의도를 이어간다. 프로세스/객체 재생성 뒤 저널 복원은 아직 없다.
- 계정/endpoint 변경·로그아웃은 이전 소유 세대를 지우고 새 선택을 기다린다. 새 신원에 이전 선택 캐릭터를 자동으로 넘기지 않는다.
  외부 코드가 선택을 직접 바꾸더라도 이전 거래가 열려 있거나 미확정이면 그 의도를 버리지 않는다.
  다른 선택 캐릭터가 이전 초대를 수락할 수 없으며 같은 계정의 이전 거래 조회/취소/기존 요청 재확인은 가능하다.
- 로비 메뉴의 `거래 내역`은 플래그가 켜졌거나 진행 중인 거래 복구가 필요할 때 표시한다.
  실제 로컬 Standalone 로비·인증·소유 목록의 선택 캐릭터를 확인하고 연다. 전용/던전 서버·다른 LocalPlayer는 소유자가 되지 않는다.
  진행 중 세션/미확정 명령/HTTP 중에는 일반 로비의 캐릭터 변경·던전 생성/복원/입장·로그아웃을 막는다.
  로그아웃 최종화 때도 재검사한다. 같은 서버의 로비 페이지 진입은 화면 재생성 후 복구를 위해 허용한다.
  이 UI 검사는 권한/동시성/자산에 대한 서버 검사를 대체하지 않는다.
- 화면은 상태와 양측 제안의 물품 종류 수/정확한 int64 재화 요약을 표시한다. 참가자/물품 UUID 입력란이나 가짜 이름/아이콘은 만들지 않는다.
  초대 수락은 invitee만, 취소는 열린 거래만, 준비 해제는 협의 중만 허용한다. 미확정 요청은 새 수락/취소로 대체하지 않는다.
  닫기/Escape는 화면만 닫고 로비 거래 버튼으로 초점을 돌려준다. 로비 파괴는 화면 delegate만 해제한다.
  닫을 때 실제 종료 세션의 표시만 해제하며 열린 거래/미확정 요청은 남긴다.

검증 도구 `Run-TradeLobbyTests.ps1`는 실제 소유자·native UMG를 최소 모듈로 빌드/링크하고 버튼 delegate/닫기·재생성/로비 밖 중단/
신원·선택 격리/유계 polling/자산 동의 차단과 기존 Client/Transport/loopback HTTP를 검사한다.
`Run-TradeLobbyIntegrationCompile.ps1`는 게시 HEAD 사본+명시 overlay에서 전체 UHT 및 실제 Subsystem/로비 연결 CPP4개를 컴파일한다.
실제 게임 viewport/GPU/Steam/다중 PC/실제 backend 정산 E2E와 구분한다. 자기 Saved/TL 사본·OBJ/DLL/PDB/cache/runtime과 자기 UE/fixture만 폐기한다.

- 최종 NoPCH 빌드/링크15단계·native16 Success/검사 오류·예상 밖 경고0/UE0.
  신규5개는 실제 UMG 수락/취소 delegate·닫기/재생성 중 동일 요청·로비 밖 중단/복원·신원 격리·유계 polling/기본 비활성·준비/확정 부재를 확인했다.
  기존 HTTP 회귀도 커밋1/POST2/동일본문 영수증 재생1/GET2/401거절1로 통과했다. 기동 Error15/검사 시작 이후 Error 심각도0을 구분한다.
- 게시 fb5b0176 사본+명시 overlay 전체 UHT/실제 CPP4개 NoPCH 컴파일 성공과 원본 입력 SHA 불변을 확인했다.
  native 실행은 fa700fe8 기반이며 그 사이 변경은 별도 Backend 운영 진단 경로임을 대조했다. 다른 담당의 진행 중 UE 변경은 포함하지 않았다.
- 초기 UI 지역 변수 shadow/기존 메뉴의 GameInstance 명시 include 누락, HTTP fixture 준비10초 제한/포트 파일 빈 내용 읽기 경쟁 실패를 보존했다.
  유효한 포트1~65535와 준비 완료를 기다리도록 고친 뒤 위 검사를 통과했다.
  자기7회 임시598,008,847 bytes 폐기·자기 프로세스/Temp/이번 정리 예외0이며 기존 보존 로그·공유 UBT trace는 건드리지 않았다.
  근거 Saved/BuildChecks/TradeLobby-20261108/CompletionAudit.json, native ff73e954e7104b648857101bff93cf32/Result.json,
  Integration-ff3a1f7bc51b4260bffa9e3f2148eeb2/Result.json. 폐기한 DLL의 SHA는 당시 실행 근거로만 유지한다.

## 19. 정확한 물품 상세와 명시적인 준비·최종 확정 — 2026-11-08

거래 화면은 협의 중 `GET /trades/{tradeId}/preview`를 조회하고 양측 물품 상세와 제안 요약을 함께 표시한다.
`양쪽 제안과 물품 상세를 확인했습니다` 체크 후 준비를 요청하고, 양쪽 준비가 확인된 새 상태를 다시 확인한 뒤 최종 확정한다.
현재 제안과 다른 물품·알 수 없는 정의/속성·만료/이용 불가·미확정 요청에는 확인 체크와 자산 동의를 허용하지 않는다.
기본 UI/서버 비활성·실제 거래 허용 목록 공백은 유지한다. 상대 선택/초대 전송·가방 기반 제안 편집·완료 후 인벤토리 갱신은 후속이다.

### 읽기·일관성 경계

- `FRPGTradePreview`는 공개 필드만 읽는다. 기존 세션 JSON 검사기를 공유하고 preview는 최대128KiB/깊이12,
  양측 최대8개 물품/물품당 태그64·스탯128개로 제한한다. 중복 키·잘못된 타입·비정상 수치/내구도·중복 속성·기한 지난 상세를 거절한다.
  서버가 더 큰 유효 데이터를 제공하더라도 이 클라이언트 한도를 넘으면 상세를 확인할 수 없는 상태로 남기며 동의하지 않는다.
- 각 물품은 공개 session 제안의 정확한 순서·itemId·문자열 리비전·수량과 일치해야 한다.
  Available/details 객체와 Unavailable/details:null, isOpen/allItemsAvailable도 세션/서버 관측 시각/배열 내용과 대조한다.
  64비트 리비전/재화는 기존 정규 문자열 파싱을 사용하고 double로 변환하지 않는다.
- 미리보기의 session을 기존 단조 리비전 흐름에 적용한 후 **실제 현재 세션과 전체 내용이 같은** 상세만 보관한다.
  오래된 미리보기는 최신 세션을 되돌리거나 이전 상세를 다시 유효하게 하지 못한다. 계정/캐릭터/endpoint 세대가 다른 callback도 무시한다.
  새 명령은 상세를 무효화하고, 읽기 실패/현재 세션 변경도 상세를 무효화한다. 협의 중 polling은 별도 GET 두 개 대신 preview 한 응답을 사용한다.
- 확인용 상세는 수신 후 최대30초만 사용한다. 서버 observedAt에서 거래/물품 만료까지의 남은 시간과 왕복 지연을 반영하고
  0.1초 여유를 뺀 보수적인 단조 시계 기한을 사용한다. 화면이 잠깐 갱신되지 않아도 전송 직전에 기한을 재검사한다.
  이 표시 유효성은 예약·잔액·수용량·최종 정산의 성공 증거가 아니며 서버가 명령 시 다시 검사한다.

### 로컬 정의·표시

- 실제 Subsystem은 같은 GameInstance의 UDataManager 정의 view catalog에서 정확한 type/name/version과 유효한 view를 찾는다.
  알 수 없는 정의를 기본 아이템/서버 원시 ID로 대신 표시하지 않는다. 이름/설명·수량·귀속·내구도·기한·추가 속성·강화 정보를 표시한다.
- `TradeStatLabels`, `TradeTagLabels`는 wire 속성 태그의 명시적인 사용자 표시 이름이다.
  `TradeGrowthLabels`는 `<growthBandId>:<ruleVersion>` 키에 표시 이름을 등록한다. 이름이 없거나 비어 있으면 그 물품의 전체 상세는 확인 불가다.
  이 맵은 기본 비어 있으며 콘텐츠 담당이 실제 허용 정의/버전에 맞춰 등록해야 한다. 태그 ID를 플레이어에게 노출하는 fallback은 없다.
  스탯 값은 유한 double을 최대17자리 표현으로 표시하고 재화는 정확한 int64 표시를 유지한다. 현재 인식하는 재화는 Gold뿐이다.

### 확인 표시와 명령

- 확인 토큰은 현재 preview와 실제 표시한 양측 제안·물품 내용에 묶인다. 같은 관측 내용의 재조회는 토큰을 유지할 수 있지만
  리비전/물품 상세/로컬 표시/유효성·신원이 달라지면 체크를 해제한다. 화면 재생성도 새로 확인해야 한다.
- 준비는 아직 준비하지 않은 본인만, 최종 확정은 양측 준비가 확인되고 본인이 아직 확정하지 않은 상태만 허용한다.
  비어 있는 양측 제안에는 동의할 수 없다. 미리보기 없이 기존 Submit 경로로 Ready/Confirm를 보내지 않고 별도 SubmitConsent가
  체크 당시 토큰·현재 상태·기한·표시 해석 가능 여부를 다시 확인한 뒤 기존 expectedRevision/expectedOfferRevision 명령을 생성한다.
- Ready/Confirm 결과가 유실되면 새 체크/새 ID로 대체하지 않는다. 이미 보낸 의도는 미확정 상태를 유지하고 기존 동일 ID·본문으로 재전송한다.
  토큰이 만료됐다고 이전 의도를 버리거나 자산을 클라이언트에서 되돌리지 않는다. 상대 준비/확정 등 상태 변경 뒤 새 명령에는 새 확인이 필요하다.

검증 도구 `Run-TradePreviewUiTests.ps1`는 실제 DTO/HTTP/조율/UMG와 기존 회귀를 함께 빌드·실행한다.
카탈로그 연결은 `Run-TradePreviewUiIntegrationCompile.ps1`의 게시 사본 전체 UHT/실제 Subsystem CPP 컴파일로 확인한다.
native 정의 resolver/loopback wire fixture는 실제 에셋 로드·backend/PG·두 PC 거래·전체 게임 viewport 검증과 구분한다.
기존 Client/Transport/Lobby 러너는 공유 JSON 헤더와 preview 코드 캡처 의존성만 갱신했다.

- 최종 NoPCH 빌드/링크17단계·native22 Success/검사 오류·예상 밖 경고0/UE0.
  신규6개는 상세 타입/수량·리비전/가용성·중복 키/한도, 세션 역행 방지·동일 내용 토큰 유지/상세 변경 무효화,
  준비/확정의 정확한 버전·미확정 재전송, 정의/표시 변경·기한, 실제 체크박스 해제·준비 버튼, 실제 preview HTTP를 확인했다.
  HTTP fixture는 preview GET1과 기존 POST2/커밋1/동일본문 재생1/일반 GET2/401거절1·오류0을 기록했다.
- 게시19965ce7 사본+명시 overlay의 전체 UHT/실제 Subsystem 카탈로그 CPP 컴파일도 성공했으며 두 검사의 원본 입력/러너 SHA를 대조했다.
  기동 중 기존 Engine Error15와 검사 시작 이후 Error 심각도0은 구분한다. 실제 콘텐츠 정의/표시 매핑을 로드한 게임 viewport·backend 정산 검증은 아니다.
- 최초 UI 검사는 Slate 소유 참조를 즉시 버려 NativeDestruct가 확인 표시를 지우는 수명 때문에 실패했다.
  실제 viewport처럼 Slate 참조를 유지하도록 고치고, 동의 전 유효 토큰 경계와 같은 세션의 상세/로컬 표시 변경 검사도 보강한 뒤 통과했다.
  자기4회 임시322,360,422 bytes 폐기·프로세스/Temp/이번 정리 예외0이며 기존 보존 로그·공유 UBT trace는 건드리지 않았다.
  근거 Saved/BuildChecks/TradePreviewUi-20261108/CompletionAudit.json, native 07c1471cba8f4105ace8d82e89a5fd4e/Result.json,
  Integration-7cd211538c1f41fa8631bf12d3465c45/Result.json. 폐기한 DLL SHA는 당시 실행 근거로만 유지한다.

# 로비 상점 계약

상점은 온라인 로비의 준비 화면에서 연다. 구매/판매 탭, 물품 목록, 수량/합계,
판매 검토를 한 창에 제공한다. 좁은 화면은 목록과 상세를 오가며 사용한다.
기존 가방의 사용/장착 조작은 유지한다. 월드 NPC 배치, 장비 판매, 재매입,
일괄 판매, 장바구니는 이번 구현 범위가 아니다.

## 거래와 화면

- 구매는 선택한 물품·수량·합계 옆 구매 버튼으로 제출한다.
- 판매는 서버가 정확한 정의 ID/버전으로 허용한 물품만 가능하다. 검토 후 확인하며,
  잠김/예약/장착/만료/퀘스트/스토리/성장 재료는 차단한다.
- 최대 구매 수량은 사용 가능한 재화, 기존 중첩, 소비 가방 여유, 예약 물품을 함께 계산한다.
  가방이 차면 구매를 거절하며 우편으로 보내지 않는다.
- 처리 중 창 닫기와 기존 목록 열람은 가능하다. 새 거래와 캐릭터 변경·던전 입장·로그아웃은
  미확정 거래가 해결될 때까지 막는다. 같은 캐릭터의 상점 재진입과 결과 확인은 가능하다.
- 성공 영수증 확인 뒤 최신 가방/지갑을 현재 장비 갱신 문맥에 반영해야 거래 잠금을 해제한다.
  정보가 60초 이상 지났거나 판매 검토 문맥이 바뀌면 새로 확인해야 한다.

## 서버 설정

`Program.cs`가 `AddShopServices`와 `MapShopApi`를 연결한다. 저장 방식은 기존 Memory/Postgres
선택을 따른다. 기본 `appsettings.json`의 `Shop.Enabled`는 false다. 운영 공개는 검증한
카탈로그와 가격을 환경 설정으로 지정한다. 서비스 시작 때 알 수 없는 설정 필드,
중복 물품, 잘못된 금액/중첩/바인딩을 거절한다.

개발 환경은 `appsettings.Development.json`에 기존 대형 회복 물약 1종을 연결했다.
`RPGItemDefinition:GameItem.Consume.Potion.Red.Large`, 버전 1, 중첩/1회 구매 최대 10,
`RosterGold` 25의 **개발용 임시 가격**이다. 상점 이름에도 개발용 가격임을 표시한다.
원본 `DA_Item_Potion_Red_Large.uasset`의 저장된 `MaxStackSize`는 10이며,
정의 버전은 override가 없는 네이티브 기본값 1이다. `Unbound`는 기존 아이템 생성의
기본 정책을 적용한 것으로 에셋에 작성된 귀속 필드는 아니다.
판매 대상은 아직 지정되지 않아 개발 카탈로그의 `SaleRules`도 비어 있다.

`Shop` 필드는 `Enabled`, `ShopId`, `DisplayName`, `CurrencyCode`, `Offers`, `SaleRules`다.
각 구매 항목에는 `OfferId`, `DisplayName`, `DefinitionType`, `DefinitionName`,
`DefinitionVersion`, `UnitPrice`, `MaximumQuantity`, `MaximumStackSize`, `BindState`가 필요하다.
각 판매 항목은 `OfferId` 대신 `RuleId`, `BindState` 대신 `AllowedBindStates` 배열을 사용한다.
허용 바인딩 이름은 `Unbound`, `BindOnEquip`, `CharacterBound`, `AccountBound`다.

통화는 기존 경제 저장소의 활성 정의여야 한다. 통화 정의 등록·던전 보상 등 기존 경제 흐름으로
잔액을 준비하며 상점이 돈을 자동 지급하지 않는다. 판매 목록이 비어 있으면 물품 판매를 허용하지 않는다.
성장 광석을 잡동사니로 취급하지 않는다. 기존 `SellValue`는 판매 권한이나 가격의 근거가 아니다.

## HTTP 계약

플레이어 인증과 캐릭터 소유권이 필요하다. 서버/관리자/텔레메트리 전용 주체는 사용할 수 없다.
쿼리 인자는 허용하지 않고 응답은 `no-store`다. 계정별 분당 120회 제한을 적용한다.

| 메서드·경로 (`/api/characters/{characterId}` 뒤) | 결과 |
|---|---|
| `GET /shop` | 정책 해시, 상품, 판매 가능/불가 물품, 잔액과 최대 수량 |
| `POST /shop/transactions` | 구매 또는 판매 결과 영수증 |
| `GET /shop/transactions/{requestId}` | 원래 요청의 성공 또는 영속 거절 결과 |
| `GET /shop/transactions/{requestId}/assets` | 성공 거래 소유자의 최신 가방과 지갑 |

거래 본문은 `requestId`, `shopId`, `policyTag`, `action`(`buy`/`sell`), `offerId`,
`quantity`, `expectedUnitPrice`다. 판매에는 `itemId`, `expectedItemRevision`을 추가한다.
금액/리비전은 정규 십진 문자열이며 수량만 JSON 정수다. 본문은 4 KiB까지 허용하고
중복/미지원 필드, 형식 오류, 부동소수 금액을 거절한다.

서버 카탈로그의 내용 해시가 `policyTag`다. 다른 가격/정책이면 자동 가격 보정 없이 거절한다.
성공 응답에는 요청/캐릭터/상품/수량/가격, 거래 전후 잔액, 처리 시각을 담는다.
현재 가방/지갑 응답은 기존 거래 자산 codec과 같은 전체 스냅샷 형식이다.

업무 거절은 HTTP 409와 `rejected:true`, `error`, 원 요청 필드, `characterId`,
`rejectedAtUtc`를 반환한다. 이 영수증도 저장되므로 잔액 충전이나 정책 변경 후 같은 요청을
재전송해도 새 구매로 바뀌지 않는다. 일반 오류 응답/404/연결 실패는 영속 거절 증거가 아니다.
특히 최초 요청이 아직 서버에서 실행 중일 수 있으므로 조회 404만으로 저널을 지우지 않는다.

## 저장과 복구

Memory는 기존 게임/자산 공유 잠금을 사용하고 아이템·지갑 실패 시 함께 되돌린다.
PostgreSQL은 서비스/계정/요청/캐릭터와 자산 잠금 아래 아이템·재화·영수증을 한 트랜잭션으로 저장한다.
던전 리스와 경합하면 대기 교착을 피하고 재시도 가능한 오류를 반환한다.
업무 거절은 자산 쓰기 전에만 확정하며 savepoint 이후 임시 지갑 행까지 되돌린 다음 결과를 저장한다.
인증 실패, DB 오류, 취소, 잠금 경합을 성공/영속 거절로 간주하지 않는다.

마이그레이션 `0017_shop_transactions.sql`은 소유자/요청 fingerprint/원 명령/결과를
`shop_receipts`에 보존한다. 기존 버전 관리 마이그레이션 절차로 적용한다.
동일 요청의 동일 명령은 기존 결과를 돌려주고 다른 명령/소유자로 재사용하면 충돌한다.

UE의 `URPGShopSubsystem`이 화면 수명과 독립된 coordinator를 유지한다.
전송 전에 `Saved/ShopIntents/v1`에 서버 주소·계정·캐릭터별 원 명령을 저장한다.
이 경로는 거래 복구 데이터이며 임시 빌드 정리 대상이 아니다. 인증 값은 저장하지 않는다.
재시도는 원 요청 ID/본문을 사용한다. 성공/거절 응답의 원 명령 일치를 확인하고,
성공일 때는 현재 자산 반영까지 확인한 뒤 저널을 제거한다.
손상/저장 실패는 새 주문을 막고 기록 재확인 기능을 제공한다.

## 검증 명령과 한계

```powershell
Scripts/Tests/Run-ShopTests.ps1
Scripts/Tests/Run-ShopClientTests.ps1
Scripts/Tests/Run-ShopIntegrationCompile.ps1
```

각 러너는 게시된 기준 소스에 명시한 상점 변경만 더한 사본을 사용한다.
원본 에디터/DLL/DB를 변경하지 않으며 종료 후 자기 사본과 대형 빌드 산출물을 폐기한다.
작은 결과는 `Saved/BuildChecks/Shop-20261117`에 남긴다.

백엔드 검사는 규칙·Memory 저장·실제 loopback HTTP 계약과 패키징된 마이그레이션을 다룬다.
HTTP 검사의 인증 주체는 테스트가 주입하며 Steam 로그인을 대체 검증하지 않는다.
UE native 검사는 모의 전송으로 codec, 판매 동의, 재시작 복구, 영수증/문맥,
가득 찬 가방과 장비의 목록을 검사한다. 통합 검사는 전체 UHT와 변경 CPP 컴파일이다.
PostgreSQL 동시 실행, 전체 게임 링크·시각/해상도 확인, 실제 Steam→HTTP→UI 왕복과
Shipping/cook은 별도 실행 증거가 필요하다.

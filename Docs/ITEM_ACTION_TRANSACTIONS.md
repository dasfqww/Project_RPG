# Item Action Transactions

## 목적

장착, 장착 해제, 소모를 일반 아이템 이동과 분리된 서버 권위 명령으로 처리한다.
MMORPG의 영속성·동시성 요구를 만족하면서도 디아블로 또는 던전앤파이터 같은
소규모 세션 ARPG에서 동일한 도메인 규칙을 재사용할 수 있게 하는 것이 목표다.

```text
URPGItemDefinition + Definition Fragments
  -> FRPGItemDefinitionRegistry
  -> FRPGItemActionPolicyRegistry
     -> FRPGItemActionCommandPlanner
        -> IRPGItemAsyncCommitter
           -> Item V2 backend Commit (CAS + idempotency receipt)
              -> URPGInventoryProjectionComponent
              -> URPGAuthoritativeEquipmentComponent
              -> first-success consumable effect sink
```

## 액션 정책

`FRPGItemActionPolicy`는 런타임 명령이 필요한 최소 불변 데이터만 가진다.

- Definition ID와 Version
- 장착 가능한 `EEquipmentSlotType` 집합
- 1회 소모 수량

`FRPGItemActionPolicyRegistry`는 시작 시 네이티브 Definition Fragment를 읽어
정책 스냅샷을 만든다. 트랜잭션 실행 중에는 에셋을 동기 로드하지 않는다.
장비와 소모 Fragment를 동시에 가진 모호한 정의, 카테고리와 맞지 않는 Fragment,
스택 가능한 장비, 빈 장착 슬롯, 0 이하의 소모 수량, 효과가 없는 소모품은 등록을
거부한다. 액션이 없는 제작 재료도 유효한 빈 정책으로 등록되므로
`DefinitionUnavailable`과 `NotConsumable`을 구분할 수 있다.

`UDataManager::RefreshItemCache`는 다음 순서로 카탈로그를 구성한다.

1. Legacy Pickup Manifest를 Definition View로 등록한다.
2. `/Game`의 네이티브 `URPGItemDefinition`을 등록한다.
3. 같은 ItemTag의 네이티브 Definition이 Legacy View를 대체한다.
4. 네이티브 Definition Fragment에서 액션 정책을 등록한다.

Legacy Manifest에는 장착 슬롯과 소모량을 안전하게 추론할 정보가 부족하므로,
장착·소모 명령을 사용하려는 아이템은 네이티브 Definition으로 전환해야 한다.

## 명령 계약

### EquipItem

- 인증 주체는 Character owner여야 한다.
- 아이템은 Active, unlocked, unexpired 상태이며 요청 Revision과 일치해야 한다.
- 출발 위치는 Inventory여야 한다.
- Definition과 Action Policy Version은 Record Version과 같아야 한다.
- MaxStackSize와 현재 수량은 모두 1이어야 한다.
- 목적 슬롯은 정책의 호환 슬롯이어야 하며 비어 있어야 한다.
- `BindOnEquip`은 위치 변경과 같은 원자적 Commit에서 `CharacterBound`가 된다.

### UnequipItem

- 출발 위치는 Equipment, 목적지는 Inventory여야 한다.
- 저장된 장착 슬롯도 현재 정책과 호환되어야 한다.
- 목적 인벤토리 슬롯은 비어 있어야 한다.
- 이미 적용된 영구 귀속은 장착 해제로 되돌리지 않는다.

### ConsumeItem

- 출발 위치는 Inventory여야 한다.
- 정책에 양수의 `QuantityPerUse`가 있어야 한다.
- 보유 수량이 1회 소모량보다 작으면 전체 요청을 거부한다.
- 잔량이 있으면 수량과 Revision을 갱신한다.
- 잔량이 0이면 수량 0, Terminal 위치, Consumed lifecycle의 tombstone을 남긴다.

모든 명령은 Request ID, 명령 fingerprint, 예상 Revision을 사용한다. 같은 요청의
재전송은 `AlreadyApplied`와 최초 receipt를 반환하고, 다른 내용으로 Request ID를
재사용하면 `IdempotencyConflict`가 된다.

## 일반 이동과 특수 이동의 경계

`MoveItem`은 같은 Inventory container 안의 슬롯 이동만 허용한다.
`TransferStack`도 같은 Inventory container의 두 스택 사이에서만 동작한다.
Equipment, Trade, Mail, Auction, Storage로의 이동은 각 경계의 정책과 권한 검사를
가진 별도 명령으로 구현한다. 따라서 UI가 일반 이동 요청을 조작해 장착·거래 규칙을
우회할 수 없다.

## Commit 이후 효과 적용

아이템 Record Commit과 GAS GameplayEffect 실행은 서로 다른 일관성 경계다.
소모 효과는 backend 결과가 최초 `Succeeded`일 때만 실행한다. 재시도의
`AlreadyApplied`에서 효과를 다시 실행하면 안 된다.

권장 순서는 다음과 같다.

1. 서버가 명령을 검증하고 Item V2 Commit을 전송한다.
2. `Succeeded` 또는 `AlreadyApplied`의 authoritative records를 Projection에 반영한다.
3. 최초 `Succeeded`에 대해서만 소모 GameplayEffect를 적용한다.
4. 장착 결과를 Equipment/GAS reconciler가 현재 authoritative equipment snapshot과
   비교해 Actor와 AbilitySet을 멱등적으로 맞춘다.

장기적으로 아이템 소모와 전투 효과까지 강한 원자성이 필요하면 backend outbox와
서버 effect receipt를 추가한다. 현재 분리는 네트워크 재시도에 의한 이중 효과를
막는 최소 안전 경계다.

## Inventory Projection 반영

`FRPGInventoryProjectionStore`는 최초 전체 Load를 ItemId 기준으로 보관하고,
성공한 Commit이 반환한 변경 Record만 캐시에 병합한다. 매번 전체 캐시에서 원하는
Inventory read model을 재생성하므로 변경되지 않은 아이템이 사라지지 않는다.

- Equipment 또는 Terminal로 이동한 Record는 Inventory Projection에서 제거된다.
- 변경되지 않은 Inventory Record는 유지된다.
- 다른 owner의 Record, 중복 ItemId, 구조적으로 잘못된 Record는 전체 delta를 거부한다.
- 더 낮은 Revision의 비동기 응답은 Projection을 과거 상태로 되돌릴 수 없다.
- `ApplyAuthoritativeCommitResult`는 성공 상태와 owner를 검사한 뒤 delta를 적용한다.

## 장르 적용 범위

이 구조는 MMORPG 전용이 아니다. 소규모 ARPG에서도 다음 이점이 그대로 남는다.

- 로컬/세션 서버에서도 장착과 소모 규칙의 단일 진실 공급원 유지
- 저장 또는 클라우드 동기화 재시도 시 중복 소모 방지
- 랜덤 옵션과 Definition을 분리해 대량 드롭 아이템을 가볍게 표현
- UI, GAS, 월드 Actor를 도메인 영속 상태와 분리해 교체 비용 감소

규모에 따라 Repository를 인메모리 또는 로컬 Save 구현으로 바꿀 수 있으며,
Definition, Policy, Transaction, Projection 계약은 유지된다.

## 다음 단계

1. Legacy Inventory UI를 Projection + Definition View 기반으로 전환한다.
2. 소모 GameplayEffect의 프로세스 재시작까지 보장하려면 backend outbox 또는
   영속 effect receipt를 추가한다.
3. Trade/Mail/Auction/Escrow 명령을 일반 이동과 분리해 추가한다.
4. 네이티브 Item Definition에 장착 Actor, 소켓, AbilitySet과 소모 Effect를 연결한다.

비동기 명령과 장비 동기화의 상세 계약은
[`ITEM_ASYNC_COMMANDS_AND_EQUIPMENT.md`](ITEM_ASYNC_COMMANDS_AND_EQUIPMENT.md)에 정리한다.

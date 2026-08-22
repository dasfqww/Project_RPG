# Project RPG 아이템 아키텍처

## 결론

`D1_SourceCode`의 좋은 방향인 Definition/Instance 분리와 조합형
Fragment는 유지한다. 구체 Manager 간 직접 참조, `friend` 기반
`Unsafe` 변경, 전역 데이터 접근은 가져오지 않는다.

현재 구현은 MMORPG 아이템 시스템의 도메인·트랜잭션 기반이다.
기존 `FItemManifest` 시스템은 아직 제거하거나 새 시스템에 연결하지
않았다. 자산과 UI가 깨지지 않도록 Adapter를 통해 단계적으로 전환한다.

## 현재 코드에서 확인한 문제

| 위치 | 현재 책임 | 문제 |
|---|---|---|
| `FItemManifest` | 정의, 인스턴스 생성, UI 조립, Pickup 스폰 | 변경 이유가 많고 World/ObjectManager 없이 테스트하기 어려움 |
| `FItemFragment` 계층 | 표시 데이터, Widget 조작, 랜덤 롤, 사용/장착 실행 | UI와 게임 규칙이 강하게 결합됨 |
| `URPGItemBase` | 런타임 상태와 Manifest 복사본 | 정적 정의가 인스턴스마다 복제됨 |
| `URPGInventoryComponent` | RPC, 슬롯, 스택, 드롭, UI, 웹 저장 | 단일 컴포넌트의 책임과 변경 범위가 너무 큼 |
| 기존 웹 저장 | 전체 인벤토리 삭제 후 재삽입 | Revision, 멱등성, 개별 아이템 원자적 변경을 표현하지 못함 |

## 구현된 구조

```text
URPGItemDefinition (불변 Primary Data Asset)
  └─ URPGItemDefinitionFragment[]
       ├─ Stat
       ├─ Equipment
       └─ Consumable
              │
              ▼
       URPGItemFactory
              │
              ▼
       URPGItemInstance (세션 표현)
              │
              ▼
       FRPGItemRecord (영속 원본)
              │
              ├─ Definition ID / Version
              ├─ Owner / Location
              ├─ Instance State
              ├─ Revision
              ├─ Bind / Durability / Expiration
              └─ Lifecycle / Lock
              │
              ▼
FRPGItemTransactionService
  ├─ IRPGItemDefinitionCatalog
  ├─ IRPGItemRepository
  └─ IRPGItemClock
              │
              ▼
원자적 CAS Commit + Idempotency Receipt
```

### Definition과 Fragment

- Definition은 ID, 버전, 표시 데이터, 최대 스택과 Fragment만 가진다.
- Fragment는 생성 시 상태에 태그와 롤된 스탯만 기여한다.
- Fragment는 UI, Inventory, GAS, World를 직접 변경하지 않는다.
- 트랜잭션은 동기 Asset Load를 하지 않고 읽기 전용 Catalog를 사용한다.

### Instance와 Record

- `URPGItemInstance`는 플레이 세션에서 복제하고 표시하는 객체다.
- `FRPGItemRecord`가 DB에 저장될 권위 있는 원본이다.
- Record에는 UObject/Actor 포인터가 없으며 외부 Owner ID와 Container ID를 쓴다.
- 활성 Record는 수량이 양수이고 유효한 위치를 가져야 한다.
- 소비·파괴·만료 Record는 수량 0과 Terminal 위치를 가진다.
- Definition 버전이 다르면 자동으로 합치지 않고 마이그레이션을 요구한다.

### Repository 계약

`IRPGItemRepository::Commit`은 하나의 잠금/DB 트랜잭션 안에서 다음을
보장해야 한다.

1. 모든 예상 Revision을 비교한다.
2. 모든 Mutation을 전부 적용하거나 전부 거부한다.
3. 활성 `(Owner, Container, Slot)` 위치의 유일성을 검사한다.
4. 성공한 Request ID와 명령 지문, 결과를 함께 저장한다.
5. 같은 요청의 재시도에는 저장된 결과를 반환한다.

`FRPGInMemoryItemRepository`는 이 계약의 스레드 안전한 참조 구현이다.
백엔드의 `PostgresItemRepository`도 행 잠금, Revision CAS, 활성 위치
유일 인덱스, Request ID advisory lock으로 같은 의미를 유지한다.

### Transaction Service

현재 다음 서버 명령을 구현했다.

- `MoveItem`: 같은 Inventory 컨테이너 안의 슬롯 이동만 허용하고 소유권,
  잠금, 만료, Revision, 목적지 점유 검증
- `TransferStack`: 양쪽 Revision과 Definition 버전, 영속 옵션,
  최대 스택을 검증한 뒤 두 Record를 원자적으로 변경
- `EquipItem`: 장착 가능 슬롯, 단일 수량, 목적 슬롯 점유와 귀속을 검증
- `UnequipItem`: 현재 장착 정책과 인벤토리 목적 슬롯을 검증
- `ConsumeItem`: Definition의 1회 소모량만 차감하고 잔량 0이면 tombstone 생성

동일 Request ID를 다른 명령에 재사용하면 명령 지문이 달라
`IdempotencyConflict`로 거부한다.

## SOLID 적용

- **SRP**: Definition은 저작 데이터, Instance는 세션 상태, Record는 영속
  상태, Repository는 저장, Service는 유스케이스만 담당한다.
- **OCP**: 새 아이템 능력은 Fragment/Policy로 확장한다.
- **LSP**: Fragment는 제한된 State Builder 계약을 지킨다.
- **ISP**: 트랜잭션은 AssetManager나 HTTP Manager 전체가 아니라
  Catalog, Repository, Clock의 작은 인터페이스만 사용한다.
- **DIP**: Transaction Service는 DB, HTTP, UObject 자산 로딩의 구체
  구현에 의존하지 않는다.

## 검증

- UnrealHeaderTool 성공
- 새 아이템 구현과 테스트 번역 단위 컴파일 성공
- `UnrealEditor-Project_RPG.dll` 링크 성공
- `ProjectRPG.Item` 자동화 테스트 17개 성공
  - 기존 Definition/Stack Policy 테스트 3개
  - 이동 멱등성, 원자적 스택, 권한·Revision 충돌 테스트 3개
  - Item V2 JSON `int64` 정밀도와 동일 본문 재시도 테스트 2개
  - 소유자 전용 Inventory Projection 필터, 안정 ID 델타와 Commit 병합 테스트 3개
  - Legacy Definition 변환과 Native 우선순위 테스트 2개
  - 장착·해제 정책과 원자적·멱등 소모 테스트 2개
  - 비동기 Commit/Projection/최초 효과 순서와 receipt 위조 방어 테스트 2개
- ASP.NET Core 백엔드 경고 없이 빌드 성공
- Item V2 API 메모리 저장소 smoke test 성공
  - CAS, 멱등성, 위치 유일성, 다중 Record 원자성, tombstone 검증
- Dedicated Server Item Backend Gateway 구현 및 런타임 테스트 성공
  - Codec, Transport, Gateway, GameInstance Subsystem 분리
  - 일시 오류 재시도 시 동일 Request ID와 직렬화 본문 재사용
  - 서비스 토큰은 서버 환경 변수에서만 주입

## 다음 마이그레이션 순서

1. **Inventory Projection (구현 완료)**
   - `FFastArraySerializer` 기반 소유자 전용 델타 복제
   - Record를 UI용 읽기 모델로 변환
   - 인증 완료 후 초기 Load 연결, Legacy UI 전환 전까지 병행 운용

2. **Legacy Definition Adapter (코드 구현 완료)**
   - `FItemManifest` 자산을 새 Definition 조회 모델로 노출
   - ItemTag 기반 안정 ID와 Native Definition 우선순위 적용
   - 신규 자산부터 Definition으로 생성하고 기존 UI를 단계적으로 전환

3. **Equip/Use Transaction (도메인 구현 완료)**
   - 장착 슬롯, 귀속, 소모 수량을 별도 서버 명령과 액션 정책으로 검증
   - 일반 Move/Stack 명령으로 Equipment 경계를 우회하지 못하게 제한
   - 성공한 Commit Record를 기존 전체 Projection 캐시에 병합

4. **비동기 Command Orchestrator와 Equipment/GAS Reconciler (구현 완료)**
   - Dedicated Server 검증, Item V2 Commit, Projection 반영을 한 흐름으로 연결
   - 최초 Commit 성공에만 소모 효과를 적용하고 장착 Actor/AbilitySet을 멱등 조정

5. **Trade/Mail/Auction Policy**
   - 귀속과 잠금 규칙, 소유권 이전, Escrow와 감사 로그 추가

6. **Dungeon Session Boundary**
   - 서버 인스턴스와 던전 세션 식별자를 백엔드 요청 컨텍스트에 포함
   - 세션이 소유한 캐릭터만 조회·변경하도록 백엔드 권한 정책 강화

7. **Legacy 제거**
   - 모든 자산과 UI 전환 후 Manifest의 생성/UI/스폰 책임 제거

백엔드 Item V2 계약과 운용 방법은
[`Backend/ITEM_API_V2.md`](../Backend/ITEM_API_V2.md)에 정리했다.
Unreal Dedicated Server 연동 방법은
[`Docs/ITEM_BACKEND_GATEWAY.md`](ITEM_BACKEND_GATEWAY.md)에 정리했다.
소유자 전용 읽기 모델과 복제 계약은
[`Docs/ITEM_INVENTORY_PROJECTION.md`](ITEM_INVENTORY_PROJECTION.md)에 정리했다.
Legacy 자산의 Definition 조회 전환은
[`Docs/ITEM_LEGACY_DEFINITION_ADAPTER.md`](ITEM_LEGACY_DEFINITION_ADAPTER.md)에 정리했다.
장착·해제·소모 명령과 Commit 이후 효과 경계는
[`Docs/ITEM_ACTION_TRANSACTIONS.md`](ITEM_ACTION_TRANSACTIONS.md)에 정리했다.
비동기 서버 명령과 장비/GAS 동기화 계약은
[`Docs/ITEM_ASYNC_COMMANDS_AND_EQUIPMENT.md`](ITEM_ASYNC_COMMANDS_AND_EQUIPMENT.md)에 정리했다.

## 의존성 규칙

- `Item/Definition`은 UI, Inventory, Equipment, ObjectManager를 모른다.
- `Item/Persistence`는 HTTP나 특정 DB 구현을 모른다.
- `Item/Transaction`은 Repository와 Catalog 인터페이스만 사용한다.
- UI는 Record를 직접 변경하지 않고 서버 명령의 결과만 표시한다.
- 모든 상태 변경은 서버의 Transaction Service를 통과한다.

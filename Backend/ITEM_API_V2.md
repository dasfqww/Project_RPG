# MMORPG Item API V2

## 목적

기존 `/api/loadInventory`, `/api/saveInventory`는 이전 클라이언트 호환을
위해 유지한다. 새 API는 아이템 하나를 안정 ID와 Revision으로 관리하고,
여러 아이템 변경과 멱등성 영수증을 하나의 DB 트랜잭션에 기록한다.

## 권한

- 조회: Dedicated Server 또는 해당 Character/Account 소유 플레이어
- Terminal 이력 포함 조회: Dedicated Server 전용
- Commit과 영수증 조회: Dedicated Server 전용

Dedicated Server는 할당된 서버 ID와 Dungeon Session에 결합된 단기 토큰을
사용한다. 일반 아이템 Commit은 게임 서버 전용이며, 던전 보상은 아래의 세션 범위
정산 API를 사용한다.

## 엔드포인트

```text
GET  /api/items?ownerType=Character&ownerId={uuid}&includeTerminal=false&limit=200
GET  /api/items/{itemId}
GET  /api/item-transactions/{requestId}
POST /api/item-transactions/commit
```

`commit` 요청 예:

```json
{
  "requestId": "032c94bb-0a68-435e-b6d2-c8d384271d72",
  "operation": "MoveItem",
  "commandFingerprint": "move|item-id|inventory-a|1|2",
  "actor": {
    "type": "Character",
    "ownerId": "ebf16a38-923f-445d-906d-9fc0ea8bb5df"
  },
  "affectedQuantity": 0,
  "mutations": [
    {
      "expectedRevision": 4,
      "newRecord": {
        "definitionType": "RPGItemDefinition",
        "definitionName": "Potion.Health.Small",
        "definitionVersion": 1,
        "owner": {
          "type": "Character",
          "ownerId": "ebf16a38-923f-445d-906d-9fc0ea8bb5df"
        },
        "location": {
          "containerType": "Inventory",
          "containerId": "ebf16a38-923f-445d-906d-9fc0ea8bb5df",
          "slotIndex": 2
        },
        "state": {
          "instanceId": "91887703-0355-4bc6-949e-a90e275d262f",
          "generationSeed": 104729,
          "quantity": 10,
          "instanceTags": [],
          "statValues": []
        },
        "revision": 4,
        "lifecycleState": "Active",
        "metadata": {
          "bindState": "Unbound",
          "durability": {
            "current": 0,
            "maximum": 0
          },
          "expiresAtUtc": null,
          "creationSource": "Item.Source.Drop",
          "isLocked": false
        }
      }
    }
  ]
}
```

요청의 `newRecord.revision`은 `expectedRevision`과 같아야 한다. Commit이
성공하면 저장소가 Revision을 1 증가시켜 응답과 영수증에 기록한다.

## 보장하는 불변식

1. 요청에 포함된 모든 Expected Revision을 비교한다.
2. 모든 Mutation을 함께 성공시키거나 전부 롤백한다.
3. 활성 `(Owner, Container, Slot)`에는 아이템 하나만 존재한다.
4. 같은 Request ID와 같은 명령은 원래 영수증을 반환한다.
5. 같은 Request ID를 다른 명령 지문에 재사용하면 `IdempotencyConflict`다.
6. 완전히 소비된 아이템은 삭제하지 않고 Terminal tombstone으로 남긴다.

## 던전 아이템 보상

던전 클리어 아이템은 일반 Commit을 파티원별로 직접 호출하지 않는다.
`POST /api/dungeon-sessions/{sessionId}/settle-rewards`의 `itemRewards`에 정적 정의,
수량, 귀속, 내구도, 태그와 롤 스탯을 넣는다. 백엔드는 각 파티원에 대해 결정적
Instance ID와 `Mail/DungeonReward.{sessionId}` 위치를 만들고, 파티 전체 재화와
아이템을 같은 트랜잭션에 저장한다. 아이템 ID·위치·검증 충돌이 발생하면 같은 정산의
재화 변경도 전부 롤백된다.

응답 상태와 HTTP 상태:

| 상태 | HTTP |
|---|---:|
| `Committed`, `AlreadyCommitted` | 200 |
| `InvalidRequest`, `ValidationFailed` | 400 |
| `NotFound` | 404 |
| `IdempotencyConflict`, `RevisionConflict`, `LocationConflict` | 409 |
| `InternalError` | 500 |

## 저장소 구현

- `InMemoryItemRepository`: 로컬 개발과 API 계약 검증용
- `PostgresItemRepository`: 행 잠금, Revision CAS, 위치 유일 인덱스,
  Request ID advisory lock, 영수증 JSONB를 사용하는 운영 구현
- `item_records`: 권위 있는 아이템 원본
- `item_transaction_receipts`: 재시도 결과와 감사 정보

## 검증

백엔드 실행 후 다음 스크립트로 메모리 저장소의 API 계약을 검사한다.

```powershell
Backend/item-smoke-test.ps1 `
    -BaseUrl http://127.0.0.1:3000 `
    -AdminToken $env:PROJECT_RPG_BACKEND_ADMIN_TOKEN
```

검사 범위는 생성, Revision CAS, 멱등 재시도, Request ID 충돌, 위치 충돌
롤백, 두 Record 원자적 변경, 소유자 조회, Terminal tombstone이다.

# Server-authoritative roster and currency API

The economy API owns account-, roster-, and character-scoped balances. A game
server submits a character context and currency deltas; it never selects the
wallet owner. The backend resolves ownership from each currency definition.

```text
Steam account
  -> roster (world: main)
       -> roster currencies
       -> character A -> character currencies
       -> character B -> character currencies
```

## Currency definitions

Definitions are managed with the administrator token. A definition's
scope is immutable after creation because changing it would silently move the
meaning of existing balances.

```http
PUT /api/economy/currency-definitions/RosterGold
Authorization: Bearer {administrator-token}
Content-Type: application/json

{
  "currencyCode": "RosterGold",
  "displayName": "Gold",
  "scope": "Roster",
  "maxBalance": 1000000000,
  "enabled": true
}
```

Valid scopes are `Account`, `Roster`, and `Character`.
`maxBalance` cannot be lowered below any balance already stored for that
currency. Such an update returns `409 Conflict`, preserving wallet invariants.

## Wallet projection

Players may read only wallets belonging to their Steam account. A dedicated
game server may read only a character that belongs to its active dungeon
session.

```http
GET /api/economy/wallets/{characterId}?dungeonSessionId={session-id}
Authorization: Bearer {player-or-session-game-server-token}
```

The response combines all three ownership scopes and includes zero balances for
enabled definitions that have never been changed.

## Atomic transactions

Only the dedicated game server assigned to the character's active dungeon
session may change balances.

```http
POST /api/economy/transactions/commit
Authorization: Bearer {session-game-server-token}
Content-Type: application/json

{
  "requestId": "b6a28e41-bf9f-4a65-87d5-8c7d9ab5c512",
  "characterId": "aef6f8aa-9c80-4fc5-9b65-6f9120955392",
  "dungeonSessionId": "49ed9f20-174b-48d5-86d5-4c89a0df1421",
  "operation": "DungeonReward",
  "commandFingerprint": "dungeon:crypt:session-42:clear:character-a",
  "reason": "Dungeon.Crypt.Clear",
  "changes": [
    { "currencyCode": "RosterGold", "delta": 500 },
    { "currencyCode": "CharacterToken", "delta": 2 }
  ]
}
```

All changes commit or fail together. Negative deltas cannot take a balance below
zero, and positive deltas cannot exceed the definition's maximum. Replaying the
same `requestId` and command returns `AlreadyCommitted`; reusing the ID for a
different command returns `IdempotencyConflict`.

Dungeon clear rewards do not use one client-side commit per party member. The
assigned game server submits one session-bound command:

```http
POST /api/dungeon-sessions/{sessionId}/settle-rewards
Authorization: Bearer {session-game-server-token}

{
  "serverId": "instance-42",
  "rewardVersion": "crypt_clear_v3",
  "changes": [
    { "currencyCode": "RosterGold", "delta": 500 }
  ],
  "itemRewards": [
    {
      "definitionType": "Consumable",
      "definitionName": "Potion.Raid",
      "definitionVersion": 1,
      "quantity": 2,
      "bindState": "CharacterBound",
      "durabilityCurrent": 0,
      "durabilityMaximum": 0,
      "instanceTags": ["Reward.Dungeon"],
      "statValues": [
        { "statTag": "Item.Power", "value": 1.5 }
      ]
    }
  ]
}
```

The backend persists the job, changes the session to `SettlementPending`, and
atomically applies the party-wide currency and item batch. Item grants are
delivered to a deterministic per-session `Mail` container, avoiding inventory
capacity races. A currency or item failure rolls the complete batch back. The
worker then transitions the session to `Cleared`; permanent failures transition
it to `Failed`. Direct
`finish(Cleared)` calls are rejected.

## Verification

Start the backend in Development with
`PROJECT_RPG_BACKEND_ADMIN_TOKEN` configured, then run:

```powershell
Backend/economy-smoke-test.ps1
```

The economy test covers roster assignment, all ownership scopes, authorization,
atomic rollback, and idempotent replay. `Backend/smoke-test.ps1` additionally
checks successful party item delivery and forced item-conflict rollback across
both currencies and items.

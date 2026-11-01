# PostgreSQL integration test

This test runs the backend against PostgreSQL, executes the dungeon/session,
economy, and Item V2 smoke suites, restarts the backend process, and verifies
that the following state is still available:

- player authentication session;
- account, roster, and character records;
- dungeon completion state;
- atomic currency reward;
- item reward delivered to the character mail container.
- security telemetry events and their session aggregate;
- production `RosterGold` definition;
- the exact Easy, Normal, Hard, and Hell reward bundles authored for UE.

The suites use run-specific Steam IDs, server IDs, and currency codes, so they
can be repeated safely against one persistent development database. The main
suite also forces an item delivery conflict and verifies that the currency
portion of the same dungeon settlement is rolled back.

`dungeon-reward-smoke-test.ps1` seeds `RosterGold`, settles all four configured
PvE reward versions, and checks their gold, potion, and helm deliveries. The
PostgreSQL integration runner repeats those checks after restarting the backend.

`security-telemetry-smoke-test.ps1` verifies game-server/session membership,
idempotent replay, immutable-ID conflicts, aggregate queries, and administrator
access after the dungeon ends. The integration runner then verifies the same
events after restarting the PostgreSQL-backed process.

## Start the development database

Docker or another Compose-compatible runtime is required only for this step.

```powershell
docker compose -f Backend/compose.postgres.yml up -d --wait
```

The development database listens only on `127.0.0.1:54329`. Its checked-in
password is intentionally local-development-only and must not be reused in
another environment.

The backend exposes `/health/live` for process liveness and `/health/ready` for
traffic readiness. The readiness endpoint performs a bounded PostgreSQL query
and returns HTTP 503 with a stable failure code when storage is unavailable.
The legacy `/health` endpoint remains available as a liveness alias.

Readiness checks share at most one in-flight PostgreSQL operation. This keeps a
stalled database from consuming one connection per health request. HTTP callers
receive a bounded response while the driver finishes cancelling and disposing
the underlying operation, and later checks recover without restarting the
backend after PostgreSQL becomes available again.

## Run the integration test

```powershell
Backend/postgres-integration-test.ps1
```

## Run isolated abandonment and membership snapshot tests

Set `PROJECT_RPG_ABANDON_TEST_POSTGRES` to the loopback development connection
string, then run:

```powershell
dotnet run --project Backend/ProjectRpg.Backend.DungeonAbandonmentTests/ProjectRpg.Backend.DungeonAbandonmentTests.csproj -c Release -- --require-postgres
```

This requires actual PostgreSQL instead of silently skipping it. Abandonment
and membership suites use separate generated, ownership-checked schemas so a
membership test's `ClaimNext` cannot consume an earlier suite's pending reward.
Only these schemas are removed; public player/reward data and the volume remain.
It checks immutable opt-out receipts, consistent reads under the shared session
row lock, consumed-ticket races, read-only closed-session grace, natural-expiry
rejection and scoped HTTP authentication. It does not run UE character ejection
or real Steam. Evidence is recorded in [the abandonment contract](../Docs/DUNGEON_ABANDONMENT.md).

## Run concurrent schema initialization tests

With the development database running, execute:

```powershell
dotnet run --project Backend/ProjectRpg.Backend.SchemaTests/ProjectRpg.Backend.SchemaTests.csproj
```

This suite creates isolated, run-specific test schemas. It checks separate
processes, four actual web hosts, repeated initialization, legacy upgrades,
SQL failure, cancellation, timeout, rollback, and recovery. Only the schemas
it created are removed; public player/reward data and the database are retained.
Remote hosts are refused. See [the initialization contract and evidence](POSTGRES_SCHEMA_INITIALIZATION.md).

## Run storage health fault tests

With the development database running, execute:

```powershell
dotnet run --project `
  Backend/ProjectRpg.Backend.HealthTests/ProjectRpg.Backend.HealthTests.csproj
```

The test starts an ephemeral loopback TCP proxy and verifies connection loss,
stalled queries, stalled connection handshakes, connection-pool exhaustion,
concurrent health requests, caller cancellation, and recovery. It never stops
or modifies the PostgreSQL container or its stored data.

To run only the configured dungeon rewards against an already running local
backend:

```powershell
Backend/dungeon-reward-smoke-test.ps1 `
  -BaseUrl http://127.0.0.1:3000 `
  -AdminToken $env:PROJECT_RPG_BACKEND_ADMIN_TOKEN
```

The configured reward checks cover all four difficulties with both legacy
mail-only and inventory-first payloads (eight cases), including restart persistence.
Use `-InventoryFirst` with `dungeon-reward-smoke-test.ps1` to check the new mode
against a running backend. `-KeepArtifacts` retains the integration build/logs.
These checks also read Pending/Ready reward snapshots using the session-bound
game-server credential. General wallet/session reads, POST, other sessions,
non-members, and player-token snapshot access remain forbidden after settlement.
The token stays restricted during the configured post-session grace; it is not
restored to normal game-server authority. See
[live reward refresh](../Docs/DUNGEON_REWARD_LIVE_REFRESH.md).

Storage-level reward delivery and concurrency tests:

```powershell
$env:PROJECT_RPG_REWARD_TEST_POSTGRES = $env:POSTGRES_CONNECTION_STRING
dotnet run --project Backend/ProjectRpg.Backend.RewardDeliveryTests/ProjectRpg.Backend.RewardDeliveryTests.csproj
```

With no test connection string, only the in-memory suite runs. Use a development
database: the PostgreSQL suite adds isolated test records and applies the existing schema.

To use an existing PostgreSQL instance for the full HTTP integration:

```powershell
Backend/postgres-integration-test.ps1 `
  -ConnectionString $env:POSTGRES_CONNECTION_STRING
```

The script does not drop or recreate the database. Every run uses unique Steam
IDs, server IDs, and currency codes so it is safe to repeat against the same
development database. It builds into a temporary directory and starts the
backend on `http://127.0.0.1:3010` by default.

## Stop the development database

```powershell
docker compose -f Backend/compose.postgres.yml down
```

Add `--volumes` only when the local development data should be permanently
deleted.

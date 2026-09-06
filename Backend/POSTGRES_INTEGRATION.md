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

## Run the integration test

```powershell
Backend/postgres-integration-test.ps1
```

To run only the configured dungeon rewards against an already running local
backend:

```powershell
Backend/dungeon-reward-smoke-test.ps1 `
  -BaseUrl http://127.0.0.1:3000 `
  -AdminToken $env:PROJECT_RPG_BACKEND_ADMIN_TOKEN
```

To use an existing PostgreSQL instance:

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

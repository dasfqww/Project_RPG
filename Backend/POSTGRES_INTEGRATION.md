# PostgreSQL integration test

This test runs the backend against PostgreSQL, executes the full API smoke
suite, restarts the backend process, and verifies that the following state is
still available:

- player authentication session;
- account, roster, and character records;
- dungeon completion state;
- atomic currency reward;
- item reward delivered to the character mail container.

The full smoke suite also forces an item delivery conflict and verifies that
the currency portion of the same dungeon settlement is rolled back.

## Start the development database

Docker or another Compose-compatible runtime is required only for this step.

```powershell
docker compose -f Backend/compose.postgres.yml up -d --wait
```

The development database listens only on `127.0.0.1:54329`. Its checked-in
password is intentionally local-development-only and must not be reused in
another environment.

## Run the integration test

```powershell
Backend/postgres-integration-test.ps1
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

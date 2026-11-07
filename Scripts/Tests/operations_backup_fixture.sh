#!/usr/bin/env bash
set -euo pipefail
trap 'echo "Native backup fixture failed at line $LINENO"' ERR
umask 077
mkdir -p /verify/results /verify/state /backup-repo
pgbackrest --version > /verify/version.txt
cat > /verify/pgbackrest.conf <<EOF
[global]
repo1-path=/backup-repo
repo1-retention-full=2
repo1-cipher-type=aes-256-cbc
repo1-cipher-pass=$(head -c 32 /dev/urandom | base64)
start-fast=y
process-max=1
archive-timeout=30
log-level-console=off
log-level-file=off
[fixture]
pg1-path=/var/lib/postgresql/data
pg1-user=postgres
EOF
export PGDATABASE=ops_test PGUSER=postgres
psql -X -q -v ON_ERROR_STOP=1 <<'SQL'
CREATE ROLE backup_reader LOGIN;
GRANT pg_monitor TO backup_reader;
GRANT EXECUTE ON FUNCTION pg_control_system(), pg_ls_archive_statusdir() TO backup_reader;
ALTER ROLE backup_reader SET default_transaction_read_only=on;
CREATE TABLE backup_fixture(id integer PRIMARY KEY, value text);
INSERT INTO backup_fixture VALUES (1,'original');
SQL
pgbackrest --config=/verify/pgbackrest.conf --stanza=fixture stanza-create
export RPG_BACKUP_ENVIRONMENT=OpsTest RPG_BACKUP_SERVICE=backend RPG_BACKUP_STANZA=fixture
export RPG_BACKUP_CONFIG=/verify/pgbackrest.conf RPG_BACKUP_STATE_DIRECTORY=/verify/state
adapter=/verify/backup_status.py
python3 /verify/test_backup_status.py
python3 "$adapter" backup --type full
sleep 2
python3 "$adapter" backup --type full
psql -X -q -v ON_ERROR_STOP=1 -c "INSERT INTO backup_fixture VALUES (2,'incremental');"
sleep 2
python3 "$adapter" backup --type incr
export PGUSER=backup_reader
python3 "$adapter" collect
cp /verify/state/backup-status.json /verify/results/healthy.json
before_rows=$(PGUSER=postgres psql -X -q -A -t -v ON_ERROR_STOP=1 -c 'SELECT count(*) FROM backup_fixture')
if psql -X -q -v ON_ERROR_STOP=1 -c "INSERT INTO backup_fixture VALUES (99,'forbidden');" >/dev/null 2>&1; then exit 1; fi
python3 - <<'PY'
import json
s=json.load(open('/verify/results/healthy.json'))
assert s['collectionSucceeded'] and s['repositoryHealthy'] and s['fullChains'] >= 2
assert s['lastFullAtUtc'] and s['lastIncrementalAtUtc'] and s['archiveConfigured']
assert not s['fullBackupFailed'] and not s['incrementalBackupFailed']
PY
# A failed scheduled full must survive collection and an unrelated successful incremental.
cp /verify/pgbackrest.conf /verify/pgbackrest.good
sed -i 's@repo1-path=/backup-repo@repo1-path=/missing-repo@' /verify/pgbackrest.conf
if python3 "$adapter" backup --type full; then exit 1; fi
mv /verify/pgbackrest.good /verify/pgbackrest.conf
python3 "$adapter" collect
cp /verify/state/backup-status.json /verify/results/failed.json
python3 - <<'PY'
import json
assert json.load(open('/verify/results/failed.json'))['fullBackupFailed']
PY
# Inject a real PostgreSQL archive transport failure while repo metadata stays readable.
failed_before=$(PGUSER=postgres psql -X -q -A -t -c 'SELECT failed_count FROM pg_stat_archiver')
touch /verify/archive-disabled
PGUSER=postgres psql -X -q -v ON_ERROR_STOP=1 -c 'SELECT pg_switch_wal();' >/dev/null
observed_failure=false
for attempt in $(seq 1 30); do
    failed_now=$(PGUSER=postgres psql -X -q -A -t -c 'SELECT failed_count FROM pg_stat_archiver')
    if [ "$failed_now" -gt "$failed_before" ]; then observed_failure=true; break; fi
    sleep 1
done
test "$observed_failure" = true
python3 "$adapter" collect
cp /verify/state/backup-status.json /verify/results/archive-failed.json
python3 - <<'PY'
import json
s=json.load(open('/verify/results/archive-failed.json'))
assert s['collectionSucceeded'] and s['lastArchiveFailureAtUtc'] and s['pendingWalCount'] > 0 and s['oldestPendingWalAtUtc']
PY
after_rows=$(PGUSER=postgres psql -X -q -A -t -v ON_ERROR_STOP=1 -c 'SELECT count(*) FROM backup_fixture')
test "$before_rows" = 2 && test "$after_rows" = "$before_rows"
# A collection transport failure publishes Unknown, not a fresh copy of old health.
if PGHOST=127.0.0.1 PGPORT=1 python3 "$adapter" collect; then exit 1; fi
cp /verify/state/backup-status.json /verify/results/unknown.json
python3 - <<'PY'
import json
assert not json.load(open('/verify/results/unknown.json'))['collectionSucceeded']
version=open('/verify/version.txt').read().strip()
json.dump(dict(passed=True, pgBackRestVersion=version, fullBackups=2, incrementalBackups=1,
    encryptedRepository=True, readOnlyCollectorRole=True, writesDenied=True, applicationRowsUnchanged=True,
    failedJobReceipt=True, realWalFailureAndQueue=True, transportUnknown=True),
    open('/verify/results/result.json','w'))
PY
echo 'PASS native pgBackRest: encrypted full2/incr1, read-only PG collector, failed job receipt and collection Unknown.'

#!/usr/bin/env python3
"""Owned PostgreSQL17 integration fixture. Never connects to an existing deployment."""
import copy
import datetime as dt
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import time

sys.path.insert(0, '/verify')
import restore_rehearsal as r

ROOT, RESULTS, PROOFS = Path('/verify'), Path('/verify/results'), Path('/verify/proofs')
RESULTS.mkdir(exist_ok=True); PROOFS.mkdir(mode=0o700, exist_ok=True)
PHASE = 'setup'


def admin(sql):
    result = subprocess.run(['/usr/bin/psql', '-X', '-q', '-A', '-t', '-U', 'postgres', '-d', 'ops_test', '-v', 'ON_ERROR_STOP=1', '-c', sql],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True, timeout=60)
    return result.stdout.decode().strip()


def run(*args):
    subprocess.run(list(args), check=True, timeout=120)


def repository_hash():
    hashes = [(str(p.relative_to('/backup-repo')), hashlib.sha256(p.read_bytes()).hexdigest())
              for p in sorted(Path('/backup-repo').rglob('*')) if p.is_file()]
    return r.digest(hashes)


def apply_schema():
    directory = ROOT / 'Database'
    files = [directory / 'schema.sql', *sorted((directory / 'Migrations').glob('*.sql'))]
    admin("""CREATE TABLE rpg_schema_migrations(version INTEGER PRIMARY KEY CHECK(version>0),
        name VARCHAR(64) NOT NULL UNIQUE,sha256 VARCHAR(64) NOT NULL CHECK(sha256 ~ '^[0-9a-f]{64}$'),
        applied_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP);""")
    for p in files:
        text = p.read_text().replace('\r\n', '\n').replace('\r', '\n')
        version, name = (1, 'baseline') if p.name == 'schema.sql' else (int(p.name[:4]), p.stem[5:])
        sha = hashlib.sha256(text.encode()).hexdigest()
        admin("BEGIN;" + text + f"\nINSERT INTO rpg_schema_migrations(version,name,sha256) VALUES({version},'{name}','{sha}');COMMIT;")
    return len(files)


def seed():
    admin("""
INSERT INTO accounts VALUES('76561198011111111','76561198011111111',false,false,now(),now());
INSERT INTO rosters VALUES('10000000-0000-0000-0000-000000000001','76561198011111111','main',now(),now());
INSERT INTO characters VALUES('20000000-0000-0000-0000-000000000001','10000000-0000-0000-0000-000000000001','76561198011111111','RehearsalBefore',now(),now());
INSERT INTO currency_definitions VALUES('RESTORE_COIN','rehearsal','Character',9223372036854775807,true,now(),now());
INSERT INTO currency_balances VALUES('Character','20000000-0000-0000-0000-000000000001','RESTORE_COIN',9007199254740993,1,now());
INSERT INTO currency_transaction_receipts VALUES('30000000-0000-0000-0000-000000000001','20000000-0000-0000-0000-000000000001','FixtureGrant','fixture-fingerprint','rehearsal','[]',now());
INSERT INTO item_records(item_id,definition_type,definition_name,definition_version,owner_type,owner_id,container_type,container_id,
 slot_index,generation_seed,quantity,revision,lifecycle_state,bind_state,created_at,updated_at)
 VALUES('40000000-0000-0000-0000-000000000001','Item','Ore',1,'Character','20000000-0000-0000-0000-000000000001',
 'Inventory','bag',0,123,1,1,'Active','Unbound',now(),now()),
 ('40000000-0000-0000-0000-000000000002','Item','MailOre',1,'Character','20000000-0000-0000-0000-000000000001',
 'Mail','mailbox',0,456,2,1,'Active','Unbound',now(),now());
INSERT INTO item_transaction_receipts VALUES('50000000-0000-0000-0000-000000000001','FixtureGrant','fixture-fingerprint','Character',
 '20000000-0000-0000-0000-000000000001',3,'[]',now());
INSERT INTO dungeon_sessions(dungeon_session_id,dungeon_id,difficulty,state,server_id,created_at,updated_at,expires_at)
 VALUES('60000000-0000-0000-0000-000000000001','training','Normal','SettlementPending','fixture',now(),now(),now()+interval '1 hour');
INSERT INTO dungeon_reward_settlements(dungeon_session_id,server_id,reward_version,command_fingerprint,state,character_ids,currency_changes,
 attempt_count,next_attempt_at,worker_id,processing_expires_at,created_at,updated_at)
 VALUES('60000000-0000-0000-0000-000000000001','fixture','fixture',repeat('a',64),'Processing',
 '["20000000-0000-0000-0000-000000000001"]','[]',1,now(),'fixture-worker',now()+interval '1 hour',now(),now());
INSERT INTO auth_sessions VALUES(repeat('b',64),'76561198011111111',now()+interval '1 hour');
INSERT INTO game_join_tickets VALUES(repeat('c',64),'76561198011111111','20000000-0000-0000-0000-000000000001',
 '60000000-0000-0000-0000-000000000001',now()+interval '1 hour');
INSERT INTO game_server_credentials VALUES(repeat('d',64),'fixture','60000000-0000-0000-0000-000000000001',now()+interval '1 hour');
INSERT INTO ops_actions(request_id,operation,input_hash,canonical_input,operator_id,target_scope,reason,committed_at,result_json,origin_http_request_id)
 VALUES('70000000-0000-0000-0000-000000000001','Ops.ServiceControl.Change.v1',decode(repeat('a',64),'hex'),'{}',
 'fixture','Service','recovery rehearsal fixture',now(),'{}','80000000-0000-0000-0000-000000000001');
UPDATE ops_service_control SET revision=1,mode='Drain',updated_at=now(),action_request_id='70000000-0000-0000-0000-000000000001';
CREATE ROLE restore_reader LOGIN;
GRANT pg_monitor TO restore_reader;
GRANT SELECT ON ALL TABLES IN SCHEMA public TO restore_reader;
GRANT EXECUTE ON FUNCTION pg_control_system(),pg_get_wal_replay_pause_state() TO restore_reader;
ALTER ROLE restore_reader SET default_transaction_read_only=on;
""")


def capture(name):
    path = PROOFS / name
    run('python3', '/verify/restore_rehearsal.py', 'capture', '--source-quiesced', '--checkpoint', str(path))
    return path


def latest_label():
    return r.execute(['/usr/bin/pgbackrest', '--config=/verify/pgbackrest.conf', '--stanza=fixture', '--output=json', 'info'])[0]['backup'][-1]['label']


def main():
    global PHASE
    PHASE = 'schema_and_seed'
    versions = apply_schema(); seed()
    config = ROOT / 'pgbackrest.conf'
    config.write_text("[global]\nrepo1-path=/backup-repo\nrepo1-retention-full=2\nrepo1-cipher-type=aes-256-cbc\nrepo1-cipher-pass="
        + os.urandom(32).hex() + "\nstart-fast=y\nprocess-max=1\narchive-timeout=30\nlog-level-console=off\nlog-level-file=info\nlog-path=/verify/backrest-logs\n"
        "[fixture]\npg1-path=/var/lib/postgresql/data\npg1-user=postgres\n")
    config.chmod(0o600); (ROOT / 'backrest-logs').mkdir(mode=0o700)
    env = dict(ENVIRONMENT='OpsTest',SERVICE='backend',STANZA='fixture',DATABASE='ops_test',USER='restore_reader',
        SOURCE_DATA_DIRECTORY='/var/lib/postgresql/data',SCRATCH_DIRECTORY='/restore-work',PROOF_DIRECTORY=str(PROOFS),
        SOURCE_SOCKET_DIRECTORY='/var/run/postgresql',CONFIG=str(config),PG_BIN_DIRECTORY='/usr/lib/postgresql/17/bin',TIMEOUT_SECONDS='120')
    os.environ.update({'RPG_RESTORE_' + k: v for k, v in env.items()})
    run('python3','/verify/test_restore_rehearsal.py')
    run('python3','/verify/test_backup_status.py')
    run('pgbackrest','--config='+str(config),'--stanza=fixture','stanza-create')
    PHASE = 'full_backup'
    run('pgbackrest','--config='+str(config),'--stanza=fixture','--type=full','backup')
    full = latest_label(); base = capture('base-point.json')
    PHASE = 'incremental_and_point'
    admin("BEGIN; UPDATE currency_balances SET balance=balance+5,revision=revision+1; UPDATE characters SET name='IncludedBeforePoint';"
          "INSERT INTO currency_transaction_receipts VALUES('30000000-0000-0000-0000-000000000002','20000000-0000-0000-0000-000000000001',"
          "'FixtureDelta','fixture-delta','rehearsal','[]',now()); COMMIT;")
    time.sleep(2)
    run('pgbackrest','--config='+str(config),'--stanza=fixture','--type=incr','backup')
    incremental = latest_label(); point = capture('time-point.json')
    time.sleep(1)
    admin("BEGIN; UPDATE currency_balances SET balance=1,revision=revision+1; UPDATE characters SET name='ExcludedAfterPoint'; DELETE FROM auth_sessions; COMMIT;")
    admin('SELECT pg_switch_wal();')
    for _ in range(40):
        if admin("SELECT count(*) FROM pg_ls_archive_statusdir() WHERE name LIKE '%.ready';") == '0':
            break
        time.sleep(1)
    else:
        raise ValueError('Archive did not drain')
    s = r.Settings()
    source = r.DatabaseVerifier(s,s.source_socket,5432)
    before = source.snapshot(); repo_before = repository_hash()
    for p in Path('/backup-repo').rglob('*'):
        p.chmod(0o500 if p.is_dir() else 0o400)
    Path('/backup-repo').chmod(0o500)
    PHASE = 'positive_restores'
    engine = r.RecoveryRehearsal(s)
    positives = [engine.run(base,full,'immediate'),engine.run(point,incremental,'immediate'),engine.run(point,full,'time')]
    assert all(x['passed'] and x['cleanupComplete'] for x in positives)
    PHASE = 'data_mismatch_rejected'
    wrong = r.read_checkpoint(point); wrong['snapshot']['tables']['accounts']['count'] += 1
    wrong_path = PROOFS / 'wrong-point.json'; r.atomic(wrong_path,wrong)
    mismatch = engine.run(wrong_path,full,'time'); assert not mismatch['passed'] and mismatch['cleanupComplete']
    PHASE = 'wrong_key_rejected'
    bad_config = ROOT / 'wrong-key.conf'
    bad_config.write_text(config.read_text().replace(next(l for l in config.read_text().splitlines() if l.startswith('repo1-cipher-pass=')),
        'repo1-cipher-pass=wrong-fixture-key'))
    bad_config.chmod(0o600)
    bad_settings = copy.copy(s); bad_settings.config = bad_config
    bad_key = r.RecoveryRehearsal(bad_settings).run(point,full,'time')
    assert not bad_key['passed'] and bad_key['cleanupComplete'] and bad_key['failurePhase']=='restore'
    PHASE = 'unreachable_target_rejected'
    future = r.read_checkpoint(point)
    timestamp = dt.datetime.fromisoformat(future['snapshot']['observedAtUtc'])+dt.timedelta(days=1)
    future['snapshot']['observedAtUtc'] = timestamp.isoformat()
    future_path=PROOFS/'future-point.json'; r.atomic(future_path,future)
    unreachable = engine.run(future_path,full,'time'); assert not unreachable['passed'] and unreachable['cleanupComplete']
    PHASE = 'missing_wal_rejected'
    broken_repo=ROOT/'broken-repo'; shutil.copytree('/backup-repo',broken_repo)
    for p in [broken_repo,*broken_repo.rglob('*')]:
        p.chmod(0o700 if p.is_dir() else 0o600)
    removed=0
    for p in (broken_repo/'archive').rglob('*'):
        if p.is_file() and re.match(r'^[0-9A-F]{24}-',p.name):
            p.unlink(); removed+=1
    assert removed>0
    wal_config=ROOT/'missing-wal.conf'; wal_config.write_text(config.read_text().replace('repo1-path=/backup-repo','repo1-path='+str(broken_repo)))
    wal_config.chmod(0o600)
    wal_settings=copy.copy(s); wal_settings.config=wal_config
    missing_wal=r.RecoveryRehearsal(wal_settings).run(point,full,'time')
    assert not missing_wal['passed'] and missing_wal['cleanupComplete']
    PHASE = 'source_and_repository_unchanged'
    after = source.snapshot()
    assert before['tables']==after['tables'] and before['migrations']==after['migrations'] and repo_before==repository_hash()
    assert not list(Path('/restore-work').iterdir())
    result = dict(passed=True,postgresVersion=before['version'],pgBackRestVersion=subprocess.check_output(['pgbackrest','--version']).decode().strip(),
        migrations=versions,tables=len(before['tables']),encryptedRepository=True,sourcePublicDataUnchanged=True,repositoryBytesUnchanged=True,
        fullRestore=True,incrementalRestore=True,pitrBeforeExcludedTransaction=True,dataMismatchRejected=True,wrongKeyRejected=True,
        unreachableTargetRejected=True,missingWalRejected=True,privatePausedClones=True,cleanupComplete=True,sourceCurrencyFixtureUsesInt64=True,
        positiveElapsedSeconds=[x['elapsedSeconds'] for x in positives],deletedCloneBytes=sum(x['deletedBytes'] for x in [*positives,mismatch,bad_key,unreachable,missing_wal]),
        productionRpoRtoValidated=False)
    (RESULTS/'result.json').write_text(json.dumps(result))
    for p in PROOFS.glob('*.json'):
        if len(p.stem)==36:
            shutil.copyfile(p,RESULTS/p.name)
    print('PASS Recovery: full/incr, inclusive time PITR, schema/all-public-data fingerprints, negative recovery guards, source/repo unchanged and cleanup.')


if __name__=='__main__':
    try:
        main()
    except BaseException as error:
        (RESULTS/'result.json').write_text(json.dumps(dict(passed=False,failurePhase=PHASE,errorType=type(error).__name__)))
        for p in PROOFS.iterdir():
            if len(p.stem.split('-postgres.log')[0])==36 or p.suffix=='.log':
                shutil.copyfile(p,RESULTS/p.name)
        print('Recovery integration fixture failed at '+PHASE+'.')
        raise SystemExit(1)

#!/usr/bin/env python3
"""Read-only checkpoint capture and disposable, paused PostgreSQL recovery rehearsal. No live cutover."""
import argparse
import datetime as dt
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import subprocess
import tempfile
import time
import threading
import uuid

from backup_status import atomic, execute, instant, now, reject_links

REQUIRED_TABLES = {'accounts', 'characters', 'item_records', 'currency_balances', 'currency_transaction_receipts',
                   'dungeon_reward_settlements', 'ops_actions', 'ops_service_control', 'rpg_schema_migrations',
                   'auth_sessions', 'game_join_tickets', 'game_server_credentials'}
CATALOG = "SELECT json_agg(tablename ORDER BY tablename) FROM pg_tables WHERE schemaname='public';"


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def read_checkpoint(path):
    with reject_links(path).open('rb') as source:
        raw = source.read(65537)
    if len(raw) > 65536:
        raise ValueError('Checkpoint too large')
    return validate_checkpoint(json.loads(raw))


def validate_checkpoint(value):
    if set(value) != {'contractVersion', 'environment', 'service', 'stanza', 'sourceDataDirectory', 'sourceQuiescedAttested', 'snapshot'}:
        raise ValueError('Invalid checkpoint')
    snapshot = value['snapshot']
    if value['contractVersion'] != 1 or value['sourceQuiescedAttested'] is not True or not isinstance(snapshot, dict):
        raise ValueError('Unsupported checkpoint')
    if set(snapshot) != {'observedAtUtc', 'systemId', 'version', 'dataDirectory', 'inRecovery', 'paused', 'userTablespaces', 'tables', 'migrations'}:
        raise ValueError('Incomplete checkpoint')
    if not REQUIRED_TABLES.issubset(snapshot['tables']) or not snapshot['migrations'] or not re.fullmatch(r'\d{10,20}', snapshot['systemId']):
        raise ValueError('Incomplete game schema')
    if not 170000 <= int(snapshot['version']) < 180000 or snapshot['inRecovery'] or snapshot['paused'] or snapshot['userTablespaces'] != 0:
        raise ValueError('Source must be PostgreSQL17 primary')
    instant(snapshot['observedAtUtc'])
    for table in snapshot['tables'].values():
        if set(table) != {'count', 'sha256'} or type(table['count']) is not int or table['count'] < 0 or not re.fullmatch(r'[0-9a-f]{64}', table['sha256']):
            raise ValueError('Invalid table fingerprint')
    return value


class Settings:
    def __init__(self):
        def read(key):
            return os.environ.get('RPG_RESTORE_' + key, '')
        self.environment, self.service, self.stanza, self.database, self.user = (read(k) for k in ('ENVIRONMENT', 'SERVICE', 'STANZA', 'DATABASE', 'USER'))
        if not all(re.fullmatch(r'[A-Za-z0-9_-]{1,64}', v) for v in (self.environment, self.service, self.stanza, self.database, self.user)):
            raise ValueError('Invalid scope or database identity')
        self.source = reject_links(read('SOURCE_DATA_DIRECTORY')).resolve()
        self.scratch = reject_links(read('SCRATCH_DIRECTORY')).resolve()
        self.proofs = reject_links(read('PROOF_DIRECTORY')).resolve()
        source_socket = Path(read('SOURCE_SOCKET_DIRECTORY'))
        if not source_socket.is_absolute():
            raise ValueError('Absolute source socket directory required')
        # /var/run is a standard Linux alias of /run. Resolve this read-only endpoint, never a writable workspace.
        self.source_socket = source_socket.resolve()
        self.source_port = int(read('SOURCE_PORT') or '5432')
        self.source_passfile = reject_links(read('SOURCE_PASSFILE')) if read('SOURCE_PASSFILE') else None
        if not 1 <= self.source_port <= 65535:
            raise ValueError('Invalid source socket port')
        self.config = reject_links(read('CONFIG'))
        self.pg_bin = reject_links(read('PG_BIN_DIRECTORY'))
        self.backrest = read('PGBACKREST') or '/usr/bin/pgbackrest'
        self.psql = read('PSQL') or '/usr/bin/psql'
        if not all(Path(p).is_absolute() for p in (self.backrest, self.psql)):
            raise ValueError('Absolute executables required')
        for p in (self.scratch, self.proofs):
            if p == Path('/') or p == self.source or p.is_relative_to(self.source) or self.source.is_relative_to(p):
                raise ValueError('Rehearsal paths overlap protected source')
            p.mkdir(mode=0o700, parents=True, exist_ok=True)
            reject_links(p)
            os.chmod(p, 0o700)
        if self.proofs == self.scratch or self.proofs.is_relative_to(self.scratch) or self.scratch.is_relative_to(self.proofs):
            raise ValueError('Proof and scratch paths must be separate')
        self.timeout = int(read('TIMEOUT_SECONDS') or '14400')
        if not 30 <= self.timeout <= 14400:
            raise ValueError('Recovery budget out of bounds')
        self.query_timeout = int(read('QUERY_TIMEOUT_SECONDS') or '120')
        if not 10 <= self.query_timeout <= 3600:
            raise ValueError('Query budget out of bounds')


class DatabaseVerifier:
    def __init__(self, settings, socket, port):
        self.settings, self.socket, self.port = settings, socket, str(port)

    def client(self, sql):
        # Never inherit a service/hostaddr/options that could redirect a clone verification to the source.
        env = {k: v for k, v in os.environ.items() if not k.startswith('PG')}
        milliseconds = (getattr(self.settings, 'query_timeout', 120) - 5) * 1000
        env.update(PGCONNECT_TIMEOUT='3', PGCLIENTENCODING='UTF8', PGPASSFILE='/dev/null',
            PGOPTIONS='-c default_transaction_read_only=on -c statement_timeout=' + str(milliseconds) + ' -c timezone=UTC -c work_mem=16MB')
        s = self.settings
        if Path(self.socket) == getattr(s, 'source_socket', None) and self.port == str(getattr(s, 'source_port', None)) and getattr(s, 'source_passfile', None):
            env['PGPASSFILE'] = str(s.source_passfile)
        return [s.psql, '-X', '-q', '-A', '-t', '-w', '-v', 'ON_ERROR_STOP=1', '-h', str(self.socket), '-p', self.port,
                '-U', s.user, '-d', s.database, '-c', sql], env

    def query(self, sql, timeout=120):
        command, env = self.client(sql)
        return execute(command, timeout=timeout, env=env)

    def paused(self):
        return self.query("SELECT json_build_object('inRecovery',pg_is_in_recovery(),'paused',pg_get_wal_replay_pause_state()='paused',"
                          "'inclusive',current_setting('recovery_target_inclusive')::boolean);", 5)

    def snapshot(self):
        tables = self.query(CATALOG)
        if not isinstance(tables, list) or not REQUIRED_TABLES.issubset(tables) or len(tables) > 256:
            raise ValueError('Unexpected schema')
        statements = []
        for name in tables:
            quoted = '"' + name.replace('"', '""') + '"'
            literal = "'" + name.replace("'", "''") + "'"
            statements.append("SELECT json_build_object('kind','table','name'," + literal + ");\n"
                "SELECT json_build_object('kind','row','data',to_jsonb(t)) FROM public." + quoted
                + " t ORDER BY to_jsonb(t)::text COLLATE \"C\";")
        sql = """BEGIN ISOLATION LEVEL REPEATABLE READ READ ONLY;
SELECT json_build_object('kind','metadata','observedAtUtc',transaction_timestamp(),'systemId',(SELECT system_identifier::text FROM pg_control_system()),
 'version',current_setting('server_version_num')::integer,'dataDirectory',current_setting('data_directory'),
 'inRecovery',pg_is_in_recovery(),'paused',CASE WHEN pg_is_in_recovery() THEN pg_get_wal_replay_pause_state()='paused' ELSE false END,
 'userTablespaces',(SELECT count(*) FROM pg_tablespace WHERE spcname NOT IN ('pg_default','pg_global')),
 'migrations',(SELECT json_agg(json_build_object('version',version,'name',name,'sha256',sha256) ORDER BY version) FROM rpg_schema_migrations));
""" + '\n'.join(statements) + "\nSELECT json_build_object('kind','complete'); COMMIT;"
        return self.stream_snapshot(sql, tables)

    def stream_snapshot(self, sql, tables):
        # Hash row streams instead of materializing whole tables/string_agg in server or client memory.
        command, env = self.client(sql)
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=env, start_new_session=True)
        def abort():
            if process.poll() is None:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
        timer = threading.Timer(getattr(self.settings, 'query_timeout', 120), abort)
        timer.start()
        try:
            records = iter(lambda: process.stdout.readline(1024 * 1024 + 1), b'')
            result = parse_snapshot(records, tables)
            if process.wait(timeout=5) != 0:
                raise ValueError('Snapshot query failed')
            return result
        finally:
            timer.cancel()
            abort()
            process.wait(timeout=5)
            process.stdout.close()


def parse_snapshot(records, tables):
    result, current, accumulator, complete = None, None, None, False
    observed = {}
    for raw in records:
        if len(raw) > 1024 * 1024:
            raise ValueError('Snapshot row exceeded bound')
        value = json.loads(raw)
        kind = value.get('kind')
        if kind == 'metadata' and result is None:
            result = value; del result['kind']
        elif kind == 'table' and result is not None and not complete:
            if current is not None:
                observed[current]['sha256'] = accumulator.hexdigest()
            current = value['name']
            if current not in tables or current in observed:
                raise ValueError('Unexpected or duplicate table')
            observed[current] = dict(count=0,sha256=None)
            accumulator = hashlib.sha256()
        elif kind == 'row' and current is not None and not complete:
            accumulator.update(raw)
            observed[current]['count'] += 1
        elif kind == 'complete' and not complete:
            if current is not None:
                observed[current]['sha256'] = accumulator.hexdigest()
            complete = True
        else:
            raise ValueError('Invalid snapshot stream')
    if not complete or set(observed) != set(tables) or result is None:
        raise ValueError('Incomplete snapshot')
    result['tables'] = observed
    return result


def compare(expected, actual):
    return (actual['inRecovery'] is True and actual['paused'] is True
            and actual['systemId'] == expected['systemId'] and actual['version'] == expected['version']
            and actual['userTablespaces'] == 0
            and actual['migrations'] == expected['migrations'] and actual['tables'] == expected['tables'])


class RecoveryRehearsal:
    def __init__(self, settings):
        self.settings = settings

    def run(self, checkpoint_path, backup_set, mode):
        s = self.settings
        checkpoint = read_checkpoint(checkpoint_path)
        if any(checkpoint[k] != getattr(s, k) for k in ('environment', 'service', 'stanza')) or Path(checkpoint['sourceDataDirectory']) != s.source:
            raise ValueError('Foreign checkpoint')
        if Path(checkpoint['snapshot']['dataDirectory']) != s.source:
            raise ValueError('Checkpoint source path mismatch')
        if not re.fullmatch(r'\d{8}-\d{6}F(?:_\d{8}-\d{6}[ID])?', backup_set) or mode not in ('immediate', 'time'):
            raise ValueError('Invalid recovery target')
        attempt = str(uuid.uuid4())
        result = dict(contractVersion=1, attemptId=attempt, environment=s.environment, service=s.service,
            mode=mode, backupSet=backup_set, checkpointSHA256=digest(checkpoint), startedAtUtc=now(), passed=False,
            phase='restore', cleanupComplete=False, elapsedSeconds=None, withinFourHourGoalOnThisDataset=False,
            productionReady=False, deletedBytes=0)
        root = Path(tempfile.mkdtemp(prefix='rehearsal-', dir=s.scratch))
        data, socket = root / 'data', root / 'socket'
        socket.mkdir(mode=0o700)
        atomic(root / 'owner.json', dict(attemptId=attempt, dataDirectory=str(data)))
        started, clock = False, time.monotonic()
        native_env = {k: v for k, v in os.environ.items() if not k.startswith(('PG', 'PGBACKREST_'))}
        def event(phase):
            result['phase'] = phase
            print(json.dumps(dict(attemptId=attempt, phase=phase)), flush=True)
        try:
            target = instant(checkpoint['snapshot']['observedAtUtc'])
            command = [s.backrest, '--config=' + str(s.config), '--stanza=' + s.stanza, '--repo=1', '--pg1-path=' + str(data),
                '--set=' + backup_set, '--type=' + mode, '--target-action=pause', '--no-link-all', '--no-delta',
                '--reset-link-map', '--reset-tablespace-map',
                '--tablespace-map-all=' + str(root / 'tablespaces'), '--lock-path=' + str(root / 'locks'),
                '--log-path=' + str(root / 'logs'), '--log-level-console=off', 'restore']
            if mode == 'time':
                # recovery_target_time uses PostgreSQL's configuration datetime grammar, not JSON's ISO T/Z form.
                native_target = dt.datetime.fromisoformat(target.replace('Z', '+00:00')).strftime('%Y-%m-%d %H:%M:%S.%f UTC')
                command.insert(-1, '--target=' + native_target)
            event('restore')
            result['requestedTargetUtc'] = target if mode == 'time' else None
            result['budgetSeconds'] = s.timeout
            result['restoreExitCode'] = execute(command, timeout=s.timeout, capture=False, env=native_env, diagnostic=root / 'command.log')
            if result['restoreExitCode'] != 0:
                raise ValueError('Restore command failed')
            for p in data.rglob('*'):
                if p.is_symlink() and not p.resolve().is_relative_to(root):
                    raise ValueError('Restore created an external link')
            # This private socket is owner-only. Do not reuse production peer/password rules on the disposable clone.
            hba = data / 'pg_hba.conf'
            hba.write_text('local "' + s.database + '" "' + s.user + '" trust\nlocal all all reject\n')
            os.chmod(hba, 0o600)
            # An unquoted pg_ctl -o is shell code. Quote every token, including deployment paths.
            options = ['-c', 'data_directory=' + str(data), '-c', 'listen_addresses=', '-c', 'port=55439',
                '-c', 'unix_socket_directories=' + str(socket), '-c', 'unix_socket_permissions=0700',
                '-c', 'primary_conninfo=', '-c', 'primary_slot_name=', '-c', 'archive_mode=off', '-c', 'archive_command=',
                '-c', 'archive_library=', '-c', 'external_pid_file=', '-c', 'ssl=off', '-c', 'logging_collector=off',
                '-c', 'hot_standby=on', '-c', 'recovery_target_inclusive=true', '-c', 'temp_tablespaces=', '-c', 'log_destination=stderr',
                '-c', 'hba_file=' + str(data / 'pg_hba.conf'), '-c', 'ident_file=' + str(data / 'pg_ident.conf')]
            event('start_private_clone')
            started = True  # Startup may succeed even if the wait command fails; always inspect/stop our data directory.
            result['startupExitCode'] = execute([str(s.pg_bin / 'pg_ctl'), '-D', str(data), '-l', str(root / 'postgres.log'), '-w', '-t', '60',
                        '-o', shlex.join(options), 'start'], timeout=65, capture=False, env=native_env, diagnostic=root / 'pgctl.log')
            if result['startupExitCode'] != 0:
                raise ValueError('Private clone startup failed')
            verifier = DatabaseVerifier(s, socket, 55439)
            event('wait_for_target_pause')
            deadline = min(clock + s.timeout, time.monotonic() + 300)
            while time.monotonic() < deadline:
                try:
                    if verifier.paused() == dict(inRecovery=True, paused=True, inclusive=True):
                        break
                except Exception:
                    pass
                time.sleep(1)
            else:
                raise ValueError('Recovery target was not reached')
            event('verify_schema_and_data')
            actual = verifier.snapshot()
            if not compare(checkpoint['snapshot'], actual) or Path(actual['dataDirectory']) != data:
                raise ValueError('Recovered data or identity mismatch')
            result['dataSHA256'] = digest(actual['tables'])
            result['migrationSHA256'] = digest(actual['migrations'])
            result['tableCount'] = len(actual['tables'])
            result['passed'] = time.monotonic() - clock <= s.timeout
            event('verified' if result['passed'] else 'budget_exceeded')
        except (Exception, KeyboardInterrupt):
            result['failurePhase'] = result['phase']
            result['passed'] = False
            event('failed')
        finally:
            try:
                owner = json.loads((root / 'owner.json').read_text())
                if owner != dict(attemptId=attempt, dataDirectory=str(data)) or root.parent != s.scratch or root.is_symlink():
                    raise ValueError('Workspace ownership changed')
                pid_file = data / 'postmaster.pid'
                if started and pid_file.exists():
                    pid = pid_file.read_text().splitlines()
                    if len(pid) < 2 or Path(pid[1]) != data:
                        raise ValueError('Clone PID file does not belong to this workspace')
                    if execute([str(s.pg_bin / 'pg_ctl'), '-D', str(data), '-w', '-t', '30', '-m', 'fast', 'stop'],
                               timeout=35, capture=False, env=native_env) != 0:
                        raise ValueError('Owned clone could not stop')
                if not result['passed']:
                    result['retainedDiagnosticBytes'] = 0
                    # Private, bounded failure evidence. Never publish native logs to a webhook or repository.
                    for log in [root / 'command.log', root / 'pgctl.log', root / 'postgres.log', *sorted((root / 'logs').glob('*.log'))[-1:]]:
                        if log.is_file() and not log.is_symlink():
                            with log.open('rb') as source:
                                source.seek(max(0, log.stat().st_size - 16384))
                                raw = source.read(16384)
                            target = s.proofs / (attempt + '-' + log.name)
                            target.write_bytes(raw)
                            os.chmod(target, 0o600)
                            result['retainedDiagnosticBytes'] += len(raw)
                result['deletedBytes'] = sum(p.stat().st_size for p in root.rglob('*') if p.is_file() and not p.is_symlink())
                # rmtree unlinks symlinks instead of traversing tablespace links.
                shutil.rmtree(root)
                result['cleanupComplete'] = True
            except Exception:
                result['passed'] = False
                result['retainedWorkspace'] = str(root)
            result['elapsedSeconds'] = round(time.monotonic() - clock, 3)
            result['passed'] = result['passed'] and result['elapsedSeconds'] <= s.timeout
            result['withinFourHourGoalOnThisDataset'] = result['passed'] and result['elapsedSeconds'] <= 14400
            result['finishedAtUtc'] = now()
            atomic(s.proofs / (attempt + '.json'), result)
        return result


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest='operation', required=True)
    capture = sub.add_parser('capture')
    capture.add_argument('--source-quiesced', action='store_true', required=True)
    capture.add_argument('--checkpoint', required=True)
    run = sub.add_parser('run')
    run.add_argument('--checkpoint', required=True)
    run.add_argument('--backup-set', required=True)
    run.add_argument('--mode', choices=('immediate', 'time'), required=True)
    args = parser.parse_args()
    phase = 'settings'
    try:
        settings = Settings()
        if args.operation == 'capture':
            phase = 'capture_path'
            path = reject_links(args.checkpoint)
            if path.exists() or path.parent != settings.proofs:
                raise ValueError('Capture requires a new private checkpoint file')
            with reject_links(settings.proofs / 'capture.lock').open('a') as ownership:
                os.chmod(ownership.name, 0o600)
                fcntl.flock(ownership, fcntl.LOCK_EX | fcntl.LOCK_NB)
                if path.exists():
                    raise ValueError('Checkpoint already exists')
                phase = 'capture_read'
                snapshot = DatabaseVerifier(settings, settings.source_socket, settings.source_port).snapshot()
                value = dict(contractVersion=1, environment=settings.environment, service=settings.service, stanza=settings.stanza,
                    sourceDataDirectory=str(settings.source), sourceQuiescedAttested=True, snapshot=snapshot)
                phase = 'capture_validate'
                validate_checkpoint(value)
                if Path(snapshot['dataDirectory']) != settings.source:
                    raise ValueError('Source identity mismatch')
                phase = 'capture_publish'
                atomic(path, value)
            print('Read-only source checkpoint captured.')
            return 0
        phase = 'run'
        result = RecoveryRehearsal(settings).run(args.checkpoint, args.backup_set, args.mode)
        print(json.dumps(dict(attemptId=result['attemptId'], passed=result['passed'], cleanupComplete=result['cleanupComplete'])))
        return 0 if result['passed'] and result['cleanupComplete'] else 1
    except (Exception, KeyboardInterrupt) as error:
        print('Recovery rehearsal failed at ' + phase + ' (' + type(error).__name__ + ').')
        return 1


if __name__ == '__main__':
    def interrupted(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, interrupted)
    raise SystemExit(main())

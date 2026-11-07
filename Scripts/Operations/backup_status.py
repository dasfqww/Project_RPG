#!/usr/bin/env python3
"""Linux DB-host adapter. Only pgBackRest performs backups; this adapter records bounded health facts."""
import argparse
import datetime as dt
import fcntl
import json
import os
from pathlib import Path
import re
import resource
import signal
import subprocess
import tempfile
import uuid

UTC = dt.timezone.utc
LIMIT = 1024 * 1024
SQL = """
SET statement_timeout='3s'; SET default_transaction_read_only=on;
WITH pending AS MATERIALIZED (SELECT modification FROM pg_ls_archive_statusdir() WHERE name LIKE '%.ready')
SELECT json_build_object(
 'systemId',(SELECT system_identifier::text FROM pg_control_system()),
 'archiveConfigured',current_setting('archive_mode') IN ('on','always')
   AND current_setting('archive_timeout')::interval > interval '0'
   AND current_setting('archive_timeout')::interval <= interval '5 min',
 'lastArchivedAtUtc',last_archived_time,'lastArchiveFailureAtUtc',last_failed_time,
 'pendingWalCount',(SELECT count(*) FROM pending),
 'oldestPendingWalAtUtc',(SELECT min(modification) FROM pending))
FROM pg_stat_archiver;
"""


def instant(value):
    if value is None:
        return None
    result = dt.datetime.fromisoformat(value.replace('Z', '+00:00'))
    if result.tzinfo is None:
        raise ValueError('Time must include timezone')
    return result.astimezone(UTC).isoformat().replace('+00:00', 'Z')


def now():
    return dt.datetime.now(UTC).isoformat().replace('+00:00', 'Z')


def reject_links(path):
    path = Path(path)
    if not path.is_absolute():
        raise ValueError('Absolute path required')
    for entry in (path, *path.parents):
        if entry.is_symlink():
            raise ValueError('Linked state refused')
    return path


def atomic(path, value):
    path = reject_links(path)
    descriptor, temporary = tempfile.mkstemp(prefix='.backup-', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'w', encoding='utf-8') as output:
            json.dump(value, output, separators=(',', ':'))
            output.flush()
            os.fsync(output.fileno())
        reject_links(path)
        os.replace(temporary, path)
        directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def read_json(path):
    with reject_links(path).open('rb') as source:
        data = source.read(8193)
    if len(data) > 8192:
        raise ValueError('State too large')
    return json.loads(data)


def execute(arguments, timeout=5, capture=True):
    # The subprocess owns its process group. No inherited/user backup process is terminated.
    def limit_output():
        resource.setrlimit(resource.RLIMIT_FSIZE, (LIMIT, LIMIT))
    with tempfile.TemporaryFile() as output:
        process = subprocess.Popen(arguments, stdout=output if capture else subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL, start_new_session=True,
                                   preexec_fn=limit_output if capture else None)
        try:
            code = process.wait(timeout=timeout)
        except BaseException:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            raise
        if not capture:
            return code
        if code != 0 or output.tell() >= LIMIT:
            raise ValueError('Command failed or output exceeded limit')
        output.seek(0)
        return json.load(output)


class Settings:
    def __init__(self):
        def read(key):
            return os.environ.get('RPG_BACKUP_' + key, '')
        self.environment, self.service, self.stanza = (read(k) for k in ('ENVIRONMENT', 'SERVICE', 'STANZA'))
        if not all(re.fullmatch(r'[A-Za-z0-9_-]{1,64}', v) for v in (self.environment, self.service, self.stanza)):
            raise ValueError('Invalid scope')
        self.config = Path(read('CONFIG'))
        if not self.config.is_absolute():
            raise ValueError('Absolute pgBackRest config required')
        self.root = reject_links(read('STATE_DIRECTORY'))
        if self.root == Path('/'):
            raise ValueError('Private state directory required')
        self.root.mkdir(mode=0o700, parents=True, exist_ok=True)
        reject_links(self.root)
        os.chmod(self.root, 0o700)
        self.backrest = read('PGBACKREST') or '/usr/bin/pgbackrest'
        self.psql = read('PSQL') or '/usr/bin/psql'
        if not all(Path(p).is_absolute() for p in (self.backrest, self.psql)):
            raise ValueError('Absolute executable required')

    def command(self, *arguments):
        return [self.backrest, '--config=' + str(self.config), '--stanza=' + self.stanza, '--repo=1',
                '--log-level-console=off', *arguments]


def attempts(settings):
    value = read_json(settings.root / 'backup-attempts.json')
    if set(value) != {'contractVersion', 'environment', 'service', 'stanza', 'full', 'incr'} or value.get('contractVersion') != 1 or value.get('environment') != settings.environment or value.get('service') != settings.service or value.get('stanza') != settings.stanza:
        raise ValueError('Foreign job state')
    for kind in ('full', 'incr'):
        job = value.get(kind)
        if job is not None and (set(job) != {'state', 'atUtc', 'attemptId', 'exitCode'}
                or str(uuid.UUID(job['attemptId'])) != job['attemptId'] or job.get('state') not in ('Running', 'Success', 'Failed')
                or job['exitCode'] is not None and (type(job['exitCode']) is not int or not -128 <= job['exitCode'] <= 255)
                or job['state'] == 'Success' and job['exitCode'] != 0 or job['state'] == 'Running' and job['exitCode'] is not None
                or job['state'] == 'Failed' and job['exitCode'] == 0
                or dt.datetime.fromisoformat(instant(job['atUtc']).replace('Z', '+00:00')) > dt.datetime.now(UTC)):
            raise ValueError('Invalid job state')
    return value


def job_failed(job, at):
    return job is not None and (job['state'] == 'Failed' or job['state'] == 'Running'
                               and dt.datetime.fromisoformat(at.replace('Z', '+00:00'))
                               - dt.datetime.fromisoformat(job['atUtc'].replace('Z', '+00:00')) > dt.timedelta(hours=2))


def from_info(info, archive, jobs, settings, at):
    if not isinstance(info, list) or len(info) != 1 or info[0]['name'] != settings.stanza:
        raise ValueError('Wrong stanza')
    stanza = info[0]
    database = max(stanza['db'], key=lambda d: d['id'])
    if str(database['system-id']) != str(archive['systemId']):
        raise ValueError('Wrong database cluster')
    backups = [b for b in stanza['backup'] if b['database']['id'] == database['id'] and b['database']['repo-key'] == 1]
    good = [b for b in backups if b.get('error') is False]
    def latest(kind):
        stamps = [b['timestamp']['stop'] for b in good if b['type'] == kind]
        return None if not stamps else dt.datetime.fromtimestamp(max(stamps), UTC).isoformat().replace('+00:00', 'Z')
    value = empty_status(settings, at)
    value.update(collectionSucceeded=True, repositoryHealthy=stanza['status']['code'] == 0 and stanza.get('cipher') == 'aes-256-cbc'
                 and not any(b.get('error') is not False for b in backups),
                 fullBackupFailed=job_failed(jobs.get('full'), at), incrementalBackupFailed=job_failed(jobs.get('incr'), at),
                 lastFullAtUtc=latest('full'), lastIncrementalAtUtc=latest('incr'),
                 fullChains=sum(b['type'] == 'full' for b in good), archiveConfigured=archive['archiveConfigured'],
                 lastArchivedAtUtc=instant(archive['lastArchivedAtUtc']), lastArchiveFailureAtUtc=instant(archive['lastArchiveFailureAtUtc']),
                 pendingWalCount=archive['pendingWalCount'], oldestPendingWalAtUtc=instant(archive['oldestPendingWalAtUtc']))
    return value


def empty_status(settings, at):
    return dict(contractVersion=1, environment=settings.environment, service=settings.service, observedAtUtc=at,
                collectionSucceeded=False, repositoryHealthy=False, fullBackupFailed=False, incrementalBackupFailed=False,
                lastFullAtUtc=None, lastIncrementalAtUtc=None, fullChains=0, archiveConfigured=False,
                lastArchivedAtUtc=None, lastArchiveFailureAtUtc=None, pendingWalCount=0, oldestPendingWalAtUtc=None)


def collect(settings):
    value = empty_status(settings, now())
    phase = 'pgbackrest_info'
    try:
        info = execute(settings.command('--log-level-file=off', '--output=json', 'info'))
        phase = 'archive_sql'
        # PG* service/password settings are deployment secrets, never command arguments or published facts.
        archive = execute([settings.psql, '-X', '-q', '-A', '-t', '-v', 'ON_ERROR_STOP=1', '-c', SQL])
        phase = 'job_state_or_contract'
        value = from_info(info, archive, attempts(settings), settings, now())
    except Exception:
        print('Backup collection failed at ' + phase + '.')  # Fixed phase only; no raw error or command contents.
    atomic(settings.root / 'backup-status.json', value)
    return 0 if value['collectionSucceeded'] else 1


def backup(settings, kind):
    state_path = settings.root / 'backup-attempts.json'
    jobs = attempts(settings) if state_path.exists() else dict(contractVersion=1, environment=settings.environment,
        service=settings.service, stanza=settings.stanza, full=None, incr=None)
    attempt_id = str(uuid.uuid4())
    jobs[kind] = dict(state='Running', atUtc=now(), attemptId=attempt_id, exitCode=None)
    atomic(state_path, jobs)
    print(json.dumps(dict(event='backup_started', attemptId=attempt_id, type=kind)), flush=True)
    succeeded = False
    code = None
    try:
        code = execute(settings.command('--type=' + kind, 'backup'), timeout=3600, capture=False)
        succeeded = code == 0
    finally:
        jobs[kind] = dict(state='Success' if succeeded else 'Failed', atUtc=now(), attemptId=attempt_id, exitCode=code)
        atomic(state_path, jobs)
        print(json.dumps(dict(event='backup_completed', attemptId=attempt_id, type=kind, state=jobs[kind]['state'], exitCode=code)), flush=True)
    return 0 if succeeded else 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('operation', choices=('collect', 'backup'))
    parser.add_argument('--type', choices=('full', 'incr'))
    args = parser.parse_args()
    if (args.operation == 'backup') != (args.type is not None):
        parser.error('--type is required only for backup')
    try:
        settings = Settings()
        # Bound startup connection latency in addition to PostgreSQL statement_timeout.
        os.environ['PGCONNECT_TIMEOUT'] = '5'
        lock = reject_links(settings.root / (args.operation + '.lock'))
        with lock.open('a') as ownership:
            os.chmod(lock, 0o600)
            fcntl.flock(ownership, fcntl.LOCK_EX | fcntl.LOCK_NB)
            return collect(settings) if args.operation == 'collect' else backup(settings, args.type)
    except KeyboardInterrupt:
        print('Backup adapter interrupted; owned command stopped.')
        return 130
    except Exception:
        print('Backup adapter failed: configuration, state, lock or command failure.')
        return 1


if __name__ == '__main__':
    def interrupted(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, interrupted)
    raise SystemExit(main())

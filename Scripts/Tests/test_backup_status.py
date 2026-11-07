import datetime as dt
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Operations'))
import backup_status as b


class BackupStatusTests(unittest.TestCase):
    def settings(self, root):
        return types.SimpleNamespace(root=Path(root), environment='OpsTest', service='backend', stanza='fixture', command=lambda *a: list(a))

    def test_failed_job_is_durable_and_does_not_clear_other_job(self):
        with tempfile.TemporaryDirectory() as root:
            s = self.settings(root)
            with patch.object(b, 'execute', return_value=1):
                self.assertEqual(1, b.backup(s, 'full'))
            with patch.object(b, 'execute', return_value=0):
                self.assertEqual(0, b.backup(s, 'incr'))
            jobs = b.attempts(s)
            self.assertEqual('Failed', jobs['full']['state'])
            self.assertEqual(1, jobs['full']['exitCode'])
            self.assertTrue(jobs['full']['attemptId'])
            self.assertEqual('Success', jobs['incr']['state'])
            with patch.object(b, 'execute', side_effect=TimeoutError):
                with self.assertRaises(TimeoutError):
                    b.backup(s, 'incr')
            self.assertEqual('Failed', b.attempts(s)['incr']['state'])

    def test_collection_failure_publishes_unknown_not_old_success(self):
        with tempfile.TemporaryDirectory() as root:
            s = self.settings(root)
            b.atomic(s.root / 'backup-status.json', {'collectionSucceeded': True})
            with patch.object(b, 'execute', side_effect=ValueError('private credentials must not be emitted')):
                self.assertEqual(1, b.collect(s))
            result = b.read_json(s.root / 'backup-status.json')
            self.assertFalse(result['collectionSucceeded'])
            self.assertNotIn('private', json.dumps(result))

    def test_oversized_corrupt_foreign_or_linked_state_is_rejected(self):
        with tempfile.TemporaryDirectory() as root:
            p = Path(root) / 'state.json'
            p.write_text('x' * 8193)
            with self.assertRaises(ValueError):
                b.read_json(p)
            p.write_text('{bad')
            with self.assertRaises(ValueError):
                b.read_json(p)
            link = Path(root) / 'linked.json'
            link.symlink_to(p)
            with self.assertRaises(ValueError):
                b.atomic(link, {})
            self.assertEqual('{bad', p.read_text())
            b.atomic(Path(root) / 'backup-attempts.json', dict(contractVersion=1, environment='Foreign', service='backend', stanza='fixture'))
            with self.assertRaises(ValueError):
                b.attempts(self.settings(root))

    def test_abandoned_running_job_eventually_fails(self):
        at = dt.datetime.now(dt.timezone.utc)
        job = {'state': 'Running', 'atUtc': (at - dt.timedelta(hours=3)).isoformat()}
        self.assertTrue(b.job_failed(job, at.isoformat()))
        job['atUtc'] = at.isoformat()
        self.assertFalse(b.job_failed(job, at.isoformat()))

    def test_wrong_cluster_and_missing_error_field_fail_closed(self):
        at = dt.datetime.now(dt.timezone.utc)
        info = [dict(name='fixture', db=[{'id': 1, 'system-id': 123}], status={'code': 0}, backup=[
            {'database': {'id': 1, 'repo-key': 1}, 'type': 'full', 'timestamp': {'stop': int(at.timestamp())}}])]
        archive = dict(systemId='wrong', archiveConfigured=True, lastArchivedAtUtc=None, lastArchiveFailureAtUtc=None,
                       pendingWalCount=0, oldestPendingWalAtUtc=None)
        with self.assertRaises(ValueError):
            b.from_info(info, archive, {}, self.settings('/tmp'), at.isoformat())
        archive['systemId'] = '123'
        value = b.from_info(info, archive, {}, self.settings('/tmp'), at.isoformat())
        self.assertFalse(value['repositoryHealthy'])
        self.assertIsNone(value['lastFullAtUtc'])


if __name__ == '__main__':
    unittest.main()

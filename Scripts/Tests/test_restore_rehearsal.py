import copy
import json
import os
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Operations'))
import restore_rehearsal as r


def checkpoint():
    return dict(contractVersion=1, environment='OpsTest', service='backend', stanza='fixture',
        sourceDataDirectory='/protected/source', sourceQuiescedAttested=True,
        snapshot=dict(observedAtUtc='2026-11-08T00:00:00Z', systemId='1234567890123456789', version=170007,
            dataDirectory='/protected/source', inRecovery=False, paused=False, userTablespaces=0,
            tables={k: dict(count=0, sha256='a' * 64) for k in r.REQUIRED_TABLES},
            migrations=[dict(version=1, name='baseline', sha256='a' * 64)]))


class RestoreRehearsalTests(unittest.TestCase):
    def test_private_diagnostic_tail_is_bounded_for_a_real_child(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / 'diagnostic.log'
            value = r.execute([sys.executable,'-c',"import sys; sys.stderr.write('x'*70000); print('{}')"], diagnostic=path)
            self.assertEqual({},value)
            self.assertEqual(65536,path.stat().st_size)

    def test_source_password_file_never_reaches_clone(self):
        settings = types.SimpleNamespace(psql='/usr/bin/psql',user='reader',database='ops_test',source_socket=Path('/source'),
                                         source_port=5432,source_passfile=Path('/private/source-pass'))
        self.assertEqual('/private/source-pass',r.DatabaseVerifier(settings,Path('/source'),5432).client('SELECT 1')[1]['PGPASSFILE'])
        self.assertEqual('/dev/null',r.DatabaseVerifier(settings,Path('/clone'),55439).client('SELECT 1')[1]['PGPASSFILE'])

    def test_streaming_hashes_large_int_rows_and_requires_complete_table_set(self):
        rows = [b'{"kind":"metadata"}\n',b'{"kind":"table","name":"accounts"}\n',
                b'{"kind":"row","data":{"balance":9007199254740993}}\n',b'{"kind":"complete"}\n']
        value = r.parse_snapshot(iter(rows),['accounts'])
        self.assertEqual(1,value['tables']['accounts']['count'])
        self.assertEqual(r.hashlib.sha256(rows[2]).hexdigest(),value['tables']['accounts']['sha256'])
        with self.assertRaises(ValueError):
            r.parse_snapshot(iter(rows[:-1]),['accounts'])
        with self.assertRaises(ValueError):
            r.parse_snapshot(iter(rows),['accounts','characters'])
        with self.assertRaises(ValueError):
            r.parse_snapshot(iter([b'x' * (1024 * 1024 + 1)]),['accounts'])

    def test_checkpoint_must_cover_game_schema_and_primary_identity(self):
        r.validate_checkpoint(checkpoint())
        for change in ('missing', 'unpaused', 'tablespace', 'hash'):
            value = checkpoint()
            if change == 'missing':
                value['snapshot']['tables'].pop('accounts')
            if change == 'unpaused':
                value['snapshot']['inRecovery'] = True
            if change == 'tablespace':
                value['snapshot']['userTablespaces'] = 1
            if change == 'hash':
                value['snapshot']['tables']['accounts']['sha256'] = 'bad'
            with self.assertRaises(ValueError):
                r.validate_checkpoint(value)

    def test_verification_requires_pause_identical_tables_and_history(self):
        expected = checkpoint()['snapshot']
        actual = copy.deepcopy(expected)
        actual.update(inRecovery=True, paused=True)
        self.assertTrue(r.compare(expected, actual))
        for key, value in (('paused', False), ('inRecovery', False), ('systemId', 'wrong'), ('migrations', []), ('tables', {})):
            changed = copy.deepcopy(actual)
            changed[key] = value
            self.assertFalse(r.compare(expected, changed))

    def test_verifier_cannot_inherit_source_service_or_hostaddr(self):
        settings = types.SimpleNamespace(psql='/usr/bin/psql', user='restore_reader', database='ops_test')
        with patch.dict(os.environ, dict(PGSERVICE='source', PGHOSTADDR='192.0.2.1', PGOPTIONS='unsafe')):
            with patch.object(r, 'execute', return_value={}) as command:
                r.DatabaseVerifier(settings, Path('/private/socket'), 55439).query('SELECT 1')
                args, kwargs = command.call_args
                self.assertNotIn('PGSERVICE', kwargs['env'])
                self.assertNotIn('PGHOSTADDR', kwargs['env'])
                self.assertIn('default_transaction_read_only=on', kwargs['env']['PGOPTIONS'])
                self.assertIn('/private/socket', args[0])
                self.assertIn('55439', args[0])

    def test_settings_refuse_source_overlap_and_linked_scratch(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            source = root / 'source'; source.mkdir()
            env = dict(RPG_RESTORE_ENVIRONMENT='OpsTest', RPG_RESTORE_SERVICE='backend', RPG_RESTORE_STANZA='fixture',
                RPG_RESTORE_DATABASE='ops_test', RPG_RESTORE_USER='restore_reader', RPG_RESTORE_SOURCE_DATA_DIRECTORY=str(source),
                RPG_RESTORE_SCRATCH_DIRECTORY=str(source / 'child'), RPG_RESTORE_PROOF_DIRECTORY=str(root / 'proof'),
                RPG_RESTORE_SOURCE_SOCKET_DIRECTORY=str(root / 'socket'), RPG_RESTORE_CONFIG=str(root / 'backrest.conf'),
                RPG_RESTORE_PG_BIN_DIRECTORY='/usr/lib/postgresql/17/bin')
            with patch.dict(os.environ, env):
                with self.assertRaises(ValueError):
                    r.Settings()
                link = root / 'link'; link.symlink_to(source, target_is_directory=True)
                with patch.dict(os.environ, dict(RPG_RESTORE_SCRATCH_DIRECTORY=str(link))):
                    with self.assertRaises(ValueError):
                        r.Settings()

    def test_oversized_or_linked_checkpoint_is_rejected(self):
        with tempfile.TemporaryDirectory() as root:
            p = Path(root) / 'point.json'; p.write_text('x' * 65537)
            with self.assertRaises(ValueError):
                r.read_checkpoint(p)
            p.write_text(json.dumps(checkpoint()))
            link = Path(root) / 'link'; link.symlink_to(p)
            with self.assertRaises(ValueError):
                r.read_checkpoint(link)


if __name__ == '__main__':
    unittest.main()

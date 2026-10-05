#!/usr/bin/env python3
"""No-network failure controls for fixed paired admission."""
from copy import deepcopy
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location('frozen_partition', ROOT / 'partition.py')
PARTITION = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PARTITION)


class FixedPairControls(unittest.TestCase):
    def setUp(self):
        self.config = {'expected_case_ids': ['source-a', 'source-b'],
                       'expected_method_ids': ['method-first', 'method-second'],
                       'case_pointer': '/metadata/case', 'method_pointer': '/metadata/method'}
        self.manifest = {'operations': [{'operation_id': f'op-{i}', 'metadata': {'case': case, 'method': method}}
                                      for i, (case, method) in enumerate((('source-a', 'method-first'),
                                          ('source-a', 'method-second'), ('source-b', 'method-first'),
                                          ('source-b', 'method-second')))]}

    def test_all_pairs_and_order_preserved(self):
        groups = PARTITION.grouped(self.manifest, self.config)
        self.assertEqual([case for case, _ in groups], ['source-a', 'source-b'])
        self.assertEqual([op for _, rows in groups for op in rows], self.manifest['operations'])

    def test_physical_identity_collision_rejected(self):
        self.manifest['operations'][2]['operation_id'] = self.manifest['operations'][0]['operation_id']
        with self.assertRaisesRegex(ValueError, 'duplicate_physical'):
            PARTITION.grouped(self.manifest, self.config)

    def test_missing_method_cannot_be_disguised_as_pair(self):
        self.manifest['operations'].pop(1)
        with self.assertRaisesRegex(ValueError, 'membership_mismatch'):
            PARTITION.grouped(self.manifest, self.config)

    def test_outcome_driven_reordering_rejected(self):
        self.manifest['operations'] = self.manifest['operations'][2:] + self.manifest['operations'][:2]
        with self.assertRaisesRegex(ValueError, 'order_or_pair_membership'):
            PARTITION.grouped(self.manifest, self.config)

    def test_repeated_method_not_two_distinct_methods(self):
        self.manifest['operations'][1]['metadata']['method'] = 'method-first'
        with self.assertRaisesRegex(ValueError, 'membership_mismatch'):
            PARTITION.grouped(self.manifest, self.config)

    def test_stale_source_digest_rejected(self):
        with tempfile.TemporaryDirectory() as dirname:
            path = Path(dirname) / 'body.json'
            path.write_bytes(b'{"body":"altered"}\n')
            with self.assertRaisesRegex(ValueError, 'frozen_source_hash_mismatch'):
                PARTITION.bound(path, hashlib.sha256(b'{"body":"original"}\n').hexdigest())

    def test_existing_partition_conflict_never_overwritten(self):
        with tempfile.TemporaryDirectory() as dirname:
            path = Path(dirname) / 'manifest.json'
            path.write_bytes(b'original')
            with self.assertRaisesRegex(ValueError, 'conflicting_partition_artifact'):
                PARTITION.write_exact(path, b'changed')
            self.assertEqual(path.read_bytes(), b'original')


if __name__ == '__main__':
    unittest.main()

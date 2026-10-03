"""Actual process races, killed workers, recovery and fenced effect receipts."""
from copy import deepcopy
import multiprocessing as mp
import os
from pathlib import Path
import sqlite3
import tempfile
import time
import unittest

from loom.tools.coordination.leases import CoordinationError, LeaseStore, execute_once

COMMIT = "fafc77f8eeebdff4c32897b5e8ef94dd26d4ec38"
EVIDENCE = [{"kind": "test_observation", "ref": "synthetic-offline-fixture"}]


def race_worker(database, gate, result_queue, effects):
    store = LeaseStore(database)
    gate.wait(10)
    def effect(spec, lease):
        with open(effects, "a") as file:
            file.write(str(os.getpid()) + "\n")
            file.flush()
            os.fsync(file.fileno())
        time.sleep(0.04)
        return {"effect": "once", "value": spec["value"]}
    try:
        result = execute_once(store, "race", owner=str(os.getpid()), lease_seconds=10, callback=effect)
        result_queue.put(("ok", result["acquired"]))
    except BaseException as exc:
        result_queue.put(("error", type(exc).__name__, str(exc)))


def crash_worker(database, queue, dispatch, effects):
    store = LeaseStore(database)
    lease = store.claim("crash", owner="killed-worker", lease_seconds=0.25)["lease"]
    if dispatch:
        store.begin(lease)
        with open(effects, "w") as file:
            file.write("external effect happened\n")
            file.flush()
            os.fsync(file.fileno())
    queue.put(lease)
    # Parent terminates us after receiving the durable-commit notification.
    time.sleep(20)


def transaction_crash_worker(database, queue):
    db = sqlite3.connect(database, isolation_level=None)
    db.execute("BEGIN IMMEDIATE")
    db.execute("UPDATE tasks SET state='succeeded' WHERE task_id='atomic'")
    queue.put("uncommitted")
    time.sleep(20)


class Clock:
    def __init__(self): self.now = 1000.0
    def __call__(self): return self.now


class LeaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.database = str(Path(self.temp.name) / "tasks.sqlite3")
        self.clock = Clock()
        self.store = LeaseStore(self.database, clock=self.clock)
        self.store.register("task", source_commit=COMMIT, spec={"value": 1, "policy": {"any": "data"}})

    def lease(self):
        return self.store.claim("task", owner="first", lease_seconds=10)["lease"]

    def test_receipts_bind_source_spec_owner_fence_and_outcome_across_restart(self):
        lease = self.lease()
        self.store.begin(lease)
        self.store.complete(lease, outcome={"result": [1, 2]}, evidence=EVIDENCE)
        reopened = LeaseStore(self.database, clock=self.clock)
        self.assertEqual(reopened.inspect("task")["state"], "succeeded")
        receipts = reopened.receipts("task")
        self.assertEqual([r["event"] for r in receipts], ["registered", "claimed", "dispatch_started", "completed"])
        for receipt in receipts:
            self.assertEqual(receipt["source_commit"], COMMIT)
            self.assertEqual(receipt["spec_sha256"], lease["spec_sha256"])
        self.assertEqual(receipts[-1]["owner"], "first")
        self.assertEqual(receipts[-1]["fence"], 1)
        self.assertEqual(receipts[-1]["detail"]["outcome"], {"result": [1, 2]})
        self.assertEqual(receipts[-1]["detail"]["evidence"], EVIDENCE)
        self.assertFalse(reopened.claim("task", owner="second", lease_seconds=1)["acquired"])

    def test_same_owner_is_not_a_second_claim_and_begin_is_not_reentrant(self):
        lease = self.lease()
        self.assertFalse(self.store.claim("task", owner="first", lease_seconds=10)["acquired"])
        self.store.begin(lease)
        with self.assertRaises(CoordinationError): self.store.begin(lease)

    def test_immutable_identity_conflict_does_not_mutate_registered_task(self):
        original = self.store.inspect("task")
        with self.assertRaisesRegex(CoordinationError, "identity_conflict"):
            self.store.register("task", source_commit=COMMIT, spec={"value": 2})
        with self.assertRaises(CoordinationError):
            self.store.register("task", source_commit="a" * 40, spec=original["spec"])
        self.assertEqual(original, self.store.inspect("task"))

    def test_pre_dispatch_expiry_reclaims_with_monotone_fence_and_rejects_old_owner(self):
        old = self.lease()
        self.clock.now += 10
        new = self.store.claim("task", owner="second", lease_seconds=10)["lease"]
        self.assertEqual(new["fence"], old["fence"] + 1)
        self.assertNotEqual(new["token"], old["token"])
        for operation in (self.store.begin, lambda x: self.store.renew(x, lease_seconds=100)):
            with self.assertRaises(CoordinationError): operation(old)
        self.store.begin(new)
        self.store.complete(new, outcome="done", evidence=EVIDENCE)

    def test_dispatched_expiry_stays_unknown_even_after_many_claims(self):
        lease = self.lease()
        self.store.begin(lease)
        self.clock.now += 100
        for _ in range(4):
            self.assertFalse(self.store.claim("task", owner="second", lease_seconds=10)["acquired"])
        self.assertEqual(self.store.inspect("task")["state"], "outcome_unknown")
        with self.assertRaises(CoordinationError):
            self.store.complete(lease, outcome="late", evidence=EVIDENCE)
        self.assertEqual(sum(r["event"] == "expired_after_dispatch" for r in self.store.receipts("task")), 1)

    def test_explicit_reconciliation_preserves_unknown_and_fences_stale_completion(self):
        old = self.lease(); self.store.begin(old)
        self.store.mark_unknown(old, evidence=EVIDENCE)
        with self.assertRaises(CoordinationError):
            self.store.reconcile("task", expected_fence=2, actor="root", decision="retry", outcome={},
                                 evidence=EVIDENCE, retry_basis="operator accepted duplication risk")
        receipt = self.store.reconcile("task", expected_fence=1, actor="root", decision="retry", outcome={},
                                       evidence=EVIDENCE, retry_basis="idempotent fixture; operator decision")
        self.assertTrue(receipt["detail"]["external_effects_may_repeat"])
        self.assertEqual(receipt["actor"], "root")
        new = self.store.claim("task", owner="second", lease_seconds=5)["lease"]
        self.assertEqual(new["fence"], 2)
        with self.assertRaises(CoordinationError): self.store.complete(old, outcome="late", evidence=EVIDENCE)
        self.assertIn("outcome_unknown", [r["event"] for r in self.store.receipts("task")])

    def test_reconcile_to_complete_does_not_dispatch(self):
        lease = self.lease(); self.store.begin(lease); self.clock.now += 10
        self.store.reconcile("task", expected_fence=1, actor="root", decision="succeeded",
                             outcome={"external_receipt": "found"}, evidence=EVIDENCE)
        self.assertEqual(self.store.inspect("task")["state"], "succeeded")
        self.assertFalse(self.store.claim("task", owner="other", lease_seconds=10)["acquired"])

    def test_renewal_extends_never_shortens_and_cannot_resurrect_expired_lease(self):
        lease = self.lease(); self.clock.now += 1
        short = self.store.renew(lease, lease_seconds=1)
        self.assertEqual(short["lease_deadline"], 1010)
        self.store.renew(lease, lease_seconds=100)
        self.clock.now = 1050
        self.assertFalse(self.store.claim("task", owner="other", lease_seconds=1)["acquired"])
        self.clock.now = 1101
        with self.assertRaisesRegex(CoordinationError, "lease_expired"):
            self.store.renew(lease, lease_seconds=100)

    def test_foreign_lease_fields_cannot_complete_or_dispatch(self):
        lease = self.lease()
        for key, value in (("owner", "other"), ("source_commit", "0" * 40), ("token", "bad"), ("fence", 10)):
            bad = dict(lease); bad[key] = value
            with self.assertRaises(CoordinationError): self.store.begin(bad)
        self.assertEqual(self.store.inspect("task")["state"], "leased")

    def test_callback_exception_is_unknown_without_message_or_retry(self):
        calls = []
        def fail(spec, lease):
            calls.append(1)
            raise RuntimeError("should never persist potential secret exception text")
        with self.assertRaises(RuntimeError):
            execute_once(self.store, "task", owner="one", lease_seconds=1, callback=fail)
        result = execute_once(self.store, "task", owner="two", lease_seconds=1, callback=fail)
        self.assertFalse(result["acquired"]); self.assertEqual(calls, [1])
        self.assertNotIn("secret exception", str(self.store.receipts("task")))

    def test_late_callback_result_preserved_without_falsely_completing(self):
        def slow(spec, lease):
            self.clock.now += 10
            return {"retained": "first result"}
        with self.assertRaises(CoordinationError):
            execute_once(self.store, "task", owner="one", lease_seconds=1, callback=slow)
        evidence = self.store.receipts("task")[-1]["detail"]["evidence"]
        self.assertEqual(evidence[-1]["outcome"], {"retained": "first result"})
        self.assertEqual(self.store.inspect("task")["state"], "outcome_unknown")

    def test_observer_expiry_does_not_erase_late_callback_return(self):
        def slow(spec, lease):
            self.clock.now += 10
            self.store.inspect("task")
            return {"late": "but preserved"}
        with self.assertRaises(CoordinationError):
            execute_once(self.store, "task", owner="one", lease_seconds=1, callback=slow)
        receipts = self.store.receipts("task")
        self.assertIn("expired_after_dispatch", [r["event"] for r in receipts])
        self.assertEqual(receipts[-1]["detail"]["evidence"][-1]["outcome"], {"late": "but preserved"})

    def test_expiry_observed_during_input_fetch_prevents_dispatch(self):
        inspect = self.store.inspect
        def expired_inspect(task_id):
            self.clock.now += 10
            return inspect(task_id)
        self.store.inspect = expired_inspect
        calls = []
        with self.assertRaises(CoordinationError):
            execute_once(self.store, "task", owner="one", lease_seconds=1,
                         callback=lambda *_: calls.append("external effect"))
        self.assertEqual(calls, [])
        self.assertEqual(inspect("task")["state"], "pending")
        self.assertNotIn("dispatch_started", [row["event"] for row in self.store.receipts("task")])
        self.assertTrue(self.store.claim("task", owner="recovery", lease_seconds=10)["acquired"])

    def test_live_wal_backup_reopens_with_complete_receipt_chain(self):
        # Hold a live connection so SQLite cannot clean up WAL on last close.
        with sqlite3.connect(self.database) as pinned:
            pinned.execute("PRAGMA journal_mode=WAL")
            lease = self.lease(); self.store.begin(lease)
            self.store.complete(lease, outcome={"persisted": 42}, evidence=EVIDENCE)
            destination = Path(self.temp.name) / "checkpoint.sqlite3"
            receipt = self.store.backup(destination)
            self.assertEqual(len(receipt["sha256"]), 64)
            with self.assertRaises(FileExistsError): self.store.backup(destination)
        reopened = LeaseStore(destination, clock=self.clock)
        self.assertEqual(reopened.inspect("task")["state"], "succeeded")
        self.assertEqual(reopened.receipts("task"), self.store.receipts("task"))

    def test_malformed_values_are_rejected_before_new_claims_or_effects(self):
        cyclic = []; cyclic.append(cyclic)
        for spec in ({1: "not string"}, {"nan": float("nan")}, cyclic):
            with self.assertRaises(CoordinationError): self.store.register("bad", source_commit=COMMIT, spec=spec)
        for duration in (0, -1, True, float("nan"), float("inf")):
            with self.assertRaises(CoordinationError): self.store.claim("task", owner="one", lease_seconds=duration)
        self.assertEqual(self.store.inspect("task")["state"], "pending")

    def test_append_only_receipt_storage_refuses_update_and_delete(self):
        with sqlite3.connect(self.database) as db:
            for statement in ("DELETE FROM receipts", "UPDATE receipts SET body_json='{}'"):
                with self.assertRaises(sqlite3.IntegrityError): db.execute(statement)


class ProcessTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.database = str(Path(self.temp.name) / "tasks.sqlite3")
        self.effects = str(Path(self.temp.name) / "effects.txt")
        self.ctx = mp.get_context("spawn")
        self.store = LeaseStore(self.database)

    def join(self, worker):
        worker.join(10)
        if worker.is_alive(): worker.terminate(); worker.join(5); self.fail("worker stuck")
        self.assertEqual(worker.exitcode, 0)

    def test_twelve_processes_dispatch_one_external_effect(self):
        self.store.register("race", source_commit=COMMIT, spec={"value": 42})
        gate = self.ctx.Event(); queue = self.ctx.Queue()
        workers = [self.ctx.Process(target=race_worker, args=(self.database, gate, queue, self.effects)) for _ in range(12)]
        for worker in workers: worker.start()
        gate.set()
        results = [queue.get(timeout=15) for _ in workers]
        for worker in workers: self.join(worker)
        self.assertEqual([r for r in results if r[0] != "ok"], [])
        self.assertEqual(sum(r[1] for r in results), 1)
        self.assertEqual(len(Path(self.effects).read_text().splitlines()), 1)
        self.assertEqual(LeaseStore(self.database).inspect("race")["state"], "succeeded")

    def crashed(self, dispatch):
        self.store.register("crash", source_commit=COMMIT, spec={"effect": "durable file"})
        queue = self.ctx.Queue()
        worker = self.ctx.Process(target=crash_worker, args=(self.database, queue, dispatch, self.effects))
        worker.start(); lease = queue.get(timeout=10)
        worker.terminate(); worker.join(5)
        self.assertFalse(worker.is_alive())
        time.sleep(0.30)
        return lease, LeaseStore(self.database)

    def test_killed_worker_before_dispatch_can_be_reclaimed_after_reopen(self):
        old, reopened = self.crashed(False)
        new = reopened.claim("crash", owner="recovery", lease_seconds=5)
        self.assertTrue(new["acquired"]); self.assertEqual(new["lease"]["fence"], 2)
        self.assertFalse(Path(self.effects).exists())
        with self.assertRaises(CoordinationError): reopened.begin(old)

    def test_killed_worker_after_external_effect_cannot_automatically_rerun(self):
        old, reopened = self.crashed(True)
        self.assertTrue(Path(self.effects).exists())
        self.assertFalse(reopened.claim("crash", owner="recovery", lease_seconds=5)["acquired"])
        self.assertEqual(reopened.inspect("crash")["state"], "outcome_unknown")
        self.assertEqual(reopened.receipts("crash")[-1]["detail"]["effects"], "unknown")

    def test_kill_inside_uncommitted_transaction_rolls_back_on_restart(self):
        self.store.register("atomic", source_commit=COMMIT, spec={})
        queue = self.ctx.Queue()
        worker = self.ctx.Process(target=transaction_crash_worker, args=(self.database, queue))
        worker.start(); self.assertEqual(queue.get(timeout=10), "uncommitted")
        worker.terminate(); worker.join(5)
        self.assertEqual(LeaseStore(self.database).inspect("atomic")["state"], "pending")


if __name__ == "__main__": unittest.main()

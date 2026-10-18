"""Work blocks (workblocks.py): more than `min_switches` switches between conversations make one block; settings, not rules."""
import unittest

from loom.tools.extract.workblocks import find_work_blocks


def runs(seq, step=1):
    return [{"conversation": c, "first": f"2026-05-01T10:{i * step:02d}:00Z", "last": f"2026-05-01T10:{i * step:02d}:30Z"} for i, c in enumerate(seq)]


class WorkBlocksTest(unittest.TestCase):
    def test_more_than_three_switches(self) -> None:
        self.assertEqual(find_work_blocks(runs("ABABA")), [{"start": 0, "end": 4, "switches": 4, "conversations": 2}])
        self.assertEqual(find_work_blocks(runs("ABABC")), [])  # 4 runs of a recurring kind, only 3 switches inside the chain

    def test_exactly_three_is_not_enough_and_the_threshold_is_a_setting(self) -> None:
        self.assertEqual(find_work_blocks(runs("ABAB")), [])
        self.assertEqual(len(find_work_blocks(runs("ABAB"), {"min_switches": 2})), 1)

    def test_one_off_tail_stays_out_and_a_sandwiched_detour_stays_in(self) -> None:
        got = find_work_blocks(runs("ABABACD"))
        self.assertEqual([(b["start"], b["end"]) for b in got], [(0, 4)])
        got = find_work_blocks(runs("ACABAB"))
        self.assertEqual([(b["start"], b["end"]) for b in got], [(0, 5)])
        self.assertEqual(find_work_blocks(runs("ACABAB"), {"sandwich": False}), [])

    def test_pause_cuts_and_switch_off(self) -> None:
        r = runs("ABABABAB")
        for k in range(4, 8):
            r[k]["first"] = r[k]["last"] = f"2026-05-01T14:0{k}:00Z"
        self.assertEqual(len(find_work_blocks(r)), 1)                          # no limit: one block of 7 switches
        self.assertEqual(find_work_blocks(r, {"max_gap_minutes": 60}), [])     # the 3 h pause cuts it into 3 + 3 switches
        self.assertEqual(find_work_blocks(runs("ABABA"), {"enabled": False}), [])


if __name__ == "__main__":
    unittest.main()

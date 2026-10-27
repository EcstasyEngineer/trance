"""Independent V4 selector checks against the source-observed V3 burst FSM."""
import random
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from pattern_v4_examples import examples, pattern
from pattern_v4_selector import trace


class Counted(random.Random):
    def __init__(self, seed):
        super().__init__(seed)
        self.calls = 0
    def randrange(self, *args):
        self.calls += 1
        return super().randrange(*args)


def legacy(frames, rng, owner_frames=None):
    # Direct independent transcription of BurstCycler::advance, parameterized
    # exactly as the production Super Fast definition. This is a reference oracle,
    # not native-runtime or pixel equivalence evidence.
    active = False
    end = 0
    cooldown = 0
    entered = 0
    duration = None
    result = []
    owner_start = 0
    for frame in range(frames):
        if owner_frames is not None and frame and frame % owner_frames == 0:
            owner_start = frame
            active = False
            end = 0
            cooldown = 0
            entered = frame
            duration = None
        exited = active and frame >= end
        if exited:
            active = False
            cooldown = 8
            entered = frame
            duration = None
        if (frame - owner_start) % 8 == 0 and not active:
            if not exited and cooldown:
                cooldown -= 1
            if not exited and not cooldown and rng.randrange(12) == 0:
                active = True
                entered = frame
                duration = 64 + 8 * rng.randrange(9)
                end = frame + duration
        result.append((frame, int(active), frame - entered, duration))
    return result


class SelectorContract(unittest.TestCase):
    def test_super_fast_independent_legacy_oracle(self):
        selector = examples()["super_fast"]["root"]["children"][0]["node"]
        for seed in range(100):
            for owner_frames in [None, 2048, 1000]:
                a, b = Counted(seed), Counted(seed)
                current = trace(selector, 4096, a, owner_frames=owner_frames)
                expected = legacy(4096, b, owner_frames=owner_frames)
                self.assertEqual([(t.frame, t.child, t.local_frame, t.duration) for t in current], expected, (seed,owner_frames))
                self.assertEqual(a.calls, b.calls, (seed,owner_frames))

    def test_initial_hit_and_return_cooldown(self):
        class Zero:
            calls = 0
            def randrange(self, n):
                self.calls += 1
                return 0
        selector = examples()["super_fast"]["root"]["children"][0]["node"]
        ticks = trace(selector, 193, Zero())
        self.assertEqual([t.frame for t in ticks if t.transitioned], [0, 64, 128, 192])
        self.assertFalse(ticks[64].checked)
        self.assertTrue(all(not t.checked for t in ticks[1:64]))
        self.assertEqual(ticks[128].child, 1)
        self.assertEqual(ticks[128].local_frame, 0)

    def test_round_robin_and_all_random_choices(self):
        for order, allow_self in [("round_robin", False), ("random", False), ("random", True)]:
            selector = pattern(*(pattern({"op":"image","source":"primary"}, duration=1) for _ in range(3)),
                               kind="random", select={"order":order,"allow_self":allow_self,"first":0})
            ticks = trace(selector, 300, random.Random(7))
            self.assertEqual({t.child for t in ticks}, {0, 1, 2})
            if order == "round_robin":
                self.assertEqual([t.child for t in ticks[:7]], [0,1,2,0,1,2,0])
            elif not allow_self:
                self.assertTrue(all(a.child != b.child for a,b in zip(ticks,ticks[1:])))
            else:
                self.assertTrue(any(a.child == b.child for a,b in zip(ticks,ticks[1:])))
            self.assertTrue(all(t.local_frame == 0 for t in ticks))

    def test_single_child_restarts_without_zero_time_loop(self):
        selector = pattern(pattern({"op":"image","source":"primary"},duration=2),kind="random",
                           select={"order":"random","allow_self":False,"first":0})
        ticks = trace(selector, 7, random.Random(2))
        self.assertEqual([t.local_frame for t in ticks], [0,1,0,1,0,1,0])

    def test_unsupported_repeat_fails_instead_of_transitioning_early(self):
        selector = pattern(pattern({"op":"image","source":"primary"},duration=2,repeat=3),
                           pattern({"op":"image","source":"secondary"},duration=2),kind="random",
                           select={"order":"round_robin","allow_self":False,"first":0})
        with self.assertRaisesRegex(ValueError, "opaque-timer"):
            trace(selector, 10, random.Random(1))

    def test_probability_endpoints_and_check_grid(self):
        for numerator in [0, 1]:
            selector = pattern(pattern({"op":"image","source":"primary"}),
                               pattern({"op":"image","source":"secondary"},duration=3),kind="random",
                               select={"order":"round_robin","allow_self":False,"first":0})
            selector["children"][0]["exit"]={"probability":{"numerator":numerator,"denominator":1},"check_every":4}
            ticks = trace(selector, 13, random.Random(1))
            self.assertEqual([t.frame for t in ticks if t.checked], [0,4,8,12])
            if numerator:
                self.assertEqual([t.frame for t in ticks if t.transitioned], [0,3,4,7,8,11,12])
            else:
                self.assertTrue(all(t.child == 0 for t in ticks))


if __name__ == "__main__":
    unittest.main()

"""Experimental AST contract checks, not playback-equivalence tests."""
import copy
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from pattern_v4 import InvalidPattern, canonical, validate
from pattern_v4_examples import examples, pattern, image
from pattern_v4_lower import lower


class PatternV4Tests(unittest.TestCase):
    def test_eight_examples_are_valid_and_canonical(self):
        for name, document in examples().items():
            with self.subTest(name=name):
                self.assertEqual(json.loads(canonical(document)), document)
                self.assertEqual((ROOT / "docs/exploration/v4/examples" / (name + ".json")).read_text(encoding="utf-8"), canonical(document))
                self.assertIn("pattern v4_scene", lower(document))

    def test_every_pair_of_container_kinds_is_composable(self):
        def wrap(kind, child):
            result = pattern(child, kind=kind, duration=16)
            if kind == "random":
                result["select"] = {"order": "random", "allow_self": False, "first": 0}
            return result
        for outer in ["parallel", "sequential", "random"]:
            for inner in ["parallel", "sequential", "random"]:
                with self.subTest(outer=outer, inner=inner):
                    leaf = pattern(image("primary", 0, 0.5), duration=16)
                    validate({"version": 4, "root": wrap(outer, wrap(inner, leaf))})

    def test_rejects_dead_controls_and_unbounded_sequential_children(self):
        invalid = []
        base = {"version": 4, "root": pattern(image("primary", 0, 0.5), duration=64)}
        for mutation in [
            lambda d: d["root"].update(kind="burst"),
            lambda d: d["root"].update(clock="unused"),
            lambda d: d["root"].update(duration=0),
            lambda d: d["root"].update(select={"order": "random", "allow_self": False, "first": 0}),
            lambda d: d["root"]["children"][0]["node"].update(zoom="[anything]"),
            lambda d: d["root"]["children"][0]["node"].update(source="runtime"),
            lambda d: d["root"]["children"][0]["node"].update(flip_probability=0.5),
            lambda d: d["root"]["children"][0]["node"].update(animation={"every": 0}),
            lambda d: d["root"]["children"][0]["node"].update(selection_probability=1.1),
            lambda d: d["root"]["children"][0]["node"].update(zoom={"curve": {"from": 0, "to": 1, "scope": "missing"}}),
            lambda d: d["root"]["children"][0]["node"].update(envelope={"in": 32, "hold": 32, "out": 32}),
            lambda d: d["root"]["children"][0].update(node=[]),
        ]:
            d = copy.deepcopy(base)
            mutation(d)
            invalid.append(d)
        invalid.append({"version": 4, "root": pattern(pattern(image("primary", 0, 1)), kind="sequential", duration=64)})
        for document in invalid:
            with self.subTest(document=document), self.assertRaises(InvalidPattern):
                validate(document)

    def test_probability_endpoints_rational_and_self_policy(self):
        for p in [0, 1, {"numerator": 1, "denominator": 12}]:
            d = examples()["super_fast"]
            d["root"]["children"][0]["node"]["children"][0]["exit"]["probability"] = p
            validate(d)
        d = examples()["super_fast"]
        d["root"]["children"][0]["node"]["select"]["allow_self"] = True
        with self.assertRaises(InvalidPattern):
            validate(d)
        d["root"]["children"][0]["node"]["select"]["order"] = "random"
        validate(d)
        with self.assertRaises(InvalidPattern):
            lower(d)
        d = {"version": 4, "root": pattern(
            pattern(pattern(image("primary", 0, 1), duration=8),
                    pattern(image("secondary", 0, 1), duration=8),
                    kind="sequential", duration=16, cadence=16), duration=16)}
        validate(d)
        with self.assertRaisesRegex(InvalidPattern, "sequential cadence"):
            lower(d)

    def test_sampled_duration_alignment_and_private_image_guard(self):
        d = examples()["super_fast"]
        d["root"]["children"][0]["node"]["children"][1]["node"]["duration"]["max"] = 127
        with self.assertRaises(InvalidPattern):
            validate(d)
        d = {"version": 4, "root": pattern(image("primary", 0, 1), image("secondary", 0, 1), duration=64)}
        validate(d)
        with self.assertRaisesRegex(InvalidPattern, "private image ownership"):
            lower(d)

    def test_review_counterexamples_do_not_silently_lower(self):
        d = examples()["animation"]
        d["root"]["children"][1]["node"]["children"][0]["node"]["zoom"] = {"triangle": {"from": 0, "peak": 1, "to": 0}}
        with self.assertRaises(InvalidPattern):
            validate(d)
        d = examples()["parallel"]
        d["root"]["id"] = "authored_root"
        d["root"]["children"][0]["node"]["children"][0]["node"]["zoom"]["curve"]["scope"] = "authored_root"
        source = lower(d)
        self.assertIn("pattern authored_root", source)
        self.assertIn("over authored_root", source)
        d = examples()["parallel"]
        d["root"]["children"][0]["node"]["id"] = "v4_scene"
        d["root"]["children"][1]["node"]["id"] = "v4scope_1"
        source = lower(d)
        self.assertIn("pattern v4_scene_root", source)
        self.assertEqual(source.count("pattern v4scope_1 "), 1)
        d = examples()["super_fast"]
        d["root"]["children"][0]["node"]["offset"] = 8
        validate(d)
        with self.assertRaises(InvalidPattern):
            lower(d)

    def test_either_image_uses_one_shared_theme_side(self):
        source = lower(examples()["animation"])
        self.assertIn("image alternate chance 0.5", source)
        self.assertNotIn("image runtime", source)
        self.assertIn("caption runtime", source)

    def test_short_parallel_children_do_not_silently_repeat(self):
        # V4 selects once at frame0, then ends at8; V3 would select at0,8,16,24.
        for repeat in [1, 2]:
            child = pattern(image("primary", 0, 1), duration=8, repeat=repeat)
            document = {"version": 4, "root": pattern(child, duration=32)}
            validate(document)
            with self.assertRaisesRegex(InvalidPattern, "inactive-tail scheduling"):
                lower(document)
        # An inherited owner duration has the same problem one level deeper.
        child = pattern(pattern(image("primary", 0, 1), duration=8))
        document = {"version": 4, "root": pattern(child, duration=32)}
        validate(document)
        with self.assertRaisesRegex(InvalidPattern, "inactive-tail scheduling"):
            lower(document)
        # Full-span and clipped children have no extra inactive tail to preserve.
        for duration in [32, 64]:
            document = {"version": 4, "root": pattern(pattern(image("primary", 0, 1), duration=duration), duration=32)}
            self.assertIn("pattern v4_scene", lower(document))


if __name__ == "__main__":
    unittest.main()

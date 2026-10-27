"""Explicit translations of all eight current builtins into the experimental AST."""
from pathlib import Path
from pattern_v4 import canonical


def curve(a, b, scope="local"):
    return {"curve": {"from": a, "to": b, "scope": scope}}


def image(source, a, b, every=None, scope="local", **properties):
    value = {"op": "image", "source": source, "zoom": curve(a, b, scope), **properties}
    if every is not None:
        value["animation"] = {"every": every}
    return value


def text(op, source, **properties):
    return {"op": op, "source": source, **properties}


def pattern(*children, kind="parallel", **properties):
    return {"kind": kind, "children": [{"node": child} for child in children], **properties}


def cadence(frames, *outputs, **properties):
    return pattern(*outputs, cadence=frames, **properties)


def spiral(speed):
    return {"op": "spiral", "speed": speed}


def examples():
    accelerate_image = image("alternate", 0, 0.1, 4, flip_probability=0.5,
                             zoom_baseline=curve(0, 0.4, "scene"))
    accelerate_image["zoom"] = {"sum": [curve(0, 0.4, "scene"), curve(0, 0.1)]}
    accelerate = pattern(
        cadence({"ramp": {"from": 56, "to": 12, "steps": 140, "ease": "early"}},
                accelerate_image, text("word", "primary", visibility=[0, 0.25], selection_probability=0.5), duration=2048),
        spiral(curve(1, 4, "scene")), duration=2048)
    slow = pattern(cadence(64, image("primary", 0, 0.5, 2)),
                   cadence(64, text("caption", "primary")), spiral(2), duration=1024)
    fast = pattern(cadence(8, image("secondary", 0, 0.8, scope="fast_section")),
                   cadence(16, text("word", "secondary")), spiral(4), duration=512, id="fast_section")
    slow_flash = pattern(slow, fast, kind="sequential", duration=1536, repeat=2)
    sub_text = pattern(cadence(64, image("either", 0, 0.375, 3)),
                       cadence(32, text("subtext", "secondary")), spiral(4), duration=1024)
    flash_text = pattern(cadence(128, image("secondary", 0, 0.8)),
                         cadence(128, image("secondary", 0, 0.8, opacity={"triangle": {"from": 0, "peak": 1, "to": 0}}), offset=64),
                         cadence(64, text("word", "secondary")), cadence(32, text("caption", "primary")), spiral(2), duration=1024)
    parallel = pattern(cadence(64, image("primary", 0, 0.5, 3)),
                       cadence(64, image("secondary", 0, 0.5, 3, opacity=0.5), offset=32),
                       cadence(32, text("caption", "primary")), spiral(3), duration=2048)
    super_parallel = pattern(cadence(96, image("primary", 0, 0.875, 2)),
                             cadence(96, image("primary", 0, 0.875, opacity=0.5), offset=32),
                             cadence(96, image("secondary", 0, 0.875, opacity=0.33), offset=64),
                             cadence(32, text("word", "either")), spiral(3), duration=1152)
    animation = pattern(cadence(64, image("either", 0, 0.625, 1)),
                        cadence(64, image("secondary", 0.5, 0.625, envelope={"in": 8, "hold": 16, "out": 8}), offset=48),
                        cadence(32, text("caption", "either")), spiral(3), duration=1024)
    rapid = pattern(cadence(8, image("either", 0.0625, 0.1875)))
    held = pattern(image("either", 0.1, 0.7, 1), duration={"min": 64, "max": 128, "step": 8})
    selector = pattern(rapid, held, kind="random", select={"order": "round_robin", "allow_self": False, "first": 0, "cooldown": 64})
    selector["children"][0]["exit"] = {"probability": {"numerator": 1, "denominator": 12}, "check_every": 8}
    super_fast = pattern(selector, cadence(8, text("word", "primary", selection_probability=0.25)), spiral(3), duration=2048)
    return {name: {"version": 4, "root": root} for name, root in [
        ("accelerate", accelerate), ("slow_flash", slow_flash), ("sub_text", sub_text),
        ("flash_text", flash_text), ("parallel", parallel), ("super_parallel", super_parallel),
        ("animation", animation), ("super_fast", super_fast)]}


if __name__ == "__main__":
    destination = Path(__file__).resolve().parents[1] / "docs/exploration/v4/examples"
    destination.mkdir(parents=True, exist_ok=True)
    for name, document in examples().items():
        (destination / (name + ".json")).write_text(canonical(document), encoding="utf-8")

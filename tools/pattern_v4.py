"""Experimental V4 AST validation and canonical JSON. This is not the playback compiler."""
import argparse
import json
import math
from pathlib import Path


class InvalidPattern(ValueError):
    pass


def validate(document):
    """Validate a strict, finite-data AST; raise a path-qualified InvalidPattern."""
    ids = set()

    def fail(path, message):
        raise InvalidPattern(f"{path}: {message}")

    def obj(value, required, optional, path):
        if not isinstance(value, dict):
            fail(path, "expected object")
        if set(value) - set(required) - set(optional):
            fail(path, "unknown fields: " + ", ".join(sorted(set(value) - set(required) - set(optional))))
        if set(required) - set(value):
            fail(path, "missing fields: " + ", ".join(sorted(set(required) - set(value))))

    def number(value, path, minimum=None, maximum=None, integer=False):
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
            fail(path, "expected finite number")
        if integer and not isinstance(value, int):
            fail(path, "expected integer")
        if minimum is not None and value < minimum or maximum is not None and value > maximum:
            fail(path, "number outside allowed range")

    def probability(value, path):
        if isinstance(value, dict):
            obj(value, ["numerator", "denominator"], [], path)
            number(value["denominator"], path, 1, integer=True)
            number(value["numerator"], path, 0, value["denominator"], integer=True)
        else:
            number(value, path, 0, 1)

    def duration(value, path, sampled=True):
        if isinstance(value, int) and not isinstance(value, bool):
            number(value, path, 1, 2147483647, integer=True)
            return
        obj(value, ["min", "max", "step"], [], path)
        if not sampled:
            fail(path, "sampled duration is not allowed here")
        for key in value:
            number(value[key], path + "." + key, 1, 2147483647, integer=True)
        if value["max"] < value["min"] or (value["max"] - value["min"]) % value["step"]:
            fail(path, "sample endpoints must be ordered and aligned to step")

    def scalar(value, path, scopes, triangle_allowed=False):
        if not isinstance(value, dict):
            number(value, path)
            return
        if "triangle" in value:
            if not triangle_allowed:
                fail(path, "triangle is an opacity shape only")
            obj(value, ["triangle"], [], path)
            obj(value["triangle"], ["from", "peak", "to"], [], path)
            for item in value["triangle"].values():
                number(item, path)
        elif "sum" in value:
            obj(value, ["sum"], [], path)
            if not isinstance(value["sum"], list) or not 2 <= len(value["sum"]) <= 8:
                fail(path, "sum needs two through eight terms")
            for index, item in enumerate(value["sum"]):
                scalar(item, f"{path}.sum[{index}]", scopes)
        else:
            obj(value, ["curve"], [], path)
            curve = value["curve"]
            obj(curve, ["from", "to"], ["ease", "scope"], path)
            number(curve["from"], path)
            number(curve["to"], path)
            if curve.get("ease", "linear") not in ["linear", "early", "late"]:
                fail(path, "unknown curve easing")
            scope = curve.get("scope", "local")
            if scope not in ["local", "scene"] + scopes:
                fail(path, "curve scope must be local, scene, or an ancestor id")

    def output(value, path, scopes):
        obj(value, ["op"], ["source", "flip_probability", "selection_probability", "zoom", "zoom_baseline", "opacity", "visibility", "envelope", "animation", "speed"], path)
        op = value["op"]
        if op not in ["image", "word", "caption", "subtext", "line", "spiral"]:
            fail(path, "unknown output operation")
        if op == "spiral":
            if set(value) - {"op", "speed"}:
                fail(path, "spiral only supports speed")
        else:
            if value.get("source") not in ["primary", "secondary", "either", "alternate"]:
                fail(path, "media source required")
            if value["source"] == "alternate" and op != "image":
                fail(path, "alternate currently belongs to image selections")
        if "flip_probability" in value:
            if value.get("source") != "alternate":
                fail(path, "flip_probability requires alternate source")
            probability(value["flip_probability"], path)
        if "selection_probability" in value:
            probability(value["selection_probability"], path)
        for key in ["zoom", "zoom_baseline", "opacity", "speed"]:
            if key in value:
                if key == "speed" and op != "spiral" or key in ["zoom", "zoom_baseline"] and op != "image" or key == "opacity" and op not in ["image", "caption", "subtext"]:
                    fail(path, f"{key} has no supported effect on {op}")
                scalar(value[key], path + "." + key, scopes, triangle_allowed=key == "opacity")
        if "animation" in value:
            if op != "image":
                fail(path, "animation requires image")
            obj(value["animation"], ["every"], [], path)
            number(value["animation"]["every"], path, 1, integer=True)
        if "visibility" in value:
            interval = value["visibility"]
            if not isinstance(interval, list) or len(interval) != 2:
                fail(path, "visibility needs [start,end] fractions")
            for item in interval:
                number(item, path, 0, 1)
            if interval[0] >= interval[1]:
                fail(path, "visibility interval must increase")
        if "envelope" in value:
            if op not in ["image", "caption", "subtext"]:
                fail(path, "envelope unsupported on this output")
            obj(value["envelope"], ["in", "hold", "out"], [], path)
            for key, item in value["envelope"].items():
                number(item, path, 0 if key == "hold" else 1, integer=True)
        if "opacity" in value and "envelope" in value:
            fail(path, "choose opacity or envelope, not overwriting modifiers")

    def node(value, path, ancestors, parent_kind=None, depth=0):
        if depth > 64:
            fail(path, "maximum nesting depth is 64")
        obj(value, ["kind", "children"], ["id", "duration", "repeat", "cadence", "offset", "select"], path)
        kind = value["kind"]
        if kind not in ["parallel", "sequential", "random"]:
            fail(path, "only parallel, sequential, random are composition kinds")
        scopes = list(ancestors)
        if "id" in value:
            label = value["id"]
            if not isinstance(label, str) or not label.isidentifier() or not label.isascii() or label in ids or label in ["local", "scene", "this", "self"]:
                fail(path, "id must be a unique ASCII identifier outside reserved clock aliases")
            ids.add(label)
            scopes.append(label)
        if "duration" in value:
            duration(value["duration"], path + ".duration")
        if "repeat" in value:
            if value["repeat"] != "forever":
                number(value["repeat"], path + ".repeat", 1, integer=True)
            if "duration" not in value:
                fail(path, "repeat needs a bounded iteration duration")
        if "offset" in value:
            number(value["offset"], path + ".offset", 0, integer=True)
        cadence = value.get("cadence")
        if cadence is not None:
            if isinstance(cadence, int) and not isinstance(cadence, bool):
                number(cadence, path + ".cadence", 1, integer=True)
            else:
                obj(cadence, ["ramp"], [], path)
                ramp = cadence["ramp"]
                obj(ramp, ["from", "to", "steps", "ease"], [], path)
                for key in ["from", "to", "steps"]:
                    number(ramp[key], path + ".cadence." + key, 1, integer=True)
                if ramp["ease"] not in ["early", "late", "linear"]:
                    fail(path, "unknown ramp easing")
                if not isinstance(value.get("duration"), int) or ramp["steps"] > value["duration"]:
                    fail(path, "ramp needs integer duration with at least one frame per step")
        children = value["children"]
        if not isinstance(children, list) or not children:
            fail(path, "nonempty children array required")
        if kind == "random":
            select = value.get("select")
            obj(select, ["order", "allow_self", "first"], ["cooldown"], path + ".select")
            if select["order"] not in ["random", "round_robin"] or not isinstance(select["allow_self"], bool):
                fail(path, "invalid selection policy")
            if select["order"] == "round_robin" and select["allow_self"]:
                fail(path, "round_robin visits the next child; allow_self only applies to random order")
            number(select["first"], path, 0, len(children) - 1, integer=True)
            if "cooldown" in select:
                number(select["cooldown"], path, 0, integer=True)
        elif "select" in value:
            fail(path, "selection policy only belongs to random composition")
        for index, edge in enumerate(children):
            edge_path = f"{path}.children[{index}]"
            obj(edge, ["node"], ["exit"], edge_path)
            child = edge["node"]
            if not isinstance(child, dict):
                fail(edge_path, "node must be an object")
            if "op" in child:
                if kind != "parallel":
                    fail(edge_path, "sequential/random children must be patterns; wrap an output in parallel")
                output(child, edge_path + ".node", scopes)
            else:
                node(child, edge_path + ".node", scopes, kind, depth + 1)
                if kind == "sequential" and ("duration" not in child or child.get("repeat") == "forever"):
                    fail(edge_path, "sequential child needs finite duration and repeat")
                if kind == "random" and "exit" not in edge:
                    if "duration" not in child or child.get("repeat") == "forever":
                        fail(edge_path, "random child needs completion or explicit chance exit")
            if "exit" in edge:
                if kind != "random":
                    fail(edge_path, "chance exit belongs to random child edges")
                obj(edge["exit"], ["probability", "check_every"], [], edge_path)
                probability(edge["exit"]["probability"], edge_path)
                number(edge["exit"]["check_every"], edge_path, 1, integer=True)
            if "envelope" in child:
                clock = cadence if isinstance(cadence, int) else value.get("duration")
                if not isinstance(clock, int) or sum(child["envelope"].values()) > clock:
                    fail(edge_path, "envelope needs a known fitting local duration")

    obj(document, ["version", "root"], [], "$")
    if document["version"] != 4:
        fail("$.version", "expected version 4")
    node(document["root"], "$.root", [])
    if "duration" not in document["root"]:
        fail("$.root", "root needs a finite iteration duration")
    return document


def canonical(document):
    validate(document)
    return json.dumps(document, indent=2, sort_keys=True, ensure_ascii=False, allow_nan=False) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--format", action="store_true", help="print canonical validated JSON")
    args = parser.parse_args()
    try:
        document = json.loads(args.source.read_text(encoding="utf-8"))
        result = canonical(document)
    except (InvalidPattern, ValueError, OSError, TypeError) as error:
        parser.exit(1, str(error) + "\n")
    print(result if args.format else "Valid experimental V4 AST", end="" if args.format else "\n")


if __name__ == "__main__":
    main()

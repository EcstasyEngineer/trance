"""Restricted V4-to-V3 experiment adapter. Generic random scheduling is unsupported.

The exact two-child Super Fast controller can lower to the existing burst runtime.
Image either uses an existing fair alternate flip to resolve the theme once for
still and animation. This preserves the side distribution, not seeded RNG traces.
The adapter cannot isolate the engine's shared animation/text channels.
"""
from pattern_v4 import InvalidPattern, validate


def lower(document):
    validate(document)
    serial = 0
    used_names = set()

    def reserve(node):
        if "id" in node:
            used_names.add(node["id"])
        for edge in node.get("children", []):
            reserve(edge["node"])
    reserve(document["root"])
    root_name = document["root"].get("id", "v4_scene")
    if "id" not in document["root"]:
        while root_name in used_names:
            root_name += "_root"
    used_names.add(root_name)

    def generated_name(node):
        nonlocal serial
        if "id" in node:
            return node["id"]
        while True:
            serial += 1
            name = f"v4scope_{serial}"
            if name not in used_names:
                used_names.add(name)
                return name

    def chance(p):
        return p["numerator"] / p["denominator"] if isinstance(p, dict) else p

    def media_chance(p):
        value = chance(p)
        if not 0.01 <= value <= 0.99 or abs(value * 100 - round(value * 100)) > 1e-9:
            raise InvalidPattern("V3 adapter cannot preserve media probability outside exact 1..99 percent buckets")
        return value

    def expression(value, scope):
        if isinstance(value, (int, float)):
            return str(value)
        if "sum" in value:
            return " + ".join("(" + expression(term, scope) + ")" for term in value["sum"])
        c = value["curve"]
        clock = {"scene": root_name, "local": "this"}.get(c.get("scope", "local"), c.get("scope"))
        progress = clock + ".progress"
        if c.get("ease") == "early":
            progress = f"(1-(1-{progress})^3)"
        elif c.get("ease") == "late":
            progress = f"({progress}^3)"
        return f"{c['from']} + ({c['to']}-{c['from']})*{progress}"

    def scalar(value):
        if isinstance(value, (int, float)):
            return str(value)
        if "sum" in value:
            return "[" + expression(value, None) + "]"
        c = value["curve"]
        suffix = ""
        if c.get("ease", "linear") != "linear":
            suffix += " ease " + c["ease"]
        if c.get("scope", "local") != "local":
            suffix += " over " + (root_name if c["scope"] == "scene" else c["scope"])
        return f"(curve {c['from']} -> {c['to']}{suffix})"

    def output(value):
        op = value["op"]
        if op == "spiral":
            return "spiral" + (" speed " + scalar(value["speed"]) if "speed" in value else "")
        source = "runtime" if value["source"] == "either" else value["source"]
        coherent_either = value["source"] == "either" and op == "image"
        if coherent_either:
            source = "alternate"
        result = op + " " + source
        if coherent_either:
            result += " chance 0.5"
        if "flip_probability" in value:
            result += " chance " + str(media_chance(value["flip_probability"]))
        for field, token in [("zoom", "zoom"), ("zoom_baseline", "origin"), ("opacity", "alpha")]:
            if field in value:
                if isinstance(value[field], dict) and "triangle" in value[field]:
                    if value[field]["triangle"] != {"from": 0, "peak": 1, "to": 0}:
                        raise InvalidPattern("adapter only supports unit opacity triangle")
                    result += " fade inout"
                else:
                    result += " " + token + " " + scalar(value[field])
        if "envelope" in value:
            e = value["envelope"]
            result += f" env in {e['in']}f hold {e['hold']}f out {e['out']}f"
        if "visibility" in value:
            result += " show " + "..".join(map(str, value["visibility"]))
        if "animation" in value:
            n = value["animation"]["every"]
            result += " anim" + (f" every {n}th" if n > 1 else "")
        if "selection_probability" in value:
            p = media_chance(value["selection_probability"])
            result += " chance " + str(p)
        return result

    def selector(node, inherited):
        if set(node) - {"kind", "children", "select", "id"}:
            raise InvalidPattern("audited selector adapter cannot discard timing properties")
        policy = node["select"]
        children = node["children"]
        if len(children) != 2 or policy != {"order": "round_robin", "allow_self": False, "first": 0, "cooldown": 64}:
            raise InvalidPattern("generic V4 selector has no V3 lowering; reference scheduler only")
        rapid, held = children
        if rapid.get("exit") != {"probability": {"numerator": 1, "denominator": 12}, "check_every": 8} or held["node"].get("duration") != {"min": 64, "max": 128, "step": 8} or "exit" in held:
            raise InvalidPattern("only audited Super Fast selector shape can lower to V3")
        if rapid["node"]["kind"] != "parallel" or held["node"]["kind"] != "parallel":
            raise InvalidPattern("Super Fast adapter requires parallel child phases")
        if set(rapid["node"]) - {"kind", "children", "id"} or set(held["node"]) - {"kind", "children", "id", "duration"}:
            raise InvalidPattern("audited selector adapter cannot discard phase timing properties")
        return "burst period 8f chance 1/12 cooldown 64f duration 64f..128f {\nbase {\n" + body(rapid["node"], inherited) + "\n}\nburst {\n" + body(held["node"], inherited) + "\n}\n}"

    def body(node, inherited):
        children = node["children"]
        images = sum(1 for edge in children if edge["node"].get("op") == "image")
        if images > 1:
            raise InvalidPattern("V3 adapter requires separate image patterns to preserve private image ownership")
        if node["kind"] == "parallel":
            for edge in children:
                child = edge["node"]
                duration = child.get("duration")
                if isinstance(duration, int):
                    repeats = child.get("repeat", 1)
                    if repeats != "forever" and duration * repeats < inherited:
                        raise InvalidPattern("V3 restarts short parallel children; V4 one-shot child needs unsupported inactive-tail scheduling")
        return "\n".join(output(edge["node"]) if "op" in edge["node"] else emit(edge["node"], inherited) for edge in children)

    def emit(node, inherited, root=False):
        if node["kind"] == "random":
            return selector(node, inherited)
        if node["kind"] == "sequential" and "cadence" in node:
            raise InvalidPattern("V3 adapter cannot preserve sequential cadence body ordering")
        name = root_name if root else generated_name(node)
        duration = node.get("duration", inherited)
        if not isinstance(duration, int):
            raise InvalidPattern("sampled duration only supported by audited selector adapter")
        repeat = node.get("repeat", 1)
        if repeat == "forever":
            raise InvalidPattern("unbounded repeat has no generic V3 adapter")
        header = f"pattern {name} for {duration}f"
        if repeat > 1:
            header += f" loop {repeat}"
        if node["kind"] == "sequential":
            header += " seq"
            total = 0
            for edge in node["children"]:
                child = edge["node"]
                if not isinstance(child.get("duration"), int):
                    raise InvalidPattern("sampled sequential child has no V3 adapter")
                total += child["duration"] * child.get("repeat", 1)
            if total < duration:
                raise InvalidPattern("V3 implicitly restarts short sequences; V4 adapter requires sequence fill or parent clipping")
        inner = body(node, node.get("cadence", duration) if isinstance(node.get("cadence", duration), int) else duration)
        if "cadence" in node:
            c = node["cadence"]
            if isinstance(c, int):
                schedule = f"every {c}f"
            else:
                r = c["ramp"]
                schedule = f"every ramp {r['from']}f -> {r['to']}f steps {r['steps']} ease {r['ease']}"
            if node.get("offset", 0):
                schedule += f" offset {node['offset']}f"
            inner = schedule + " {\n" + inner + "\n}"
        elif node.get("offset", 0):
            raise InvalidPattern("offset without cadence has no generic V3 adapter")
        return header + " {\n" + inner + "\n}"

    return emit(document["root"], document["root"]["duration"], root=True) + "\n"

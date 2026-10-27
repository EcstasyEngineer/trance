"""Executable V4 selector contract, independent of playback. Children are opaque timers.

This validates selection/exit semantics, not texture rendering or complete V4 scheduling.
"""
from dataclasses import dataclass


@dataclass(frozen=True)
class Tick:
    frame: int
    child: int
    local_frame: int
    duration: int | None
    checked: bool
    transitioned: bool


def trace(selector, frames, rng, owner_frames=None):
    """rng.randrange(n) supplies deterministic choice and rational chance draws."""
    if owner_frames is not None and owner_frames < 1:
        raise ValueError("owner_frames must be positive")
    edges = selector["children"]
    policy = selector["select"]
    for timer in [selector] + [edge["node"] for edge in edges]:
        if any(key in timer for key in ["cadence", "offset"]) or timer.get("repeat", 1) != 1:
            raise ValueError("opaque-timer reference does not simulate cadence, offset or repeat")

    active = policy["first"]
    entered = 0
    lifetime = None
    cooldown_until = 0
    output = []

    def sample(index):
        value = edges[index]["node"].get("duration")
        if isinstance(value, dict):
            choices = 1 + (value["max"] - value["min"]) // value["step"]
            return value["min"] + value["step"] * (rng.randrange(choices) if choices > 1 else 0)
        return value

    def choose(index):
        if policy["order"] == "round_robin":
            return (index + 1) % len(edges)
        options = [i for i in range(len(edges)) if policy["allow_self"] or i != index]
        return options[rng.randrange(len(options))] if options else index

    def hit(probability):
        if isinstance(probability, dict):
            return rng.randrange(probability["denominator"]) < probability["numerator"]
        return rng.random() < probability

    # A finite first child's duration is sampled exactly once on entry. An initial
    # eligible chance transition is evaluated before output operations execute.
    lifetime = sample(active)
    owner_start = 0
    for frame in range(frames):
        if owner_frames is not None and frame and frame % owner_frames == 0:
            owner_start = frame
            active = policy["first"]
            entered = frame
            lifetime = sample(active)
            cooldown_until = frame
        transition = lifetime is not None and frame - entered >= lifetime
        checked = False
        if not transition:
            exit_rule = edges[active].get("exit")
            if exit_rule and frame >= cooldown_until and (frame - owner_start) % exit_rule["check_every"] == 0:
                checked = True
                transition = hit(exit_rule["probability"])
        if transition:
            active = choose(active)
            entered = frame
            lifetime = sample(active)
            cooldown_until = frame + policy.get("cooldown", 0)
        output.append(Tick(frame, active, frame - entered, lifetime, checked, transition))
    return output

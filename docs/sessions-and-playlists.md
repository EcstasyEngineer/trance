# Sessions and playlists

Session JSON contains themes, programs, a playlist, and optional variables.
Its parent directory is the media root. See [session JSON](session-json-format.md)
for field definitions and [audio](audio.md) for sound configuration.

## Load and save

Run `trance.exe path/to/my.session.json`. A malformed explicitly named session
fails with a diagnostic. With no argument, the player loads `./default.json`.
If no default exists, it scans the working directory, creates a default, saves
it, and reloads it. An existing invalid default is not overwritten.

The loader parses JSON, reads pattern files, expands scans, resolves inheritance,
and validates the session. Validation repairs references/ranges and supplies
essential defaults. The runtime uses `trance_pb` messages plus file/scan metadata
in a sidecar.

F2 saves committed edits to the loaded path; comment keys are not preserved.
Export writes a copy. `--export_archive out.trance` writes `session.json` and
referenced assets in a standard ZIP; extract it to play it.

Legacy upstream text-protobuf `.session` files migrate to a JSON sibling without
replacing the original. `trance --lint old.session` uses this load path without
a window. Fork-specific fields are outside the frozen legacy importer.

## Themes

A theme's `scan` reads one directory's direct files on load. `exclude` holds
files out. `inherit` adds the nearest ancestor theme's resolved pool, continuing
upward while ancestors also inherit. Descendants and siblings are not included.

`theme_scan_root` defaults to `"."` and discovers new theme directories on load.
Use `auto_rescan: false` in its object form to freeze the theme list. Missing folders
retain their entries/settings. Loose root files form `/root/`. Explicit media
lists remain supported where no matching nonempty folder can replace them.

The engine has **two live slots**, primary and secondary. The four queue positions
are unloading, primary, secondary, and loading-next; the other two are not extra
simultaneously addressable themes.

| Session pins | Selection |
|---|---|
| None | Weighted rotation. |
| One | Keep it resident; other weighted themes can occupy the other slot. |
| Two | Those themes form the live pair. |
| Three or more | Choose two slots from the pinned set. |

Runtime `theme pin` clears rotation candidates: one name occupies both slots.
This override is separate from saved pin flags.

Inherited **images** use source-tier weights, then select within that tier;
loading uses the same weighting. A source with no positive rotation weight gets
tier weight 1. Animations, text, and audio use flat merged pools.

## Playlist flow

Each named item contains exactly one of:

- `standard`: program name and duration in seconds.
- `subroutine`: ordered playlist item names.

Items can also carry audio events and weighted `next_item` branches.
`first_playlist_item` names the entry point.

A standard item holds until its duration expires, then chooses an enabled branch
by weight. **Zero duration permits immediate transition.** With no eligible
branch, a top-level item holds indefinitely; a subroutine child returns instead.

A conditional branch requires the live variable to equal its specified value.
Unconditional branches join the same weighted pool. Supply variables with
`--variables="name=value;other=value"`; backslash escapes `;`, `=`, and
backslash. The current player does not apply stored variable defaults;
an unspecified value compares as an empty string.

Subroutines push children onto a stack. Children can branch or call subroutines.
A child finishing without a branch returns to the next child in its caller.
The stack limit is 256. Entering an item fires audio cues and, for standard
items, selects its program. Pause freezes the switch clock.

**Limitation:** a cycle of immediately transitioning items can keep
`PlaylistRunner::advance` looping forever. The depth guard does not bound this
loop. Give cyclic standard-item paths positive durations.

## Example

With programs `intro` and `main`, this playlist runs the introduction for
30 seconds, then holds main indefinitely:

```json
{
  "first_playlist_item": "opening",
  "playlist": {
    "opening": {
      "standard": { "program": "intro", "play_time_seconds": 30 },
      "next_item": [{ "playlist_item_name": "hold", "random_weight": 1 }]
    },
    "hold": { "standard": { "program": "main" } }
  }
}
```

Implementation: [session.cpp](../src/common/session.cpp),
[session_json.cpp](../src/common/session_json.cpp),
[playlist_runner.cpp](../src/trance/playlist_runner.cpp),
[theme_bank.cpp](../src/trance/theme_bank.cpp).

# Audio

Playlist music channels, one pattern-controlled theme-audio slot, and a synthesized
bed mix during playback. `Audio` owns all three; global mute applies to all.

## Playlist music

Each playlist item's `audio_event` list runs when the item is entered.

| Type | Behavior |
|---|---|
| `play` | Open root-relative `path`, set `loop` and `volume`, then play on `channel`. |
| `stop` | Stop that channel. |
| `fade` | Ramp from current volume to `volume` over `time_seconds`. |

Volume is 0–100. Channels are created on demand. A play event with
`next_unused_channel: true` searches upward from `channel` for one that is not
playing, allowing overlapping cues. Scanned audio extensions are WAV, FLAC, OGG,
and AIFF. Pause freezes playback and fade clocks; mute preserves underlying gains.

## Theme audio

A theme's `audio_path` pool supplies recorded files to the pattern language's
`audio` effect. One dedicated object is separate from indexed playlist channels.
Starting another track replaces this slot's current audio. Pattern volume is
0–1, clamped. The slot initially has full volume, then retains the last volume
set across subsequent tracks; a bare audio draw does not reset it. See the
[grammar](spec-grammar-v3.md) for scheduling and curves.

## Synthesized bed

`EntrainmentStream` synthesizes the program's `entrainment.layer` array at
44.1 kHz stereo.

| Field | Meaning |
|---|---|
| `center_hz` | Carrier center frequency. |
| `binaural_hz` | Left/right frequency difference: left = center − half the difference, right = center + half. Zero gives an unsplit carrier. |
| `pulse_hz` | Independent cosine amplitude modulation. Zero gives a continuous carrier; stereo gates are 180° apart. |
| `amplitude_db` | Relative layer balance. |

Split and pulse can operate simultaneously at different rates. Stereo separation
preserves the left/right split; speaker mixing changes the resulting signal.

`master_db` controls target RMS level. Zero or omission selects **−28 dB**.
Configuration measures two seconds of target synthesis to choose a gain, capped
by a measured peak ceiling of 0.8. That calibration does not prove every later
sample or live transition stays below the ceiling.

Layer levels are relative: lowering the only layer can be compensated by
normalization. Use master level for overall volume and remove layers for silence.

Generated default programs use:

| Center | Binaural | Pulse | Relative level |
|---|---|---|---|
| 312 Hz | 3 Hz | 5 Hz | 0 dB |
| 60 Hz | 3 Hz | 3.25 Hz | −6 dB |

An absent `entrainment` block means no bed. Live changes glide frequencies/gains
over about 300 ms of synthesized audio; stream buffering adds latency. Layers
match by array position, so a middle removal morphs later layers and fades the
excess tail. An emptied live bed fades to silence and continues streaming zeros
to permit reliable re-enable. Unchanged configurations are left alone.

## Controls and saving

**M**, F2 mute, and `mute on|off` share global mute. Hide pauses and mutes;
show restores the prior requested states. F2 bed edits autosave the active program.

Command/MCP bed edits do not initiate a save, but a later F2 save can persist
them because both surfaces edit the same program. Runtime theme/text/visual pins
are separate state. See the [command reference](spec-mcp-ambient-daemon.md).

Implementation: [audio.cpp](../src/trance/media/audio.cpp),
[entrainment.cpp](../src/trance/media/entrainment.cpp).
Fields: [session JSON](session-json-format.md).

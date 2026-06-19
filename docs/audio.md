# Audio

Player-side audio is two independent things mixed under the visuals: a set of
**music channels** triggered by the playlist, and a continuously-synthesised
**entrainment bed**. Both are owned by the `Audio` class
(`src/trance/media/audio.{h,cpp}`), which exists only in realtime mode — video
export has no audio.

## Channels

`Audio` holds a `std::vector<channel>` (`audio.h`), each wrapping an
`sf::Music`. Channels are driven by `AudioEvent` messages (`trance.proto:193`)
attached to playlist items; `TriggerEvents` fires every event on the item the
playlist switches to (`main.cpp` calls it at each switch and subroutine step).

An `AudioEvent` has a `Type`:

- **`AUDIO_PLAY`** — open the file (`root_path + "/" + path`), set loop and
  volume, and play. With `next_unused_channel`, the event scans from `channel`
  upward for the first non-playing channel instead of using a fixed index, so
  overlapping cues stack onto fresh channels.
- **`AUDIO_STOP`** — stop the channel.
- **`AUDIO_FADE`** — linearly ramp the channel volume from its current value to
  `volume` over `time_seconds`. The ramp is interpolated each frame in
  `Audio::Update()`, which `play_session` calls once per loop.

Channels are created on demand: `TriggerEvent` grows the vector until the
requested channel index exists. Supported formats are `.wav`, `.flac`, `.ogg`,
`.aiff` (see `is_audio_file`, `src/common/session.cpp`).

## The entrainment bed

`EntrainmentStream` (`src/trance/media/entrainment.{h,cpp}`) is an
`sf::SoundStream` that synthesises a binaural/isochronic bed on SFML's audio
thread — no audio file involved. It is configured per program from the
`Entrainment` proto message (`trance.proto:105`).

### Layer model

The bed is a sum of `EntrainmentLayer`s (`trance.proto:88`). Each layer is one
carrier oscillator with two optional modulations:

- **Binaural split.** `center_hz` ± `binaural_hz/2` — left ear gets
  `center - beat/2`, right gets `center + beat/2`. `binaural_hz == 0` means a mono
  carrier (no split). Computed in `Configure` (`entrainment.cpp:118`).
- **Isochronic pulse.** `pulse_hz` amplitude-gates the carrier with a unipolar
  (0..1) cosine envelope. `pulse_hz == 0` means continuous (no gate). The gate
  runs the **L/R pulse 180° out of phase** (`entrainment.cpp:50`): when the left
  ear pulses to its peak the right is at its trough. The intent is to entrain with
  or without headphones — the binaural carrier split needs headphones, but the
  anti-phase pulsing still drives on speakers.

`amplitude_db` is the layer's relative level in dB (0 = unity). Synthesis runs in
`synth_frame` (`entrainment.cpp:38`), which advances each layer's phase
accumulators per sample at 44.1 kHz and keeps them wrapped to avoid float drift
over long sessions. Chunks are 0.5 s of stereo each (a deliberately deep buffer:
latency is irrelevant for an ambient bed, and the slack keeps the stream from
underrunning into clicks when the CPU is busy).

### Mixing and the peak-headroom cap

`Configure` (`entrainment.cpp:102`) normalises the whole bed:

1. Layers are summed at their relative gains.
2. `measure()` runs a 2-second copy of the synthesis to get the summed bed's RMS
   and peak at unit master gain.
3. The RMS gain targets `master_db` (default **−28 dB**; `master_db == 0` means
   "unset, use the default").
4. A **peak ceiling** caps the gain so peaks stay below 0.8 (~2 dB headroom):
   `gain = min(gain_rms, peak_ceiling / peak)`. This prevents hard-clip clicks on
   hot or phase-aligned configs; it is inactive for the default bed, whose peaks
   sit well under full scale.

An empty layer list, or a degenerate all-silent bed, leaves the stream stopped
(silence) rather than playing. `Configure` also skips a no-op reconfigure (it
compares the serialized config against the last one) so an unchanged bed keeps
playing across program changes instead of glitching on a stop/restart.

### How the bed defaults into a session

There are **no named presets** — the layers are stored in full in the session, so
a `.session` is self-contained. `set_default_program` (`src/common/session.cpp:81`)
seeds every default program with a two-layer bed:

```
add_layer(312, 3,    5,    0);   // carrier 312 Hz, binaural 3 Hz, pulse 5 Hz, 0 dB
add_layer(60,  3,    3.25, -6);  // carrier 60 Hz,  binaural 3 Hz, pulse 3.25 Hz, -6 dB
```

To disable the bed for a program, delete its layers (an empty `Entrainment.layer`
list). `play_session` pushes the active program's `entrainment()` into the audio
via `Audio::SetEntrainment` → `EntrainmentStream::Configure` at startup and on
every playlist program change.

## Mute toggle

`Audio::ToggleMute()` (bound to **M** in `handle_events`, `main.cpp:80`) flips a
global mute over **every channel and the entrainment bed at once**. It is
implemented with SFML's listener global volume (`sf::Listener::setGlobalVolume`,
0 or 100), so it sits above the per-channel fade logic and is exactly reversible —
the underlying per-channel volumes and the bed's gain are untouched. The F1
overlay shows `[MUTED]` on the entrainment line while muted.

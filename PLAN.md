# ToneLoader

ToneLoader is a unified, mono LV2 guitar plugin for REAPER on Linux AMD64. It
combines two Neural Audio Modeler stages with an impulse-response loader:

```text
input -> pedal NAM -> amp NAM -> IR -> output
```

The plugin runs only at 48 kHz. Keeping the initial target narrow avoids
resampling NAM models and matches the expected sample rate of the supported
models and IRs.

## Scope

- One mono input and one mono output.
- One pedal NAM slot, one amp NAM slot, and one IR slot in a fixed order.
- Hard bypass for each slot, with no crossfade.
- Bypassed slots do not run their DSP processors.
- Model changes may interrupt or click; seamless real-time switching is not a
  goal.
- REAPER on Linux AMD64 is the only supported host and platform initially.
- Other hosts, platforms, sample rates, channel layouts, and configurable
  signal chains are out of scope.

## Upstream sources

- <https://github.com/mikeoliphant/neural-amp-modeler-lv2>
- <https://github.com/brummer10/neural-amp-modeler-ui>
- <https://github.com/brummer10/ImpulseLoader.lv2>

Use upstream projects or their components when they provide a suitable reusable
boundary:

- Use NeuralAudio, the engine behind `neural-amp-modeler-lv2`, for each NAM
  stage rather than embedding complete LV2 plugin instances.
- Use ImpulseLoader's mono convolution engine or its underlying components for
  the IR stage.
- Use libxputty and the upstream UIs as the starting point for the custom LV2
  UI.
- Reimplement integration code where upstream code is coupled to its original
  plugin or does not fit ToneLoader's state and loading model.

Pin every upstream source and submodule to a known revision. Record and comply
with the license of every reused component. In particular,
`neural-amp-modeler-lv2` is GPL-3.0, ImpulseLoader's principal code uses 0BSD,
and FFTConvolver uses MIT; the dependency inventory remains part of the build
work.

Bundle the `Crate Vintage Club 20` NAM model as the factory default amp:

- Source: <https://www.tone3000.com/tones/crate-vintage-club-20-6515>
- Model: `Crate Vintage Club 20`
- License: CC0

Pin the downloaded model artifact used by the build, record its checksum and
source metadata, and include its CC0 notice in the distribution. Do not fetch
the model when the plugin runs.

## Model library

The default library base directory is:

```text
~/Music/ToneLoader/
```

The base directory is a plugin-wide setting shared by all ToneLoader instances.
The UI stores it in:

```text
~/.config/ToneLoader/settings.toml
```

If the settings file or value does not exist, use the default above. Do not
store the base directory separately in each REAPER project.

It contains separate module directories:

```text
~/Music/ToneLoader/
├── pedal/
├── NAM/
└── IR/
```

Each module directory supports:

- A ZIP pack directly in the module directory.
- A pack directory containing supported model files, exactly one directory
  level below the module directory.

For example:

```text
~/Music/ToneLoader/IR/MESA/mesa001.wav
~/Music/ToneLoader/IR/MESA.zip
```

Do not treat files directly in a module directory as models: every model must
belong to either a ZIP pack or a pack directory.

The factory-default model bundled in the LV2 bundle is the sole exception to
the user-library pack layout. Present it as a read-only factory pack in the amp
chooser.

Do not scan directories below the pack-directory level. For example,
`IR/MESA/SM57/mesa001.wav` is ignored. ZIP packs follow the same rule in a
simpler form: only supported files at the ZIP root are considered, and ZIP
directories are ignored.

Supported files are:

- `pedal`: `.nam`
- `NAM`: `.nam`
- `IR`: `.wav`

The module directory names are case-sensitive and must be exactly `pedal`,
`NAM`, and `IR`.

File-extension matching is case-insensitive. IR files must be 48 kHz. Because
the plugin is mono, only mono IR files are supported; incompatible files are
shown in red, expose the validation error on mouse hover, and cannot be loaded.

Reject unsafe ZIP members, including absolute paths and names containing `..`.
Ignore directories and unsupported members. A corrupt pack or invalid model
must not replace the currently loaded model.

## Cache

The UI presents ZIP packs and pack directories uniformly. DSP engines only
receive ordinary extracted or source file paths.

Extract ZIP members outside the audio thread into a persistent cache such as:

```text
~/.cache/toneloader/<pack-fingerprint>/<member-name>
```

A small cache component is shared by the UI and DSP worker. The UI owns model
discovery and browsing, while the worker may rebuild a missing cache entry
during state restoration without requiring the UI to be open. Cache entries
are not automatically evicted in the initial implementation.

Identify a model by its source type and logical location:

- Pack directory file: source path.
- ZIP member: ZIP path, member name, and pack fingerprint.

Fingerprint ZIP packs using file size and modification time. Tone3000 packs are
treated as immutable after download; content hashing and automatic change
detection are out of scope.

Do not use an extracted cache path as the sole persisted identity.

## DSP and loading

All filesystem access, ZIP extraction, model parsing, IR loading, allocation,
and processor preparation happen outside the LV2 audio callback using the LV2
worker extension.

While a model is loading, the existing processor continues to run. When the
replacement is ready, swap processors at an audio-block boundary. No crossfade
is performed. If loading fails, retain the existing committed processor and
report the error to the UI.

Keep loaded processors allocated while bypassed so that enabling a slot is
immediate. Bypass skips processing but does not reduce the slot's memory use.

Controls are:

- Pedal: bypass, model, input, output, and quality.
- Amp: bypass, model, input, output, and quality.
- IR: bypass, model, input, and wet.

ImpulseLoader's input control is gain before the convolution engine. It affects
only the wet signal and does not affect the dry portion of the wet mix.

When a new plugin instance is created, load the bundled `Crate Vintage Club 20`
model into the amp and enable the amp. The pedal and IR modules start empty and
bypassed. Restored REAPER project state takes precedence over these new-instance
defaults. Controls remain adjustable while their module is bypassed. Bypass and
numeric controls are exposed as host-automatable LV2 control ports; model
selection is persisted state but is not automatable.

Use the upstream control ranges and behavior unless integration requires a
documented change.

## UI

Use a flat, modern visual language rather than simulated hardware: charcoal
module cards, thin borders, restrained typography, compact controls, and a
single teal accent for active/current state. Do not use textures, fake lighting,
screws, or photorealistic knobs.

Display the pedal, amp, and IR modules from top to bottom in signal-chain order.
Within each module, display controls from left to right:

1. Bypass.
2. Button to show or hide the model chooser.
3. Current model name.
4. Module control knobs.

Only one model chooser may be open at a time. The chooser replaces the entire
900 by 430 module view instead of resizing the plugin viewport. Its fixed
header shows the module icon, current model, input/output gain controls (input
and wet for IR), and a close button. Temporarily enable a bypassed module while
its chooser is open. Restore its prior bypass state when the chooser closes
without committing; a successful commit leaves the module enabled.

### Model chooser

The embedded chooser has two columns similar to the macOS Finder column view:

- Pack: a ZIP file or one-level pack directory.
- Model: a supported file in that pack.

Show an interactive scrollbar for either column whenever its contents exceed
the visible rows. Mouse-wheel scrolling over either column moves that column.
The chooser does not handle arrow, Escape, or Enter keys because they conflict
with REAPER's host shortcuts.

Scan the selected module directory whenever its chooser opens. Do not use a
filesystem watcher or scan continuously while the chooser remains open.

Trim file extensions and redundant common prefixes or substrings for display
only. Preserve the unmodified source identity internally.

Use the following model states consistently:

- **Current**: committed and persisted.
- **Audition**: temporary and never persisted.
- **Loading**: a pending worker operation.

Chooser behavior is:

- Single-clicking a model auditions it.
- Single-clicking the model already being auditioned is a no-op.
- Double-clicking a model commits it, regardless of whether it is currently
  being auditioned.
- Keep the chooser open while a commit load is pending. Return to the main view
  only after the DSP confirms that the selected model was committed; keep the
  chooser open and show the model in red if loading fails.
- A successful commit enables the selected module (bypass off).
- If the audition processor is already ready, committing promotes it without
  loading it again.
- Otherwise, commit only after the requested model loads successfully.
- Show italic pulsing model text while a model is being auditioned; stop on
  commit, cancellation, or failure.
- Closing the chooser without committing a new model restores the current
  model.
- Closing the plugin UI abandons all auditions and restores current models.
- Closing REAPER or removing the plugin abandons auditions because audition
  state is never serialized.

Clicking the numeric value below a knob opens direct text entry. Accept signed
decimal values for input/output gain and values from 0 through 100 for percent
controls. Enter applies the value and Escape cancels editing.

Double-clicking a knob resets it to its declared default value.

The UI sends distinct audition, commit, and cancel-audition operations so a
temporary path update cannot accidentally become persistent state.

## State

The DSP owns committed state. Persist and restore:

- The logical source identity for the pedal, amp, and IR models.
- Bypass and all control values for every module.
- A state-format version for future migrations.

Do not persist:

- Audition or pending models.
- Chooser visibility or highlighted rows.
- Loading animations.
- Extracted cache paths as the only model identity.

On restoration, resolve pack-directory model paths directly. Resolve ZIP
members through the cache, rebuilding a missing entry on the worker thread. If
a source is missing or invalid, leave that module bypassed, label it as missing
in the UI, and preserve enough identity to show which model could not be
restored.

## Build

Do not install build dependencies on the host.

- Provide a pinned Dockerfile containing all build and test dependencies.
- Provide a Makefile target that builds a release LV2 bundle inside the
  container.
- Provide debug and test targets inside the container.
- Provide an install target that copies the completed bundle to `~/.lv2` on
  the host.
- Pin upstream repositories and submodules for reproducible builds.
- Validate the completed LV2 bundle with `lv2lint` or an equivalent validator.

## Implementation plan

1. **Dependency and license inventory**
   - Pin the upstream revisions and bundled factory model, and document every
     reused component and asset.
   - Verify a clean container can fetch or build the pinned dependency tree.
2. **Three-stage DSP prototype**
   - Implement the fixed mono pedal -> amp -> IR chain and its controls.
   - Verify processing order, 48 kHz enforcement, and that bypassed processors
     are not called.
3. **Asynchronous loading and state**
   - Load extracted NAM and WAV files through the LV2 worker.
   - Verify failed loads retain the previous model and a REAPER project restores
     every committed model and control.
4. **Minimal libxputty UI**
   - Add module controls, plugin-wide settings, and selection from pack
     directories.
   - Verify control synchronization and automation in REAPER.
5. **Library discovery and ZIP cache**
   - Add bounded directory scanning, ZIP validation, extraction, and cache
     reconstruction.
   - Verify one-level pack directories and root-level ZIP members appear
     identically in the chooser; loose and deeper files do not appear.
6. **Audition workflow**
   - Add audition, commit, cancellation, loading feedback, and chooser takeover.
   - Verify single-click, double-click, chooser close, UI close, and failed-load
     behavior.
7. **Packaging and REAPER smoke test**
   - Produce and install the LV2 bundle, including the factory amp model and
     license notices, from a clean container.
   - Verify the release bundle loads, processes audio, saves state, and restores
     state in REAPER on Linux AMD64.

## Completion criteria

ToneLoader is complete for the initial scope when a clean container build
produces an installable LV2 bundle that runs at 48 kHz in REAPER on Linux AMD64;
loads pedal NAM, amp NAM, and mono IR models from the supported library layouts;
loads and enables the bundled Crate factory amp for a new instance while leaving
pedal and IR bypassed; implements the documented audition behavior; skips
bypassed DSP; and restores all committed models and controls without persisting
temporary UI state.

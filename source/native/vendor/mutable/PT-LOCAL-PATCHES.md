# Pocket Tracker portability patches

The original Mutable Instruments authorship and license notices remain unchanged.

## Braids analog output scaling (2026-11-09)

`braids/analog_oscillator.cc`: replace four signed left shifts of centered BLEP
samples in CSaw, Square, Saw and Variable Saw with multiplication by two.
The centered samples can be negative; left shifting a negative signed integer is
undefined in the project's C++17 mode. Multiplication is defined for these bounded
32-bit intermediates and retains the original fixed-point output scaling.

Validation: `tests/studio/braids_pcm.cpp` renders all four affected oscillators
with three macro pairs, three pitches and fractional sync. Its integer PCM
hashes were captured twice from the unmodified beta.1 library before patching.
The patched implementation must match those hashes exactly. The focused check and
the original live-DSP check also run under AddressSanitizer and
UndefinedBehaviorSanitizer, without suppressing signed-shift diagnostics.

The fixture explicitly starts from zeroed storage, matching the original
firmware's static oscillator allocation; the upstream empty constructor does not
initialize all interpolation history. This avoids comparing allocator fill
patterns between native and sanitizer builds.

This is a bounded output-scaling fix, not a general rewrite of the upstream
fixed-point algorithms. Reapply or reassess it when updating the vendor sources.

## Clouds heap lifecycle and active correlation (2026-11-09)

`clouds/dsp/correlator.cc` initializes every search field in `Init`, including
the word increment and search size. The original embedded allocation was static;
reused native heap storage can contain arbitrary values. `EvaluateSomeCandidates`
now returns when no search is pending. Previously an indeterminate signed size
could become an enormous unsigned background-work count on entering Stretch.
An active search keeps its normal candidate count and result.

The candidate bitstream combines the next word only for nonzero bit offsets.
An aligned candidate therefore uses its current word directly, avoiding the
undefined shift by 32 at offset zero.

`clouds/dsp/window.h` re-arms `done_` and clears `half_` in `Window::Start`.
Without this lifecycle reset a fresh or exhausted overlap window remained done,
so Stretch's wet signal was silent.

The host integration in `studio/rack.cpp` also calls Clouds' existing `Prepare`
service between its 48-frame effect blocks. Spectral FFT buffers and Stretch
correlation require this ongoing service. Unchanged modes reuse the existing
workspace; no new C++ allocation occurs in processing or on mode switches.

Validation: `tests/studio/clouds_lifecycle.cpp` covers poisoned/reused storage,
aligned and nonaligned active-search matches, window restarts, silence-to-signal
wet-only output for all four Clouds modes, mode switches and audio allocation
guards. The focused Correlator/Window test runs with ASAN/UBSAN. Initialized
Granular and Loop Delay PCM fingerprints match the previous integration exactly;
Stretch and Spectral intentionally gain their previously missing wet signal.
The complete 256-preset collection is also rendered through the native DSP with
finite-output, level and tail checks. Timing reports are host measurements,
not physical handheld benchmarks.

## ShyFFT backward indexing (2026-11-09)

Full-dependency UBSAN reached a previously inactive Spectral FFT path and found
unsigned pointer-offset overflow. In all six compile-time/runtime forward and
inverse locations, `j` is `size_t`: `di[-j]` and `si[-j]` negate an unsigned value
before pointer arithmetic. They now use `*(di - j)` and `*(si - j)` respectively.
The positive distance is smaller than the current half-block, so the result
remains within the same FFT array. Floating-point arithmetic, twiddle factors
and the intended element addresses are unchanged; no sanitizer is suppressed.

Validation includes the full-dependency Spectral path plus forward/inverse FFT
roundtrips, maintained by the aggregate release check. The earlier focused
Correlator/Window sanitizer run linked normal FFT objects and did not exercise
this internal pointer-arithmetic diagnostic.

## Warps amplifier heap lifecycle (2026-11-10)

`warps/dsp/modulator.h`: `SaturatingAmplifier::Init` now explicitly initializes
`level_`, `pre_gain_`, `post_gain_` and `unclipped_gain_` to zero alongside
`drive_`. `Process` reads the envelope and gain interpolation histories before
writing them. The original firmware's static/BSS allocation supplied zeros,
whereas the class's empty user-provided constructor leaves reused heap bytes
untouched, even when its containing modulator is written as `warps{}`.

A full native factory sweep exposed a crash after preset195: indeterminate
amplifier history produced a huge signal and an out-of-range fold-table index.
The original poisoned-storage probe reproduces the invalid float-to-int cast
and lookup-table access under ASAN/UBSAN. This patch fixes the initialization;
it does not clamp/mask the DSP output, replace a preset, or change any saved ID.

Validation: `tests/studio/warps_init.cpp` compares four seconds of stereo PCM
across all nine integrated Warps modes with five zero/dirty storage patterns,
including repeated use of the same allocation, and exercises production
factory195/196 construction/reset cadence. The fixed PCM also matches the
unmodified zeroed/BSS implementation byte-for-byte in all45 captured comparisons.
The complete256 preset sweep additionally runs with zeroed and poisoned test-only
allocation fills. The original upstream copyright/MIT notice remains unchanged.

## Rings reverb filter histories (2026-11-10)

`rings/dsp/fx/reverb.h`: `Reverb::Init` now resets both feedback low-pass
histories (`lp_decay_1_`, `lp_decay_2_`) to zero. `Process` reads them immediately;
all-NaN reused heap storage otherwise contaminates the String+Reverb model's
feedback loop and can make the integrated rack silent. The caller already sets
amount, input gain, reverb time and tone before processing. The two added resets
match original zeroed/BSS startup, without changing coefficients, preset values,
IDs or output sanitization. The full factory sweep runs under zero and dirty
allocator fills; focused poisoned-storage checks preserve initialized PCM.

The same bounded Rings placement-storage probe found `FMVoice::Process` reading
`previous_sample_` before `FMVoice::Init` had assigned it. Dirty/NaN storage changed
FM PCM and triggered UBSAN on its phase conversion. `Init` now sets that feedback
history to zero as well. This one-field reset preserves the old zeroed/BSS PCM;
it changes neither the feedback amount nor oscillator/phase algorithms.

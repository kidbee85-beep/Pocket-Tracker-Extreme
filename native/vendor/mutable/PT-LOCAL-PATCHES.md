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

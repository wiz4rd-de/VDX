# Hardware capture (SPEC §15.4 human follow-up)

Use these steps to A/B VDX against a DX7/TX7 for the items in §15.4 (AM depth
curve, rates 77–99, detune in Hz, portamento curve, voice stealing, controller
ranges).

1. Program the same test voice on both: INIT VOICE, algorithm 32, OP1 only
   (the §13 reference test voice), then change only the parameter under test
   (e.g. OP1 R2 for decay rates, LFO speed + AMD for the AM curve).
2. Record the instrument's output at 48 kHz/24-bit, one take per setting, note
   C3 (MIDI 60) at velocity 100.
3. Record VDX the same way (standalone, Settings: Mk I, DAC on).
4. Run `python3 tools/capture/analyse.py take.wav --from <s> --to <s>` on
   both and compare slope, frequency and modulation period.
5. Where the hardware differs, adjust the table in `core/fm/dx7tables.h` (or the
   generator `tools/gen_tables.py` for the kernel tables), regenerate the
   goldens (`VDX_UPDATE_GOLDENS=1 ./build/tests/vdx_tests "AC-E1.4*"`) in the same
   commit, and record the change in DECISIONS.md.

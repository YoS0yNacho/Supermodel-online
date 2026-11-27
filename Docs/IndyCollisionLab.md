# Indy 500 local collision investigation

Status: investigation and experimental physics input; no validated fix. This
work is limited to Indy 500 Twin Rev A. ROM addresses below are not validated
for other revisions or games.

**Important impairment correction:** The collision harness originally passed
its fake latency and jitter to the TCP ring but accidentally constructed the
UDP relay without them. Therefore all earlier runs below labeled with simulated
latency applied that impairment to **TCP only**, not UDP. The UDP-primary
observations, fixture bytes and measured replay results remain observations of
those runs, but they are not tests of the stated end-to-end UDP latency. This
also qualifies earlier user-facing descriptions of those runs. The harness now
passes the same impairment object to the UDP relay; fresh captures are needed
for genuine delayed-UDP conclusions.

## Observations

- In the examined ROM, `0xaa690` starts a collision-response routine. It reads
  an index from `actor + 0xfc` at `0xaa6b8`; at `0xaa71c` it writes 30 to
  `actor + 0x104`. The response at `0xaacf0` applies changes over that
  countdown. Prior offline traces associate these writes with close AI pairs.
  New traces also contain indices above 12; do not assume every `+0xfc` value
  identifies one of the 13 tracked cars, or that this is the player's only
  contact path.
- The ROM code at `0xab340` reads a pointer from `actor + 0x15c` and copies
  the pointed-to motion into `actor + 0x124/0x128/0x12c`. At `0xab490` it
  reads the local actor index and writes its `+0x0c/+0x10/+0x14` position to
  the native transmit row. This shows where pose crosses the interface, but
  does not yet prove which network state produces the reported correction.
- UDP race motion is not just the separate draw-only prediction: validated UDP
  movement can populate native receive rows in `ApplyIndyRacePair` in
  `Src/Model2/Model2Comm.cpp`. A solution must therefore consider actual
  collision inputs, not just the displayed pose. The TCP ring remains in use
  for ordered state. Do not infer that the project implements full rollback
  solely from a visible correction.

## Trace analyzer

`Tests/indy_collision_trace.py` summarizes instrumented `P` and `W` records.
For example:

```
python3 Tests/indy_collision_trace.py --first-frame 2300 --local-actor 0 \
  /tmp/dm/twocab/cab0/wlog.txt /tmp/dm/twocab/cab1/wlog.txt
python3 Tests/test_indy_collision_trace.py
```

`--local-actor` is a manual assumption, not detected per cabinet. Snapshots
are at vblank, not exactly when a trigger executes. The nearest tracked car is
not necessarily the partner. The analyzer reports tracked actor coverage and
flags an incomplete final line; do not compare cabinet-local frame numbers as
though they shared a clock. The diagnostic write PC itself needs validation
at each site: earlier investigations found `m_PIP` misleading for other
writes. All-zero snapshots are excluded; individually zero-valued positions
are retained and may represent uninitialized actors.

On the old 13-actor single-player `wlog4` and `wlog5` captures after frame
2300 the analyzer saw 20 trigger writes in each; the first pair was actors 8
and 9, sampled 2.48 units apart. Neither capture had an actor-0 trigger. The
older `wlog2` only tracked seven actor positions even though it contained
writes for actors 8 and 9, so it cannot establish distances for those actors.
These logs end with truncated records.

## Experimental two-cab capture

`Tests/run_indy_collision_lab.py` runs two Xvfb instances and a relay with UDP
support, 100 ms one-way latency and 8 ms jitter. It waits for native links,
attract mode and a two-player active-race predicate before accelerating. It
retries coin/start only for a cabinet still in attract or entry-credit mode.
`Tests/IndyCollisionTrace.patch` is a **diagnostic-only** patch against commit
`905859e`; apply it to a separate clean checkout and build it there, not in the
production tree. It writes sampled states, trigger observations and selected
near-player writes. The output directory must not exist.

```
INDY_TRACE_BINARY=/tmp/dm/trace/src/bin/supermodel \
INDY_COLLISION_OUTPUT=/tmp/dm/indy-collision-new \
RACE_SECONDS=100 python3 Tests/run_indy_collision_lab.py
```

This starts emulators and injects keyboard input only into its own Xvfb
instances. `INDY_DISPLAY_BASE` changes the default display pair `91,92`. It
fails rather than intentionally sending keys if an Xvfb instance fails to
start. Review availability and disk space first: logs can be large. Once race
state is confirmed, the input schedule uses real wall-clock sleeps. Set e.g.
`INDY_STEER=0,Left,2,0.7` to steer cabinet 0 left for 0.7 seconds after a
2-second delay. `python3 Tests/test_indy_collision_lab.py` checks phase parsing.

A run on 2026-11-26 negotiated Indy race UDP and established both native
links, but did **not** log activation of UDP primary, and the final screenshot
showed a boot/attract screen. The input schedule did not independently verify
a live race. Its write logs contain zero observed trigger writes; this is
inconclusive, not evidence that collisions are fixed. Its output is currently
under `/tmp/dm/twocab-straight-udp/` and may be deleted by the system.

## Local echo prototype — 2026-11-26

The revised harness reached active two-player races on both cabinets and logs
confirmed UDP primary. Their local actor indices were 0 and 1 respectively.
At `0xab37c/0xab3ac` the ROM also copies the **local** actor's X/Z from its
receive-row pointer (`0x01a123a4` in the experimental run) to the actor's
`+0x124/+0x12c` cache. Its live physics coordinates are at `+0x0c/+0x14`.
In the unmodified steered run, selected same-frame writes when the two cached
player positions were within 10 units differed from local physics writes by a
mean of 21.86 units on cabinet 0 (102 frame pairs) and 10.13 on cabinet 1
(21 frame pairs). These writes occur at different points within a frame, so
this is not an exact measurement of echo latency.

The diagnostic patch contains an opt-in, **not production-ready** experiment:
`SUPERMODEL_INDY_LAB_LOCAL_POSE=1` replaces those two local-actor cache stores
with current local physics X/Z. It is restricted to the verified ROM words,
i960, two-player active online races, and the local actor. It does not modify
the ring, remote actors, or local physics directly. In the steered prototype
run, the corresponding same-frame write differences averaged 1.47 and 1.52
units (85 and 92 pairs); the remaining difference includes normal movement
between the two stores. At the exact substitution site, the native versus
replacement coordinates differed on average by 16.76 and 16.77 units across
2,660 and 2,437 paired X/Z samples. This establishes the stale local cache and
that the experiment changes it; it does **not** prove collision improvement.
The runs are not deterministic A/B replays, so do not interpret the comparisons
as a controlled gameplay-effect measurement.

The unmodified player caches approached within 2.66 and 1.30 units in one
run, but no local-actor `0xaa71c` write was observed. Proximity alone is not
proof of contact; player collision detection/response may use another path.
The prototype run also had no observed local `0xaa71c` writes. Neither run
proves that real player collisions were exercised, removed, or fixed. There was
an SCSP MIDI FIFO overflow in the prototype logs; its relevance is unknown.

The experiments are currently under `/tmp/dm/indy-collision-{writes1,local-pose2}`
and are temporary. The patch is opt-in and must not be shipped as-is: it lacks
reset/special-state handling and validation for more than two seats, does not
isolate exactly which consumers need the newer pose, and may affect other
behavior that reads the cache. Its `T` records sample the ROM's index but do
not prove that it is always a player-car index. Cabinet frame counters are
independent.

## Required before a production patch

1. Capture verified player-to-player contact and trace the local player's own
   collision/physics path, not merely AI `0xaa71c` triggers. Include synchronized
   source ticks and pre/post collision state on both cabs.
2. Determine whether updating the local cache alone changes collisions or
   causes unwanted AI, display, physics, or state effects. If needed, target
   only the actual collision reads; handle reset and special states explicitly.
3. Compare deterministic or repeatable off/on captures at low/high latency,
   with TCP fallback and loss, mixed clients, 2–8 seats, respawns, walls, AI,
   offline play and non-Indy games. Do not claim removal of rollback until the
   specific correction mechanism is established.

## Local-player collision path and collision-read experiment

Further ROM inspection found a separate path for the local player. At `0xd790`,
`0xac230` calls the candidate routine `0xac4c0`; the latter reads the local
actor cache and nearby actors. On a selected candidate, `0xac338` writes a
nonzero flag to `0x5440b0`. The local physics routine reads that flag at
`0x21dc8`, may call `0x20ac0`, and that routine writes the local physics X/Z
at `0x20ec4/0x20ed0`. Thus **the collision decision/response runs on each cab**;
the evidence does not show a remote-authoritative rollback. Its inputs still
include delayed network-fed actor poses, and the cabs can disagree. The contact
routine projects positions/velocities, so distance alone is not a contact oracle.

The diagnostic trace now records `H` when `0xac338` selects an actor and `I`
when `0x20ec4/0x20ed0` write local physics. It records the selected candidate
pointer from the CPU register, not a nearest-car guess. Each `H` is one flag
write, not a unique collision. `Tests/indy_contact_report.py` summarizes the
records. `--physics-hook-present` must only be passed for logs captured with
the `I` hook: old logs without `I` instrumentation cannot show its absence.
Same-frame H/I association does not prove causation and cabinet frame counters
are not synchronized.

A high-ping baseline (`/tmp/dm/indy-contact-baseline1`) reached a verified
2-player race and UDP primary. Cab 0 selected the other player 5 times and cab
1 selected it 11 times. For cab 1 these peer-flag records had local cached vs
live physics position differences averaging 14.21 units (max 23.42). The later
physics write hook was not yet installed, so baseline response writes are
unknown. Some measured distances to the peer are large, but contact projections
and differing sample ages mean these alone do **not** prove ghost collisions.

The opt-in lab patch adds a narrower experimental alternative:
`SUPERMODEL_INDY_LAB_COLLISION_READS=1`. Instead of globally replacing the
local cache, it substitutes local physics X/Z **only at eight verified ROM read
sites in `0xac4c0` and `0xac230`**, and shifts the corresponding history reads
by the same amount to preserve the native cached motion delta. Deltas of eight
or more units per axis are clamped to zero; the threshold is a lab assumption,
not a validated reset or teleport policy. It is restricted to active 2-player
Indy online sessions and returns the native value if checks fail. Do not enable
it with `SUPERMODEL_INDY_LAB_LOCAL_POSE`; the read prototype is disabled if
both are present. Neither experiment modifies production source or transport.

In `/tmp/dm/indy-contact-read-prototype1`, both cabs entered the race and UDP
primary. H records selected the peer twice on cab 0 and once on cab 1; each
had local physics X/Z writes in the same frame. Q records showed the four
candidate-read substitutions on 2,168 and 2,309 frames, and response-path
reads on 3 and 28 frames respectively. The report checked 4,342 and 4,674
current/history pairs: no observed history-shift mismatches or clamps. This
validates the instrumented substitutions in that run, **not gameplay quality**.
Remaining collision coordinates still depend on remote state and the ROM's
projection. The physics hook cannot establish whether each response was
correct, and unrelated AI candidates are also present. SCSP MIDI FIFO overflow
was logged; relevance unknown. Runs differ in timing and are not controlled
replays. Do not ship either prototype without the tests in the preceding list,
including local/remote gameplay, reset, AI, loss, and mixed-version behavior.

## Experimental local physics history (not a fix)

`SUPERMODEL_INDY_LAB_LIVE_HISTORY=1` enables a third mutually exclusive lab
prototype. At the same eight selected ROM read sites it latches the local car's
actual physics X/Z on the first eligible read in an emulator frame and uses its
own previous frame as history, rather than borrowing the delayed echo's delta.
It affects only active two-player Indy online race reads, never network/transport
or native actor storage. The three prototype switches must not be combined;
if more than one is set, all three substitutions are disabled.

`Tests/IndyLocalCollisionHistory.h` and its ROM-free C++ test cover first and
same-frame samples, actor changes, missing/reversed frames, invalid coordinates,
explicit clear and warp-like steps. An 8-unit Euclidean per-frame threshold is
provisional, not a measured reset rule. The lab clears history on inactive race,
invalid local identity, Indy motion clear (including Reset and LoadState), and
frame/actor discontinuities. The actual emulator reset, save/load and race
reentry paths have NOT been exercised end-to-end by these tests. This is static
lab state and not designed for multiple emulator instances in one process.

`m_frameNumber` increments once at the end of `RunFrame`; it is an emulator frame,
not established to be a ROM collision/physics tick. `g_wlogFrame` updates at
vblank and can differ from it before vblank. Earlier L logs used g_wlogFrame;
the final lab patch now records actual m_frameNumber for L. Other H/I traces
still use g_wlogFrame; never assume an L frame is directly comparable to H/I.
Even with correct frame IDs, first-read latching versus later guest writes may
change native behavior; actual ROM update cadence needs further investigation.
`Tests/indy_live_history_report.py` checks internal L history/latching only and
cannot assess whether a response is correct.

High-ping final build `/tmp/dm/indy-contact-live-history-final1` reached a
verified two-cab race with UDP primary and 100 ms one-way simulated latency,
zero jitter; 40 seconds of gas and actor-0 steering input. Cab 0/1 had 2,070 and
2,226 sequential X/Z history checks, with zero reported mismatches. Cab 1 had
one peer-candidate flag with a same-vblank-frame X/Z physics write; cab 0 had
none. Those are not proof of a collision or its cause. The provisional reset
case was NOT observed in sequential live capture (it was unit-tested only).

Two earlier 60-second 100 ms runs with actor-targeted identical scripted inputs
(`/tmp/dm/indy-contact-matched-base1` and `...-matched-live1`) saw zero peer
candidate flags on both cabs. They are not deterministic replays; start times,
packet timing and vehicle paths can differ, so no relative efficacy conclusion
is possible. Earlier 75-second low-latency prototype and native exploratory
runs also had different actor assignments. `INDY_STEER_TARGET=actor` now maps
steering by the actual local actor to avoid that particular confound, but this
still does not make the runs deterministic. Harness defaults remain 100 ms
one-way and 8 ms jitter; override with `INDY_LATENCY_MS` and `INDY_JITTER_MS`.

The ring is still required for ordered control and fallback. No evidence yet
shows that local-history substitution improves collisions or cross-cab
agreement. Test actual player contact with synchronized/replayable inputs and
reset/reentry/save-load, special states, AI, more seats, packet loss and mixed
versions before considering a production change. No production source changed.

## Frame-scheduled input experiment

The latest lab-only patch also supports `SUPERMODEL_INDY_LAB_INPUT_SCRIPT`.
It overrides the Indy analogue controls at `UpdateInputs`, **not guest RAM or
network packets**, on qualifying two-player online frames. Both players default
to steering 128, full gas 255 and brake 0. Each semicolon-separated segment is
`actor,begin,end,steer,gas,brake`: all values are decimal, frame ranges are
half-open, actor is 0 or 1, control values are 0–255, and same-actor ranges must
not overlap. For example `0,40,55,190,255,0` gives actor 0 a 15-frame steering
pulse. `Tests/IndyFrameInputs.h` validates this syntax; its C++ test requires no
ROM. Use this switch only with the diagnostic build and state tracing. As with
other instrumentation, its global state assumes one emulator per process.

The relative frame origin is each cabinet's **own first qualifying active
frame**. Input scheduling continues through the observed phase 5 after starting
in phase 4; that continuity avoids restarting the pulse when phase 4 temporarily
ends. Reset/load and leaving the qualifying race reset the timeline. Actual
save/load/reentry behavior still needs end-to-end testing. B records contain
emulator frame, host monotonic microseconds, relative frame, actor, the three
applied controls, local live X/Z and network-fed cached peer X/Z. B is written
at input update; H/I use the vblank trace counter. A local-to-cached-peer distance
uses positions of differing age, and is neither a collision oracle nor the true
simultaneous separation of the cars.

The harness validates input scripts before launching and checks B records
against the requested controls plus full segment coverage before declaring a
capture complete. `Tests/indy_frame_input_report.py --script SCRIPT BASE EXP`
checks continuity, actor mapping, input coverage and differences between
local paths at corresponding per-actor relative frames. Its start-clock skew
is meaningful only for the two emulators on the same host. Equal relative
frames and controls are **not** synchronized global time or deterministic replay.
No script: the original keyboard-driven harness remains available.

Example (each output path must not already exist):

```sh
SUPERMODEL_INDY_LAB_INPUT_SCRIPT='0,40,55,190,255,0' \
INDY_TRACE_BINARY=/tmp/dm/trace/src/bin/supermodel \
INDY_COLLISION_OUTPUT=/tmp/dm/example-native INDY_LATENCY_MS=100 \
INDY_JITTER_MS=0 RACE_SECONDS=50 python3 Tests/run_indy_collision_lab.py
# Repeat with a new output path and SUPERMODEL_INDY_LAB_LIVE_HISTORY=1.
python3 Tests/indy_frame_input_report.py --script '0,40,55,190,255,0' \
  /tmp/dm/example-native /tmp/dm/example-experiment
```

Three verified two-cab, 100 ms one-way, zero-jitter captures use this script:
`/tmp/dm/indy-frame-script-base2` and `...-base3` (native) and
`...-live2` (live-history prototype). Each reached UDP primary. The live run
also logged TCP fallback and recovery on one cab. The comparator found zero
input mismatches and full pulse coverage in all three; both actors ran full gas
by default. Each capture lasted 50 wall-clock seconds after race confirmation.
Host-clock differences between actor-0 and actor-1 script starts were 288 ms,
282 ms and 212 ms respectively. The live run already differed from base2 by
10.29 position units for actor 0 at relative frame zero.

The two **native** runs themselves diverged: matched-frame local position
differences reached 410.97/399.49 units (actors 0/1); means were 145.19/97.21.
They had peer candidate flags of 28/2 and 8/2. The prototype run had 2/3.
These are event counts, not unique collisions; frame alignment, starting state,
network traffic and subsequent trajectories differ, and the prototype run had
fallback. Therefore these numbers cannot establish that the prototype changes
or improves collision behavior. The previous `...-base1` capture used an early
scheduler that restarted the script after a phase-5 interval and should not be
used in comparisons.

Next experiment should capture and replay equivalent initial guest and network
state (or replay the detector inputs at a verified boundary), including timing
and packet delivery, and verify a real contact/response independently. Merely
matching controls or candidate flags is insufficient. Normal shipping code and
the ordered ring are still unchanged.

## i960 collision-boundary replay (experimental)

The opt-in fixture capture now records an i960 state including register-window
cache, main/work RAM, local-history sample and the emulator's own program ROM
bytes at a selected instruction. It also records every i960 coprocessor FIFO
read/write and whether a read stalled until the chosen return boundary. These
files include copyrighted ROM and guest state: they are PRIVATE temporary data,
not project assets, and must not be committed or redistributed. Writing them
while running is intrusive and may perturb timing.

`INDY_CAPTURE_FIXTURE=1` requires the live-history prototype and sets a separate
output prefix for each cab. Up to four fixtures per cabinet are captured. By
default they start at `0xac230`, gated by local-live vs cached-peer proximity
under 12 units (not an actual collision). Set
`SUPERMODEL_INDY_LAB_FIXTURE_PEER=1` to capture at `0xac264`, immediately after
`0xac4c0` returns a pointer to the other player's actor, rather than before the
selection. This later boundary is conditional on the prototype's already-made
selection: it cannot compare how native and prototype would select candidates.
The capture remembers the *outer* return PC from `0xac230`; the inner call's
RIP at `0xac264` is not that return PC. `/tmp/dm/indy-boundary-peer1` used the
wrong RIP and is INVALID; discard it. `...-peer2` used the corrected boundary.
A selected candidate/flag is still not proof of physical contact or an impulse.

`Tests/IndyCollisionReplay.cpp` replays the i960 code with a restricted bus. It
rejects unsupported accesses; for coprocessor I/O it checks the exact order,
PC, address, mask and write values against the recorded transcript, feeds the
recorded read/stall results, and refuses a counterfactual as soon as any write
differs. It does NOT emulate the coprocessor or network. It replays the captured
experimental mode twice, compares full captured CPU state and both RAM buffers
at the recorded return boundary, and only then attempts the native variant
from the exact same captured state. Exit status 0 means the experimental
reproduction passed; inspect output for `native replay inconclusive`, which is
not a successful native comparison. Fixture inputs can be untrusted or corrupt;
treat this lab utility as a diagnostic, not a hardened file parser.

A 75-second, two-cab run at 100 ms one-way latency, no jitter, with the frame
script captured **five peer-selected fixtures** in `/tmp/dm/indy-boundary-peer2`
(two from cab 0, three from cab 1). Both cabs reached UDP primary. For each,
the experimental replay consumed zero coprocessor events, executed 35 i960
instructions, performed four selected local-coordinate substitutions, and
matched captured main RAM, work RAM and serialized i960 state exactly. Repeating
the experimental replay gave identical results. Native replay of that *same
post-selection state* completed for all five. Both modes left the same selected
peer pointer and `0x5440b0=1`, but four output words differed in each fixture:
`0x5440d8`, `0x5440e0`, `0x5440e8`, `0x5440ec`. These are values built by the
post-selection projection code; `0x20ac0` later reads them along its response
path. In cab 0 fixture 0, for example, `0x5440e0` interpreted as float was
-521.75128 native versus -535.48102 experimental. This shows the conditional
routine outputs differ from an identical starting state, **not** that the
subsequent physics response is correct, better, or even necessarily called.
The replay stops before the separate `0x20ac0` response and does not reproduce a
whole race or establish cross-cab agreement.

For comparison, all eight earlier pre-selection proximity fixtures in
`/tmp/dm/indy-boundary-capture2` reproduced the experimental CPU/RAM state
exactly, but native replay hit a different coprocessor write at event 2 in
every case. Feeding those experimental coprocessor answers to the native
variant would be invalid, so the utility marks each comparison inconclusive.
The next technical step is to capture/replay the coprocessor and scheduler (or
supply an independently verified equivalent), then replay through the response
path, including initial and final physics state and interruption boundaries.
Do not claim a complete controlled native-versus-prototype contact comparison.

To reproduce locally in this project (the fixture command can fail if no peer
is selected during the capture; paths are examples):

```sh
# Build diagnostic binary after applying Tests/IndyCollisionTrace.patch to a
# clean copy that also contains the two Tests/Indy*Inputs/History headers.
SUPERMODEL_INDY_LAB_INPUT_SCRIPT='0,40,55,190,255,0' \
SUPERMODEL_INDY_LAB_LIVE_HISTORY=1 SUPERMODEL_INDY_LAB_FIXTURE_PEER=1 \
INDY_CAPTURE_FIXTURE=1 INDY_TRACE_BINARY=/tmp/dm/trace/src/bin/supermodel \
INDY_COLLISION_OUTPUT=/tmp/dm/example-fixture INDY_LATENCY_MS=100 \
INDY_JITTER_MS=0 RACE_SECONDS=75 python3 Tests/run_indy_collision_lab.py

# Create a NEW private folder from the locally owned ROM and matching config.
python3 Tests/indy_fixture_roms.py roms/indy500.zip Config/Games.xml /tmp/dm/private-roms
cmp /tmp/dm/private-roms/program.bin /tmp/dm/example-fixture/cab0/fixture-program.bin
# The extractor validates ZIP entry CRCs against the indy500 XML mapping.
g++ -std=c++17 -O2 -I Src -I Src/OSD/SDL Tests/IndyCollisionReplay.cpp \
  Src/CPU/I960/I960.cpp Src/CPU/I960/I960Disasm.cpp Src/BlockFile.cpp \
  -o /tmp/dm/indy-collision-replay
/tmp/dm/indy-collision-replay /tmp/dm/example-fixture/cab0/fixture-0.in \
  /tmp/dm/example-fixture/cab0/fixture-0.out \
  /tmp/dm/example-fixture/cab0/fixture-program.bin \
  /tmp/dm/private-roms/main_data.bin /tmp/dm/example-fixture/cab0/fixture-0.io
```

Optional private-fixture integration tests use environment variables documented
at the top of `Tests/test_indy_collision_replay.py`; without them those tests
skip. `Tests/test_indy_fixture_roms.py` uses synthetic data and no game ROM.
No production source, rollback behavior or ordered ring implementation changed.

## SHARC replay experiment (not scheduler-validated)

For 2B, the collision detector also drives an ADSP-21062 SHARC through input and
output FIFOs. The recorded SHARC output from the earlier run cannot be reused
when native i960 code sends different FIFO inputs. `SUPERMODEL_INDY_LAB_SHARC_FIXTURE=1`
now adds the SHARC serialized state, both FIFO contents, buffer RAM and selected
scheduler state to each opt-in fixture. A versioned timing block additionally
captures wait-state RAM, timers, interrupts, scan-line/UART fraction, and the
in-progress i960 slice. Version 2 records the slice's original budget/start,
whether the i960 has an unserialized delayed callback, and a UART snapshot.
The original i960 save state does not serialize delayed callbacks: a replay
must reject a pending one rather than silently drop it. These snapshots are
private, potentially copyrighted or sensitive, and remain under `/tmp/dm`, not
the repository.

`Tests/IndySharcReplay.cpp` is a **simplified asynchronous experiment**, not a
Model 2 scheduler: it resumes SHARC when the i960 needs a FIFO result, and
rejects unsupported accesses and FIFO overflow. It actually runs the SHARC
core, rather than feeding it previously recorded results. It checks non-stall
FIFO events against the original experimental trace and compares the final
serialized i960 state and both RAM buffers with the real capture. After the
i960 boundary, it optionally advances SHARC to see if queued work converges
to the captured SHARC/FIFO/buffer state. This post-hoc advancement is a
diagnostic, not proof of correct timing, scheduling, interrupts or device
interactions. The program deliberately returns exit code 1 and prints
`INCONCLUSIVE` even when those checks match. Its optional `--explore-native`
outputs are explicitly marked **UNVALIDATED** and must not be used as a
production decision or a demonstrated collision comparison.

In `/tmp/dm/indy-sharc-capture1`, both cabinets reached the two-player race and
UDP primary at simulated 100 ms one-way latency, zero jitter. The capture was
stopped early after seven complete proximity-gated fixtures were saved, to
avoid competing with another active workload on the host; it did not finish
the planned 45-second capture or the harness's final script verification.
For all seven fixtures, asynchronous SHARC execution reproduced the captured
experimental i960 state and both RAM buffers, and the non-stall FIFO trace
matched. At the i960 return boundary the SHARC and input FIFO initially
**differed**; after 42 manually advanced SHARC steps, the serialized SHARC,
both FIFO queues and buffer RAM matched each captured output. That convergence
is not a reproduction of the actual timing. All seven experimental detector
returns had a zero selected-candidate/flag; proximity alone is not contact.
`/tmp/dm/indy-sharc-capture1/asynchronous-report.txt` contains the exploratory
results, which remain inconclusive even if a native result is printed.

To replay a private fixture, `Tests/indy_fixture_roms.py` now also creates an
8 MiB `copro_data.bin` from the same CRC-checked ROM ZIP and XML. Build with:

```sh
g++ -std=c++17 -O2 -I Src -I Src/OSD/SDL Tests/IndySharcReplay.cpp \
  Src/CPU/I960/I960.cpp Src/CPU/I960/I960Disasm.cpp \
  Src/CPU/SHARC/SHARC.cpp Src/CPU/SHARC/SHARCDisasm.cpp Src/BlockFile.cpp \
  -o /tmp/dm/indy-sharc-replay
/tmp/dm/indy-sharc-replay /tmp/dm/indy-sharc-capture1/cab0/fixture-0.in \
  /tmp/dm/indy-sharc-capture1/cab0/fixture-0.out \
  /tmp/dm/indy-sharc-capture1/cab0/fixture-program.bin \
  /tmp/dm/indy-fixture-roms-sharc/main_data.bin \
  /tmp/dm/indy-fixture-roms-sharc/copro_data.bin \
  /tmp/dm/indy-sharc-capture1/cab0/fixture-0.io
```

Optional private-fixture checks and environment names are documented in
`Tests/test_indy_sharc_replay.py`; they skip without configured fixtures.

### Restricted timed-scheduler experiment

`Tests/IndyTimedSharcReplay.cpp` is a separate, fail-closed diagnostic. It
resumes the remaining portion of the captured i960 slice, then models `RunLine`
quantum selection, memory wait charges, SHARC execution, FIFO stalls and
wakeups, UART clocks and supported IRQ changes. It compares **at the original
return instruction**, without tail steps: serialized CPUs and UART, main/work
RAM, buffer and both FIFOs, the entire recorded FIFO trace including empty
reads, scheduler times and slice start/budget/elapsed, line, clock fraction,
wait table, timer/interrupt values, FIFO triggers, and copro control/hold state.
It rejects version 1 captures, pending i960 callbacks or interrupts, asserted
or held interrupt lines, stalled/burst i960 resume states, running timers, FIFO
extra/overflow, vblank/frame crossing, unsupported bus accesses and UART
transmission. It checks required fixture block boundaries and ABI-specific CPU,
SHARC, UART and timing lengths before loading. The conservative IRQ rejection
is necessary because loading the CPU state re-enters `Run()` and rechecks
interrupts. Sound-board/external input and the rest of the board are not fully
captured or validated. An exact match for the checked fields is stronger
evidence than the asynchronous test, but not full-system equivalence.
It always prints `INCONCLUSIVE` and exits 1 even on an exact subset match.
Optional native detector/response modes are **exploratory and unvalidated**;
they do not prove whole-board equivalence or a gameplay benefit. The bare
binary's exploratory flags do not gate themselves on a successful baseline;
use the wrapper described below if exploring a captured peer response.

On `/tmp/dm/indy-sharc-timed2`, two linked cabinets reached the two-player
race with UDP primary at 100 ms one-way and zero jitter; the scripted input was
verified without mismatches and with all 15 pulse frames observed. An opt-in
harness stopped once both cabinets had completed a fixture and script coverage.
Cab 0 frame 4073 and cab 1 frame 4056 each produced a proximity-selected
fixture (distances 11.956 and 11.612 units; proximity is not contact). Both v2
fixtures passed the listed boundary comparisons, with 3 scan lines and 6
empty FIFO stalls each. Cab 0 executed 461 i960 / 3180 SHARC instructions;
cab 1 executed 457 / 2939. The separate asynchronous diagnostic reported zero
candidate return and flag in both, but is not itself a validated counterfactual.
No peer-selected collision or later physics response was established.

Build the timed diagnostic from clean production sources:

```sh
g++ -std=c++17 -O2 -I Src -I Src/OSD/SDL Tests/IndyTimedSharcReplay.cpp \
  Src/CPU/I960/I960.cpp Src/CPU/I960/I960Disasm.cpp \
  Src/CPU/SHARC/SHARC.cpp Src/CPU/SHARC/SHARCDisasm.cpp \
  Src/Model1/I8251.cpp Src/BlockFile.cpp -o /tmp/dm/indy-timed-sharc-replay
/tmp/dm/indy-timed-sharc-replay /tmp/dm/indy-sharc-timed2/cab0/fixture-0.in \
  /tmp/dm/indy-sharc-timed2/cab0/fixture-0.out \
  /tmp/dm/indy-sharc-timed2/cab0/fixture-program.bin \
  /tmp/dm/indy-fixture-roms-sharc/main_data.bin \
  /tmp/dm/indy-fixture-roms-sharc/copro_data.bin \
  /tmp/dm/indy-sharc-timed2/cab0/fixture-0.io
```

`Tests/indy_fixture_timing.py` checks block layout and reports timing metadata
without printing captured RAM; `Tests/test_indy_fixture_timing.py` uses only
synthetic data. `Tests/test_indy_timed_sharc_replay.py` documents the optional
private-fixture environment and tests an exact subset match, modified FIFO
transcript, truncated transcript and state blocks, and rejection of a pending
callback, active timer, interrupt, or stalled/burst resume. A prior v1 capture timed out waiting for
both cabinets; one complete v1 pair was saved and its timing block checked, but
it lacks slice budget, callback status and UART state and is explicitly rejected
by the new replayer. The harness finally cleaned up its own processes.

### Peer-selected response fixture and exploratory comparison

The opt-in diagnostic build now supports `SUPERMODEL_INDY_LAB_FIXTURE_RESPONSE=1`.
It starts at `0xac230` before selection, retains only a peer pointer at
`0xac264` followed by a flag of 1 at `0xac340`, observes the original detector
return, and requires execution of `0x21dc8`, the call at `0x21e28`, and the
`0x20ec4`/`0x20ed0` local physics stores. It snapshots the exit immediately
before `0x20ed8`, in the same cabinet frame, and discards attempts if another
detector runs first or the frame changes. This observed path is stronger than
a same-frame correlation, but is not a complete proof that no unrelated state
changed between hooks. It saves private fixture files only and does not alter
collision state or network traffic. The `...FIXTURE_REQUIRE_PEER=1` option
instead ends at the detector return; do not mix either mode with the older
post-selection `...FIXTURE_PEER=1` mode.

The first bounded capture, `/tmp/dm/indy-peer-response1`, timed out: its
8-frame sampling interval skipped observed peer flags. It cleaned up its own
processes. The frame script was subsequently checked manually: zero mismatches,
15/15 pulse frames. The retry `/tmp/dm/indy-peer-response2` sampled at most once
per frame and completed with zero script mismatches and 15/15 coverage; both
cabinets had UDP primary at simulated 100 ms each way. Cab 0 frame 4739 selected
actor 1, and cab 1 frame 4640 selected actor 0; both captured the specified
selection-to-physics-store path. The frame counters are local to each cabinet.

A missing burst flag in the replay for buffer and data ROM originally caused a
cab 0 mismatch; it was corrected to match `CModel2::Burst` for supported
regions and covered by `Tests/IndyTimedSharcReplayTest.cpp`. The corrected
cab 0 replay matched all previously listed captured state, device clocks,
FIFO events including stalls, and the peer/physics hook path at the **original
response boundary**: 12 scan lines, 1,746 i960 / 15,812 SHARC instructions,
45 stalls, eight substitutions. No post-boundary steps were used. Cab 1
rejected an unsupported comm-board read at `0x01a121ca` / PC `0x0a6538`; it is
not a validated full response replay. Display-prediction hook reads are also
rejected because their associated state was not captured.

For cab 0, `Tests/indy_peer_counterfactual.py` first requires the captured
experimental subset and response path to match before it runs an exploratory
native detector; it runs the exploratory response only if the same peer and flag
were selected. Both runs start from the same private pre-selection fixture.
The native probe selected the same peer and took the checked response path;
its two captured physics position words differed, with an illustrative x/z
separation of 0.420247 guest coordinate units and 71 work-RAM bytes different
at the response boundary. The FIFO command transcript diverges at event 2,
as expected for different inputs. **This is not a validated counterfactual or
proof of better or worse gameplay:** external/full-board state is incomplete,
only one positive response case in that capture matched, the ring/network path
and mixed versions have not been validated, and the peer selection itself is not a
physical-contact oracle. The cab 1 wrapper correctly refuses to run native
when its baseline encounters the unsupported comm read. The private report is
`/tmp/dm/indy-peer-response2/peer-response-report.txt`. All modes and the wrapper
return nonzero and mark the result inconclusive. `Tests/test_indy_peer_counterfactual.py`
checks the baseline and same-peer gates without ROMs; optional private-fixture
response tests require `INDY_TIMED_RESPONSE_FIXTURE=1`.

Run the gated exploratory wrapper with the same six fixture/ROM arguments as the
timed replay, preceded by the timed replay binary path:

```sh
python3 Tests/indy_peer_counterfactual.py /tmp/dm/indy-timed-sharc-replay \
  /tmp/dm/indy-peer-response2/cab0/fixture-0.in \
  /tmp/dm/indy-peer-response2/cab0/fixture-0.out \
  /tmp/dm/indy-peer-response2/cab0/fixture-program.bin \
  /tmp/dm/indy-fixture-roms-sharc/main_data.bin \
  /tmp/dm/indy-fixture-roms-sharc/copro_data.bin \
  /tmp/dm/indy-peer-response2/cab0/fixture-0.io
```

### Restricted comm-board snapshot and read replay

The additional opt-in `SUPERMODEL_INDY_LAB_COMM_FIXTURE=1` requires the SHARC
fixture. It adds the existing `Model 2 comm` save-state block (including raw
shared RAM and selected control state), a separate 16 KiB effective
`ReadShared` guest view (including any active Indy overlay), and a private
`.comm` transcript of guest shared-memory reads/writes. The harness requires
the transcript before treating a fixture as complete. The previous response
fixtures did not record these data; they cannot be retrospectively filled in.

The replayer supports only **read-only**, unchanged comm state between the two
boundaries. It requires the raw saved block and guest-view snapshot to match
exactly at entry and return, applies Model 2 mirrored addressing, byte masks
and burst timing, and compares each runtime read against the captured ordered
PC/address/mask/value transcript. It rejects missing or changed snapshots,
comm writes, missing/extra/mismatched reads, FG/control reads, and vblank/frame
crossing. It never feeds a recorded value as a substitute for a computed guest
read. The saved comm block is not the complete online state: UDP/ring queues,
packet arrivals, guest-overlay lifecycle and other asynchronous activity are
not modeled. Matching endpoint snapshots and read events do **not** prove that
no unobserved transient update occurred, or validate the live network protocol.
The native exploration also remains inconclusive even if these checks pass.

`/tmp/dm/indy-peer-comm1` never reached attract mode and timed out cleanly;
the retry `/tmp/dm/indy-peer-comm2` completed both fixtures, with UDP primary,
100 ms one-way latency, zero jitter, and zero scripted-input mismatches (15/15
pulse frames). Cab 0 frame 4408 and cab 1 frame 4390 selected their respective
peer and followed the response path. Both raw/guest comm snapshots were unchanged
and there were no comm writes; cab 0 made zero shared reads, cab 1 made one byte
read at PC `0x0a6538`, aligned address `0x01a121c8`, mask `0x00ff0000`.
Both experimental replays matched the serialized CPUs and UART, memories,
FIFOs, complete FIFO transcript, checked comm snapshots/read transcript,
scheduler fields, and response path at the original boundary. Cab 0 executed
1,737 i960 / 15,449 SHARC instructions over 11 lines with 43 stalls; cab 1
executed 1,675 / 15,662 over 12 lines with 44 stalls. Neither result establishes
full-board equivalence or a physical-contact oracle.

The gated exploratory native probe for cab 0 selected the same peer and reached
the response stores; its checked x/z separation from the experimental captured
position was 4.428310 guest units, with 63 changed work-RAM bytes. It also ran
a different number of scan lines, so omitted asynchronous timing can matter.
For cab 1, the native detector probe selected no peer/flag; the wrapper therefore
refused to run a comparable response. **Neither is a validated native physics
counterfactual or a gameplay efficacy result.** The private report is
`/tmp/dm/indy-peer-comm2/peer-comm-report.txt`.

`Tests/IndyTimedSharcReplayTest.cpp` has ROM-free address/mask, comm-read,
transcript-mismatch and burst checks. Optional private integration checks use
`INDY_TIMED_COMM_FIXTURE=1` in addition to the variables documented by
`Tests/test_indy_timed_sharc_replay.py`; both captured cabinets passed. Tests
also reject altered or absent transcripts, comm writes and altered boundary
snapshots. The wrapper requires a consistent comm diagnostic marker, warns for
older fixtures without it, and still always reports inconclusive.

### Version 2 comm entry-point witness

Endpoint equality alone cannot exclude a change that is later reversed. The
isolated build now emits a version-2 comm-view payload and a framed `.comm`
witness: a begin record, ordered shared accesses/unsupported-entry records,
and an end record. It logs shared-memory reads and writes from any CPU,
CN/FG control accesses, the comm vblank tick, reset/load, and the Indy race-entry
instruction gate (which may clear the guest overlay). The replay requires both
markers for a version-2 view and rejects any unsupported-entry record, any
write, data after the end marker, or a missing/line-truncated transcript. The
markers are diagnostics for the reviewed entry points, not cryptographic
integrity protection. Version-1 fixtures remain readable but the wrapper
explicitly warns that they have no entry-point witness.

This narrows the transient-update concern for the reviewed paths: source
inspection places ring application and the guest-view publication in the
emulation-thread tick or FG handling, with direct guest writes and the Indy
entry gate covered separately. It does **not** observe asynchronous network
queues, establish absence of every possible external/device event, or replace a
full-board model. Any newly added mutation path would need explicit auditing and
instrumentation; endpoint equality and witness silence are not a universal
proof that no state changed. No packet timing is invented or replayed.

Two 100 ms one-way witnessed runs (`/tmp/dm/indy-peer-comm-witness1` and
`...witness2`) timed out before attract after repeated link initialization.
The cause is unknown. At 40 ms one-way, `...witness3` reached race/UDP primary
but timed out after 100 seconds without complete fixtures. A separate extended
40 ms / zero-jitter run `...witness4` completed both response fixtures, with
zero input mismatches and 15/15 scripted pulse frames. Cab 0 frame 4040 and
cab 1 frame 4039 selected their respective peer and executed the captured
response path. The framed witness reported no unsupported-entry records and
no writes; cab 0 had zero shared reads and cab 1 had one. Both original-boundary
subset replays matched their CPU/SHARC/UART/RAM/FIFO/events/timing/flags and
comm snapshots/transcripts. Cab 0 took 12 lines, 1,726 i960 / 15,764 SHARC
instructions and 45 stalls; cab 1 took 12 lines, 1,694 / 15,519 and 45 stalls.
Each performed eight experimental substitutions. The lower latency is a changed
condition, not an A/B comparison with the 100 ms captures.

The wrapper's cab 0 native exploration reached the same checked peer/response
path, with an x/z separation of 3.516076 guest units and 81 changed work-RAM
bytes. Cab 1's exploratory detector selected no peer, so its comparable
response was refused. These are still **unvalidated diagnostics**, not gameplay
or efficacy results. The private report is
`/tmp/dm/indy-peer-comm-witness4/report.txt`. Both witnessed fixture test suites
passed, including injected unsupported entry, removed markers, line-aligned
truncation, writes, read corruption and snapshot corruption. ROM-free parser
checks also exercise marker ordering, empty input and legacy compatibility.

Future work must model or fail closed on comm writes, packet-arrival/FG/vblank
updates and other device inputs, verify lifecycle and more gameplay scenarios,
then evaluate whether any production change is warranted. At that point no production
collision, rollback, network or ring code had changed.

## Experimental local self-pose change (default on)

A later source-tree experiment adds `-indy-local-collision` (or
`IndyLocalCollision = 1` in the Network configuration). At the user's request,
it is now **on by default** for its supported cases; `-no-indy-local-collision`
or `IndyLocalCollision = 0` disables it. This default is a user-directed
experimental deployment decision, not an indication that the behavior is validated.
The intent is to avoid using the delayed local-car ring echo for eight selected Rev A
collision X/Z and prior-X/Z reads. It uses the local car's physics coordinates
and a per-emulator-instance, first-read-per-emulator-frame history instead.
It does not modify guest actor storage, remote poses, TCP/UDP traffic, or ring
control; it does not make the two cabinets agree about contact. The ROM can use
these local inputs when considering AI as well as the other player. This is a
candidate, **not a validated physics fix**.

The experiment is limited to the exact verified program CRC and instruction
words, Indy Online, active two-player races, local actors 0 or 1, i960 full-word
reads and the exact eight addresses. Unknown revisions, offline play, other
games, other seat counts, and conflicting older lab switches leave native reads
unchanged. It clears history on reset/load, inactive race or online failure;
identity change, invalid coordinates, frame gaps/rewinds and movements over the
provisional 8-unit-per-frame bound use the native reads for the entire affected
frame. The first sample also uses native reads, so replacement starts only
after two consecutive suitable samples. The 8-unit bound and use of emulator
frames (rather than an established ROM physics tick) remain unvalidated. A
latched sample is reused for selected reads in the same frame until invalidated;
if a lifecycle/roster reset occurs mid-frame, later reads fall back to native,
so a single frame can contain both modes. That transition is not yet validated.

Build-time validation used the isolated trace tree, not the project's normal
binary. `Tests/IndyLocalCollisionTest.cpp` checks opt-in/ROM/race eligibility,
all eight sites, peer and wrong-address exclusion, per-frame latching, previous
values and fallback on gaps/rewinds, identity changes, invalid values, jumps and
clear. The normal binary must be rebuilt before its option is available. Do
not combine this option with `SUPERMODEL_INDY_LAB_LIVE_HISTORY`,
`...LOCAL_POSE`, or `...COLLISION_READS`; the new option disables itself when
one of those older experiments is present.

An initial two-cab integration run `/tmp/dm/indy-local-collision-exp1` reached
UDP primary at 40 ms one-way, zero jitter, with the opt-in enabled on both cabs;
script verification had zero mismatches and 15/15 pulse frames. Cab 0 logged
6,392 substituted reads across 1,572 frames (6,309 values differed from native);
cab 1 logged 5,872 across 1,448 frames (5,872 differed). The K-record report
checked 3,140 and 2,890 adjacent-frame X/Z history relationships, respectively,
without a mismatch. Peer candidates were observed, but the capture is not a
controlled comparison, a physical-contact oracle or proof of improved behavior.
Subsequently the eligibility logic was refactored into separately unit-tested
functions. A fresh integration run of the rebuilt final candidate,
`/tmp/dm/indy-local-collision-exp2`, reached UDP primary with the same 40 ms
one-way latency and zero jitter. Script verification again had zero mismatches
and 15/15 pulse frames. Cab 0 recorded 4,376 substitutions across 1,089 frames
and 2,176 adjacent-frame X/Z history checks; cab 1 recorded 4,420 substitutions
across 1,104 frames and 2,206 checks. No history inconsistency was detected.
The peer flag appeared three times on cab 0 and once on cab 1; each cab had
one peer flag with same-frame physics X/Z writes. As above, those observations
are not proof of contact causation or improved gameplay. The private contact
report is `/tmp/dm/indy-local-collision-exp2/contact-report.json`.

After the default was changed, `/tmp/dm/indy-local-collision-default1` tested
one cabinet with no collision flag and the other with the explicit disable
flag, at 40 ms one-way and zero jitter. Both reached UDP primary and the script
passed (zero mismatches, 15/15 frames). The default cabinet logged 1,692
substituted reads and 780 adjacent-frame history checks without inconsistency;
the disabled cabinet logged zero substituted reads and reported the configuration
override. This is a configuration smoke test, not a controlled gameplay comparison.

Additional synchronized/replayable comparisons, reset/load, multiplayer,
mixed-version, loss/fallback and real gameplay checks are required to validate
this default-on deployment. No rollback or network protocol changed.

### Motion investigation: verified UDP impairment (in progress)

- Corrected harness shares the production `Latency` object with UDP; a harness
  unit test checks object wiring. Test captures also measured age on the
  **receiving** native race-UDP lane using logging in the isolated build only.
- `/tmp/dm/indy-motion-realudp-100-age1`: configured 100 ms one-way, zero
  jitter. Both cabinets entered UDP primary. Sampled receive ages: cab 0
  median 101.24 ms, p95 106.84 ms (60 samples); cab 1 median 101.25 ms,
  p95 106.28 ms (58 samples). Both cabinets reached race; scripted input
  covered 15/15 frames. Trace's existing `udp_age_ms` is the legacy pose lane,
  not these race-UDP sample-age measurements.
- `/tmp/dm/indy-motion-realudp-200-age1`: configured 200 ms one-way, zero
  jitter. Measured receive ages cab 0 median 201.13 ms, p95 203.49 ms
  (58 samples); cab 1 median 201.24 ms, p95 204.65 ms (66 samples). Both
  cabinets received snapshots and reached race but neither logged entry into
  UDP primary; the race-UDP entry freshness threshold is 155 ms. Both
  completed 15/15 scripted input frames. This demonstrates a mode boundary,
  not that raising it is safe or that it fixes perceived rubber-banding.
- Existing `IndyRaceUdpTest` fails its stale-hold assertion: an initial
  selection of the previous pose bypasses its freshness check. An isolated
  candidate that checks age and handles tick wrap passes the unit test and
  added boundary checks; it is not deployed and has no integration test.
- The isolated tree was subsequently set up to test only a 260 ms entry
  threshold (leaving the stale-fix candidate saved separately under
  `/tmp/dm/indy-race-udp-stale-fix-candidate.h`). SSH disconnected during
  the isolated rebuild and reconnect timed out; its build completion and
  runtime behavior are unknown. Do not assume this experiment is validated
  or apply it to project source without further tests, including packet loss,
  fallback, jitter, low latency, and controlled gameplay comparison.

#### Resumed investigation: render-only high-ping candidate

The remote machine's temporary `/tmp/dm` tree was cleared before this work
resumed, so the older capture paths and binaries above are no longer available.
New isolated sources, binaries and raw logs are under
`/home/nacho/indy-motion-lab/` on the remote machine.

The 155 ms race-UDP primary entry threshold was confirmed at 200 ms simulated
one-way delay: the unmodified build received UDP samples but did not enter
primary mode. A 260 ms entry-window experiment entered UDP primary and, under
a six-second UDP snapshot blackout, fell back to TCP and later re-entered UDP.
That alternative was **not deployed**: it changes native motion transport and
could affect collision inputs and failure behavior.

The selected candidate instead keeps primary thresholds, native comm/guest
motion, ordered TCP and collision paths unchanged. While TCP native motion is
in use and compatible race-UDP samples arrive, a newer same-race, same-peer,
same-slot, same-state, timely and geometrically aligned snapshot may feed the
existing draw-only prediction/blend. Selection is limited to a 16-tick lead
and 340 ms source-age; the existing 12-unit/heading bounds and near-local
return toward native collision pose remain. The extra race-UDP blend correction
is eased and fades on loss. Existing `-no-indy-prediction` and `-no-indy-udp`
disable this rendering path. This is bounded presentation, not rollback,
remote authority, or a change to the experimental local-collision input.

- `draw-200-jitter30-eased`: isolated same-frame counterfactual, 200 ms
  simulated one-way latency, uniform +/-30 ms jitter and a six-second UDP
  snapshot blackout. Both cabs reached race, script 15/15 frames, and
  snapshots stopped being selected during the blackout and were selected again
  after recovery. A diagnostic traces original TCP prediction, immediate UDP
  blend, eased blend, and peer TX. The last TX is only a proxy for the peer's
  true position. In one visible example after recovery (cab 1 slot 5, 233
  matched rows), median distance to that proxy was 3.631 units for TCP-only
  prediction, 0.577 for immediate blending and 0.581 for easing; p95
  consecutive-step change was 0.009, 0.275 and 0.065 units respectively.
  This shows a closeness/jitter tradeoff in this particular segment; some
  other segments worsened in some metrics. It is not deterministic gameplay
  equivalence, evidence that all cars are smoother, or a contact test.
- `draw-15-eased-blackout`: 15 ms simulated one-way, zero jitter, five-second
  UDP blackout. Both cabs reached primary, fell back and resumed primary;
  15/15 scripted frames passed. In the sampled stable primary intervals the
  alternate draw correction was unused, and output matched the original
  predictor apart from floating-point trace rounding.
- `final-200-jitter30-blackout`: uninstrumented final binary, 200 ms one-way,
  +/-30 ms jitter, five-second UDP blackout. Both reached race and completed
  15/15 scripted frames. Drawing used race-UDP snapshots before and after
  blackout, reverted to the normal path during blackout, and native UDP
  primary remained off. Reported draw sample-age medians in sampled windows
  were approximately 227–238 ms; this includes time after receipt.
- The previously failing `IndyRaceUdpTest` stale-hold assertion is unchanged.
  A separate isolated candidate passed a unit check, but was not integrated
  because it changes guest-visible native motion and could introduce a
  fallback correction. It remains a known issue, not a pass.

These are two-cab localhost relay simulations with limited inputs, not WAN,
large-roster, real-contact, long-race, savestate, or mixed-version gameplay
validation. Actual rubber-banding may remain, and no arbitrary-latency or
packet-loss guarantee is made. The isolated final binary is
`/home/nacho/indy-motion-lab/final-src/bin/supermodel`; it does not replace the
project's normal `bin/supermodel`.
- `final-15-blackout`: uninstrumented final binary at 15 ms one-way, zero
  jitter and a five-second UDP snapshot blackout; both cabinets entered primary,
  fell back, and re-entered. Both reached race and input script verified 15/15.
  This is a smoke test, not a perceptual or collision validation.

The tested render-only candidate was applied to project source without changing
`EnterFreshUs`, native comm reconstruction or the previously applied diagnostic
and local-collision changes. Its six source files and test were compared with
the isolated final source tree. A clean isolated build succeeded; the normal
project binary was not rebuilt or replaced. Motion, pose, local-collision and
new draw-selection C++ checks passed; race relay fast/delayed/mixed/TCP-only
protocol checks passed; Python Indy discovery ran 53 tests with 16 skips. The
separate pre-existing `IndyRaceUdpTest` stale-hold failure remains as noted.

## Same-cabinet CPU-versus-CPU collision experiment

The separately guarded Rev A AI detector at `0xa96f0` is now included in the
experimental collision input. For two AI cars owned and simulated by this
cabinet, it can use locally latched current and previous physics coordinates
at the detector's eight verified reads. Both actor histories must be consecutive
and valid, or the detector uses native inputs. AI pairs that include a
remote-owned car, player cars, unverified ROMs, and inactive sessions use
native inputs. This does not alter guest RAM, transport or remote poses and
does not synchronize collision outcomes. It uses the existing
`IndyLocalCollision` switch (default on; set `IndyLocalCollision = 0` or use
`-no-indy-local-collision` to disable both the player and AI experiments).

This was applied to the project at the user's request, **not as a proven fix**.
Unit tests exercise guards, two-actor warmup, history gaps, invalid inputs and
clear. An isolated two-cabinet 100 ms one-way test of the instrumented same
candidate showed both cabinets entering race UDP primary, 1,872/11,432 AI read
substitutions, no logged cross-cabinet substitutions, and no history mismatch
in 436/2,404 adjacent logged comparisons. One AI response-write marker matched
a substituted detector pair on the same frame; this does not establish a
real contact, response correctness, or gameplay benefit. An earlier version
showed 40 logged history mismatches and was not used. Lifecycle transitions,
contact replay, WAN and gameplay safety remain unverified. The detailed lab
record is `/home/nacho/indy-ai-collision-lab/Findings.md`.

A clean build of the applied project source succeeded in the isolated copy
`/home/nacho/indy-ai-collision-lab/project-build`; the project binary was not
rebuilt or replaced by this application. AI and player collision C++ tests
passed; Indy Python discovery passed 53 tests with 16 skipped. A targeted
`git diff --check` on the edited Model2 files passed. A repository-wide
check reported existing CRLF/trailing-whitespace lines in SDL Main.cpp.

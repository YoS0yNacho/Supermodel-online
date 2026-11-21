# Super GT 24h online integration — work in progress

EXPERIMENTAL native TCP online path is now wired through GUI, launcher, client
and relay. Real ROM boot/racing has NOT been verified. UDP motion and local
response verification remain unfinished; this is not quality-parity support.

## ROM evidence

Verified installed epr-19155.15 CRC 593952fd and epr-19156.16 CRC a91fc4ee,
524288 bytes each. Two-byte lane interleave yields 1 MiB, CRC 22d76fe3.
Linear disassembly is an exploration aid; data sections are not code.

- Service routine 0xbbc90 cycles cabinet ID 0..3 and unlinked mode.
- 0xbbd20 cycles linked maximum 2..4. Do not copy Indy's eight-seat limit.
- 0xbc51c writes RAM settings: count 0x5109b9, disabled 0x5109ba,
  master 0x5109bb, zero-based seat 0x5109bc.
- 0x71940 copies these into runtime state (see Profile.h).
- 0xb8074 chooses shared[1] master/slave using native seat.
- 0xb80b4 sets shared frame offset to 0x380 and size to 0xe00.
- 0xb9614 copies RX at 0x1a12380 with 0x380 row stride into native
  actor records at 0x512420 with 0x47c stride, rotated from next seat.
  TX uses 0x1a12000 and the local record at 0x511230 + seat*0x47c.
- Own row is last in this RX rotation. This does NOT establish which
  fields drive local physics/drawing or justify a full-row substitution.

## Required before release

1. Runtime-test the revision-gated bootstrap; session-only overrides must never
   persist into offline NVRAM.
2. Add versioned lobby/relay capability and reject incompatible clients;
   use experimental identity 0x5bf177da34881 (checked unused in current source).
3. Prove native 2/3/4-cabinet boot, entry, race, finish and reconnect.
4. Decode race identity, seat ownership and pose fields from this ROM;
   no borrowed Indy/Sega Rally pose offsets or guessed float scanning.
5. Add optional UDP motion, stale/context rejection, safe fallback and
   local-car independence without modifying native wire/control semantics.
6. Test loss/jitter/delay and input-to-response behavior, not merely decoding.
7. Run all existing network guards plus SGT-specific tests.

Current tests check profile geometry and EEPROM image transforms, not emulator behavior. No GUI lab
was started and disk NVRAM was not changed. The binary was rebuilt after bootstrap integration.

## Native EEPROM transform (wired to emulator bootstrap, not public launch)

RelayEeprom.h accepts only the initialized title header 3353 4132 4906 0000,
settings marker ad85, and a valid settings byte sum. Word 4 starts the primary
32-byte record; word 20 starts its backup. The first word of each is a sum of
the remaining 30 bytes (0x71a18..0x71b8c), NOT Indy checksum code.
The ROM also contains a separate CRC-like routine at 0x70c40; do not confuse
that with this settings-save path.

Word reads at 0x70890 map EEPROM words directly to RAM 0x5109a0. Role edits
are confined to record bytes +0x11 count, +0x12 linked mode, +0x13 master,
+0x14 zero-based seat. Primary preferred; valid backup used if primary is
bad. Both copies are refreshed; rejection leaves the entire image unchanged.

Sgt24hRelayEepromTest.cpp passed with address/undefined-behavior sanitizers:
all seats in 2/3/4-cabinet rooms, checksum vector, backup recovery, invalid
seat/count/header/marker/checksum rejection, unrelated data preservation,
and idempotence. Fixtures are synthetic, not captured running cabinets.

Main.cpp SaveNVRAM already refuses disk writes with NetRelay set. This must
remain true when the SGT launch path is connected; the pure image helper
does not itself enforce session lifetime or persistence protection.

Further evidence: the native row-copy function pointer is set to RAM code
0x21be00 by 0xb9580. Decode that uploaded routine before assuming 0x380
wire rows have the same field layout as 0x47c actor records.

## Emulator bootstrap integration

PrepareSgt24hRelayRole waits for the native relay roster (2..4), configures
an EEPROM copy, permits up to 1800 initialization frames for blank settings,
and resets after initialization. Board Tick is gated until NativeRoleConfigured.
The loaded program's first 1 MiB must have CRC 22d76fe3; the emulator allocates
2 MiB, so the size of its storage buffer must not be confused with ROM size.

NetBoardLink now recognizes SGT as native Model 2 (no hybrid/UDP negotiation),
validates Ready capacity and rejects bad-length/pre-Ready/overflow board frames.
Native FF/FE establishment still verifies final ID/count against relay assignment.

This path is compiled but inaccessible through current Main.cpp relay whitelist
and lobby: those remain intentionally unchanged until server admission is added.
Expected GUI roster fields are not wired for SGT yet; currently only Ready
and native establishment agree. No claim of emulator-in-the-loop verification.

Build: 880173072957b98a69290e119d7572a14e83c332075e70117472248468b91d24.
Five existing guards and both SGT unit tests passed after the build.
Backup (including previous binary):
/home/nacho/.supermodel-sgt24h-bootstrap-1795205241/

Whole-tree git diff --check reports pre-existing CRLF/trailing-whitespace issues
in .gitignore, Gui.cpp and Main.cpp; these files were not changed by this step.

## Native server transport

relay.py now registers GUID 0x5bf177da34881, 3585-byte native payloads and
four-seat capacity. It uses existing native Model 2 reverse ring routing,
not star fan-out, hybrid frames, or UDP. No lobby game ID/capability has
been advertised. The running external relay was NOT restarted or deployed.

Tests/run_sgt24h_transport.py uses fake board clients on an ephemeral local
server. Nine cases passed: 2/3/4 seats, fast path at zero latency, buffered
path at 35 ms with jitter, coroutine fallback at 35 ms with jitter.
Checks cover RLJ3/zero nonce/wrong game/duplicate/fifth-seat rejection,
Ready roster fields, reverse ring routing, fragmented/coalesced frames,
byte-exact forwarding, no UDP/star/adaptive mode, running-room rejection,
and whole-room failure on malformed native frame length.
These are transport tests, not authenticated lobby-ticket tests and not ROM races.

Regression tests passed: run_srallyc_online, run_srallyc_pose,
run_model2_room_video, run_indy500_online, run_online_lobby,
run_daytona_online. Existing binary unchanged during this server-only step.
Backup: /home/nacho/.supermodel-sgt24h-relay-1795205923/relay.py.

## Server-side ticketed lobby

Added stable game ID 12, exact SGT24H1 HEL1 capability/reply, four-seat room
limit, central-only hosting, and per-game capability checks. The handshake
requires native/settings capability bits; old clients do not discover SGT.
Current SGT handshake advertises only SGT plus normal Model 3 availability;
combined-title discovery must be designed before wiring the emulator GUI.
SGT room options accept ONLY zero (native defaults), including rejection of
60 Hz until that capability is implemented. No car-number override exposed.

Tests/run_sgt24h_online.py passed for 2/3/4 seats: old and under-capable client
isolation, invalid options, minimum/maximum seats, master-only launch, unique
session tickets, wrong-game/zero-ticket/hybrid rejection, reversed channel
arrival preserving reserved roster, byte-exact ring, duplicate ticket rejection,
and disconnect driving room failure. Fake clients only; no emulator race.

Transport matrix and existing lobby, Indy, Daytona, Rally, pose and room-video
regression tests passed again. Emulator binary unchanged in this step; running
external relay not restarted. Backup: /home/nacho/.supermodel-sgt24h-lobby-1795206203/.

Next: extend client capability negotiation without losing existing Model 2
availability, append GUI game and native-only settings policy, wire launcher
roster fields and Main.cpp whitelist, then build/test the complete launch path.

## Client/GUI/launcher integration (supersedes earlier pending-launch notes)

Stable game ID 12 is appended to client ROM/title lists with capacity four.
SGT24H2 plus flags 0..7 negotiates SGT alongside Indy/Rally/Daytona availability
and their existing Model 2 video capability. SGT itself still accepts only zero
room options; its refresh-rate control is disabled. Exact reply required;
older relays trigger reconnect using unchanged earlier greetings and hide SGT.
Creator-hosted SGT launch is rejected. The experimental/default-only/TCP status
is visible in the room UI. Expected seat/count flow GUI -> Main -> comm Ready
validation; direct ticket launches still use the validated relay roster.
Main validates the ticket, native-only options, rate, network flags and NVRAM
persistence suppression. Bootstrap and program revision guard are now reachable.

Tests use the real C++ LobbyClient and CNetBoardLink, not the full emulator:
2/3/4 seats passed discovery, combined-game capability, launch parsing,
Ready, reverse native packet flow and disconnect. An old pre-SGT relay passed
fallback to a two-cabinet Daytona room with SGT unavailable. Existing full
C++ online-client suite passed, as did updated session-policy assertions,
SGT server/transport tests, Daytona/Rally/video regressions and four C++
SGT/Rally unit tests. No GUI lab was created and no real SGT race was run.

Build SHA256: 60978cba73f1c95f4e8e1a011ac31373a6a71e310399c3f69af852b3a7faf9a2
Backup: /home/nacho/.supermodel-sgt24h-client-1795206359/ (includes previous binary).
The deployed relay process has not been restarted. Changes remain uncommitted.
Next: verify native ROM link/bootstrap, then decode motion fields and implement
UDP/local-control semantics with runtime evidence, not guessed offsets.

## Verified ROM copy semantics

Tests/run_sgt24h_rom_copy.py verifies both program ROM CRCs, interleaves in
memory, and disassembles the ROM copier using the in-tree disassembler.
PASS: 56 matching 16-byte load/store chunks at 0xbb56c..0xbb728 copy exactly
0x380 bytes and restore registers. Loader 0x56590 uploads it to 0x21be00.
There is no field conversion or packing: the first 0x380 bytes of each 0x47c
actor record are wire-identical. The tail is not copied by this routine.

Profile.h now records copier addresses/size and RowSeat(), with exhaustive
2/3/4-seat ownership tests (own echo always last, each seat represented once,
invalid geometry rejected). Both geometry and actual-ROM static tests passed.

Control evidence: 0x58758..0x587c4 checks RX record +2 against 3, compares
record +3 with another record, and tests bit 6 of +0x1a. Thus full-row UDP
replacement cannot be justified as presentation-only. Exact meanings and
pose fields remain to be traced. No broad local mirror was added.
No GUI lab or emulator session was started for this audit.

## Source-only follow-up: actor serialization

User explicitly chose source-only again; do NOT start an SGT emulator lab.

Found serializer/deserializer pair:
- ROM 0xbace0 uploaded to 0x215c00; pointer 0x5346c8.
- ROM 0xbaa80 uploaded to 0x216000; pointer 0x5346c4.
- Calls at 0xba920/0xba970 pass native TX/RX record base, subrecord index,
  and actor index. Subrecord stride 0x5c; full actor stride 0x1728 at 0x517c50.

Verified raw field transfers (NOT yet semantic pose identifications):
wire offsets are relative to record + subrecord*0x5c.
wire 0x2c/30/34: 32-bit actor source +0x7e0/7e4/7e8,
                 received actor destination +0xf98/f9c/fa0.
wire 0x38/3c/40: 32-bit actor source +0x804/808/80c,
                 received destination +0xfbc/fc0/fc4.
wire 0x44/46/48: 16-bit source +0x81c/820/824,
                 sign-extended receive destination +0xfd4/fd8/fdc.

These are promising motion candidates but X/Y/Z, units, angle scale, race
identity and ownership are not proven. No prediction/mirror writes enabled.
Follow the actor-field consumers before assigning motion semantics. State and
bit-packed control fields appear in the SAME serialized subrecords, so a raw
subrecord UDP replacement is not a safe presentation-only implementation.

## Spatial sample decoder (read-only, not a motion transport)

Position.h reads the first full subrecord's +0x2c/30/34 as little-endian
IEEE floats. It requires an exact 0x380-byte row, rejects nonfinite values,
and leaves output unchanged on failure. It makes no race/owner validity claim,
axis/unit assumption, velocity estimate, packet format or guest-memory write.
Sgt24hPositionTest passed ASan/UBSan with truncation, oversize, null, each
nonfinite component, endian vectors, input preservation and output atomicity.

Actual-ROM audit now asserts serializer 0xbad00 and deserializer 0xbaaa0
instruction sequences and floating subtractions at 0x45464. All passed.
0x3a2c8 uses first/third local components in floating spatial dot products;
0xbb0f4 scales the second by 100 and converts to signed integer in the
compact representation. The +0x38/3c/40 group is stored as halfwords by
0xbb114..0xbb128; do not assume it is floating velocity.

Still needed: consumer-based axis/angle interpretation, race/owner semantics,
which full versus compact subrecord belongs to which actor, draw-only hook,
UDP protocol freshness policy and local response analysis. No runtime tests
were authorized. The emulator binary was not rebuilt for this unused helper.

## Compact spatial format decoder

ReadCompactPosition now implements the ROM's mixed encoding: first/third
components are float32 at record +0/+8; middle is signed int16 at +4 divided
by 100. Base 0x19c, stride 0x20. Capacity 15 is ONLY the buffer bound, not a
claim that 15 actors are active or owned by this cabinet. Exact row length,
null, nonfinite and index checks; no memory writes or prediction enabled.

ASan/UBSan test passed all 65,536 signed values in all 15 bounded slots,
plus malformed lengths, invalid indices including UINT_MAX, float nonfinite
rejection and failure atomicity. Actual-ROM audit asserts serializer scaling
by 100 (0xbb0ec) and sign-extension/division by 100 (0xbaf50), both passed.

Continuing limitation: format interpretation is not actor ownership. Full and
compact record selectors still need their state/owner provenance traced.
The integer orientation candidates remain unlabeled; apparent field matches
in unrelated instructions or disassembled data are not semantic evidence.
No lab was started and the unused helper did not require an emulator rebuild.

## Verified serialization authority and format selection

ROM audit now passes real instruction PCs to the disassembler and asserts
branch targets, not only load/store offsets. New checks passed:
- 0xba8d0: actor type word == 2 selects compact path at 0xba98c.
- Full serialization at 0xba8f8 only occurs when local seat 0x51095c equals
  selected cabinet 0x510dd0; uses local TX record 0x511230 + seat*0x47c.
- Full receive at 0xba954 ALWAYS uses RX record 0x512420 + selected*0x47c,
  with actor slot r4 and full subrecord selector r6.
- Compact serialization at 0xba98c has the same local==selected gate.
- Compact receive at 0xba9bc uses selected cabinet RX and actor index r4.
- RAM pointers c4/c8/cc/d0 correspond respectively to full read/write and
  compact read/write (ROM routines baa80/bace0/baf50/bb0d0).

Implication: do NOT infer actor owner from its cabinet row position. Native
row ownership and actor serialization authority are different concepts.
A blanket local TX mirror on non-selected cabinets could substitute stale or
unpopulated poses. This is evidence of serialization authority, not yet proof
that all physics executes exclusively on one cabinet.

Selection is dynamic: 0x2497c can assign local seat in states 1/8; offline
0x24998 selects zero. 0xba474 and 0xba6bc update selection during linked
membership processing. Full subrecord selector r6 is found by scanning RX
record state +2 (3 or 4) and matching record +3 against selected cabinet +3;
it can persist between loop iterations, so it must not be guessed from actor
index alone. Compact actor type 2 does not by itself prove a CPU-car label.

Next source work: trace this selector's race context and local actor update
path; establish a presentation-only override with explicit selected-cabinet
and scene validation. No UDP, local mirror, or physics patch enabled here.

## Selected-cabinet gate also controls an actor-update loop

Actual-ROM regression checks added and passed:
0x28444 compares selected cabinet 0x510dd0 with local 0x51095c; inequality
branches to 0x286c0 (loop increment). The skipped block includes call 0x2ee00
at 0x284ac with body base 0x518408 + actor*0x1728. In that routine, one path
writes +0x28/+0x2c/+0x30 at 0x2eed4/2eedc/2eee4: those equal actor
+0x7e0/+0x7e4/+0x7e8, the serialized spatial triple. Other paths invoke
function pointers 0x52ef40/0x52ef3c. State predicates select those paths.

This strengthens authority evidence beyond serialization. It does NOT prove
that every position update or every physics stage runs only on the selected
cabinet: alternate actor paths and those indirect callees remain to trace.
Do not remove the gate to claim local control: the surrounding block includes
multiple shared actor-processing calls whose side effects are not isolated.

No production behavior changed in this step; no emulator lab or live relay
restart. Source-only verification remains the user's explicit constraint.
Next: resolve 0x52ef40/0x52ef3c targets and identify input/physics/presentation
boundaries before any local simulation or prediction hook.

## Indirect update targets and middle orientation component

Resolved uploads: 0x52ef3c -> RAM 0x217c00 from ROM 0x2cfd0;
0x52ef40 -> RAM 0x216400 from ROM 0x2ba30. Actual-ROM checks pass.
The former contains spatial integration at 0x2e51c: adds globals
0x5110c0/c4/c8 to body +0x28/2c/30 (= serialized positions).
Those increments' provenance remains to be traced; they are not assumed
constant velocities or independent per-cabinet input-driven results.

0x2e56c compares body +0x50 and +0x5c using mask 0xffff and threshold
0x7fff. Body +0x50 == actor +0x808 == full wire +0x3c == compact +0x1aa.
Added Orientation.h: bounded read-only low16 component extraction and a
shortest modular delta primitive (half-turn tie -32768). No axis label,
angular units conversion, or production smoothing hook claimed.

Sgt24hOrientationTest passed ASan/UBSan: all 65,536 full component values,
all from-values with eight wrap/boundary deltas, every bounded compact slot,
invalid lengths/indices/null, unchanged input and failure output.
The ROM audit now asserts the actual target uploads, integration instructions,
modular comparison and both serial formats' component mapping. All passed.
No binary rebuild, lab, or relay restart; gameplay behavior unchanged.

## Movement increment provenance: shared transform scratch

Resolved 0x52ef08 -> RAM 0x21bc80, loaded from ROM 0x21180 by 0x56604.
Routine takes values in g0/g1/g2 and orientation-like inputs in g3/g4/g5,
uses peripheral/table reads plus multiply/add/subtract arithmetic, and stores
outputs to 0x5110c8/c4/c0 at 0x21298..0x212a8. These are SHARED SCRATCH,
not stable actor-owned velocity fields. Other callers overwrite them.

Verified actor producer/consumer chain:
0x2e338 sets inputs (including body +4c/+5c/+54), calls transform, then
0x2e358 onward adds outputs to body position. 0x2e3b0 sets another input
set (+a0/+58/+a4/+60, with g0=g1=0) and calls transform again, overwriting
scratch before the subsequent position addition at 0x2e51c.
Actual-ROM checks for upload, stores and both call sites all passed.

Never sample scratch once per emulator frame as vehicle velocity. A future
predictor must use actor-owned state or time-stamped position differences
with verified source/scene continuity. The exact transform axis convention,
input-control source and frame phase still need tracing; no local physics
hack or UDP predictor enabled. Binary unchanged; source-only work.

## Three-state car actors versus cabinet records

Clarified naming in Profile.h: 0x47c is CabinetRecordStride (ActorStride kept
as a historical alias); actual car actors are at 0x517c50 with stride 0x1728.
Each contains three equal 0x7b8 state areas. Named offsets describe observed
use, not complete simulation ownership:
base +0; transmitted +0x7b8; received +0xf70. Position within each is +0x28,
thus transmitted +0x7e0 and received +0xf98. Middle orientation within a
state is +0x50, giving +0x808 and +0xfc0 respectively.

0x25f60 calls field-copy routine 0x16040 with base->transmitted, then
transmitted->received, advancing each car by 0x1728. This is initialization
evidence, not proof of per-frame overwrite. The copier includes control
fields +0x228/+0x22c/+0x230. Actual-ROM checks and profile tests passed.

Input tracing must follow all three areas: a write at actor +0x22c is NOT
the same field as body +0x22c at actor +0x9e4. Earlier apparent missing
writers were partly this offset-base distinction. Do not bypass selected
cabinet logic until per-frame state propagation and reconciliation are known.
No guest behavior, binary, or deployed relay change in this step.

## Local input application before the selected-cabinet physics gate

At 0x28404, actor index 0x510e60 selects g0=base state and g1=received
state; call 0x2b8f0 occurs before the 0x28444 selected/local comparison.
The call reads 0x510eb4 -> base +0xc0 and 0x510e9c -> a bounded/ramped
value written to BOTH base +0xbc and received +0xbc. An indirect handler
selected by base +0xf8 produces a value stored to both states' +0xe4.
Do not label that indirect result until its handlers are resolved.

Input processing writes 0x510e9c at 0x5f114 (calibrated conversion) and
0x510eb4 at 0x5f188 (lookup at 0xf0500). These are not network pose fields.
ROM assertions for producer stores, actor pointer setup, reads and paired
stores pass. This proves the input application call precedes the authority
check; it does NOT prove all callers/frame phases execute it, or that local
physics advances on non-selected cabinets. The downstream propagation from
base/received controls to transmitted simulation remains unresolved.

Source-only, uncommitted; no runtime input substitution or gate bypass.

## Input wire handoff into transmitted simulation state

Verified complete native input codec (ROM baa40 pack / baa00 unpack):

| Base actor field | Cabinet row | Transmitted actor field | Receive conversion |
| --- | --- | --- | --- |
| +bc | +26 | +874 = +7b8+bc | signed 16 -> 32 |
| +c0 | +28 | +878 = +7b8+c0 | unsigned byte -> 32 |
| +e4 | +29 | +89c = +7b8+e4 | unsigned byte -> 32 |
| +ec | +2a | +8a4 = +7b8+ec | unsigned byte -> 32 |

At ba8d8 actor type zero packs base controls into local TX (511230 + local
seat*47c), independent of the pose serialization authority comparison.
At ba92c, only local==selected imports controls from RX (512420+r6*47c)
into transmitted state for actor r4. r6 is the matched row selector from
the existing state/group scan, NOT necessarily actor index or local seat.
The update routine ends with a call to ba830 at 287f4; other entry calls
exist at 27c50/27c90. This is source evidence of network-mediated control
handoff, not a runtime measurement of frame delay or proof of sole writers.

Added unused read-only Inputs.h and exhaustive signed16/byte decoder tests;
ASan/UBSan pass. ROM audit verifies both codecs, type gate, selected-cabinet
import gate and TX/RX pointers; all pass. No encoder, packet mutation,
local substitution, prediction, binary rebuild, or relay restart.

Next trace the consumers of transmitted +bc/+c0/+e4/+ec, and state
reconciliation, before deciding a safe local-control implementation.

## Imported-control consumers and prediction side effects

Verified 2ee00's normal branch 2ef2c: if body +290 is zero, calls 2b0f0,
then 52ef40 (ROM 2ba30); always continues to 52ef3c (ROM 2cfd0).
Earlier mode/state branches can bypass this sequence; no claim of universal
per-frame execution is made.

2b0f0 consumes body +bc and maintains a five-word history at
52f350 + body[+4]*20. This is actor-ID-indexed shared RAM, not contained
in the three actor state areas. Existing samples shift back one slot;
current sample is clamped to [48,208]. The current-vs-oldest absolute
difference is capped at 255 and selects table 2aa70. A target derived
from (128 - body[+bc])*1093/80 and that table drives body +22c; its
per-invocation delta is clamped to [-768,+768]. This is consistent with
a steering-control filter, but the field retains its offset-based name.
ROM 2cfd0 consumes +22c (including the halfword read at 2d178).

ROM 2ba30 consumes +c0, +e4, +ec and has paths writing +b0 and +24c,
which feed the already-traced movement update. Assertions cover actual
instructions and dispatch order, not full execution-path reachability for
every game state. All ROM checks pass.

IMPORTANT: a speculative extra call into native simulation can mutate
shared control history even if only the actor structure is saved/restored.
Do not implement prediction by duplicating 2ee00 calls or copying just a
0x1728 actor. Other shared scratch, collision and event effects still need
an audit. No production prediction or local-control override added.

## Received-state consumers and source-only regression entry point

ROM 27d50 entry passes the local actor's RECEIVED state to 53b0 and to
an indirect mode handler selected by received +27c. The actor loop also
uses received positions for spatial queries and the local/other dispatch.
Other-actor call through 52ef30 receives that same state; 56910 chooses
ROM 21dd0 directly or its RAM 21a400 copy according to state 510948==3.
21dd0 computes a planar distance from received +28/+30 and globals
5110d8/5110e0. One branch writes 1 to transmitted state +29c (absolute
5186a4 + actorID*1728). Local branch 28068 clears that field, and the
outer actor loop initializes it at 2802c. These are verified state writes,
not proof that the flag is a collision gate or a prediction hook.

The received state is therefore not an immutable render snapshot. A
position substitution at its general memory reads cannot yet be called
presentation-only. Additional received-state consumers include 23b20,
which derives globals from +b0, +e4 and +23c. No whole-state replacement
or unrestricted memory-read substitution has been enabled.

New single-command source-only regression entry point:

```sh
python3 Tests/run_sgt24h_source.py --sanitize --rom roms/sgt24h.zip
```

Completed successfully: all five C++ helpers (Profile, RelayEeprom,
Position, Orientation, Inputs) under ASan/UBSan; complete actual-ROM audit;
ticketed lobby for 2/3/4 seats; transport for 2/3/4 seats at zero latency,
35ms+jitter and fallback transport. Tests use temporary helper binaries
and isolated loopback fixtures, not an emulator/GUI lab or deployed relay.
Without --rom, the audit is explicitly marked SKIP. Each subprocess has
a timeout and failure aborts the suite. No ROM is bundled by the runner.

These passing tests are NOT linked-race, low-latency local-control, UDP,
or rendering-quality acceptance. Those remain unfinished/unverified.

## Full-slot decoders and compact cross-state control side effect

Added bounded ReadFullPosition and ReadFullOrientation with explicit slot
index; first-slot helpers remain wrappers. FullStride=0x5c, FullCapacity=4
is the contiguous layout capacity between 0x2c and compact base 0x19c,
NOT active population or row-to-actor ownership. Serializer bace0 and
reader baa80 independently use slot*0x5c and actor*0x1728. Do not conflate
these indices. Unused read-only helpers, no guest memory writes.

New FullSlots tests cover distinct samples in every slot, nonfinite rejection
with atomic output and unaffected neighboring slots, all 65,536 orientation
values per slot, invalid indices (including UINT_MAX), all short lengths,
null and oversized input. Complete source suite passed with ASan/UBSan,
actual-ROM checks, 2/3/4-seat lobby and all nine transport cases.

Compact receiver side effect now asserted directly: row status byte at
compact +0x1a (absolute +0x1b6 + index*0x20), bit4, writes 10 or zero to
BOTH received actor +0x105c and transmitted actor +0x8a4. Both correspond
to state +0xec, one of the previously traced input/simulation fields.
This is a concrete reason not to feed unsequenced whole compact records
through the native decoder as a supposedly visual-only UDP update.

All changes source-only/uncommitted; no binary rebuild or live relay change.

## Host analog inputs through ROM calibration to wire fields

Model2.cpp's Indy500 input layout maps SGT steering to channel0, reversed
accelerator to channel1, reversed brake to channel2. IO offset 0x0f selects
an analog channel on write and reads/advances it modulo8. ROM 5ef60 resets
that selector through 1c0001e, then reads channels into 510a10/11/12.
Actual-ROM assertions cover this sequence. Emulator source mapping was
manually inspected; it is not covered by the ROM assertion itself.

Channel0 -> calibrated 510e9c -> base +bc -> wire +26 -> transmitted +bc
-> control-history/rate limiter -> transmitted +22c -> simulation.
Channel1 -> pedal transform/calibration/table f0500 -> 510eb4 -> base +c0
-> wire +28 -> transmitted +c0 -> movement-state routine.
Channel2 -> calibration/table f0900 -> 510ec8 -> base +ec -> wire +2a
-> transmitted +ec -> movement-state routine. Mode-specific overrides at
2ba0c onward mean wire brake is not universally the direct table output.

Input handler ed610 entries 0/1 resolve to ROM 2b260/2b6f0, both preserving
previous +e4 into +e8. Their branching includes speed and shift globals;
complete gear semantics still not asserted. Decoder names remain offset-
based to avoid treating input codes as raw axis magnitudes or gear numbers.

Full source suite passed again (six sanitizer helper suites, ROM audit,
2/3/4-seat lobby and nine transport cases). No calibration bypass, runtime
input writes, binary rebuild or production relay change. Source-only.

## Actor role initialization and phase-specific membership

The linked initializer builds membership from RX group byte +3 matching
the selected cabinet and state +2 being 3 OR 8 (24ecc). It walks matching
cabinet records when constructing actors. At 25130, default actor type is
2. In the applicable linked mode branch, matching group/state plus cabinet
index==local seat sets type0; another matching seat sets type1 (2518c..
251e4). Later the type0 actor index is stored to 510e60 at 25e8c.

Added explicit Profile constants LocalActorType=0, LinkedPeerActorType=1,
CompactActorType=2, LocalActorIndex=510e60, SelectedCabinet=510dd0.
These describe this initialization path; offline/attract/replay mode branches
can override types. Type2 is not universally labeled CPU without tracing
those paths. Never use type alone as a race-validity or ownership check.

IMPORTANT phase difference: the per-frame ba830 mapping accepts row states
3/4, whereas initialization accepts 3/8. Both are now ROM-asserted. A generic
'active row' predicate shared across phases would silently alter native
membership. Also, local actor index is not cabinet index: initialization
places cabinet-associated actors according to matching membership and actor
population, rather than an unconditional actor==seat mapping.

All six sanitizer helper suites, expanded actual-ROM audit, ticketed lobby
and nine transport cases passed. No runtime ownership shortcut introduced;
source-only and uncommitted, binary and deployed relay unchanged.

## State-2 authority selection, including no-match retention

Verified ba490 selection branch: only state 510948==2 enters ba600.
Scans all FOUR cabinet slots (two unrolled iterations), not link-count slots.
A match requires row +2==2 AND bit0 of row +1a. First match sets selected
510dd0 and related cabinet index 510dcc; total matches becomes 510dd4.
The first-match index and running count also update local TX +3/+4.
With no matches, loaded previous globals are written back unchanged and
those TX bytes are not rewritten. An empty scan is NOT a disconnect signal.

Added unused Selection.h source model of the three global results. It does
not implement TX writes, other phases, timeout handling or live authority.
Tests exhaust all 16 membership masks with prior selected/group/count states,
all 256 state bytes x 256 flags per slot, and authority-change/empty scans.
Actual-ROM assertions cover phase gate, prior loads, both unrolled branches,
four-slot bound and result stores. Full seven-helper ASan/UBSan suite plus
ROM, lobby and transport tests passed. No production behavior change.

Future motion continuity must invalidate on observed authority/scene changes,
but neither retained selection nor finite position proves freshness. Freshness
and sequencing need independent transport/session evidence; do not infer
liveness from this ROM selection routine.

## Reproducible real-client integration and older-relay fallback

run_sgt24h_source.py now accepts --integration to compile/run the real C++
lobby-client and session-policy harnesses, in addition to helper/ROM/Python
protocol tests. --sanitize propagates to both integration runners. Optional
--pre-sgt24h-relay /path/to/older/relay.py exposes the previously manual
pre-SGT handshake fallback case through run_online_client.py. No backup
path is hardcoded; invalid paths or use without --integration are rejected.

Executed the complete suite with --sanitize --rom roms/sgt24h.zip
--integration and the preserved pre-SGT relay source. PASS: C++ SGT
2/3/4 discovery, native TCP tickets, ordered board ring, disconnect;
all existing client regression cases and session policy; old relay greeting
rejection/reconnect fallback with Daytona 2-seat launch and SGT capability
hidden. Expected rejected-greeting messages are part of that fallback test.
Log: ~/.supermodel-sgt24h-research/source-integration.log.

This builds harnesses only, not the emulator binary, and uses isolated
loopback fixtures, not the live relay. No GUI/emulator lab was started.
Main.cpp SaveNVRAM relay guard was also inspected: it returns before disk
save while NetRelay is set, preserving session-role isolation. No NVRAM
code change was necessary. Linked race/local-control/UDP acceptance is
still outstanding despite the passing real-client protocol tests.

## Narrow geometry-read candidates (NOT enabled hooks)

In other-actor routine ROM 21dd0, distance selection and cross-state flag
writes precede the three loads at 21f20/21f24/21f28. Those load received
body +28/+2c/+30 via g0 into g5/g6/g7, followed by command write 880120
and consecutive stores through pointer loaded from 59f40. This is a much
narrower candidate for visual substitution than general received-state reads.
Actual instructions and FIFO stores are now ROM-asserted.

Routine is uploaded from ROM 21dd0 to RAM 21a400, 0xc00 bytes (64 groups
of 12 words). Candidate RAM PCs are 21a550/21a554/21a558. Copy setup and
loop increments/bound are asserted, preventing the common error of matching
only ROM PCs when state3 dispatch uses RAM. Direct ROM dispatch exists in
other states and must NOT be enabled merely because the opcode matches.

No hook installed. Before enabling: audit live register/return-PC context,
all downstream uses of loaded values, per-actor slot ownership, scene/race
validity, epoch and freshness, local/remote distinction, and camera/LOD
consistency. Local actor uses a different dispatch path. These candidates
therefore do not solve local input latency, even if remote visual smoothing
is eventually proven safe. Full runtime quality remains unverified.

## Candidate draw-read downstream register audit

After position emission 21f38/3c/40, g5 is overwritten by actor type at
21f44, and g6 by peripheral pointer at 21ff0. The possible intervening
call at 21fd0 goes through 52ef10, assigned ROM 72c90 at 21ecc.
That callee was inspected in full: it replaces g6/g5 and uses geometry
FIFO output plus the g0 model index; it does not consume incoming g7.
The body split at 22044 selects detailed versus coarse geometry: g7 is
replaced at 221b0 on the detailed path and 224dc on the coarse path.
Added ROM checks for these overwritten registers, branch, target assignment
and complete emission-callee instruction sequence. All pass.

This strengthens a remote geometry-only substitution candidate but is NOT
a generic control-flow proof or an enabled draw patch. Tests pin the inspected
sites; they do not automatically discover every future dataflow edge. Original
positions must still feed distance/culling and game-state consumers. Before
runtime use: validate exact live actor address and call context, cache a whole
triple once per drawing event, preserve camera/LOD behavior, bind each actor
to the correct native full slot, enforce authority/scene/session continuity,
and reject stale/unsequenced samples. Local input response remains separate.

No emulator binary rebuild, runtime hook, UDP enablement or relay restart.

## Native full-record slot binding metadata

At ba968, full receive is called with g1=r6 (matched slot), g2=r4 (actor),
and g0=selected cabinet RX record. At bacc8 the decoder stores g1 into
actor +f6c. This is transmitted-state +7b4, the last word of that state.
At 283a8, non-type2 actors copy absolute 518bbc+actorOffset to
519374+actorOffset: transmitted +7b4 -> received +7b4 (actor +1724).
This copy precedes the actor-update block and final ba830 receive call;
therefore the two metadata copies can belong to different update phases.

A consumer at 55934 reads state +7b4, multiplies by 47c and reads RX state
+2; it also embeds this index in a command at 510960. This corroborates
cabinet-slot meaning, not merely an arbitrary spatial-array index. Added
Profile offsets and ROM assertions for call arguments, decoder store,
non-type2 metadata copy and consumer. Full source-only suite passed.

A future binding should corroborate the decoded slot with current scan
membership, actor type, selected authority, local actor and scene/session
identity. It must not use received +7b4 alone: that word can lag the last
receive and carries no sequence/timestamp. Compact actors skip this frame
copy and their native decoder does not establish this full-slot binding.
No runtime binding or substitution enabled; binary/relay unchanged.

## Conservative fresh membership matcher

Added unused read-only Membership.h::FindFullSlot. It applies the verified
per-frame state3/4 + selected-group predicate in ascending cabinet order
from an explicit cursor. Bounds enforce linked count2..4, selected<count,
and start<count. Exhaustion/invalid inputs return false without changing
output. Caller MUST honor the boolean: unchanged output is not a valid
fresh binding. No previous slot is silently promoted into a new match.

This intentionally differs from the complete native ba830 routine, which
can retain r6, skips scanning type2 actors, and has a state1 local-slot
override. The helper is only the fresh-match predicate, not an emulator
of every mode or a replacement for native selection. Inputs must be in
cabinet order, not raw rotated comm-RX row order. State8 is initialization
membership, not eligible here. Packet freshness/session validity remain
independent requirements.

Tests exhaust eligible-state/group masks across counts/selections/cursors,
all state/group byte values, out-of-range inputs, unchanged inputs/output,
and no-match transitions after previous success. Expanded ROM checks pin
eligibility, scan increments and assignment. Eight sanitizer helpers and
full ROM/Python lobby/transport suite pass; C++ integration not rerun this
step. No runtime binding or smoothing enabled, uncommitted source only.

## Ring-byte metadata to cabinet-order metadata

Added unused ReadRingMembership in Membership.h. Input is ONLY the active
RX span starting at comm offset2380: exactly count*380 bytes, count2..4,
local<count. It rotates next-seat-first/own-last rows into cabinet order
using the existing verified RowSeat geometry, reads bytes +2/+3 and clears
inactive output entries. Invalid inputs fail without changing output.
This does not accept a whole e00+tag network frame or guest 47c records.

ROM audit now pins source1a12380, destination512420, destination wrap,
source stride380 and destination stride47c in b9634..b9690. Nine-helper
source suite passes under ASan/UBSan plus ROM/lobby/transport tests.
New tests cover every2/3/4-cabinet rotation, all short/long lengths through
FrameBytes+1, null, invalid counts/seats, output atomicity, unchanged input,
and end-to-end use of rotated metadata by FindFullSlot.

No runtime use yet: packet arrival alone is not a certified native-copy
boundary, nor proof of scene/authority continuity. No binary or relay change.

## Native copy-context witness (unused)

CopyContext.h validates the last word read of the final ldq at RAM21bfd0
(ROM bb720), returnPC b9678, exact source1a12380+ringRow*380, destination
512420+cabinet*47c, full32 mask, linked count/local bounds and rotated
cabinet identity. Rejects offline/self-copy and outgoing-copy call sites,
ROM-PC rather than relocated execution, alias/destination mismatch and
wrong addresses/masks. Not connected to emulator read handlers.

This witnesses a source read, NOT completion (two stq instructions still
follow), and NOT network freshness. CPU, programCRC, online session/scene
must be separately verified by a future caller; replaying the same shared
RAM may yield many valid copy contexts for one received frame. Live CPU
register/PC semantics still require integration validation.

Initial new test compile caught a missing initializer_list include; fixed.
All ten helpers then passed ASan/UBSan; ROM audit and Python lobby/transport
suite passed. C++ client integration not rerun. No production behavior change.

## Applied-frame freshness model, not copy-count freshness

Inspected Model2Comm receive/application paths: both Tick's established-link
loop and ReadFGLink can write validated received payloads to shared RX.
ROM copy events can recur without any newly applied network frame. FC
control is handled separately and must not renew motion freshness.

Added unused Freshness.h with caller-provided nonzero session epoch,
monotonic applied generation/time, read-only token validation, caller-chosen
maximum age, invalidation on bad clock/zero time, and explicit Clear.
Same-time successive network applications still advance generation; repeated
Current/Token calls never renew age. Old-generation and old-epoch tokens
fail. A new generation certifies application, NOT sender-side novelty: TCP
may contain repeated poses and this helper does not invent a wire sequence.

Integration obligations remain: notify only after complete validated RX
application in BOTH receive paths; assign unique epochs across resets,
load-state and reconnect; clear on disconnect/failure; serialize access;
combine with ROM/CPU/copy/scene/authority/actor binding guards. No callback
is wired into transport yet. This helper deliberately has no default timeout.

Eleven helper suites passed ASan/UBSan, including repeated-copy observation,
exact age boundaries, same-time frames, rollback, stale generation/session,
clear/rebegin, and near-UINT64_MAX clocks; full ROM/Python lobby/transport
suite passed. Source-only and uncommitted; no emulator or relay change.

## Integrated application freshness observation (source, not deployed)

Freshness is now wired into Model2Comm, rather than only a standalone helper.
Both Tick and ReadFGLink notify AFTER accepted data has been copied into
shared RX. FC/control paths do not notify. SGT-only checks require connected,
nonfailed, enabled/alive link, count2..4 and valid native ID, RX2380, sizee00,
and received size e01. Invalid observation clears the tracker fail-closed;
it remains disabled until a lifecycle reset. Delivery bytes/order unchanged.

Init, Reset, WriteCN and LoadState advance a comm-instance-local epoch.
Native failure, lost connection and relay-size mismatch clear observations.
New Sgt24hAppliedToken/Current queries expose read-only application identity
and age; failed/disabled/disconnected queries reject. Tracker fields have a
mutex; callers must still follow the emulator's existing thread ownership
for board state. Tokens must never cross comm-object lifetimes.

Actual Model2Comm.cpp passed syntax checking and object compilation. Full
11-helper sanitizer/ROM/Python lobby/transport suite passed, and diff --check
passed for changed comm files. These tests do not execute the new observer
inside an emulated race; no runtime verification is claimed. Backup:
~/.supermodel-sgt24h-freshness-1795224953/Model2Comm.{h,cpp}.

No emulator executable relink/replacement or lab/relay restart. Copy-context
consumer and drawing path remain unwired; UDP and local-control work remain
unfinished. The integrated observer is measurement infrastructure only.

## Copy observer wired to applied-frame tokens (source, not deployed)

Model2 comm-memory Read32 now invokes the SGT observer only for configured
SGT Online, i960, full-word access and verified relocated final-load PC.
It passes RIP, copier g9/g11 source/destination and original address to the
strict CopyContext matcher. Native returned data is untouched. Startup's
existing SGT program CRC gate remains the revision guard.

Model2Comm binds the matched zero-based cabinet to the current applied
frame token; it requires an active configured link and native ID matching
the reserved seat. No call to Applied occurs here: repeated guest copies
cannot refresh arrival age. New network applications clear all witnesses;
reset/load/failure clears them too. Read-only Sgt24hCopyToken rejects stale
application generations, epoch, expiry and disconnected/disabled links.
Witnesses certify last source read, NOT completed decoder or actor writes.

Changed Model2Comm compiled to an object and Model2.cpp passed syntax check
using repository Src/Pkgs GL headers (initial command omitted that include
path; corrected). Eleven sanitizer helpers, ROM and Python lobby/transport
tests passed. These are not a runtime execution test of the observer.
Backup: ~/.supermodel-sgt24h-copy-observer-1795225089/.

No car state/read value, packet contents, control or prediction behavior
changed. No emulator executable relink/replacement, lab or relay restart.
The next connection is actor decode/binding to the copy witness, then a
strictly scoped presentation consumer. Local-control remains separate.

## Full actor decode observer connected (source, not deployed)

Model2 work-RAM Write32 now observes AFTER the native final full-decoder
store, only configured SGT/i960/full-mask/PC21624c. Added ROM assertions
for relocation, final store and actor-base g2 plus caller RIPba97c.
The observer validates actor bounds/alignment/ID, nonlocal linked-peer type,
slot bounds, metadata slot, native roster/link-enabled state, authority RX
pointer, group/state membership and a fresh-match reconstruction across
all preceding full actors. Exhausted native scans or unknown types fail
closed rather than certifying retained r6. Compact actors are skipped.

Accepted entries bind actor+slot+authority+group+phase+frame to the authority
cabinet's applied-copy token. Reset and LoadState clear the array. A new
entry clears conflicting same-actor bindings. No position substitution,
physics call or network write is performed. Entries are not consumed by a
draw path yet, and may remain stored after token expiry; any future consumer
MUST validate token freshness and current scene/authority again. Capture
uses unlimited age deliberately: capture records provenance, not permission
to render/predict. No unverified timeout silently becomes gameplay policy.

Model2.cpp syntax check and diff check pass. Eleven sanitizer helpers, ROM
and Python lobby/transport tests pass; new runtime observer execution is
not verified in an emulated race. Backup:
~/.supermodel-sgt24h-decode-observer-1795226052/.
No executable replacement, lab or relay restart; uncommitted source only.

## Draw-side provenance observer connected (source, not deployed)

Model2 work-RAM Read32 now observes only RAM PCs21a550/54/58 for SGT i960
full-word reads. ObserveSgt24hDraw requires RIP2811c (other-actor dispatch),
exact received-body component address, actor alignment/bounds, nonlocal
linked-peer role, runtime state3 and existing 510f14==1 flag. It checks
current native count, authority, group, row membership and decoded slot,
then requires the binding's token to match the current authority-copy token.
Observation ceiling is100ms and at most one emulator frame since decode;
these are conservative observation limits, not a prediction policy.

At the first eligible axis it snapshots the native triple. Subsequent axes
must arrive in order with the same token/frame/actor and unchanged component.
Partial observations are not complete (axes must be7). No value is substituted:
Read32 still returns its original native data. Reset/load clear observations.
Old stored records must never be consumed without revalidation. No public
prediction consumer currently reads them.

Model2 syntax check and existing eleven-helper/ROM/Python protocol suite pass.
This does not dynamically test the new draw observer or prove acceptance
rates; no emulator lab or live binary was run. Source-level chain now reaches
network application -> native copy -> actor decode -> scoped draw observation.
Remaining: direct observer tests/diagnostics, full scene-transition identity,
actual smoothing policy, UDP integration and separate local-control solution.
Backup: ~/.supermodel-sgt24h-draw-observer-1795227328/.

## Actual two-cabinet native link and race-entry test

User explicitly authorized an isolated SGT runtime test, superseding the
source-only restriction for this lab. Used private bwrap process/mount/IPC
namespaces, private Xvfb displays, writable temporary cabinet homes/config,
and a private loopback relay on an OS-selected port. Network namespace
loopback setup was denied, so network was shared but relay bound127.0.0.1
on its own ephemeral port. No production relay restart or stock rom_session
script; no blanket pkill. Lab processes stopped after evidence capture.

Tested an unchanged copy of bin/supermodel, SHA256
60978cba73f1c95f4e8e1a011ac31373a6a71e310399c3f69af852b3a7faf9a2.
This predates the recent observer source changes. Private relay uses current
source and ticketed SGT lobby. Both native logs confirm assigned MASTER/CAR1
and SLAVE/CAR2, then link established id1/2 and id2/2. AsyncNet=0 and
Model2CommFrameSync=0. Game passes boot to attract mode; inserted coin3,
start1, then accelerator Up to confirm course/car choices. Both enter race:
red/white player cars, positions7/8 and8/8, LAP1/5 and closely aligned race
timers (captured ~14.9s lap time, remaining24). Confirms actual two-cabinet
link plus race entry, NOT WAN latency behavior or completion of a full race.

Initial lab attempt failed before game boot due missing copied Assets;
fixed the private lab assets, no emulator patch. Lobby fixture clients time
out after launch because they stop servicing lobby traffic; native gameplay
channels continued through race entry. Production deployment equivalence
has not been established. No emulator rebuild was needed for this success.

Evidence and reproducible private script retained at
~/.supermodel-sgt-link.haaPdI/ (lab.py, run.sh, cab0/cab1 logs/screenshots).
All source remains uncommitted. Stop expanding motion features to diagnose
basic link failures: this tested baseline already links on the private relay.

## Two-cabinet link/race entry with simulated delay

Repeated the authorized isolated runtime lab using the same unchanged binary
and fresh temporary cabinet homes. Applied latency to the LOBBY HUB (which
creates gameplay rooms), not just the legacy Room. Relay explicitly logged:
35ms one-way per direction, +/-8ms random jitter. An initial setup that only
configured the legacy room was stopped before counting it as a delay test.

Both game logs report native id1/2 and id2/2 established. Both pass selection
and enter the race. Matched capture cycle8 shows red/white player cars in
positions7/8 and8/8, LAP1/5 and remaining time25. Captures are sequential,
not simultaneous: lap clocks11.43/12.65 do not establish exact synchronization.
No input-latency or motion-smoothness measurement is claimed. This establishes
link and race-entry tolerance at this simulated delay, not WAN certification.

All four lab processes alive through120s; harness completed and terminated
its processes, after which expected channel-close/room-failed cleanup logs
appeared. No live relay restart, production binary change or NVRAM write.
Evidence: ~/.supermodel-sgt-delay.1izc6w2g/ (lab.py/run.sh, cab logs and numbered
captures). Existing optional missing driveboard ROM warning did not prevent
link/race entry. Production deployment parity still unverified.

## Four-cabinet native link and race-entry runtime test

Expanded the authorized private SGT lab to FOUR cabinets with no injected
delay and the same unchanged emulator binary. All logs confirm reserved
roles and native links id1/4, id2/4, id3/4, id4/4. Coin/start/accelerator
confirmation took all four through selection into the race. Numbered
capture8 shows red/white/yellow/blue players at positions1/4,2/4,3/4,4/4;
rear cabinets visibly show other player cars ahead. All show LAP1/5 and
remaining28. Captures are sequential (~0.5s apart), so their lap-clock
spread is not a measured synchronization defect or latency result.

All eight Xvfb/emulator processes remained alive through120s, then the
harness exited0 and cleaned them up. Room-failed/channel-close messages
occurred during deliberate teardown. No production relay restart or binary
replacement; source stays uncommitted. Evidence/private scripts retained:
~/.supermodel-sgt-four.twir5sw9/. This verifies maximum-cabinet link and race
entry locally, not full-race completion, input response or WAN stability.

## Four-cabinet link/race entry with delay: PASS

Ran the unchanged baseline binary with four authorized cabinets and lobby-Hub
latency35ms one-way per direction +/-8ms jitter; relay explicitly confirmed
latency active. All four native logs establish id1..4 of4. Capture cycle8
shows all four in LAP1/5, positions1/4..4/4, red/white/yellow/blue cars,
remaining30; rear views show other player cars. Screens captured sequentially,
so different lap times are not simultaneous timing measurements.

All8 processes stayed alive through120s. Harness exited0 after intentional
teardown; subsequent room-failed/channel-close messages are cleanup, not an
in-race disconnect. Evidence: ~/.supermodel-sgt-four-delay.urph_o7a/.
No emulator/source gameplay changes, binary replacement, live relay restart,
or persistent production NVRAM changes. Changes remain uncommitted.

Verified runtime matrix now: 2 and4 cabinets, each with zero injected delay
and35ms +/-8ms jitter, native linking and race entry PASS. This does not
certify3-cabinet runtime (protocol tests only), completed races, WAN deployment,
measured input response, UDP motion, or recent source-only observer code.

## Three-cabinet delayed link and basic driving: PASS

Private authorized lab ran three cabinets with lobby-Hub latency35ms +/-8ms,
unchanged baseline binary. Logs report native links1/3,2/3,3/3. All entered
race with six total cars. Held accelerator on cabinet3 only from capture
cycle8 through11. Yellow car at near-idle before hold reached110km/h in
cycle9, progressed along track and advanced position; cabinet1's cycle9
screen shows yellow car beside red. White remained near-idle. With no
steering, yellow subsequently hit the barrier and reached TIME IS UP by
cycle12. This confirms basic input/movement response and observed peer
presence, not a measured input-latency budget or completed race.

All six Xvfb/emulator processes alive through120s; harness exit0 and scoped
cleanup. No game-log link-loss/timeouts before teardown. Evidence:
~/.supermodel-sgt-three-drive.39ezdshz/. No live relay change, executable
rebuild, production NVRAM change or commit. Runtime verified cabinet counts
now2/3/4 at35ms+/-8ms;2/4 also at zero injected delay. Source-only observer
changes are NOT in this tested baseline executable.

## Experimental UDP negotiation and prediction groundwork

Added opt-in Sgt24hPoseUdp (default false) at model2 link creation, dedicated
SGP1 greeting, 960-byte snapshot envelope limit, optional UDP config handling
in NetBoardLink, and relay admission/rate limits. All participating clients
must advertise SGT pose capability; mixed clients and TCP-only relays retain
the native TCP ring. No TCP control/state payload is replaced. Syntax checks
pass. New loopback tests pass2/3/4 UDP routing, mixed fallback and TCP-only
fallback, plus exact native TCP payload forwarding. The transport test payload
is deliberately opaque: it does not validate semantic pose decoding.

Added bounded constant-velocity Prediction.h core: requires matching epoch,
authority and slot, positive ordered sample times, finite positions, bounded
gap/age/speed/displacement; rejects discontinuities and preserves output on
failure. Limits must be supplied in native units; no guessed tuning enabled.
ASan/UBSan tests pass. Included both tests in source suite.

NOT FINISHED: gameplay snapshot format/lineage encoder+decoder, actual pose
send/receive integration, draw substitution and runtime tuning. Prediction
core is unused by gameplay. UDP negotiation is compiled-source only, opt-in
off by default; baseline executable and production relay were not replaced.
Do not describe this stage as working UDP prediction in-game.
Backup: ~/.supermodel-sgt24h-udp-1795235699/.

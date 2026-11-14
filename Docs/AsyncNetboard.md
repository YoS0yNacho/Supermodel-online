# Experimental asynchronous HLE netboard

This change removes network-data waits from the HLE netboard's frame path when
`AsyncNet` is enabled. It lets local emulation continue at its normal speed while
the ring is exchanging data over a high-latency connection. It does **not** remove
network latency, guarantee game-level synchronization, or implement rollback.

For supported exact-ROM games, async mode can coordinate native race entry
before the countdown. That brief pre-race hold does not restore per-frame
lockstep during racing.

The normal, synchronized HLE mode remains the default. The low-level emulated
netboard is unchanged.

## Enable it

Use the **same patched build, same game, and async setting on every cabinet**:

```sh
supermodel <rom.zip> -net -simulate-netboard -async-netboard
```

Keep your existing ring addresses, TCP ports, and in-game master/slave settings.
For example, on two different computers:

```sh
# Cabinet A (set master in the game's test menu)
supermodel <rom.zip> -net -simulate-netboard -async-netboard \
  -port-in=1970 -port-out=1970 -address-out=<cabinet-B-IP>

# Cabinet B (set slave in the game's test menu)
supermodel <rom.zip> -net -simulate-netboard -async-netboard \
  -port-in=1970 -port-out=1970 -address-out=<cabinet-A-IP>
```

Allow the configured incoming TCP ports through your firewall. These commands do
not provide NAT traversal. On one computer, use different incoming ports and
point each instance at the other's port instead.

Alternatively, in the applicable section of `Config/Supermodel.ini`:

```ini
Network = true
SimulateNet = true
AsyncNet = true
```

To return to frame-locked behavior, use `-sync-netboard` or `AsyncNet = false` on
**all** cabinets. `-async-netboard` does not itself turn on networking or override
`-emulate-netboard`.

## Implementation

- `CNetBoardLink::TryReceive()` distinguishes a pending receive from a packet or
  a closed/broken link, without waiting for network data. The existing blocking
  `Receive()` remains available to synchronized HLE and LLE.
- TCP receive and send already run in `CNetServer` and `CTCPConnection` background
  threads. No additional HLE worker is needed. Guest RAM and HLE state continue
  to be accessed only by the emulation thread.
- Setup uses a bounded, nonblocking state machine for the GUID-verification and
  cabinet-enumeration passes. The type-1 heartbeat continues while packets are
  pending. Type-2 playable/relay indices are preserved.
- Async uses a different GUID to reject accidentally mixed synchronized and
  async cabinets. This is a mode/protocol check, not a hash of the executable.
- In the ready state, supported exact-ROM games (`dayto2pe`, `daytona2`, `scud`,
  `scudplus`, `srally2`, `lemans24`) wrap each native segment in a 48-byte `TimedSegment`
  header (`hops`, monotonic timestamps, accumulated one-way `ageUs` and
  `uncertaintyUs`; the origin dates the segment by the game's last CommRAM
  write, `CSimNetBoard::SampleTiming`, so `uncertaintyUs` starts at the span
  of that write burst rather than at a full frame) and stream changed local
  snapshots at a bounded ~60 Hz wall-
  clock pace (`MaxInFlight = 48`, `MaxUnackedFromPeer = 30`) without waiting
  for full-ring completion. Arriving peer hops are forwarded by the network
  receive thread the moment they arrive (`CNetBoardLink::EnableEarlyForward`),
  so a hop costs network time only instead of also waiting for the next
  emulated frame; the emulation thread still publishes the payload at its own
  pace and adds the queue dwell time to the sample age. Sega Rally 2 uses
  the same forwarding; what its barriers need is covered by the publication
  rule below. In relay sessions whose members all advertise
  it, the relay fans the segments out itself (`RLS1`, see
  `Docs/OnlineLobbies.md`): the receive thread still dates each arrival but
  forwards nothing. Peer hops are published directly to their
  corresponding CommRAM slot (`slot = incomingHops`)
  so remote cars update at 60 Hz with one-way hop age instead of stepping once
  per full-ring RTT. Untimed legacy games retain stop-and-wait complete-ring
  publication (`m_asyncWorking` -> `m_asyncLatest`).
- Unchanged payloads while rounds are in flight and stalled-peer links are
  flow-controlled so outstanding HLE queues remain bounded even under fast-
  forward or paused peers.
- Each alternating CommRAM bank is refreshed from `m_asyncLatest` before that
  bank is exposed to the game. By default, the local echo slot uses current
  local data rather than waiting for its own packet to return; games whose
  native ring barriers depend on round-consistent returning echo (`srally2`)
  preserve the returning ring snapshot and publish at most one segment per
  slot per frame: the receive drain stops at a returning self-echo and before
  a slot that already received a segment this frame (`PeekNextSlot`), so a
  value held for a single segment is never overwritten before the game has
  seen it, whatever path (ring, receive-thread forwarding, relay fan-out)
  delivered it. A backlog of more than three rounds is drained freely.
- Frame counters and bank swaps continue at the local emulation rate. Remote
  snapshot updates remain limited by ring traversal time and peer scheduling.
  Other ROMs have no game-specific prediction or interpolation; native
  packet handling remains unchanged.
- Setup packet lengths, segment lengths, machine counts, and per-bank RAM bounds
  are checked before copying. A broken link, malformed packet, or 30 seconds
  without protocol progress moves the board to its error state. The watchdog
  uses wall-clock time, not the number of emulated frames.
- Async reset sends a link-broken notification without waiting for an
  acknowledgement, discards snapshots, and restores the initial bank pointers.
  Initialization also clears RAM from the allocation base, avoiding an overrun
  when the previously active bank was the upper bank.

## Race-start barrier timing

Supported games (`dayto2pe`, `daytona2`, `scud`, `scudplus`, `lemans24`,
`srally2`) hold each cabinet at a game-specific checkpoint just before the race
intro and release all of them at one agreed moment. The leader (lowest native
participant) runs a three-step token exchange over the ring:

1. **collect** — repeated every 100 ms until every participant is at the gate
   and has a fresh clock sample to its upstream neighbour;
2. **prepare** — carries the release deadline, translated hop by hop into each
   cabinet's monotonic clock;
3. **commit** — point of no return; everyone releases at the deadline.

The deadline lead is **measured, not fixed**: `lead = 3 × round + 300 ms`,
clamped to `[0.5 s, 20 s]`, where `round` is the longest collect/prepare token
round seen in this barrier (tokens are forwarded by the emulation thread, so a
round already includes one frame of scheduling per hop). Prepare and commit
need two rounds; the third round plus the margin is headroom for the last
cabinet, which must still see the deadline at least 100 ms ahead when the
commit arrives. On a LAN that is the 0.5 s floor; at 100 ms per hop with two
cabinets it is about 1 s. The earlier fixed two-second floor is gone.

Nothing is allowed to go unsynchronized when the lead turns out too short:

- A cabinet that receives a prepare whose deadline is already too close (or
  whose clock sample is stale) **declines** it: it clears its clock bit in the
  token and arms nothing. Cabinets further downstream see the missing bit and
  forward the token unarmed. The leader sees the incomplete acknowledgement and
  re-collects with a longer lead (two more rounds per retry, three retries, then
  the usual fatal error).
- A prepared cabinet whose deadline passes without a commit **lapses** instead
  of failing: it disarms and waits for the next prepare. This is safe because
  the leader commits only while the deadline is still at least one prepare
  round plus 150 ms ahead, so a commit for that serial can no longer be
  accepted anywhere; a commit that does arrive late is still rejected (fatal),
  and the leader itself still fails if its own commit does not close in time.

The log shows the whole sequence per cabinet: `start READY`, `start PREPARE
... round_ms=.. lead_ms=..`, optional `prepare DECLINED` / `prepare LAPSED` /
`prepare RETRY`, then `start RELEASE ... late_ms=.. held_ms=..`.
`Tests/run_async_netboard.py` covers it with `start-fast` (release under one
second after the last cabinet reaches the gate), `start-stall` (a cabinet
stops servicing its board for longer than the lead: decline, lapse, retry,
release, no errors) and the existing start scenarios up to 650 ms per hop.
Set `ASYNC_NETBOARD_TEST_VERBOSE=1` to see the board's info log in the test
output; `--only <prefix>` runs a subset.

## Remote-car correction smoothing

Remote cars are dead-reckoned from the last sample and, when a newer sample
arrives, the difference between the shown car and the new target is blended
out as `correction * e^(-elapsed / tau)`. At 200 ms of one-way delay the old
code produced a faint, periodic "micro rubber band" even with a steady relay.
Two things caused it, both in the Daytona 2 predictor and both fixed:

* the dead-reckoning horizon was capped exactly at the measured latency, so a
  packet that was one frame late made the shown car stall for a frame and
  then catch up. The horizon now gets three frames of headroom over the
  latency (`HoldHeadroomFrames`) when smoothing is enabled;
* the non-smooth target branch used a different lead than the stalled-link
  branch, so crossing the horizon produced a step in the velocity estimate
  (the finite difference in `EvaluateVelocity`), which the velocity
  correction then amplified into a jump. The branches now use the same lead
  and the velocity correction is clamped to `MaxSpeed`.

On top of that, `Src/Network/Games/CorrectionSmoothing.h` stretches `tau` for
corrections that are small compared with how far the car moves per frame
(`ErrorScaledTau`) and raises the knee of the far-latency term
(`FarLatencyTau`). It is shared by the Daytona 2, Scud Race, Le Mans 24 and
Sega Rally 2 predictors. The strength is a knob:

| `NetPredictionSmoothing` / `-net-smoothing=` | Effect |
| --- | --- |
| `0` | legacy fixed blend (also keeps the legacy horizon) |
| `1` | default: error-proportional blend, hold headroom |
| `2` | stronger stretch; a little more position error for a little less noise |

Below ~115 ms of sample age the stretch is not applied at all, so a LAN or a
nearby relay sees no added lag. The `predict-rubberband` scenario in
`Tests/run_async_netboard.py` drives the Daytona 2 predictor with a synthetic
path at 100 / 200 / 300 ms (20% one-frame jitter) and reports per level the
frame-to-frame roughness of the shown car and its mean error against the
true path. Representative numbers (roughness rms / mean error, world units):

| latency | level 0 | level 1 | level 2 |
| --- | --- | --- | --- |
| 100 ms | 0.028 / 1.54 | 0.028 / 1.54 | 0.028 / 1.54 |
| 200 ms | 0.594 / 4.65 | 0.034 / 3.53 | 0.035 / 3.57 |
| 300 ms | 0.154 / 6.61 | 0.071 / 6.24 | 0.072 / 6.25 |

`RUBBERBAND_REPORT_ONLY=1` prints the table without asserting on it, for
tuning.

## Adaptive update tick rate

The update tick rate that a relay or self-host room assigns (`RLT1`) is a
ceiling. When every cabinet in the room advertised the capability (lobby HELLO
bit 8) the host also sends `RLT2 {version, flags, ceiling, floor}` after the
initial `RLT1`; from then on it may send further `RLT1` frames mid-session and
the client applies them without touching its race pacing state
(`SimNetBoard::SetTickRate(hz, midSession=true)`): the exchange interval, the
predictor horizon and the correction smoothing re-tune to the new rate, the
60 Hz setup/start/first-race-frames pacing is untouched. Rates above the
announced ceiling, or any mid-session `RLT1` from a host that never sent
`RLT2`, are ignored.

Each client also measures its own downlink: `RelayUdpClient` keeps a
per-origin one-way delay meter (arrival minus sender stamp; the running minimum
is treated as the uncongested baseline and the excess as queueing delay) and a
sequence-number loss meter, closes a window every 900 ms and puts the worst
peer's p50 queueing delay (ms) and loss (%) into the next UDP hello
(`Header::total` / `Header::part`). The feedback is only enabled after `RLT2`,
so pre-adaptive hosts keep receiving all-zero hellos. The host zeroes the two
fields before echoing the ACK.

The ladder/threshold logic lives in `Src/Network/Online/AdaptiveTickRate.h`
and is covered socket-free by the `adaptive-tick` scenario in
`Tests/AsyncNetboardTest.cpp`; `Docs/OnlineLobbies.md` describes the host-side
evidence, the GUI slider for self-hosted rooms (default 20 Hz, 5-60) and the
relay flags `--no-adaptive-tick` / `--simulate-congestion`.

## Daytona USA 2 Power Edition boot time

With a net board attached, `dayto2pe` spends its NETWORK CHECKING screen (one
link exchange per emulated frame, a few hundred exchanges) drawing a course it
never shows: the boot program DMA-copies a canned Real3D database from CROM bank
5 (about 13 000 culling nodes, 5 000 models, 1 MB of polygons) and a viewport
that points at it to culling RAM `0x8E000000`, then idles while the opaque 2D
text layer hides the result. On a software renderer that is 2-3 FPS, so the
check takes minutes; `daytona2` submits an empty viewport in the same phase.

The fix is a session-only RAM code patch in the style of the Hornet and secret
cars options (`Src/Network/Games/Daytona2/BootScene.h`), applied by
`CModel3::RunMainBoardFrame` for `dayto2pe` whenever a net board is attached.
It changes one word of the database loader (RAM `0x13B34`, `lwz r31,0x1c(r1)`
-> `li r31,0x7f00`) so that the boot viewport is copied to the unused hole
`0x8E007F00` instead of `0x8E000000`; culling RAM at the real viewport slot
keeps its reset zeros, so the check renders an empty frame at full speed. The
patch verifies the words before and after the target plus the hook entry, its
caller and the `oris r3,r31,0x8e00` that builds the destination, and refuses
anything else; nothing is written to the ROM set, NVRAM, or the config file.
The loader's bookkeeping (pointer-list offsets at `0x737918/0x737928`) and
every other boot DMA are unchanged, and the game writes its own viewport from
RAM `0x660010` once the link check passes.

Measured on the 2-cabinet relay rig (llvmpipe): the viewport DMA lands at
`8E007F00`, the check runs at ~57 FPS, ~280 ring round trips in the first
20 s of the link (was ~25), link check about 15 s (was over 150 s), and the
attract mode screenshots match an unpatched run. `-no-boot-scene-patch`
(`NetBootScenePatch = 0` in the ini) keeps the original behaviour; the ROM-free
`boot-scene` scenario in `Tests/run_async_netboard.py` covers the patch logic.

## Important limitations

**Full local frame speed is not the same as correct linked gameplay.** The game
can see repeated/stale remote state while its own simulation advances. One-frame
events that occur between sampled exchanges can be missed. Depending on the
game, this can cause jumping opponents, inconsistent collisions, missed starts
or inputs, or a game-level link failure. Input-synchronized games may need
per-game protocol handling, prediction, or rollback rather than generic HLE
snapshot holding. Do not treat this as validated competitive netplay.

The receive slots initially contain zeroes until the first complete exchange;
the local echo is updated immediately. Setup itself still takes network round
trips, but no longer blocks local frame execution.

TCP retransmission/head-of-line blocking and the existing connection/shutdown
implementation are unchanged. The bounded exchange scheme limits backlog for
normal peers; it is not a new general-purpose hostile-peer queue limiter.

**Restart all linked emulator instances after a mid-session reset, disconnect,
mode mismatch, or timeout.** Reset is nonblocking, but this patch does not add a
session/epoch protocol or automatic reconnection capable of distinguishing old
queued packets from a fresh handshake. Network save-state recovery is also not
implemented (the upstream HLE save/load methods are empty).

## Build and test

Use the repository's normal platform build instructions. On Linux:

```sh
sudo apt install g++ make libsdl2-dev libsdl2-net-dev libglu1-mesa-dev
make -f Makefiles/Makefile.UNIX -j4
```

The ROM-free regression runner compiles the actual modified HLE board and TCP
transport. Python TCP proxies add controlled delay; the tests do not mock out
`Send()`, `Receive()`, or `TryReceive()`.

```sh
python3 Tests/run_async_netboard.py
python3 Tests/run_async_netboard.py --sanitize
```

The second command enables AddressSanitizer and UndefinedBehaviorSanitizer.
The runner currently targets a Unix-like environment with `g++`, `sdl2-config`,
and Python 3.8 or newer. It uses automatically selected localhost ports and
requires no elevated network privileges or ROMs.

Coverage includes:

- Legacy synchronized self-link as a regression check.
- Async self-link and two-/three-/four-cabinet rings.
- Type-1 and type-2 register/segment handling, including relay cabinets.
- Up to 150 ms one-way proxy delay (approximately 300 ms TCP ping RTT).
- Local counters and bank swaps advancing independently of delayed packets.
- Ring segment order, repeated snapshots, current local echo, and payload
  updates after a peer stops advancing.
- Bounded receive queues while local emulation outruns the network.
- Zero/oversized segment rejection, both mixed-mode master/slave arrangements,
  malformed ready packets, and connection closure.
- Watchdog expiration by advancing its stored timestamp, nonblocking reset, and
  safe reinitialization after an upper-bank reset.
- Adaptive tick-rate meters and ladder controller (`adaptive-tick`): queueing
  delay baseline, loss percentage with reordering, step-down/step-up timing,
  ceiling clipping.

These are synthetic board/protocol tests, not native-ROM races. Jitter/loss,
long-term stability and competitive correctness remain unvalidated.

## Early-entry-sync CPU/network revision

Daytona CPU prediction validates native ownership before changing only a
local receive copy; it does not alter authoritative packet forwarding.

For lobby-based play with a shared relay endpoint and automatic native
roles, see [OnlineLobbies.md](OnlineLobbies.md). Direct-link sessions still
require manual native roles and a matching async setting on every cabinet.

## Source organization

Game-specific hooks are under `Src/Network/Games/`; shared transport and
async board state remain in `Src/Model3/SimNetBoard.cpp`.

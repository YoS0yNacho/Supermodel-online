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
  `uncertaintyUs`) and stream changed local snapshots at a bounded ~60 Hz wall-
  clock pace (`MaxInFlight = 48`, `MaxUnackedFromPeer = 30`) without waiting
  for full-ring completion. Arriving peer hops are forwarded immediately and
  published directly to their corresponding CommRAM slot (`slot = incomingHops`)
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
  preserve the returning ring snapshot and break the per-frame receive drain at
  each returning self-echo boundary.
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

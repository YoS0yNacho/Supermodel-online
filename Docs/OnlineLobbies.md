# Online tab: relay lobbies in Supermodel

Start Supermodel **without command-line arguments** to open the existing
settings/game-selection GUI. The **Online** tab uses the relay for lobby control
and chat. Gameplay uses the relay by default, with an optional creator-hosted
TCP/UDP path.

## Saved servers

The Online tab has a **Saved servers** dropdown. The built-in servers always
appear first, in this order:

1. `onlinetest-useast.viewdns.net:1970`
2. `onlinetest.viewdns.net:1970`

Choose one, enter your player name, and click **Connect**. To add a server,
enter its hostname or IPv4 address in **Relay IP:port** and click **Save server**.
The port defaults to `1970` when omitted. Custom servers can be removed with
**Remove server**; the two built-in entries are retained. Disconnect before
changing servers. This is a selection list, not automatic failover.

Server additions, removals and dropdown selections are saved immediately in
`Config/Supermodel.ini` (or the platform's active config path). Connecting to a
manually entered custom server also remembers it. Up to 32 entries are kept,
including the defaults; equivalent case/port spellings are deduplicated.

The global `OnlineServers` setting stores the comma-separated ordered list;
`OnlineRelay` remains the selected server for compatibility with existing INIs.
A legacy custom `OnlineRelay` is added to the list on load, and the previous
selection is respected. New configurations select the first built-in server.
A command-line `-relay=...` override stays session-only unless you explicitly
save that endpoint or choose a saved entry in the GUI.

## Start the updated relay once

Use the bundled `relay.py` from the repository root. It is standalone and
requires Python 3.9+; no third-party Python modules are needed.

```
python3 relay.py --bind 0.0.0.0 --port 1970
```

On Windows, use `py -3` instead of `python3` (or `pypy3` on weak-CPU relay hosts
for lower relay CPU overhead). The server is named **relay.py**. It defaults to
TCP port `1970`; loopback binding is retained for safety, so use the explicit
`--bind 0.0.0.0` above (or a VPN interface address) for remote cabinets and
restrict access with a firewall. An older relay does **not** understand GUI
lobbies: update `relay.py` and every cabinet's executable together.
Allow inbound TCP 1970 on the relay computer; clients need only outbound
connections. No additional control port or web browser is used.

## Daytona USA (original Model 2 Revision A)

This room requires **`daytona.zip`**, the original Rev A Model 2 program.
It is not Daytona USA 2, Daytona USA 2 Power Edition, or an alternate Model 2
revision. Use a client and central `relay.py` that both advertise Daytona
support. Older relays remain usable for their existing titles; they cannot
create a Rev A Online room.

1. In **Online → Create Lobby**, choose **Daytona USA (Rev A)**. This is a
   central relay room for **2–8 cabinets**. Each cabinet needs the exact ROM.
2. The creator can set **Difficulty** (Easy, Normal, Hard, Hardest) and
   **Game mode** (Normal, Grand Prix, Endurance) under **Options**. Defaults
   are Normal / Normal. Seats are assigned from the booked roster when the
   creator presses **Start Link**: cabinet 1 is MASTER, the rest are SLAVE.
3. Wait for native Communication Board Checking to finish. Use the game's
   normal Coin and Start controls on each cabinet, and the wheel and pedal
   for selection. Online settings affect only this session; the client does
   not write the assigned roles/options to a cabinet's offline NVRAM.
4. If a cabinet disconnects, the room ends; return to Online and create a
   new one. Late joins, live master migration, manual car-number overrides,
   hosted-game routes and Model 3 gameplay modifiers do not apply to
   original Daytona. See [Room video rate](#room-video-rate-57524--60-hz-throttle-and-returning-to-online)
   for the shared refresh-rate setting.

**Native TCP remains authoritative.** The ordered ring still forwards the
full 3,585-byte Model 2 payloads and uses the game's own FF/FE link setup and
numbered data/FC frames. UDP, when all cabinets advertise it, carries a
separate `DYP1` remote-body presentation sample. It cannot replace the TCP
ring or change cars' input, collision, timing, rankings or cameras. Stale,
unmatched or unavailable samples fall back to the TCP draw predictor. Close
linked cars, transitions and unverified draw paths use native coordinates.
TCP and UDP use the same relay port number; TCP alone is sufficient to race.

- `-no-daytona-udp` on any cabinet, or relay `--tcp-only`: no Daytona pose
  UDP in that room; the TCP ring is unchanged.
- `-no-daytona-prediction`: draw native remote cars locally and opt that
  cabinet out of pose negotiation. The other cabinets can still use the
  TCP draw predictor.
- These switches are only for original Daytona Rev A Online. They do not
  affect offline/stock-direct-link Daytona or Model 3 Daytona USA 2.

## Sega Rally Championship Twin/DX (Model 2A Revision C)

Use the exact **`srallyc.zip`** (Twin/DX, Revision C) on all cabinets, and a
client and central `relay.py` that advertise `srallyc` support. This is the
original Sega Rally Championship, **not** Sega Rally 2 or a renamed Rev B,
Rev C conversion, or DX-only clone. The ROM loader checks the archive and
Online checks the assembled Rev C program before opening the board channels.
Older relays/clients continue to work with their existing games; they cannot
create or join this new room.

1. Go to **Online**, connect to the updated relay, and create a **Sega Rally
   Championship (Rev C)** room. Other cabinets select the room and **Join
   Lobby**. There are **2–4 driving seats**; join in the desired Link order.
2. The creator can set **Difficulty** (Easy, Normal, Hard, Hardest) and **Game
   mode** (Normal, Short, Long) in **Options**. Defaults are Normal / Normal;
   the selected values are fixed for all seats at Start Link. Other cabinet
   preferences such as country and controls stay local.
3. The creator clicks **Start Link**. Link 1 is native **CAR 1**, and Link 2–4
   are native **CAR 2–4**. Start with any offline link setting or no save at all;
   do not edit the Test menu. Roles and room settings apply to the in-memory
   EEPROM only. Relay play never writes the offline NVRAM file.
4. Let **COMMUNICATION BOARD CHECKING** finish on all cabinets, then use the
   native **Coin**, **Start**, car and course controls. **Start Link** launches
   the cabinets; it does not start the native race. If a cabinet drops, all
   survivors return to Online; use a new room to race again.

The Rev C board always uses the complete ordered TCP ring: both RLJ2 channels
agree on the reserved roster before the game boots its role, then FF/FE must
agree with it. The relay forwards native 3,585-byte payloads on
`1 → N → … → 2 → 1` without rearranging them. No creator hosting, adaptive tick
rate, Model 3 race overrides, or Indy compact board frames are used.
`NOTLINK` and the hardware `RELAY` role are not driving seats. The room's
emulated video rate defaults to 57.524 Hz.

**Remote car motion.** The exact Rev C drawing code can smooth the horizontal
position and yaw of a verified remote car. It keeps the native position when
cars are close or when the scene, row identity or continuity is uncertain.
It does not change the local car, vertical position, collision, race progress,
setup, clocks, or saved data. `-no-srallyc-prediction` (or
`SrallycPrediction = false`) makes this cabinet draw native positions.

With the updated relay and every seat opted in, a separate **SRP1** UDP lane
can supply a newer position. The receiver requires a live, recent pose for
that seat/scene and an exact match to the current TCP row in the sender's
bounded history. Late, unrelated, or lost UDP data fades back to TCP-based
presentation; the full native board traffic continues on TCP the entire time.
A previous Rev C build, `-no-srallyc-udp` (`SrallycPoseUdp = false`) on any
seat, an older Rev C relay, or relay `--tcp-only` leaves the whole room on TCP.
This is comparable presentation behavior to Indy, not protocol interchange,
TCP bandwidth reduction, or a guarantee of equal WAN latency.

ROM-free checks: `python3 Tests/run_srallyc_online.py`,
`python3 Tests/run_srallyc_pose.py`,
`g++ -std=c++17 -ISrc Tests/SrallycMotionTest.cpp -o /tmp/srallyc-motion-test && /tmp/srallyc-motion-test`,
`python3 Tests/run_online_client.py --pre-srallyc-relay /path/to/previous/relay.py`,
and `g++ -std=c++17 -ISrc Tests/SrallycRelayEepromTest.cpp -o /tmp/srallyc-eeprom-test && /tmp/srallyc-eeprom-test`.

## INDY 500 Twin (Model 2 native link)

Use the exact **INDY 500 Twin (Revision A, Newer)** `indy500.zip` on every
cabinet. This lab accepts **2–8 linked driving seats** (up to four Twin sets).
Use matching **pre-entry sync** clients on all Indy seats and the eight-seat
`relay.py`. Do not mix them with the older native-only or GO-sync Indy clients.
The native Indy frame size and cabinet timing are unchanged.

1. In **Online**, connect to a private central relay and create an **INDY 500
   Twin** lobby. Join in cabinet-ID order. The creator is Link 1. A ninth seat
   cannot join, and no one can join after **Start Link**.
2. Before **Start Link**, open **Options** to set **Difficulty** (Very Easy
   through Hardest), **Race mode** (Normal or Long), and **Handicap** (On/Off).
   There is no enable switch. The host edits; guests can see the choices.
   Updated rooms start with Normal / Normal / On. If an unmodified room
   includes a client without Indy room-options support, the game uses each
   cabinet's settings. Nondefault choices require every client to support
   Indy room options.
3. The creator clicks **Start Link**. Current clients automatically apply
   native cabinet IDs from the relay: Link 1 is **MASTER** and Link 2 through N
   are **SLAVE**. Standalone saves and fresh installs work without editing the
   Test menu. This changes only the session's in-memory settings; Online does
   not write NVRAM. Keep other native race settings matched. Older clients
   without automatic assignment still require matching IDs and link modes in
   **Test > Game Assignments**, even if the relay reports both slots ready.
4. Wait for the ROM to finish **Checking Network**, then use the normal coin,
   Start, course and transmission controls. Select the same course. Start Link
   opens and links the cabinets; it does not start the race by itself.

Indy's **native link is central TCP**. It does not use Model 3 AsyncNet,
fan-out of the native ring, adaptive tick rate, self-hosting, Model 3 race
modifiers. The relay needs inbound TCP and, optionally, UDP on that same port
for the race-motion lane below. Keep it on a private network or VPN.
The room's emulated video rate defaults to 57.524 Hz.

The relay routes the native `FF`/`FE` handshake and each ordered data + `FC`
pair over TCP. The native payloads are 3,585 bytes after decoding. In a room of N seats the wire goes `1 → N → … → 2 → 1`: `FE`
assigns the highest ID to the first Slave, so this yields the visible lobby
IDs. For 3–8 seats the master seeds at least one complete data/FC pair per
seat. If the native `FF` handshake takes longer to make its round trip, the
master can seed additional pairs while awaiting the first complete return.
It sends at most one per guest vblank and stops at the first usable return
or at the measured limit (no more than 32 pairs). Then each cabinet consumes
one ordered predecessor pair per guest vblank before sending the next. The
250 ms wait for a missing pair and the local receive-dwell limit still apply;
no pairs are skipped or replayed. Long or stalled rings can still time out.
This is not a claim of low end-to-end latency.

**Race-entry sync is automatic for linked native racers.** The stock course
and transmission selections end normally. Indy waits on its native flag /
**“Ladies and gentlemen, start your engines”** screen, just before the track
intro. The CPUs, vblanks, and native board traffic continue while the screen
holds. The flag overlay cannot expire during that hold; its countdown resumes
at release. This does **not** hold the rolling-start **GO**, set player inputs,
or change the race timer. The original GO lab switch
is obsolete. Offline and direct-link play do not use this gate.

The barrier takes the actual race participants from the ROM. Cabinets outside
that race still forward the control token but are not required to coin in or
start; no synchronized deadline is needed for a native solo race. Like the
Model 3 games, the start leader takes a complete ring-time measurement and
uses `RaceStart::ReleaseLead` (at least 500 ms; three measured rounds plus
300 ms, with bounded retries). Each cabinet converts the deadline with its
incoming peer's monotonic clock offset. Prepare and commit must close in time;
a lost link or failed deadline does not fall back to an unsynchronized release.
Release is consumed at the next eligible guest frame, not a scanout lock.

Two-seat Online uses the earlier lab's two-pair priming and 160 ms local receive
age bound by default. The two-seat lab playout overrides remain available; the
3–8-seat flow-controlled window and 250 ms bounds are separate.

**Remote motion (INDY 500 Twin Rev A, Newer).** During normal driving in an
Online race, the client uses the native car-to-cabinet mapping and the position
and update counter already in the link stream. It predicts the view of the
other players and the AI cars their cabinets own, not the local car or its AI.
The client filters velocity, extrapolates horizontal position and wrapped yaw,
and blends small corrections more gently than large ones. Its lead is a
bounded hint from receive dwell, hop RTT and native forwarding, not an exact
measure of total latency. It advances no more than eight native frames into a
short gap, with separate translation/heading caps. Old samples or unusual
motion fall back to the native pose. Close to your car, the display blends
back to the native pose used for collisions.

Only operands of the verified car drawing path are changed. This optional drawing step does not alter native state, link timing,
or race control. No prediction runs at the flag/intro, in attract, or offline/direct.
`-no-indy-prediction` (or `IndyPrediction = false`) disables it per cabinet;
`-net-smoothing=0|1|2` selects fixed, adaptive (default), or extra blend. The
setting affects what that cabinet draws, so matching *pre-entry protocol*
clients may use different levels or leave prediction disabled. No new server
or lobby setting is required for TCP prediction.

**UDP race motion (Rev A Online).** In a fully updated room, once all
linked cabinets are racing and mutually receiving fresh samples, positions
and angles travel by latest-wins UDP fan-out (at most 261 bytes per datagram
before IP/UDP headers). Validated samples are used in the native receive rows,
even with `-no-indy-prediction`. Every native data + `FC` tick still completes
in TCP order. Compact control pairs keep the other bytes reliable, including
car state, phase, laps and finish, with full movement anchors on state
changes and at regular intervals. The ring forwards these anchors intact;
they do not rewind a remote car that already has a newer, verified position.
Fresh motion can advance across a short gap between ordered pairs. Genuine
state changes and resets still take effect in order. Startup, entry and
special car states use full native motion on TCP.

A lost UDP sample does not block playout. Stale or unusable samples cause
automatic full TCP fallback; the race can return to UDP after the lane is
healthy again. The ring must still be connected. Compatible older clients
keep full TCP movement in mixed rooms and may use the older pose feed.
`-no-indy-udp` (or the existing `IndyPoseUdp = false` setting) opts a client
out; `--tcp-only` on the relay disables UDP for every room.

This is not WAN-validated. For a first race, leave `--latency-ms` and
`--jitter-ms` off: these are artificial test impairments, not speed settings.
If the ROM stays on **Checking Network**, check that every cabinet uses the
same supported ROM and a current client. The client log should report an
assigned native ID and then `link established`; the relay slot message alone
only confirms TCP admission. A disconnected cabinet ends the room for
everyone; create a new room instead of reusing the launch tickets.

Protocol regressions: `python3 Tests/run_indy500_online.py`,
`python3 Tests/run_indy_room_options.py`, and `python3 Tests/run_indy_race_udp.py`.

## Configurable tick rate (`--tick-rate`)

For Model 3, the default requested tick rate is `60` Hz; relay sessions run at
`57.524` (or `60` with the room's 60 Hz option) emulated frames/s, so transmission is capped at one originating
snapshot per emulated frame. On weak relay
devices, metered connections, or larger multi-cabinet lobbies, the relay can
advertise a lower update tick rate (`5`–`60` Hz) to every connected emulator:

```
pypy3 relay.py --bind 0.0.0.0 --port 1970 --tick-rate 20
```

- `--tick-rate HZ` sets the network update rate (`5`–`60`, default `60`).
- When `--tick-rate` is below `60`, the relay sends an `RLT1` control frame on
  each game channel right before `RLA1`. Default `60` Hz omits `RLT1` so wire
  handshakes remain unchanged.
- Connected emulators pace originating async ring snapshots (`hops == 0`) to
  `HZ` using a refresh-aware milliHz phase accumulator plus wall-clock spacing
  instead of sending every emulated frame, while still forwarding peer hops and
  race-start sync tokens immediately without added delay.
- Receive-side motion predictors (`daytona2`, `dayto2pe`, `scud`, `scudplus`,
  `srally2`, `lemans24`, `dirtdvls`) automatically adapt their horizon, trajectory
  smoothing, and 2D/3D turn-rate extrapolation to the configured tick rate so
  remote cars continue moving smoothly every emulated frame (57.524 FPS online, or 60 with the room option) even at low tick rates (e.g.
  `10`–`30` Hz).

## Adaptive tick rate (congestion control)

The configured tick rate is a **ceiling**, not a fixed rate. Whenever every
member of a room advertised the capability (lobby HELLO bit 8, every build
since this change), the room host lowers the rate while the link or the host
is congested and restores it once the link has been clean again:

- **Self-hosted rooms**: the Create Lobby dialog gets a *Tick rate (Hz)* slider
  (5-60, default **20**) when *Self-host room + TCP/UDP* is selected. The
  creator's own host (`Src/Network/Online/PlayerHost.h`) applies the ceiling
  and adapts below it; the central relay's `--tick-rate` does not apply to
  these rooms.
- **Central relay rooms**: `--tick-rate` is the ceiling (default 60).
  `--no-adaptive-tick` keeps it fixed.

How a host decides (`Src/Network/Online/AdaptiveTickRate.h`, mirrored in
`relay.py`), once per second:

- *Uplink* of every cabinet: one-way delay of its UDP snapshots measured as
  arrival time minus the sender's stamp. No clock sync is needed: the running
  minimum is the uncongested path and the excess over it is queueing (the
  LEDBAT idea; the minimum inflates 0.25 ms/s so drift cannot pin it).
  p50 queueing >= 40 ms, or >= 5 % loss from the sequence numbers, marks the
  second congested.
- *Downlink* of every cabinet: each client measures the same queueing delay /
  loss on what it receives and reports them in its twice-a-second UDP hello
  (`Header::total` ms, `Header::part` %), only after the host announced itself
  with `RLT2`, so older hosts never see the fields.
- *The host itself*: a ring connection that cannot drain for 300 ms, UDP sends
  refused by the kernel, or the host loop running a quarter second late.

The ladder is 60-45-30-24-20-15-12-10 Hz clipped to the ceiling, floor 10 Hz
(or the ceiling if lower). Two consecutive congested seconds step down one
rung, at most once every 3 s; 20 consecutive clean seconds step up one rung,
at most once every 20 s. Each change is one `RLT1` on the cabinet's first
channel; the client logs `update tick rate changed to N Hz by the host`, keeps
its race pacing state (no 60 Hz settling burst) and the predictors re-tune
their horizon and smoothing to the new rate, so remote cars keep moving every
frame throughout. Setup, the start barrier and the first 60 race frames still
run at 60 Hz regardless.

`--simulate-congestion FROM-TO` (relay.py, test/CI only) treats every adaptive
room as congested between FROM and TO seconds after Start Link. Measured on the
2-cabinet Scud Race rig with `--tick-rate 30 --simulate-congestion 40-52`: the
relay stepped 30 -> 24 -> 20 -> 15 Hz during the window and restored 20 -> 24 ->
30 Hz afterwards at 20 s intervals; both cabinets applied every change, the
link stayed up (1000+ ring round trips) and a race started on the UDP lane with
the feedback hellos. No spurious step occurred during the race on the loaded
test machine. Mixed versions: a room with one pre-adaptive client runs at the
fixed ceiling; an old creator-hosted UDP fan-out (legacy mode 1) that rejects
feedback hellos simply makes clients fall back to the central UDP path.

## UDP race traffic (default) and `--tcp-only`

By default the relay opens UDP on the same port number as TCP. Allow **both**
TCP and UDP on that port.

- **During a race** each cabinet sends its snapshot by UDP at the tick rate,
  and the relay fans it out to every other cabinet in one hop. A lost datagram
  is not resent; the next snapshot replaces it.
- **Positions by UDP, control messages by TCP.** Each cabinet classifies the
  fields of its own outgoing packet. A 32-bit field that keeps changing on more
  than one tick in seven (positions, speeds, frame counters) is
  *streamed* and travels by UDP only. A change in any other field (lap, rank,
  finish and handshake flags, race phase) is a *control message*: that packet
  is sent round the TCP ring, in order, exactly as before. No per-game table
  is needed; the split is learned from the traffic.
- **Ordering.** Every UDP snapshot names the last TCP control message it was
  built on. While that message is still in flight the receiver applies only
  the snapshot's *streamed* words and keeps the control words of the last
  reliable segment; when the TCP message lands its control words are merged
  in. A control state is therefore never skipped or seen out of order, UDP
  never delivers control data that TCP has not delivered first, and remote
  cars keep moving while a lap/rank/phase change crosses the ring. For this
  each cabinet appends its streamed-word mask (one bit per 32-bit word,
  27–102 bytes) to a snapshot whenever the classification changes and at
  least every 500 ms; until a receiver has a peer's mask it holds that peer's
  snapshots whole, as before.
- **TCP also carries** link setup, the race-start barrier and a 1 Hz keepalive
  ping. The log reports how many control messages were sent and which packet
  offsets caused them (`Relay UDP: N control messages sent by TCP so far`).
- **Automatic fallback.** A cabinet stops sending on TCP only while every other
  cabinet reports, inside its own UDP snapshots, that it is receiving that
  cabinet's UDP. The lane becomes UDP-only once every cabinet has been heard
  within 3.5 ticks or 150 ms (whichever is longer) for one second; once it
  is, a cabinet may stay quiet for 400 ms (or twice that window) before the
  TCP ring resumes, so a short stall or a run of lost snapshots costs a
  moment of staleness instead of a burst of keyframes and another second of
  settling. A real outage (firewall, NAT) still falls back within half a
  second, and UDP-only racing returns one second after confirmation is
  restored. The log line `Relay UDP: TCP ring resumed (...)` names the cause.
- **Sample age.** Every UDP acknowledgement carries the relay clock, so
  receivers measure how old each snapshot is and the predictors compensate.
  The age counts from the moment the game wrote the segment into CommRAM,
  not from the moment the net board picked it up a frame later: the board
  records the write burst for each CommRAM bank and sends its age and span
  with the snapshot (`sampleAgeUs`, `sampleUncertaintyUs`) and with every
  TCP timed segment, whose origin uncertainty therefore drops from a whole
  frame (~17 ms) to about a millisecond.
  Snapshots are sent by the emulation thread the moment a tick is due and
  read by a worker that blocks on the socket, so neither direction waits on
  a polling interval.
- **Delta snapshots.** A UDP snapshot is normally sent as the XOR of the
  current segment with the cabinet's last TCP segment (its keyframe), which
  every receiver already holds. Between control messages only the streamed
  words differ, so after zero-run encoding a 1–3 KB segment usually travels
  in well under 100 bytes and always fits one datagram. The trailer names the
  keyframe stamp; a delta that arrives before its keyframe is kept until the
  TCP ring delivers it (the same hold that already protects control order),
  and a delta built on an older keyframe than the receiver holds is discarded
  because a newer TCP segment has superseded it. The relay never decodes
  game bytes. The log line `Relay UDP: delta lane ...` reports the saving.
- **Sega Rally 2** depends on the snapshot returning round the TCP ring, so it
  keeps the ring at the tick rate during races and uses UDP only as predictor
  input; those inputs carry the relay-clock sample age like every other
  game's, so its predictor compensates latency the same way. Its ring segments
  are forwarded by the receive thread and fanned out by the relay like the
  other games' (the client guarantees the game still sees every segment of
  every slot, one per frame). Late TCP poses from the same race cannot rewind
  a newer accepted UDP pose during a short gap; near-simultaneous copies of
  the same native pose do not create a second motion sample. Live driving
  flags follow TCP without being treated as a different car. Only the remote
  receive-copy pose predictor is affected. The ordered ring, returning self
  echo, and genuine phase changes remain native.
- **`--tcp-only`** disables the UDP lane; all game traffic uses the TCP ring.
- **Star fan-out of the TCP ring.** For the timed-segment games the relay
  does not pass each segment from cabinet to cabinet:
  it delivers it to every other cabinet directly, with the hop count that
  cabinet would have seen on the ring, and returns the origin its echo at
  once. A control message or a `--tcp-only` segment therefore reaches the
  farthest cabinet after one relay delivery instead of up to N-1, and the
  origin's echo (which releases the next UDP delta base) after one round trip
  instead of N. The relay pings every cabinet once a second and translates
  the segment timestamps through its own clock, so sample ages keep their
  measured uncertainty. Fan-out starts once every cabinet has sent its first
  segment, i.e. has finished the ring handshake. The relay announces it with
  an `RLS1` frame before `RLA1`; it is used only when every member's GUI
  advertised the capability (lobby hello flag 64), so a room with an older
  client and a creator-hosted room keep the hop-by-hop ring.
  Cabinets never forward segments in a fan-out room. The ROM-free test
  reports both: `ring-latency` (4 cabinets, 40 ms per delivery) measures a
  166 ms ring round trip and 168 ms worst staleness, `star-latency` 45 ms and
  88 ms.

```
python3 relay.py --bind 0.0.0.0 --port 1970 --tick-rate 20             # UDP racing (default)
python3 relay.py --bind 0.0.0.0 --port 1970 --tick-rate 20 --tcp-only  # TCP ring only
```

The UDP wire format is version 5. Update `relay.py` and every cabinet together;
an older build on either side is ignored on UDP and the room runs on TCP.

ROM-free test: `python3 Tests/run_udp_primary.py [--sanitize] [--tc] [--only NAME]`.

The log line `Relay UDP: inbound N snapshots, L lost (x%), R reordered, D
duplicated; lane fell back to TCP F times` reports what the UDP lane actually
experienced, from the per-peer sequence numbers.

### Testing under impaired networks

`Tests/netem.py` holds impairment profiles (`wifi`, `congested`, `mobile`:
one-way delay, jitter, random and bursty Gilbert-Elliott loss, duplicates) and
the `netem` scenarios of `run_udp_primary.py` race under each of them. By
default the rig runs in-process and deterministically: UDP snapshots are
dropped, duplicated, delayed and reordered on their way out of the relay, and
the relay's own fake latency/jitter delays the TCP ring in order. Each run
asserts that the lane settles into UDP-only racing and stays there, that
control values arrive in order, that staleness stays within the profile's
bound, and that the cabinets' loss count agrees with what the rig dropped.
`--tc` installs a kernel `netem` qdisc on loopback instead (Linux,
CAP_NET_ADMIN, `sch_netem`), so TCP and UDP both cross a real impaired path;
when netem is unavailable the runner says so and uses the in-process rig.

### Real-ROM sessions on one machine (testing only)

`-lobby-ticket=<32 hex>` together with `-relay=HOST:PORT` and a ROM argument
joins an already-started lobby room **without the GUI**, exactly as the Online
tab does after it receives its launch message. The ticket comes from the
relay's lobby: `Tests/lobby_tickets.py --relay HOST:1970 --game srally2
--players 2` creates a room as the GUI would (same HELLO capabilities, so the
relay chooses star fan-out as it would for real GUIs; `--no-star` keeps the
ring), joins the members, presses Start Link and prints one ticket per
cabinet. `Tests/rom_session.sh GAME N` wires this up end to end on Linux:
relay, tickets and `N` cabinets on separate Xvfb displays (Mesa llvmpipe, no
GPU), `Tests/rom_key.sh DISPLAY KEY` injects keys per cabinet. Cabinets log
`Async net board: link stats: ...` every 10 seconds: transport (ring or relay
star fan-out), ring round trips and mean RTT, segments forwarded by the
receive thread, predictor inputs taken from UDP (and of those, timed) and UDP
snapshots applied. Tickets are single-session and never written to the INI.

Measured with the real `srally2.zip` and `scud.zip` archives, 2 cabinets and
the relay on one 2-core sandbox (software GL, emulation ran at roughly a
third of real time, so absolute RTTs are CPU-bound and pessimistic):

| session | transport | result |
| --- | --- | --- |
| srally2, star fan-out | relay fans segments out, 0 forwarded by clients | LINK GAME OK on both; coin -> course -> car -> name entry -> `DESERT START!!` and the 60 s clock started on the same frame on both cabinets; race to TIME IS UP with consistent 1st/2nd on both; back to attract; a **second race in the same session** passed the same barriers. 15 000 ring round trips, 5 040 predictor inputs from UDP, **100 % timed**, no reset/flush, no fallback. |
| srally2, ring (`--no-star`) | 5 253 of 5 554 segments forwarded by the receive thread | same flow, race clocks within 0.1 s of each other, 1 245 predictor inputs from UDP, 100 % timed. |
| scud, star fan-out | UDP-primary lane | course/car select, GO!, 4-lap race in sync; `positions now UDP only`; 5 935/5 936 snapshots as deltas, 90 % bytes saved, 0 lost, 0 stale, 0 waited for keyframe; one TCP resume at the race-state change, none during the race. |

## Fake latency (testing only)

The relay can add artificial delay to the linked game traffic, which is useful
for checking prediction, net-board behaviour and start synchronization without
needing cabinets in different cities:

```
python3 relay.py --bind 0.0.0.0 --port 1970 --latency-ms 120
python3 relay.py --bind 0.0.0.0 --port 1970 --latency-ms 120 --jitter-ms 25
```

- `--latency-ms N` adds **N ms one-way, per direction**. Because both cabinets
  pay it, the round-trip time the game measures is about `2N`. `0` (default) is
  off; the range is 0-5000.
- `--jitter-ms J` makes each frame's delay `N +/- J` ms (uniform, never below 0).
  `0` (default) means a constant delay; the range is 0-5000.
- Only 848/1024/2048/3264/3584-byte game board frames, `PIN1`/`PON1` pings are delayed, and only
  after Start Link. Lobby creation, joining and the room list are never delayed,
  and the relay still validates everything it forwards.
- Each frame is delayed from its arrival time rather than by pausing the read
  loop, so pipelined frames keep their original spacing instead of being
  serialized, and no traffic is dropped or throttled. Per-direction backlog is
  bounded (4096 frames): overflow fails the room instead of buffering forever.
- The delays are per relay, not per lobby, and are announced on startup
  (`Fake latency enabled: ...`). Restarting the relay with different values is
  the only way to change them.

This changes timing only. It does not simulate packet loss, reordering beyond
the configured jitter, bandwidth limits or NAT. Because it inflates measured RTT,
prediction and early-entry synchronization behave as they would on a distant
link; that is the intent of the setting.

## Create and join

1. In **Core**, set **ROMDirectory** to your local ROM folder.
2. In **Online**, enter the relay in **Relay IP:port** (defaults to
   `onlinetest.viewdns.net:1970`; port `1970` is assumed if omitted) plus your
   **Player name**. Both values are saved to `Supermodel.ini` (`OnlineRelay` and
   `OnlinePlayer`) when you connect, launch a game, or save settings on exit.
3. Click **Connect**. Model 3 gameplay uses AsyncNet; the Model 2 games use
   their native ring instead. Neither rewrites your offline or per-game
   AsyncNet setting in the INI file.
4. One player selects a supported ROM, enters a lobby name and clicks
   **Create Lobby**. The creator is the master, car 1.
5. Other players select the lobby in the live list and click **Join Lobby**.
   They must have the selected exact ROM locally. Games are never downloaded
   or transferred by the relay. The GUI checks filenames/required parent;
   the normal ROM loader still performs actual contents/CRC validation.
6. The roster displays MASTER/SLAVE and car numbers. With **2–16 Daytona players** (Scud: 2–8, or 2–16 in the optional Scud Race Plus 16-player mode, Sega Rally 2: 2–4 or 2–8 in the optional 8-player mode, Le Mans 24: 2–6, Dirt Devils: 2–4), only
   the creator can click **Start Link**. Until then everybody stays in the lobby.
7. **Start Link automatically loads the chosen game on every joined cabinet.**
   The relay waits for both native channels from every member, then starts the
   link. No operator `start` command is needed. Normal coin/start controls and
   the existing early-entry synchronization still govern entering a race.

The creator stays master even if a slave loads its ROM or connects its game
channels first. Car order is reserved from the lobby roster, not socket timing.
The supported selection is exactly:

| GUI selection | Local file |
|---|---|
| Daytona USA 2 | `daytona2.zip` (up to 16 cabinets) |
| Daytona USA 2 Power Edition | `dayto2pe.zip` (up to 16 cabinets) |
| Scud Race | `scud.zip` |
| Scud Race Plus | `scudplus.zip` plus `scud.zip` in the same folder |
| Sega Rally 2 | `srally2.zip` (up to 4 cabinets) |
| Sega Rally 2 - 8 Player | `srally2.zip` (up to 8 cabinets; select the optional mode) |
| Le Mans 24 | `lemans24.zip` (up to 6 cabinets) |
| Dirt Devils | `dirtdvls.zip` (up to 4 cabinets) |
| Scud Race Plus - 16 Player | `scudplus.zip` plus `scud.zip` (up to 16 cabinets; experimental ROM patch, see below) |
| INDY 500 Twin | `indy500.zip` (2–8 cabinets; Model 2 native TCP) |
| Sega Rally Championship (Rev C) | `srallyc.zip` (2–4 cabinets; Model 2A native TCP) |

### Scud Race Plus - 16 Player (experimental)

The ninth lobby entry runs the exact Export Rev A `scudplus` ROM with an
in-memory PowerPC patch (`Src/Network/Games/Scud/SixteenPlayerResearch.h`,
generated by `romtest/sp16/gen_patch.py`). Nothing on disk is modified: every
patch site is checked against its expected opcode and the fixed-CROM CRC32
before boot, and a mismatching ROM refuses to start. The patch lifts the link
caps from 8 to 16 cabinets, widens the car import/autopilot loops and the
course-vote mask, and sends the 16-entry roster in the otherwise unused
`+0x300` region of every 1 KB CommRAM slot. The lobby option is session-only
(`NetScudplusSixteen`, relay GUID `0x5bf177da3487d`, ROM alias `scudplus_16`);
stock 8-player Scud Race Plus rooms are unaffected. Native room settings, secret
cars, car numbers 1–16 and the 8→16 player relay/UDP paths are the same code
paths Daytona USA 2 already uses for 16 cabinets. It has been validated with a
synthetic 16-node ring in the emulator and with nine real cabinets through the
relay (car numbers 1–9, course select, race); treat larger real rooms as a
preview. Headless rigs reproduce a GUI launch with `-lobby-ticket=<hex>
-scudplus-16 -lobby-options=<mask> -lobby-car=<n>`; the car number matters,
because the room assigns CAR NUMBERS 1–16 and a cabinet that rejects its number
never reports native-ready, which holds every cabinet at the link screen.

## Room ping

Inside a lobby every member row shows that player's round trip to the room
host (the central relay, or the creator for creator-hosted rooms), green under
60 ms, yellow under 120 ms, red beyond; `your ping` is shown next to
**Create Lobby** in the lounge and in the room header. The number is what
matters for play: all game traffic goes through that host, so two players'
pings added together is roughly their mutual delay. It is measured by the
GUI's own lobby poll: every second the client sends `OLQ2` (a clock stamp
plus the last RTT it measured), the host echoes the stamp (`OLP2`) before the
snapshot, and the snapshot lists every member's reported RTT (`ORP1`). Clients
advertise the feature with HELLO capability bit 128; older clients keep
polling with `OLQ1` and see no pings. Nothing is sent to other players'
addresses and no extra sockets are opened.

## Lobby chat and in-game chat

- **Lounge chat (before joining a lobby):** When connected to the relay and
  browsing lobbies, the right-hand **Lounge Chat** pane lets connected players
  coordinate which game or lobby to host.
- **Lobby chat (inside a waiting lobby):** Once you create or join a lobby, the
  chat pane switches to **Lobby Chat** for that specific room, tagging messages
  with `[Car N]` and player name. Late joiners receive recent lobby messages,
  and all lobby messages carry seamlessly into **In-Game Chat** when **Start
  Link** launches the game.
- **In-game chat controls (during loading, race, and post-race):**
  - **`Enter`** (without `Alt`): Open the chat input box (or send the typed
    message and return to gameplay). While typing, keyboard game/UI hotkeys are
    suppressed so typing never inserts coins, shifts gears, pauses, or exits,
    while steering wheels/controllers and emulation continue uninterrupted.
  - **`Esc`** (while typing): Cancel and close the chat input box without
    exiting the emulator.
  - **`Tab`** (when not typing): Peek recent chat messages on the HUD for 8
    seconds.
  - **`F8`** (when not typing): Mute/unmute automatic chat popups on the HUD.
- In-game chat runs over a ticket-authenticated room chat channel (`RLC1`) on
  the **same relay TCP port**, completely separate from the two `RLJ2` net-board
  channels, so `--latency-ms` fake game latency never delays chat and chat
  messages do not block the game-packet path.

Unsupported clones do not appear. Missing games are disabled in the selector;
joining a lobby for an unavailable game is disabled and also checked by the
relay. The general/offline game list above the tabs still shows all games.
Disconnect Online before using the ordinary **Load game** button.

## Leaving, errors and multiple lobbies

- **Leave lobby** keeps your relay connection; **Disconnect** leaves the server.
- If the creator leaves a waiting lobby, that lobby closes; there is no master
  migration. Other players may create/join another lobby.
- One relay supports up to **16 separate GUI lobbies**, with game-specific capacity (Daytona editions: 16, Scud editions: 8 (Scud Race Plus 16-player mode: 16), Sega Rally 2: 4 (8-player mode: 8), Le Mans 24: 6, Dirt Devils: 4, INDY 500: 8).
  A client can join only one room at a time. Each room selects one exact game.
- Once Start Link is pressed, the roster is locked: no late joins or live
  reconnect. If any game fails to load/connect within **90 seconds**, the room
  fails instead of starting a partial ring. Affected online players automatically return to the Online launcher;
  create or join a fresh lobby. Failed/ended rooms are removed after a short grace period.
- Each GUI has one control connection while browsing/waiting. During handoff it
  closes that connection and opens the normal **two game channels per cabinet**.
  This is expected and does not create duplicate players.
- Lobby tickets and relay assignments are session-only, never saved in the INI.
  Tickets are redacted in runtime logs. Existing no-NVRAM-save relay behavior
  and the corrected Scud MASTER/car 1 encoding are preserved.

## Launch Online from a console command

```
supermodel -relay=HOST:1970 -async-netboard
```

This **opens the Online GUI and connects automatically**. No ROM argument is
required. With a ROM argument, for example
`supermodel scudplus.zip -relay=HOST:1970 -async-netboard`, the GUI also selects
Scud Plus and uses that archive's folder. It does not boot the game until the
lobby master presses Start Link.

An already-enabled effective AsyncNet setting in the INI also permits automatic
connection. If the effective AsyncNet setting is false (including
`-sync-netboard`), the GUI still opens the Online tab with the endpoint filled
in, and you connect by pressing **Connect**. Model 3 Online sessions use
AsyncNet; Model 2 Online uses native timing. There is no per-session checkbox
and the emulator never rewrites your INI setting. A failed/cancelled automatic attempt is not retried in a
loop.

A configured NetRelay endpoint likewise opens Online. CLI overrides still win
over global/game INI settings. Invalid endpoints are rejected clearly; `-help`
and other utility commands do not open the GUI. Disconnect and use Load game
for an offline session: the initiating relay endpoint is not carried into that
session. Relay endpoints and authorization tickets are not written from CLI
into the INI; the selected ROM folder can be saved with the normal GUI settings.

The relay **does not read console commands**. There is no `start`, `status`,
`reset`, `quit` or command-file control. Stop it with **Ctrl+C**. Old direct
manual-room clients are rejected promptly instead of waiting forever. Update
all clients and `relay.py`. Only GUI-authorized lobby registrations
can start a link. The internal room start/reset methods remain for transport
implementation/tests; they are not console commands.

## Implementation and safety limits

GUI widgets are in `Src/OSD/SDL/OnlineGui.*` and `Src/OSD/SDL/InGameChat.*`.
Generic lobby and chat transport/state live in `Src/Network/Online/`;
game-specific native fixes remain in `Src/Network/Games/Daytona2/`,
`Src/Network/Games/Scud/` and `Src/Network/Games/Srally2/`. The GUI supplies
only session runtime overrides and the selected ROM path. It does not modify
guest RAM, ROM files, native packet layouts, prediction or race synchronization.

The control protocol is framed/bounded, versioned and little-endian; `OLH1`
registers a GUI, `OLC1` creates, `OLJ1` joins, `OLT1` requests master start,
`OLL1` leaves, `OLQ1` refreshes, `OLS1`/`OLM1` carry bounded UTF-8 text chat,
and `RLC1`/`RLC2` attach the in-game room chat overlay using the session ticket.
`OLB1/OLR1/OLZ1` form an atomic room-list
snapshot; `OLG1` supplies a one-session 128-bit random ticket and reserved slot.
`RLJ2` adds the ticket to the existing game registration; unauthorized legacy `RLJ1` is
rejected by the running server (the room primitive remains testable). The server validates membership/creator authority, exact game,
channel count, size and room state, then forwards existing game bytes unchanged.

This is **not a public matchmaking/authentication service**. There is no TLS,
account login, NAT traversal, internet server discovery, voice chat or ROM
sharing. Tickets prevent accidental/cross-room registration but are visible on
an unencrypted connection. Use only a trusted VPN/private network and firewall.

ROM-free regression commands included with this port:

```
python3 Tests/run_async_netboard.py
python3 Tests/run_online_lobby.py
python3 Tests/run_online_client.py
python3 Tests/run_online_session_policy.py
python3 Tests/run_model2_room_video.py
```

The runners exercise the HLE board, online C++ client, lobby protocol and session
policy without game ROMs. `run_online_client.py` also drives a complete
self-hosted room through the real relay lobby: reachability probe, creator and
joiner handoff to the creator's `PlayerHost`, hosted lobby chat, Start Link and
the creator-hosted two-channel game link. They are not a substitute for an actual-ROM or Windows
GUI smoke test.


## Room video rate (57.524 / 60 Hz), throttle and returning to Online

The room master sets the **emulated video rate** in **Options → Emulated video
(all cabinets) → Refresh rate** before starting the link. This applies to
Model 3 rooms and the three central-relay Model 2 games (Indy 500, Sega Rally
Championship Rev C, and Daytona USA Rev A).

- **57.524 Hz (original hardware)** — the default.
- **60 Hz** — runs the emulated frame clock about 4.3% faster. Not just a
  monitor setting, and not the **Tick rate (Hz)** network-update slider.

The relay checks that every member supports 60 Hz before accepting it, rejects
joins by incompatible clients while it is set, and hides the option bit from
those clients' room listings. Old clients and old relays can still use the
native rate for titles they already support. On an old relay the updated
client shows the Model 2 selector disabled. The room can be set back to the
native rate before launch to let older clients join.

Launch freezes the rate and sends the same full options mask (with bit 1024,
`NativeRoomSettings::Video60`, for 60 Hz) to each cabinet. Model 3 retains its
existing capability bit 256 and creator-hosted `PlayerRoom` support. Model 2
uses a separate `HEL1` exchange (`Supermodel Online M2VIDEO1 N` / `Supermodel
Online lobby v1 M2VIDEO1 N`); `N` is the existing-title availability mask in
one ASCII digit (Indy=1, Rev C=2, Daytona=4, additive). The OEH6 flags/body and
gameplay protocol are unchanged. This prevents an older client that advertises
60 Hz for Model 3 from joining a 60 Hz Model 2 session.

Software frame limiting at that rate is mandatory and its in-game toggle is
disabled online. **Display VSync is no longer forced off**: your own `VSync`
setting is kept. With VSync on, the throttle still paces emulation at
57.524/60 Hz and the buffer swap merely waits for the next display refresh;
choose 60 Hz in the room if everyone has a 60 Hz display and wants the swap
and emulation aligned. This is an emulation rate, not a guarantee the monitor
supports a matching video mode. A cabinet still needs enough rendering and
emulation headroom to sustain the selected rate. Offline settings are
unchanged and these overrides are not saved. Model 3 relay tick rates remain real-time Hz (capped
by one send per emulated frame). Model 2 still runs its native ordered TCP
ring, with one board tick per emulated frame. Model 2 pose cadence and
frame-based age conversions follow the selected rate. Match the cabinet
hardware by leaving the room on the default if you want original game speed.

Model 3 headless rigs: `Tests/lobby_tickets.py --options 1024` sets the option
before Start Link. Each cabinet is launched with `-lobby-options=1024` next to its
`-lobby-ticket=`. Verified on the 2-cabinet Scud Race rig with `-vsync`:
`RefreshRate=60`, `Throttle=1`, `VSync=1` in both logs, race started and ran
on the UDP lane.

If either board channel disconnects, the relay reports failure, or the HLE
networking detects an unrecoverable link/start-sync error, the cabinet exits
the game cleanly and reopens **Online**, reconnecting to the same relay.
The old session is closed, not resumed: fresh lobbies get fresh tickets.
This is also checked while the local emulator is paused. Ordinary Quit/window
close still exits the application.

The master (Car 1) can press **Enter** to open the in-game chat panel, click
**Return everyone to lobby**, then confirm **Return to lobby**. Ending the
master's board channels invalidates the old ring and returns all other linked
players to Online too. Slaves do not get this host control. Chat-only connection
trouble does not count as a broken game link.


## In-race player positions

The top-left Online positions panel lists online humans in each game's native
race order, with permanent cabinet/name labels (`3P: name`, `1P: name`, etc.).
It covers all seven lobby selections across six ROM families and is independent of chat/F8 visibility. Update `relay.py` and all emulator clients together. See
[OnlineRaceStandings.md](OnlineRaceStandings.md) for native-rank addresses,
phase/stale-data behavior, transport limits and verification scope.


## Scud Race Plus secret vehicles on every course

With the room's **Secret Cars** option (room option bit 1, session-only RAM
patch `Src/Network/Games/Scud/SecretCars.h`), the bus, tank, cat and rocket
can be chosen on any course. The game itself only ever shows them on Super
Beginner, and that is where their textures live: Scud Race Plus keeps one
2048x2048 texture RAM layout per course (texture lists in banked CROM bank 0 at
`0x38040`, `0x38194`, `0x382E8`, `0x3843C`, `0x38590` = Super Beginner,
28 LZSS-compressed 256x256 blocks each) and the other four lists load water,
track and building texels into the exact rectangles the vehicle models point
at. Unpatched, the vehicles therefore raced in those texels (wooden-barrel
rocket, turtle-skinned bus, and so on) everywhere except Super Beginner.

The fix (`Src/Network/Games/Scud/HiddenCarTextures.cpp`) does not touch
texture RAM, because every free-looking cell of it is used by something later
in a race, by link-play markers or by the car-select screen. Instead, when the
Secret Cars patch is applied, Supermodel decodes the Super Beginner list
straight from the CROM (same LZSS as the game's loader at `0x331AC`, same
8x8-tile layout, mipmaps included), copies the 52 texture rectangles the 573
vehicle models use into a private **texture overlay page**
(`Src/Graphics/TextureOverlay.h`, one 2048x1024 Real3D page with its mipmap
regions) and hands it to the renderer through `CReal3D::SetTextureOverlay`
(attached on the render thread at the next `BeginFrame`). Both renderers
redirect the textured polygons of exactly those VROM models to the overlay:

- **New3D** adds a third `TextureBank` (texture unit 2, uniform
  `textureBaseBank`); the mesh keeps its page so micro-textures still come from
  the hardware pages.
- **Legacy3D** allocates one extra texture sheet (format bit 3 in the
  `CTextureRefs` key selects it); if the GPU cannot provide a ninth 2048x2048
  sheet the overlay is ignored and the vehicles look as before.

The model list comes from the game's own vehicle scene-graph templates in
CROM bank 0 (culling-node images the program copies to culling RAM): every
VROM model of those templates that samples a Super Beginner-only rectangle is
redirected, which covers all 24 running-cat animation frames and the cat's
face, the rocket and the tank at every level of detail (including the far
LODs other cabinets see), the rocket rider's animation and the bus. Rectangles
the vehicles share with the per-course common lists, and shared assets that
every course loads its own copy of (the window reflection map, seats, the
grille) stay in texture RAM so the vehicles keep the current course's
versions. On Super Beginner the overlay is texel-identical to what the game
loads, so nothing changes there. The
emulated program, save states, NVRAM and the ROM set are untouched; the ROM is
verified (list position, first header `00F6F040`, block headers, 28 entries)
before anything is built, and other revisions simply log a warning and keep
the old look. `-no-hidden-car-textures` (`ScudPlusHiddenCarTextures = 0`)
disables it. ROM-free coverage: `python3 Tests/run_scud_hidden_car_textures.py`
(LZSS, ROM validation, per-rectangle level-0 and mipmap copies, no overlaps);
verified on the 2-cabinet relay rig with all four vehicles on Beginner,
Medium, Expert and Super Beginner (Legacy3D; the New3D path compiles and
loads but the rig's software GL cannot show it).

## Daytona USA 2 Power Edition: Reverse on the MIX course

Power Edition takes **Mirror** (hold Start while confirming the course) and
**Reverse** (hold Brake while the car is being chosen and confirmed) on
Beginner, Advanced and Expert, but the car-select program refuses Reverse on
the MIX course even though the game ships the reversed data for it (reverse
checkpoint tables, speed factors, scenery/voice event lists and the reversed
section order all have a MIX entry). Supermodel now removes that single check
for `dayto2pe`, always, online or offline, so MIX can be driven in Reverse and
in Mirror + Reverse like the other courses.

- Pick MIX, then hold Brake on **every** cabinet while the car is selected and
  confirmed; the `REVERSE` badge appears under the car and the race starts at
  the Beginner-course end of the point-to-point route. The link vote keeps the
  original rule that all players must ask for it, exactly as on the other
  courses.
- The patch is a session-only RAM code change in the style of the boot-scene
  patch (`Src/Network/Games/Daytona2/MixReverse.h`): one `beq` at RAM
  `0xAF38C` (the "course is MIX" bail-out in the Reverse decision) becomes a
  `nop`, after the three words before it (course byte load and compare) and
  the seven words after it (brake value load, threshold test and flag store)
  have been verified. Other revisions log a notice and keep the original
  behaviour; ROM set, NVRAM and config are untouched.
- Grand Prix / Endurance keep refusing Reverse (a different test on the game
  mode byte), as in the original.

ROM-free coverage: `python3 Tests/run_async_netboard.py --only mix-reverse`.
Verified on the 2-cabinet relay rig: normal, Mirror, Reverse and Mirror +
Reverse on MIX and Beginner, and a full reversed MIX run to the results screen
(section order Beginner -> Expert -> Advanced, 7 checkpoints, time extensions
and the lap/section counters behaving as on the normal direction).

# Online tab: relay lobbies in Supermodel

Start Supermodel **without command-line arguments** to open the existing
settings/game-selection GUI. The **Online** tab uses the relay for lobby control
and chat. Gameplay uses the relay by default, with an optional creator-hosted
TCP/UDP path.

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

## Configurable tick rate (`--tick-rate`)

The default requested network tick rate is `60` Hz; relay sessions run at
`57.524` emulated frames/s, so transmission is capped at one originating
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
  `srally2`, `lemans24`) automatically adapt their horizon, trajectory
  smoothing, and 2D/3D turn-rate extrapolation to the configured tick rate so
  remote cars continue moving smoothly every emulated frame (57.524 FPS online) even at low tick rates (e.g.
  `10`–`30` Hz).

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
- Only 848/1024/2048/3264-byte game board frames, `PIN1`/`PON1` pings are delayed, and only
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
3. Click **Connect**. Online play always runs with AsyncNet; there is no
   per-session opt-in checkbox. This does not rewrite your offline or per-game
   AsyncNet setting in the INI file.
4. One player selects a supported ROM, enters a lobby name and clicks
   **Create Lobby**. The creator is the master, car 1.
5. Other players select the lobby in the live list and click **Join Lobby**.
   They must have the selected exact ROM locally. Games are never downloaded
   or transferred by the relay. The GUI checks filenames/required parent;
   the normal ROM loader still performs actual contents/CRC validation.
6. The roster displays MASTER/SLAVE and car numbers. With **2–16 Daytona players** (Scud: 2–8, Sega Rally 2: 2–4 or 2–8 in the optional 8-player mode, Le Mans 24: 2–6), only
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
- One relay supports up to **16 separate GUI lobbies**, with game-specific capacity (Daytona editions: 16, Scud editions: 8, Sega Rally 2: 4 (8-player mode: 8), Le Mans 24: 6).
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
in, and you connect by pressing **Connect**. Online sessions always run with
AsyncNet; the emulator never needs a per-session checkbox and never rewrites
your INI setting. A failed/cancelled automatic attempt is not retried in a
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
```

The runners exercise the HLE board, online C++ client, lobby protocol and session
policy without game ROMs. They are not a substitute for an actual-ROM or Windows
GUI smoke test.


## Mandatory relay refresh and returning to Online

All relay sessions force **57.524 Hz emulation** after global/game INI and CLI
settings have been resolved. Software frame limiting is mandatory and its
in-game toggle is disabled online. Display VSync is disabled for relay sessions
so a 50/60/120 Hz monitor cannot dictate cabinet speed. This is an emulation
rate requirement, not a guarantee the physical monitor supports a 57.524 Hz
video mode. Offline settings are unchanged and these overrides are not saved.
Relay tick rates remain real-time Hz (capped by one send per emulated frame);
transport timing/age metadata uses the enforced frame duration.

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

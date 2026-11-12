# Online race positions

During an Online game, the positions panel shows the linked human players in
native race order, with their lobby car number and player name (for example,
`3P: Alice`). CPU opponents affect the game's ranking but are not listed.
Chat visibility and the in-game chat mute do not hide the panel.

The panel is available for all seven lobby selections across six ROM families:
Daytona USA 2, Daytona USA 2 Power Edition, Scud Race, Scud Race Plus, Sega
Rally 2 (standard and optional 8-player modes), and Le Mans 24. Both Rally
modes use `srally2.zip`; the selected mode determines whether the room allows
up to four or eight cabinets.

Ranks are read from validated native game state on the emulation thread, then
sent over the existing ticket-authenticated room-chat connection. The network
worker never reads guest RAM. Names and cabinet numbers come from the lobby
roster, not from the rank report. Missing or stale ranks are shown as unknown;
the client does not infer positions from car coordinates or cabinet order.

This is a display feature, not anti-cheat. It does not change the game packet,
physics, or native ranking. The standings channel is independent of the game
link: a chat/standings connection problem does not stop an otherwise healthy
race. Updates are bounded and expire when the owning client disconnects or
stops reporting.

For implementation, see `Src/Network/Online/RaceStandings.h` and the in-game
overlay in `Src/OSD/SDL/InGameChat.cpp`. The ROM-free client regression
`python3 Tests/run_online_client.py` covers native sampling, lifecycle,
authenticated roster names, rank changes, ties, and temporarily unavailable
ranks. It does not run game ROMs or verify an actual multi-cabinet race.

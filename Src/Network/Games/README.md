# Game-specific network code

Put native game-specific async logic/fixes in a named child folder:

- `Daytona2/`: exact `dayto2pe` and `daytona2`, separate header/CPU/EEPROM profiles and GUIDs.
- `Lemans24/`: exact `lemans24` (Step 1.5), EEPROM CRC/role, Challenger Entry + start-gate barriers, and 3D human/CPU pose + angle prediction.
- `Scud/`: exact `scud` and `scudplus`, separate revision checks and protocol IDs.
- `Srally2/`: exact `srally2`, EEPROM LINK TYPE role, self-echo preservation, and human/CPU prediction.

Shared scheduling/dispatch lives here; socket transport/framing stays in
`Src/Network/`. Keep machine integration thin. Native addresses, packet decoding,
prediction, EEPROM mapping and gate predicates belong to their game module.
See `Docs/GameSpecificNetworking.md` and `Docs/ScudNetworking.md`.

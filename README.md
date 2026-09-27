# Versus

Versus provides a room browser and waiting room for one-on-one matches.

- The built-in Versus button opens a blue lobby with a moving background.
- Search rooms by name. The list refreshes every ten seconds and uses bounded
  pages to avoid oversized Geometry Dash scroll layers.
- Create a room with an optional four-digit private code. Every room has exactly
  two seats: its host and one guest.
- The waiting room downloads the selected level and audio automatically. Both
  players ready independently; changing the map or rules clears both states.
- Both player cards show preparation percentages, bars and music/SFX file
  counts. Percentages weight each asset equally, not by byte size; the native
  map request has no byte-progress callback. Peer updates are throttled to
  roughly two seconds plus network latency.
- Map Mode offers the existing native map picker or a rated-only Random Map
  mode. Multi-select native difficulty icons, check exact stars, and choose
  Classic or Platformer. Selected filters are combined with OR. Both players Ready,
  then the host presses Roll: up to ten matching maps appear in a shared,
  decelerating roulette with names, gold-font creators, difficulty icons,
  stars and ticking audio. Manual selection opens with Search Map .. .
  Once chosen, both clients download the map, music and SFX automatically,
  then Ready again and Start. A finished random match requires a fresh roll.
- Random candidates are sampled from bounded random pages of GD's native
  rated search, filtered and deduplicated; this is not a uniform sample over
  the entire GD catalogue. A sparse filter may return 2–9 cards; fewer than
  two candidates produces a retry message instead of an invalid draw.
- The host configures attempts or percentage rules and Practice. The lobby
  shows the active rule before joining; either player can open Game Rules.
  Platformer maps support Attempts only. Room names use the host's nickname.
- Click a room player's name to view their recent matches or GD account.
  History access expires when either player leaves the room; historical
  attempt logs remain private unless the viewer played that match.
- Room emotes provide four rate-limited reactions in animated speech bubbles;
  the membership edition adds a fifth money reaction.
- Starting loads the selected map for both players. Gameplay waits for both to
  enter, with a 60-second deadline and a shared three-second countdown.
- Download errors, cancellation and loading timeouts return to the room.
- Quitting/forfeiting an active Versus match keeps its room. A forfeit waits
  for server confirmation before returning; a sustained connection failure
  can delay this return. Ordinary GD map exits are unaffected.
- Leaving as host closes the room and returns its guest to the room browser.
- Large gold-font Win counts track this room's current pair only. Each finished
  match counts once; draws add no wins. Rematches/map/rule changes preserve the
  score. Guest departure or timeout clears both counters before another joins.
- Match HUDs show both players and progress. Attempts, spectating,
  percentage ties, forced Practice, pause timeouts, forfeits, result animations,
  and recent history are synchronized through the room state.
- Position and camera data are sent at roughly 7 Hz only while an opponent is
  spectating; percentage matches never send player positions.

Database: `https://tipp7versus-default-rtdb.firebaseio.com/`

See [FIREBASE_SETUP.md](FIREBASE_SETUP.md) for Authentication, the server API key
and the separate database rules before connecting players.

## Build and install

Build targets are Windows, Android32 and Android64. iOS/macOS builds (including
CI) are paused until explicitly requested again. The fixed combined filename
remains `dist/tipp7.versus-AllPlatform.geode`; it contains only these three
current targets, never older Apple binaries.

The release also provides `dist/tipp7.versus-AllPlatform-standard.geode` and
`dist/tipp7.versus-AllPlatform-membership.geode`. The fixed legacy filename is
an alias of the standard package. Membership adds green-to-white nickname
colors and the money emote. Both share the same mod ID: install only one.
This is a compile-time cosmetic edition, not server-verified paid entitlement.

Use `-DVERSUS_MEMBERSHIP=ON` or `OFF` when configuring CMake to select the
edition. `-DGEODE_DONT_INSTALL_MODS=ON` packages without replacing the local
installed mod. The CI builds both editions for all three targets and verifies
architecture, edition markers, resources and ZIP integrity before publishing.

The Geode CLI is configured to use the `First` profile. Run this command from
the project directory:

```powershell
geode build --config Release
```

On Windows, every successful build packages the mod and installs it into the
active Geometry Dash Geode profile automatically.

## Verification

Run the CMake tests in `tests/`, the offline `tests/package_editions.py`
regressions, and `tests/firebase-rules.ps1` against a local database emulator.
The Firebase test script addresses loopback only and never deploys rules.

Before distributing a release, manually check two-client downloads, native
map decorations, money reactions, spectator timeout results and repeated
room-return/rematch cycles on Windows and low-memory Android devices. These
in-game/device checks are not covered by the headless test suite.

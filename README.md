# Versus

Versus provides a room browser and waiting room for one-on-one matches.

- The built-in Versus button opens a blue lobby with a moving background.
- Search rooms by name. The list refreshes every ten seconds and uses bounded
  pages to avoid oversized Geometry Dash scroll layers.
- Create a room with an optional four-digit private code. Every room has exactly
  two seats: its host and one guest.
- The waiting room downloads the selected level and audio automatically. Both
  players ready independently; changing the map or rules clears both states.
- The host configures attempts or percentage rules, Practice, and optional
  sequential turns. The lobby shows the active rule before joining.
- Room emotes provide four rate-limited reactions in animated speech bubbles.
- Starting loads the selected map for both players. Gameplay waits for both to
  enter, with a 60-second deadline and a shared three-second countdown.
- Download errors, cancellation and loading timeouts return to the room.
- Leaving as host closes the room and returns its guest to the room browser.
- Match HUDs show both players and progress. Attempts, sequential spectating,
  percentage ties, forced Practice, pause timeouts, forfeits, result animations,
  and recent history are synchronized through the room state.
- Position and camera data are sent at roughly 7 Hz only while an opponent is
  spectating; percentage matches never send player positions.

Database: `https://tipp7versus-default-rtdb.firebaseio.com/`

See [FIREBASE_SETUP.md](FIREBASE_SETUP.md) for Authentication, the server API key
and the separate database rules before connecting players.

## Build and install

The Geode CLI is configured to use the `First` profile. Run this command from
the project directory:

```powershell
geode build --config Release
```

On Windows, every successful build packages the mod and installs it into the
active Geometry Dash Geode profile automatically.

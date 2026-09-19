# v0.4.2

- Replace inactive Ready with Choose Map or Download until its prerequisites are met.
- Correct disabled-button opacity, refresh completed downloads, and show retry errors.
- Defer room UI rebuilds until after touch callbacks and ignore stale download callbacks.
- Recover the map download queue after expired native requests.
- Reject readiness for maps or rules changed while the request was waiting.
- Decorate room rows with blue and sky-blue beveled border blocks.
- Keep the latest combined build at dist/tipp7.versus-AllPlatform.geode.

# v0.4.1

- Fix empty rooms being incorrectly reported as full and host readiness resetting on heartbeat.
- Hide sequence controls in Percent mode and keep rule editing locked while saving.
- Give the Ready button a continuous rainbow background and glow.
- Restyle room rows with blue gradients, mode badges, and distinct occupancy colors.
- Show the correct fewest-attempts rule for practice matches.

# v0.4.0

- Add host rule controls for attempts, percent targets, sequence and practice.
- Require each player to download the selected map and ready up before starting.
- Add room emotes, match HUD, progress updates, spectating and winner presentation.
- Apply pause and forfeit rules during matches and record finalized results.

# v0.3.1

- Bundle the Versus Firebase Web API key so players can connect without entering it manually.

# v0.3.0

- Add guest Ready/Unready controls and require readiness before starting.
- Load the selected map for both players and wait for both to enter.
- Add a 60-second loading deadline and shared three-second start countdown.
- Return to the room on loading failure, cancellation or timeout.
- Keep gameplay local, without player synchronization or result submission.
- Extend Firebase rules to validate readiness and each player's loading state.

# v0.2.0

- Open the Versus room browser from the built-in Versus button.
- Add moving backgrounds, blue panels, room-name search and bounded room pages.
- Refresh room lists every ten seconds and add a recent-match stats panel.
- Add fixed two-player rooms with optional four-digit private entry codes.
- Add a waiting room with player profiles, map selection and host-only controls.
- Return guests to the browser when the host leaves or the host lease expires.
- Add authenticated Firebase access and separate database rules.

# v0.1.1

- Enable and recolor the built-in Versus button.
- Add the blue and white Tipp7 icon badge.
- Use the Geometry Dash Versus artwork as the mod logo.

# v0.1.0

- Create the initial Geode mod project.

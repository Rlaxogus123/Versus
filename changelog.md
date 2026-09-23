# v0.4.15

- Restore the original Versus interface styling and remove added sound cues.
- Combine percentage comparison and winner decision into one result phase.
- Show both player icons, names, and percentages clearly in the final result window.

# v0.4.14

- Remove square color-gradient overlays from rounded panels while keeping the existing layout.
- Animate result percentages with frame-rate-independent exponential interpolation.
- Separate winner and execution phases, then add faster attacks, impact flashes, particles, camera zoom, tilt, and shake.
- Play the ready cue when either the local player or the opponent becomes ready.

# v0.4.13

- Refresh the lobby, room, battle HUD, synchronized start, result panel, and Creator Layer badge with native Geometry Dash framing and cleaner spacing.
- Add restrained sparkles, shadows, cyan/pink player accents, and fade/slide transitions.
- Add stage-specific sound cues with five optional custom OGG overrides and built-in fallbacks.

# v0.4.12

- Remove automatic cheating detection and its alerts.
- Separate result progress, winner, execution, and confirmation scenes with fades and a dark transition overlay.
- Show a thin countdown line at the bottom for every result scene.

# v0.4.11

- Restore the desktop cursor after returning from a match.
- Replace the Creator Layer Versus player icon with a “Versus Mode!” label.

# v0.4.10

- Refresh expired Firebase authentication before changing a room, including rule edits and room departure.

# v0.4.9

- Fix match start and ordinary progress against servers still using the previous Firebase rules by omitting the optional false cheat flag.
- If older rules reject a detected cheat marker, record the offender's loss through the existing forfeit path.
- Distinguish Firebase rule denials from authentication failures and log the failing database path.
- Publish the included `firebase-rules.json` to enable shared cheat notices and early attempt wins on the live server.

# v0.4.8

- Remove the sequence mode control and prevent new sequence matches while retaining legacy match compatibility.
- Build the combined package for Windows, macOS, Android32, and Android64; exclude iOS.

# v0.4.7

- Put clean, edge-aligned player cards in the match HUD and use Life Calculator's heart shape.
- Enlarge the spectator runner and replace its flying enemy with a black bat with white eyes.
- End an attempt battle early after the opponent exhausts their attempts when a player leads with fewer attempts.
- Detect suspicious noclip, repeated midair jumps, speed changes, and forbidden test/practice mode; report the offender as the loser.
- Stage the result with animated progress bars, winner effects, a finishing move, and a confirmation screen with a return button and 20-second timer.
- Show a result-review notice in the room while the other player is still viewing the result.

# v0.4.6

- Show the winner's nickname and each player's best percentage throughout the result animation.
- Keep results above native game overlays for seven seconds, then return both players to their room.
- Treat pause timeouts as match losses that return to the room, rather than leaving the room.

# v0.4.5

- Count native null-player deaths and deduplicate death/reset notifications.
- Show the spectator runner only while the opponent is still playing; suppress native retry dialogs during battles.
- Return to the room independently of history uploads and safely retry result acknowledgements.
- Prevent duplicate room resets from clearing readiness for the next match.
- Close the native pause menu when results arrive so it cannot cover the result animation.
- Use bigFont player cards with blue gradients, mirrored layouts and heart life counters.

# v0.4.4

- Show the winner name and both players' best percentages in result animations.
- Save results before returning both players to the same room for a rematch.
- Add opponent icons and detailed match history, per-attempt comparisons, map links and ID copying.
- Replace camera spectating with a local endless runner using the opponent icon and live match stats.
- Add tap/hold jumps, scrolling GD scenery, spikes and flying monsters without position traffic.
- Extend Firebase rules for private attempt history and independent result acknowledgements.

# v0.4.3

- Fetch song metadata before downloading map music; include all listed songs and SFX.
- Retry transient map/audio failures and report the failing asset ID.
- Allow larger multi-song downloads to complete before readying up.
- Avoid removing music delegates inside native callback iteration.
- Remove room-row block borders and place game rules beside the room name.

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

# Stage 4 — Persistent Voice Controls: manual acceptance checklist

Status: **NOT RUN** — this checklist requires a live client build against a local
server. No item below has been executed or verified by automation; the automated
coverage lives in `TestVoiceControlBar` and `TestMainWindowShell` (component
scope only — no real `MainWindow`/`ConfigDialog` can be constructed in a test
harness).

Compare behavior against the same-base official client where applicable.

## Buttons / action authority

- [ ] Mute/deafen via the bar's buttons, the Self menu, the tray, RPC/DBus and
      the global shortcuts — all presentations agree (button checked state,
      tray icon, tree icons, log lines).
- [ ] Mute↔deafen coupling in both directions, including `unmuteOnUndeaf`
      (un-deafening does not un-mute when the setting is off).
- [ ] Audio wizard guard: while the audio wizard is open, mute/deafen
      attempts are rejected (the button state snaps back; the wizard is
      never opened by muting). Verify from every entry point: bar buttons,
      Self menu, tray, global shortcuts, RPC/DBus.
- [ ] Settings button opens the config dialog from both normal and minimal
      mode (minimal mode previously had no settings access at all).

## Voice status area

- [ ] Server mute/deafen/suppress shows the status icon + text and does NOT
      flip the bar's mute/deafen buttons.
- [ ] Push-to-mute shows the transient "Push-to-mute" status only; the mute
      button stays unchanged.
- [ ] Whisper/shout/talking states show the correct text/icon.
- [ ] With several states active at once (e.g. server mute + suppression +
      talking), the visible line shows only the highest-priority status while
      the tooltip (status area AND identity label) lists every active status,
      self and server states as separate lines.
- [ ] Self-only mute/deafen: no visible status, but the identity label's
      tooltip and accessibleDescription carry the "(self)" lines.

## Connection lifecycle

- [ ] Identity ("user in channel") populates on connect; self channel moves
      update the channel name immediately (the `userMoved` fix); channel
      renames update it; self rename (server-side) updates the identity.
- [ ] Disconnect clears identity and status to the empty presentation
      completely - no self-mute/self-deafen tooltip lines and no transient
      push-to-mute status either - while the mute/deafen buttons keep their
      self state; reconnect repopulates. Test this while self-muted,
      self-deafened and push-to-mute is held. Repeat with the server killed
      under the client (failed reconnect path).

## Transmission-mode combo

- [ ] Exactly one combo exists (the bar's); three modes; switching applies
      immediately (`setTransmissionMode` path, `transmissionModeChanged`).
- [ ] `gsCycleTransmitMode` and RPC/DBus/API transmit-mode changes update the
      bar combo.
- [ ] Combo hidden unless `bShowTransmitModeComboBox` is enabled; toggling it
      in LookConfig takes effect on **Apply** with the dialog still open, on
      **OK**, and after Reset/Restore Defaults + Apply/OK; restart prompt
      behavior unchanged.
- [ ] With `bShowPTTButtonWindow` enabled: switching into Push-to-Talk (via
      the combo, `gsCycleTransmitMode`/transmit-mode shortcuts, RPC/DBus)
      shows the floating PTT window immediately; switching away removes it;
      toggling the setting in LookConfig syncs it on **Apply**/**OK**
      through the same path. Verify transitions in both directions from
      every mode.

## Modes and geometry

- [ ] Bar visible and usable in normal AND minimal mode (incl. settings
      access); frameless minimal sizing sane.
- [ ] Narrow resize down to minimal-mode widths, combo shown and hidden: no
      overlap, buttons never clipped, elided identity/status, complete tooltip
      on elision. With the combo shown the window floor is wider (~220-260 px)
      — documented, unavoidable.
- [ ] RTL layout, large fonts, 150 %/200 % scaling: same properties hold.

## Keyboard pass

- [ ] Tab/Shift+Tab: tree → log → chat input → [combo] → mute → deaf →
      settings in normal mode; tree → [combo] → buttons in minimal mode. The
      combo's hidden state is skipped.
- [ ] Connected presentation (chat input enabled): Tab in the chat input
      autocompletes when the text starts with `@`; otherwise focus advances
      through the real focus chain into the bar's chain head (the live slot
      routes ordinary Tab through the focus chain, not a fixed jump). The
      disconnected presentation disables the chat input, so Tab out of the
      log reaches the bar directly.
- [ ] F6 cycles the main areas (log → chat input → tree); starting from a
      voice-bar button or any other non-major widget it begins at the log;
      in minimal mode the hidden log/chat input are skipped (the tree keeps
      focus); a disabled area is skipped as well.
- [ ] Space/Enter activate the focused control; one activation = one trigger.

## Explicitly NOT claimed by this stage

- Stage 1–3 manual compatibility/live-server checks (still pending).
- Stage 2 official/fork alternation gate (still pending).

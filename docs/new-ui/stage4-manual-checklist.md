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
- [ ] Audio wizard guard: attempting to mute/deafen with unsuitable audio
      settings still opens the wizard instead of muting.
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
- [ ] Disconnect clears identity and status to the empty presentation while
      the mute/deafen buttons keep their self state; reconnect repopulates.
      Repeat with the server killed under the client (failed reconnect path).

## Transmission-mode combo

- [ ] Exactly one combo exists (the bar's); three modes; switching applies
      immediately (`setTransmissionMode` path, `transmissionModeChanged`).
- [ ] `gsCycleTransmitMode` and RPC/DBus/API transmit-mode changes update the
      bar combo.
- [ ] Combo hidden unless `bShowTransmitModeComboBox` is enabled; toggling it
      in LookConfig takes effect on **Apply** with the dialog still open, on
      **OK**, and after Reset/Restore Defaults + Apply/OK; restart prompt
      behavior unchanged.

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
- [ ] Note: the chat input consumes Tab/Backtab itself (autocomplete), so the
      bar is reached from the tree side, whenever the chat input is disabled
      (the disconnected presentation), or with Shift+Tab out of the chain
      head.
- [ ] Space/Enter activate the focused control; one activation = one trigger.

## Explicitly NOT claimed by this stage

- Stage 1–3 manual compatibility/live-server checks (still pending).
- Stage 2 official/fork alternation gate (still pending).

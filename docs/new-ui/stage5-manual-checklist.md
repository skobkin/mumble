# Stage 5 — Chat/Activity Split: manual acceptance checklist

Status: **NOT RUN** — this checklist requires a live client build against a
local server (two clients for delivery). No item below has been executed or
verified by automation; the automated coverage lives in `TestLogPresentation`,
`TestLogDocument`, `TestChatStream`, `TestChatTarget`, `TestMainWindowShell`
and `TestUiStateStore` (component scope only — no real `MainWindow`,
`ServerHandler` or live connection can be constructed in a test harness).

Compare behavior against the same-base official client where applicable.
Stage 4's checklist (`stage4-manual-checklist.md`) remains a prerequisite.

## Routing and delivery (two clients)

- [ ] A channel message addressed to a channel appears in Chat on BOTH
      clients; every other event type (joins, permission denials, warnings,
      server welcome text, plugin messages) lands in Activity only.
- [ ] A direct (private) message appears in Chat for both sides; the sender's
      own echo is the single accepted entry (the server never echoes the
      sender back).
- [ ] A tree-scoped message renders in Chat on every client in the subtree;
      under the Selected Channel filter it matches every channel the tree
      root covers.
- [ ] A multi-target message (multiple channels/direct recipients in one
      delivery) shows as ONE logical entry, matching under any of its
      targets.
- [ ] Locally ignored senders, reconnect-suppressed entries and
      console-disabled message types never appear in Chat — nor reappear
      after switching filters back and forth (the store is written only past
      all vetoes).

## Filters

- [ ] All / Selected Channel / Direct behave as labeled; the row is visible
      only while the Chat tab is active.
- [ ] Selecting a channel in the tree does NOT activate the filter by
      itself; activating Selected Channel tracks the live selection from
      then on.
- [ ] A listener-proxy row drives Selected Channel to the LISTENED channel.
- [ ] Removing the tracked channel (or disconnecting) while Selected Channel
      is active falls back to All — no frozen empty view.
- [ ] Reconnect with reused channel ids: old (pre-reconnect) entries stay in
      All/Direct but never leak into (or hide from) the new connection's
      Selected Channel view.
- [ ] Scroll positions are remembered per filter; a first visit to a filter
      opens at the bottom.

## Unread and notifications

- [ ] A private message arriving while Selected Channel is active produces NO
      extra notification (highlight/balloon behavior identical to the
      combined-log client), but the All and Direct filter badges increment.
- [ ] Switching to Direct displays the entry and drains its badge; the Chat
      tab badge (visible while Activity is active) tracks unseen entries.
- [ ] The Activity badge clears on showing the tab; unseen
      warnings/errors show the warning state; counts cap at 99+; counts drop
      when evictions remove the entries they counted.
- [ ] Filter-hidden entries keep normal highlight/balloon/sound/TTS
      behavior — filters change visibility only, never notifications.

## Clear

- [ ] Chat's context-menu Clear removes only the ACTIVE filter's entries
      (Direct-clear keeps channel messages; Selected-clear keeps directs;
      All-clear empties the stream); cleared entries never reappear on any
      filter switch or reconnect.
- [ ] Activity's Clear clears only the Activity document; Chat content is
      untouched, and vice versa.

## Composer target and sends

- [ ] The destination label above the input always names the current target
      (channel with full path tooltip / user), regardless of active tab and
      filter; it disappears when disconnected.
- [ ] The label updates when the selection changes, when the renamed user is
      the selected target, and after toggling the
      "Use selection" chatbar setting in the config dialog (Apply, without
      closing the dialog).
- [ ] All five non-composer send paths deliver to the labeled target: user
      dialog DM, channel/tree dialog sends, the configured text-message
      shortcut. The shortcut's always-current-channel divergence is
      pre-existing behavior (documented, unchanged).
- [ ] Sending re-resolves: changing the selection between reading the label
      and pressing Enter delivers to the NEW target (the label is never a
      routing input).
- [ ] Clipboard paste and image paste sends behave as before.
- [ ] A listener-proxy selected row: label shows the user with the
      listener-proxy note; the message reaches the user directly.
- [ ] Permission-denied: without Write/TextMessage rights the composer is
      disabled; the optimistic window while permissions are still unknown
      matches the official client.

## Presentation and lifecycle

- [ ] Tab switches never touch documents, scroll positions or selections;
      minimal mode hides the whole panel and both badges resurface on
      return.
- [ ] Keyboard: Tab reaches the switch row, the active console, the whole
      filter row (all three buttons — never autoExclusive-stripped) and the
      composer; F6 cycles {active console, composer, tree}.
- [ ] Ctrl+Shift+L toggles Chat/Activity.
- [ ] Live theme switch restyles BOTH consoles.
- [ ] The persisted tab survives restart; a revision-1 sidecar from a
      stage-4 build loads with Chat active and no diagnostics.
- [ ] `iMaxLogBlocks` bounds BOTH destinations: Activity stops growing at
      the block budget; Chat stops at the same number of entries; lowering
      the limit in the config dialog trims both immediately.
- [ ] Long sessions with many unique inline images stay within memory
      bounds (record RSS before/after for the PR).

## Accessibility and desktop pass

- [ ] Screen reader: switch/filter buttons announce their label AND unread
      count; the activity warning state is announced; the target label is
      read with the composer context.
- [ ] Large fonts, RTL locale and narrow window widths: switch/filter rows
      and the target label stay usable; nothing overflows or truncates
      illegibly.

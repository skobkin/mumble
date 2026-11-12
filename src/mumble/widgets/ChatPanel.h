// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the Mumble
// source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_WIDGETS_CHATPANEL_H_
#define MUMBLE_WIDGETS_CHATPANEL_H_

#include <QWidget>

class ChatbarTextEdit;
class QAction;
class QLabel;
class LogConsoleView;
class QMenu;
class QStackedWidget;
class QToolButton;

/// Chat-area presentation of the redesigned main window: a Chat/Activity
/// switch row above a stacked pair of console views (chat messages vs.
/// server-wide activity), with the chat composer below. The compact
/// chat-filter control (a dropdown) sits right-aligned in the switch row.
/// Pure presentation: the views and the composer are created by MainWindow
/// and adopted here, and the panel never sees message types or logging
/// policy - MainWindow routes entries to the views directly and drives the
/// active tab and the unread badges through the setters below. Switching
/// tabs only swaps the stacked page: documents, scroll positions and
/// selections are never touched.
class ChatPanel : public QWidget {
	Q_OBJECT

public:
	/// The chat-stream filters (presentation mirror of
	/// ChatStreamModel::Filter; MainWindow maps between the two).
	enum class ChatFilter { All, SelectedChannel, Direct };

	/// Adopts \p chatConsole, \p activityConsole and \p chatInput (reparenting
	/// them into this panel). Detaching the widgets from their legacy dock
	/// containers hides them; adoption explicitly shows them again so normal
	/// mode cannot end up with invisible content widgets. The Chat tab is
	/// active initially; MainWindow applies the persisted tab on top.
	ChatPanel(LogConsoleView *chatConsole, LogConsoleView *activityConsole, ChatbarTextEdit *chatInput,
			  QWidget *parent = nullptr);

	LogConsoleView *chatConsole() const;
	LogConsoleView *activityConsole() const;
	ChatbarTextEdit *chatInput() const;
	/// The console whose tab is active (the stacked current widget).
	LogConsoleView *activeConsole() const;
	/// True while the Activity tab is active.
	bool activityTabActive() const;

	/// Switches the active tab. Passing one of the adopted views selects its
	/// tab; unknown pointers are ignored. Selecting the already-active tab is
	/// a no-op.
	void setActiveConsole(LogConsoleView *view);

	/// The tab switch buttons (presentation access for tests and focus-chain
	/// wiring; behavior is driven through setActiveConsole).
	QToolButton *chatTabButton() const;
	QToolButton *activityTabButton() const;

	/// Unread badges on the tab buttons: 0 hides the counter, counts above 99
	/// display "99+". The activity badge can additionally carry a warning
	/// state (icon + tooltip) for unseen Warning/CriticalError entries.
	void setChatBadge(int count);
	void setActivityBadge(int count, bool warning);

	/// The chat filter control: a compact dropdown button, right-aligned in
	/// the switch row, visible only while the Chat tab is active. Its label
	/// names the active filter; the dropdown menu offers the three filters
	/// with their unread counts. Selecting the already-active filter is a
	/// no-op; an unavailable Selected Channel (no tracked channel) is a
	/// disabled menu option and programmatic requests for it are refused -
	/// an unavailable filter is never silently swapped for another one.
	ChatFilter chatFilter() const;
	void setChatFilter(ChatFilter filter);
	/// Unread counts alongside the individual filter options in the dropdown
	/// menu (same 99+ style; 0 hides the counter). Screen readers get the
	/// count through the accessible name, like the tab badges.
	void setFilterBadge(ChatFilter filter, int count);
	/// How many unseen chat messages the ACTIVE filter hides (store-derived
	/// by MainWindow: unseen total minus the active filter's unseen count).
	/// Shown on the dropdown control; 0 hides it, counts above 99 display
	/// "99+".
	void setFilterHiddenUnread(int count);
	/// Whether the Selected Channel filter is available (a tracked channel
	/// exists). Disabling never unselects it: MainWindow falls back to All
	/// through setChatFilter before the selection can go stale.
	void setSelectedChannelAvailable(bool available);
	/// The filter control and its dropdown menu (presentation access for
	/// tests and focus wiring).
	QToolButton *filterButton() const;
	QMenu *filterMenu() const;

	/// The composer's explicit destination label, above the input. Always
	/// visible once it has content - regardless of the active tab and the
	/// chat filter (the destination is a composer property, not a chat-view
	/// projection property). Empty rich text hides it. Presentation only:
	/// the label is never a routing input (sends re-resolve their target).
	void setTargetDisplay(const QString &richText, const QString &toolTip);
	QLabel *targetLabel() const;

	/// Establishes the panel-internal keyboard chain after \p previous
	/// (switch row with the filter control -> chat view -> activity view ->
	/// composer). Widgets hidden by the stack or the tab state (the
	/// inactive view, the filter control while Activity is active) are
	/// skipped dynamically by Qt's focus traversal.
	void establishTabOrderAfter(QWidget *previous);

signals:
	/// Emitted after the active tab changed (user click or programmatic
	/// switch). \p activityActive names the new state.
	void consoleSwitched(bool activityActive);
	/// Emitted after the chat filter changed (user click or programmatic
	/// setChatFilter).
	void chatFilterChanged(ChatPanel::ChatFilter filter);

private:
	LogConsoleView *m_chatConsole;
	LogConsoleView *m_activityConsole;
	ChatbarTextEdit *m_chatInput;
	QToolButton *m_chatTabButton;
	QToolButton *m_activityTabButton;
	QStackedWidget *m_stack;
	QToolButton *m_filterButton;
	QMenu *m_filterMenu;
	/// The three filter options in dropdown order (All, Selected Channel,
	/// Direct); owned by m_filterMenu.
	QAction *m_filterActions[3];
	QLabel *m_targetLabel;

	void applyBadge(QToolButton *button, const QString &baseText, int count);
	/// The mnemonic base label of a filter option (the same tr strings the
	/// former button row used, so translations carry over).
	QString filterLabel(ChatFilter filter) const;
	/// Composes the dropdown control's text and accessible name from the
	/// active filter and the hidden-unread badge.
	void updateFilterButtonDisplay();

	ChatFilter m_chatFilter = ChatFilter::All;
	/// Whether the Selected Channel filter option is usable (a tracked
	/// channel exists); starts available like the former buttons did -
	/// MainWindow drives the real availability.
	bool m_selectedChannelAvailable = true;
	int m_filterHiddenUnread        = 0;
};

#endif // MUMBLE_WIDGETS_CHATPANEL_H_

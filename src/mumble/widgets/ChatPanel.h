// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the Mumble
// source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_WIDGETS_CHATPANEL_H_
#define MUMBLE_WIDGETS_CHATPANEL_H_

#include <QWidget>

class ChatbarTextEdit;
class LogConsoleView;
class QStackedWidget;
class QToolButton;

/// Chat-area presentation of the redesigned main window: a Chat/Activity
/// switch row above a stacked pair of console views (chat messages vs.
/// server-wide activity), with the chat composer below. Pure presentation:
/// the views and the composer are created by MainWindow and adopted here, and
/// the panel never sees message types or logging policy - MainWindow routes
/// entries to the views directly and drives the active tab and the unread
/// badges through the setters below. Switching tabs only swaps the stacked
/// page: documents, scroll positions and selections are never touched.
class ChatPanel : public QWidget {
	Q_OBJECT

public:
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

	/// Establishes the panel-internal keyboard chain after \p previous
	/// (switch row -> chat view -> activity view -> composer). Views hidden by
	/// the stack are skipped dynamically by Qt's focus traversal.
	void establishTabOrderAfter(QWidget *previous);

signals:
	/// Emitted after the active tab changed (user click or programmatic
	/// switch). \p activityActive names the new state.
	void consoleSwitched(bool activityActive);

private:
	LogConsoleView *m_chatConsole;
	LogConsoleView *m_activityConsole;
	ChatbarTextEdit *m_chatInput;
	QToolButton *m_chatTabButton;
	QToolButton *m_activityTabButton;
	QStackedWidget *m_stack;

	void applyBadge(QToolButton *button, const QString &baseText, int count);
};

#endif // MUMBLE_WIDGETS_CHATPANEL_H_

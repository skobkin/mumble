// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_WIDGETS_MAINWINDOWSHELL_H_
#define MUMBLE_WIDGETS_MAINWINDOWSHELL_H_

#include <QList>
#include <QWidget>

class ChatPanel;
class ChatbarTextEdit;
class QLabel;
class LogConsoleView;
class QSplitter;
class UserView;

/// Central-widget presentation for the redesigned main window: a header
/// container (filled by ConnectionHeader) above a horizontal splitter with the
/// channel/user tree on the left and the chat panel (chat/activity switch row,
/// stacked console views, chat input) on the right, plus the minimal-view
/// note.
///
/// The shell owns layout containers only. It adopts the existing widgets
/// (UserView, the two LogConsoleView destinations, ChatbarTextEdit) created by
/// MainWindow, so all signal connections, handlers and external member access
/// keep working with the very same objects. It has no Global dependency and
/// creates no models or connections, which keeps it constructible in isolated
/// tests.
class MainWindowShell : public QWidget {
	Q_OBJECT

public:
	/// Adopts \p tree (reparented from the legacy central-widget role),
	/// \p chatConsole, \p activityConsole and \p chatInput (created by, or
	/// reparented from, MainWindow) into the shell layout.
	MainWindowShell(UserView *tree, LogConsoleView *chatConsole, LogConsoleView *activityConsole,
					ChatbarTextEdit *chatInput, QWidget *parent = nullptr);

	/// The adopted channel/user tree. Same object the generated UI created.
	UserView *tree() const;
	/// The adopted chat panel wrapping the console switch row, the stacked
	/// console views and the chat input.
	ChatPanel *chatPanel() const;

	/// Places \p header into the reserved header container. Passing nullptr
	/// clears it. The header is only visible in normal mode.
	void setHeaderWidget(QWidget *header);

	/// Adopts \p bar as the persistent bottom control strip, appended after
	/// the minimal-view note row. Unlike the header, the bar is not tied to
	/// a presentation mode: it stays visible in normal and minimal mode, so
	/// it is never touched by setMinimal and its minimum flows into the
	/// layout constraints of both modes. A previously installed bar is
	/// scheduled for deletion. Passing nullptr only removes a previous bar.
	void setVoiceBar(QWidget *bar);

	QList< int > splitterSizes() const;
	void setSplitterSizes(const QList< int > &sizes);

	/// Applies the normal/minimal presentation: minimal mode hides the header
	/// and the chat panel (keeping the tree) and may show the note. Establishing
	/// this layout before restoring a mode's geometry is the caller's
	/// responsibility.
	void setMinimal(bool minimal);
	bool isMinimal() const;

	/// The minimal-view note is shown only when wanted AND in minimal mode
	/// (mirrors the legacy qdwMinimalViewNote visibility rule).
	void setNoteVisible(bool visible);
	void setNoteText(const QString &text);

	/// Moves keyboard focus to the next main interaction area, cycling
	/// ACTIVE console view → chat input → user tree (the order the main
	/// window's F6 has always used; only one console is visible at a time).
	/// Areas that cannot take focus in the current presentation are skipped
	/// (the chat panel with both consoles and the chat input is hidden in
	/// minimal mode; disabled areas are skipped too), and focus held by any
	/// other widget (e.g. a button inside the persistent voice bar) starts
	/// the cycle from the beginning instead of being treated as an error.
	/// Returns the area that received focus, or nullptr when no area is
	/// eligible.
	QWidget *focusNextMainArea();

private:
	QWidget *m_headerContainer;
	QSplitter *m_splitter;
	QLabel *m_note;
	/// The persistent bottom control strip (VoiceControlBar in the live
	/// window; a plain QWidget stand-in in tests). Untyped on purpose: the
	/// shell has no Global dependency and must stay isolated-test friendly.
	QWidget *m_voiceBar = nullptr;
	UserView *m_tree;
	ChatPanel *m_chatPanel;
	/// The chat panel's minimum width in normal mode. Released while the
	/// panel is hidden in minimal mode (hidden splitter children would
	/// otherwise keep the window's minimum width at the normal-mode value).
	static constexpr int m_chatPanelMinimumWidth = 280;
	bool m_minimal                               = false;
	bool m_noteVisible                           = false;
};

#endif // MUMBLE_WIDGETS_MAINWINDOWSHELL_H_

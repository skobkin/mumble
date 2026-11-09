// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_WIDGETS_CHATPANEL_H_
#define MUMBLE_WIDGETS_CHATPANEL_H_

#include <QWidget>

class ChatbarTextEdit;
class LogTextBrowser;

/// Presentation wrapper around the existing chat input and the combined log
/// browser used by the redesigned main window shell. It owns layout
/// containers only: the widgets are created by the generated MainWindow UI
/// and adopted (reparented) here, so all existing signal connections,
/// handlers and external access paths keep working with the very same
/// objects. The explicit chat-target label and the chat/activity split are
/// Stage 5 work; this panel deliberately keeps the combined presentation.
class ChatPanel : public QWidget {
	Q_OBJECT

public:
	/// Adopts \p log and \p chatInput (reparenting them into this panel).
	/// Detaching the widgets from their legacy dock containers hides them;
	/// adoption explicitly shows them again so normal mode cannot end up with
	/// invisible content widgets.
	ChatPanel(LogTextBrowser *log, ChatbarTextEdit *chatInput, QWidget *parent = nullptr);

	LogTextBrowser *log() const;
	ChatbarTextEdit *chatInput() const;

private:
	LogTextBrowser *m_log;
	ChatbarTextEdit *m_chatInput;
};

#endif // MUMBLE_WIDGETS_CHATPANEL_H_

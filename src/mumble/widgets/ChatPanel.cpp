// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "widgets/ChatPanel.h"

#include "CustomElements.h"

#include <QVBoxLayout>

ChatPanel::ChatPanel(LogTextBrowser *log, ChatbarTextEdit *chatInput, QWidget *parent)
	: QWidget(parent), m_log(log), m_chatInput(chatInput) {
	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	layout->addWidget(m_log, 1);
	layout->addWidget(m_chatInput, 0);

	// Reparenting a widget that was detached from its dock via
	// QDockWidget::setWidget(nullptr) leaves it hidden, and being inserted
	// into a not-yet-shown parent does not un-hide it. Show both explicitly
	// so normal mode cannot end up with invisible content widgets;
	// MainWindowShell::setMinimal() hides them again when entering minimal
	// mode.
	m_log->show();
	m_chatInput->show();
}

LogTextBrowser *ChatPanel::log() const {
	return m_log;
}

ChatbarTextEdit *ChatPanel::chatInput() const {
	return m_chatInput;
}

// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "widgets/MainWindowShell.h"

#include "CustomElements.h"
#include "UserView.h"
#include "widgets/ChatPanel.h"

#include <QLabel>
#include <QSplitter>
#include <QVBoxLayout>

MainWindowShell::MainWindowShell(UserView *tree, LogTextBrowser *log, ChatbarTextEdit *chatInput,
								 QWidget *parent)
	: QWidget(parent), m_headerContainer(new QWidget(this)), m_splitter(new QSplitter(this)),
	  m_note(new QLabel(this)), m_tree(tree), m_chatPanel(new ChatPanel(log, chatInput)) {
	m_tree->setMinimumWidth(180);
	m_chatPanel->setMinimumWidth(m_chatPanelMinimumWidth);
	m_chatPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	m_splitter->setChildrenCollapsible(false);
	m_splitter->addWidget(m_tree);
	m_splitter->addWidget(m_chatPanel);
	m_splitter->setStretchFactor(0, 0);
	m_splitter->setStretchFactor(1, 1);
	// Handing the tree over from the legacy central-widget role detaches it
	// from the QMainWindow, which hides it; insertion into the splitter does
	// not un-hide it. The ChatPanel re-shows its own adopted children.
	m_tree->show();

	m_note->setText(QObject::tr("You are currently in minimal view but not connected to a server. "
								"Use the context menu to connect to a server or disable minimal view."));
	m_note->setWordWrap(true);
	m_note->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	m_note->hide();

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(m_headerContainer, 0);
	layout->addWidget(m_splitter, 1);
	layout->addWidget(m_note, 0);

	// Explicit keyboard navigation chain across the adopted widgets.
	QWidget::setTabOrder(m_tree, m_chatPanel->log());
	QWidget::setTabOrder(m_chatPanel->log(), m_chatPanel->chatInput());
}

UserView *MainWindowShell::tree() const {
	return m_tree;
}

ChatPanel *MainWindowShell::chatPanel() const {
	return m_chatPanel;
}

void MainWindowShell::setHeaderWidget(QWidget *header) {
	// Remove a previously installed header before replacing the layout:
	// deleting a QLayout only deletes its items, never the widgets it managed,
	// so the old header would otherwise be orphaned inside the container
	// (still a child, still shown, no longer positioned by any layout).
	const auto previousHeaders = m_headerContainer->findChildren< QWidget * >(Qt::FindDirectChildrenOnly);
	for (QWidget *previous : previousHeaders) {
		if (previous != header) {
			previous->deleteLater();
		}
	}
	delete m_headerContainer->layout();

	if (header) {
		QVBoxLayout *headerLayout = new QVBoxLayout(m_headerContainer);
		headerLayout->setContentsMargins(0, 0, 0, 0);
		header->setParent(m_headerContainer);
		headerLayout->addWidget(header);
		header->show();
	}

	m_headerContainer->setVisible(!m_minimal && header != nullptr);
}

void MainWindowShell::setVoiceBar(QWidget *bar) {
	if (m_voiceBar && m_voiceBar != bar) {
		// deleteLater, not delete: a click arriving through the event queue
		// may still reference the old bar (same discipline as setHeaderWidget).
		m_voiceBar->deleteLater();
		m_voiceBar = nullptr;
	}

	if (bar) {
		bar->setParent(this);
		// The note row stays last-but-one; the control strip is the very
		// bottom row in both presentation modes.
		if (QVBoxLayout *root = qobject_cast< QVBoxLayout * >(layout())) {
			root->addWidget(bar, 0);
		}
		bar->show();
		m_voiceBar = bar;
	}
}

QList< int > MainWindowShell::splitterSizes() const {
	return m_splitter->sizes();
}

void MainWindowShell::setSplitterSizes(const QList< int > &sizes) {
	m_splitter->setSizes(sizes);
}

void MainWindowShell::setMinimal(bool minimal) {
	if (m_minimal == minimal) {
		return;
	}
	m_minimal = minimal;

	// Hiding the chat panel alone does not drop the window's minimum width:
	// QSplitter still counts hidden children's minimum sizes. The minimum is
	// lowered while hidden and restored on the way back, so a compact window
	// may genuinely be smaller than the normal layout's minimum.
	if (minimal) {
		m_chatPanel->setMinimumWidth(0);
		m_chatPanel->hide();
	} else {
		m_chatPanel->setMinimumWidth(m_chatPanelMinimumWidth);
		m_chatPanel->show();
	}
	m_headerContainer->setVisible(!minimal && m_headerContainer->layout() != nullptr);
	m_note->setVisible(m_noteVisible && minimal);

	// Synchronize the size constraints NOW, before the caller applies the
	// destination mode's geometry. Hiding the chat panel changes the
	// splitter's minimum, but the surrounding layout keeps a stale cached copy
	// of that minimum (and the top-level window's minimum is never lowered
	// automatically); a compact geometry restored right after the mode switch
	// would be clamped back up to the obsolete normal-mode minimum. Refreshing
	// the cached item minimums and re-activating the layout chain reinstalls
	// the constraints of the CURRENT state (raising them works the same way).
	m_splitter->updateGeometry();
	updateGeometry();
	if (QWidget *tl = window()) {
		if (QLayout *tlLayout = tl->layout()) {
			tlLayout->invalidate();
			tlLayout->activate();
		}
	}
}

bool MainWindowShell::isMinimal() const {
	return m_minimal;
}

bool MainWindowShell::isConsoleVisible() const {
	return !m_minimal && isVisible() && m_chatPanel->log()->isVisible();
}

void MainWindowShell::setNoteVisible(bool visible) {
	m_noteVisible = visible;
	m_note->setVisible(visible && m_minimal);
}

void MainWindowShell::setNoteText(const QString &text) {
	m_note->setText(text);
}

QWidget *MainWindowShell::focusNextMainArea() {
	QWidget *mainAreas[] = {
		m_chatPanel->log(),
		m_chatPanel->chatInput(),
		m_tree,
	};
	const int numAreas = sizeof(mainAreas) / sizeof(mainAreas[0]);

	// The focus widget of the window the shell lives in. It may be null (no
	// focus yet) or belong to any part of the window (e.g. a button inside
	// the persistent voice bar); both unrecognized cases start the cycle
	// from the beginning. The ancestor check runs in the right direction: a
	// main area CONTAINING the focus widget counts as the current position,
	// never the other way around.
	QWidget *focus = window()->focusWidget();
	int startIndex = 0;
	for (int i = 0; i < numAreas; ++i) {
		if (mainAreas[i] == focus || mainAreas[i]->isAncestorOf(focus)) {
			startIndex = (i + 1) % numAreas;
			break;
		}
	}

	// Skip areas that cannot take focus in the current presentation; the
	// full loop covering every area once also implements the wrap-around.
	for (int step = 0; step < numAreas; ++step) {
		QWidget *area = mainAreas[(startIndex + step) % numAreas];
		if (area->isVisible() && area->isEnabled()) {
			area->setFocus();
			return area;
		}
	}
	return nullptr;
}

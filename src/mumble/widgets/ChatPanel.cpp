// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the Mumble
// source tree or at <https://www.mumble.info/LICENSE>.

#include "widgets/ChatPanel.h"

#include "CustomElements.h"
#include "widgets/LogConsoleView.h"

#include <QHBoxLayout>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
/// Strips a mnemonic marker from button text for the accessible name (the
/// same pattern the Stage 2-4 widgets use).
QString strippedMnemonic(const QString &text) {
	QString stripped(text);
	stripped.remove(QLatin1Char('&'));
	return stripped;
}
} // namespace

ChatPanel::ChatPanel(LogConsoleView *chatConsole, LogConsoleView *activityConsole, ChatbarTextEdit *chatInput,
					 QWidget *parent)
	: QWidget(parent), m_chatConsole(chatConsole), m_activityConsole(activityConsole), m_chatInput(chatInput),
	  m_chatTabButton(new QToolButton(this)), m_activityTabButton(new QToolButton(this)),
	  m_stack(new QStackedWidget(this)) {
	m_chatTabButton->setObjectName(QLatin1String("chatTabButton"));
	m_activityTabButton->setObjectName(QLatin1String("activityTabButton"));
	m_chatTabButton->setText(tr("&Chat"));
	m_activityTabButton->setText(tr("&Activity"));
	for (QToolButton *button : { m_chatTabButton, m_activityTabButton }) {
		// No setAutoExclusive: Qt demotes every sibling of a checkable
		// exclusive button out of the tab chain (radio-group semantics,
		// QAbstractButtonPrivate::fixFocusPolicy), which would make the
		// inactive tab button unreachable by keyboard. Exactly-one-checked
		// is enforced in setActiveConsole instead.
		button->setCheckable(true);
		button->setToolButtonStyle(Qt::ToolButtonTextOnly);
		button->setAccessibleName(strippedMnemonic(button->text()));
		button->setToolTip(tr("Switch between chat messages and the activity log"));
	}
	m_chatTabButton->setChecked(true);

	m_stack->addWidget(m_chatConsole);
	m_stack->addWidget(m_activityConsole);
	// QStackedWidget shows the current page and hides the other; Chat is the
	// initial tab (MainWindow applies the persisted tab on top).
	m_stack->setCurrentWidget(m_chatConsole);

	// Button clicks switch tabs; setActiveConsole re-checks the buttons, and
	// auto-exclusivity keeps exactly one of them checked.
	connect(m_chatTabButton, &QToolButton::clicked, this, [this]() { setActiveConsole(m_chatConsole); });
	connect(m_activityTabButton, &QToolButton::clicked, this, [this]() { setActiveConsole(m_activityConsole); });

	QWidget *switchRow     = new QWidget(this);
	QHBoxLayout *rowLayout = new QHBoxLayout(switchRow);
	rowLayout->setContentsMargins(0, 0, 0, 0);
	rowLayout->setSpacing(0);
	rowLayout->addWidget(m_chatTabButton);
	rowLayout->addWidget(m_activityTabButton);
	rowLayout->addStretch(1);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(switchRow, 0);
	layout->addWidget(m_stack, 1);
	layout->addWidget(m_chatInput, 0);

	// Reparenting a widget that was detached from its dock via
	// QDockWidget::setWidget(nullptr) leaves it hidden, and being inserted
	// into a not-yet-shown parent does not un-hide it. The stacked pages are
	// shown by the stack itself; the composer needs the explicit show so
	// normal mode cannot end up with an invisible input widget.
	m_chatInput->show();
}

LogConsoleView *ChatPanel::chatConsole() const {
	return m_chatConsole;
}

LogConsoleView *ChatPanel::activityConsole() const {
	return m_activityConsole;
}

ChatbarTextEdit *ChatPanel::chatInput() const {
	return m_chatInput;
}

LogConsoleView *ChatPanel::activeConsole() const {
	return qobject_cast< LogConsoleView * >(m_stack->currentWidget());
}

bool ChatPanel::activityTabActive() const {
	return activeConsole() == m_activityConsole;
}

void ChatPanel::setActiveConsole(LogConsoleView *view) {
	if (view != m_chatConsole && view != m_activityConsole) {
		return;
	}

	// Manual exclusivity. Re-checking runs before the same-view early return
	// on purpose: clicking (or space-toggling) the already-active checkable
	// tab button flips it off before this slot runs, and the re-check puts it
	// back - clicking the active tab never leaves both buttons unchecked.
	m_chatTabButton->setChecked(view == m_chatConsole);
	m_activityTabButton->setChecked(view == m_activityConsole);
	if (activeConsole() == view) {
		return;
	}
	// Swapping the stacked page hides one view and shows the other; nothing
	// about the documents, their scroll positions or their selections is
	// touched.
	m_stack->setCurrentWidget(view);
	emit consoleSwitched(view == m_activityConsole);
}

QToolButton *ChatPanel::chatTabButton() const {
	return m_chatTabButton;
}

QToolButton *ChatPanel::activityTabButton() const {
	return m_activityTabButton;
}

void ChatPanel::applyBadge(QToolButton *button, const QString &baseText, int count) {
	if (count > 0) {
		const QString shown = count > 99 ? QStringLiteral("99+") : QString::number(count);
		button->setText(tr("%1 (%2)").arg(baseText, shown));
	} else {
		button->setText(baseText);
	}
}

void ChatPanel::setChatBadge(int count) {
	applyBadge(m_chatTabButton, tr("&Chat"), count);
}

void ChatPanel::setActivityBadge(int count, bool warning) {
	applyBadge(m_activityTabButton, tr("&Activity"), count);
	if (warning) {
		m_activityTabButton->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
		m_activityTabButton->setToolTip(tr("Unseen warnings or errors"));
	} else {
		m_activityTabButton->setIcon(QIcon());
		m_activityTabButton->setToolTip(tr("Switch between chat messages and the activity log"));
	}
}

void ChatPanel::establishTabOrderAfter(QWidget *previous) {
	QWidget::setTabOrder(previous, m_chatTabButton);
	QWidget::setTabOrder(m_chatTabButton, m_activityTabButton);
	QWidget::setTabOrder(m_activityTabButton, m_chatConsole);
	QWidget::setTabOrder(m_chatConsole, m_activityConsole);
	QWidget::setTabOrder(m_activityConsole, m_chatInput);
}

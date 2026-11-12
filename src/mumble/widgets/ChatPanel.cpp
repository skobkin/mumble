// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the Mumble
// source tree or at <https://www.mumble.info/LICENSE>.

#include "widgets/ChatPanel.h"

#include "CustomElements.h"
#include "widgets/LogConsoleView.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
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
	  m_stack(new QStackedWidget(this)), m_filterButton(new QToolButton(this)), m_filterMenu(new QMenu(m_filterButton)),
	  m_targetLabel(new QLabel(this)) {
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

	// The compact chat-stream filter control, right-aligned in the switch
	// row (the former bottom All/Selected Channel/Direct button row is gone;
	// the dropdown keeps the filtering semantics, the per-filter unread
	// counts and the unavailable-state handling without a second row).
	m_filterButton->setObjectName(QLatin1String("filterButton"));
	m_filterButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	m_filterButton->setToolTip(tr("Filter which chat messages are shown"));
	m_filterButton->setAccessibleDescription(tr("Choose which chat messages are displayed"));
	m_filterButton->setPopupMode(QToolButton::InstantPopup);
	m_filterButton->setMenu(m_filterMenu);
	// Checkable options keep the active filter visible and announced inside
	// the menu; an unavailable option stays visibly disabled - it is never
	// silently swapped for another filter.
	m_filterActions[0] = m_filterMenu->addAction(filterLabel(ChatFilter::All));
	m_filterActions[1] = m_filterMenu->addAction(filterLabel(ChatFilter::SelectedChannel));
	m_filterActions[2] = m_filterMenu->addAction(filterLabel(ChatFilter::Direct));
	m_filterActions[0]->setObjectName(QLatin1String("filterActionAll"));
	m_filterActions[1]->setObjectName(QLatin1String("filterActionSelectedChannel"));
	m_filterActions[2]->setObjectName(QLatin1String("filterActionDirect"));
	for (QAction *action : m_filterActions) {
		action->setCheckable(true);
	}
	m_filterActions[0]->setChecked(true);
	// The control's label composes the active filter and the (empty)
	// hidden-unread badge.
	updateFilterButtonDisplay();

	// Filter selection follows the menu options; setChatFilter re-checks
	// them.
	connect(m_filterActions[0], &QAction::triggered, this, [this]() { setChatFilter(ChatFilter::All); });
	connect(m_filterActions[1], &QAction::triggered, this, [this]() { setChatFilter(ChatFilter::SelectedChannel); });
	connect(m_filterActions[2], &QAction::triggered, this, [this]() { setChatFilter(ChatFilter::Direct); });

	m_stack->addWidget(m_chatConsole);
	m_stack->addWidget(m_activityConsole);
	// QStackedWidget shows the current page and hides the other; Chat is the
	// initial tab (MainWindow applies the persisted tab on top).
	m_stack->setCurrentWidget(m_chatConsole);

	// Button clicks switch tabs; setActiveConsole re-checks the buttons, and
	// manual exclusivity keeps exactly one of them checked.
	connect(m_chatTabButton, &QToolButton::clicked, this, [this]() { setActiveConsole(m_chatConsole); });
	connect(m_activityTabButton, &QToolButton::clicked, this, [this]() { setActiveConsole(m_activityConsole); });

	QWidget *switchRow     = new QWidget(this);
	QHBoxLayout *rowLayout = new QHBoxLayout(switchRow);
	rowLayout->setContentsMargins(0, 0, 0, 0);
	rowLayout->setSpacing(0);
	rowLayout->addWidget(m_chatTabButton);
	rowLayout->addWidget(m_activityTabButton);
	rowLayout->addStretch(1);
	rowLayout->addWidget(m_filterButton);

	// The composer's explicit destination. Hidden until MainWindow hands it
	// a target (setTargetDisplay); a label is not focusable, so it never
	// enters the keyboard chain.
	m_targetLabel->setObjectName(QLatin1String("targetLabel"));
	m_targetLabel->setTextFormat(Qt::RichText);
	m_targetLabel->setTextInteractionFlags(Qt::NoTextInteraction);
	m_targetLabel->hide();

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(switchRow, 0);
	layout->addWidget(m_stack, 1);
	layout->addWidget(m_targetLabel, 0);
	layout->addWidget(m_chatInput, 0);
	// The filter control belongs to the chat projection (the initial tab),
	// so it starts visible: setActiveConsole(chatConsole) is a no-op on the
	// already-current page and must not need to re-show it. Switching to the
	// Activity tab hides the control - that log is server-wide, not a chat
	// projection.

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
	// touched. The filter control belongs to the Chat tab alone.
	m_filterButton->setVisible(view == m_chatConsole);
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
	const QString strippedBase = strippedMnemonic(baseText);
	if (count > 0) {
		const QString shown = count > 99 ? QStringLiteral("99+") : QString::number(count);
		button->setText(tr("%1 (%2)").arg(baseText, shown));
		// The badge count is text, but the fixed accessible name would hide
		// it from screen readers: announce the unread count explicitly.
		button->setAccessibleName(tr("%1 (%2 unread)").arg(strippedBase, shown));
	} else {
		button->setText(baseText);
		button->setAccessibleName(strippedBase);
	}
}

void ChatPanel::setChatBadge(int count) {
	applyBadge(m_chatTabButton, tr("&Chat"), count);
}

void ChatPanel::setActivityBadge(int count, bool warning) {
	applyBadge(m_activityTabButton, tr("&Activity"), count);
	if (warning) {
		m_activityTabButton->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
		const QString warningNote = tr("Unseen warnings or errors");
		m_activityTabButton->setToolTip(warningNote);
		// Expose the warning state beyond the visual icon, too.
		m_activityTabButton->setAccessibleDescription(warningNote);
	} else {
		m_activityTabButton->setIcon(QIcon());
		m_activityTabButton->setToolTip(tr("Switch between chat messages and the activity log"));
		m_activityTabButton->setAccessibleDescription(QString());
	}
}

ChatPanel::ChatFilter ChatPanel::chatFilter() const {
	return m_chatFilter;
}

QString ChatPanel::filterLabel(ChatFilter filter) const {
	switch (filter) {
		case ChatFilter::All:
			return tr("&All");
		case ChatFilter::SelectedChannel:
			return tr("Selected &Channel");
		case ChatFilter::Direct:
			return tr("&Direct");
	}
	return QString();
}

void ChatPanel::setChatFilter(ChatFilter filter) {
	if (filter == ChatFilter::SelectedChannel && !m_selectedChannelAvailable) {
		// No tracked channel: the filter matches nothing. Keep the current
		// selection and stay quiet - an unavailable filter is never silently
		// swapped for another one (the dropdown option is disabled; this
		// also guards programmatic calls). The presenter holds the same
		// line.
		m_filterActions[0]->setChecked(m_chatFilter == ChatFilter::All);
		m_filterActions[1]->setChecked(m_chatFilter == ChatFilter::SelectedChannel);
		m_filterActions[2]->setChecked(m_chatFilter == ChatFilter::Direct);
		return;
	}

	const bool changed = m_chatFilter != filter;
	m_chatFilter       = filter;
	// Manual exclusivity in the menu, re-applied before the no-change early
	// return (the same discipline as setActiveConsole on the tab row).
	m_filterActions[0]->setChecked(filter == ChatFilter::All);
	m_filterActions[1]->setChecked(filter == ChatFilter::SelectedChannel);
	m_filterActions[2]->setChecked(filter == ChatFilter::Direct);
	updateFilterButtonDisplay();
	if (!changed) {
		return;
	}
	emit chatFilterChanged(filter);
}

void ChatPanel::setFilterBadge(ChatFilter filter, int count) {
	QAction *action    = filter == ChatFilter::All               ? m_filterActions[0]
						 : filter == ChatFilter::SelectedChannel ? m_filterActions[1]
																 : m_filterActions[2];
	const QString base = filterLabel(filter);
	QString text       = base;
	if (count > 0) {
		const QString shown = count > 99 ? QStringLiteral("99+") : QString::number(count);
		// A menu item announces its text: the count must live in it (QAction
		// has no separate accessible name), so spell the qualifier out. The
		// mnemonic stays embedded for keyboard menu navigation.
		text = tr("%1 (%2 unread)").arg(base, shown);
	}
	action->setText(text);
}

void ChatPanel::setFilterHiddenUnread(int count) {
	m_filterHiddenUnread = count;
	updateFilterButtonDisplay();
}

void ChatPanel::setSelectedChannelAvailable(bool available) {
	m_selectedChannelAvailable = available;
	// The dropdown option stays visibly disabled while unavailable.
	m_filterActions[1]->setEnabled(available);
}

QToolButton *ChatPanel::filterButton() const {
	return m_filterButton;
}

QMenu *ChatPanel::filterMenu() const {
	return m_filterMenu;
}

void ChatPanel::updateFilterButtonDisplay() {
	const QString base         = filterLabel(m_chatFilter);
	const QString strippedBase = strippedMnemonic(base);
	QString text               = base;
	QString accessible         = strippedBase;
	if (m_filterHiddenUnread > 0) {
		const QString shown = m_filterHiddenUnread > 99 ? QStringLiteral("99+") : QString::number(m_filterHiddenUnread);
		text                = tr("%1 (%2)").arg(base, shown);
		// Announce the hidden-unread count explicitly (the tab-badge
		// discipline); the description explains what the count means.
		accessible = tr("%1 (%2 unread hidden)").arg(strippedBase, shown);
	}
	m_filterButton->setText(text);
	m_filterButton->setAccessibleName(accessible);
}

void ChatPanel::setTargetDisplay(const QString &richText, const QString &toolTip) {
	if (richText.isEmpty()) {
		m_targetLabel->clear();
		m_targetLabel->hide();
		return;
	}
	m_targetLabel->setText(richText);
	m_targetLabel->setToolTip(toolTip);
	m_targetLabel->show();
}

QLabel *ChatPanel::targetLabel() const {
	return m_targetLabel;
}

void ChatPanel::establishTabOrderAfter(QWidget *previous) {
	QWidget::setTabOrder(previous, m_chatTabButton);
	QWidget::setTabOrder(m_chatTabButton, m_activityTabButton);
	QWidget::setTabOrder(m_activityTabButton, m_filterButton);
	QWidget::setTabOrder(m_filterButton, m_chatConsole);
	QWidget::setTabOrder(m_chatConsole, m_activityConsole);
	QWidget::setTabOrder(m_activityConsole, m_chatInput);
}

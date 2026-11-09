// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the Mumble source tree
// or at <https://www.mumble.info/LICENSE>.

#include "widgets/ConnectionHeader.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

ConnectionHeader::ConnectionHeader(QAction *connectAction, QAction *disconnectAction, QAction *informationAction,
								   QAction *searchAction, QWidget *parent)
	: QWidget(parent), m_statusLabel(new QLabel(this)), m_countLabel(new QLabel(this)) {
	m_statusLabel->setText(tr("Not connected"));

	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setContentsMargins(6, 2, 6, 2);
	layout->setSpacing(4);
	layout->addWidget(m_statusLabel, 1);
	layout->addWidget(m_countLabel, 0);

	// The buttons delegate everything to the existing actions; this class
	// never triggers or rewrites them.
	for (QAction *action : { connectAction, disconnectAction, informationAction, searchAction }) {
		QToolButton *button = new QToolButton(this);
		button->setDefaultAction(action);
		button->setAutoRaise(true);
		layout->addWidget(button, 0);
	}
}

void ConnectionHeader::updateConnectionSnapshot(ConnectionState state, const QString &serverDescription,
												int userCount, int channelCount) {
	m_state = state;

	switch (state) {
		case ConnectionState::Disconnected:
			m_statusLabel->setText(tr("Not connected"));
			m_countLabel->clear();
			break;
		case ConnectionState::Connecting:
			m_statusLabel->setText(tr("Connecting to %1...").arg(serverDescription));
			m_countLabel->clear();
			break;
		case ConnectionState::Synchronized:
			m_statusLabel->setText(tr("Connected to %1").arg(serverDescription));
			m_countLabel->setText(tr("%1 users, %2 channels").arg(userCount).arg(channelCount));
			break;
	}
}

ConnectionHeader::ConnectionState ConnectionHeader::connectionState() const {
	return m_state;
}

QLabel *ConnectionHeader::statusLabel() const {
	return m_statusLabel;
}

QLabel *ConnectionHeader::countLabel() const {
	return m_countLabel;
}

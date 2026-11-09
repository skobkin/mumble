// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the Mumble source tree
// or at <https://www.mumble.info/LICENSE>.

// Light presentation tests of the connection header (Stage 2 of the new UI,
// commit 3). The header is exercised with plain QActions standing in for the
// MainWindow actions: the button row must be a pure projection of them
// (identity, enabled/checked state, triggering), and the pushed snapshot must
// fully replace the previous one so no stale connection text survives a
// disconnect. MainWindow-side push timing is covered by the manual smoke
// checklist (the tri-state is derived from live Global state that a harness
// cannot construct).

#include <QtTest>

#include "widgets/ConnectionHeader.h"

#include <QAction>
#include <QLabel>
#include <QSignalSpy>
#include <QToolButton>

class TestConnectionHeader : public QObject {
	Q_OBJECT

private slots:
	void initialSnapshotIsDisconnected();
	void buttonsMirrorActionsInOrder();
	void actionStatePropagatesToButtons();
	void clickTriggersExactlyOnce();
	void snapshotReplacesStaleState();

	// The count-freshness contract: MainWindow re-pushes a snapshot on every
	// UserModel userAdded/userRemoved/channelAdded/channelRemoved emission
	// (each fires after the underlying collection was updated), so the
	// displayed counts must track every re-pushed value - including the
	// steady growth of an initial synchronization. The model-side wiring
	// itself lives in MainWindow (Global-bound, manual coverage); this pins
	// the header half: no stale count may survive a later snapshot.
	void countSnapshotsStayFreshUnderRepeatedPushes();

private:
	static QList< QToolButton * > buttonsOf(const ConnectionHeader &header) {
		return header.findChildren< QToolButton * >();
	}
};

void TestConnectionHeader::initialSnapshotIsDisconnected() {
	QAction connectAction, disconnectAction, informationAction, searchAction;
	ConnectionHeader header(&connectAction, &disconnectAction, &informationAction, &searchAction);
	header.show();

	QCOMPARE(header.connectionState(), ConnectionHeader::ConnectionState::Disconnected);
	QVERIFY(header.countLabel()->text().isEmpty());
}

void TestConnectionHeader::buttonsMirrorActionsInOrder() {
	QAction connectAction, disconnectAction, informationAction, searchAction;
	ConnectionHeader header(&connectAction, &disconnectAction, &informationAction, &searchAction);
	header.show();

	const QList< QToolButton * > buttons = buttonsOf(header);
	QCOMPARE(buttons.size(), 4);
	QCOMPARE(buttons.at(0)->defaultAction(), &connectAction);
	QCOMPARE(buttons.at(1)->defaultAction(), &disconnectAction);
	QCOMPARE(buttons.at(2)->defaultAction(), &informationAction);
	QCOMPARE(buttons.at(3)->defaultAction(), &searchAction);
}

void TestConnectionHeader::actionStatePropagatesToButtons() {
	QAction connectAction, disconnectAction, informationAction, searchAction;
	ConnectionHeader header(&connectAction, &disconnectAction, &informationAction, &searchAction);
	header.show();

	QToolButton *button = buttonsOf(header).at(0);
	QVERIFY(button->isEnabled());

	connectAction.setEnabled(false);
	QVERIFY(!button->isEnabled());
	connectAction.setEnabled(true);
	QVERIFY(button->isEnabled());

	connectAction.setCheckable(true);
	connectAction.setChecked(true);
	QVERIFY(button->isChecked());
	connectAction.setChecked(false);
	QVERIFY(!button->isChecked());
}

void TestConnectionHeader::clickTriggersExactlyOnce() {
	QAction connectAction, disconnectAction, informationAction, searchAction;
	ConnectionHeader header(&connectAction, &disconnectAction, &informationAction, &searchAction);
	header.show();

	QToolButton *button = buttonsOf(header).at(0);
	QSignalSpy spy(&connectAction, &QAction::triggered);
	QTest::mouseClick(button, Qt::LeftButton);

	QCOMPARE(spy.count(), 1);
}

void TestConnectionHeader::snapshotReplacesStaleState() {
	QAction connectAction, disconnectAction, informationAction, searchAction;
	ConnectionHeader header(&connectAction, &disconnectAction, &informationAction, &searchAction);
	header.show();

	// A synchronized snapshot with recognizable values...
	header.updateConnectionSnapshot(ConnectionHeader::ConnectionState::Synchronized, QStringLiteral("example.org:64738"), 5, 3);
	QCOMPARE(header.connectionState(), ConnectionHeader::ConnectionState::Synchronized);
	QVERIFY(header.statusLabel()->text().contains(QStringLiteral("example.org:64738")));
	QVERIFY(header.countLabel()->text().contains(QStringLiteral("5")));
	QVERIFY(header.countLabel()->text().contains(QStringLiteral("3")));

	// ...and a connecting snapshot replacing it...
	header.updateConnectionSnapshot(ConnectionHeader::ConnectionState::Connecting, QStringLiteral("other.org:64738"), 0, 0);
	QCOMPARE(header.connectionState(), ConnectionHeader::ConnectionState::Connecting);
	QVERIFY(header.statusLabel()->text().contains(QStringLiteral("other.org:64738")));
	QVERIFY(!header.statusLabel()->text().contains(QStringLiteral("example.org:64738")));
	QVERIFY(header.countLabel()->text().isEmpty());

	// ...and the disconnected snapshot clearing every trace.
	header.updateConnectionSnapshot(ConnectionHeader::ConnectionState::Disconnected, QString(), 0, 0);
	QCOMPARE(header.connectionState(), ConnectionHeader::ConnectionState::Disconnected);
	QVERIFY(!header.statusLabel()->text().contains(QStringLiteral("other.org:64738")));
	QVERIFY(header.countLabel()->text().isEmpty());
}

void TestConnectionHeader::countSnapshotsStayFreshUnderRepeatedPushes() {
	QAction connectAction, disconnectAction, informationAction, searchAction;
	ConnectionHeader header(&connectAction, &disconnectAction, &informationAction, &searchAction);
	header.show();

	// Initial synchronization arriving user by user: every re-push must be
	// reflected immediately, never lag one change behind.
	for (int users = 1; users <= 5; ++users) {
		header.updateConnectionSnapshot(ConnectionHeader::ConnectionState::Synchronized,
										QStringLiteral("example.org:64738"), users, 2);
		QVERIFY(header.countLabel()->text().contains(QString::number(users)));
	}

	// Users leaving again.
	const int shrinking[] = { 4, 3, 1 };
	for (const int users : shrinking) {
		header.updateConnectionSnapshot(ConnectionHeader::ConnectionState::Synchronized,
										QStringLiteral("example.org:64738"), users, 2);
		QVERIFY(header.countLabel()->text().contains(QString::number(users)));
	}

	// The connection attempt push carries no counts...
	header.updateConnectionSnapshot(ConnectionHeader::ConnectionState::Connecting, QStringLiteral("other.org:64738"), 0, 0);
	QVERIFY(header.countLabel()->text().isEmpty());

	// ...and a later synchronized snapshot restores live counts.
	header.updateConnectionSnapshot(ConnectionHeader::ConnectionState::Synchronized, QStringLiteral("other.org:64738"), 7, 4);
	QVERIFY(header.countLabel()->text().contains(QStringLiteral("7")));
	QVERIFY(header.countLabel()->text().contains(QStringLiteral("4")));
}

QTEST_MAIN(TestConnectionHeader)
#include "TestConnectionHeader.moc"

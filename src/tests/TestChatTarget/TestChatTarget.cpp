// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the Mumble
// source tree or at <https://www.mumble.info/LICENSE>.

// Decision-table tests for the composer target resolver (Stage 5 of the
// new UI). resolveChatTarget is pure value logic over live connection
// objects - Global-free by design - so the whole matrix runs on stack
// Channel/ClientUser objects: no window, no model, no connection. The
// table reproduces the former inline target logic of updateChatBar()/
// sendChatbarMessage() byte-for-byte, plus one deliberate fix: a
// disconnect teardown race degrades to Disconnected instead of
// dereferencing the dead self user.

#include <QtTest>

#include "Channel.h"
#include "ChatTarget.h"
#include "ClientUser.h"

class TestChatTarget : public QObject {
	Q_OBJECT

private slots:
	// No self user (disconnected): Disconnected wins over every other
	// input, whatever is selected and whatever the setting says.
	void disconnectedAlwaysWins();

	// bChatBarUseSelection off: always the self channel (the fallback),
	// never the selection - user row, channel row or no row at all.
	void selectionUseOffAlwaysTargetsSelfChannel();

	// bChatBarUseSelection on: no usable selection falls back to the self
	// channel; a channel row targets that channel; the SELF user row
	// targets the row's channel WITHOUT the fallback flag (it comes from
	// the selection); another user's row is a user target whose
	// permission channel is the selected row's channel.
	void selectionUseOnMatrix();

	// A listener-proxy row (user sitting in a different channel than the
	// row): still a user target - the flag is presentation-only - and the
	// permission channel is the LISTENED channel, not the user's own.
	void listenerProxyRowIsAUserTarget();

	// The disconnect race the old inline code crashed on: session user
	// object present but already stripped of its channel (or gone)
	// degrades to Disconnected.
	void defensiveDegradationOnTeardownRace();

	// displayName/isValid are derived from the kind; a user selection
	// without a row channel (model corner) keeps a null permission
	// channel, disabling the composer exactly like the old rule.
	void displayNameValidityAndCornerFlags();

	// The resolver returns the very objects it was handed, never copies
	// or lookups - callers hold live connection state.
	void pointerIdentity();
};

void TestChatTarget::disconnectedAlwaysWins() {
	Channel selfChannel(0, QStringLiteral("Root"));
	ClientUser alice;
	alice.uiSession = 2;
	alice.qsName    = QStringLiteral("alice");
	alice.cChannel  = &selfChannel;

	for (bool useSelection : { false, true }) {
		for (bool listener : { false, true }) {
			const ChatTarget target = resolveChatTarget(&alice, &selfChannel, listener, nullptr, useSelection);
			QCOMPARE(target.kind, ChatTarget::Kind::Disconnected);
			QVERIFY(!target.isValid());
			QVERIFY(target.channel == nullptr);
			QVERIFY(target.user == nullptr);
			QVERIFY(target.permissionChannel == nullptr);
		}
	}
}

void TestChatTarget::selectionUseOffAlwaysTargetsSelfChannel() {
	Channel root(0, QStringLiteral("Root"));
	Channel selfChannel(1, QStringLiteral("Lounge"), &root);
	Channel otherChannel(2, QStringLiteral("Balcony"), &root);

	ClientUser self;
	self.uiSession = 1;
	self.qsName    = QStringLiteral("self");
	self.cChannel  = &selfChannel;

	ClientUser alice;
	alice.uiSession = 2;
	alice.qsName    = QStringLiteral("alice");
	alice.cChannel  = &otherChannel;

	// A selected foreign user row, a selected channel row and no selection
	// at all: all resolve to the self channel through the fallback.
	const ChatTarget userRow = resolveChatTarget(&alice, &otherChannel, false, &self, false);
	const ChatTarget chanRow = resolveChatTarget(nullptr, &otherChannel, false, &self, false);
	const ChatTarget noRow   = resolveChatTarget(nullptr, nullptr, false, &self, false);
	for (const ChatTarget *target : { &userRow, &chanRow, &noRow }) {
		QCOMPARE(target->kind, ChatTarget::Kind::Channel);
		QVERIFY(target->isValid());
		QCOMPARE(target->channel, &selfChannel);
		QCOMPARE(target->permissionChannel, &selfChannel);
		QVERIFY(target->fromSelfChannelFallback);
		QVERIFY(!target->viaListenerProxy);
	}
}

void TestChatTarget::selectionUseOnMatrix() {
	Channel root(0, QStringLiteral("Root"));
	Channel selfChannel(1, QStringLiteral("Lounge"), &root);
	Channel otherChannel(2, QStringLiteral("Balcony"), &root);

	ClientUser self;
	self.uiSession = 1;
	self.qsName    = QStringLiteral("self");
	self.cChannel  = &selfChannel;

	ClientUser alice;
	alice.uiSession = 2;
	alice.qsName    = QStringLiteral("alice");
	alice.cChannel  = &otherChannel;

	// No selection at all: the self-channel fallback.
	const ChatTarget none = resolveChatTarget(nullptr, nullptr, false, &self, true);
	QCOMPARE(none.kind, ChatTarget::Kind::Channel);
	QCOMPARE(none.channel, &selfChannel);
	QVERIFY(none.fromSelfChannelFallback);

	// A channel row: that channel, no fallback flag.
	const ChatTarget channelRow = resolveChatTarget(nullptr, &otherChannel, false, &self, true);
	QCOMPARE(channelRow.kind, ChatTarget::Kind::Channel);
	QCOMPARE(channelRow.channel, &otherChannel);
	QCOMPARE(channelRow.permissionChannel, &otherChannel);
	QVERIFY(!channelRow.fromSelfChannelFallback);

	// The SELF user row: the row's channel WITHOUT the fallback flag (the
	// channel comes from the selection, same as the old inline code's
	// getChannel(currentIndex())).
	const ChatTarget selfRow = resolveChatTarget(&self, &selfChannel, false, &self, true);
	QCOMPARE(selfRow.kind, ChatTarget::Kind::Channel);
	QCOMPARE(selfRow.channel, &selfChannel);
	QVERIFY(!selfRow.fromSelfChannelFallback);

	// Another user's row: a user target; the permission channel is the
	// selected row's channel (the user's own channel here).
	const ChatTarget userRow = resolveChatTarget(&alice, &otherChannel, false, &self, true);
	QCOMPARE(userRow.kind, ChatTarget::Kind::User);
	QCOMPARE(userRow.user, &alice);
	QCOMPARE(userRow.permissionChannel, &otherChannel);
	QVERIFY(!userRow.fromSelfChannelFallback);
	QVERIFY(!userRow.viaListenerProxy);
}

void TestChatTarget::listenerProxyRowIsAUserTarget() {
	Channel root(0, QStringLiteral("Root"));
	Channel lounge(1, QStringLiteral("Lounge"), &root);
	Channel balcony(2, QStringLiteral("Balcony"), &root);

	ClientUser self;
	self.uiSession = 1;
	self.qsName    = QStringLiteral("self");
	self.cChannel  = &lounge;

	// Alice SITS in the balcony but her selected row is her listener proxy
	// under the lounge: the row's channel is the lounge.
	ClientUser alice;
	alice.uiSession = 2;
	alice.qsName    = QStringLiteral("alice");
	alice.cChannel  = &balcony;

	const ChatTarget target = resolveChatTarget(&alice, &lounge, true, &self, true);
	QCOMPARE(target.kind, ChatTarget::Kind::User);
	QCOMPARE(target.user, &alice);
	// The send addresses the user regardless of the proxy row; the flag is
	// presentation-only.
	QVERIFY(target.viaListenerProxy);
	// The permission channel follows the SELECTED ROW's channel (the
	// listened lounge), not the user's own balcony.
	QCOMPARE(target.permissionChannel, &lounge);
	QCOMPARE(target.channel, nullptr);
}

void TestChatTarget::defensiveDegradationOnTeardownRace() {
	Channel root(0, QStringLiteral("Root"));
	Channel lounge(1, QStringLiteral("Lounge"), &root);

	ClientUser self;
	self.uiSession = 1;
	self.qsName    = QStringLiteral("self");
	// Mid-teardown: the user object exists but its channel reference is
	// already gone. The old inline code dereferenced
	// ClientUser::get(uiSession)->cChannel here and crashed.
	self.cChannel = nullptr;

	ClientUser alice;
	alice.uiSession = 2;
	alice.qsName    = QStringLiteral("alice");
	alice.cChannel  = &lounge;

	const ChatTarget withSelection = resolveChatTarget(&alice, &lounge, false, &self, true);
	QCOMPARE(withSelection.kind, ChatTarget::Kind::Disconnected);
	QVERIFY(!withSelection.isValid());

	const ChatTarget withoutSelection = resolveChatTarget(nullptr, nullptr, false, &self, true);
	QCOMPARE(withoutSelection.kind, ChatTarget::Kind::Disconnected);
}

void TestChatTarget::displayNameValidityAndCornerFlags() {
	Channel root(0, QStringLiteral("Root"));
	Channel balcony(2, QStringLiteral("Balcony"), &root);

	ClientUser self;
	self.uiSession = 1;
	self.qsName    = QStringLiteral("self");
	self.cChannel  = &root;

	ClientUser alice;
	alice.uiSession = 2;
	alice.qsName    = QStringLiteral("alice");
	alice.cChannel  = &balcony;

	// displayName is the raw name; escaping stays at the presentation
	// site. isValid tracks the kind.
	const ChatTarget channelTarget = resolveChatTarget(nullptr, &balcony, false, &self, true);
	QCOMPARE(channelTarget.displayName(), QStringLiteral("Balcony"));
	QVERIFY(channelTarget.isValid());

	const ChatTarget userTarget = resolveChatTarget(&alice, &balcony, false, &self, true);
	QCOMPARE(userTarget.displayName(), QStringLiteral("alice"));
	QVERIFY(userTarget.isValid());

	const ChatTarget disconnected = resolveChatTarget(&alice, &balcony, false, nullptr, true);
	QCOMPARE(disconnected.displayName(), QString());
	QVERIFY(!disconnected.isValid());

	// Model corner: a user selection whose row reports no channel. The
	// user target stands, but the null permission channel disables the
	// composer - exactly what the old rule produced (permissions of a
	// null channel are None).
	const ChatTarget noRowChannel = resolveChatTarget(&alice, nullptr, false, &self, true);
	QCOMPARE(noRowChannel.kind, ChatTarget::Kind::User);
	QCOMPARE(noRowChannel.user, &alice);
	QCOMPARE(noRowChannel.permissionChannel, nullptr);
}

void TestChatTarget::pointerIdentity() {
	Channel root(0, QStringLiteral("Root"));
	Channel balcony(2, QStringLiteral("Balcony"), &root);

	ClientUser self;
	self.uiSession = 1;
	self.qsName    = QStringLiteral("self");
	self.cChannel  = &root;

	ClientUser alice;
	alice.uiSession = 2;
	alice.qsName    = QStringLiteral("alice");
	alice.cChannel  = &balcony;

	const ChatTarget target = resolveChatTarget(&alice, &balcony, false, &self, true);
	// The resolver hands back the very objects it was given - never copies
	// or fresh lookups. Callers hold live connection state.
	QCOMPARE(target.user, &alice);
	QCOMPARE(target.permissionChannel, &balcony);

	const ChatTarget fallbackTarget = resolveChatTarget(nullptr, nullptr, false, &self, true);
	QCOMPARE(fallbackTarget.channel, &root);
	QCOMPARE(fallbackTarget.permissionChannel, &root);
}

QTEST_MAIN(TestChatTarget)
#include "TestChatTarget.moc"

// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the Mumble
// source tree or at <https://www.mumble.info/LICENSE>.

// Store and projection tests for the chat stream (Stage 5 of the new UI):
// ChatStreamModel is the ONE process-local bounded buffer of accepted chat
// entries, and ChatStreamPresenter projects it through (filter, selected
// channel, connection epoch) into a LogConsoleView. The trio is Global-free
// by design, which is what makes it constructible here without any fixture.
// The Log side of the routing-metadata contract (queued delivery through
// Log::log, deferred startup logs) lives in TestLogPresentation, which owns
// the temporary-directory Global.

#include <QtTest>

#include "ChatStreamModel.h"
#include "ChatStreamPresenter.h"
#include "ConnectionEpoch.h"
#include "Log.h"
#include "widgets/LogConsoleView.h"

#include <QApplication>
#include <QDateTime>
#include <QSignalSpy>
#include <QTextDocument>

namespace {

ChatRoutingInfo directRouting(unsigned int session, quint64 epoch = 1) {
	ChatRoutingInfo routing;
	routing.present       = true;
	routing.senderSession = 7;
	routing.senderName    = QStringLiteral("alice");
	routing.directSessions.append(session);
	routing.epoch = epoch;
	return routing;
}

ChatRoutingInfo channelRouting(unsigned int channelId, quint64 epoch = 1) {
	ChatRoutingInfo routing;
	routing.present       = true;
	routing.senderSession = 7;
	routing.senderName    = QStringLiteral("alice");
	routing.channelIds.append(channelId);
	routing.epoch = epoch;
	return routing;
}

ChatRoutingInfo treeRouting(unsigned int rootId, quint64 epoch = 1) {
	ChatRoutingInfo routing;
	routing.present       = true;
	routing.senderSession = 7;
	routing.senderName    = QStringLiteral("alice");
	routing.treeIds.append(rootId);
	routing.epoch = epoch;
	return routing;
}

ChatEntry makeEntry(const QString &marker, const QDateTime &timestamp, const ChatRoutingInfo &routing) {
	ChatEntry entry;
	entry.timestamp      = timestamp;
	entry.consoleHtml    = QString::fromLatin1("<p>%1</p>").arg(marker);
	entry.renderSettings = LogConsoleView::RenderSettings{ 3, true };
	entry.routing        = routing;
	return entry;
}

/// Deterministic timestamp: \p day selects the date (date-separator tests
/// span several), \p minute keeps strict ordering inside it.
QDateTime atTime(int day, int minute) {
	return QDateTime(QDate(2024, 3, day), QTime(10, minute, 0));
}

/// Fixed two-digit marker ("mk07"), so substring containment checks cannot
/// confuse mk1 with mk10..mk12.
QString marker(int number) {
	return QString::fromLatin1("mk%1").arg(number, 2, 10, QLatin1Char('0'));
}

/// A subtree resolver whose root 1 covers exactly channel 5.
ChatStreamModel::TreeCoverage coversRootOne() {
	return [](unsigned int rootId, unsigned int channelId) { return rootId == 1 && channelId == 5; };
}

} // namespace

class TestChatStream : public QObject {
	Q_OBJECT

private slots:
	// The standalone connection-epoch counter: monotonic under bump(), safe
	// to read from any producer thread (atomic loads).
	void connectionEpochCounterIsMonotonic();

	// The store is the one bounded buffer: 0 = unlimited; a cap evicts
	// oldest-first, reporting the evicted ids; lowering the cap trims
	// immediately, not on the next append.
	void storeEntryCapSemantics();

	// Classification parity with msgTextMessage: tree > channel > direct.
	// Direct requires metadata, no tree and no channel target and at least
	// one session.
	void classificationParity();

	// Filter matching: All matches everything (metadata or not); Direct is
	// a pure metadata predicate; Selected Channel is id- and epoch-scoped,
	// resolves tree-root coverage through the live-topology resolver, and a
	// multi-target message is one logical entry.
	void filterMatchingMatrix();

	// Id-based matching is only valid within the entry's connection epoch:
	// after a reconnect the same channel id means a different channel.
	void epochScopedSelectionAcrossReconnects();

	// Seen means actually displayed; badges derive from the store's seen
	// flags alone. The worked example: a private arriving while Selected
	// Channel is active is not displayed even though the chat view is
	// visible - the All and Direct badges grow, and switching to Direct
	// displays it and drains them.
	void seenFlagsDriveBadges();

	// Live appends under one filter with a store cap: the rendered document
	// never outgrows the store (lockstep eviction, date separators
	// included), and its content equals a full rebuild from the store - no
	// filter switch involved.
	void lockstepEvictionEquivalence();

	// Store-aware Clear removes only the active filter's entries; hidden
	// entries survive and stay reachable through their own filters; nothing
	// ever reappears; badges recompute from the store.
	void storeAwareClearMatrix();

	// Losing the tracked channel (removed or disconnected) while Selected
	// Channel is active falls back to All; re-activating Selected Channel
	// without a selection is refused; re-setting the same selection only
	// refreshes the resolver.
	void selectedChannelLifecycle();

private:
	// LogConsoleView always owns a LogDocument (constructor, clearConsole,
	// compact and rebuild all create one), so the cast cannot fail.
	static LogDocument *docOf(LogConsoleView &view) { return static_cast< LogDocument * >(view.document()); }
};

void TestChatStream::connectionEpochCounterIsMonotonic() {
	const quint64 before = ConnectionEpoch::current();
	ConnectionEpoch::bump();
	const quint64 middle = ConnectionEpoch::current();
	QVERIFY(middle > before);
	ConnectionEpoch::bump();
	ConnectionEpoch::bump();
	QVERIFY(ConnectionEpoch::current() > middle);
}

void TestChatStream::storeEntryCapSemantics() {
	ChatStreamModel store;
	QCOMPARE(store.entryCap(), 0);
	QCOMPARE(store.size(), 0);

	// The default cap is 0 = unlimited: nothing ever evicts.
	for (int i = 0; i < 100; ++i) {
		const ChatStreamModel::AppendResult result =
			store.append(makeEntry(marker(i + 1), atTime(1, i), directRouting(9)));
		QCOMPARE(result.evictedIds.count(), 0);
	}
	QCOMPARE(store.size(), 100);
	// Ids are store-assigned and monotonically increasing.
	QCOMPARE(store.at(0).id, quint64(1));
	QCOMPARE(store.at(99).id, quint64(100));

	// Lowering the cap trims the overflow immediately, oldest first.
	store.setEntryCap(10);
	QCOMPARE(store.size(), 10);
	QCOMPARE(store.at(0).id, quint64(91));

	// Further appends evict oldest-first and report the evicted ids.
	const ChatStreamModel::AppendResult result = store.append(makeEntry(marker(101), atTime(1, 101), directRouting(9)));
	QCOMPARE(store.size(), 10);
	QCOMPARE(result.assignedId, quint64(101));
	QCOMPARE(result.evictedIds, QVector< quint64 >() << 91);
	QCOMPARE(store.at(0).id, quint64(92));

	// Id lookup reflects the eviction.
	QCOMPARE(store.findById(91), nullptr);
	QVERIFY(store.findById(101) != nullptr);
}

void TestChatStream::classificationParity() {
	// No metadata at all: not direct (still renders, matches only All).
	QVERIFY(!ChatStreamModel::isDirect(ChatRoutingInfo()));

	// Metadata targeting only sessions: direct.
	QVERIFY(ChatStreamModel::isDirect(directRouting(9)));

	// A tree target outranks the direct one - msgTextMessage classifies
	// tree > channel > direct, and so must the filter predicate.
	ChatRoutingInfo treeAndDirect = directRouting(9);
	treeAndDirect.treeIds.append(1);
	QVERIFY(!ChatStreamModel::isDirect(treeAndDirect));

	// A channel target outranks the direct one the same way.
	ChatRoutingInfo channelAndDirect = directRouting(9);
	channelAndDirect.channelIds.append(5);
	QVERIFY(!ChatStreamModel::isDirect(channelAndDirect));

	// Tree-only and channel-only are never direct.
	QVERIFY(!ChatStreamModel::isDirect(treeRouting(1)));
	QVERIFY(!ChatStreamModel::isDirect(channelRouting(5)));

	// Metadata without any target is not direct either.
	ChatRoutingInfo empty;
	empty.present = true;
	QVERIFY(!ChatStreamModel::isDirect(empty));
}

void TestChatStream::filterMatchingMatrix() {
	ChatStreamModel store;
	ChatStreamModel::FilterContext ctx;

	const ChatEntry absent = makeEntry(marker(1), atTime(1, 0), ChatRoutingInfo());
	const ChatEntry direct = makeEntry(marker(2), atTime(1, 1), directRouting(9));
	const ChatEntry chan5  = makeEntry(marker(3), atTime(1, 2), channelRouting(5));
	const ChatEntry chan6  = makeEntry(marker(4), atTime(1, 3), channelRouting(6));

	// All is the unfiltered projection of the store - metadata or not.
	ctx.filter = ChatStreamModel::Filter::All;
	QVERIFY(store.matches(absent, ctx));
	QVERIFY(store.matches(direct, ctx));
	QVERIFY(store.matches(chan5, ctx));

	// Direct is a pure metadata predicate, independent of any topology.
	ctx.filter = ChatStreamModel::Filter::Direct;
	QVERIFY(!store.matches(absent, ctx));
	QVERIFY(store.matches(direct, ctx));
	QVERIFY(!store.matches(chan5, ctx));

	// Selected Channel: id-based matching inside the current epoch, with
	// tree-root coverage resolved against the live topology.
	ctx.filter            = ChatStreamModel::Filter::SelectedChannel;
	ctx.selectedChannelId = 5;
	ctx.currentEpoch      = 1;
	ctx.treeCovers        = coversRootOne();

	QVERIFY(!store.matches(absent, ctx)); // no metadata: no id-based filter
	QVERIFY(!store.matches(direct, ctx)); // directs never match the channel view
	QVERIFY(store.matches(chan5, ctx));
	QVERIFY(!store.matches(chan6, ctx));

	QVERIFY(store.matches(makeEntry(marker(5), atTime(1, 4), treeRouting(1)), ctx));  // root covers 5
	QVERIFY(!store.matches(makeEntry(marker(6), atTime(1, 5), treeRouting(2)), ctx)); // root covers nothing

	// A multi-target message is one logical entry: matching any target
	// matches the entry.
	ChatRoutingInfo multiChannel = channelRouting(6);
	multiChannel.channelIds.append(5);
	QVERIFY(store.matches(makeEntry(marker(7), atTime(1, 6), multiChannel), ctx));

	ChatRoutingInfo treeAndChannel = treeRouting(1);
	treeAndChannel.channelIds.append(7);
	QVERIFY(store.matches(makeEntry(marker(8), atTime(1, 7), treeAndChannel), ctx));

	// A different connection epoch: the ids mean different channels there.
	QVERIFY(!store.matches(makeEntry(marker(9), atTime(1, 8), channelRouting(5, 2)), ctx));

	// No tracked channel: the filter matches nothing.
	ctx.selectedChannelId = 0;
	QVERIFY(!store.matches(chan5, ctx));
}

void TestChatStream::epochScopedSelectionAcrossReconnects() {
	LogConsoleView view;
	ChatStreamModel store;
	ChatStreamPresenter presenter(&view, &store);
	presenter.setEpoch(1);
	presenter.setSelectedChannel(5, coversRootOne());
	presenter.setFilter(ChatStreamModel::Filter::SelectedChannel);

	// Connection 1: two entries for channel 5.
	presenter.appendEntry(makeEntry(marker(1), atTime(1, 0), channelRouting(5, 1)), true);
	presenter.appendEntry(makeEntry(marker(2), atTime(1, 1), channelRouting(5, 1)), true);
	QCOMPARE(docOf(view)->entryCount(), 2);

	// Reconnect: epoch 2, where the same channel id 5 is a DIFFERENT
	// channel. The new entry matches the live view (epoch 2 == presenter
	// epoch) and renders incrementally...
	presenter.setEpoch(2);
	presenter.appendEntry(makeEntry(marker(3), atTime(1, 2), channelRouting(5, 2)), true);
	QCOMPARE(docOf(view)->entryCount(), 3);

	// ...but a rebuild over the whole store drops the epoch-1 entries from
	// the channel view: reused ids can neither leak old messages in nor
	// hide new ones.
	presenter.setEntryCap(0); // no store change; exercises the rebuild path
	QCOMPARE(docOf(view)->entryCount(), 1);
	QVERIFY(!view.document()->toPlainText().contains(marker(1)));
	QVERIFY(!view.document()->toPlainText().contains(marker(2)));
	QVERIFY(view.document()->toPlainText().contains(marker(3)));

	// All is topology-free: the process-local history survives reconnects.
	presenter.setFilter(ChatStreamModel::Filter::All);
	QCOMPARE(docOf(view)->entryCount(), 3);
	QVERIFY(view.document()->toPlainText().contains(marker(1)));
	QVERIFY(view.document()->toPlainText().contains(marker(3)));
}

void TestChatStream::seenFlagsDriveBadges() {
	LogConsoleView view;
	ChatStreamModel store;
	ChatStreamPresenter presenter(&view, &store);
	presenter.setEpoch(1);
	presenter.setSelectedChannel(5, coversRootOne());
	presenter.setFilter(ChatStreamModel::Filter::SelectedChannel);

	// Channel entries arriving while the chat view is visible: displayed,
	// hence seen - no badges anywhere.
	presenter.appendEntry(makeEntry(marker(1), atTime(1, 0), channelRouting(5)), true);
	presenter.appendEntry(makeEntry(marker(2), atTime(1, 1), channelRouting(5)), true);
	QCOMPARE(presenter.chatTabBadge(), 0);
	QCOMPARE(presenter.filterBadge(ChatStreamModel::Filter::All), 0);

	// A private arrives: it does not match the active filter, so it is not
	// displayed - even though the chat view itself is visible.
	presenter.appendEntry(makeEntry(marker(3), atTime(1, 2), directRouting(9)), true);
	QVERIFY(!view.document()->toPlainText().contains(marker(3)));
	// Discoverability is not lost: the topology-free filters' badges grow,
	// derived purely from the store's seen flags.
	QCOMPARE(presenter.filterBadge(ChatStreamModel::Filter::All), 1);
	QCOMPARE(presenter.filterBadge(ChatStreamModel::Filter::Direct), 1);
	QCOMPARE(presenter.filterBadge(ChatStreamModel::Filter::SelectedChannel), 0);
	QCOMPARE(presenter.chatTabBadge(), 1);

	// Re-displaying the ACTIVE filter's content (tab away and back) sees
	// only what is rendered: the hidden private stays unseen.
	presenter.markDisplayed();
	QCOMPARE(presenter.filterBadge(ChatStreamModel::Filter::Direct), 1);

	// Switching to Direct displays it and drains the badge; it matched All
	// too, but being displayed made it seen for every filter.
	presenter.setFilter(ChatStreamModel::Filter::Direct);
	QVERIFY(view.document()->toPlainText().contains(marker(3)));
	QCOMPARE(presenter.filterBadge(ChatStreamModel::Filter::Direct), 0);
	QCOMPARE(presenter.filterBadge(ChatStreamModel::Filter::All), 0);
	QCOMPARE(presenter.chatTabBadge(), 0);

	// An entry arriving under the MATCHING active filter while the view is
	// hidden (Activity tab, minimal mode) is unseen until actually
	// displayed...
	presenter.setFilter(ChatStreamModel::Filter::All);
	presenter.appendEntry(makeEntry(marker(4), atTime(1, 3), directRouting(9)), false);
	QCOMPARE(presenter.chatTabBadge(), 1);
	// ...and showing the view (markDisplayed) drains it.
	presenter.markDisplayed();
	QCOMPARE(presenter.chatTabBadge(), 0);
}

void TestChatStream::lockstepEvictionEquivalence() {
	LogConsoleView view;
	ChatStreamModel store;
	ChatStreamPresenter presenter(&view, &store);
	presenter.setEntryCap(5);

	// Twelve matching entries over three dates (four each), appended live
	// under the All filter.
	for (int i = 0; i < 12; ++i) {
		const int day = 1 + i / 4;
		presenter.appendEntry(makeEntry(marker(i + 1), atTime(day, i), channelRouting(5)), true);
		// Lockstep: the document plateaus at the store cap - it never
		// outgrows the store, so live appending under one filter can never
		// grow the document without bound.
		QVERIFY(docOf(view)->entryCount() <= 5);
		QCOMPARE(store.size(), qMin(i + 1, 5));
	}
	QCOMPARE(docOf(view)->entryCount(), 5);

	const QString liveText = view.document()->toPlainText();
	// The survivors are the newest five; the evicted markers are gone.
	for (int i = 0; i < 7; ++i) {
		QVERIFY(!liveText.contains(marker(i + 1)));
	}
	for (int i = 7; i < 12; ++i) {
		QVERIFY(liveText.contains(marker(i + 1)));
	}

	// Equivalence without a filter switch: rebuilding from the store
	// reproduces the live document's content exactly - including the date
	// separators, whose leading obsolete ones were evicted in lockstep with
	// their entries.
	presenter.setEntryCap(5); // cap unchanged; exercises the rebuild path
	QCOMPARE(view.document()->toPlainText(), liveText);
	QCOMPARE(docOf(view)->entryCount(), 5);

	// A same-date follow-up append inserts no duplicate separator and stays
	// equivalent across another rebuild.
	presenter.appendEntry(makeEntry(marker(13), atTime(3, 30), channelRouting(5)), true);
	const QString liveText2 = view.document()->toPlainText();
	presenter.setEntryCap(5);
	QCOMPARE(view.document()->toPlainText(), liveText2);

	// Non-rendered entries evict without any view work: under Direct none
	// of the channel entries render, yet their eviction must not disturb
	// the view or lose the survivors.
	presenter.setFilter(ChatStreamModel::Filter::Direct);
	presenter.setEntryCap(2);
	QCOMPARE(docOf(view)->entryCount(), 0);
	for (int i = 0; i < 4; ++i) {
		presenter.appendEntry(makeEntry(QString::fromLatin1("chan%1").arg(i), atTime(1, i), channelRouting(5)), true);
	}
	QCOMPARE(docOf(view)->entryCount(), 0);
	QCOMPARE(store.size(), 2);
	presenter.setFilter(ChatStreamModel::Filter::All);
	QCOMPARE(docOf(view)->entryCount(), 2);
}

void TestChatStream::storeAwareClearMatrix() {
	LogConsoleView view;
	ChatStreamModel store;
	ChatStreamPresenter presenter(&view, &store);
	presenter.setEpoch(1);
	presenter.setSelectedChannel(5, coversRootOne());

	// Five entries under All, all displayed: two directs, two channel-5,
	// one channel-6.
	presenter.appendEntry(makeEntry(marker(1), atTime(1, 0), directRouting(9)), true);
	presenter.appendEntry(makeEntry(marker(2), atTime(1, 1), directRouting(9)), true);
	presenter.appendEntry(makeEntry(marker(3), atTime(1, 2), channelRouting(5)), true);
	presenter.appendEntry(makeEntry(marker(4), atTime(1, 3), channelRouting(5)), true);
	presenter.appendEntry(makeEntry(marker(5), atTime(1, 4), channelRouting(6)), true);
	QCOMPARE(store.size(), 5);

	// Clearing under Direct removes only the direct entries; everything
	// hidden behind other filters survives.
	presenter.setFilter(ChatStreamModel::Filter::Direct);
	presenter.clearActiveFilter();
	QCOMPARE(store.size(), 3);
	QVERIFY(!view.document()->toPlainText().contains(marker(1)));

	// The survivors stay reachable through their own filters.
	presenter.setFilter(ChatStreamModel::Filter::All);
	QCOMPARE(docOf(view)->entryCount(), 3);
	QVERIFY(view.document()->toPlainText().contains(marker(3)));
	QVERIFY(view.document()->toPlainText().contains(marker(5)));

	// While Selected Channel is active, an arriving private is not
	// displayed - it survives the channel clear with its unseen badge
	// intact (badges recompute from the store).
	presenter.setFilter(ChatStreamModel::Filter::SelectedChannel);
	presenter.appendEntry(makeEntry(marker(6), atTime(1, 5), directRouting(9)), true);
	QCOMPARE(presenter.filterBadge(ChatStreamModel::Filter::Direct), 1);
	presenter.clearActiveFilter();
	QCOMPARE(store.size(), 2); // the channel-6 entry and the unseen private
	QCOMPARE(presenter.filterBadge(ChatStreamModel::Filter::Direct), 1);

	// Switching to Direct displays the survivor and drains its badge - and
	// only the survivor is there (the cleared directs never reappear).
	presenter.setFilter(ChatStreamModel::Filter::Direct);
	QCOMPARE(docOf(view)->entryCount(), 1);
	QVERIFY(view.document()->toPlainText().contains(marker(6)));
	QVERIFY(!view.document()->toPlainText().contains(marker(1)));
	QCOMPARE(presenter.filterBadge(ChatStreamModel::Filter::Direct), 0);

	// All-clear empties the stream; nothing reappears through any filter.
	presenter.setFilter(ChatStreamModel::Filter::All);
	presenter.clearActiveFilter();
	QCOMPARE(store.size(), 0);
	QCOMPARE(docOf(view)->entryCount(), 0);
	presenter.setFilter(ChatStreamModel::Filter::Direct);
	presenter.setFilter(ChatStreamModel::Filter::All);
	QCOMPARE(docOf(view)->entryCount(), 0);
	QCOMPARE(presenter.chatTabBadge(), 0);
	QCOMPARE(presenter.filterBadge(ChatStreamModel::Filter::All), 0);

	// The empty stream accepts new entries normally.
	presenter.appendEntry(makeEntry(marker(7), atTime(1, 6), directRouting(9)), true);
	QCOMPARE(docOf(view)->entryCount(), 1);
	QCOMPARE(store.size(), 1);
}

void TestChatStream::selectedChannelLifecycle() {
	LogConsoleView view;
	ChatStreamModel store;
	ChatStreamPresenter presenter(&view, &store);
	QSignalSpy stateSpy(&presenter, &ChatStreamPresenter::stateChanged);
	presenter.setEpoch(1);
	presenter.setSelectedChannel(5, coversRootOne());
	presenter.setFilter(ChatStreamModel::Filter::SelectedChannel);

	presenter.appendEntry(makeEntry(marker(1), atTime(1, 0), channelRouting(5)), true);
	presenter.appendEntry(makeEntry(marker(2), atTime(1, 1), directRouting(9)), true);
	QCOMPARE(docOf(view)->entryCount(), 1);
	stateSpy.clear();

	// Losing the tracked channel (removed or disconnected) while the filter
	// is active falls back to All rather than freeze an empty projection.
	presenter.setSelectedChannel(0, {});
	QCOMPARE(presenter.filter(), ChatStreamModel::Filter::All);
	QCOMPARE(presenter.selectedChannel(), 0u);
	QCOMPARE(docOf(view)->entryCount(), 2);
	QVERIFY(view.document()->toPlainText().contains(marker(2)));

	// Re-activating Selected Channel without a selection is refused: the
	// filter would match nothing.
	presenter.setFilter(ChatStreamModel::Filter::SelectedChannel);
	QCOMPARE(presenter.filter(), ChatStreamModel::Filter::All);

	// A new tracked channel re-enables it (rebuild against the new
	// topology).
	presenter.setSelectedChannel(6, coversRootOne());
	presenter.setFilter(ChatStreamModel::Filter::SelectedChannel);
	QCOMPARE(presenter.filter(), ChatStreamModel::Filter::SelectedChannel);
	QCOMPARE(docOf(view)->entryCount(), 0); // no channel-6 entries exist
	stateSpy.clear();

	// Re-setting the SAME id only refreshes the resolver (the live channel
	// objects may have been rebuilt under the same id): no rebuild, no
	// state change.
	presenter.setSelectedChannel(6, coversRootOne());
	QCOMPARE(stateSpy.count(), 0);
	QCOMPARE(presenter.filter(), ChatStreamModel::Filter::SelectedChannel);
}

QTEST_MAIN(TestChatStream)
#include "TestChatStream.moc"

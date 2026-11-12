// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the Mumble
// source tree or at <https://www.mumble.info/LICENSE>.

// Presentation tests of the log console rendering and routing (Stage 5 of
// the new UI). Two families live here: the renderer tests pin LogConsoleView
// (the entry renderer extracted from Log::log()'s former inline insertion
// block) without constructing MainWindow or Global - the widget is
// Global-free by design and takes all per-entry settings through
// RenderSettings; the routing/policy tests drive a real Log through the
// ConsoleSink seam under a temporary-directory Global, observing the Log side
// of the contract with a recording sink (never a window, never TTS or sound:
// the per-type flags are set to console-only unless a slot tests the
// notification paths explicitly).

#include <QtTest>

#include "ConsoleUnreadTracker.h"
#include "Log.h"
#include "Settings.h"
#include "widgets/LogConsoleView.h"
#include "Global.h"

#include <QApplication>
#include <QBuffer>
#include <QImage>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextFrame>
#include <QThread>

/// The ConsoleSink contract, recorded. Pins what Log hands across the seam:
/// the ORIGINAL console HTML (not a plain-text projection), the ownMessage
/// flag, the arrival timestamp, the producer-captured routing metadata, and
/// per-entry visibility queries.
class RecordingSink : public ConsoleSink {
public:
	struct Entry {
		Log::MsgType mt;
		QString console;
		bool ownMessage;
		QDateTime dt;
		ChatRoutingInfo routing;
	};

	QVector< Entry > entries;
	/// Number of isConsoleEntryVisible() consultations (const method, so
	/// the counter is mutable).
	mutable int visibilityQueries = 0;
	/// What isConsoleEntryVisible() answers.
	bool visible = true;

	void appendConsoleEntry(Log::MsgType mt, const QString &console, bool ownMessage, const QDateTime &dt,
							const ChatRoutingInfo &routing) override {
		entries.append({ mt, console, ownMessage, dt, routing });
	}

	bool isConsoleEntryVisible(Log::MsgType) const override {
		++visibilityQueries;
		return visible;
	}
};

/// A real Global over a throwaway base path, assigned to
/// Global::g_global_struct for the duration of one test (main.cpp assigns the
/// pointer manually - the Global constructor never touches it). Global::l
/// stays null: Log instances in these tests are driven directly, so
/// logOrDefer() keeps deferring the way it does before startup finishes.
class GlobalFixture {
public:
	GlobalFixture() {
		m_dir                   = new QTemporaryDir;
		Global::g_global_struct = new Global(m_dir->filePath(QStringLiteral("mumble.ini")));
	}

	~GlobalFixture() {
		delete Global::g_global_struct;
		Global::g_global_struct = nullptr;
		delete m_dir;
	}

private:
	QTemporaryDir *m_dir = nullptr;
};

class TestLogPresentation : public QObject {
	Q_OBJECT

private slots:
	// The view owns its document and the interaction policies the former
	// .ui/init code established, and classifies message destinations by
	// explicit type names (never by enum range arithmetic).
	void viewOwnsDocumentAndPolicies();

	// Parity with the former Log::qdDate seeding: the constructor date is
	// "today", so the very first entry of a session gets no date separator.
	void firstEntryOfSessionHasNoDateSeparator();

	// One child frame per entry; the timestamp prefix reflects the
	// snapshotted clock setting (24h and 12h).
	void framePerEntryWithTimestamp24h();
	void framePerEntryWithTimestamp12h();

	// A date change inserts a separator frame (and only then); returning to
	// an earlier date inserts another one.
	void dateSeparatorInsertedOnDateChange();

	// A rebuild reproduces the live document's leading date separator
	// exactly: eviction leaves (or re-establishes) the separator the
	// projection shows, and the rebuild keeps the view content-identical
	// instead of re-deriving the leading state from the current date.
	void rebuildReproducesLiveLeadingDateSeparator();

	// Store-driven removal is exact: the return value equals the number of
	// message entries removed across date separators, non-rendered
	// evictions are no-ops, and removing every rendered entry empties the
	// document completely.
	void storeDrivenRemovalMatchesEvictedCount();

	// Entry frames carry the snapshotted margin.
	void marginsAppliedFromSettings();

	// Trailing invisible content paints the dashed border; ordinary
	// entries stay borderless.
	void dashedBorderForInvisibleTrailingContent();

	// The rendering path validates HTML: allowed-scheme anchors survive,
	// disallowed-scheme anchors are stripped, data: images load, external
	// images resolve to the 1x1 placeholder instead of being fetched.
	void htmlValidationThroughAppendEntry();

	// Log::validHtml must be callable with no Global/MainWindow alive at
	// all (the guarded screen-geometry lookup).
	void validHtmlWorksWithoutMainWindow();

	// data: images WITHOUT the base64 flag (raw percent-encoded payload)
	// keep decoding: the pre-Stage-5 LogDocument deferred them to Qt's own
	// decoder, and RichTextEditor still renders foreign HTML through a
	// LogDocument - once through the console rendering path, once through a
	// bare document the way RichTextHtmlEdit uses it.
	void nonBase64DataImagesRender();
	void nonBase64DataImagesInRichTextDocument();

	// Scroll preservation: a foreign entry keeps the reader's scroll
	// position; an own entry scrolls to the bottom.
	void scrollRestoredForForeignEntriesOnly();

	// Two views never share documents or content - the structural
	// precondition for the chat/activity split.
	void twoViewsAreIndependent();

	// --- Routing and policy through the ConsoleSink seam (real Log under a
	// temporary Global; the sink is a stub, flags are console-only unless a
	// slot says otherwise) ---

	// Every message type Log defines reaches the sink exactly once per log()
	// call, with the original console HTML and the ownMessage flag intact.
	// The loop deliberately covers PluginMessage, which sits OUTSIDE
	// [firstMsgType, lastMsgType].
	void routingMatrixEveryMsgTypeReachesSinkOnce();

	// A visible destination suppresses highlight and balloon; a hidden one
	// emits each exactly once per entry; own messages never notify. The
	// visibility query itself happens only for received messages.
	void sinkVisibilityGatesHighlightAndBalloon();

	// Without a sink attached (the shutdown window between the MainWindow
	// detach and Log destruction) entries are dropped, and re-registration
	// is immediately effective.
	void nullSinkDropsEntriesAndReregisters();

	// Startup ordering as main.cpp installs it: entries deferred before the
	// Log existed replay through the sink that was registered BEFORE
	// processDeferredLogs() ran.
	void deferredLogsReplayThroughRegisteredSink();

	// The ignore counter consumes entries before the sink ever sees them,
	// per message type, and lets the (counter + 1)-th through.
	void ignoreCounterConsumesEntriesBeforeSink();

	// A type without the console bit never reaches the sink's append; its
	// notification flags still apply on their own (pre-split parity: the
	// console bit gates rendering, never highlight/balloon).
	void consoleDisabledTypeSkipsSinkAppend();

	// Chat routing metadata rides through Log as owned value data: a
	// same-thread log() and a deferred startup logOrDefer both deliver the
	// producer's ChatRoutingInfo copy intact (absent for non-chat entries).
	void routingMetadataReachesSinkAndDeferredLogs();

	// The queued thread-bounce queues a metatype-registered COPY: a
	// producer thread whose ChatRoutingInfo local dies before delivery
	// cannot dangle - the sink receives the metadata intact.
	void queuedDeliveryCarriesOwnedRoutingMetadata();

	// Unread bookkeeping only counts entries the destination document still
	// retains: eviction (and wholesale clears) fold back into the badge
	// count and the unseen-warning latch.
	void unreadTrackerCountsOnlyRetainedEntries();

private:
	/// Processes events until deferred layout work settles.
	void drainEvents() {
		for (int i = 0; i < 4; ++i) {
			QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
		}
	}

	QDateTime at(const QDateTime &base, int daysOffset, int hour) const {
		QDateTime dt = base.addDays(daysOffset);
		QTime t      = dt.time();
		t.setHMS(hour, 5, 6);
		dt.setTime(t);
		return dt;
	}

	LogConsoleView::RenderSettings settings(int margins = 3) const { return { margins, true }; }

	/// A green 4x4 PNG as an RFC 2397 data: URL without the base64 flag
	/// (raw percent-encoded image bytes).
	QUrl nonBase64DataImageUrl() const {
		QImage img(4, 4, QImage::Format_RGB32);
		img.fill(Qt::green);
		QByteArray png;
		QBuffer buf(&png);
		buf.open(QIODevice::WriteOnly);
		img.save(&buf, "PNG");
		return QUrl(QString::fromLatin1("data:image/png,") + QString::fromLatin1(png.toPercentEncoding()));
	}
};

void TestLogPresentation::viewOwnsDocumentAndPolicies() {
	LogConsoleView view;

	QVERIFY(view.document());
	QCOMPARE(view.document()->parent(), &view);
	// The owned document is the log's validating document.
	QVERIFY(qobject_cast< LogDocument * >(view.document()) != nullptr);
	QCOMPARE(view.openLinks(), false);
	QCOMPARE(view.contextMenuPolicy(), Qt::CustomContextMenu);

	// Destination classification: exactly the two text-message types are
	// chat; PluginMessage (outside [firstMsgType, lastMsgType]) is activity.
	QCOMPARE(LogConsoleView::isChatMessageType(Log::TextMessage), true);
	QCOMPARE(LogConsoleView::isChatMessageType(Log::PrivateTextMessage), true);
	QCOMPARE(LogConsoleView::isChatMessageType(Log::Information), false);
	QCOMPARE(LogConsoleView::isChatMessageType(Log::DebugInfo), false);
	QCOMPARE(LogConsoleView::isChatMessageType(Log::CriticalError), false);
	QCOMPARE(LogConsoleView::isChatMessageType(Log::PluginMessage), false);

	// Block-limit forwarding reaches the document's whole-entry budget, and
	// the corrupting Qt mechanism stays disengaged (canary: always 0).
	view.setLogBlockLimit(42);
	const LogDocument *doc = qobject_cast< const LogDocument * >(view.document());
	QVERIFY(doc != nullptr);
	QCOMPARE(doc->blockBudget(), 42);
	QCOMPARE(view.document()->maximumBlockCount(), 0);
}

void TestLogPresentation::firstEntryOfSessionHasNoDateSeparator() {
	LogConsoleView view;

	view.appendEntry(QDateTime::currentDateTime(), QLatin1String("<p>hello</p>"), false, settings());

	// The constructor seeds the entry date with today (former Log::qdDate
	// seeding): the first entry must not grow a "[Date changed]" frame.
	QCOMPARE(view.document()->rootFrame()->childFrames().count(), 1);
	QVERIFY(!view.document()->toPlainText().contains(QLatin1String("Date changed")));
}

void TestLogPresentation::framePerEntryWithTimestamp24h() {
	LogConsoleView view;
	const QDateTime base = QDateTime::currentDateTime();

	view.appendEntry(at(base, 0, 1), QLatin1String("<p>first</p>"), false, settings());
	view.appendEntry(at(base, 0, 23), QLatin1String("<p>second</p>"), false, settings());

	QCOMPARE(view.document()->rootFrame()->childFrames().count(), 2);
	const QString plain = view.document()->toPlainText();
	QVERIFY(plain.contains(QLatin1String("[01:05:06] first")));
	QVERIFY(plain.contains(QLatin1String("[23:05:06] second")));
}

void TestLogPresentation::framePerEntryWithTimestamp12h() {
	LogConsoleView view;
	const QDateTime base = QDateTime::currentDateTime();

	LogConsoleView::RenderSettings s = { 3, false };
	view.appendEntry(at(base, 0, 1), QLatin1String("<p>first</p>"), false, s);
	view.appendEntry(at(base, 0, 13), QLatin1String("<p>afternoon</p>"), false, s);

	const QString plain = view.document()->toPlainText();
	QVERIFY(plain.contains(QLatin1String("[01:05:06 AM] first")));
	QVERIFY(plain.contains(QLatin1String("[01:05:06 PM] afternoon")));
}

void TestLogPresentation::dateSeparatorInsertedOnDateChange() {
	LogConsoleView view;
	const QDateTime base = QDateTime::currentDateTime();

	view.appendEntry(at(base, 0, 1), QLatin1String("<p>today</p>"), false, settings());
	view.appendEntry(at(base, -1, 2), QLatin1String("<p>yesterday</p>"), false, settings());
	view.appendEntry(at(base, 0, 3), QLatin1String("<p>today again</p>"), false, settings());

	// Three entry frames plus two separator frames (today->yesterday and
	// yesterday->today).
	QCOMPARE(view.document()->rootFrame()->childFrames().count(), 5);

	// The separator frames carry the localized date of the changed-to date
	// (the trailing \n of the source string is HTML-collapsed on insert, so
	// the plain text carries the visible prefix + date).
	const QString yesterday = QLocale().toString(at(base, -1, 2).date(), QLocale::ShortFormat);
	const QString today     = QLocale().toString(base.date(), QLocale::ShortFormat);
	const QString plain     = view.document()->toPlainText();
	QVERIFY(plain.contains(QString::fromLatin1("[Date changed to %1]").arg(yesterday)));
	QVERIFY(plain.contains(QString::fromLatin1("[Date changed to %1]").arg(today)));
}

void TestLogPresentation::rebuildReproducesLiveLeadingDateSeparator() {
	LogConsoleView view;
	const QDateTime base = QDateTime::currentDateTime();

	// The leading frame's role tag (a DateFrameRole lead is the projection
	// rule's leading separator).
	const auto leadingRoleOf = [](LogConsoleView &v) {
		return v.document()->rootFrame()->childFrames().constFirst()->frameFormat().intProperty(
			LogDocument::FrameRoleProperty);
	};
	const auto hasLeadingSeparator = [&leadingRoleOf](LogConsoleView &v) {
		return leadingRoleOf(v) == static_cast< int >(LogDocument::DateFrameRole);
	};

	// Yesterday's entries followed by today's: the live view carries a
	// leading separator for yesterday and a date-change separator for
	// today.
	view.appendEntry(at(base, -1, 1), QLatin1String("<p>old1</p>"), false, settings());
	view.appendEntry(at(base, -1, 2), QLatin1String("<p>old2</p>"), false, settings());
	view.appendEntry(at(base, 0, 3), QLatin1String("<p>new1</p>"), false, settings());
	view.appendEntry(at(base, 0, 4), QLatin1String("<p>new2</p>"), false, settings());

	// Eviction of the leading entries (what the chat store drives through
	// removeLeadingRenderedEntries in lockstep): the obsolete [yesterday]
	// separator goes with its entries, the [today] one stays and becomes
	// leading.
	QCOMPARE(view.removeLeadingRenderedEntries(2), 2);
	const QString liveText = view.document()->toPlainText();
	QVERIFY(!liveText.contains(QLatin1String("old1")));
	QVERIFY(!liveText.contains(QLatin1String("old2")));
	QVERIFY(hasLeadingSeparator(view));

	// A rebuild from the same retained sources must reproduce the live
	// document exactly - including the leading [today] separator: the
	// live view converged to it through the evictions, and the rebuild
	// carries the replaced document's leading state over.
	const QVector< LogConsoleView::EntrySource > sources = {
		{ at(base, 0, 3), QLatin1String("<p>new1</p>"), false, settings() },
		{ at(base, 0, 4), QLatin1String("<p>new2</p>"), false, settings() }
	};
	view.rebuild(sources);
	QCOMPARE(view.document()->toPlainText(), liveText);
	QVERIFY(hasLeadingSeparator(view));

	// Rebuilding twice stays stable: no separator is lost or invented.
	view.rebuild(sources);
	QCOMPARE(view.document()->toPlainText(), liveText);

	// The same-day convergence rule: all-today entries start WITHOUT a
	// leading separator (the session inherits the construction date), but
	// once eviction consumes the oldest entry's same-date predecessors, the
	// live view re-inserts the leading separator - the shape a rebuild of
	// the same sources reproduces.
	LogConsoleView view2;
	view2.appendEntry(at(base, 0, 5), QLatin1String("<p>a</p>"), false, settings());
	view2.appendEntry(at(base, 0, 6), QLatin1String("<p>b</p>"), false, settings());
	view2.appendEntry(at(base, 0, 7), QLatin1String("<p>c</p>"), false, settings());
	QVERIFY(!hasLeadingSeparator(view2));
	QCOMPARE(view2.removeLeadingRenderedEntries(1), 1);
	const QString liveText2 = view2.document()->toPlainText();
	QVERIFY(hasLeadingSeparator(view2));
	const QVector< LogConsoleView::EntrySource > sources2 = {
		{ at(base, 0, 6), QLatin1String("<p>b</p>"), false, settings() },
		{ at(base, 0, 7), QLatin1String("<p>c</p>"), false, settings() }
	};
	view2.rebuild(sources2);
	QCOMPARE(view2.document()->toPlainText(), liveText2);
	QVERIFY(hasLeadingSeparator(view2));

	// Removing every rendered entry empties the document completely (no
	// ghost message, no orphan separator) and resets the date state: the
	// next live append re-establishes the leading separator.
	QCOMPARE(view2.removeLeadingRenderedEntries(2), 2);
	QCOMPARE(view2.document()->rootFrame()->childFrames().count(), 0);
	QCOMPARE(static_cast< LogDocument * >(view2.document())->entryCount(), 0);
	view2.appendEntry(at(base, 0, 8), QLatin1String("<p>d</p>"), false, settings());
	QCOMPARE(static_cast< LogDocument * >(view2.document())->entryCount(), 1);
	QVERIFY(hasLeadingSeparator(view2));
}

void TestLogPresentation::storeDrivenRemovalMatchesEvictedCount() {
	LogConsoleView view;
	const QDateTime base = QDateTime::currentDateTime();

	// Three entries over two dates (the middle one crosses midnight):
	// [M1(today)][Date(tomorrow)][M2][M3].
	view.appendEntry(at(base, 0, 1), QLatin1String("<p>e1</p>"), false, settings());
	view.appendEntry(at(base, 1, 2), QLatin1String("<p>e2</p>"), false, settings());
	view.appendEntry(at(base, 1, 3), QLatin1String("<p>e3</p>"), false, settings());
	QCOMPARE(static_cast< LogDocument * >(view.document())->entryCount(), 3);

	// Removing one message entry reports exactly one (a date separator
	// frame that may go alongside never counts: e1 was appended same-day
	// as the session seed, so its removal involves no separator at all).
	QCOMPARE(view.removeLeadingRenderedEntries(1), 1);
	QCOMPARE(static_cast< LogDocument * >(view.document())->entryCount(), 2);
	QVERIFY(!view.document()->toPlainText().contains(QLatin1String("e1")));

	// Asking for more than present removes what exists - exactly two.
	QCOMPARE(view.removeLeadingRenderedEntries(5), 2);
	QCOMPARE(static_cast< LogDocument * >(view.document())->entryCount(), 0);
	QCOMPARE(view.document()->rootFrame()->childFrames().count(), 0);
	// A removal on the emptied view is a no-op.
	QCOMPARE(view.removeLeadingRenderedEntries(1), 0);
}

void TestLogPresentation::marginsAppliedFromSettings() {
	LogConsoleView view;
	const QDateTime now = QDateTime::currentDateTime();

	view.appendEntry(now, QLatin1String("<p>one</p>"), false, settings(5));
	view.appendEntry(now, QLatin1String("<p>two</p>"), false, settings(5));

	const QList< QTextFrame * > frames = view.document()->rootFrame()->childFrames();
	QCOMPARE(frames.count(), 2);
	for (const QTextFrame *frame : frames) {
		QCOMPARE(frame->frameFormat().topMargin(), 0.0);
		QCOMPARE(frame->frameFormat().bottomMargin(), 5.0);
	}
}

void TestLogPresentation::dashedBorderForInvisibleTrailingContent() {
	LogConsoleView view;
	const QDateTime now = QDateTime::currentDateTime();

	// Ordinary entry: no border.
	view.appendEntry(now, QLatin1String("<p>clean</p>"), false, settings());
	// Entry whose plain text ends with a blank line: dashed border.
	view.appendEntry(now, QLatin1String("<p>tricky</p><p> </p>"), false, settings());

	const QList< QTextFrame * > frames = view.document()->rootFrame()->childFrames();
	QCOMPARE(frames.count(), 2);
	QCOMPARE(frames.at(0)->frameFormat().border(), 0.0);
	QCOMPARE(frames.at(1)->frameFormat().border(), 1.0);
	QCOMPARE(frames.at(1)->frameFormat().borderStyle(), QTextFrameFormat::BorderStyle_Dashed);
	QCOMPARE(frames.at(1)->frameFormat().padding(), 2.0);
}

void TestLogPresentation::htmlValidationThroughAppendEntry() {
	LogConsoleView view;
	const QDateTime now = QDateTime::currentDateTime();

	// A PNG data URL image, encoded exactly like pasted chat images.
	QImage img(4, 4, QImage::Format_RGB32);
	img.fill(Qt::red);
	QByteArray png;
	QBuffer buf(&png);
	buf.open(QIODevice::WriteOnly);
	img.save(&buf, "PNG");
	const QString imageHtml = Log::imageToImg(QByteArray("png"), png);

	const QString html = QString::fromLatin1("<p><a href='https://example.invalid/ok'>safe link</a>"
											 "<a href='javascript:evil()'>stripped link</a></p>")
						 + imageHtml + QLatin1String("<img src=\"http://example.invalid/ext.png\" />");

	view.appendEntry(now, html, false, settings());

	const QString plain = view.document()->toPlainText();
	// The allowed-scheme anchor text survives...
	QVERIFY(plain.contains(QLatin1String("safe link")));
	// ...the disallowed-scheme anchor text survives as plain text but must
	// not be an anchor anymore.
	bool strippedIsAnchor = false;
	for (QTextBlock block = view.document()->begin(); block.isValid(); block = block.next()) {
		for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
			const QTextFragment fragment = it.fragment();
			if (fragment.text().contains(QLatin1String("stripped link"))
				&& !fragment.charFormat().anchorHref().isEmpty()) {
				strippedIsAnchor = true;
			}
		}
	}
	QVERIFY(!strippedIsAnchor);

	// The data: image round-trips at full size through the document. The
	// base loadResource hands out the raw bytes (the layout decodes them),
	// so decode generically before asserting the geometry.
	QRegularExpression srcExpr(QLatin1String("src=\"(data:image/png[^\"]*)\""));
	QRegularExpressionMatch match = srcExpr.match(imageHtml);
	QVERIFY(match.hasMatch());
	const QVariant dataResource = view.document()->resource(QTextDocument::ImageResource, QUrl(match.captured(1)));
	QImage dataImage            = qvariant_cast< QImage >(dataResource);
	if (dataImage.isNull()) {
		dataImage = QImage::fromData(dataResource.toByteArray());
	}
	QCOMPARE(dataImage.width(), 4);
	QCOMPARE(dataImage.height(), 4);

	// The external image resolves to the 1x1 placeholder - it is never
	// fetched. LogDocument::loadResource caches the placeholder QImage
	// itself, so this one arrives as an image.
	const QVariant externalResource =
		view.document()->resource(QTextDocument::ImageResource, QUrl(QLatin1String("http://example.invalid/ext.png")));
	QImage externalImage = qvariant_cast< QImage >(externalResource);
	QCOMPARE(externalImage.width(), 1);
	QCOMPARE(externalImage.height(), 1);
}

void TestLogPresentation::validHtmlWorksWithoutMainWindow() {
	// No Global was constructed in this test process at all: the guarded
	// screen-geometry lookup must fall back to the primary screen.
	QVERIFY(Global::g_global_struct == nullptr);
	const QString result = Log::validHtml(QLatin1String("<p><b>bold</b> text</p>"));
	QVERIFY(!result.isEmpty());
	QVERIFY(result.contains(QLatin1String("bold")));
}

void TestLogPresentation::nonBase64DataImagesRender() {
	LogConsoleView view;
	const QUrl url = nonBase64DataImageUrl();

	view.appendEntry(QDateTime::currentDateTime(),
					 QString::fromLatin1("<p>raw <img src=\"%1\" /></p>").arg(url.toString()), false, settings());

	// Full-size decode, not the external-image 1x1 placeholder (and not a
	// broken/empty resource).
	const QVariant resource = view.document()->resource(QTextDocument::ImageResource, url);
	QImage decoded          = qvariant_cast< QImage >(resource);
	if (decoded.isNull()) {
		decoded = QImage::fromData(resource.toByteArray());
	}
	QCOMPARE(decoded.width(), 4);
	QCOMPARE(decoded.height(), 4);
}

void TestLogPresentation::nonBase64DataImagesInRichTextDocument() {
	// RichTextHtmlEdit owns a bare LogDocument and renders arbitrary pasted
	// HTML through it; the URL family it can encounter is the same.
	const QUrl url = nonBase64DataImageUrl();
	LogDocument document;
	document.setHtml(QString::fromLatin1("<p><img src=\"%1\" /></p>").arg(url.toString()));

	const QVariant resource = document.resource(QTextDocument::ImageResource, url);
	QImage decoded          = qvariant_cast< QImage >(resource);
	if (decoded.isNull()) {
		decoded = QImage::fromData(resource.toByteArray());
	}
	QCOMPARE(decoded.width(), 4);
	QCOMPARE(decoded.height(), 4);
}

void TestLogPresentation::scrollRestoredForForeignEntriesOnly() {
	LogConsoleView view;
	view.resize(300, 120);
	view.show();
	drainEvents();

	// Enough tall entries to give the scroll bar a real range.
	for (int i = 0; i < 30; ++i) {
		view.appendEntry(QDateTime::currentDateTime(), QString::fromLatin1("<p>line %1 a</p><p>b</p><p>c</p>").arg(i),
						 false, settings());
	}
	drainEvents();
	QVERIFY(view.verticalScrollBar()->maximum() > 0);

	// The reader scrolls up...
	view.setLogScroll(0);
	drainEvents();

	// ...a foreign (received) entry must not move them.
	view.appendEntry(QDateTime::currentDateTime(), QLatin1String("<p>foreign marker</p>"), false, settings());
	drainEvents();
	QCOMPARE(view.getLogScroll(), 0);

	// ...an own (sent) entry scrolls to the bottom.
	view.appendEntry(QDateTime::currentDateTime(), QLatin1String("<p>own marker</p>"), true, settings());
	drainEvents();
	QCOMPARE(view.getLogScroll(), view.verticalScrollBar()->maximum());
	QVERIFY(view.document()->toPlainText().contains(QLatin1String("own marker")));
}

void TestLogPresentation::twoViewsAreIndependent() {
	LogConsoleView a;
	LogConsoleView b;

	a.appendEntry(QDateTime::currentDateTime(), QLatin1String("<p>in a</p>"), false, settings());
	b.appendEntry(QDateTime::currentDateTime(), QLatin1String("<p>in b</p>"), false, settings());

	QVERIFY(a.document() != b.document());
	QVERIFY(a.document()->toPlainText().contains(QLatin1String("in a")));
	QVERIFY(!a.document()->toPlainText().contains(QLatin1String("in b")));
	QVERIFY(b.document()->toPlainText().contains(QLatin1String("in b")));
	QVERIFY(!b.document()->toPlainText().contains(QLatin1String("in a")));
}

void TestLogPresentation::routingMatrixEveryMsgTypeReachesSinkOnce() {
	GlobalFixture fixture;
	RecordingSink sink;
	Log log;
	log.setConsoleSink(&sink);

	// The whole declared enum, one past lastMsgType: PluginMessage (enum
	// order's last member) is deliberately outside the [firstMsgType,
	// lastMsgType] range that a display-order loop would have covered, and
	// must be routed like any other type.
	const QDateTime before = QDateTime::currentDateTime();
	for (int mt = Log::DebugInfo; mt <= Log::PluginMessage; ++mt) {
		const Log::MsgType type = static_cast< Log::MsgType >(mt);
		const QString console   = QString::fromLatin1("<p>body %1</p>").arg(mt);
		Global::get().s.qmMessages.insert(mt, Settings::LogConsole);

		sink.entries.clear();
		log.log(type, console);

		QCOMPARE(sink.entries.count(), 1);
		QCOMPARE(sink.entries.at(0).mt, type);
		// The sink receives the original console HTML; any projection
		// happens on the sink side.
		QCOMPARE(sink.entries.at(0).console, console);
		QCOMPARE(sink.entries.at(0).ownMessage, false);
		QVERIFY(sink.entries.at(0).dt >= before);
		QVERIFY(sink.entries.at(0).dt <= QDateTime::currentDateTime());
	}
}

void TestLogPresentation::sinkVisibilityGatesHighlightAndBalloon() {
	GlobalFixture fixture;
	RecordingSink sink;
	Log log;
	log.setConsoleSink(&sink);

	const Log::MsgType mt = Log::Warning;
	Global::get().s.qmMessages.insert(mt, Settings::LogConsole | Settings::LogHighlight | Settings::LogBalloon);

	QSignalSpy highlightSpy(&log, &Log::highlightSpawned);
	QSignalSpy balloonSpy(&log, &Log::notificationSpawned);

	// Visible destination: the entry renders, nothing notifies.
	sink.visible = true;
	log.log(mt, QLatin1String("<p>seen</p>"));
	QCOMPARE(sink.entries.count(), 1);
	QCOMPARE(highlightSpy.count(), 0);
	QCOMPARE(balloonSpy.count(), 0);
	QCOMPARE(sink.visibilityQueries, 1);

	// Hidden destination: exactly one highlight and one balloon per entry.
	sink.visible = false;
	log.log(mt, QLatin1String("<p>unseen</p>"));
	QCOMPARE(sink.entries.count(), 2);
	QCOMPARE(highlightSpy.count(), 1);
	QCOMPARE(balloonSpy.count(), 1);
	QCOMPARE(sink.visibilityQueries, 2);
	// The balloon carries the type's display name and the entry's plain
	// text, with the warning icon for warning-severity types.
	QCOMPARE(balloonSpy.at(0).at(0).toString(), log.msgName(mt));
	QCOMPARE(balloonSpy.at(0).at(1).toString(), QLatin1String("unseen"));
	QCOMPARE(balloonSpy.at(0).at(2).value< QSystemTrayIcon::MessageIcon >(), QSystemTrayIcon::Warning);

	// Own messages never notify, whatever the visibility - and never even
	// query it.
	log.log(mt, QLatin1String("<p>own</p>"), QString(), true);
	QCOMPARE(sink.entries.count(), 3);
	QCOMPARE(highlightSpy.count(), 1);
	QCOMPARE(balloonSpy.count(), 1);
	QCOMPARE(sink.visibilityQueries, 2);
}

void TestLogPresentation::nullSinkDropsEntriesAndReregisters() {
	GlobalFixture fixture;
	RecordingSink sink;
	Log log;
	Global::get().s.qmMessages.insert(Log::Information, Settings::LogConsole);

	// No sink attached: the entry is dropped without touching anything
	// (the shutdown window between the MainWindow detach and Log's own
	// destruction).
	log.log(Log::Information, QLatin1String("<p>lost</p>"));
	QCOMPARE(sink.entries.count(), 0);

	// Registration is repeatable and immediately effective.
	log.setConsoleSink(&sink);
	log.log(Log::Information, QLatin1String("<p>found</p>"));
	QCOMPARE(sink.entries.count(), 1);
	QCOMPARE(sink.entries.at(0).console, QLatin1String("<p>found</p>"));
}

void TestLogPresentation::deferredLogsReplayThroughRegisteredSink() {
	GlobalFixture fixture;
	RecordingSink sink;

	// Global::l is null in this fixture: logOrDefer defers, exactly like
	// startup logs produced before main.cpp created the Log.
	Log::logOrDefer(Log::ServerConnected, QLatin1String("<p>early</p>"));
	Log::logOrDefer(Log::Information, QLatin1String("<p>boot</p>"));

	// Then the Log exists, the sink is registered BEFORE the deferred logs
	// are replayed (main.cpp's ordering), so the backlog routes through the
	// live sink in arrival order.
	Log log;
	log.setConsoleSink(&sink);
	Global::get().s.qmMessages.insert(Log::ServerConnected, Settings::LogConsole);
	Global::get().s.qmMessages.insert(Log::Information, Settings::LogConsole);
	log.processDeferredLogs();

	QCOMPARE(sink.entries.count(), 2);
	QCOMPARE(sink.entries.at(0).mt, Log::ServerConnected);
	QCOMPARE(sink.entries.at(0).console, QLatin1String("<p>early</p>"));
	QCOMPARE(sink.entries.at(1).mt, Log::Information);
	QCOMPARE(sink.entries.at(1).console, QLatin1String("<p>boot</p>"));
}

void TestLogPresentation::ignoreCounterConsumesEntriesBeforeSink() {
	GlobalFixture fixture;
	RecordingSink sink;
	Log log;
	log.setConsoleSink(&sink);
	Global::get().s.qmMessages.insert(Log::TextMessage, Settings::LogConsole);
	Global::get().s.qmMessages.insert(Log::PrivateTextMessage, Settings::LogConsole);

	// Two entries are consumed by the counter (2 -> 1 -> 0)...
	log.setIgnore(Log::TextMessage, 2);
	log.log(Log::TextMessage, QLatin1String("<p>one</p>"));
	log.log(Log::TextMessage, QLatin1String("<p>two</p>"));
	QCOMPARE(sink.entries.count(), 0);

	// ...the counter is per message type: other types pass while it runs...
	log.log(Log::PrivateTextMessage, QLatin1String("<p>side</p>"));
	QCOMPARE(sink.entries.count(), 1);

	// ...and once exhausted, the type's entries pass again.
	log.log(Log::TextMessage, QLatin1String("<p>three</p>"));
	QCOMPARE(sink.entries.count(), 2);
	QCOMPARE(sink.entries.at(0).console, QLatin1String("<p>side</p>"));
	QCOMPARE(sink.entries.at(1).console, QLatin1String("<p>three</p>"));
}

void TestLogPresentation::consoleDisabledTypeSkipsSinkAppend() {
	GlobalFixture fixture;
	RecordingSink sink;
	Log log;
	log.setConsoleSink(&sink);

	// Console bit off, notification bits on.
	Global::get().s.qmMessages.insert(Log::TextMessage, Settings::LogHighlight);
	QSignalSpy highlightSpy(&log, &Log::highlightSpawned);

	// Hidden destination: no append ever happens...
	sink.visible = false;
	log.log(Log::TextMessage, QLatin1String("<p>silent</p>"));
	QCOMPARE(sink.entries.count(), 0);

	// ...but the notification decision is independent of the console bit
	// (pre-split parity: LogConsole gates rendering, never highlight).
	QCOMPARE(highlightSpy.count(), 1);
}

void TestLogPresentation::routingMetadataReachesSinkAndDeferredLogs() {
	GlobalFixture fixture;
	RecordingSink sink;
	Log log;
	log.setConsoleSink(&sink);
	Global::get().s.qmMessages.insert(Log::Information, Settings::LogConsole);
	Global::get().s.qmMessages.insert(Log::PrivateTextMessage, Settings::LogConsole);
	Global::get().s.qmMessages.insert(Log::TextMessage, Settings::LogConsole);

	// Non-chat entries carry no routing metadata at all.
	log.log(Log::Information, QLatin1String("<p>plain</p>"));
	QCOMPARE(sink.entries.count(), 1);
	QVERIFY(!sink.entries.at(0).routing.present);

	// A chat entry's producer-captured metadata crosses the seam intact:
	// every field, value data end to end.
	ChatRoutingInfo routing;
	routing.present        = true;
	routing.senderSession  = 42;
	routing.senderName     = QStringLiteral("alice");
	routing.directSessions = { 7u, 8u };
	routing.channelIds     = { 5u, 6u };
	routing.treeIds        = { 1u };
	routing.epoch          = 99;

	log.log(Log::PrivateTextMessage, QLatin1String("<p>direct</p>"), QString(), false, QString(), false, routing);
	QCOMPARE(sink.entries.count(), 2);
	const ChatRoutingInfo &delivered = sink.entries.at(1).routing;
	QCOMPARE(delivered.present, true);
	QCOMPARE(delivered.senderSession, 42u);
	QCOMPARE(delivered.senderName, QStringLiteral("alice"));
	QCOMPARE(delivered.directSessions, QVector< unsigned int >() << 7u << 8u);
	QCOMPARE(delivered.channelIds, QVector< unsigned int >() << 5u << 6u);
	QCOMPARE(delivered.treeIds, QVector< unsigned int >() << 1u);
	QCOMPARE(delivered.epoch, quint64(99));

	// Deferred startup logs hold their own copy: Global::l is null in this
	// fixture, so logOrDefer queues exactly like the pre-startup logs in
	// main.cpp.
	Log::logOrDefer(Log::TextMessage, QLatin1String("<p>early chat</p>"), QString(), false, QString(), false, routing);
	log.processDeferredLogs();
	QCOMPARE(sink.entries.count(), 3);
	QCOMPARE(sink.entries.at(2).routing.present, true);
	QCOMPARE(sink.entries.at(2).routing.senderSession, 42u);
	QCOMPARE(sink.entries.at(2).routing.epoch, quint64(99));
}

void TestLogPresentation::queuedDeliveryCarriesOwnedRoutingMetadata() {
	GlobalFixture fixture;
	RecordingSink sink;
	Log log;
	log.setConsoleSink(&sink);
	Global::get().s.qmMessages.insert(Log::PrivateTextMessage, Settings::LogConsole);

	// Produced on another thread: log() queues the cross-thread bounce
	// with a metatype-registered COPY of the routing metadata. The
	// producer's local dies with the thread before the main thread
	// delivers - nothing in the metadata path is a pointer.
	QThread *producer = QThread::create([&log]() {
		ChatRoutingInfo local;
		local.present        = true;
		local.senderSession  = 11;
		local.senderName     = QStringLiteral("bob");
		local.directSessions = { 4u };
		local.epoch          = 7;

		log.log(Log::PrivateTextMessage, QLatin1String("<p>cross-thread</p>"), QString(), false, QString(), false,
				local);
	});
	producer->start();
	producer->wait();
	delete producer;
	drainEvents();

	QCOMPARE(sink.entries.count(), 1);
	const ChatRoutingInfo &delivered = sink.entries.at(0).routing;
	QCOMPARE(delivered.present, true);
	QCOMPARE(delivered.senderSession, 11u);
	QCOMPARE(delivered.senderName, QStringLiteral("bob"));
	QCOMPARE(delivered.directSessions, QVector< unsigned int >() << 4u);
	QCOMPARE(delivered.epoch, quint64(7));
	QCOMPARE(sink.entries.at(0).console, QLatin1String("<p>cross-thread</p>"));
}

void TestLogPresentation::unreadTrackerCountsOnlyRetainedEntries() {
	ConsoleUnreadTracker tracker;

	// Nothing unseen: no count, no warning latch.
	QCOMPARE(tracker.unreadCount(), 0);
	QVERIFY(!tracker.hasUnseenWarnings());

	// Entries arriving while the destination is hidden: count and warning
	// latch grow.
	tracker.append(Log::Information);
	tracker.append(Log::Warning);
	tracker.append(Log::TextMessage);
	QCOMPARE(tracker.unreadCount(), 3);
	QVERIFY(tracker.hasUnseenWarnings());

	// An eviction that only removes entries seen before the hidden batch
	// (the document still holds more entries than were unseen) changes
	// nothing.
	tracker.retainOnly(5);
	QCOMPARE(tracker.unreadCount(), 3);
	QVERIFY(tracker.hasUnseenWarnings());

	// An eviction that eats into the batch: the oldest unseen entries -
	// including the warning - are gone; count drops and the latch
	// recomputes from what survives.
	tracker.retainOnly(1);
	QCOMPARE(tracker.unreadCount(), 1);
	QVERIFY(!tracker.hasUnseenWarnings());

	// Full eviction (or a wholesale clear): the badge cannot outlive the
	// entries it counted.
	tracker.retainOnly(0);
	QCOMPARE(tracker.unreadCount(), 0);
	QVERIFY(!tracker.hasUnseenWarnings());

	// Showing the destination clears the batch; later evictions stay
	// no-ops.
	tracker.append(Log::CriticalError);
	tracker.clear();
	QCOMPARE(tracker.unreadCount(), 0);
	QVERIFY(!tracker.hasUnseenWarnings());
	tracker.retainOnly(0);
	QCOMPARE(tracker.unreadCount(), 0);
}

QTEST_MAIN(TestLogPresentation)
#include "TestLogPresentation.moc"

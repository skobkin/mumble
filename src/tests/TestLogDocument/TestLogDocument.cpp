// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the Mumble
// source tree or at <https://www.mumble.info/LICENSE>.

// Whole-entry block-budget tests for LogDocument/LogConsoleView (Stage 5 of
// the new UI). Qt's setMaximumBlockCount is documented as undefined for
// frame-based documents; on this Qt it trims at most once (later inserts grow
// the document unchecked past the limit) and, once the budget nears the
// frame-structure cost, that one-time trim wipes the entire document. These
// tests first record both failure modes as the development gate that
// justifies the replacement, then pin the whole-entry budget: oldest-entry
// trimming, date-separator-aware eviction, image-resource lifecycle (release,
// rehydration, key-budget compaction) and wholesale clears.

#include <QtTest>

#include "Log.h"
#include "widgets/LogConsoleView.h"

#include <QApplication>
#include <QBuffer>
#include <QColor>
#include <QImage>
#include <QLocale>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>

class TestLogDocument : public QObject {
	Q_OBJECT

private slots:
	// Development gate: the NATIVE block-granular limit is unusable for
	// frame-based documents - it trims at most once, and low budgets wipe
	// the document wholesale. Justifies never calling setMaximumBlockCount
	// again.
	void nativeMaximumBlockCountCorruptsFrames();

	// Budget <= 0 never trims anything (the former unlimited growth).
	void budgetZeroNeverTrims();

	// Trimming removes the OLDEST complete entries until the budget is met:
	// block and entry counts plateau, retained content stays intact, and
	// whole markers survive a hundred further appends (canary).
	void wholeOldestEntryTrimmingPlateaus();

	// A unique image's bytes are released when its entry is trimmed; the
	// retained entries' images keep their full size.
	void evictedImagesReleased();

	// An entry re-referencing a previously evicted data: image re-decodes it
	// at full size - resource() alone would keep serving the cached
	// placeholder forever. Repeated evict/re-reference cycles.
	void rehydrationCycles();

	// Date-separator-aware eviction across the midnight boundary: an
	// obsolete leading separator goes with its messages; a separator still
	// labeling retained messages survives; the follow-up same-date append
	// adds no duplicate separator; entryCount() never counts date frames.
	void dateAwareEvictionCrossingMidnight();

	// Stale resource-cache keys (URL strings Qt keeps forever) are bounded:
	// past the key budget the view compacts into a fresh document with
	// identical content.
	void compactionPreservesContent();
	void compactionAutoTriggersOnStaleKeys();

	// clearConsole() releases everything: content, resource cache and
	// eviction bookkeeping die with the swapped-out document.
	void clearReleasesEverything();

private:
	// LogConsoleView always owns a LogDocument (constructor, clearConsole
	// and compact all create one), so the cast cannot fail.
	static LogDocument *docOf(LogConsoleView &view) { return static_cast< LogDocument * >(view.document()); }

	static void appendMarked(LogConsoleView &view, const QString &marker) {
		view.appendEntry(QDateTime::currentDateTime(),
						 QString::fromLatin1("<p>%1 some entry content text</p>").arg(marker), false,
						 LogConsoleView::RenderSettings{ 3, true });
	}

	/// PNG data-URL image tag whose bytes are unique per \p seed
	/// (deterministic for a given seed).
	static QString uniqueImageHtml(int seed) {
		QImage img(4, 4, QImage::Format_RGB32);
		img.fill(QColor((seed * 37) % 255, (seed * 71) % 255, (seed * 113) % 255));
		QByteArray png;
		QBuffer buf(&png);
		buf.open(QIODevice::WriteOnly);
		img.save(&buf, "PNG");
		return Log::imageToImg(QByteArray("png"), png);
	}

	static QUrl imageUrlOf(const QString &imageHtml) {
		static const QRegularExpression expr(QLatin1String("src=\"(data:image/png[^\"]*)\""));
		const QRegularExpressionMatch match = expr.match(imageHtml);
		if (!match.hasMatch()) {
			return QUrl();
		}
		return QUrl(match.captured(1));
	}

	static QImage resourceImage(LogDocument *doc, const QUrl &name) {
		return qvariant_cast< QImage >(doc->resource(QTextDocument::ImageResource, name));
	}

	/// The concatenated text of all blocks inside \p frame. (Frame delimiter
	/// characters are invisible to QTextBlock::text(), so they contribute
	/// nothing here.)
	static QString frameText(const QTextFrame *frame) {
		QString text;
		for (QTextFrame::iterator it = frame->begin(); !it.atEnd(); ++it) {
			const QTextBlock block = it.currentBlock();
			if (block.isValid()) {
				text += block.text();
			}
		}
		return text;
	}

	/// Message-frame integrity: every message frame holds exactly one whole
	/// marker from \p expectedMarkers (a frame cut in half would hold zero or
	/// a fragment; merged frames would hold two), date frames hold none, and
	/// the retained markers are exactly the expected ones. Delimiter
	/// characters never surface in QTextBlock::text(), so whole-marker
	/// presence - not delimiter counting - is the observable integrity
	/// signal.
	static void verifyRetainedMarkers(LogDocument *doc, const QStringList &expectedMarkers) {
		QStringList found;
		const QList< QTextFrame * > frames = doc->rootFrame()->childFrames();
		for (const QTextFrame *frame : frames) {
			const bool isMessage =
				frame->frameFormat().intProperty(LogDocument::FrameRoleProperty) == LogDocument::MessageFrameRole;
			const QString text = frameText(frame);
			int matches        = 0;
			for (const QString &marker : expectedMarkers) {
				matches += static_cast< int >(text.count(marker));
			}
			if (isMessage) {
				QVERIFY2(matches == 1, qPrintable(QString::fromLatin1("frame text '%1' does not hold "
																	  "exactly one expected marker")
													  .arg(text)));
				for (const QString &marker : expectedMarkers) {
					if (text.contains(marker)) {
						found << marker;
					}
				}
			} else {
				QCOMPARE(matches, 0);
			}
		}
		QStringList expected = expectedMarkers;
		expected.sort();
		found.sort();
		QCOMPARE(found, expected);
	}

	/// Appends uniform frame-based entries the way LogConsoleView does (for
	/// the native setMaximumBlockCount development gate).
	static void appendFrameEntries(QTextDocument *doc, int firstIndex, int count) {
		for (int i = firstIndex; i < firstIndex + count; ++i) {
			QTextCursor tc(doc);
			tc.movePosition(QTextCursor::End);
			QTextFrameFormat fmt;
			fmt.setBottomMargin(3);
			tc.insertFrame(fmt);
			tc.insertText(QString::fromLatin1("entry %1 content text").arg(i));
		}
	}

	/// The date frames among the root's child frames, in order.
	static QList< const QTextFrame * > dateFrames(const QTextDocument *doc) {
		QList< const QTextFrame * > frames;
		for (const QTextFrame *frame : doc->rootFrame()->childFrames()) {
			if (frame->frameFormat().intProperty(LogDocument::FrameRoleProperty) == LogDocument::DateFrameRole) {
				frames.append(frame);
			}
		}
		return frames;
	}

	/// Walks the block budget down one block at a time until exactly
	/// \p target entries remain. The exact budget value is irrelevant for
	/// the structural assertions made afterwards.
	static void trimDownTo(LogDocument *doc, int target) {
		doc->setBlockBudget(doc->totalBlockCount() + 1);
		while (doc->entryCount() > target && doc->blockBudget() > 0) {
			doc->setBlockBudget(doc->blockBudget() - 1);
		}
	}
};

void TestLogDocument::nativeMaximumBlockCountCorruptsFrames() {
	// Development gate recording what Qt's setMaximumBlockCount actually does
	// to a frame-based document (exact numbers in the commit message):
	// (1) a moderate budget trims once when set - and then never again, so
	// later inserts grow the document unchecked past the limit; (2) a budget
	// near the frame-structure cost (about half the blocks) makes that
	// one-time trim wipe the ENTIRE document. Together: unusable for frame
	// entries - the whole-entry budget replaces it.

	QTextDocument doc;
	appendFrameEntries(&doc, 0, 40);
	QCOMPARE(doc.rootFrame()->childFrames().count(), 40);
	const int blocksBefore = doc.blockCount();

	// (1) Moderate budget: the one-time trim runs...
	doc.setMaximumBlockCount(blocksBefore - 1);
	QVERIFY(doc.blockCount() <= doc.maximumBlockCount());
	// ...but four more entries later, the "limit" is long forgotten.
	appendFrameEntries(&doc, 100, 4);
	QVERIFY2(doc.blockCount() > doc.maximumBlockCount(),
			 "setMaximumBlockCount enforces later inserts on frame documents now - "
			 "re-examine whether the whole-entry budget is still required");

	// (2) Small budget: the whole document is destroyed at once.
	QTextDocument wiped;
	appendFrameEntries(&wiped, 0, 40);
	wiped.setMaximumBlockCount(wiped.blockCount() / 2);
	QCOMPARE(wiped.rootFrame()->childFrames().count(), 0);
	QVERIFY(wiped.isEmpty());
}

void TestLogDocument::budgetZeroNeverTrims() {
	LogConsoleView view;
	view.setLogBlockLimit(0);
	for (int i = 0; i < 50; ++i) {
		appendMarked(view, QString::fromLatin1("mk%1").arg(i, 2, 10, QLatin1Char('0')));
	}

	LogDocument *doc = docOf(view);
	QCOMPARE(doc->blockBudget(), 0);
	QCOMPARE(doc->entryCount(), 50);
	QCOMPARE(doc->rootFrame()->childFrames().count(), 50);
}

void TestLogDocument::wholeOldestEntryTrimmingPlateaus() {
	LogConsoleView view;

	appendMarked(view, QLatin1String("mk00"));
	const int blocksForOne = docOf(view)->totalBlockCount();
	appendMarked(view, QLatin1String("mk01"));
	const int blocksForTwo = docOf(view)->totalBlockCount();
	const int perEntry     = blocksForTwo - blocksForOne;
	QVERIFY(perEntry > 0);

	// Budget that fits exactly ten uniform entries.
	const int keepEntries = 10;
	const int budget      = blocksForOne + perEntry * (keepEntries - 1);
	view.setLogBlockLimit(budget);

	for (int i = 2; i < 30; ++i) {
		appendMarked(view, QString::fromLatin1("mk%1").arg(i, 2, 10, QLatin1Char('0')));
	}

	LogDocument *doc = docOf(view);
	QCOMPARE(doc->entryCount(), keepEntries);
	QVERIFY(doc->totalBlockCount() <= budget);
	// Oldest twenty trimmed away, the newest ten retained.
	const QString plain = view.document()->toPlainText();
	QVERIFY(!plain.contains(QLatin1String("mk19")));
	QVERIFY(!plain.contains(QLatin1String("mk00")));
	QVERIFY(plain.contains(QLatin1String("mk20")));
	QVERIFY(plain.contains(QLatin1String("mk29")));
	QStringList expected;
	for (int i = 20; i < 30; ++i) {
		expected << QString::fromLatin1("mk%1").arg(i, 2, 10, QLatin1Char('0'));
	}
	verifyRetainedMarkers(doc, expected);

	// Canary: a hundred further appends keep every retained frame whole and
	// the counts plateau.
	for (int i = 30; i < 130; ++i) {
		appendMarked(view, QString::fromLatin1("mk%1").arg(i, 3, 10, QLatin1Char('0')));
	}
	QStringList expectedCanary;
	for (int i = 120; i < 130; ++i) {
		expectedCanary << QString::fromLatin1("mk%1").arg(i, 3, 10, QLatin1Char('0'));
	}
	verifyRetainedMarkers(doc, expectedCanary);
	QVERIFY(doc->totalBlockCount() <= budget);
	QCOMPARE(doc->entryCount(), keepEntries);
	QVERIFY(!view.document()->toPlainText().contains(QLatin1String("mk119")));
	QVERIFY(view.document()->toPlainText().contains(QLatin1String("mk129")));
}

void TestLogDocument::evictedImagesReleased() {
	LogConsoleView view;
	const auto settings = LogConsoleView::RenderSettings{ 3, true };

	const QString htmlOldest = uniqueImageHtml(1);
	const QString htmlNewest = uniqueImageHtml(2);
	const QUrl urlOldest     = imageUrlOf(htmlOldest);
	const QUrl urlNewest     = imageUrlOf(htmlNewest);

	// Two entries; then a budget that fits exactly one.
	view.appendEntry(QDateTime::currentDateTime(), QString::fromLatin1("<p>oldest</p>") + htmlOldest, false, settings);
	const int blocksForOne = docOf(view)->totalBlockCount();
	view.appendEntry(QDateTime::currentDateTime(), QString::fromLatin1("<p>newest</p>") + htmlNewest, false, settings);
	const int perEntry = docOf(view)->totalBlockCount() - blocksForOne;
	view.setLogBlockLimit(blocksForOne + perEntry - 1);

	LogDocument *doc = docOf(view);
	// The oldest entry (and with it its unique image) is trimmed.
	QCOMPARE(doc->entryCount(), 1);
	QVERIFY(doc->staleResourceKeyBytes() > 0);

	// The evicted image's slot now holds the 1x1 placeholder...
	QCOMPARE(resourceImage(doc, urlOldest).width(), 1);
	// ...while the retained entry's image is untouched (full size).
	QCOMPARE(resourceImage(doc, urlNewest).width(), 4);
}

void TestLogDocument::rehydrationCycles() {
	LogConsoleView view;
	const auto settings = LogConsoleView::RenderSettings{ 3, true };

	const QString imageHtml = uniqueImageHtml(7);
	const QUrl imageUrl     = imageUrlOf(imageHtml);

	// Measure with two plain entries, then hold exactly two entries.
	appendMarked(view, QLatin1String("seed"));
	const int blocksForOne = docOf(view)->totalBlockCount();
	appendMarked(view, QLatin1String("probe"));
	const int perEntry = docOf(view)->totalBlockCount() - blocksForOne;
	view.setLogBlockLimit(blocksForOne + perEntry);

	for (int round = 0; round < 3; ++round) {
		// The image entry goes in (retained - it is the newest)...
		view.appendEntry(QDateTime::currentDateTime(),
						 QString::fromLatin1("<p>image entry %1</p>").arg(round) + imageHtml, false, settings);
		LogDocument *doc = docOf(view);
		QCOMPARE(resourceImage(doc, imageUrl).width(), 4);

		// ...then enough unique followers push it out.
		for (int i = 0; i < 6; ++i) {
			view.appendEntry(QDateTime::currentDateTime(),
							 QString::fromLatin1("<p>filler %1-%2</p>").arg(round).arg(i)
								 + uniqueImageHtml(100 * round + i),
							 false, settings);
		}
		doc = docOf(view);
		QVERIFY(!view.document()->toPlainText().contains(QString::fromLatin1("image entry %1").arg(round)));
		// Its resource slot is the placeholder now...
		QCOMPARE(resourceImage(doc, imageUrl).width(), 1);
		QVERIFY(doc->staleResourceKeyBytes() > 0);

		// ...and a NEW entry referencing the same data: URL rehydrates it at
		// full size (resource() alone would keep serving the placeholder:
		// it never calls loadResource() for cached names).
		view.appendEntry(QDateTime::currentDateTime(),
						 QString::fromLatin1("<p>re-reference %1</p>").arg(round) + imageHtml, false, settings);
		doc = docOf(view);
		QCOMPARE(resourceImage(doc, imageUrl).width(), 4);
		QVERIFY(view.document()->toPlainText().contains(QString::fromLatin1("re-reference %1").arg(round)));
	}
}

void TestLogDocument::dateAwareEvictionCrossingMidnight() {
	LogConsoleView view;
	const auto settings = LogConsoleView::RenderSettings{ 3, true };

	const QDateTime base = QDateTime::currentDateTime();
	const QDate dateA    = base.date().addDays(-5);
	const QDate dateB    = base.date();

	const auto appendOn = [&view, &settings](const QDate &date, const QString &marker) {
		view.appendEntry(QDateTime(date, QTime(12, 0, 0)), QString::fromLatin1("<p>%1 content</p>").arg(marker), false,
						 settings);
	};

	appendOn(dateA, QLatin1String("a1"));
	appendOn(dateA, QLatin1String("a2"));
	appendOn(dateA, QLatin1String("a3"));
	appendOn(dateB, QLatin1String("b1"));
	appendOn(dateB, QLatin1String("b2"));

	LogDocument *doc = docOf(view);
	// [D_A][a1][a2][a3][D_B][b1][b2]
	QCOMPARE(doc->rootFrame()->childFrames().count(), 7);
	QCOMPARE(doc->entryCount(), 5);
	QCOMPARE(dateFrames(doc).count(), 2);

	trimDownTo(doc, 2);
	QCOMPARE(doc->entryCount(), 2);

	// The obsolete D_A went with its messages; D_B is the leading frame
	// labeling the oldest retained entry.
	QCOMPARE(doc->rootFrame()->childFrames().count(), 3);
	QCOMPARE(dateFrames(doc).count(), 1);
	const QTextFrame *first = doc->rootFrame()->childFrames().constFirst();
	QCOMPARE(first->frameFormat().intProperty(LogDocument::FrameRoleProperty),
			 static_cast< int >(LogDocument::DateFrameRole));
	const QString plain = view.document()->toPlainText();
	QVERIFY(plain.contains(QLatin1String("[Date changed to ") + QLocale().toString(dateB, QLocale::ShortFormat)));
	QVERIFY(!plain.contains(QLatin1String("a3")));
	QVERIFY(plain.contains(QLatin1String("b1")));
	verifyRetainedMarkers(doc, { QLatin1String("b1"), QLatin1String("b2") });

	// A follow-up entry on the same date adds no duplicate separator.
	appendOn(dateB, QLatin1String("b3"));
	doc = docOf(view);
	QCOMPARE(doc->rootFrame()->childFrames().count(), 4);
	QCOMPARE(doc->entryCount(), 3);
	QCOMPARE(dateFrames(doc).count(), 1);

	// --- mid-date eviction keeps the still-labeling separator ---
	LogConsoleView view2;
	const auto appendOn2 = [&view2, &settings](const QDate &date, const QString &marker) {
		view2.appendEntry(QDateTime(date, QTime(12, 0, 0)), QString::fromLatin1("<p>%1 content</p>").arg(marker), false,
						  settings);
	};
	appendOn2(dateA, QLatin1String("a1"));
	appendOn2(dateA, QLatin1String("a2"));
	appendOn2(dateA, QLatin1String("a3"));
	appendOn2(dateB, QLatin1String("b1"));

	LogDocument *doc2 = docOf(view2);
	trimDownTo(doc2, 2);
	QCOMPARE(doc2->entryCount(), 2);
	// a3 (still on date A) is retained -> D_A survives alongside D_B, and
	// there are never two consecutive date frames.
	const QList< QTextFrame * > children2 = doc2->rootFrame()->childFrames();
	QCOMPARE(children2.count(), 4);
	QCOMPARE(dateFrames(doc2).count(), 2);
	for (qsizetype i = 0; i + 1 < children2.count(); ++i) {
		const int roleThis = children2.at(i)->frameFormat().intProperty(LogDocument::FrameRoleProperty);
		const int roleNext = children2.at(i + 1)->frameFormat().intProperty(LogDocument::FrameRoleProperty);
		QVERIFY(!(roleThis == LogDocument::DateFrameRole && roleNext == LogDocument::DateFrameRole));
	}
	QVERIFY(view2.document()->toPlainText().contains(QLatin1String("a3")));
	verifyRetainedMarkers(doc2, { QLatin1String("a3"), QLatin1String("b1") });
}

void TestLogDocument::compactionPreservesContent() {
	LogConsoleView view;
	const auto settings = LogConsoleView::RenderSettings{ 3, true };

	const QDateTime base = QDateTime::currentDateTime();
	for (int i = 0; i < 12; ++i) {
		view.appendEntry(base.addSecs(i), QString::fromLatin1("<p>keep %1 text</p>").arg(i), false, settings);
	}

	const QString htmlBefore     = view.document()->toHtml();
	const qsizetype framesBefore = view.document()->rootFrame()->childFrames().count();

	view.compact();

	QCOMPARE(view.document()->toHtml(), htmlBefore);
	QCOMPARE(view.document()->rootFrame()->childFrames().count(), framesBefore);
	QCOMPARE(docOf(view)->staleResourceKeyBytes(), 0);
}

void TestLogDocument::compactionAutoTriggersOnStaleKeys() {
	LogConsoleView view;
	const auto settings = LogConsoleView::RenderSettings{ 3, true };

	// Measure with two unique-image entries, then hold exactly two.
	view.appendEntry(QDateTime::currentDateTime(), QString::fromLatin1("<p>seed</p>") + uniqueImageHtml(900), false,
					 settings);
	const int blocksForOne = docOf(view)->totalBlockCount();
	view.appendEntry(QDateTime::currentDateTime(), QString::fromLatin1("<p>uni 00</p>") + uniqueImageHtml(0), false,
					 settings);
	const int perEntry = docOf(view)->totalBlockCount() - blocksForOne;
	view.setLogBlockLimit(blocksForOne + perEntry);
	docOf(view)->setResourceKeyBudget(512);

	QTextDocument *const documentBefore = view.document();
	QVERIFY(documentBefore != nullptr);
	// Compaction is detected by object identity, not pointer inequality:
	// the swap churn frees the original document early, and the allocator
	// can hand a later fresh document the very same address (observed as a
	// full-suite-only flake). A dynamic property dies with its object, so
	// the stamped original can never be mistaken for a replacement.
	documentBefore->setProperty("compactionWitness", true);

	// Sustained unique-image traffic: every append evicts the previous
	// window, accumulating stale keys far beyond the budget.
	for (int i = 1; i < 30; ++i) {
		view.appendEntry(QDateTime::currentDateTime(),
						 QString::fromLatin1("<p>uni %1</p>").arg(i, 2, 10, QLatin1Char('0')) + uniqueImageHtml(i),
						 false, settings);
	}

	LogDocument *doc = docOf(view);
	// Compaction fired (the document was swapped out from under us)...
	QVERIFY(!view.document()->property("compactionWitness").toBool());
	// ...often enough that stale keys never sit beyond the budget.
	QVERIFY(!doc->compactionDue());
	// The retained window is intact at full image size.
	QCOMPARE(doc->entryCount(), 2);
	QCOMPARE(resourceImage(doc, imageUrlOf(uniqueImageHtml(29))).width(), 4);
	QVERIFY(view.document()->toPlainText().contains(QLatin1String("uni 29")));
}

void TestLogDocument::clearReleasesEverything() {
	LogConsoleView view;
	const auto settings = LogConsoleView::RenderSettings{ 3, true };

	const QString imageHtml = uniqueImageHtml(5);
	const QUrl imageUrl     = imageUrlOf(imageHtml);
	view.appendEntry(QDateTime::currentDateTime(), QString::fromLatin1("<p>one</p>") + imageHtml, false, settings);
	view.appendEntry(QDateTime::currentDateTime(), QLatin1String("<p>two</p>"), false, settings);
	view.setLogBlockLimit(1);
	LogDocument *doc = docOf(view);
	QVERIFY(doc->staleResourceKeyBytes() > 0);

	QTextDocument *const oldDocument = view.document();

	view.clearConsole();

	doc = docOf(view);
	QVERIFY(view.document() != oldDocument);
	QCOMPARE(doc->entryCount(), 0);
	QCOMPARE(doc->staleResourceKeyBytes(), 0);
	QVERIFY(view.document()->toPlainText().isEmpty());
	QCOMPARE(view.document()->rootFrame()->childFrames().count(), 0);
	// The budget survived the swap.
	QCOMPARE(doc->blockBudget(), 1);
	// A new entry renders normally afterwards.
	view.appendEntry(QDateTime::currentDateTime(), QLatin1String("<p>after clear</p>"), false, settings);
	QCOMPARE(doc->entryCount(), 1);
	QVERIFY(view.document()->toPlainText().contains(QLatin1String("after clear")));
}

QTEST_MAIN(TestLogDocument)
#include "TestLogDocument.moc"

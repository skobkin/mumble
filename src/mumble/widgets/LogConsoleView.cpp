// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the Mumble
// source tree or at <https://www.mumble.info/LICENSE>.

#include "widgets/LogConsoleView.h"

#include <QLocale>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextFrame>

LogConsoleView::LogConsoleView(QWidget *p) : LogTextBrowser(p), m_lastEntryDate(QDate::currentDate()) {
	// Each destination owns the document it renders into: the widget is the
	// single place that decides which document console entries land in.
	LogDocument *doc = new LogDocument(this);
	setDocument(doc);
	setContextMenuPolicy(Qt::CustomContextMenu);
	setOpenLinks(false);
	connectDocument(doc);
}

void LogConsoleView::connectDocument(LogDocument *doc) {
	connect(doc, &LogDocument::entriesTrimmed, this, &LogConsoleView::onEntriesTrimmed);
	connect(doc, &LogDocument::compactionRecommended, this, &LogConsoleView::compact);
}

bool LogConsoleView::isChatMessageType(Log::MsgType mt) {
	return mt == Log::TextMessage || mt == Log::PrivateTextMessage;
}

void LogConsoleView::appendEntry(const QDateTime &dt, const QString &console, bool ownMessage,
								 const RenderSettings &settings) {
	QTextCursor tc = textCursor();

	tc.movePosition(QTextCursor::End);

	// We copy the value from the settings in order to make sure that
	// we use the same margin everywhere while in this method (even if
	// the setting might change in that time).
	const int msgMargin = settings.messageMargins;

	QTextFrameFormat qttf;
	qttf.setTopMargin(0);
	qttf.setBottomMargin(msgMargin);

	const int oldscrollvalue = getLogScroll();
	// Restore the previous scroll position after inserting a new message
	// if the message was not sent by the user AND the chat log is not
	// scrolled all the way down.
	const bool restoreScroll = !(ownMessage || isScrolledToBottom());

	// A newline is inserted after each frame, but this spaces out the
	// log entries too much, so the line height is set to zero to reduce
	// the space between the log entries. This line height is only set for the
	// blank lines between entries, not for entries themselves.
	//
	// NOTE: All further log entries must go in a new text frame.
	// Otherwise, they will not display correctly as a result of having
	// line height equal to 0 for the current block.
	QTextBlockFormat bf = tc.blockFormat();
	bf.setLineHeight(0, QTextBlockFormat::FixedHeight);
	bf.setTopMargin(0);
	bf.setBottomMargin(0);

	// Set the line height of the leading blank line to zero
	tc.setBlockFormat(bf);

	if (m_lastEntryDate != dt.date()) {
		m_lastEntryDate = dt.date();
		// Date-separator frames are tagged so the document's budget
		// accounting can tell them apart from message frames.
		qttf.setProperty(LogDocument::FrameRoleProperty, LogDocument::DateFrameRole);
		tc.insertFrame(qttf);
		// Log::tr keeps this string in the translation context it has always
		// had, even though the rendering moved into this widget.
		tc.insertHtml(Log::tr("[Date changed to %1]\n")
						  .arg(QLocale().toString(m_lastEntryDate, QLocale::ShortFormat).toHtmlEscaped()));
		tc.movePosition(QTextCursor::End);
		tc.setBlockFormat(bf);
	}

	// Derive the entry's plain text the same way the former inline code did,
	// to detect invisible trailing content.
	QString plain = QTextDocumentFragment::fromHtml(console).toPlainText();
	// Convert CRLF to unix-style LF and old mac-style LF (single \r) to unix-style as well
	plain.replace(QLatin1String("\r\n"), QLatin1String("\n")).replace(QLatin1String("\r"), QLatin1String("\n"));

	if (plain.contains(QRegularExpression(QLatin1String("\\n[ \\t]*$")))) {
		// If the message ends with one or more blank lines (or lines only containing whitespace)
		// paint a border around the message to make clear that it contains invisible parts.
		// The beginning of the message is clear anyway (the date and potentially the "To XY" part)
		// so we don't have to care about that.
		qttf.setBorder(1);
		qttf.setPadding(2);
		qttf.setBorderStyle(QTextFrameFormat::BorderStyle_Dashed);
	}

	// Message frames carry the entry-role tag (see the date frame above).
	qttf.setProperty(LogDocument::FrameRoleProperty, LogDocument::MessageFrameRole);
	tc.insertFrame(qttf);

	const QString timeString = dt.time().toString(QLatin1String(settings.use24HourClock ? "HH:mm:ss" : "hh:mm:ss AP"));
	tc.insertHtml(Log::msgColor(QString::fromLatin1("[%1] ").arg(timeString.toHtmlEscaped()), Log::Time));

	Log::validHtml(console, &tc);
	tc.movePosition(QTextCursor::End);
	setTextCursor(tc);

	// Set the line height of the trailing blank line to zero
	tc.setBlockFormat(bf);

	// Record the entry for budget-bounded compaction rebuilds, enforce the
	// whole-entry block budget (may trim the oldest entries), and re-decode
	// any image references whose bytes were evicted earlier. The just-inserted
	// frame is never a trim victim (the newest entry is always kept), so this
	// ordering is safe.
	m_retainedSources.append(EntrySource{ dt, console, ownMessage, settings });
	if (LogDocument *doc = qobject_cast< LogDocument * >(document())) {
		doc->enforceBlockBudget();
		const QList< QTextFrame * > frames = doc->rootFrame()->childFrames();
		for (int i = static_cast< int >(frames.size()) - 1; i >= 0; --i) {
			if (frames.at(i)->frameFormat().intProperty(LogDocument::FrameRoleProperty)
				== LogDocument::MessageFrameRole) {
				doc->rehydrateImagesInRange(frames.at(i)->firstPosition() - 1, frames.at(i)->lastPosition() + 1);
				break;
			}
		}

		// Compact only after finishing the append: swapping the document from
		// inside the eviction above would invalidate it mid-use (the fresh
		// document is rebuilt from the retained sources, which already
		// include the entry appended here).
		if (doc->compactionDue()) {
			compact();
		}
	}

	if (restoreScroll) {
		setLogScroll(oldscrollvalue);
	}
}

void LogConsoleView::applyTheme(const QString &styleSheet) {
	// Cached: document swaps (clearConsole, compact) must keep the theme and
	// QTextDocument offers no defaultStyleSheet getter.
	m_appliedStyleSheet = styleSheet;
	document()->setDefaultStyleSheet(styleSheet);
}

void LogConsoleView::setLogBlockLimit(int limit) {
	if (LogDocument *doc = qobject_cast< LogDocument * >(document())) {
		doc->setBlockBudget(limit);
		// Lowering the limit evicts in bulk and can release many image
		// resources at once - the same post-append compaction check applies
		// (compacting from inside the eviction itself would swap the
		// document mid-call). doc is not touched after this point: compact
		// destroys it.
		if (doc->compactionDue()) {
			compact();
		}
	}
}

void LogConsoleView::setResourceKeyBudget(int bytes) {
	if (LogDocument *doc = qobject_cast< LogDocument * >(document())) {
		doc->setResourceKeyBudget(bytes);
	}
}

void LogConsoleView::clearConsole() {
	LogDocument *old     = qobject_cast< LogDocument * >(document());
	const int oldEntries = old ? old->entryCount() : 0;
	LogDocument *fresh   = new LogDocument(this);
	if (old) {
		fresh->setBlockBudget(old->blockBudget());
		fresh->setResourceKeyBudget(old->resourceKeyBudget());
	}
	fresh->setDefaultStyleSheet(m_appliedStyleSheet);

	m_retainedSources.clear();
	setDocument(fresh);
	connectDocument(fresh);
	if (old && old != fresh) {
		delete old;
	}

	// Reseed like a fresh view: the next entry gets no leading separator for
	// today (the former Log::qdDate survived QTextEdit::clear() with the same
	// effect, since post-clear entries always carry the current date).
	m_lastEntryDate = QDate::currentDate();

	// A clear removes every entry, seen and unseen; report it through the
	// same channel as eviction so owners' unread bookkeeping cannot outlive
	// the entries it counted.
	if (oldEntries > 0) {
		emit entriesTrimmed(oldEntries);
	}
}

void LogConsoleView::compact() {
	if (m_inCompaction) {
		return;
	}
	m_inCompaction = true;

	LogDocument *old = qobject_cast< LogDocument * >(document());
	if (old) {
		// Preserve the reader's position across the swap (ratio-clamped).
		QScrollBar *bar     = verticalScrollBar();
		const qreal ratio   = bar->maximum() > 0 ? qreal(bar->value()) / qreal(bar->maximum()) : 1.0;
		const int budget    = old->blockBudget();
		const int keyBudget = old->resourceKeyBudget();
		const bool leadingDateFrame =
			!old->rootFrame()->childFrames().isEmpty()
			&& old->rootFrame()->childFrames().constFirst()->frameFormat().intProperty(LogDocument::FrameRoleProperty)
				   == LogDocument::DateFrameRole;
		const QVector< EntrySource > sources = m_retainedSources;

		LogDocument *fresh = new LogDocument(this);
		fresh->setBlockBudget(budget);
		fresh->setResourceKeyBudget(keyBudget);
		fresh->setDefaultStyleSheet(m_appliedStyleSheet);

		setDocument(fresh);
		connectDocument(fresh);
		m_retainedSources.clear();

		// Regenerate the leading separator state exactly as it was: a leading
		// date frame is reproduced by seeding the date with the day before the
		// oldest retained entry; a leading message frame by seeding with that
		// entry's own date (no separator).
		if (sources.isEmpty()) {
			m_lastEntryDate = QDate::currentDate();
		} else if (leadingDateFrame) {
			m_lastEntryDate = sources.constFirst().timestamp.date().addDays(-1);
		} else {
			m_lastEntryDate = sources.constFirst().timestamp.date();
		}

		for (const EntrySource &source : sources) {
			appendEntry(source.timestamp, source.consoleHtml, source.ownMessage, source.settings);
		}

		bar->setValue(qRound(ratio * bar->maximum()));
		// The old document - and with it every stale resource-cache key -
		// is gone.
		if (old != fresh) {
			delete old;
		}
	}

	m_inCompaction = false;
}

void LogConsoleView::onEntriesTrimmed(int count) {
	if (count <= 0) {
		return;
	}
	if (count >= m_retainedSources.size()) {
		m_retainedSources.clear();
	} else {
		m_retainedSources.remove(0, count);
	}

	// Relay for owners (unread-badge bookkeeping); see the signal's docs.
	emit entriesTrimmed(count);
}

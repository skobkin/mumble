// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the Mumble
// source tree or at <https://www.mumble.info/LICENSE>.

#include "widgets/LogConsoleView.h"

#include <QLocale>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextFrame>

LogConsoleView::LogConsoleView(QWidget *p) : LogTextBrowser(p), m_lastEntryDate(QDate::currentDate()) {
	// Each destination owns the document it renders into: the widget is the
	// single place that decides which document console entries land in.
	setDocument(new LogDocument(this));
	setContextMenuPolicy(Qt::CustomContextMenu);
	setOpenLinks(false);
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

	tc.insertFrame(qttf);

	const QString timeString = dt.time().toString(QLatin1String(settings.use24HourClock ? "HH:mm:ss" : "hh:mm:ss AP"));
	tc.insertHtml(Log::msgColor(QString::fromLatin1("[%1] ").arg(timeString.toHtmlEscaped()), Log::Time));

	Log::validHtml(console, &tc);
	tc.movePosition(QTextCursor::End);
	setTextCursor(tc);

	// Set the line height of the trailing blank line to zero
	tc.setBlockFormat(bf);

	if (restoreScroll) {
		setLogScroll(oldscrollvalue);
	}
}

void LogConsoleView::applyTheme(const QString &styleSheet) {
	document()->setDefaultStyleSheet(styleSheet);
}

void LogConsoleView::setLogBlockLimit(int limit) {
	document()->setMaximumBlockCount(limit);
}

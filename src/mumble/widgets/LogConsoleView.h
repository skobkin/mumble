// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the Mumble
// source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_WIDGETS_LOGCONSOLEVIEW_H_
#define MUMBLE_WIDGETS_LOGCONSOLEVIEW_H_

#include "CustomElements.h"
#include "Log.h"

#include <QtCore/QDate>
#include <QtCore/QDateTime>
#include <QtCore/QString>

/// A log destination: a LogTextBrowser that owns its LogDocument and knows how
/// one console entry is rendered into it.
///
/// The rendering is a verbatim port of the insertion block that used to live
/// inline in Log::log(): each entry becomes a child QTextFrame of the
/// document's root frame, preceded by a "[Date changed ...]" separator frame
/// whenever the entry's date differs from the previously appended entry's
/// date. The widget reads no Global state itself; all per-entry settings are
/// handed in by the caller through RenderSettings so instances stay
/// constructible in isolated tests.
class LogConsoleView : public LogTextBrowser {
private:
	Q_OBJECT
	Q_DISABLE_COPY(LogConsoleView)

public:
	/// Per-entry rendering parameters. The caller snapshots these from the
	/// settings before each entry so that one entry renders consistently even
	/// if the settings change while it is being inserted (the former code
	/// copied the margin for exactly this reason).
	struct RenderSettings {
		int messageMargins;
		bool use24HourClock;
	};

	explicit LogConsoleView(QWidget *p = nullptr);

	/// Whether mt belongs to a chat destination (user-to-user or channel text
	/// messages) as opposed to the activity log.
	///
	/// Deliberately checks the two text types by name instead of iterating
	/// [firstMsgType, lastMsgType]: message types outside that range (e.g.
	/// PluginMessage) must never be misrouted by range arithmetic.
	static bool isChatMessageType(Log::MsgType mt);

	/// Appends one console entry. Rendering is byte-identical to the former
	/// inline block in Log::log() (frames, date separators, timestamp,
	/// HTML validation, scroll preservation).
	void appendEntry(const QDateTime &timestamp, const QString &consoleHtml, bool ownMessage,
					 const RenderSettings &settings);

	/// Forwards a stylesheet to the owned document (theme restyling).
	void applyTheme(const QString &styleSheet);
	/// Forwards the log block limit to the owned document.
	void setLogBlockLimit(int limit);

private:
	/// The date of the most recently appended entry. Seeded with the
	/// construction date to mirror the former Log::qdDate seeding: the very
	/// first entry of a session gets no "[Date changed]" separator.
	QDate m_lastEntryDate;
};

#endif // MUMBLE_WIDGETS_LOGCONSOLEVIEW_H_

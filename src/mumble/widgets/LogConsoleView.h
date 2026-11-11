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
#include <QtCore/QVector>

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

	/// One rendered entry, kept for budget-bounded compaction rebuilds.
	/// Sources are dropped when their entry is trimmed or the console is
	/// cleared, so the list is never larger than what the document shows.
	/// Filter rebuilds pass these back in through rebuild().
	struct EntrySource {
		QDateTime timestamp;
		QString consoleHtml;
		bool ownMessage;
		RenderSettings settings;
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
	/// Forwards the stale resource-key byte budget to the owned document
	/// (tests lower this to exercise compaction cheaply; production keeps
	/// the default).
	void setResourceKeyBudget(int bytes);

	/// Clears the console by replacing the document wholesale, so the
	/// resource cache and all eviction bookkeeping are released with it
	/// (Qt has no API to purge a document's resource cache). Date state
	/// reseeds like a fresh view.
	void clearConsole();
	/// Rebuilds the document from the retained entry sources, discarding all
	/// stale resource-cache keys with the old document. Emitted
	/// automatically through the document's compactionRecommended signal
	/// once stale keys exceed the resource-key budget; content and scroll
	/// are preserved.
	void compact();
	/// Wholesale rebuild for filter projections: clears (fresh document, all
	/// budgets and the theme carried over) and re-appends \p sources in
	/// order. Date-separator state resets like a fresh view, so the first
	/// source gets no leading separator. The view's retained-source list
	/// becomes exactly \p sources.
	void rebuild(const QVector< EntrySource > &sources);
	/// Removes the \p count oldest rendered entries (whole frames + their
	/// date separators). The store-driven chat view uses this for lockstep
	/// eviction; returns the number of entries actually removed.
	int removeLeadingRenderedEntries(int count);

signals:
	/// The document lost \p count entries from its front: budget eviction
	/// or a wholesale clear. Owners folding this into unread bookkeeping
	/// must query the document's current entryCount() (unseen entries are
	/// the newest ones, so how many survived is derivable). Survives
	/// document swaps - it is re-emitted from whichever document is
	/// current.
	void entriesTrimmed(int count);

private slots:
	void onEntriesTrimmed(int count);

private:
	void connectDocument(LogDocument *doc);

	QVector< EntrySource > m_retainedSources;
	/// Cached stylesheet so document swaps (clearConsole, compact) keep the
	/// applied theme (QTextDocument has no defaultStyleSheet getter).
	QString m_appliedStyleSheet;
	/// Guards re-entrancy while compact() rebuilds.
	bool m_inCompaction = false;

	/// The date of the most recently appended entry. Seeded with the
	/// construction date to mirror the former Log::qdDate seeding: the very
	/// first entry of a session gets no "[Date changed]" separator.
	QDate m_lastEntryDate;
};

#endif // MUMBLE_WIDGETS_LOGCONSOLEVIEW_H_

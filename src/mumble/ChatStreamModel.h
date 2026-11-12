// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the Mumble
// source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_CHATSTREAMMODEL_H_
#define MUMBLE_CHATSTREAMMODEL_H_

#include "Log.h"
#include "widgets/LogConsoleView.h"

#include <functional>
#include <optional>

#include <QtCore/QDateTime>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVector>

/// One accepted chat entry, stored exactly as it will be (re-)rendered.
/// Accepted means past Log's ignore counter and per-type console flag: the
/// entry's HTML and its routing metadata entered the stream atomically or not
/// at all, so a suppressed message can never reappear through a filter
/// rebuild.
struct ChatEntry {
	/// Store-assigned monotonically increasing id (lockstep eviction and
	/// targeted removal key on it).
	quint64 id = 0;
	QDateTime timestamp;
	QString consoleHtml;
	bool ownMessage = false;
	/// The per-entry render parameters snapshotted at arrival (rebuilds
	/// re-render historical entries with the settings they arrived under,
	/// exactly like a compaction rebuild).
	LogConsoleView::RenderSettings renderSettings;
	/// Whether the entry has actually been displayed: live append matching the
	/// active filter while the chat view is visible, or display during a
	/// filter rebuild. Feeds the tab and filter badges - no parallel counters.
	bool seen = false;
	/// Producer-captured routing metadata (copy; may be absent).
	ChatRoutingInfo routing;
};

/// The process-local chat stream: ONE bounded buffer of every accepted chat
/// entry, kept for the whole process lifetime (across reconnects and server
/// switches). The rendered chat view is a pure projection of this store
/// through (filter, selected channel, connection epoch); nothing is ever
/// stored per channel and nothing is written to disk.
///
/// Classification mirrors msgTextMessage exactly: tree > channel > direct.
/// The All and Direct filters are topology-free; id-based matching (Selected
/// Channel) is only valid within the entry's connection epoch.
class ChatStreamModel : public QObject {
public:
	enum class Filter { All, SelectedChannel, Direct };

	explicit ChatStreamModel(QObject *parent = nullptr) : QObject(parent) {}

	/// Answers whether channel \p channelId lies inside the subtree rooted at
	/// \p rootId, over the live channel tree. Supplied by the owner (live
	/// Channel objects are connection state, not store state).
	using TreeCoverage = std::function< bool(unsigned int rootId, unsigned int channelId) >;

	/// Everything a filter decision needs besides the entry itself.
	struct FilterContext {
		Filter filter = Filter::All;
		/// The channel the Selected Channel filter tracks (nullopt = none;
		/// the filter matches nothing then). This is an explicit optional
		/// state rather than a 0 sentinel because 0 is the root channel's
		/// real id and a legitimate Selected Channel target.
		std::optional< unsigned int > selectedChannelId;
		/// The connection epoch id-based matching is valid in.
		quint64 currentEpoch = 0;
		/// Subtree resolver; may be null for Topology-free queries.
		TreeCoverage treeCovers;
	};

	/// Sets the entry cap (iMaxLogBlocks interpreted as an entry budget for
	/// the chat stream). Values <= 0 mean unlimited, like the block budget.
	void setEntryCap(int cap);
	int entryCap() const;

	int size() const;
	const ChatEntry &at(int index) const;
	const ChatEntry *findById(quint64 id) const;

	/// Result of append(): the id assigned to the stored entry and the ids of
	/// the entries evicted from the front, oldest first.
	struct AppendResult {
		quint64 assignedId = 0;
		QVector< quint64 > evictedIds;
	};

	/// Appends the entry (assigning its id) and evicts the oldest entries
	/// that no longer fit the cap.
	AppendResult append(ChatEntry entry);

	/// Removes every entry matching the filter (store-aware Clear). Returns
	/// the removed entries' ids, oldest first.
	QVector< quint64 > removeMatching(const FilterContext &ctx);

	/// Pure predicate: whether \p entry is displayed under \p ctx.
	bool matches(const ChatEntry &entry, const FilterContext &ctx) const;

	/// Classification parity with msgTextMessage: an entry is direct iff it
	/// carries metadata at all, targets no tree and no channel, and targets
	/// at least one session.
	static bool isDirect(const ChatRoutingInfo &routing);

	/// Unseen entries matching the filter (filter badges).
	int unseenCount(const FilterContext &ctx) const;
	/// Unseen entries regardless of filter (chat tab badge).
	int unseenTotal() const;

	/// Marks the given entries seen (they were displayed).
	void markSeen(const QVector< quint64 > &ids);

private:
	QVector< ChatEntry > m_entries;
	int m_entryCap   = 0;
	quint64 m_nextId = 1;
};

#endif // MUMBLE_CHATSTREAMMODEL_H_

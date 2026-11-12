// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the Mumble
// source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_CHATSTREAMPRESENTER_H_
#define MUMBLE_CHATSTREAMPRESENTER_H_

#include "ChatStreamModel.h"
#include "widgets/LogConsoleView.h"

#include <optional>

#include <QtCore/QMap>
#include <QtCore/QObject>
#include <QtCore/QVector>

/// Bridges the chat stream store and the rendered chat view. The view is a
/// pure projection of the store through (filter, selected channel, epoch):
///
/// - appendEntry() stores every accepted chat entry and renders it iff it
///   matches the active filter; when the store's entry cap evicts entries,
///   the rendered frames of evicted-and-rendered entries go in the same step
///   (lockstep - the document never outgrows the store, and a later filter
///   switch never discards more than the store already lost).
/// - setFilter()/setSelectedChannel() rebuild the view from the store; a
///   rebuild displays every match, which marks them seen.
/// - clearActiveFilter() removes the active filter's entries from the store
///   itself (a bare widget clear would be undone by the next rebuild) and
///   rebuilds - hidden entries that do not match survive, reachable through
///   their filters.
///
/// The presenter owns no Global state: the epoch and the live-channel
/// subtree resolver are handed in by MainWindow, which makes the whole
/// store-view system constructible in isolated tests.
class ChatStreamPresenter : public QObject {
	Q_OBJECT
	Q_DISABLE_COPY(ChatStreamPresenter)

public:
	ChatStreamPresenter(LogConsoleView *view, ChatStreamModel *store, QObject *parent = nullptr);

	ChatStreamModel *store() const;
	LogConsoleView *view() const;

	/// The connection epoch id-based matching is currently valid in (bumped
	/// by the owner at every connection lifecycle start).
	void setEpoch(quint64 epoch);
	/// The currently tracked channel (nullopt = none; Selected Channel is
	/// unavailable then). An explicit optional state: 0 is the root
	/// channel's real id and a legitimate target.
	std::optional< unsigned int > selectedChannel() const { return m_selectedChannel; }
	/// The channel the Selected Channel filter tracks plus the live-subtree
	/// resolver used for tree-targeted entries. No selection (nullopt)
	/// means the Selected Channel filter matches nothing and an active one
	/// falls back to All (removed channel, disconnect). Re-setting the same
	/// id only refreshes the resolver.
	void setSelectedChannel(std::optional< unsigned int > channelId, const ChatStreamModel::TreeCoverage &treeCovers);
	/// Applies the entry cap to the store (iMaxLogBlocks as the chat-side
	/// budget) and rebuilds the view: a lowered cap takes effect immediately,
	/// not on the next append.
	void setEntryCap(int cap);

	ChatStreamModel::Filter filter() const;
	/// Switches the filter and rebuilds the view from the store (per-filter
	/// scroll is preserved; a filter visited for the first time starts at the
	/// bottom).
	void setFilter(ChatStreamModel::Filter filter);

	/// One accepted chat entry (HTML + metadata atomically): stores it,
	/// evicts in lockstep, renders iff it matches the active filter.
	/// \p viewVisible is whether the chat view is currently shown to the
	/// user (a matching live append while visible is seen immediately);
	/// it also refreshes the tracked visibility (setViewVisible).
	void appendEntry(ChatEntry entry, bool viewVisible);

	/// Whether the chat view is currently shown to the user. Rebuilds only
	/// mark their displayed matches seen while this is set: a background
	/// rebuild (settings-driven cap change, tree selection change while the
	/// Activity tab is active, minimal mode, hidden window) must keep the
	/// unread badges intact. MainWindow mirrors the view's Hide events
	/// here; Show flows through markDisplayed().
	void setViewVisible(bool visible);

	/// The rendered entries just became visible to the user (tab switch,
	/// return from minimal mode, window un-hide): they are seen now, and
	/// the view counts as visible until the next setViewVisible(false).
	void markDisplayed();

	/// Store-aware Clear: removes the ACTIVE filter's entries from the store
	/// and rebuilds. Entries hidden behind other filters survive.
	void clearActiveFilter();

	/// Badge sources, derived from the store's seen flags alone.
	int chatTabBadge() const;
	int filterBadge(ChatStreamModel::Filter filter) const;

signals:
	/// Anything that can change badge counts or the rendered content
	/// happened (append, eviction, rebuild, clear, markDisplayed).
	void stateChanged();

private:
	ChatStreamModel::FilterContext contextFor(ChatStreamModel::Filter filter) const;
	/// Rebuilds the view from the store through the current filter; every
	/// displayed match is marked seen.
	void rebuildView();

	LogConsoleView *m_view;
	ChatStreamModel *m_store;
	ChatStreamModel::Filter m_filter = ChatStreamModel::Filter::All;
	std::optional< unsigned int > m_selectedChannel;
	quint64 m_epoch = 0;
	ChatStreamModel::TreeCoverage m_treeCovers;
	/// Whether the chat view is currently shown to the user (see
	/// setViewVisible); gates the markSeen of rebuilds.
	bool m_viewVisible = false;
	/// Ids of the currently rendered entries, in render order (= store order
	/// restricted to matches). Evicted-and-rendered entries are always a
	/// prefix of this list.
	QVector< quint64 > m_renderedIds;
	/// Last known scroll position per filter.
	QMap< ChatStreamModel::Filter, int > m_filterScrolls;
};

#endif // MUMBLE_CHATSTREAMPRESENTER_H_

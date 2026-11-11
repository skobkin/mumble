// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the Mumble
// source tree or at <https://www.mumble.info/LICENSE>.

#include "ChatStreamPresenter.h"

#include <limits>

ChatStreamPresenter::ChatStreamPresenter(LogConsoleView *view, ChatStreamModel *store, QObject *parent)
	: QObject(parent), m_view(view), m_store(store) {
}

ChatStreamModel *ChatStreamPresenter::store() const {
	return m_store;
}

LogConsoleView *ChatStreamPresenter::view() const {
	return m_view;
}

void ChatStreamPresenter::setEpoch(quint64 epoch) {
	m_epoch = epoch;
}

void ChatStreamPresenter::setEntryCap(int cap) {
	m_store->setEntryCap(cap);
	rebuildView();
	emit stateChanged();
}

ChatStreamModel::FilterContext ChatStreamPresenter::contextFor(ChatStreamModel::Filter filter) const {
	ChatStreamModel::FilterContext ctx;
	ctx.filter            = filter;
	ctx.selectedChannelId = m_selectedChannel;
	ctx.currentEpoch      = m_epoch;
	ctx.treeCovers        = m_treeCovers;
	return ctx;
}

void ChatStreamPresenter::setSelectedChannel(unsigned int channelId, const ChatStreamModel::TreeCoverage &treeCovers) {
	// The resolver is always refreshed (it closes over live channel objects
	// that may have been rebuilt under the same ids).
	const bool changed = m_selectedChannel != channelId;
	m_selectedChannel  = channelId;
	m_treeCovers       = treeCovers;

	if (!changed) {
		return;
	}
	if (m_filter == ChatStreamModel::Filter::SelectedChannel) {
		if (channelId == 0) {
			// The tracked channel is gone (removed or disconnected): fall
			// back to All rather than freeze an empty view. The store and
			// its per-filter scrolls survive.
			setFilter(ChatStreamModel::Filter::All);
		} else {
			rebuildView();
			emit stateChanged();
		}
	}
}

ChatStreamModel::Filter ChatStreamPresenter::filter() const {
	return m_filter;
}

void ChatStreamPresenter::setFilter(ChatStreamModel::Filter filter) {
	if (filter == m_filter) {
		return;
	}
	// Selected Channel without a tracked channel behaves like no filter at
	// all; the owner keeps the button disabled, this keeps the state honest.
	if (filter == ChatStreamModel::Filter::SelectedChannel && m_selectedChannel == 0) {
		return;
	}

	m_filterScrolls.insert(m_filter, m_view->getLogScroll());
	m_filter = filter;
	rebuildView();

	// Per-filter scroll: revisit restores the saved position; a filter
	// visited for the first time starts at the bottom (where the newest
	// match was just rendered). setValue clamps to the scrollbar's range,
	// so a large value lands at the bottom.
	if (m_filterScrolls.contains(filter)) {
		m_view->setLogScroll(m_filterScrolls.value(filter));
	} else {
		m_view->setLogScroll(std::numeric_limits< int >::max());
	}
	emit stateChanged();
}

void ChatStreamPresenter::appendEntry(ChatEntry entry, bool viewVisible) {
	// Matching is decided against the active filter exactly as rendering is:
	// a non-matching entry stays in the store (never lost), a matching one
	// renders incrementally. Seen means actually displayed.
	const bool matching = m_store->matches(entry, contextFor(m_filter));
	entry.seen          = matching && viewVisible;

	const ChatStreamModel::AppendResult appended = m_store->append(entry);

	// Lockstep eviction. Evicted entries that were rendered form a prefix of
	// the rendered list (render order is store order): drop that prefix and
	// remove the same number of leading frames from the document - resources
	// and date separators go with them, same discipline as the block budget.
	int removeFromView = 0;
	for (quint64 evictedId : appended.evictedIds) {
		if (!m_renderedIds.isEmpty() && m_renderedIds.constFirst() == evictedId) {
			m_renderedIds.removeFirst();
			++removeFromView;
		}
	}
	if (removeFromView > 0) {
		m_view->removeLeadingRenderedEntries(removeFromView);
	}

	if (matching) {
		m_renderedIds.append(appended.assignedId);
		m_view->appendEntry(entry.timestamp, entry.consoleHtml, entry.ownMessage, entry.renderSettings);
	}
	emit stateChanged();
}

void ChatStreamPresenter::markDisplayed() {
	m_store->markSeen(m_renderedIds);
	emit stateChanged();
}

void ChatStreamPresenter::clearActiveFilter() {
	// Remove from the authoritative store, not from the widget: a bare clear
	// would be undone by the next rebuild. Entries the active filter hides
	// survive and stay reachable through their own filters.
	m_store->removeMatching(contextFor(m_filter));
	rebuildView();
	emit stateChanged();
}

int ChatStreamPresenter::chatTabBadge() const {
	return m_store->unseenTotal();
}

int ChatStreamPresenter::filterBadge(ChatStreamModel::Filter filter) const {
	return m_store->unseenCount(contextFor(filter));
}

void ChatStreamPresenter::rebuildView() {
	QVector< LogConsoleView::EntrySource > sources;
	QVector< quint64 > ids;
	const ChatStreamModel::FilterContext ctx = contextFor(m_filter);
	for (int i = 0; i < m_store->size(); ++i) {
		const ChatEntry &entry = m_store->at(i);
		if (m_store->matches(entry, ctx)) {
			sources.append({ entry.timestamp, entry.consoleHtml, entry.ownMessage, entry.renderSettings });
			ids.append(entry.id);
		}
	}
	m_view->rebuild(sources);
	m_renderedIds = ids;
	// Displaying a filter's matches is what makes them seen - this drains
	// the badge of the filter being switched to (and only that one).
	m_store->markSeen(ids);
}

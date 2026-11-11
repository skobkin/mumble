// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the Mumble
// source tree or at <https://www.mumble.info/LICENSE>.

#include "ChatStreamModel.h"

void ChatStreamModel::setEntryCap(int cap) {
	m_entryCap = cap;
	// A lowered cap trims immediately, like the document block budget: the
	// store never holds more than it is allowed to, not even until the next
	// append.
	while (m_entryCap > 0 && m_entries.size() > static_cast< qsizetype >(m_entryCap)) {
		m_entries.removeFirst();
	}
}

int ChatStreamModel::entryCap() const {
	return m_entryCap;
}

int ChatStreamModel::size() const {
	return static_cast< int >(m_entries.size());
}

const ChatEntry &ChatStreamModel::at(int index) const {
	return m_entries.at(index);
}

const ChatEntry *ChatStreamModel::findById(quint64 id) const {
	for (const ChatEntry &entry : m_entries) {
		if (entry.id == id) {
			return &entry;
		}
	}
	return nullptr;
}

ChatStreamModel::AppendResult ChatStreamModel::append(ChatEntry entry) {
	entry.id = m_nextId++;
	AppendResult result;
	result.assignedId = entry.id;
	m_entries.append(entry);

	while (m_entryCap > 0 && m_entries.size() > static_cast< qsizetype >(m_entryCap)) {
		result.evictedIds.append(m_entries.constFirst().id);
		m_entries.removeFirst();
	}
	return result;
}

QVector< quint64 > ChatStreamModel::removeMatching(const FilterContext &ctx) {
	QVector< quint64 > removed;
	for (int i = 0; i < m_entries.size();) {
		if (matches(m_entries.at(i), ctx)) {
			removed.append(m_entries.at(i).id);
			m_entries.removeAt(i);
		} else {
			++i;
		}
	}
	return removed;
}

bool ChatStreamModel::matches(const ChatEntry &entry, const FilterContext &ctx) const {
	switch (ctx.filter) {
		case Filter::All:
			return true;
		case Filter::Direct:
			return isDirect(entry.routing);
		case Filter::SelectedChannel: {
			// Id-based matching is only valid inside the entry's connection
			// epoch: after a reconnect the same ids mean different channels,
			// so historical entries stay in All/Direct but never leak into
			// (or hide from) the channel view under a new topology.
			if (!entry.routing.present || entry.routing.epoch != ctx.currentEpoch || ctx.selectedChannelId == 0) {
				return false;
			}
			for (unsigned int channelId : entry.routing.channelIds) {
				if (channelId == ctx.selectedChannelId) {
					return true;
				}
			}
			for (unsigned int rootId : entry.routing.treeIds) {
				if (ctx.treeCovers && ctx.treeCovers(rootId, ctx.selectedChannelId)) {
					return true;
				}
			}
			return false;
		}
	}
	return false;
}

bool ChatStreamModel::isDirect(const ChatRoutingInfo &routing) {
	return routing.present && routing.treeIds.isEmpty() && routing.channelIds.isEmpty()
		   && !routing.directSessions.isEmpty();
}

int ChatStreamModel::unseenCount(const FilterContext &ctx) const {
	int count = 0;
	for (const ChatEntry &entry : m_entries) {
		if (!entry.seen && matches(entry, ctx)) {
			++count;
		}
	}
	return count;
}

int ChatStreamModel::unseenTotal() const {
	int count = 0;
	for (const ChatEntry &entry : m_entries) {
		if (!entry.seen) {
			++count;
		}
	}
	return count;
}

void ChatStreamModel::markSeen(const QVector< quint64 > &ids) {
	for (quint64 id : ids) {
		for (ChatEntry &entry : m_entries) {
			if (entry.id == id) {
				entry.seen = true;
				break;
			}
		}
	}
}

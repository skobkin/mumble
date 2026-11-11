// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of this
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_CONSOLE_UNREAD_TRACKER_H_
#define MUMBLE_MUMBLE_CONSOLE_UNREAD_TRACKER_H_

#include "Log.h"

#include <QVector>

/// Unread bookkeeping for one console destination's tab badge: the message
/// types appended while the destination was not visible.
///
/// The badge must only count entries the destination's document still
/// retains: the whole-entry block budget evicts old entries without
/// consulting the badge, so eviction has to fold back into this
/// bookkeeping. Unseen entries are always the newest ones (they were
/// appended after everything the user has already seen), so folding is
/// exact: whenever the document loses entries, retainOnly() drops the
/// equally oldest unseen types, which also recomputes the
/// Warning/CriticalError latch - it can only survive in entries that still
/// exist.
class ConsoleUnreadTracker {
public:
	/// An entry was appended while its destination was hidden.
	void append(Log::MsgType mt) { m_types.append(mt); }

	/// The destination became visible: everything is seen now.
	void clear() { m_types.clear(); }

	/// The destination's document retains \p retainedEntries entries in
	/// total (seen and unseen). Any unseen entries beyond that were evicted
	/// and stop counting.
	void retainOnly(int retainedEntries) {
		if (m_types.size() > retainedEntries) {
			m_types.remove(0, static_cast< int >(m_types.size()) - retainedEntries);
		}
	}

	int unreadCount() const { return static_cast< int >(m_types.size()); }

	/// Whether any still-unseen entry is a Warning or CriticalError.
	bool hasUnseenWarnings() const {
		for (Log::MsgType mt : m_types) {
			if (mt == Log::Warning || mt == Log::CriticalError) {
				return true;
			}
		}
		return false;
	}

private:
	QVector< Log::MsgType > m_types;
};

#endif // MUMBLE_MUMBLE_CONSOLE_UNREAD_TRACKER_H_

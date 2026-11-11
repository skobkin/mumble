// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the Mumble
// source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_CONNECTIONEPOCH_H_
#define MUMBLE_CONNECTIONEPOCH_H_

#include <atomic>

/// Monotonic count of server connections opened by this process.
///
/// Channel and session ids are connection-local: two connections may reuse the
/// same ids for unrelated channels and users. Any id-based matching (the
/// Selected Channel chat filter) is therefore only valid between entries and a
/// query that share an epoch - historical entries render in the All and Direct
/// views but are never re-interpreted against the current connection's
/// topology.
///
/// The counter is bumped by MainWindow at the start of every connection
/// lifecycle (initial connect, reconnect, server switch) and read by message
/// producers from any thread. Producers must capture the epoch at production
/// time: a queued entry delivered after a reconnect must never inherit the
/// newer connection's epoch.
namespace ConnectionEpoch {

namespace detail {
	inline std::atomic< quint64 > &counter() {
		static std::atomic< quint64 > value{ 0 };
		return value;
	}
} // namespace detail

/// The epoch of the connection that is currently being established or used.
/// Zero means "never connected".
inline quint64 current() {
	return detail::counter().load(std::memory_order_relaxed);
}

/// Starts a new epoch. Called at the start of every connection lifecycle.
inline void bump() {
	detail::counter().fetch_add(1, std::memory_order_relaxed);
}

} // namespace ConnectionEpoch

#endif // MUMBLE_CONNECTIONEPOCH_H_

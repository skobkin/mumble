// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the Mumble
// source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_CHATTARGET_H_
#define MUMBLE_CHATTARGET_H_

#include <QtCore/QString>

class Channel;
class ClientUser;

/// Where the composer sends, resolved from the same inputs everywhere it
/// matters: the placeholder (updateChatBar), the actual send
/// (sendChatbarMessage, which re-resolves - the placeholder is a
/// presentation, never a routing input) and the composer enablement
/// (updateMenuPermissions). Pure value data over live connection objects;
/// Global-free, so the decision table is unit-testable with stack objects.
struct ChatTarget {
	enum class Kind {
		Disconnected, ///< No connection (or a disconnect race): nothing to send.
		Channel,      ///< A channel message (the selected channel, or the self channel).
		User          ///< A private message to the selected user.
	};

	Kind kind = Kind::Disconnected;
	/// The Channel-kind target's channel.
	Channel *channel = nullptr;
	/// The User-kind target's user.
	ClientUser *user = nullptr;
	/// The channel whose permissions gate sending from the composer. The
	/// rule is channel-scoped even for user targets (it predates this
	/// split and is preserved byte-for-byte): for a user selection it is
	/// the SELECTED ROW's channel - for a listener-proxy row the listened
	/// channel, not the user's own. Null when disconnected.
	Channel *permissionChannel = nullptr;
	/// True when the Channel target came from the self-channel fallback
	/// (no usable selection / selection-use disabled) rather than from an
	/// explicit channel selection.
	bool fromSelfChannelFallback = false;
	/// True when the User target's row is a channel-listener proxy (the
	/// user sits in a different channel than the selected row). The send
	/// still addresses the user; the flag is presentation-only.
	bool viaListenerProxy = false;

	bool isValid() const;
	/// The target's display name (raw - escape at the presentation site).
	QString displayName() const;
};

/// The decision table, reproducing the former inline target logic of
/// updateChatBar()/sendChatbarMessage() byte-for-byte plus one defensive
/// fix: a disconnect race (session id still set, self user object already
/// gone) used to dereference null here; it now degrades to Disconnected.
///
/// \p selectedUser / \p selectedChannel are the current tree selection's
/// user and channel (a user row yields both: the user and the row's
/// channel); \p selectionIsListenerProxy whether that row is a
/// channel-listener proxy; \p self the local user (null when
/// disconnected); \p useSelection the bChatBarUseSelection setting.
ChatTarget resolveChatTarget(ClientUser *selectedUser, Channel *selectedChannel, bool selectionIsListenerProxy,
							 ClientUser *self, bool useSelection);

#endif // MUMBLE_CHATTARGET_H_

// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the Mumble
// source tree or at <https://www.mumble.info/LICENSE>.

#include "ChatTarget.h"

#include "Channel.h"
#include "ClientUser.h"

bool ChatTarget::isValid() const {
	return kind != Kind::Disconnected;
}

QString ChatTarget::displayName() const {
	switch (kind) {
		case Kind::Channel:
			return channel ? channel->qsName : QString();
		case Kind::User:
			return user ? user->qsName : QString();
		case Kind::Disconnected:
			break;
	}
	return QString();
}

ChatTarget resolveChatTarget(ClientUser *selectedUser, Channel *selectedChannel, bool selectionIsListenerProxy,
							 ClientUser *self, bool useSelection) {
	ChatTarget target;

	// Defensive degradation instead of the former null dereference: the
	// callers used to fetch ClientUser::get(uiSession)->cChannel inline,
	// which races a disconnect teardown.
	if (!self || !self->cChannel) {
		return target;
	}

	if (!useSelection || !selectedUser || selectedUser == self) {
		// Channel-tree target: the selected channel, falling back to the
		// self channel when there is no usable selection.
		if (!useSelection || !selectedChannel) {
			selectedChannel                = self->cChannel;
			target.fromSelfChannelFallback = true;
		}
		target.kind              = ChatTarget::Kind::Channel;
		target.channel           = selectedChannel;
		target.permissionChannel = selectedChannel;
	} else {
		// User target. The permission channel is the selected row's channel
		// (the listened channel for a proxy row), matching the enablement
		// rule that predates the resolver.
		target.kind              = ChatTarget::Kind::User;
		target.user              = selectedUser;
		target.permissionChannel = selectedChannel;
		target.viaListenerProxy  = selectionIsListenerProxy;
	}

	return target;
}

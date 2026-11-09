// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the Mumble source tree
// or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_WIDGETS_CONNECTIONHEADER_H_
#define MUMBLE_WIDGETS_CONNECTIONHEADER_H_

#include <QWidget>

class QAction;
class QLabel;

/// Status strip above the main window's content: a connection-state line with
/// the server description and user/channel counts, plus a row of tool buttons
/// that are pure projections of the existing server actions (identity,
/// enabled/checked state and triggering all live on the action layer).
///
/// Presentation only: MainWindow pushes display snapshots at the connection
/// lifecycle points (connected, synchronized, disconnected). This class
/// derives nothing itself, has no Global dependency and owns no connection
/// state machine, so it can be exercised in isolated tests with plain
/// QActions.
class ConnectionHeader : public QWidget {
	Q_OBJECT

public:
	enum class ConnectionState { Disconnected, Connecting, Synchronized };

	/// \param connectAction action for the connect button.
	/// \param disconnectAction action for the disconnect button.
	/// \param informationAction action for the server-information button.
	/// \param searchAction action for the search button.
	explicit ConnectionHeader(QAction *connectAction, QAction *disconnectAction, QAction *informationAction,
							   QAction *searchAction, QWidget *parent = nullptr);

	/// Replaces the displayed snapshot wholesale. A disconnected snapshot
	/// (empty description, zero counts) clears every trace of a previous
	/// connection's stale text.
	void updateConnectionSnapshot(ConnectionState state, const QString &serverDescription, int userCount,
								  int channelCount);

	ConnectionState connectionState() const;

	/// The state/server line (left, expanding).
	QLabel *statusLabel() const;
	/// The user/channel count line (right of the status line).
	QLabel *countLabel() const;

private:
	QLabel *m_statusLabel;
	QLabel *m_countLabel;
	ConnectionState m_state = ConnectionState::Disconnected;
};

#endif // MUMBLE_WIDGETS_CONNECTIONHEADER_H_

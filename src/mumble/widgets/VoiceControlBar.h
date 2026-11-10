// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file at the root of the Mumble source tree or at
// <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_WIDGETS_VOICECONTROLBAR_H_
#define MUMBLE_WIDGETS_VOICECONTROLBAR_H_

#include "Settings.h"

#include <QIcon>
#include <QVector>
#include <QWidget>

class QAction;
class QLabel;
class QToolButton;
class MUComboBox;

/// Persistent bottom control strip of the redesigned main window: mute,
/// deafen and settings tool buttons that are pure projections of the existing
/// actions (identity, enabled/checked state and triggering all live on the
/// action layer), plus the local user's identity and a voice-status area.
///
/// Presentation only: MainWindow pushes complete VoiceStatusSnapshot values
/// at the same lifecycle points that already refresh the tray icon. This
/// class derives nothing itself, has no Global dependency and owns no
/// connection state, so it can be exercised in isolated tests with plain
/// QActions.
///
/// The visible status line shows only the highest-priority active status;
/// the complete list of simultaneously active statuses is exposed as the
/// tooltip of the status area AND of the always-present identity label
/// (mirrored into its accessibleDescription), so a self-only muted state -
/// where the status area is empty - still carries its "(self)" lines.
///
/// Self mute/self deafen never appear in the visible status: the buttons
/// already present them through their checked state.
class VoiceControlBar : public QWidget {
	Q_OBJECT

public:
	/// Facts about the local user, resolved by MainWindow at push time and
	/// never cached here. A default-constructed snapshot is the cleared,
	/// disconnected presentation.
	struct VoiceStatusSnapshot {
		/// Local user's name; empty when not synchronized.
		QString userName;
		/// Current channel's name; empty when none.
		QString channelName;
		/// Local user's talking state. Never inferred from the actions.
		Settings::TalkState talkState = Settings::Passive;
		/// Self mute (Settings::bMute). Tooltip-only information; the mute
		/// button is the authoritative presentation.
		bool selfMuted = false;
		/// Self deafen (Settings::bDeaf). Tooltip-only, like selfMuted.
		bool selfDeafened = false;
		/// Muted by the server/admin (ClientUser::bMute). NOT self mute.
		bool serverMuted = false;
		/// Deafened by the server (ClientUser::bDeaf). NOT self deafen.
		bool serverDeafened = false;
		/// Suppressed by the server (ClientUser::bSuppress).
		bool suppressed = false;
		/// Transient push-to-mute (Global::bPushToMute). Never a persistent
		/// self-mute presentation.
		bool pushToMute = false;
	};

	/// The one status presented in the visible line. Priority mirrors the
	/// tray icon derivation minus the self states the buttons present.
	enum class StatusKind {
		None,
		ServerDeafened,
		ServerMuted,
		Suppressed,
		PushToMute,
		Talking,
		Whispering,
		Shouting,
	};

	/// \param muteAction action for the mute button (qaAudioMute).
	/// \param deafenAction action for the deafen button (qaAudioDeaf).
	/// \param configureAction action for the settings button (qaConfigDialog).
	explicit VoiceControlBar(QAction *muteAction, QAction *deafenAction, QAction *configureAction,
							 QWidget *parent = nullptr);

	/// Replaces the displayed snapshot wholesale. A default-constructed
	/// snapshot clears every trace of a previous connection's stale text.
	void updateVoiceStatusSnapshot(const VoiceStatusSnapshot &snapshot);

	const VoiceStatusSnapshot &snapshot() const;
	StatusKind statusKind() const;
	/// The complete multi-line status text (also used as the tooltip), or
	/// an empty string when nothing is active.
	QString statusToolTip() const;

	/// The head of the bar's focus chain, for the embedding code's
	/// QWidget::setTabOrder() link. The bar itself never takes focus; the
	/// head is the transmission-mode combo (a hidden widget is skipped by
	/// focus traversal, so while the combo is hidden traversal effectively
	/// starts at the mute button).
	QWidget *focusEntryWidget() const;

	/// The transmission-mode selector, born inside the bar and never
	/// reparented (constructing a widget inside a QWidgetAction-owned
	/// toolbar slot would tie its lifetime to that action). The bar adds no
	/// items and connects nothing: the host fills the items, owns the
	/// activated handling and applies the visibility setting; the combo
	/// starts hidden.
	MUComboBox *transmitModeComboBox() const;

	/// The identity label (elides its full text when narrow).
	QLabel *identityLabel() const;
	/// The visible status text label (elides; may fall back to empty when
	/// extremely narrow, leaving icon + tooltip).
	QLabel *statusLabel() const;
	/// The status icon label (empty pixmap when no visible status).
	QLabel *statusIconLabel() const;

private:
	QLabel *m_identityLabel;
	QLabel *m_statusIconLabel;
	QLabel *m_statusLabel;
	QToolButton *m_muteButton;
	QToolButton *m_deafenButton;
	QToolButton *m_configureButton;
	MUComboBox *m_transmitModeComboBox;
	QVector< QIcon > m_statusIcons;
	VoiceStatusSnapshot m_snapshot;
	StatusKind m_statusKind = StatusKind::None;
	QString m_statusText;
	QString m_statusToolTip;

	void refreshDisplays();
};

#endif // MUMBLE_WIDGETS_VOICECONTROLBAR_H_

// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file at the root of the Mumble source tree or at
// <https://www.mumble.info/LICENSE>.

#include "widgets/VoiceControlBar.h"

#include "SvgIcon.h"
#include "widgets/MUComboBox.h"

#include <QAction>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QStringList>
#include <QToolButton>

namespace {

/// A label that keeps its full text separate from the displayed one and
/// re-elides whenever its own geometry changes, so the two text areas of the
/// bar can shrink to nothing (QSizePolicy::Ignored) without ever forcing the
/// surrounding window wider.
class ElidedLabel : public QLabel {
public:
	using QLabel::QLabel;

	void setFullText(const QString &text) {
		m_fullText = text;
		updateElision();
	}

	const QString &fullText() const { return m_fullText; }

protected:
	void resizeEvent(QResizeEvent *event) Q_DECL_OVERRIDE {
		QLabel::resizeEvent(event);
		updateElision();
	}

private:
	void updateElision() {
		if (m_fullText.isEmpty()) {
			clear();
			return;
		}
		// Graceful narrow fallback: if not even an ellipsis plus a character
		// fits, show nothing at all - the status information keeps living in
		// the (always complete) tooltip of this label and its neighbors.
		const int fallbackFloor = 2 * fontMetrics().horizontalAdvance(QLatin1Char('x'));
		if (width() < fallbackFloor) {
			clear();
			return;
		}
		setText(fontMetrics().elidedText(m_fullText, Qt::ElideRight, width()));
	}

	QString m_fullText;
};

/// Localized text of the one visible status.
QString visibleStatusText(VoiceControlBar::StatusKind kind) {
	switch (kind) {
		case VoiceControlBar::StatusKind::ServerDeafened:
			return VoiceControlBar::tr("Deafened by server");
		case VoiceControlBar::StatusKind::ServerMuted:
			return VoiceControlBar::tr("Muted by server");
		case VoiceControlBar::StatusKind::Suppressed:
			return VoiceControlBar::tr("Suppressed");
		case VoiceControlBar::StatusKind::PushToMute:
			return VoiceControlBar::tr("Push-to-mute");
		case VoiceControlBar::StatusKind::Talking:
			return VoiceControlBar::tr("Talking");
		case VoiceControlBar::StatusKind::Whispering:
			return VoiceControlBar::tr("Whispering");
		case VoiceControlBar::StatusKind::Shouting:
			return VoiceControlBar::tr("Shouting");
		case VoiceControlBar::StatusKind::None:
			break;
	}
	return QString();
}

QString talkStateText(Settings::TalkState talkState) {
	switch (talkState) {
		case Settings::Talking:
		case Settings::MutedTalking:
			return VoiceControlBar::tr("Talking");
		case Settings::Whispering:
			return VoiceControlBar::tr("Whispering");
		case Settings::Shouting:
			return VoiceControlBar::tr("Shouting");
		case Settings::Passive:
			break;
	}
	return QString();
}

/// The complete list of simultaneously active statuses, one localized line
/// each, in a fixed order. Self and server states are separate lines and are
/// never merged into one (matching the wording precedent of
/// ClientUser's flag list).
QString completeStatusText(const VoiceControlBar::VoiceStatusSnapshot &snapshot) {
	QStringList lines;
	if (snapshot.selfDeafened)
		lines << VoiceControlBar::tr("Deafened (self)");
	if (snapshot.selfMuted)
		lines << VoiceControlBar::tr("Muted (self)");
	if (snapshot.serverDeafened)
		lines << VoiceControlBar::tr("Deafened by server");
	if (snapshot.serverMuted)
		lines << VoiceControlBar::tr("Muted by server");
	if (snapshot.suppressed)
		lines << VoiceControlBar::tr("Suppressed");
	if (snapshot.pushToMute)
		lines << VoiceControlBar::tr("Push-to-mute");
	const QString talking = talkStateText(snapshot.talkState);
	if (!talking.isEmpty())
		lines << talking;
	return lines.join(QLatin1Char('\n'));
}

VoiceControlBar::StatusKind highestStatusKind(const VoiceControlBar::VoiceStatusSnapshot &snapshot) {
	if (snapshot.serverDeafened)
		return VoiceControlBar::StatusKind::ServerDeafened;
	if (snapshot.serverMuted)
		return VoiceControlBar::StatusKind::ServerMuted;
	if (snapshot.suppressed)
		return VoiceControlBar::StatusKind::Suppressed;
	if (snapshot.pushToMute)
		return VoiceControlBar::StatusKind::PushToMute;
	switch (snapshot.talkState) {
		case Settings::Whispering:
			return VoiceControlBar::StatusKind::Whispering;
		case Settings::Shouting:
			return VoiceControlBar::StatusKind::Shouting;
		case Settings::Talking:
		case Settings::MutedTalking:
			return VoiceControlBar::StatusKind::Talking;
		case Settings::Passive:
			break;
	}
	return VoiceControlBar::StatusKind::None;
}

/// Renders a mnemonic-stripped, user-visible name for accessibility from an
/// action's text ("M&ute Self" -> "Mute Self"). QWidget's automatic
/// accessibleName is empty for tool buttons bound via setDefaultAction, and
/// the fallback (the raw text) would announce the ampersand marker.
QString strippedActionName(const QAction *action) {
	QString name = action->text();
	name.remove(QLatin1Char('&'));
	return name;
}

} // namespace

VoiceControlBar::VoiceControlBar(QAction *muteAction, QAction *deafenAction, QAction *configureAction,
								 QWidget *parent)
	: QWidget(parent), m_identityLabel(new ElidedLabel(this)), m_statusIconLabel(new QLabel(this)),
	  m_statusLabel(new ElidedLabel(this)), m_muteButton(new QToolButton(this)),
	  m_deafenButton(new QToolButton(this)), m_configureButton(new QToolButton(this)),
	  m_transmitModeComboBox(new MUComboBox(this)),
	  m_statusIcons(static_cast< int >(StatusKind::Shouting) + 1) {
	// The bar itself is never a tab stop; traversal enters its children
	// through focusEntryWidget().
	setFocusPolicy(Qt::NoFocus);

	// Only the two text areas shrink (down to nothing); the buttons keep
	// their minimum, so the control strip never loses a button and can never
	// force overlap.
	m_identityLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	m_statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

	// The same skin resources the tray icon presentation uses.
	SvgIcon::addSvgPixmapsToIcon(m_statusIcons[static_cast< int >(StatusKind::ServerDeafened)],
								QLatin1String("skin:deafened_server.svg"));
	SvgIcon::addSvgPixmapsToIcon(m_statusIcons[static_cast< int >(StatusKind::ServerMuted)],
								QLatin1String("skin:muted_server.svg"));
	SvgIcon::addSvgPixmapsToIcon(m_statusIcons[static_cast< int >(StatusKind::Suppressed)],
								QLatin1String("skin:muted_suppressed.svg"));
	SvgIcon::addSvgPixmapsToIcon(m_statusIcons[static_cast< int >(StatusKind::PushToMute)],
								QLatin1String("skin:muted_pushtomute.svg"));
	SvgIcon::addSvgPixmapsToIcon(m_statusIcons[static_cast< int >(StatusKind::Talking)],
								QLatin1String("skin:talking_on.svg"));
	SvgIcon::addSvgPixmapsToIcon(m_statusIcons[static_cast< int >(StatusKind::Whispering)],
								QLatin1String("skin:talking_whisper.svg"));
	SvgIcon::addSvgPixmapsToIcon(m_statusIcons[static_cast< int >(StatusKind::Shouting)],
								QLatin1String("skin:talking_alt.svg"));

	// The transmission-mode selector is born in its final container and is
	// never reparented or wrapped in a QWidgetAction, so no toolbar-action
	// teardown can ever destroy it. The host owns contents, activation
	// handling and visibility; the combo starts hidden.
	m_transmitModeComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	m_transmitModeComboBox->hide();

	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setContentsMargins(6, 2, 6, 2);
	layout->setSpacing(4);
	layout->addWidget(m_identityLabel, 2);
	layout->addWidget(m_statusIconLabel, 0);
	layout->addWidget(m_statusLabel, 1);
	layout->addWidget(m_transmitModeComboBox, 0);

	// The buttons delegate everything to the existing actions; this class
	// never triggers or rewrites them (same contract as ConnectionHeader).
	for (auto [button, action] : { std::pair{ m_muteButton, muteAction }, std::pair{ m_deafenButton, deafenAction },
								   std::pair{ m_configureButton, configureAction } }) {
		button->setDefaultAction(action);
		button->setAutoRaise(true);
		button->setAccessibleName(strippedActionName(action));
		layout->addWidget(button, 0);
	}

	// Explicit focus chain so Tab order does not depend on creation order:
	// combo -> mute -> deaf -> config. The combo is the chain head; while it
	// is hidden, focus traversal skips it and effectively starts at the mute
	// button.
	QWidget::setTabOrder(m_transmitModeComboBox, m_muteButton);
	QWidget::setTabOrder(m_muteButton, m_deafenButton);
	QWidget::setTabOrder(m_deafenButton, m_configureButton);
}

void VoiceControlBar::updateVoiceStatusSnapshot(const VoiceStatusSnapshot &snapshot) {
	m_snapshot     = snapshot;
	m_statusKind   = highestStatusKind(snapshot);
	m_statusText   = visibleStatusText(m_statusKind);
	m_statusToolTip = completeStatusText(snapshot);

	refreshDisplays();
}

const VoiceControlBar::VoiceStatusSnapshot &VoiceControlBar::snapshot() const {
	return m_snapshot;
}

VoiceControlBar::StatusKind VoiceControlBar::statusKind() const {
	return m_statusKind;
}

QString VoiceControlBar::statusToolTip() const {
	return m_statusToolTip;
}

QWidget *VoiceControlBar::focusEntryWidget() const {
	return m_transmitModeComboBox;
}

MUComboBox *VoiceControlBar::transmitModeComboBox() const {
	return m_transmitModeComboBox;
}

QLabel *VoiceControlBar::identityLabel() const {
	return m_identityLabel;
}

QLabel *VoiceControlBar::statusLabel() const {
	return m_statusLabel;
}

QLabel *VoiceControlBar::statusIconLabel() const {
	return m_statusIconLabel;
}

void VoiceControlBar::refreshDisplays() {
	QString identityText;
	if (!m_snapshot.userName.isEmpty()) {
		if (m_snapshot.channelName.isEmpty())
			identityText = m_snapshot.userName;
		else
			identityText = tr("%1 in %2").arg(m_snapshot.userName).arg(m_snapshot.channelName);
	}

	static_cast< ElidedLabel * >(m_identityLabel)->setFullText(identityText);
	static_cast< ElidedLabel * >(m_statusLabel)->setFullText(m_statusText);

	if (m_statusKind == StatusKind::None) {
		m_statusIconLabel->clear();
	} else {
		const int iconEdge = fontMetrics().height();
		m_statusIconLabel->setPixmap(
			m_statusIcons[static_cast< int >(m_statusKind)].pixmap(QSize(iconEdge, iconEdge)));
	}

	// The complete status is reachable even where the visible line is empty
	// or elided: tooltip on the status area AND on the always-present
	// identity label, whose accessibleDescription additionally serves screen
	// readers without a hover.
	m_identityLabel->setToolTip(m_statusToolTip);
	m_statusIconLabel->setToolTip(m_statusToolTip);
	m_statusLabel->setToolTip(m_statusToolTip);
	m_identityLabel->setAccessibleDescription(m_statusToolTip);

	m_identityLabel->setAccessibleName(identityText);
	m_statusLabel->setAccessibleName(m_statusText);
	m_statusIconLabel->setAccessibleName(m_statusText);
}

// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file at the root of the Mumble source tree or at
// <https://www.mumble.info/LICENSE>.

// Component tests of the persistent voice control bar (Stage 4 of the new
// UI). The bar is exercised exactly as the live MainWindow drives it: plain
// QActions injected through the constructor and wholesale snapshot pushes.
// The bar has no Global/ServerHandler/ClientUser dependency, so nothing needs
// to be stubbed. MainWindow-side wiring (settingsAccepted refresh, push
// timing, userMoved consumption) is manual-checklist coverage - a real
// MainWindow/ConfigDialog cannot be constructed in a test harness.

#include <QtTest>

#include "widgets/VoiceControlBar.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QLineEdit>
#include <QPointer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {
/// Production binds actions that carry icons (and mute/deaf are checkable),
/// which makes the tool buttons render icon-only at their compact minimum.
QIcon testIcon() {
	QPixmap pixmap(16, 16);
	pixmap.fill(Qt::darkGreen);
	return QIcon(pixmap);
}
} // namespace

class TestVoiceControlBar : public QObject {
	Q_OBJECT

private slots:
	// Establishes the "skin:" icon search path (the live client does this in
	// Themes::setTheme; the Default theme resources are compiled into the
	// linked client object library).
	void initTestCase();

	// Fresh actions and bar for every test.
	void init();
	void cleanup();

	// Exactly three tool buttons, bound to the injected actions in order,
	// auto-raised.
	void buttonsBindExistingActions();

	// The actions remain the single authority: their checked/enabled state is
	// projected onto the buttons; the bar never writes back.
	void actionCheckedStatePropagates();
	void actionEnabledStatePropagates();
	void clickTriggersActionExactlyOnce();
	void disabledActionIgnoresClick();

	// Snapshot contract.
	void initialSnapshotIsCleared();
	void snapshotReplacesStaleIdentityAndStatus();
	void statusPrecedenceMatrix();
	void combinedStatusesTooltipComplete();
	void pushToMuteIsNotSelfMute();
	void talkingNotInferredFromActionStates();
	void snapshotPushDoesNotTriggerActions();

	// Accessibility: explicit names (mnemonic-stripped), complete status
	// mirrored into the identity label's accessibleDescription.
	void accessibleNames();

	// Responsive geometry: both text areas elide, buttons stay fully visible,
	// no overlap, no uncontrolled minimum growth.
	void responsiveLayoutAtSupportedWidths();
	void responsiveLayoutLargeFonts();
	void minimumWidthFloor();

	// Focus chain.
	void focusPolicies();
	void focusTraversalChain();
	void focusEntryWidgetIsChainHead();

	// Ownership: destroying the bar destroys its widgets, never the actions.
	void destructionCleansUpOwnedWidgetsOnly();

private:
	/// The button bound to the given action (nullptr when not bound).
	QToolButton *buttonFor(const QAction *action) const {
		const auto buttons = m_bar->findChildren< QToolButton * >();
		for (QToolButton *button : buttons) {
			if (button->defaultAction() == action) {
				return button;
			}
		}
		return nullptr;
	}

	VoiceControlBar::VoiceStatusSnapshot snapshotWithName(const QString &user, const QString &channel) const {
		VoiceControlBar::VoiceStatusSnapshot snapshot;
		snapshot.userName    = user;
		snapshot.channelName = channel;
		return snapshot;
	}

	/// Asserts the layout contract at the bar's current size: every child
	// fully inside the bar rect, children mutually disjoint, all three
	// buttons visible and at least at their minimum, and the row height
	// staying within ~2.5x a tool button's hint.
	void verifyLayoutContract() const;

	/// Shows the bar at a generous size where short texts fit unelided.
	void showWide() {
		m_bar->resize(800, m_bar->sizeHint().height());
		m_bar->show();
		QApplication::processEvents();
	}

	QAction *m_muteAction    = nullptr;
	QAction *m_deafenAction  = nullptr;
	QAction *m_configAction  = nullptr;
	VoiceControlBar *m_bar   = nullptr;
};

void TestVoiceControlBar::initTestCase() {
	QDir::setSearchPaths(QLatin1String("skin"), { QLatin1String(":/themes/Default") });
}

void TestVoiceControlBar::init() {
	// Mnemonic markers in the texts exercise the accessibleName stripping.
	// Checkability and icons mirror the production actions (qaAudioMute /
	// qaAudioDeaf are checkable and carry icons; qaConfigDialog is a plain
	// triggered action with an icon).
	m_muteAction = new QAction(QStringLiteral("M&ute Self"), this);
	m_muteAction->setCheckable(true);
	m_muteAction->setIcon(testIcon());
	m_deafenAction = new QAction(QStringLiteral("&Deafen Self"), this);
	m_deafenAction->setCheckable(true);
	m_deafenAction->setIcon(testIcon());
	m_configAction = new QAction(QStringLiteral("Co&nfigure"), this);
	m_configAction->setIcon(testIcon());
	m_bar = new VoiceControlBar(m_muteAction, m_deafenAction, m_configAction);
}

void TestVoiceControlBar::cleanup() {
	delete m_bar;
	m_bar = nullptr;
	// The actions are parented to the test object and outlive the bar; drop
	// them explicitly so every test starts fresh.
	delete m_muteAction;
	delete m_deafenAction;
	delete m_configAction;
	m_muteAction = m_deafenAction = m_configAction = nullptr;
}

void TestVoiceControlBar::buttonsBindExistingActions() {
	showWide();

	const auto buttons = m_bar->findChildren< QToolButton * >();
	QCOMPARE(buttons.size(), 3);

	QVERIFY(buttonFor(m_muteAction));
	QVERIFY(buttonFor(m_deafenAction));
	QVERIFY(buttonFor(m_configAction));
	QCOMPARE(buttonFor(m_muteAction)->defaultAction(), m_muteAction);
	QCOMPARE(buttonFor(m_deafenAction)->defaultAction(), m_deafenAction);
	QCOMPARE(buttonFor(m_configAction)->defaultAction(), m_configAction);

	for (QToolButton *button : buttons) {
		QVERIFY(button->autoRaise());
	}
}

void TestVoiceControlBar::actionCheckedStatePropagates() {
	showWide();

	QToolButton *muteButton = buttonFor(m_muteAction);
	QVERIFY(!muteButton->isChecked());

	m_muteAction->setChecked(true);
	QVERIFY(muteButton->isChecked());

	m_muteAction->setChecked(false);
	QVERIFY(!muteButton->isChecked());

	// Deafen couples to mute inside the MainWindow handlers, never here.
	QVERIFY(!buttonFor(m_deafenAction)->isChecked());
}

void TestVoiceControlBar::actionEnabledStatePropagates() {
	showWide();

	QToolButton *configButton = buttonFor(m_configAction);
	QVERIFY(configButton->isEnabled());

	m_configAction->setEnabled(false);
	QVERIFY(!configButton->isEnabled());

	m_configAction->setEnabled(true);
	QVERIFY(configButton->isEnabled());
}

void TestVoiceControlBar::clickTriggersActionExactlyOnce() {
	showWide();

	QSignalSpy muteSpy(m_muteAction, &QAction::triggered);
	QSignalSpy deafenSpy(m_deafenAction, &QAction::triggered);
	QSignalSpy configSpy(m_configAction, &QAction::triggered);

	QTest::mouseClick(buttonFor(m_muteAction), Qt::LeftButton);
	QTest::mouseClick(buttonFor(m_deafenAction), Qt::LeftButton);
	QTest::mouseClick(buttonFor(m_configAction), Qt::LeftButton);

	QCOMPARE(muteSpy.count(), 1);
	QCOMPARE(deafenSpy.count(), 1);
	QCOMPARE(configSpy.count(), 1);
}

void TestVoiceControlBar::disabledActionIgnoresClick() {
	showWide();

	m_muteAction->setEnabled(false);
	QSignalSpy muteSpy(m_muteAction, &QAction::triggered);

	QTest::mouseClick(buttonFor(m_muteAction), Qt::LeftButton);

	QCOMPARE(muteSpy.count(), 0);
}

void TestVoiceControlBar::initialSnapshotIsCleared() {
	showWide();

	QCOMPARE(m_bar->statusKind(), VoiceControlBar::StatusKind::None);
	QCOMPARE(m_bar->statusToolTip(), QString());
	QVERIFY(m_bar->identityLabel()->text().isEmpty());
	QVERIFY(m_bar->statusLabel()->text().isEmpty());
	QVERIFY(m_bar->statusIconLabel()->pixmap().isNull());
	QVERIFY(m_bar->identityLabel()->toolTip().isEmpty());
	QVERIFY(m_bar->identityLabel()->accessibleDescription().isEmpty());
}

void TestVoiceControlBar::snapshotReplacesStaleIdentityAndStatus() {
	showWide();

	VoiceControlBar::VoiceStatusSnapshot snapshot = snapshotWithName("Alice", "Lobby");
	snapshot.talkState     = Settings::Talking;
	snapshot.serverMuted   = true;
	snapshot.selfMuted     = true;
	m_bar->updateVoiceStatusSnapshot(snapshot);

	QCOMPARE(m_bar->identityLabel()->text(), QString("Alice in Lobby"));
	QCOMPARE(m_bar->statusLabel()->text(), QString("Muted by server"));
	QCOMPARE(m_bar->statusKind(), VoiceControlBar::StatusKind::ServerMuted);
	QCOMPARE(m_bar->snapshot().userName, QString("Alice"));
	QCOMPARE(m_bar->snapshot().channelName, QString("Lobby"));
	QCOMPARE(m_bar->snapshot().talkState, Settings::Talking);
	QVERIFY(m_bar->snapshot().selfMuted);
	QVERIFY(m_bar->snapshot().serverMuted);

	// A default-constructed snapshot is the wholesale clear: every trace of
	// the previous connection's identity and status is gone, on all hosts.
	m_bar->updateVoiceStatusSnapshot(VoiceControlBar::VoiceStatusSnapshot());

	QCOMPARE(m_bar->statusKind(), VoiceControlBar::StatusKind::None);
	QCOMPARE(m_bar->statusToolTip(), QString());
	QVERIFY(m_bar->identityLabel()->text().isEmpty());
	QVERIFY(m_bar->statusLabel()->text().isEmpty());
	QVERIFY(m_bar->statusIconLabel()->pixmap().isNull());
	QVERIFY(m_bar->identityLabel()->toolTip().isEmpty());
	QVERIFY(m_bar->statusLabel()->toolTip().isEmpty());
	QVERIFY(m_bar->statusIconLabel()->toolTip().isEmpty());
	QVERIFY(m_bar->identityLabel()->accessibleDescription().isEmpty());

	// Identity without a channel shows the name alone.
	m_bar->updateVoiceStatusSnapshot(snapshotWithName("Bob", QString()));
	QCOMPARE(m_bar->identityLabel()->text(), QString("Bob"));
}

void TestVoiceControlBar::statusPrecedenceMatrix() {
	showWide();

	struct Case {
		VoiceControlBar::StatusKind kind;
		const char *visibleText;
		VoiceControlBar::VoiceStatusSnapshot snapshot;
	};

	QList< Case > cases;
	{
		Case c = { VoiceControlBar::StatusKind::ServerDeafened, "Deafened by server", {} };
		c.snapshot.serverDeafened = true;
		cases << c;
	}
	{
		Case c = { VoiceControlBar::StatusKind::ServerMuted, "Muted by server", {} };
		c.snapshot.serverMuted = true;
		cases << c;
	}
	{
		Case c = { VoiceControlBar::StatusKind::Suppressed, "Suppressed", {} };
		c.snapshot.suppressed = true;
		cases << c;
	}
	{
		Case c = { VoiceControlBar::StatusKind::PushToMute, "Push-to-mute", {} };
		c.snapshot.pushToMute = true;
		cases << c;
	}
	{
		Case c = { VoiceControlBar::StatusKind::Whispering, "Whispering", {} };
		c.snapshot.talkState = Settings::Whispering;
		cases << c;
	}
	{
		Case c = { VoiceControlBar::StatusKind::Shouting, "Shouting", {} };
		c.snapshot.talkState = Settings::Shouting;
		cases << c;
	}
	{
		// MutedTalking presents as Talking (tray derivation precedent).
		Case c = { VoiceControlBar::StatusKind::Talking, "Talking", {} };
		c.snapshot.talkState = Settings::MutedTalking;
		cases << c;
	}
	{
		Case c = { VoiceControlBar::StatusKind::Talking, "Talking", {} };
		c.snapshot.talkState = Settings::Talking;
		cases << c;
	}
	{
		// Self states never enter the visible line (buttons own them).
		Case c = { VoiceControlBar::StatusKind::None, "", {} };
		c.snapshot.selfMuted    = true;
		c.snapshot.selfDeafened = true;
		cases << c;
	}
	// Highest-priority wins over any combination.
	{
		Case c = { VoiceControlBar::StatusKind::ServerDeafened, "Deafened by server", {} };
		c.snapshot.serverDeafened = true;
		c.snapshot.serverMuted    = true;
		c.snapshot.suppressed     = true;
		c.snapshot.pushToMute     = true;
		c.snapshot.talkState      = Settings::Shouting;
		cases << c;
	}
	{
		Case c = { VoiceControlBar::StatusKind::ServerMuted, "Muted by server", {} };
		c.snapshot.serverMuted = true;
		c.snapshot.suppressed  = true;
		c.snapshot.pushToMute  = true;
		c.snapshot.talkState   = Settings::Talking;
		cases << c;
	}
	{
		Case c = { VoiceControlBar::StatusKind::Suppressed, "Suppressed", {} };
		c.snapshot.suppressed = true;
		c.snapshot.pushToMute = true;
		c.snapshot.talkState  = Settings::Whispering;
		cases << c;
	}
	{
		Case c = { VoiceControlBar::StatusKind::PushToMute, "Push-to-mute", {} };
		c.snapshot.pushToMute = true;
		c.snapshot.talkState  = Settings::Shouting;
		cases << c;
	}

	for (const Case &c : cases) {
		m_bar->updateVoiceStatusSnapshot(c.snapshot);
		QCOMPARE(m_bar->statusKind(), c.kind);
		QCOMPARE(m_bar->statusLabel()->text(), QString::fromLatin1(c.visibleText));
	}
}

void TestVoiceControlBar::combinedStatusesTooltipComplete() {
	showWide();

	VoiceControlBar::VoiceStatusSnapshot snapshot = snapshotWithName("Alice", "Lobby");
	snapshot.serverMuted = true;
	snapshot.suppressed  = true;
	snapshot.pushToMute  = true;
	snapshot.talkState   = Settings::Talking;
	m_bar->updateVoiceStatusSnapshot(snapshot);

	// Visible line: only the highest-priority status.
	QCOMPARE(m_bar->statusLabel()->text(), QString("Muted by server"));
	// Complete tooltip: every simultaneously active status, one line each,
	// in the fixed order.
	const QString expected = QStringLiteral("Muted by server\nSuppressed\nPush-to-mute\nTalking");
	QCOMPARE(m_bar->statusToolTip(), expected);
	QCOMPARE(m_bar->statusLabel()->toolTip(), expected);
	QCOMPARE(m_bar->statusIconLabel()->toolTip(), expected);
	QCOMPARE(m_bar->identityLabel()->toolTip(), expected);
	// The status icon renders for an active status.
	QVERIFY(!m_bar->statusIconLabel()->pixmap().isNull());

	// Self states join as separate lines while the visible line is unchanged.
	snapshot.selfMuted    = true;
	snapshot.selfDeafened = true;
	m_bar->updateVoiceStatusSnapshot(snapshot);
	QCOMPARE(m_bar->statusLabel()->text(), QString("Muted by server"));
	const QString expectedWithSelf =
		QStringLiteral("Deafened (self)\nMuted (self)\nMuted by server\nSuppressed\nPush-to-mute\nTalking");
	QCOMPARE(m_bar->statusToolTip(), expectedWithSelf);
	QCOMPARE(m_bar->identityLabel()->toolTip(), expectedWithSelf);

	// Self-only: no visible status (the buttons present self states), but
	// the complete tooltip is reachable through the always-present identity
	// label, mirrored into its accessibleDescription.
	snapshot                = snapshotWithName("Alice", "Lobby");
	snapshot.selfMuted      = true;
	snapshot.selfDeafened   = true;
	m_bar->updateVoiceStatusSnapshot(snapshot);
	QCOMPARE(m_bar->statusKind(), VoiceControlBar::StatusKind::None);
	QVERIFY(m_bar->statusLabel()->text().isEmpty());
	QVERIFY(m_bar->statusIconLabel()->pixmap().isNull());
	const QString selfOnly = QStringLiteral("Deafened (self)\nMuted (self)");
	QCOMPARE(m_bar->identityLabel()->toolTip(), selfOnly);
	QCOMPARE(m_bar->identityLabel()->accessibleDescription(), selfOnly);
	// The identity label's accessible name stays the identity text itself.
	QCOMPARE(m_bar->identityLabel()->accessibleName(), QString("Alice in Lobby"));

	// A later snapshot without the flags clears tooltip and description
	// again on every host.
	m_bar->updateVoiceStatusSnapshot(snapshotWithName("Alice", "Lobby"));
	QCOMPARE(m_bar->statusToolTip(), QString());
	QVERIFY(m_bar->identityLabel()->toolTip().isEmpty());
	QVERIFY(m_bar->statusLabel()->toolTip().isEmpty());
	QVERIFY(m_bar->statusIconLabel()->toolTip().isEmpty());
	QVERIFY(m_bar->identityLabel()->accessibleDescription().isEmpty());
}

void TestVoiceControlBar::pushToMuteIsNotSelfMute() {
	showWide();

	VoiceControlBar::VoiceStatusSnapshot snapshot;
	snapshot.pushToMute = true;
	m_bar->updateVoiceStatusSnapshot(snapshot);

	QCOMPARE(m_bar->statusKind(), VoiceControlBar::StatusKind::PushToMute);
	QCOMPARE(m_bar->statusLabel()->text(), QString("Push-to-mute"));
	// The transient status never flips the mute presentation.
	QVERIFY(!m_muteAction->isChecked());
	QVERIFY(!buttonFor(m_muteAction)->isChecked());
}

void TestVoiceControlBar::talkingNotInferredFromActionStates() {
	showWide();

	VoiceControlBar::VoiceStatusSnapshot snapshot = snapshotWithName("Alice", "Lobby");
	snapshot.talkState = Settings::Talking;
	m_bar->updateVoiceStatusSnapshot(snapshot);

	// Talking and a checked mute action coexist independently: the talk
	// state is pushed fact, never derived from the actions.
	m_muteAction->setChecked(true);
	QCOMPARE(m_bar->statusKind(), VoiceControlBar::StatusKind::Talking);
	QCOMPARE(m_bar->statusLabel()->text(), QString("Talking"));
	QVERIFY(buttonFor(m_muteAction)->isChecked());

	m_muteAction->setChecked(false);
	QCOMPARE(m_bar->statusKind(), VoiceControlBar::StatusKind::Talking);
}

void TestVoiceControlBar::snapshotPushDoesNotTriggerActions() {
	showWide();

	QSignalSpy muteSpy(m_muteAction, &QAction::triggered);
	QSignalSpy deafenSpy(m_deafenAction, &QAction::triggered);
	QSignalSpy configSpy(m_configAction, &QAction::triggered);

	VoiceControlBar::VoiceStatusSnapshot snapshot = snapshotWithName("Alice", "Lobby");
	m_bar->updateVoiceStatusSnapshot(snapshot);
	snapshot.serverMuted = true;
	m_bar->updateVoiceStatusSnapshot(snapshot);
	snapshot.selfDeafened = true;
	m_bar->updateVoiceStatusSnapshot(snapshot);
	snapshot.talkState = Settings::Shouting;
	m_bar->updateVoiceStatusSnapshot(snapshot);
	m_bar->updateVoiceStatusSnapshot(VoiceControlBar::VoiceStatusSnapshot());

	QCOMPARE(muteSpy.count(), 0);
	QCOMPARE(deafenSpy.count(), 0);
	QCOMPARE(configSpy.count(), 0);
}

void TestVoiceControlBar::accessibleNames() {
	showWide();

	// Explicit, mnemonic-stripped names - the raw action text carries the
	// '&' marker, and setDefaultAction does not sync accessibleName.
	QCOMPARE(buttonFor(m_muteAction)->accessibleName(), QString("Mute Self"));
	QCOMPARE(buttonFor(m_deafenAction)->accessibleName(), QString("Deafen Self"));
	QCOMPARE(buttonFor(m_configAction)->accessibleName(), QString("Configure"));

	// Populated labels carry their content as accessible name.
	VoiceControlBar::VoiceStatusSnapshot snapshot = snapshotWithName("Alice", "Lobby");
	snapshot.serverMuted = true;
	m_bar->updateVoiceStatusSnapshot(snapshot);
	QCOMPARE(m_bar->identityLabel()->accessibleName(), QString("Alice in Lobby"));
	QCOMPARE(m_bar->statusLabel()->accessibleName(), QString("Muted by server"));
}

void TestVoiceControlBar::verifyLayoutContract() const {
	// Every direct child fully inside the bar rect (no clipping).
	const QRect barRect = m_bar->rect();
	const auto children = m_bar->findChildren< QWidget * >(QString(), Qt::FindDirectChildrenOnly);
	QVERIFY(!children.isEmpty());
	for (QWidget *child : children) {
		QVERIFY2(barRect.contains(child->geometry()), qPrintable(QStringLiteral("child %1 clipped at width %2")
																	   .arg(child->objectName())
																	   .arg(m_bar->width())));
	}
	// No overlap between children.
	for (int i = 0; i < children.size(); ++i) {
		for (int j = i + 1; j < children.size(); ++j) {
			QVERIFY2(!children.at(i)->geometry().intersects(children.at(j)->geometry()),
					 "overlapping children");
		}
	}
	// Buttons fully visible and never below their minimum.
	const auto buttons = m_bar->findChildren< QToolButton * >();
	QCOMPARE(buttons.size(), 3);
	for (QToolButton *button : buttons) {
		QVERIFY(button->isVisible());
		QVERIFY(button->width() >= button->minimumSizeHint().width());
	}
	// One compact row: not taller than ~2.5 tool buttons.
	const int buttonHint = buttons.first()->sizeHint().height();
	QVERIFY(m_bar->sizeHint().height() <= 2.5 * buttonHint);
}

void TestVoiceControlBar::responsiveLayoutAtSupportedWidths() {
	// Realistic stress: long identity, every server restriction active at
	// once (longest status string).
	VoiceControlBar::VoiceStatusSnapshot snapshot;
	snapshot.userName     = QStringLiteral("Alexander MaximiliantheValueOfNames");
	snapshot.channelName  = QStringLiteral("Internationale Konferenzraum Ebene 2");
	snapshot.serverDeafened = true;
	snapshot.suppressed   = true;
	snapshot.pushToMute   = true;
	m_bar->updateVoiceStatusSnapshot(snapshot);

	const QString fullIdentity = QStringLiteral("%1 in %2").arg(snapshot.userName).arg(snapshot.channelName);
	const QString fullStatus   = QStringLiteral("Deafened by server");

	for (int width : { 190, 200, 240 }) {
		m_bar->resize(width, m_bar->sizeHint().height());
		m_bar->show();
		QApplication::processEvents();

		verifyLayoutContract();

		// The text areas absorbed the shrinkage (elided or fallen back to
		// empty); the buttons kept their minimums.
		QVERIFY2(m_bar->identityLabel()->text() != fullIdentity,
				 qPrintable(QStringLiteral("identity not elided at %1").arg(width)));
		QVERIFY2(m_bar->statusLabel()->text() != fullStatus,
				 qPrintable(QStringLiteral("status not elided at %1").arg(width)));
	}
}

void TestVoiceControlBar::responsiveLayoutLargeFonts() {
	QFont large     = m_bar->font();
	const int point = std::max(10, large.pointSize());
	large.setPointSize(point + 6);
	m_bar->setFont(large);

	VoiceControlBar::VoiceStatusSnapshot snapshot;
	snapshot.userName      = QStringLiteral("Alexander MaximiliantheValueOfNames");
	snapshot.channelName   = QStringLiteral("Internationale Konferenzraum Ebene 2");
	snapshot.serverDeafened = true;
	snapshot.pushToMute    = true;
	m_bar->updateVoiceStatusSnapshot(snapshot);

	for (int width : { 240, 300 }) {
		m_bar->resize(width, m_bar->sizeHint().height());
		m_bar->show();
		QApplication::processEvents();
		verifyLayoutContract();
	}
}

void TestVoiceControlBar::minimumWidthFloor() {
	// Without the (later, optional) transmission combo the bar adds no
	// minimum-width pressure: its floor stays below the minimal-mode tree
	// minimum (180 px) that the shell establishes.
	QVERIFY(m_bar->minimumSizeHint().width() > 0);
	QVERIFY(m_bar->minimumSizeHint().width() <= 180);

	// The two text areas shrink to nothing (Ignored policy), so the floor is
	// buttons + margins only.
	m_bar->updateVoiceStatusSnapshot(snapshotWithName("A Very Long User Name Indeed", "A Very Long Channel"));
	QVERIFY(m_bar->minimumSizeHint().width() <= 180);
}

void TestVoiceControlBar::focusPolicies() {
	showWide();

	// The bar itself is never a tab stop; its children are.
	QCOMPARE(m_bar->focusPolicy(), Qt::NoFocus);
	for (QToolButton *button : m_bar->findChildren< QToolButton * >()) {
		QVERIFY(button->focusPolicy() != Qt::NoFocus);
	}
}

void TestVoiceControlBar::focusTraversalChain() {
	// The hosting pattern MainWindow uses: a sibling editor linked to the
	// bar's focus chain head, exactly like setTabOrder(qteChat, ...).
	QWidget host;
	QLineEdit *lineEdit = new QLineEdit(&host);
	QVBoxLayout *layout = new QVBoxLayout(&host);
	layout->addWidget(lineEdit);
	layout->addWidget(m_bar);
	QWidget::setTabOrder(lineEdit, m_bar->focusEntryWidget());

	host.resize(800, 200);
	host.show();
	lineEdit->setFocus();
	QApplication::processEvents();
	QCOMPARE(QApplication::focusWidget(), static_cast< QWidget * >(lineEdit));

	QToolButton *muteButton   = buttonFor(m_muteAction);
	QToolButton *deafButton   = buttonFor(m_deafenAction);
	QToolButton *configButton = buttonFor(m_configAction);

	// Tab: editor -> mute -> deaf -> config -> wrap to the editor.
	QTest::keyClick(lineEdit, Qt::Key_Tab);
	QCOMPARE(QApplication::focusWidget(), static_cast< QWidget * >(muteButton));
	QTest::keyClick(muteButton, Qt::Key_Tab);
	QCOMPARE(QApplication::focusWidget(), static_cast< QWidget * >(deafButton));
	QTest::keyClick(deafButton, Qt::Key_Tab);
	QCOMPARE(QApplication::focusWidget(), static_cast< QWidget * >(configButton));
	QTest::keyClick(configButton, Qt::Key_Tab);
	QCOMPARE(QApplication::focusWidget(), static_cast< QWidget * >(lineEdit));

	// Shift+Tab reverses exactly.
	QTest::keyClick(lineEdit, Qt::Key_Backtab);
	QCOMPARE(QApplication::focusWidget(), static_cast< QWidget * >(configButton));
	QTest::keyClick(configButton, Qt::Key_Backtab);
	QCOMPARE(QApplication::focusWidget(), static_cast< QWidget * >(deafButton));
	QTest::keyClick(deafButton, Qt::Key_Backtab);
	QCOMPARE(QApplication::focusWidget(), static_cast< QWidget * >(muteButton));
	QTest::keyClick(muteButton, Qt::Key_Backtab);
	QCOMPARE(QApplication::focusWidget(), static_cast< QWidget * >(lineEdit));

	// The bar left the host's layout on destruction of the host only;
	// detach it again so the fixture cleanup can delete it.
	layout->removeWidget(m_bar);
	m_bar->setParent(nullptr);
}

void TestVoiceControlBar::focusEntryWidgetIsChainHead() {
	showWide();

	// C2 form: the head is the mute button (the bar is complete and
	// traversable without any transmission combo).
	QCOMPARE(m_bar->focusEntryWidget(), static_cast< QWidget * >(buttonFor(m_muteAction)));
}

void TestVoiceControlBar::destructionCleansUpOwnedWidgetsOnly() {
	showWide();

	QPointer< VoiceControlBar > barPointer(m_bar);
	QPointer< QToolButton > buttonPointer(buttonFor(m_muteAction));

	delete m_bar;
	m_bar = nullptr;

	// The bar's widgets died with it; the actions (owned by whoever injected
	// them - MainWindow, or this fixture) survive.
	QVERIFY(barPointer.isNull());
	QVERIFY(buttonPointer.isNull());
	QCOMPARE(m_muteAction->text(), QString("M&ute Self"));
	QCOMPARE(m_deafenAction->text(), QString("&Deafen Self"));
}

QTEST_MAIN(TestVoiceControlBar)
#include "TestVoiceControlBar.moc"

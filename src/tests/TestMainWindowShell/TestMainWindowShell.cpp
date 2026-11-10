// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

// Presentation tests of the redesigned main window shell (Stage 2 of the new
// UI). The shell is exercised with the REAL widget types it adopts (UserView,
// LogTextBrowser, ChatbarTextEdit) - the same objects the generated MainWindow
// UI creates - so identity, ownership, visibility and focus behavior are
// verified against the actual production classes. No MainWindow/Global is
// constructed here; MainWindow-level integration is covered by the manual
// smoke checklist and the alternation tests.

#include <QtTest>

#include "CustomElements.h"
#include "UserView.h"
#include "widgets/ChatPanel.h"
#include "widgets/MainWindowShell.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QPointer>
#include <QSplitter>
#include <QToolButton>

class TestMainWindowShell : public QObject {
	Q_OBJECT

private slots:
	// The adopted widgets are the very same objects; the shell only owns
	// layout containers.
	void widgetIdentityAndAncestry();

	// QDockWidget::setWidget(nullptr)-style detachment leaves the content
	// widgets hidden; adoption must explicitly re-show them (a regression here
	// would leave an invisible log/chat input in normal mode).
	void adoptionReshowsHiddenContent();

	// takeCentralWidget()-style hand-off leaves the tree hidden as well.
	void treeAdoptionReshowsTree();

	// Destroying the shell must destroy nothing but the containers: the
	// adopted widgets are owned by whoever created them... actually the shell
	// does own them through reparenting; destruction must be clean with
	// pending queued events.
	void destructionWithPendingEvents();

	void splitterRoundTrip();

	// Minimum sizes: both panes keep their minimum width, a narrow resize
	// cannot collapse either side.
	void narrowResizeHonorsMinimums();

	// The clamp hazard: in minimal mode the chat panel must no longer
	// contribute to the window minimum, so a compact size below the normal
	// minimum sticks - including across repeated mode switches.
	void compactGeometryUnderNormalMinimum();
	void repeatedModeSwitchCycles();

	// The console-visibility query must reflect the actual log widget, not a
	// stored flag.
	void consoleVisibilityMatrix();

	// Explicit tab chain tree -> log -> chat input. Deliberately the LAST
	// widget test: it depends on the offscreen platform's focus state, which
	// the QMainWindow used by minimalShrinkInsideWindow disturbs.
	void tabOrderTraversal();

	// The persistent voice control bar row (Stage 4): a stand-in strip
	// installed through setVoiceBar stays visible in normal AND minimal
	// mode, does not break the compact-geometry contract, and joins the
	// keyboard chain through the cross-boundary link the live MainWindow
	// establishes (chat input -> the bar's focus head).
	void voiceBarPersistentAndTraversal();

	// Production nesting: as the central widget of a QMainWindow the shell's
	// layout synchronization must drop the WINDOW's minimum width, not just
	// the shell's own (QMainWindowLayout is the constraint path real toggles
	// run through). Runs last: its QMainWindow leaves the offscreen
	// platform's focus state unusable for focus-dependent tests.
	void minimalShrinkInsideWindow();

private:
	QWidget *splitterOf(const MainWindowShell *shell) const {
		// The splitter is the chat panel's parent widget inside the shell.
		return shell->chatPanel()->parentWidget();
	}

	/// Processes events until the widgets' deferred-work cascades settle.
	///
	/// ChatbarTextEdit re-posts a deferred applyPlaceholder() lambda from
	/// resizeEvent() (and the UserView constructor defers its font adjustment
	/// the same way); one event-loop pass performs one cascade step. Draining
	/// while the widgets are alive keeps those receiver-less lambdas from ever
	/// firing on freed objects once the test's widgets are destroyed (in
	/// production the MainWindow tree lives for the whole session, so this is
	/// a test-harness concern only).
	static void drainEvents(int passes = 10) {
		for (int i = 0; i < passes; ++i) {
			QApplication::processEvents();
		}
	}
};

void TestMainWindowShell::widgetIdentityAndAncestry() {
	UserView *tree       = new UserView(nullptr);
	LogTextBrowser *log  = new LogTextBrowser();
	ChatbarTextEdit *chat = new ChatbarTextEdit();

	MainWindowShell shell(tree, log, chat);

	QCOMPARE(shell.tree(), tree);
	QCOMPARE(shell.chatPanel()->log(), log);
	QCOMPARE(shell.chatPanel()->chatInput(), chat);
	QCOMPARE(shell.chatPanel(), chat->parentWidget());

	QVERIFY(shell.isAncestorOf(tree));
	QVERIFY(shell.isAncestorOf(log));
	QVERIFY(shell.isAncestorOf(chat));

	// The splitter holds exactly the tree and the chat panel.
	QSplitter *splitter = qobject_cast< QSplitter * >(splitterOf(&shell));
	QVERIFY(splitter);
	QCOMPARE(splitter->count(), 2);
	QCOMPARE(splitter->widget(0), static_cast< QWidget * >(tree));
	QCOMPARE(splitter->widget(1), static_cast< QWidget * >(shell.chatPanel()));
	QVERIFY(!splitter->childrenCollapsible());

	// Drain the UserView constructor's deferred font-adjustment timer while
	// the view is still alive: its zero-timeout lambda has no receiver object,
	// so it would otherwise fire on freed memory once this test's widgets are
	// destroyed (in production the MainWindow tree lives for the whole
	// session, making this a non-issue).
	drainEvents();
}

void TestMainWindowShell::adoptionReshowsHiddenContent() {
	UserView *tree       = new UserView(nullptr);
	LogTextBrowser *log  = new LogTextBrowser();
	ChatbarTextEdit *chat = new ChatbarTextEdit();

	// Simulate the hiding side effect of QDockWidget::setWidget(nullptr)
	log->hide();
	chat->hide();
	QVERIFY(!log->isVisible());
	QVERIFY(!chat->isVisible());

	MainWindowShell shell(tree, log, chat);
	shell.resize(1000, 700);
	shell.show();
	drainEvents();

	QVERIFY(shell.isVisible());
	QVERIFY(log->isVisible());
	QVERIFY(chat->isVisible());
	QVERIFY(!shell.isMinimal());
}

void TestMainWindowShell::treeAdoptionReshowsTree() {
	UserView *tree       = new UserView(nullptr);
	LogTextBrowser *log  = new LogTextBrowser();
	ChatbarTextEdit *chat = new ChatbarTextEdit();

	// takeCentralWidget() reparents the tree to null, which hides it
	tree->hide();
	QVERIFY(!tree->isVisible());

	MainWindowShell shell(tree, log, chat);
	shell.resize(1000, 700);
	shell.show();
	drainEvents();

	QVERIFY(tree->isVisible());
}

void TestMainWindowShell::destructionWithPendingEvents() {
	QPointer< UserView > tree(new UserView(nullptr));
	QPointer< LogTextBrowser > log(new LogTextBrowser());
	QPointer< ChatbarTextEdit > chat(new ChatbarTextEdit());
	QPointer< MainWindowShell > shell(new MainWindowShell(tree, log, chat));

	shell->show();
	drainEvents();

	// Post events targeting the shell and its children (the UserView ctor
	// already schedules a zero-timeout timer for icon adjustment).
	QTimer::singleShot(0, tree.data(), []() {});
	drainEvents();

	delete shell.data();
	drainEvents();

	QVERIFY(shell.isNull());
	// The shell owned the adopted widgets through reparenting; they died with
	// the tree, not with the external pointers.
	QVERIFY(tree.isNull());
	QVERIFY(log.isNull());
	QVERIFY(chat.isNull());
}

void TestMainWindowShell::splitterRoundTrip() {
	UserView *tree       = new UserView(nullptr);
	LogTextBrowser *log  = new LogTextBrowser();
	ChatbarTextEdit *chat = new ChatbarTextEdit();

	MainWindowShell shell(tree, log, chat);
	shell.resize(1000, 700);
	shell.show();
	drainEvents();

	QSplitter *splitter = qobject_cast< QSplitter * >(splitterOf(&shell));
	QVERIFY(splitter);

	// The total available width excludes the splitter handle.
	const int handleWidth = splitter->handleWidth() > 0 ? splitter->handleWidth() : 5;
	const int available   = splitter->width() - handleWidth;

	const QList< int > sizes = { 240, available - 240 };
	shell.setSplitterSizes(sizes);
	drainEvents();

	const QList< int > readBack = shell.splitterSizes();
	QCOMPARE(readBack.size(), 2);
	QCOMPARE(readBack.at(0), 240);
	QCOMPARE(readBack.at(1), available - 240);
}

void TestMainWindowShell::narrowResizeHonorsMinimums() {
	UserView *tree       = new UserView(nullptr);
	LogTextBrowser *log  = new LogTextBrowser();
	ChatbarTextEdit *chat = new ChatbarTextEdit();

	MainWindowShell shell(tree, log, chat);
	shell.resize(1000, 700);
	shell.show();
	drainEvents();

	QSplitter *splitter = qobject_cast< QSplitter * >(splitterOf(&shell));
	QVERIFY(splitter);
	const int handleWidth = splitter->handleWidth() > 0 ? splitter->handleWidth() : 5;
	// Read the pane minimum before any mode change: minimal mode releases the
	// chat panel's minimum on purpose.
	const int paneMinimum = tree->minimumWidth() + shell.chatPanel()->minimumWidth() + handleWidth;

	// In normal mode the shell cannot shrink below the pane minimums.
	shell.resize(320, 240);
	drainEvents();

	QVERIFY(shell.width() >= paneMinimum);
	QVERIFY(tree->width() >= tree->minimumWidth());
	QVERIFY(shell.chatPanel()->width() >= shell.chatPanel()->minimumWidth());
}

void TestMainWindowShell::compactGeometryUnderNormalMinimum() {
	UserView *tree       = new UserView(nullptr);
	LogTextBrowser *log  = new LogTextBrowser();
	ChatbarTextEdit *chat = new ChatbarTextEdit();

	MainWindowShell shell(tree, log, chat);
	shell.resize(1000, 700);
	shell.show();
	drainEvents();

	QSplitter *splitter = qobject_cast< QSplitter * >(splitterOf(&shell));
	const int handleWidth = splitter->handleWidth() > 0 ? splitter->handleWidth() : 5;
	// Read the pane minimum before the mode change: minimal mode releases the
	// chat panel's minimum on purpose.
	const int normalMinimum = tree->minimumWidth() + shell.chatPanel()->minimumWidth() + handleWidth;

	// Establish the destination layout BEFORE the compact geometry: the shell
	// minimum must already have dropped when the small size is applied (the
	// splitter's margin/layout overhead is included in the achievable size).
	shell.setMinimal(true);
	QVERIFY(!shell.chatPanel()->isVisible());
	const int compactWidth = shell.minimumSizeHint().width();
	QVERIFY(compactWidth < normalMinimum);

	shell.resize(compactWidth, 300);
	drainEvents();
	QCOMPARE(shell.width(), compactWidth);
}

void TestMainWindowShell::repeatedModeSwitchCycles() {
	UserView *tree       = new UserView(nullptr);
	LogTextBrowser *log  = new LogTextBrowser();
	ChatbarTextEdit *chat = new ChatbarTextEdit();

	MainWindowShell shell(tree, log, chat);
	shell.resize(1000, 700);
	shell.show();
	drainEvents();

	QSplitter *splitter = qobject_cast< QSplitter * >(splitterOf(&shell));
	const int handleWidth = splitter->handleWidth() > 0 ? splitter->handleWidth() : 5;
	// Read the pane minimum before any mode change: minimal mode releases the
	// chat panel's minimum on purpose.
	const int normalMinimum = tree->minimumWidth() + shell.chatPanel()->minimumWidth() + handleWidth;
	const int normalWidth   = 1000;

	// Discover the achievable compact width once (the minimum is
	// mode-dependent), then return to normal mode for the cycle start.
	shell.setMinimal(true);
	drainEvents();
	const int compactWidth = shell.minimumSizeHint().width() + 20;
	QVERIFY(compactWidth < normalMinimum);
	shell.setMinimal(false);
	drainEvents();

	for (int i = 0; i < 3; ++i) {
		// Into minimal mode: layout first, then compact size.
		shell.setMinimal(true);
		shell.resize(compactWidth, 300);
		drainEvents();
		QCOMPARE(shell.width(), compactWidth);

		// Back into normal mode: layout first, then normal size.
		shell.setMinimal(false);
		shell.resize(normalWidth, 700);
		drainEvents();
		QVERIFY(shell.width() >= normalMinimum);
		QCOMPARE(shell.width(), normalWidth);
	}

	// The composer (and the rest of the adopted family) survived the cycles
	// unchanged.
	QCOMPARE(shell.chatPanel()->chatInput(), chat);
	QCOMPARE(shell.chatPanel()->log(), log);
	QCOMPARE(shell.tree(), tree);
	QVERIFY(chat->isVisible());
}

void TestMainWindowShell::minimalShrinkInsideWindow() {
	UserView *tree       = new UserView(nullptr);
	LogTextBrowser *log  = new LogTextBrowser();
	ChatbarTextEdit *chat = new ChatbarTextEdit();

	// The window MUST be declared before the shell: destruction runs in
	// reverse declaration order, so the stack shell object is destroyed (and
	// removes itself from the window) before the QMainWindow would otherwise
	// delete it as its central widget - a second, invalid destruction.
	QMainWindow window;
	// This window must not become the platform's active window: later
	// focus-dependent tests rely on their own top-level widgets being active
	// for setFocus()/keyClick() to work.
	window.setAttribute(Qt::WA_ShowWithoutActivating);
	MainWindowShell shell(tree, log, chat);
	window.setCentralWidget(&shell);
	window.resize(1000, 700);
	window.show();
	drainEvents();

	const int normalMinimum = window.minimumSizeHint().width();
	QVERIFY(normalMinimum > 0);

	shell.setMinimal(true);
	drainEvents();
	const int compactMinimum = window.minimumSizeHint().width();
	QVERIFY(compactMinimum < normalMinimum);

	window.resize(compactMinimum, 300);
	drainEvents();
	QCOMPARE(window.width(), compactMinimum);
}

void TestMainWindowShell::consoleVisibilityMatrix() {
	UserView *tree       = new UserView(nullptr);
	LogTextBrowser *log  = new LogTextBrowser();
	ChatbarTextEdit *chat = new ChatbarTextEdit();

	MainWindowShell shell(tree, log, chat);
	shell.resize(1000, 700);
	shell.show();
	drainEvents();

	// Normal mode: visible.
	QVERIFY(shell.isConsoleVisible());

	// Minimal mode: the chat panel (holding the log) is hidden.
	shell.setMinimal(true);
	QVERIFY(!shell.isConsoleVisible());

	// Back to normal.
	shell.setMinimal(false);
	QVERIFY(shell.isConsoleVisible());

	// Hiding the log widget directly must be reflected - the query is bound to
	// the actual widget, not a stored flag.
	log->hide();
	drainEvents();
	QVERIFY(!shell.isConsoleVisible());

	log->show();
	drainEvents();
	QVERIFY(shell.isConsoleVisible());
}

void TestMainWindowShell::tabOrderTraversal() {
	UserView *tree       = new UserView(nullptr);
	LogTextBrowser *log  = new LogTextBrowser();
	ChatbarTextEdit *chat = new ChatbarTextEdit();

	MainWindowShell shell(tree, log, chat);
	shell.resize(1000, 700);
	shell.show();
	drainEvents();

	tree->setFocus();
	drainEvents();
	QCOMPARE(QApplication::focusWidget(), static_cast< QWidget * >(tree));

	// tree -> log
	QTest::keyClick(tree, Qt::Key_Tab);
	QCOMPARE(QApplication::focusWidget(), static_cast< QWidget * >(log));

	// log -> chat input (ChatbarTextEdit handles Tab itself, so the chain is
	// asserted up to it)
	QTest::keyClick(log, Qt::Key_Tab);
	QCOMPARE(QApplication::focusWidget(), static_cast< QWidget * >(chat));
}

void TestMainWindowShell::voiceBarPersistentAndTraversal() {
	UserView *tree       = new UserView(nullptr);
	LogTextBrowser *log  = new LogTextBrowser();
	ChatbarTextEdit *chat = new ChatbarTextEdit();

	MainWindowShell shell(tree, log, chat);

	// Stand-in control strip: a plain QWidget with two focusable buttons.
	// The shell must stay free of VoiceControlBar/Global dependencies, so
	// the real bar's behavior is reduced to exactly what the shell itself
	// has to guarantee: a bottom row that survives mode switches.
	QWidget *voiceBar         = new QWidget(nullptr);
	QHBoxLayout *barLayout    = new QHBoxLayout(voiceBar);
	QToolButton *firstButton  = new QToolButton(voiceBar);
	QToolButton *secondButton = new QToolButton(voiceBar);
	barLayout->addWidget(firstButton);
	barLayout->addWidget(secondButton);

	shell.setVoiceBar(voiceBar);
	shell.resize(1000, 700);
	shell.show();
	drainEvents();

	// Visible in normal mode and persistent across mode switches (never
	// touched by setMinimal).
	QVERIFY(voiceBar->isVisible());
	shell.setMinimal(true);
	drainEvents();
	QVERIFY(voiceBar->isVisible());
	shell.setMinimal(false);
	drainEvents();
	QVERIFY(voiceBar->isVisible());

	// The bar row must not break the compact-geometry contract: in minimal
	// mode the shell can still shrink below the normal-mode minimum.
	QSplitter *splitter     = qobject_cast< QSplitter * >(splitterOf(&shell));
	const int handleWidth   = splitter->handleWidth() > 0 ? splitter->handleWidth() : 5;
	const int normalMinimum = tree->minimumWidth() + shell.chatPanel()->minimumWidth() + handleWidth;
	shell.setMinimal(true);
	drainEvents();
	const int compactWidth = shell.minimumSizeHint().width();
	QVERIFY(compactWidth < normalMinimum);
	shell.resize(compactWidth, 300);
	drainEvents();
	QCOMPARE(shell.width(), compactWidth);
	QVERIFY(voiceBar->isVisible());

	// The cross-boundary focus link the live MainWindow establishes after
	// installing the bar (chat input -> the bar's focus chain head).
	QWidget::setTabOrder(shell.chatPanel()->chatInput(), firstButton);
	QWidget::setTabOrder(firstButton, secondButton);

	// Normal mode. NOTE: ChatbarTextEdit consumes Tab AND Backtab itself and
	// emits tabPressed/backtabPressed instead; MainWindow answers tabPressed
	// by advancing the real focus chain, so an enabled (connected) chat
	// input hands focus into the bar (verified with the real bar in
	// TestVoiceControlBar::chatTabAdvancesIntoBar). The shell-side
	// guarantees are exercised here: Backtab out of the bar's head lands in
	// the chat input, and Tab out of the log reaches the bar whenever the
	// chat input cannot take focus - which is exactly the disconnected
	// presentation (setupGui disables the chat input when not connected),
	// where the bar is the primary control surface.
	shell.setMinimal(false);
	shell.resize(1000, 700);
	drainEvents();

	shell.chatPanel()->chatInput()->setEnabled(false);
	tree->setFocus();
	drainEvents();
	QCOMPARE(QApplication::focusWidget(), static_cast< QWidget * >(tree));
	QTest::keyClick(tree, Qt::Key_Tab);
	QCOMPARE(QApplication::focusWidget(), static_cast< QWidget * >(log));
	QTest::keyClick(log, Qt::Key_Tab);
	QCOMPARE(QApplication::focusWidget(), static_cast< QWidget * >(firstButton));
	QTest::keyClick(firstButton, Qt::Key_Tab);
	QCOMPARE(QApplication::focusWidget(), static_cast< QWidget * >(secondButton));

	// Shift+Tab reverses the whole walk.
	QTest::keyClick(secondButton, Qt::Key_Backtab);
	QCOMPARE(QApplication::focusWidget(), static_cast< QWidget * >(firstButton));
	QTest::keyClick(firstButton, Qt::Key_Backtab);
	QCOMPARE(QApplication::focusWidget(), static_cast< QWidget * >(log));
	QTest::keyClick(log, Qt::Key_Backtab);
	QCOMPARE(QApplication::focusWidget(), static_cast< QWidget * >(tree));

	// With the chat input enabled again, Backtab out of the bar's head lands
	// in it: the cross-boundary link itself.
	shell.chatPanel()->chatInput()->setEnabled(true);
	firstButton->setFocus();
	drainEvents();
	QCOMPARE(QApplication::focusWidget(), static_cast< QWidget * >(firstButton));
	QTest::keyClick(firstButton, Qt::Key_Backtab);
	QCOMPARE(QApplication::focusWidget(), static_cast< QWidget * >(shell.chatPanel()->chatInput()));

	// Minimal mode: the chat panel is hidden and skipped, so the tree hands
	// focus straight into the bar.
	shell.setMinimal(true);
	drainEvents();
	tree->setFocus();
	drainEvents();
	QCOMPARE(QApplication::focusWidget(), static_cast< QWidget * >(tree));
	QTest::keyClick(tree, Qt::Key_Tab);
	QCOMPARE(QApplication::focusWidget(), static_cast< QWidget * >(firstButton));
	QTest::keyClick(firstButton, Qt::Key_Tab);
	QCOMPARE(QApplication::focusWidget(), static_cast< QWidget * >(secondButton));
	QTest::keyClick(secondButton, Qt::Key_Backtab);
	QCOMPARE(QApplication::focusWidget(), static_cast< QWidget * >(firstButton));
	QTest::keyClick(firstButton, Qt::Key_Backtab);
	QCOMPARE(QApplication::focusWidget(), static_cast< QWidget * >(tree));

	// Replacing the bar retires the previous one. deleteLater events are not
	// delivered by a same-nesting-level processEvents() pass, so the deferred
	// deletes are flushed explicitly.
	QPointer< QWidget > retiredBar(voiceBar);
	QWidget *replacement = new QWidget(nullptr);
	shell.setVoiceBar(replacement);
	drainEvents();
	QVERIFY(replacement->isVisible());
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	QVERIFY(retiredBar.isNull());
}

QTEST_MAIN(TestMainWindowShell)
#include "TestMainWindowShell.moc"

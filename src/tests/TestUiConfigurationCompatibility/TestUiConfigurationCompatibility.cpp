// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

// Configuration-compatibility tests of the redesigned UI (Stage 2, new-UI
// architecture 5.3/5.4).
//
// What is REAL here: Settings JSON serialization (the actual save/load used
// by the client), the production protection seam commitAcceptedSettings()
// (the function ConfigDialog::apply() calls instead of a plain assignment),
// and the fork persistence policy UiStatePersistence on top of the real
// UiStateStore sidecar.
//
// What is SIMULATED: the dialog flow itself. A real ConfigDialog cannot be
// constructed in a test harness (ConfigDialog::apply() calls Audio::start()
// which creates real audio backends and spin-waits, and the widget registry
// would instantiate every configuration page), so the Apply/OK/Reset paths
// are simulated at the candidate-settings level with exactly the production
// ordering: widgets save into a candidate -> commitAcceptedSettings(candidate,
// current) -> current.save(). GUI-level behavior (buttons, disabled controls)
// is covered by the manual smoke checklist.

#include <QtTest>

#include "Settings.h"
#include "UiStatePersistence.h"
#include "UiStateStore.h"

#include <QApplication>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QScreen>
#include <QTemporaryDir>
#include <QVBoxLayout>

namespace {

/// The JSON location of every shared setting the fork must never modify
/// (new-UI architecture 5.3), as "<category>/<key>" pairs.
const QStringList PROTECTED_JSON_KEYS = {
	"ui/window_geometry",              // qbaMainWindowGeometry
	"ui/window_state",                 // qbaMainWindowState
	"ui/minimal_view_window_geometry", // qbaMinimalViewGeometry
	"ui/minimal_view_window_state",    // qbaMinimalViewState
	"ui/prevent_window_states",        // preventWindowStates
	"ui/minimal_view",                 // bMinimalView
	"ui/hide_frame",                   // bHideFrame
	"ui/window_layout",                // wlWindowLayout
	"ui/lock_layout",                  // bLockLayout
};

QJsonObject loadJson(const QString &path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return QJsonDocument::fromJson(file.readAll()).object();
}

/// Snapshot of the protected fields as stored on disk. Fails (returns false)
/// if the seed does not contain a storable value for every protected field.
bool snapshotProtected(const QString &settingsPath, QHash< QString, QJsonValue > &snapshot) {
	const QJsonObject root = loadJson(settingsPath);
	for (const QString &location : PROTECTED_JSON_KEYS) {
		const QStringList parts = location.split('/');
		if (!root.contains(parts.at(0))) {
			qWarning("Missing category %s in seeded profile", qPrintable(parts.at(0)));
			return false;
		}
		const QJsonValue value = root.value(parts.at(0)).toObject().value(parts.at(1));
		if (value.isUndefined()) {
			qWarning("Seeded profile must contain non-default %s", qPrintable(location));
			return false;
		}
		snapshot.insert(location, value);
	}
	return true;
}

/// Asserts that every protected field is identical on disk to the seed
/// snapshot and that the schema version is unchanged.
bool protectedUnchanged(const QString &settingsPath, const QHash< QString, QJsonValue > &seedSnapshot) {
	const QJsonObject root = loadJson(settingsPath);
	for (auto it = seedSnapshot.constBegin(); it != seedSnapshot.constEnd(); ++it) {
		const QStringList parts = it.key().split('/');
		const QJsonValue current = root.value(parts.at(0)).toObject().value(parts.at(1));
		if (current != it.value()) {
			qWarning("Protected field %s changed", qPrintable(it.key()));
			return false;
		}
	}
	if (root.value("settings_version").toInt() != 1) {
		qWarning("settings_version changed");
		return false;
	}
	return true;
}

/// Recursively checks that no key mentions the fork's sidecar namespace.
bool containsNewUiKeys(const QJsonObject &object, const QString &path = QString()) {
	for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
		const QString keyPath = path.isEmpty() ? it.key() : path + "/" + it.key();
		if (it.key().contains(QLatin1String("new-ui"))) {
			qWarning("Fork key %s leaked into the shared settings", qPrintable(keyPath));
			return true;
		}
		if (it.value().isObject() && containsNewUiKeys(it.value().toObject(), keyPath)) {
			return true;
		}
	}
	return false;
}

/// Seeds a disposable profile the way the official client would have left it:
/// recognizable, non-default values in the protected fields plus ordinary
/// non-default settings. Returns the profile's settings path.
///
/// \param latchActive seeds preventWindowStates=true (the official client's
/// read-only crash-safety latch). Tests that exercise the fork's geometry
/// restoration must pass false; the veto behavior itself is covered by
/// sharedLatchVetoesRestoreOnly.
QString seedOfficialProfile(const QTemporaryDir &dir, bool latchActive = true) {
	const QString path = dir.path() + "/mumble_settings.json";

	Settings official;
	official.qbaMainWindowGeometry  = QByteArray("official-normal-geometry");
	official.qbaMainWindowState     = QByteArray("official-normal-state");
	official.qbaMinimalViewGeometry = QByteArray("official-minimal-geometry");
	official.qbaMinimalViewState    = QByteArray("official-minimal-state");
	official.bMinimalView           = true;
	official.bHideFrame             = true;
	official.wlWindowLayout         = Settings::LayoutCustom;
	official.bLockLayout            = true;
	if (latchActive) {
		official.preventWindowStates = true;
	}

	official.atTransmit         = Settings::PushToTalk;
	official.qsDatabaseLocation = dir.path() + "/official.sqlite";

	official.mumbleQuitNormally = true;
	official.save(path);

	return path;
}

QByteArray readFileBytes(const QString &path) {
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

/// Geometry comparison with tolerance for platform frame margins (the
/// offscreen platform may adjust restored geometry by a few pixels).
bool sizeNear(const QSize &actual, const QSize &expected, int tolerance = 6) {
	return qAbs(actual.width() - expected.width()) <= tolerance
		   && qAbs(actual.height() - expected.height()) <= tolerance;
}

bool pointNear(const QPoint &actual, const QPoint &expected, int tolerance = 6) {
	return qAbs(actual.x() - expected.x()) <= tolerance && qAbs(actual.y() - expected.y()) <= tolerance;
}

} // namespace

class TestUiConfigurationCompatibility : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();

	// --- Production protection seam: commitAcceptedSettings ---
	void commitSeamProtectedFieldsSurvive();
	void commitSeamOrdinaryEditsLand();
	void commitSeamDefaultsAllKeepsProtected();

	// --- Every shared save point leaves the protected fields untouched ---
	void savePointMigrationStyle();
	void savePointConfigOkStyle();
	void savePointSessionStyle();
	void savePointShutdownStyle();

	// --- Fork policy isolation ---
	void forkPolicyNeverTouchesSharedFile();
	void sidecarExistsOnlyAfterFirstCapture();
	void noSidecarSeedsFromSharedOnce();

	// --- Splitter hand-off contract ---
	void splitterRoundTripThroughSidecar();
	void minimalCaptureKeepsNormalSplitter();

	// --- Transition geometry integrity ---
	void repeatedModeTransitionsNoCrossContamination();
	void geometryPreservedAcrossPolicyRestart();

	// --- Veto matrix ---
	void sharedLatchVetoesRestoreOnly();
	void cliVetoBlocksRestoreAndCapture();

	// --- Crash recovery / restore latch ---
	void crashDuringRestoreBlocksUntilExplicitReset();

	// --- Reset semantics ---
	void resetClearsGeometryAndReleasesLatch();
	void closeAfterResetPersistsNoStaleLayout();
	void defaultWindowRectIsSane();

	// --- Live window-flag transitions ---
	void windowFlagsPreservedWhenVisible();
	void windowFlagsHiddenStaysHidden();
	void windowFlagsUnchangedIsNoOp();

private:
	/// Disposable root for the process-wide TMPDIR override in initTestCase;
	/// must outlive every test.
	QTemporaryDir m_stagingRoot;
};

void TestUiConfigurationCompatibility::initTestCase() {
	// The production Settings::save() stages every write through a fixed-name
	// scratch file in QStandardPaths::TempLocation before renaming it onto the
	// target. Pointing TMPDIR at a disposable directory keeps that scratch
	// file away from a concurrently running Mumble client of the same user,
	// which would otherwise race on the identical staging name (on Unix
	// writableLocation(TempLocation) is resolved from TMPDIR on each call, so
	// overriding it here covers every later save).
	QVERIFY(!m_stagingRoot.path().isEmpty());
	qputenv("TMPDIR", m_stagingRoot.path().toUtf8());
}

void TestUiConfigurationCompatibility::commitSeamProtectedFieldsSurvive() {
	Settings current;
	current.qbaMainWindowGeometry  = QByteArray("keep-geometry");
	current.qbaMainWindowState     = QByteArray("keep-state");
	current.qbaMinimalViewGeometry = QByteArray("keep-min-geometry");
	current.qbaMinimalViewState    = QByteArray("keep-min-state");
	current.preventWindowStates    = true;
	current.bMinimalView           = true;
	current.bHideFrame             = true;
	current.wlWindowLayout         = Settings::LayoutHybrid;
	current.bLockLayout            = true;

	// A candidate holding scrambled values in every protected field (exactly
	// what a stale dialog copy would try to write back).
	Settings candidate = current;
	candidate.qbaMainWindowGeometry  = QByteArray("clobber-geometry");
	candidate.qbaMainWindowState     = QByteArray("clobber-state");
	candidate.qbaMinimalViewGeometry = QByteArray("clobber-min-geometry");
	candidate.qbaMinimalViewState    = QByteArray("clobber-min-state");
	candidate.preventWindowStates    = false;
	candidate.bMinimalView           = false;
	candidate.bHideFrame             = false;
	candidate.wlWindowLayout         = Settings::LayoutStacked;
	candidate.bLockLayout            = false;

	commitAcceptedSettings(candidate, current);

	QCOMPARE(current.qbaMainWindowGeometry, QByteArray("keep-geometry"));
	QCOMPARE(current.qbaMainWindowState, QByteArray("keep-state"));
	QCOMPARE(current.qbaMinimalViewGeometry, QByteArray("keep-min-geometry"));
	QCOMPARE(current.qbaMinimalViewState, QByteArray("keep-min-state"));
	QCOMPARE(current.preventWindowStates, true);
	QCOMPARE(current.bMinimalView, true);
	QCOMPARE(current.bHideFrame, true);
	QCOMPARE(current.wlWindowLayout, Settings::LayoutHybrid);
	QCOMPARE(current.bLockLayout, true);
}

void TestUiConfigurationCompatibility::commitSeamOrdinaryEditsLand() {
	Settings current;
	current.atTransmit = Settings::VAD;

	Settings candidate = current;
	candidate.atTransmit         = Settings::PushToTalk;
	candidate.qsDatabaseLocation = "/tmp/somewhere.sqlite";
	// ...and the clobber attempt is still ignored:
	candidate.qbaMainWindowGeometry = QByteArray("clobber");

	commitAcceptedSettings(candidate, current);

	QCOMPARE(current.atTransmit, Settings::PushToTalk);
	QCOMPARE(current.qsDatabaseLocation, QString("/tmp/somewhere.sqlite"));
	QVERIFY(current.qbaMainWindowGeometry.isEmpty());
}

void TestUiConfigurationCompatibility::commitSeamDefaultsAllKeepsProtected() {
	// "Defaults for all" hands the seam a fresh default Settings object.
	Settings current;
	current.qbaMainWindowGeometry = QByteArray("official-geometry");
	current.bMinimalView          = true;
	current.wlWindowLayout        = Settings::LayoutCustom;
	current.atTransmit            = Settings::PushToTalk;

	Settings defaults;
	commitAcceptedSettings(defaults, current);

	// The protected nine keep the official client's values...
	QCOMPARE(current.qbaMainWindowGeometry, QByteArray("official-geometry"));
	QCOMPARE(current.bMinimalView, true);
	QCOMPARE(current.wlWindowLayout, Settings::LayoutCustom);
	// ...while everything else is reset to defaults.
	QCOMPARE(current.atTransmit, Settings::VAD);
}

void TestUiConfigurationCompatibility::savePointMigrationStyle() {
	QTemporaryDir dir;
	const QString path = seedOfficialProfile(dir);
	QHash< QString, QJsonValue > snapshot;
	QVERIFY(snapshotProtected(path, snapshot));

	// Migration-style: load, mutate an ordinary field, save (the pattern of
	// main.cpp's database migration save point).
	Settings s;
	s.load(path, /*skipSettingsBackupPrompt=*/true);
	s.qsDatabaseLocation = dir.path() + "/migrated.sqlite";
	s.save(path);

	QVERIFY(protectedUnchanged(path, snapshot));
	QVERIFY(!containsNewUiKeys(loadJson(path)));

	// The ordinary edit is really on disk.
	Settings reloaded;
	reloaded.load(path, true);
	QCOMPARE(reloaded.qsDatabaseLocation, dir.path() + "/migrated.sqlite");
}

void TestUiConfigurationCompatibility::savePointConfigOkStyle() {
	QTemporaryDir dir;
	const QString path = seedOfficialProfile(dir);
	QHash< QString, QJsonValue > snapshot;
	QVERIFY(snapshotProtected(path, snapshot));

	Settings global;
	global.load(path, true);

	// Config-OK-style: the dialog's candidate is a copy of the global settings
	// with user edits - and stale copies of the protected fields. The
	// production ordering (ConfigDialog::apply) is: save() into candidate ->
	// commitAcceptedSettings(candidate, global) -> global.save().
	Settings candidate = global;
	candidate.atTransmit = Settings::Continuous;
	candidate.qbaMainWindowGeometry  = QByteArray("stale-dialog-geometry");
	candidate.qbaMainWindowState     = QByteArray("stale-dialog-state");
	candidate.qbaMinimalViewGeometry = QByteArray("stale-dialog-min-geometry");
	candidate.qbaMinimalViewState    = QByteArray("stale-dialog-min-state");
	candidate.preventWindowStates    = false;
	candidate.bMinimalView           = false;
	candidate.bHideFrame             = false;
	candidate.wlWindowLayout         = Settings::LayoutClassic;
	candidate.bLockLayout            = false;

	commitAcceptedSettings(candidate, global);
	global.save(path);

	QVERIFY(protectedUnchanged(path, snapshot));
	QVERIFY(!containsNewUiKeys(loadJson(path)));

	Settings reloaded;
	reloaded.load(path, true);
	QCOMPARE(reloaded.atTransmit, Settings::Continuous);
}

void TestUiConfigurationCompatibility::savePointSessionStyle() {
	QTemporaryDir dir;
	const QString path = seedOfficialProfile(dir);
	QHash< QString, QJsonValue > snapshot;
	QVERIFY(snapshotProtected(path, snapshot));

	// Session-end style (MumbleApplication::onCommitDataRequest): the fork
	// captures its own state first, then the shared settings are saved.
	Settings s;
	s.load(path, true);

	QWidget window;
	UiStatePersistence fork(s, window);
	fork.resolveAndPinPrimaryPath();
	QCOMPARE(fork.startupLoad(), UiStateStore::LoadResult::NoSidecar);
	fork.captureGeometry(false, QList< int >() << 240 << 720);
	QCOMPARE(fork.save(), UiStateStore::SaveResult::Saved);

	s.mumbleQuitNormally = true;
	s.save(path);

	QVERIFY(protectedUnchanged(path, snapshot));
	QVERIFY(!containsNewUiKeys(loadJson(path)));
	QVERIFY(QFile::exists(path + ".new-ui.json"));
}

void TestUiConfigurationCompatibility::savePointShutdownStyle() {
	QTemporaryDir dir;
	const QString path = seedOfficialProfile(dir);
	QHash< QString, QJsonValue > snapshot;
	QVERIFY(snapshotProtected(path, snapshot));

	// Shutdown style (main.cpp): an ordinary save of whatever the session
	// changed, after the fork has done its own captures.
	Settings s;
	s.load(path, true);
	s.atTransmit = Settings::Continuous;
	s.save(path);

	QVERIFY(protectedUnchanged(path, snapshot));
	QVERIFY(!containsNewUiKeys(loadJson(path)));
}

void TestUiConfigurationCompatibility::forkPolicyNeverTouchesSharedFile() {
	QTemporaryDir dir;
	const QString path = seedOfficialProfile(dir, /*latchActive=*/false);
	const QByteArray sharedBytes = readFileBytes(path);

	Settings s;
	s.load(path, true);

	QWidget window;
	UiStatePersistence fork(s, window);
	fork.resolveAndPinPrimaryPath();
	QCOMPARE(fork.startupLoad(), UiStateStore::LoadResult::NoSidecar);

	// Full fork lifecycle: seeded load, captures in both modes, saves,
	// explicit reset - the shared file must stay byte-identical throughout.
	window.resize(500, 400);
	fork.captureGeometry(false, QList< int >() << 240 << 720);
	QCOMPARE(fork.save(), UiStateStore::SaveResult::Saved);
	QCOMPARE(readFileBytes(path), sharedBytes);

	fork.setMinimal(true);
	fork.captureGeometry(true, QList< int >());
	QCOMPARE(fork.save(), UiStateStore::SaveResult::Saved);
	QCOMPARE(readFileBytes(path), sharedBytes);

	QVERIFY(fork.resetLayout());
	QCOMPARE(readFileBytes(path), sharedBytes);
}

void TestUiConfigurationCompatibility::sidecarExistsOnlyAfterFirstCapture() {
	QTemporaryDir dir;
	const QString path = seedOfficialProfile(dir, /*latchActive=*/false);

	Settings s;
	s.load(path, true);

	QWidget window;
	UiStatePersistence fork(s, window);
	fork.resolveAndPinPrimaryPath();
	QCOMPARE(fork.startupLoad(), UiStateStore::LoadResult::NoSidecar);
	QVERIFY(!QFile::exists(path + ".new-ui.json"));

	// Mode changes alone do not create the sidecar.
	fork.setMinimal(false);
	QVERIFY(!QFile::exists(path + ".new-ui.json"));

	fork.captureGeometry(false, QList< int >() << 240 << 720);
	QCOMPARE(fork.save(), UiStateStore::SaveResult::Saved);
	QVERIFY(QFile::exists(path + ".new-ui.json"));
}

void TestUiConfigurationCompatibility::noSidecarSeedsFromSharedOnce() {
	QTemporaryDir dir;
	const QString path = seedOfficialProfile(dir, /*latchActive=*/false);
	const QByteArray sharedBytes = readFileBytes(path);

	Settings s;
	s.load(path, true);
	QCOMPARE(s.bMinimalView, true);
	QCOMPARE(s.bHideFrame, true);

	QWidget window;
	UiStatePersistence fork(s, window);
	fork.resolveAndPinPrimaryPath();
	QCOMPARE(fork.startupLoad(), UiStateStore::LoadResult::NoSidecar);

	// The fork adopts the official client's display mode as its one-time
	// seed...
	QVERIFY(fork.state().minimal);
	QVERIFY(fork.state().hideFrame);
	// ...but the shared file is untouched and holds the official values.
	QCOMPARE(readFileBytes(path), sharedBytes);

	Settings reloaded;
	reloaded.load(path, true);
	QCOMPARE(reloaded.bMinimalView, true);
	QCOMPARE(reloaded.bHideFrame, true);
}

void TestUiConfigurationCompatibility::splitterRoundTripThroughSidecar() {
	QTemporaryDir dir;
	const QString path = seedOfficialProfile(dir, /*latchActive=*/false);

	Settings s;
	s.load(path, true);

	QWidget window;
	UiStatePersistence fork(s, window);
	fork.resolveAndPinPrimaryPath();
	QCOMPARE(fork.startupLoad(), UiStateStore::LoadResult::NoSidecar);

	const QList< int > sizes = QList< int >() << 240 << 720;
	fork.captureGeometry(false, sizes);
	QCOMPARE(fork.save(), UiStateStore::SaveResult::Saved);

	// A fresh policy on the same sidecar (next start) reads them back.
	QWidget window2;
	UiStatePersistence fork2(s, window2);
	fork2.resolveAndPinPrimaryPath();
	QCOMPARE(fork2.startupLoad(), UiStateStore::LoadResult::Loaded);
	QCOMPARE(fork2.state().normalSplitterSizes, sizes);
}

void TestUiConfigurationCompatibility::minimalCaptureKeepsNormalSplitter() {
	QTemporaryDir dir;
	const QString path = seedOfficialProfile(dir, /*latchActive=*/false);

	Settings s;
	s.load(path, true);

	QWidget window;
	UiStatePersistence fork(s, window);
	fork.resolveAndPinPrimaryPath();
	QCOMPARE(fork.startupLoad(), UiStateStore::LoadResult::NoSidecar);

	const QList< int > sizes = QList< int >() << 240 << 720;
	fork.captureGeometry(false, sizes);
	QCOMPARE(fork.save(), UiStateStore::SaveResult::Saved);

	// A capture in minimal mode must not touch the stored normal splitter
	// sizes (minimal mode has no splitter).
	fork.setMinimal(true);
	fork.captureGeometry(true, QList< int >());
	QCOMPARE(fork.save(), UiStateStore::SaveResult::Saved);
	QCOMPARE(fork.state().normalSplitterSizes, sizes);
}

void TestUiConfigurationCompatibility::repeatedModeTransitionsNoCrossContamination() {
	QTemporaryDir dir;
	const QString path = seedOfficialProfile(dir, /*latchActive=*/false);

	Settings s;
	s.load(path, true);

	QWidget window;
	// Both rectangles fit the offscreen platform's 800x600 virtual screen:
	// QWidget::restoreGeometry() shifts windows that would extend past the
	// screen edges back onscreen, which would show up as position drift.
	window.resize(700, 500);
	window.move(30, 20);
	UiStatePersistence fork(s, window);
	fork.resolveAndPinPrimaryPath();
	QCOMPARE(fork.startupLoad(), UiStateStore::LoadResult::NoSidecar);

	// Distinct per-mode geometry: normal is large, compact is small (below
	// what the normal layout would allow - the clamp-hazard scenario).
	const QSize normalSize(700, 500);
	const QSize compactSize(260, 220);
	const QPoint normalPos(30, 20);
	const QPoint compactPos(300, 250);

	// The authoritative transition ordering (mirrors MainWindow::setupView):
	// the window is IN the outgoing mode's geometry when it is captured; the
	// layout flips; the incoming mode's geometry is restored. The first entry
	// into minimal mode has nothing stored, so the "user" arranges the compact
	// window manually - from the second round on, the stored bytes restore it.
	for (int i = 0; i < 3; ++i) {
		// normal -> minimal (the window is at the normal geometry here)
		QVERIFY(sizeNear(window.size(), normalSize));
		const QByteArray outgoingNormal = window.saveGeometry();
		fork.setMinimal(true);
		fork.captureGeometry(false, QList< int >() << 240 << 720); // outgoing normal
		QCOMPARE(fork.state().normalGeometry, outgoingNormal);

		QCOMPARE(fork.restoreGeometry(true), i > 0);
		if (i == 0) {
			window.resize(compactSize);
			window.move(compactPos);
		} else {
			QVERIFY(sizeNear(window.size(), compactSize));
			QVERIFY(pointNear(window.pos(), compactPos));
		}
		QCOMPARE(fork.save(), UiStateStore::SaveResult::Saved);

		// minimal -> normal (the window is at the compact geometry here)
		const QByteArray outgoingCompact = window.saveGeometry();
		fork.setMinimal(false);
		fork.captureGeometry(true, QList< int >()); // outgoing compact
		QCOMPARE(fork.state().compactGeometry, outgoingCompact);

		QVERIFY(fork.restoreGeometry(false));
		QVERIFY(sizeNear(window.size(), normalSize));
		QVERIFY(pointNear(window.pos(), normalPos));
		QCOMPARE(fork.save(), UiStateStore::SaveResult::Saved);
	}
	// Cross-contamination is excluded by the per-iteration byte comparisons
	// above: each mode's stored bytes are exactly the bytes captured while the
	// window verifiably carried that mode's geometry.
}

void TestUiConfigurationCompatibility::geometryPreservedAcrossPolicyRestart() {
	QTemporaryDir dir;
	const QString path = seedOfficialProfile(dir, /*latchActive=*/false);

	Settings s;
	s.load(path, true);

	QWidget window;
	window.resize(650, 480);
	window.move(90, 120);

	UiStatePersistence fork(s, window);
	fork.resolveAndPinPrimaryPath();
	QCOMPARE(fork.startupLoad(), UiStateStore::LoadResult::NoSidecar);
	fork.captureGeometry(false, QList< int >() << 240 << 720);
	QCOMPARE(fork.save(), UiStateStore::SaveResult::Saved);

	const QSize capturedSize = window.size();
	const QPoint capturedPos = window.pos();

	// "Restart": a brand-new window and policy on the same sidecar.
	QWidget window2;
	UiStatePersistence fork2(s, window2);
	fork2.resolveAndPinPrimaryPath();
	QCOMPARE(fork2.startupLoad(), UiStateStore::LoadResult::Loaded);
	QVERIFY(fork2.restoreGeometry(false));

	QVERIFY(sizeNear(window2.size(), capturedSize));
	QVERIFY(pointNear(window2.pos(), capturedPos));
	// Splitter sizes survive the restart too (applied by MainWindow).
	QCOMPARE(fork2.state().normalSplitterSizes, QList< int >() << 240 << 720);
}

void TestUiConfigurationCompatibility::sharedLatchVetoesRestoreOnly() {
	// The official client's preventWindowStates latch is read-only for the
	// fork and vetoes RESTORATION ONLY - captures into the fork's own sidecar
	// remain allowed (veto matrix, new-UI architecture 5.3).
	QTemporaryDir dir;
	const QString path = seedOfficialProfile(dir, /*latchActive=*/true);

	Settings s;
	s.load(path, true);
	QCOMPARE(s.preventWindowStates, true);

	QWidget window;
	window.resize(600, 450);
	UiStatePersistence fork(s, window);
	fork.resolveAndPinPrimaryPath();
	QCOMPARE(fork.startupLoad(), UiStateStore::LoadResult::NoSidecar);

	// Capture is allowed and persists.
	fork.captureGeometry(false, QList< int >() << 240 << 720);
	QVERIFY(!fork.state().normalGeometry.isEmpty());
	QCOMPARE(fork.save(), UiStateStore::SaveResult::Saved);

	// Restoration is vetoed, now and after a restart.
	QVERIFY(!fork.restoreGeometry(false));

	QWidget window2;
	UiStatePersistence fork2(s, window2);
	fork2.resolveAndPinPrimaryPath();
	QCOMPARE(fork2.startupLoad(), UiStateStore::LoadResult::Loaded);
	QVERIFY(!fork2.restoreGeometry(false));
}

void TestUiConfigurationCompatibility::cliVetoBlocksRestoreAndCapture() {
	// --no-window-states: both restoration and capture are vetoed.
	QTemporaryDir dir;
	const QString path = seedOfficialProfile(dir, /*latchActive=*/false);

	Settings s;
	s.load(path, true);

	QWidget window;
	UiStatePersistence fork(s, window);
	fork.resolveAndPinPrimaryPath();
	QCOMPARE(fork.startupLoad(), UiStateStore::LoadResult::NoSidecar);
	fork.captureGeometry(false, QList< int >() << 240 << 720);
	QCOMPARE(fork.save(), UiStateStore::SaveResult::Saved);

	// A fresh start with the CLI veto set.
	QWidget window2;
	UiStatePersistence fork2(s, window2);
	fork2.setCliVeto(true);
	fork2.resolveAndPinPrimaryPath();
	QCOMPARE(fork2.startupLoad(), UiStateStore::LoadResult::Loaded);
	QVERIFY(!fork2.restoreGeometry(false));

	// Capture is vetoed as well: the stored bytes stay exactly as they were.
	const QByteArray stored = fork2.state().normalGeometry;
	QVERIFY(!stored.isEmpty());
	window2.resize(123, 456);
	fork2.captureGeometry(false, QList< int >() << 111 << 222);
	QCOMPARE(fork2.state().normalGeometry, stored);
	QCOMPARE(fork2.state().normalSplitterSizes, QList< int >() << 240 << 720);
	QCOMPARE(fork2.save(), UiStateStore::SaveResult::Saved);

	UiStateStore verify(path);
	QCOMPARE(verify.load(), UiStateStore::LoadResult::Loaded);
	QCOMPARE(verify.loadedState().normalGeometry, stored);
	QCOMPARE(verify.loadedState().normalSplitterSizes, QList< int >() << 240 << 720);
}

void TestUiConfigurationCompatibility::crashDuringRestoreBlocksUntilExplicitReset() {
	QTemporaryDir dir;
	const QString path = seedOfficialProfile(dir, /*latchActive=*/false);

	Settings s;
	s.load(path, true);

	QWidget window;
	UiStatePersistence fork(s, window);
	fork.resolveAndPinPrimaryPath();
	QCOMPARE(fork.startupLoad(), UiStateStore::LoadResult::NoSidecar);
	fork.captureGeometry(false, QList< int >() << 240 << 720);
	QCOMPARE(fork.save(), UiStateStore::SaveResult::Saved);
	QVERIFY(!fork.state().normalGeometry.isEmpty());

	// Simulate a crash mid-restoration: a fresh store arms the restore latch
	// and dies before finishing.
	{
		UiStateStore store(path);
		UiState candidate = fork.state();
		QVERIFY(store.beginGeometryRestore(candidate));
		// no finish/abort: the instance is dropped, emulating a crash
	}

	// The next start refuses to restore until the layout is explicitly reset.
	QWidget window2;
	UiStatePersistence fork2(s, window2);
	fork2.resolveAndPinPrimaryPath();
	QCOMPARE(fork2.startupLoad(), UiStateStore::LoadResult::LoadedRestoreBlocked);
	QVERIFY(!fork2.restoreGeometry(false));

	// The explicit reset releases the latch and clears geometry...
	QVERIFY(fork2.resetLayout());
	// ...and a subsequent start finds a clean, restorable sidecar.
	QWidget window3;
	UiStatePersistence fork3(s, window3);
	fork3.resolveAndPinPrimaryPath();
	QCOMPARE(fork3.startupLoad(), UiStateStore::LoadResult::Loaded);
	QVERIFY(fork3.state().normalGeometry.isEmpty());
	QVERIFY(!fork3.restoreGeometry(false)); // nothing stored; safe defaults

	// Re-seed geometry and confirm guarded restore works end to end.
	window3.resize(640, 440);
	window3.move(30, 40);
	fork3.captureGeometry(false, QList< int >() << 240 << 720);
	QCOMPARE(fork3.save(), UiStateStore::SaveResult::Saved);

	QWidget window4;
	UiStatePersistence fork4(s, window4);
	fork4.resolveAndPinPrimaryPath();
	QCOMPARE(fork4.startupLoad(), UiStateStore::LoadResult::Loaded);
	QVERIFY(fork4.restoreGeometry(false));
	QVERIFY(sizeNear(window4.size(), QSize(640, 440)));
}

void TestUiConfigurationCompatibility::resetClearsGeometryAndReleasesLatch() {
	QTemporaryDir dir;
	const QString path = seedOfficialProfile(dir, /*latchActive=*/false);

	Settings s;
	s.load(path, true);

	QWidget window;
	UiStatePersistence fork(s, window);
	fork.resolveAndPinPrimaryPath();
	QCOMPARE(fork.startupLoad(), UiStateStore::LoadResult::NoSidecar);
	fork.captureGeometry(false, QList< int >() << 240 << 720);
	fork.captureGeometry(true, QList< int >());
	QCOMPARE(fork.save(), UiStateStore::SaveResult::Saved);

	QVERIFY(fork.resetLayout());
	QVERIFY(fork.state().normalGeometry.isEmpty());
	QVERIFY(fork.state().compactGeometry.isEmpty());
	QVERIFY(fork.state().normalSplitterSizes.isEmpty());
	QCOMPARE(fork.state().minimal, false);

	// The latch release is durable: a fresh load finds a clean sidecar.
	UiStateStore store(path);
	QCOMPARE(store.load(), UiStateStore::LoadResult::Loaded);
	QVERIFY(!store.loadedState().restoreBlocked);
}

void TestUiConfigurationCompatibility::closeAfterResetPersistsNoStaleLayout() {
	QTemporaryDir dir;
	const QString path = seedOfficialProfile(dir, /*latchActive=*/false);

	Settings s;
	s.load(path, true);

	QWidget window;
	window.resize(555, 444);
	UiStatePersistence fork(s, window);
	fork.resolveAndPinPrimaryPath();
	QCOMPARE(fork.startupLoad(), UiStateStore::LoadResult::NoSidecar);

	// A pre-reset layout is persisted...
	fork.captureGeometry(false, QList< int >() << 240 << 720);
	QCOMPARE(fork.save(), UiStateStore::SaveResult::Saved);
	const QByteArray preResetGeometry = fork.state().normalGeometry;

	// ...then the user resets (MainWindow also applies the default rect to
	// the live window; the stand-in keeps a distinct post-reset geometry)...
	QVERIFY(fork.resetLayout());
	window.resize(700, 500);

	// ...and closes immediately: the capture persists the CURRENT (default)
	// geometry, never the pre-reset one.
	fork.captureGeometry(false, QList< int >() << 240 << 720);
	QCOMPARE(fork.save(), UiStateStore::SaveResult::Saved);
	QVERIFY(fork.state().normalGeometry != preResetGeometry);

	UiStateStore verify(path);
	QCOMPARE(verify.load(), UiStateStore::LoadResult::Loaded);
	QVERIFY(verify.loadedState().normalGeometry != preResetGeometry);
}

void TestUiConfigurationCompatibility::defaultWindowRectIsSane() {
	QTemporaryDir dir;
	const QString path = seedOfficialProfile(dir, /*latchActive=*/false);

	Settings s;
	s.load(path, true);

	// A stand-in with a REAL minimum (a bare QWidget reports an invalid
	// minimumSizeHint and the minimum assertions below would be vacuous).
	QWidget window;
	QVBoxLayout *windowLayout = new QVBoxLayout(&window);
	QWidget *windowContent    = new QWidget;
	windowContent->setMinimumSize(400, 300);
	windowLayout->addWidget(windowContent);
	window.resize(2000, 1600);
	UiStatePersistence fork(s, window);
	fork.resolveAndPinPrimaryPath();

	const QRect rect    = fork.defaultWindowRect();
	const QSize minimum = window.minimumSizeHint();
	QVERIFY(minimum.isValid());
	QVERIFY(minimum.width() >= 400 && minimum.height() >= 300);

	// Within the offscreen virtual screen (or clamped to it)...
	QScreen *screen = window.screen();
	if (!screen) {
		screen = QGuiApplication::primaryScreen();
	}
	QVERIFY(screen);
	QVERIFY(rect.width() <= screen->availableGeometry().width());
	QVERIFY(rect.height() <= screen->availableGeometry().height());
	// ...never below the window minimums...
	QVERIFY(rect.width() >= minimum.width());
	QVERIFY(rect.height() >= minimum.height());
	// ...and positioned inside the available area.
	QVERIFY(screen->availableGeometry().contains(rect.topLeft()));
}

void TestUiConfigurationCompatibility::windowFlagsPreservedWhenVisible() {
	QWidget window;
	window.resize(400, 300);
	window.move(80, 90);
	window.show();
	QApplication::processEvents();
	QVERIFY(window.isVisible());

	const QSize size = window.size();
	const QPoint pos = window.pos();

	// A real flag change (the live minimal/hide-frame toggle scenario).
	UiStatePersistence::applyWindowFlagsPreservingState(window, Qt::Window | Qt::FramelessWindowHint);
	QApplication::processEvents();

	QVERIFY(window.isVisible());
	QCOMPARE(window.windowFlags() & Qt::FramelessWindowHint, Qt::WindowFlags(Qt::FramelessWindowHint));
	QCOMPARE(window.size(), size);
	QVERIFY(pointNear(window.pos(), pos));
}

void TestUiConfigurationCompatibility::windowFlagsHiddenStaysHidden() {
	QWidget window;
	window.resize(400, 300);
	window.move(80, 90);
	QApplication::processEvents();
	QVERIFY(!window.isVisible());

	UiStatePersistence::applyWindowFlagsPreservingState(window, Qt::Window | Qt::FramelessWindowHint);
	QApplication::processEvents();

	// A tray-hidden or never-shown window must never be forced onto the
	// screen by a flag change.
	QVERIFY(!window.isVisible());
}

void TestUiConfigurationCompatibility::windowFlagsUnchangedIsNoOp() {
	QWidget window;
	window.resize(400, 300);
	window.move(80, 90);
	window.show();
	QApplication::processEvents();

	const Qt::WindowFlags before = window.windowFlags();
	// Same flags: the helper must not even call setWindowFlags (which would
	// hide the window).
	UiStatePersistence::applyWindowFlagsPreservingState(window, before);
	QApplication::processEvents();

	QVERIFY(window.isVisible());
	QCOMPARE(window.windowFlags(), before);
}

QTEST_MAIN(TestUiConfigurationCompatibility)
#include "TestUiConfigurationCompatibility.moc"

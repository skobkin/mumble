// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include "UiStateStore.h"

namespace {

/// Writes bytes to a file, creating or truncating it.
bool writeBytes(const QString &path, const QByteArray &bytes) {
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	if (file.write(bytes) != bytes.size()) {
		return false;
	}
	file.close();
	return true;
}

QByteArray readBytes(const QString &path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return QByteArray();
	}
	const QByteArray bytes = file.readAll();
	file.close();
	return bytes;
}

/// Creates a fixture "primary settings file" (plain sentinel content, never a
/// real Mumble configuration) and returns its path.
QString settingsPath(const QTemporaryDir &dir, const QString &name = QStringLiteral("mumble_settings.json")) {
	const QString path = dir.filePath(name);
	QDir().mkpath(QFileInfo(path).absolutePath());
	writeBytes(path, QByteArrayLiteral("{\"settings_version\": 1, \"fixture\": true}\n"));
	return path;
}

QString sidecarPathFor(const QString &settingsPath_) {
	return settingsPath_ + QStringLiteral(".new-ui.json");
}

/// QByteArray that passes the store's structural geometry validation: the
/// QWidget::saveGeometry() magic followed by filler to a plausible size.
QByteArray validGeometryBytes(int fillerSize = 64) {
	QByteArray bytes = QByteArray::fromHex("01d9d0cb");
	bytes.append(QByteArray(fillerSize, 'x'));
	return bytes;
}

UiState sampleState() {
	UiState state;
	state.minimal             = true;
	state.hideFrame           = false;
	state.activeTabIsActivity = true;
	state.normalGeometry      = validGeometryBytes();
	state.compactGeometry     = validGeometryBytes(48);
	state.normalSplitterSizes = QList< int >{ 240, 720 };
	return state;
}

/// Builds a valid revision-1 sidecar document (the retired "activityVisible"
/// era - exactly what a revision-1 client wrote), then applies key overrides
/// (added/replaced) and removals so individual fields can be corrupted.
QByteArray makeSidecar(const QJsonObject &overrides = QJsonObject(), const QStringList &removeKeys = QStringList()) {
	QJsonObject normal;
	normal.insert(QStringLiteral("splitterSizes"), QJsonArray{ 240, 720 });
	QJsonObject root;
	root.insert(QStringLiteral("version"), 1);
	root.insert(QStringLiteral("layoutRevision"), 1);
	root.insert(QStringLiteral("qtVersion"), QString::fromLatin1(qVersion()));
	root.insert(QStringLiteral("restoreBlocked"), false);
	root.insert(QStringLiteral("minimal"), false);
	root.insert(QStringLiteral("hideFrame"), false);
	root.insert(QStringLiteral("activityVisible"), true);
	root.insert(QStringLiteral("normal"), normal);
	root.insert(QStringLiteral("compact"), QJsonObject());
	for (auto it = overrides.begin(); it != overrides.end(); ++it) {
		root.insert(it.key(), it.value());
	}
	for (const QString &key : removeKeys) {
		root.remove(key);
	}
	return QJsonDocument(root).toJson();
}

QJsonObject parseObject(const QString &path) {
	return QJsonDocument::fromJson(readBytes(path)).object();
}

} // namespace

class TestUiStateStore : public QObject {
	Q_OBJECT
private slots:
	void missingSidecarDefaults();
	void saveLoadRoundTrip();
	void twoProfilesSameDirectory();
	void schemaVersionHandling();
	void newerVersionPreserved();
	/// layoutRevision 1 -> 2: a genuine revision-1 body (with the retired
	/// "activityVisible": true) loads as migrated Chat-active state, and the
	/// next save stamps revision 2 without quarantine. A revision-2 body
	/// without "activeTabIsActivity" default-fills to Chat.
	void layoutRevisionMigration();
	void invalidJsonAndTypes();
	void geometryValidation();
	void splitterZeroPaneRoundTrip();
	void failedWrites();
	void restorationGuard();
	void interruptedRestorationRecovery();
	void onDiskLatchAcrossInstances();
	void restoreProtocolNegativePaths();
	void nestedNonexistentDirectories();
	void sidecarPathResolution();
	void unknownKeysTolerated();
	void quarantineRecovery();
	void sharedSettingsUntouched();
};

void TestUiStateStore::missingSidecarDefaults() {
	QTemporaryDir dir;
	const QString settings = settingsPath(dir);
	UiStateStore store(settings);

	QCOMPARE(store.load(), UiStateStore::LoadResult::NoSidecar);

	const UiState &loaded = store.loadedState();
	QCOMPARE(loaded.minimal, false);
	QCOMPARE(loaded.hideFrame, false);
	QCOMPARE(loaded.activeTabIsActivity, false);
	QVERIFY(loaded.normalGeometry.isEmpty());
	QVERIFY(loaded.compactGeometry.isEmpty());
	QVERIFY(loaded.normalSplitterSizes.isEmpty());
	QCOMPARE(loaded.layoutRevision, 1);
	QCOMPARE(loaded.restoreBlocked, false);
	QVERIFY(loaded.qtVersion.isEmpty());

	// Loading must never create the sidecar.
	QVERIFY(!QFileInfo::exists(sidecarPathFor(settings)));
}

void TestUiStateStore::saveLoadRoundTrip() {
	QTemporaryDir dir;
	const QString settings  = settingsPath(dir);
	const QString sidecar   = sidecarPathFor(settings);
	const UiState reference = sampleState();

	UiStateStore store(settings);
	QString error;
	QCOMPARE(store.save(reference, &error), UiStateStore::SaveResult::Saved);
	QVERIFY(error.isEmpty());

	// On-disk format: indented, human-diagnosable, current metadata.
	const QByteArray raw = readBytes(sidecar);
	QVERIFY(raw.contains('\n'));
	const QJsonObject root = QJsonDocument::fromJson(raw).object();
	QCOMPARE(root.value(QStringLiteral("version")).toInt(), 1);
	QCOMPARE(root.value(QStringLiteral("layoutRevision")).toInt(), UiStateStore::LayoutRevision);
	QCOMPARE(root.value(QStringLiteral("qtVersion")).toString(), QString::fromLatin1(qVersion()));
	QCOMPARE(root.value(QStringLiteral("restoreBlocked")).toBool(), false);
	// Revision 2 writes the new field and no longer writes the retired one.
	QCOMPARE(root.value(QStringLiteral("activeTabIsActivity")).toBool(), true);
	QVERIFY(!root.contains(QStringLiteral("activityVisible")));

	// Round trip through a fresh store.
	UiStateStore reloaded(settings);
	QCOMPARE(reloaded.load(&error), UiStateStore::LoadResult::Loaded);
	QVERIFY(error.isEmpty());
	const UiState &loaded = reloaded.loadedState();
	QCOMPARE(loaded.minimal, reference.minimal);
	QCOMPARE(loaded.hideFrame, reference.hideFrame);
	QCOMPARE(loaded.activeTabIsActivity, reference.activeTabIsActivity);
	QCOMPARE(loaded.normalGeometry, reference.normalGeometry);
	QCOMPARE(loaded.compactGeometry, reference.compactGeometry);
	QCOMPARE(loaded.normalSplitterSizes, reference.normalSplitterSizes);
	QCOMPARE(loaded.layoutRevision, UiStateStore::LayoutRevision);
	QCOMPARE(loaded.qtVersion, QString::fromLatin1(qVersion()));
	QCOMPARE(loaded.restoreBlocked, false);
}

void TestUiStateStore::twoProfilesSameDirectory() {
	QTemporaryDir dir;
	const QString settingsA = settingsPath(dir, QStringLiteral("a.json"));
	const QString settingsB = settingsPath(dir, QStringLiteral("b.json"));
	const QString sidecarA  = sidecarPathFor(settingsA);
	const QString sidecarB  = sidecarPathFor(settingsB);

	QVERIFY(sidecarA != sidecarB);

	UiStateStore storeA(settingsA);
	UiStateStore storeB(settingsB);
	QCOMPARE(storeA.save(sampleState()), UiStateStore::SaveResult::Saved);

	UiState stateB;
	stateB.minimal = false;
	QCOMPARE(storeB.save(stateB), UiStateStore::SaveResult::Saved);
	const QByteArray bytesB = readBytes(sidecarB);

	// Re-saving profile A must not disturb profile B's sidecar.
	UiState stateA             = sampleState();
	stateA.normalSplitterSizes = QList< int >{ 100, 200 };
	QCOMPARE(storeA.save(stateA), UiStateStore::SaveResult::Saved);
	QCOMPARE(readBytes(sidecarB), bytesB);
}

void TestUiStateStore::schemaVersionHandling() {
	QTemporaryDir dir;
	const QString settings = settingsPath(dir);
	const QString sidecar  = sidecarPathFor(settings);

	const auto check = [&](const QByteArray &json, UiStateStore::LoadResult expected) {
		QVERIFY(writeBytes(sidecar, json));
		UiStateStore store(settings);
		QString error;
		QCOMPARE(store.load(&error), expected);
		if (expected == UiStateStore::LoadResult::Invalid) {
			// Invalid files are preserved, defaults returned.
			QCOMPARE(readBytes(sidecar), json);
			QCOMPARE(store.loadedState().minimal, false);
		}
	};

	// Baseline: the reference document loads.
	check(makeSidecar(), UiStateStore::LoadResult::Loaded);

	// Newer versions are unsupported - including files whose body would fail
	// current-revision validation (version gate runs before body validation).
	check(makeSidecar({ { QStringLiteral("version"), 2 } }), UiStateStore::LoadResult::Unsupported);
	check(makeSidecar({ { QStringLiteral("version"), 2 }, { QStringLiteral("minimal"), QStringLiteral("garbage") } }),
		  UiStateStore::LoadResult::Unsupported);
	check(makeSidecar({ { QStringLiteral("layoutRevision"), UiStateStore::LayoutRevision + 1 } }),
		  UiStateStore::LoadResult::Unsupported);

	// Version 0 / negative / missing / wrong type: corruption, not older files
	// (both formats start at 1; there is no migration from version 0).
	check(makeSidecar({ { QStringLiteral("version"), 0 } }), UiStateStore::LoadResult::Invalid);
	check(makeSidecar({ { QStringLiteral("version"), -1 } }), UiStateStore::LoadResult::Invalid);
	check(makeSidecar({ { QStringLiteral("version"), 1.5 } }), UiStateStore::LoadResult::Invalid);
	check(makeSidecar({ { QStringLiteral("version"), QStringLiteral("1") } }), UiStateStore::LoadResult::Invalid);
	check(makeSidecar(QJsonObject(), { QStringLiteral("version") }), UiStateStore::LoadResult::Invalid);
	check(makeSidecar({ { QStringLiteral("layoutRevision"), 0 } }), UiStateStore::LoadResult::Invalid);
	check(makeSidecar({ { QStringLiteral("layoutRevision"), QStringLiteral("1") } }),
		  UiStateStore::LoadResult::Invalid);
	check(makeSidecar(QJsonObject(), { QStringLiteral("layoutRevision") }), UiStateStore::LoadResult::Invalid);
}

void TestUiStateStore::newerVersionPreserved() {
	QTemporaryDir dir;
	const QString settings = settingsPath(dir);
	const QString sidecar  = sidecarPathFor(settings);
	const QByteArray newerBytes =
		makeSidecar({ { QStringLiteral("version"), 2 }, { QStringLiteral("minimal"), QStringLiteral("garbage") } });
	QVERIFY(writeBytes(sidecar, newerBytes));

	UiStateStore store(settings);
	QString error;
	QCOMPARE(store.load(&error), UiStateStore::LoadResult::Unsupported);
	QCOMPARE(store.save(sampleState(), &error), UiStateStore::SaveResult::RefusedNewerVersion);
	QCOMPARE(store.beginGeometryRestore(sampleState(), &error), false);
	// A newer-version file must not be quarantined either.
	QCOMPARE(store.quarantineExisting(&error), false);
	QCOMPARE(readBytes(sidecar), newerBytes);

	// finish/abort hit the same guard when the file is replaced by a newer
	// one after a successful begin.
	QTemporaryDir dir2;
	const QString settings2 = settingsPath(dir2, QStringLiteral("second.json"));
	const QString sidecar2  = sidecarPathFor(settings2);
	UiStateStore guard(settings2);
	QCOMPARE(guard.save(sampleState()), UiStateStore::SaveResult::Saved);
	QCOMPARE(guard.beginGeometryRestore(sampleState()), true);
	QVERIFY(writeBytes(sidecar2, newerBytes));
	QCOMPARE(guard.finishGeometryRestore(sampleState(), &error), UiStateStore::SaveResult::RefusedNewerVersion);
	QCOMPARE(guard.abortGeometryRestore(sampleState(), &error), UiStateStore::SaveResult::RefusedNewerVersion);
	QCOMPARE(readBytes(sidecar2), newerBytes);
}

void TestUiStateStore::layoutRevisionMigration() {
	QTemporaryDir dir;
	const QString settings = settingsPath(dir);
	const QString sidecar  = sidecarPathFor(settings);

	// A genuine revision-1 body, exactly what a revision-1 client wrote
	// (makeSidecar): "activityVisible": true and no "activeTabIsActivity".
	QVERIFY(writeBytes(sidecar, makeSidecar()));
	// Drop the retired key in a second variant to cover its absence too.
	const QByteArray withoutRetired = makeSidecar(QJsonObject(), { QStringLiteral("activityVisible") });

	UiStateStore store(settings);
	QString error;
	QCOMPARE(store.load(&error), UiStateStore::LoadResult::Loaded);
	// Migration semantics: the retired boolean is ignored (it never labeled a
	// shipped control); the revision-2 field default-fills to Chat.
	QCOMPARE(store.loadedState().layoutRevision, 1);
	QCOMPARE(store.loadedState().activeTabIsActivity, false);
	QCOMPARE(store.loadedState().minimal, false);

	// The next save stamps the current revision (and drops the retired key).
	QCOMPARE(store.save(sampleState(), &error), UiStateStore::SaveResult::Saved);
	const QJsonObject root = parseObject(sidecar);
	QCOMPARE(root.value(QStringLiteral("layoutRevision")).toInt(), UiStateStore::LayoutRevision);
	QVERIFY(!root.contains(QStringLiteral("activityVisible")));
	QCOMPARE(root.value(QStringLiteral("activeTabIsActivity")).toBool(), true);

	// Reload: the saved revision-2 value round-trips.
	QCOMPARE(store.load(&error), UiStateStore::LoadResult::Loaded);
	QCOMPARE(store.loadedState().activeTabIsActivity, true);
	QCOMPARE(store.loadedState().layoutRevision, UiStateStore::LayoutRevision);

	// A revision-2 body without the new field default-fills (missing = Chat).
	QVERIFY(writeBytes(sidecar, makeSidecar({ { QStringLiteral("layoutRevision"), UiStateStore::LayoutRevision } },
											{ QStringLiteral("activeTabIsActivity") })));
	QCOMPARE(store.load(&error), UiStateStore::LoadResult::Loaded);
	QCOMPARE(store.loadedState().activeTabIsActivity, false);

	// And the no-retired-key revision-1 variant migrates the same way.
	QVERIFY(writeBytes(sidecar, withoutRetired));
	QCOMPARE(store.load(&error), UiStateStore::LoadResult::Loaded);
	QCOMPARE(store.loadedState().activeTabIsActivity, false);
}

void TestUiStateStore::invalidJsonAndTypes() {
	QTemporaryDir dir;
	const QString settings = settingsPath(dir);
	const QString sidecar  = sidecarPathFor(settings);

	const auto check = [&](const QByteArray &json) {
		QVERIFY(writeBytes(sidecar, json));
		UiStateStore store(settings);
		QString error;
		QCOMPARE(store.load(&error), UiStateStore::LoadResult::Invalid);
		// File preserved; defaults in memory.
		QCOMPARE(readBytes(sidecar), json);
		QCOMPARE(store.loadedState().minimal, false);
		// Writes refuse to replace the invalid file.
		QCOMPARE(store.save(sampleState(), &error), UiStateStore::SaveResult::RefusedInvalidExisting);
		QCOMPARE(readBytes(sidecar), json);
	};

	check(QByteArrayLiteral("{\"version\": 1,")); // truncated JSON
	check(QByteArrayLiteral("[1, 2, 3]"));        // non-object root
	check(makeSidecar({ { QStringLiteral("minimal"), QStringLiteral("yes") } }));
	check(makeSidecar({ { QStringLiteral("hideFrame"), 1 } }));
	// The retired revision-1 "activityVisible" is an ignored key now - even a
	// bad-typed value in a revision-1 body is not corruption. Revision 2's
	// replacement field IS validated when present.
	check(makeSidecar({ { QStringLiteral("layoutRevision"), UiStateStore::LayoutRevision },
						{ QStringLiteral("activeTabIsActivity"), 1 } }));
	check(makeSidecar({ { QStringLiteral("restoreBlocked"), QStringLiteral("true") } }));
	check(makeSidecar(QJsonObject(), { QStringLiteral("qtVersion") })); // required metadata
	check(makeSidecar({ { QStringLiteral("qtVersion"), 62 } }));
	check(makeSidecar({ { QStringLiteral("qtVersion"), QString() } }));
	check(makeSidecar({ { QStringLiteral("normal"), 5 } })); // non-object mode
	check(makeSidecar(
		{ { QStringLiteral("normal"), QJsonObject{ { QStringLiteral("geometry"), 123 } } } })); // wrong type
	check(makeSidecar(
		{ { QStringLiteral("normal"), QJsonObject{ { QStringLiteral("splitterSizes"), QStringLiteral("x") } } } }));
	check(makeSidecar(
		{ { QStringLiteral("normal"), QJsonObject{ { QStringLiteral("splitterSizes"), QJsonArray{ 240, -1 } } } } }));
	check(makeSidecar(
		{ { QStringLiteral("normal"), QJsonObject{ { QStringLiteral("splitterSizes"), QJsonArray{ 1.5, 2 } } } } }));
	check(makeSidecar(
		{ { QStringLiteral("normal"), QJsonObject{ { QStringLiteral("splitterSizes"), QJsonArray{ 240 } } } } }));
	check(makeSidecar(
		{ { QStringLiteral("normal"), QJsonObject{ { QStringLiteral("splitterSizes"), QJsonArray{ 1, 70000 } } } } }));
	check(makeSidecar({ { QStringLiteral("normal"), QJsonObject{ { QStringLiteral("splitterSizes"),
																   QJsonArray{ 1, 2, 3, 4, 5, 6, 7, 8, 9 } } } } }));
}

void TestUiStateStore::geometryValidation() {
	QTemporaryDir dir;
	const QString settings = settingsPath(dir);
	const QString sidecar  = sidecarPathFor(settings);

	const auto checkInvalid = [&](const QByteArray &json) {
		QVERIFY(writeBytes(sidecar, json));
		UiStateStore store(settings);
		QString error;
		QCOMPARE(store.load(&error), UiStateStore::LoadResult::Invalid);
		QCOMPARE(readBytes(sidecar), json);
	};

	const auto withNormalGeometry = [](const QByteArray &geometry) {
		return makeSidecar(
			{ { QStringLiteral("normal"),
				QJsonObject{ { QStringLiteral("geometry"), QString::fromLatin1(geometry.toBase64()) } } } });
	};

	// Not base64.
	checkInvalid(
		makeSidecar({ { QStringLiteral("normal"),
						QJsonObject{ { QStringLiteral("geometry"), QStringLiteral("!!!! not base64 !!!!") } } } }));
	// Decodes, but below the minimum plausible size.
	checkInvalid(withNormalGeometry(QByteArrayLiteral("01234567")));
	// Decodes with the right magic, but oversized.
	QByteArray oversized = validGeometryBytes(0);
	oversized.append(QByteArray(UiStateStore::MaxGeometryBytes, 'x'));
	checkInvalid(withNormalGeometry(oversized));
	// Plausible size, but not Qt geometry data (wrong magic).
	checkInvalid(withNormalGeometry(QByteArray(UiStateStore::MinGeometryBytes + 16, 0)));

	// Oversized geometry passed programmatically is rejected before any write.
	UiStateStore store(settings);
	UiState bad        = sampleState();
	bad.normalGeometry = oversized;
	QString error;
	QCOMPARE(store.save(bad, &error), UiStateStore::SaveResult::InvalidInput);
	QVERIFY(!error.isEmpty());

	// Qt-version gate: geometry written by a different Qt version is dropped
	// in favor of safe placement; simple preferences survive.
	const QByteArray geometryJson = makeSidecar(
		{ { QStringLiteral("qtVersion"), QStringLiteral("5.0.0") },
		  { QStringLiteral("minimal"), true },
		  { QStringLiteral("normal"),
			QJsonObject{ { QStringLiteral("geometry"), QString::fromLatin1(validGeometryBytes().toBase64()) } } } });
	QVERIFY(writeBytes(sidecar, geometryJson));
	UiStateStore foreign(settings);
	QCOMPARE(foreign.load(&error), UiStateStore::LoadResult::LoadedGeometryDropped);
	QCOMPARE(foreign.loadedState().minimal, true);
	QVERIFY(foreign.loadedState().normalGeometry.isEmpty());

	// Saving the adjusted state stamps the running Qt version (no stale
	// qtVersion loop).
	QCOMPARE(foreign.save(foreign.loadedState(), &error), UiStateStore::SaveResult::Saved);
	const QJsonObject root = parseObject(sidecar);
	QCOMPARE(root.value(QStringLiteral("qtVersion")).toString(), QString::fromLatin1(qVersion()));
	QVERIFY(!root.value(QStringLiteral("normal")).toObject().contains(QStringLiteral("geometry")));
	UiStateStore afterSave(settings);
	QCOMPARE(afterSave.load(), UiStateStore::LoadResult::Loaded);
}

void TestUiStateStore::splitterZeroPaneRoundTrip() {
	QTemporaryDir dir;
	const QString settings = settingsPath(dir);

	// Zero is a valid splitter size (Qt collapses a pane to zero); it must
	// survive a save/load round trip.
	UiState state;
	state.normalSplitterSizes = QList< int >{ 240, 0, 720 };
	UiStateStore store(settings);
	QString error;
	QCOMPARE(store.save(state, &error), UiStateStore::SaveResult::Saved);
	QVERIFY(error.isEmpty());
	UiStateStore reloaded(settings);
	QCOMPARE(reloaded.load(&error), UiStateStore::LoadResult::Loaded);
	const QList< int > expectedSizes{ 240, 0, 720 };
	QCOMPARE(reloaded.loadedState().normalSplitterSizes, expectedSizes);

	// Parsing accepts a zero pane from a hand-written sidecar as well.
	const QString zeroSettings = settingsPath(dir, QStringLiteral("zero.json"));
	QVERIFY(writeBytes(sidecarPathFor(zeroSettings),
					   makeSidecar({ { QStringLiteral("normal"),
									   QJsonObject{ { QStringLiteral("splitterSizes"), QJsonArray{ 0, 100 } } } } })));
	UiStateStore zeroed(zeroSettings);
	QCOMPARE(zeroed.load(&error), UiStateStore::LoadResult::Loaded);
	const QList< int > expectedZeroSizes{ 0, 100 };
	QCOMPARE(zeroed.loadedState().normalSplitterSizes, expectedZeroSizes);

	// Negative and oversized values remain invalid when passed
	// programmatically (the on-disk variants are covered by
	// invalidJsonAndTypes).
	UiState negative;
	negative.normalSplitterSizes = QList< int >{ -1, 100 };
	QCOMPARE(store.save(negative, &error), UiStateStore::SaveResult::InvalidInput);
	QVERIFY(!error.isEmpty());
	UiState oversized;
	oversized.normalSplitterSizes = QList< int >{ 100, UiStateStore::MaxSplitterSize + 1 };
	QCOMPARE(store.save(oversized, &error), UiStateStore::SaveResult::InvalidInput);
}

void TestUiStateStore::failedWrites() {
	QTemporaryDir dir;

	// --- Empty primary path: usable session without persistence ---
	UiStateStore nowhere{ QString() };
	QString error;
	QCOMPARE(nowhere.load(&error), UiStateStore::LoadResult::NoSidecar);
	QCOMPARE(nowhere.save(sampleState(), &error), UiStateStore::SaveResult::WriteError);
	QCOMPARE(nowhere.beginGeometryRestore(sampleState(), &error), false);
	QCOMPARE(nowhere.quarantineExisting(&error), false);

	// --- Sidecar path occupied by a directory ---
	const QString settingsDirCase = settingsPath(dir, QStringLiteral("dirconflict.json"));
	const QString sidecarDir      = sidecarPathFor(settingsDirCase);
	QVERIFY(QDir().mkpath(sidecarDir));
	UiStateStore blocked(settingsDirCase);
	QCOMPARE(blocked.load(&error), UiStateStore::LoadResult::IoError);
	QCOMPARE(blocked.save(sampleState(), &error), UiStateStore::SaveResult::WriteError);
	QCOMPARE(blocked.beginGeometryRestore(sampleState(), &error), false);
	QCOMPARE(blocked.quarantineExisting(&error), false);

#ifdef Q_OS_UNIX
	// --- Read-only directory: atomic writes fail, loads still work ---
	const QString roSettings = settingsPath(dir, QStringLiteral("ro/mumble_settings.json"));
	const QString roDir      = QFileInfo(roSettings).absolutePath();
	QVERIFY(QDir().mkpath(roDir));
	const QString roSidecar = sidecarPathFor(roSettings);
	QVERIFY(writeBytes(roSidecar, makeSidecar()));

	QFile roDirFile(roDir);
	QVERIFY(roDirFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::ExeOwner));
	UiStateStore readOnly(roSettings);
	QCOMPARE(readOnly.load(&error), UiStateStore::LoadResult::Loaded);
	QCOMPARE(readOnly.save(sampleState(), &error), UiStateStore::SaveResult::WriteError);
	QCOMPARE(readOnly.beginGeometryRestore(sampleState(), &error), false);
	QCOMPARE(readOnly.finishGeometryRestore(sampleState(), &error), UiStateStore::SaveResult::InvalidOperation);
	// Restore permissions so QTemporaryDir cleanup can remove the directory.
	QVERIFY(roDirFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
#else
	QSKIP("Read-only directory enforcement is not available on this platform");
#endif
}

void TestUiStateStore::restorationGuard() {
	QTemporaryDir dir;
	const QString settings  = settingsPath(dir);
	const QString sidecar   = sidecarPathFor(settings);
	const UiState candidate = sampleState();

	UiStateStore store(settings);
	QCOMPARE(store.load(), UiStateStore::LoadResult::NoSidecar);

	// A successful begin writes the guard: latch set, no geometry bytes.
	QCOMPARE(store.beginGeometryRestore(candidate), true);
	QJsonObject root = parseObject(sidecar);
	QCOMPARE(root.value(QStringLiteral("restoreBlocked")).toBool(), true);
	QVERIFY(!root.value(QStringLiteral("normal")).toObject().contains(QStringLiteral("geometry")));
	QVERIFY(!root.value(QStringLiteral("compact")).toObject().contains(QStringLiteral("geometry")));
	QCOMPARE(
		root.value(QStringLiteral("normal")).toObject().value(QStringLiteral("splitterSizes")).toArray().at(0).toInt(),
		240);
	QCOMPARE(root.value(QStringLiteral("minimal")).toBool(), true);

	// Second begin is refused; a mid-protocol save stays latched and
	// geometry-free.
	QCOMPARE(store.beginGeometryRestore(candidate), false);
	UiState midSave = sampleState();
	QCOMPARE(store.save(midSave), UiStateStore::SaveResult::Saved);
	root = parseObject(sidecar);
	QCOMPARE(root.value(QStringLiteral("restoreBlocked")).toBool(), true);
	QVERIFY(!root.value(QStringLiteral("normal")).toObject().contains(QStringLiteral("geometry")));

	// finish without a successful begin is a hard protocol error.
	UiStateStore fresh(settingsPath(dir, QStringLiteral("other.json")));
	QCOMPARE(fresh.finishGeometryRestore(candidate), UiStateStore::SaveResult::InvalidOperation);

	// A proper finish persists geometry together with the cleared latch.
	UiState restored        = candidate;
	restored.normalGeometry = validGeometryBytes(80);
	QCOMPARE(store.finishGeometryRestore(restored), UiStateStore::SaveResult::Saved);
	root = parseObject(sidecar);
	QCOMPARE(root.value(QStringLiteral("restoreBlocked")).toBool(), false);
	QCOMPARE(
		QByteArray::fromBase64(
			root.value(QStringLiteral("normal")).toObject().value(QStringLiteral("geometry")).toString().toLatin1()),
		restored.normalGeometry);
	UiStateStore afterFinish(settings);
	QCOMPARE(afterFinish.load(), UiStateStore::LoadResult::Loaded);
	QCOMPARE(afterFinish.loadedState().normalGeometry, restored.normalGeometry);

	// Abort after a failed QWidget::restoreGeometry(): latch released,
	// geometry discarded.
	const QString settingsB = settingsPath(dir, QStringLiteral("b.json"));
	UiStateStore aborting(settingsB);
	QCOMPARE(aborting.beginGeometryRestore(candidate), true);
	QCOMPARE(aborting.abortGeometryRestore(candidate), UiStateStore::SaveResult::Saved);
	root = parseObject(sidecarPathFor(settingsB));
	QCOMPARE(root.value(QStringLiteral("restoreBlocked")).toBool(), false);
	QVERIFY(!root.value(QStringLiteral("normal")).toObject().contains(QStringLiteral("geometry")));

	// begin is refused while the loaded file is latched.
	const QString settingsC = settingsPath(dir, QStringLiteral("c.json"));
	QVERIFY(writeBytes(sidecarPathFor(settingsC), makeSidecar({ { QStringLiteral("restoreBlocked"), true } })));
	UiStateStore blocked(settingsC);
	QCOMPARE(blocked.load(), UiStateStore::LoadResult::LoadedRestoreBlocked);
	QCOMPARE(blocked.beginGeometryRestore(sampleState()), false);
}

void TestUiStateStore::interruptedRestorationRecovery() {
	QTemporaryDir dir;
	const QString settings = settingsPath(dir);
	const QString sidecar  = sidecarPathFor(settings);

	// Simulate the on-disk state after a crash between guard write and finish.
	QVERIFY(writeBytes(
		sidecar, makeSidecar({ { QStringLiteral("restoreBlocked"), true }, { QStringLiteral("minimal"), true } })));
	UiStateStore store(settings);
	QString error;
	QCOMPARE(store.load(&error), UiStateStore::LoadResult::LoadedRestoreBlocked);
	QCOMPARE(store.loadedState().restoreBlocked, true);
	QCOMPARE(store.loadedState().minimal, true);
	QVERIFY(store.loadedState().normalGeometry.isEmpty());

	// An ordinary save keeps the latch and strips geometry - even when the
	// caller passes geometry bytes.
	UiState captured        = store.loadedState();
	captured.normalGeometry = validGeometryBytes();
	QCOMPARE(store.save(captured, &error), UiStateStore::SaveResult::Saved);
	QJsonObject root = parseObject(sidecar);
	QCOMPARE(root.value(QStringLiteral("restoreBlocked")).toBool(), true);
	QVERIFY(!root.value(QStringLiteral("normal")).toObject().contains(QStringLiteral("geometry")));
	UiStateStore reloaded(settings);
	QCOMPARE(reloaded.load(), UiStateStore::LoadResult::LoadedRestoreBlocked);

	// The explicit reset releases the latch and clears geometry together.
	QCOMPARE(store.save(captured, &error, /*clearRestoreBlock=*/true), UiStateStore::SaveResult::Saved);
	root = parseObject(sidecar);
	QCOMPARE(root.value(QStringLiteral("restoreBlocked")).toBool(), false);
	QVERIFY(!root.value(QStringLiteral("normal")).toObject().contains(QStringLiteral("geometry")));
	QVERIFY(!root.value(QStringLiteral("compact")).toObject().contains(QStringLiteral("geometry")));

	// With the latch released, an ordinary save persists geometry normally.
	QCOMPARE(store.save(sampleState(), &error), UiStateStore::SaveResult::Saved);
	root = parseObject(sidecar);
	QCOMPARE(root.value(QStringLiteral("restoreBlocked")).toBool(), false);
	QVERIFY(root.value(QStringLiteral("normal")).toObject().contains(QStringLiteral("geometry")));
	UiStateStore fresh(settings);
	QCOMPARE(fresh.load(), UiStateStore::LoadResult::Loaded);
	QCOMPARE(fresh.loadedState().normalGeometry, sampleState().normalGeometry);
}

void TestUiStateStore::onDiskLatchAcrossInstances() {
	QTemporaryDir dir;
	const QString settings = settingsPath(dir);
	const QString sidecar  = sidecarPathFor(settings);

	// Simulate a crash between guard write and finish: the latch is on disk,
	// and the process that set it is gone.
	UiStateStore crashed(settings);
	QCOMPARE(crashed.beginGeometryRestore(sampleState()), true);

	// A fresh instance that never called load() must not smuggle geometry
	// past the on-disk latch through an ordinary save: preferences persist,
	// the latch stays, geometry is stripped.
	UiStateStore unaware(settings);
	QString error;
	QCOMPARE(unaware.save(sampleState(), &error), UiStateStore::SaveResult::Saved);
	QJsonObject root = parseObject(sidecar);
	QCOMPARE(root.value(QStringLiteral("restoreBlocked")).toBool(), true);
	QCOMPARE(root.value(QStringLiteral("minimal")).toBool(), true);
	QVERIFY(!root.value(QStringLiteral("normal")).toObject().contains(QStringLiteral("geometry")));
	QVERIFY(!root.value(QStringLiteral("compact")).toObject().contains(QStringLiteral("geometry")));

	// A fresh instance must not start a new guarded restoration either (that
	// would re-arm the crashed restoration and defeat the latch).
	UiStateStore alsoUnaware(settings);
	QCOMPARE(alsoUnaware.beginGeometryRestore(sampleState(), &error), false);
	QVERIFY(!error.isEmpty());
	error.clear();

	// Only the explicit reset releases the on-disk latch - and it persists no
	// geometry.
	UiStateStore resetting(settings);
	QCOMPARE(resetting.save(sampleState(), &error, /*clearRestoreBlock=*/true), UiStateStore::SaveResult::Saved);
	root = parseObject(sidecar);
	QCOMPARE(root.value(QStringLiteral("restoreBlocked")).toBool(), false);
	QVERIFY(!root.value(QStringLiteral("normal")).toObject().contains(QStringLiteral("geometry")));
	QVERIFY(!root.value(QStringLiteral("compact")).toObject().contains(QStringLiteral("geometry")));

	// With the latch released, an ordinary save from yet another fresh
	// instance persists geometry normally.
	UiStateStore next(settings);
	QCOMPARE(next.save(sampleState(), &error), UiStateStore::SaveResult::Saved);
	root = parseObject(sidecar);
	QCOMPARE(root.value(QStringLiteral("restoreBlocked")).toBool(), false);
	QVERIFY(root.value(QStringLiteral("normal")).toObject().contains(QStringLiteral("geometry")));
	UiStateStore reloaded(settings);
	QCOMPARE(reloaded.load(), UiStateStore::LoadResult::Loaded);
	QCOMPARE(reloaded.loadedState().normalGeometry, sampleState().normalGeometry);
}

void TestUiStateStore::restoreProtocolNegativePaths() {
	QTemporaryDir dir;
	const QString settings = settingsPath(dir);
	const QString sidecar  = sidecarPathFor(settings);

	// abort without a preceding successful begin is a hard protocol error,
	// exactly like finish; nothing is written.
	UiStateStore store(settings);
	QString error;
	QCOMPARE(store.abortGeometryRestore(sampleState(), &error), UiStateStore::SaveResult::InvalidOperation);
	QVERIFY(!error.isEmpty());
	QVERIFY(!QFileInfo::exists(sidecar));

#ifdef Q_OS_UNIX
	// A failed explicit reset leaves an active restoration intact: the
	// protocol can still be finished afterwards.
	const QString roSettings = settingsPath(dir, QStringLiteral("ro/mumble_settings.json"));
	const QString roDir      = QFileInfo(roSettings).absolutePath();
	const QString roSidecar  = sidecarPathFor(roSettings);
	UiStateStore restoring(roSettings);
	QCOMPARE(restoring.beginGeometryRestore(sampleState()), true);

	QFile roDirFile(roDir);
	QVERIFY(roDirFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::ExeOwner));
	QCOMPARE(restoring.save(sampleState(), &error, /*clearRestoreBlock=*/true), UiStateStore::SaveResult::WriteError);
	// Restore permissions so QTemporaryDir cleanup can remove the directory.
	QVERIFY(roDirFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

	// The failed reset did not cancel the restoration: finish still works and
	// persists geometry together with the released latch.
	QCOMPARE(restoring.finishGeometryRestore(sampleState(), &error), UiStateStore::SaveResult::Saved);
	const QJsonObject root = parseObject(roSidecar);
	QCOMPARE(root.value(QStringLiteral("restoreBlocked")).toBool(), false);
	QVERIFY(root.value(QStringLiteral("normal")).toObject().contains(QStringLiteral("geometry")));
#else
	QSKIP("Read-only directory enforcement is not available on this platform");
#endif
}

void TestUiStateStore::nestedNonexistentDirectories() {
	QTemporaryDir dir;
	const QString canonical = QFileInfo(dir.path()).canonicalFilePath();
	QVERIFY(!canonical.isEmpty());

	// Multiple missing intermediate directories are preserved in the derived
	// sidecar path (regression: they used to be dropped while walking up to
	// the deepest existing ancestor).
	const QString deepSettings = dir.filePath(QStringLiteral("profiles/group/nested/deep/mumble_settings.json"));
	const QString deepSidecar =
		canonical + QStringLiteral("/profiles/group/nested/deep/mumble_settings.json.new-ui.json");
	QCOMPARE(UiStateStore::sidecarPathFor(deepSettings), deepSidecar);
	UiStateStore deep(deepSettings);
	QCOMPARE(deep.sidecarPath(), deepSidecar);
	QCOMPARE(deep.load(), UiStateStore::LoadResult::NoSidecar);

	// Distinct profiles with identical file names in different not-yet-created
	// directories derive distinct sidecar paths (they used to collapse into
	// one) and stay independent once the directories come into existence.
	const QString settingsA = dir.filePath(QStringLiteral("notyet/a/mumble_settings.json"));
	const QString settingsB = dir.filePath(QStringLiteral("notyet/b/mumble_settings.json"));
	const QString sidecarA  = canonical + QStringLiteral("/notyet/a/mumble_settings.json.new-ui.json");
	const QString sidecarB  = canonical + QStringLiteral("/notyet/b/mumble_settings.json.new-ui.json");
	QCOMPARE(UiStateStore::sidecarPathFor(settingsA), sidecarA);
	QCOMPARE(UiStateStore::sidecarPathFor(settingsB), sidecarB);
	QVERIFY(sidecarA != sidecarB);

	QVERIFY(QDir().mkpath(QFileInfo(settingsA).absolutePath()));
	QVERIFY(QDir().mkpath(QFileInfo(settingsB).absolutePath()));
	QVERIFY(writeBytes(settingsA, QByteArrayLiteral("{}")));
	QVERIFY(writeBytes(settingsB, QByteArrayLiteral("{}")));
	UiStateStore storeA(settingsA);
	UiStateStore storeB(settingsB);
	QCOMPARE(storeA.save(sampleState()), UiStateStore::SaveResult::Saved);
	UiState plain;
	plain.minimal = true;
	QCOMPARE(storeB.save(plain), UiStateStore::SaveResult::Saved);
	QVERIFY(QFileInfo::exists(sidecarA));
	QVERIFY(QFileInfo::exists(sidecarB));

	UiStateStore reloadedA(settingsA);
	QCOMPARE(reloadedA.load(), UiStateStore::LoadResult::Loaded);
	QCOMPARE(reloadedA.loadedState().normalGeometry, sampleState().normalGeometry);
	UiStateStore reloadedB(settingsB);
	QCOMPARE(reloadedB.load(), UiStateStore::LoadResult::Loaded);
	QCOMPARE(reloadedB.loadedState().minimal, true);
	QVERIFY(reloadedB.loadedState().normalGeometry.isEmpty());
}

void TestUiStateStore::sidecarPathResolution() {
	QTemporaryDir dir;
	const QString canonical = QFileInfo(dir.path()).canonicalFilePath();
	QVERIFY(!canonical.isEmpty());

	// Relative input is absolutized (defensively; the contract is absolute).
	const QString originalCwd = QDir::current().absolutePath();
	if (QDir::setCurrent(dir.path())) {
		QCOMPARE(UiStateStore::sidecarPathFor(QStringLiteral("mumble_settings.json")),
				 QDir(canonical).filePath(QStringLiteral("mumble_settings.json.new-ui.json")));
	}
	QVERIFY(QDir::setCurrent(originalCwd));

	// A trailing ".back" backup suffix resolves to the primary sidecar.
	QCOMPARE(UiStateStore::sidecarPathFor(dir.filePath(QStringLiteral("mumble_settings.json.back"))),
			 QDir(canonical).filePath(QStringLiteral("mumble_settings.json.new-ui.json")));

	// A nonexistent primary in an existing directory resolves inside it.
	QCOMPARE(UiStateStore::sidecarPathFor(dir.filePath(QStringLiteral("new-profile.json"))),
			 QDir(canonical).filePath(QStringLiteral("new-profile.json.new-ui.json")));

	// Empty input: no sidecar path at all.
	QVERIFY(UiStateStore::sidecarPathFor(QString()).isEmpty());

	// A symlinked settings directory resolves through to the real directory.
	const QString realDir      = dir.filePath(QStringLiteral("real"));
	const QString settingsFile = QDir(realDir).filePath(QStringLiteral("mumble_settings.json"));
	QVERIFY(QDir().mkpath(realDir));
	QVERIFY(writeBytes(settingsFile, QByteArrayLiteral("{}")));
	const QString aliasDir = dir.filePath(QStringLiteral("alias"));
	if (!QFile::link(realDir, aliasDir)) {
		QSKIP("Cannot create symlinks on this platform");
	}
	QCOMPARE(UiStateStore::sidecarPathFor(QDir(aliasDir).filePath(QStringLiteral("mumble_settings.json"))),
			 QDir(QFileInfo(realDir).canonicalFilePath()).filePath(QStringLiteral("mumble_settings.json.new-ui.json")));

	// A symlinked settings file keeps its sidecar next to the symlink (the
	// primary write path), not next to the link target: the shared saver
	// replaces its target via rename, so resolving the file component through
	// the symlink would silently move the sidecar between runs.
	const QString otherDir = dir.filePath(QStringLiteral("other"));
	QVERIFY(QDir().mkpath(otherDir));
	const QString targetFile = QDir(otherDir).filePath(QStringLiteral("real-settings.json"));
	QVERIFY(writeBytes(targetFile, QByteArrayLiteral("{}")));
	const QString linkFile = dir.filePath(QStringLiteral("link-settings.json"));
	if (!QFile::link(targetFile, linkFile)) {
		QSKIP("Cannot create symlinks on this platform");
	}
	QCOMPARE(UiStateStore::sidecarPathFor(linkFile),
			 QDir(canonical).filePath(QStringLiteral("link-settings.json.new-ui.json")));
}

void TestUiStateStore::unknownKeysTolerated() {
	QTemporaryDir dir;
	const QString settings = settingsPath(dir);
	QVERIFY(writeBytes(sidecarPathFor(settings), makeSidecar({ { QStringLiteral("futureField"), QStringLiteral("x") },
															   { QStringLiteral("anotherFutureField"), 42 } })));
	UiStateStore store(settings);
	QString error;
	QCOMPARE(store.load(&error), UiStateStore::LoadResult::Loaded);
	const QList< int > expectedSizes{ 240, 720 };
	QCOMPARE(store.loadedState().normalSplitterSizes, expectedSizes);
	// Overwriting a valid file with unknown keys is allowed.
	QCOMPARE(store.save(sampleState(), &error), UiStateStore::SaveResult::Saved);
}

void TestUiStateStore::quarantineRecovery() {
	QTemporaryDir dir;
	const QString settings = settingsPath(dir);
	const QString sidecar  = sidecarPathFor(settings);

	// Invalid file: writes refuse; quarantine preserves bytes and resumes
	// normal persistence.
	const QByteArray invalidBytes = QByteArrayLiteral("definitely not json");
	QVERIFY(writeBytes(sidecar, invalidBytes));
	UiStateStore store(settings);
	QString error;
	QCOMPARE(store.save(sampleState(), &error), UiStateStore::SaveResult::RefusedInvalidExisting);
	QString quarantinePath;
	QVERIFY(store.quarantineExisting(&quarantinePath));
	QCOMPARE(quarantinePath, sidecar + QStringLiteral(".invalid"));
	QCOMPARE(readBytes(quarantinePath), invalidBytes);
	QVERIFY(!QFileInfo::exists(sidecar));
	QCOMPARE(store.save(sampleState(), &error), UiStateStore::SaveResult::Saved);
	UiStateStore recovered(settings);
	QCOMPARE(recovered.load(), UiStateStore::LoadResult::Loaded);

	// Valid files are never quarantined.
	QCOMPARE(recovered.quarantineExisting(&error), false);
	QVERIFY(QFileInfo::exists(sidecar));

	// Neither are newer-version files.
	const QString settings2     = settingsPath(dir, QStringLiteral("second.json"));
	const QString sidecar2      = sidecarPathFor(settings2);
	const QByteArray newerBytes = makeSidecar({ { QStringLiteral("version"), 2 } });
	QVERIFY(writeBytes(sidecar2, newerBytes));
	UiStateStore newer(settings2);
	QCOMPARE(newer.quarantineExisting(&error), false);
	QCOMPARE(readBytes(sidecar2), newerBytes);

	// Repeated quarantine: collision-safe names, previous artifacts preserved.
	const QByteArray invalidBytes2 = QByteArrayLiteral("{ still not valid");
	QVERIFY(writeBytes(sidecar, invalidBytes2));
	QVERIFY(store.quarantineExisting(&quarantinePath));
	QCOMPARE(quarantinePath, sidecar + QStringLiteral(".invalid.1"));
	QCOMPARE(readBytes(sidecar + QStringLiteral(".invalid")), invalidBytes);
	QCOMPARE(readBytes(quarantinePath), invalidBytes2);

#ifdef Q_OS_UNIX
	// Failed rename: the original invalid file is left untouched.
	QTemporaryDir dir2;
	const QString settings3 = settingsPath(dir2, QStringLiteral("sub/mumble_settings.json"));
	const QString subDir    = QFileInfo(settings3).absolutePath();
	QVERIFY(QDir().mkpath(subDir));
	const QString sidecar3 = sidecarPathFor(settings3);
	QVERIFY(writeBytes(sidecar3, invalidBytes));
	QFile subDirFile(subDir);
	QVERIFY(subDirFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::ExeOwner));
	UiStateStore stuck(settings3);
	QCOMPARE(stuck.quarantineExisting(&error), false);
	QVERIFY(subDirFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
	QCOMPARE(readBytes(sidecar3), invalidBytes);
	QVERIFY(!QFileInfo::exists(sidecar3 + QStringLiteral(".invalid")));
#else
	QSKIP("Read-only directory enforcement is not available on this platform");
#endif
}

void TestUiStateStore::sharedSettingsUntouched() {
	QTemporaryDir dir;
	const QString settings    = settingsPath(dir);
	const QByteArray sentinel = readBytes(settings);

	const QString sidecar = sidecarPathFor(settings);
	UiStateStore store(settings);

	// Failure paths must never spill over into the shared settings file.
	QString error;
	QVERIFY(writeBytes(sidecar, QByteArrayLiteral("corrupt")));
	QCOMPARE(store.load(&error), UiStateStore::LoadResult::Invalid);
	QCOMPARE(store.save(sampleState(), &error), UiStateStore::SaveResult::RefusedInvalidExisting);
	QVERIFY(store.quarantineExisting(&error));
	QVERIFY(writeBytes(sidecar, makeSidecar({ { QStringLiteral("version"), 9 } })));
	QCOMPARE(store.load(&error), UiStateStore::LoadResult::Unsupported);
	QCOMPARE(store.save(sampleState(), &error), UiStateStore::SaveResult::RefusedNewerVersion);
	QVERIFY(writeBytes(sidecar, makeSidecar()));
	QCOMPARE(store.load(&error), UiStateStore::LoadResult::Loaded);
	QCOMPARE(store.beginGeometryRestore(sampleState()), true);
	QCOMPARE(store.finishGeometryRestore(sampleState()), UiStateStore::SaveResult::Saved);
	QCOMPARE(store.save(sampleState(), &error), UiStateStore::SaveResult::Saved);

	QCOMPARE(readBytes(settings), sentinel);
}

QTEST_MAIN(TestUiStateStore)
#include "TestUiStateStore.moc"

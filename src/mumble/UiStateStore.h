// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_UISTATESTORE_H_
#define MUMBLE_MUMBLE_UISTATESTORE_H_

#include <QByteArray>
#include <QList>
#include <QString>

/// Layout values persisted in the new-UI sidecar file (layoutRevision 1).
///
/// The layout fields are owned by the caller: the shell integration reads them
/// after UiStateStore::load() and fills them from live widgets before
/// UiStateStore::save(). The metadata fields describe the file the values were
/// loaded from; they are informational only and are ignored when writing -
/// UiStateStore always stamps the currently supported schema version, layout
/// revision and Qt version.
struct UiState {
	bool minimal         = false;
	bool hideFrame       = false;
	bool activityVisible = true;

	/// Opaque QWidget::saveGeometry() bytes. Empty means "not saved".
	QByteArray normalGeometry;
	QByteArray compactGeometry;
	/// Explicit splitter sizes in pixels. Empty means "not saved".
	QList< int > normalSplitterSizes;

	// --- Metadata as loaded from the sidecar; ignored on save ---
	int layoutRevision  = 1;
	QString qtVersion;
	bool restoreBlocked = false;
};

/// Standalone persistence helper for state that belongs exclusively to the
/// redesigned UI. State is kept in a per-profile sidecar file derived from the
/// resolved primary Mumble settings file ("mumble_settings.json" ->
/// "mumble_settings.json.new-ui.json"), so the original client's configuration
/// and database are never read or written by this class.
///
/// The store depends on QtCore only. It has no connection to Global, Settings,
/// the client database or any widget; the shell integration translates between
/// widget state and UiState values and performs the actual
/// QWidget::restoreGeometry() call between beginGeometryRestore() and
/// finishGeometryRestore()/abortGeometryRestore().
///
/// Concurrency: there is no cross-process locking. The sidecar is written with
/// QSaveFile (atomic same-directory replacement, no direct-write fallback) and
/// an existing file is checked for validity immediately before each write, but
/// the design assumes the single-instance/full-exit alternation documented in
/// the new-UI architecture (docs/new-ui/architecture.md), not simultaneous
/// writers.
///
/// Durability: the crash-recovery guard relies on QSaveFile::commit() flushing
/// the guard file before the restore attempt. On platforms or builds where
/// that flush is not guaranteed to survive power loss, the narrow failure mode
/// is one additional crash-then-blocked cycle, never a restore crash loop.
class UiStateStore {
public:
	/// JSON schema version of the sidecar format ("version" field).
	static constexpr int SchemaVersion = 1;
	/// Version of the layout field set the running shell understands
	/// ("layoutRevision" field).
	static constexpr int LayoutRevision = 1;

	/// Leading big-endian quint32 of QWidget::saveGeometry() output
	/// (0x1D9D0CB). Used to reject blobs that are not Qt geometry data before
	/// they are ever handed to a restore call. The exact Qt version gate
	/// limits the blast radius of any future Qt format change to a single
	/// safe-fallback transition.
	static constexpr quint32 GeometryMagic = 0x1D9D0CB;
	/// Plausible decoded size window for opaque geometry blobs, in bytes.
	static constexpr qsizetype MinGeometryBytes = 16;
	static constexpr qsizetype MaxGeometryBytes = 4096;
	/// Splitter size array bounds (layoutRevision 1). An empty list means
	/// "not saved"; otherwise the element count must be within this window.
	static constexpr qsizetype MinSplitterCount = 2;
	static constexpr qsizetype MaxSplitterCount = 8;
	/// Largest plausible splitter size in pixels.
	static constexpr int MaxSplitterSize = 65535;

	enum class LoadResult {
		/// Values loaded and usable as-is.
		Loaded,
		/// Values loaded, but recorded geometry was discarded because it was
		/// written by a different Qt version; use safe default placement.
		LoadedGeometryDropped,
		/// Values loaded, but a previous restoration attempt was interrupted:
		/// the restore latch is set and no geometry is present. The shell must
		/// not attempt restoration until the user explicitly resets the layout
		/// (save() with clearRestoreBlock).
		LoadedRestoreBlocked,
		/// No sidecar exists; loadedState() holds defaults.
		NoSidecar,
		/// The sidecar uses a newer schema version or layout revision. The
		/// file is preserved and must not be overwritten by this client.
		Unsupported,
		/// The sidecar exists but is malformed (bad JSON, wrong field types,
		/// failed validation). The file is preserved for diagnosis.
		Invalid,
		/// The sidecar exists but cannot be read (permissions) or is a
		/// directory.
		IoError
	};

	enum class SaveResult {
		Saved,
		/// The passed values failed validation; nothing was written.
		InvalidInput,
		/// Protocol misuse, e.g. finishGeometryRestore() without a preceding
		/// successful beginGeometryRestore().
		InvalidOperation,
		/// The existing sidecar is invalid and was preserved for diagnosis;
		/// quarantine it (quarantineExisting()) to resume normal writes.
		RefusedInvalidExisting,
		/// The existing sidecar uses a newer version/revision and was
		/// preserved; an older client never overwrites a newer file.
		RefusedNewerVersion,
		/// The write could not be completed (unwritable or missing directory,
		/// failed atomic commit, no sidecar path configured).
		WriteError
	};

	/// Creates a store for the given resolved primary settings write path.
	///
	/// The path must be the absolute primary write target of the shared
	/// settings file (Settings::settingsLocation, pinned by the caller after
	/// load/backup selection). A relative path is absolutized defensively; an
	/// empty path yields a store that cannot persist anything but stays
	/// usable in memory.
	explicit UiStateStore(QString primarySettingsPath);

	/// Sidecar path for a resolved primary settings path: the primary absolute
	/// write path (never resolved through a symlinked file, which the shared
	/// saver would replace via rename) with one trailing ".back" backup
	/// suffix stripped and ".new-ui.json" appended. Only the deepest existing
	/// ancestor directory is canonicalized.
	static QString sidecarPathFor(const QString &primarySettingsPath);

	/// Sidecar path of this store; empty if constructed with an empty path.
	QString sidecarPath() const;

	/// Loads and validates the sidecar. Replaces the snapshot returned by
	/// loadedState() with defaults first, then with the loaded values on
	/// success. Never creates or modifies any file.
	LoadResult load(QString *errorString = nullptr);

	/// Values of the last load(); defaults until a load succeeded. begin /
	/// finish / abort never mutate the snapshot.
	const UiState &loadedState() const;

	/// Persists layout values.
	///
	/// While the restore latch is set (a load found restoreBlocked=true, or a
	/// beginGeometryRestore() guard is active) the latch is sticky: the write
	/// keeps restoreBlocked=true and strips geometry, so unsafe bytes can
	/// never persist through an ordinary save. With clearRestoreBlock the
	/// latch is released and geometry is cleared in the same write - the
	/// explicit "reset fork layout" transition; this also cancels an active
	/// restoration. Only finishGeometryRestore() persists geometry together
	/// with a cleared latch.
	SaveResult save(const UiState &state, QString *errorString = nullptr, bool clearRestoreBlock = false);

	/// Begins a guarded geometry restoration.
	///
	/// Atomically writes the candidate values with both geometries cleared
	/// and restoreBlocked=true, and only then lets the caller attempt the
	/// actual QWidget::restoreGeometry() using its in-memory copy of the
	/// candidate (the caller must retain the loaded UiState; the inactive
	/// mode's geometry is needed again for finishGeometryRestore()).
	///
	/// Returns false - and restoration must be skipped in favor of safe
	/// defaults - when the session is already blocked, when the candidate is
	/// invalid, or when the guard write fails or is refused. Also refuses a
	/// second call until the restoration is finished or aborted.
	bool beginGeometryRestore(const UiState &candidate, QString *errorString = nullptr);

	/// Completes a guarded restoration after QWidget::restoreGeometry()
	/// succeeded. The only transition that persists geometry together with a
	/// cleared latch. Requires a successful beginGeometryRestore() in this
	/// process; otherwise fails with SaveResult::InvalidOperation.
	SaveResult finishGeometryRestore(const UiState &restored, QString *errorString = nullptr);

	/// Aborts a guarded restoration after QWidget::restoreGeometry() failed.
	/// Persists the passed values with both geometries cleared and the latch
	/// released: Qt rejected the bytes, so fall back to safe placement without
	/// keeping them.
	SaveResult abortGeometryRestore(const UiState &current, QString *errorString = nullptr);

	/// Renames an existing invalid sidecar out of the way so normal writes can
	/// resume, preserving the original bytes for diagnosis. The target is
	/// "<sidecar>.invalid", or "<sidecar>.invalid.1", ".2", ... when that name
	/// is already taken (existing quarantine files are never overwritten). If
	/// the rename fails, the original file is left untouched. Refuses to touch
	/// files that are valid or newer-version - they need no quarantine.
	bool quarantineExisting(QString *errorString = nullptr);

private:
	/// Validates values (both save input and existing-file content share these
	/// rules). Returns an error description, or an empty string when valid.
	static QString validateValues(const UiState &state);

	/// Shared write path: checks overwrite protection, stamps metadata, and
	/// writes the JSON document atomically. includeGeometry=false strips both
	/// geometries from the persisted values.
	SaveResult writeValidated(const UiState &values, bool writeRestoreBlocked, bool includeGeometry,
							  QString *errorString);

	QString m_sidecarPath;
	UiState m_loadedState;
	bool m_restoreBlocked    = false;
	bool m_restoreInProgress = false;
};

#endif // MUMBLE_MUMBLE_UISTATESTORE_H_

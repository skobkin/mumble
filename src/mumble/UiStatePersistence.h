// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_UISTATEPERSISTENCE_H_
#define MUMBLE_MUMBLE_UISTATEPERSISTENCE_H_

#include <QList>
#include <QRect>
#include <QString>
#include <QWidget>

#include "UiStateStore.h"

class Settings;

/// Fork-side persistence policy for the redesigned main window shell
/// (docs/new-ui/architecture.md 5.3/5.4): owns the UiStateStore sidecar and
/// the current UiState and implements the guarded restoration protocol,
/// capture rules and the explicit layout reset. MainWindow translates between
/// this policy and its widgets; the policy never touches widgets other than
/// the window reference it is given and never writes any serialized shared
/// Settings field (it only reads bMinimalView/bHideFrame for one-time seeding
/// and the read-only preventWindowStates restoration veto). Its single write
/// to the shared settings object is the unserialized settingsLocation runtime
/// pin installed by resolveAndPinPrimaryPath(), which keeps the sidecar and
/// any later shared save on one agreed write target. It deliberately has no
/// Global dependency so it can be exercised in isolated tests with a plain
/// QWidget stand-in.
class UiStatePersistence {
public:
	/// \param sharedSettings the global settings object; used to resolve and
	/// pin the primary settings write path and for the seeds/vetoes above.
	/// \param window the main window whose geometry is captured and restored.
	UiStatePersistence(Settings &sharedSettings, QWidget &window);

	/// Makes sure the sidecar is derived from the resolved primary settings
	/// write target. If the settings location has not been pinned yet (a
	/// profile that was never saved), it is resolved exactly the way the
	/// shared Settings::save() would resolve it and pinned, so the sidecar
	/// always lives next to the file shared saving actually writes.
	void resolveAndPinPrimaryPath();

	/// Loads the sidecar and applies the load-result matrix to the in-memory
	/// state. Returns the store's result so the caller can surface visible
	/// diagnostics for Unsupported/Invalid/IoError; the policy itself only
	/// logs.
	UiStateStore::LoadResult startupLoad();

	/// Runs the guarded restoration protocol for the given mode's geometry
	/// (minimal -> compact pair, otherwise normal pair). Splitter sizes are
	/// deliberately not applied here: the caller reads state() afterwards and
	/// applies them to the shell. Returns whether a stored geometry was
	/// successfully restored. Honors the veto matrix: the shared
	/// preventWindowStates latch (read-only) and --no-window-states each block
	/// restoration of opaque geometry.
	bool restoreGeometry(bool minimalPair);

	/// Captures the window geometry into the given mode's field and (in normal
	/// mode) the given splitter sizes. Skipped entirely under the
	/// --no-window-states veto and while the window is embedded in the
	/// interactive overlay proxy (such a capture would persist overlay-sized
	/// geometry as the desktop default); previously stored bytes are kept in
	/// those cases. Does not write the sidecar; call save() afterwards.
	void captureGeometry(bool minimalPair, const QList< int > &normalSplitterSizes);

	/// Persists the current state through the store. An invalid existing
	/// sidecar is quarantined (preserved for diagnosis) and the write retried
	/// once; newer-version sidecars are never overwritten.
	UiStateStore::SaveResult save(QString *errorString = nullptr);

	void setMinimal(bool minimal);
	void setHideFrame(bool hideFrame);
	/// Records which console tab was active at the last normal-mode save
	/// (layoutRevision 2). Deliberately NOT captured while minimal - the
	/// panel is hidden there, so the stored value keeps naming the tab the
	/// user last saw in normal mode.
	void setActiveTabIsActivity(bool activity);
	void setCliVeto(bool veto);

	/// Default window rectangle for the layout reset: the default size,
	/// clamped to the window's current screen's available geometry and the
	/// window minimums, centered on that screen.
	QRect defaultWindowRect() const;

	/// Explicit fork layout reset (new-UI architecture 5.4): resets the
	/// in-memory state to defaults and releases the restore latch in the same
	/// write, so an interrupted restoration can never block the next start.
	/// The caller is responsible for applying the defaults to the live
	/// widgets. An invalid existing sidecar is quarantined first.
	bool resetLayout(QString *errorString = nullptr);

	const UiState &state() const;

	/// Applies new window flags without the Qt side effects that break live
	/// toggles: a no-op for unchanged flags; on a real change the window's
	/// visibility, size and position are preserved (setWindowFlags() hides a
	/// visible window), and a window that was hidden (e.g. minimized to tray)
	/// or never shown stays hidden.
	static void applyWindowFlagsPreservingState(QWidget &window, Qt::WindowFlags flags);

private:
	bool restoreVetoed() const;

	UiStateStore m_store;
	UiState m_state;
	Settings &m_shared;
	QWidget &m_window;
	bool m_cliVeto        = false;
	bool m_restoreBlocked = false;
};

#endif // MUMBLE_MUMBLE_UISTATEPERSISTENCE_H_

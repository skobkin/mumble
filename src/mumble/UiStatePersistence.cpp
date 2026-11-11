// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "UiStatePersistence.h"

#include "Settings.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>

#include <QDebug>

UiStatePersistence::UiStatePersistence(Settings &sharedSettings, QWidget &window)
	: m_store(sharedSettings.settingsLocation), m_shared(sharedSettings), m_window(window) {
}

void UiStatePersistence::resolveAndPinPrimaryPath() {
	if (m_shared.settingsLocation.isEmpty()) {
		// A profile that has never been saved: resolve the target the shared
		// saver would use and pin it, so the sidecar and any later shared save
		// agree on one write location (new-UI architecture 5.4).
		m_shared.settingsLocation = m_shared.findSettingsLocation();
	}
	m_shared.settingsLocation = QFileInfo(m_shared.settingsLocation).absoluteFilePath();
	m_store                   = UiStateStore(m_shared.settingsLocation);
}

UiStateStore::LoadResult UiStatePersistence::startupLoad() {
	QString error;
	const UiStateStore::LoadResult result = m_store.load(&error);
	m_restoreBlocked                      = false;

	switch (result) {
		case UiStateStore::LoadResult::Loaded:
		case UiStateStore::LoadResult::LoadedGeometryDropped:
			m_state = m_store.loadedState();
			break;
		case UiStateStore::LoadResult::LoadedRestoreBlocked:
			m_state          = m_store.loadedState();
			m_restoreBlocked = true;
			qWarning("UiStatePersistence: restoration blocked by the sidecar's restore latch; "
					 "using safe defaults until the fork layout is reset");
			break;
		case UiStateStore::LoadResult::NoSidecar:
		case UiStateStore::LoadResult::Unsupported:
		case UiStateStore::LoadResult::Invalid:
		case UiStateStore::LoadResult::IoError:
			if (result != UiStateStore::LoadResult::NoSidecar) {
				qWarning("UiStatePersistence: unusable sidecar (%s): using defaults and leaving the "
						 "file untouched",
						 qPrintable(error));
			}
			// Seed the fork's one-time display defaults from the shared
			// values the official client last wrote (5.3); they are never
			// written back to shared settings by the fork.
			m_state           = UiState{};
			m_state.minimal   = m_shared.bMinimalView;
			m_state.hideFrame = m_shared.bHideFrame;
			break;
	}

	return result;
}

bool UiStatePersistence::restoreVetoed() const {
	return m_shared.preventWindowStates || m_cliVeto;
}

bool UiStatePersistence::restoreGeometry(bool minimalPair) {
	if (restoreVetoed()) {
		// The official client's read-only safety latch and --no-window-states
		// both veto restoring opaque window geometry. They are never written
		// by the fork.
		return false;
	}
	if (m_restoreBlocked) {
		// A previous restoration attempt was interrupted. Stay on safe
		// defaults until the user explicitly resets the fork layout.
		return false;
	}

	const QByteArray &geometry = minimalPair ? m_state.compactGeometry : m_state.normalGeometry;
	if (geometry.isEmpty()) {
		return false;
	}

	QString error;
	if (!m_store.beginGeometryRestore(m_state, &error)) {
		qWarning("UiStatePersistence: refusing guarded geometry restoration (%s); using safe placement",
				 qPrintable(error));
		return false;
	}

	if (m_window.restoreGeometry(geometry)) {
		if (m_store.finishGeometryRestore(m_state, &error) != UiStateStore::SaveResult::Saved) {
			qWarning("UiStatePersistence: completing the guarded restoration failed (%s); the restore "
					 "latch stays set until the next successful write",
					 qPrintable(error));
		}
		return true;
	}

	// Qt rejected the bytes: discard them and fall back to safe placement
	// without keeping the latch armed.
	UiState cleaned = m_state;
	cleaned.normalGeometry.clear();
	cleaned.compactGeometry.clear();
	if (m_store.abortGeometryRestore(cleaned, &error) != UiStateStore::SaveResult::Saved) {
		qWarning("UiStatePersistence: aborting the failed restoration failed (%s)", qPrintable(error));
	}
	m_state.normalGeometry.clear();
	m_state.compactGeometry.clear();
	return false;
}

void UiStatePersistence::captureGeometry(bool minimalPair, const QList< int > &normalSplitterSizes) {
	if (m_cliVeto) {
		// --no-window-states: no capture of opaque window geometry.
		return;
	}
	if (m_window.graphicsProxyWidget()) {
		// The window is embedded in the interactive overlay: its geometry
		// describes the overlay proxy, not the desktop shell. Never capture
		// it (new-UI architecture 5.5).
		return;
	}

	const QByteArray geometry = m_window.saveGeometry();
	if (minimalPair) {
		m_state.compactGeometry = geometry;
	} else {
		m_state.normalGeometry      = geometry;
		m_state.normalSplitterSizes = normalSplitterSizes;
	}
}

UiStateStore::SaveResult UiStatePersistence::save(QString *errorString) {
	QString error;
	UiStateStore::SaveResult result = m_store.save(m_state, &error);

	if (result == UiStateStore::SaveResult::RefusedInvalidExisting) {
		// The existing sidecar is invalid and was preserved for diagnosis.
		// Quarantine it so normal persistence can resume.
		QString quarantineError;
		if (m_store.quarantineExisting(&quarantineError)) {
			result = m_store.save(m_state, &error);
		} else {
			qWarning("UiStatePersistence: quarantining the invalid sidecar failed (%s)", qPrintable(quarantineError));
		}
	}

	if (result != UiStateStore::SaveResult::Saved) {
		qWarning("UiStatePersistence: saving the UI sidecar failed (%s)", qPrintable(error));
		if (errorString) {
			*errorString = error;
		}
	}

	return result;
}

void UiStatePersistence::setMinimal(bool minimal) {
	m_state.minimal = minimal;
}

void UiStatePersistence::setHideFrame(bool hideFrame) {
	m_state.hideFrame = hideFrame;
}

void UiStatePersistence::setActiveTabIsActivity(bool activity) {
	m_state.activeTabIsActivity = activity;
}

void UiStatePersistence::setCliVeto(bool veto) {
	m_cliVeto = veto;
}

QRect UiStatePersistence::defaultWindowRect() const {
	const QSize defaultSize(1000, 700);

	QScreen *screen = m_window.screen();
	if (!screen) {
		screen = QGuiApplication::primaryScreen();
	}
	const QRect available =
		screen ? screen->availableGeometry() : QRect(QPoint(0, 0), defaultSize.boundedTo(QSize(800, 600)));

	QSize size          = defaultSize.boundedTo(available.size());
	const QSize minimum = m_window.minimumSizeHint();
	if (minimum.isValid() && !minimum.isEmpty()) {
		size = size.expandedTo(minimum);
	}

	QRect rect(QPoint(0, 0), size);
	rect.moveCenter(available.center());
	return rect;
}

bool UiStatePersistence::resetLayout(QString *errorString) {
	m_state          = UiState{};
	m_restoreBlocked = false;

	QString error;
	UiStateStore::SaveResult result = m_store.save(m_state, &error, /*clearRestoreBlock=*/true);

	if (result == UiStateStore::SaveResult::RefusedInvalidExisting) {
		QString quarantineError;
		if (m_store.quarantineExisting(&quarantineError)) {
			result = m_store.save(m_state, &error, /*clearRestoreBlock=*/true);
		} else {
			qWarning("UiStatePersistence: quarantining the invalid sidecar during reset failed (%s)",
					 qPrintable(quarantineError));
		}
	}

	if (result != UiStateStore::SaveResult::Saved) {
		qWarning("UiStatePersistence: resetting the UI layout sidecar failed (%s)", qPrintable(error));
		if (errorString) {
			*errorString = error;
		}
		return false;
	}

	return true;
}

const UiState &UiStatePersistence::state() const {
	return m_state;
}

void UiStatePersistence::applyWindowFlagsPreservingState(QWidget &window, Qt::WindowFlags flags) {
	if (window.windowFlags() == flags) {
		return;
	}

	const bool wasVisible = window.isVisible();
	const QRect geometry  = window.geometry();

	window.setWindowFlags(flags);
	window.resize(geometry.size());
	window.move(geometry.topLeft());
	if (wasVisible) {
		// setWindowFlags() hid the window; restore its visibility. A window
		// that was already hidden (e.g. minimized to tray) or never shown
		// stays hidden.
		window.show();
	}
}

// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "UiStateStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QtEndian>

#include <cmath>
#include <limits>

namespace {

/// Upper bound for a sidecar we are willing to read into memory. Valid files
/// are far smaller (bounded geometry blobs plus a handful of values).
constexpr qsizetype MaxSidecarBytes = 64 * 1024;

enum class ExistingVerdict {
	Absent,
	Ok,
	NewerVersion,
	InvalidContent,
	IoError,
};

struct ParsedSidecar {
	int version        = 0;
	int layoutRevision = 0;
	QString qtVersion;
	bool restoreBlocked = false;
	UiState values;
};

void assignError(QString *errorString, const QString &message) {
	if (errorString) {
		*errorString = message;
	}
}

/// Extracts an integral int from a JSON number. JSON has no integer type, so
/// non-integral doubles and out-of-range values are rejected.
bool jsonInt(const QJsonValue &value, int *out) {
	if (!value.isDouble()) {
		return false;
	}
	const double number = value.toDouble();
	if (std::floor(number) != number) {
		return false;
	}
	if (number < static_cast< double >(std::numeric_limits< int >::min())
		|| number > static_cast< double >(std::numeric_limits< int >::max())) {
		return false;
	}
	*out = static_cast< int >(number);
	return true;
}

/// Structural validation of an in-memory geometry blob: the plausible size
/// window and the well-known QWidget::saveGeometry() magic. Returns an empty
/// string when the blob is acceptable (empty means "not saved").
QString geometryError(const QByteArray &geometry) {
	if (geometry.isEmpty()) {
		return QString();
	}
	if (geometry.size() < UiStateStore::MinGeometryBytes
		|| geometry.size() > UiStateStore::MaxGeometryBytes) {
		return QStringLiteral("decoded size %1 outside [%2, %3]")
			.arg(geometry.size())
			.arg(UiStateStore::MinGeometryBytes)
			.arg(UiStateStore::MaxGeometryBytes);
	}
	const quint32 magic = qFromBigEndian< quint32 >(geometry.constData());
	if (magic != UiStateStore::GeometryMagic) {
		return QStringLiteral("unexpected magic 0x%1").arg(magic, 0, 16);
	}
	return QString();
}

/// Reads and validates an existing sidecar file. Validation is ordered so the
/// version gates run before any body validation: a newer-version file may use
/// incompatible field semantics and must classify as NewerVersion, never as
/// InvalidContent. Unknown keys are ignored (additive forward compatibility
/// within the same schema version).
ExistingVerdict readExisting(const QString &path, ParsedSidecar *out, QString *errorString) {
	QFileInfo info(path);
	if (!info.exists()) {
		return ExistingVerdict::Absent;
	}
	if (info.isDir()) {
		assignError(errorString, QStringLiteral("'%1' is a directory").arg(path));
		return ExistingVerdict::IoError;
	}
	if (info.size() > MaxSidecarBytes) {
		assignError(errorString, QStringLiteral("'%1' is implausibly large").arg(path));
		return ExistingVerdict::InvalidContent;
	}

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		assignError(errorString, QStringLiteral("could not open '%1' for reading: %2")
									 .arg(path, file.errorString()));
		return ExistingVerdict::IoError;
	}
	const QByteArray raw = file.readAll();
	file.close();

	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(raw, &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		assignError(errorString,
					QStringLiteral("invalid JSON in '%1': %2").arg(path, parseError.errorString()));
		return ExistingVerdict::InvalidContent;
	}
	const QJsonObject root = document.object();

	// Version gates (before body validation).
	int version = 0;
	if (!jsonInt(root.value(QStringLiteral("version")), &version)) {
		assignError(errorString, QStringLiteral("missing/invalid 'version' in '%1'").arg(path));
		return ExistingVerdict::InvalidContent;
	}
	if (version > UiStateStore::SchemaVersion) {
		assignError(errorString,
					QStringLiteral("sidecar '%1' uses schema version %2 > %3")
						.arg(path)
						.arg(version)
						.arg(UiStateStore::SchemaVersion));
		return ExistingVerdict::NewerVersion;
	}
	// Both formats start at 1; anything below is corruption, not an older
	// file. There is deliberately no migration path from version 0.
	if (version < 1) {
		assignError(errorString, QStringLiteral("invalid 'version' %1 in '%2'").arg(version).arg(path));
		return ExistingVerdict::InvalidContent;
	}
	Q_ASSERT(version == UiStateStore::SchemaVersion);

	int layoutRevision = 0;
	if (!jsonInt(root.value(QStringLiteral("layoutRevision")), &layoutRevision)) {
		assignError(errorString,
					QStringLiteral("missing/invalid 'layoutRevision' in '%1'").arg(path));
		return ExistingVerdict::InvalidContent;
	}
	if (layoutRevision > UiStateStore::LayoutRevision) {
		assignError(errorString,
					QStringLiteral("sidecar '%1' uses layout revision %2 > %3")
						.arg(path)
						.arg(layoutRevision)
						.arg(UiStateStore::LayoutRevision));
		return ExistingVerdict::NewerVersion;
	}
	if (layoutRevision < 1) {
		assignError(errorString,
					QStringLiteral("invalid 'layoutRevision' %1 in '%2'").arg(layoutRevision).arg(path));
		return ExistingVerdict::InvalidContent;
	}
	Q_ASSERT(layoutRevision == UiStateStore::LayoutRevision);

	// Body validation.
	const QJsonValue qtVersionValue = root.value(QStringLiteral("qtVersion"));
	if (!qtVersionValue.isString() || qtVersionValue.toString().isEmpty()) {
		assignError(errorString, QStringLiteral("missing/invalid 'qtVersion' in '%1'").arg(path));
		return ExistingVerdict::InvalidContent;
	}
	out->version        = version;
	out->layoutRevision = layoutRevision;
	out->qtVersion      = qtVersionValue.toString();

	const auto readBool = [&root, &path, &errorString](const QString &key, bool *out) -> bool {
		const QJsonValue value = root.value(key);
		if (!value.isBool()) {
			assignError(errorString,
						QStringLiteral("missing/invalid '%1' in '%2'").arg(key, path));
			return false;
		}
		*out = value.toBool();
		return true;
	};
	if (!readBool(QStringLiteral("restoreBlocked"), &out->restoreBlocked)
		|| !readBool(QStringLiteral("minimal"), &out->values.minimal)
		|| !readBool(QStringLiteral("hideFrame"), &out->values.hideFrame)
		|| !readBool(QStringLiteral("activityVisible"), &out->values.activityVisible)) {
		return ExistingVerdict::InvalidContent;
	}

	const auto readGeometry = [&path, &errorString](const QJsonObject &mode, const QString &modeName,
													const QString &key,
													QByteArray *out) -> bool {
		if (!mode.contains(key)) {
			return true; // optional; absent means "not saved"
		}
		const QJsonValue value = mode.value(key);
		if (!value.isString()) {
			assignError(errorString,
						QStringLiteral("invalid '%1.%2' in '%3'").arg(modeName, key, path));
			return false;
		}
		const QString text = value.toString();
		if (text.isEmpty()) {
			return true; // canonical representation of "not saved"
		}
		const QByteArray::FromBase64Result decoded =
			QByteArray::fromBase64Encoding(text.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
		if (decoded.decodingStatus != QByteArray::Base64DecodingStatus::Ok) {
			assignError(errorString,
						QStringLiteral("'%1.%2' in '%3' is not valid base64").arg(modeName, key, path));
			return false;
		}
		const QString error = geometryError(*decoded);
		if (!error.isEmpty()) {
			assignError(errorString,
						QStringLiteral("'%1.%2' in '%3': %4").arg(modeName, key, path, error));
			return false;
		}
		*out = *decoded;
		return true;
	};

	// "normal" and "compact" are optional objects; an omitted object is
	// equivalent to an empty one ("add fields only when used").
	const QJsonValue normalValue = root.value(QStringLiteral("normal"));
	if (normalValue.isUndefined()) {
		// nothing to read
	} else if (!normalValue.isObject()) {
		assignError(errorString, QStringLiteral("invalid 'normal' object in '%1'").arg(path));
		return ExistingVerdict::InvalidContent;
	} else {
		const QJsonObject normal = normalValue.toObject();
		if (!readGeometry(normal, QStringLiteral("normal"), QStringLiteral("geometry"),
						  &out->values.normalGeometry)) {
			return ExistingVerdict::InvalidContent;
		}
		if (normal.contains(QStringLiteral("splitterSizes"))) {
			const QJsonValue sizesValue = normal.value(QStringLiteral("splitterSizes"));
			if (!sizesValue.isArray()) {
				assignError(errorString,
							QStringLiteral("invalid 'normal.splitterSizes' in '%1'").arg(path));
				return ExistingVerdict::InvalidContent;
			}
			const QJsonArray sizes = sizesValue.toArray();
			const qsizetype count = sizes.size();
			if (count != 0
				&& (count < UiStateStore::MinSplitterCount || count > UiStateStore::MaxSplitterCount)) {
				assignError(errorString,
							QStringLiteral("implausible splitter count %1 in '%2'").arg(count).arg(path));
				return ExistingVerdict::InvalidContent;
			}
			QList< int > parsedSizes;
			for (const QJsonValue &element : sizes) {
				int size = 0;
				if (!jsonInt(element, &size) || size < 1 || size > UiStateStore::MaxSplitterSize) {
					assignError(errorString,
								QStringLiteral("invalid splitter size in '%1'").arg(path));
					return ExistingVerdict::InvalidContent;
				}
				parsedSizes.append(size);
			}
			out->values.normalSplitterSizes = parsedSizes;
		}
	}

	const QJsonValue compactValue = root.value(QStringLiteral("compact"));
	if (compactValue.isUndefined()) {
		// nothing to read
	} else if (!compactValue.isObject()) {
		assignError(errorString, QStringLiteral("invalid 'compact' object in '%1'").arg(path));
		return ExistingVerdict::InvalidContent;
	} else if (!readGeometry(compactValue.toObject(), QStringLiteral("compact"), QStringLiteral("geometry"),
							&out->values.compactGeometry)) {
		return ExistingVerdict::InvalidContent;
	}

	return ExistingVerdict::Ok;
}

QJsonDocument buildDocument(const UiState &values, bool restoreBlocked, bool includeGeometry) {
	QJsonObject normal;
	if (includeGeometry && !values.normalGeometry.isEmpty()) {
		normal.insert(QStringLiteral("geometry"),
					  QString::fromLatin1(values.normalGeometry.toBase64()));
	}
	if (!values.normalSplitterSizes.isEmpty()) {
		QJsonArray sizes;
		for (int size : values.normalSplitterSizes) {
			sizes.append(size);
		}
		normal.insert(QStringLiteral("splitterSizes"), sizes);
	}

	QJsonObject compact;
	if (includeGeometry && !values.compactGeometry.isEmpty()) {
		compact.insert(QStringLiteral("geometry"),
					   QString::fromLatin1(values.compactGeometry.toBase64()));
	}

	QJsonObject root;
	root.insert(QStringLiteral("version"), UiStateStore::SchemaVersion);
	root.insert(QStringLiteral("layoutRevision"), UiStateStore::LayoutRevision);
	root.insert(QStringLiteral("qtVersion"), QString::fromLatin1(qVersion()));
	root.insert(QStringLiteral("restoreBlocked"), restoreBlocked);
	root.insert(QStringLiteral("minimal"), values.minimal);
	root.insert(QStringLiteral("hideFrame"), values.hideFrame);
	root.insert(QStringLiteral("activityVisible"), values.activityVisible);
	root.insert(QStringLiteral("normal"), normal);
	root.insert(QStringLiteral("compact"), compact);
	return QJsonDocument(root);
}

} // namespace

UiStateStore::UiStateStore(QString primarySettingsPath)
	: m_sidecarPath(sidecarPathFor(primarySettingsPath)) {
}

QString UiStateStore::sidecarPathFor(const QString &primarySettingsPath) {
	if (primarySettingsPath.isEmpty()) {
		return QString();
	}

	// Defensively absolutize; the contract is that the caller passes the
	// resolved absolute primary write path.
	QString path = QFileInfo(primarySettingsPath).absoluteFilePath();

	// Mirror the shared settings loader, which strips a trailing ".back"
	// backup suffix to obtain the primary target (Settings.cpp), so that a
	// caller passing the backup path still derives the primary sidecar.
	if (path.endsWith(QStringLiteral(".back"))) {
		path.chop(QStringLiteral(".back").size());
	}

	// Canonicalize only the deepest existing ancestor directory, never the
	// final file component: the shared settings saver replaces its target via
	// rename(), which would break through a symlinked settings file and
	// silently move the effective sidecar location between runs. Directories
	// survive renames of the files they contain.
	const QString fileName = QFileInfo(path).fileName();
	QString dir            = QFileInfo(path).absolutePath();
	for (;;) {
		const QFileInfo dirInfo(dir);
		if (dirInfo.isDir()) {
			const QString canonicalDir = dirInfo.canonicalFilePath();
			if (!canonicalDir.isEmpty()) {
				dir = canonicalDir;
				break;
			}
		}
		const QString parent = QFileInfo(dir).path();
		if (parent == dir) {
			break; // reached the filesystem root without a resolvable directory
		}
		dir = parent;
	}
	return QDir(dir).filePath(fileName + QStringLiteral(".new-ui.json"));
}

QString UiStateStore::sidecarPath() const {
	return m_sidecarPath;
}

UiStateStore::LoadResult UiStateStore::load(QString *errorString) {
	m_loadedState    = UiState();
	m_restoreBlocked = false;

	ParsedSidecar parsed;
	QString readError;
	const ExistingVerdict verdict = readExisting(m_sidecarPath, &parsed, &readError);
	switch (verdict) {
		case ExistingVerdict::Absent:
			return LoadResult::NoSidecar;
		case ExistingVerdict::NewerVersion:
			assignError(errorString, readError);
			qWarning("UiStateStore: %s", qUtf8Printable(readError));
			return LoadResult::Unsupported;
		case ExistingVerdict::InvalidContent:
			assignError(errorString, readError);
			qWarning("UiStateStore: %s", qUtf8Printable(readError));
			return LoadResult::Invalid;
		case ExistingVerdict::IoError:
			assignError(errorString, readError);
			qWarning("UiStateStore: %s", qUtf8Printable(readError));
			return LoadResult::IoError;
		case ExistingVerdict::Ok:
			break;
	}

	UiState values = parsed.values;
	values.layoutRevision = parsed.layoutRevision;
	values.qtVersion      = parsed.qtVersion;

	// A set restore latch means a previous restoration attempt was
	// interrupted. It takes precedence: geometry must not be restored until
	// the user explicitly resets the layout.
	if (parsed.restoreBlocked) {
		values.restoreBlocked   = true;
		values.normalGeometry.clear();
		values.compactGeometry.clear();
		m_loadedState    = values;
		m_restoreBlocked = true;
		return LoadResult::LoadedRestoreBlocked;
	}

	if (!values.normalGeometry.isEmpty() || !values.compactGeometry.isEmpty()) {
		if (parsed.qtVersion != QString::fromLatin1(qVersion())) {
			// Geometry is only retained for the exact Qt version that wrote
			// it; otherwise fall back to safe placement.
			values.normalGeometry.clear();
			values.compactGeometry.clear();
			m_loadedState = values;
			return LoadResult::LoadedGeometryDropped;
		}
	}

	values.restoreBlocked = false;
	m_loadedState         = values;
	return LoadResult::Loaded;
}

const UiState &UiStateStore::loadedState() const {
	return m_loadedState;
}

QString UiStateStore::validateValues(const UiState &state) {
	const QString normalError = geometryError(state.normalGeometry);
	if (!normalError.isEmpty()) {
		return QStringLiteral("normal geometry: %1").arg(normalError);
	}
	const QString compactError = geometryError(state.compactGeometry);
	if (!compactError.isEmpty()) {
		return QStringLiteral("compact geometry: %1").arg(compactError);
	}
	const qsizetype count = state.normalSplitterSizes.size();
	if (count != 0 && (count < MinSplitterCount || count > MaxSplitterCount)) {
		return QStringLiteral("implausible splitter count %1").arg(count);
	}
	for (int size : state.normalSplitterSizes) {
		if (size < 1 || size > MaxSplitterSize) {
			return QStringLiteral("invalid splitter size %1").arg(size);
		}
	}
	return QString();
}

UiStateStore::SaveResult UiStateStore::save(const UiState &state, QString *errorString,
											bool clearRestoreBlock) {
	if (clearRestoreBlock) {
		// Explicit "reset fork layout": release the latch and clear any
		// potentially unsafe geometry in the same write. A reset also cancels
		// an active restoration.
		m_restoreInProgress = false;
		const SaveResult result = writeValidated(state, false, false, errorString);
		if (result == SaveResult::Saved) {
			m_restoreBlocked = false;
		}
		return result;
	}
	if (m_restoreBlocked) {
		// Sticky latch: geometry can never persist through an ordinary save
		// while restoration is blocked.
		return writeValidated(state, true, false, errorString);
	}
	return writeValidated(state, false, true, errorString);
}

bool UiStateStore::beginGeometryRestore(const UiState &candidate, QString *errorString) {
	if (m_restoreBlocked || m_restoreInProgress) {
		const QString message = QStringLiteral(
			"restoration is blocked; an explicit layout reset (save with clearRestoreBlock) is "
			"required before restoring geometry again");
		assignError(errorString, message);
		qWarning("UiStateStore: %s", qUtf8Printable(message));
		return false;
	}
	if (writeValidated(candidate, true, false, errorString) != SaveResult::Saved) {
		return false;
	}
	m_restoreInProgress = true;
	m_restoreBlocked    = true;
	return true;
}

UiStateStore::SaveResult UiStateStore::finishGeometryRestore(const UiState &restored,
															 QString *errorString) {
	if (!m_restoreInProgress) {
		const QString message =
			QStringLiteral("finishGeometryRestore without a successful beginGeometryRestore");
		assignError(errorString, message);
		qWarning("UiStateStore: %s", qUtf8Printable(message));
		return SaveResult::InvalidOperation;
	}
	const SaveResult result = writeValidated(restored, false, true, errorString);
	if (result == SaveResult::Saved) {
		m_restoreInProgress = false;
		m_restoreBlocked    = false;
	}
	return result;
}

UiStateStore::SaveResult UiStateStore::abortGeometryRestore(const UiState &current,
															QString *errorString) {
	const SaveResult result = writeValidated(current, false, false, errorString);
	if (result == SaveResult::Saved) {
		m_restoreInProgress = false;
		m_restoreBlocked    = false;
	}
	return result;
}

bool UiStateStore::quarantineExisting(QString *errorString) {
	if (m_sidecarPath.isEmpty()) {
		assignError(errorString, QStringLiteral("no sidecar path configured"));
		return false;
	}
	const QFileInfo info(m_sidecarPath);
	if (!info.exists()) {
		assignError(errorString, QStringLiteral("no sidecar to quarantine"));
		return false;
	}
	if (info.isDir()) {
		assignError(errorString, QStringLiteral("'%1' is a directory").arg(m_sidecarPath));
		return false;
	}

	ParsedSidecar parsed;
	QString readError;
	switch (readExisting(m_sidecarPath, &parsed, &readError)) {
		case ExistingVerdict::InvalidContent:
			break; // the only case quarantine is for
		case ExistingVerdict::Ok:
			assignError(errorString, QStringLiteral("existing sidecar is valid; nothing to quarantine"));
			return false;
		case ExistingVerdict::NewerVersion:
			assignError(errorString,
						QStringLiteral("refusing to quarantine a newer-version sidecar"));
			return false;
		case ExistingVerdict::IoError:
			assignError(errorString, QStringLiteral("sidecar is unreadable: %1").arg(readError));
			return false;
		case ExistingVerdict::Absent:
			assignError(errorString, QStringLiteral("no sidecar to quarantine"));
			return false;
	}

	// Collision-safe target: never overwrite a previous quarantine artifact.
	QString target = m_sidecarPath + QStringLiteral(".invalid");
	int suffix     = 0;
	while (QFileInfo::exists(target)) {
		++suffix;
		if (suffix > 999) {
			const QString message = QStringLiteral("too many existing quarantine files");
			assignError(errorString, message);
			qWarning("UiStateStore: %s", qUtf8Printable(message));
			return false;
		}
		target = m_sidecarPath + QStringLiteral(".invalid.%1").arg(suffix);
	}
	if (!QFile::rename(m_sidecarPath, target)) {
		// The original invalid file is left untouched; recovery can be
		// retried after the user intervenes.
		const QString message = QStringLiteral("could not rename sidecar for quarantine");
		assignError(errorString, message);
		qWarning("UiStateStore: %s", qUtf8Printable(message));
		return false;
	}
	assignError(errorString, target);
	qInfo("UiStateStore: quarantined invalid sidecar to '%s'", qUtf8Printable(target));
	return true;
}

UiStateStore::SaveResult UiStateStore::writeValidated(const UiState &values, bool writeRestoreBlocked,
													  bool includeGeometry, QString *errorString) {
	if (m_sidecarPath.isEmpty()) {
		const QString message = QStringLiteral("no sidecar path configured; cannot persist UI state");
		assignError(errorString, message);
		qWarning("UiStateStore: %s", qUtf8Printable(message));
		return SaveResult::WriteError;
	}
	const QFileInfo pathInfo(m_sidecarPath);
	if (pathInfo.isDir()) {
		const QString message = QStringLiteral("sidecar path '%1' is a directory").arg(m_sidecarPath);
		assignError(errorString, message);
		qWarning("UiStateStore: %s", qUtf8Printable(message));
		return SaveResult::WriteError;
	}

	const QString valuesError = validateValues(values);
	if (!valuesError.isEmpty()) {
		const QString message = QStringLiteral("refusing to save invalid values: %1").arg(valuesError);
		assignError(errorString, message);
		qWarning("UiStateStore: %s", qUtf8Printable(message));
		return SaveResult::InvalidInput;
	}

	// Overwrite protection: only a file this client can validate as a
	// current-version sidecar (or no file at all) may be replaced. Checked
	// immediately before the write; there is no cross-process locking.
	ParsedSidecar existing;
	QString existingError;
	switch (readExisting(m_sidecarPath, &existing, &existingError)) {
		case ExistingVerdict::Absent:
		case ExistingVerdict::Ok:
			break;
		case ExistingVerdict::NewerVersion:
			assignError(errorString, existingError);
			qWarning("UiStateStore: %s", qUtf8Printable(existingError));
			return SaveResult::RefusedNewerVersion;
		case ExistingVerdict::InvalidContent:
			assignError(errorString,
						QStringLiteral("existing sidecar is invalid and preserved for diagnosis: %1")
							.arg(existingError));
			qWarning("UiStateStore: refusing to overwrite invalid sidecar '%s'",
					 qUtf8Printable(m_sidecarPath));
			return SaveResult::RefusedInvalidExisting;
		case ExistingVerdict::IoError:
			assignError(errorString,
						QStringLiteral("existing sidecar is unreadable, refusing to overwrite: %1")
							.arg(existingError));
			qWarning("UiStateStore: %s", qUtf8Printable(existingError));
			return SaveResult::RefusedInvalidExisting;
	}

	QSaveFile file(m_sidecarPath);
	if (!file.open(QIODevice::WriteOnly)) {
		const QString message = QStringLiteral("could not open '%1' for writing: %2")
									.arg(m_sidecarPath, file.errorString());
		assignError(errorString, message);
		qWarning("UiStateStore: %s", qUtf8Printable(message));
		return SaveResult::WriteError;
	}
	QByteArray payload = buildDocument(values, writeRestoreBlocked, includeGeometry)
							 .toJson(QJsonDocument::Indented);
	payload.append('\n');
	if (file.write(payload) != payload.size()) {
		const QString message = QStringLiteral("short write to '%1': %2")
									.arg(m_sidecarPath, file.errorString());
		assignError(errorString, message);
		qWarning("UiStateStore: %s", qUtf8Printable(message));
		file.cancelWriting();
		return SaveResult::WriteError;
	}
	if (!file.commit()) {
		const QString message = QStringLiteral("atomic commit of '%1' failed: %2")
									.arg(m_sidecarPath, file.errorString());
		assignError(errorString, message);
		qWarning("UiStateStore: %s", qUtf8Printable(message));
		return SaveResult::WriteError;
	}
	return SaveResult::Saved;
}

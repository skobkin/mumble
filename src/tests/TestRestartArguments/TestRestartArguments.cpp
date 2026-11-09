// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

// Isolated tests for the restart-argument reconstruction of the new-UI
// architecture's Stage 1 foundation fix (docs/new-ui/architecture.md 5.5): an
// application-initiated restart must preserve the resolved explicit --config
// profile and the persistent CLI options, with option names and values as
// separate argument elements.
//
// One-shot invocations (--help, --version, license printouts, the rpc
// subcommand, the positional URL/plugin arguments), --hidden and --locale are
// deliberately excluded: the RestartOptions snapshot has no fields for them,
// so their exclusion is enforced by construction. --locale is not replayed
// because it permanently overwrites the settings language and would override
// a language change that just triggered the restart.
//
// A process-level restart fixture (launch, trigger the restart, relaunch and
// assert profile continuity) requires a modal settings-dialog interaction and
// a disposable OS profile; it is deferred to the profile/restart process tests
// of the architecture's later test matrix (docs/new-ui/architecture.md 7.2).

#include <QtTest>

#include "RestartArguments.h"

namespace {

/// All expected strings below describe raw characters. In the Windows quoting
/// tests, "\\" in a C++ literal is one backslash and "\\\"" is an escaped
/// double quote; the rules being pinned are: an argument is quoted iff it is
/// empty or contains a space, tab or double quote; k backslashes directly
/// before an embedded double quote are emitted as 2k+1 backslashes followed by
/// the escaped quote; a trailing backslash run inside a quoted argument is
/// doubled before the closing quote; backslashes anywhere else are literal.

} // namespace

class TestRestartArguments : public QObject {
	Q_OBJECT
private slots:
	// --- buildRestartArguments ---
	void emptySnapshotYieldsNoArguments();
	void configEmittedAsSeparateNameAndValue();
	void configPathEmittedVerbatim();
	void booleanFlagsInFixedOrder();
	void valuedOptionsInFixedOrder();
	void fullSnapshotGoldenOrder();
	void emptyReconnectUrlOmitted();
	void urlOnlySnapshot();
	void unicodeValuesPreserved();
	void shellSpecialCharactersStaySingleArguments();

	// --- quoteWindowsArgument / toWindowsArgumentString ---
	void simpleArgumentNotQuoted();
	void spaceForcesQuoting();
	void tabForcesQuoting();
	void emptyArgumentQuoted();
	void embeddedQuoteEscaped();
	void singleBackslashBeforeQuote();
	void multipleBackslashesBeforeQuote();
	void trailingBackslashesDoubledInsideQuotes();
	void trailingBackslashWithoutWhitespaceStaysUnquoted();
	void interiorBackslashesUnchangedInsideQuotes();
	void pathWithoutWhitespaceUnquoted();
	void unicodeArgumentQuotedVerbatim();
	void joinedStringSeparatesArguments();
	void joinedStringQuotesEmptyArgument();
	void joinedEmptyListIsEmpty();
};

void TestRestartArguments::emptySnapshotYieldsNoArguments() {
	const RestartOptions options;

	QCOMPARE(buildRestartArguments(options), QStringList());
}

void TestRestartArguments::configEmittedAsSeparateNameAndValue() {
	// A path with spaces must stay a single element, separate from the name.
	RestartOptions options;
	options.configPath = QStringLiteral("/home/user/my settings/mumble_settings.json");

	const QStringList arguments = buildRestartArguments(options);

	QCOMPARE(arguments.size(), 2);
	QCOMPARE(arguments.at(0), QStringLiteral("--config"));
	QCOMPARE(arguments.at(1), QStringLiteral("/home/user/my settings/mumble_settings.json"));
}

void TestRestartArguments::configPathEmittedVerbatim() {
	// The builder does no path munging: a path that still carries the ".back"
	// suffix (already trimmed by the caller when filling the snapshot) is
	// passed through unchanged, as are relative paths.
	RestartOptions options;
	options.configPath = QStringLiteral("profiles/x.json.back");

	QCOMPARE(buildRestartArguments(options).at(1), QStringLiteral("profiles/x.json.back"));
}

void TestRestartArguments::booleanFlagsInFixedOrder() {
	RestartOptions options;
	options.allowMultiple            = true;
	options.suppressIdentity         = true;
	options.noWindowStates           = true;
	options.dumpInputStreams         = true;
	options.printEchoCancelQueue     = true;
	options.skipSettingsBackupPrompt = true;

	const QStringList arguments = buildRestartArguments(options);

	QCOMPARE(arguments, QStringList() << QStringLiteral("--multiple") << QStringLiteral("--no-identity")
									  << QStringLiteral("--no-window-states") << QStringLiteral("--dump-input-streams")
									  << QStringLiteral("--print-echocancel-queue")
									  << QStringLiteral("--skip-settings-backup-prompt"));
}

void TestRestartArguments::valuedOptionsInFixedOrder() {
	RestartOptions options;
	options.jackClientName        = QStringLiteral("client");
	options.windowTitleExt        = QStringLiteral("(dev)");
	options.defaultCertificateDir = QStringLiteral("/opt/certs");
	options.translationDir        = QStringLiteral("/opt/translations");

	const QStringList arguments = buildRestartArguments(options);

	QCOMPARE(arguments, QStringList() << QStringLiteral("--jack-name") << QStringLiteral("client")
									  << QStringLiteral("--window-title-ext") << QStringLiteral("(dev)")
									  << QStringLiteral("--default-certificate-dir") << QStringLiteral("/opt/certs")
									  << QStringLiteral("--translation-dir") << QStringLiteral("/opt/translations"));
}

void TestRestartArguments::fullSnapshotGoldenOrder() {
	RestartOptions options;
	options.allowMultiple            = true;
	options.suppressIdentity         = true;
	options.noWindowStates           = true;
	options.dumpInputStreams         = true;
	options.printEchoCancelQueue     = true;
	options.skipSettingsBackupPrompt = true;
	options.configPath               = QStringLiteral("/tmp/my dir/ünïcode.json");
	options.jackClientName           = QStringLiteral("Мумбле Ω");
	options.windowTitleExt           = QStringLiteral("(dev)");
	options.defaultCertificateDir    = QStringLiteral("/opt/certs dir");
	options.translationDir           = QStringLiteral("/opt/trans dir");
	options.reconnectUrl             = QStringLiteral("mumble://user@example.com");

	QCOMPARE(buildRestartArguments(options),
			 QStringList() << QStringLiteral("--config") << QStringLiteral("/tmp/my dir/ünïcode.json")
						   << QStringLiteral("--multiple") << QStringLiteral("--no-identity")
						   << QStringLiteral("--no-window-states") << QStringLiteral("--dump-input-streams")
						   << QStringLiteral("--print-echocancel-queue")
						   << QStringLiteral("--skip-settings-backup-prompt") << QStringLiteral("--jack-name")
						   << QStringLiteral("Мумбле Ω") << QStringLiteral("--window-title-ext")
						   << QStringLiteral("(dev)") << QStringLiteral("--default-certificate-dir")
						   << QStringLiteral("/opt/certs dir") << QStringLiteral("--translation-dir")
						   << QStringLiteral("/opt/trans dir") << QStringLiteral("mumble://user@example.com"));
}

void TestRestartArguments::emptyReconnectUrlOmitted() {
	RestartOptions options;
	options.allowMultiple = true;
	options.reconnectUrl  = QString();

	QCOMPARE(buildRestartArguments(options), QStringList() << QStringLiteral("--multiple"));
}

void TestRestartArguments::urlOnlySnapshot() {
	RestartOptions options;
	options.reconnectUrl = QStringLiteral("mumble://user%40example.com:64738/my%20channel");

	QCOMPARE(buildRestartArguments(options), QStringList()
												 << QStringLiteral("mumble://user%40example.com:64738/my%20channel"));
}

void TestRestartArguments::unicodeValuesPreserved() {
	RestartOptions options;
	options.configPath     = QStringLiteral("/tmp/übungs prófil/mumble_settings.json");
	options.jackClientName = QStringLiteral("Мумбле Ω");

	const QStringList arguments = buildRestartArguments(options);

	QCOMPARE(arguments.at(1), QStringLiteral("/tmp/übungs prófil/mumble_settings.json"));
	QCOMPARE(arguments.at(3), QStringLiteral("Мумбле Ω"));
}

void TestRestartArguments::shellSpecialCharactersStaySingleArguments() {
	const QString nasty = QStringLiteral("$(x) ; | & < > ` '~!#");
	RestartOptions options;
	options.jackClientName = nasty;
	options.reconnectUrl   = QStringLiteral("mumble://a b:c@d/?x=1&y=2");

	const QStringList arguments = buildRestartArguments(options);

	QCOMPARE(arguments.size(), 3);
	QCOMPARE(arguments.at(1), nasty);
	QCOMPARE(arguments.at(2), QStringLiteral("mumble://a b:c@d/?x=1&y=2"));
}

void TestRestartArguments::simpleArgumentNotQuoted() {
	QCOMPARE(quoteWindowsArgument(QStringLiteral("--config")), QStringLiteral("--config"));
}

void TestRestartArguments::spaceForcesQuoting() {
	QCOMPARE(quoteWindowsArgument(QStringLiteral("C:\\My Settings\\s.json")),
			 QStringLiteral("\"C:\\My Settings\\s.json\""));
}

void TestRestartArguments::tabForcesQuoting() {
	QCOMPARE(quoteWindowsArgument(QStringLiteral("tab\there")), QStringLiteral("\"tab\there\""));
}

void TestRestartArguments::emptyArgumentQuoted() {
	QCOMPARE(quoteWindowsArgument(QString()), QStringLiteral("\"\""));
}

void TestRestartArguments::embeddedQuoteEscaped() {
	// a"b -> "a\"b"
	QCOMPARE(quoteWindowsArgument(QStringLiteral("a\"b")), QStringLiteral("\"a\\\"b\""));
}

void TestRestartArguments::singleBackslashBeforeQuote() {
	// a\"b (a backslash quote b) -> "a\\\"b" (quote a \\\ quote b quote)
	QCOMPARE(quoteWindowsArgument(QStringLiteral("a\\\"b")), QStringLiteral("\"a\\\\\\\"b\""));
}

void TestRestartArguments::multipleBackslashesBeforeQuote() {
	// a\\"b (a backslash backslash quote b) -> five backslashes before the
	// escaped quote, which CommandLineToArgvW decodes back to two backslashes
	// and a literal quote.
	QCOMPARE(quoteWindowsArgument(QStringLiteral("a\\\\\"b")), QStringLiteral("\"a\\\\\\\\\\\"b\""));
}

void TestRestartArguments::trailingBackslashesDoubledInsideQuotes() {
	// C:\My Dir\ -> "C:\My Dir\\"
	QCOMPARE(quoteWindowsArgument(QStringLiteral("C:\\My Dir\\")), QStringLiteral("\"C:\\My Dir\\\\\""));
}

void TestRestartArguments::trailingBackslashWithoutWhitespaceStaysUnquoted() {
	// No whitespace, no quotes: backslashes are only special immediately
	// before a double quote, so the argument round-trips unquoted.
	QCOMPARE(quoteWindowsArgument(QStringLiteral("C:\\dir\\")), QStringLiteral("C:\\dir\\"));
}

void TestRestartArguments::interiorBackslashesUnchangedInsideQuotes() {
	QCOMPARE(quoteWindowsArgument(QStringLiteral("C:\\My Dir\\f.json")), QStringLiteral("\"C:\\My Dir\\f.json\""));
}

void TestRestartArguments::pathWithoutWhitespaceUnquoted() {
	QCOMPARE(quoteWindowsArgument(QStringLiteral("C:\\dir\\f.json")), QStringLiteral("C:\\dir\\f.json"));
}

void TestRestartArguments::unicodeArgumentQuotedVerbatim() {
	QCOMPARE(quoteWindowsArgument(QStringLiteral("Настройки β.json")), QStringLiteral("\"Настройки β.json\""));
}

void TestRestartArguments::joinedStringSeparatesArguments() {
	const QStringList arguments = { QStringLiteral("--config"),
									QStringLiteral("C:\\My Settings\\mumble_settings.json") };

	QCOMPARE(toWindowsArgumentString(arguments), QStringLiteral("--config \"C:\\My Settings\\mumble_settings.json\""));
}

void TestRestartArguments::joinedStringQuotesEmptyArgument() {
	const QStringList arguments = { QStringLiteral("mumble"), QString(), QStringLiteral("a b") };

	QCOMPARE(toWindowsArgumentString(arguments), QStringLiteral("mumble \"\" \"a b\""));
}

void TestRestartArguments::joinedEmptyListIsEmpty() {
	QCOMPARE(toWindowsArgumentString(QStringList()), QString());
}

QTEST_MAIN(TestRestartArguments)
#include "TestRestartArguments.moc"

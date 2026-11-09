// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_RESTARTARGUMENTS_H_
#define MUMBLE_MUMBLE_RESTARTARGUMENTS_H_

#include <QString>
#include <QStringList>

#include <optional>

/// Snapshot of the persistent command-line options that must survive an
/// application-initiated restart (docs/new-ui/architecture.md 5.5).
///
/// The snapshot is filled in main() before Global is torn down, so the restart
/// block that runs after the teardown never touches Global.
///
/// One-shot invocations (--help, --version, --license, --authors,
/// --third-party-licenses, --print-translation-dirs, the rpc subcommand and
/// the positional hyperlink/plugin arguments) deliberately have no fields:
/// their exclusion from the restart command line is enforced by construction.
/// --locale is excluded because it permanently overwrites the settings
/// language at startup; replaying it would override a language change that
/// just triggered the restart. --hidden is a one-shot presentation mode and is
/// not replayed either (matching the previous restart behavior).
struct RestartOptions {
	bool allowMultiple            = false; // -m/--multiple
	bool suppressIdentity         = false; // --no-identity
	bool noWindowStates           = false; // --no-window-states
	bool dumpInputStreams         = false; // --dump-input-streams
	bool printEchoCancelQueue     = false; // --print-echocancel-queue
	bool skipSettingsBackupPrompt = false; // --skip-settings-backup-prompt

	/// Absolute primary settings write path (Settings::settingsLocation after
	/// load(), i.e. with a trailing ".back" backup suffix already trimmed), set
	/// only when an explicit --config was actually used. Emitted verbatim.
	std::optional< QString > configPath;
	std::optional< QString > jackClientName;        // --jack-name
	std::optional< QString > windowTitleExt;        // --window-title-ext
	std::optional< QString > defaultCertificateDir; // --default-certificate-dir
	std::optional< QString > translationDir;        // --translation-dir

	/// URL of the live connection to reconnect to; empty means none.
	QString reconnectUrl;
};

/// Builds the argument list for the restart invocation in a fixed, documented
/// order: --config first, then the boolean flags, then the valued options,
/// then the reconnect URL last. The option name and its value are always
/// separate elements; values are emitted verbatim (no quoting, no path
/// normalization - the caller resolves paths before filling the struct).
/// Pure function without side effects.
QStringList buildRestartArguments(const RestartOptions &options);

/// Quotes a single argument for the Windows MSVCRT / CommandLineToArgvW
/// parsing rules: an argument that is empty or contains a space, tab or double
/// quote is wrapped in double quotes; backslashes are passed through unchanged
/// except that a run of backslashes directly before an embedded double quote
/// (or before the closing quote) is doubled, and embedded double quotes are
/// escaped with a backslash. An argument that needs no quoting is returned
/// verbatim, trailing backslashes included, because backslashes are only
/// special immediately before a double quote. Compiled and tested on all
/// platforms.
QString quoteWindowsArgument(const QString &argument);

/// Joins the given arguments into the single command-line string expected by
/// ShellExecuteEx's lpParameters: each argument is quoted per
/// quoteWindowsArgument() and the quoted arguments are separated by single
/// spaces.
QString toWindowsArgumentString(const QStringList &arguments);

#endif // MUMBLE_MUMBLE_RESTARTARGUMENTS_H_

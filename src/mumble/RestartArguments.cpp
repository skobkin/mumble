// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "RestartArguments.h"

QStringList buildRestartArguments(const RestartOptions &options) {
	QStringList arguments;

	if (options.configPath) {
		arguments << QLatin1String("--config") << *options.configPath;
	}

	if (options.allowMultiple) {
		arguments << QLatin1String("--multiple");
	}
	if (options.suppressIdentity) {
		arguments << QLatin1String("--no-identity");
	}
	if (options.noWindowStates) {
		arguments << QLatin1String("--no-window-states");
	}
	if (options.dumpInputStreams) {
		arguments << QLatin1String("--dump-input-streams");
	}
	if (options.printEchoCancelQueue) {
		arguments << QLatin1String("--print-echocancel-queue");
	}
	if (options.skipSettingsBackupPrompt) {
		arguments << QLatin1String("--skip-settings-backup-prompt");
	}

	if (options.jackClientName) {
		arguments << QLatin1String("--jack-name") << *options.jackClientName;
	}
	if (options.windowTitleExt) {
		arguments << QLatin1String("--window-title-ext") << *options.windowTitleExt;
	}
	if (options.defaultCertificateDir) {
		arguments << QLatin1String("--default-certificate-dir") << *options.defaultCertificateDir;
	}
	if (options.translationDir) {
		arguments << QLatin1String("--translation-dir") << *options.translationDir;
	}

	if (!options.reconnectUrl.isEmpty()) {
		arguments << options.reconnectUrl;
	}

	return arguments;
}

QString quoteWindowsArgument(const QString &argument) {
	const bool needsQuoting = argument.isEmpty() || argument.contains(QLatin1Char(' '))
							  || argument.contains(QLatin1Char('\t')) || argument.contains(QLatin1Char('"'));
	if (!needsQuoting) {
		return argument;
	}

	QString result               = QStringLiteral("\"");
	qsizetype pendingBackslashes = 0;
	for (const QChar &current : argument) {
		if (current == QLatin1Char('\\')) {
			++pendingBackslashes;
			continue;
		}

		if (current == QLatin1Char('"')) {
			// A double quote terminates the pending backslash run: emit the
			// run doubled, then the escaped quote itself.
			result += QString(pendingBackslashes * 2 + 1, QLatin1Char('\\'));
			result += QLatin1Char('"');
		} else {
			result += QString(pendingBackslashes, QLatin1Char('\\'));
			result += current;
		}
		pendingBackslashes = 0;
	}
	// The closing quote also terminates a trailing backslash run: double it.
	result += QString(pendingBackslashes * 2, QLatin1Char('\\'));
	result += QLatin1Char('"');

	return result;
}

QString toWindowsArgumentString(const QStringList &arguments) {
	QStringList quotedArguments;
	quotedArguments.reserve(arguments.size());
	for (const QString &argument : arguments) {
		quotedArguments << quoteWindowsArgument(argument);
	}

	return quotedArguments.join(QLatin1String(" "));
}

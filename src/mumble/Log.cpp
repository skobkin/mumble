// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "Log.h"

#include "Accessibility.h"
#include "AudioOutput.h"
#include "AudioOutputSample.h"
#include "AudioOutputToken.h"
#include "Channel.h"
#include "MainWindow.h"
#include "NetworkConfig.h"
#include "RichTextEditor.h"
#include "Screen.h"
#include "ServerHandler.h"
#ifndef USE_NO_TTS
#	include "TextToSpeech.h"
#endif
#include "Utils.h"
#include "VolumeAdjustment.h"
#include "Global.h"

#include <limits>
#include <type_traits>

#include <QSignalBlocker>
#include <QtCore/QMutexLocker>
#include <QtCore/QRegularExpression>
#include <QtGui/QImageWriter>
#include <QtGui/QScreen>
#include <QtGui/QTextBlock>
#include <QtGui/QTextDocumentFragment>
#include <QtNetwork/QNetworkReply>

const QString LogConfig::name = QLatin1String("LogConfig");

static ConfigWidget *LogConfigDialogNew(Settings &st) {
	return new LogConfig(st);
}

static ConfigRegistrar registrarLog(4000, LogConfigDialogNew);

LogConfig::LogConfig(Settings &st) : ConfigWidget(st) {
	setupUi(this);

#ifdef USE_NO_TTS
	qgbTTS->setDisabled(true);
	qsTTSVolume->setDisabled(true);
#endif

	qtwMessages->header()->setSectionResizeMode(ColMessage, QHeaderView::Stretch);
	qtwMessages->header()->setSectionResizeMode(ColConsole, QHeaderView::ResizeToContents);
	qtwMessages->header()->setSectionResizeMode(ColNotification, QHeaderView::ResizeToContents);
	qtwMessages->header()->setSectionResizeMode(ColHighlight, QHeaderView::ResizeToContents);
	qtwMessages->header()->setSectionResizeMode(ColTTS, QHeaderView::ResizeToContents);
	qtwMessages->header()->setSectionResizeMode(ColMessageLimit, QHeaderView::ResizeToContents);
	qtwMessages->header()->setSectionResizeMode(ColStaticSound, QHeaderView::ResizeToContents);

	qtwMessages->headerItem()->setData(ColMessage, Qt::AccessibleTextRole, tr("Message type"));
	qtwMessages->headerItem()->setData(ColConsole, Qt::AccessibleTextRole, tr("Log message to console checkbox"));
	qtwMessages->headerItem()->setData(ColNotification, Qt::AccessibleTextRole,
									   tr("Display pop-up notification for message checkbox"));
	qtwMessages->headerItem()->setData(ColHighlight, Qt::AccessibleTextRole,
									   tr("Highlight window for message checkbox"));
	qtwMessages->headerItem()->setData(ColTTS, Qt::AccessibleTextRole,
									   tr("Read message using text to speech checkbox"));
	qtwMessages->headerItem()->setData(ColMessageLimit, Qt::AccessibleTextRole,
									   tr("Limit message notification if user count is high checkbox"));
	qtwMessages->headerItem()->setData(ColStaticSound, Qt::AccessibleTextRole,
									   tr("Play sound file for message checkbox"));
	qtwMessages->headerItem()->setData(ColStaticSoundPath, Qt::AccessibleTextRole, tr("Path to sound file"));

	// Add a "All messages" entry
	allMessagesItem = new QTreeWidgetItem(qtwMessages);
	allMessagesItem->setText(ColMessage, QObject::tr("All messages"));
	allMessagesItem->setCheckState(ColConsole, Qt::Unchecked);
	allMessagesItem->setToolTip(ColConsole, QObject::tr("Toggle console for all events"));
	allMessagesItem->setCheckState(ColNotification, Qt::Unchecked);
	allMessagesItem->setToolTip(ColNotification, QObject::tr("Toggle pop-up notifications for all events"));
	allMessagesItem->setCheckState(ColHighlight, Qt::Unchecked);
	allMessagesItem->setToolTip(ColHighlight, QObject::tr("Toggle window highlight (if not active) for all events"));
	allMessagesItem->setCheckState(ColMessageLimit, Qt::Unchecked);
	allMessagesItem->setToolTip(ColMessageLimit, tr("Click here to toggle message limiting for all events - If using "
													"this option be sure to change the user limit below."));
	allMessagesItem->setCheckState(ColStaticSound, Qt::Unchecked);
	allMessagesItem->setToolTip(ColStaticSound, QObject::tr("Click here to toggle sound notifications for all events"));
#ifndef USE_NO_TTS
	allMessagesItem->setCheckState(ColTTS, Qt::Unchecked);
	allMessagesItem->setToolTip(ColTTS, QObject::tr("Toggle Text-to-Speech for all events"));
#endif

	QTreeWidgetItem *twi;
	for (int i = Log::firstMsgType; i <= Log::lastMsgType; ++i) {
		Log::MsgType t            = Log::msgOrder[i];
		const QString messageName = Global::get().l->msgName(t);

		twi = new QTreeWidgetItem(qtwMessages);
		twi->setData(ColMessage, Qt::UserRole, static_cast< int >(t));
		twi->setText(ColMessage, messageName);
		twi->setCheckState(ColConsole, Qt::Unchecked);
		twi->setCheckState(ColNotification, Qt::Unchecked);
		twi->setCheckState(ColHighlight, Qt::Unchecked);
		twi->setCheckState(ColMessageLimit, Qt::Unchecked);
		twi->setCheckState(ColStaticSound, Qt::Unchecked);

		twi->setToolTip(ColConsole, tr("Toggle console for %1 events").arg(messageName));
		twi->setToolTip(ColNotification, tr("Toggle pop-up notifications for %1 events").arg(messageName));
		twi->setToolTip(ColHighlight, tr("Toggle window highlight (if not active) for %1 events").arg(messageName));
		twi->setToolTip(ColMessageLimit, tr("Toggle message limiting behavior for %1 events ").arg(messageName));
		twi->setToolTip(ColStaticSound, tr("Click here to toggle sound notification for %1 events").arg(messageName));
		twi->setToolTip(ColStaticSoundPath, tr("Path to sound file used for sound notifications in the case of %1 "
											   "events<br />Single click to play<br />Double-click to change")
												.arg(messageName));

		twi->setWhatsThis(ColConsole, tr("Click here to toggle console output for %1 events.<br />If checked, this "
										 "option makes Mumble output all %1 events in its message log.")
										  .arg(messageName));
		twi->setWhatsThis(ColNotification,
						  tr("Click here to toggle pop-up notifications for %1 events.<br />If checked, a notification "
							 "pop-up will be created by Mumble for every %1 event.")
							  .arg(messageName));
		twi->setWhatsThis(ColHighlight, tr("Click here to toggle window highlight for %1 events.<br />If checked, "
										   "Mumble's window will be highlighted for every %1 event, if not active.")
											.arg(messageName));
		twi->setWhatsThis(
			ColMessageLimit,
			tr("Click here to toggle limiting for %1 events.<br />If checked, notifications for this event type "
			   "will not be played when the user count on the server exceeds the set threshold.")
				.arg(messageName));
		twi->setWhatsThis(ColStaticSound, tr("Click here to toggle sound notification for %1 events.<br />If checked, "
											 "Mumble uses a sound file predefined by you to indicate %1 events. Sound "
											 "files and Text-To-Speech cannot be used at the same time.")
											  .arg(messageName));
		twi->setWhatsThis(ColStaticSoundPath,
						  tr("Path to sound file used for sound notifications in the case of %1 events.<br />Single "
							 "click to play<br />Double-click to change<br />Ensure that sound notifications for these "
							 "events are enabled or this field will not have any effect.")
							  .arg(messageName));
#ifndef USE_NO_TTS
		twi->setCheckState(ColTTS, Qt::Unchecked);
		twi->setToolTip(ColTTS, tr("Toggle Text-To-Speech for %1 events").arg(messageName));
		twi->setWhatsThis(
			ColTTS,
			tr("Click here to toggle Text-To-Speech for %1 events.<br />If checked, Mumble uses Text-To-Speech to read "
			   "%1 events out loud to you. Text-To-Speech is also able to read the contents of the event which is not "
			   "true for sound files. Text-To-Speech and sound files cannot be used at the same time.")
				.arg(messageName));
#endif
	}
}

void LogConfig::updateSelectAllButtons() {
	QList< QTreeWidgetItem * > qlItems = qtwMessages->findItems(QString(), Qt::MatchContains);
	bool allConsoleChecked             = true;
	bool allNotificationChecked        = true;
	bool allHighlightChecked           = true;
#ifndef USE_NO_TTS
	bool allTTSChecked = true;
#endif
	bool allSoundChecked = true;
	bool allLimitChecked = true;
	for (QTreeWidgetItem *i : qlItems) {
		if (i == allMessagesItem) {
			continue;
		}

		if (i->checkState(ColConsole) != Qt::Checked) {
			allConsoleChecked = false;
		}
		if (i->checkState(ColNotification) != Qt::Checked) {
			allNotificationChecked = false;
		}
		if (i->checkState(ColHighlight) != Qt::Checked) {
			allHighlightChecked = false;
		}
#ifndef USE_NO_TTS
		if (i->checkState(ColTTS) != Qt::Checked) {
			allTTSChecked = false;
		}
#endif
		if (i->checkState(ColMessageLimit) != Qt::Checked) {
			allLimitChecked = false;
		}
		if (i->checkState(ColStaticSound) != Qt::Checked) {
			allSoundChecked = false;
		}

		if (!allConsoleChecked && !allNotificationChecked && !allHighlightChecked && !allSoundChecked) {
#ifndef USE_NO_TTS
			if (!allTTSChecked) {
				break;
			}
#else
			break;
#endif
		}
	}

	const QSignalBlocker blocker(qtwMessages);
	allMessagesItem->setCheckState(ColConsole, allConsoleChecked ? Qt::Checked : Qt::Unchecked);
	allMessagesItem->setCheckState(ColNotification, allNotificationChecked ? Qt::Checked : Qt::Unchecked);
	allMessagesItem->setCheckState(ColHighlight, allHighlightChecked ? Qt::Checked : Qt::Unchecked);
#ifndef USE_NO_TTS
	allMessagesItem->setCheckState(ColTTS, allTTSChecked ? Qt::Checked : Qt::Unchecked);
#endif
	allMessagesItem->setCheckState(ColMessageLimit, allLimitChecked ? Qt::Checked : Qt::Unchecked);
	allMessagesItem->setCheckState(ColStaticSound, allSoundChecked ? Qt::Checked : Qt::Unchecked);
}

QString LogConfig::title() const {
	return windowTitle();
}

const QString &LogConfig::getName() const {
	return LogConfig::name;
}

QIcon LogConfig::icon() const {
	return QIcon(QLatin1String("skin:config_msgs.png"));
}

void LogConfig::load(const Settings &r) {
	QList< QTreeWidgetItem * > qlItems = qtwMessages->findItems(QString(), Qt::MatchContains);

	for (QTreeWidgetItem *i : qlItems) {
		if (i == allMessagesItem) {
			continue;
		}
		Log::MsgType mt         = static_cast< Log::MsgType >(i->data(ColMessage, Qt::UserRole).toInt());
		Settings::MessageLog ml = static_cast< Settings::MessageLog >(r.qmMessages.value(mt));

		i->setCheckState(ColConsole, (ml & Settings::LogConsole) ? Qt::Checked : Qt::Unchecked);
		i->setCheckState(ColNotification, (ml & Settings::LogBalloon) ? Qt::Checked : Qt::Unchecked);
		i->setCheckState(ColHighlight, (ml & Settings::LogHighlight) ? Qt::Checked : Qt::Unchecked);
#ifndef USE_NO_TTS
		i->setCheckState(ColTTS, (ml & Settings::LogTTS) ? Qt::Checked : Qt::Unchecked);
#endif
		i->setCheckState(ColMessageLimit, (ml & Settings::LogMessageLimit) ? Qt::Checked : Qt::Unchecked);
		i->setCheckState(ColStaticSound, (ml & Settings::LogSoundfile) ? Qt::Checked : Qt::Unchecked);
		i->setText(ColStaticSoundPath, r.qmMessageSounds.value(mt));
	}

	qsbMaxBlocks->setValue(r.iMaxLogBlocks);
	qcb24HourClock->setChecked(r.bLog24HourClock);
	qsbChatMessageMargins->setValue(r.iChatMessageMargins);

#ifdef USE_NO_TTS
	qtwMessages->hideColumn(ColTTS);
	qsTTSVolume->hide();
	qlTTSVolume->hide();
#else
	loadSlider(qsTTSVolume, r.iTTSVolume);
	qsbThreshold->setValue(r.iTTSThreshold);
	qcbReadBackOwn->setChecked(r.bTTSMessageReadBack);
	qcbNoScope->setChecked(r.bTTSNoScope);
	qcbNoAuthor->setChecked(r.bTTSNoAuthor);
	qcbEnableTTS->setChecked(r.bTTS);

#endif
	loadSlider(qsNotificationVolume, VolumeAdjustment::toIntegerDBAdjustment(r.notificationVolume));
	loadSlider(qsCueVolume, VolumeAdjustment::toIntegerDBAdjustment(r.cueVolume));
	qcbWhisperFriends->setChecked(r.bWhisperFriends);
	qsbMessageLimitUsers->setValue(r.iMessageLimitUserThreshold);
}

void LogConfig::save() const {
	QList< QTreeWidgetItem * > qlItems = qtwMessages->findItems(QString(), Qt::MatchContains);
	for (QTreeWidgetItem *i : qlItems) {
		if (i == allMessagesItem) {
			continue;
		}
		Log::MsgType mt = static_cast< Log::MsgType >(i->data(ColMessage, Qt::UserRole).toInt());

		int v = 0;
		if (i->checkState(ColConsole) == Qt::Checked)
			v |= Settings::LogConsole;
		if (i->checkState(ColNotification) == Qt::Checked)
			v |= Settings::LogBalloon;
		if (i->checkState(ColHighlight) == Qt::Checked)
			v |= Settings::LogHighlight;
#ifndef USE_NO_TTS
		if (i->checkState(ColTTS) == Qt::Checked)
			v |= Settings::LogTTS;
#endif
		if (i->checkState(ColMessageLimit) == Qt::Checked) {
			v |= Settings::LogMessageLimit;
		}
		if (i->checkState(ColStaticSound) == Qt::Checked)
			v |= Settings::LogSoundfile;
		s.qmMessages[mt]      = static_cast< unsigned int >(v);
		s.qmMessageSounds[mt] = i->text(ColStaticSoundPath);
	}
	s.iMaxLogBlocks       = qsbMaxBlocks->value();
	s.bLog24HourClock     = qcb24HourClock->isChecked();
	s.iChatMessageMargins = qsbChatMessageMargins->value();

#ifndef USE_NO_TTS
	s.iTTSVolume          = qsTTSVolume->value();
	s.iTTSThreshold       = qsbThreshold->value();
	s.bTTSMessageReadBack = qcbReadBackOwn->isChecked();
	s.bTTSNoScope         = qcbNoScope->isChecked();
	s.bTTSNoAuthor        = qcbNoAuthor->isChecked();
	s.bTTS                = qcbEnableTTS->isChecked();
#endif
	s.notificationVolume         = VolumeAdjustment::toFactor(qsNotificationVolume->value());
	s.cueVolume                  = VolumeAdjustment::toFactor(qsCueVolume->value());
	s.bWhisperFriends            = qcbWhisperFriends->isChecked();
	s.iMessageLimitUserThreshold = qsbMessageLimitUsers->value();
}

void LogConfig::accept() const {
#ifndef USE_NO_TTS
	Global::get().l->tts->setVolume(s.iTTSVolume);
#endif
	// Forwarded through the window so every console destination gets the new
	// limit (the window is the sink; Log itself holds no widgets).
	Global::get().mw->applyConsoleBlockLimit();
}

void LogConfig::on_qtwMessages_itemChanged(QTreeWidgetItem *i, int column) {
	if (i->isSelected() && i != allMessagesItem) {
		switch (column) {
			case ColTTS:
				if (i->checkState(ColTTS))
					i->setCheckState(ColStaticSound, Qt::Unchecked);
				break;
			case ColStaticSound:
				if (i->checkState(ColStaticSound)) {
					i->setCheckState(ColTTS, Qt::Unchecked);
					if (i->text(ColStaticSoundPath).isEmpty())
						browseForAudioFile();
				}
				break;
			default:
				break;
		}
	}

	if (i != allMessagesItem) {
		updateSelectAllButtons();
	} else {
		// Suppress signals on the TreeWidget
		const QSignalBlocker blocker(qtwMessages);
		// Select / Unselect all entries of that column
		QList< QTreeWidgetItem * > qlItems = qtwMessages->findItems(QString(), Qt::MatchContains);
		for (QTreeWidgetItem *item : qlItems) {
			if (item != allMessagesItem) {
				item->setCheckState(column, allMessagesItem->checkState(column));
			}
		}
	}

	if (column != ColMessage && column != ColStaticSoundPath) {
		i->setData(column, Qt::AccessibleDescriptionRole,
				   i->checkState(column) == Qt::Checked ? tr("checked") : tr("unchecked"));
	}
}

void LogConfig::on_qtwMessages_itemClicked(QTreeWidgetItem *item, int column) {
	if (item && item != allMessagesItem && column == ColStaticSoundPath) {
		AudioOutputPtr ao = Global::get().ao;
		if (ao) {
			if (!ao->playSample(item->text(ColStaticSoundPath), Global::get().s.notificationVolume)) {
				browseForAudioFile();
			}
		}
	}
}

void LogConfig::on_qtwMessages_itemDoubleClicked(QTreeWidgetItem *item, int column) {
	if (item && item != allMessagesItem && column == ColStaticSoundPath)
		browseForAudioFile();
}

void LogConfig::browseForAudioFile() {
	QTreeWidgetItem *i = qtwMessages->selectedItems()[0];
	QString defaultpath(i->text(ColStaticSoundPath));
	QString file = AudioOutputSample::browseForSndfile(defaultpath);
	if (!file.isEmpty()) {
		i->setText(ColStaticSoundPath, file);
		i->setCheckState(ColStaticSound, Qt::Checked);
	}
}

void LogConfig::on_qsNotificationVolume_valueChanged(int value) {
	qsbNotificationVolume->setValue(value);
	Mumble::Accessibility::setSliderSemanticValue(qsNotificationVolume,
												  QString("%1 %2").arg(value).arg(tr("decibels")));
}

void LogConfig::on_qsCueVolume_valueChanged(int value) {
	qsbCueVolume->setValue(value);
	Mumble::Accessibility::setSliderSemanticValue(qsCueVolume, QString("%1 %2").arg(value).arg(tr("decibels")));
}

void LogConfig::on_qsTTSVolume_valueChanged(int value) {
	qsbTTSVolume->setValue(value);
	Mumble::Accessibility::setSliderSemanticValue(qsTTSVolume, Mumble::Accessibility::SliderMode::READ_PERCENT, "%");
}

void LogConfig::on_qsbNotificationVolume_valueChanged(int value) {
	qsNotificationVolume->setValue(value);
}

void LogConfig::on_qsbCueVolume_valueChanged(int value) {
	qsCueVolume->setValue(value);
}

void LogConfig::on_qsbTTSVolume_valueChanged(int value) {
	qsTTSVolume->setValue(value);
}


QMutex Log::qmDeferredLogs;
QVector< LogMessage > Log::qvDeferredLogs;


Log::Log(QObject *p) : QObject(p) {
	qRegisterMetaType< Log::MsgType >();
	// The queued thread-bounce below delivers routing metadata by value.
	qRegisterMetaType< ChatRoutingInfo >();

#ifndef USE_NO_TTS
	tts = new TextToSpeech(this);
	tts->setVolume(Global::get().s.iTTSVolume);
#endif
	uiLastId = 0;

	QObject::connect(this, &Log::highlightSpawned, Global::get().mw, &MainWindow::highlightWindow);
}

// Display order in settingsscreen, allows to insert new events without breaking config-compatibility with older
// versions
const Log::MsgType Log::msgOrder[] = { DebugInfo,
									   CriticalError,
									   Warning,
									   Information,
									   ServerConnected,
									   ServerDisconnected,
									   UserJoin,
									   UserLeave,
									   ChannelListeningAdd,
									   ChannelListeningRemove,
									   Recording,
									   YouKicked,
									   UserKicked,
									   UserRenamed,
									   SelfMute,
									   SelfUnmute,
									   SelfDeaf,
									   SelfUndeaf,
									   OtherSelfMute,
									   YouMuted,
									   YouMutedOther,
									   OtherMutedOther,
									   SelfChannelJoin,
									   SelfChannelJoinOther,
									   ChannelJoin,
									   ChannelLeave,
									   ChannelJoinConnect,
									   ChannelLeaveDisconnect,
									   PermissionDenied,
									   TextMessage,
									   PrivateTextMessage,
									   PluginMessage };

const char *Log::msgNames[] = { QT_TRANSLATE_NOOP("Log", "Debug"),
								QT_TRANSLATE_NOOP("Log", "Critical"),
								QT_TRANSLATE_NOOP("Log", "Warning"),
								QT_TRANSLATE_NOOP("Log", "Information"),
								QT_TRANSLATE_NOOP("Log", "Server connected"),
								QT_TRANSLATE_NOOP("Log", "Server disconnected"),
								QT_TRANSLATE_NOOP("Log", "User joined server"),
								QT_TRANSLATE_NOOP("Log", "User left server"),
								QT_TRANSLATE_NOOP("Log", "User recording state changed"),
								QT_TRANSLATE_NOOP("Log", "User kicked (you or by you)"),
								QT_TRANSLATE_NOOP("Log", "User kicked"),
								QT_TRANSLATE_NOOP("Log", "You self-muted"),
								QT_TRANSLATE_NOOP("Log", "Other self-muted/deafened"),
								QT_TRANSLATE_NOOP("Log", "User muted (you)"),
								QT_TRANSLATE_NOOP("Log", "User muted (by you)"),
								QT_TRANSLATE_NOOP("Log", "User muted (other)"),
								QT_TRANSLATE_NOOP("Log", "User joined channel"),
								QT_TRANSLATE_NOOP("Log", "User left channel"),
								QT_TRANSLATE_NOOP("Log", "Permission denied"),
								QT_TRANSLATE_NOOP("Log", "Text message"),
								QT_TRANSLATE_NOOP("Log", "You self-unmuted"),
								QT_TRANSLATE_NOOP("Log", "You self-deafened"),
								QT_TRANSLATE_NOOP("Log", "You self-undeafened"),
								QT_TRANSLATE_NOOP("Log", "User renamed"),
								QT_TRANSLATE_NOOP("Log", "You joined channel"),
								QT_TRANSLATE_NOOP("Log", "You joined channel (moved)"),
								QT_TRANSLATE_NOOP("Log", "User connected and entered channel"),
								QT_TRANSLATE_NOOP("Log", "User left channel and disconnected"),
								QT_TRANSLATE_NOOP("Log", "Private text message"),
								QT_TRANSLATE_NOOP("Log", "User started listening to channel"),
								QT_TRANSLATE_NOOP("Log", "User stopped listening to channel"),
								QT_TRANSLATE_NOOP("Log", "Plugin message") };

QString Log::msgName(MsgType t) const {
	return tr(msgNames[t]);
}

const char *Log::colorClasses[] = { "time", "server", "privilege" };

const QStringList Log::allowedSchemes() {
	QStringList qslAllowedSchemeNames;
	qslAllowedSchemeNames << QLatin1String("mumble");
	qslAllowedSchemeNames << QLatin1String("http");
	qslAllowedSchemeNames << QLatin1String("https");
	qslAllowedSchemeNames << QLatin1String("gemini");
	qslAllowedSchemeNames << QLatin1String("ftp");
	qslAllowedSchemeNames << QLatin1String("clientid");
	qslAllowedSchemeNames << QLatin1String("channelid");
	qslAllowedSchemeNames << QLatin1String("spotify");
	qslAllowedSchemeNames << QLatin1String("steam");
	qslAllowedSchemeNames << QLatin1String("irc");
	qslAllowedSchemeNames << QLatin1String("gg"); // Gadu-Gadu http://gg.pl - Polish instant messenger
	qslAllowedSchemeNames << QLatin1String("mailto");
	qslAllowedSchemeNames << QLatin1String("xmpp");
	qslAllowedSchemeNames << QLatin1String("skype");
	qslAllowedSchemeNames << QLatin1String("rtmp");   // http://en.wikipedia.org/wiki/Real_Time_Messaging_Protocol
	qslAllowedSchemeNames << QLatin1String("magnet"); // https://en.wikipedia.org/wiki/Magnet_URI_scheme

	return qslAllowedSchemeNames;
}

QString Log::msgColor(const QString &text, LogColorType t) {
	QString classname;

	return QString::fromLatin1("<span class='log-%1'>%2</span>").arg(QString::fromLatin1(colorClasses[t])).arg(text);
}

QString Log::formatChannel(::Channel *c) {
	return QString::fromLatin1("<a href='channelid://id.%1/%3' class='log-channel'>%2</a>")
		.arg(c->iId)
		.arg(c->qsName.toHtmlEscaped())
		.arg(QString::fromLatin1(Global::get().sh->qbaDigest.toBase64()));
}

void Log::logOrDefer(Log::MsgType mt, const QString &console, const QString &terse, bool ownMessage,
					 const QString &overrideTTS, bool ignoreTTS, const ChatRoutingInfo &routing) {
	if (Global::get().l) {
		// log directly as it seems the log-UI has been set-up already
		Global::get().l->log(mt, console, terse, ownMessage, overrideTTS, ignoreTTS, routing);
	} else {
		// defer the log
		QMutexLocker mLock(&Log::qmDeferredLogs);

		qvDeferredLogs.append(LogMessage(mt, console, terse, ownMessage, overrideTTS, ignoreTTS, routing));
	}
}

QString Log::formatClientUser(ClientUser *cu, LogColorType t, const QString &displayName) {
	QString className;
	if (t == Log::Target) {
		className = QString::fromLatin1("target");
	} else if (t == Log::Source) {
		className = QString::fromLatin1("source");
	}

	if (cu) {
		QString name = (displayName.isNull() ? cu->qsName : displayName).toHtmlEscaped();
		if (cu->qsHash.isEmpty()) {
			return QString::fromLatin1("<a href='clientid://id.%2/%4' class='log-user log-%1'>%3</a>")
				.arg(className)
				.arg(cu->uiSession)
				.arg(name)
				.arg(QString::fromLatin1(Global::get().sh->qbaDigest.toBase64()));
		} else {
			return QString::fromLatin1("<a href='clientid://%2' class='log-user log-%1'>%3</a>")
				.arg(className)
				.arg(cu->qsHash)
				.arg(name);
		}
	} else {
		return QString::fromLatin1("<span class='log-server log-%1'>%2</span>").arg(className).arg(tr("the server"));
	}
}

void Log::setIgnore(MsgType t, int ignore) {
	qmIgnore.insert(t, ignore);
}

void Log::clearIgnore() {
	qmIgnore.clear();
}

QString Log::imageToImg(const QByteArray &format, const QByteArray &image) {
	QString fmt = QLatin1String(format);

	if (fmt.isEmpty())
		fmt = QLatin1String("qt");

	QByteArray rawbase = image.toBase64();
	QByteArray encoded;
	int i     = 0;
	int begin = 0, end = 0;
	do {
		begin = i * 72;
		end   = begin + 72;

		encoded.append(QUrl::toPercentEncoding(QLatin1String(rawbase.mid(begin, 72))));
		if (end < rawbase.length())
			encoded.append('\n');

		++i;
	} while (end < rawbase.length());

	return QString::fromLatin1("<img src=\"data:image/%1;base64,%2\" />").arg(fmt).arg(QLatin1String(encoded));
}

QString Log::imageToImg(QImage img, int maxSize) {
	constexpr int MAX_WIDTH  = 1600;
	constexpr int MAX_HEIGHT = 1000;

	if ((img.width() > MAX_WIDTH) || (img.height() > MAX_HEIGHT)) {
		img = img.scaled(MAX_WIDTH, MAX_HEIGHT, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	}

	int quality       = 100;
	QByteArray format = "JPEG";

	QByteArray qba;
	QString result;
	while (quality > 0) {
		qba.clear();
		QBuffer qb(&qba);
		qb.open(QIODevice::WriteOnly);

		QImageWriter imgwrite(&qb, format);
		imgwrite.setQuality(quality);
		imgwrite.write(img);
		result = imageToImg(format, qba);
		if (result.length() < maxSize || maxSize == 0) {
			return result;
		}
		quality -= 10;
	}
	return QString();
}

QString Log::validHtml(const QString &html, QTextCursor *tc) {
	LogDocument qtd;

	// The MainWindow is unavailable in isolated tests (and during parts of
	// startup/shutdown); fall back to the primary screen's geometry then
	// instead of dereferencing a null pointer.
	QRectF qr;
	if (Global::g_global_struct && Global::get().mw) {
		qr = Mumble::Screen::screenFromWidget(*Global::get().mw)->availableGeometry();
	} else {
		const QScreen *primary = qApp->primaryScreen();
		qr                     = primary ? primary->availableGeometry() : QRectF(0, 0, 800, 600);
	}
	qtd.setTextWidth(qr.width() / 2);
	qtd.setDefaultStyleSheet(qApp->styleSheet());

	// Call documentLayout on our LogDocument to ensure
	// it has a layout backing it. With a layout set on
	// the document, it will attempt to load all the
	// resources it contains as soon as we call setHtml(),
	// allowing our validation checks for things such as
	// data URL images to run.
	(void) qtd.documentLayout();
	qtd.setHtml(html);

	QStringList qslAllowed = allowedSchemes();
	for (QTextBlock qtb = qtd.begin(); qtb != qtd.end(); qtb = qtb.next()) {
		for (QTextBlock::iterator qtbi = qtb.begin(); qtbi != qtb.end(); ++qtbi) {
			const QTextFragment &qtf = qtbi.fragment();
			QTextCharFormat qcf      = qtf.charFormat();
			if (!qcf.anchorHref().isEmpty()) {
				QUrl url(qcf.anchorHref());
				if (!url.isValid() || !qslAllowed.contains(url.scheme())) {
					QTextCharFormat qcfn = QTextCharFormat();
					QTextCursor qtc(&qtd);
					qtc.setPosition(qtf.position(), QTextCursor::MoveAnchor);
					qtc.setPosition(qtf.position() + qtf.length(), QTextCursor::KeepAnchor);
					qtc.setCharFormat(qcfn);
					qtbi = qtb.begin();
				}
			}
		}
	}

	qtd.adjustSize();
	QSizeF s = qtd.size();

	if (!s.isValid() || s.width() == 0 || s.height() == 0) {
		QString errorInvalidSizeMessage = tr("[[ Invalid size ]]");
		if (tc) {
			tc->insertText(errorInvalidSizeMessage);
			return QString();
		} else {
			return errorInvalidSizeMessage;
		}
	}

	static constexpr unsigned int allowedSize = 2048 * 2048;
	// This checks against negative sizes as well as sizes whose product overflows the integral type we use to represent
	// the maximum allowed size
	const bool saneSize =
		s.width() > 0 && s.height() > 0 && s.width() < std::numeric_limits< decltype(allowedSize) >::max() / s.height();
	const auto messageSize = static_cast< std::decay_t< decltype(allowedSize) > >(s.width() * s.height());

	if (!saneSize || messageSize > allowedSize) {
		QString errorSizeMessage = tr("[[ Text object too large to display ]]");
		if (tc) {
			tc->insertText(errorSizeMessage);
			return QString();
		} else {
			return errorSizeMessage;
		}
	}

	if (tc) {
		QTextCursor tcNew(&qtd);
		tcNew.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
		tc->insertFragment(tcNew.selection());
		return QString();
	} else {
		return qtd.toHtml();
	}
}

void Log::setConsoleSink(ConsoleSink *sink) {
	m_consoleSink = sink;
}

void Log::log(MsgType mt, const QString &console, const QString &terse, bool ownMessage, const QString &overrideTTS,
			  bool ignoreTTS, const ChatRoutingInfo &routing) {
	if (QThread::currentThread() != thread()) {
		// Invoke in main thread in order to keep the Qt gods on our side by not calling any UI
		// functions from a separate thread (can lead to program crashes).
		// The metatype-registered copy of the routing info is what gets
		// queued: the producer's locals may die before delivery.
		QMetaObject::invokeMethod(this, "log", Qt::QueuedConnection, Q_ARG(Log::MsgType, mt),
								  Q_ARG(const QString &, console), Q_ARG(const QString &, terse),
								  Q_ARG(bool, ownMessage), Q_ARG(const QString &, overrideTTS), Q_ARG(bool, ignoreTTS),
								  Q_ARG(ChatRoutingInfo, routing));
		return;
	}

	QDateTime dt = QDateTime::currentDateTime();

	int ignore = qmIgnore.value(mt);
	if (ignore) {
		ignore--;
		qmIgnore.insert(mt, ignore);
		return;
	}

	// No sink attached. The only reachable window is between the MainWindow's
	// destruction (the sink detaches as its destructor's first statement) and
	// Log's own destruction at process exit: no event delivery can happen
	// there (the last event pump ran before the window was deleted), and this
	// guard keeps any direct same-thread call from dereferencing a dying
	// window. Entries are dropped.
	if (!m_consoleSink) {
		return;
	}

	QString plain = QTextDocumentFragment::fromHtml(console).toPlainText();

	quint32 flags = Global::get().s.qmMessages.value(mt);

	// Message output on console
	if ((flags & Settings::LogConsole)) {
		// Convert CRLF to unix-style LF and old mac-style LF (single \r) to unix-style as well.
		// Kept here, and in-place, because the balloon and TTS payloads below
		// read the normalized text.
		plain.replace(QLatin1String("\r\n"), QLatin1String("\n")).replace(QLatin1String("\r"), QLatin1String("\n"));

		// Routing (chat vs. activity destination) and rendering (frames, date
		// separators, timestamp, validation, scroll preservation) live on the
		// sink side; the per-entry settings are snapshotted there so one entry
		// renders consistently. The routing metadata rides along: HTML and
		// metadata enter the store on the other side as one accepted entry or
		// not at all (everything before this line already had its chance to
		// veto).
		m_consoleSink->appendConsoleEntry(mt, console, ownMessage, dt, routing);
	}

	if (!ownMessage) {
		if (!m_consoleSink->isConsoleEntryVisible(mt)) {
			// Message notification with window highlight
			if (flags & Settings::LogHighlight) {
				emit highlightSpawned();
			}

			// Message notification with balloon tooltips
			if (flags & Settings::LogBalloon) {
				// Replace any instances of a "Object Replacement Character" from QTextDocumentFragment::toPlainText
				plain = plain.replace("\xEF\xBF\xBC", tr("[embedded content]"));

				QSystemTrayIcon::MessageIcon msgIcon = QSystemTrayIcon::NoIcon;
				switch (mt) {
					case DebugInfo:
					case CriticalError:
						msgIcon = QSystemTrayIcon::Critical;
						break;
					case Warning:
						msgIcon = QSystemTrayIcon::Warning;
						break;
					case TextMessage:
					case PrivateTextMessage:
						msgIcon = QSystemTrayIcon::NoIcon;
						break;
					case Information:
					case ServerConnected:
					case ServerDisconnected:
					case UserJoin:
					case UserLeave:
					case Recording:
					case YouKicked:
					case UserKicked:
					case SelfMute:
					case OtherSelfMute:
					case YouMuted:
					case YouMutedOther:
					case OtherMutedOther:
					case ChannelJoin:
					case ChannelLeave:
					case PermissionDenied:
					case SelfUnmute:
					case SelfDeaf:
					case SelfUndeaf:
					case UserRenamed:
					case SelfChannelJoin:
					case SelfChannelJoinOther:
					case ChannelJoinConnect:
					case ChannelLeaveDisconnect:
					case ChannelListeningAdd:
					case ChannelListeningRemove:
					case PluginMessage:
						msgIcon = QSystemTrayIcon::Information;
						break;
				}
				emit notificationSpawned(msgName(mt), plain, msgIcon);
			}
		}

		// Don't make any noise if we are self deafened (Unless it is the sound for activating self deaf)
		if (Global::get().s.bDeaf && mt != Log::SelfDeaf) {
			return;
		}

		// Message notification with static sounds
		qsizetype connectedUsers = 0;
		{
			QReadLocker lock(&ClientUser::c_qrwlUsers);
			connectedUsers = ClientUser::c_qmUsers.size();
		}
		if ((flags & Settings::LogSoundfile)
			&& !(flags & Settings::LogMessageLimit && connectedUsers > Global::get().s.iMessageLimitUserThreshold)
			&& !NotificationSoundBlocker::s_blockedNotificationSounds.contains(mt)) {
			QString sSound    = Global::get().s.qmMessageSounds.value(mt);
			AudioOutputPtr ao = Global::get().ao;

			if (!ao || !ao->playSample(sSound, Global::get().s.notificationVolume)) {
				qWarning() << "Sound file" << sSound << "is not a valid audio file, fallback to TTS.";
				flags ^= Settings::LogSoundfile | Settings::LogTTS; // Fallback to TTS
			}
		}
	} else if (!Global::get().s.bTTSMessageReadBack) {
		return;
	}

	// Message notification with Text-To-Speech
	if (Global::get().s.bDeaf || !Global::get().s.bTTS || !(flags & Settings::LogTTS) || ignoreTTS) {
		return;
	}

	// If overrideTTS is a valid string use its contents as message
	if (!overrideTTS.isNull()) {
		plain = overrideTTS;
	}

	// Apply simplifications to spoken text
	const QRegularExpression identifyURL(QRegularExpression::anchoredPattern(QLatin1String("[a-z-]+://[^ <]*")),
										 QRegularExpression::CaseInsensitiveOption);

	const QStringList qslAllowed  = allowedSchemes();
	QRegularExpressionMatch match = identifyURL.match(plain);
	qsizetype pos                 = 0;

	while (match.hasMatch()) {
		QUrl url(match.captured(0).toLower());
		if (url.isValid() && qslAllowed.contains(url.scheme())) {
			// Replace it appropriately
			QString replacement;
			QString host = url.host().replace(QRegularExpression(QLatin1String("^www.")), QString());

			if (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https"))
				replacement = tr("link to %1").arg(host);
			else if (url.scheme() == QLatin1String("ftp"))
				replacement = tr("FTP link to %1").arg(host);
			else if (url.scheme() == QLatin1String("clientid"))
				replacement = tr("player link");
			else if (url.scheme() == QLatin1String("channelid"))
				replacement = tr("channel link");
			else
				replacement = tr("%1 link").arg(url.scheme());

			plain.replace(pos, match.capturedLength(), replacement);
		} else {
			pos += match.capturedLength();
		}

		match = identifyURL.match(plain, pos);
	}

#ifndef USE_NO_TTS
	// TTS threshold limiter.
	if (plain.length() <= Global::get().s.iTTSThreshold)
		tts->say(plain);
	else if ((!terse.isEmpty()) && (terse.length() <= Global::get().s.iTTSThreshold))
		tts->say(terse);
#else
	// Mark as unused
	Q_UNUSED(terse);
#endif
}

void Log::processDeferredLogs() {
	QMutexLocker mLocker(&Log::qmDeferredLogs);

	while (!qvDeferredLogs.isEmpty()) {
		LogMessage msg = qvDeferredLogs.takeFirst();

		log(msg.mt, msg.console, msg.terse, msg.ownMessage, msg.overrideTTS, msg.ignoreTTS, msg.routing);
	}
}

LogMessage::LogMessage(Log::MsgType mt, const QString &console, const QString &terse, bool ownMessage,
					   const QString &overrideTTS, bool ignoreTTS, const ChatRoutingInfo &routing)
	: mt(mt), console(console), terse(terse), ownMessage(ownMessage), overrideTTS(overrideTTS), ignoreTTS(ignoreTTS),
	  routing(routing) {
}

// ---------------------------------------------------------------------------
// LogDocument
// ---------------------------------------------------------------------------

namespace {

/// Reads the frame-role tag LogConsoleView::appendEntry stamps onto every
/// child frame of the root frame.
LogDocument::FrameRole frameRoleOf(const QTextFrame *frame) {
	return static_cast< LogDocument::FrameRole >(frame->frameFormat().intProperty(LogDocument::FrameRoleProperty));
}

/// Collects the image resource names referenced inside [from, to].
void collectImageNamesInRange(const QTextDocument *doc, int from, int to, QSet< QUrl > &names) {
	if (to < from)
		return;
	for (QTextBlock block = doc->findBlock(from); block.isValid() && block.position() <= to; block = block.next()) {
		for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
			const QTextFragment fragment = it.fragment();
			if (fragment.isValid() && fragment.charFormat().isImageFormat()) {
				const QString name = fragment.charFormat().toImageFormat().name();
				if (!name.isEmpty()) {
					names.insert(QUrl(name));
				}
			}
		}
	}
}

/// Approximate bytes a retained resource-cache KEY costs (the URL string
/// in UTF-16 plus map overhead). The decoded image data dominates this by
/// far; this is only the accounting for keys that outlive their image.
int resourceKeyBytes(const QUrl &name) {
	return static_cast< int >(name.toString().size()) * 2 + 32;
}

} // namespace

LogDocument::LogDocument(QObject *p) : QTextDocument(p) {
	// Display-only: no undo stack to carry around (setMaximumBlockCount used
	// to disable undo implicitly whenever a block limit was set).
	setUndoRedoEnabled(false);
	// QTextDocument::clear() is not virtual, so wholesale clears coming from
	// QTextEdit::clear() are detected through the content stream instead.
	connect(this, &QTextDocument::contentsChange, this, &LogDocument::onContentsChange);
}

const QImage &LogDocument::placeholderImage() {
	static const QImage placeholder(1, 1, QImage::Format_Mono);
	return placeholder;
}

QVariant LogDocument::loadResource(int type, const QUrl &url) {
	// Ignore requests for all external resources
	// that aren't images. We don't support any of them.
	if (type != QTextDocument::ImageResource) {
		addResource(type, url, QByteArray());
		return QByteArray();
	}

	// Only accept data URLs, not external resources
	if (url.isValid() && url.scheme() == QLatin1String("data")) {
		// Decode the image here (instead of deferring to
		// QTextDocument::loadResource) so that it lands in the overwritable
		// resources map: the eviction logic must be able to swap it for the
		// placeholder when its entry is trimmed.
		const QImage img = decodeDataUrlImage(url);
		if (!img.isNull()) {
			addResource(type, url, img);
			return img;
		}
		// Decode failure stays uncached.
		return QImage();
	}

	addResource(type, url, placeholderImage());

	return placeholderImage();
}

QImage LogDocument::decodeDataUrlImage(const QUrl &url) const {
	// Log::imageToImg percent-encodes the base64 payload and wraps it at 72
	// characters; QUrl parsing may additionally mangle whitespace. Rebuild
	// the payload defensively.
	QString encoded = url.toString(QUrl::FullyEncoded);
	encoded.remove(QLatin1Char('\n')).remove(QLatin1Char('\r'));

	// RFC 2397: data:[<mediatype>][;base64],<payload>. The base64 flag has
	// its own path: the payload is percent-encoded base64 text.
	static const QLatin1String base64Marker("base64,");
	qsizetype payloadStart = encoded.indexOf(base64Marker);
	if (payloadStart >= 0) {
		payloadStart += base64Marker.size();
		const QByteArray decoded =
			QByteArray::fromBase64(QByteArray::fromPercentEncoding(encoded.mid(payloadStart).toLatin1()));
		return QImage::fromData(decoded);
	}
	static const QLatin1String base64MarkerEncoded("base64%2C");
	payloadStart = encoded.indexOf(base64MarkerEncoded);
	if (payloadStart >= 0) {
		payloadStart += base64MarkerEncoded.size();
		const QByteArray decoded =
			QByteArray::fromBase64(QByteArray::fromPercentEncoding(encoded.mid(payloadStart).toLatin1()));
		return QImage::fromData(decoded);
	}

	// Without the flag the payload IS the percent-encoded image. The
	// pre-Stage-5 LogDocument deferred these URLs to Qt's own decoder
	// (QTextDocument::loadResource -> qDecodeDataUrl), so they must keep
	// decoding here: RichTextEditor renders foreign HTML through a
	// LogDocument, not only the console views. The payload starts after
	// the first comma (plain or re-encoded), and QImage::fromData sniffs
	// the format exactly like the old handler path did. A percent-encoded
	// payload can never contain the base64 markers itself (every special
	// byte is escaped), so the branches cannot trade places.
	qsizetype comma              = encoded.indexOf(QLatin1Char(','));
	const qsizetype commaEncoded = encoded.indexOf(QLatin1String("%2C"));
	if (commaEncoded >= 0 && (comma < 0 || commaEncoded < comma)) {
		comma = commaEncoded + 2;
	}
	if (comma < 0) {
		return QImage();
	}
	return QImage::fromData(QByteArray::fromPercentEncoding(encoded.mid(comma + 1).toLatin1()));
}

void LogDocument::setBlockBudget(int budget) {
	const int previous = m_blockBudget;
	m_blockBudget      = budget;

	// Lowering (or newly enabling) the budget trims immediately.
	if (budget > 0 && (previous <= 0 || budget < previous)) {
		enforceBlockBudget();
	}
}

int LogDocument::totalBlockCount() const {
	int count = 0;
	for (QTextBlock block = begin(); block.isValid(); block = block.next()) {
		++count;
	}
	return count;
}

int LogDocument::entryCount() const {
	int count                          = 0;
	const QList< QTextFrame * > frames = rootFrame()->childFrames();
	for (const QTextFrame *frame : frames) {
		if (frameRoleOf(frame) == MessageFrameRole) {
			++count;
		}
	}
	return count;
}

void LogDocument::enforceBlockBudget() {
	if (m_blockBudget <= 0) {
		return;
	}
	const int budget = m_blockBudget;
	trimInternal([this, budget](int) { return totalBlockCount() > budget; });
}

int LogDocument::removeLeadingEntries(int count) {
	return trimInternal([count](int removedSoFar) { return removedSoFar < count; });
}

int LogDocument::trimInternal(const std::function< bool(int) > &shouldTrim) {
	int trimmedEntries = 0;

	m_inInternalMutation = true;

	// Removes one whole child frame (both delimiters included - the canonical
	// frame removal selection), then drops residual empty leading blocks and
	// releases images that lost their last reference.
	const auto removeLeadingFrame = [this](QTextFrame *victim) {
		QSet< QUrl > candidateNames;
		collectImageNamesInRange(this, victim->firstPosition() - 1, victim->lastPosition() + 1, candidateNames);

		QTextCursor remover(this);
		remover.setPosition(victim->firstPosition() - 1);
		remover.setPosition(victim->lastPosition() + 1, QTextCursor::KeepAnchor);
		remover.removeSelectedText();

		// Drop empty blocks left at the very front (former inter-entry
		// separators). The block holding the first remaining frame's
		// start-of-frame delimiter always stays.
		while (!rootFrame()->childFrames().isEmpty()) {
			const QTextFrame *firstFrame = rootFrame()->childFrames().constFirst();
			const QTextBlock sofBlock    = findBlock(firstFrame->firstPosition() - 1);
			const QTextBlock leading     = begin();
			if (!leading.isValid() || leading == sofBlock || !leading.text().isEmpty() || leading.length() > 1) {
				break;
			}
			QTextCursor eraser(this);
			eraser.setPosition(leading.position());
			eraser.setPosition(leading.position() + leading.length(), QTextCursor::KeepAnchor);
			eraser.removeSelectedText();
		}

		releaseUnreferencedImageResources(candidateNames);
	};

	while (shouldTrim(trimmedEntries)) {
		const QList< QTextFrame * > frames = rootFrame()->childFrames();

		int messageCount = 0;
		for (const QTextFrame *frame : frames) {
			if (frameRoleOf(frame) == MessageFrameRole) {
				++messageCount;
			}
		}

		// Always keep the newest entry: a single oversized entry may overshoot
		// the budget, but the pane is never blanked and formatting is never
		// stripped mid-entry.
		if (messageCount <= 1) {
			break;
		}

		// One victim per iteration, always from the front:
		// - a leading MESSAGE frame is the oldest entry: it goes.
		// - a leading DATE frame is kept while any of its date's messages
		//   remain (the oldest MESSAGE right after it is the victim); it only
		//   goes itself once another date frame directly follows (it labels
		//   nothing anymore).
		QTextFrame *victim   = nullptr;
		bool removingMessage = false;
		if (frameRoleOf(frames.at(0)) == MessageFrameRole) {
			victim          = frames.at(0);
			removingMessage = true;
		} else if (frames.size() > 1 && frameRoleOf(frames.at(1)) == MessageFrameRole) {
			victim          = frames.at(1);
			removingMessage = true;
		} else {
			victim = frames.at(0);
		}

		if (!victim) {
			break;
		}

		removeLeadingFrame(victim);

		if (removingMessage) {
			++trimmedEntries;
		}
	}

	// Post-loop normalization: the last trimmed message may have been the
	// last of its date, leaving an obsolete leading date frame directly
	// before the next date frame. (Loop exit at budget satisfaction can stop
	// right at that boundary.)
	while (!rootFrame()->childFrames().isEmpty()) {
		const QList< QTextFrame * > frames = rootFrame()->childFrames();
		if (frames.size() < 2 || frameRoleOf(frames.at(0)) != DateFrameRole
			|| frameRoleOf(frames.at(1)) != DateFrameRole) {
			break;
		}
		removeLeadingFrame(frames.at(0));
	}

	m_inInternalMutation = false;

	if (trimmedEntries > 0) {
		emit entriesTrimmed(trimmedEntries);
	}
	return trimmedEntries;
}

void LogDocument::releaseImageResource(const QUrl &name) {
	QSet< QUrl > candidates;
	candidates.insert(name);
	releaseUnreferencedImageResources(candidates);
}

void LogDocument::releaseUnreferencedImageResources(const QSet< QUrl > &candidates) {
	if (candidates.isEmpty()) {
		return;
	}

	// An image pasted twice keeps its bytes while any copy is retained.
	QSet< QUrl > stillReferenced;
	collectImageNamesInRange(this, 0, qMax(0, characterCount() - 1), stillReferenced);

	for (const QUrl &name : candidates) {
		if (stillReferenced.contains(name)) {
			continue;
		}
		if (!m_releasedImageNames.contains(name)) {
			m_releasedImageNames.insert(name);
			m_staleResourceKeyBytes += resourceKeyBytes(name);
		}
		addResource(QTextDocument::ImageResource, name, placeholderImage());
	}
}

void LogDocument::setResourceKeyBudget(int bytes) {
	m_resourceKeyBudget = bytes;
	if (compactionDue()) {
		emit compactionRecommended();
	}
}

void LogDocument::rehydrateImagesInRange(int from, int to) {
	if (m_releasedImageNames.isEmpty()) {
		return;
	}

	QSet< QUrl > names;
	collectImageNamesInRange(this, from, to, names);

	for (const QUrl &name : names) {
		if (!m_releasedImageNames.contains(name)) {
			continue;
		}
		// resource() would keep serving the cached placeholder without ever
		// calling loadResource() again - the re-decode is driven from here,
		// from the self-contained data: URL.
		const QImage img = decodeDataUrlImage(name);
		if (img.isNull()) {
			continue;
		}
		addResource(QTextDocument::ImageResource, name, img);
		m_releasedImageNames.remove(name);
		m_staleResourceKeyBytes = qMax(0, m_staleResourceKeyBytes - resourceKeyBytes(name));
	}
}

void LogDocument::onContentsChange(int, int removed, int) {
	if (m_inInternalMutation) {
		return;
	}

	// A wholesale clear leaves the empty one-block document: drop the
	// eviction bookkeeping. (The resource cache itself cannot be purged
	// without swapping the document - the owning view's clearConsole()
	// does exactly that.)
	if (removed > 0 && rootFrame()->childFrames().isEmpty() && characterCount() <= 1) {
		m_releasedImageNames.clear();
		m_staleResourceKeyBytes = 0;
	}
}

std::set< Log::MsgType > NotificationSoundBlocker::s_blockedNotificationSounds;

NotificationSoundBlocker::NotificationSoundBlocker(Log::MsgType msgType) : m_msgType(msgType) {
	s_blockedNotificationSounds.insert(m_msgType);
}

NotificationSoundBlocker::~NotificationSoundBlocker() {
	s_blockedNotificationSounds.erase(m_msgType);
}

// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_LOG_H_
#define MUMBLE_MUMBLE_LOG_H_

#include <functional>
#include <set>

#include <QDateTime>
#include <QSystemTrayIcon>
#include <QtCore/QMutex>
#include <QtCore/QSet>
#include <QtCore/QUrl>
#include <QtCore/QVector>
#include <QtGui/QImage>
#include <QtGui/QTextCursor>
#include <QtGui/QTextDocument>

#include "ConfigDialog.h"
#include "ui_Log.h"

#ifndef USE_NO_TTS
class TextToSpeech;
#endif

class ConsoleSink;

/// Routing metadata a chat-destination entry carries through Log.
///
/// Owned value data end to end: the queued thread-bounce delivery and the
/// deferred startup queue hold their own copies, so the producer's locals
/// may die at any time - nothing in the metadata path is a pointer.
/// Producers fill this at PRODUCTION time; the epoch especially belongs to
/// the connection the message was produced under, never to whichever
/// connection happens to be current when a queued entry is delivered.
/// (Namespace scope like LogMessage: a nested ChatRoutingInfo cannot
/// serve as a default argument inside class Log - its default member
/// initializers only complete with the enclosing class.)
struct ChatRoutingInfo {
	/// Whether routing metadata was attached at all. Chat entries without
	/// any still render and store (they match the All filter), but they
	/// match no id-based filter.
	bool present = false;
	/// Sender's session id and plain name (empty for the server).
	unsigned int senderSession = 0;
	QString senderName;
	/// Direct (private) recipient sessions.
	QVector< unsigned int > directSessions;
	/// Channel ids a channel-scoped send targets.
	QVector< unsigned int > channelIds;
	/// Root channel ids a tree-scoped send targets.
	QVector< unsigned int > treeIds;
	/// Connection epoch captured at production time (see
	/// ConnectionEpoch). Only meaningful together with present.
	quint64 epoch = 0;
};

class LogConfig : public ConfigWidget, public Ui::LogConfig {
private:
	Q_OBJECT
	Q_DISABLE_COPY(LogConfig)

	QTreeWidgetItem *allMessagesItem;

protected:
	void updateSelectAllButtons();

public:
	/// The unique name of this ConfigWidget
	static const QString name;
	enum Column {
		ColMessage,
		ColConsole,
		ColNotification,
		ColHighlight,
		ColTTS,
		ColMessageLimit,
		ColStaticSound,
		ColStaticSoundPath
	};
	LogConfig(Settings &st);
	QString title() const Q_DECL_OVERRIDE;
	const QString &getName() const Q_DECL_OVERRIDE;
	QIcon icon() const Q_DECL_OVERRIDE;
public slots:
	void accept() const Q_DECL_OVERRIDE;
	void save() const Q_DECL_OVERRIDE;
	void load(const Settings &) Q_DECL_OVERRIDE;

	void on_qtwMessages_itemChanged(QTreeWidgetItem *, int);
	void on_qtwMessages_itemClicked(QTreeWidgetItem *, int);
	void on_qtwMessages_itemDoubleClicked(QTreeWidgetItem *, int);
	void browseForAudioFile();

	void on_qsNotificationVolume_valueChanged(int value);
	void on_qsCueVolume_valueChanged(int value);
	void on_qsTTSVolume_valueChanged(int value);
	void on_qsbNotificationVolume_valueChanged(int value);
	void on_qsbCueVolume_valueChanged(int value);
	void on_qsbTTSVolume_valueChanged(int value);
};

class ClientUser;
class Channel;
class LogMessage;

class Log : public QObject {
	friend class LogConfig;

private:
	Q_OBJECT
	Q_DISABLE_COPY(Log)
public:
	enum MsgType {
		DebugInfo,
		CriticalError,
		Warning,
		Information,
		ServerConnected,
		ServerDisconnected,
		UserJoin,
		UserLeave,
		Recording,
		YouKicked,
		UserKicked,
		SelfMute,
		OtherSelfMute,
		YouMuted,
		YouMutedOther,
		OtherMutedOther,
		ChannelJoin,
		ChannelLeave,
		PermissionDenied,
		TextMessage,
		SelfUnmute,
		SelfDeaf,
		SelfUndeaf,
		UserRenamed,
		SelfChannelJoin,
		SelfChannelJoinOther,
		ChannelJoinConnect,
		ChannelLeaveDisconnect,
		PrivateTextMessage,
		ChannelListeningAdd,
		ChannelListeningRemove,
		PluginMessage
	};

	enum LogColorType { Time, Server, Privilege, Source, Target };
	static const MsgType firstMsgType = DebugInfo;
	static const MsgType lastMsgType  = ChannelListeningRemove;

	// Display order in settingsscreen, allows to insert new events without breaking config-compatibility with older
	// versions.
	static const MsgType msgOrder[];

protected:
	/// Mutex for qvDeferredLogs
	static QMutex qmDeferredLogs;
	/// A vector containing deferred log messages
	static QVector< LogMessage > qvDeferredLogs;

	QHash< MsgType, int > qmIgnore;
	static const char *msgNames[];
	static const char *colorClasses[];
#ifndef USE_NO_TTS
	TextToSpeech *tts;
#endif
	unsigned int uiLastId;
	static const QStringList allowedSchemes();

public:
	Log(QObject *p = nullptr);
	QString msgName(MsgType t) const;
	void setIgnore(MsgType t, int ignore = 1 << 30);
	void clearIgnore();
	/// Attaches (or detaches, passing nullptr) the console sink that accepted
	/// entries are routed to. Safe to call repeatedly. The production sink is
	/// the MainWindow; it registers after Log exists (before the deferred
	/// startup logs replay) and detaches as its destructor's first statement
	/// because Log outlives the window.
	void setConsoleSink(ConsoleSink *sink);
	static QString validHtml(const QString &html, QTextCursor *tc = nullptr);
	static QString imageToImg(const QByteArray &format, const QByteArray &image);
	static QString imageToImg(QImage img, int maxSize = 0);
	static QString msgColor(const QString &text, LogColorType t);
	static QString formatClientUser(ClientUser *cu, LogColorType t, const QString &displayName = QString());
	static QString formatChannel(::Channel *c);
	/// Either defers the LogMessage or defers it, depending on whether Global::l is created already
	/// (if it is, it is used to directly log the msg)
	static void logOrDefer(Log::MsgType mt, const QString &console, const QString &terse = QString(),
						   bool ownMessage = false, const QString &overrideTTS = QString(), bool ignoreTTS = false,
						   const ChatRoutingInfo &routing = ChatRoutingInfo());
public slots:
	// We have to explicitly use Log::MsgType and not only MsgType in order to be able to use QMetaObject::invokeMethod
	// with this function.
	void log(Log::MsgType mt, const QString &console, const QString &terse = QString(), bool ownMessage = false,
			 const QString &overrideTTS = QString(), bool ignoreTTS = false,
			 const ChatRoutingInfo &routing = ChatRoutingInfo());
	/// Logs LogMessages that have been deferred so far
	void processDeferredLogs();

signals:
	/// Signal emitted when there was a message received whose type was configured to spawn a notification
	void notificationSpawned(QString title, QString body, QSystemTrayIcon::MessageIcon icon);

	/// Signal emitted when there was a message received whose type was configured to highlight the application
	void highlightSpawned();

private:
	/// The presentation boundary accepted console entries are routed through
	/// (see class ConsoleSink below). Null until the sink registers.
	ConsoleSink *m_consoleSink = nullptr;
};

/// Presentation boundary for console output (new-UI architecture 4.3): Log
/// owns every piece of policy (ignore counters, per-type output flags,
/// highlight/balloon/sound/TTS decisions); accepted console entries are
/// handed to the sink, which owns routing to the destination views. MainWindow
/// implements this; tests inject a stub to observe the Log side of the
/// contract without a window. The sink must outlive its registration or
/// detach itself before dying.
class ConsoleSink {
public:
	virtual ~ConsoleSink() = default;

	/// Renders one accepted console entry (past the ignore counter and the
	/// per-type console flag) into its destination view. The per-entry
	/// presentation settings are snapshotted by the implementer. \p routing is
	/// the producer-captured chat metadata (empty for non-chat entries); the
	/// implementer copies whatever it keeps - the reference dies with this
	/// call.
	virtual void appendConsoleEntry(Log::MsgType mt, const QString &console, bool ownMessage, const QDateTime &dt,
									const ChatRoutingInfo &routing) = 0;

	/// Whether entries of this type are currently visible to the user in
	/// their routed destination. Gates highlight and balloon only - never
	/// sound or TTS.
	virtual bool isConsoleEntryVisible(Log::MsgType mt) const = 0;
};

class LogMessage {
public:
	Log::MsgType mt;
	QString console;
	QString terse;
	bool ownMessage;
	QString overrideTTS;
	bool ignoreTTS;
	/// Producer-captured routing metadata for chat entries (a copy: deferred
	/// startup logs may outlive their producer by minutes).
	ChatRoutingInfo routing;

	LogMessage() = default;
	LogMessage(Log::MsgType mt, const QString &console, const QString &terse, bool ownMessage,
			   const QString &overrideTTS, bool ignoreTTS, const ChatRoutingInfo &routing = ChatRoutingInfo());
};

/// A QTextDocument specialized for log consoles: it only ever loads data:
/// images, and it enforces a whole-entry block budget.
///
/// Qt's own QTextDocument::setMaximumBlockCount() trims at BLOCK granularity
/// from the front of the document. With frame-based entries (each entry is a
/// child QTextFrame of the root frame) that cuts frames in half and leaves
/// orphan frame-delimiter characters behind - documented as undefined
/// behavior, and corrupting in practice. This class therefore never uses
/// setMaximumBlockCount and instead removes the oldest COMPLETE entries
/// (whole child frames plus their trailing separator blocks) through one
/// frame-contained cursor removal.
class LogDocument : public QTextDocument {
private:
	Q_OBJECT
	Q_DISABLE_COPY(LogDocument)

public:
	/// Custom QTextFrameFormat property that tags a child frame of the root
	/// frame as a message frame or a date-separator frame (set by
	/// LogConsoleView::appendEntry). rootFrame()->childFrames() contains both
	/// kinds, so the budget accounting cannot treat the child-frame list as
	/// the entry list without this tag.
	static const int FrameRoleProperty = QTextFormat::UserProperty;
	enum FrameRole {
		NoFrameRole   = 0,
		DateFrameRole = 1,
		/// A frame carrying one console entry.
		MessageFrameRole = 2,
	};

	LogDocument(QObject *p = nullptr);
	QVariant loadResource(int, const QUrl &) Q_DECL_OVERRIDE;

	// Whole-entry block budget ----------------------------------------------

	/// Sets the maximum number of blocks the document may hold. Blocks are
	/// counted by direct iteration (totalBlockCount()), which reliably
	/// includes the frame delimiter pseudo-blocks. A budget <= 0 disables
	/// trimming entirely (the default; identical to the former unlimited
	/// growth).
	void setBlockBudget(int budget);
	int blockBudget() const { return m_blockBudget; }
	/// The number of blocks, counted by manual QTextBlock iteration.
	int totalBlockCount() const;
	/// The number of retained message entries. Date-separator frames never
	/// count as entries.
	int entryCount() const;
	/// Trims the oldest complete entries - whole message frames, their
	/// trailing separator blocks, and date frames whose date no longer labels
	/// any retained message - until the budget is met. The newest entry is
	/// always kept (a single oversized entry may overshoot the budget).
	void enforceBlockBudget();
	/// Removes the \p count oldest complete entries (same frame and separator
	/// discipline as enforceBlockBudget; emits entriesTrimmed with the number
	/// of removed message entries). The store-driven chat view uses this to
	/// keep its document in lockstep with the stream store's own eviction.
	/// Returns the number of message entries actually removed.
	int removeLeadingEntries(int count);
	/// Overwrites the resource for \p name with the shared 1x1 placeholder
	/// and records the name's key as stale. Qt has no API to remove a
	/// resource-cache entry, so the KEY stays until the owning view compacts
	/// into a fresh document (compactionRecommended).
	void releaseImageResource(const QUrl &name);

	// Stale resource-key accounting -------------------------------------------

	/// Sets the accumulated stale resource-key byte budget. Once
	/// staleResourceKeyBytes() exceeds it, compactionRecommended is emitted.
	/// A budget <= 0 disables the recommendation (the stale bytes are still
	/// tracked). Defaults to 1 MiB.
	void setResourceKeyBudget(int bytes);
	int resourceKeyBudget() const { return m_resourceKeyBudget; }
	int staleResourceKeyBytes() const { return m_staleResourceKeyBytes; }
	/// Whether the stale resource-key bytes currently exceed the budget.
	/// Checked by the owning view AFTER finishing an append (compacting from
	/// inside the append would invalidate the document mid-use).
	bool compactionDue() const { return m_resourceKeyBudget > 0 && m_staleResourceKeyBytes > m_resourceKeyBudget; }
	/// Re-decodes data: images inside the position range [\p from, \p to]
	/// whose resource slot currently holds the eviction placeholder. data:
	/// URLs are self-contained, so a re-inserted reference renders at full
	/// size again. Used by the renderer after inserting an entry.
	void rehydrateImagesInRange(int from, int to);

signals:
	/// Emitted after enforceBlockBudget() removed \p count message entries
	/// (date frames removed alongside do not count).
	void entriesTrimmed(int count);
	/// Emitted when the resource-key budget is set below already-accumulated
	/// stale bytes. During eviction the view polls compactionDue() itself
	/// after finishing the append (compacting from inside the append would
	/// invalidate the document mid-use); it should compact into a fresh
	/// document in response.
	void compactionRecommended();

private slots:
	void onContentsChange(int from, int removed, int added);

private:
	static const QImage &placeholderImage();
	/// Decodes a data: image URL produced by Log::imageToImg (percent-encoded
	/// base64 payload, possibly wrapped). Returns a null image on failure.
	QImage decodeDataUrlImage(const QUrl &url) const;
	/// Overwrites the resources of all candidate names that no longer have a
	/// reference anywhere in the document (an image pasted twice keeps its
	/// bytes while any copy is retained).
	void releaseUnreferencedImageResources(const QSet< QUrl > &candidates);
	/// Shared trimming core of enforceBlockBudget() and
	/// removeLeadingEntries(): removes leading whole frames while \p shouldTrim
	/// (called with the number of message entries removed so far) returns
	/// true, always keeping the newest entry, then drops obsolete leading date
	/// separators and emits entriesTrimmed. Returns the number of removed
	/// message entries.
	int trimInternal(const std::function< bool(int) > &shouldTrim);

	int m_blockBudget           = 0;
	int m_resourceKeyBudget     = 1024 * 1024;
	int m_staleResourceKeyBytes = 0;
	/// Names whose resource slot was replaced by the placeholder.
	QSet< QUrl > m_releasedImageNames;
	/// Guards re-entrancy while our own trimming mutates the document.
	bool m_inInternalMutation = false;
};

class NotificationSoundBlocker {
public:
	friend class Log;
	NotificationSoundBlocker(Log::MsgType msgType);
	~NotificationSoundBlocker();

private:
	static std::set< Log::MsgType > s_blockedNotificationSounds;
	const Log::MsgType m_msgType;
};

Q_DECLARE_METATYPE(Log::MsgType)
Q_DECLARE_METATYPE(ChatRoutingInfo)

#endif

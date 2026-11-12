// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MAINWINDOW_H_
#define MUMBLE_MUMBLE_MAINWINDOW_H_

#include <QtCore/QPointer>
#include <QtCore/QtGlobal>
#include <QtGui/QImage>
#include <QtNetwork/QAbstractSocket>
#include <QtWidgets/QMainWindow>

#include "ChatStreamModel.h"
#include "ChatStreamPresenter.h"
#include "ChatTarget.h"
#include "ConsoleUnreadTracker.h"
#include "CustomElements.h"
#include "Log.h"
#include "MUComboBox.h"
#include "Mumble.pb.h"
#include "MumbleProtocol.h"
#include "QtUtils.h"
#include "Usage.h"
#include "UserLocalNicknameDialog.h"
#include "widgets/ChatPanel.h"

#include <memory>
#include <optional>
#include <stack>

#include "ui_MainWindow.h"

#define MB_QEVENT (QEvent::User + 939)
#define OU_QEVENT (QEvent::User + 940)

class ACLEditor;
class BanEditor;
class UserEdit;
class ServerHandler;
class GlobalShortcut;
class TextToSpeech;
class UserModel;
class Tokens;
class Channel;
class UserInformation;
class VoiceRecorderDialog;
class PositionalAudioViewer;
class PTTButtonWidget;

namespace Search {
class SearchDialog;
}

class MenuLabel;
class ListenerVolumeSlider;
class UserLocalVolumeSlider;
class ConnectionHeader;
class LogConsoleView;
class MainWindowShell;
class VoiceControlBar;
class UiStatePersistence;

struct ShortcutTarget;

struct ContextMenuTarget {
	ClientUser *user = nullptr;
	Channel *channel = nullptr;
};

class MessageBoxEvent : public QEvent {
public:
	QString msg;
	MessageBoxEvent(QString msg);
};

class OpenURLEvent : public QEvent {
public:
	QUrl url;
	OpenURLEvent(QUrl url);
};

class MainWindow : public QMainWindow, public Ui::MainWindow, public ConsoleSink {
	friend class UserModel;

private:
	Q_OBJECT
	Q_DISABLE_COPY(MainWindow)
public:
	UserModel *pmModel;
	QMenu *qmUser;
	QMenu *qmChannel;
	QMenu *qmListener;
	QMenu *qmDeveloper;
	QIcon qiIcon, qiIconMutePushToMute, qiIconMuteSelf, qiIconMuteServer, qiIconDeafSelf, qiIconDeafServer,
		qiIconMuteSuppressed;
	QIcon qiTalkingOn, qiTalkingWhisper, qiTalkingShout, qiTalkingOff;
	QIcon m_iconInformation;
	std::unordered_map< unsigned int, qt_unique_ptr< UserLocalNicknameDialog > > qmUserNicknameTracker;

	/// "Action" for when there are no actions available
	QAction *qaEmpty;

	GlobalShortcut *gsPushTalk, *gsResetAudio, *gsMuteSelf, *gsDeafSelf;
	GlobalShortcut *gsUnlink, *gsPushMute, *gsJoinChannel;
#ifdef USE_OVERLAY
	GlobalShortcut *gsToggleOverlay;
#endif
	GlobalShortcut *gsMinimal, *gsVolumeUp, *gsVolumeDown, *gsWhisper, *gsLinkChannel, *gsListenChannel;
	GlobalShortcut *gsCycleTransmitMode, *gsToggleMainWindowVisibility, *gsTransmitModePushToTalk,
		*gsTransmitModeContinuous, *gsTransmitModeVAD;
	GlobalShortcut *gsSendTextMessage, *gsSendClipboardTextMessage;
	GlobalShortcut *gsToggleTalkingUI;
	GlobalShortcut *gsToggleSearch;
	GlobalShortcut *gsServerConnect, *gsServerDisconnect, *gsServerInformation, *gsServerTokens;
	GlobalShortcut *gsServerUserList, *gsServerBanList;
	GlobalShortcut *gsSelfPrioritySpeaker;
	GlobalShortcut *gsRecording;
	GlobalShortcut *gsSelfComment, *gsServerTexture, *gsServerTextureRemove;
	GlobalShortcut *gsSelfRegister, *gsAudioStats;
	GlobalShortcut *gsConfigDialog, *gsAudioWizard, *gsConfigCert;
	GlobalShortcut *gsAudioTTS;
	GlobalShortcut *gsHelpAbout, *gsHelpAboutQt, *gsHelpVersionCheck;
	GlobalShortcut *gsTogglePositionalAudio;
	GlobalShortcut *gsMoveBack;
	GlobalShortcut *gsCycleListenerAttenuationMode, *gsListenerAttenuationUp, *gsListenerAttenuationDown;
	GlobalShortcut *gsAdaptivePush;

	/// The redesigned central-widget shell owning the presentation layout.
	/// The dock members qdwChat/qdwMinimalViewNote are deleted during setupGui
	/// and their pointers are nulled; only the adopted content widgets
	/// (qtvUsers, qteChat and the two console views below) keep living inside
	/// the shell.
	MainWindowShell *m_shell = nullptr;

	/// The two console destinations, code-created during setupGui (the legacy
	/// combined log dock is gone from the .ui). Chat messages render into
	/// m_chatConsole, everything else into m_activityConsole; MainWindow is
	/// Log's ConsoleSink and does the routing in appendConsoleEntry(). Owned
	/// through the shell after adoption.
	LogConsoleView *m_chatConsole     = nullptr;
	LogConsoleView *m_activityConsole = nullptr;

	/// Unread bookkeeping for the ACTIVITY tab badge: entries appended while
	/// the activity view was not visible. Cleared on the view's actual Show
	/// event (see eventFilter) and folded back onto the document's retained
	/// entries whenever eviction or a clear removes some of them (see
	/// onConsoleEntriesTrimmed): the badge never counts entries the document
	/// no longer holds. The chat badge is derived from the stream store's
	/// seen flags instead (m_chatPresenter).
	ConsoleUnreadTracker m_activityUnseen;

	/// The process-local chat stream (every accepted chat entry, capped by
	/// iMaxLogBlocks read as an entry budget) and its projection onto
	/// m_chatConsole (filter, selected channel, lockstep eviction,
	/// store-aware Clear, seen-derived badges). Created in setupGui.
	ChatStreamModel *m_chatStream        = nullptr;
	ChatStreamPresenter *m_chatPresenter = nullptr;

	/// Connection status strip inside the shell's header container (normal
	/// mode only). Created during setupGui; owned through the shell.
	ConnectionHeader *m_header = nullptr;

	/// Persistent bottom control strip (normal AND minimal mode). Created
	/// during setupGui; owned through the shell. Presentation only: the bar
	/// binds the existing audio/config actions and displays snapshots pushed
	/// by updateVoiceControlStatus(); it never derives state itself.
	VoiceControlBar *m_voiceBar = nullptr;

	/// Fork-side UI persistence policy (sidecar store + guarded restore/capture
	/// protocol). Owns no widgets; MainWindow translates between it and the
	/// shell. Null until setupGui.
	std::unique_ptr< UiStatePersistence > m_uiStatePersistence;

	ACLEditor *aclEdit;
	BanEditor *banEdit;
	UserEdit *userEdit;
	Tokens *tokenEdit;

	VoiceRecorderDialog *voiceRecorderDialog;

	MumbleProto::Reject_RejectType rtLast;
	bool bRetryServer;
	QString qsDesiredChannel;

	bool forceQuit;
	/// Restart the client after shutdown
	bool restartOnQuit;

	/// Contains the cursor whose position is immediately before the image to
	/// save when activating the "Save Image As..." context menu item, together
	/// with the document of the log view the selection was made in. Both
	/// halves are captured atomically while the context menu is built;
	/// pendingLogImage() is the only reader.
	QTextCursor qtcSaveImageCursor;
	QPointer< QTextDocument > qpdSaveImageDocument;

	QPointer< Channel > cContextChannel;
	QPointer< ClientUser > cuContextUser;

	QPoint qpContextPosition;

	void recheckTTS();
	void msgBox(QString msg);
	void setOnTop(bool top);
	void updateAudioToolTips();
	/// Shows or hides the floating push-to-talk button window according to
	/// the current settings (bShowPTTButtonWindow and PTT transmit mode).
	/// Runs during window setup, on settings acceptance (through setupView)
	/// and on every transmission-mode change, so mode switches through the
	/// combo box, global shortcuts or the API keep the floating window in
	/// sync without re-deriving any window layout.
	void updatePTTButtonWindow();
	void updateUserModel();
	void focusNextMainWidget();
	QPair< QByteArray, QImage > openImageFile();

	/// ConsoleSink: renders one accepted console entry into its routed
	/// destination (chat messages vs. everything else) and tracks the unread
	/// badge state for entries that arrive while their destination is hidden.
	/// Chat entries additionally enter the stream store together with their
	/// routing metadata (HTML and metadata arrive as one accepted entry or
	/// not at all - everything with a veto ran inside Log already).
	void appendConsoleEntry(Log::MsgType mt, const QString &console, bool ownMessage, const QDateTime &dt,
							const ChatRoutingInfo &routing) Q_DECL_OVERRIDE;

	/// ConsoleSink: whether a console entry of the given type is currently
	/// visible to the user. This is the single presentation boundary used by
	/// Log to decide notification suppression: the active main window plus the
	/// visibility of the entry's routed destination only - a chat entry is
	/// never "visible" merely because the activity view is on screen, and
	/// vice versa. Never gates sound or TTS.
	bool isConsoleEntryVisible(Log::MsgType mt) const Q_DECL_OVERRIDE;

	/// Pushes the current theme style sheet into every console document
	/// (Themes::apply and setupGui call this; the old direct
	/// document()->setDefaultStyleSheet() call sites are gone).
	void setConsoleStyleSheet(const QString &sheet);

	/// Pushes the configured block limit into the activity document (block
	/// budget) and the chat stream store (entry budget; the chat document is
	/// bounded in lockstep by the store instead). setupGui and
	/// LogConfig::accept call this.
	void applyConsoleBlockLimit();

	/// Refreshes the tab badges (activity from the tracker, chat and the
	/// filter badges from the stream store's seen flags) and the Selected
	/// Channel availability.
	void updateConsoleBadges();

	/// The source document lost entries (eviction or a wholesale clear):
	/// folds the unread bookkeeping onto what the document still retains.
	/// Activity only - the chat view's bookkeeping lives in the store.
	void onConsoleEntriesTrimmed(LogConsoleView *source);

	/// The chat filter selection changed (ChatPanel row): forwards to the
	/// presenter and refreshes badges.
	void onChatFilterChanged(ChatPanel::ChatFilter filter);

	/// Anything in the chat stream or its projection changed: refreshes the
	/// chat-derived badges.
	void onChatStreamStateChanged();

	/// The tree selection moved (qtvUserCurrentChanged): while the Selected
	/// Channel filter is active it tracks the selection (listener rows
	/// resolve to the listened channel). Selecting never activates the
	/// filter.
	void updateChatFilterSelection();

	/// A channel was removed: if the Selected Channel filter tracked it, the
	/// filter falls back to All (the store and its scrolls survive).
	void onChatChannelRemoved(unsigned int channelId);

	/// Whether \p channelId lies inside the subtree rooted at \p rootId, over
	/// the live channel tree (Selected Channel matching for tree-targeted
	/// chat entries). Null-safe on both ends.
	bool chatTreeCovers(unsigned int rootId, unsigned int channelId) const;

	/// Routing metadata for own-message echoes: the send path knows exactly
	/// what it targeted (channel/tree/direct). The epoch is captured HERE,
	/// at production time - never at delivery time.
	ChatRoutingInfo chatRoutingForChannel(unsigned int channelId, bool treeScoped) const;
	ChatRoutingInfo chatRoutingForDirect(unsigned int session) const;

	/// Ctrl+Shift+L: switches the active console tab.
	void toggleConsoleTab();

	/// Current minimal-view mode of the fork's window presentation. This is the
	/// single source of truth for code outside MainWindow (e.g. the overlay);
	/// the shared Settings field is never written by the fork.
	bool isMinimalView() const;
	/// Current frameless (hide-frame) mode of the fork's window presentation.
	bool isHideFrame() const;

	void loadState(bool minimalView);
	void storeState(bool minimalView);

	/// Resolves the composer's send target (placeholder, send routing and
	/// enablement all go through this one decision table; see ChatTarget).
	ChatTarget currentChatTarget() const;
	void updateChatBar();
	void openTextMessageDialog(ClientUser *p);
	void openUserLocalNicknameDialog(const ClientUser &p);

#ifdef Q_OS_WIN
	bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) Q_DECL_OVERRIDE;
	unsigned int uiNewHardware;
#endif
protected:
	Usage uUsage;
	QTimer *qtReconnect;
	std::unique_ptr< NotificationSoundBlocker > m_reconnectSoundBlocker;

	QList< QAction * > qlServerActions;
	QList< QAction * > qlChannelActions;
	QList< QAction * > qlUserActions;

	QHash< ShortcutTarget, int > qmCurrentTargets;
	/// A map that contains information about the currently active
	/// shout/whisper targets. The mapping is between a List of
	/// ShortcutTargets that are all triggered together and the
	/// target ID for this specific combination of ShortcutTargets.
	/// The target ID is what the server uses to identify this specific
	/// set of ShortcutTargets.
	QHash< QList< ShortcutTarget >, int > qmTargets;
	/// This is a map between all target IDs the client will ever use
	/// and a helper-number (see iTargetCounter).
	QMap< int, int > qmTargetUse;
	Channel *mapChannel(int idx) const;
	/// This is a pure helper number whose job is to always be increased
	/// if a new VoiceTarget is needed. It will be used as the helper
	/// number in qmTargetUse.
	int iTargetCounter;
	QMap< unsigned int, UserInformation * > qmUserInformations;

	std::unique_ptr< PositionalAudioViewer > m_paViewer;

	PTTButtonWidget *qwPTTButtonWidget;

	/// Non-owning: the transmission-mode selector created inside the voice
	/// control bar (never reparented); MainWindow fills its items and owns
	/// the activation handling. Same lifetime discipline as m_header.
	MUComboBox *qcbTransmitMode;

	Search::SearchDialog *m_searchDialog = nullptr;

	qt_unique_ptr< MenuLabel > m_localVolumeLabel;
	qt_unique_ptr< UserLocalVolumeSlider > m_userLocalVolumeSlider;
	qt_unique_ptr< ListenerVolumeSlider > m_listenerVolumeSlider;

	std::stack< unsigned int > m_previousChannels;
	std::optional< unsigned int > m_movedBackFromChannel;

	void createActions();
	void setupGui();
	void updateWindowTitle();
	/// Re-derives the connection snapshot (state, server description, user
	/// and channel counts) from the live connection state and pushes it to
	/// the header. Called at the connection lifecycle points and on every
	/// UserModel user/channel addition/removal (the model emits after the
	/// underlying collections have been updated, so counts are fresh);
	/// counts are resolved at push time, never cached.
	void updateConnectionHeader();

	/// Pushes the Connecting snapshot at the moment a connection attempt
	/// starts. The tri-state cannot be derived there yet (the freshly
	/// created server handler's thread may not report running until after
	/// start() returns), so the state is pushed explicitly; the server
	/// description is read from the already-configured handler.
	void showConnectionAttemptInHeader();

	/// Re-derives the local user's identity and voice-status snapshot and
	/// pushes it to the voice control bar. The ClientUser is resolved fresh
	/// on every call (never cached); a null self user (not synchronized,
	/// disconnected) yields the cleared snapshot. Connected to
	/// talkingStatusChanged (local talking/mute/deaf/priority/recording
	/// changes, self mute/deaf toggles, push-to-mute, config acceptance,
	/// self rename) and to disconnectedFromServer.
	void updateVoiceControlStatus();

	/// Shared snapshot filler. \p newChannel, when set, overrides the
	/// channel name with the channel resolved from UserModel::userMoved's
	/// newChannelID argument: that signal is emitted BEFORE
	/// ClientUser::cChannel is re-pointed, so reading the self user there
	/// would show the previous channel (same consumption pattern as the
	/// TalkingUI's userMoved handling). Self mute/deaf and push-to-mute are
	/// folded in only while a self user exists; with none (disconnected, or
	/// before synchronization) the bar receives the fully cleared snapshot -
	/// the buttons keep their action-driven state, the status presentation
	/// does not.
	void pushVoiceSnapshot(const Channel *newChannel);

	/// Re-derives the settings-dependent presentation (mode layout, transmit
	/// mode combo, PTT button window, model and action/tooltip state) after
	/// settings were accepted. Connected to ConfigDialog::settingsAccepted,
	/// so Apply refreshes the live UI without closing the dialog and OK (and
	/// Reset/Restore Defaults followed by either) refresh through the same
	/// single path. Contains no dialog-closing concerns (activation, restart
	/// prompt stay with the OK path) and never emits transmissionModeChanged:
	/// the combo is only re-synchronized to the settings value.
	void refreshUiAfterSettingsAccepted();
	void updateFavoriteButton();
	void customEvent(QEvent *evt) Q_DECL_OVERRIDE;
	void findDesiredChannel();
	void setupView(bool toggle_minimize = true);
	void closeEvent(QCloseEvent *e) Q_DECL_OVERRIDE;
	void hideEvent(QHideEvent *e) Q_DECL_OVERRIDE;
	void showEvent(QShowEvent *e) Q_DECL_OVERRIDE;
	void changeEvent(QEvent *e) Q_DECL_OVERRIDE;
	void keyPressEvent(QKeyEvent *e) Q_DECL_OVERRIDE;
	/// Watches the console views for actual Show events: a destination that
	/// comes on screen has its unread badge (and the activity warning state)
	/// cleared - the entries are visible now.
	bool eventFilter(QObject *watched, QEvent *event) Q_DECL_OVERRIDE;

	QMenu *createPopupMenu() Q_DECL_OVERRIDE;

	bool handleSpecialContextMenu(const QUrl &url, const QPoint &pos_, bool focus = false);
	Channel *getContextMenuChannel();
	ClientUser *getContextMenuUser();
	ContextMenuTarget getContextMenuTargets();

	void autocompleteUsername();

public slots:
	void on_qmServer_aboutToShow();
	void on_qaServerConnect_triggered(bool autoconnect = false);
	void on_qaServerDisconnect_triggered();
	void on_qaServerBanList_triggered();
	void on_qaServerUserList_triggered();
	void on_qaServerInformation_triggered();
	void on_qaServerTexture_triggered();
	void on_qaServerTextureRemove_triggered();
	void on_qaServerTokens_triggered();
	void on_qmSelf_aboutToShow();
	void on_qaSelfComment_triggered();
	void on_qaSelfRegister_triggered();
	void qcbTransmitMode_activated(int index);
	void updateTransmitModeComboBox(Settings::AudioTransmit newMode);
	void qmUser_aboutToShow();
	void qmListener_aboutToShow();
	void on_qaUserCommentReset_triggered();
	void on_qaUserTextureReset_triggered();
	void on_qaUserCommentView_triggered();
	void on_qaUserKick_triggered();
	void on_qaUserBan_triggered();
	void on_qaUserMute_triggered();
	void on_qaUserDeaf_triggered();
	void on_qaSelfPrioritySpeaker_triggered();
	void on_qaUserPrioritySpeaker_triggered();
	void on_qaUserLocalIgnore_triggered();
	void on_qaUserLocalIgnoreTTS_triggered();
	void on_qaUserLocalMute_triggered();
	void on_qaUserLocalNickname_triggered();
	void on_qaUserTextMessage_triggered();
	void on_qaUserRegister_triggered();
	void on_qaUserInformation_triggered();
	void on_qaUserFriendAdd_triggered();
	void on_qaUserFriendRemove_triggered();
	void on_qaUserFriendUpdate_triggered();
	void qmChannel_aboutToShow();
	void on_qaChannelJoin_triggered();
	void on_qaUserJoin_triggered();
	void on_qaUserMove_triggered();
	void on_qaChannelListen_triggered();
	void on_qaChannelAdd_triggered();
	void on_qaChannelRemove_triggered();
	void on_qaChannelACL_triggered();
	void on_qaChannelLink_triggered();
	void on_qaChannelUnlink_triggered();
	void on_qaChannelUnlinkAll_triggered();
	void on_qaChannelSendMessage_triggered();
	void on_qaChannelHide_triggered();
	void on_qaChannelPin_triggered();
	void on_qaChannelCopyURL_triggered();
	void on_qaAudioReset_triggered();
	void on_qaAudioMute_triggered();
	void on_qaAudioDeaf_triggered();
	void on_qaRecording_triggered();
	void on_qaAudioTTS_triggered();
	void on_qaAudioUnlink_triggered();
	void on_qaAudioStats_triggered();
	void on_qaConfigDialog_triggered();
	void on_qaConfigHideFrame_triggered();
	void on_qmConfig_aboutToShow();
	void on_qaConfigMinimal_triggered();
	void on_qaConfigCert_triggered();
	void on_qaAudioWizard_triggered();
	void on_qaDeveloperConsole_triggered();
	void on_qaPositionalAudioViewer_triggered();
	void on_qaHelpWhatsThis_triggered();
	void on_qaHelpAbout_triggered();
	void on_qaHelpAboutQt_triggered();
	void on_qaHelpVersionCheck_triggered();
	void on_qaQuit_triggered();
	void on_qteChat_tabPressed();
	void on_qteChat_backtabPressed();
	void on_qteChat_ctrlSpacePressed();
	void on_qtvUsers_customContextMenuRequested(const QPoint &mpos, bool usePositionForGettingContext = true);
	void on_PushToTalk_triggered(bool, QVariant);
	void on_PushToMute_triggered(bool, QVariant);
	void on_VolumeUp_triggered(bool, QVariant);
	void on_VolumeDown_triggered(bool, QVariant);
	void on_gsMuteSelf_down(QVariant);
	void on_gsDeafSelf_down(QVariant);
	void on_gsWhisper_triggered(bool, QVariant);
	void addTarget(ShortcutTarget *);
	void removeTarget(ShortcutTarget *);
	void on_gsListenChannel_triggered(bool, QVariant);
	void on_gsCycleTransmitMode_triggered(bool, QVariant);
	void on_gsToggleMainWindowVisibility_triggered(bool, QVariant);
	void on_gsTransmitModePushToTalk_triggered(bool, QVariant);
	void on_gsTransmitModeContinuous_triggered(bool, QVariant);
	void on_gsTransmitModeVAD_triggered(bool, QVariant);
	void on_gsSendTextMessage_triggered(bool, QVariant);
	void on_gsSendClipboardTextMessage_triggered(bool, QVariant);
	void on_gsToggleTalkingUI_triggered(bool, QVariant);
	void on_gsToggleSearch_triggered(bool, QVariant);
	void on_gsServerConnect_triggered(bool, QVariant);
	void on_gsServerDisconnect_triggered(bool, QVariant);
	void on_gsServerInformation_triggered(bool, QVariant);
	void on_gsServerTokens_triggered(bool, QVariant);
	void on_gsServerUserList_triggered(bool, QVariant);
	void on_gsServerBanList_triggered(bool, QVariant);
	void on_gsSelfPrioritySpeaker_triggered(bool, QVariant);
	void on_gsRecording_triggered(bool, QVariant);
	void on_gsSelfComment_triggered(bool, QVariant);
	void on_gsServerTexture_triggered(bool, QVariant);
	void on_gsServerTextureRemove_triggered(bool, QVariant);
	void on_gsSelfRegister_triggered(bool, QVariant);
	void on_gsAudioStats_triggered(bool, QVariant);
	void on_gsConfigDialog_triggered(bool, QVariant);
	void on_gsAudioWizard_triggered(bool, QVariant);
	void on_gsConfigCert_triggered(bool, QVariant);
	void on_gsAudioTTS_triggered(bool, QVariant);
	void on_gsHelpAbout_triggered(bool, QVariant);
	void on_gsHelpAboutQt_triggered(bool, QVariant);
	void on_gsHelpVersionCheck_triggered(bool, QVariant);
	void on_gsTogglePositionalAudio_triggered(bool, QVariant);
	void on_gsMoveBack_triggered(bool, QVariant);
	void on_gsCycleListenerAttenuationMode_triggered(bool, QVariant);
	void on_gsListenerAttenuationUp_triggered(bool, QVariant);
	void on_gsListenerAttenuationDown_triggered(bool, QVariant);
	void on_gsAdaptivePush_triggered(bool, QVariant);

	void on_Reconnect_timeout();
	void on_qaTalkingUIToggle_triggered();
	void voiceRecorderDialog_finished(int);
	void qtvUserCurrentChanged(const QModelIndex &, const QModelIndex &);
	void serverConnected();
	void serverDisconnected(QAbstractSocket::SocketError, QString reason);
	void resolverError(QAbstractSocket::SocketError, QString reason);
	void viewCertificate(bool);
	void openUrl(const QUrl &url);
	void context_triggered();
	void updateTarget();
	void updateMenuPermissions();
	void on_muteCuePopup_triggered();
	/// Handles state changes like talking mode changes and mute/unmute
	/// or priority speaker flag changes for the gui user
	void userStateChanged();
	void on_channelStateChanged(Channel *channel, bool forceUpdateTree);
	void destroyUserInformation();
	void sendChatbarMessage(QString msg);
	void sendChatbarText(QString msg, bool plainText = false);
	void pttReleased();
	void whisperReleased(QVariant scdata);
	void onResetAudio();
	void showRaiseWindow();
	void on_qaFilterToggle_triggered();
	/// Builds and executes the log context menu for the given source view
	/// (anchor dispatch, standard menu, image actions, Clear). The source is
	/// parameterized so every log destination gets identical interactions.
	void displayLogContextMenu(LogTextBrowser *source, const QPoint &mpos);
	/// Opens external links from any log view (internal clientid/channelid
	/// anchors are dispatched by handleSpecialContextMenu instead).
	void handleLogLinkActivated(const QUrl &url);
	/// Shows the link tooltip anchored at the given source view.
	void handleLogLinkHighlighted(LogTextBrowser *source, const QUrl &url);
	/// The image currently selected for "Save Image As..."/"Open Image",
	/// resolved from the document the selection was recorded against.
	/// Returns a null image when the selection is stale.
	QImage pendingLogImage() const;
	/// Opens a save dialog for the image referenced by qtcSaveImageCursor.
	void saveImageAs();
	/// Returns the path to the user's image directory, optionally with a
	/// filename included.
	QString getImagePath(QString filename = QString()) const;

	/// Shows a dialog with the image from the context menu as a QPixmap
	void showImageDialog();
	/// Updates the user's image directory to the given path (any included
	/// filename is discarded).
	void updateImagePath(QString filepath) const;
	void setTransmissionMode(Settings::AudioTransmit mode);
	/// Sets the local user's mute state
	///
	/// @param mute Whether to mute the user
	void setAudioMute(bool mute);
	/// Sets the local user's deaf state
	///
	/// @param deaf Whether to deafen the user
	void setAudioDeaf(bool deaf);
	// Callback the search action being triggered
	void on_qaSearch_triggered();
	void toggleSearchDialogVisibility();
	/// Enables or disables the recording feature
	void enableRecording(bool recordingAllowed);
	/// Invokes OS native window highlighting
	void highlightWindow();
	void on_user_moved(unsigned int sessionID, const std::optional< unsigned int > &prevChannelID,
					   unsigned int newChannelID);
	void on_qaMoveBack_triggered();
signals:
	/// Signal emitted when the server and the client have finished
	/// synchronizing (after a new connection).
	void serverSynchronized();
	/// Signal emitted whenever a user adds a new ChannelListener
	void userAddedChannelListener(ClientUser *user, Channel *channel);
	/// Signal emitted whenever a user removes a ChannelListener
	void userRemovedChannelListener(ClientUser *user, Channel *channel);
	void transmissionModeChanged(Settings::AudioTransmit newMode);

	/// Signal emitted when the local user changes their talking status either actively or passively
	void talkingStatusChanged();

	/// Signal when channel state has been changed
	void channelStateChanged(Channel *channel, bool forceUpdateTree);

	/// Signal emitted when the connection was terminated and all cleanup code has been run
	void disconnectedFromServer();

	/// Signal emitted when the window manager notifies the Mumble MainWindow that the application was just minimized
	void windowMinimized();
	/// Signal emitted when the user requested to toggle the MainWindow visibility
	void windowVisibilityToggled();
	/// Signal emitted whenever the Mumble MainWindow regains the active state from the window manager
	void windowActivated();

public:
	MainWindow(QWidget *parent);
	~MainWindow() Q_DECL_OVERRIDE;

	// Implementation in Messages.cpp
#define PROCESS_MUMBLE_TCP_MESSAGE(name, value) void msg##name(const MumbleProto::name &);
	MUMBLE_ALL_TCP_MESSAGES
#undef PROCESS_MUMBLE_TCP_MESSAGE
	void removeContextAction(const MumbleProto::ContextActionModify &msg);
	/// Logs a message that an action could not be saved permanently because
	/// the user has no certificate and can't be reliably identified.
	///
	/// @param actionName  The name of the action that has been executed.
	/// @param p  The user on which the action was performed.
	void logChangeNotPermanent(const QString &actionName, ClientUser *const p) const;

	void openServerConnectDialog(bool autoconnect = false);
	void disconnectFromServer();
	void addServerAsFavorite();
	void openServerInformationDialog();
	void openServerTokensDialog();
	void openServerUserListDialog();
	void openServerBanListDialog();
	void toggleSelfPrioritySpeaker();
	void recording();
	void openSelfCommentDialog();
	void changeServerTexture();
	void removeServerTexture();
	void selfRegister();
	void openAudioStatsDialog();
	void openConfigDialog();
	/// Resets the fork's window presentation to factory defaults: normal mode,
	/// framed, default splitter sizes and a centered default window rectangle.
	/// Releases the sidecar's restore latch in the same write so an interrupted
	/// restoration can never keep blocking starts. The shared settings file is
	/// not touched.
	void resetUiStateLayout();
	void openAudioWizardDialog();
	void openCertWizardDialog();
	void enableAudioTTS(bool enable);
	void openAboutDialog();
	void openAboutQtDialog();
	void versionCheck();
	void enablePositionalAudio(bool enable);
};

#endif

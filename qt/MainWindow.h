/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H


#include "core/ApplicationSettings.h"
#include "core/ChatAliases.h"
#include "core/ChatCommandParser.h"
#include "core/DownloadManager.h"
#include "core/FileUploadServer.h"
#include "core/ShareScanner.h"
#include "core/UserFilterSet.h"
#include "core/ServerConnection.h"
#include "core/ServerConnectionListener.h"

#include "platform/qt/QPostEventCallbackMechanism.h"

#include <QMainWindow>
#include <QVector>


class QAction;
class QLabel;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSortFilterProxyModel;
class QSpinBox;
class QSplitter;
class QTimer;
class QTreeView;


namespace hitux {


class ChatInputLine;
class ChatLogView;
class DesktopNotifier;
class FileResultModel;
class UserListModel;


/** The application window.
  *
  * Implements ServerConnectionListener directly.  Because the connection is
  * constructed with a QPostEventCallbackMechanism, every listener callback arrives
  * on the GUI thread, so these methods may touch widgets without any locking -- that
  * is the whole reason MUSCLE's Qt integration is worth using.
  */
class MainWindow : public QMainWindow, public ServerConnectionListener,
	public DownloadManagerListener, public FileUploadServerListener
{
	Q_OBJECT

public:
	explicit MainWindow(QWidget* parent = nullptr);
	~MainWindow() override;

	// ServerConnectionListener
	void ConnectionStateChanged(ConnectionState state) override;
	void LocalSessionIdAssigned(const muscle::String& sessionId,
		const muscle::String& hostName) override;
	void UserUpdated(const UserRecord& user, bool isNewUser) override;
	void UserLeft(const UserRecord& user) override;
	void ChatMessageReceived(const ChatMessage& message) override;
	void PingReplyReceived(const UserRecord& user, uint64 roundTripMicroseconds,
		const muscle::String& peerVersion) override;
	void QueryResultAdded(const FileResult& result) override;
	void QueryResultRemoved(const muscle::String& sessionId,
		const muscle::String& fileName) override;
	void QueryResultsCleared() override;
	void QuerySweepStateChanged(bool isSweeping) override;

	// DownloadManagerListener
	void DownloadListChanged() override;
	void DownloadChanged(uint32 index) override;
	void DownloadReport(LogMessageType type, const muscle::String& text) override;

	// FileUploadServerListener
	void UploadsChanged(FileUploadServer* server) override;
	void UploadReport(LogMessageType type, const muscle::String& text) override;

protected:
	void closeEvent(QCloseEvent* event) override;

private slots:
	void _OnConnectButtonClicked();
	void _OnInputReturnPressed();
	void _OnUserDoubleClicked(const QModelIndex& index);
	void _OnIdleTimerFired();
	void _OnDownloadSelected();
	void _OnClearFinishedTransfers();
	void _OnCancelSelectedTransfer();
	void _OnResultsDoubleClicked(const QModelIndex& index);
	void _OnQueryButtonClicked();
	void _OnQueryFieldReturnPressed();
	void _OnFlushPendingResults();
	void _OnStatusChanged();
	void _OnShowSettings();
	void _OnToggleNotifications(bool enabled);
	void _OnChooseShareFolder();
	void _OnToggleFileSharing(bool enabled);
	void _OnShowAbout();
	void _OnToggleTimestamps(bool showTimestamps);

private:
	void _BuildUserInterface();
	void _BuildMenus();
	void _LoadSettings();
	void _SaveSettings();

	void _HandleUserInput(const QString& input);
	void _SendChatToEveryone(const QString& text, bool isAction);
	void _SendPrivateMessage(const QString& target, const QString& text);
	void _PingUser(const QString& target);
	void _ShowCommandHelp();
	void _ShowConnectionInformation();

	void _ConnectToConfiguredServer();
	void _StartQuery();
	void _DownloadSelectedResults();
	void _UpdateQueryWidgets();
	void _UpdateResultCount();
	void _UpdateConnectionWidgets();
	void _PopulateServerList();
	QString _GetSelectedServerAddress() const;
	void _UpdateStatusBar();
	void _SetUserStatus(const muscle::String& status);
	void _HandleFilterCommand(UserFilterSet& filter, const muscle::String& argument,
		const QString& filterName, void (ApplicationSettings::*setter)(
			const muscle::String&));
	void _HandleAliasCommand(const ChatCommand& command);
	void _ApplySettings();
	void _StartSharing();
	void _StopSharing();
	void _DrainShareScanner();

	QStringList _GetCompletionCandidates(const QString& prefix) const;

	void _AppendLocalLine(LogMessageType type, const QString& text);
	void _MaybeNotifyAboutChat(const ChatMessage& message);
	bool _MentionsLocalUser(const muscle::String& text) const;
	bool _UserIsLookingAtUs() const;

	// Declared before fConnection so that the connection -- which registers with it
	// from MUSCLE's internal thread -- is destroyed first.
	muscle::QPostEventCallbackMechanism fCallbackMechanism;

	ApplicationSettings fSettings;
	ServerConnection fConnection;

	ChatLogView* fChatLogView;
	QTreeView* fResultsView;
	FileResultModel* fResultsModel;
	QSortFilterProxyModel* fResultsProxyModel;
	QLineEdit* fQueryField;
	QPushButton* fQueryButton;
	QLabel* fResultCountLabel;
	QSplitter* fLeftSplitter;
	QTreeView* fTransfersView;
	class TransferModel* fTransferModel;
	QPushButton* fDownloadButton;
	ChatInputLine* fChatInputLine;
	QTreeView* fUserListView;
	UserListModel* fUserListModel;
	QSortFilterProxyModel* fUserListProxyModel;
	QSplitter* fSplitter;

	QComboBox* fServerAddressBox;
	QSpinBox* fServerPortField;
	QLineEdit* fUserNameField;
	QComboBox* fUserStatusBox;
	QPushButton* fConnectButton;

	QLabel* fStatusLabel;
	QLabel* fUserCountLabel;
	QLabel* fShareLabel;

	QAction* fConnectAction;
	QAction* fDisconnectAction;
	QAction* fShowTimestampsAction;
	QAction* fShowHostColumnAction;
	QAction* fFileSharingAction;
	QAction* fNotificationsAction;
	DesktopNotifier* fNotifier;

	QTimer* fIdleTimer;

	// A bare "*" against a peer sharing twenty thousand files delivers twenty
	// thousand results in a burst.  They are buffered and flushed as one model
	// transaction rather than inserted a row at a time.
	QVector<FileResult> fPendingResults;
	QTimer* fResultFlushTimer;
	bool fUserNamesDirty;

	// Declared last so they are destroyed first: all three call back into us.
	DownloadManager fDownloads;
	ShareScanner fShareScanner;
	FileUploadServer fUploadServer;

	UserFilterSet fIgnoreFilter;
	UserFilterSet fWatchFilter;
	UserFilterSet fAutoPrivFilter;
	ChatAliases fAliases;

	muscle::Hashtable<muscle::String, SharedFile> fSharedFiles;
	uint32 fSharedFileCount;
};


}  // namespace hitux


#endif  // MAIN_WINDOW_H

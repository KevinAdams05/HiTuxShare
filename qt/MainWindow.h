/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H


#include "core/ApplicationSettings.h"
#include "core/ServerConnection.h"
#include "core/ServerConnectionListener.h"

#include "platform/qt/QPostEventCallbackMechanism.h"

#include <QMainWindow>


class QAction;
class QLabel;
class QComboBox;
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
class UserListModel;


/** The application window.
  *
  * Implements ServerConnectionListener directly.  Because the connection is
  * constructed with a QPostEventCallbackMechanism, every listener callback arrives
  * on the GUI thread, so these methods may touch widgets without any locking -- that
  * is the whole reason MUSCLE's Qt integration is worth using.
  */
class MainWindow : public QMainWindow, public ServerConnectionListener
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

protected:
	void closeEvent(QCloseEvent* event) override;

private slots:
	void _OnConnectButtonClicked();
	void _OnInputReturnPressed();
	void _OnUserDoubleClicked(const QModelIndex& index);
	void _OnIdleTimerFired();
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
	void _UpdateConnectionWidgets();
	void _PopulateServerList();
	QString _GetSelectedServerAddress() const;
	void _UpdateStatusBar();

	QStringList _GetCompletionCandidates(const QString& prefix) const;

	void _AppendLocalLine(LogMessageType type, const QString& text);

	// Declared before fConnection so that the connection -- which registers with it
	// from MUSCLE's internal thread -- is destroyed first.
	muscle::QPostEventCallbackMechanism fCallbackMechanism;

	ApplicationSettings fSettings;
	ServerConnection fConnection;

	ChatLogView* fChatLogView;
	ChatInputLine* fChatInputLine;
	QTreeView* fUserListView;
	UserListModel* fUserListModel;
	QSortFilterProxyModel* fUserListProxyModel;
	QSplitter* fSplitter;

	QComboBox* fServerAddressBox;
	QSpinBox* fServerPortField;
	QLineEdit* fUserNameField;
	QPushButton* fConnectButton;

	QLabel* fStatusLabel;
	QLabel* fUserCountLabel;

	QAction* fConnectAction;
	QAction* fDisconnectAction;
	QAction* fShowTimestampsAction;
	QAction* fShowHostColumnAction;

	QTimer* fIdleTimer;
};


}  // namespace hitux


#endif  // MAIN_WINDOW_H

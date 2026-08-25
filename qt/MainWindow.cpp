/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "qt/MainWindow.h"

#include "core/BeShareProtocol.h"
#include "core/ChatCommandParser.h"
#include "core/HiTuxShareVersion.h"
#include "qt/ChatInputLine.h"
#include "qt/ChatLogView.h"
#include "qt/FileResultModel.h"
#include "qt/QtConversions.h"
#include "qt/UserListModel.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

using namespace muscle;


namespace hitux {


namespace {


// How often we let the core do its housekeeping.  The core deliberately owns no
// timer of its own, so this is the only thing driving its keepalive.
const int kIdleTimerIntervalMilliseconds = 5000;

const char* const kSettingsFieldWindowGeometry = "qt:windowgeometry";
const char* const kSettingsFieldSplitterState = "qt:splitterstate";
const char* const kSettingsFieldShowTimestamps = "qt:showtimestamps";
const char* const kSettingsFieldShowHostColumn = "qt:showhostcolumn";


/** Stores a Qt byte array in the settings Message.
  * @param settings the message to write into
  * @param fieldName the field to write
  * @param data the bytes to store
  */
void
PutByteArray(Message& settings, const char* fieldName, const QByteArray& data)
{
	(void) settings.RemoveName(fieldName);
	(void) settings.AddData(fieldName, B_RAW_TYPE, data.constData(),
		(uint32) data.size());
}


/** Reads a Qt byte array back out of the settings Message.
  * @param settings the message to read from
  * @param fieldName the field to read
  * @returns the bytes, or an empty array if the field is absent
  */
QByteArray
GetByteArray(const Message& settings, const char* fieldName)
{
	const void* data = NULL;
	uint32 dataSize = 0;
	if (settings.FindData(fieldName, B_RAW_TYPE, &data, &dataSize).IsError())
		return QByteArray();

	return QByteArray(static_cast<const char*>(data), (int) dataSize);
}


}  // unnamed namespace


MainWindow::MainWindow(QWidget* parent)
	:
	QMainWindow(parent),
	fCallbackMechanism(this),
	fConnection(&fCallbackMechanism),
	fChatLogView(nullptr),
	fResultsView(nullptr),
	fResultsModel(nullptr),
	fResultsProxyModel(nullptr),
	fQueryField(nullptr),
	fQueryButton(nullptr),
	fResultCountLabel(nullptr),
	fLeftSplitter(nullptr),
	fChatInputLine(nullptr),
	fUserListView(nullptr),
	fUserListModel(nullptr),
	fUserListProxyModel(nullptr),
	fSplitter(nullptr),
	fServerAddressBox(nullptr),
	fServerPortField(nullptr),
	fUserNameField(nullptr),
	fConnectButton(nullptr),
	fStatusLabel(nullptr),
	fUserCountLabel(nullptr),
	fConnectAction(nullptr),
	fDisconnectAction(nullptr),
	fShowTimestampsAction(nullptr),
	fShowHostColumnAction(nullptr),
	fIdleTimer(nullptr),
	fResultFlushTimer(nullptr),
	fUserNamesDirty(false)
{
	(void) fSettings.Load();

	_BuildUserInterface();
	_BuildMenus();
	_LoadSettings();

	fConnection.SetListener(this);
	fConnection.SetInstallId(fSettings.GetInstallId());
	fConnection.SetLocalUserName(fSettings.GetUserName());
	fConnection.SetLocalUserStatus(fSettings.GetUserStatus());

	fIdleTimer = new QTimer(this);
	fIdleTimer->setInterval(kIdleTimerIntervalMilliseconds);
	connect(fIdleTimer, &QTimer::timeout, this, &MainWindow::_OnIdleTimerFired);
	fIdleTimer->start();

	// Single-shot and restarted on each burst, so a stream of results coalesces
	// into one model transaction instead of one per row.
	fResultFlushTimer = new QTimer(this);
	fResultFlushTimer->setSingleShot(true);
	fResultFlushTimer->setInterval(120);
	connect(fResultFlushTimer, &QTimer::timeout,
		this, &MainWindow::_OnFlushPendingResults);

	fResultsModel->SetUserRegistry(&fConnection.GetUsers());

	_UpdateConnectionWidgets();
	_UpdateQueryWidgets();
	_UpdateStatusBar();

	_AppendLocalLine(LOG_INFORMATION_MESSAGE,
		tr("%1 %2 -- type /help for a list of commands.")
			.arg(QLatin1String(HITUX_SHARE_NAME),
				QLatin1String(HITUX_SHARE_VERSION_STRING)));

	if (fSettings.GetConnectOnStartup())
		_ConnectToConfiguredServer();
}


MainWindow::~MainWindow()
{
	// Stop the connection talking to us before any of our widgets go away.
	fConnection.SetListener(nullptr);
}


// #pragma mark - Construction


void
MainWindow::_BuildUserInterface()
{
	setWindowTitle(QLatin1String(HITUX_SHARE_NAME));

	QWidget* centralWidget = new QWidget(this);
	QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
	mainLayout->setContentsMargins(6, 6, 6, 6);
	mainLayout->setSpacing(6);

	// Connection bar.
	QHBoxLayout* connectionLayout = new QHBoxLayout();

	connectionLayout->addWidget(new QLabel(tr("Server:"), centralWidget));

	// Editable so a server that is not on the list can still be typed in, but with
	// NoInsert: entries are added by RememberServer() once a connection actually
	// succeeds, so a typo never earns a permanent place in the menu.
	fServerAddressBox = new QComboBox(centralWidget);
	fServerAddressBox->setEditable(true);
	fServerAddressBox->setInsertPolicy(QComboBox::NoInsert);
	fServerAddressBox->setMinimumWidth(260);
	fServerAddressBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	connectionLayout->addWidget(fServerAddressBox, 1);

	fServerPortField = new QSpinBox(centralWidget);
	fServerPortField->setRange(1, 65535);
	fServerPortField->setValue(kDefaultServerPort);
	connectionLayout->addWidget(fServerPortField);

	connectionLayout->addSpacing(12);
	connectionLayout->addWidget(new QLabel(tr("Name:"), centralWidget));

	fUserNameField = new QLineEdit(centralWidget);
	fUserNameField->setMaximumWidth(160);
	connectionLayout->addWidget(fUserNameField);

	fConnectButton = new QPushButton(tr("Connect"), centralWidget);
	fConnectButton->setDefault(false);
	fConnectButton->setAutoDefault(false);
	connect(fConnectButton, &QPushButton::clicked,
		this, &MainWindow::_OnConnectButtonClicked);
	connectionLayout->addWidget(fConnectButton);

	mainLayout->addLayout(connectionLayout);

	// Results above chat on the left, user list on the right.
	fSplitter = new QSplitter(Qt::Horizontal, centralWidget);
	fLeftSplitter = new QSplitter(Qt::Vertical, fSplitter);

	QWidget* resultsContainer = new QWidget(fLeftSplitter);
	QVBoxLayout* resultsLayout = new QVBoxLayout(resultsContainer);
	resultsLayout->setContentsMargins(0, 0, 0, 0);
	resultsLayout->setSpacing(4);

	QHBoxLayout* queryLayout = new QHBoxLayout();
	queryLayout->addWidget(new QLabel(tr("Search:"), resultsContainer));

	fQueryField = new QLineEdit(resultsContainer);
	fQueryField->setPlaceholderText(tr("File name pattern, e.g. *.hpkg"));
	fQueryField->setClearButtonEnabled(true);
	connect(fQueryField, &QLineEdit::returnPressed,
		this, &MainWindow::_OnQueryFieldReturnPressed);
	queryLayout->addWidget(fQueryField, 1);

	fQueryButton = new QPushButton(tr("Search"), resultsContainer);
	fQueryButton->setAutoDefault(false);
	connect(fQueryButton, &QPushButton::clicked,
		this, &MainWindow::_OnQueryButtonClicked);
	queryLayout->addWidget(fQueryButton);

	fResultCountLabel = new QLabel(resultsContainer);
	queryLayout->addWidget(fResultCountLabel);

	resultsLayout->addLayout(queryLayout);

	fResultsModel = new FileResultModel(this);

	fResultsProxyModel = new QSortFilterProxyModel(this);
	fResultsProxyModel->setSourceModel(fResultsModel);
	fResultsProxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
	// Size and Modified carry a raw comparable value under this role, so they
	// sort by magnitude while still displaying as "2.8 MB" and a date.
	fResultsProxyModel->setSortRole(FileResultModel::kSortRole);

	fResultsView = new QTreeView(resultsContainer);
	fResultsView->setModel(fResultsProxyModel);
	fResultsView->setRootIsDecorated(false);
	fResultsView->setAlternatingRowColors(true);
	fResultsView->setSortingEnabled(true);
	fResultsView->setSelectionBehavior(QAbstractItemView::SelectRows);
	fResultsView->setSelectionMode(QAbstractItemView::ExtendedSelection);
	fResultsView->setUniformRowHeights(true);
	fResultsView->header()->setStretchLastSection(false);
	fResultsView->header()->setSectionResizeMode(FileResultModel::COLUMN_NAME,
		QHeaderView::Stretch);
	for (int column = FileResultModel::COLUMN_SIZE;
			column < FileResultModel::COLUMN_COUNT; column++) {
		fResultsView->header()->setSectionResizeMode(column,
			QHeaderView::ResizeToContents);
	}

	resultsLayout->addWidget(fResultsView, 1);
	fLeftSplitter->addWidget(resultsContainer);

	QWidget* chatContainer = new QWidget(fLeftSplitter);
	QVBoxLayout* chatLayout = new QVBoxLayout(chatContainer);
	chatLayout->setContentsMargins(0, 0, 0, 0);
	chatLayout->setSpacing(4);

	fChatLogView = new ChatLogView(chatContainer);
	chatLayout->addWidget(fChatLogView, 1);

	fChatInputLine = new ChatInputLine(chatContainer);
	fChatInputLine->setPlaceholderText(tr("Type a message, or /help"));
	connect(fChatInputLine, &QLineEdit::returnPressed,
		this, &MainWindow::_OnInputReturnPressed);
	fChatInputLine->SetCompletionProvider(
		[this](const QString& prefix) { return _GetCompletionCandidates(prefix); });
	chatLayout->addWidget(fChatInputLine);

	fLeftSplitter->addWidget(chatContainer);
	fLeftSplitter->setStretchFactor(0, 3);
	fLeftSplitter->setStretchFactor(1, 2);
	fLeftSplitter->setSizes({360, 240});

	fSplitter->addWidget(fLeftSplitter);

	fUserListModel = new UserListModel(this);

	fUserListProxyModel = new QSortFilterProxyModel(this);
	fUserListProxyModel->setSourceModel(fUserListModel);
	fUserListProxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);

	fUserListView = new QTreeView(fSplitter);
	fUserListView->setModel(fUserListProxyModel);
	fUserListView->setRootIsDecorated(false);
	fUserListView->setAlternatingRowColors(true);
	fUserListView->setSortingEnabled(true);
	fUserListView->setSelectionBehavior(QAbstractItemView::SelectRows);
	fUserListView->setUniformRowHeights(true);
	fUserListView->sortByColumn(UserListModel::COLUMN_NAME, Qt::AscendingOrder);
	// Let every column size to its contents except Name, which absorbs the slack.
	// Fixed pixel widths are wrong twice over here: they ignore the user's font size,
	// and five guessed widths overflow this pane so the rightmost column vanishes
	// behind a horizontal scrollbar.
	QHeaderView* userListHeader = fUserListView->header();
	userListHeader->setStretchLastSection(false);
	userListHeader->setSectionResizeMode(UserListModel::COLUMN_NAME,
		QHeaderView::Stretch);
	userListHeader->setSectionResizeMode(UserListModel::COLUMN_STATUS,
		QHeaderView::ResizeToContents);
	userListHeader->setSectionResizeMode(UserListModel::COLUMN_CLIENT,
		QHeaderView::ResizeToContents);
	userListHeader->setSectionResizeMode(UserListModel::COLUMN_FILES,
		QHeaderView::ResizeToContents);
	userListHeader->setSectionResizeMode(UserListModel::COLUMN_HOST,
		QHeaderView::ResizeToContents);

	// Host is the least useful column while we are only doing chat, and it is the one
	// that pushes the pane into needing a scrollbar.  It stays available in the row
	// tooltip and through the View menu; Phase 2 may well want it shown by default.
	fUserListView->setColumnHidden(UserListModel::COLUMN_HOST, true);

	connect(fUserListView, &QTreeView::doubleClicked,
		this, &MainWindow::_OnUserDoubleClicked);

	fSplitter->addWidget(fUserListView);
	fSplitter->setStretchFactor(0, 2);
	fSplitter->setStretchFactor(1, 1);
	fSplitter->setSizes({620, 340});

	mainLayout->addWidget(fSplitter, 1);

	setCentralWidget(centralWidget);

	fStatusLabel = new QLabel(this);
	statusBar()->addWidget(fStatusLabel, 1);

	fUserCountLabel = new QLabel(this);
	statusBar()->addPermanentWidget(fUserCountLabel);

	resize(1100, 720);
	fChatInputLine->setFocus();
}


void
MainWindow::_BuildMenus()
{
	QMenu* fileMenu = menuBar()->addMenu(tr("&File"));

	fConnectAction = fileMenu->addAction(tr("&Connect"), this,
		&MainWindow::_ConnectToConfiguredServer);
	fConnectAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+K")));

	fDisconnectAction = fileMenu->addAction(tr("&Disconnect"), this,
		[this]() { fConnection.DisconnectFromServer(); });
	fDisconnectAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+K")));

	fileMenu->addSeparator();

	QAction* quitAction = fileMenu->addAction(tr("&Quit"), this, &QWidget::close);
	quitAction->setShortcut(QKeySequence::Quit);

	QMenu* viewMenu = menuBar()->addMenu(tr("&View"));

	fShowTimestampsAction = viewMenu->addAction(tr("Show &timestamps"));
	fShowTimestampsAction->setCheckable(true);
	fShowTimestampsAction->setChecked(true);
	connect(fShowTimestampsAction, &QAction::toggled,
		this, &MainWindow::_OnToggleTimestamps);

	fShowHostColumnAction = viewMenu->addAction(tr("Show &host column"));
	fShowHostColumnAction->setCheckable(true);
	fShowHostColumnAction->setChecked(false);
	connect(fShowHostColumnAction, &QAction::toggled, this,
		[this](bool showHostColumn) {
			fUserListView->setColumnHidden(UserListModel::COLUMN_HOST,
				showHostColumn == false);
		});

	viewMenu->addSeparator();

	viewMenu->addAction(tr("&Clear chat log"), this,
		[this]() { fChatLogView->clear(); });

	QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
	helpMenu->addAction(tr("&About HiTuxShare"), this, &MainWindow::_OnShowAbout);
}


void
MainWindow::_LoadSettings()
{
	_PopulateServerList();
	fServerPortField->setValue(fSettings.GetServerPort());
	fUserNameField->setText(ToQString(fSettings.GetUserName()));

	const QByteArray geometryData
		= GetByteArray(fSettings.GetRawMessage(), kSettingsFieldWindowGeometry);
	if (geometryData.isEmpty() == false)
		(void) restoreGeometry(geometryData);

	const QByteArray splitterData
		= GetByteArray(fSettings.GetRawMessage(), kSettingsFieldSplitterState);
	if (splitterData.isEmpty() == false)
		(void) fSplitter->restoreState(splitterData);

	bool showTimestamps = true;
	(void) fSettings.GetRawMessage().FindBool(kSettingsFieldShowTimestamps,
		showTimestamps);
	fShowTimestampsAction->setChecked(showTimestamps);
	fChatLogView->SetShowTimestamps(showTimestamps);

	bool showHostColumn = false;
	(void) fSettings.GetRawMessage().FindBool(kSettingsFieldShowHostColumn,
		showHostColumn);
	fShowHostColumnAction->setChecked(showHostColumn);
	fUserListView->setColumnHidden(UserListModel::COLUMN_HOST,
		showHostColumn == false);
}


void
MainWindow::_SaveSettings()
{
	fSettings.SetServerAddress(ToMuscleString(_GetSelectedServerAddress()));
	fSettings.SetServerPort((uint16) fServerPortField->value());
	fSettings.SetUserName(ToMuscleString(fUserNameField->text()));
	fSettings.SetUserStatus(fConnection.GetLocalUserStatus());

	PutByteArray(fSettings.GetRawMessage(), kSettingsFieldWindowGeometry,
		saveGeometry());
	PutByteArray(fSettings.GetRawMessage(), kSettingsFieldSplitterState,
		fSplitter->saveState());
	(void) fSettings.GetRawMessage().ReplaceBool(true, kSettingsFieldShowTimestamps,
		fShowTimestampsAction->isChecked());
	(void) fSettings.GetRawMessage().ReplaceBool(true, kSettingsFieldShowHostColumn,
		fShowHostColumnAction->isChecked());

	(void) fSettings.Save();
}


// #pragma mark - ServerConnectionListener


void
MainWindow::ConnectionStateChanged(ConnectionState state)
{
	if (state == CONNECTION_DISCONNECTED) {
		fUserListModel->Clear();
		setWindowTitle(QLatin1String(HITUX_SHARE_NAME));
	}

	_UpdateConnectionWidgets();
	_UpdateQueryWidgets();
	_UpdateStatusBar();
}


void
MainWindow::LocalSessionIdAssigned(const String& sessionId, const String& /*hostName*/)
{
	// Promote only now, not at connect time: reaching a session ID is the first point
	// at which we know the server is a working BeShare server and not just a socket
	// that happened to accept us.
	fSettings.RememberServer(fConnection.GetServerAddress());
	_PopulateServerList();

	// Deliberately the server's address, not the (hostName) the server reports back.
	// That value is the address the server sees US at -- i.e. the user's own public IP
	// -- which is both useless in a title bar and something they would not expect to
	// be putting on screen during a screen-share or a screenshot.
	setWindowTitle(tr("%1 -- %2 on %3")
		.arg(QLatin1String(HITUX_SHARE_NAME),
			ToQString(fConnection.GetLocalUserName()),
			ToQString(fConnection.GetServerAddress())));

	_AppendLocalLine(LOG_INFORMATION_MESSAGE,
		tr("You are session %1.").arg(ToQString(sessionId)));
}


void
MainWindow::UserUpdated(const UserRecord& user, bool isNewUser)
{
	fUserListModel->UpdateUser(user);
	fUserNamesDirty = true;
	if (fResultFlushTimer->isActive() == false)
		fResultFlushTimer->start();

	if (isNewUser) {
		ChatMessage joinMessage(LOG_USER_EVENT_MESSAGE, muscle::String());
		joinMessage.text = ToMuscleString(tr("%1 has joined.")
			.arg(ToQString(user.GetDisplayName())));
		fChatLogView->AppendChatMessage(joinMessage);
	}

	_UpdateStatusBar();
}


void
MainWindow::UserLeft(const UserRecord& user)
{
	fUserListModel->RemoveUser(user.sessionId);

	// Their files went with them.  Dropping the lot in one operation matters:
	// a peer sharing thousands of files would otherwise arrive as thousands of
	// individual removals, each shifting every row after it.
	fResultsModel->RemoveResultsForSession(user.sessionId);
	_UpdateResultCount();

	ChatMessage leaveMessage(LOG_USER_EVENT_MESSAGE, muscle::String());
	leaveMessage.text = ToMuscleString(tr("%1 has left.")
		.arg(ToQString(user.GetDisplayName())));
	fChatLogView->AppendChatMessage(leaveMessage);

	_UpdateStatusBar();
}


void
MainWindow::ChatMessageReceived(const ChatMessage& message)
{
	fChatLogView->AppendChatMessage(message);
}


void
MainWindow::PingReplyReceived(const UserRecord& user, uint64 roundTripMicroseconds,
	const String& peerVersion)
{
	QString text = tr("Ping reply from %1: %2 ms")
		.arg(ToQString(user.GetDisplayName()))
		.arg(roundTripMicroseconds / 1000);

	if (peerVersion.HasChars())
		text += tr(" (%1)").arg(ToQString(peerVersion));

	_AppendLocalLine(LOG_INFORMATION_MESSAGE, text);
}


void
MainWindow::QueryResultAdded(const FileResult& result)
{
	fPendingResults.append(result);
	if (fResultFlushTimer->isActive() == false)
		fResultFlushTimer->start();
}


void
MainWindow::QueryResultRemoved(const String& sessionId, const String& fileName)
{
	fResultsModel->RemoveResult(sessionId, fileName);
	_UpdateResultCount();
}


void
MainWindow::QueryResultsCleared()
{
	fPendingResults.clear();
	fResultsModel->Clear();
	_UpdateResultCount();
}


void
MainWindow::QuerySweepStateChanged(bool isSweeping)
{
	if (isSweeping == false) {
		// Flush immediately rather than waiting out the timer, so "search
		// complete" and the last results appear together.
		_OnFlushPendingResults();
	}

	_UpdateQueryWidgets();
	_UpdateResultCount();
}


// #pragma mark - Slots


void
MainWindow::_OnConnectButtonClicked()
{
	if (fConnection.GetConnectionState() == CONNECTION_DISCONNECTED)
		_ConnectToConfiguredServer();
	else
		fConnection.DisconnectFromServer();
}


void
MainWindow::_OnInputReturnPressed()
{
	const QString input = fChatInputLine->text();
	fChatInputLine->AcceptCurrentLine();

	if (input.trimmed().isEmpty() == false)
		_HandleUserInput(input);
}


void
MainWindow::_OnUserDoubleClicked(const QModelIndex& index)
{
	if (index.isValid() == false)
		return;

	const QModelIndex nameIndex = fUserListProxyModel->index(index.row(),
		UserListModel::COLUMN_NAME);
	const QString userName = fUserListProxyModel->data(nameIndex).toString();
	if (userName.isEmpty())
		return;

	fChatInputLine->setText(QStringLiteral("/msg %1 ").arg(userName));
	fChatInputLine->setCursorPosition(fChatInputLine->text().length());
	fChatInputLine->setFocus();
}


void
MainWindow::_OnIdleTimerFired()
{
	fConnection.PerformIdleTasks();
}


void
MainWindow::_OnQueryButtonClicked()
{
	if (fConnection.IsQueryActive())
		fConnection.StopQuery();
	else
		_StartQuery();
}


void
MainWindow::_OnQueryFieldReturnPressed()
{
	_StartQuery();
}


void
MainWindow::_OnFlushPendingResults()
{
	if (fPendingResults.isEmpty() == false) {
		fResultsModel->AddResults(fPendingResults);
		fPendingResults.clear();
		_UpdateResultCount();
	}

	if (fUserNamesDirty) {
		// A user's name arrives separately from their files, so results shared
		// by someone whose name node has not landed yet would otherwise keep
		// showing a bare session ID.
		fResultsModel->RefreshUserNames();
		fUserNamesDirty = false;
	}
}


void
MainWindow::_OnShowAbout()
{
	QMessageBox::about(this, tr("About HiTuxShare"),
		tr("<h3>%1 %2</h3>"
			"<p>A native Linux client for the BeShare file-sharing network, "
			"speaking the MUSCLE protocol.</p>"
			"<p>Ported from HiShare for Haiku, itself the modern edition of "
			"Jeremy Friesner's BeShare.</p>"
			"<p>Built on MUSCLE %3.</p>")
		.arg(QLatin1String(HITUX_SHARE_NAME),
			QLatin1String(HITUX_SHARE_VERSION_STRING),
			QLatin1String(MUSCLE_VERSION_STRING)));
}


void
MainWindow::_OnToggleTimestamps(bool showTimestamps)
{
	fChatLogView->SetShowTimestamps(showTimestamps);
}


// #pragma mark - Command handling


void
MainWindow::_HandleUserInput(const QString& input)
{
	const ChatCommand command = ChatCommandParser::Parse(ToMuscleString(input));

	switch (command.type) {
		case CHAT_COMMAND_NONE:
			_SendChatToEveryone(ToQString(command.argument), false);
			break;

		case CHAT_COMMAND_ACTION:
			_SendChatToEveryone(ToQString(command.argument), true);
			break;

		case CHAT_COMMAND_NICK:
		{
			if (command.argument.IsEmpty()) {
				_AppendLocalLine(LOG_WARNING_MESSAGE, tr("Usage: /nick <name>"));
				break;
			}

			fConnection.SetLocalUserName(command.argument);
			fUserNameField->setText(ToQString(command.argument));
			_AppendLocalLine(LOG_INFORMATION_MESSAGE,
				tr("You are now known as %1.").arg(ToQString(command.argument)));
			break;
		}

		case CHAT_COMMAND_STATUS:
			fConnection.SetLocalUserStatus(command.argument.HasChars()
				? command.argument : muscle::String("here"));
			_AppendLocalLine(LOG_INFORMATION_MESSAGE,
				tr("Status set to %1.")
					.arg(ToQString(fConnection.GetLocalUserStatus())));
			break;

		case CHAT_COMMAND_AWAY:
			fConnection.SetLocalUserStatus(command.argument.HasChars()
				? command.argument : fSettings.GetAwayStatus());
			_AppendLocalLine(LOG_INFORMATION_MESSAGE,
				tr("Status set to %1.")
					.arg(ToQString(fConnection.GetLocalUserStatus())));
			break;

		case CHAT_COMMAND_MESSAGE:
			if (command.target.IsEmpty() || command.argument.IsEmpty()) {
				_AppendLocalLine(LOG_WARNING_MESSAGE,
					tr("Usage: /msg <user> <text>"));
				break;
			}

			_SendPrivateMessage(ToQString(command.target),
				ToQString(command.argument));
			break;

		case CHAT_COMMAND_PING:
			if (command.target.IsEmpty()) {
				_AppendLocalLine(LOG_WARNING_MESSAGE, tr("Usage: /ping <user>"));
				break;
			}

			_PingUser(ToQString(command.target));
			break;

		case CHAT_COMMAND_CONNECT:
			if (command.argument.HasChars())
				fServerAddressBox->setCurrentText(ToQString(command.argument));

			_ConnectToConfiguredServer();
			break;

		case CHAT_COMMAND_DISCONNECT:
			fConnection.DisconnectFromServer();
			break;

		case CHAT_COMMAND_START_QUERY:
			if (command.argument.HasChars())
				fQueryField->setText(ToQString(command.argument));

			_StartQuery();
			break;

		case CHAT_COMMAND_STOP_QUERY:
			fConnection.StopQuery();
			_UpdateQueryWidgets();
			break;

		case CHAT_COMMAND_CLEAR:
			fChatLogView->clear();
			break;

		case CHAT_COMMAND_HELP:
			_ShowCommandHelp();
			break;

		case CHAT_COMMAND_INFO:
			_ShowConnectionInformation();
			break;

		case CHAT_COMMAND_QUIT:
			close();
			break;

		case CHAT_COMMAND_UNKNOWN:
			_AppendLocalLine(LOG_ERROR_MESSAGE,
				tr("Unknown command \"/%1\". Type /help for a list.")
					.arg(ToQString(command.commandName)));
			break;
	}
}


void
MainWindow::_SendChatToEveryone(const QString& text, bool isAction)
{
	if (fConnection.IsConnected() == false) {
		_AppendLocalLine(LOG_ERROR_MESSAGE, tr("Not connected to a server."));
		return;
	}

	fConnection.SendChatText(muscle::String("*"), ToMuscleString(text));

	// We drop the server's echo of our own lines, so echo here or the sender never
	// sees what they just said.
	ChatMessage localEcho;
	localEcho.type = LOG_LOCAL_USER_CHAT_MESSAGE;
	localEcho.senderName = fConnection.GetLocalUserName();
	localEcho.isFromLocalUser = true;
	localEcho.isAction = isAction;

	// An action goes out with its "/me " prefix intact -- that prefix is the wire
	// format -- but it must not be shown twice locally.
	localEcho.text = isAction
		? ToMuscleString(text.mid((int) strlen(BESHARE_ACTION_PREFIX)))
		: ToMuscleString(text);

	fChatLogView->AppendChatMessage(localEcho);
}


void
MainWindow::_SendPrivateMessage(const QString& target, const QString& text)
{
	if (fConnection.IsConnected() == false) {
		_AppendLocalLine(LOG_ERROR_MESSAGE, tr("Not connected to a server."));
		return;
	}

	const Queue<muscle::String> targetSessionIds
		= fConnection.GetUsers().ResolveToSessionIds(ToMuscleString(target));
	if (targetSessionIds.IsEmpty()) {
		_AppendLocalLine(LOG_ERROR_MESSAGE, tr("No such user: %1").arg(target));
		return;
	}

	for (uint32 i = 0; i < targetSessionIds.GetNumItems(); i++) {
		fConnection.SendChatText(targetSessionIds[i], ToMuscleString(text));

		ChatMessage localEcho;
		localEcho.type = LOG_LOCAL_USER_CHAT_MESSAGE;
		localEcho.isPrivate = true;
		localEcho.isFromLocalUser = true;
		localEcho.senderName = ToMuscleString(tr("to %1").arg(ToQString(
			fConnection.GetUsers().GetDisplayNameForSession(targetSessionIds[i]))));
		localEcho.text = ToMuscleString(text);

		fChatLogView->AppendChatMessage(localEcho);
	}
}


void
MainWindow::_PingUser(const QString& target)
{
	if (fConnection.IsConnected() == false) {
		_AppendLocalLine(LOG_ERROR_MESSAGE, tr("Not connected to a server."));
		return;
	}

	const Queue<muscle::String> targetSessionIds
		= fConnection.GetUsers().ResolveToSessionIds(ToMuscleString(target));
	if (targetSessionIds.IsEmpty()) {
		_AppendLocalLine(LOG_ERROR_MESSAGE, tr("No such user: %1").arg(target));
		return;
	}

	for (uint32 i = 0; i < targetSessionIds.GetNumItems(); i++)
		fConnection.SendPing(targetSessionIds[i]);
}


void
MainWindow::_ShowCommandHelp()
{
	_AppendLocalLine(LOG_INFORMATION_MESSAGE, tr("Available commands:"));

	const Queue<ChatCommandHelpEntry> helpEntries
		= ChatCommandParser::GetHelpEntries();
	for (uint32 i = 0; i < helpEntries.GetNumItems(); i++) {
		const ChatCommandHelpEntry& entry = helpEntries[i];

		QString line = QStringLiteral("  /%1").arg(QLatin1String(entry.commandName));
		if (entry.arguments != NULL)
			line += QStringLiteral(" %1").arg(QLatin1String(entry.arguments));

		line += QStringLiteral(" -- %1").arg(QLatin1String(entry.description));
		_AppendLocalLine(LOG_INFORMATION_MESSAGE, line);
	}
}


void
MainWindow::_ShowConnectionInformation()
{
	if (fConnection.IsConnected() == false) {
		_AppendLocalLine(LOG_INFORMATION_MESSAGE, tr("Not connected."));
		return;
	}

	_AppendLocalLine(LOG_INFORMATION_MESSAGE,
		tr("Connected to %1:%2 as %3 (session %4). %5 users online.")
			.arg(ToQString(fConnection.GetServerAddress()))
			.arg(fConnection.GetServerPort())
			.arg(ToQString(fConnection.GetLocalUserName()))
			.arg(ToQString(fConnection.GetLocalSessionId()))
			.arg(fConnection.GetUsers().GetUserCount()));
}


// #pragma mark - Helpers


void
MainWindow::_ConnectToConfiguredServer()
{
	const QString serverAddress = _GetSelectedServerAddress();
	if (serverAddress.isEmpty()) {
		_AppendLocalLine(LOG_ERROR_MESSAGE, tr("Enter a server address first."));
		return;
	}

	const QString userName = fUserNameField->text().trimmed();
	if (userName.isEmpty()) {
		_AppendLocalLine(LOG_ERROR_MESSAGE, tr("Enter a user name first."));
		return;
	}

	fConnection.SetLocalUserName(ToMuscleString(userName));

	(void) fConnection.ConnectToServer(ToMuscleString(serverAddress),
		(uint16) fServerPortField->value());
}


void
MainWindow::_PopulateServerList()
{
	const QString previousSelection = fServerAddressBox->currentText();

	// Repopulating clears the edit field, so restore whatever the user had typed --
	// this runs after a successful connect, and silently discarding their text would
	// be worse than the reordering is helpful.
	fServerAddressBox->clear();

	const Queue<muscle::String> serverList = fSettings.GetServerList();
	for (uint32 i = 0; i < serverList.GetNumItems(); i++)
		fServerAddressBox->addItem(ToQString(serverList[i]));

	if (previousSelection.isEmpty() == false)
		fServerAddressBox->setCurrentText(previousSelection);
	else
		fServerAddressBox->setCurrentText(ToQString(fSettings.GetServerAddress()));
}


QString
MainWindow::_GetSelectedServerAddress() const
{
	return fServerAddressBox->currentText().trimmed();
}


void
MainWindow::_StartQuery()
{
	if (fConnection.IsConnected() == false) {
		_AppendLocalLine(LOG_ERROR_MESSAGE, tr("Not connected to a server."));
		return;
	}

	const QString pattern = fQueryField->text().trimmed();

	// An empty box means "everything", which is a legitimate thing to ask for on
	// a small server and a very large thing to ask for on a busy one.
	fConnection.StartQuery(muscle::String("*"),
		ToMuscleString(pattern.isEmpty() ? QStringLiteral("*") : pattern));

	_UpdateQueryWidgets();
}


void
MainWindow::_UpdateQueryWidgets()
{
	const bool isActive = fConnection.IsQueryActive();
	fQueryButton->setText(isActive ? tr("Stop") : tr("Search"));
	fQueryButton->setEnabled(fConnection.IsConnected());
	fQueryField->setEnabled(fConnection.IsConnected());
}


void
MainWindow::_UpdateResultCount()
{
	const int count = fResultsModel->rowCount();
	if (count == 0 && fConnection.IsQueryActive() == false) {
		fResultCountLabel->clear();
		return;
	}

	fResultCountLabel->setText(fConnection.IsQueryActive()
		? tr("%n result(s), still listening", "", count)
		: tr("%n result(s)", "", count));
}


void
MainWindow::_UpdateConnectionWidgets()
{
	const ConnectionState state = fConnection.GetConnectionState();
	const bool isDisconnected = (state == CONNECTION_DISCONNECTED);

	fConnectButton->setText(isDisconnected ? tr("Connect") : tr("Disconnect"));
	fConnectAction->setEnabled(isDisconnected);
	fDisconnectAction->setEnabled(isDisconnected == false);

	// The server fields describe where we are connected, so freeze them while that
	// is true rather than letting them drift out of sync with reality.
	fServerAddressBox->setEnabled(isDisconnected);
	fServerPortField->setEnabled(isDisconnected);
}


void
MainWindow::_UpdateStatusBar()
{
	switch (fConnection.GetConnectionState()) {
		case CONNECTION_DISCONNECTED:
			fStatusLabel->setText(tr("Not connected"));
			break;

		case CONNECTION_CONNECTING:
			fStatusLabel->setText(tr("Connecting to %1...")
				.arg(ToQString(fConnection.GetServerAddress())));
			break;

		case CONNECTION_CONNECTED:
			fStatusLabel->setText(tr("Connected to %1")
				.arg(ToQString(fConnection.GetServerAddress())));
			break;
	}

	const uint32 userCount = fConnection.GetUsers().GetUserCount();
	fUserCountLabel->setText(fConnection.IsConnected()
		? tr("%n user(s)", "", (int) userCount) : QString());
}


QStringList
MainWindow::_GetCompletionCandidates(const QString& prefix) const
{
	QStringList candidates;

	const Hashtable<muscle::String, UserRecord>& users
		= fConnection.GetUsers().GetUsers();
	for (auto iterator = users.GetIterator(); iterator.HasData(); iterator++) {
		const QString userName = ToQString(iterator.GetValue().userName);
		if (userName.isEmpty() == false
				&& userName.startsWith(prefix, Qt::CaseInsensitive)) {
			candidates.append(userName);
		}
	}

	candidates.sort(Qt::CaseInsensitive);
	return candidates;
}


void
MainWindow::_AppendLocalLine(LogMessageType type, const QString& text)
{
	fChatLogView->AppendLocalMessage(type, text);
}


void
MainWindow::closeEvent(QCloseEvent* event)
{
	_SaveSettings();
	fConnection.SetListener(nullptr);
	fConnection.DisconnectFromServer();

	QMainWindow::closeEvent(event);
}


}  // namespace hitux

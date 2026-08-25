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
#include "qt/DesktopNotifier.h"
#include "qt/FileResultModel.h"
#include "qt/QtConversions.h"
#include "qt/TransferModel.h"
#include "qt/UserListModel.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
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
	fTransfersView(nullptr),
	fTransferModel(nullptr),
	fDownloadButton(nullptr),
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
	fShareLabel(nullptr),
	fConnectAction(nullptr),
	fDisconnectAction(nullptr),
	fShowTimestampsAction(nullptr),
	fShowHostColumnAction(nullptr),
	fFileSharingAction(nullptr),
	fNotificationsAction(nullptr),
	fNotifier(nullptr),
	fIdleTimer(nullptr),
	fResultFlushTimer(nullptr),
	fUserNamesDirty(false),
	fDownloads(&fCallbackMechanism),
	fUploadServer(&fCallbackMechanism),
	fSharedFileCount(0)
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

	fNotifier = new DesktopNotifier(this);
	fNotifier->SetEnabled(fSettings.GetNotificationsEnabled());

	fDownloads.SetListener(this);
	fUploadServer.SetListener(this);
	fTransferModel->SetUploadServer(&fUploadServer);
	fDownloads.SetDownloadDirectory(fSettings.GetDownloadDirectory());
	fDownloads.SetRetainFilePaths(fSettings.GetRetainFilePaths());

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

	// Download button strip under the results.
	QHBoxLayout* resultActionLayout = new QHBoxLayout();
	fDownloadButton = new QPushButton(tr("Download Selected"), resultsContainer);
	fDownloadButton->setAutoDefault(false);
	fDownloadButton->setEnabled(false);
	connect(fDownloadButton, &QPushButton::clicked,
		this, &MainWindow::_OnDownloadSelected);
	resultActionLayout->addWidget(fDownloadButton);
	resultActionLayout->addStretch(1);
	resultsLayout->addLayout(resultActionLayout);

	connect(fResultsView, &QTreeView::doubleClicked,
		this, &MainWindow::_OnResultsDoubleClicked);
	connect(fResultsView->selectionModel(), &QItemSelectionModel::selectionChanged,
		this, [this]() {
			fDownloadButton->setEnabled(
				fResultsView->selectionModel()->hasSelection());
		});

	QWidget* transfersContainer = new QWidget(fLeftSplitter);
	QVBoxLayout* transfersLayout = new QVBoxLayout(transfersContainer);
	transfersLayout->setContentsMargins(0, 0, 0, 0);
	transfersLayout->setSpacing(4);

	fTransferModel = new TransferModel(&fDownloads, this);

	fTransfersView = new QTreeView(transfersContainer);
	fTransfersView->setModel(fTransferModel);
	fTransfersView->setRootIsDecorated(false);
	fTransfersView->setAlternatingRowColors(true);
	fTransfersView->setSelectionBehavior(QAbstractItemView::SelectRows);
	fTransfersView->setUniformRowHeights(true);
	fTransfersView->setItemDelegateForColumn(TransferModel::COLUMN_PROGRESS,
		new TransferProgressDelegate(this));
	fTransfersView->header()->setStretchLastSection(true);
	fTransfersView->header()->setSectionResizeMode(TransferModel::COLUMN_FILE,
		QHeaderView::Stretch);
	fTransfersView->setColumnWidth(TransferModel::COLUMN_PROGRESS, 110);
	transfersLayout->addWidget(fTransfersView, 1);

	QHBoxLayout* transferActionLayout = new QHBoxLayout();
	QPushButton* cancelButton = new QPushButton(tr("Cancel"), transfersContainer);
	cancelButton->setAutoDefault(false);
	connect(cancelButton, &QPushButton::clicked,
		this, &MainWindow::_OnCancelSelectedTransfer);
	transferActionLayout->addWidget(cancelButton);

	QPushButton* clearButton = new QPushButton(tr("Clear Finished"),
		transfersContainer);
	clearButton->setAutoDefault(false);
	connect(clearButton, &QPushButton::clicked,
		this, &MainWindow::_OnClearFinishedTransfers);
	transferActionLayout->addWidget(clearButton);
	transferActionLayout->addStretch(1);
	transfersLayout->addLayout(transferActionLayout);

	fLeftSplitter->addWidget(transfersContainer);

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
	fLeftSplitter->setStretchFactor(0, 4);
	fLeftSplitter->setStretchFactor(1, 1);
	fLeftSplitter->setStretchFactor(2, 2);
	fLeftSplitter->setSizes({380, 140, 220});

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

	fShareLabel = new QLabel(this);
	statusBar()->addPermanentWidget(fShareLabel);

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

	fileMenu->addAction(tr("Choose &Share Folder..."), this,
		&MainWindow::_OnChooseShareFolder);

	fFileSharingAction = fileMenu->addAction(tr("Share my &files"));
	fFileSharingAction->setCheckable(true);
	connect(fFileSharingAction, &QAction::toggled,
		this, &MainWindow::_OnToggleFileSharing);

	fileMenu->addSeparator();

	QAction* quitAction = fileMenu->addAction(tr("&Quit"), this, &QWidget::close);
	quitAction->setShortcut(QKeySequence::Quit);

	QMenu* viewMenu = menuBar()->addMenu(tr("&View"));

	fShowTimestampsAction = viewMenu->addAction(tr("Show &timestamps"));
	fShowTimestampsAction->setCheckable(true);
	fShowTimestampsAction->setChecked(true);
	connect(fShowTimestampsAction, &QAction::toggled,
		this, &MainWindow::_OnToggleTimestamps);

	fNotificationsAction = viewMenu->addAction(tr("Desktop &notifications"));
	fNotificationsAction->setCheckable(true);
	fNotificationsAction->setChecked(true);
	connect(fNotificationsAction, &QAction::toggled,
		this, &MainWindow::_OnToggleNotifications);

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
	fFileSharingAction->setChecked(fSettings.GetFileSharingEnabled());
	fNotificationsAction->setChecked(fSettings.GetNotificationsEnabled());
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

	// Only now: sharing publishes under our session, so it needs one to exist.
	_StartSharing();
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
	_MaybeNotifyAboutChat(message);
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


// #pragma mark - DownloadManagerListener


void
MainWindow::DownloadListChanged()
{
	fTransferModel->NotifyListChanged();
}


void
MainWindow::DownloadChanged(uint32 index)
{
	fTransferModel->NotifyRowChanged(index);
}


void
MainWindow::DownloadReport(LogMessageType type, const String& text)
{
	_AppendLocalLine(type, ToQString(text));

	// A finished download is the one transfer event worth interrupting for:
	// it is what the user was waiting on, and it is not self-announcing the way
	// a chat line in a visible window is.
	if (type == LOG_INFORMATION_MESSAGE && fNotifier != nullptr
			&& _UserIsLookingAtUs() == false
			&& ToQString(text).startsWith(QLatin1String("Finished downloading"))) {
		fNotifier->Notify(DesktopNotifier::CATEGORY_TRANSFER,
			tr("Download finished"), ToQString(text));
	}
}


void
MainWindow::UploadsChanged(FileUploadServer* /*server*/)
{
	fTransferModel->NotifyListChanged();
	_UpdateStatusBar();
}


void
MainWindow::UploadReport(LogMessageType type, const String& text)
{
	_AppendLocalLine(type, ToQString(text));
}


// #pragma mark - Sharing


void
MainWindow::_StartSharing()
{
	if (fSettings.GetFileSharingEnabled() == false)
		return;

	const String shareDirectory = fSettings.GetShareDirectory();
	if (shareDirectory.IsEmpty()) {
		_AppendLocalLine(LOG_WARNING_MESSAGE,
			tr("Choose a folder to share first (File menu)."));
		return;
	}

	// Listen before publishing. A peer that reads our file list and connects
	// back before we are accepting would simply be refused.
	const uint16 listenPort = fUploadServer.StartListening(kDefaultTransferPort,
		kTransferPortRange);
	if (listenPort == 0) {
		_AppendLocalLine(LOG_ERROR_MESSAGE,
			tr("Could not listen on any port, so nobody can download from us."));
	} else {
		fUploadServer.SetLocalIdentity(fConnection.GetLocalSessionId(),
			fConnection.GetLocalUserName());
		fConnection.SetAdvertisedPort(listenPort);
		_AppendLocalLine(LOG_INFORMATION_MESSAGE,
			tr("Accepting downloads on port %1.").arg(listenPort));
	}

	fSharedFiles.Clear();
	fSharedFileCount = 0;
	fShareScanner.SetShareDirectory(shareDirectory);
	fShareScanner.StartScan();

	_AppendLocalLine(LOG_INFORMATION_MESSAGE,
		tr("Scanning %1...").arg(ToQString(shareDirectory)));
}


void
MainWindow::_StopSharing()
{
	fShareScanner.StopScan();
	fUploadServer.StopListening();
	fConnection.SetAdvertisedPort(0);
	fConnection.UnpublishAllSharedFiles();
	fConnection.PublishSharedFileCount(0);

	fSharedFiles.Clear();
	fSharedFileCount = 0;

	fTransferModel->NotifyListChanged();
	_UpdateStatusBar();
}


void
MainWindow::_DrainShareScanner()
{
	// Publishing happens here rather than on the scanning thread: neither the
	// connection nor the upload server is thread-safe, and the scan is
	// deliberately allowed to run ahead of the network.
	const Queue<SharedFile> discovered = fShareScanner.TakeDiscoveredFiles();
	if (discovered.HasItems()) {
		fConnection.PublishSharedFiles(discovered);
		for (uint32 i = 0; i < discovered.GetNumItems(); i++)
			(void) fSharedFiles.Put(discovered[i].fileName, discovered[i]);

		fUploadServer.SetSharedFiles(fSharedFiles);
		fSharedFileCount += discovered.GetNumItems();
		_UpdateStatusBar();
	}

	if (fShareScanner.TakeScanFinished()) {
		fConnection.PublishSharedFileCount(fSharedFileCount);

		QString message = tr("Sharing %n file(s).", "", (int) fSharedFileCount);
		const uint32 duplicates = fShareScanner.GetDuplicateNameCount();
		if (duplicates > 0) {
			message += QLatin1Char(' ') + tr("%n file(s) were skipped because "
				"another file already had that name -- the protocol identifies "
				"a shared file by its name alone.", "", (int) duplicates);
		}

		_AppendLocalLine(LOG_INFORMATION_MESSAGE, message);
		_UpdateStatusBar();
	}
}


// #pragma mark - Slots


void
MainWindow::_OnToggleNotifications(bool enabled)
{
	fSettings.SetNotificationsEnabled(enabled);
	fNotifier->SetEnabled(enabled);

	if (enabled && fNotifier->IsAvailable() == false) {
		_AppendLocalLine(LOG_WARNING_MESSAGE,
			tr("No notification service is running on this desktop, so nothing"
				" will appear."));
	}
}


void
MainWindow::_OnChooseShareFolder()
{
	const QString chosen = QFileDialog::getExistingDirectory(this,
		tr("Choose a folder to share"),
		ToQString(fSettings.GetShareDirectory()));
	if (chosen.isEmpty())
		return;

	fSettings.SetShareDirectory(ToMuscleString(chosen));
	_AppendLocalLine(LOG_INFORMATION_MESSAGE,
		tr("Share folder set to %1.").arg(chosen));

	if (fSettings.GetFileSharingEnabled() && fConnection.IsConnected()) {
		_StopSharing();
		_StartSharing();
	}
}


void
MainWindow::_OnToggleFileSharing(bool enabled)
{
	fSettings.SetFileSharingEnabled(enabled);

	if (enabled == false) {
		_StopSharing();
		_AppendLocalLine(LOG_INFORMATION_MESSAGE, tr("File sharing is off."));
		return;
	}

	if (fConnection.IsConnected())
		_StartSharing();
	else
		_AppendLocalLine(LOG_INFORMATION_MESSAGE,
			tr("File sharing will start when you connect."));
}


void
MainWindow::_OnDownloadSelected()
{
	_DownloadSelectedResults();
}


void
MainWindow::_OnResultsDoubleClicked(const QModelIndex& /*index*/)
{
	_DownloadSelectedResults();
}


void
MainWindow::_OnClearFinishedTransfers()
{
	fDownloads.ClearFinishedDownloads();
}


void
MainWindow::_OnCancelSelectedTransfer()
{
	const QModelIndexList selected
		= fTransfersView->selectionModel()->selectedRows();
	for (const QModelIndex& index : selected)
		fDownloads.AbortDownload((uint32) index.row());
}


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
	_DrainShareScanner();
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

		case CHAT_COMMAND_GET:
			_DownloadSelectedResults();
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
MainWindow::_DownloadSelectedResults()
{
	const QModelIndexList selected
		= fResultsView->selectionModel()->selectedRows();
	if (selected.isEmpty()) {
		_AppendLocalLine(LOG_WARNING_MESSAGE,
			tr("Select something in the results list first."));
		return;
	}

	Queue<FileResult> chosen;
	for (const QModelIndex& proxyIndex : selected) {
		const QModelIndex sourceIndex
			= fResultsProxyModel->mapToSource(proxyIndex);
		const FileResult* result
			= fResultsModel->GetResultForRow(sourceIndex.row());
		if (result != nullptr)
			(void) chosen.AddTail(*result);
	}

	// Settings can have changed since construction, and a download that ignored
	// the folder the user just chose would be its own bug report.
	fDownloads.SetDownloadDirectory(fSettings.GetDownloadDirectory());
	fDownloads.SetRetainFilePaths(fSettings.GetRetainFilePaths());

	fDownloads.StartDownloads(chosen, fConnection.GetUsers(),
		fConnection.GetLocalSessionId(), fConnection.GetLocalUserName());
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

	QString shareText;
	if (fSharedFileCount > 0)
		shareText = tr("Sharing %n file(s)", "", (int) fSharedFileCount);

	const uint32 uploadCount = fUploadServer.GetActiveUploadCount();
	if (uploadCount > 0) {
		if (shareText.isEmpty() == false)
			shareText += QLatin1String(" -- ");

		shareText += tr("%n peer(s) downloading", "", (int) uploadCount);
	}

	fShareLabel->setText(shareText);

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


bool
MainWindow::_MentionsLocalUser(const muscle::String& text) const
{
	const QString localName = ToQString(fConnection.GetLocalUserName()).trimmed();
	if (localName.isEmpty())
		return false;

	const QString haystack = ToQString(text);
	int index = haystack.indexOf(localName, 0, Qt::CaseInsensitive);
	while (index >= 0) {
		// Require the match to stand alone, so somebody called "sam" is not
		// notified by every mention of "same".
		const bool startsCleanly = (index == 0)
			|| haystack.at(index - 1).isLetterOrNumber() == false;
		const int after = index + localName.length();
		const bool endsCleanly = (after >= haystack.length())
			|| haystack.at(after).isLetterOrNumber() == false;

		if (startsCleanly && endsCleanly)
			return true;

		index = haystack.indexOf(localName, index + 1, Qt::CaseInsensitive);
	}

	return false;
}


bool
MainWindow::_UserIsLookingAtUs() const
{
	// All three conditions, because "the window is focused" and "the user can
	// see the window" are not the same question. A minimised window on some
	// setups still reports as active, and notifying about a window nobody can
	// see is the case a notification exists for.
	return isActiveWindow() && isMinimized() == false && isVisible();
}


void
MainWindow::_MaybeNotifyAboutChat(const ChatMessage& message)
{
	if (fNotifier == nullptr || message.isFromLocalUser)
		return;

	// Notifying about a window the user is already looking at is pure noise.
	if (_UserIsLookingAtUs())
		return;

	if (message.type != LOG_REMOTE_USER_CHAT_MESSAGE)
		return;

	const QString sender = ToQString(message.senderName);

	if (message.isPrivate) {
		fNotifier->Notify(DesktopNotifier::CATEGORY_CHAT,
			tr("Private message from %1").arg(sender),
			ToQString(message.text));
	} else if (_MentionsLocalUser(message.text)) {
		fNotifier->Notify(DesktopNotifier::CATEGORY_CHAT,
			tr("%1 mentioned you").arg(sender), ToQString(message.text));
	}
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
	fDownloads.SetListener(nullptr);
	fDownloads.AbortAll();
	fUploadServer.SetListener(nullptr);
	fShareScanner.StopScan();
	fUploadServer.StopListening();
	fConnection.SetListener(nullptr);
	fConnection.DisconnectFromServer();

	QMainWindow::closeEvent(event);
}


}  // namespace hitux

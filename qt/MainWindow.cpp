/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "qt/MainWindow.h"

#include "core/BeShareProtocol.h"
#include "core/ChatCommandParser.h"
#include "core/HiTuxShareVersion.h"

#include "util/ByteBuffer.h"
#include "qt/ChatInputLine.h"
#include "qt/ChatLogView.h"
#include "qt/DesktopNotifier.h"
#include "qt/FileResultModel.h"
#include "qt/QtConversions.h"
#include "qt/ServerListUpdater.h"
#include "qt/SettingsDialog.h"
#include "qt/TransferModel.h"
#include "qt/UserListModel.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QInputDialog>
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
	fConnections(&fCallbackMechanism),
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
	fUserNameBox(nullptr),
	fUserStatusBox(nullptr),
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
	fServerListUpdater(nullptr),
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

	fConnections.SetListener(this);
	fConnections.SetInstallId(fSettings.GetInstallId());
	fConnections.SetLocalUserName(fSettings.GetUserName());
	fConnections.SetLocalUserStatus(fSettings.GetUserStatus());

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


	fNotifier = new DesktopNotifier(this);

	fServerListUpdater = new ServerListUpdater(this);
	connect(fServerListUpdater, &ServerListUpdater::ServerListReceived,
		this, &MainWindow::_OnServerListReceived);
	connect(fServerListUpdater, &ServerListUpdater::UpdateFailed, this,
		[this](const QString& reason) {
			_AppendLocalLine(LOG_WARNING_MESSAGE,
				tr("Could not fetch the server list: %1").arg(reason));
		});

	fDownloads.SetListener(this);
	fUploadServer.SetListener(this);
	fTransferModel->SetUploadServer(&fUploadServer);

	// Everything stored becomes running behaviour in one place, so a setting
	// cannot be half-wired: if it is not applied in _ApplySettings(), it does
	// nothing at all.
	_ApplySettings();

	_UpdateConnectionWidgets();
	_UpdateQueryWidgets();
	_UpdateStatusBar();

	_AppendLocalLine(LOG_INFORMATION_MESSAGE,
		tr("%1 %2 -- type /help for a list of commands.")
			.arg(QLatin1String(HITUX_SHARE_NAME),
				QLatin1String(HITUX_SHARE_VERSION_STRING)));

	_RestoreExtraServers();

	if (fSettings.GetAutoUpdateServerList())
		fServerListUpdater->Start();

	if (fSettings.GetConnectOnStartup())
		_ConnectToConfiguredServer();
}


MainWindow::~MainWindow()
{
	// Stop the connection talking to us before any of our widgets go away.
	fConnections.SetListener(nullptr);
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

	fUserNameBox = new QComboBox(centralWidget);
	fUserNameBox->setEditable(true);
	fUserNameBox->setInsertPolicy(QComboBox::NoInsert);
	fUserNameBox->setMinimumWidth(160);
	connectionLayout->addWidget(fUserNameBox);

	connectionLayout->addWidget(new QLabel(tr("Status:"), centralWidget));

	// Editable with presets rather than a plain field: "here" and "away" cover
	// almost every use, but the protocol carries free text and people use it.
	// Changing status is a frequent action, so it belongs in the window rather
	// than behind a settings dialog.
	fUserStatusBox = new QComboBox(centralWidget);
	fUserStatusBox->setEditable(true);
	fUserStatusBox->setInsertPolicy(QComboBox::NoInsert);
	fUserStatusBox->setMinimumWidth(120);
	fUserStatusBox->addItems({tr("here"), tr("away"), tr("busy"),
		tr("back soon"), tr("idle")});
	connect(fUserStatusBox, &QComboBox::currentTextChanged,
		this, &MainWindow::_OnStatusChanged);
	connectionLayout->addWidget(fUserStatusBox);

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
	fResultsView->header()->setSectionResizeMode(QHeaderView::Interactive);

	// File gets the lion's share: it is the column people read, and letting it
	// be squeezed to an ellipsis by a long MIME type makes the list useless.
	fResultsView->setColumnWidth(FileResultModel::COLUMN_NAME, 340);
	fResultsView->setColumnWidth(FileResultModel::COLUMN_SIZE, 90);
	fResultsView->setColumnWidth(FileResultModel::COLUMN_USER, 130);
	fResultsView->setColumnWidth(FileResultModel::COLUMN_SERVER, 150);
	fResultsView->setColumnWidth(FileResultModel::COLUMN_MODIFIED, 140);
	fResultsView->setColumnWidth(FileResultModel::COLUMN_KIND, 180);
	fResultsView->setColumnWidth(FileResultModel::COLUMN_PATH, 200);

	// The Server column is noise until there is more than one of them.
	fResultsView->setColumnHidden(FileResultModel::COLUMN_SERVER, true);

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
	fTransfersView->header()->setStretchLastSection(false);
	fTransfersView->header()->setSectionResizeMode(QHeaderView::Interactive);
	fTransfersView->setColumnWidth(TransferModel::COLUMN_FILE, 280);
	fTransfersView->setColumnWidth(TransferModel::COLUMN_PROGRESS, 110);
	fTransfersView->setColumnWidth(TransferModel::COLUMN_SIZE, 90);
	fTransfersView->setColumnWidth(TransferModel::COLUMN_RATE, 90);
	fTransfersView->setColumnWidth(TransferModel::COLUMN_FROM, 130);
	fTransfersView->setColumnWidth(TransferModel::COLUMN_STATUS, 180);
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
	// Every section Interactive, because Qt makes Stretch and ResizeToContents
	// sections impossible for the user to drag. Sensible starting widths give
	// the same tidy first impression without taking the ability away, and the
	// layout is saved, so an adjustment sticks.
	QHeaderView* userListHeader = fUserListView->header();
	userListHeader->setStretchLastSection(false);
	userListHeader->setSectionResizeMode(QHeaderView::Interactive);
	fUserListView->setColumnWidth(UserListModel::COLUMN_NAME, 150);
	fUserListView->setColumnWidth(UserListModel::COLUMN_STATUS, 90);
	fUserListView->setColumnWidth(UserListModel::COLUMN_CLIENT, 140);
	fUserListView->setColumnWidth(UserListModel::COLUMN_FILES, 70);
	fUserListView->setColumnWidth(UserListModel::COLUMN_SERVER, 150);
	fUserListView->setColumnWidth(UserListModel::COLUMN_HOST, 140);

	// Hidden until there is more than one server, and set here rather than
	// beside the results view's equivalent -- fUserListView does not exist yet
	// at that point in this function, which is a segfault rather than a
	// no-op.
	fUserListView->setColumnHidden(UserListModel::COLUMN_SERVER, true);

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
		[this]() { fConnections.DisconnectAll(); });
	fDisconnectAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+K")));

	fileMenu->addAction(tr("Connect to &Additional Server..."), this,
		&MainWindow::_OnConnectAdditionalServer);
	fileMenu->addAction(tr("Disconnect &All"), this,
		&MainWindow::_OnDisconnectAll);

	fileMenu->addSeparator();

	QAction* settingsAction = fileMenu->addAction(tr("&Settings..."), this,
		&MainWindow::_OnShowSettings);
	// Spelled out rather than QKeySequence::Preferences, which has zero
	// bindings on X11 -- relying on it leaves the item with no shortcut at all.
	settingsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
	settingsAction->setMenuRole(QAction::PreferencesRole);

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
	_PopulateNameAndStatusLists();

	const QByteArray geometryData
		= GetByteArray(fSettings.GetRawMessage(), kSettingsFieldWindowGeometry);
	if (geometryData.isEmpty() == false)
		(void) restoreGeometry(geometryData);

	const QByteArray splitterData
		= GetByteArray(fSettings.GetRawMessage(), kSettingsFieldSplitterState);
	if (splitterData.isEmpty() == false)
		(void) fSplitter->restoreState(splitterData);

	_RestoreColumnLayouts();

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
	fSettings.SetUserName(ToMuscleString(_GetUserName()));
	fSettings.SetUserStatus(fSettings.GetUserStatus());

	PutByteArray(fSettings.GetRawMessage(), kSettingsFieldWindowGeometry,
		saveGeometry());
	PutByteArray(fSettings.GetRawMessage(), kSettingsFieldSplitterState,
		fSplitter->saveState());
	_SaveColumnLayouts();
	_SaveExtraServers();
	(void) fSettings.GetRawMessage().ReplaceBool(true, kSettingsFieldShowTimestamps,
		fShowTimestampsAction->isChecked());
	(void) fSettings.GetRawMessage().ReplaceBool(true, kSettingsFieldShowHostColumn,
		fShowHostColumnAction->isChecked());

	(void) fSettings.Save();
}


// #pragma mark - ServerConnectionListener


void
MainWindow::ConnectionStateChanged(ServerConnection* connection,
	ConnectionState state)
{
	if (state == CONNECTION_DISCONNECTED) {
		// Only this connection's users; another server may still be up.
		if (connection != nullptr) {
			fUserListModel->RemoveUsersForConnection(connection);
			fResultsModel->RemoveResultsForConnection(connection);
		}
		setWindowTitle(QLatin1String(HITUX_SHARE_NAME));
	}

	_UpdateConnectionWidgets();
	_UpdateQueryWidgets();
	_UpdateMultiServerUi();
	_UpdateStatusBar();
}


void
MainWindow::LocalSessionIdAssigned(ServerConnection* connection,
	const String& sessionId, const String& /*hostName*/)
{
	// Promote only now, not at connect time: reaching a session ID is the first point
	// at which we know the server is a working BeShare server and not just a socket
	// that happened to accept us.
	fSettings.RememberServer(connection->GetServerAddress());
	_PopulateServerList();

	// Deliberately the server's address, not the (hostName) the server reports back.
	// That value is the address the server sees US at -- i.e. the user's own public IP
	// -- which is both useless in a title bar and something they would not expect to
	// be putting on screen during a screen-share or a screenshot.
	setWindowTitle(tr("%1 -- %2 on %3")
		.arg(QLatin1String(HITUX_SHARE_NAME),
			ToQString(fSettings.GetUserName()),
			ToQString(connection->GetServerAddress())));

	_AppendLocalLine(LOG_INFORMATION_MESSAGE,
		tr("You are session %1.").arg(ToQString(sessionId)));

	// Only now: sharing publishes under our session, so it needs one to exist.
	_StartSharing();
}


void
MainWindow::UserUpdated(ServerConnection* connection, const UserRecord& user,
	bool isNewUser)
{
	fUserListModel->UpdateUser(connection,
		ToQString(connection->GetServerAddress()), user);
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
MainWindow::UserLeft(ServerConnection* connection, const UserRecord& user)
{
	fUserListModel->RemoveUser(connection, user.sessionId);

	// Their files went with them.  Dropping the lot in one operation matters:
	// a peer sharing thousands of files would otherwise arrive as thousands of
	// individual removals, each shifting every row after it.
	fResultsModel->RemoveResultsForSession(connection, user.sessionId);
	_UpdateResultCount();

	ChatMessage leaveMessage(LOG_USER_EVENT_MESSAGE, muscle::String());
	leaveMessage.text = ToMuscleString(tr("%1 has left.")
		.arg(ToQString(user.GetDisplayName())));
	fChatLogView->AppendChatMessage(leaveMessage);

	_UpdateStatusBar();
}


void
MainWindow::ChatMessageReceived(ServerConnection* connection,
	const ChatMessage& message)
{
	// Ignored users are dropped before anything else sees them: not logged, not
	// counted, not notified. A half-ignored user is worse than none.
	if (message.type == LOG_REMOTE_USER_CHAT_MESSAGE
			&& fIgnoreFilter.Matches(message.senderName,
				message.senderSessionId)) {
		return;
	}

	ChatMessage displayed = message;

	// With several servers connected, who said something is only half the
	// answer -- the same name on two servers is two different people.
	if (fConnections.GetCount() > 1 && connection != nullptr
			&& displayed.senderName.HasChars()) {
		displayed.senderName = displayed.senderName + "@"
			+ connection->GetServerAddress();
	}

	// A watched user's lines are marked for highlighting, so they stand out in a
	// busy room without a sound or a popup.
	if (message.type == LOG_REMOTE_USER_CHAT_MESSAGE
			&& fWatchFilter.Matches(message.senderName,
				message.senderSessionId)) {
		displayed.isHighlighted = true;
	}

	fChatLogView->AppendChatMessage(displayed);
	fChatLogger.Log(displayed);
	_MaybeNotifyAboutChat(message);
}


void
MainWindow::PingReplyReceived(ServerConnection* /*connection*/,
	const UserRecord& user, uint64 roundTripMicroseconds,
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
MainWindow::QueryResultAdded(ServerConnection* connection,
	const FileResult& result)
{
	FileResultModel::Entry entry;
	entry.result = result;
	entry.connection = connection;
	entry.serverName = ToQString(connection->GetServerAddress());
	entry.sharerName = ToQString(
		connection->GetUsers().GetDisplayNameForSession(result.sessionId));

	fPendingResults.append(entry);
	if (fResultFlushTimer->isActive() == false)
		fResultFlushTimer->start();
}


void
MainWindow::QueryResultRemoved(ServerConnection* connection,
	const String& sessionId, const String& fileName)
{
	fResultsModel->RemoveResult(connection, sessionId, fileName);
	_UpdateResultCount();
}


void
MainWindow::QueryResultsCleared(ServerConnection* connection)
{
	// Only this connection's rows: another server's query may still be live.
	for (int i = fPendingResults.size() - 1; i >= 0; i--) {
		if (fPendingResults.at(i).connection == connection)
			fPendingResults.removeAt(i);
	}

	fResultsModel->RemoveResultsForConnection(connection);
	_UpdateResultCount();
}


void
MainWindow::QuerySweepStateChanged(ServerConnection* /*connection*/,
	bool isSweeping)
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
		// One listening socket serves every server's peers, so it reports the
		// primary connection's session ID. A peer only uses it for display.
		const ServerConnection* primary = _Primary();
		fUploadServer.SetLocalIdentity(
			primary != nullptr ? primary->GetLocalSessionId() : muscle::String(),
			fSettings.GetUserName());
		fConnections.SetAdvertisedPort(listenPort);
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
	fConnections.SetAdvertisedPort(0);
	fConnections.UnpublishAllSharedFiles();
	fConnections.PublishSharedFileCountOnAll(0);

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
		fConnections.PublishSharedFilesOnAll(discovered);
		for (uint32 i = 0; i < discovered.GetNumItems(); i++)
			(void) fSharedFiles.Put(discovered[i].fileName, discovered[i]);

		fUploadServer.SetSharedFiles(fSharedFiles);
		fSharedFileCount += discovered.GetNumItems();
		_UpdateStatusBar();
	}

	if (fShareScanner.TakeScanFinished()) {
		fConnections.PublishSharedFileCountOnAll(fSharedFileCount);

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
MainWindow::_OnStatusChanged()
{
	const QString status = fUserStatusBox->currentText().trimmed();
	if (status.isEmpty())
		return;

	// SetLocalUserStatus republishes only when the value actually differs, so
	// this is safe to call on every keystroke.
	fConnections.SetLocalUserStatus(ToMuscleString(status));
	fSettings.SetUserStatus(ToMuscleString(status));
	fSettings.RememberStatus(ToMuscleString(status));
}


void
MainWindow::_OnConnectAdditionalServer()
{
	bool accepted = false;
	const QString serverAddress = QInputDialog::getText(this,
		tr("Connect to Additional Server"),
		tr("Server address:"), QLineEdit::Normal, QString(), &accepted).trimmed();
	if (accepted == false || serverAddress.isEmpty())
		return;

	if (fConnections.FindByAddress(ToMuscleString(serverAddress)) != nullptr) {
		_AppendLocalLine(LOG_WARNING_MESSAGE,
			tr("Already connected to %1.").arg(serverAddress));
		return;
	}

	ServerConnection* connection = fConnections.AddConnection(
		ToMuscleString(serverAddress), kDefaultServerPort);
	if (connection == nullptr) {
		_AppendLocalLine(LOG_ERROR_MESSAGE,
			tr("Too many connections already."));
		return;
	}

	_ApplySettings();
	_SaveExtraServers();
	_UpdateMultiServerUi();
}


void
MainWindow::_OnDisconnectAll()
{
	fConnections.DisconnectAll();
}


void
MainWindow::_UpdateMultiServerUi()
{
	// The Server column and the per-line server tag are noise with one
	// connection and essential with two, so they appear exactly when they
	// start meaning something.
	const bool showServer = (fConnections.GetCount() > 1);
	fResultsView->setColumnHidden(FileResultModel::COLUMN_SERVER,
		showServer == false);
	fUserListView->setColumnHidden(UserListModel::COLUMN_SERVER,
		showServer == false);
}


void
MainWindow::_RestoreExtraServers()
{
	const Queue<muscle::String> extras = fSettings.GetExtraServers();
	for (uint32 i = 0; i < extras.GetNumItems(); i++)
		(void) fConnections.AddConnection(extras[i], kDefaultServerPort);

	_UpdateMultiServerUi();
}


void
MainWindow::_SaveExtraServers()
{
	// Everything except the first, which is the one the Server field shows and
	// which is already stored as "server".
	Queue<muscle::String> extras;
	for (uint32 i = 1; i < fConnections.GetCount(); i++)
		(void) extras.AddTail(fConnections.GetAt(i)->GetServerAddress());

	fSettings.SetExtraServers(extras);
}


void
MainWindow::_OnServerListReceived(const QStringList& serversToAdd,
	const QStringList& serversToRemove)
{
	Queue<muscle::String> serverList = fSettings.GetServerList();
	uint32 addedCount = 0;
	uint32 removedCount = 0;

	for (const QString& server : serversToRemove) {
		const muscle::String candidate = ToMuscleString(server);

		// Never remove the server we are using or the one selected: a list
		// fetched over the network should not be able to take away what is
		// working right now.
		bool isInUse = candidate.EqualsIgnoreCase(
			ToMuscleString(_GetSelectedServerAddress()));
		for (uint32 i = 0; i < fConnections.GetCount() && isInUse == false; i++) {
			if (candidate.EqualsIgnoreCase(
					fConnections.GetAt(i)->GetServerAddress())) {
				isInUse = true;
			}
		}

		if (isInUse)
			continue;

		for (int32 i = (int32) serverList.GetNumItems() - 1; i >= 0; i--) {
			if (serverList[(uint32) i].EqualsIgnoreCase(candidate)) {
				(void) serverList.RemoveItemAt((uint32) i);
				removedCount++;
			}
		}
	}

	for (const QString& server : serversToAdd) {
		const muscle::String candidate = ToMuscleString(server);

		bool alreadyKnown = false;
		for (uint32 i = 0; i < serverList.GetNumItems(); i++) {
			if (serverList[i].EqualsIgnoreCase(candidate)) {
				alreadyKnown = true;
				break;
			}
		}

		// Appended rather than promoted: this list suggests what exists, it
		// does not get to decide what you connect to next.
		if (alreadyKnown == false && serverList.AddTail(candidate).IsOK())
			addedCount++;
	}

	if (addedCount == 0 && removedCount == 0) {
		// Says how many entries the file actually contained, so a parse that
		// silently finds nothing looks different from a list with nothing new
		// in it. Those two were indistinguishable and one of them was a bug.
		_AppendLocalLine(LOG_INFORMATION_MESSAGE,
			tr("Server list checked: %n server(s) listed, nothing new.", "",
				serversToAdd.size()));
		return;
	}

	fSettings.SetServerList(serverList);
	(void) fSettings.Save();
	_PopulateServerList();

	_AppendLocalLine(LOG_INFORMATION_MESSAGE,
		tr("Server list updated: %n added", "", (int) addedCount)
			+ tr(", %n removed.", "", (int) removedCount));
}


void
MainWindow::_OnShowSettings()
{
	SettingsDialog dialog(fSettings, this);
	if (dialog.exec() != QDialog::Accepted)
		return;

	const bool wasSharing = fSettings.GetFileSharingEnabled();
	dialog.ApplyToSettings();

	_ApplySettings();

	// Sharing has to be restarted rather than adjusted: the folder or the
	// firewall flag may have changed, and both decide what gets published.
	const bool isSharing = fSettings.GetFileSharingEnabled();
	if (wasSharing || isSharing) {
		_StopSharing();
		if (isSharing && fConnections.IsAnyConnected())
			_StartSharing();
	}

	fFileSharingAction->setChecked(isSharing);
	fNotificationsAction->setChecked(fSettings.GetNotificationsEnabled());
	(void) fSettings.Save();
}


void
MainWindow::_SetUserStatus(const muscle::String& status)
{
	fConnections.SetLocalUserStatus(status);
	fSettings.SetUserStatus(status);
	fSettings.RememberStatus(status);

	const QSignalBlocker blocker(fUserStatusBox);
	fUserStatusBox->setCurrentText(ToQString(status));

	_AppendLocalLine(LOG_INFORMATION_MESSAGE,
		tr("Status set to %1.").arg(ToQString(status)));
}


void
MainWindow::_HandleFilterCommand(UserFilterSet& filter,
	const muscle::String& argument, const QString& filterName,
	void (ApplicationSettings::*setter)(const muscle::String&))
{
	// No argument reports the current list; that is more useful than an error,
	// and it is how you find out what you set three weeks ago.
	if (argument.IsEmpty()) {
		_AppendLocalLine(LOG_INFORMATION_MESSAGE, filter.IsEmpty()
			? tr("%1 list is empty.").arg(filterName)
			: tr("%1 list: %2").arg(filterName,
				ToQString(filter.GetPattern())));
		return;
	}

	if (argument == "-") {
		filter.Clear();
		(fSettings.*setter)(filter.GetPattern());
		(void) fSettings.Save();
		_AppendLocalLine(LOG_INFORMATION_MESSAGE,
			tr("%1 list cleared.").arg(filterName));
		return;
	}

	filter.SetPattern(argument);
	(fSettings.*setter)(filter.GetPattern());
	(void) fSettings.Save();

	_AppendLocalLine(LOG_INFORMATION_MESSAGE,
		tr("%1 list set to: %2").arg(filterName, ToQString(filter.GetPattern())));
}


void
MainWindow::_HandleAliasCommand(const ChatCommand& command)
{
	if (command.type == CHAT_COMMAND_UNALIAS) {
		if (command.argument.IsEmpty()) {
			_AppendLocalLine(LOG_WARNING_MESSAGE, tr("Usage: /unalias <name>"));
			return;
		}

		_AppendLocalLine(LOG_INFORMATION_MESSAGE,
			fAliases.RemoveAlias(command.argument)
				? tr("Alias /%1 removed.").arg(ToQString(command.argument))
				: tr("No alias called /%1.").arg(ToQString(command.argument)));
	} else {
		if (command.target.IsEmpty()) {
			if (fAliases.GetCount() == 0) {
				_AppendLocalLine(LOG_INFORMATION_MESSAGE,
					tr("No aliases defined. Try /alias hi /me waves"));
				return;
			}

			_AppendLocalLine(LOG_INFORMATION_MESSAGE, tr("Aliases:"));
			const Hashtable<muscle::String, muscle::String>& aliases
				= fAliases.GetAliases();
			for (auto iterator = aliases.GetIterator(); iterator.HasData();
					iterator++) {
				_AppendLocalLine(LOG_INFORMATION_MESSAGE,
					QStringLiteral("  /%1 -> %2")
						.arg(ToQString(iterator.GetKey()),
							ToQString(iterator.GetValue())));
			}
			return;
		}

		if (command.argument.IsEmpty()) {
			_AppendLocalLine(LOG_WARNING_MESSAGE,
				tr("Usage: /alias <name> <what it expands to>"));
			return;
		}

		fAliases.SetAlias(command.target, command.argument);
		_AppendLocalLine(LOG_INFORMATION_MESSAGE,
			tr("/%1 now expands to: %2").arg(ToQString(command.target),
				ToQString(command.argument)));
	}

	fSettings.SetAliases(fAliases.GetAliases());
	(void) fSettings.Save();
}


void
MainWindow::_ApplySettings()
{
	// One place where stored settings become running behaviour, so a new
	// setting cannot be half-wired: if it is not applied here, it does nothing.
	fConnections.SetLocalUserName(fSettings.GetUserName());
	fConnections.SetLocalUserStatus(fSettings.GetUserStatus());
	fConnections.SetFirewalled(fSettings.GetFirewalled());

	fDownloads.SetDownloadDirectory(fSettings.GetDownloadDirectory());
	fDownloads.SetRetainFilePaths(fSettings.GetRetainFilePaths());
	fDownloads.SetMaxSimultaneousDownloads(
		fSettings.GetMaxSimultaneousDownloads());
	fDownloads.SetRateLimit(fSettings.GetMaxDownloadRate());
	fDownloads.SetAutoClearFinished(fSettings.GetAutoClearFinishedTransfers());

	fUploadServer.SetMaxSimultaneousUploads(
		fSettings.GetMaxSimultaneousUploads());
	fUploadServer.SetRateLimit(fSettings.GetMaxUploadRate());

	fNotifier->SetEnabled(fSettings.GetNotificationsEnabled());
	fChatLogView->SetFontPointSize(fSettings.GetChatFontPointSize());

	fChatLogger.SetLogDirectory(fSettings.GetLogDirectory());
	fChatLogger.SetServerName(fSettings.GetServerAddress());
	fChatLogger.SetEnabled(fSettings.GetChatLoggingEnabled());

	fIgnoreFilter.SetPattern(fSettings.GetIgnorePattern());
	fWatchFilter.SetPattern(fSettings.GetWatchPattern());
	fAutoPrivFilter.SetPattern(fSettings.GetAutoPrivPattern());

	fAliases.Clear();
	const Hashtable<muscle::String, muscle::String> storedAliases
		= fSettings.GetAliases();
	for (auto iterator = storedAliases.GetIterator(); iterator.HasData();
			iterator++) {
		fAliases.SetAlias(iterator.GetKey(), iterator.GetValue());
	}

	_PopulateNameAndStatusLists();

	// Block signals so restoring the control does not look like the user
	// changing it, which would republish and re-save on every apply.
	const QSignalBlocker blocker(fUserStatusBox);
	fUserStatusBox->setCurrentText(ToQString(fSettings.GetUserStatus()));
}


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

	if (fSettings.GetFileSharingEnabled() && fConnections.IsAnyConnected()) {
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

	if (fConnections.IsAnyConnected())
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
	if (fConnections.IsAnyConnected() == false)
		_ConnectToConfiguredServer();
	else
		fConnections.DisconnectAll();
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
	fConnections.PerformIdleTasks();
	fDownloads.PerformIdleTasks();
	_DrainShareScanner();
}


void
MainWindow::_OnQueryButtonClicked()
{
	if (fConnections.IsAnyQueryActive())
		fConnections.StopQueryOnAll();
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

	// A user's name arrives separately from their files, so results shared by
	// someone whose name node has not landed yet would keep showing a bare
	// session ID until it does.
	if (fUserNamesDirty) {
		for (uint32 i = 0; i < fConnections.GetCount(); i++) {
			ServerConnection* connection = fConnections.GetAt(i);
			const Hashtable<muscle::String, UserRecord>& users
				= connection->GetUsers().GetUsers();
			for (auto iterator = users.GetIterator(); iterator.HasData();
					iterator++) {
				fResultsModel->UpdateSharerName(connection, iterator.GetKey(),
					ToQString(iterator.GetValue().GetDisplayName()));
			}
		}

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
	// Aliases are expanded before parsing, so an alias may expand to any command
	// including another alias's text -- but only once, so nothing can loop.
	const muscle::String expanded = fAliases.Expand(ToMuscleString(input));
	const ChatCommand command = ChatCommandParser::Parse(expanded);

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

			fConnections.SetLocalUserName(command.argument);
			fSettings.SetUserName(command.argument);

			// /nick is how people actually change their name, so this is where
			// the history has to be recorded -- doing it only at connect time
			// meant every name ever used from the chat line was forgotten.
			fSettings.RememberUserName(command.argument);

			fUserNameBox->setCurrentText(ToQString(command.argument));
			_PopulateNameAndStatusLists();
			_AppendLocalLine(LOG_INFORMATION_MESSAGE,
				tr("You are now known as %1.").arg(ToQString(command.argument)));
			break;
		}

		case CHAT_COMMAND_STATUS:
			_SetUserStatus(command.argument.HasChars()
				? command.argument : muscle::String("here"));
			break;

		case CHAT_COMMAND_AWAY:
			_SetUserStatus(command.argument.HasChars()
				? command.argument : fSettings.GetAwayStatus());
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
			fConnections.DisconnectAll();
			break;

		case CHAT_COMMAND_START_QUERY:
			if (command.argument.HasChars())
				fQueryField->setText(ToQString(command.argument));

			_StartQuery();
			break;

		case CHAT_COMMAND_STOP_QUERY:
			fConnections.StopQueryOnAll();
			_UpdateQueryWidgets();
			break;

		case CHAT_COMMAND_GET:
			_DownloadSelectedResults();
			break;

		case CHAT_COMMAND_IGNORE:
			_HandleFilterCommand(fIgnoreFilter, command.argument, tr("Ignore"),
				&ApplicationSettings::SetIgnorePattern);
			break;

		case CHAT_COMMAND_WATCH:
			_HandleFilterCommand(fWatchFilter, command.argument, tr("Watch"),
				&ApplicationSettings::SetWatchPattern);
			break;

		case CHAT_COMMAND_AUTOPRIV:
			_HandleFilterCommand(fAutoPrivFilter, command.argument,
				tr("Always-notify"), &ApplicationSettings::SetAutoPrivPattern);
			break;

		case CHAT_COMMAND_ALIAS:
		case CHAT_COMMAND_UNALIAS:
			_HandleAliasCommand(command);
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
	if (fConnections.IsAnyConnected() == false) {
		_AppendLocalLine(LOG_ERROR_MESSAGE, tr("Not connected to a server."));
		return;
	}

	fConnections.SendChatToAll(ToMuscleString(text));

	// We drop the server's echo of our own lines, so echo here or the sender never
	// sees what they just said.
	ChatMessage localEcho;
	localEcho.type = LOG_LOCAL_USER_CHAT_MESSAGE;
	localEcho.senderName = fSettings.GetUserName();
	localEcho.isFromLocalUser = true;
	localEcho.isAction = isAction;

	// An action goes out with its "/me " prefix intact -- that prefix is the wire
	// format -- but it must not be shown twice locally.
	localEcho.text = isAction
		? ToMuscleString(text.mid((int) strlen(BESHARE_ACTION_PREFIX)))
		: ToMuscleString(text);

	fChatLogView->AppendChatMessage(localEcho);
	fChatLogger.Log(localEcho);
}


void
MainWindow::_SendPrivateMessage(const QString& target, const QString& text)
{
	if (fConnections.IsAnyConnected() == false) {
		_AppendLocalLine(LOG_ERROR_MESSAGE, tr("Not connected to a server."));
		return;
	}

	// A name can exist on more than one server, and those are different people.
	// Each match carries its own connection, so the message goes to the right
	// server rather than to whichever one happens to be primary.
	const Queue<ResolvedUser> targets
		= fConnections.ResolveToUsers(ToMuscleString(target));
	if (targets.IsEmpty()) {
		_AppendLocalLine(LOG_ERROR_MESSAGE, tr("No such user: %1").arg(target));
		return;
	}

	for (uint32 i = 0; i < targets.GetNumItems(); i++) {
		const ResolvedUser& user = targets[i];
		user.connection->SendChatText(user.sessionId, ToMuscleString(text));

		ChatMessage localEcho;
		localEcho.type = LOG_LOCAL_USER_CHAT_MESSAGE;
		localEcho.isPrivate = true;
		localEcho.isFromLocalUser = true;
		localEcho.senderName = ToMuscleString(tr("to %1")
			.arg(ToQString(user.displayName)));
		localEcho.text = ToMuscleString(text);

		fChatLogView->AppendChatMessage(localEcho);
		fChatLogger.Log(localEcho);
	}
}


void
MainWindow::_PingUser(const QString& target)
{
	if (fConnections.IsAnyConnected() == false) {
		_AppendLocalLine(LOG_ERROR_MESSAGE, tr("Not connected to a server."));
		return;
	}

	const Queue<ResolvedUser> targets
		= fConnections.ResolveToUsers(ToMuscleString(target));
	if (targets.IsEmpty()) {
		_AppendLocalLine(LOG_ERROR_MESSAGE, tr("No such user: %1").arg(target));
		return;
	}

	for (uint32 i = 0; i < targets.GetNumItems(); i++)
		targets[i].connection->SendPing(targets[i].sessionId);
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
	if (fConnections.IsAnyConnected() == false) {
		_AppendLocalLine(LOG_INFORMATION_MESSAGE, tr("Not connected."));
		return;
	}

	_AppendLocalLine(LOG_INFORMATION_MESSAGE,
		tr("Connected as %1:").arg(ToQString(fSettings.GetUserName())));

	for (uint32 i = 0; i < fConnections.GetCount(); i++) {
		const ServerConnection* connection = fConnections.GetAt(i);
		if (connection->IsConnected() == false) {
			_AppendLocalLine(LOG_INFORMATION_MESSAGE,
				tr("  %1 -- not connected")
					.arg(ToQString(connection->GetServerAddress())));
			continue;
		}

		_AppendLocalLine(LOG_INFORMATION_MESSAGE,
			tr("  %1:%2 -- session %3, %n user(s)", "",
				(int) connection->GetUsers().GetUserCount())
				.arg(ToQString(connection->GetServerAddress()))
				.arg(connection->GetServerPort())
				.arg(ToQString(connection->GetLocalSessionId())));
	}
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

	const QString userName = _GetUserName();
	if (userName.isEmpty()) {
		_AppendLocalLine(LOG_ERROR_MESSAGE, tr("Enter a user name first."));
		return;
	}

	fConnections.SetLocalUserName(ToMuscleString(userName));
	fSettings.RememberUserName(ToMuscleString(userName));

	(void) fConnections.AddConnection(ToMuscleString(serverAddress),
		(uint16) fServerPortField->value());
}


QString
MainWindow::_GetUserName() const
{
	return fUserNameBox->currentText().trimmed();
}


void
MainWindow::_PopulateNameAndStatusLists()
{
	// Repopulating clears the edit field, so what the user has typed is put
	// back afterwards rather than silently discarded.
	const QString currentName = fUserNameBox->currentText();
	fUserNameBox->clear();

	const Queue<muscle::String> names = fSettings.GetRecentUserNames();
	for (uint32 i = 0; i < names.GetNumItems(); i++)
		fUserNameBox->addItem(ToQString(names[i]));

	fUserNameBox->setCurrentText(currentName.isEmpty()
		? ToQString(fSettings.GetUserName()) : currentName);

	const QSignalBlocker blocker(fUserStatusBox);
	const QString currentStatus = fUserStatusBox->currentText();
	fUserStatusBox->clear();

	// Remembered statuses first, then the built-in ones that are not already
	// there -- so a status somebody actually uses is at the top of the list.
	QStringList entries;
	const Queue<muscle::String> statuses = fSettings.GetRecentStatuses();
	for (uint32 i = 0; i < statuses.GetNumItems(); i++)
		entries.append(ToQString(statuses[i]));

	for (const QString& preset : {tr("here"), tr("away"), tr("busy"),
			tr("back soon"), tr("idle")}) {
		if (entries.contains(preset, Qt::CaseInsensitive) == false)
			entries.append(preset);
	}

	fUserStatusBox->addItems(entries);
	fUserStatusBox->setCurrentText(currentStatus.isEmpty()
		? ToQString(fSettings.GetUserStatus()) : currentStatus);
}


void
MainWindow::_SaveColumnLayouts()
{
	const QByteArray results = fResultsView->header()->saveState();
	fSettings.SetColumnLayout("results.v2", results.constData(),
		(uint32) results.size());

	const QByteArray users = fUserListView->header()->saveState();
	fSettings.SetColumnLayout("users.v2", users.constData(), (uint32) users.size());

	const QByteArray transfers = fTransfersView->header()->saveState();
	fSettings.SetColumnLayout("transfers.v2", transfers.constData(),
		(uint32) transfers.size());
}


void
MainWindow::_RestoreColumnLayouts()
{
	const struct { const char* name; QTreeView* view; } views[] = {
		{"results.v2", fResultsView},
		{"users.v2", fUserListView},
		{"transfers", fTransfersView}
	};

	for (const auto& entry : views) {
		const ByteBufferRef stored = fSettings.GetColumnLayout(entry.name);
		if (stored() == NULL || stored()->GetNumBytes() == 0)
			continue;

		// A layout saved by an older build may have a different column count,
		// in which case restoreState refuses and the defaults stand.
		(void) entry.view->header()->restoreState(
			QByteArray(reinterpret_cast<const char*>(stored()->GetBuffer()),
				(int) stored()->GetNumBytes()));
	}
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
	if (fConnections.IsAnyConnected() == false) {
		_AppendLocalLine(LOG_ERROR_MESSAGE, tr("Not connected to a server."));
		return;
	}

	const QString pattern = fQueryField->text().trimmed();

	// An empty box means "everything", which is a legitimate thing to ask for on
	// a small server and a very large thing to ask for on a busy one.
	fConnections.StartQueryOnAll(muscle::String("*"),
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
	QVector<const void*> chosenConnections;
	for (const QModelIndex& proxyIndex : selected) {
		const QModelIndex sourceIndex
			= fResultsProxyModel->mapToSource(proxyIndex);
		const FileResultModel::Entry* entry
			= fResultsModel->GetEntryForRow(sourceIndex.row());
		if (entry != nullptr) {
			(void) chosen.AddTail(entry->result);
			chosenConnections.append(entry->connection);
		}
	}

	// Settings can have changed since construction, and a download that ignored
	// the folder the user just chose would be its own bug report.
	fDownloads.SetDownloadDirectory(fSettings.GetDownloadDirectory());
	fDownloads.SetRetainFilePaths(fSettings.GetRetainFilePaths());

	// Group by connection before starting: a session ID only means something on
	// the server that issued it, so a result from server A must be fetched
	// using A's user list and A's session ID.
	for (uint32 i = 0; i < fConnections.GetCount(); i++) {
		ServerConnection* connection = fConnections.GetAt(i);

		Queue<FileResult> forThisServer;
		for (uint32 j = 0; j < chosen.GetNumItems(); j++) {
			if (chosenConnections[j] == connection)
				(void) forThisServer.AddTail(chosen[j]);
		}

		if (forThisServer.HasItems()) {
			fDownloads.StartDownloads(forThisServer, connection->GetUsers(),
				connection->GetLocalSessionId(), fSettings.GetUserName());
		}
	}
}


void
MainWindow::_UpdateQueryWidgets()
{
	const bool isActive = fConnections.IsAnyQueryActive();
	fQueryButton->setText(isActive ? tr("Stop") : tr("Search"));
	fQueryButton->setEnabled(fConnections.IsAnyConnected());
	fQueryField->setEnabled(fConnections.IsAnyConnected());
}


void
MainWindow::_UpdateResultCount()
{
	const int count = fResultsModel->rowCount();
	if (count == 0 && fConnections.IsAnyQueryActive() == false) {
		fResultCountLabel->clear();
		return;
	}

	fResultCountLabel->setText(fConnections.IsAnyQueryActive()
		? tr("%n result(s), still listening", "", count)
		: tr("%n result(s)", "", count));
}


void
MainWindow::_UpdateConnectionWidgets()
{
	const ServerConnection* primary = _Primary();
	const ConnectionState state = (primary != nullptr)
		? primary->GetConnectionState() : CONNECTION_DISCONNECTED;
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
	const ServerConnection* statusConnection = _Primary();
	const ConnectionState statusState = (statusConnection != nullptr)
		? statusConnection->GetConnectionState() : CONNECTION_DISCONNECTED;
	const QString statusAddress = (statusConnection != nullptr)
		? ToQString(statusConnection->GetServerAddress()) : QString();

	switch (statusState) {
		case CONNECTION_DISCONNECTED:
			fStatusLabel->setText(tr("Not connected"));
			break;

		case CONNECTION_CONNECTING:
			fStatusLabel->setText(tr("Connecting to %1...").arg(statusAddress));
			break;

		case CONNECTION_CONNECTED:
			// With several servers the count matters more than any one name.
			fStatusLabel->setText(fConnections.GetConnectedCount() > 1
				? tr("Connected to %n server(s)", "",
					(int) fConnections.GetConnectedCount())
				: tr("Connected to %1").arg(statusAddress));
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

	const uint32 userCount = fConnections.GetTotalUserCount();
	fUserCountLabel->setText(fConnections.IsAnyConnected()
		? tr("%n user(s)", "", (int) userCount) : QString());
}


QStringList
MainWindow::_GetCompletionCandidates(const QString& prefix) const
{
	QStringList candidates;

	for (uint32 i = 0; i < fConnections.GetCount(); i++) {
		const Hashtable<muscle::String, UserRecord>& users
			= fConnections.GetAt(i)->GetUsers().GetUsers();
		for (auto iterator = users.GetIterator(); iterator.HasData();
				iterator++) {
			const QString userName = ToQString(iterator.GetValue().userName);
			// The same person on two servers is two entries, but completing
			// their name twice would be silly.
			if (userName.isEmpty() == false
					&& userName.startsWith(prefix, Qt::CaseInsensitive)
					&& candidates.contains(userName) == false) {
				candidates.append(userName);
			}
		}
	}

	candidates.sort(Qt::CaseInsensitive);
	return candidates;
}


bool
MainWindow::_MentionsLocalUser(const muscle::String& text) const
{
	const QString localName = ToQString(fSettings.GetUserName()).trimmed();
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

	if (message.type != LOG_REMOTE_USER_CHAT_MESSAGE)
		return;

	const QString sender = ToQString(message.senderName);

	// An autopriv user is the one case that interrupts even a focused window.
	// HiShare opens a private chat window for these people; we show private
	// messages inline, so the closest honest equivalent is to always announce
	// them rather than to pretend a window appeared.
	if (fAutoPrivFilter.Matches(message.senderName, message.senderSessionId)) {
		fNotifier->Notify(DesktopNotifier::CATEGORY_CHAT,
			tr("%1 says").arg(sender), ToQString(message.text));
		return;
	}

	// Everything below is noise if the user is already looking at the window.
	if (_UserIsLookingAtUs())
		return;

	if (fWatchFilter.Matches(message.senderName, message.senderSessionId)) {
		fNotifier->Notify(DesktopNotifier::CATEGORY_CHAT,
			tr("%1 says").arg(sender), ToQString(message.text));
		return;
	}

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
	fConnections.SetListener(nullptr);
	fConnections.DisconnectAll();

	QMainWindow::closeEvent(event);
}


}  // namespace hitux

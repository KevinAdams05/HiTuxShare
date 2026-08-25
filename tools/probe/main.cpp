/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "core/ApplicationSettings.h"
#include "core/ChatCommandParser.h"
#include "core/FileDownload.h"
#include "core/FormatUtilities.h"
#include "core/HiTuxShareVersion.h"
#include "core/ServerConnection.h"
#include "core/FileUploadServer.h"
#include "core/ShareScanner.h"

#include "dataio/StdinDataIO.h"
#include "iogateway/PlainTextMessageIOGateway.h"
#include "system/SetupSystem.h"
#include "util/SocketCallbackMechanism.h"
#include "util/MiscUtilityFunctions.h"
#include "util/SocketMultiplexer.h"

#include <stdio.h>

using namespace muscle;
using namespace hitux;


namespace {


/** Prints everything the connection reports straight to stdout.
  *
  * There is deliberately no formatting cleverness here: the point of the probe is to
  * show what actually arrived, so that the GUI can be compared against it.
  */
class ProbeListener : public ServerConnectionListener,
	public FileDownloadListener, public FileUploadServerListener
{
public:
	ProbeListener()
		:
		fUsers(NULL),
		fScanner(NULL),
		fShouldStartScan(false),
		fLastUploadCount(0),
		fResultCount(0),
		fLastProgressTenths(-1),
		fShouldQuit(false)
	{
	}

	void SetUserRegistry(const UserRegistry* users) { fUsers = users; }

	bool ShouldQuit() const { return fShouldQuit; }
	void RequestQuit() { fShouldQuit = true; }

	virtual void ConnectionStateChanged(ConnectionState state)
	{
		switch (state) {
			case CONNECTION_DISCONNECTED:
				printf("*** Disconnected.\n");
				break;

			case CONNECTION_CONNECTING:
				printf("*** Connecting...\n");
				break;

			case CONNECTION_CONNECTED:
				printf("*** Connected.\n");
				break;
		}

		fflush(stdout);
	}

	void SetScanner(ShareScanner* scanner) { fScanner = scanner; }
	bool ShouldStartScan() const { return fShouldStartScan; }
	void ClearStartScan() { fShouldStartScan = false; }

	virtual void LocalSessionIdAssigned(const String& sessionId,
		const String& /*hostName*/)
	{
		fShouldStartScan = true;
		// Deliberately not printing the hostName the server reports: that is the
		// address the server sees US at, i.e. this machine's public IP, and probe
		// output is exactly the sort of thing that gets pasted into a bug report.
		printf("*** We are session %s.\n", sessionId());
		fflush(stdout);
	}

	virtual void UserUpdated(const UserRecord& user, bool isNewUser)
	{
		if (isNewUser == false)
			return;

		printf("*** (%s) %s joined", user.sessionId(), user.GetDisplayName()());
		if (user.clientVersion.HasChars())
			printf(" [%s]", user.clientVersion());

		printf("\n");
		fflush(stdout);
	}

	virtual void UserLeft(const UserRecord& user)
	{
		printf("*** (%s) %s left.\n", user.sessionId(), user.GetDisplayName()());
		fflush(stdout);
	}

	virtual void ChatMessageReceived(const ChatMessage& message)
	{
		const char* privateMarker = message.isPrivate ? "[PRIVATE] " : "";

		if (message.isAction) {
			printf("%s* %s %s\n", privateMarker, message.senderName(), message.text());
		} else if (message.senderName.HasChars()) {
			printf("%s<%s> %s\n", privateMarker, message.senderName(), message.text());
		} else {
			printf("*** %s\n", message.text());
		}

		fflush(stdout);
	}

	virtual void QueryResultAdded(const FileResult& result)
	{
		(void) fResults.AddTail(result);

		printf("  [%2u] %-36s %10s  %s%s\n",
			(unsigned) fResults.GetNumItems() - 1, result.fileName(),
			FormatByteSize(result.fileSize)(),
			fUsers != NULL
				? fUsers->GetDisplayNameForSession(result.sessionId)()
				: result.sessionId(),
			result.isFirewalled ? " [firewalled]" : "");
		fflush(stdout);
		fResultCount++;
	}

	virtual void QueryResultRemoved(const String& sessionId, const String& fileName)
	{
		printf("  -- %s (from %s) is no longer shared\n", fileName(), sessionId());
		fflush(stdout);
		if (fResultCount > 0)
			fResultCount--;
	}

	virtual void QueryResultsCleared()
	{
		fResultCount = 0;
		fResults.Clear();
	}

	const Queue<FileResult>& GetResults() const { return fResults; }

	// FileDownloadListener
	virtual void DownloadStateChanged(FileDownload* download)
	{
		switch (download->GetState()) {
			case DOWNLOAD_CONNECTING:
				printf("*** Connecting to peer...\n");
				break;

			case DOWNLOAD_REQUESTING:
				printf("*** Connected; asking for the file...\n");
				break;

			case DOWNLOAD_QUEUED_REMOTELY:
				printf("*** The peer has put us on their waiting list.\n");
				break;

			case DOWNLOAD_TRANSFERRING:
				printf("*** Receiving %s (%s)...\n",
					download->GetCurrentFileName()(),
					FormatByteSize(download->GetCurrentFileSize())());
				break;

			case DOWNLOAD_FINISHED:
				printf("*** Download finished: %u file(s), %s.\n",
					(unsigned) download->GetCompletedFileCount(),
					FormatByteSize(download->GetTotalBytesDone())());
				break;

			case DOWNLOAD_FAILED:
				printf("*** Download failed: %s\n", download->GetErrorText()());
				break;

			default:
				break;
		}

		fflush(stdout);
	}

	virtual void DownloadProgress(FileDownload* download)
	{
		// One line per 10%, so a big file does not scroll the terminal away.
		const int64 size = download->GetCurrentFileSize();
		if (size <= 0)
			return;

		const int tenths = (int) ((download->GetCurrentFileBytesDone() * 10) / size);
		if (tenths == fLastProgressTenths)
			return;

		fLastProgressTenths = tenths;
		printf("      %3d%%  %s  %s\n", tenths * 10,
			FormatByteSize(download->GetCurrentFileBytesDone())(),
			FormatTransferRate(download->GetBytesPerSecond())());
		fflush(stdout);
	}

	virtual void DownloadFileCompleted(FileDownload* download,
		const String& localPath)
	{
		(void) download;
		fLastProgressTenths = -1;
		printf("*** Saved to %s\n", localPath());
		fflush(stdout);
	}

	virtual void QuerySweepStateChanged(bool isSweeping)
	{
		printf(isSweeping
			? "*** Searching...\n"
			: "*** Search complete (%u result(s) so far; more arrive live).\n",
			(unsigned) fResultCount);
		fflush(stdout);
	}

	// FileUploadServerListener
	virtual void UploadsChanged(FileUploadServer* server)
	{
		const uint32 count = server->GetActiveUploadCount();
		if (count != fLastUploadCount) {
			fLastUploadCount = count;
			printf("*** %u peer(s) connected to us.\n", (unsigned) count);
			fflush(stdout);
		}
	}

	virtual void UploadReport(LogMessageType /*type*/, const String& text)
	{
		printf("*** %s\n", text());
		fflush(stdout);
	}

	virtual void PingReplyReceived(const UserRecord& user,
		uint64 roundTripMicroseconds, const String& peerVersion)
	{
		printf("*** Ping reply from %s: %llu ms%s%s\n", user.GetDisplayName()(),
			(unsigned long long) (roundTripMicroseconds / 1000),
			peerVersion.HasChars() ? " -- " : "",
			peerVersion.HasChars() ? peerVersion() : "");
		fflush(stdout);
	}

private:
	const UserRegistry* fUsers;
	ShareScanner* fScanner;
	bool fShouldStartScan;
	uint32 fLastUploadCount;
	Queue<FileResult> fResults;
	uint32 fResultCount;
	int fLastProgressTenths;
	bool fShouldQuit;
};


void
PrintHelp()
{
	printf("Commands:\n");

	const Queue<ChatCommandHelpEntry> helpEntries = ChatCommandParser::GetHelpEntries();
	for (uint32 i = 0; i < helpEntries.GetNumItems(); i++) {
		const ChatCommandHelpEntry& entry = helpEntries[i];
		char commandBuffer[64];
		snprintf(commandBuffer, sizeof(commandBuffer), "/%s %s", entry.commandName,
			entry.arguments != NULL ? entry.arguments : "");
		printf("  %-24s %s\n", commandBuffer, entry.description);
	}

	printf("Anything else is sent as chat.\n");
	fflush(stdout);
}


/** Acts on one line the user typed.
  * @param line the raw input line
  * @param connection the connection to act on
  * @param listener the listener, so /quit can stop the loop
  */
void
HandleUserInput(const String& line, ServerConnection& connection,
	ProbeListener& listener, ICallbackMechanism& callbackMechanism,
	const String& downloadDirectory, bool retainFilePaths,
	Queue<FileDownload*>& downloads)
{
	const ChatCommand command = ChatCommandParser::Parse(line);

	switch (command.type) {
		case CHAT_COMMAND_NONE:
		case CHAT_COMMAND_ACTION:
			if (connection.IsConnected() == false) {
				printf("*** Not connected.\n");
				break;
			}

			connection.SendChatText("*", command.argument);

			// The server relays our own line back to every other session but we drop
			// our own echo, so print it here or the sender never sees what they said.
			if (command.type == CHAT_COMMAND_ACTION) {
				printf("* %s %s\n", connection.GetLocalUserName()(),
					command.argument.Substring(4)());
			} else {
				printf("<%s> %s\n", connection.GetLocalUserName()(),
					command.argument());
			}
			break;

		case CHAT_COMMAND_NICK:
			if (command.argument.IsEmpty()) {
				printf("*** Usage: /nick <name>\n");
				break;
			}

			connection.SetLocalUserName(command.argument);
			printf("*** You are now known as %s.\n", command.argument());
			break;

		case CHAT_COMMAND_STATUS:
			connection.SetLocalUserStatus(command.argument.HasChars()
				? command.argument : String("here"));
			printf("*** Status set to %s.\n", connection.GetLocalUserStatus()());
			break;

		case CHAT_COMMAND_AWAY:
			connection.SetLocalUserStatus(command.argument.HasChars()
				? command.argument : String("away"));
			printf("*** Status set to %s.\n", connection.GetLocalUserStatus()());
			break;

		case CHAT_COMMAND_MESSAGE:
		{
			if (command.target.IsEmpty() || command.argument.IsEmpty()) {
				printf("*** Usage: /msg <user> <text>\n");
				break;
			}

			const Queue<String> targets
				= connection.GetUsers().ResolveToSessionIds(command.target);
			if (targets.IsEmpty()) {
				printf("*** No such user: %s\n", command.target());
				break;
			}

			for (uint32 i = 0; i < targets.GetNumItems(); i++) {
				connection.SendChatText(targets[i], command.argument);
				printf("[PRIVATE] -> %s: %s\n",
					connection.GetUsers().GetDisplayNameForSession(targets[i])(),
					command.argument());
			}
			break;
		}

		case CHAT_COMMAND_PING:
		{
			if (command.target.IsEmpty()) {
				printf("*** Usage: /ping <user>\n");
				break;
			}

			const Queue<String> targets
				= connection.GetUsers().ResolveToSessionIds(command.target);
			if (targets.IsEmpty()) {
				printf("*** No such user: %s\n", command.target());
				break;
			}

			for (uint32 i = 0; i < targets.GetNumItems(); i++)
				connection.SendPing(targets[i]);
			break;
		}

		case CHAT_COMMAND_START_QUERY:
			if (connection.IsConnected() == false) {
				printf("*** Not connected.\n");
				break;
			}

			connection.StartQuery(String("*"), command.argument.HasChars()
				? command.argument : String("*"));
			break;

		case CHAT_COMMAND_STOP_QUERY:
			connection.StopQuery();
			printf("*** Search stopped.\n");
			break;

		case CHAT_COMMAND_GET:
		{
			const Queue<FileResult>& results = listener.GetResults();
			const uint32 index = (uint32) atoi(command.argument());
			if (command.argument.IsEmpty() || index >= results.GetNumItems()) {
				printf("*** Usage: /get <number from the search results>\n");
				break;
			}

			const FileResult& chosen = results[index];
			const UserRecord* sharer
				= connection.GetUsers().FindUser(chosen.sessionId);
			if (sharer == NULL) {
				printf("*** That user is no longer here.\n");
				break;
			}

			if (chosen.isFirewalled || sharer->port <= 0) {
				printf("*** %s is firewalled; connect-back is not implemented"
					" yet.\n", sharer->GetDisplayName()());
				break;
			}

			// One download object per session; it deletes itself once the
			// caller is done with it, which for the probe is at exit.
			FileDownload* download = new FileDownload(&callbackMechanism,
				downloadDirectory);
			download->SetListener(&listener);
			download->SetRetainFilePaths(retainFilePaths);
			download->AddRequestedFile(chosen.fileName, chosen.path,
				chosen.fileSize);

			printf("*** Asking %s at %s:%d for %s\n",
				sharer->GetDisplayName()(), sharer->hostName(),
				(int) sharer->port, chosen.fileName());

			if (download->Start(sharer->hostName, (uint16) sharer->port,
					chosen.sessionId, connection.GetLocalSessionId(),
					connection.GetLocalUserName()).IsError()) {
				printf("*** Could not start the download.\n");
			}

			(void) downloads.AddTail(download);
			break;
		}

		case CHAT_COMMAND_DISCONNECT:
			connection.DisconnectFromServer();
			break;

		case CHAT_COMMAND_CONNECT:
			if (command.argument.HasChars()) {
				(void) connection.ConnectToServer(command.argument,
					kDefaultServerPort);
			} else {
				(void) connection.ConnectToServer(connection.GetServerAddress(),
					connection.GetServerPort());
			}
			break;

		case CHAT_COMMAND_INFO:
			printf("*** %s v%s, %s:%u, session %s, %u users known.\n",
				HITUX_SHARE_NAME, HITUX_SHARE_VERSION_STRING,
				connection.GetServerAddress()(), connection.GetServerPort(),
				connection.GetLocalSessionId().HasChars()
					? connection.GetLocalSessionId()() : "(none)",
				(unsigned) connection.GetUsers().GetUserCount());
			break;

		case CHAT_COMMAND_CLEAR:
			printf("\033[2J\033[H");
			break;

		case CHAT_COMMAND_HELP:
			PrintHelp();
			break;

		case CHAT_COMMAND_QUIT:
			listener.RequestQuit();
			break;

		case CHAT_COMMAND_UNKNOWN:
			printf("*** Unknown command \"/%s\". Try /help.\n", command.commandName());
			break;
	}

	fflush(stdout);
}


}  // unnamed namespace


int
main(int argc, char** argv)
{
	CompleteSetupSystem setupSystem;

	ApplicationSettings settings;
	(void) settings.Load();

	String serverAddress = settings.GetServerAddress();
	String userName = settings.GetUserName();
	uint16 serverPort = settings.GetServerPort();

	if (argc > 1)
		serverAddress = argv[1];
	if (argc > 2)
		userName = argv[2];
	if (argc > 3)
		serverPort = (uint16) atoi(argv[3]);

	printf("%s probe v%s -- connecting to %s:%u as \"%s\"\n", HITUX_SHARE_NAME,
		HITUX_SHARE_VERSION_STRING, serverAddress(), serverPort, userName());
	printf("Type /help for commands, /quit to exit.\n");
	fflush(stdout);

	const String downloadDirectory = settings.GetDownloadDirectory();
	const bool retainFilePaths = settings.GetRetainFilePaths();
	printf("Downloads go to %s\n", downloadDirectory());

	const String shareDirectory = settings.GetFileSharingEnabled()
		? settings.GetShareDirectory() : String();
	if (shareDirectory.HasChars())
		printf("Sharing from %s\n", shareDirectory());

	SocketCallbackMechanism callbackMechanism;
	Queue<FileDownload*> downloads;
	ShareScanner scanner;
	FileUploadServer uploadServer(&callbackMechanism);
	Hashtable<String, SharedFile> sharedFiles;
	uint32 sharedSoFar = 0;

	ProbeListener listener;
	ServerConnection connection(&callbackMechanism);
	connection.SetListener(&listener);
	connection.SetInstallId(settings.GetInstallId());
	listener.SetUserRegistry(&connection.GetUsers());
	uploadServer.SetListener(&listener);
	connection.SetLocalUserName(userName);
	connection.SetLocalUserStatus(settings.GetUserStatus());

	if (connection.ConnectToServer(serverAddress, serverPort).IsError()) {
		printf("*** Could not start the connection.\n");
		return 10;
	}

	// Reading stdin through MUSCLE's gateway rather than fgets() keeps partial lines
	// and EOF handling consistent with the rest of the event loop.
	StdinDataIO stdinDataIO(false);
	PlainTextMessageIOGateway stdinGateway;
	stdinGateway.SetDataIO(DummyDataIORef(stdinDataIO));

	QueueGatewayMessageReceiver stdinReceiver;
	SocketMultiplexer multiplexer;

	const int stdinFileDescriptor
		= stdinDataIO.GetReadSelectSocket().GetFileDescriptor();
	const int notifierFileDescriptor
		= callbackMechanism.GetDispatchThreadNotifierSocket().GetFileDescriptor();

	while (listener.ShouldQuit() == false) {
		(void) multiplexer.RegisterSocketForReadReady(stdinFileDescriptor);
		(void) multiplexer.RegisterSocketForReadReady(notifierFileDescriptor);

		// Wake up regularly regardless of traffic so the keepalive can go out.
		if (multiplexer.WaitForEvents(GetRunTime64() + SecondsToMicros(5)).IsError())
			break;

		if (multiplexer.IsSocketReadyForRead(notifierFileDescriptor))
			callbackMechanism.DispatchCallbacks();

		if (multiplexer.IsSocketReadyForRead(stdinFileDescriptor)) {
			if (stdinGateway.DoInput(stdinReceiver).IsError())
				break;  // stdin closed

			MessageRef inputMessage;
			while (stdinReceiver.RemoveHead(inputMessage).IsOK()) {
				const String* line = NULL;
				for (int32 i = 0;
						inputMessage()->FindString(PR_NAME_TEXT_LINE, i, &line).IsOK();
						i++) {
					HandleUserInput(*line, connection, listener, callbackMechanism,
						downloadDirectory, retainFilePaths, downloads);
				}
			}
		}

		connection.PerformIdleTasks();

		if (listener.ShouldStartScan() && shareDirectory.HasChars()) {
			listener.ClearStartScan();

			// Listen before publishing: a peer that reads our file list and
			// connects back before we are accepting would just get refused.
			const uint16 listenPort = uploadServer.StartListening(
				kDefaultTransferPort, kTransferPortRange);
			if (listenPort > 0) {
				printf("*** Accepting downloads on port %u\n",
					(unsigned) listenPort);
				uploadServer.SetLocalIdentity(connection.GetLocalSessionId(),
					connection.GetLocalUserName());
				connection.SetAdvertisedPort(listenPort);
			} else {
				printf("*** Could not listen on any port; sharing read-only.\n");
			}

			printf("*** Sharing %s\n", shareDirectory());
			scanner.SetShareDirectory(shareDirectory);
			scanner.StartScan();
		}

		// Publishing happens here rather than on the scanning thread: the
		// connection is not thread-safe, and the scan is deliberately allowed
		// to run ahead of the network.
		const Queue<SharedFile> discovered = scanner.TakeDiscoveredFiles();
		if (discovered.HasItems()) {
			connection.PublishSharedFiles(discovered);
			for (uint32 i = 0; i < discovered.GetNumItems(); i++)
				(void) sharedFiles.Put(discovered[i].fileName, discovered[i]);

			uploadServer.SetSharedFiles(sharedFiles);
			sharedSoFar += discovered.GetNumItems();
		}

		if (scanner.TakeScanFinished()) {
			connection.PublishSharedFileCount(sharedSoFar);
			printf("*** Sharing %u file(s)", (unsigned) sharedSoFar);
			const uint32 duplicates = scanner.GetDuplicateNameCount();
			if (duplicates > 0) {
				printf("; %u skipped for having a name another file already"
					" used", (unsigned) duplicates);
			}

			printf(".\n");
			fflush(stdout);
		}
	}

	connection.DisconnectFromServer();

	for (uint32 i = 0; i < downloads.GetNumItems(); i++)
		delete downloads[i];

	printf("*** Goodbye.\n");
	return 0;
}

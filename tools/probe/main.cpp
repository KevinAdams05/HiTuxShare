/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "core/ApplicationSettings.h"
#include "core/ChatCommandParser.h"
#include "core/FormatUtilities.h"
#include "core/HiTuxShareVersion.h"
#include "core/ServerConnection.h"

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
class ProbeListener : public ServerConnectionListener
{
public:
	ProbeListener()
		:
		fUsers(NULL),
		fResultCount(0),
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

	virtual void LocalSessionIdAssigned(const String& sessionId,
		const String& /*hostName*/)
	{
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
		printf("  %-40s %10s  %s%s\n", result.fileName(),
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
	}

	virtual void QuerySweepStateChanged(bool isSweeping)
	{
		printf(isSweeping
			? "*** Searching...\n"
			: "*** Search complete (%u result(s) so far; more arrive live).\n",
			(unsigned) fResultCount);
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
	uint32 fResultCount;
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
	ProbeListener& listener)
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

	SocketCallbackMechanism callbackMechanism;

	ProbeListener listener;
	ServerConnection connection(&callbackMechanism);
	connection.SetListener(&listener);
	connection.SetInstallId(settings.GetInstallId());
	listener.SetUserRegistry(&connection.GetUsers());
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
					HandleUserInput(*line, connection, listener);
				}
			}
		}

		connection.PerformIdleTasks();
	}

	connection.DisconnectFromServer();
	printf("*** Goodbye.\n");
	return 0;
}

/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "core/ServerConnection.h"

#include "core/HiTuxShareVersion.h"

#include "reflector/StorageReflectConstants.h"
#include "regex/PathMatcher.h"
#include "util/TimeUtilityFunctions.h"

using namespace muscle;


namespace hitux {


ServerConnection::ServerConnection(ICallbackMechanism* callbackMechanism)
	:
	CallbackMessageTransceiverThread(callbackMechanism),
	fListener(NULL),
	fConnectionState(CONNECTION_DISCONNECTED),
	fLocalUserName("binky"),
	fLocalUserStatus("here"),
	fServerPort(kDefaultServerPort),
	fInstallId(0),
	fLastTrafficTime(0),
	fLoginTime(0),
	fHasPublishedIdentity(false)
{
}


ServerConnection::~ServerConnection()
{
	// The internal thread holds a pointer to our callback mechanism and will call
	// back into us, so it has to be stopped before any of our members go away.
	ShutdownInternalThread();
}


status_t
ServerConnection::ConnectToServer(const String& hostName, uint16 port)
{
	DisconnectFromServer();

	if (hostName.IsEmpty())
		return B_BAD_ARGUMENT;

	fServerAddress = hostName;
	fServerPort = (port > 0) ? port : kDefaultServerPort;

	const status_t startResult = StartInternalThread();
	if (startResult.IsError()) {
		_ReportToUser(LOG_ERROR_MESSAGE,
			String("Could not start the network thread: ") + startResult());
		return startResult;
	}

	const status_t connectResult = AddNewConnectSession(fServerAddress, fServerPort);
	if (connectResult.IsError()) {
		ShutdownInternalThread();
		_ReportToUser(LOG_ERROR_MESSAGE,
			String("Could not connect to ") + fServerAddress + ": " + connectResult());
		return connectResult;
	}

	_SetConnectionState(CONNECTION_CONNECTING);
	_ReportToUser(LOG_INFORMATION_MESSAGE,
		String("Connecting to ") + fServerAddress + "...");
	return B_NO_ERROR;
}


void
ServerConnection::DisconnectFromServer()
{
	const bool wasActive = (fConnectionState != CONNECTION_DISCONNECTED);

	ShutdownInternalThread();
	Reset();

	fUsers.Clear();
	fLocalSessionId.Clear();
	fHasPublishedIdentity = false;
	fLoginTime = 0;

	if (wasActive) {
		_ReportToUser(LOG_INFORMATION_MESSAGE, "Disconnected from server.");
		_SetConnectionState(CONNECTION_DISCONNECTED);
	}
}


void
ServerConnection::SetLocalUserName(const String& name)
{
	// Never publish an empty name: it turns us into a nameless row in every other
	// client's user list, and there is no way for anyone to address us.
	if (name.IsEmpty())
		return;

	if (name == fLocalUserName)
		return;

	fLocalUserName = name;
	if (IsConnected())
		_PublishLocalUserName();
}


void
ServerConnection::SetLocalUserStatus(const String& status)
{
	if (status == fLocalUserStatus)
		return;

	fLocalUserStatus = status;
	if (IsConnected())
		_PublishLocalUserStatus();
}


void
ServerConnection::SendChatText(const String& targetSessionId, const String& text)
{
	if (IsConnected() == false || text.IsEmpty())
		return;

	MessageRef chatMessage = GetMessageFromPool(NET_CLIENT_NEW_CHAT_TEXT);
	if (chatMessage() == NULL)
		return;

	(void) chatMessage()->AddString(PR_NAME_KEYS,
		MakeSessionTargetPath(targetSessionId));
	(void) chatMessage()->AddString(BESHARE_FIELD_SESSION, fLocalSessionId);
	(void) chatMessage()->AddString(BESHARE_FIELD_TEXT, text);

	// The presence of the field is what marks a message private -- its value is never
	// read.  Only set it for a directed message, or every client shows "<PRIVATE>".
	if (targetSessionId != "*")
		(void) chatMessage()->AddBool(BESHARE_FIELD_PRIVATE, true);

	_SendToServer(chatMessage);
}


void
ServerConnection::SendPing(const String& targetSessionId)
{
	if (IsConnected() == false)
		return;

	MessageRef pingMessage = GetMessageFromPool(NET_CLIENT_PING);
	if (pingMessage() == NULL)
		return;

	(void) pingMessage()->AddString(PR_NAME_KEYS,
		MakeSessionTargetPath(targetSessionId));
	(void) pingMessage()->AddString(BESHARE_FIELD_SESSION, fLocalSessionId);
	(void) pingMessage()->AddInt64(BESHARE_FIELD_WHEN, (int64) GetRunTime64());

	_SendToServer(pingMessage);
}


void
ServerConnection::PerformIdleTasks()
{
	if (IsConnected() == false)
		return;

	const uint64 now = GetRunTime64();
	if (now > fLastTrafficTime + kServerKeepAliveMicroseconds)
		_SendToServer(GetMessageFromPool(PR_COMMAND_NOOP));
}


// #pragma mark - MessageTransceiverThread callbacks


void
ServerConnection::SessionConnected(const String& /*sessionId*/,
	const IPAddressAndPort& connectedTo)
{
	fLoginTime = GetRunTime64();
	_SetConnectionState(CONNECTION_CONNECTED);

	_ReportToUser(LOG_INFORMATION_MESSAGE,
		String("Connected to ") + connectedTo.ToString() + ".");

	// Order matters only in that the name node should exist before other clients see
	// us in their subscription results, so publish before subscribing.
	_PublishLocalUserName();
	_PublishLocalUserStatus();
	_SubscribeToUsers();
	_RequestSessionParameters();
}


void
ServerConnection::SessionDisconnected(const String& /*sessionId*/)
{
	fUsers.Clear();
	fLocalSessionId.Clear();
	fHasPublishedIdentity = false;

	_ReportToUser(LOG_ERROR_MESSAGE, "Connection to server lost.");
	_SetConnectionState(CONNECTION_DISCONNECTED);
}


void
ServerConnection::SessionDetached(const String& /*sessionId*/)
{
	if (fConnectionState != CONNECTION_DISCONNECTED) {
		fUsers.Clear();
		fLocalSessionId.Clear();
		fHasPublishedIdentity = false;
		_SetConnectionState(CONNECTION_DISCONNECTED);
	}
}


void
ServerConnection::MessageReceived(const MessageRef& messageRef,
	const String& /*sessionId*/)
{
	const Message* message = messageRef();
	if (message == NULL)
		return;

	switch (message->what) {
		case PR_RESULT_DATAITEMS:
			_HandleDataItems(*message);
			break;

		case PR_RESULT_PARAMETERS:
			_HandleParameters(*message);
			break;

		case NET_CLIENT_NEW_CHAT_TEXT:
			_HandleChatText(*message);
			break;

		case NET_CLIENT_PING:
			_HandlePing(messageRef);
			break;

		case NET_CLIENT_PONG:
			_HandlePong(*message);
			break;

		default:
			// Everything else on this connection belongs to a part of the protocol we
			// have not implemented yet (file transfers, server status replies).
			// Ignoring it is correct -- BeShare clients are expected to skip what they
			// do not understand rather than complain.
			break;
	}
}


// #pragma mark - Incoming message handlers


void
ServerConnection::_HandleDataItems(const Message& message)
{
	// Nodes that went away.  A removal at session-ID depth means the whole user is
	// gone; deeper removals are individual pieces of their published state.
	String nodePath;
	for (int32 i = 0; message.FindString(PR_NAME_REMOVED_DATAITEMS, i, nodePath).IsOK();
			i++)
		_HandleNodeRemoved(nodePath);

	// Nodes that were added or changed.  Each one arrives as a sub-Message whose field
	// name is the full node path.
	for (MessageFieldNameIterator fieldIterator
			= message.GetFieldNameIterator(B_MESSAGE_TYPE);
			fieldIterator.HasData(); fieldIterator++) {
		const String& fieldName = fieldIterator.GetFieldName();

		MessageRef nodeData;
		if (message.FindMessage(fieldName, nodeData).IsOK())
			_HandleNodeUpdated(fieldName, nodeData);
	}
}


void
ServerConnection::_HandleNodeRemoved(const String& nodePath)
{
	if (GetPathDepth(nodePath()) != SESSION_ID_DEPTH)
		return;

	const String sessionId = _ExtractSessionId(nodePath);
	if (sessionId.IsEmpty())
		return;

	UserRecord departedUser;
	if (fUsers.RemoveUser(sessionId, departedUser) == false)
		return;

	if (fListener != NULL)
		fListener->UserLeft(departedUser);
}


void
ServerConnection::_HandleNodeUpdated(const String& nodePath, const MessageRef& nodeData)
{
	if (GetPathDepth(nodePath()) < USER_NAME_DEPTH)
		return;

	const String sessionId = _ExtractSessionId(nodePath);
	if (sessionId.IsEmpty())
		return;

	// Our own published nodes come back to us through the subscription.  Skipping them
	// keeps us out of our own user list.
	if (sessionId == fLocalSessionId)
		return;

	const Message* nodeMessage = nodeData();
	if (nodeMessage == NULL)
		return;

	const char* nodeNameClause = GetPathClause(USER_NAME_DEPTH, nodePath());
	if (nodeNameClause == NULL)
		return;

	const String nodeName(nodeNameClause);

	bool isNewUser = false;
	UserRecord* user = fUsers.GetOrCreateUser(sessionId, isNewUser);
	if (user == NULL)
		return;

	bool didChange = false;

	if (nodeName.StartsWith("name")) {
		String publishedName;
		if (nodeMessage->FindString(BESHARE_FIELD_NAME, publishedName).IsOK()) {
			user->userName = publishedName;
			didChange = true;
		}

		// The host name is a clause of the node path rather than a field, because it
		// is the server's view of where the user connected from.
		const char* hostClause = GetPathClause(HOST_NAME_DEPTH, nodePath());
		if (hostClause != NULL) {
			String hostName(hostClause);
			const int32 slashIndex = hostName.IndexOf('/');
			if (slashIndex >= 0)
				hostName = hostName.Substring(0, slashIndex);
			user->hostName = hostName;
		}

		(void) nodeMessage->FindInt32(BESHARE_FIELD_PORT, user->port);
		(void) nodeMessage->FindInt64(BESHARE_FIELD_INSTALL_ID, (int64&) user->installId);
		(void) nodeMessage->FindBool(BESHARE_FIELD_BOT, user->isBot);
		(void) nodeMessage->FindBool(BESHARE_FIELD_SUPPORTS_PARTIAL_HASHING,
			user->supportsPartialHashing);
		(void) nodeMessage->FindBool(BESHARE_FIELD_SUPPORTS_SSL, user->supportsSsl);
		(void) nodeMessage->FindBool(BESHARE_FIELD_SUPPORTS_RANGES, user->supportsRanges);

		// Two generations of version advertising: the modern pair of fields, and the
		// bare "version" string older BeShare builds send.  A leading digit in the
		// legacy field means classic BeShare, which never named itself.
		String versionName;
		String versionNumber;
		if (nodeMessage->FindString(BESHARE_FIELD_VERSION_NAME, versionName).IsOK()
				&& nodeMessage->FindString(BESHARE_FIELD_VERSION_NUMBER,
					versionNumber).IsOK()) {
			user->clientVersion = versionName + " v" + versionNumber;
		} else {
			String legacyVersion;
			if (nodeMessage->FindString(BESHARE_FIELD_LEGACY_VERSION,
					legacyVersion).IsOK()) {
				user->clientVersion = (legacyVersion.HasChars()
					&& legacyVersion[0] >= '0' && legacyVersion[0] <= '9')
						? String("BeShare v") + legacyVersion : legacyVersion;
			}
		}

		didChange = true;
	} else if (nodeName.StartsWith("userstatus")) {
		String status;
		if (nodeMessage->FindString(BESHARE_FIELD_USER_STATUS, status).IsOK()) {
			user->userStatus = status;
			didChange = true;
		}
	} else if (nodeName.StartsWith("uploadstats")) {
		int32 current = 0;
		int32 maximum = 0;
		if (nodeMessage->FindInt32(BESHARE_FIELD_UPLOADS_CURRENT, current).IsOK()
				&& nodeMessage->FindInt32(BESHARE_FIELD_UPLOADS_MAX, maximum).IsOK()) {
			user->uploadsCurrent = (uint32) current;
			user->uploadsMax = (uint32) maximum;
			didChange = true;
		}
	} else if (nodeName.StartsWith("bandwidth")) {
		String label;
		int32 bitsPerSecond = 0;
		if (nodeMessage->FindString(BESHARE_FIELD_BANDWIDTH_LABEL, label).IsOK()
				&& nodeMessage->FindInt32(BESHARE_FIELD_BANDWIDTH_BITS,
					bitsPerSecond).IsOK()) {
			user->bandwidthLabel = label;
			user->bandwidthBitsPerSecond = (uint32) bitsPerSecond;
			didChange = true;
		}
	} else if (nodeName.StartsWith("filecount")) {
		int32 fileCount = 0;
		if (nodeMessage->FindInt32(BESHARE_FIELD_FILE_COUNT, fileCount).IsOK()) {
			user->fileCount = (uint32) fileCount;
			didChange = true;
		}
	} else if (nodeName.StartsWith("fires")) {
		// Files published under "fires" instead of "files": this peer cannot accept
		// incoming connections.
		user->isFirewalled = true;
		didChange = true;
	} else if (nodeName.StartsWith("files")) {
		user->isFirewalled = false;
		didChange = true;
	}

	if (didChange || isNewUser)
		_NotifyUserUpdated(sessionId, isNewUser);
	else if (isNewUser == false && user->userName.IsEmpty())
		return;
}


void
ServerConnection::_HandleChatText(const Message& message)
{
	String senderSessionId;
	String text;
	if (message.FindString(BESHARE_FIELD_SESSION, senderSessionId).IsError()
			|| message.FindString(BESHARE_FIELD_TEXT, text).IsError())
		return;

	// We echo our own lines locally the moment we send them, so that the chat log
	// never appears to swallow what the user typed.  The server also relays them back
	// to us because our own session matches the "everyone" pattern; drop that copy.
	if (senderSessionId == fLocalSessionId)
		return;

	ChatMessage chatMessage;
	chatMessage.type = LOG_REMOTE_USER_CHAT_MESSAGE;
	chatMessage.senderSessionId = senderSessionId;
	chatMessage.senderName = fUsers.GetDisplayNameForSession(senderSessionId);
	chatMessage.isPrivate = message.HasName(BESHARE_FIELD_PRIVATE);
	chatMessage.isFromLocalUser = false;

	if (text.StartsWith(BESHARE_ACTION_PREFIX)) {
		chatMessage.isAction = true;
		chatMessage.text = text.Substring(strlen(BESHARE_ACTION_PREFIX));
	} else {
		chatMessage.text = text;
	}

	if (fListener != NULL)
		fListener->ChatMessageReceived(chatMessage);
}


void
ServerConnection::_HandlePing(const MessageRef& messageRef)
{
	Message* message = messageRef();
	if (message == NULL)
		return;

	String replyToSessionId;
	if (message->FindString(BESHARE_FIELD_SESSION, replyToSessionId).IsError())
		return;

	// Answering pings is expected client behaviour, not a courtesy: other clients use
	// it to measure latency and to discover what software their peers run.  Reuse the
	// incoming Message so the "when" timestamp comes back unaltered.
	message->what = NET_CLIENT_PONG;

	(void) message->RemoveName(PR_NAME_KEYS);
	(void) message->AddString(PR_NAME_KEYS, MakeSessionTargetPath(replyToSessionId));

	(void) message->RemoveName(BESHARE_FIELD_SESSION);
	(void) message->AddString(BESHARE_FIELD_SESSION, fLocalSessionId);

	(void) message->RemoveName(BESHARE_FIELD_LEGACY_VERSION);
	(void) message->AddString(BESHARE_FIELD_LEGACY_VERSION,
		String(HITUX_SHARE_NAME) + " v" + HITUX_SHARE_VERSION_STRING);

	const uint64 now = GetRunTime64();

	(void) message->RemoveName(BESHARE_FIELD_UPTIME);
	(void) message->AddInt64(BESHARE_FIELD_UPTIME, (int64) now);

	(void) message->RemoveName(BESHARE_FIELD_ONLINE_TIME);
	(void) message->AddInt64(BESHARE_FIELD_ONLINE_TIME,
		(int64) ((fLoginTime > 0) ? (now - fLoginTime) : 0));

	_SendToServer(messageRef);
}


void
ServerConnection::_HandlePong(const Message& message)
{
	String senderSessionId;
	int64 sentAt = 0;
	if (message.FindString(BESHARE_FIELD_SESSION, senderSessionId).IsError()
			|| message.FindInt64(BESHARE_FIELD_WHEN, sentAt).IsError())
		return;

	const UserRecord* user = fUsers.FindUser(senderSessionId);
	UserRecord unknownUser;
	if (user == NULL) {
		unknownUser.sessionId = senderSessionId;
		user = &unknownUser;
	}

	String peerVersion;
	(void) message.FindString(BESHARE_FIELD_LEGACY_VERSION, peerVersion);

	const uint64 roundTrip = GetRunTime64() - (uint64) sentAt;

	if (fListener != NULL)
		fListener->PingReplyReceived(*user, roundTrip, peerVersion);
}


void
ServerConnection::_HandleParameters(const Message& message)
{
	String sessionRoot;
	if (message.FindString(PR_NAME_SESSION_ROOT, sessionRoot).IsError())
		return;

	// The session root has the form "/<hostname>/<sessionID>".  Our session ID is
	// everything after the last slash; the host name is what lies between the two.
	const int32 lastSlashIndex = sessionRoot.LastIndexOf('/');
	if (lastSlashIndex < 0)
		return;

	fLocalSessionId = sessionRoot.Substring(lastSlashIndex + 1);
	const String serverHostName = sessionRoot.Substring(1, lastSlashIndex);

	fHasPublishedIdentity = true;

	if (fListener != NULL)
		fListener->LocalSessionIdAssigned(fLocalSessionId, serverHostName);
}


// #pragma mark - Outgoing helpers


void
ServerConnection::_PublishLocalUserName()
{
	MessageRef nameNode = GetMessageFromPool();
	if (nameNode() == NULL)
		return;

	(void) nameNode()->AddString(BESHARE_FIELD_NAME, fLocalUserName);

	// We accept no incoming transfers yet, and port 0 is how that is advertised.
	// Phase 3 replaces this with the real listening port.
	(void) nameNode()->AddInt32(BESHARE_FIELD_PORT, 0);
	(void) nameNode()->AddInt64(BESHARE_FIELD_INSTALL_ID, (int64) fInstallId);
	(void) nameNode()->AddString(BESHARE_FIELD_VERSION_NAME, HITUX_SHARE_NAME);
	(void) nameNode()->AddString(BESHARE_FIELD_VERSION_NUMBER,
		HITUX_SHARE_VERSION_STRING);

	// Only claim capabilities we actually implement.  A peer that believes we honour
	// byte ranges and finds out otherwise gets a corrupted download, not an error.

	_SetDataNodeValue(BESHARE_NODE_NAME, nameNode);
}


void
ServerConnection::_PublishLocalUserStatus()
{
	MessageRef statusNode = GetMessageFromPool();
	if (statusNode() == NULL)
		return;

	(void) statusNode()->AddString(BESHARE_FIELD_USER_STATUS, fLocalUserStatus);
	_SetDataNodeValue(BESHARE_NODE_USER_STATUS, statusNode);
}


void
ServerConnection::_SubscribeToUsers()
{
	MessageRef subscription = GetMessageFromPool(PR_COMMAND_SETPARAMETERS);
	if (subscription() == NULL)
		return;

	(void) subscription()->AddBool(BESHARE_SUBSCRIBE_ALL, true);
	_SendToServer(subscription);
}


void
ServerConnection::_RequestSessionParameters()
{
	_SendToServer(GetMessageFromPool(PR_COMMAND_GETPARAMETERS));
}


void
ServerConnection::_SetDataNodeValue(const char* nodePath, const MessageRef& value)
{
	MessageRef uploadMessage = GetMessageFromPool(PR_COMMAND_SETDATA);
	if (uploadMessage() == NULL)
		return;

	(void) uploadMessage()->AddMessage(nodePath, value);
	_SendToServer(uploadMessage);
}


void
ServerConnection::_SendToServer(const MessageRef& message)
{
	if (message() == NULL)
		return;

	if (SendMessageToSessions(message).IsOK())
		fLastTrafficTime = GetRunTime64();
}


// #pragma mark - Notification helpers


void
ServerConnection::_SetConnectionState(ConnectionState state)
{
	if (state == fConnectionState)
		return;

	fConnectionState = state;
	if (fListener != NULL)
		fListener->ConnectionStateChanged(state);
}


void
ServerConnection::_ReportToUser(LogMessageType type, const String& text)
{
	if (fListener == NULL)
		return;

	fListener->ChatMessageReceived(ChatMessage(type, text));
}


void
ServerConnection::_NotifyUserUpdated(const String& sessionId, bool isNewUser)
{
	const UserRecord* user = fUsers.FindUser(sessionId);
	if (user == NULL || fListener == NULL)
		return;

	fListener->UserUpdated(*user, isNewUser);
}


String
ServerConnection::_ExtractSessionId(const String& nodePath)
{
	const char* sessionClause = GetPathClause(SESSION_ID_DEPTH, nodePath());
	if (sessionClause == NULL)
		return String();

	String sessionId(sessionClause);
	const int32 slashIndex = sessionId.IndexOf('/');
	if (slashIndex >= 0)
		sessionId = sessionId.Substring(0, slashIndex);

	return sessionId;
}


}  // namespace hitux

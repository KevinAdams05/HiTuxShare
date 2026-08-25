/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "core/ConnectionManager.h"

using namespace muscle;


namespace hitux {


namespace {


// A cap so a stray loop or a very enthusiastic server list cannot open dozens
// of connections. HiShare uses a limit for the same reason.
const uint32 kMaximumConnections = 8;


}  // unnamed namespace


ConnectionManager::ConnectionManager(ICallbackMechanism* callbackMechanism)
	:
	fCallbackMechanism(callbackMechanism),
	fListener(NULL)
{
}


ConnectionManager::~ConnectionManager()
{
	RemoveAll();
}


void
ConnectionManager::SetListener(ServerConnectionListener* listener)
{
	fListener = listener;
	for (uint32 i = 0; i < fConnections.GetNumItems(); i++)
		fConnections[i]->SetListener(listener);
}


ServerConnection*
ConnectionManager::AddConnection(const String& serverAddress, uint16 port)
{
	ServerConnection* existing = FindByAddress(serverAddress);
	if (existing != NULL)
		return existing;

	if (fConnections.GetNumItems() >= kMaximumConnections)
		return NULL;

	ServerConnection* connection = new ServerConnection(fCallbackMechanism);
	connection->SetListener(fListener);

	if (fConnections.AddTail(connection).IsError()) {
		delete connection;
		return NULL;
	}

	// Remembered on the connection so a later reconnect knows where to go, even
	// before it has been dialled once.
	(void) connection->ConnectToServer(serverAddress, port);
	return connection;
}


bool
ConnectionManager::RemoveConnection(ServerConnection* connection)
{
	for (uint32 i = 0; i < fConnections.GetNumItems(); i++) {
		if (fConnections[i] != connection)
			continue;

		// Detach the listener first: tearing the connection down produces
		// state changes, and the front-end is about to lose the pointer.
		connection->SetListener(NULL);
		connection->DisconnectFromServer();
		(void) fConnections.RemoveItemAt(i);
		delete connection;
		return true;
	}

	return false;
}


void
ConnectionManager::RemoveAll()
{
	for (uint32 i = 0; i < fConnections.GetNumItems(); i++) {
		fConnections[i]->SetListener(NULL);
		fConnections[i]->DisconnectFromServer();
		delete fConnections[i];
	}

	fConnections.Clear();
}


ServerConnection*
ConnectionManager::GetAt(uint32 index) const
{
	return (index < fConnections.GetNumItems()) ? fConnections[index] : NULL;
}


ServerConnection*
ConnectionManager::GetPrimary() const
{
	for (uint32 i = 0; i < fConnections.GetNumItems(); i++) {
		if (fConnections[i]->IsConnected())
			return fConnections[i];
	}

	return fConnections.HasItems() ? fConnections.Head() : NULL;
}


ServerConnection*
ConnectionManager::FindByAddress(const String& serverAddress) const
{
	for (uint32 i = 0; i < fConnections.GetNumItems(); i++) {
		if (fConnections[i]->GetServerAddress().EqualsIgnoreCase(serverAddress))
			return fConnections[i];
	}

	return NULL;
}


uint32
ConnectionManager::GetConnectedCount() const
{
	uint32 count = 0;
	for (uint32 i = 0; i < fConnections.GetNumItems(); i++) {
		if (fConnections[i]->IsConnected())
			count++;
	}

	return count;
}


uint32
ConnectionManager::GetTotalUserCount() const
{
	uint32 count = 0;
	for (uint32 i = 0; i < fConnections.GetNumItems(); i++)
		count += fConnections[i]->GetUsers().GetUserCount();

	return count;
}


void
ConnectionManager::SetInstallId(uint64 installId)
{
	for (uint32 i = 0; i < fConnections.GetNumItems(); i++)
		fConnections[i]->SetInstallId(installId);
}


void
ConnectionManager::SetLocalUserName(const String& userName)
{
	for (uint32 i = 0; i < fConnections.GetNumItems(); i++)
		fConnections[i]->SetLocalUserName(userName);
}


void
ConnectionManager::SetLocalUserStatus(const String& status)
{
	for (uint32 i = 0; i < fConnections.GetNumItems(); i++)
		fConnections[i]->SetLocalUserStatus(status);
}


void
ConnectionManager::SetFirewalled(bool firewalled)
{
	for (uint32 i = 0; i < fConnections.GetNumItems(); i++)
		fConnections[i]->SetFirewalled(firewalled);
}


void
ConnectionManager::SetAdvertisedPort(int32 port)
{
	// One listening socket serves every server's peers, so the same port is
	// advertised on all of them.
	for (uint32 i = 0; i < fConnections.GetNumItems(); i++)
		fConnections[i]->SetAdvertisedPort(port);
}


void
ConnectionManager::ConnectAll()
{
	for (uint32 i = 0; i < fConnections.GetNumItems(); i++) {
		ServerConnection* connection = fConnections[i];
		if (connection->GetConnectionState() == CONNECTION_DISCONNECTED
				&& connection->GetServerAddress().HasChars()) {
			(void) connection->ConnectToServer(connection->GetServerAddress(),
				connection->GetServerPort());
		}
	}
}


void
ConnectionManager::DisconnectAll()
{
	for (uint32 i = 0; i < fConnections.GetNumItems(); i++)
		fConnections[i]->DisconnectFromServer();
}


void
ConnectionManager::PerformIdleTasks()
{
	for (uint32 i = 0; i < fConnections.GetNumItems(); i++)
		fConnections[i]->PerformIdleTasks();
}


void
ConnectionManager::SendChatToAll(const String& text)
{
	for (uint32 i = 0; i < fConnections.GetNumItems(); i++) {
		if (fConnections[i]->IsConnected())
			fConnections[i]->SendChatText(String("*"), text);
	}
}


void
ConnectionManager::StartQueryOnAll(const String& sessionExpression,
	const String& fileExpression)
{
	for (uint32 i = 0; i < fConnections.GetNumItems(); i++) {
		if (fConnections[i]->IsConnected())
			fConnections[i]->StartQuery(sessionExpression, fileExpression);
	}
}


void
ConnectionManager::StopQueryOnAll()
{
	for (uint32 i = 0; i < fConnections.GetNumItems(); i++)
		fConnections[i]->StopQuery();
}


bool
ConnectionManager::IsAnyQueryActive() const
{
	for (uint32 i = 0; i < fConnections.GetNumItems(); i++) {
		if (fConnections[i]->IsQueryActive())
			return true;
	}

	return false;
}


void
ConnectionManager::PublishSharedFilesOnAll(const Queue<SharedFile>& files)
{
	for (uint32 i = 0; i < fConnections.GetNumItems(); i++)
		fConnections[i]->PublishSharedFiles(files);
}


void
ConnectionManager::UnpublishAllSharedFiles()
{
	for (uint32 i = 0; i < fConnections.GetNumItems(); i++)
		fConnections[i]->UnpublishAllSharedFiles();
}


void
ConnectionManager::PublishSharedFileCountOnAll(uint32 fileCount)
{
	for (uint32 i = 0; i < fConnections.GetNumItems(); i++)
		fConnections[i]->PublishSharedFileCount(fileCount);
}


Queue<ResolvedUser>
ConnectionManager::ResolveToUsers(const String& nameOrSessionId) const
{
	Queue<ResolvedUser> resolved;

	for (uint32 i = 0; i < fConnections.GetNumItems(); i++) {
		ServerConnection* connection = fConnections[i];
		const Queue<String> sessionIds
			= connection->GetUsers().ResolveToSessionIds(nameOrSessionId);

		for (uint32 j = 0; j < sessionIds.GetNumItems(); j++) {
			ResolvedUser user;
			user.connection = connection;
			user.sessionId = sessionIds[j];
			user.displayName
				= connection->GetUsers().GetDisplayNameForSession(sessionIds[j]);
			(void) resolved.AddTail(user);
		}
	}

	return resolved;
}


ResolvedUser
ConnectionManager::FindUserBySessionId(const String& sessionId) const
{
	ResolvedUser resolved;

	// A session ID is only unique within one server, so this returns the first
	// match. Callers that know which connection they mean should ask it
	// directly rather than searching.
	for (uint32 i = 0; i < fConnections.GetNumItems(); i++) {
		const UserRecord* user = fConnections[i]->GetUsers().FindUser(sessionId);
		if (user != NULL) {
			resolved.connection = fConnections[i];
			resolved.sessionId = sessionId;
			resolved.displayName = user->GetDisplayName();
			break;
		}
	}

	return resolved;
}


}  // namespace hitux

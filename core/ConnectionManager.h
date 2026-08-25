/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H


#include "core/ServerConnection.h"


namespace hitux {


/** One user, on one particular server. */
struct ResolvedUser
{
	ResolvedUser() : connection(NULL) {}

	ServerConnection* connection;
	muscle::String sessionId;
	muscle::String displayName;
};


/** Owns the set of servers we are connected to, and fans work out across them.
  *
  * Several servers at once is the point of this class, and the reason it needs
  * to exist at all is that almost nothing on this network is global: a session
  * ID means something only on the server that issued it, user lists do not
  * overlap, and the same person on two servers is two unrelated entries. So
  * anything addressed at a *user* has to name a connection too, which is what
  * ResolvedUser carries.
  *
  * Broadcast helpers are here rather than in the front-end because "publish my
  * name" and "share these files" genuinely mean "on every server" -- a share
  * that appeared on only one of them would be a puzzle to the user.
  */
class ConnectionManager
{
public:
	/** Constructor.
	  * @param callbackMechanism handed to every connection we create
	  */
	explicit ConnectionManager(muscle::ICallbackMechanism* callbackMechanism);

	~ConnectionManager();

	/** Sets the listener every connection reports to. Callbacks name their
	  * connection, so one listener can serve them all.
	  * @param listener the listener, not owned
	  */
	void SetListener(ServerConnectionListener* listener);

	/** Creates a connection for a server, or returns the existing one.
	  *
	  * Connecting twice to the same box would double every user in the list
	  * and every result in a query, so a duplicate address returns what is
	  * already there.
	  *
	  * @param serverAddress the host to connect to
	  * @param port its port
	  * @returns the connection, or NULL if the limit is reached
	  */
	ServerConnection* AddConnection(const muscle::String& serverAddress,
		uint16 port);

	/** Disconnects and destroys one connection.
	  * @param connection the one to remove
	  */
	bool RemoveConnection(ServerConnection* connection);

	void RemoveAll();

	uint32 GetCount() const { return fConnections.GetNumItems(); }
	ServerConnection* GetAt(uint32 index) const;

	/** The connection single-server actions apply to: the first connected one,
	  * or the first one at all if none has connected yet.
	  */
	ServerConnection* GetPrimary() const;

	ServerConnection* FindByAddress(const muscle::String& serverAddress) const;

	uint32 GetConnectedCount() const;
	bool IsAnyConnected() const { return GetConnectedCount() > 0; }

	/** Total users across every connection. The same person on two servers
	  * counts twice, because as far as the protocol is concerned they are two
	  * unrelated people.
	  */
	uint32 GetTotalUserCount() const;

	// Applied to every connection.
	void SetInstallId(uint64 installId);
	void SetLocalUserName(const muscle::String& userName);
	void SetLocalUserStatus(const muscle::String& status);
	void SetFirewalled(bool firewalled);
	void SetAdvertisedPort(int32 port);

	void ConnectAll();
	void DisconnectAll();
	void PerformIdleTasks();

	void SendChatToAll(const muscle::String& text);
	void StartQueryOnAll(const muscle::String& sessionExpression,
		const muscle::String& fileExpression);
	void StopQueryOnAll();
	bool IsAnyQueryActive() const;

	void PublishSharedFilesOnAll(const muscle::Queue<SharedFile>& files);
	void UnpublishAllSharedFiles();
	void PublishSharedFileCountOnAll(uint32 fileCount);

	/** Resolves what the user typed into users, across every connection.
	  *
	  * A name may exist on more than one server, so this can return several
	  * results for one word -- and each carries its connection, because
	  * messaging them means talking to different servers.
	  *
	  * @param nameOrSessionId a session ID, a name, or a name pattern
	  */
	muscle::Queue<ResolvedUser> ResolveToUsers(
		const muscle::String& nameOrSessionId) const;

	/** Finds the user behind a session ID, searching every connection.
	  * @param sessionId the session to look for
	  */
	ResolvedUser FindUserBySessionId(const muscle::String& sessionId) const;

private:
	muscle::ICallbackMechanism* fCallbackMechanism;
	ServerConnectionListener* fListener;

	muscle::Queue<ServerConnection*> fConnections;
};


}  // namespace hitux


#endif  // CONNECTION_MANAGER_H

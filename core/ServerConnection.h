/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef SERVER_CONNECTION_H
#define SERVER_CONNECTION_H


#include "core/BeShareProtocol.h"
#include "core/ServerConnectionListener.h"
#include "core/UserRegistry.h"

#include "system/CallbackMessageTransceiverThread.h"


namespace hitux {


/** One client connection to one MUSCLE server, speaking BeShare's conventions.
  *
  * MUSCLE runs the socket on its own internal thread; this class subclasses
  * CallbackMessageTransceiverThread so that incoming Messages are delivered on the
  * thread that owns the ICallbackMechanism passed to the constructor.  For the Qt
  * front-end that is the GUI thread, which is why the listener callbacks may touch
  * widgets without locking.
  *
  * Everything this class publishes about the local user, and everything it reads
  * about remote users, is fixed by the BeShare protocol -- see docs/PROTOCOL.md.
  * Deviating breaks interoperability with the Haiku clients silently.
  */
class ServerConnection : public muscle::CallbackMessageTransceiverThread
{
public:
	/** Constructor.
	  * @param callbackMechanism the event-loop bridge that decides which thread our
	  *        listener callbacks run on.  Must outlive this object.
	  */
	ServerConnection(muscle::ICallbackMechanism* callbackMechanism);

	virtual ~ServerConnection();

	/** Sets who receives our callbacks.  May be NULL to detach.
	  * @param listener the listener, not owned by us
	  */
	void SetListener(ServerConnectionListener* listener) { fListener = listener; }

	/** Sets the stable per-installation ID we publish.  Must be called before
	  * connecting for it to reach the server.
	  * @param installId the ID, as generated and persisted by ApplicationSettings
	  */
	void SetInstallId(uint64 installId) { fInstallId = installId; }

	/** Closes any existing connection and starts a new one.
	  * @param hostName server host name or IP address
	  * @param port TCP port, normally kDefaultServerPort
	  * @returns B_NO_ERROR if the attempt started -- success is reported later
	  *          through ConnectionStateChanged()
	  */
	muscle::status_t ConnectToServer(const muscle::String& hostName, uint16 port);

	/** Closes the connection, if any, and forgets every user. */
	void DisconnectFromServer();

	ConnectionState GetConnectionState() const { return fConnectionState; }
	bool IsConnected() const { return fConnectionState == CONNECTION_CONNECTED; }

	/** Changes the name we publish.  Safe to call while connected -- the name node is
	  * re-published so everyone sees the change immediately.
	  * @param name the new user name.  Empty names are refused: publishing one makes
	  *        us a nameless row in every other client's user list.
	  */
	void SetLocalUserName(const muscle::String& name);

	/** Changes the status string we publish, e.g. "here" or "away".
	  * @param status the new status
	  */
	void SetLocalUserStatus(const muscle::String& status);

	const muscle::String& GetLocalUserName() const { return fLocalUserName; }
	const muscle::String& GetLocalUserStatus() const { return fLocalUserStatus; }
	const muscle::String& GetLocalSessionId() const { return fLocalSessionId; }
	const muscle::String& GetServerAddress() const { return fServerAddress; }
	uint16 GetServerPort() const { return fServerPort; }

	/** Sends a chat line.
	  * @param targetSessionId a session ID for a private message, or "*" for everyone
	  * @param text what to say.  A leading "/me " makes it an action.
	  */
	void SendChatText(const muscle::String& targetSessionId, const muscle::String& text);

	/** Pings one or all peers; replies arrive via PingReplyReceived().
	  * @param targetSessionId a session ID, or "*" for everyone
	  */
	void SendPing(const muscle::String& targetSessionId);

	const UserRegistry& GetUsers() const { return fUsers; }

	/** Housekeeping the connection cannot do for itself.
	  *
	  * The core owns no timer -- that would drag in a toolkit or a thread -- so the
	  * front-end must call this every few seconds.  It sends a keepalive when the
	  * link has been idle long enough that a NAT or a firewall might drop it.
	  */
	void PerformIdleTasks();

protected:
	virtual void MessageReceived(const muscle::MessageRef& message,
		const muscle::String& sessionId);
	virtual void SessionConnected(const muscle::String& sessionId,
		const muscle::IPAddressAndPort& connectedTo);
	virtual void SessionDisconnected(const muscle::String& sessionId);
	virtual void SessionDetached(const muscle::String& sessionId);

private:
	void _HandleDataItems(const muscle::Message& message);
	void _HandleNodeUpdated(const muscle::String& nodePath,
		const muscle::MessageRef& nodeData);
	void _HandleNodeRemoved(const muscle::String& nodePath);
	void _HandleChatText(const muscle::Message& message);
	void _HandlePing(const muscle::MessageRef& messageRef);
	void _HandlePong(const muscle::Message& message);
	void _HandleParameters(const muscle::Message& message);

	void _PublishLocalUserName();
	void _PublishLocalUserStatus();
	void _SubscribeToUsers();
	void _RequestSessionParameters();

	void _SetDataNodeValue(const char* nodePath, const muscle::MessageRef& value);
	void _SendToServer(const muscle::MessageRef& message);

	void _SetConnectionState(ConnectionState state);
	void _ReportToUser(LogMessageType type, const muscle::String& text);
	void _NotifyUserUpdated(const muscle::String& sessionId, bool isNewUser);

	static muscle::String _ExtractSessionId(const muscle::String& nodePath);

	ServerConnectionListener* fListener;
	UserRegistry fUsers;

	ConnectionState fConnectionState;

	muscle::String fLocalUserName;
	muscle::String fLocalUserStatus;
	muscle::String fLocalSessionId;
	muscle::String fServerAddress;
	uint16 fServerPort;

	uint64 fInstallId;

	// When we last put anything on the wire, for the keepalive in PerformIdleTasks().
	uint64 fLastTrafficTime;

	// When this connection came up, so a peer's ping reply can report how long we
	// have been logged in.
	uint64 fLoginTime;

	// True once we have both a session ID and a published name, i.e. once we are
	// genuinely usable.  Guards against sending chat the server would reject.
	bool fHasPublishedIdentity;
};


}  // namespace hitux


#endif  // SERVER_CONNECTION_H

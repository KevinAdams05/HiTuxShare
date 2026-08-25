/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef FILE_UPLOAD_SERVER_H
#define FILE_UPLOAD_SERVER_H


#include "core/BeShareProtocol.h"
#include "core/ChatMessage.h"
#include "core/ShareScanner.h"

#include "system/CallbackMessageTransceiverThread.h"
#include "util/Hashtable.h"

#include <stdio.h>


namespace hitux {


class FileUploadServer;


/** How a front-end hears about people downloading from us. */
class FileUploadServerListener
{
public:
	virtual ~FileUploadServerListener() {}

	/** A peer connected, asked for something, finished, or went away.
	  * @param server the server whose state changed
	  */
	virtual void UploadsChanged(FileUploadServer* server) = 0;

	/** Something worth telling the user, for the chat log.
	  * @param type how to colour it
	  * @param text what to say
	  */
	virtual void UploadReport(LogMessageType type,
		const muscle::String& text) = 0;
};


/** Serves our shared files to peers that connect to us.
  *
  * A single MessageTransceiverThread with an accept factory hosts every
  * incoming peer, rather than one thread per connection. BeShare used a separate
  * accept thread that handed raw sockets to per-transfer threads, but
  * AcceptSocketsThread was removed from MUSCLE in 8.40 and PutAcceptFactory()
  * does the same job with one thread instead of N.
  *
  * Sending is paced by output-queue drain notifications rather than by writing a
  * whole file into the queue: a peer on a slow link asking for a large file
  * would otherwise be a straight copy of that file into our memory.
  */
class FileUploadServer : public muscle::CallbackMessageTransceiverThread
{
public:
	/** Constructor.
	  * @param callbackMechanism the event-loop bridge; must outlive us
	  */
	explicit FileUploadServer(muscle::ICallbackMechanism* callbackMechanism);

	virtual ~FileUploadServer();

	void SetListener(FileUploadServerListener* listener) { fListener = listener; }

	/** Replaces the set of files we are willing to serve, keyed by leaf name.
	  *
	  * Nothing outside this set is servable: a request names a file, and a name
	  * that is not here is refused. That is what stops a peer asking for
	  * something we never offered.
	  *
	  * @param files the current share, by file name
	  */
	void SetSharedFiles(const muscle::Hashtable<muscle::String, SharedFile>& files);

	/** Starts listening for peers, trying a range of ports.
	  *
	  * @param preferredPort the first port to try
	  * @param portRange how many consecutive ports to try before giving up
	  * @returns the port we ended up on, or 0 if none could be bound
	  */
	uint16 StartListening(uint16 preferredPort, uint16 portRange);

	void StopListening();

	uint16 GetListenPort() const { return fListenPort; }

	uint32 GetActiveUploadCount() const { return fPeers.GetNumItems(); }

	/** Our own identity, sent to peers so they can show who is serving them.
	  * @param sessionId our session ID on the server
	  * @param userName our name
	  */
	void SetLocalIdentity(const muscle::String& sessionId,
		const muscle::String& userName);

protected:
	virtual void MessageReceived(const muscle::MessageRef& message,
		const muscle::String& sessionId);
	virtual void SessionAccepted(const muscle::String& sessionId,
		uint32 factoryID, const muscle::IPAddressAndPort& peerAddress);
	virtual void SessionDetached(const muscle::String& sessionId);
	virtual void OutputQueuesDrained(const muscle::MessageRef& notification);

private:
	/** One peer that is downloading from us. */
	struct PeerUpload
	{
		PeerUpload()
			:
			currentFile(NULL),
			currentFileSize(0),
			bytesSent(0),
			mungeMode(MUNGE_MODE_NONE),
			nextFileIndex(0)
		{
		}

		muscle::String peerAddress;
		muscle::String remoteSessionId;
		muscle::String remoteUserName;

		// Files still to send, resolved against the share at request time.
		muscle::Queue<SharedFile> requestedFiles;

		FILE* currentFile;
		muscle::String currentFileName;
		int64 currentFileSize;
		int64 bytesSent;

		int32 mungeMode;
		uint32 nextFileIndex;
	};

	void _HandleFileList(const muscle::String& sessionId,
		const muscle::Message& message);
	bool _BeginNextFile(const muscle::String& sessionId, PeerUpload& peer);
	void _SendNextChunk(const muscle::String& sessionId, PeerUpload& peer);
	void _CloseCurrentFile(PeerUpload& peer);
	void _SendToPeer(const muscle::String& sessionId,
		const muscle::MessageRef& message);

	static uint32 _CalculateChecksum(const uint8* data, uint32 byteCount);

	FileUploadServerListener* fListener;

	muscle::Hashtable<muscle::String, SharedFile> fSharedFiles;
	muscle::Hashtable<muscle::String, PeerUpload> fPeers;

	muscle::String fLocalSessionId;
	muscle::String fLocalUserName;

	uint16 fListenPort;
};


}  // namespace hitux


#endif  // FILE_UPLOAD_SERVER_H

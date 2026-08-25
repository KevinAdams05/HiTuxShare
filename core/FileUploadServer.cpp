/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "core/FileUploadServer.h"

#include "reflector/RateLimitSessionIOPolicy.h"
#include "reflector/StorageReflectConstants.h"

using namespace muscle;


namespace hitux {


namespace {


// Bytes per FILE_DATA message. Large enough that the per-message overhead does
// not matter, small enough that a paused peer costs us one buffer rather than a
// whole file.
const uint32 kUploadChunkSize = 32 * 1024;


}  // unnamed namespace


FileUploadServer::FileUploadServer(ICallbackMechanism* callbackMechanism)
	:
	CallbackMessageTransceiverThread(callbackMechanism),
	fListener(NULL),
	fListenPort(0),
	fMaxSimultaneousUploads(4),
	fRateLimit(0)
{
}


FileUploadServer::~FileUploadServer()
{
	ShutdownInternalThread();

	for (HashtableIterator<String, PeerUpload> iterator(fPeers);
			iterator.HasData(); iterator++) {
		_CloseCurrentFile(iterator.GetValue());
	}
}


void
FileUploadServer::SetSharedFiles(const Hashtable<String, SharedFile>& files)
{
	fSharedFiles = files;
}


Queue<FileUploadServer::UploadStatus>
FileUploadServer::GetUploadStatuses() const
{
	Queue<UploadStatus> statuses;

	for (auto iterator = fPeers.GetIterator(); iterator.HasData(); iterator++) {
		const PeerUpload& peer = iterator.GetValue();

		UploadStatus status;
		status.peerName = peer.remoteUserName.HasChars()
			? peer.remoteUserName : peer.peerAddress;
		status.fileName = peer.currentFileName;
		status.fileSize = peer.currentFileSize;
		status.bytesSent = peer.bytesSent;
		status.isSending = (peer.currentFile != NULL);
		status.isQueued = peer.isQueued;
		(void) statuses.AddTail(status);
	}

	return statuses;
}


void
FileUploadServer::SetLocalIdentity(const String& sessionId, const String& userName)
{
	fLocalSessionId = sessionId;
	fLocalUserName = userName;
}


uint16
FileUploadServer::StartListening(uint16 preferredPort, uint16 portRange)
{
	StopListening();

	if (StartInternalThread().IsError())
		return 0;

	if (fRateLimit > 0) {
		AbstractSessionIOPolicyRef policy(new RateLimitSessionIOPolicy(fRateLimit));
		(void) SetNewOutputPolicy(policy);
	}

	// Walk a range rather than insisting on one port: a second client on the
	// same machine, or a previous run whose socket is still in TIME_WAIT, would
	// otherwise leave us unable to accept anything at all.
	for (uint16 offset = 0; offset < portRange; offset++) {
		const uint16 candidate = (uint16) (preferredPort + offset);
		uint16 actualPort = 0;
		if (PutAcceptFactory(candidate, ReflectSessionFactoryRef(), invalidIP,
				&actualPort).IsOK()) {
			fListenPort = (actualPort > 0) ? actualPort : candidate;
			return fListenPort;
		}
	}

	ShutdownInternalThread();
	return 0;
}


void
FileUploadServer::StopListening()
{
	if (fListenPort != 0)
		(void) RemoveAcceptFactory(fListenPort);

	ShutdownInternalThread();
	Reset();

	for (HashtableIterator<String, PeerUpload> iterator(fPeers);
			iterator.HasData(); iterator++) {
		_CloseCurrentFile(iterator.GetValue());
	}

	fPeers.Clear();
	fListenPort = 0;
}


// #pragma mark - Transceiver callbacks


void
FileUploadServer::SessionAccepted(const String& sessionId, uint32 /*factoryID*/,
	const IPAddressAndPort& peerAddress)
{
	PeerUpload peer;
	peer.peerAddress = peerAddress.ToString();
	(void) fPeers.Put(sessionId, peer);

	if (fListener != NULL)
		fListener->UploadsChanged(this);
}


void
FileUploadServer::SessionDetached(const String& sessionId)
{
	PeerUpload* peer = fPeers.Get(sessionId);
	if (peer != NULL) {
		_CloseCurrentFile(*peer);
		(void) fPeers.Remove(sessionId);
	}

	// A departure frees a slot whether they finished or gave up.
	_StartNextQueuedPeer();

	if (fListener != NULL)
		fListener->UploadsChanged(this);
}


void
FileUploadServer::MessageReceived(const MessageRef& messageRef,
	const String& sessionId)
{
	const Message* message = messageRef();
	if (message == NULL)
		return;

	PeerUpload* peer = fPeers.Get(sessionId);
	if (peer == NULL)
		return;

	switch (message->what) {
		case TRANSFER_COMMAND_PEER_ID:
			(void) message->FindString(BESHARE_FIELD_FROM_SESSION,
				peer->remoteSessionId);
			(void) message->FindString(BESHARE_FIELD_FROM_USER_NAME,
				peer->remoteUserName);
			if (fListener != NULL)
				fListener->UploadsChanged(this);
			break;

		case TRANSFER_COMMAND_FILE_LIST:
			_HandleFileList(sessionId, *message);
			break;

		default:
			break;
	}
}


void
FileUploadServer::OutputQueuesDrained(const MessageRef& notification)
{
	// The notification carries the session it belongs to, so a slow peer cannot
	// pace a fast one.
	String sessionId;
	if (notification() == NULL
			|| notification()->FindString("hitux:session", sessionId).IsError()) {
		return;
	}

	PeerUpload* peer = fPeers.Get(sessionId);
	if (peer == NULL)
		return;

	if (peer->currentFile != NULL)
		_SendNextChunk(sessionId, *peer);
	else if (_BeginNextFile(sessionId, *peer))
		_SendNextChunk(sessionId, *peer);
}


// #pragma mark - Serving


void
FileUploadServer::_HandleFileList(const String& sessionId, const Message& message)
{
	PeerUpload* peer = fPeers.Get(sessionId);
	if (peer == NULL)
		return;

	(void) message.FindString(BESHARE_FIELD_FROM_SESSION, peer->remoteSessionId);
	(void) message.FindString(BESHARE_FIELD_FROM_USER_NAME, peer->remoteUserName);

	int32 requestedMungeMode = MUNGE_MODE_NONE;
	if (message.FindInt32(BESHARE_FIELD_MUNGE_MODE, requestedMungeMode).IsOK()
			&& requestedMungeMode == MUNGE_MODE_XOR) {
		peer->mungeMode = MUNGE_MODE_XOR;
	}

	peer->requestedFiles.Clear();
	peer->nextFileIndex = 0;

	// Resolve each requested name against what we actually offer. A name we do
	// not share is skipped rather than answered: the request comes from a
	// stranger, and the share table is the only thing that decides what is
	// reachable.
	String requestedName;
	uint32 unknownCount = 0;
	for (int32 i = 0;
			message.FindString(BESHARE_FIELD_FILE_LIST_NAMES, i,
				requestedName).IsOK(); i++) {
		const SharedFile* shared = fSharedFiles.Get(requestedName);
		if (shared != NULL)
			(void) peer->requestedFiles.AddTail(*shared);
		else
			unknownCount++;
	}

	if (fListener != NULL) {
		const String who = peer->remoteUserName.HasChars()
			? peer->remoteUserName : peer->peerAddress;
		if (peer->requestedFiles.IsEmpty()) {
			fListener->UploadReport(LOG_WARNING_MESSAGE,
				who + " asked for " + String("%1").Arg(unknownCount)
					+ " file(s) we do not share.");
		} else {
			fListener->UploadReport(LOG_UPLOAD_EVENT_MESSAGE,
				who + " is downloading "
					+ String("%1").Arg(peer->requestedFiles.GetNumItems())
					+ " file(s) from us.");
		}
	}

	// Hold the peer if we are already serving as many as we allow. Telling them
	// they are queued is what NOTIFY_QUEUED exists for, and is far better than
	// either refusing them or quietly serving everyone at once badly.
	if (_CountActiveUploads() >= fMaxSimultaneousUploads) {
		peer->isQueued = true;

		MessageRef queued = GetMessageFromPool(TRANSFER_COMMAND_NOTIFY_QUEUED);
		if (queued() != NULL)
			_SendToPeer(sessionId, queued);

		if (fListener != NULL)
			fListener->UploadsChanged(this);

		return;
	}

	peer->isQueued = false;
	if (_BeginNextFile(sessionId, *peer))
		_SendNextChunk(sessionId, *peer);
}


uint32
FileUploadServer::_CountActiveUploads() const
{
	uint32 count = 0;
	for (auto iterator = fPeers.GetIterator(); iterator.HasData(); iterator++) {
		const PeerUpload& peer = iterator.GetValue();
		if (peer.isQueued == false && peer.requestedFiles.HasItems())
			count++;
	}

	return count;
}


void
FileUploadServer::_StartNextQueuedPeer()
{
	if (_CountActiveUploads() >= fMaxSimultaneousUploads)
		return;

	for (HashtableIterator<String, PeerUpload> iterator(fPeers);
			iterator.HasData(); iterator++) {
		PeerUpload& peer = iterator.GetValue();
		if (peer.isQueued == false || peer.requestedFiles.IsEmpty())
			continue;

		peer.isQueued = false;
		const String sessionId = iterator.GetKey();
		if (_BeginNextFile(sessionId, peer))
			_SendNextChunk(sessionId, peer);

		return;
	}
}


bool
FileUploadServer::_BeginNextFile(const String& sessionId, PeerUpload& peer)
{
	_CloseCurrentFile(peer);

	if (peer.nextFileIndex >= peer.requestedFiles.GetNumItems())
		return false;

	const SharedFile& file = peer.requestedFiles[peer.nextFileIndex];
	peer.nextFileIndex++;

	peer.currentFile = fopen(file.absolutePath(), "rb");
	if (peer.currentFile == NULL) {
		// Disappeared between the scan and the request. Move on rather than
		// failing the whole session.
		return _BeginNextFile(sessionId, peer);
	}

	peer.currentFileName = file.fileName;
	peer.currentFileSize = file.fileSize;
	peer.bytesSent = 0;

	MessageRef header = GetMessageFromPool(TRANSFER_COMMAND_FILE_HEADER);
	if (header() == NULL)
		return false;

	(void) header()->AddString(BESHARE_FIELD_FILE_NAME, file.fileName);
	(void) header()->AddInt64(BESHARE_FIELD_FILE_SIZE, file.fileSize);
	(void) header()->AddString(BESHARE_FIELD_FROM_SESSION, fLocalSessionId);
	(void) header()->AddString(BESHARE_FIELD_PATH, file.relativePath);
	if (file.kind.HasChars())
		(void) header()->AddString(BESHARE_FIELD_KIND, file.kind);

	_SendToPeer(sessionId, header);

	if (fListener != NULL)
		fListener->UploadsChanged(this);

	return true;
}


void
FileUploadServer::_SendNextChunk(const String& sessionId, PeerUpload& peer)
{
	if (peer.currentFile == NULL)
		return;

	uint8* buffer = new uint8[kUploadChunkSize];
	const size_t readCount = fread(buffer, 1, kUploadChunkSize, peer.currentFile);

	if (readCount == 0) {
		delete[] buffer;

		// Out of data. The downloader decides a file is complete by counting
		// bytes against the header, so there is nothing to send to say so.
		if (_BeginNextFile(sessionId, peer)) {
			_SendNextChunk(sessionId, peer);
		} else {
			// This peer is done, so somebody waiting can start.
			_StartNextQueuedPeer();
			if (fListener != NULL)
				fListener->UploadsChanged(this);
		}

		return;
	}

	if (peer.mungeMode == MUNGE_MODE_XOR) {
		for (size_t i = 0; i < readCount; i++)
			buffer[i] ^= 0xFF;
	}

	MessageRef chunk = GetMessageFromPool(TRANSFER_COMMAND_FILE_DATA);
	if (chunk() == NULL) {
		delete[] buffer;
		return;
	}

	(void) chunk()->AddData(BESHARE_FIELD_DATA, B_RAW_TYPE, buffer,
		(uint32) readCount);

	// The checksum covers the bytes as they go on the wire, so it is computed
	// after munging. The receiver verifies before un-munging.
	(void) chunk()->AddInt32(BESHARE_FIELD_CHECKSUM,
		(int32) _CalculateChecksum(buffer, (uint32) readCount));
	(void) chunk()->AddInt32(BESHARE_FIELD_MUNGE_MODE, peer.mungeMode);

	delete[] buffer;

	_SendToPeer(sessionId, chunk);
	peer.bytesSent += (int64) readCount;

	// Ask to be told when this has actually gone out, and send the next chunk
	// then. Without this the whole file would be queued at once.
	MessageRef notification = GetMessageFromPool();
	if (notification() != NULL) {
		(void) notification()->AddString("hitux:session", sessionId);
		(void) RequestOutputQueuesDrainedNotification(notification, sessionId);
	}

	if (fListener != NULL)
		fListener->UploadsChanged(this);
}


void
FileUploadServer::_CloseCurrentFile(PeerUpload& peer)
{
	if (peer.currentFile != NULL) {
		fclose(peer.currentFile);
		peer.currentFile = NULL;
	}

	peer.currentFileName.Clear();
	peer.currentFileSize = 0;
	peer.bytesSent = 0;
}


void
FileUploadServer::_SendToPeer(const String& sessionId, const MessageRef& message)
{
	// The "session ID" MessageTransceiverThread hands to our callbacks is
	// already the session's full root path -- "/192.168.74.137/5" -- not a bare
	// ID, so it is used as the distribution path unchanged. Wrapping it as
	// "/*/<id>" produces "/*//192.168.74.137/5", which matches nothing, and a
	// message sent to no one is not reported as an error: the transfer simply
	// stalls after the file list with both sides believing they are fine.
	(void) SendMessageToSessions(message, sessionId);
}


uint32
FileUploadServer::_CalculateChecksum(const uint8* data, uint32 byteCount)
{
	uint32 sum = 0;
	for (uint32 i = 0; i < byteCount; i++)
		sum += ((uint32) *(data++)) << (i % 24);

	return sum;
}


}  // namespace hitux

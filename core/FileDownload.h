/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef FILE_DOWNLOAD_H
#define FILE_DOWNLOAD_H


#include "core/BeShareProtocol.h"

#include "system/CallbackMessageTransceiverThread.h"
#include "util/Queue.h"
#include "util/String.h"

#include <stdio.h>


namespace hitux {


class FileDownload;


/** What a download is doing. */
enum DownloadState
{
	DOWNLOAD_IDLE = 0,
	DOWNLOAD_CONNECTING,
	DOWNLOAD_REQUESTING,
	DOWNLOAD_QUEUED_REMOTELY,
	DOWNLOAD_TRANSFERRING,
	DOWNLOAD_FINISHED,
	DOWNLOAD_FAILED
};


/** How a front-end hears about a download's progress. */
class FileDownloadListener
{
public:
	virtual ~FileDownloadListener() {}

	/** The download moved between states.
	  * @param download which download
	  */
	virtual void DownloadStateChanged(FileDownload* download) = 0;

	/** Bytes arrived. Called often -- a front-end should throttle its redraws
	  * rather than repaint per callback.
	  * @param download which download
	  */
	virtual void DownloadProgress(FileDownload* download) = 0;

	/** One file finished and is closed on disk.
	  * @param download which download
	  * @param localPath where it landed
	  */
	virtual void DownloadFileCompleted(FileDownload* download,
		const muscle::String& localPath) = 0;
};


/** One connection to one peer, downloading one or more files from it.
  *
  * BeShare opens a separate TCP session per peer rather than multiplexing, so
  * this owns its own MessageTransceiverThread. Files requested together are
  * served back-to-back over that one connection.
  *
  * Everything arriving here comes from an unauthenticated stranger. File names
  * and paths are sanitised before they are allowed near the filesystem, chunk
  * checksums are verified, and a size that disagrees with the header aborts the
  * session.
  */
class FileDownload : public muscle::CallbackMessageTransceiverThread
{
public:
	/** Constructor.
	  * @param callbackMechanism the event-loop bridge; must outlive us
	  * @param downloadDirectory where completed files are written
	  */
	FileDownload(muscle::ICallbackMechanism* callbackMechanism,
		const muscle::String& downloadDirectory);

	virtual ~FileDownload();

	void SetListener(FileDownloadListener* listener) { fListener = listener; }

	/** Whether to recreate the sharer's sub-path under the download directory.
	  * @param retainFilePaths true to rebuild their directory structure
	  */
	void SetRetainFilePaths(bool retainFilePaths)
	{
		fRetainFilePaths = retainFilePaths;
	}

	/** Queues a file to request. Call before Start().
	  * @param fileName the name as it appears in the query result
	  * @param path the sharer's sub-path for it, may be empty
	  * @param expectedSize the size the query reported, for progress display
	  */
	void AddRequestedFile(const muscle::String& fileName,
		const muscle::String& path, int64 expectedSize);

	/** Connects to the peer and asks for the queued files.
	  * @param hostName the peer's advertised host
	  * @param port the peer's advertised port
	  * @param remoteSessionId the peer's session ID, for display
	  * @param localSessionId our session ID, which the peer expects to be told
	  * @param localUserName our name, likewise
	  */
	muscle::status_t Start(const muscle::String& hostName, uint16 port,
		const muscle::String& remoteSessionId,
		const muscle::String& localSessionId,
		const muscle::String& localUserName);

	/** Stops the transfer and closes any part-written file. */
	void Abort(const muscle::String& reason);

	DownloadState GetState() const { return fState; }
	const muscle::String& GetErrorText() const { return fErrorText; }
	const muscle::String& GetRemoteSessionId() const { return fRemoteSessionId; }
	const muscle::String& GetRemoteUserName() const { return fRemoteUserName; }
	const muscle::String& GetCurrentFileName() const { return fCurrentFileName; }

	int64 GetCurrentFileSize() const { return fCurrentFileSize; }
	int64 GetCurrentFileBytesDone() const { return fCurrentFileBytesDone; }
	int64 GetTotalBytesDone() const { return fTotalBytesDone; }
	int64 GetTotalBytesExpected() const { return fTotalBytesExpected; }

	uint32 GetRequestedFileCount() const { return fRequestedFiles.GetNumItems(); }
	uint32 GetCompletedFileCount() const { return fCompletedFileCount; }

	/** Recent transfer rate in bytes per second, or 0 before enough has arrived. */
	int64 GetBytesPerSecond() const;

	/** True once the session is over, successfully or not. */
	bool IsFinished() const
	{
		return fState == DOWNLOAD_FINISHED || fState == DOWNLOAD_FAILED;
	}

	/** Makes one path component safe to place on the filesystem.
	  *
	  * Peers choose these strings, so this is a security boundary rather than
	  * tidying: separators, "." and ".." and leading dashes are all things a
	  * hostile or broken peer can send, and a name that survives unaltered would
	  * let them write outside the download directory.
	  *
	  * @param component the untrusted string
	  * @returns a safe component, or an empty String if nothing usable remains
	  */
	static muscle::String SanitizePathComponent(const muscle::String& component);

protected:
	virtual void MessageReceived(const muscle::MessageRef& message,
		const muscle::String& sessionId);
	virtual void SessionConnected(const muscle::String& sessionId,
		const muscle::IPAddressAndPort& connectedTo);
	virtual void SessionDisconnected(const muscle::String& sessionId);

private:
	struct RequestedFile
	{
		RequestedFile() : expectedSize(0) {}

		muscle::String fileName;
		muscle::String path;
		int64 expectedSize;
	};

	void _SendPeerIdentity();
	void _SendFileList();
	void _HandleFileHeader(const muscle::Message& message);
	void _HandleFileData(const muscle::Message& message);
	void _HandleRejected(const muscle::Message& message);

	bool _OpenOutputFile(const muscle::String& fileName,
		const muscle::String& relativePath);
	void _CloseOutputFile();
	void _FinishCurrentFile();
	void _SetState(DownloadState state);

	static uint32 _CalculateChecksum(const uint8* data, uint32 byteCount);

	FileDownloadListener* fListener;
	bool fRetainFilePaths;

	muscle::String fDownloadDirectory;
	muscle::String fRemoteSessionId;
	muscle::String fRemoteUserName;
	muscle::String fLocalSessionId;
	muscle::String fLocalUserName;
	muscle::String fErrorText;

	muscle::Queue<RequestedFile> fRequestedFiles;

	DownloadState fState;

	FILE* fOutputFile;
	muscle::String fOutputPath;
	muscle::String fCurrentFileName;
	int64 fCurrentFileSize;
	int64 fCurrentFileBytesDone;

	int64 fTotalBytesDone;
	int64 fTotalBytesExpected;
	uint32 fCompletedFileCount;

	// A short window of (when, byteCount) samples, so the displayed rate reflects
	// the last few seconds rather than the average since the session began.
	struct TransferSample
	{
		TransferSample() : when(0), byteCount(0) {}
		TransferSample(uint64 w, uint32 b) : when(w), byteCount(b) {}

		uint64 when;
		uint32 byteCount;
	};

	muscle::Queue<TransferSample> fTransferSamples;
};


}  // namespace hitux


#endif  // FILE_DOWNLOAD_H

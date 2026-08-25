/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "core/FileDownload.h"

#include "reflector/RateLimitSessionIOPolicy.h"
#include "util/StringTokenizer.h"
#include "util/TimeUtilityFunctions.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

using namespace muscle;


namespace hitux {


namespace {


// How many recent chunks the rate estimate averages over.
const uint32 kMaximumTransferSamples = 32;

// How far back the rate estimate looks.
const uint64 kRateWindowMicroseconds = 5 * 1000000ULL;

// Attempts at "name (2).ext" before giving up on finding an unused name.
const uint32 kMaximumRenameAttempts = 64;


/** Creates a directory and any missing parents, like "mkdir -p".
  * @param directoryPath the directory to create
  */
status_t
CreateDirectoryTree(const String& directoryPath)
{
	if (directoryPath.IsEmpty())
		return B_BAD_ARGUMENT;

	String pathSoFar;
	StringTokenizer tokenizer(directoryPath(), "/");
	const char* component = NULL;
	while ((component = tokenizer.GetNextToken()) != NULL) {
		pathSoFar += "/";
		pathSoFar += component;
		if (mkdir(pathSoFar(), 0755) != 0 && errno != EEXIST)
			return B_ERRNO;
	}

	return B_NO_ERROR;
}


/** True if a path already exists on disk.
  * @param path the path to test
  */
bool
PathExists(const String& path)
{
	struct stat information;
	return stat(path(), &information) == 0;
}


}  // unnamed namespace


FileDownload::FileDownload(ICallbackMechanism* callbackMechanism,
	const String& downloadDirectory)
	:
	CallbackMessageTransceiverThread(callbackMechanism),
	fListener(NULL),
	fRetainFilePaths(false),
	fRateLimit(0),
	fDownloadDirectory(downloadDirectory),
	fState(DOWNLOAD_IDLE),
	fOutputFile(NULL),
	fCurrentFileSize(0),
	fCurrentFileBytesDone(0),
	fTotalBytesDone(0),
	fTotalBytesExpected(0),
	fCompletedFileCount(0)
{
}


FileDownload::~FileDownload()
{
	// The internal thread calls back into us, so it has to stop before any of
	// our members go away.
	ShutdownInternalThread();
	_CloseOutputFile();
}


void
FileDownload::AddRequestedFile(const String& fileName, const String& path,
	int64 expectedSize)
{
	RequestedFile requested;
	requested.fileName = fileName;
	requested.path = path;
	requested.expectedSize = expectedSize;

	if (fRequestedFiles.AddTail(requested).IsOK() && expectedSize > 0)
		fTotalBytesExpected += expectedSize;
}


status_t
FileDownload::Start(const String& hostName, uint16 port,
	const String& remoteSessionId, const String& localSessionId,
	const String& localUserName)
{
	if (fRequestedFiles.IsEmpty())
		return B_BAD_ARGUMENT;

	if (hostName.IsEmpty() || port == 0) {
		// A peer that advertises no port accepts no connections. Downloading
		// from them needs the connect-back path, which is not implemented yet.
		fErrorText = "Peer is not accepting connections";
		_SetState(DOWNLOAD_FAILED);
		return B_BAD_ARGUMENT;
	}

	fRemoteSessionId = remoteSessionId;
	fLocalSessionId = localSessionId;
	fLocalUserName = localUserName;

	const status_t startResult = StartInternalThread();
	if (startResult.IsError()) {
		fErrorText = startResult();
		_SetState(DOWNLOAD_FAILED);
		return startResult;
	}

	// The policy applies to sessions created after it is set, so it has to go on
	// before the connect session rather than after.
	if (fRateLimit > 0) {
		AbstractSessionIOPolicyRef policy(new RateLimitSessionIOPolicy(fRateLimit));
		(void) SetNewInputPolicy(policy);
	}

	const status_t connectResult = AddNewConnectSession(hostName, port);
	if (connectResult.IsError()) {
		ShutdownInternalThread();
		fErrorText = connectResult();
		_SetState(DOWNLOAD_FAILED);
		return connectResult;
	}

	_SetState(DOWNLOAD_CONNECTING);
	return B_NO_ERROR;
}


void
FileDownload::Abort(const String& reason)
{
	if (IsFinished())
		return;

	ShutdownInternalThread();
	_CloseOutputFile();

	// A part-written file is not a download. Removing it avoids leaving
	// something that looks complete but is not.
	if (fOutputPath.HasChars())
		(void) remove(fOutputPath());

	fErrorText = reason;
	_SetState(DOWNLOAD_FAILED);
}


int64
FileDownload::GetBytesPerSecond() const
{
	if (fTransferSamples.GetNumItems() < 2)
		return 0;

	const uint64 now = GetRunTime64();
	const uint64 oldest = fTransferSamples.Head().when;
	const uint64 elapsed = (now > oldest) ? (now - oldest) : 0;
	if (elapsed == 0)
		return 0;

	uint64 total = 0;
	for (uint32 i = 0; i < fTransferSamples.GetNumItems(); i++)
		total += fTransferSamples[i].byteCount;

	return (int64) ((total * 1000000ULL) / elapsed);
}


// #pragma mark - Transceiver callbacks


void
FileDownload::SessionConnected(const String& /*sessionId*/,
	const IPAddressAndPort& /*connectedTo*/)
{
	_SetState(DOWNLOAD_REQUESTING);

	// Identity first: building a file list can take the peer a while, and this
	// lets them show who is asking in the meantime.
	_SendPeerIdentity();
	_SendFileList();
}


void
FileDownload::SessionDisconnected(const String& /*sessionId*/)
{
	if (IsFinished())
		return;

	// A peer that hangs up having sent everything we asked for has simply
	// finished; one that hangs up mid-file has not.
	if (fOutputFile != NULL) {
		Abort("Peer disconnected during transfer");
		return;
	}

	_CloseOutputFile();
	_SetState(fCompletedFileCount > 0 ? DOWNLOAD_FINISHED : DOWNLOAD_FAILED);

	if (fCompletedFileCount == 0 && fErrorText.IsEmpty())
		fErrorText = "Peer disconnected without sending anything";
}


void
FileDownload::MessageReceived(const MessageRef& messageRef,
	const String& /*sessionId*/)
{
	const Message* message = messageRef();
	if (message == NULL)
		return;

	switch (message->what) {
		case TRANSFER_COMMAND_FILE_HEADER:
			_HandleFileHeader(*message);
			break;

		case TRANSFER_COMMAND_FILE_DATA:
			_HandleFileData(*message);
			break;

		case TRANSFER_COMMAND_PEER_ID:
		{
			// Only overwrite the seeded name if they actually sent one.
			String announcedName;
			if (message->FindString(BESHARE_FIELD_FROM_USER_NAME,
					announcedName).IsOK() && announcedName.HasChars()) {
				fRemoteUserName = announcedName;
			}
			break;
		}

		case TRANSFER_COMMAND_NOTIFY_QUEUED:
			// They have us on a waiting list rather than refusing us.
			_SetState(DOWNLOAD_QUEUED_REMOTELY);
			break;

		case TRANSFER_COMMAND_REJECTED:
			_HandleRejected(*message);
			break;

		default:
			break;
	}
}


// #pragma mark - Outgoing


void
FileDownload::_SendPeerIdentity()
{
	MessageRef identity = GetMessageFromPool(TRANSFER_COMMAND_PEER_ID);
	if (identity() == NULL)
		return;

	(void) identity()->AddString(BESHARE_FIELD_FROM_SESSION, fLocalSessionId);
	(void) identity()->AddString(BESHARE_FIELD_FROM_USER_NAME, fLocalUserName);
	(void) SendMessageToSessions(identity);
}


void
FileDownload::_SendFileList()
{
	MessageRef request = GetMessageFromPool(TRANSFER_COMMAND_FILE_LIST);
	if (request() == NULL)
		return;

	(void) request()->AddString(BESHARE_FIELD_FROM_SESSION, fLocalSessionId);
	(void) request()->AddString(BESHARE_FIELD_FROM_USER_NAME, fLocalUserName);

	// State our munge preference. The sender stamps each chunk with what it
	// actually applied, so we handle either regardless of what we ask for.
	(void) request()->AddInt32(BESHARE_FIELD_MUNGE_MODE, MUNGE_MODE_XOR);

	// The four per-file fields are read by index and must stay aligned. An
	// offset of zero means "send from the start"; the md5 that would validate a
	// resume is then a single dummy byte, which is what BeShare sends too.
	const uint8 dummyDigest = 0;
	for (uint32 i = 0; i < fRequestedFiles.GetNumItems(); i++) {
		const RequestedFile& requested = fRequestedFiles[i];
		(void) request()->AddString(BESHARE_FIELD_FILE_LIST_NAMES,
			requested.fileName);
		(void) request()->AddInt64(BESHARE_FIELD_FILE_LIST_OFFSETS, 0);
		(void) request()->AddString(BESHARE_FIELD_PATH, requested.path);
		(void) request()->AddData(BESHARE_FIELD_FILE_LIST_MD5, B_RAW_TYPE,
			&dummyDigest, sizeof(dummyDigest));
	}

	(void) SendMessageToSessions(request);
}


// #pragma mark - Incoming


void
FileDownload::_HandleFileHeader(const Message& message)
{
	// A header arriving while a file is open means the peer moved on early.
	_FinishCurrentFile();

	String fileName;
	int64 fileSize = 0;
	if (message.FindString(BESHARE_FIELD_FILE_NAME, fileName).IsError()
			|| message.FindInt64(BESHARE_FIELD_FILE_SIZE, fileSize).IsError()) {
		Abort("Peer sent a malformed file header");
		return;
	}

	(void) message.FindString(BESHARE_FIELD_FROM_SESSION, fRemoteSessionId);

	String relativePath;
	(void) message.FindString(BESHARE_FIELD_PATH, relativePath);

	fCurrentFileName = fileName;
	fCurrentFileSize = fileSize;
	fCurrentFileBytesDone = 0;

	if (_OpenOutputFile(fileName, relativePath) == false) {
		Abort(String("Could not create a file for ") + fileName);
		return;
	}

	_SetState(DOWNLOAD_TRANSFERRING);
}


void
FileDownload::_HandleFileData(const Message& message)
{
	if (fOutputFile == NULL)
		return;

	const void* data = NULL;
	uint32 byteCount = 0;
	if (message.FindData(BESHARE_FIELD_DATA, B_RAW_TYPE, &data,
			&byteCount).IsError()) {
		// A data message with no data is how the sender marks the end of a file
		// on some paths; the size check below is the authoritative one.
		_FinishCurrentFile();
		return;
	}

	// Checksum first, un-munge second, and that order is the protocol rather
	// than a preference: the sender computes "chk" over the bytes as they go on
	// the wire, i.e. after munging. Verifying the decoded bytes instead makes
	// every XOR-munged chunk look corrupt.
	//
	// TCP should make this check unnecessary at all. BeShare added it because
	// resumes kept producing corrupt files anyway, and a mismatch is worth
	// failing loudly over rather than writing bad bytes to disk.
	int32 expectedChecksum = 0;
	if (message.FindInt32(BESHARE_FIELD_CHECKSUM, expectedChecksum).IsOK()) {
		const uint32 actual
			= _CalculateChecksum(static_cast<const uint8*>(data), byteCount);
		if (actual != (uint32) expectedChecksum) {
			Abort("Checksum mismatch -- the data arrived corrupted");
			return;
		}
	}

	// Copy before un-munging: the Message owns that buffer and may be shared.
	uint8* writable = new uint8[byteCount];
	memcpy(writable, data, byteCount);

	int32 mungeMode = MUNGE_MODE_NONE;
	(void) message.FindInt32(BESHARE_FIELD_MUNGE_MODE, mungeMode);

	if (mungeMode == MUNGE_MODE_XOR) {
		for (uint32 i = 0; i < byteCount; i++)
			writable[i] ^= 0xFF;
	} else if (mungeMode != MUNGE_MODE_NONE) {
		delete[] writable;
		Abort("Peer used a data encoding we do not understand");
		return;
	}

	const size_t written = fwrite(writable, 1, byteCount, fOutputFile);
	delete[] writable;

	if (written != byteCount) {
		Abort("Could not write to disk");
		return;
	}

	fCurrentFileBytesDone += byteCount;
	fTotalBytesDone += byteCount;

	(void) fTransferSamples.AddTail(TransferSample(GetRunTime64(), byteCount));
	while (fTransferSamples.GetNumItems() > kMaximumTransferSamples
			|| (fTransferSamples.GetNumItems() > 1
				&& GetRunTime64() - fTransferSamples.Head().when
					> kRateWindowMicroseconds)) {
		(void) fTransferSamples.RemoveHead();
	}

	if (fListener != NULL)
		fListener->DownloadProgress(this);

	if (fCurrentFileSize >= 0 && fCurrentFileBytesDone >= fCurrentFileSize)
		_FinishCurrentFile();
}


void
FileDownload::_HandleRejected(const Message& message)
{
	int64 secondsLeft = 0;
	String reason("The peer refused the request");
	if (message.FindInt64(BESHARE_FIELD_TIME_LEFT, secondsLeft).IsOK()
			&& secondsLeft > 0) {
		reason += " (banned for ";
		reason += String("%1").Arg(secondsLeft / 60);
		reason += " more minutes)";
	}

	Abort(reason);
}


// #pragma mark - Files


bool
FileDownload::_OpenOutputFile(const String& fileName, const String& relativePath)
{
	const String safeName = SanitizePathComponent(fileName);
	if (safeName.IsEmpty())
		return false;

	String directory = fDownloadDirectory;

	// The sharer's sub-path is rebuilt component by component, each sanitised,
	// so a path like "../../.ssh" cannot escape the download directory.
	if (fRetainFilePaths) {
		StringTokenizer tokenizer(relativePath(), "/");
		const char* component = NULL;
		while ((component = tokenizer.GetNextToken()) != NULL) {
			const String safeComponent = SanitizePathComponent(component);
			if (safeComponent.HasChars()) {
				directory += "/";
				directory += safeComponent;
			}
		}
	}

	if (CreateDirectoryTree(directory).IsError())
		return false;

	String candidate = directory + "/" + safeName;

	// Never overwrite. A download that silently replaced an existing file would
	// be a data-loss bug triggerable by anyone who can name a file.
	if (PathExists(candidate)) {
		const int32 dotIndex = safeName.LastIndexOf('.');
		const String stem = (dotIndex > 0) ? safeName.Substring(0, dotIndex)
			: safeName;
		const String extension = (dotIndex > 0) ? safeName.Substring(dotIndex)
			: String();

		uint32 attempt = 2;
		for (; attempt < kMaximumRenameAttempts; attempt++) {
			candidate = directory + "/" + stem + " (" + String("%1").Arg(attempt)
				+ ")" + extension;
			if (PathExists(candidate) == false)
				break;
		}

		if (attempt >= kMaximumRenameAttempts)
			return false;
	}

	fOutputFile = fopen(candidate(), "wb");
	if (fOutputFile == NULL)
		return false;

	fOutputPath = candidate;
	return true;
}


void
FileDownload::_CloseOutputFile()
{
	if (fOutputFile != NULL) {
		fclose(fOutputFile);
		fOutputFile = NULL;
	}
}


void
FileDownload::_FinishCurrentFile()
{
	if (fOutputFile == NULL)
		return;

	_CloseOutputFile();

	const bool isComplete = (fCurrentFileSize <= 0
		|| fCurrentFileBytesDone >= fCurrentFileSize);

	if (isComplete) {
		fCompletedFileCount++;
		if (fListener != NULL)
			fListener->DownloadFileCompleted(this, fOutputPath);
	} else {
		// Short file: keep nothing that pretends to be the real thing.
		(void) remove(fOutputPath());
	}

	fOutputPath.Clear();
	fCurrentFileBytesDone = 0;
	fCurrentFileSize = 0;
}


void
FileDownload::_SetState(DownloadState state)
{
	if (state == fState)
		return;

	fState = state;
	if (fListener != NULL)
		fListener->DownloadStateChanged(this);
}


uint32
FileDownload::_CalculateChecksum(const uint8* data, uint32 byteCount)
{
	// Bit-for-bit what BeShare computes. It is a weak checksum, but matching it
	// exactly is the entire requirement.
	uint32 sum = 0;
	for (uint32 i = 0; i < byteCount; i++)
		sum += ((uint32) *(data++)) << (i % 24);

	return sum;
}


String
FileDownload::SanitizePathComponent(const String& component)
{
	String safe;
	(void) safe.Prealloc(component.Length());

	for (uint32 i = 0; i < component.Length(); i++) {
		const char character = component[i];

		// Separators and NUL would change what path this names; control
		// characters make a name that cannot be seen or typed.
		if (character == '/' || character == '\\' || character == '\0'
				|| (unsigned char) character < 0x20) {
			safe += '_';
		} else {
			safe += character;
		}
	}

	safe = safe.Trimmed();

	// "." and ".." navigate rather than name.  A leading dot would hide the
	// file; a leading dash makes it look like an option to any tool the user
	// later points at it.
	while (safe.StartsWith(".") || safe.StartsWith("-"))
		safe = safe.Substring(1).Trimmed();

	if (safe == "." || safe == "..")
		return String();

	return safe;
}


}  // namespace hitux

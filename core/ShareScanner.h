/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef SHARE_SCANNER_H
#define SHARE_SCANNER_H


#include "core/MimeTypeGuesser.h"

#include "util/Queue.h"
#include "util/String.h"

#include <pthread.h>


namespace hitux {


/** One file we are offering to other people. */
struct SharedFile
{
	SharedFile()
		:
		fileSize(0),
		modificationTime(0)
	{
	}

	// The leaf name, which is also the key other clients address it by. Note
	// that BeShare's node path is "beshare/files/<leafname>" with no directory
	// part, so two files with the same name in different sub-folders collide --
	// see ShareScanner's class comment.
	muscle::String fileName;

	// Sub-path below the share root, empty for a file at the top level. Sent as
	// "beshare:Path" so a downloader can rebuild the structure if they want it.
	muscle::String relativePath;

	// Where it actually lives, for serving it later. Never published.
	muscle::String absolutePath;

	muscle::String kind;

	int64 fileSize;
	int32 modificationTime;
};


/** Walks the share folder and reports what is in it.
  *
  * The walk happens on a background thread because a share folder can hold tens
  * of thousands of files and stat()ing them all would otherwise freeze the UI.
  * Results are handed to the main thread by TakeDiscoveredFiles(), which the
  * owner calls from its idle tick -- there is no callback into the caller from
  * the scanning thread, so nothing here needs to be thread-safe on the far side.
  *
  * On duplicate leaf names: the protocol keys a shared file by its bare name, so
  * "notes.txt" in two sub-folders is one node as far as the server is concerned.
  * The first one found wins and the rest are counted and reported, because
  * silently sharing an arbitrary one of them is worse than saying so.
  */
class ShareScanner
{
public:
	ShareScanner();
	~ShareScanner();

	/** Sets the folder to share. Takes effect on the next StartScan().
	  * @param shareDirectory absolute path to the folder
	  */
	void SetShareDirectory(const muscle::String& shareDirectory);

	const muscle::String& GetShareDirectory() const { return fShareDirectory; }

	/** Starts a scan on a background thread, cancelling any scan in progress. */
	void StartScan();

	/** Cancels a running scan and waits for the thread to stop. */
	void StopScan();

	bool IsScanning() const;

	/** Returns files found since the last call, and empties the pending list.
	  * Call from the owning thread only.
	  */
	muscle::Queue<SharedFile> TakeDiscoveredFiles();

	/** Returns true exactly once, on the first call after a scan completes. */
	bool TakeScanFinished();

	/** How many leaf names were skipped because another file already had that
	  * name. Only meaningful once a scan has finished.
	  */
	uint32 GetDuplicateNameCount() const;

private:
	static void* _ThreadEntry(void* argument);
	void _ScanThread();
	void _ScanDirectory(const muscle::String& directoryPath,
		const muscle::String& relativePath, uint32 depth);

	muscle::String fShareDirectory;

	MimeTypeGuesser fMimeTypes;

	// Guards everything below it, which both threads touch.
	mutable pthread_mutex_t fLock;

	muscle::Queue<SharedFile> fPendingFiles;

	// Leaf names already claimed, so a duplicate can be recognised and counted.
	muscle::Hashtable<muscle::String, bool> fClaimedNames;

	uint32 fDuplicateNameCount;

	pthread_t fThread;
	bool fThreadRunning;
	bool fScanFinished;
	bool fShouldAbort;
};


}  // namespace hitux


#endif  // SHARE_SCANNER_H

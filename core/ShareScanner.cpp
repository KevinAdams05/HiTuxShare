/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "core/ShareScanner.h"

#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

using namespace muscle;


namespace hitux {


namespace {


// Symlinked folders can point back up the tree, and a share folder is the user's
// own, so a cycle is careless rather than hostile -- but it still has to
// terminate. This is far deeper than any real share layout.
const uint32 kMaximumScanDepth = 32;


}  // unnamed namespace


ShareScanner::ShareScanner()
	:
	fDuplicateNameCount(0),
	fThreadRunning(false),
	fScanFinished(false),
	fShouldAbort(false)
{
	pthread_mutex_init(&fLock, NULL);
}


ShareScanner::~ShareScanner()
{
	StopScan();
	pthread_mutex_destroy(&fLock);
}


void
ShareScanner::SetShareDirectory(const String& shareDirectory)
{
	fShareDirectory = shareDirectory;
}


void
ShareScanner::StartScan()
{
	StopScan();

	if (fShareDirectory.IsEmpty())
		return;

	pthread_mutex_lock(&fLock);
	fPendingFiles.Clear();
	fClaimedNames.Clear();
	fDuplicateNameCount = 0;
	fScanFinished = false;
	fShouldAbort = false;
	pthread_mutex_unlock(&fLock);

	if (pthread_create(&fThread, NULL, _ThreadEntry, this) == 0)
		fThreadRunning = true;
}


void
ShareScanner::StopScan()
{
	if (fThreadRunning == false)
		return;

	pthread_mutex_lock(&fLock);
	fShouldAbort = true;
	pthread_mutex_unlock(&fLock);

	(void) pthread_join(fThread, NULL);
	fThreadRunning = false;
}


bool
ShareScanner::IsScanning() const
{
	if (fThreadRunning == false)
		return false;

	pthread_mutex_lock(&fLock);
	const bool finished = fScanFinished;
	pthread_mutex_unlock(&fLock);
	return finished == false;
}


Queue<SharedFile>
ShareScanner::TakeDiscoveredFiles()
{
	pthread_mutex_lock(&fLock);
	Queue<SharedFile> taken;
	taken.SwapContents(fPendingFiles);
	pthread_mutex_unlock(&fLock);
	return taken;
}


bool
ShareScanner::TakeScanFinished()
{
	pthread_mutex_lock(&fLock);
	const bool finished = fScanFinished;
	if (finished)
		fScanFinished = false;

	pthread_mutex_unlock(&fLock);
	return finished;
}


uint32
ShareScanner::GetDuplicateNameCount() const
{
	pthread_mutex_lock(&fLock);
	const uint32 count = fDuplicateNameCount;
	pthread_mutex_unlock(&fLock);
	return count;
}


void*
ShareScanner::_ThreadEntry(void* argument)
{
	static_cast<ShareScanner*>(argument)->_ScanThread();
	return NULL;
}


void
ShareScanner::_ScanThread()
{
	_ScanDirectory(fShareDirectory, String(), 0);

	pthread_mutex_lock(&fLock);
	fScanFinished = true;
	pthread_mutex_unlock(&fLock);
}


void
ShareScanner::_ScanDirectory(const String& directoryPath,
	const String& relativePath, uint32 depth)
{
	if (depth > kMaximumScanDepth)
		return;

	DIR* directory = opendir(directoryPath());
	if (directory == NULL)
		return;

	struct dirent* entry = NULL;
	while ((entry = readdir(directory)) != NULL) {
		pthread_mutex_lock(&fLock);
		const bool shouldAbort = fShouldAbort;
		pthread_mutex_unlock(&fLock);
		if (shouldAbort)
			break;

		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;

		// A dot-file in a share folder is nearly always something the user did
		// not mean to publish to strangers.
		if (entry->d_name[0] == '.')
			continue;

		const String entryName(entry->d_name);
		const String fullPath = directoryPath + "/" + entryName;

		// stat() rather than lstat(): following a symlink is what the user
		// meant by putting it there. The depth cap handles a link that loops.
		struct stat information;
		if (stat(fullPath(), &information) != 0)
			continue;

		if (S_ISDIR(information.st_mode)) {
			const String childRelative = relativePath.IsEmpty()
				? entryName : (relativePath + "/" + entryName);
			_ScanDirectory(fullPath, childRelative, depth + 1);
			continue;
		}

		if (S_ISREG(information.st_mode) == false)
			continue;

		pthread_mutex_lock(&fLock);

		// The protocol keys a shared file by its bare name, so the same leaf
		// name in two folders would be one node. Keep the first and count the
		// rest rather than letting one silently shadow the other.
		if (fClaimedNames.ContainsKey(entryName)) {
			fDuplicateNameCount++;
			pthread_mutex_unlock(&fLock);
			continue;
		}

		(void) fClaimedNames.Put(entryName, true);
		pthread_mutex_unlock(&fLock);

		SharedFile shared;
		shared.fileName = entryName;
		shared.relativePath = relativePath;
		shared.absolutePath = fullPath;
		shared.fileSize = (int64) information.st_size;
		shared.modificationTime = (int32) information.st_mtime;
		shared.kind = fMimeTypes.GuessMimeType(entryName);

		pthread_mutex_lock(&fLock);
		(void) fPendingFiles.AddTail(shared);
		pthread_mutex_unlock(&fLock);
	}

	closedir(directory);
}


}  // namespace hitux

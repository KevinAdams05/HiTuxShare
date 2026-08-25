/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "core/DownloadManager.h"

#include "core/FormatUtilities.h"

#include "util/Hashtable.h"

using namespace muscle;


namespace hitux {


DownloadManager::DownloadManager(ICallbackMechanism* callbackMechanism)
	:
	fCallbackMechanism(callbackMechanism),
	fListener(NULL),
	fRetainFilePaths(false)
{
}


DownloadManager::~DownloadManager()
{
	AbortAll();

	for (uint32 i = 0; i < fDownloads.GetNumItems(); i++)
		delete fDownloads[i];
}


void
DownloadManager::StartDownloads(const Queue<FileResult>& results,
	const UserRegistry& users, const String& localSessionId,
	const String& localUserName)
{
	if (results.IsEmpty())
		return;

	// Group by sharer first, so one peer means one connection carrying all the
	// files the user picked from them.
	Hashtable<String, Queue<FileResult> > bySharer;
	for (uint32 i = 0; i < results.GetNumItems(); i++) {
		Queue<FileResult>* group = bySharer.GetOrPut(results[i].sessionId);
		if (group != NULL)
			(void) group->AddTail(results[i]);
	}

	bool listChanged = false;

	for (HashtableIterator<String, Queue<FileResult> > iterator(bySharer);
			iterator.HasData(); iterator++) {
		const String& sessionId = iterator.GetKey();
		const Queue<FileResult>& group = iterator.GetValue();

		const UserRecord* sharer = users.FindUser(sessionId);
		if (sharer == NULL) {
			if (fListener != NULL) {
				fListener->DownloadReport(LOG_ERROR_MESSAGE,
					String("That user has left, so their files are gone too."));
			}
			continue;
		}

		// A peer that advertises no port accepts no connections, and reaching
		// them needs the connect-back path we have not built yet. Saying so is
		// better than a connection that silently never establishes.
		if (sharer->isFirewalled || sharer->port <= 0) {
			if (fListener != NULL) {
				fListener->DownloadReport(LOG_WARNING_MESSAGE,
					sharer->GetDisplayName()
						+ " is behind a firewall. Downloading from firewalled"
						" peers needs connect-back, which is not implemented"
						" yet.");
			}
			continue;
		}

		FileDownload* download = new FileDownload(fCallbackMechanism,
			fDownloadDirectory);
		download->SetListener(this);
		download->SetRetainFilePaths(fRetainFilePaths);
		download->SetRemoteUserName(sharer->GetDisplayName());

		for (uint32 i = 0; i < group.GetNumItems(); i++) {
			download->AddRequestedFile(group[i].fileName, group[i].path,
				group[i].fileSize);
		}

		if (fDownloads.AddTail(download).IsError()) {
			delete download;
			continue;
		}

		listChanged = true;

		if (download->Start(sharer->hostName, (uint16) sharer->port, sessionId,
				localSessionId, localUserName).IsError()) {
			if (fListener != NULL) {
				fListener->DownloadReport(LOG_ERROR_MESSAGE,
					String("Could not reach ") + sharer->GetDisplayName()
						+ ": " + download->GetErrorText());
			}
		} else if (fListener != NULL) {
			fListener->DownloadReport(LOG_INFORMATION_MESSAGE,
				String("Asking ") + sharer->GetDisplayName() + " for "
					+ String("%1").Arg(group.GetNumItems()) + " file(s).");
		}
	}

	if (listChanged && fListener != NULL)
		fListener->DownloadListChanged();
}


void
DownloadManager::AbortDownload(uint32 index)
{
	if (index >= fDownloads.GetNumItems())
		return;

	fDownloads[index]->Abort("Cancelled");
}


void
DownloadManager::ClearFinishedDownloads()
{
	bool removedAny = false;

	for (int32 i = (int32) fDownloads.GetNumItems() - 1; i >= 0; i--) {
		FileDownload* download = fDownloads[(uint32) i];
		if (download->IsFinished()) {
			(void) fDownloads.RemoveItemAt((uint32) i);
			delete download;
			removedAny = true;
		}
	}

	if (removedAny && fListener != NULL)
		fListener->DownloadListChanged();
}


void
DownloadManager::AbortAll()
{
	for (uint32 i = 0; i < fDownloads.GetNumItems(); i++)
		fDownloads[i]->Abort("Shutting down");
}


const FileDownload*
DownloadManager::GetDownloadAt(uint32 index) const
{
	return (index < fDownloads.GetNumItems()) ? fDownloads[index] : NULL;
}


void
DownloadManager::DownloadStateChanged(FileDownload* download)
{
	const int32 index = _FindIndexOf(download);
	if (index < 0)
		return;

	if (download->GetState() == DOWNLOAD_FAILED && fListener != NULL) {
		fListener->DownloadReport(LOG_ERROR_MESSAGE,
			String("Download from ")
				+ (download->GetRemoteUserName().HasChars()
					? download->GetRemoteUserName()
					: download->GetRemoteSessionId())
				+ " failed: " + download->GetErrorText());
	}

	if (fListener != NULL)
		fListener->DownloadChanged((uint32) index);
}


void
DownloadManager::DownloadProgress(FileDownload* download)
{
	const int32 index = _FindIndexOf(download);
	if (index >= 0 && fListener != NULL)
		fListener->DownloadChanged((uint32) index);
}


void
DownloadManager::DownloadFileCompleted(FileDownload* download,
	const String& localPath)
{
	const int32 index = _FindIndexOf(download);
	if (index >= 0 && fListener != NULL)
		fListener->DownloadChanged((uint32) index);

	if (fListener != NULL) {
		fListener->DownloadReport(LOG_INFORMATION_MESSAGE,
			String("Finished downloading ") + localPath);
	}
}


int32
DownloadManager::_FindIndexOf(const FileDownload* download) const
{
	for (uint32 i = 0; i < fDownloads.GetNumItems(); i++) {
		if (fDownloads[i] == download)
			return (int32) i;
	}

	return -1;
}


}  // namespace hitux

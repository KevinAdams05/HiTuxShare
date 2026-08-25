/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef DOWNLOAD_MANAGER_H
#define DOWNLOAD_MANAGER_H


#include "core/ChatMessage.h"
#include "core/FileDownload.h"
#include "core/FileResult.h"
#include "core/UserRegistry.h"


namespace hitux {


/** How a front-end hears that the set of transfers, or one transfer, changed. */
class DownloadManagerListener
{
public:
	virtual ~DownloadManagerListener() {}

	/** A transfer was added or removed, so the list itself changed shape. */
	virtual void DownloadListChanged() = 0;

	/** One transfer progressed or changed state.
	  * @param index which transfer, by position in the list
	  */
	virtual void DownloadChanged(uint32 index) = 0;

	/** Something worth telling the user, for the chat log.
	  * @param type how to colour it
	  * @param text what to say
	  */
	virtual void DownloadReport(LogMessageType type,
		const muscle::String& text) = 0;
};


/** Owns the running downloads and decides how requests become sessions.
  *
  * Selecting five files from one peer produces one connection carrying five
  * files, not five connections: BeShare peers serve one session at a time and
  * queue the rest, so fanning out would just make us wait in our own way five
  * times over.
  */
class DownloadManager : public FileDownloadListener
{
public:
	/** Constructor.
	  * @param callbackMechanism the event-loop bridge, passed to each download
	  */
	explicit DownloadManager(muscle::ICallbackMechanism* callbackMechanism);

	virtual ~DownloadManager();

	void SetListener(DownloadManagerListener* listener) { fListener = listener; }

	void SetDownloadDirectory(const muscle::String& downloadDirectory)
	{
		fDownloadDirectory = downloadDirectory;
	}

	void SetRetainFilePaths(bool retainFilePaths)
	{
		fRetainFilePaths = retainFilePaths;
	}

	/** Starts downloading the given results, grouped by who is sharing them.
	  * @param results what the user selected
	  * @param users the registry, for each sharer's advertised address
	  * @param localSessionId our session ID, which peers expect to be told
	  * @param localUserName our name, likewise
	  */
	void StartDownloads(const muscle::Queue<FileResult>& results,
		const UserRegistry& users, const muscle::String& localSessionId,
		const muscle::String& localUserName);

	/** Aborts a transfer.
	  * @param index which transfer
	  */
	void AbortDownload(uint32 index);

	/** Drops finished and failed transfers from the list. */
	void ClearFinishedDownloads();

	/** Aborts everything, e.g. on quit. */
	void AbortAll();

	uint32 GetDownloadCount() const { return fDownloads.GetNumItems(); }

	const FileDownload* GetDownloadAt(uint32 index) const;

	// FileDownloadListener
	virtual void DownloadStateChanged(FileDownload* download);
	virtual void DownloadProgress(FileDownload* download);
	virtual void DownloadFileCompleted(FileDownload* download,
		const muscle::String& localPath);

private:
	int32 _FindIndexOf(const FileDownload* download) const;

	muscle::ICallbackMechanism* fCallbackMechanism;
	DownloadManagerListener* fListener;

	muscle::String fDownloadDirectory;
	bool fRetainFilePaths;

	muscle::Queue<FileDownload*> fDownloads;
};


}  // namespace hitux


#endif  // DOWNLOAD_MANAGER_H

/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef FILE_RESULT_H
#define FILE_RESULT_H


#include "message/Message.h"
#include "util/String.h"


namespace hitux {


/** One file that somebody on the server is sharing.
  *
  * Results arrive from a live subscription and keep arriving for as long as the
  * query is running -- a new match appears the moment another user shares
  * something, without anyone refreshing anything. So a result set is a stream,
  * not a snapshot, and the UI has to be able to absorb additions and removals
  * at any time.
  *
  * A result is identified by (sessionId, fileName) together: file names are not
  * unique across users, and the same user cannot share two files under one name.
  */
struct FileResult
{
	FileResult()
		:
		fileSize(0),
		modificationTime(0),
		isFirewalled(false)
	{
	}

	// Who is sharing it.  Session IDs are only meaningful within one connection.
	muscle::String sessionId;

	muscle::String fileName;

	// Sub-path below the sharer's share root, empty for a file at the top level.
	muscle::String path;

	// MIME type as the sharer's system understood it, e.g. "audio/x-mpeg".
	// Often empty when the sharer is not running Haiku.
	muscle::String kind;

	int64 fileSize;

	// Unix time.  On the wire this is an int32, so it will wrap in 2038 -- that
	// is the protocol's problem, and pretending otherwise here would just move
	// the surprise somewhere less obvious.
	int32 modificationTime;

	// True when the sharer published under "beshare/fires/", meaning they cannot
	// accept an incoming connection and we would have to ask them to call us
	// back. Two firewalled peers can never transfer to each other.
	bool isFirewalled;

	/** The complete node Message, kept so the UI can show whatever extra
	  * attributes the sharer published without the core having to know what any
	  * of them mean.
	  *
	  * On Haiku these are real BFS attributes taken from the file's MIME type --
	  * track numbers, bitrates, mail subjects. We can display them; we cannot
	  * produce them, because Linux xattrs are tiny and nobody populates them.
	  */
	muscle::MessageRef attributes;
};


}  // namespace hitux


#endif  // FILE_RESULT_H

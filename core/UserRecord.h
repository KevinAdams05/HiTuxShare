/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef USER_RECORD_H
#define USER_RECORD_H


#include "util/String.h"


namespace hitux {


/** Everything one remote user has published about themselves on a server.
  *
  * This is a plain data record rather than an encapsulated class: it is assembled
  * field by field as the server sends separate node updates for the name, status,
  * bandwidth and file count, so accessors would buy nothing but noise.
  *
  * A user is only ever identified by their session ID.  Names are not unique, are
  * not authenticated, and change at will -- never key anything off them.
  */
struct UserRecord
{
	UserRecord()
		:
		port(0),
		installId(0),
		fileCount(0),
		bandwidthBitsPerSecond(0),
		uploadsCurrent(0),
		uploadsMax(0),
		isBot(false),
		isFirewalled(false),
		supportsPartialHashing(false),
		supportsSsl(false),
		supportsRanges(false)
	{
	}

	muscle::String sessionId;
	muscle::String userName;
	muscle::String hostName;

	// Free-form status string, e.g. "here" or "away".
	muscle::String userStatus;

	// Client identification, e.g. "HiShare v1.2" or "BeShare v3.04".  Empty when the
	// peer publishes no version fields at all.
	muscle::String clientVersion;

	// Human-readable connection speed the user picked, e.g. "Cable modem".
	muscle::String bandwidthLabel;

	// The port this user accepts incoming file transfers on.  Zero means they accept
	// none, which is normal for chat-only clients.
	int32 port;

	// Stable per-installation identifier, used for upload bans and queue fairness.
	// It is not an authenticated identity -- any client can claim any value.
	uint64 installId;

	uint32 fileCount;
	uint32 bandwidthBitsPerSecond;

	// How many uploads this user currently has running, out of their maximum.
	uint32 uploadsCurrent;
	uint32 uploadsMax;

	bool isBot;

	// True when this user published their files under "beshare/fires/" instead of
	// "beshare/files/", meaning they cannot accept incoming connections.  Two
	// firewalled peers can never transfer to each other.
	bool isFirewalled;

	// Capabilities the peer advertises.  Absent flags mean "no", so an old client
	// that has never heard of these fields degrades correctly.
	bool supportsPartialHashing;
	bool supportsSsl;
	bool supportsRanges;

	/** Returns the name to show in the user list, falling back to the session ID for
	  * a user whose name node has not arrived yet.
	  */
	muscle::String GetDisplayName() const
	{
		return userName.HasChars() ? userName : sessionId;
	}
};


}  // namespace hitux


#endif  // USER_RECORD_H

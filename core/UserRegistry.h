/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef USER_REGISTRY_H
#define USER_REGISTRY_H


#include "core/UserRecord.h"

#include "util/Hashtable.h"
#include "util/Queue.h"


namespace hitux {


/** Everyone we currently know about on one server, keyed by session ID.
  *
  * The server publishes a user's state as several independent nodes -- name, status,
  * bandwidth, file count -- which arrive in no guaranteed order, and a user may be
  * mentioned in chat before their name node ever shows up.  So the registry creates
  * a record on first mention of a session ID and fills it in as pieces arrive, rather
  * than waiting for a complete user.
  */
class UserRegistry
{
public:
	UserRegistry();
	~UserRegistry();

	/** Returns the existing record for (sessionId), creating an empty one if this is
	  * the first time we have heard of it.
	  * @param sessionId the session to look up
	  * @param isNewUser set true iff a record was created by this call
	  * @returns a pointer that stays valid until the user is removed
	  */
	UserRecord* GetOrCreateUser(const muscle::String& sessionId, bool& isNewUser);

	/** Returns the record for (sessionId), or NULL if we do not know that session.
	  * @param sessionId the session to look up
	  */
	const UserRecord* FindUser(const muscle::String& sessionId) const;

	/** Removes a user, copying their final state out first.
	  * @param sessionId the session that left
	  * @param removedUser receives the departing user's last known state
	  * @returns true iff we knew that session
	  */
	bool RemoveUser(const muscle::String& sessionId, UserRecord& removedUser);

	/** Forgets every user.  Called on disconnect, because session IDs are only
	  * meaningful within one connection to one server.
	  */
	void Clear();

	/** Returns the name to show for a session, falling back to the session ID itself
	  * when the name node has not arrived (or the user is already gone).
	  * @param sessionId the session to name
	  */
	muscle::String GetDisplayNameForSession(const muscle::String& sessionId) const;

	/** Resolves what the user typed into the session IDs it refers to.
	  *
	  * Accepts an exact session ID, an exact user name, or a name pattern with '*'
	  * and '?' wildcards.  Several users may share a name, so this returns every
	  * match rather than picking one; commands such as /msg act on all of them, which
	  * is what BeShare does.
	  *
	  * @param nameOrSessionId what the user typed
	  * @returns matching session IDs, empty if nothing matched
	  */
	muscle::Queue<muscle::String> ResolveToSessionIds(
		const muscle::String& nameOrSessionId) const;

	/** Returns every known user, keyed by session ID. */
	const muscle::Hashtable<muscle::String, UserRecord>& GetUsers() const
	{
		return fUsers;
	}

	uint32 GetUserCount() const { return fUsers.GetNumItems(); }

private:
	muscle::Hashtable<muscle::String, UserRecord> fUsers;
};


}  // namespace hitux


#endif  // USER_REGISTRY_H

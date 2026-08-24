/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "core/UserRegistry.h"

#include "regex/StringMatcher.h"

using namespace muscle;


namespace hitux {


UserRegistry::UserRegistry()
{
}


UserRegistry::~UserRegistry()
{
}


UserRecord*
UserRegistry::GetOrCreateUser(const String& sessionId, bool& isNewUser)
{
	UserRecord* existingUser = fUsers.Get(sessionId);
	if (existingUser != NULL) {
		isNewUser = false;
		return existingUser;
	}

	UserRecord newUser;
	newUser.sessionId = sessionId;
	if (fUsers.Put(sessionId, newUser).IsError()) {
		isNewUser = false;
		return NULL;
	}

	isNewUser = true;
	return fUsers.Get(sessionId);
}


const UserRecord*
UserRegistry::FindUser(const String& sessionId) const
{
	return fUsers.Get(sessionId);
}


bool
UserRegistry::RemoveUser(const String& sessionId, UserRecord& removedUser)
{
	const UserRecord* user = fUsers.Get(sessionId);
	if (user == NULL)
		return false;

	removedUser = *user;
	(void) fUsers.Remove(sessionId);
	return true;
}


void
UserRegistry::Clear()
{
	fUsers.Clear();
}


String
UserRegistry::GetDisplayNameForSession(const String& sessionId) const
{
	const UserRecord* user = fUsers.Get(sessionId);
	if (user == NULL)
		return sessionId;

	return user->GetDisplayName();
}


Queue<String>
UserRegistry::ResolveToSessionIds(const String& nameOrSessionId) const
{
	Queue<String> matches;
	if (nameOrSessionId.IsEmpty())
		return matches;

	// An exact session ID wins outright.  Session IDs are numeric and assigned by the
	// server, so a user whose name happens to look like one cannot shadow them.
	if (fUsers.ContainsKey(nameOrSessionId)) {
		(void) matches.AddTail(nameOrSessionId);
		return matches;
	}

	// Then exact name matches, which is the overwhelmingly common case and must not
	// be beaten by a wildcard interpretation of the same string.
	for (auto iterator = fUsers.GetIterator(); iterator.HasData(); iterator++) {
		if (iterator.GetValue().userName == nameOrSessionId)
			(void) matches.AddTail(iterator.GetKey());
	}

	if (matches.HasItems())
		return matches;

	// Finally treat it as a pattern, but only if it actually looks like one -- other-
	// wise a typo'd name would silently match everybody through the implicit prefix
	// matching that StringMatcher does.
	if (nameOrSessionId.IndexOf('*') < 0 && nameOrSessionId.IndexOf('?') < 0)
		return matches;

	StringMatcher nameMatcher(nameOrSessionId);
	for (auto iterator = fUsers.GetIterator(); iterator.HasData(); iterator++) {
		if (nameMatcher.Match(iterator.GetValue().userName()))
			(void) matches.AddTail(iterator.GetKey());
	}

	return matches;
}


}  // namespace hitux

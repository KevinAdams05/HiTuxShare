/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef USER_FILTER_SET_H
#define USER_FILTER_SET_H


#include "util/String.h"


namespace hitux {


/** A comma-separated list of users, matched by session ID or by name pattern.
  *
  * The separator is a comma and specifically not whitespace, because user names
  * on this network routinely contain spaces -- splitting on those would make
  * half the names in a room unmatchable.
  *
  * Each entry is either an exact session ID or a case-insensitive glob against
  * the user's name, which is how BeShare has always read these. A session ID is
  * precise but changes every time that person reconnects; a name pattern
  * survives reconnects but is not authenticated, since anybody may use any name.
  * Both are offered because neither alone is good enough.
  */
class UserFilterSet
{
public:
	UserFilterSet();
	~UserFilterSet();

	/** Replaces the whole list.
	  * @param pattern comma-separated session IDs and name globs
	  */
	void SetPattern(const muscle::String& pattern);

	const muscle::String& GetPattern() const { return fPattern; }

	bool IsEmpty() const { return fPattern.IsEmpty(); }

	/** Adds one entry, leaving any existing ones alone.
	  * @param entry a session ID or name glob
	  */
	void AddEntry(const muscle::String& entry);

	/** Removes one entry if it is present.
	  * @param entry the entry to remove
	  * @returns true if something was removed
	  */
	bool RemoveEntry(const muscle::String& entry);

	void Clear();

	/** True if this user matches any entry.
	  * @param userName the user's display name
	  * @param sessionId the user's session ID
	  */
	bool Matches(const muscle::String& userName,
		const muscle::String& sessionId) const;

private:
	muscle::String fPattern;
};


}  // namespace hitux


#endif  // USER_FILTER_SET_H

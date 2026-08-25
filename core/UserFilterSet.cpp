/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "core/UserFilterSet.h"

#include "regex/StringMatcher.h"
#include "util/Queue.h"
#include "util/StringTokenizer.h"

using namespace muscle;


namespace hitux {


namespace {


/** Splits a comma-separated list into trimmed, non-empty entries.
  * @param pattern the list to split
  */
Queue<String>
SplitEntries(const String& pattern)
{
	Queue<String> entries;

	StringTokenizer tokenizer(pattern(), ",");
	const char* token = NULL;
	while ((token = tokenizer.GetNextToken()) != NULL) {
		const String entry = String(token).Trimmed();
		if (entry.HasChars())
			(void) entries.AddTail(entry);
	}

	return entries;
}


}  // unnamed namespace


UserFilterSet::UserFilterSet()
{
}


UserFilterSet::~UserFilterSet()
{
}


void
UserFilterSet::SetPattern(const String& pattern)
{
	fPattern = pattern.Trimmed();
}


void
UserFilterSet::AddEntry(const String& entry)
{
	const String trimmed = entry.Trimmed();
	if (trimmed.IsEmpty())
		return;

	const Queue<String> existing = SplitEntries(fPattern);
	for (uint32 i = 0; i < existing.GetNumItems(); i++) {
		if (existing[i].EqualsIgnoreCase(trimmed))
			return;
	}

	if (fPattern.HasChars())
		fPattern += ",";

	fPattern += trimmed;
}


bool
UserFilterSet::RemoveEntry(const String& entry)
{
	const String trimmed = entry.Trimmed();
	const Queue<String> existing = SplitEntries(fPattern);

	String rebuilt;
	bool removedAny = false;
	for (uint32 i = 0; i < existing.GetNumItems(); i++) {
		if (existing[i].EqualsIgnoreCase(trimmed)) {
			removedAny = true;
			continue;
		}

		if (rebuilt.HasChars())
			rebuilt += ",";

		rebuilt += existing[i];
	}

	fPattern = rebuilt;
	return removedAny;
}


void
UserFilterSet::Clear()
{
	fPattern.Clear();
}


bool
UserFilterSet::Matches(const String& userName, const String& sessionId) const
{
	if (fPattern.IsEmpty())
		return false;

	const Queue<String> entries = SplitEntries(fPattern);
	for (uint32 i = 0; i < entries.GetNumItems(); i++) {
		const String& entry = entries[i];

		// An exact session ID first: it is unambiguous, and it stops a name
		// pattern from accidentally matching a numeric ID.
		if (entry == sessionId)
			return true;

		if (userName.IsEmpty())
			continue;

		StringMatcher matcher(ToCaseInsensitive(entry));
		if (matcher.Match(userName()))
			return true;
	}

	return false;
}


}  // namespace hitux

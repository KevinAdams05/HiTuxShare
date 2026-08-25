/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "core/ChatAliases.h"

using namespace muscle;


namespace hitux {


ChatAliases::ChatAliases()
{
}


ChatAliases::~ChatAliases()
{
}


void
ChatAliases::SetAlias(const String& name, const String& value)
{
	const String cleanName = name.Trimmed().ToLowerCase();
	if (cleanName.IsEmpty() || value.Trimmed().IsEmpty())
		return;

	(void) fAliases.Put(cleanName, value.Trimmed());
}


bool
ChatAliases::RemoveAlias(const String& name)
{
	return fAliases.Remove(name.Trimmed().ToLowerCase()).IsOK();
}


void
ChatAliases::Clear()
{
	fAliases.Clear();
}


String
ChatAliases::Expand(const String& input) const
{
	const String trimmed = input.Trimmed();
	if (trimmed.StartsWith('/') == false || trimmed.StartsWith("//"))
		return input;

	const String withoutSlash = trimmed.Substring(1);
	const int32 spaceIndex = withoutSlash.IndexOf(' ');

	const String word = (spaceIndex >= 0)
		? withoutSlash.Substring(0, spaceIndex) : withoutSlash;
	const String remainder = (spaceIndex >= 0)
		? withoutSlash.Substring(spaceIndex + 1).Trimmed() : String();

	const String* value = fAliases.Get(word.ToLowerCase());
	if (value == NULL)
		return input;

	// Single pass: the result is not re-expanded, so an alias defined in terms
	// of another alias simply does not chain rather than looping forever.
	return remainder.HasChars() ? (*value + " " + remainder) : *value;
}


}  // namespace hitux

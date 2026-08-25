/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef CHAT_ALIASES_H
#define CHAT_ALIASES_H


#include "util/Hashtable.h"
#include "util/String.h"


namespace hitux {


/** User-defined shorthands for things typed into the chat line.
  *
  * "/alias hi /me waves hello" makes "/hi" expand to "/me waves hello".
  * Anything typed after the alias is appended, so "/alias g /msg" turns
  * "/g alice hello" into "/msg alice hello".
  *
  * Expansion is deliberately single-pass. An alias that expands to another
  * alias would be a small pleasure and an easy infinite loop, and nothing here
  * is worth a hang.
  */
class ChatAliases
{
public:
	ChatAliases();
	~ChatAliases();

	/** Defines or redefines an alias.
	  * @param name the alias name, without its leading slash
	  * @param value what it expands to
	  */
	void SetAlias(const muscle::String& name, const muscle::String& value);

	/** Removes an alias.
	  * @param name the alias to remove
	  * @returns true if it existed
	  */
	bool RemoveAlias(const muscle::String& name);

	void Clear();

	/** Expands a line if it starts with a known alias.
	  *
	  * @param input the raw line the user typed
	  * @returns the expanded line, or (input) unchanged if no alias applies
	  */
	muscle::String Expand(const muscle::String& input) const;

	const muscle::Hashtable<muscle::String, muscle::String>& GetAliases() const
	{
		return fAliases;
	}

	uint32 GetCount() const { return fAliases.GetNumItems(); }

private:
	muscle::Hashtable<muscle::String, muscle::String> fAliases;
};


}  // namespace hitux


#endif  // CHAT_ALIASES_H

/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef CHAT_COMMAND_PARSER_H
#define CHAT_COMMAND_PARSER_H


#include "util/Queue.h"
#include "util/String.h"


namespace hitux {


/** Which IRC-style command the user typed, if any. */
enum ChatCommandType
{
	// Not a command at all -- ordinary chat text, to be sent as typed.
	CHAT_COMMAND_NONE = 0,

	// Began with a slash but matched nothing we know.
	CHAT_COMMAND_UNKNOWN,

	CHAT_COMMAND_ACTION,
	CHAT_COMMAND_NICK,
	CHAT_COMMAND_STATUS,
	CHAT_COMMAND_AWAY,
	CHAT_COMMAND_MESSAGE,
	CHAT_COMMAND_PING,
	CHAT_COMMAND_CONNECT,
	CHAT_COMMAND_DISCONNECT,
	CHAT_COMMAND_START_QUERY,
	CHAT_COMMAND_STOP_QUERY,
	CHAT_COMMAND_GET,
	CHAT_COMMAND_CLEAR,
	CHAT_COMMAND_HELP,
	CHAT_COMMAND_INFO,
	CHAT_COMMAND_QUIT
};


/** One parsed line of user input. */
struct ChatCommand
{
	ChatCommand()
		:
		type(CHAT_COMMAND_NONE)
	{
	}

	ChatCommandType type;

	// The command word as typed, without the slash.  Kept so an unknown command can
	// be quoted back accurately in the error message.
	muscle::String commandName;

	// First argument for commands that address somebody, e.g. the nickname in
	// "/msg alice hello".  Empty otherwise.
	muscle::String target;

	// Everything after the command word, or after the target for commands that take
	// one.  For CHAT_COMMAND_NONE and CHAT_COMMAND_ACTION this is the text to send.
	muscle::String argument;
};


/** One row of the /help output. */
struct ChatCommandHelpEntry
{
	const char* commandName;
	const char* arguments;
	const char* description;
};


/** Turns what the user typed into a ChatCommand.
  *
  * Kept separate from any front-end so the same parsing serves the GUI, the headless
  * probe and unit tests, and so the command set cannot quietly diverge between them.
  */
class ChatCommandParser
{
public:
	/** Parses one line of input.
	  *
	  * A leading "//" escapes the slash: "//foo" is the literal chat text "/foo".
	  * A line starting with "/me " is reported as CHAT_COMMAND_ACTION but its
	  * argument keeps the "/me " prefix, because that prefix is what travels over the
	  * wire -- receivers are the ones that strip it.
	  *
	  * @param input the raw line the user typed
	  */
	static ChatCommand Parse(const muscle::String& input);

	/** Returns the table behind /help, in display order. */
	static muscle::Queue<ChatCommandHelpEntry> GetHelpEntries();
};


}  // namespace hitux


#endif  // CHAT_COMMAND_PARSER_H

/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "core/ChatCommandParser.h"

#include "core/BeShareProtocol.h"

using namespace muscle;


namespace hitux {


namespace {


struct CommandTableEntry
{
	const char* name;
	ChatCommandType type;
	bool takesTarget;
};


// The commands Phase 1 implements.  BeShare has around forty; these are the ones
// that carry their weight in a chat client, and the rest can land as they become
// useful without changing anything structural here.
const CommandTableEntry kCommandTable[] = {
	{ "nick",       CHAT_COMMAND_NICK,       false },
	{ "status",     CHAT_COMMAND_STATUS,     false },
	{ "away",       CHAT_COMMAND_AWAY,       false },
	{ "msg",        CHAT_COMMAND_MESSAGE,    true  },
	{ "ping",       CHAT_COMMAND_PING,       true  },
	{ "connect",    CHAT_COMMAND_CONNECT,    false },
	{ "start",      CHAT_COMMAND_START_QUERY, false },
	{ "query",      CHAT_COMMAND_START_QUERY, false },
	{ "stop",       CHAT_COMMAND_STOP_QUERY, false },
	{ "disconnect", CHAT_COMMAND_DISCONNECT, false },
	{ "clear",      CHAT_COMMAND_CLEAR,      false },
	{ "help",       CHAT_COMMAND_HELP,       false },
	{ "info",       CHAT_COMMAND_INFO,       false },
	{ "quit",       CHAT_COMMAND_QUIT,       false }
};

const uint32 kCommandTableSize = ARRAYITEMS(kCommandTable);


}  // unnamed namespace


ChatCommand
ChatCommandParser::Parse(const String& input)
{
	ChatCommand command;

	const String trimmedInput = input.Trimmed();
	if (trimmedInput.IsEmpty())
		return command;

	if (trimmedInput.StartsWith('/') == false) {
		command.type = CHAT_COMMAND_NONE;
		command.argument = trimmedInput;
		return command;
	}

	// "//foo" is how a user says the literal text "/foo" without it being read as a
	// command.  Strip one slash and send the rest as ordinary chat.
	if (trimmedInput.StartsWith("//")) {
		command.type = CHAT_COMMAND_NONE;
		command.argument = trimmedInput.Substring(1);
		return command;
	}

	// An action keeps its "/me " prefix: that prefix is the wire format, and every
	// other client strips it on receipt.  Stripping it here would send a plain line.
	if (trimmedInput == "/me" || trimmedInput.StartsWith(BESHARE_ACTION_PREFIX)) {
		command.type = CHAT_COMMAND_ACTION;
		command.commandName = "me";
		command.argument = trimmedInput;
		return command;
	}

	// Split "/word rest..." into the command word and everything after it.
	const String withoutSlash = trimmedInput.Substring(1);
	const int32 firstSpaceIndex = withoutSlash.IndexOf(' ');

	String commandWord;
	String remainder;
	if (firstSpaceIndex >= 0) {
		commandWord = withoutSlash.Substring(0, firstSpaceIndex);
		remainder = withoutSlash.Substring(firstSpaceIndex + 1).Trimmed();
	} else {
		commandWord = withoutSlash;
	}

	command.commandName = commandWord;
	const String lowerCommandWord = commandWord.ToLowerCase();

	for (uint32 i = 0; i < kCommandTableSize; i++) {
		if (lowerCommandWord != kCommandTable[i].name)
			continue;

		command.type = kCommandTable[i].type;

		if (kCommandTable[i].takesTarget) {
			const int32 targetSpaceIndex = remainder.IndexOf(' ');
			if (targetSpaceIndex >= 0) {
				command.target = remainder.Substring(0, targetSpaceIndex);
				command.argument = remainder.Substring(targetSpaceIndex + 1).Trimmed();
			} else {
				command.target = remainder;
			}
		} else {
			command.argument = remainder;
		}

		return command;
	}

	command.type = CHAT_COMMAND_UNKNOWN;
	return command;
}


Queue<ChatCommandHelpEntry>
ChatCommandParser::GetHelpEntries()
{
	Queue<ChatCommandHelpEntry> entries;

	const ChatCommandHelpEntry kHelpEntries[] = {
		{ "away",       "[message]",     "Set yourself away, with an optional message" },
		{ "clear",      NULL,            "Clear the chat log" },
		{ "connect",    "[server]",      "Connect to a server" },
		{ "disconnect", NULL,            "Disconnect from the server" },
		{ "help",       NULL,            "Show this help text" },
		{ "info",       NULL,            "Show connection and server information" },
		{ "me",         "<action>",      "Send an action, e.g. /me waves" },
		{ "msg",        "<user> <text>", "Send a private message" },
		{ "nick",       "<name>",        "Change your user name" },
		{ "ping",       "<user>",        "Measure the round trip to another client" },
		{ "quit",       NULL,            "Quit HiTuxShare" },
		{ "start",      "<pattern>",     "Search shared files, e.g. /start *.hpkg" },
		{ "stop",       NULL,            "Stop the running search" },
		{ "status",     "<status>",      "Set your status string" }
	};

	for (uint32 i = 0; i < ARRAYITEMS(kHelpEntries); i++)
		(void) entries.AddTail(kHelpEntries[i]);

	return entries;
}


}  // namespace hitux

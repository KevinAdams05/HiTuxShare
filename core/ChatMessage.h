/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef CHAT_MESSAGE_H
#define CHAT_MESSAGE_H


#include "util/String.h"


namespace hitux {


/** How a line in the chat log should be classified for colouring and filtering.
  *
  * The set matches BeShare's LogMessageType so that a user moving from HiShare sees
  * the same categories, and so a future settings import can map straight across.
  */
enum LogMessageType
{
	LOG_INFORMATION_MESSAGE = 0,
	LOG_WARNING_MESSAGE,
	LOG_ERROR_MESSAGE,
	LOG_LOCAL_USER_CHAT_MESSAGE,
	LOG_REMOTE_USER_CHAT_MESSAGE,
	LOG_USER_EVENT_MESSAGE,
	LOG_UPLOAD_EVENT_MESSAGE,

	NUM_LOG_MESSAGE_TYPES
};


/** One line destined for the chat log.
  *
  * Chat, actions ("/me waves"), private messages, join and leave notices and our own
  * error reports all arrive here, distinguished by (type) and the two flags.  The
  * front-end decides how each is rendered; the core never formats for display.
  */
struct ChatMessage
{
	ChatMessage()
		:
		type(LOG_INFORMATION_MESSAGE),
		isPrivate(false),
		isAction(false),
		isFromLocalUser(false),
		isHighlighted(false)
	{
	}

	ChatMessage(LogMessageType messageType, const muscle::String& messageText)
		:
		type(messageType),
		text(messageText),
		isPrivate(false),
		isAction(false),
		isFromLocalUser(false),
		isHighlighted(false)
	{
	}

	LogMessageType type;

	// Session ID of the sender, empty for messages the client generated itself.
	muscle::String senderSessionId;

	// Sender's name resolved at the time of receipt.  Kept as a copy rather than
	// looked up later because users rename themselves and leave, and a chat log
	// should say who said it *then*.
	muscle::String senderName;

	muscle::String text;

	bool isPrivate;

	// True when (text) had the "/me " prefix stripped and should be rendered as an
	// action rather than as speech.
	bool isAction;

	bool isFromLocalUser;

	// Set by the front-end for a line it wants to stand out -- a watched user,
	// for instance. The core never sets it; it is a presentation decision.
	bool isHighlighted;
};


}  // namespace hitux


#endif  // CHAT_MESSAGE_H

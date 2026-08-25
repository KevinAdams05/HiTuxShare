/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef CHAT_LOGGER_H
#define CHAT_LOGGER_H


#include "core/ChatMessage.h"

#include <stdio.h>


namespace hitux {


/** Appends chat to a file, one file per server per day.
  *
  * Off by default and deliberately so. A chat log is a record of other people's
  * words written to your disk, and turning that on should be a decision rather
  * than a default somebody discovers later.
  *
  * The file is reopened when the date rolls over, so a client left running for
  * a week produces seven readable files rather than one enormous one.
  */
class ChatLogger
{
public:
	ChatLogger();
	~ChatLogger();

	void SetEnabled(bool enabled);
	bool GetEnabled() const { return fIsEnabled; }

	/** Sets where logs are written; takes effect on the next message.
	  * @param directory absolute path to the log folder
	  */
	void SetLogDirectory(const muscle::String& directory);

	/** Sets which server's log we are writing, so two connections do not
	  * interleave into one file.
	  * @param serverName the server's address
	  */
	void SetServerName(const muscle::String& serverName);

	/** Appends one message, opening or rotating the file as needed.
	  * @param message the line to record
	  */
	void Log(const ChatMessage& message);

	/** The file currently being written, or empty if logging is off. */
	const muscle::String& GetCurrentPath() const { return fCurrentPath; }

private:
	bool _EnsureFileOpen();
	void _CloseFile();
	static muscle::String _SanitizeForFileName(const muscle::String& text);

	bool fIsEnabled;
	muscle::String fDirectory;
	muscle::String fServerName;

	FILE* fFile;
	muscle::String fCurrentPath;

	// The date the open file belongs to, as YYYY-MM-DD, so a roll-over is a
	// string comparison rather than date arithmetic.
	muscle::String fCurrentDate;
};


}  // namespace hitux


#endif  // CHAT_LOGGER_H

/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "core/ChatLogger.h"

#include "util/StringTokenizer.h"

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

using namespace muscle;


namespace hitux {


namespace {


/** Creates a directory and any missing parents.
  * @param directoryPath the directory to create
  */
bool
CreateDirectoryTree(const String& directoryPath)
{
	if (directoryPath.IsEmpty())
		return false;

	String pathSoFar;
	StringTokenizer tokenizer(directoryPath(), "/");
	const char* component = NULL;
	while ((component = tokenizer.GetNextToken()) != NULL) {
		pathSoFar += "/";
		pathSoFar += component;
		if (mkdir(pathSoFar(), 0700) != 0 && errno != EEXIST)
			return false;
	}

	return true;
}


/** Returns the local date as YYYY-MM-DD. */
String
TodaysDate()
{
	const time_t now = time(NULL);
	struct tm brokenDown;
	if (localtime_r(&now, &brokenDown) == NULL)
		return String("unknown");

	char buffer[32];
	(void) strftime(buffer, sizeof(buffer), "%Y-%m-%d", &brokenDown);
	return String(buffer);
}


/** Returns the local time as HH:MM:SS. */
String
CurrentTimeOfDay()
{
	const time_t now = time(NULL);
	struct tm brokenDown;
	if (localtime_r(&now, &brokenDown) == NULL)
		return String("??:??:??");

	char buffer[32];
	(void) strftime(buffer, sizeof(buffer), "%H:%M:%S", &brokenDown);
	return String(buffer);
}


}  // unnamed namespace


ChatLogger::ChatLogger()
	:
	fIsEnabled(false),
	fFile(NULL)
{
}


ChatLogger::~ChatLogger()
{
	_CloseFile();
}


void
ChatLogger::SetEnabled(bool enabled)
{
	if (enabled == fIsEnabled)
		return;

	fIsEnabled = enabled;
	if (enabled == false)
		_CloseFile();
}


void
ChatLogger::SetLogDirectory(const String& directory)
{
	if (directory == fDirectory)
		return;

	fDirectory = directory;
	_CloseFile();
}


void
ChatLogger::SetServerName(const String& serverName)
{
	if (serverName == fServerName)
		return;

	fServerName = serverName;
	_CloseFile();
}


void
ChatLogger::Log(const ChatMessage& message)
{
	if (fIsEnabled == false || _EnsureFileOpen() == false)
		return;

	const String timestamp = CurrentTimeOfDay();
	const char* privateMarker = message.isPrivate ? "[private] " : "";

	if (message.isAction) {
		fprintf(fFile, "[%s] %s* %s %s\n", timestamp(), privateMarker,
			message.senderName(), message.text());
	} else if (message.senderName.HasChars()) {
		fprintf(fFile, "[%s] %s<%s> %s\n", timestamp(), privateMarker,
			message.senderName(), message.text());
	} else {
		fprintf(fFile, "[%s] *** %s\n", timestamp(), message.text());
	}

	// Flushed per line rather than buffered: a log that loses the last few
	// minutes because the program was killed is not much of a log.
	fflush(fFile);
}


bool
ChatLogger::_EnsureFileOpen()
{
	const String today = TodaysDate();
	if (fFile != NULL && today == fCurrentDate)
		return true;

	_CloseFile();

	if (fDirectory.IsEmpty() || CreateDirectoryTree(fDirectory) == false)
		return false;

	const String server = fServerName.HasChars()
		? _SanitizeForFileName(fServerName) : String("chat");

	fCurrentDate = today;
	fCurrentPath = fDirectory + "/" + server + "-" + today + ".log";

	fFile = fopen(fCurrentPath(), "a");
	if (fFile == NULL) {
		fCurrentPath.Clear();
		return false;
	}

	return true;
}


void
ChatLogger::_CloseFile()
{
	if (fFile != NULL) {
		fclose(fFile);
		fFile = NULL;
	}

	fCurrentPath.Clear();
	fCurrentDate.Clear();
}


String
ChatLogger::_SanitizeForFileName(const String& text)
{
	// A server name is user-supplied and reaches the filesystem here, so it is
	// reduced to characters that cannot navigate anywhere.
	String safe;
	for (uint32 i = 0; i < text.Length(); i++) {
		const char character = text[i];
		const bool isSafe = (character >= 'a' && character <= 'z')
			|| (character >= 'A' && character <= 'Z')
			|| (character >= '0' && character <= '9')
			|| character == '.' || character == '-' || character == '_';
		safe += isSafe ? character : '_';
	}

	while (safe.StartsWith("."))
		safe = safe.Substring(1);

	return safe.HasChars() ? safe : String("chat");
}


}  // namespace hitux

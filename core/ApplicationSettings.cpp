/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "core/ApplicationSettings.h"

#include "core/BeShareProtocol.h"

#include "dataio/FileDataIO.h"
#include "util/MiscUtilityFunctions.h"
#include "util/StringTokenizer.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

using namespace muscle;


namespace hitux {


namespace {


const char* const kFieldUserName = "username";
const char* const kFieldUserStatus = "userstatus";
const char* const kFieldAwayStatus = "awaystatus";
const char* const kFieldServerAddress = "server";
const char* const kFieldServerPort = "serverport";
const char* const kFieldInstallId = "installid";
const char* const kFieldConnectOnStartup = "connectonstartup";
const char* const kFieldNotificationsEnabled = "notifications";
const char* const kFieldDownloadDirectory = "downloaddir";
const char* const kFieldRetainFilePaths = "retainfilepaths";
const char* const kFieldMaxDownloads = "maxdownloads";
const char* const kFieldMaxUploads = "maxuploads";
const char* const kFieldMaxDownloadRate = "maxdownloadrate";
const char* const kFieldMaxUploadRate = "maxuploadrate";
const char* const kFieldFirewalled = "firewalled";
const char* const kFieldAutoClear = "autoclear";
const char* const kFieldChatFontPointSize = "chatfontsize";
const char* const kFieldChatLogging = "chatlogging";
const char* const kFieldLogDirectory = "logdir";
const char* const kFieldIgnorePattern = "ignorepattern";
const char* const kFieldWatchPattern = "watchpattern";
const char* const kFieldAutoPrivPattern = "autoprivpattern";
const char* const kFieldAliasName = "aliasname";
const char* const kFieldAliasValue = "aliasvalue";

// Chosen to be neighbourly rather than fast. Three at a time saturates a home
// link perfectly well, and a peer serving one file at a time is the norm on
// this network.
const uint32 kDefaultMaxDownloads = 3;
const uint32 kDefaultMaxUploads = 4;

// A cap on the caps. A value of zero means "no limit" everywhere, so these only
// bound what a stored number can be.
const uint32 kMaximumSessionLimit = 32;
const char* const kFieldShareDirectory = "sharedir";
const char* const kFieldFileSharingEnabled = "sharingenabled";

// Named to match HiShare's own settings key, so the two files stay legible to
// anyone reading both.
const char* const kFieldServerList = "serverlist";

// Keep the list short enough to stay a menu rather than a haystack.
const uint32 kMaximumRememberedServers = 16;

const char* const kDefaultUserName = "binky";
const char* const kDefaultUserStatus = "here";
const char* const kDefaultAwayStatus = "away";

// The long-running public server the BeShare community has used for years.  It is a
// starting point, not a hard-coded dependency -- the user can point anywhere.
const char* const kDefaultServerAddress = "beshare.tycomsystems.com";

// The servers HiShare ships with: Minox's and Alexander G. M. Smith's.  Both have
// been up for years, and the second is run by a Haiku developer who is usually on it.
const char* const kDefaultServerList[] = {
	"beshare.tycomsystems.com",
	"beshare.agmsmith.ca"
};

// Sanity cap for the settings file.  It normally holds a few hundred bytes; the
// only way past this is corruption, and refusing is better than allocating on a
// bad length header.
const uint32 kMaximumSettingsFileSize = 4 * 1024 * 1024;


/** Returns the value of an environment variable, or an empty String if it is unset
  * or empty.
  * @param variableName the variable to read
  */
String
GetEnvironmentVariable(const char* variableName)
{
	const char* value = getenv(variableName);
	return (value != NULL && value[0] != '\0') ? String(value) : String();
}


/** Creates a directory and any missing parents, like "mkdir -p".
  * @param directoryPath the directory to create
  */
status_t
CreateDirectoryTree(const String& directoryPath)
{
	if (directoryPath.IsEmpty())
		return B_BAD_ARGUMENT;

	String pathSoFar;
	StringTokenizer tokenizer(directoryPath(), "/");
	const char* component = NULL;
	while ((component = tokenizer.GetNextToken()) != NULL) {
		pathSoFar += "/";
		pathSoFar += component;

		// EEXIST is the expected outcome for every component but the last.
		if (mkdir(pathSoFar(), 0700) != 0 && errno != EEXIST)
			return B_ERRNO;
	}

	return B_NO_ERROR;
}


}  // unnamed namespace


ApplicationSettings::ApplicationSettings()
{
}


ApplicationSettings::~ApplicationSettings()
{
}


status_t
ApplicationSettings::Load()
{
	const String settingsPath = GetSettingsFilePath();
	if (settingsPath.IsEmpty())
		return B_BAD_OBJECT;

	// Open the file ourselves rather than letting FileDataIO(path, mode) do it: that
	// constructor defers the fopen() until the first read, so there would be no point
	// at which we could tell whether the file opened.
	FILE* file = fopen(settingsPath(), "rb");
	if (file == NULL)
		return B_FILE_NOT_FOUND;

	FileDataIO settingsFile(file);

	// -1 means "read the four-byte length header first", which pairs with the
	// addSizeHeader=true that Save() writes.  The cap stops a truncated or corrupt
	// settings file from talking us into an enormous allocation.
	return fSettings.UnflattenFromDataIO(settingsFile, -1, kMaximumSettingsFileSize);
}


status_t
ApplicationSettings::Save() const
{
	const status_t directoryResult = CreateDirectoryTree(GetConfigDirectoryPath());
	if (directoryResult.IsError())
		return directoryResult;

	const String settingsPath = GetSettingsFilePath();
	if (settingsPath.IsEmpty())
		return B_BAD_OBJECT;

	FILE* file = fopen(settingsPath(), "wb");
	if (file == NULL)
		return B_ERRNO;

	FileDataIO settingsFile(file);
	return fSettings.FlattenToDataIO(settingsFile, true);
}


String
ApplicationSettings::GetUserName() const
{
	return _GetString(kFieldUserName, kDefaultUserName);
}


void
ApplicationSettings::SetUserName(const String& userName)
{
	_SetString(kFieldUserName, userName);
}


String
ApplicationSettings::GetUserStatus() const
{
	return _GetString(kFieldUserStatus, kDefaultUserStatus);
}


void
ApplicationSettings::SetUserStatus(const String& userStatus)
{
	_SetString(kFieldUserStatus, userStatus);
}


String
ApplicationSettings::GetAwayStatus() const
{
	return _GetString(kFieldAwayStatus, kDefaultAwayStatus);
}


void
ApplicationSettings::SetAwayStatus(const String& awayStatus)
{
	_SetString(kFieldAwayStatus, awayStatus);
}


String
ApplicationSettings::GetServerAddress() const
{
	return _GetString(kFieldServerAddress, kDefaultServerAddress);
}


void
ApplicationSettings::SetServerAddress(const String& serverAddress)
{
	_SetString(kFieldServerAddress, serverAddress);
}


Queue<String>
ApplicationSettings::GetServerList() const
{
	Queue<String> serverList;

	String serverAddress;
	for (int32 i = 0; fSettings.FindString(kFieldServerList, i, serverAddress).IsOK();
			i++) {
		if (serverAddress.HasChars())
			(void) serverList.AddTail(serverAddress);
	}

	if (serverList.IsEmpty()) {
		for (uint32 i = 0; i < ARRAYITEMS(kDefaultServerList); i++)
			(void) serverList.AddTail(kDefaultServerList[i]);
	}

	return serverList;
}


void
ApplicationSettings::SetServerList(const Queue<String>& serverList)
{
	(void) fSettings.RemoveName(kFieldServerList);

	for (uint32 i = 0; i < serverList.GetNumItems(); i++)
		(void) fSettings.AddString(kFieldServerList, serverList[i]);
}


void
ApplicationSettings::RememberServer(const String& serverAddress)
{
	if (serverAddress.IsEmpty())
		return;

	Queue<String> serverList = GetServerList();

	// Drop any existing entry first so promoting an already-known server moves it
	// rather than duplicating it.  Case-insensitive, because host names are.
	for (int32 i = (int32) serverList.GetNumItems() - 1; i >= 0; i--) {
		if (serverList[i].EqualsIgnoreCase(serverAddress))
			(void) serverList.RemoveItemAt((uint32) i);
	}

	(void) serverList.AddHead(serverAddress);

	while (serverList.GetNumItems() > kMaximumRememberedServers)
		(void) serverList.RemoveTail();

	SetServerList(serverList);
}


uint16
ApplicationSettings::GetServerPort() const
{
	int32 storedPort = 0;
	if (fSettings.FindInt32(kFieldServerPort, storedPort).IsError())
		return kDefaultServerPort;

	if (storedPort <= 0 || storedPort > 65535)
		return kDefaultServerPort;

	return (uint16) storedPort;
}


void
ApplicationSettings::SetServerPort(uint16 serverPort)
{
	(void) fSettings.ReplaceInt32(true, kFieldServerPort, (int32) serverPort);
}


String
ApplicationSettings::GetDownloadDirectory() const
{
	return _GetString(kFieldDownloadDirectory, GetDefaultDownloadDirectoryPath());
}


void
ApplicationSettings::SetDownloadDirectory(const String& downloadDirectory)
{
	_SetString(kFieldDownloadDirectory, downloadDirectory);
}


String
ApplicationSettings::GetShareDirectory() const
{
	return _GetString(kFieldShareDirectory, GetDefaultShareDirectoryPath());
}


void
ApplicationSettings::SetShareDirectory(const String& shareDirectory)
{
	_SetString(kFieldShareDirectory, shareDirectory);
}


bool
ApplicationSettings::GetFileSharingEnabled() const
{
	bool fileSharingEnabled = false;
	(void) fSettings.FindBool(kFieldFileSharingEnabled, fileSharingEnabled);
	return fileSharingEnabled;
}


void
ApplicationSettings::SetFileSharingEnabled(bool fileSharingEnabled)
{
	(void) fSettings.ReplaceBool(true, kFieldFileSharingEnabled,
		fileSharingEnabled);
}


uint32
ApplicationSettings::GetMaxSimultaneousDownloads() const
{
	int32 stored = 0;
	if (fSettings.FindInt32(kFieldMaxDownloads, stored).IsError() || stored <= 0)
		return kDefaultMaxDownloads;

	return muscleMin((uint32) stored, kMaximumSessionLimit);
}


void
ApplicationSettings::SetMaxSimultaneousDownloads(uint32 maxSimultaneousDownloads)
{
	(void) fSettings.ReplaceInt32(true, kFieldMaxDownloads,
		(int32) muscleClamp(maxSimultaneousDownloads, (uint32) 1,
			kMaximumSessionLimit));
}


uint32
ApplicationSettings::GetMaxSimultaneousUploads() const
{
	int32 stored = 0;
	if (fSettings.FindInt32(kFieldMaxUploads, stored).IsError() || stored <= 0)
		return kDefaultMaxUploads;

	return muscleMin((uint32) stored, kMaximumSessionLimit);
}


void
ApplicationSettings::SetMaxSimultaneousUploads(uint32 maxSimultaneousUploads)
{
	(void) fSettings.ReplaceInt32(true, kFieldMaxUploads,
		(int32) muscleClamp(maxSimultaneousUploads, (uint32) 1,
			kMaximumSessionLimit));
}


uint32
ApplicationSettings::GetMaxDownloadRate() const
{
	int32 stored = 0;
	if (fSettings.FindInt32(kFieldMaxDownloadRate, stored).IsError() || stored < 0)
		return 0;

	return (uint32) stored;
}


void
ApplicationSettings::SetMaxDownloadRate(uint32 maxDownloadRate)
{
	(void) fSettings.ReplaceInt32(true, kFieldMaxDownloadRate,
		(int32) maxDownloadRate);
}


uint32
ApplicationSettings::GetMaxUploadRate() const
{
	int32 stored = 0;
	if (fSettings.FindInt32(kFieldMaxUploadRate, stored).IsError() || stored < 0)
		return 0;

	return (uint32) stored;
}


void
ApplicationSettings::SetMaxUploadRate(uint32 maxUploadRate)
{
	(void) fSettings.ReplaceInt32(true, kFieldMaxUploadRate,
		(int32) maxUploadRate);
}


bool
ApplicationSettings::GetFirewalled() const
{
	bool firewalled = false;
	(void) fSettings.FindBool(kFieldFirewalled, firewalled);
	return firewalled;
}


void
ApplicationSettings::SetFirewalled(bool firewalled)
{
	(void) fSettings.ReplaceBool(true, kFieldFirewalled, firewalled);
}


bool
ApplicationSettings::GetAutoClearFinishedTransfers() const
{
	bool autoClear = false;
	(void) fSettings.FindBool(kFieldAutoClear, autoClear);
	return autoClear;
}


void
ApplicationSettings::SetAutoClearFinishedTransfers(bool autoClearFinishedTransfers)
{
	(void) fSettings.ReplaceBool(true, kFieldAutoClear, autoClearFinishedTransfers);
}


uint32
ApplicationSettings::GetChatFontPointSize() const
{
	int32 stored = 0;
	if (fSettings.FindInt32(kFieldChatFontPointSize, stored).IsError()
			|| stored <= 0) {
		return 0;
	}

	return muscleClamp((uint32) stored, (uint32) 6, (uint32) 32);
}


void
ApplicationSettings::SetChatFontPointSize(uint32 chatFontPointSize)
{
	(void) fSettings.ReplaceInt32(true, kFieldChatFontPointSize,
		(int32) chatFontPointSize);
}


bool
ApplicationSettings::GetChatLoggingEnabled() const
{
	bool chatLoggingEnabled = false;
	(void) fSettings.FindBool(kFieldChatLogging, chatLoggingEnabled);
	return chatLoggingEnabled;
}


void
ApplicationSettings::SetChatLoggingEnabled(bool chatLoggingEnabled)
{
	(void) fSettings.ReplaceBool(true, kFieldChatLogging, chatLoggingEnabled);
}


String
ApplicationSettings::GetLogDirectory() const
{
	return _GetString(kFieldLogDirectory, GetDefaultLogDirectoryPath());
}


void
ApplicationSettings::SetLogDirectory(const String& logDirectory)
{
	_SetString(kFieldLogDirectory, logDirectory);
}


String
ApplicationSettings::GetDefaultLogDirectoryPath()
{
	// XDG_STATE_HOME is the right home for logs: they are neither
	// configuration nor cached data, and they should survive a cache clear.
	String stateHome = GetEnvironmentVariable("XDG_STATE_HOME");
	if (stateHome.IsEmpty()) {
		const String homeDirectory = GetEnvironmentVariable("HOME");
		if (homeDirectory.IsEmpty())
			return String();

		stateHome = homeDirectory + "/.local/state";
	}

	return stateHome + "/hituxshare/logs";
}


String
ApplicationSettings::GetIgnorePattern() const
{
	return _GetString(kFieldIgnorePattern, String());
}


void
ApplicationSettings::SetIgnorePattern(const String& pattern)
{
	_SetString(kFieldIgnorePattern, pattern);
}


String
ApplicationSettings::GetWatchPattern() const
{
	return _GetString(kFieldWatchPattern, String());
}


void
ApplicationSettings::SetWatchPattern(const String& pattern)
{
	_SetString(kFieldWatchPattern, pattern);
}


String
ApplicationSettings::GetAutoPrivPattern() const
{
	return _GetString(kFieldAutoPrivPattern, String());
}


void
ApplicationSettings::SetAutoPrivPattern(const String& pattern)
{
	_SetString(kFieldAutoPrivPattern, pattern);
}


Hashtable<String, String>
ApplicationSettings::GetAliases() const
{
	Hashtable<String, String> aliases;

	String name;
	String value;
	for (int32 i = 0; fSettings.FindString(kFieldAliasName, i, name).IsOK(); i++) {
		// Parallel arrays, so a missing value means the pair is broken and the
		// alias is skipped rather than defined as empty.
		if (fSettings.FindString(kFieldAliasValue, i, value).IsOK())
			(void) aliases.Put(name, value);
	}

	return aliases;
}


void
ApplicationSettings::SetAliases(const Hashtable<String, String>& aliases)
{
	(void) fSettings.RemoveName(kFieldAliasName);
	(void) fSettings.RemoveName(kFieldAliasValue);

	for (auto iterator = aliases.GetIterator(); iterator.HasData(); iterator++) {
		(void) fSettings.AddString(kFieldAliasName, iterator.GetKey());
		(void) fSettings.AddString(kFieldAliasValue, iterator.GetValue());
	}
}


bool
ApplicationSettings::GetRetainFilePaths() const
{
	bool retainFilePaths = false;
	(void) fSettings.FindBool(kFieldRetainFilePaths, retainFilePaths);
	return retainFilePaths;
}


void
ApplicationSettings::SetRetainFilePaths(bool retainFilePaths)
{
	(void) fSettings.ReplaceBool(true, kFieldRetainFilePaths, retainFilePaths);
}


bool
ApplicationSettings::GetNotificationsEnabled() const
{
	bool notificationsEnabled = true;
	(void) fSettings.FindBool(kFieldNotificationsEnabled, notificationsEnabled);
	return notificationsEnabled;
}


void
ApplicationSettings::SetNotificationsEnabled(bool notificationsEnabled)
{
	(void) fSettings.ReplaceBool(true, kFieldNotificationsEnabled,
		notificationsEnabled);
}


bool
ApplicationSettings::GetConnectOnStartup() const
{
	bool connectOnStartup = false;
	(void) fSettings.FindBool(kFieldConnectOnStartup, connectOnStartup);
	return connectOnStartup;
}


void
ApplicationSettings::SetConnectOnStartup(bool connectOnStartup)
{
	(void) fSettings.ReplaceBool(true, kFieldConnectOnStartup, connectOnStartup);
}


uint64
ApplicationSettings::GetInstallId()
{
	int64 storedInstallId = 0;
	if (fSettings.FindInt64(kFieldInstallId, storedInstallId).IsOK()
			&& storedInstallId != 0) {
		return (uint64) storedInstallId;
	}

	// First run.  This only has to be stable and unlikely to collide -- it is a
	// bookkeeping handle for upload bans, not a secret, so a cheap PRNG is right.
	const uint64 newInstallId = GetInsecurePseudoRandomNumber64();
	(void) fSettings.ReplaceInt64(true, kFieldInstallId, (int64) newInstallId);
	return newInstallId;
}


String
ApplicationSettings::GetConfigDirectoryPath()
{
	String configHome = GetEnvironmentVariable("XDG_CONFIG_HOME");
	if (configHome.IsEmpty()) {
		const String homeDirectory = GetEnvironmentVariable("HOME");
		if (homeDirectory.IsEmpty())
			return String();

		configHome = homeDirectory + "/.config";
	}

	return configHome + "/hituxshare";
}


String
ApplicationSettings::GetSettingsFilePath()
{
	const String configDirectory = GetConfigDirectoryPath();
	if (configDirectory.IsEmpty())
		return String();

	return configDirectory + "/settings.msg";
}


String
ApplicationSettings::GetDefaultDownloadDirectoryPath()
{
	const String homeDirectory = GetEnvironmentVariable("HOME");
	if (homeDirectory.IsEmpty())
		return String();

	return homeDirectory + "/Downloads/HiTuxShare";
}


String
ApplicationSettings::GetDefaultShareDirectoryPath()
{
	const String homeDirectory = GetEnvironmentVariable("HOME");
	if (homeDirectory.IsEmpty())
		return String();

	return homeDirectory + "/HiTuxShare/shared";
}


String
ApplicationSettings::_GetString(const char* fieldName, const String& defaultValue) const
{
	String value;
	if (fSettings.FindString(fieldName, value).IsError() || value.IsEmpty())
		return defaultValue;

	return value;
}


void
ApplicationSettings::_SetString(const char* fieldName, const String& value)
{
	(void) fSettings.ReplaceString(true, fieldName, value);
}


}  // namespace hitux

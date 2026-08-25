/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef APPLICATION_SETTINGS_H
#define APPLICATION_SETTINGS_H


#include "message/Message.h"
#include "util/Queue.h"
#include "util/String.h"


namespace hitux {


/** Persistent settings, stored as a flattened muscle::Message.
  *
  * Using a Message rather than an INI file or QSettings keeps the core free of Qt and
  * costs nothing -- Message already flattens portably, and a front-end can stash its
  * own opaque state (window geometry, splitter positions) in the same file through
  * GetRawMessage() without the core needing to know what any of it means.
  *
  * The file lives at $XDG_CONFIG_HOME/hituxshare/settings.msg, falling back to
  * ~/.config/hituxshare/settings.msg.
  */
class ApplicationSettings
{
public:
	ApplicationSettings();
	~ApplicationSettings();

	/** Reads the settings file if it exists.
	  * @returns B_NO_ERROR if settings were loaded, an error if the file is missing
	  *          or unreadable.  A missing file is not a failure worth reporting to
	  *          the user -- it just means first run.
	  */
	muscle::status_t Load();

	/** Writes the settings file, creating the directory if needed. */
	muscle::status_t Save() const;

	muscle::String GetUserName() const;
	void SetUserName(const muscle::String& userName);

	muscle::String GetUserStatus() const;
	void SetUserStatus(const muscle::String& userStatus);

	muscle::String GetAwayStatus() const;
	void SetAwayStatus(const muscle::String& awayStatus);

	muscle::String GetServerAddress() const;
	void SetServerAddress(const muscle::String& serverAddress);

	/** Returns the remembered server list, most recently used first.
	  *
	  * Never empty: if nothing has been stored yet it returns the two long-running
	  * public servers, which is what BeShare and HiShare have always shipped with.
	  */
	muscle::Queue<muscle::String> GetServerList() const;
	void SetServerList(const muscle::Queue<muscle::String>& serverList);

	/** Moves a server to the front of the remembered list, adding it if new.
	  *
	  * Called after a successful connection, so the list orders itself by what the
	  * user actually uses instead of by whatever order they were typed in.
	  *
	  * @param serverAddress the server to promote
	  */
	void RememberServer(const muscle::String& serverAddress);

	uint16 GetServerPort() const;
	void SetServerPort(uint16 serverPort);

	/** Where completed downloads are written.
	  *
	  * Defaults to ~/Downloads/HiTuxShare rather than the bare Downloads folder:
	  * files arrive here named by strangers, and keeping them in their own place
	  * makes that visible.
	  */
	muscle::String GetDownloadDirectory() const;
	void SetDownloadDirectory(const muscle::String& downloadDirectory);

	/** Whether a download recreates the sharer's directory structure.
	  *
	  * Off by default. A peer's path can be a dozen levels deep -- the first
	  * real download during development landed under
	  * "BeShare Mirrors/H-kon/Genki CD/beos/etc/install/..." -- and burying a
	  * file the user asked for is a worse default than putting it where they
	  * can see it. BeShare has the same switch.
	  */
	/** The folder whose contents we offer to other people.
	  *
	  * Defaults to ~/HiTuxShare/shared, deliberately not any existing folder:
	  * sharing is opt-in and it should be obvious which files are exposed.
	  */
	muscle::String GetShareDirectory() const;
	void SetShareDirectory(const muscle::String& shareDirectory);

	/** Whether to publish the share folder at all. Off until asked. */
	bool GetFileSharingEnabled() const;
	void SetFileSharingEnabled(bool fileSharingEnabled);

	bool GetRetainFilePaths() const;
	void SetRetainFilePaths(bool retainFilePaths);

	/** Whether to raise desktop notifications for private messages, mentions of
	  * your name, and finished downloads. On by default; only ever fires when
	  * the window is not the active one.
	  */
	bool GetNotificationsEnabled() const;
	void SetNotificationsEnabled(bool notificationsEnabled);

	bool GetConnectOnStartup() const;
	void SetConnectOnStartup(bool connectOnStartup);

	/** Returns this installation's stable ID, generating and storing one the first
	  * time it is asked for.
	  *
	  * Peers use it for upload bans and queue fairness.  It identifies an install,
	  * not a person, and it is not authenticated in any way -- any client can claim
	  * any value, so never treat it as proof of identity.
	  */
	uint64 GetInstallId();

	/** Direct access for front-end state the core does not interpret. */
	muscle::Message& GetRawMessage() { return fSettings; }
	const muscle::Message& GetRawMessage() const { return fSettings; }

	static muscle::String GetConfigDirectoryPath();
	static muscle::String GetSettingsFilePath();
	static muscle::String GetDefaultDownloadDirectoryPath();
	static muscle::String GetDefaultShareDirectoryPath();

private:
	muscle::String _GetString(const char* fieldName,
		const muscle::String& defaultValue) const;
	void _SetString(const char* fieldName, const muscle::String& value);

	muscle::Message fSettings;
};


}  // namespace hitux


#endif  // APPLICATION_SETTINGS_H

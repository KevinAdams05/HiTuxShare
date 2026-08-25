/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "core/MimeTypeGuesser.h"

#include <stdio.h>

using namespace muscle;


namespace hitux {


namespace {


const char* const kSystemGlobsPath = "/usr/share/mime/globs";

// Enough to be useful when shared-mime-info is not installed. Not a substitute
// for it -- just the types most likely to turn up in a share folder.
const char* const kBuiltInTypes[] = {
	"txt", "text/plain",          "html", "text/html",
	"css", "text/css",            "csv", "text/csv",
	"xml", "application/xml",     "json", "application/json",
	"pdf", "application/pdf",     "zip", "application/zip",
	"gz", "application/gzip",     "bz2", "application/x-bzip2",
	"xz", "application/x-xz",     "tar", "application/x-tar",
	"7z", "application/x-7z-compressed", "rar", "application/vnd.rar",
	"png", "image/png",           "jpg", "image/jpeg",
	"jpeg", "image/jpeg",         "gif", "image/gif",
	"bmp", "image/bmp",           "svg", "image/svg+xml",
	"webp", "image/webp",         "ico", "image/vnd.microsoft.icon",
	"mp3", "audio/mpeg",          "ogg", "audio/ogg",
	"flac", "audio/flac",         "wav", "audio/x-wav",
	"m4a", "audio/mp4",           "opus", "audio/opus",
	"mp4", "video/mp4",           "mkv", "video/x-matroska",
	"avi", "video/x-msvideo",     "webm", "video/webm",
	"mov", "video/quicktime",     "iso", "application/vnd.efi.iso",
	"c", "text/x-csrc",           "h", "text/x-chdr",
	"cpp", "text/x-c++src",       "hpp", "text/x-c++hdr",
	"py", "text/x-python",        "sh", "application/x-shellscript",
	"md", "text/markdown",        "hpkg", "application/x-vnd.haiku-package"
};


}  // unnamed namespace


MimeTypeGuesser::MimeTypeGuesser()
	:
	fIsLoaded(false)
{
}


MimeTypeGuesser::~MimeTypeGuesser()
{
}


String
MimeTypeGuesser::GuessMimeType(const String& fileName)
{
	const int32 dotIndex = fileName.LastIndexOf('.');
	if (dotIndex < 0 || dotIndex == (int32) fileName.Length() - 1)
		return String();

	_EnsureLoaded();

	const String extension = fileName.Substring(dotIndex + 1).ToLowerCase();
	const String* found = fTypesByExtension.Get(extension);
	return (found != NULL) ? *found : String();
}


void
MimeTypeGuesser::_EnsureLoaded()
{
	if (fIsLoaded)
		return;

	fIsLoaded = true;

	// Built-ins first so the system database, being the better source, wins on
	// any extension the two disagree about.
	_LoadBuiltInTypes();
	(void) _LoadSystemDatabase();
}


void
MimeTypeGuesser::_LoadBuiltInTypes()
{
	for (uint32 i = 0; i + 1 < ARRAYITEMS(kBuiltInTypes); i += 2)
		(void) fTypesByExtension.Put(kBuiltInTypes[i], kBuiltInTypes[i + 1]);
}


bool
MimeTypeGuesser::_LoadSystemDatabase()
{
	FILE* file = fopen(kSystemGlobsPath, "r");
	if (file == NULL)
		return false;

	// Lines are "type:glob". Only plain "*.ext" globs are taken: the handful of
	// entries using character classes or matching whole file names would need a
	// real matcher for very little benefit.
	char line[512];
	while (fgets(line, sizeof(line), file) != NULL) {
		if (line[0] == '#')
			continue;

		String entry(line);
		entry = entry.Trimmed();

		const int32 colonIndex = entry.IndexOf(':');
		if (colonIndex <= 0)
			continue;

		const String mimeType = entry.Substring(0, colonIndex);
		const String glob = entry.Substring(colonIndex + 1);
		if (glob.StartsWith("*.") == false || glob.Length() < 3)
			continue;

		const String extension = glob.Substring(2).ToLowerCase();
		if (extension.IndexOf('*') >= 0 || extension.IndexOf('[') >= 0
				|| extension.IndexOf('?') >= 0) {
			continue;
		}

		(void) fTypesByExtension.Put(extension, mimeType);
	}

	fclose(file);
	return true;
}


}  // namespace hitux

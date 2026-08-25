/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef MIME_TYPE_GUESSER_H
#define MIME_TYPE_GUESSER_H


#include "util/Hashtable.h"
#include "util/String.h"


namespace hitux {


/** Works out a file's MIME type from its name.
  *
  * Peers publish a "beshare:Kind" alongside each shared file, and on Haiku that
  * comes from the filesystem's own type attribute. Linux has no such attribute,
  * so we derive it from the extension.
  *
  * The table is read from the freedesktop shared-mime-info glob database at
  * /usr/share/mime/globs, which is a plain text file present on essentially
  * every Linux desktop. That avoids a build dependency on libmagic or a Qt mime
  * database, keeps the core toolkit-free, and is more accurate than anything
  * hand-maintained -- but it is only a lookup, so a small built-in table covers
  * the case where the file is missing.
  *
  * Content sniffing is deliberately not attempted. "beshare:Kind" is a display
  * hint that peers show in a column; reading the head of every shared file to
  * refine it would cost far more than it is worth.
  */
class MimeTypeGuesser
{
public:
	MimeTypeGuesser();
	~MimeTypeGuesser();

	/** Returns the MIME type for a file name, or an empty String if unknown.
	  * @param fileName the name to classify; only its extension is examined
	  */
	muscle::String GuessMimeType(const muscle::String& fileName);

private:
	void _EnsureLoaded();
	void _LoadBuiltInTypes();
	bool _LoadSystemDatabase();

	muscle::Hashtable<muscle::String, muscle::String> fTypesByExtension;
	bool fIsLoaded;
};


}  // namespace hitux


#endif  // MIME_TYPE_GUESSER_H

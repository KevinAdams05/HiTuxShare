/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef QT_CONVERSIONS_H
#define QT_CONVERSIONS_H


#include "util/String.h"

#include <QString>


namespace hitux {


/** Converts a muscle::String to a QString, treating the bytes as UTF-8.
  *
  * BeShare declares no encoding on the wire -- a client sends whatever bytes it
  * happened to hold.  UTF-8 is correct for every modern client and for Haiku, which
  * is UTF-8 throughout, and fromUtf8() substitutes replacement characters rather
  * than failing if some ancient peer sends something else.
  *
  * @param text the string to convert
  */
inline QString
ToQString(const muscle::String& text)
{
	return QString::fromUtf8(text(), (int) text.Length());
}


/** Converts a QString to a muscle::String as UTF-8.
  * @param text the string to convert
  */
inline muscle::String
ToMuscleString(const QString& text)
{
	const QByteArray utf8Bytes = text.toUtf8();
	return muscle::String(utf8Bytes.constData());
}


}  // namespace hitux


#endif  // QT_CONVERSIONS_H

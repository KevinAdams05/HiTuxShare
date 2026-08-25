/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef FORMAT_UTILITIES_H
#define FORMAT_UTILITIES_H


#include "util/String.h"


namespace hitux {


/** Formats a byte count for display, e.g. 1536 becomes "1.5 KB".
  *
  * Uses binary multiples (1024) with the familiar KB/MB/GB labels, which is
  * what BeShare and every other client on this network has always shown --
  * being pedantically correct with KiB here would just make our sizes look
  * different from everyone else's for the same file.
  *
  * @param byteCount the size to format; negative values render as "?"
  */
muscle::String FormatByteSize(int64 byteCount);


/** Formats a unix timestamp as local date and time, e.g. "2026-08-24 19:41".
  * @param unixTime seconds since the epoch, as the wire carries it
  */
muscle::String FormatTimestamp(int32 unixTime);


/** Formats a transfer rate in bytes per second, e.g. "1.2 MB/s".
  * @param bytesPerSecond the rate to format
  */
muscle::String FormatTransferRate(int64 bytesPerSecond);


}  // namespace hitux


#endif  // FORMAT_UTILITIES_H

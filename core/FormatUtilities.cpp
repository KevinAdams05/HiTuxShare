/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "core/FormatUtilities.h"

#include <stdio.h>
#include <time.h>

using namespace muscle;


namespace hitux {


namespace {


const char* const kUnitLabels[] = {"bytes", "KB", "MB", "GB", "TB", "PB"};
const uint32 kUnitCount = ARRAYITEMS(kUnitLabels);


}  // unnamed namespace


String
FormatByteSize(int64 byteCount)
{
	if (byteCount < 0)
		return String("?");

	if (byteCount < 1024)
		return String(muscle::String("%1 bytes").Arg(byteCount));

	double scaled = (double) byteCount;
	uint32 unitIndex = 0;
	while (scaled >= 1024.0 && unitIndex + 1 < kUnitCount) {
		scaled /= 1024.0;
		unitIndex++;
	}

	// One decimal place below 10 and none above it: "9.8 MB" is useful
	// precision, "983.2 MB" is noise.
	char buffer[64];
	snprintf(buffer, sizeof(buffer), (scaled < 10.0) ? "%.1f %s" : "%.0f %s",
		scaled, kUnitLabels[unitIndex]);

	return String(buffer);
}


String
FormatTimestamp(int32 unixTime)
{
	if (unixTime <= 0)
		return String();

	const time_t when = (time_t) unixTime;
	struct tm brokenDown;
	if (localtime_r(&when, &brokenDown) == NULL)
		return String();

	char buffer[64];
	if (strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &brokenDown) == 0)
		return String();

	return String(buffer);
}


String
FormatTransferRate(int64 bytesPerSecond)
{
	if (bytesPerSecond <= 0)
		return String();

	return FormatByteSize(bytesPerSecond) + "/s";
}


}  // namespace hitux

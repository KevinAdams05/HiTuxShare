/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "tests/style/good.h"

#include "util/Queue.h"

#include <stdio.h>


namespace hitux {


// Regression guard for the comment-stripping fix.  Every line below would trip a
// rule if the checker looked at comment prose: a long-running task, the
// "beshare/name" node, the "version_num" field, a nullptr, and TRUE.  None of
// them may be reported, because they are all just words in a comment.
/*
 * The same again in a block comment, spanning lines, mentioning
 * "SUBSCRIBE:beshare/*" and nullptr and TRUE and a long value.
 */


/** Writing to a file handle is not a stray debug print, and must not be
  * reported. This line is the regression guard for that.
  */
void
WriteReport(FILE* reportFile, const GoodRecord& record)
{
	fprintf(reportFile, "%s %u\n", record.name(), record.itemCount);
}


/** Counts the ready records in (records).
  * @param records the records to examine
  */
uint32
CountReadyRecords(const muscle::Queue<GoodRecord>& records)
{
	uint32 readyCount = 0;

	for (uint32 i = 0; i < records.GetNumItems(); i++) {
		const GoodRecord& record = records[i];
		if (record.isReady && record.name.HasChars())
			readyCount++;
	}

	return readyCount;
}


/** Returns the first ready record, or NULL when there is none.
  * @param records the records to search
  */
const GoodRecord*
FindFirstReadyRecord(const muscle::Queue<GoodRecord>& records)
{
	for (uint32 i = 0; i < records.GetNumItems(); i++) {
		if (records[i].isReady)
			return &records[i];
	}

	return NULL;
}


}  // namespace hitux

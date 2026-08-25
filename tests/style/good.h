/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef GOOD_H
#define GOOD_H


#include "util/String.h"


namespace hitux {


/** A fixture that must produce no findings at all.
  *
  * If a rule ever fires here it means that rule has a false positive, which is
  * worth more attention than a missed finding: a checker people learn to ignore
  * is worse than no checker.
  */
struct GoodRecord
{
	GoodRecord()
		:
		itemCount(0),
		isReady(false)
	{
	}

	muscle::String name;
	uint32 itemCount;
	bool isReady;
};


}  // namespace hitux


#endif  // GOOD_H

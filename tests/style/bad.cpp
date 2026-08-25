// Deliberately broken fixture for scripts/style-check.py --self-test.
// Pretends to live in core/, so the core-only rules fire.
// Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
// Distributed under the terms of the MIT License.

#include <QWidget>

#include <stdio.h>

// TODO Kevin: this fixture exists to be reported, not fixed.
void
BadFunction(const char* name)
{
#if 0
	int deadCode = 0;
#endif
	bool flag = TRUE;
	void* pointer = nullptr;

	if (NULL == pointer)
		printf("beshare/name %s\n", name);
	fprintf(stderr, "also flagged\n");
}
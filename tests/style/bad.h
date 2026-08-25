// Deliberately broken fixture for scripts/style-check.py --self-test.
// No copyright header, wrong guard, and one violation per rule below.
#pragma once

struct BadRecord {
	long legacyCount;
	char *pointerOnTheWrongSide;
	int reallyLongFieldNamereallyLongFieldNamereallyLongFieldNamereallyLongFieldNamereallyLongFieldNamereallyLongFieldName;   
   int indentedWithSpaces;
};

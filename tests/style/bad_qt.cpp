// Deliberately broken fixture for scripts/style-check.py --self-test.
// Pretends to live in qt/, so html-unescaped fires.
// Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
// Distributed under the terms of the MIT License.

#include <QString>

QString
RenderBadly(const muscle::String& peerText)
{
	return QStringLiteral("<span style=\"color:red;\">%1</span>").arg(ToQString(peerText));
}

/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "qt/ChatLogView.h"

#include "qt/QtConversions.h"

#include <QEvent>
#include <QScrollBar>
#include <QTime>


namespace hitux {


namespace {


/** Returns true if the palette we are drawing against is a dark one.
  *
  * Qt has no "is dark theme" flag, and checking a stylesheet name or a platform
  * setting only works on some desktops.  Comparing the lightness of the window and
  * text colours works everywhere and follows a live theme change.
  *
  * @param palette the palette to inspect
  */
bool
IsDarkPalette(const QPalette& palette)
{
	return palette.color(QPalette::Window).lightness()
		< palette.color(QPalette::WindowText).lightness();
}


}  // unnamed namespace


ChatLogView::ChatLogView(QWidget* parent)
	:
	QTextBrowser(parent),
	fShowTimestamps(true)
{
	setOpenExternalLinks(true);
	setReadOnly(true);
	setUndoRedoEnabled(false);

	// Chat is a transcript, not a document: a hard cap keeps memory bounded during a
	// long session without the user ever noticing the trim.
	document()->setMaximumBlockCount(5000);
}


ChatLogView::~ChatLogView()
{
}


void
ChatLogView::AppendChatMessage(const ChatMessage& message)
{
	const QString senderName = ToQString(message.senderName).toHtmlEscaped();
	const QString text = ToQString(message.text).toHtmlEscaped();
	const QColor color = _GetColorForMessageType(message.type);

	QString html;

	if (message.isPrivate) {
		html += QStringLiteral("<span style=\"color:%1;\">[%2]</span> ")
			.arg(_GetColorForMessageType(LOG_WARNING_MESSAGE).name(), tr("private"));
	}

	if (message.isAction) {
		html += QStringLiteral("<span style=\"color:%1;\">&#42; %2 %3</span>")
			.arg(color.name(), senderName, text);
	} else if (senderName.isEmpty()) {
		html += QStringLiteral("<span style=\"color:%1;\">%2</span>")
			.arg(color.name(), text);
	} else {
		html += QStringLiteral("<b style=\"color:%1;\">&lt;%2&gt;</b> "
			"<span style=\"color:%3;\">%4</span>")
			.arg(_GetColorForMessageType(message.isFromLocalUser
					? LOG_LOCAL_USER_CHAT_MESSAGE : LOG_REMOTE_USER_CHAT_MESSAGE).name(),
				senderName,
				palette().color(QPalette::Text).name(),
				text);
	}

	_AppendHtmlLine(html);
}


void
ChatLogView::AppendLocalMessage(LogMessageType type, const QString& text)
{
	const QColor color = _GetColorForMessageType(type);
	_AppendHtmlLine(QStringLiteral("<span style=\"color:%1;\">%2</span>")
		.arg(color.name(), text.toHtmlEscaped()));
}


void
ChatLogView::SetShowTimestamps(bool showTimestamps)
{
	fShowTimestamps = showTimestamps;
}


void
ChatLogView::changeEvent(QEvent* event)
{
	QTextBrowser::changeEvent(event);

	// A live theme switch changes what our colours should be, but lines already in
	// the log keep the colours they were written with.  Re-rendering the backlog
	// would mean keeping every ChatMessage around; leaving it is the honest trade,
	// and new lines pick up the new theme immediately.
	if (event->type() == QEvent::PaletteChange)
		viewport()->update();
}


QColor
ChatLogView::_GetColorForMessageType(LogMessageType type) const
{
	const bool isDark = IsDarkPalette(palette());

	switch (type) {
		case LOG_ERROR_MESSAGE:
			return isDark ? QColor(0xff, 0x8a, 0x80) : QColor(0xc5, 0x22, 0x1f);

		case LOG_WARNING_MESSAGE:
			return isDark ? QColor(0xff, 0xd5, 0x4f) : QColor(0xb0, 0x60, 0x00);

		case LOG_USER_EVENT_MESSAGE:
			return isDark ? QColor(0x81, 0xc9, 0x95) : QColor(0x0d, 0x65, 0x2d);

		case LOG_UPLOAD_EVENT_MESSAGE:
			return isDark ? QColor(0x8a, 0xb4, 0xf8) : QColor(0x17, 0x4e, 0xa6);

		case LOG_LOCAL_USER_CHAT_MESSAGE:
			return isDark ? QColor(0x8a, 0xb4, 0xf8) : QColor(0x19, 0x67, 0xd2);

		case LOG_REMOTE_USER_CHAT_MESSAGE:
			return isDark ? QColor(0xc5, 0x92, 0xff) : QColor(0x6a, 0x1b, 0x9a);

		case LOG_INFORMATION_MESSAGE:
		default:
			return palette().color(QPalette::Disabled, QPalette::Text);
	}
}


void
ChatLogView::_AppendHtmlLine(const QString& html)
{
	const bool wasAtBottom = _IsScrolledToBottom();

	QString line;
	if (fShowTimestamps) {
		line += QStringLiteral("<span style=\"color:%1;\">%2</span> ")
			.arg(palette().color(QPalette::Disabled, QPalette::Text).name(),
				_FormatTimestamp());
	}

	line += html;

	append(line);

	if (wasAtBottom)
		verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}


bool
ChatLogView::_IsScrolledToBottom() const
{
	const QScrollBar* scrollBar = verticalScrollBar();

	// A couple of pixels of slack, because a partially-scrolled last line should
	// still count as "following along".
	return scrollBar->value() >= scrollBar->maximum() - 4;
}


QString
ChatLogView::_FormatTimestamp()
{
	return QStringLiteral("[%1]").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")));
}


}  // namespace hitux

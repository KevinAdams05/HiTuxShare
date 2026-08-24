/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef CHAT_LOG_VIEW_H
#define CHAT_LOG_VIEW_H


#include "core/ChatMessage.h"

#include <QColor>
#include <QTextBrowser>


namespace hitux {


/** The scrolling chat log.
  *
  * Uses QTextBrowser for rich text rather than a plain QPlainTextEdit, because chat
  * wants per-message colouring and clickable links.  That makes HTML escaping a
  * security requirement rather than a nicety: every byte of message text originates
  * with a remote peer, and anything unescaped would let them inject markup.
  */
class ChatLogView : public QTextBrowser
{
	Q_OBJECT

public:
	explicit ChatLogView(QWidget* parent = nullptr);
	~ChatLogView() override;

	/** Appends one message, scrolling to it only if the user was already at the
	  * bottom -- scrolling away from what someone is reading is worse than making
	  * them scroll down themselves.
	  * @param message the message to render
	  */
	void AppendChatMessage(const ChatMessage& message);

	/** Appends a locally generated line, e.g. the echo of what we just sent.
	  * @param type how to colour it
	  * @param text the text, which is escaped before display
	  */
	void AppendLocalMessage(LogMessageType type, const QString& text);

	void SetShowTimestamps(bool showTimestamps);
	bool GetShowTimestamps() const { return fShowTimestamps; }

protected:
	void changeEvent(QEvent* event) override;

private:
	QColor _GetColorForMessageType(LogMessageType type) const;
	void _AppendHtmlLine(const QString& html);
	bool _IsScrolledToBottom() const;
	static QString _FormatTimestamp();

	bool fShowTimestamps;
};


}  // namespace hitux


#endif  // CHAT_LOG_VIEW_H

/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "qt/ChatInputLine.h"

#include <QKeyEvent>


namespace hitux {


namespace {


// Appended after a completed nickname when it begins the line, so that "ke<Tab>"
// becomes "kevin: " -- the convention every IRC client follows.
const char* const kLeadingCompletionSuffix = ": ";

const int kMaximumHistoryEntries = 100;


}  // unnamed namespace


ChatInputLine::ChatInputLine(QWidget* parent)
	:
	QLineEdit(parent),
	fHistoryPosition(0),
	fCompletionIndex(0),
	fCompletionPrefixStart(0),
	fIsCompleting(false)
{
	setClearButtonEnabled(false);
}


ChatInputLine::~ChatInputLine()
{
}


void
ChatInputLine::SetCompletionProvider(std::function<QStringList(const QString&)> provider)
{
	fCompletionProvider = std::move(provider);
}


void
ChatInputLine::AcceptCurrentLine()
{
	const QString line = text();
	if (line.isEmpty() == false) {
		// Repeating the previous line should not grow the history, which is what
		// makes walking back through it useful rather than tedious.
		if (fHistory.isEmpty() || fHistory.last() != line) {
			fHistory.append(line);
			while (fHistory.size() > kMaximumHistoryEntries)
				fHistory.removeFirst();
		}
	}

	fHistoryPosition = fHistory.size();
	fPendingLine.clear();
	_ResetCompletionState();
	clear();
}


void
ChatInputLine::keyPressEvent(QKeyEvent* event)
{
	switch (event->key()) {
		case Qt::Key_Up:
			_NavigateHistory(-1);
			return;

		case Qt::Key_Down:
			_NavigateHistory(1);
			return;

		case Qt::Key_Tab:
			_PerformCompletion();
			return;

		default:
			break;
	}

	// Any other keystroke means the user has moved on, so a subsequent Tab starts a
	// fresh completion rather than continuing to cycle the old one.
	_ResetCompletionState();
	QLineEdit::keyPressEvent(event);
}


void
ChatInputLine::_NavigateHistory(int direction)
{
	if (fHistory.isEmpty())
		return;

	// Stash whatever is being typed before walking away from it.
	if (fHistoryPosition == fHistory.size())
		fPendingLine = text();

	const int newPosition = fHistoryPosition + direction;
	if (newPosition < 0 || newPosition > fHistory.size())
		return;

	fHistoryPosition = newPosition;

	setText(fHistoryPosition == fHistory.size()
		? fPendingLine : fHistory.at(fHistoryPosition));
	setCursorPosition(text().length());
}


void
ChatInputLine::_PerformCompletion()
{
	if (fCompletionProvider == nullptr)
		return;

	if (fIsCompleting == false) {
		// Find the word the cursor sits at the end of.
		const QString currentText = text();
		const int cursor = cursorPosition();

		int wordStart = cursor;
		while (wordStart > 0 && currentText.at(wordStart - 1).isSpace() == false)
			wordStart--;

		if (wordStart == cursor)
			return;  // nothing to complete

		fCompletionPrefixStart = wordStart;
		fCompletionPrefix = currentText.mid(wordStart, cursor - wordStart);
		fCompletionMatches = fCompletionProvider(fCompletionPrefix);
		fCompletionIndex = 0;

		if (fCompletionMatches.isEmpty())
			return;

		fIsCompleting = true;
	} else {
		fCompletionIndex = (fCompletionIndex + 1) % fCompletionMatches.size();
	}

	QString completion = fCompletionMatches.at(fCompletionIndex);
	if (fCompletionPrefixStart == 0)
		completion += QString::fromLatin1(kLeadingCompletionSuffix);

	// Replace from the prefix start to the end of whatever the previous cycle wrote,
	// which is everything from there to the cursor.
	QString newText = text();
	newText.replace(fCompletionPrefixStart,
		cursorPosition() - fCompletionPrefixStart, completion);

	// setText() would reset our state through keyPressEvent's usual path, so drive
	// the widget directly and restore the cursor ourselves.
	const int newCursorPosition = fCompletionPrefixStart + completion.length();
	QLineEdit::setText(newText);
	setCursorPosition(newCursorPosition);
}


void
ChatInputLine::_ResetCompletionState()
{
	fIsCompleting = false;
	fCompletionMatches.clear();
	fCompletionIndex = 0;
	fCompletionPrefix.clear();
	fCompletionPrefixStart = 0;
}


}  // namespace hitux

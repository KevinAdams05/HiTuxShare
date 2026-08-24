/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef CHAT_INPUT_LINE_H
#define CHAT_INPUT_LINE_H


#include <QLineEdit>
#include <QStringList>

#include <functional>


namespace hitux {


/** The chat entry field, with input history and nickname tab-completion.
  *
  * Both behaviours are what anyone arriving from IRC or from BeShare expects, and
  * both are cheap.  Completion candidates are fetched through a callback rather than
  * pushed in, so the list is always current without the user list having to remember
  * to notify us every time somebody joins.
  */
class ChatInputLine : public QLineEdit
{
	Q_OBJECT

public:
	explicit ChatInputLine(QWidget* parent = nullptr);
	~ChatInputLine() override;

	/** Sets the function that supplies nickname completion candidates.
	  * @param provider called with the prefix being completed; returns the matches
	  */
	void SetCompletionProvider(std::function<QStringList(const QString&)> provider);

	/** Records a line in the history and clears the field. */
	void AcceptCurrentLine();

protected:
	void keyPressEvent(QKeyEvent* event) override;

private:
	void _NavigateHistory(int direction);
	void _PerformCompletion();
	void _ResetCompletionState();

	std::function<QStringList(const QString&)> fCompletionProvider;

	QStringList fHistory;

	// Where we are while walking the history with the arrow keys.  Equal to the
	// history size when the user is on the (unsubmitted) current line.
	int fHistoryPosition;

	// The partially typed line, stashed when the user starts walking backwards so
	// that walking forward again restores it.
	QString fPendingLine;

	// Completion cycling state, so pressing Tab repeatedly walks the matches instead
	// of re-completing the same one.
	QStringList fCompletionMatches;
	int fCompletionIndex;
	int fCompletionPrefixStart;
	QString fCompletionPrefix;
	bool fIsCompleting;
};


}  // namespace hitux


#endif  // CHAT_INPUT_LINE_H

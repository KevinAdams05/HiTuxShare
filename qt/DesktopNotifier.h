/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef DESKTOP_NOTIFIER_H
#define DESKTOP_NOTIFIER_H


#include <QObject>
#include <QString>


namespace hitux {


/** Sends desktop notifications through org.freedesktop.Notifications.
  *
  * Spoken to over D-Bus directly rather than through QSystemTrayIcon, which
  * would require showing a tray icon we do not otherwise want, and gives no
  * control over replacing an earlier notification.
  *
  * Everything degrades to silence: a session with no notification daemon is
  * perfectly normal, and a chat client that complains about it is worse than one
  * that quietly does without.
  */
class DesktopNotifier : public QObject
{
	Q_OBJECT

public:
	/** What a notification is about.
	  *
	  * Each category replaces its own previous notification instead of stacking.
	  * A busy chat room would otherwise pile up dozens of popups, which is how a
	  * notification becomes something people switch off entirely.
	  */
	enum Category
	{
		CATEGORY_CHAT = 0,
		CATEGORY_TRANSFER,

		CATEGORY_COUNT
	};

	explicit DesktopNotifier(QObject* parent = nullptr);
	~DesktopNotifier() override;

	/** True if a notification service answered on the session bus. */
	bool IsAvailable() const { return fIsAvailable; }

	void SetEnabled(bool enabled) { fIsEnabled = enabled; }
	bool GetEnabled() const { return fIsEnabled; }

	/** Shows a notification, replacing the previous one in the same category.
	  *
	  * @param category which kind of notification this is
	  * @param summary the title line
	  * @param body the detail; treated as untrusted and escaped before sending
	  */
	void Notify(Category category, const QString& summary, const QString& body);

private:
	bool fIsAvailable;
	bool fIsEnabled;

	// Per-category id of the last notification, for replacing rather than
	// stacking. Zero means "no previous notification to replace".
	quint32 fLastIds[CATEGORY_COUNT];
};


}  // namespace hitux


#endif  // DESKTOP_NOTIFIER_H

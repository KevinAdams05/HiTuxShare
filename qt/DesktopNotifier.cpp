/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "qt/DesktopNotifier.h"

#include "core/HiTuxShareVersion.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QVariantMap>


namespace hitux {


namespace {


const char* const kNotificationService = "org.freedesktop.Notifications";
const char* const kNotificationPath = "/org/freedesktop/Notifications";

// How long a notification stays up, in milliseconds. -1 would let the daemon
// decide, which on some desktops means "until dismissed" -- too sticky for a
// chat line.
const int kNotificationTimeout = 6000;


}  // unnamed namespace


DesktopNotifier::DesktopNotifier(QObject* parent)
	:
	QObject(parent),
	fIsAvailable(false),
	fIsEnabled(true)
{
	for (int i = 0; i < CATEGORY_COUNT; i++)
		fLastIds[i] = 0;

	fIsAvailable = QDBusConnection::sessionBus().isConnected()
		&& QDBusConnection::sessionBus().interface()->isServiceRegistered(
			QLatin1String(kNotificationService)).value();
}


DesktopNotifier::~DesktopNotifier()
{
}


void
DesktopNotifier::Notify(Category category, const QString& summary,
	const QString& body)
{
	if (fIsEnabled == false || fIsAvailable == false
			|| category < 0 || category >= CATEGORY_COUNT) {
		return;
	}

	QDBusInterface notifications(QLatin1String(kNotificationService),
		QLatin1String(kNotificationPath), QLatin1String(kNotificationService),
		QDBusConnection::sessionBus());
	if (notifications.isValid() == false)
		return;

	// The body is escaped because a daemon advertising "body-markup" parses it
	// as markup, and every word of it came from a stranger on the network. An
	// unescaped "<" from a chat line would at best swallow the rest of the
	// message and at worst be interpreted.
	const QVariantList arguments = {
		QLatin1String(HITUX_SHARE_NAME),
		fLastIds[category],
		QLatin1String("hituxshare"),
		summary.toHtmlEscaped(),
		body.toHtmlEscaped(),
		QStringList(),
		QVariantMap(),
		kNotificationTimeout
	};

	const QDBusReply<quint32> reply = notifications.callWithArgumentList(
		QDBus::Block, QLatin1String("Notify"), arguments);
	if (reply.isValid())
		fLastIds[category] = reply.value();
}


}  // namespace hitux

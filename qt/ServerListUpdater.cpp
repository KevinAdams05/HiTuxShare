/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "qt/ServerListUpdater.h"

#include "core/HiTuxShareVersion.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>


namespace hitux {


namespace {


// HTTPS first, then the same file over plain HTTP. The upstream servers predate
// ubiquitous TLS, so insisting on HTTPS would simply mean the feature never
// works; trying it first means it is used wherever it is available.
const char* const kServerListUrls[] = {
	"https://beshare.tycomsystems.com/servers.txt",
	"http://beshare.tycomsystems.com/servers.txt"
};

const int kServerListUrlCount = 2;

// A server list is a few hundred bytes. Anything wildly larger is not the file
// we asked for, and is refused rather than parsed.
const qint64 kMaximumBodySize = 256 * 1024;


}  // unnamed namespace


ServerListUpdater::ServerListUpdater(QObject* parent)
	:
	QObject(parent),
	fNetwork(new QNetworkAccessManager(this)),
	fReply(nullptr),
	fAttemptIndex(0)
{
}


ServerListUpdater::~ServerListUpdater()
{
	if (fReply != nullptr)
		fReply->abort();
}


void
ServerListUpdater::Start()
{
	if (IsRunning())
		return;

	fAttemptIndex = 0;
	_Fetch(QUrl(QLatin1String(kServerListUrls[0])));
}


void
ServerListUpdater::_Fetch(const QUrl& url)
{
	QNetworkRequest request(url);
	request.setHeader(QNetworkRequest::UserAgentHeader,
		QStringLiteral("%1/%2").arg(QLatin1String(HITUX_SHARE_NAME),
			QLatin1String(HITUX_SHARE_VERSION_STRING)));
	request.setTransferTimeout(15000);

	// Redirects are followed, but only to HTTPS or HTTP, and never from a
	// secure URL down to an insecure one.
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setMaximumRedirectsAllowed(3);

	fReply = fNetwork->get(request);
	connect(fReply, &QNetworkReply::finished,
		this, &ServerListUpdater::_OnReplyFinished);
}


void
ServerListUpdater::_OnReplyFinished()
{
	QNetworkReply* reply = fReply;
	fReply = nullptr;
	if (reply == nullptr)
		return;

	reply->deleteLater();

	if (reply->error() != QNetworkReply::NoError) {
		fAttemptIndex++;
		if (fAttemptIndex < kServerListUrlCount) {
			// HTTPS did not work; try the same file unencrypted once.
			_Fetch(QUrl(QLatin1String(kServerListUrls[fAttemptIndex])));
			return;
		}

		emit UpdateFailed(reply->errorString());
		return;
	}

	const QByteArray body = reply->read(kMaximumBodySize);
	_ParseBody(body);
}


void
ServerListUpdater::_ParseBody(const QByteArray& body)
{
	QStringList serversToAdd;
	QStringList serversToRemove;

	const QStringList lines = QString::fromUtf8(body).split(QLatin1Char('\n'));
	for (const QString& rawLine : lines) {
		QString line = rawLine.trimmed();

		// Trailing comments are the norm in this file, not the exception:
		// every real entry looks like
		//   beshare_addserver = host.example.com   # Somebody's server
		// and taking the rest of the line as the value yields a "host name"
		// full of spaces that then gets rejected. That failure is silent, and
		// it made the whole feature quietly do nothing.
		const int commentIndex = line.indexOf(QLatin1Char('#'));
		if (commentIndex >= 0)
			line = line.left(commentIndex).trimmed();

		if (line.isEmpty())
			continue;

		const int equalsIndex = line.indexOf(QLatin1Char('='));
		if (equalsIndex <= 0)
			continue;

		QString key = line.left(equalsIndex).trimmed().toLower();
		const QString value = line.mid(equalsIndex + 1).trimmed();
		if (value.isEmpty())
			continue;

		// Keys are published as "beshare_addserver"; the bare "addserver" form
		// is accepted too, since both spellings appear in the wild.
		if (key.startsWith(QLatin1String("beshare_")))
			key = key.mid(8);

		// A host name is all this file may contribute. Anything carrying a
		// slash, a space or a scheme is not one, and is dropped rather than
		// offered to the user as somewhere to connect.
		if (value.contains(QLatin1Char('/')) || value.contains(QLatin1Char(' '))
				|| value.contains(QLatin1Char(':'))) {
			continue;
		}

		if (key == QLatin1String("addserver"))
			serversToAdd.append(value);
		else if (key == QLatin1String("removeserver"))
			serversToRemove.append(value);
	}

	emit ServerListReceived(serversToAdd, serversToRemove);
}


}  // namespace hitux

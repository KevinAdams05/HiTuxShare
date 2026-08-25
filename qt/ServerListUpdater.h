/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef SERVER_LIST_UPDATER_H
#define SERVER_LIST_UPDATER_H


#include <QObject>
#include <QStringList>
#include <QUrl>


class QNetworkAccessManager;
class QNetworkReply;


namespace hitux {


/** Fetches the community list of MUSCLE servers.
  *
  * The list is a plain "key=value" file published by the long-running BeShare
  * servers, with "addserver=host" and "removeserver=host" lines. HiShare reads
  * the same file.
  *
  * Three deliberate constraints, because this is the one feature that takes
  * instructions from the network about who to talk to:
  *
  *  - HTTPS is tried first and plain HTTP only as a fallback, since the
  *    upstream servers are old and may not offer TLS.
  *  - A fetched server is only ever *offered* in the dropdown. Nothing here
  *    connects to anything; that stays the user's decision.
  *  - The whole feature is off unless switched on, because it contacts a third
  *    party and reveals the user's address to them.
  */
class ServerListUpdater : public QObject
{
	Q_OBJECT

public:
	explicit ServerListUpdater(QObject* parent = nullptr);
	~ServerListUpdater() override;

	/** Starts a fetch. Does nothing if one is already running. */
	void Start();

	bool IsRunning() const { return fReply != nullptr; }

signals:
	/** Emitted once per successful fetch.
	  * @param serversToAdd hosts the list says to offer
	  * @param serversToRemove hosts the list says are gone
	  */
	void ServerListReceived(const QStringList& serversToAdd,
		const QStringList& serversToRemove);

	/** Emitted when the fetch failed, with a human-readable reason.
	  * @param reason what went wrong
	  */
	void UpdateFailed(const QString& reason);

private slots:
	void _OnReplyFinished();

private:
	void _Fetch(const QUrl& url);
	void _ParseBody(const QByteArray& body);

	QNetworkAccessManager* fNetwork;
	QNetworkReply* fReply;

	// Which URL we are on, so an HTTPS failure can retry once over HTTP and
	// then stop rather than looping.
	int fAttemptIndex;
};


}  // namespace hitux


#endif  // SERVER_LIST_UPDATER_H

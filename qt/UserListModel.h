/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef USER_LIST_MODEL_H
#define USER_LIST_MODEL_H


#include "core/UserRecord.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QVector>


namespace hitux {


/** The user list, as a Qt model.
  *
  * The model keeps its own copy of each UserRecord rather than reading through to
  * the ServerConnection's registry.  That is deliberate: a model has to be able to
  * answer data() for a row that a delete is halfway through removing, and mirroring
  * the records makes row indices ours to manage rather than something we infer from
  * a hash table's iteration order.
  *
  * Sorting is left to a QSortFilterProxyModel on top, so this model never reorders
  * itself under the user's selection.
  */
class UserListModel : public QAbstractTableModel
{
	Q_OBJECT

public:
	enum Column
	{
		COLUMN_NAME = 0,
		COLUMN_STATUS,
		COLUMN_CLIENT,
		COLUMN_FILES,
		COLUMN_SERVER,
		COLUMN_HOST,

		COLUMN_COUNT
	};

	// Role that yields the session ID for a row, for callers that need to address a
	// user rather than display one.
	static const int kSessionIdRole = Qt::UserRole + 1;

	explicit UserListModel(QObject* parent = nullptr);
	~UserListModel() override;

	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	int columnCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QVariant headerData(int section, Qt::Orientation orientation,
		int role = Qt::DisplayRole) const override;

	/** Inserts or refreshes a user.
	  * @param user the user's current state
	  */
	void UpdateUser(const void* connection, const QString& serverName,
		const UserRecord& user);

	/** Removes a user, if present.
	  * @param sessionId the session that left
	  */
	void RemoveUser(const void* connection, const muscle::String& sessionId);

	/** Drops every user belonging to one connection, e.g. on disconnect. */
	void RemoveUsersForConnection(const void* connection);

	/** Drops every user, as happens on disconnect. */
	void Clear();

	/** Returns the session ID at a row, or an empty string if the row is invalid.
	  * @param row the row to look up
	  */
	QString GetSessionIdForRow(int row) const;

private:
	/** A user together with the server they are on.
	  *
	  * Keyed by both, because a session ID is only unique within one server:
	  * session 5 on two servers is two unrelated people, and keying by the ID
	  * alone would have them overwrite each other.
	  */
	struct Entry
	{
		Entry() : connection(nullptr) {}

		UserRecord user;
		const void* connection;
		QString serverName;
	};

	static QString _MakeKey(const void* connection, const QString& sessionId);
	int _FindRow(const void* connection, const QString& sessionId) const;
	void _RebuildIndex();

	QVector<Entry> fUsers;
	QHash<QString, int> fRowsByKey;
};


}  // namespace hitux


#endif  // USER_LIST_MODEL_H

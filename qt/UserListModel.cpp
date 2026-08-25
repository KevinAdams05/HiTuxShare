/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "qt/UserListModel.h"

#include "qt/QtConversions.h"


namespace hitux {


UserListModel::UserListModel(QObject* parent)
	:
	QAbstractTableModel(parent)
{
}


UserListModel::~UserListModel()
{
}


int
UserListModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : fUsers.size();
}


int
UserListModel::columnCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : COLUMN_COUNT;
}


QVariant
UserListModel::data(const QModelIndex& index, int role) const
{
	if (index.isValid() == false || index.row() >= fUsers.size())
		return QVariant();

	const Entry& entry = fUsers.at(index.row());
	const UserRecord& user = entry.user;

	if (role == kSessionIdRole)
		return ToQString(user.sessionId);

	if (role == Qt::TextAlignmentRole && index.column() == COLUMN_FILES)
		return QVariant(Qt::AlignRight | Qt::AlignVCenter);

	if (role == Qt::ToolTipRole) {
		QString tooltip = tr("Session %1").arg(ToQString(user.sessionId));
		if (user.hostName.HasChars())
			tooltip += tr("\nHost: %1").arg(ToQString(user.hostName));
		if (user.isFirewalled)
			tooltip += tr("\nBehind a firewall -- cannot accept connections");
		if (user.bandwidthLabel.HasChars())
			tooltip += tr("\nConnection: %1").arg(ToQString(user.bandwidthLabel));

		return tooltip;
	}

	if (role != Qt::DisplayRole)
		return QVariant();

	switch (index.column()) {
		case COLUMN_NAME:
			return ToQString(user.GetDisplayName());

		case COLUMN_STATUS:
			return ToQString(user.userStatus);

		case COLUMN_CLIENT:
			return ToQString(user.clientVersion);

		case COLUMN_FILES:
			// Sorts as a number rather than as text because the proxy model compares
			// the underlying QVariant, not the rendered string.
			return QVariant((qulonglong) user.fileCount);

		case COLUMN_SERVER:
			return entry.serverName;

		case COLUMN_HOST:
			return ToQString(user.hostName);

		default:
			return QVariant();
	}
}


QVariant
UserListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
		return QVariant();

	switch (section) {
		case COLUMN_NAME:
			return tr("Name");

		case COLUMN_STATUS:
			return tr("Status");

		case COLUMN_CLIENT:
			return tr("Client");

		case COLUMN_FILES:
			return tr("Files");

		case COLUMN_SERVER:
			return tr("Server");

		case COLUMN_HOST:
			return tr("Host");

		default:
			return QVariant();
	}
}


void
UserListModel::UpdateUser(const void* connection, const QString& serverName,
	const UserRecord& user)
{
	const QString sessionId = ToQString(user.sessionId);
	const int existingRow = _FindRow(connection, sessionId);

	Entry entry;
	entry.user = user;
	entry.connection = connection;
	entry.serverName = serverName;

	if (existingRow >= 0) {
		fUsers[existingRow] = entry;
		emit dataChanged(index(existingRow, 0),
			index(existingRow, COLUMN_COUNT - 1));
		return;
	}

	const int newRow = fUsers.size();
	beginInsertRows(QModelIndex(), newRow, newRow);
	fUsers.append(entry);
	fRowsByKey.insert(_MakeKey(connection, sessionId), newRow);
	endInsertRows();
}


void
UserListModel::RemoveUser(const void* connection, const muscle::String& sessionId)
{
	const int row = _FindRow(connection, ToQString(sessionId));
	if (row < 0)
		return;

	beginRemoveRows(QModelIndex(), row, row);
	fUsers.remove(row);

	// Every row after the removed one shifted down, so the lookup table is
	// rebuilt rather than patched -- getting this wrong is how a user list
	// starts addressing the wrong person.
	_RebuildIndex();
	endRemoveRows();
}


void
UserListModel::RemoveUsersForConnection(const void* connection)
{
	QVector<Entry> kept;
	kept.reserve(fUsers.size());
	for (const Entry& entry : fUsers) {
		if (entry.connection != connection)
			kept.append(entry);
	}

	if (kept.size() == fUsers.size())
		return;

	beginResetModel();
	fUsers = kept;
	_RebuildIndex();
	endResetModel();
}


void
UserListModel::Clear()
{
	if (fUsers.isEmpty())
		return;

	beginResetModel();
	fUsers.clear();
	fRowsByKey.clear();
	endResetModel();
}


QString
UserListModel::GetSessionIdForRow(int row) const
{
	if (row < 0 || row >= fUsers.size())
		return QString();

	return ToQString(fUsers.at(row).user.sessionId);
}


QString
UserListModel::_MakeKey(const void* connection, const QString& sessionId)
{
	return QString::number(reinterpret_cast<quintptr>(connection))
		+ QChar('\n') + sessionId;
}


int
UserListModel::_FindRow(const void* connection, const QString& sessionId) const
{
	const auto iterator = fRowsByKey.constFind(_MakeKey(connection, sessionId));
	return (iterator != fRowsByKey.constEnd()) ? iterator.value() : -1;
}


void
UserListModel::_RebuildIndex()
{
	fRowsByKey.clear();
	fRowsByKey.reserve(fUsers.size());
	for (int row = 0; row < fUsers.size(); row++) {
		fRowsByKey.insert(_MakeKey(fUsers[row].connection,
			ToQString(fUsers[row].user.sessionId)), row);
	}
}


}  // namespace hitux

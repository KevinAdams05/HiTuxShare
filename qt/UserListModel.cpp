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

	const UserRecord& user = fUsers.at(index.row());

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

		case COLUMN_HOST:
			return tr("Host");

		default:
			return QVariant();
	}
}


void
UserListModel::UpdateUser(const UserRecord& user)
{
	const QString sessionId = ToQString(user.sessionId);
	const int existingRow = _FindRowForSessionId(sessionId);

	if (existingRow >= 0) {
		fUsers[existingRow] = user;
		emit dataChanged(index(existingRow, 0),
			index(existingRow, COLUMN_COUNT - 1));
		return;
	}

	const int newRow = fUsers.size();
	beginInsertRows(QModelIndex(), newRow, newRow);
	fUsers.append(user);
	fRowsBySessionId.insert(sessionId, newRow);
	endInsertRows();
}


void
UserListModel::RemoveUser(const muscle::String& sessionId)
{
	const QString sessionIdString = ToQString(sessionId);
	const int row = _FindRowForSessionId(sessionIdString);
	if (row < 0)
		return;

	beginRemoveRows(QModelIndex(), row, row);
	fUsers.remove(row);
	fRowsBySessionId.remove(sessionIdString);

	// Every row after the removed one shifted down by one, so the lookup table has to
	// be rebuilt rather than patched -- getting this wrong is how a user list starts
	// addressing the wrong person.
	for (auto iterator = fRowsBySessionId.begin();
			iterator != fRowsBySessionId.end(); ++iterator) {
		if (iterator.value() > row)
			iterator.value() = iterator.value() - 1;
	}

	endRemoveRows();
}


void
UserListModel::Clear()
{
	if (fUsers.isEmpty())
		return;

	beginResetModel();
	fUsers.clear();
	fRowsBySessionId.clear();
	endResetModel();
}


QString
UserListModel::GetSessionIdForRow(int row) const
{
	if (row < 0 || row >= fUsers.size())
		return QString();

	return ToQString(fUsers.at(row).sessionId);
}


int
UserListModel::_FindRowForSessionId(const QString& sessionId) const
{
	const auto iterator = fRowsBySessionId.constFind(sessionId);
	return (iterator != fRowsBySessionId.constEnd()) ? iterator.value() : -1;
}


}  // namespace hitux

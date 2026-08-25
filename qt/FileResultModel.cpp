/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "qt/FileResultModel.h"

#include "core/FormatUtilities.h"
#include "qt/QtConversions.h"


namespace hitux {


FileResultModel::FileResultModel(QObject* parent)
	:
	QAbstractTableModel(parent)
{
}


FileResultModel::~FileResultModel()
{
}


int
FileResultModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : fResults.size();
}


int
FileResultModel::columnCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : COLUMN_COUNT;
}


QVariant
FileResultModel::data(const QModelIndex& index, int role) const
{
	if (index.isValid() == false || index.row() >= fResults.size())
		return QVariant();

	const Entry& entry = fResults.at(index.row());
	const FileResult& result = entry.result;

	switch (role) {
		case kSessionIdRole:
			return ToQString(result.sessionId);

		case kFileNameRole:
			return ToQString(result.fileName);

		case kSortRole:
			// Only the two columns whose text does not sort correctly need a
			// separate key; everything else sorts fine as displayed.
			if (index.column() == COLUMN_SIZE)
				return QVariant((qlonglong) result.fileSize);
			if (index.column() == COLUMN_MODIFIED)
				return QVariant((qlonglong) result.modificationTime);
			break;

		case Qt::TextAlignmentRole:
			if (index.column() == COLUMN_SIZE)
				return QVariant(Qt::AlignRight | Qt::AlignVCenter);
			break;

		case Qt::ToolTipRole:
		{
			QString tooltip = ToQString(result.fileName);
			if (result.path.HasChars())
				tooltip += tr("\nIn: %1").arg(ToQString(result.path));
			if (result.isFirewalled) {
				tooltip += tr("\nSharer is firewalled -- they must connect back"
					" to us");
			}
			return tooltip;
		}

		default:
			break;
	}

	if (role != Qt::DisplayRole && role != kSortRole)
		return QVariant();

	switch (index.column()) {
		case COLUMN_NAME:
			return ToQString(result.fileName);

		case COLUMN_SIZE:
			return ToQString(FormatByteSize(result.fileSize));

		case COLUMN_USER:
			return entry.sharerName.isEmpty()
				? ToQString(result.sessionId) : entry.sharerName;

		case COLUMN_SERVER:
			return entry.serverName;

		case COLUMN_MODIFIED:
			return ToQString(FormatTimestamp(result.modificationTime));

		case COLUMN_KIND:
			return ToQString(result.kind);

		case COLUMN_PATH:
			return ToQString(result.path);

		default:
			return QVariant();
	}
}


QVariant
FileResultModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
		return QVariant();

	switch (section) {
		case COLUMN_NAME:
			return tr("File");

		case COLUMN_SIZE:
			return tr("Size");

		case COLUMN_USER:
			return tr("Shared by");

		case COLUMN_SERVER:
			return tr("Server");

		case COLUMN_MODIFIED:
			return tr("Modified");

		case COLUMN_KIND:
			return tr("Kind");

		case COLUMN_PATH:
			return tr("Path");

		default:
			return QVariant();
	}
}


void
FileResultModel::AddResults(const QVector<Entry>& entries)
{
	if (entries.isEmpty())
		return;

	// Split into genuinely new rows and refreshes of rows we already hold: the
	// first go in as one insert, the second as in-place updates.
	QVector<Entry> additions;
	additions.reserve(entries.size());

	for (const Entry& entry : entries) {
		const QString key = _MakeKey(entry.connection, entry.result.sessionId,
			entry.result.fileName);
		const auto existing = fRowsByKey.constFind(key);
		if (existing != fRowsByKey.constEnd()) {
			fResults[existing.value()] = entry;
			emit dataChanged(index(existing.value(), 0),
				index(existing.value(), COLUMN_COUNT - 1));
		} else {
			additions.append(entry);
		}
	}

	if (additions.isEmpty())
		return;

	const int firstRow = fResults.size();
	beginInsertRows(QModelIndex(), firstRow, firstRow + additions.size() - 1);

	for (const Entry& entry : additions) {
		fRowsByKey.insert(_MakeKey(entry.connection, entry.result.sessionId,
			entry.result.fileName), fResults.size());
		fResults.append(entry);
	}

	endInsertRows();
}


void
FileResultModel::RemoveResult(const void* connection,
	const muscle::String& sessionId, const muscle::String& fileName)
{
	const auto existing
		= fRowsByKey.constFind(_MakeKey(connection, sessionId, fileName));
	if (existing == fRowsByKey.constEnd())
		return;

	const int row = existing.value();
	beginRemoveRows(QModelIndex(), row, row);
	fResults.remove(row);
	_RebuildIndex();
	endRemoveRows();
}


void
FileResultModel::RemoveResultsForSession(const void* connection,
	const muscle::String& sessionId)
{
	const QString sessionIdString = ToQString(sessionId);

	QVector<Entry> kept;
	kept.reserve(fResults.size());
	for (const Entry& entry : fResults) {
		if (entry.connection != connection
				|| ToQString(entry.result.sessionId) != sessionIdString) {
			kept.append(entry);
		}
	}

	if (kept.size() == fResults.size())
		return;

	beginResetModel();
	fResults = kept;
	_RebuildIndex();
	endResetModel();
}


void
FileResultModel::Clear()
{
	if (fResults.isEmpty())
		return;

	beginResetModel();
	fResults.clear();
	fRowsByKey.clear();
	endResetModel();
}


void
FileResultModel::RemoveResultsForConnection(const void* connection)
{
	QVector<Entry> kept;
	kept.reserve(fResults.size());
	for (const Entry& entry : fResults) {
		if (entry.connection != connection)
			kept.append(entry);
	}

	if (kept.size() == fResults.size())
		return;

	beginResetModel();
	fResults = kept;
	_RebuildIndex();
	endResetModel();
}


void
FileResultModel::UpdateSharerName(const void* connection,
	const muscle::String& sessionId, const QString& displayName)
{
	const QString sessionIdString = ToQString(sessionId);
	int firstChanged = -1;
	int lastChanged = -1;

	for (int row = 0; row < fResults.size(); row++) {
		Entry& entry = fResults[row];
		if (entry.connection != connection
				|| ToQString(entry.result.sessionId) != sessionIdString
				|| entry.sharerName == displayName) {
			continue;
		}

		entry.sharerName = displayName;
		if (firstChanged < 0)
			firstChanged = row;

		lastChanged = row;
	}

	if (firstChanged >= 0) {
		emit dataChanged(index(firstChanged, COLUMN_USER),
			index(lastChanged, COLUMN_USER));
	}
}


const FileResultModel::Entry*
FileResultModel::GetEntryForRow(int row) const
{
	if (row < 0 || row >= fResults.size())
		return nullptr;

	return &fResults.at(row);
}


QString
FileResultModel::_MakeKey(const void* connection,
	const muscle::String& sessionId, const muscle::String& fileName)
{
	// The connection is part of the key because a session ID is only unique
	// within one server: the same ID on two servers is two different people.
	// A newline cannot appear in any of these, so it cannot make two different
	// triples collide the way a more obvious separator might.
	return QString::number(reinterpret_cast<quintptr>(connection))
		+ QChar('\n') + ToQString(sessionId) + QChar('\n')
		+ ToQString(fileName);
}


void
FileResultModel::_RebuildIndex()
{
	fRowsByKey.clear();
	fRowsByKey.reserve(fResults.size());
	for (int row = 0; row < fResults.size(); row++) {
		fRowsByKey.insert(_MakeKey(fResults[row].connection,
			fResults[row].result.sessionId, fResults[row].result.fileName), row);
	}
}


}  // namespace hitux

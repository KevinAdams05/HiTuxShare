/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "qt/FileResultModel.h"

#include "core/FormatUtilities.h"
#include "core/UserRegistry.h"
#include "qt/QtConversions.h"


namespace hitux {


FileResultModel::FileResultModel(QObject* parent)
	:
	QAbstractTableModel(parent),
	fUsers(nullptr)
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

	const FileResult& result = fResults.at(index.row());

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
			return fUsers != nullptr
				? ToQString(fUsers->GetDisplayNameForSession(result.sessionId))
				: ToQString(result.sessionId);

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
FileResultModel::AddResults(const QVector<FileResult>& results)
{
	if (results.isEmpty())
		return;

	// Split into genuinely new rows and refreshes of rows we already hold: the
	// first go in as one insert, the second as in-place updates.
	QVector<FileResult> additions;
	additions.reserve(results.size());

	for (const FileResult& result : results) {
		const QString key = _MakeKey(result.sessionId, result.fileName);
		const auto existing = fRowsByKey.constFind(key);
		if (existing != fRowsByKey.constEnd()) {
			fResults[existing.value()] = result;
			emit dataChanged(index(existing.value(), 0),
				index(existing.value(), COLUMN_COUNT - 1));
		} else {
			additions.append(result);
		}
	}

	if (additions.isEmpty())
		return;

	const int firstRow = fResults.size();
	beginInsertRows(QModelIndex(), firstRow, firstRow + additions.size() - 1);

	for (const FileResult& result : additions) {
		fRowsByKey.insert(_MakeKey(result.sessionId, result.fileName),
			fResults.size());
		fResults.append(result);
	}

	endInsertRows();
}


void
FileResultModel::RemoveResult(const muscle::String& sessionId,
	const muscle::String& fileName)
{
	const auto existing = fRowsByKey.constFind(_MakeKey(sessionId, fileName));
	if (existing == fRowsByKey.constEnd())
		return;

	const int row = existing.value();
	beginRemoveRows(QModelIndex(), row, row);
	fResults.remove(row);
	_RebuildIndex();
	endRemoveRows();
}


void
FileResultModel::RemoveResultsForSession(const muscle::String& sessionId)
{
	const QString sessionIdString = ToQString(sessionId);

	QVector<FileResult> kept;
	kept.reserve(fResults.size());
	for (const FileResult& result : fResults) {
		if (ToQString(result.sessionId) != sessionIdString)
			kept.append(result);
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
FileResultModel::RefreshUserNames()
{
	if (fResults.isEmpty())
		return;

	emit dataChanged(index(0, COLUMN_USER),
		index(fResults.size() - 1, COLUMN_USER));
}


const FileResult*
FileResultModel::GetResultForRow(int row) const
{
	if (row < 0 || row >= fResults.size())
		return nullptr;

	return &fResults.at(row);
}


QString
FileResultModel::_MakeKey(const muscle::String& sessionId,
	const muscle::String& fileName)
{
	// A newline cannot appear in a session ID, so it cannot make two different
	// pairs collide the way a more obvious separator might.
	return ToQString(sessionId) + QChar('\n') + ToQString(fileName);
}


void
FileResultModel::_RebuildIndex()
{
	fRowsByKey.clear();
	fRowsByKey.reserve(fResults.size());
	for (int row = 0; row < fResults.size(); row++) {
		fRowsByKey.insert(_MakeKey(fResults[row].sessionId,
			fResults[row].fileName), row);
	}
}


}  // namespace hitux

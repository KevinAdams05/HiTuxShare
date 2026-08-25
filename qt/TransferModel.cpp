/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "qt/TransferModel.h"

#include "core/DownloadManager.h"
#include "core/FileUploadServer.h"
#include "core/FormatUtilities.h"
#include "qt/QtConversions.h"

#include <QApplication>
#include <QPainter>
#include <QStyleOptionProgressBar>


namespace hitux {


namespace {


/** A short phrase for a download's state, for the Status column.
  * @param download the transfer to describe
  */
QString
DescribeState(const FileDownload& download)
{
	switch (download.GetState()) {
		case DOWNLOAD_IDLE:
			return QObject::tr("Waiting");

		case DOWNLOAD_CONNECTING:
			return QObject::tr("Connecting");

		case DOWNLOAD_REQUESTING:
			return QObject::tr("Requesting");

		case DOWNLOAD_QUEUED_REMOTELY:
			return QObject::tr("Queued by peer");

		case DOWNLOAD_TRANSFERRING:
			return QObject::tr("Downloading");

		case DOWNLOAD_FINISHED:
			return QObject::tr("Finished");

		case DOWNLOAD_FAILED:
			return ToQString(download.GetErrorText());

		default:
			return QString();
	}
}


}  // unnamed namespace


QVariant
TransferModel::_UploadData(const FileUploadServer::UploadStatus& upload,
	int column, int role)
{
	if (role == kProgressRole) {
		if (upload.fileSize <= 0)
			return 0;

		return (int) qBound<int64>(0,
			(upload.bytesSent * 100) / upload.fileSize, (int64) 100);
	}

	if (role != Qt::DisplayRole)
		return QVariant();

	switch (column) {
		case TransferModel::COLUMN_FILE:
			return upload.fileName.HasChars()
				? ToQString(upload.fileName) : QObject::tr("(waiting)");

		case TransferModel::COLUMN_PROGRESS:
			return (upload.fileSize > 0)
				? QObject::tr("%1%").arg((upload.bytesSent * 100)
					/ upload.fileSize) : QString();

		case TransferModel::COLUMN_SIZE:
			return ToQString(FormatByteSize(upload.fileSize));

		case TransferModel::COLUMN_RATE:
			return QString();

		case TransferModel::COLUMN_FROM:
			// Reads as a direction rather than a name, so an upload row cannot
			// be mistaken for a download from the same person.
			return QObject::tr("to %1").arg(ToQString(upload.peerName));

		case TransferModel::COLUMN_STATUS:
			return upload.isSending ? QObject::tr("Uploading")
				: QObject::tr("Connected");

		default:
			return QVariant();
	}
}


TransferModel::TransferModel(const DownloadManager* downloads, QObject* parent)
	:
	QAbstractTableModel(parent),
	fDownloads(downloads),
	fUploads(nullptr)
{
}


void
TransferModel::SetUploadServer(const FileUploadServer* uploads)
{
	beginResetModel();
	fUploads = uploads;
	endResetModel();
}


TransferModel::~TransferModel()
{
}


int
TransferModel::rowCount(const QModelIndex& parent) const
{
	if (parent.isValid())
		return 0;

	const int uploadCount = (fUploads != nullptr)
		? (int) fUploads->GetActiveUploadCount() : 0;
	return (int) fDownloads->GetDownloadCount() + uploadCount;
}


int
TransferModel::columnCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : COLUMN_COUNT;
}


QVariant
TransferModel::data(const QModelIndex& index, int role) const
{
	if (index.isValid() == false)
		return QVariant();

	// Downloads occupy the first rows, uploads the rest.
	const int downloadCount = (int) fDownloads->GetDownloadCount();
	if (index.row() >= downloadCount) {
		if (fUploads == nullptr)
			return QVariant();

		const muscle::Queue<FileUploadServer::UploadStatus> uploads
			= fUploads->GetUploadStatuses();
		const uint32 uploadIndex = (uint32) (index.row() - downloadCount);
		if (uploadIndex >= uploads.GetNumItems())
			return QVariant();

		return _UploadData(uploads[uploadIndex], index.column(), role);
	}

	const FileDownload* download
		= fDownloads->GetDownloadAt((uint32) index.row());
	if (download == nullptr)
		return QVariant();

	const int64 expected = download->GetTotalBytesExpected();
	const int64 done = download->GetTotalBytesDone();

	if (role == kProgressRole) {
		if (download->GetState() == DOWNLOAD_FINISHED)
			return 100;
		if (expected <= 0)
			return 0;

		return (int) qBound<int64>(0, (done * 100) / expected, (int64) 100);
	}

	if (role == Qt::TextAlignmentRole && index.column() == COLUMN_SIZE)
		return QVariant(Qt::AlignRight | Qt::AlignVCenter);

	if (role != Qt::DisplayRole)
		return QVariant();

	switch (index.column()) {
		case COLUMN_FILE:
		{
			const QString current = ToQString(download->GetCurrentFileName());
			const uint32 total = download->GetRequestedFileCount();
			if (total > 1) {
				return tr("%1 (%2 of %3)").arg(current.isEmpty()
					? tr("(waiting)") : current)
					.arg(download->GetCompletedFileCount() + 1).arg(total);
			}

			return current.isEmpty() ? tr("(waiting)") : current;
		}

		case COLUMN_PROGRESS:
			// Drawn by the delegate; the text is a fallback for anything that
			// asks the model directly, such as a copy-to-clipboard.
			return (expected > 0)
				? tr("%1%").arg((done * 100) / expected) : QString();

		case COLUMN_SIZE:
			return ToQString(FormatByteSize(expected > 0 ? expected : done));

		case COLUMN_RATE:
			return ToQString(FormatTransferRate(download->GetBytesPerSecond()));

		case COLUMN_FROM:
			return download->GetRemoteUserName().HasChars()
				? ToQString(download->GetRemoteUserName())
				: ToQString(download->GetRemoteSessionId());

		case COLUMN_STATUS:
			return DescribeState(*download);

		default:
			return QVariant();
	}
}


QVariant
TransferModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
		return QVariant();

	switch (section) {
		case COLUMN_FILE:
			return tr("File");

		case COLUMN_PROGRESS:
			return tr("Progress");

		case COLUMN_SIZE:
			return tr("Size");

		case COLUMN_RATE:
			return tr("Rate");

		case COLUMN_FROM:
			return tr("From");

		case COLUMN_STATUS:
			return tr("Status");

		default:
			return QVariant();
	}
}


void
TransferModel::NotifyListChanged()
{
	beginResetModel();
	endResetModel();
}


void
TransferModel::NotifyRowChanged(uint32 index)
{
	if (index >= (uint32) rowCount())
		return;

	emit dataChanged(this->index((int) index, 0),
		this->index((int) index, COLUMN_COUNT - 1));
}


TransferProgressDelegate::TransferProgressDelegate(QObject* parent)
	:
	QStyledItemDelegate(parent)
{
}


void
TransferProgressDelegate::paint(QPainter* painter,
	const QStyleOptionViewItem& option, const QModelIndex& index) const
{
	const QVariant progress = index.data(TransferModel::kProgressRole);
	if (progress.isValid() == false) {
		QStyledItemDelegate::paint(painter, option, index);
		return;
	}

	// Draw the row's own background first -- selection highlight, alternating
	// colours -- but without its text, which the bar replaces.
	QStyleOptionViewItem background(option);
	initStyleOption(&background, index);
	background.text.clear();
	QStyledItemDelegate::paint(painter, background, index);

	QStyleOptionProgressBar bar;
	bar.rect = option.rect.adjusted(2, 3, -2, -3);

	// These four must be carried over from the item option. Left at their
	// defaults the bar draws unfilled and its text lands outside the cell,
	// which looks like a broken transfer rather than a broken delegate.
	bar.palette = option.palette;
	bar.fontMetrics = option.fontMetrics;
	bar.direction = option.direction;
	bar.state = QStyle::State_Enabled | QStyle::State_Horizontal;

	bar.minimum = 0;
	bar.maximum = 100;
	bar.progress = progress.toInt();
	bar.text = QStringLiteral("%1%").arg(bar.progress);
	bar.textVisible = true;
	bar.textAlignment = Qt::AlignCenter;

	QApplication::style()->drawControl(QStyle::CE_ProgressBar, &bar, painter);
}


}  // namespace hitux

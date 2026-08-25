/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef TRANSFER_MODEL_H
#define TRANSFER_MODEL_H


#include "core/FileUploadServer.h"
#include "support/MuscleSupport.h"

#include <QAbstractTableModel>
#include <QStyledItemDelegate>


namespace hitux {


class DownloadManager;


/** The running transfers, as a Qt model.
  *
  * A view over DownloadManager rather than a copy of it: transfers are few and
  * change constantly, so reading through on demand is simpler than mirroring
  * and keeps the two from disagreeing.
  */
class TransferModel : public QAbstractTableModel
{
	Q_OBJECT

public:
	enum Column
	{
		COLUMN_FILE = 0,
		COLUMN_PROGRESS,
		COLUMN_SIZE,
		COLUMN_RATE,
		COLUMN_FROM,
		COLUMN_STATUS,

		COLUMN_COUNT
	};

	// Percent complete, for the progress-bar delegate to draw.
	static const int kProgressRole = Qt::UserRole + 1;

	/** Constructor.
	  * @param downloads the manager to read from; not owned, must outlive us
	  * @param parent Qt parent
	  */
	explicit TransferModel(const DownloadManager* downloads,
		QObject* parent = nullptr);

	/** Supplies the upload server, so people downloading from us appear in the
	  * same list as our own downloads -- which is what BeShare shows and what
	  * anyone watching a transfer list actually wants to know.
	  * @param uploads the server; not owned, must outlive us
	  */
	void SetUploadServer(const FileUploadServer* uploads);
	~TransferModel() override;

	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	int columnCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QVariant headerData(int section, Qt::Orientation orientation,
		int role = Qt::DisplayRole) const override;

	/** The list gained or lost a transfer. */
	void NotifyListChanged();

	/** One transfer progressed.
	  * @param index which row
	  */
	void NotifyRowChanged(uint32 index);

private:
	static QVariant _UploadData(
		const class FileUploadServer::UploadStatus& upload, int column, int role);

	const DownloadManager* fDownloads;
	const FileUploadServer* fUploads;
};


/** Draws the Progress column as a progress bar.
  *
  * A bar reads at a glance in a way a percentage string does not, which is the
  * whole point of a transfer list you leave running in the corner of a screen.
  */
class TransferProgressDelegate : public QStyledItemDelegate
{
	Q_OBJECT

public:
	explicit TransferProgressDelegate(QObject* parent = nullptr);

	void paint(QPainter* painter, const QStyleOptionViewItem& option,
		const QModelIndex& index) const override;
};


}  // namespace hitux


#endif  // TRANSFER_MODEL_H

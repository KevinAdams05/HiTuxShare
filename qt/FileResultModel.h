/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef FILE_RESULT_MODEL_H
#define FILE_RESULT_MODEL_H


#include "core/FileResult.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QVector>


namespace hitux {


/** Query results, as a Qt model.
  *
  * Sized for the real case rather than the demo one: a bare "*" against a peer
  * sharing twenty thousand files delivers twenty thousand rows, so inserts are
  * batched and removals for a whole departing user are done as one reset rather
  * than row by row.
  *
  * Sorting happens through a proxy on top, using kSortRole so that Size sorts
  * numerically while still displaying as "2.8 MB".
  */
class FileResultModel : public QAbstractTableModel
{
	Q_OBJECT

public:
	enum Column
	{
		COLUMN_NAME = 0,
		COLUMN_SIZE,
		COLUMN_USER,
		COLUMN_SERVER,
		COLUMN_MODIFIED,
		COLUMN_KIND,
		COLUMN_PATH,

		COLUMN_COUNT
	};

	/** One result together with where it came from.
	  *
	  * The server is part of a result's identity, not decoration: the same file
	  * name from the same session ID on two different servers is two unrelated
	  * files, and a download has to go back to the right one.
	  */
	struct Entry
	{
		Entry() : connection(nullptr) {}

		FileResult result;

		// Opaque here on purpose -- the model does not need to know what a
		// ServerConnection is, only that rows from different ones differ.
		const void* connection;

		QString serverName;
		QString sharerName;
	};

	// Raw comparable value, so Size and Modified sort by magnitude rather than
	// by the text they render as.
	static const int kSortRole = Qt::UserRole + 1;
	static const int kSessionIdRole = Qt::UserRole + 2;
	static const int kFileNameRole = Qt::UserRole + 3;

	explicit FileResultModel(QObject* parent = nullptr);
	~FileResultModel() override;

	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	int columnCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QVariant headerData(int section, Qt::Orientation orientation,
		int role = Qt::DisplayRole) const override;

	/** Adds or refreshes a batch of results in one model transaction.
	  * @param results the results to add
	  */
	void AddResults(const QVector<Entry>& entries);

	/** Removes one result, if present.
	  * @param sessionId who was sharing it
	  * @param fileName the file that went away
	  */
	void RemoveResult(const void* connection, const muscle::String& sessionId,
		const muscle::String& fileName);

	/** Removes every result belonging to one user, as one operation.
	  *
	  * A departing user who shared thousands of files would otherwise arrive as
	  * thousands of individual removals, each shifting every later row.
	  *
	  * @param sessionId the user who left
	  */
	void RemoveResultsForSession(const void* connection,
		const muscle::String& sessionId);

	/** Drops every row belonging to one connection, e.g. when it disconnects. */
	void RemoveResultsForConnection(const void* connection);

	void Clear();

	/** Replaces the displayed sharer name wherever it is now known.
	  * @param connection which server the user belongs to
	  * @param sessionId the user
	  * @param displayName their current name
	  */
	void UpdateSharerName(const void* connection, const muscle::String& sessionId,
		const QString& displayName);

	const Entry* GetEntryForRow(int row) const;

private:
	static QString _MakeKey(const void* connection,
		const muscle::String& sessionId, const muscle::String& fileName);
	void _RebuildIndex();

	QVector<Entry> fResults;
	QHash<QString, int> fRowsByKey;
};


}  // namespace hitux


#endif  // FILE_RESULT_MODEL_H

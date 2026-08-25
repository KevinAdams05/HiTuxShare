/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef SETTINGS_DIALOG_H
#define SETTINGS_DIALOG_H


#include <QDialog>


class QCheckBox;
class QLineEdit;
class QSpinBox;


namespace hitux {


class ApplicationSettings;


/** One place to see and change everything the application remembers.
  *
  * Deliberately not a pile of menu toggles. HiShare grew its settings across a
  * Settings menu, a File menu and a separate window, and the result is that
  * things like the share folder are hard to find. Every control here is bound
  * to a setting that actually changes behaviour -- a preference that does
  * nothing is worse than a missing one, because it quietly lies.
  */
class SettingsDialog : public QDialog
{
	Q_OBJECT

public:
	/** Constructor.
	  * @param settings the settings to read and, on accept, write back
	  * @param parent Qt parent
	  */
	explicit SettingsDialog(ApplicationSettings& settings,
		QWidget* parent = nullptr);
	~SettingsDialog() override;

	/** Writes every control back into the settings object. Called on accept. */
	void ApplyToSettings();

private slots:
	void _OnBrowseDownloadFolder();
	void _OnBrowseShareFolder();
	void _OnBrowseLogFolder();

private:
	QWidget* _BuildIdentityPage();
	QWidget* _BuildFoldersPage();
	QWidget* _BuildTransfersPage();
	QWidget* _BuildBehaviourPage();

	void _LoadFromSettings();

	ApplicationSettings& fSettings;

	QLineEdit* fUserNameField;
	QLineEdit* fUserStatusField;
	QLineEdit* fAwayStatusField;

	QLineEdit* fDownloadFolderField;
	QLineEdit* fShareFolderField;
	QCheckBox* fRetainFilePathsBox;

	QCheckBox* fFileSharingBox;
	QCheckBox* fFirewalledBox;

	QSpinBox* fMaxDownloadsBox;
	QSpinBox* fMaxUploadsBox;
	QSpinBox* fDownloadRateBox;
	QSpinBox* fUploadRateBox;
	QCheckBox* fAutoClearBox;

	QCheckBox* fConnectOnStartupBox;
	QCheckBox* fNotificationsBox;
	QCheckBox* fAutoUpdateServersBox;
	QCheckBox* fChatLoggingBox;
	QLineEdit* fLogFolderField;
	QSpinBox* fChatFontSizeBox;
};


}  // namespace hitux


#endif  // SETTINGS_DIALOG_H

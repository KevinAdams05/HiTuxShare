/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "qt/SettingsDialog.h"

#include "core/ApplicationSettings.h"
#include "qt/QtConversions.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>


namespace hitux {


namespace {


// Rates are stored in bytes per second but shown in kilobytes, because nobody
// thinks about their connection in bytes.
const uint32 kBytesPerKilobyte = 1024;


/** Builds a read-only-looking path field with a Browse button beside it.
  * @param field the line edit to place
  * @param browseButton receives the button, so the caller can connect it
  * @param parent the owning widget
  */
QWidget*
MakePathRow(QLineEdit* field, QPushButton** browseButton, QWidget* parent)
{
	QWidget* row = new QWidget(parent);
	QHBoxLayout* layout = new QHBoxLayout(row);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(field, 1);

	*browseButton = new QPushButton(QObject::tr("Browse..."), row);
	(*browseButton)->setAutoDefault(false);
	layout->addWidget(*browseButton);

	return row;
}


}  // unnamed namespace


SettingsDialog::SettingsDialog(ApplicationSettings& settings, QWidget* parent)
	:
	QDialog(parent),
	fSettings(settings)
{
	setWindowTitle(tr("HiTuxShare Settings"));
	setModal(true);

	QVBoxLayout* mainLayout = new QVBoxLayout(this);

	QTabWidget* tabs = new QTabWidget(this);
	tabs->addTab(_BuildIdentityPage(), tr("Identity"));
	tabs->addTab(_BuildFoldersPage(), tr("Folders"));
	tabs->addTab(_BuildTransfersPage(), tr("Transfers"));
	tabs->addTab(_BuildBehaviourPage(), tr("Behaviour"));
	mainLayout->addWidget(tabs);

	QDialogButtonBox* buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	mainLayout->addWidget(buttons);

	_LoadFromSettings();
	resize(560, 380);
}


SettingsDialog::~SettingsDialog()
{
}


QWidget*
SettingsDialog::_BuildIdentityPage()
{
	QWidget* page = new QWidget(this);
	QFormLayout* layout = new QFormLayout(page);

	fUserNameField = new QLineEdit(page);
	layout->addRow(tr("User name:"), fUserNameField);

	fUserStatusField = new QLineEdit(page);
	layout->addRow(tr("Status:"), fUserStatusField);

	fAwayStatusField = new QLineEdit(page);
	layout->addRow(tr("Away message:"), fAwayStatusField);

	QLabel* note = new QLabel(tr("Names are not reserved or authenticated on "
		"this network — anyone can use any name."), page);
	note->setWordWrap(true);
	note->setEnabled(false);
	layout->addRow(note);

	return page;
}


QWidget*
SettingsDialog::_BuildFoldersPage()
{
	QWidget* page = new QWidget(this);
	QFormLayout* layout = new QFormLayout(page);

	fDownloadFolderField = new QLineEdit(page);
	QPushButton* browseDownloads = nullptr;
	layout->addRow(tr("Download to:"),
		MakePathRow(fDownloadFolderField, &browseDownloads, page));
	connect(browseDownloads, &QPushButton::clicked,
		this, &SettingsDialog::_OnBrowseDownloadFolder);

	fShareFolderField = new QLineEdit(page);
	QPushButton* browseShare = nullptr;
	layout->addRow(tr("Share folder:"),
		MakePathRow(fShareFolderField, &browseShare, page));
	connect(browseShare, &QPushButton::clicked,
		this, &SettingsDialog::_OnBrowseShareFolder);

	fFileSharingBox = new QCheckBox(tr("Share the files in that folder"), page);
	layout->addRow(fFileSharingBox);

	fRetainFilePathsBox = new QCheckBox(
		tr("Recreate the sharer's folder structure when downloading"), page);
	layout->addRow(fRetainFilePathsBox);

	QLabel* note = new QLabel(tr("Everything in the share folder is offered to "
		"anyone on the server. A file is identified by its name alone, so the "
		"same name in two sub-folders can only be shared once."), page);
	note->setWordWrap(true);
	note->setEnabled(false);
	layout->addRow(note);

	return page;
}


QWidget*
SettingsDialog::_BuildTransfersPage()
{
	QWidget* page = new QWidget(this);
	QFormLayout* layout = new QFormLayout(page);

	fMaxDownloadsBox = new QSpinBox(page);
	fMaxDownloadsBox->setRange(1, 32);
	layout->addRow(tr("Downloads at once:"), fMaxDownloadsBox);

	fMaxUploadsBox = new QSpinBox(page);
	fMaxUploadsBox->setRange(1, 32);
	layout->addRow(tr("Uploads at once:"), fMaxUploadsBox);

	// Zero reads as "Unlimited" rather than "0 KB/s", which would look like a
	// setting that blocks all transfers.
	fDownloadRateBox = new QSpinBox(page);
	fDownloadRateBox->setRange(0, 1024 * 100);
	fDownloadRateBox->setSuffix(tr(" KB/s"));
	fDownloadRateBox->setSpecialValueText(tr("Unlimited"));
	layout->addRow(tr("Download speed limit:"), fDownloadRateBox);

	fUploadRateBox = new QSpinBox(page);
	fUploadRateBox->setRange(0, 1024 * 100);
	fUploadRateBox->setSuffix(tr(" KB/s"));
	fUploadRateBox->setSpecialValueText(tr("Unlimited"));
	layout->addRow(tr("Upload speed limit:"), fUploadRateBox);

	fFirewalledBox = new QCheckBox(
		tr("I am behind a firewall and cannot accept connections"), page);
	layout->addRow(fFirewalledBox);

	fAutoClearBox = new QCheckBox(
		tr("Remove finished transfers from the list automatically"), page);
	layout->addRow(fAutoClearBox);

	QLabel* note = new QLabel(tr("Speed and connection limits take effect on "
		"the next transfer; running ones keep the limits they started with."),
		page);
	note->setWordWrap(true);
	note->setEnabled(false);
	layout->addRow(note);

	return page;
}


QWidget*
SettingsDialog::_BuildBehaviourPage()
{
	QWidget* page = new QWidget(this);
	QFormLayout* layout = new QFormLayout(page);

	fConnectOnStartupBox = new QCheckBox(
		tr("Connect when HiTuxShare starts"), page);
	layout->addRow(fConnectOnStartupBox);

	fNotificationsBox = new QCheckBox(
		tr("Notify me about private messages, mentions and finished downloads"),
		page);
	layout->addRow(fNotificationsBox);

	fChatFontSizeBox = new QSpinBox(page);
	fChatFontSizeBox->setRange(0, 32);
	fChatFontSizeBox->setSpecialValueText(tr("System default"));
	fChatFontSizeBox->setSuffix(tr(" pt"));
	layout->addRow(tr("Chat font size:"), fChatFontSizeBox);

	return page;
}


void
SettingsDialog::_LoadFromSettings()
{
	fUserNameField->setText(ToQString(fSettings.GetUserName()));
	fUserStatusField->setText(ToQString(fSettings.GetUserStatus()));
	fAwayStatusField->setText(ToQString(fSettings.GetAwayStatus()));

	fDownloadFolderField->setText(ToQString(fSettings.GetDownloadDirectory()));
	fShareFolderField->setText(ToQString(fSettings.GetShareDirectory()));
	fFileSharingBox->setChecked(fSettings.GetFileSharingEnabled());
	fRetainFilePathsBox->setChecked(fSettings.GetRetainFilePaths());

	fMaxDownloadsBox->setValue((int) fSettings.GetMaxSimultaneousDownloads());
	fMaxUploadsBox->setValue((int) fSettings.GetMaxSimultaneousUploads());
	fDownloadRateBox->setValue(
		(int) (fSettings.GetMaxDownloadRate() / kBytesPerKilobyte));
	fUploadRateBox->setValue(
		(int) (fSettings.GetMaxUploadRate() / kBytesPerKilobyte));
	fFirewalledBox->setChecked(fSettings.GetFirewalled());
	fAutoClearBox->setChecked(fSettings.GetAutoClearFinishedTransfers());

	fConnectOnStartupBox->setChecked(fSettings.GetConnectOnStartup());
	fNotificationsBox->setChecked(fSettings.GetNotificationsEnabled());
	fChatFontSizeBox->setValue((int) fSettings.GetChatFontPointSize());
}


void
SettingsDialog::ApplyToSettings()
{
	// An empty name would publish us as a nameless row in everyone's user list,
	// so it is ignored rather than saved.
	const QString userName = fUserNameField->text().trimmed();
	if (userName.isEmpty() == false)
		fSettings.SetUserName(ToMuscleString(userName));

	fSettings.SetUserStatus(ToMuscleString(fUserStatusField->text().trimmed()));
	fSettings.SetAwayStatus(ToMuscleString(fAwayStatusField->text().trimmed()));

	fSettings.SetDownloadDirectory(
		ToMuscleString(fDownloadFolderField->text().trimmed()));
	fSettings.SetShareDirectory(
		ToMuscleString(fShareFolderField->text().trimmed()));
	fSettings.SetFileSharingEnabled(fFileSharingBox->isChecked());
	fSettings.SetRetainFilePaths(fRetainFilePathsBox->isChecked());

	fSettings.SetMaxSimultaneousDownloads((uint32) fMaxDownloadsBox->value());
	fSettings.SetMaxSimultaneousUploads((uint32) fMaxUploadsBox->value());
	fSettings.SetMaxDownloadRate(
		(uint32) fDownloadRateBox->value() * kBytesPerKilobyte);
	fSettings.SetMaxUploadRate(
		(uint32) fUploadRateBox->value() * kBytesPerKilobyte);
	fSettings.SetFirewalled(fFirewalledBox->isChecked());
	fSettings.SetAutoClearFinishedTransfers(fAutoClearBox->isChecked());

	fSettings.SetConnectOnStartup(fConnectOnStartupBox->isChecked());
	fSettings.SetNotificationsEnabled(fNotificationsBox->isChecked());
	fSettings.SetChatFontPointSize((uint32) fChatFontSizeBox->value());
}


void
SettingsDialog::_OnBrowseDownloadFolder()
{
	const QString chosen = QFileDialog::getExistingDirectory(this,
		tr("Choose a folder for downloads"), fDownloadFolderField->text());
	if (chosen.isEmpty() == false)
		fDownloadFolderField->setText(chosen);
}


void
SettingsDialog::_OnBrowseShareFolder()
{
	const QString chosen = QFileDialog::getExistingDirectory(this,
		tr("Choose a folder to share"), fShareFolderField->text());
	if (chosen.isEmpty() == false)
		fShareFolderField->setText(chosen);
}


}  // namespace hitux

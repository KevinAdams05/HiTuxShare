/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "core/HiTuxShareVersion.h"
#include "qt/MainWindow.h"

#include "system/SetupSystem.h"

#include <QApplication>


int
main(int argc, char** argv)
{
	// MUSCLE's object pools, socket layer and logging are set up and torn down by
	// this object's lifetime, so it has to outlive everything that touches them.
	muscle::CompleteSetupSystem setupSystem;

	QApplication application(argc, argv);
	application.setApplicationName(QLatin1String(HITUX_SHARE_NAME));
	application.setApplicationVersion(QLatin1String(HITUX_SHARE_VERSION_STRING));
	application.setOrganizationName(QLatin1String("HiTuxShare"));
	application.setDesktopFileName(QLatin1String("hituxshare"));

	hitux::MainWindow mainWindow;
	mainWindow.show();

	return application.exec();
}

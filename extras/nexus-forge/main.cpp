/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * Based on MystiQ by Maikel Llamaret Heredia (GPLv3)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <QApplication>
#include <QStyleFactory>
#include <QDebug>
#include <QDir>
#include <QSettings>
#include <QMessageBox>
#include "ui/mainwindow.h"
#include "services/paths.h"
#include "services/notification.h"
#include "services/constants.h"

/**
 * @brief Load program constants from constants.xml resource.
 * @return true on success, false on failure
 */
static bool load_constants()
{
    QString constant_xml_filename = QStringLiteral(":/other/constants.xml");

    QFile constant_xml(constant_xml_filename);
    constant_xml.open(QIODevice::ReadOnly);
    if (!constant_xml.isOpen()) {
        qCritical() << "Failed to read file: " << constant_xml_filename;
        QMessageBox::critical(nullptr,
                              QStringLiteral(APP_NAME),
                              QStringLiteral("Cannot load %1. The program will exit now.")
                                  .arg(constant_xml_filename));
        return false;
    }

    qDebug() << "Reading file: " << constant_xml_filename;
    if (!Constants::readFile(constant_xml)) {
        QMessageBox::critical(nullptr,
                              QStringLiteral(APP_NAME),
                              QStringLiteral("%1 contains error(s). "
                                  "Reinstall the application may solve the problem.")
                                  .arg(constant_xml_filename));
        return false;
    }

    return true;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
#endif

    if (!load_constants()) {
        app.exec();
        return EXIT_FAILURE;
    }

    // Register QSettings information
    app.setOrganizationName(QStringLiteral(APP_ORGANIZATION));
    app.setApplicationName(QStringLiteral(APP_NAME));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    if (Constants::getBool("Portable")) {
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           app.applicationDirPath());
    }
    qDebug() << "Settings filename: " + QSettings().fileName();

    Paths::setAppPath(app.applicationDirPath());

    // Construct input file list from command line arguments
    QStringList inputFiles(app.arguments());
    inputFiles.removeFirst(); // exclude executable name

    // Setup notification
    Notification::init();
    if (!Notification::setType(Notification::TYPE_LIBNOTIFY))
        Notification::setType(Notification::TYPE_NOTIFYSEND);

    // Create main window
    MainWindow window(nullptr, inputFiles);
    window.show();

    int status = app.exec();

    Notification::release();

    return status;
}

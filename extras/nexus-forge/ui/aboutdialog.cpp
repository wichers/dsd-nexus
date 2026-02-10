/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <QtGlobal>
#include "aboutdialog.h"
#include "ui_aboutdialog.h"
#include <nexus-forge/version.h>

AboutDialog::AboutDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AboutDialog)
{
    ui->setupUi(this);

    QTextBrowser *info = ui->txtInfo;
    QTextBrowser *license = ui->txtLicense;

    info->setOpenExternalLinks(true);

    info->setText(
         "<h2>Nexus Forge " + QString("%1</h2>").arg(NEXUS_FORGE_VERSION_STRING)
         + tr("Compiled with Qt %1").arg(QT_VERSION_STR)
         + "<br><br>"
         + tr("Nexus Forge is a Qt frontend for DSD audio conversion.")
         + "<br>"
         + tr("It supports SACD ISO, DSF, and DSDIFF formats with conversion "
              "to WAV, FLAC, DSF, and DSDIFF output.")
         + "<br><br>"
         + tr("Based on libdsdpipe, libsacd, libdsdiff, libdsf, and libdsdpcm.")
         + "<br><br>"
         + tr("This program is free software; you can redistribute it and/or modify it "
              "under the terms of the GNU General Public License version 3.")
         );

    license->setText(
         "GNU General Public License version 3\n\n"
         "This program is free software: you can redistribute it and/or modify "
         "it under the terms of the GNU General Public License as published by "
         "the Free Software Foundation, either version 3 of the License, or "
         "(at your option) any later version.\n\n"
         "This program is distributed in the hope that it will be useful, "
         "but WITHOUT ANY WARRANTY; without even the implied warranty of "
         "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the "
         "GNU General Public License for more details.\n\n"
         "You should have received a copy of the GNU General Public License "
         "along with this program. If not, see <https://www.gnu.org/licenses/>."
         );

    // Set background color to match window
    QPalette p = info->palette();
    p.setColor(QPalette::Base, this->palette().color(QPalette::Window));
    info->setPalette(p);
    info->setFrameShape(QTextBrowser::NoFrame);
    license->setPalette(p);
    license->setFrameShape(QTextBrowser::NoFrame);

    this->adjustSize();
    this->setMinimumSize(this->size());
    this->setMaximumSize(this->size());
}

AboutDialog::~AboutDialog()
{
    delete ui;
}

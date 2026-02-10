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
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "convertlist.h"
#include "optionsdialog.h"
#include "aboutdialog.h"
#include "addtaskdialog.h"
#include "extractdialog.h"
#include "services/notification.h"
#include "services/constants.h"
#include "pipeline/dsdpipeparameters.h"
#include <QHBoxLayout>
#include <QMessageBox>
#include <QLabel>
#include <QFileDialog>
#include <QDesktopServices>
#include <QApplication>
#include <QSettings>
#include <QCloseEvent>
#include <QTimer>
#include <QPushButton>
#include <QDebug>
#include <QUrl>
#include <QRegularExpression>
#include <QStandardPaths>

MainWindow::MainWindow(QWidget *parent, const QStringList& fileList) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    m_list(new ConvertList(this)),
    m_argv_input_files(fileList),
    m_elapsedTimeLabel(new QLabel(this)),
    m_timer(new QTimer(this))
{
    ui->setupUi(this);

    connect(m_list, SIGNAL(task_finished(int)),
            this, SLOT(task_finished(int)));
    connect(m_list, SIGNAL(all_tasks_finished()),
            this, SLOT(all_tasks_finished()));
    connect(m_list, SIGNAL(customContextMenuRequested(QPoint)),
            this, SLOT(slotListContextMenu(QPoint)));
    connect(m_list, SIGNAL(itemSelectionChanged()),
            this, SLOT(refresh_action_states()));
    connect(m_timer, SIGNAL(timeout()),
            this, SLOT(timerEvent()));
    connect(m_list, SIGNAL(started()),
            this, SLOT(conversion_started()));
    connect(m_list, SIGNAL(stopped()),
            this, SLOT(conversion_stopped()));
    connect(m_list, SIGNAL(filesDropped(QStringList)),
            this, SLOT(slotFilesDropped(QStringList)));

    setup_widgets();
    setup_menus();
    setup_toolbar(Constants::getSpaceSeparatedList("ToolbarEntries"));
    setup_statusbar();
    setup_appicon();

    load_settings();

    refresh_action_states();

    QTimer::singleShot(0, this, SLOT(window_ready()));
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::window_ready()
{
    if (!m_argv_input_files.isEmpty()) {
        add_files(m_argv_input_files);
    }
    refresh_status();
}

void MainWindow::task_finished(int exitcode)
{
    if (exitcode == 0) { // succeed
        #if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
        Notification::send(this, "Nexus Forge"
                                 , tr("Conversion finished successfully.")
                                 , NotifyLevel::INFO);
        #endif
    } else { // failed
        QMessageBox::critical(this, this->windowTitle()
                              , tr("Conversion failed.")
                              , QMessageBox::Ok);
    }
}

void MainWindow::all_tasks_finished()
{
    Notification::send(this, "Nexus Forge",
                       tr("All tasks have finished."), NotifyLevel::INFO);
    activateWindow(); // notify the user (make taskbar entry blink)
    refresh_action_states();
}

// Menu Events

void MainWindow::slotAddFiles()
{
    add_files();
}

void MainWindow::slotExtractSacd()
{
    ExtractDialog dialog(this);
    dialog.exec();
}

void MainWindow::slotOptions()
{
    OptionsDialog dialog(this);
    dialog.exec();
}

void MainWindow::slotExit()
{
    this->close();
}

void MainWindow::slotStartConversion()
{
    if (m_list->isEmpty()) {
        QMessageBox::information(this, this->windowTitle(),
                                 tr("Nothing to convert."), QMessageBox::Ok);
    } else {
        m_list->start();
    }
}

void MainWindow::slotStopConversion()
{
    m_list->stop();
}

void MainWindow::slotSetConversionParameters()
{
    if (m_list->selectedCount() > 0) {
        m_list->editSelectedParameters();
    }
}

// Open the output folder of the file.
void MainWindow::slotOpenOutputFolder()
{
    const DsdPipeParameters *param = m_list->getCurrentIndexParameter();
    if (param) {
        QString folder_path = param->outputDir;
        if (QFileInfo(folder_path).exists()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(folder_path));
        }
    }
}

void MainWindow::slotAboutQt()
{
    QMessageBox::aboutQt(this);
}

void MainWindow::slotAbout()
{
    AboutDialog(this).exec();
}

void MainWindow::slotOpenSettingFolder()
{
    QSettings settings;
    QString settingsFile = settings.fileName();
    QString settingsDir = QFileInfo(settingsFile).absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(settingsDir));
}

void MainWindow::slotFilesDropped(const QStringList &files)
{
    add_files(files);
}

void MainWindow::slotListContextMenu(QPoint /*pos*/)
{
    refresh_action_states();

    QMenu menu;
    menu.addAction(ui->actionOpenOutputFolder);
    menu.addSeparator();
    menu.addAction(ui->actionRemoveSelectedItems);
    menu.addSeparator();
    menu.addAction(ui->actionRetry);
    menu.addAction(ui->actionRetryAll);
    menu.addSeparator();
    menu.addAction(ui->actionShowErrorMessage);
    menu.addAction(ui->actionChangeOutputFilename);
    menu.addAction(ui->actionChangeOutputDirectory);
    menu.addAction(ui->actionSetParameters);

    menu.exec(QCursor::pos());
}

// Events

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_list->isBusy()) {
        int reply = QMessageBox::warning(this, this->windowTitle(),
                             tr("Conversion is still in progress. Abort?"),
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply == QMessageBox::No) {
            event->ignore();
            return;
        }
    }

    m_list->stop();

    save_settings();
}

void MainWindow::timerEvent()
{
    refresh_status();
}

void MainWindow::conversion_started()
{
    m_elapsedTimeLabel->clear();
    m_timer->start(1000);
    refresh_status();
    refresh_action_states();
}

void MainWindow::conversion_stopped()
{
    m_timer->stop();
    refresh_status();
    refresh_action_states();
}

// Private Methods

void MainWindow::add_files()
{
    QSettings settings;
    QString last_dir = settings.value("addtask/last_source_dir",
                                      QStandardPaths::writableLocation(
                                          QStandardPaths::HomeLocation)).toString();

    QStringList files = QFileDialog::getOpenFileNames(
        this,
        tr("Select DSD Files"),
        last_dir,
        tr("DSD Files (*.iso *.dsf *.dff *.dsdiff);;SACD ISO Images (*.iso);;"
           "DSF Files (*.dsf);;DSDIFF Files (*.dff *.dsdiff);;All Files (*)"));

    if (!files.isEmpty()) {
        settings.setValue("addtask/last_source_dir",
                          QFileInfo(files.first()).absolutePath());
        add_files(files);
    }
}

void MainWindow::add_files(const QStringList &fileList)
{
    for (const QString &file : fileList) {
        AddTaskDialog dialog(this);
        dialog.setSourceFile(file);
        if (dialog.exec() == QDialog::Accepted) {
            QList<DsdPipeParameters> paramList = dialog.allParameters();
            if (!paramList.isEmpty()) {
                m_list->addTasks(paramList);
            }
        }
    }
}

void MainWindow::setup_widgets()
{
    ui->centralWidget->layout()->addWidget(m_list);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);

    this->m_elapsedTimeLabel->clear();
}

void MainWindow::setup_menus()
{
    /* === Menu Events === */

    // File
    connect(ui->actionAddFiles, SIGNAL(triggered()),
            this, SLOT(slotAddFiles()));
    connect(ui->actionExtractSacd, SIGNAL(triggered()),
            this, SLOT(slotExtractSacd()));
    connect(ui->actionOptions, SIGNAL(triggered()),
            this, SLOT(slotOptions()));
    connect(ui->actionExit, SIGNAL(triggered()),
            this, SLOT(slotExit()));

    // Edit
    connect(ui->menuEdit, SIGNAL(aboutToShow()),
            this, SLOT(refresh_action_states()));
    connect(ui->actionRemoveSelectedItems, SIGNAL(triggered()),
            m_list, SLOT(removeSelectedItems()));
    connect(ui->actionRemoveCompletedItems, SIGNAL(triggered()),
            m_list, SLOT(removeCompletedItems()));
    connect(ui->actionClearList, SIGNAL(triggered()),
            m_list, SLOT(clear()));
    connect(ui->actionSetParameters, SIGNAL(triggered()),
            this, SLOT(slotSetConversionParameters()));
    connect(ui->actionOpenOutputFolder, SIGNAL(triggered()),
            this, SLOT(slotOpenOutputFolder()));
    connect(ui->actionOpenSettingFolder, SIGNAL(triggered()),
            this, SLOT(slotOpenSettingFolder()));
    connect(ui->actionChangeOutputFilename, SIGNAL(triggered()),
            m_list, SLOT(changeSelectedOutputFile()));
    connect(ui->actionChangeOutputDirectory, SIGNAL(triggered()),
            m_list, SLOT(changeSelectedOutputDirectory()));
    connect(ui->actionShowErrorMessage, SIGNAL(triggered()),
            m_list, SLOT(showErrorMessage()));

    // Convert
    connect(ui->menuConvert, SIGNAL(aboutToShow()),
            this, SLOT(refresh_action_states()));
    connect(ui->actionStartConversion, SIGNAL(triggered()),
            this, SLOT(slotStartConversion()));
    connect(ui->actionStopConversion, SIGNAL(triggered()),
            this, SLOT(slotStopConversion()));
    connect(ui->actionRetry, SIGNAL(triggered()),
            m_list, SLOT(retrySelectedItems()));
    connect(ui->actionRetry, SIGNAL(triggered()),
            this, SLOT(refresh_action_states()));
    connect(ui->actionRetryAll, SIGNAL(triggered()),
            m_list, SLOT(retryAll()));
    connect(ui->actionRetryAll, SIGNAL(triggered()),
            this, SLOT(refresh_action_states()));

    // Help
    connect(ui->actionAboutQt, SIGNAL(triggered()),
            this, SLOT(slotAboutQt()));
    connect(ui->actionAbout, SIGNAL(triggered()),
            this, SLOT(slotAbout()));
}

void MainWindow::setup_toolbar(const QStringList &entries)
{
    // construct a table of available actions
    // map action name to action pointer
    QMap<QString, QAction*> toolbar_table;
#define ADD_ACTION(name) toolbar_table[QString(#name).toUpper()] = ui->action ## name
    ADD_ACTION(AddFiles);
    ADD_ACTION(ExtractSacd);
    ADD_ACTION(Options);
    ADD_ACTION(Exit);
    ADD_ACTION(RemoveSelectedItems);
    ADD_ACTION(RemoveCompletedItems);
    ADD_ACTION(ClearList);
    ADD_ACTION(OpenOutputFolder);
    ADD_ACTION(OpenSettingFolder);
    ADD_ACTION(SetParameters);
    ADD_ACTION(ChangeOutputFilename);
    ADD_ACTION(ChangeOutputDirectory);
    ADD_ACTION(ShowErrorMessage);
    ADD_ACTION(StartConversion);
    ADD_ACTION(StopConversion);
    ADD_ACTION(Retry);
    ADD_ACTION(RetryAll);
    ADD_ACTION(AboutQt);
    ADD_ACTION(About);

    for (int i=0; i<entries.size(); i++) {
        QString entry = entries[i].toUpper(); // case-insensitive compare
        if (entry == "|") // separator
            ui->toolBar->addSeparator();
        else if (toolbar_table.contains(entry))
            ui->toolBar->addAction(toolbar_table[entry]);
    }
}

void MainWindow::setup_statusbar()
{
    ui->statusBar->addPermanentWidget(m_elapsedTimeLabel);
    ui->statusBar->setSizeGripEnabled(false);
}

// Fill window icon with multiple sizes of images.
void MainWindow::setup_appicon()
{
    QIcon icon;
    QDir iconDir = QDir(":/app/icons/");
    QStringList fileList = iconDir.entryList();
    QRegularExpression pattern("^nexus_forge_[0-9]+x[0-9]+\\.png$");
    foreach (QString file, fileList) {
        if (file.indexOf(pattern) >= 0) {
            icon.addPixmap(QPixmap(iconDir.absoluteFilePath(file)));
        }
    }
    if (!icon.isNull()) {
        setWindowIcon(icon);
    }
}

// Hide unused actions
void MainWindow::refresh_action_states()
{
    int selected_file_count = m_list->selectedCount();

    // Hide actionSetParameters if no item in m_list is selected.
    bool hide_SetParameters = (selected_file_count == 0);

    // Hide actionStartConversion if the conversion is in progress.
    bool hide_StartConversion = m_list->isBusy();

    // Hide actionStopConversion if nothing is being converted.
    bool hide_StopConversion = !m_list->isBusy();

    // Show actionOpenOutputFolder only if 1 file is selected.
    bool hide_OpenFolder = (selected_file_count <= 0);

    // Hide actionRemoveSelectedItems if no file is selected.
    bool hide_RemoveSelectedItems = (selected_file_count == 0);

    bool hide_Retry = (selected_file_count == 0);
    bool hide_RetryAll = (m_list->isEmpty());

    bool hide_ClearList = (m_list->isEmpty());

    bool hide_ChangeOutputFilename = m_list->selectedCount() != 1;
    bool hide_ChangeOutputDirectory = m_list->selectedCount() <= 0;

    /* Show actionShowErrorMessage if and only if one task is selected
       and the state of the selected task is FAILED
     */
    bool hide_ShowErrorMessage = (selected_file_count != 1
                                  || !m_list->selectedTaskFailed());

    ui->actionSetParameters->setDisabled(hide_SetParameters);
    ui->actionStartConversion->setDisabled(hide_StartConversion);
    ui->actionStopConversion->setDisabled(hide_StopConversion);
    ui->actionOpenOutputFolder->setDisabled(hide_OpenFolder);
    ui->actionRemoveSelectedItems->setDisabled(hide_RemoveSelectedItems);
    ui->actionRetry->setDisabled(hide_Retry);
    ui->actionRetryAll->setDisabled(hide_RetryAll);
    ui->actionClearList->setDisabled(hide_ClearList);
    ui->actionChangeOutputFilename->setDisabled(hide_ChangeOutputFilename);
    ui->actionChangeOutputDirectory->setDisabled(hide_ChangeOutputDirectory);
    ui->actionShowErrorMessage->setDisabled(hide_ShowErrorMessage);
}

void MainWindow::load_settings()
{
    QSettings settings;
    restoreGeometry(settings.value("mainwindow/geometry").toByteArray());
    restoreState(settings.value("mainwindow/state").toByteArray());
}

void MainWindow::save_settings()
{
    QSettings settings;
    settings.setValue("mainwindow/geometry", saveGeometry());
    settings.setValue("mainwindow/state", saveState());
}

void MainWindow::refresh_status()
{
    refresh_statusbar();
    refresh_titlebar();
}

void MainWindow::refresh_statusbar()
{
    if (m_list->isBusy()) {
        int total_seconds = m_list->elapsedTime() / 1000;
        int hours = total_seconds / 3600;
        int minutes = (total_seconds / 60) % 60;
        int seconds = total_seconds % 60;

        QString timeinfo = tr("Elapsed Time: %1 h %2 m %3 s")
                .arg(hours).arg(minutes).arg(seconds);
        this->m_elapsedTimeLabel->setText(timeinfo);
    }
}

void MainWindow::refresh_titlebar()
{
    const int task_count = m_list->count();
    const int finished_task_count = m_list->finishedCount();
    if (finished_task_count < task_count && m_list->isBusy()) {
        //: Converting the %1-th file in %2 files. %2 is the number of files.
        setWindowTitle(tr("Nexus Forge - Converting %1/%2")
                       .arg(finished_task_count+1).arg(task_count));
    } else {
        setWindowTitle(tr("Nexus Forge"));
    }
}

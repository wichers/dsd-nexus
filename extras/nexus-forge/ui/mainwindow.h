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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTime>

class ConvertList;

namespace Ui {
    class MainWindow;
}

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /*! Construct the main window
     *  @param parent the parent of the QObject
     *  @param fileList the input files from argv
     */
    explicit MainWindow(QWidget *parent = nullptr, const QStringList& fileList = QStringList());
    ~MainWindow();

private slots:
    void window_ready(); //!< The main window is completely loaded.
    void task_finished(int);
    void all_tasks_finished();

    // Menu Events
    void slotAddFiles();
    void slotExtractSacd();
    void slotOptions();
    void slotExit();
    void slotStartConversion();
    void slotStopConversion();
    void slotSetConversionParameters();
    void slotOpenOutputFolder();
    void slotAboutQt();
    void slotAbout();
    void slotOpenSettingFolder();

    void slotListContextMenu(QPoint);
    void slotFilesDropped(const QStringList &files);

    void refresh_action_states();
    void timerEvent(); ///< 1-second timer event
    void conversion_started();
    void conversion_stopped();

protected:
    void closeEvent(QCloseEvent *);

private:
    Ui::MainWindow *ui;
    ConvertList *m_list;
    const QStringList m_argv_input_files;
    QLabel *m_elapsedTimeLabel;
    QTimer *m_timer;
    void add_files();
    void add_files(const QStringList& files);
    void setup_widgets();
    void setup_menus();
    void setup_toolbar(const QStringList& entries);
    void setup_statusbar();
    void setup_appicon();
    void load_settings();
    void save_settings();
    void refresh_status();
    void refresh_statusbar();
    void refresh_titlebar();
};

#endif // MAINWINDOW_H

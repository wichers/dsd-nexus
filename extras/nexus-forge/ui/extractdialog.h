/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef EXTRACTDIALOG_H
#define EXTRACTDIALOG_H

#include <QDialog>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLineEdit;
class QLabel;
class QProgressBar;
class QPushButton;
class QThread;
QT_END_NAMESPACE

class ExtractWorker;
class Ps3DriveWorker;

/**
 * @brief Dialog for extracting SACD disc images from PS3 drives or network.
 *
 * Input can be a local device path (e.g. D: on Windows, /dev/sr0 on Linux)
 * or a PS3 network address (host:port).
 *
 * Output is a raw ISO image file.
 */
class ExtractDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ExtractDialog(QWidget *parent = nullptr);
    ~ExtractDialog();

private slots:
    void slotBrowseOutput();
    void slotStartExtract();
    void slotCancelExtract();
    void slotProgressUpdated(uint32_t currentSector, uint32_t totalSectors,
                             double speedMBs);
    void slotFinished(int resultCode, const QString &errorMessage);
    void slotAuthenticateDrive();
    void slotPairDrive();
    void slotDriveOperationFinished(int resultCode, const QString &message);

private:
    // Input
    QComboBox *m_cboInputMode;
    QLineEdit *m_editDevicePath;
    QLineEdit *m_editNetworkAddr;

    // Output
    QLineEdit *m_editOutputPath;

    // Progress
    QProgressBar *m_progressBar;
    QLabel *m_lblStatus;
    QLabel *m_lblSpeed;

    // Buttons
    QPushButton *m_btnStart;
    QPushButton *m_btnCancel;
    QPushButton *m_btnClose;
    QPushButton *m_btnAuthenticate;
    QPushButton *m_btnPair;

    // Worker
    QThread *m_thread;
    ExtractWorker *m_worker;
    QThread *m_driveThread;
    Ps3DriveWorker *m_driveWorker;

    void setupUi();
    void setExtracting(bool running);
    void setDriveOperationRunning(bool running);
    void updateDriveButtons();
    QString inputPath() const;
};

#endif // EXTRACTDIALOG_H

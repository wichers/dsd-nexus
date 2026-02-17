/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "extractdialog.h"
#include "pipeline/extractworker.h"
#ifndef SACD_NO_PS3DRIVE
#include "pipeline/ps3driveworker.h"
#endif

#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QStackedWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QThread>

ExtractDialog::ExtractDialog(QWidget *parent)
    : QDialog(parent)
    , m_thread(nullptr)
    , m_worker(nullptr)
#ifndef SACD_NO_PS3DRIVE
    , m_driveThread(nullptr)
    , m_driveWorker(nullptr)
#endif
{
    setupUi();
}

ExtractDialog::~ExtractDialog()
{
    if (m_thread) {
        if (m_worker)
            m_worker->cancel();
        m_thread->quit();
        m_thread->wait(5000);
    }
#ifndef SACD_NO_PS3DRIVE
    if (m_driveThread) {
        m_driveThread->quit();
        m_driveThread->wait(5000);
    }
#endif
}

void ExtractDialog::slotBrowseOutput()
{
    QString startDir = m_editOutputPath->text();
    if (startDir.isEmpty()) {
        startDir = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    }

    QString path = QFileDialog::getSaveFileName(
        this,
        tr("Save SACD ISO Image"),
        startDir,
        tr("ISO Images (*.iso);;All Files (*)"));

    if (!path.isEmpty()) {
        m_editOutputPath->setText(path);
    }
}

void ExtractDialog::slotStartExtract()
{
    QString input = inputPath().trimmed();
    QString output = m_editOutputPath->text().trimmed();

    if (input.isEmpty()) {
        QMessageBox::warning(this, tr("Missing Input"),
                             tr("Please specify a device path or network address."));
        return;
    }

    if (output.isEmpty()) {
        QMessageBox::warning(this, tr("Missing Output"),
                             tr("Please specify an output ISO file path."));
        return;
    }

    setExtracting(true);
    m_lblStatus->setText(tr("Connecting..."));
    m_progressBar->setValue(0);

    /* Create worker and thread */
    m_worker = new ExtractWorker();
    m_thread = new QThread();

    m_worker->moveToThread(m_thread);

    connect(m_worker, &ExtractWorker::progressUpdated,
            this, &ExtractDialog::slotProgressUpdated, Qt::QueuedConnection);
    connect(m_worker, &ExtractWorker::finished,
            this, &ExtractDialog::slotFinished, Qt::QueuedConnection);

    /* Clean up when thread finishes */
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);

    m_thread->start();

    /* Invoke run() on the worker thread */
    QMetaObject::invokeMethod(m_worker, "run",
                              Qt::QueuedConnection,
                              Q_ARG(QString, input),
                              Q_ARG(QString, output));
}

void ExtractDialog::slotCancelExtract()
{
    if (m_worker) {
        m_worker->cancel();
        m_lblStatus->setText(tr("Cancelling..."));
        m_btnCancel->setEnabled(false);
    }
}

void ExtractDialog::slotProgressUpdated(uint32_t currentSector, uint32_t totalSectors,
                                         double speedMBs)
{
    if (totalSectors > 0) {
        int pct = static_cast<int>(
            (static_cast<double>(currentSector) / totalSectors) * 100.0);
        m_progressBar->setValue(pct);

        double sizeMB = static_cast<double>(totalSectors) * 2048.0 / (1024.0 * 1024.0);
        double doneMB = static_cast<double>(currentSector) * 2048.0 / (1024.0 * 1024.0);

        m_lblStatus->setText(tr("Extracting: %1 / %2 MB (%3%)")
                             .arg(doneMB, 0, 'f', 1)
                             .arg(sizeMB, 0, 'f', 1)
                             .arg(pct));

        if (speedMBs > 0.0) {
            m_lblSpeed->setText(tr("%1 MB/s").arg(speedMBs, 0, 'f', 2));
        }
    }
}

void ExtractDialog::slotFinished(int resultCode, const QString &errorMessage)
{
    setExtracting(false);

    /* Detach worker/thread pointers (they will self-delete via deleteLater) */
    m_worker = nullptr;
    m_thread = nullptr;

    if (resultCode == 0) {
        m_progressBar->setValue(100);
        m_lblStatus->setText(tr("Extraction complete."));
        m_lblSpeed->clear();

        QMessageBox::information(this, tr("Extraction Complete"),
                                 tr("SACD image extracted successfully to:\n%1")
                                 .arg(m_editOutputPath->text()));
    } else {
        m_progressBar->setValue(0);
        m_lblStatus->setText(tr("Extraction failed."));
        m_lblSpeed->clear();

        QMessageBox::critical(this, tr("Extraction Failed"), errorMessage);
    }
}

void ExtractDialog::setupUi()
{
    setWindowTitle(tr("Extract SACD"));
    setMinimumWidth(500);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);

    // Input source
    {
        QGroupBox *grp = new QGroupBox(tr("Input Source"), this);
        QVBoxLayout *grpLay = new QVBoxLayout(grp);

        QHBoxLayout *modeRow = new QHBoxLayout();
        modeRow->addWidget(new QLabel(tr("Mode:"), grp));
        m_cboInputMode = new QComboBox(grp);
        m_cboInputMode->addItem(tr("Device (PS3 Drive)"), 0);
        m_cboInputMode->addItem(tr("Network (PS3 Server)"), 1);
        modeRow->addWidget(m_cboInputMode, 1);
        grpLay->addLayout(modeRow);

        QFormLayout *inputForm = new QFormLayout();

        m_editDevicePath = new QLineEdit(grp);
#ifdef Q_OS_WIN
        m_editDevicePath->setPlaceholderText(tr("e.g. D: or \\\\.\\CdRom0"));
#else
        m_editDevicePath->setPlaceholderText(tr("e.g. /dev/sr0"));
#endif
        inputForm->addRow(tr("Device:"), m_editDevicePath);

        m_editNetworkAddr = new QLineEdit(grp);
        m_editNetworkAddr->setPlaceholderText(tr("e.g. 192.168.1.100:2002"));
        inputForm->addRow(tr("Address:"), m_editNetworkAddr);

        grpLay->addLayout(inputForm);

#ifndef SACD_NO_PS3DRIVE
        QHBoxLayout *driveRow = new QHBoxLayout();
        m_btnAuthenticate = new QPushButton(tr("Authenticate Drive"), grp);
        m_btnPair = new QPushButton(tr("Pair Drive"), grp);
        driveRow->addWidget(m_btnAuthenticate);
        driveRow->addWidget(m_btnPair);
        driveRow->addStretch();
        grpLay->addLayout(driveRow);
#endif

        mainLayout->addWidget(grp);

        // Enable/disable fields based on mode, clear the inactive field
        auto updateInputMode = [this](int index) {
            bool isDevice = (index == 0);
            m_editDevicePath->setEnabled(isDevice);
            m_editNetworkAddr->setEnabled(!isDevice);
            if (isDevice)
                m_editNetworkAddr->clear();
            else
                m_editDevicePath->clear();
#ifndef SACD_NO_PS3DRIVE
            updateDriveButtons();
#endif
        };

        connect(m_cboInputMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, updateInputMode);
#ifndef SACD_NO_PS3DRIVE
        connect(m_editDevicePath, &QLineEdit::textChanged,
                this, &ExtractDialog::updateDriveButtons);
#endif

#ifndef SACD_NO_PS3DRIVE
        connect(m_btnAuthenticate, &QPushButton::clicked,
                this, &ExtractDialog::slotAuthenticateDrive);
        connect(m_btnPair, &QPushButton::clicked,
                this, &ExtractDialog::slotPairDrive);
#endif

        // Default: device mode - network address disabled
        m_editNetworkAddr->setEnabled(false);
#ifndef SACD_NO_PS3DRIVE
        updateDriveButtons();
#endif
    }

    // Output
    {
        QGroupBox *grp = new QGroupBox(tr("Output"), this);
        QHBoxLayout *outRow = new QHBoxLayout(grp);
        outRow->addWidget(new QLabel(tr("ISO File:"), grp));
        m_editOutputPath = new QLineEdit(grp);
        m_editOutputPath->setPlaceholderText(tr("Select output path..."));
        QPushButton *btnBrowse = new QPushButton(tr("Browse..."), grp);
        outRow->addWidget(m_editOutputPath, 1);
        outRow->addWidget(btnBrowse);
        mainLayout->addWidget(grp);

        connect(btnBrowse, &QPushButton::clicked,
                this, &ExtractDialog::slotBrowseOutput);
    }

    // Progress
    {
        QGroupBox *grp = new QGroupBox(tr("Progress"), this);
        QVBoxLayout *progLay = new QVBoxLayout(grp);

        m_progressBar = new QProgressBar(grp);
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(0);
        progLay->addWidget(m_progressBar);

        QHBoxLayout *statusRow = new QHBoxLayout();
        m_lblStatus = new QLabel(grp);
        m_lblSpeed = new QLabel(grp);
        m_lblSpeed->setAlignment(Qt::AlignRight);
        statusRow->addWidget(m_lblStatus, 1);
        statusRow->addWidget(m_lblSpeed);
        progLay->addLayout(statusRow);

        mainLayout->addWidget(grp);
    }

    // Buttons
    {
        QHBoxLayout *btnRow = new QHBoxLayout();
        btnRow->addStretch();

        m_btnStart = new QPushButton(tr("Start Extraction"), this);
        m_btnStart->setDefault(true);
        m_btnCancel = new QPushButton(tr("Cancel"), this);
        m_btnCancel->setEnabled(false);
        m_btnClose = new QPushButton(tr("Close"), this);

        btnRow->addWidget(m_btnStart);
        btnRow->addWidget(m_btnCancel);
        btnRow->addWidget(m_btnClose);

        mainLayout->addLayout(btnRow);

        connect(m_btnStart, &QPushButton::clicked,
                this, &ExtractDialog::slotStartExtract);
        connect(m_btnCancel, &QPushButton::clicked,
                this, &ExtractDialog::slotCancelExtract);
        connect(m_btnClose, &QPushButton::clicked,
                this, &QDialog::reject);
    }
}

void ExtractDialog::setExtracting(bool running)
{
    m_btnStart->setEnabled(!running);
    m_btnCancel->setEnabled(running);
    m_cboInputMode->setEnabled(!running);
    m_editDevicePath->setEnabled(!running);
    m_editNetworkAddr->setEnabled(!running);
    m_editOutputPath->setEnabled(!running);
#ifndef SACD_NO_PS3DRIVE
    m_btnAuthenticate->setEnabled(!running);
    m_btnPair->setEnabled(!running);
    if (!running)
        updateDriveButtons();
#endif
}

QString ExtractDialog::inputPath() const
{
    if (m_cboInputMode->currentData().toInt() == 0) {
        return m_editDevicePath->text();
    } else {
        return m_editNetworkAddr->text();
    }
}

#ifndef SACD_NO_PS3DRIVE
void ExtractDialog::updateDriveButtons()
{
    bool isDevice = (m_cboInputMode->currentData().toInt() == 0);
    bool hasPath = !m_editDevicePath->text().trimmed().isEmpty();
    bool enabled = isDevice && hasPath && !m_driveWorker;
    m_btnAuthenticate->setEnabled(enabled);
    m_btnPair->setEnabled(enabled);
}

void ExtractDialog::setDriveOperationRunning(bool running)
{
    m_btnStart->setEnabled(!running);
    m_cboInputMode->setEnabled(!running);
    m_editDevicePath->setEnabled(!running);
    m_editNetworkAddr->setEnabled(!running);
    m_editOutputPath->setEnabled(!running);
    m_btnAuthenticate->setEnabled(!running);
    m_btnPair->setEnabled(!running);
}

void ExtractDialog::slotAuthenticateDrive()
{
    QString device = m_editDevicePath->text().trimmed();
    if (device.isEmpty()) {
        QMessageBox::warning(this, tr("Missing Device"),
                             tr("Please specify a device path."));
        return;
    }

    setDriveOperationRunning(true);
    m_lblStatus->setText(tr("Authenticating..."));

    m_driveWorker = new Ps3DriveWorker();
    m_driveThread = new QThread();

    m_driveWorker->moveToThread(m_driveThread);

    connect(m_driveWorker, &Ps3DriveWorker::finished,
            this, &ExtractDialog::slotDriveOperationFinished, Qt::QueuedConnection);
    connect(m_driveThread, &QThread::finished, m_driveWorker, &QObject::deleteLater);
    connect(m_driveThread, &QThread::finished, m_driveThread, &QObject::deleteLater);

    m_driveThread->start();

    QMetaObject::invokeMethod(m_driveWorker, "authenticate",
                              Qt::QueuedConnection,
                              Q_ARG(QString, device));
}

void ExtractDialog::slotPairDrive()
{
    QString device = m_editDevicePath->text().trimmed();
    if (device.isEmpty()) {
        QMessageBox::warning(this, tr("Missing Device"),
                             tr("Please specify a device path."));
        return;
    }

    QMessageBox::StandardButton reply = QMessageBox::warning(
        this, tr("Pair Drive"),
        tr("You are about to pair the drive at \"%1\".\n\n"
           "This operation writes cryptographic data to the drive. "
           "Only proceed if you know what you are doing.\n\n"
           "Continue?").arg(device),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (reply != QMessageBox::Yes)
        return;

    setDriveOperationRunning(true);
    m_lblStatus->setText(tr("Pairing drive..."));

    m_driveWorker = new Ps3DriveWorker();
    m_driveThread = new QThread();

    m_driveWorker->moveToThread(m_driveThread);

    connect(m_driveWorker, &Ps3DriveWorker::finished,
            this, &ExtractDialog::slotDriveOperationFinished, Qt::QueuedConnection);
    connect(m_driveThread, &QThread::finished, m_driveWorker, &QObject::deleteLater);
    connect(m_driveThread, &QThread::finished, m_driveThread, &QObject::deleteLater);

    m_driveThread->start();

    QMetaObject::invokeMethod(m_driveWorker, "pair",
                              Qt::QueuedConnection,
                              Q_ARG(QString, device));
}

void ExtractDialog::slotDriveOperationFinished(int resultCode, const QString &message)
{
    setDriveOperationRunning(false);

    m_driveWorker = nullptr;
    m_driveThread = nullptr;

    if (resultCode == 0) {
        m_lblStatus->setText(message);
        QMessageBox::information(this, tr("Success"), message);
    } else {
        m_lblStatus->setText(tr("Operation failed."));
        QMessageBox::critical(this, tr("Error"), message);
    }
}
#endif /* !SACD_NO_PS3DRIVE */

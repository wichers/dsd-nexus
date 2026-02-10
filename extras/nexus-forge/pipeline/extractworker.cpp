/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "extractworker.h"

#include <libsacd/sacd.h>

#include <QFile>
#include <QElapsedTimer>
#include <QDebug>

#define SACD_SECTOR_SIZE     2048
#define SECTORS_PER_READ     256

ExtractWorker::ExtractWorker(QObject *parent)
    : QObject(parent)
    , m_cancelled(false)
{
}

ExtractWorker::~ExtractWorker()
{
}

void ExtractWorker::cancel()
{
    m_cancelled.store(true);
}

void ExtractWorker::run(const QString &inputPath, const QString &outputPath)
{
    m_cancelled.store(false);

    QByteArray inputUtf8 = inputPath.toUtf8();
    const char *input_str = inputUtf8.constData();

    /* Create SACD reader */
    sacd_t *reader = sacd_create();
    if (!reader) {
        emit finished(-1, tr("Failed to create SACD reader."));
        return;
    }

    /* Initialize (opens device/network, performs authentication) */
    int rc = sacd_init(reader, input_str, 1, 1);
    if (rc != 0) {
        sacd_destroy(reader);
        emit finished(-1, tr("Failed to initialize SACD reader for \"%1\".\n"
                             "Check that the device path or network address is correct.")
                       .arg(inputPath));
        return;
    }

    /* Get total sectors */
    uint32_t total_sectors = 0;
    rc = sacd_get_total_sectors(reader, &total_sectors);
    if (rc != 0 || total_sectors == 0) {
        sacd_close(reader);
        sacd_destroy(reader);
        emit finished(-1, tr("Failed to read disc size."));
        return;
    }

    /* Open output file */
    QFile outputFile(outputPath);
    if (!outputFile.open(QIODevice::WriteOnly)) {
        sacd_close(reader);
        sacd_destroy(reader);
        emit finished(-1, tr("Failed to create output file \"%1\":\n%2")
                       .arg(outputPath, outputFile.errorString()));
        return;
    }

    /* Allocate read buffer */
    QByteArray buffer(SECTORS_PER_READ * SACD_SECTOR_SIZE, '\0');
    uint8_t *buf = reinterpret_cast<uint8_t *>(buffer.data());

    /* Extraction loop */
    uint32_t sectors_done = 0;
    bool success = true;
    QString errorMsg;
    QElapsedTimer progressTimer;
    QElapsedTimer speedTimer;
    progressTimer.start();
    speedTimer.start();
    uint64_t bytes_at_last_report = 0;

    while (sectors_done < total_sectors) {
        if (m_cancelled.load()) {
            errorMsg = tr("Extraction cancelled.");
            success = false;
            break;
        }

        uint32_t remaining = total_sectors - sectors_done;
        uint32_t to_read = (remaining > SECTORS_PER_READ) ? SECTORS_PER_READ : remaining;

        uint32_t sectors_read = 0;
        rc = sacd_read_raw_sectors(reader, sectors_done, to_read, buf, &sectors_read);
        if (rc != 0 || sectors_read == 0) {
            errorMsg = tr("Read error at sector %1.").arg(sectors_done);
            success = false;
            break;
        }

        qint64 bytes_to_write = static_cast<qint64>(sectors_read) * SACD_SECTOR_SIZE;
        qint64 written = outputFile.write(reinterpret_cast<const char *>(buf), bytes_to_write);
        if (written != bytes_to_write) {
            errorMsg = tr("Write error: %1").arg(outputFile.errorString());
            success = false;
            break;
        }

        sectors_done += sectors_read;

        /* Report progress every 500ms */
        if (progressTimer.elapsed() >= 500) {
            uint64_t bytes_done = static_cast<uint64_t>(sectors_done) * SACD_SECTOR_SIZE;
            double elapsed_sec = speedTimer.elapsed() / 1000.0;
            uint64_t bytes_delta = bytes_done - bytes_at_last_report;
            double speed = (elapsed_sec > 0.0)
                ? (static_cast<double>(bytes_delta) / (1024.0 * 1024.0))
                  / (progressTimer.elapsed() / 1000.0)
                : 0.0;

            emit progressUpdated(sectors_done, total_sectors, speed);

            bytes_at_last_report = bytes_done;
            progressTimer.restart();
        }
    }

    /* Close everything */
    outputFile.close();
    sacd_close(reader);
    sacd_destroy(reader);

    if (!success) {
        /* Remove incomplete output file */
        QFile::remove(outputPath);
        emit finished(-1, errorMsg);
    } else {
        /* Final progress update */
        emit progressUpdated(total_sectors, total_sectors, 0.0);
        emit finished(0, QString());
    }
}

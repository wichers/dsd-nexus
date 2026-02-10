/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef EXTRACTWORKER_H
#define EXTRACTWORKER_H

#include <QObject>
#include <QString>
#include <atomic>

/**
 * @brief Worker object that performs raw SACD extraction on a background thread.
 *
 * This object must be moved to a QThread via moveToThread().
 * Call run() via QMetaObject::invokeMethod after the thread has started.
 *
 * Reads raw sectors from a PS3 drive (device path) or PS3 network server
 * (host:port) and writes them to an ISO file.
 */
class ExtractWorker : public QObject
{
    Q_OBJECT
public:
    explicit ExtractWorker(QObject *parent = nullptr);
    ~ExtractWorker();

    void cancel();

signals:
    /** @brief Emitted periodically during extraction */
    void progressUpdated(uint32_t currentSector, uint32_t totalSectors,
                         double speedMBs);

    /** @brief Emitted when extraction completes or fails */
    void finished(int resultCode, const QString &errorMessage);

public slots:
    /** @brief Start extraction (call via QMetaObject::invokeMethod) */
    void run(const QString &inputPath, const QString &outputPath);

private:
    std::atomic<bool> m_cancelled;
};

#endif // EXTRACTWORKER_H

/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef PS3DRIVEWORKER_H
#define PS3DRIVEWORKER_H

#include <QObject>
#include <QString>

/**
 * @brief Worker object that performs PS3 drive operations on a background thread.
 *
 * This object must be moved to a QThread via moveToThread().
 * Call authenticate() or pair() via QMetaObject::invokeMethod after the thread
 * has started.
 */
class Ps3DriveWorker : public QObject
{
    Q_OBJECT
public:
    explicit Ps3DriveWorker(QObject *parent = nullptr);
    ~Ps3DriveWorker();

signals:
    /** @brief Emitted when the operation completes or fails */
    void finished(int resultCode, const QString &message);

public slots:
    /** @brief Authenticate with the PS3 BD drive */
    void authenticate(const QString &devicePath);

    /** @brief Pair the PS3 drive with default pairing data */
    void pair(const QString &devicePath);
};

#endif // PS3DRIVEWORKER_H

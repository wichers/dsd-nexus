/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "ps3driveworker.h"

#include <libps3drive/ps3drive.h>

Ps3DriveWorker::Ps3DriveWorker(QObject *parent)
    : QObject(parent)
{
}

Ps3DriveWorker::~Ps3DriveWorker()
{
}

void Ps3DriveWorker::authenticate(const QString &devicePath)
{
    QByteArray pathUtf8 = devicePath.toUtf8();
    const char *path = pathUtf8.constData();

    ps3drive_t *handle = nullptr;
    ps3drive_error_t err = ps3drive_open(path, &handle);
    if (err != PS3DRIVE_OK) {
        emit finished(-1, tr("Failed to open drive \"%1\": %2")
                       .arg(devicePath, ps3drive_error_string(err)));
        return;
    }

    err = ps3drive_authenticate(handle);
    if (err != PS3DRIVE_OK) {
        QString detail = QString::fromUtf8(ps3drive_get_error(handle));
        ps3drive_close(handle);
        emit finished(-1, tr("BD authentication failed: %1")
                       .arg(detail.isEmpty()
                            ? QString::fromUtf8(ps3drive_error_string(err))
                            : detail));
        return;
    }

    ps3drive_close(handle);
    emit finished(0, tr("BD authentication successful."));
}

void Ps3DriveWorker::pair(const QString &devicePath)
{
    QByteArray pathUtf8 = devicePath.toUtf8();
    const char *path = pathUtf8.constData();

    ps3drive_pairing_ctx_t *ctx = nullptr;
    ps3drive_error_t err = ps3drive_pairing_create_default(&ctx);
    if (err != PS3DRIVE_OK) {
        emit finished(-1, tr("Failed to create pairing context: %1")
                       .arg(ps3drive_error_string(err)));
        return;
    }

    ps3drive_t *handle = nullptr;
    err = ps3drive_open(path, &handle);
    if (err != PS3DRIVE_OK) {
        ps3drive_pairing_free(ctx);
        emit finished(-1, tr("Failed to open drive \"%1\": %2")
                       .arg(devicePath, ps3drive_error_string(err)));
        return;
    }

    err = ps3drive_pair(handle, ctx);
    if (err != PS3DRIVE_OK) {
        QString detail = QString::fromUtf8(ps3drive_get_error(handle));
        ps3drive_close(handle);
        ps3drive_pairing_free(ctx);
        emit finished(-1, tr("Drive pairing failed: %1")
                       .arg(detail.isEmpty()
                            ? QString::fromUtf8(ps3drive_error_string(err))
                            : detail));
        return;
    }

    ps3drive_close(handle);
    ps3drive_pairing_free(ctx);
    emit finished(0, tr("Drive pairing completed successfully."));
}

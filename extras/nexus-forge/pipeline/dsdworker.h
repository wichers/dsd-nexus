/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DSDWORKER_H
#define DSDWORKER_H

#include <QObject>
#include "dsdpipeparameters.h"

struct dsdpipe_s;
typedef struct dsdpipe_s dsdpipe_t;

/**
 * @brief Worker object that runs dsdpipe_run() on a background thread.
 *
 * This object must be moved to a QThread via moveToThread().
 * Call run() via QMetaObject::invokeMethod or signal-slot after the
 * thread has started.
 *
 * The progress callback bridges from the C callback to Qt signals
 * using QMetaObject::invokeMethod(Qt::QueuedConnection).
 */
class DsdWorker : public QObject
{
    Q_OBJECT
public:
    explicit DsdWorker(QObject *parent = nullptr);
    ~DsdWorker();

    void cancel();

signals:
    void progressUpdated(int trackNum, int trackTotal,
                         float trackPct, float totalPct,
                         const QString &title, const QString &sink);
    void finished(int resultCode, const QString &errorMessage);

public slots:
    void run(DsdPipeParameters param);

private:
    dsdpipe_t *m_pipe;
    bool m_cancelled;

    int configurePipeline(const DsdPipeParameters &param);

    static int progressCallback(const struct dsdpipe_progress_s *progress,
                                    void *userdata);
};

#endif // DSDWORKER_H

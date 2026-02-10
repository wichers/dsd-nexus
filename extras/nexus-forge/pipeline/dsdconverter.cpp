/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "dsdconverter.h"
#include "dsdworker.h"
#include "dsdpipeparameters.h"

#include <QThread>
#include <QDebug>

DsdConverter::DsdConverter(QObject *parent)
    : QObject(parent)
    , m_thread(nullptr)
    , m_worker(nullptr)
    , m_progress(0.0)
    , m_running(false)
{
}

DsdConverter::~DsdConverter()
{
    stop();
}

bool DsdConverter::start(const DsdPipeParameters &param)
{
    if (m_running)
        return false;

    m_progress = 0.0;
    m_errorMessage.clear();
    m_running = true;

    /* Create worker and thread */
    m_thread = new QThread(this);
    m_worker = new DsdWorker(); /* no parent - will be moved to thread */

    m_worker->moveToThread(m_thread);

    /* Wire signals */
    connect(m_worker, &DsdWorker::progressUpdated,
            this, &DsdConverter::onWorkerProgress, Qt::QueuedConnection);
    connect(m_worker, &DsdWorker::finished,
            this, &DsdConverter::onWorkerFinished, Qt::QueuedConnection);

    /* Clean up thread and worker when done */
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);

    /* Start the thread */
    m_thread->start();

    /* Invoke run() on the worker (in its thread) */
    QMetaObject::invokeMethod(m_worker, "run",
                              Qt::QueuedConnection,
                              Q_ARG(DsdPipeParameters, param));

    return true;
}

void DsdConverter::stop()
{
    if (!m_running)
        return;

    if (m_worker) {
        m_worker->cancel();
    }

    if (m_thread && m_thread->isRunning()) {
        m_thread->quit();
        m_thread->wait(5000); /* wait up to 5 seconds */
    }

    m_running = false;
    m_thread = nullptr;
    m_worker = nullptr;
}

bool DsdConverter::isRunning() const
{
    return m_running;
}

double DsdConverter::progress() const
{
    return m_progress;
}

QString DsdConverter::errorMessage() const
{
    return m_errorMessage;
}

void DsdConverter::onWorkerProgress(int trackNum, int trackTotal,
                                     float trackPct, float totalPct,
                                     const QString &title, const QString &sink)
{
    m_progress = static_cast<double>(totalPct);
    emit progressRefreshed(static_cast<int>(totalPct));
    emit trackProgress(trackNum, trackTotal, trackPct, totalPct, title, sink);
}

void DsdConverter::onWorkerFinished(int resultCode, const QString &errMsg)
{
    m_running = false;
    m_errorMessage = errMsg;

    /* Clean up the thread */
    if (m_thread && m_thread->isRunning()) {
        m_thread->quit();
    }
    m_thread = nullptr;
    m_worker = nullptr;

    emit finished(resultCode == 0 ? 0 : 1);
}

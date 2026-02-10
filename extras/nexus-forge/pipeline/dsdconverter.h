/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DSDCONVERTER_H
#define DSDCONVERTER_H

#include <QObject>

class QThread;
class DsdWorker;
struct DsdPipeParameters;

/**
 * @brief High-level DSD conversion controller
 *
 * Owns a worker thread that runs dsdpipe_run(). Provides start/stop/progress
 * interface for the ConvertList queue system.
 */
class DsdConverter : public QObject
{
    Q_OBJECT
public:
    explicit DsdConverter(QObject *parent = nullptr);
    ~DsdConverter();

    bool start(const DsdPipeParameters &param);
    void stop();
    bool isRunning() const;
    double progress() const;
    QString errorMessage() const;

signals:
    void finished(int exitcode);
    void progressRefreshed(int percentage);
    void trackProgress(int trackNum, int trackTotal,
                       float trackPct, float totalPct,
                       const QString &title, const QString &sink);

private slots:
    void onWorkerProgress(int trackNum, int trackTotal,
                          float trackPct, float totalPct,
                          const QString &title, const QString &sink);
    void onWorkerFinished(int resultCode, const QString &errMsg);

private:
    QThread *m_thread;
    DsdWorker *m_worker;
    double m_progress;
    QString m_errorMessage;
    bool m_running;
};

#endif // DSDCONVERTER_H

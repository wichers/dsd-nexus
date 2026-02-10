/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DSDPROBE_H
#define DSDPROBE_H

#include <QObject>
#include <QString>
#include <cstdint>

/**
 * @brief Wraps libdsdpipe metadata probing
 *
 * Opens a DSD source file (SACD ISO, DSF, DSDIFF) and reads album/track
 * metadata without performing any conversion.
 */
class DsdProbe : public QObject
{
    Q_OBJECT
public:
    struct TrackInfo {
        int number;
        QString title;
        QString performer;
        double durationSeconds;
        QString isrc;
    };

    explicit DsdProbe(QObject *parent = nullptr);
    ~DsdProbe();

    bool probe(const QString &path, int channelType = 0);
    void close();
    bool isProbed() const;

    // Source info
    int sourceType() const;
    QString sourceTypeString() const;
    bool isSacd() const;

    // Audio format
    uint32_t sampleRate() const;
    uint16_t channelCount() const;
    QString channelConfigString() const;
    QString dsdRateString() const;

    // Track info
    int trackCount() const;
    TrackInfo trackInfo(int trackNumber) const;

    // Album metadata
    QString albumTitle() const;
    QString albumArtist() const;
    uint16_t year() const;
    QString genre() const;

private:
    struct Private;
    Private *d;
};

#endif // DSDPROBE_H

/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "dsdprobe.h"

#include <libdsdpipe/dsdpipe.h>

#include <QFileInfo>
#include <QVector>

struct DsdProbe::Private {
    dsdpipe_t *pipe = nullptr;
    bool probed = false;
    dsdpipe_source_type_t srcType = DSDPIPE_SOURCE_NONE;
    dsdpipe_format_t format = {};
    QVector<TrackInfo> tracks;
    QString albumTitleStr;
    QString albumArtistStr;
    QString genreStr;
    uint16_t albumYear = 0;
};

DsdProbe::DsdProbe(QObject *parent)
    : QObject(parent)
    , d(new Private)
{
}

DsdProbe::~DsdProbe()
{
    close();
    delete d;
}

bool DsdProbe::probe(const QString &path, int channelType)
{
    /* Close any previous probe session */
    close();

    /* Determine source type from file extension */
    QFileInfo fi(path);
    QString ext = fi.suffix().toLower();

    dsdpipe_source_type_t src_type = DSDPIPE_SOURCE_NONE;
    if (ext == QStringLiteral("iso")) {
        src_type = DSDPIPE_SOURCE_SACD;
    } else if (ext == QStringLiteral("dsf")) {
        src_type = DSDPIPE_SOURCE_DSF;
    } else if (ext == QStringLiteral("dff") || ext == QStringLiteral("dsdiff")) {
        src_type = DSDPIPE_SOURCE_DSDIFF;
    } else {
        return false;
    }

    /* Create the pipeline */
    dsdpipe_t *pipe = dsdpipe_create();
    if (!pipe) {
        return false;
    }

    /* Convert path to UTF-8 for the C API */
    QByteArray path_utf8 = path.toUtf8();
    const char *path_str = path_utf8.constData();

    /* Set the source */
    int rc = DSDPIPE_OK;
    switch (src_type) {
    case DSDPIPE_SOURCE_SACD: {
        dsdpipe_channel_type_t ch = (channelType == 1)
            ? DSDPIPE_CHANNEL_MULTICHANNEL
            : DSDPIPE_CHANNEL_STEREO;
        rc = dsdpipe_set_source_sacd(pipe, path_str, ch);
        break;
    }
    case DSDPIPE_SOURCE_DSDIFF:
        rc = dsdpipe_set_source_dsdiff(pipe, path_str);
        break;
    case DSDPIPE_SOURCE_DSF:
        rc = dsdpipe_set_source_dsf(pipe, path_str);
        break;
    default:
        dsdpipe_destroy(pipe);
        return false;
    }

    if (rc != DSDPIPE_OK) {
        dsdpipe_destroy(pipe);
        return false;
    }

    /* Read source format */
    dsdpipe_format_t format = {};
    rc = dsdpipe_get_source_format(pipe, &format);
    if (rc != DSDPIPE_OK) {
        dsdpipe_destroy(pipe);
        return false;
    }

    /* Read track count */
    uint8_t track_count = 0;
    rc = dsdpipe_get_track_count(pipe, &track_count);
    if (rc != DSDPIPE_OK) {
        dsdpipe_destroy(pipe);
        return false;
    }

    /* Read album metadata */
    dsdpipe_metadata_t album_meta;
    dsdpipe_metadata_init(&album_meta);
    rc = dsdpipe_get_album_metadata(pipe, &album_meta);
    if (rc != DSDPIPE_OK) {
        dsdpipe_metadata_free(&album_meta);
        dsdpipe_destroy(pipe);
        return false;
    }

    /* Read per-track metadata */
    QVector<TrackInfo> tracks;
    tracks.reserve(track_count);

    for (uint8_t i = 1; i <= track_count; ++i) {
        dsdpipe_metadata_t track_meta;
        dsdpipe_metadata_init(&track_meta);

        rc = dsdpipe_get_track_metadata(pipe, i, &track_meta);
        if (rc != DSDPIPE_OK) {
            dsdpipe_metadata_free(&track_meta);
            dsdpipe_metadata_free(&album_meta);
            dsdpipe_destroy(pipe);
            return false;
        }

        TrackInfo info;
        info.number = static_cast<int>(i);
        info.title = track_meta.track_title
                         ? QString::fromUtf8(track_meta.track_title)
                         : QString();
        info.performer = track_meta.track_performer
                             ? QString::fromUtf8(track_meta.track_performer)
                             : QString();
        info.durationSeconds = track_meta.duration_seconds;
        info.isrc = QString::fromUtf8(track_meta.isrc);

        tracks.append(info);
        dsdpipe_metadata_free(&track_meta);
    }

    /* Store everything in the Private struct */
    d->pipe = pipe;
    d->probed = true;
    d->srcType = src_type;
    d->format = format;
    d->tracks = tracks;
    d->albumTitleStr = album_meta.album_title
                           ? QString::fromUtf8(album_meta.album_title)
                           : QString();
    d->albumArtistStr = album_meta.album_artist
                            ? QString::fromUtf8(album_meta.album_artist)
                            : QString();
    d->genreStr = album_meta.genre
                      ? QString::fromUtf8(album_meta.genre)
                      : QString();
    d->albumYear = album_meta.year;

    dsdpipe_metadata_free(&album_meta);
    return true;
}

void DsdProbe::close()
{
    if (d->pipe) {
        dsdpipe_destroy(d->pipe);
        d->pipe = nullptr;
    }
    d->probed = false;
    d->srcType = DSDPIPE_SOURCE_NONE;
    d->format = {};
    d->tracks.clear();
    d->albumTitleStr.clear();
    d->albumArtistStr.clear();
    d->genreStr.clear();
    d->albumYear = 0;
}

bool DsdProbe::isProbed() const
{
    return d->probed;
}

int DsdProbe::sourceType() const
{
    return static_cast<int>(d->srcType);
}

QString DsdProbe::sourceTypeString() const
{
    switch (d->srcType) {
    case DSDPIPE_SOURCE_SACD:   return QStringLiteral("SACD ISO");
    case DSDPIPE_SOURCE_DSDIFF: return QStringLiteral("DSDIFF");
    case DSDPIPE_SOURCE_DSF:    return QStringLiteral("DSF");
    default:                    return QStringLiteral("Unknown");
    }
}

bool DsdProbe::isSacd() const
{
    return d->srcType == DSDPIPE_SOURCE_SACD;
}

uint32_t DsdProbe::sampleRate() const
{
    return d->format.sample_rate;
}

uint16_t DsdProbe::channelCount() const
{
    return d->format.channel_count;
}

QString DsdProbe::channelConfigString() const
{
    if (!d->probed) {
        return QStringLiteral("Unknown");
    }
    const char *config = dsdpipe_get_speaker_config_string(&d->format);
    if (config) {
        return QString::fromUtf8(config);
    }
    return QStringLiteral("%1 channels").arg(d->format.channel_count);
}

QString DsdProbe::dsdRateString() const
{
    if (d->format.sample_rate == 2822400)  return QStringLiteral("DSD64");
    if (d->format.sample_rate == 5644800)  return QStringLiteral("DSD128");
    if (d->format.sample_rate == 11289600) return QStringLiteral("DSD256");
    if (d->format.sample_rate == 22579200) return QStringLiteral("DSD512");
    return QStringLiteral("%1 Hz").arg(d->format.sample_rate);
}

int DsdProbe::trackCount() const
{
    return d->tracks.size();
}

DsdProbe::TrackInfo DsdProbe::trackInfo(int trackNumber) const
{
    /* trackNumber is 1-based; convert to 0-based index */
    int idx = trackNumber - 1;
    if (idx < 0 || idx >= d->tracks.size()) {
        return TrackInfo{0, QString(), QString(), 0.0, QString()};
    }
    return d->tracks.at(idx);
}

QString DsdProbe::albumTitle() const
{
    return d->albumTitleStr;
}

QString DsdProbe::albumArtist() const
{
    return d->albumArtistStr;
}

uint16_t DsdProbe::year() const
{
    return d->albumYear;
}

QString DsdProbe::genre() const
{
    return d->genreStr;
}

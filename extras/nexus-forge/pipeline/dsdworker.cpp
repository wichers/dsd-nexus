/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "dsdworker.h"

extern "C" {
#include <libdsdpipe/dsdpipe.h>
#include <libsautil/sa_path.h>
#include <libsautil/sastring.h>
#include <libsautil/mem.h>
}

#include <QDebug>
#include <QDir>

DsdWorker::DsdWorker(QObject *parent)
    : QObject(parent)
    , m_pipe(nullptr)
    , m_cancelled(false)
{
}

DsdWorker::~DsdWorker()
{
    if (m_pipe) {
        dsdpipe_destroy(m_pipe);
        m_pipe = nullptr;
    }
}

void DsdWorker::cancel()
{
    m_cancelled = true;
    if (m_pipe) {
        dsdpipe_cancel(m_pipe);
    }
}

void DsdWorker::run(DsdPipeParameters param)
{
    m_cancelled = false;

    int rc = configurePipeline(param);
    if (rc != DSDPIPE_OK) {
        QString errMsg = m_pipe
            ? QString::fromUtf8(dsdpipe_get_error_message(m_pipe))
            : QStringLiteral("Failed to create pipeline");
        if (m_pipe) {
            dsdpipe_destroy(m_pipe);
            m_pipe = nullptr;
        }
        emit finished(rc, errMsg);
        return;
    }

    /* Run the pipeline (blocks until done) */
    rc = dsdpipe_run(m_pipe);

    QString errMsg;
    if (rc != DSDPIPE_OK && rc != DSDPIPE_ERROR_CANCELLED) {
        errMsg = QString::fromUtf8(dsdpipe_get_error_message(m_pipe));
    }

    dsdpipe_destroy(m_pipe);
    m_pipe = nullptr;

    emit finished(rc, errMsg);
}

int DsdWorker::configurePipeline(const DsdPipeParameters &param)
{
    m_pipe = dsdpipe_create();
    if (!m_pipe) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    QByteArray srcPath = param.source.toUtf8();
    int rc = DSDPIPE_OK;

    /* === Set source === */
    switch (param.sourceType) {
    case 1: { /* DSDPIPE_SOURCE_SACD */
        dsdpipe_channel_type_t ch = (param.channelType == 1)
            ? DSDPIPE_CHANNEL_MULTICHANNEL
            : DSDPIPE_CHANNEL_STEREO;
        rc = dsdpipe_set_source_sacd(m_pipe, srcPath.constData(), ch);
        break;
    }
    case 2: /* DSDPIPE_SOURCE_DSDIFF */
        rc = dsdpipe_set_source_dsdiff(m_pipe, srcPath.constData());
        break;
    case 3: /* DSDPIPE_SOURCE_DSF */
        rc = dsdpipe_set_source_dsf(m_pipe, srcPath.constData());
        break;
    default:
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (rc != DSDPIPE_OK)
        return rc;

    /* === Select tracks === */
    QByteArray trackSpec = param.trackSpec.toUtf8();
    if (param.trackSpec.toLower() == QStringLiteral("all")) {
        rc = dsdpipe_select_all_tracks(m_pipe);
    } else {
        rc = dsdpipe_select_tracks_str(m_pipe, trackSpec.constData());
    }
    if (rc != DSDPIPE_OK)
        return rc;

    /* === Generate album output directory from metadata === */
    QString finalOutputDir = param.outputDir;
    {
        dsdpipe_metadata_t album_meta;
        dsdpipe_metadata_init(&album_meta);

        if (dsdpipe_get_album_metadata(m_pipe, &album_meta) == DSDPIPE_OK) {
            dsdpipe_album_format_t dir_format =
                (param.albumFormat == 1)
                    ? DSDPIPE_ALBUM_ARTIST_TITLE
                    : DSDPIPE_ALBUM_TITLE_ONLY;

            char *album_dir = dsdpipe_get_album_dir(&album_meta, dir_format);
            if (album_dir) {
                QByteArray baseDir = param.outputDir.toUtf8();
                char *album_path = sa_unique_path(baseDir.constData(),
                                                  album_dir, nullptr);
                if (album_path) {
                    /* Handle multi-disc sets */
                    if (album_meta.disc_total > 1 && album_meta.disc_number > 0) {
                        char disc_subdir[32];
                        snprintf(disc_subdir, sizeof(disc_subdir),
                                 "Disc %u", album_meta.disc_number);
                        char *full_path = sa_append_path_component(
                            album_path, disc_subdir);
                        sa_free(album_path);
                        album_path = full_path;
                    }

                    if (album_path) {
                        finalOutputDir = QString::fromUtf8(album_path);
                        sa_free(album_path);
                    }
                }
                sa_free(album_dir);
            }
            dsdpipe_metadata_free(&album_meta);
        }
    }

    /* === Ensure output directory exists === */
    QByteArray outDir = finalOutputDir.toUtf8();
    QDir().mkpath(finalOutputDir);

    /* === Add sinks === */
    if (param.outputFormats & DSD_FORMAT_DSF) {
        rc = dsdpipe_add_sink_dsf(m_pipe, outDir.constData(), param.writeId3);
        if (rc != DSDPIPE_OK) return rc;
    }

    if (param.outputFormats & DSD_FORMAT_DSDIFF) {
        rc = dsdpipe_add_sink_dsdiff(m_pipe, outDir.constData(),
                                     param.writeDst, false, param.writeId3);
        if (rc != DSDPIPE_OK) return rc;
    }

    if (param.outputFormats & DSD_FORMAT_EDIT_MASTER) {
        rc = dsdpipe_add_sink_dsdiff(m_pipe, outDir.constData(),
                                     param.writeDst, true, param.writeId3);
        if (rc != DSDPIPE_OK) return rc;
    }

    if (param.outputFormats & DSD_FORMAT_WAV) {
        rc = dsdpipe_add_sink_wav(m_pipe, outDir.constData(),
                                  param.pcmBitDepth, param.pcmSampleRate);
        if (rc != DSDPIPE_OK) return rc;
    }

    if (param.outputFormats & DSD_FORMAT_FLAC) {
        rc = dsdpipe_add_sink_flac(m_pipe, outDir.constData(),
                                   param.pcmBitDepth, param.flacCompression);
        if (rc != DSDPIPE_OK) return rc;
    }

    if (param.outputFormats & DSD_FORMAT_XML) {
        rc = dsdpipe_add_sink_xml(m_pipe, outDir.constData());
        if (rc != DSDPIPE_OK) return rc;
    }

    if (param.outputFormats & DSD_FORMAT_CUE) {
        /* CUE needs a reference audio filename; use the output dir basename */
        rc = dsdpipe_add_sink_cue(m_pipe, outDir.constData(), nullptr);
        if (rc != DSDPIPE_OK) return rc;
    }

    /* === Set conversion quality === */
    dsdpipe_pcm_quality_t quality;
    switch (param.pcmQuality) {
    case 0:  quality = DSDPIPE_PCM_QUALITY_FAST;   break;
    case 2:  quality = DSDPIPE_PCM_QUALITY_HIGH;    break;
    default: quality = DSDPIPE_PCM_QUALITY_NORMAL;  break;
    }
    rc = dsdpipe_set_pcm_quality(m_pipe, quality);
    if (rc != DSDPIPE_OK) return rc;

    /* === Set track filename format === */
    dsdpipe_track_format_t tfmt;
    switch (param.trackFormat) {
    case 0:  tfmt = DSDPIPE_TRACK_NUM_ONLY;         break;
    case 1:  tfmt = DSDPIPE_TRACK_NUM_TITLE;         break;
    default: tfmt = DSDPIPE_TRACK_NUM_ARTIST_TITLE;  break;
    }
    rc = dsdpipe_set_track_filename_format(m_pipe, tfmt);
    if (rc != DSDPIPE_OK) return rc;

    /* === Set progress callback === */
    rc = dsdpipe_set_progress_callback(m_pipe,
        &DsdWorker::progressCallback,
        this);
    if (rc != DSDPIPE_OK) return rc;

    return DSDPIPE_OK;
}

int DsdWorker::progressCallback(const dsdpipe_progress_t *progress, void *userdata)
{
    auto *self = static_cast<DsdWorker *>(userdata);

    if (self->m_cancelled)
        return 1; /* request cancellation */

    QString title = progress->track_title
        ? QString::fromUtf8(progress->track_title)
        : QString();
    QString sink = progress->current_sink
        ? QString::fromUtf8(progress->current_sink)
        : QString();

    emit self->progressUpdated(
        static_cast<int>(progress->track_number),
        static_cast<int>(progress->track_total),
        progress->track_percent,
        progress->total_percent,
        title, sink);

    return 0; /* continue */
}

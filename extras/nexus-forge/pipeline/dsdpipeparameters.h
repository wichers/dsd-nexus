/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DSDPIPEPARAMETERS_H
#define DSDPIPEPARAMETERS_H

#include <QString>
#include <QMetaType>
#include <cstdint>

/**
 * @brief Output format bitmask flags
 */
enum DsdOutputFormat : uint32_t {
    DSD_FORMAT_NONE       = 0,
    DSD_FORMAT_DSF        = (1 << 0),
    DSD_FORMAT_DSDIFF     = (1 << 1),
    DSD_FORMAT_EDIT_MASTER = (1 << 2),
    DSD_FORMAT_WAV        = (1 << 3),
    DSD_FORMAT_FLAC       = (1 << 4),
    DSD_FORMAT_XML        = (1 << 5),
    DSD_FORMAT_CUE        = (1 << 6),
};

/**
 * @brief Parameters for a single DSD conversion task
 *
 * Each task in the queue has one DsdPipeParameters object describing
 * the complete conversion configuration.
 */
struct DsdPipeParameters {
    // Source
    QString source;             ///< Input file path (ISO, DSF, DFF)
    int sourceType;             ///< DSDPIPE_SOURCE_SACD/DSDIFF/DSF (0 = auto-detect)

    // Output
    QString outputDir;          ///< Base output directory
    uint32_t outputFormats;     ///< Bitmask of DsdOutputFormat flags

    // SACD-specific
    int channelType;            ///< DSDPIPE_CHANNEL_STEREO (0) or MULTICHANNEL (1)

    // Track selection
    QString trackSpec;          ///< "all", "1-5", "1,3,5"

    // PCM options (WAV/FLAC)
    int pcmBitDepth;            ///< 16, 24, or 32
    int pcmSampleRate;          ///< 0 = auto
    int pcmQuality;             ///< DSDPIPE_PCM_QUALITY_FAST/NORMAL/HIGH
    int flacCompression;        ///< 0-8

    // DSD options
    bool writeDst;              ///< Preserve DST compression (DSDIFF output)
    bool writeId3;              ///< Write ID3v2 metadata tags

    // Output naming
    int trackFormat;            ///< DSDPIPE_TRACK_NUM_ONLY/NUM_TITLE/NUM_ARTIST_TITLE
    int albumFormat;            ///< DSDPIPE_ALBUM_TITLE_ONLY/ARTIST_TITLE

    // Display fields (populated from probe, used for list columns)
    QString albumTitle;
    QString albumArtist;
    int trackCount;
    QString formatSummary;      ///< e.g. "DSF + WAV 24-bit"

    DsdPipeParameters();

    /** @brief Copy conversion configuration from another params object (not display fields) */
    void copyConfigurationFrom(const DsdPipeParameters &other);

    /** @brief Generate a human-readable format summary string */
    QString buildFormatSummary() const;
};

Q_DECLARE_METATYPE(DsdPipeParameters)

#endif // DSDPIPEPARAMETERS_H

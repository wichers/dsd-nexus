/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "dsdpipeparameters.h"
#include <QStringList>

DsdPipeParameters::DsdPipeParameters()
    : sourceType(0)          // auto-detect
    , outputFormats(DSD_FORMAT_NONE)
    , channelType(0)         // DSDPIPE_CHANNEL_STEREO
    , trackSpec(QStringLiteral("all"))
    , pcmBitDepth(24)
    , pcmSampleRate(0)       // auto
    , pcmQuality(1)          // DSDPIPE_PCM_QUALITY_NORMAL
    , flacCompression(5)
    , writeDst(false)
    , writeId3(true)
    , trackFormat(2)         // DSDPIPE_TRACK_NUM_ARTIST_TITLE
    , albumFormat(1)         // DSDPIPE_ALBUM_ARTIST_TITLE
    , trackCount(0)
{
}

void DsdPipeParameters::copyConfigurationFrom(const DsdPipeParameters &other)
{
    outputFormats = other.outputFormats;
    pcmBitDepth = other.pcmBitDepth;
    pcmSampleRate = other.pcmSampleRate;
    pcmQuality = other.pcmQuality;
    flacCompression = other.flacCompression;
    writeDst = other.writeDst;
    writeId3 = other.writeId3;
    trackFormat = other.trackFormat;
    albumFormat = other.albumFormat;
}

QString DsdPipeParameters::buildFormatSummary() const
{
    QStringList parts;

    if (outputFormats & DSD_FORMAT_DSF)
        parts << QStringLiteral("DSF");
    if (outputFormats & DSD_FORMAT_DSDIFF)
        parts << QStringLiteral("DSDIFF");
    if (outputFormats & DSD_FORMAT_EDIT_MASTER)
        parts << QStringLiteral("Edit Master");
    if (outputFormats & DSD_FORMAT_WAV)
        parts << QStringLiteral("WAV %1-bit").arg(pcmBitDepth);
    if (outputFormats & DSD_FORMAT_FLAC)
        parts << QStringLiteral("FLAC %1-bit").arg(pcmBitDepth);
    if (outputFormats & DSD_FORMAT_XML)
        parts << QStringLiteral("XML");
    if (outputFormats & DSD_FORMAT_CUE)
        parts << QStringLiteral("CUE");

    return parts.isEmpty() ? QStringLiteral("(none)") : parts.join(QStringLiteral(" + "));
}

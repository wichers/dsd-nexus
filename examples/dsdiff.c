/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * DSD-Nexus is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * DSD-Nexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with DSD-Nexus; if not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <libsautil/mem.h>

#include <libdsdiff/dsdiff.h>

// Marker sorting constants
#define MARKERSORT_TIMESTAMP     0  // MARKER SORTING ON TIMESTAMP

/* Print comment function */
static void print_comment(dsdiff_comment_t myComment) {
    printf("     %5d [min]    \n", myComment.minute);
    printf("     %5d [hrs]    \n", myComment.hour);
    printf("     %5d [days]   \n", myComment.day);
    printf("     %5d [months] \n", myComment.month);
    printf("     %5d [years]  \n", myComment.year);

    switch (myComment.comment_type) {
    case DSDIFF_COMMENT_TYPE_GENERAL:
        printf("     comment type       = GENERAL\n");
        printf("     comment reference  = %5d\n", myComment.comment_ref);
        break;
    case DSDIFF_COMMENT_TYPE_CHANNEL:
        printf("     comment type       = CHANNEL\n");
        printf("     comment reference  = %5d\n", myComment.comment_ref);
        break;
    case DSDIFF_COMMENT_TYPE_SOUND_SOURCE:
        printf("     comment type       = SOUNDSOURCE\n");
        switch (myComment.comment_ref) {
        case DSDIFF_SOURCE_DSD_RECORDING:
            printf("     comment reference  = DSD recording\n");
            break;
        case DSDIFF_SOURCE_ANALOG_RECORDING:
            printf("     comment reference  = Analog recording\n");
            break;
        case DSDIFF_SOURCE_PCM_RECORDING:
            printf("     comment reference  = PCM recording\n");
            break;
        default:
            printf("     comment reference  = %5d\n", myComment.comment_ref);
            break;
        }
        break;
    case DSDIFF_COMMENT_TYPE_FILE_HISTORY:
        printf("     comment type       = FILEHISTORY\n");
        switch (myComment.comment_ref) {
        case DSDIFF_HISTORY_REMARK:
            printf("     comment reference  = Remark\n");
            break;
        case DSDIFF_HISTORY_OPERATOR:
            printf("     comment reference  = operator\n");
            break;
        case DSDIFF_HISTORY_PLACE_ZONE:
            printf("     comment reference  = place - zone info\n");
            break;
        case DSDIFF_HISTORY_REVISION:
            printf("     comment reference  = revision info\n");
            break;
        default:
            printf("     comment reference  = %5d\n", myComment.comment_ref);
            break;
        }
        break;
    default:
        printf("     comment type (unknown)= %5d\n", myComment.comment_type);
        printf("     comment reference  = %5d\n", myComment.comment_ref);
        break;
    }

    printf("     text length        = %5d\n", myComment.text_length);
    if (myComment.text != NULL && myComment.text_length > 0) {
        printf("%.*s\n", (int)myComment.text_length, myComment.text);
    }
}

/* Print marker function */
static void printMarker(dsdiff_marker_t myMarker) {
    printf("     %5d [hrs]    \n", myMarker.time.hours);
    printf("     %5d [min]    \n", myMarker.time.minutes);
    printf("     %5d [sec]    \n", myMarker.time.seconds);
    printf("     %5u [sam]    \n", myMarker.time.samples);
    printf("     %5d [off]    \n", myMarker.offset);

    if (myMarker.mark_channel == DSDIFF_MARK_CHANNEL_ALL) {
        printf("     Channel            = ALL Channels\n");
    } else {
        printf("     Channel            = %5d\n", myMarker.mark_channel);
    }

    switch (myMarker.mark_type) {
    case DSDIFF_MARK_TRACK_START:
        printf("     mark type          = Start Track\n");
        break;
    case DSDIFF_MARK_TRACK_STOP:
        printf("     mark type          = Stop Track\n");
        break;
    case DSDIFF_MARK_PROGRAM_START:
        printf("     mark type          = Program Start\n");
        break;
    case DSDIFF_MARK_INDEX:
        printf("     mark type          = Index\n");
        break;
    default:
        printf("     mark type          = %d\n", (int)myMarker.mark_type);
        break;
    }

    printf("     Flags              = 0X%x\n", myMarker.track_flags);
    if (myMarker.marker_text != NULL && myMarker.text_length > 0) {
        printf("%.*s\n", (int)myMarker.text_length, myMarker.marker_text);
    }
}

/* Example Read function */
static void ExampleRead(char* Filename) {
    uint16_t NumChannels;
    uint32_t freq;
    dsdiff_file_mode_t filemode;
    uint64_t sizeSndData;
    char sFilename[255];

    /* Create the file object */
    dsdiff_t* File = NULL;

    dsdiff_new(&File);

    /* Open the file for reading */
    dsdiff_open(File, Filename);

    /* Check on closed file */
    dsdiff_get_open_mode(File, &filemode);
    printf("  get_openMode  (closed) = %10d\n", filemode);

    /* File version */
    uint8_t version, chunk;
    dsdiff_get_format_version(File, &version, &chunk);
    printf("  File Version            = %d,%d\n", version, chunk);

    /* Get the size of the sound data */
    dsdiff_get_dsd_data_size(File, &sizeSndData);
    printf("  get_sizeSndData        = %10llu\n", (unsigned long long)sizeSndData);

    /* === PROPERTIES === */

    /* Determine the amount of channels */
    dsdiff_get_channel_count(File, &NumChannels);
    printf("  GetnumChannels         = %10d\n", NumChannels);

    /* The sample frequency */
    dsdiff_get_sample_rate(File, &freq);
    printf("  get_sampleFreq         = %10u\n", freq);

    /* Retrieve the filename */
    dsdiff_get_filename(File, sFilename, 255);
    printf("  GetFileName            = %s\n", sFilename);

    uint64_t sampleframes;
    dsdiff_get_sample_frame_count(File, &sampleframes);
    printf("  GetnumSampleFrames     = %10llu\n", (unsigned long long)sampleframes);

    /* Check on open file */
    dsdiff_get_open_mode(File, &filemode);
    printf("  GetopenMode            = %10d\n", filemode);

    uint16_t samplebits;
    dsdiff_get_sample_bits(File, &samplebits);
    printf("  GetsampleBits          = %10d\n", samplebits);

    uint32_t samplefreq;
    dsdiff_get_sample_rate(File, &samplefreq);
    printf("  GetsampleFreq          = %10u\n", samplefreq);

    dsdiff_get_dsd_data_size(File, &sizeSndData);
    printf("  GetsizeSndData         = %10llu\n", (unsigned long long)sizeSndData);

    /* Timecode */
    int IsStartTimeCode;
    dsdiff_has_start_timecode(File, &IsStartTimeCode);
    if (IsStartTimeCode) {
        dsdiff_timecode_t TimeCode;
        dsdiff_get_start_timecode(File, &TimeCode);
        printf("  GetStartTimeCode [h]   = %10d\n", TimeCode.hours);
        printf("  GetStartTimeCode [m]   = %10d\n", TimeCode.minutes);
        printf("  GetStartTimeCode [s]   = %10d\n", TimeCode.seconds);
        printf("  GetStartTimeCode [o]   = %10u\n", TimeCode.samples);
    } else {
        printf("  no time code available\n");
    }

    /* Loudspeaker configuration */
    int LoudSpeakerConfig;
    dsdiff_has_loudspeaker_config(File, &LoudSpeakerConfig);
    if (LoudSpeakerConfig) {
        dsdiff_loudspeaker_config_t Conf;
        dsdiff_get_loudspeaker_config(File, &Conf);
        printf("  GetLoudSpeakerConfig   = %d\n", (int)Conf);
    } else {
        printf("  GetLoudSpeakerConfig   = not available\n");
    }

    /* === INFORMATIONAL DATA === */

    int NrComments;
    dsdiff_get_comment_count(File, &NrComments);
    printf("  GetNrComments          = %10d\n", NrComments);
    if (NrComments > 0) {
        for (int i = 0; i < NrComments; i++) {
            dsdiff_comment_t myComment;
            printf("     ----------- Comment %2d -----------\n", i);
            dsdiff_get_comment(File, i, &myComment);
            print_comment(myComment);
        }
    }

    /* The markers */
    int NrMarkers;
    dsdiff_get_dsd_marker_count(File, &NrMarkers);
    if (NrMarkers > 0) {
        printf("  GetNrDSDMarkers           = %10d\n", NrMarkers);
        for (int i = 0; i < NrMarkers; i++) {
          dsdiff_marker_t myMarker = {0};
            printf("     ----------- Marker %2d -----------\n", i + 1);
            dsdiff_get_dsd_marker(File, i, &myMarker);
            printMarker(myMarker);
        }
    }

    /* The Disc Artist */
    int DiscArtistAvail;
    dsdiff_has_disc_artist(File, &DiscArtistAvail);
    if (DiscArtistAvail) {
        uint32_t Size = 255;
        char Name[255];
        dsdiff_get_disc_artist(File, &Size, Name);
        printf("  GetDiscArtist          = %s\n", Name);
    }

    /* The Disc Title */
    int DiscTitleAvail;
    dsdiff_has_disc_title(File, &DiscTitleAvail);
    if (DiscTitleAvail) {
        uint32_t Size = 255;
        char Name[255];
        dsdiff_get_disc_title(File, &Size, Name);
        printf("  GetDiscTitle           = %s\n", Name);
    }

    /* The Edited Master ID */
    int EMIDAvail;
    dsdiff_has_emid(File, &EMIDAvail);
    if (EMIDAvail) {
        uint32_t Size = 255;
        char Name[255];
        dsdiff_get_emid(File, &Size, Name);
        printf("  GetEMID                = %s\n", Name);
    }

    /* Data reading (DSD) */
    if (sizeSndData > 0) {
        unsigned char databuf[100];
        uint32_t readframes;
        dsdiff_read_dsd_data(File, databuf, 1, &readframes);
    }

    /* Data reading (DST) */
    dsdiff_audio_type_t FileType;
    dsdiff_get_audio_type(File, &FileType);
    if (FileType == DSDIFF_AUDIO_DST) {
        uint32_t NrDstFrame;
        dsdiff_get_dst_frame_count(File, &NrDstFrame);
        uint32_t FrameSize;
        dsdiff_get_dst_max_frame_size(File, &FrameSize);
        printf("  Nr Dst Frames          = %u\n", NrDstFrame);
        printf("  MaxFrameSize           = %u\n", FrameSize);

        /* Reading without CRC */
        if (NrDstFrame > 1) {
            uint32_t SizeCurrentFrame;
            unsigned char* databuf = (unsigned char*)sa_malloc(FrameSize);
            /* Read the first frame */
            dsdiff_read_dst_frame(File, databuf, FrameSize, &SizeCurrentFrame);
            /* Reading the first frame again, but now indexed */
            dsdiff_read_dst_frame_at_index(File, 1, databuf, FrameSize, &SizeCurrentFrame);
            sa_free(databuf);
        }

        /* Reading with CRC */
        int CrcAvail;
        dsdiff_has_dst_crc(File, &CrcAvail);
        if (CrcAvail) {
            printf("  Crc Avail              = %d\n", CrcAvail);
            uint32_t MaxCrcSize;
            dsdiff_get_dst_crc_size(File, &MaxCrcSize);
            if (NrDstFrame > 1) {
                uint32_t SizeCurrentFrame;
                uint32_t SizeCurrentCrc;
                unsigned char* databuf = (unsigned char*)sa_malloc(FrameSize);
                unsigned char* crcbuf = (unsigned char*)sa_malloc(MaxCrcSize);
                /* Read the first frame */
                dsdiff_read_dst_frame_with_crc(File, databuf, FrameSize, &SizeCurrentFrame, crcbuf, MaxCrcSize, &SizeCurrentCrc);
                /* Reading the first frame again, but now indexed */
                dsdiff_read_dst_frame_at_index_with_crc(File, 1, databuf, FrameSize, &SizeCurrentFrame, crcbuf, MaxCrcSize, &SizeCurrentCrc);
                sa_free(databuf);
                sa_free(crcbuf);
            }
        }
    }

    dsdiff_close(File);
}

/* Example Write DSD function */
static void ExampleWriteDSD(char* Filename) {
    /* Create the file object */
    dsdiff_t* File = NULL;

    dsdiff_new(&File);

    /* === PROPERTIES === */

    /* Create the start time code */
    dsdiff_timecode_t TimeCode;
    TimeCode.hours = 1;
    TimeCode.minutes = 1;
    TimeCode.seconds = 1;
    TimeCode.samples = 1;
    dsdiff_set_start_timecode(File, &TimeCode);

    /* Set the loudspeaker configuration */
    dsdiff_set_loudspeaker_config(File, DSDIFF_LS_CONFIG_MULTI5);

    /* Open the file for writing (channel IDs are auto-set based on channel count) */
    dsdiff_create(File, Filename, DSDIFF_AUDIO_DSD, 5, 1, (44100 * 64));
    
    /* === INFORMATIONAL DATA === */

    /* Allocate the comment structure */
    dsdiff_comment_t myComment;
    char text[] = "Comment -> abcdefghijklmnnopqrstuwxyz1234567890!@#$%^&*()_+";

    myComment.minute = 1;
    myComment.hour = 2;
    myComment.day = 3;
    myComment.month = 4;
    myComment.year = 5;
    myComment.comment_type = DSDIFF_COMMENT_TYPE_GENERAL;
    myComment.comment_ref = 0;
    myComment.text_length = (uint32_t) strlen(text);
    myComment.text = text;
    dsdiff_add_comment(File, &myComment);

    /* Setting the artist */
    char Artist[] = "My Name";
    dsdiff_set_disc_artist(File, Artist);

    char Title[] = "My Title Of This Disc";
    dsdiff_set_disc_title(File, Title);

    char Emid[] = "PHILIPS-CFT-ABCDE12345";
    dsdiff_set_emid(File, Emid);

    /* Allocating the marker structure */
    dsdiff_marker_t myMarker;
    char markertext[] = "MARK -> ABCDEFGHIJKLMNNOPQRSTUWXYZ\n1234567890\n!@#$%^&*()_+\n";

    myMarker.time.hours = 1;
    myMarker.time.minutes = 2;
    myMarker.time.seconds = 3;
    myMarker.time.samples = 4;
    myMarker.offset = 5;
    myMarker.mark_channel = (dsdiff_mark_channel_t)DSDIFF_MARK_CHANNEL_ALL;
    myMarker.mark_type = DSDIFF_MARK_INDEX;
    myMarker.track_flags = DSDIFF_TRACK_FLAG_LFE_MUTE;
    myMarker.text_length = (uint32_t) strlen(markertext);
    myMarker.marker_text = markertext;
    dsdiff_add_dsd_marker(File, &myMarker);

    /* === SOUND DATA === */
    unsigned char* data = (unsigned char*)sa_malloc(5 * 1024);
    /* Init on digital silence */
    memset(data, 85, 5 * 1024);  /* 85 = 01010101 pattern is digital silence */
    uint32_t sizeWrite;
    /* Write data to file (5 channels with each 1024) */
    dsdiff_write_dsd_data(File, data, 1024, &sizeWrite);
    sa_free(data);

    /* Make all chunks correct */
    dsdiff_finalize(File);
    /* Close file */
    dsdiff_close(File);
}

/* Example Write DST function */
static void ExampleWriteDST(char* Filename) {
    /* Create the file object */
    dsdiff_t* File = NULL;

    /* === PROPERTIES === */

    dsdiff_new(&File);

    /* === PROPERTIES === */

    /* Create the start time code */
    dsdiff_timecode_t TimeCode;
    TimeCode.hours = 2;
    TimeCode.minutes = 2;
    TimeCode.seconds = 2;
    TimeCode.samples = 2;
    dsdiff_set_start_timecode(File, &TimeCode);

    /* Open the file for writing (channel IDs are auto-set based on channel count) */
    dsdiff_create(File, Filename, DSDIFF_AUDIO_DST, 2, 1,
                  DSDIFF_SAMPLE_FREQ_64FS);

    /* Set the start time code after creation */
    dsdiff_set_start_timecode(File, &TimeCode);

    /* Set the loudspeaker configuration */
    dsdiff_set_loudspeaker_config(File, DSDIFF_LS_CONFIG_STEREO);

    /* === INFORMATIONAL DATA === */

    dsdiff_comment_t myComment;
    char text[] = "This File Contains Bogus DST data, which represent nothing!!!";

    myComment.minute = 1;
    myComment.hour = 2;
    myComment.day = 3;
    myComment.month = 4;
    myComment.year = 5;
    myComment.comment_type = DSDIFF_COMMENT_TYPE_SOUND_SOURCE;
    myComment.comment_ref = DSDIFF_SOURCE_DSD_RECORDING;
    myComment.text_length = (uint32_t) strlen(text);
    myComment.text = text;
    dsdiff_add_comment(File, &myComment);

    char Artist[] = "An Artist Name";
    dsdiff_set_disc_artist(File, Artist);

    char Title[] = "A Disc Name";
    dsdiff_set_disc_title(File, Title);

    char Emid[] = "PHILIPS-CFT-ABCDEFG123456789";
    dsdiff_set_emid(File, Emid);

    dsdiff_marker_t myMarker;
    char markertext[] = "FILE Contains Bogus DST data!!!";

    myMarker.time.hours = 1;
    myMarker.time.minutes = 2;
    myMarker.time.seconds = 3;
    myMarker.time.samples = 4;
    myMarker.offset = 0;
    myMarker.mark_channel = (dsdiff_mark_channel_t)DSDIFF_MARK_CHANNEL_ALL;
    myMarker.mark_type = DSDIFF_MARK_TRACK_START;
    myMarker.track_flags = DSDIFF_TRACK_FLAG_NONE;
    myMarker.text_length = (uint32_t) strlen(markertext);
    myMarker.marker_text = markertext;
    dsdiff_add_dsd_marker(File, &myMarker);

    /* === SOUND DATA === */
    unsigned char* data = (unsigned char*)sa_malloc(1024);
    /* Init on digital silence */
    memset(data, 66, 1024);  /* 66 == arbitrary data !!! */
    /* Write data to file (2 channels converted to one frame of size 1024) */
    dsdiff_write_dst_frame(File, data, 1024);  /* arbitrary size !! */
    memset(data, 55, 1024);  /* 55 == arbitrary data !!! */
    dsdiff_write_dst_frame(File, data, 804);  /* arbitrary size !! */
    sa_free(data);

    /* Make all chunks correct */
    dsdiff_finalize(File);
    /* Close file */
    dsdiff_close(File);
}

/* Example Modify function */
static void ExampleModify(char* Filename) {
    /* Create the file object */
    dsdiff_t* File = NULL;

    dsdiff_new(&File);

    /* === PROPERTIES === */

    /* Open the file for modifying */
    dsdiff_modify(File, Filename);

    /* === PROPERTIES === */

    /* Can only be modified when it is in the file */
    int StartTimeAvail;
    dsdiff_has_start_timecode(File, &StartTimeAvail);
    if (StartTimeAvail) {
        dsdiff_timecode_t TimeCode;
        TimeCode.hours = 1;
        TimeCode.minutes = 1;
        TimeCode.seconds = 1;
        TimeCode.samples = 1;
        printf(" setting start time code\n");
        dsdiff_set_start_timecode(File, &TimeCode);
    } else {
        printf(" no start time code available\n");
    }

    /* Can only be modified when it is in the file */
    int LoudSpeakerAvail;
    dsdiff_has_loudspeaker_config(File, &LoudSpeakerAvail);
    if (LoudSpeakerAvail) {
        printf(" changing loudspeaker configuration \n");
        dsdiff_set_loudspeaker_config(File, DSDIFF_LS_CONFIG_STEREO);
    } else {
        printf(" no loudspeaker configuration in file \n");
    }

    /* === INFORMATIONAL DATA === */

    dsdiff_comment_t myComment;
    char text[] = "Modify Comment -> abcdefghijklmnnopqrstuwxyz1234567890!@#$%^&*()_+";

    myComment.minute = 1;
    myComment.hour = 2;
    myComment.day = 3;
    myComment.month = 4;
    myComment.year = 5;
    myComment.comment_type = DSDIFF_COMMENT_TYPE_GENERAL;
    myComment.comment_ref = 0;
    myComment.text_length = (uint32_t) strlen(text);
    myComment.text = text;
    dsdiff_add_comment(File, &myComment);

    /* Setting the artist (overrule) */
    char Artist[] = "My Modified Name";
    dsdiff_set_disc_artist(File, Artist);

    /* Setting the Title (overrule) */
    char Title[] = "My Modified Title Of This Disc";
    dsdiff_set_disc_title(File, Title);

    /* Setting the EMID (overrule) */
    char Emid[] = "PHILIPS-CFT-MODIFIED";
    dsdiff_set_emid(File, Emid);

    /* Allocating the marker structure */
    dsdiff_marker_t myMarker;
    char markertext[] = "MODIFIED MARK -> ABCDEFGHIJKLMNNOPQRSTUWXYZ\n1234567890\n!@#$%^&*()_+\n";

    myMarker.time.hours = 1;
    myMarker.time.minutes = 2;
    myMarker.time.seconds = 3;
    myMarker.time.samples = 4;
    myMarker.offset = 5;
    myMarker.mark_channel = (dsdiff_mark_channel_t)DSDIFF_MARK_CHANNEL_ALL;
    myMarker.mark_type = DSDIFF_MARK_INDEX;
    myMarker.track_flags = DSDIFF_TRACK_FLAG_LFE_MUTE;
    myMarker.text_length = (uint32_t) strlen(markertext);
    myMarker.marker_text = markertext;
    dsdiff_add_dsd_marker(File, &myMarker);

    /* Make all chunks correct */
    dsdiff_finalize(File);
    /* Close file */
    dsdiff_close(File);
}

/* The Main program */
int main(int argc, char* argv[]) {
    /* Say hello to the user */
    printf("----------------------------\n");
    printf("Example DSDIFF Sources (C)\n");
    printf("----------------------------\n");

    /* Just some argument checking */
    if (argc != 3) {
        printf("usage : ExampleDSDIFF14 -read  <filename>\n");
        printf("        ExampleDSDIFF14 -write <filename>\n");
        printf("        ExampleDSDIFF14 -writeDST <filename>\n");
        printf("        ExampleDSDIFF14 -modify <filename>\n");
        return 1;
    }

    if (strcmp(argv[1], "-read") == 0) {
        ExampleRead(argv[2]);  /* Actual reading of the file */
    }
    if (strcmp(argv[1], "-write") == 0) {
        ExampleWriteDSD(argv[2]);  /* Actual writing of the DSD file */
    }
    if (strcmp(argv[1], "-writeDST") == 0) {
        ExampleWriteDST(argv[2]);  /* Actual writing of the DST file */
    }
    if (strcmp(argv[1], "-modify") == 0) {
        ExampleModify(argv[2]);  /* Actual modify of the file */
    }

    return 0;
}

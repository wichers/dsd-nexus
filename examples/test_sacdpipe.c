/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Comprehensive test program for libdsdpipe API
 * This program tests all scenarios:
 * 1. Reading multiple tracks from virtual SACD ISO
 * 2. Output to individual tracks (DSDIFF & DSF)
 * 3. Output to an edit master (single file with markers)
 * 4. Running through DST decoder transform
 * 5. Running through DSD-to-PCM converter transform
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

#include <libdsdpipe/dsdpipe.h>

/*============================================================================
 * Test Configuration
 *============================================================================*/

#define SEPARATOR "============================================"
#define SUBSEP    "--------------------------------------------"

/*============================================================================
 * Progress Callback
 *============================================================================*/

static int progress_callback(const dsdpipe_progress_t *progress, void *userdata)
{
    const char *test_name = (const char *)userdata;

    printf("[%s] Track %d/%d: %.1f%% (frame %llu/%llu) - Overall: %.1f%%",
           test_name ? test_name : "PROGRESS",
           progress->track_number,
           progress->track_total,
           progress->track_percent,
           (unsigned long long)progress->frames_done,
           (unsigned long long)progress->frames_total,
           progress->total_percent);

    if (progress->track_title) {
        printf(" - \"%s\"", progress->track_title);
    }

    printf("\n");

    return 0;  /* Continue processing */
}

/*============================================================================
 * Basic Tests
 *============================================================================*/

static void test_version(void)
{
    printf("\n");
    printf(SEPARATOR "\n");
    printf("TEST: Version Info\n");
    printf(SEPARATOR "\n");
    printf("Version string: %s\n", dsdpipe_version_string());
    printf("Version int:    0x%06X\n", dsdpipe_version_int());
    printf("FLAC support:   %s\n", dsdpipe_has_flac_support() ? "yes" : "no");
    printf("\n");
}

static void test_error_strings(void)
{
    printf(SEPARATOR "\n");
    printf("TEST: Error Strings\n");
    printf(SEPARATOR "\n");
    printf("DSDPIPE_OK:                    %s\n", dsdpipe_error_string(DSDPIPE_OK));
    printf("DSDPIPE_ERROR_INVALID_ARG:     %s\n", dsdpipe_error_string(DSDPIPE_ERROR_INVALID_ARG));
    printf("DSDPIPE_ERROR_OUT_OF_MEMORY:   %s\n", dsdpipe_error_string(DSDPIPE_ERROR_OUT_OF_MEMORY));
    printf("DSDPIPE_ERROR_SOURCE_OPEN:     %s\n", dsdpipe_error_string(DSDPIPE_ERROR_SOURCE_OPEN));
    printf("DSDPIPE_ERROR_CANCELLED:       %s\n", dsdpipe_error_string(DSDPIPE_ERROR_CANCELLED));
    printf("\n");
}

static void test_track_selection(void)
{
    printf(SEPARATOR "\n");
    printf("TEST: Track Selection Parsing\n");
    printf(SEPARATOR "\n");

    dsdpipe_t *pipe = dsdpipe_create();
    if (!pipe) {
        printf("ERROR: Failed to create pipeline\n");
        return;
    }

    /* Set up a virtual SACD source */
    int result = dsdpipe_set_source_sacd(pipe, "virtual_album.iso", DSDPIPE_CHANNEL_STEREO);
    printf("Set source: %s (result=%d)\n",
           result == DSDPIPE_OK ? "OK" : dsdpipe_get_error_message(pipe), result);

    /* Get track count */
    uint8_t track_count = 0;
    result = dsdpipe_get_track_count(pipe, &track_count);
    printf("Track count: %d (result=%d)\n\n", track_count, result);

    /* Test various track selection strings */
    const char *selections[] = {
        "all",
        "1",
        "1,3,5",
        "1-5",
        "1-3,5,7-9",
        "5-1",           /* Reverse range */
    };

    for (size_t i = 0; i < sizeof(selections) / sizeof(selections[0]); i++) {
        result = dsdpipe_select_tracks_str(pipe, selections[i]);

        size_t count = 0;
        uint8_t tracks[32];
        dsdpipe_get_selected_tracks(pipe, tracks, 32, &count);

        printf("Selection \"%s\" -> %zu tracks: ", selections[i], count);
        for (size_t j = 0; j < count; j++) {
            printf("%d%s", tracks[j], (j < count - 1) ? "," : "");
        }
        printf(" (result=%d)\n", result);
    }

    dsdpipe_destroy(pipe);
    printf("\n");
}

static void test_metadata(void)
{
    printf(SEPARATOR "\n");
    printf("TEST: Metadata Handling\n");
    printf(SEPARATOR "\n");

    dsdpipe_t *pipe = dsdpipe_create();
    if (!pipe) {
        printf("ERROR: Failed to create pipeline\n");
        return;
    }

    /* Set up source */
    dsdpipe_set_source_sacd(pipe, "virtual_album.iso", DSDPIPE_CHANNEL_STEREO);

    /* Get album metadata */
    dsdpipe_metadata_t album_meta;
    dsdpipe_metadata_init(&album_meta);

    int result = dsdpipe_get_album_metadata(pipe, &album_meta);
    printf("Album metadata (result=%d):\n", result);
    printf("  Title:     %s\n", album_meta.album_title ? album_meta.album_title : "(null)");
    printf("  Artist:    %s\n", album_meta.album_artist ? album_meta.album_artist : "(null)");
    printf("  Publisher: %s\n", album_meta.album_publisher ? album_meta.album_publisher : "(null)");
    printf("  Year:      %d\n", album_meta.year);
    printf("  Genre:     %s\n", album_meta.genre ? album_meta.genre : "(null)");

    dsdpipe_metadata_free(&album_meta);
    printf("\n");

    /* Get track metadata for first few tracks */
    for (uint8_t t = 1; t <= 3; t++) {
        dsdpipe_metadata_t track_meta;
        dsdpipe_metadata_init(&track_meta);

        result = dsdpipe_get_track_metadata(pipe, t, &track_meta);
        printf("Track %d metadata (result=%d):\n", t, result);
        printf("  Title:     %s\n", track_meta.track_title ? track_meta.track_title : "(null)");
        printf("  Performer: %s\n", track_meta.track_performer ? track_meta.track_performer : "(null)");
        printf("  ISRC:      %s\n", track_meta.isrc[0] ? track_meta.isrc : "(none)");
        printf("  Number:    %d/%d\n", track_meta.track_number, track_meta.track_total);

        dsdpipe_metadata_free(&track_meta);
    }

    dsdpipe_destroy(pipe);
    printf("\n");
}

/*============================================================================
 * Scenario 1: Multiple Tracks to Individual DSF Files
 *============================================================================*/

static void test_scenario_multiple_tracks_dsf(void)
{
    printf(SEPARATOR "\n");
    printf("SCENARIO 1: Multiple Tracks -> Individual DSF Files\n");
    printf(SEPARATOR "\n");
    printf("Testing: SACD source -> DST decoder -> DSF sink (per-track)\n\n");

    dsdpipe_t *pipe = dsdpipe_create();
    if (!pipe) {
        printf("ERROR: Failed to create pipeline\n");
        return;
    }

    /* Configure source */
    printf("Configuring pipeline...\n");
    int result = dsdpipe_set_source_sacd(pipe, "virtual_classical.iso", DSDPIPE_CHANNEL_STEREO);
    printf("  Source: virtual_classical.iso (stereo)\n");
    printf("  Result: %s\n", result == DSDPIPE_OK ? "OK" : dsdpipe_get_error_message(pipe));

    /* Select multiple tracks */
    result = dsdpipe_select_tracks_str(pipe, "1-5");
    printf("  Tracks: 1-5 (5 tracks)\n");
    printf("  Result: %s\n", result == DSDPIPE_OK ? "OK" : dsdpipe_get_error_message(pipe));

    /* Add DSF sink with ID3 metadata */
    result = dsdpipe_add_sink_dsf(pipe, "output/classical", true);
    printf("  Sink: DSF with ID3 -> output/classical_trackNN.dsf\n");
    printf("  Result: %s\n", result == DSDPIPE_OK ? "OK" : dsdpipe_get_error_message(pipe));

    /* Set progress callback */
    dsdpipe_set_progress_callback(pipe, progress_callback, (void *)"DSF");

    printf("\nStarting pipeline...\n");
    printf(SUBSEP "\n");

    result = dsdpipe_run(pipe);

    printf(SUBSEP "\n");
    printf("Pipeline finished: %s (result=%d)\n",
           result == DSDPIPE_OK ? "SUCCESS" : "FAILED", result);

    if (result != DSDPIPE_OK) {
        printf("Error: %s\n", dsdpipe_get_error_message(pipe));
    }

    dsdpipe_destroy(pipe);
    printf("\n");
}

/*============================================================================
 * Scenario 2: Multiple Tracks to Individual DSDIFF Files
 *============================================================================*/

static void test_scenario_multiple_tracks_dsdiff(void)
{
    printf(SEPARATOR "\n");
    printf("SCENARIO 2: Multiple Tracks -> Individual DSDIFF Files\n");
    printf(SEPARATOR "\n");
    printf("Testing: SACD source -> DST decoder -> DSDIFF sink (per-track)\n\n");

    dsdpipe_t *pipe = dsdpipe_create();
    if (!pipe) {
        printf("ERROR: Failed to create pipeline\n");
        return;
    }

    /* Configure source */
    printf("Configuring pipeline...\n");
    int result = dsdpipe_set_source_sacd(pipe, "virtual_jazz.iso", DSDPIPE_CHANNEL_STEREO);
    printf("  Source: virtual_jazz.iso (stereo)\n");
    printf("  Result: %s\n", result == DSDPIPE_OK ? "OK" : dsdpipe_get_error_message(pipe));

    /* Select multiple tracks (non-contiguous) */
    result = dsdpipe_select_tracks_str(pipe, "1,3,5,7");
    printf("  Tracks: 1,3,5,7 (4 tracks, non-contiguous)\n");
    printf("  Result: %s\n", result == DSDPIPE_OK ? "OK" : dsdpipe_get_error_message(pipe));

    /* Add DSDIFF sink (per-track mode, no DST passthrough) */
    result = dsdpipe_add_sink_dsdiff(pipe, "output/jazz", false, false);
    printf("  Sink: DSDIFF per-track -> output/jazz_trackNN.dff\n");
    printf("  Result: %s\n", result == DSDPIPE_OK ? "OK" : dsdpipe_get_error_message(pipe));

    /* Set progress callback */
    dsdpipe_set_progress_callback(pipe, progress_callback, (void *)"DSDIFF");

    printf("\nStarting pipeline...\n");
    printf(SUBSEP "\n");

    result = dsdpipe_run(pipe);

    printf(SUBSEP "\n");
    printf("Pipeline finished: %s (result=%d)\n",
           result == DSDPIPE_OK ? "SUCCESS" : "FAILED", result);

    if (result != DSDPIPE_OK) {
        printf("Error: %s\n", dsdpipe_get_error_message(pipe));
    }

    dsdpipe_destroy(pipe);
    printf("\n");
}

/*============================================================================
 * Scenario 3: Multiple Tracks to DSDIFF Edit Master
 *============================================================================*/

static void test_scenario_edit_master(void)
{
    printf(SEPARATOR "\n");
    printf("SCENARIO 3: Multiple Tracks -> DSDIFF Edit Master\n");
    printf(SEPARATOR "\n");
    printf("Testing: SACD source -> DST decoder -> DSDIFF Edit Master sink\n");
    printf("         (single file with track markers)\n\n");

    dsdpipe_t *pipe = dsdpipe_create();
    if (!pipe) {
        printf("ERROR: Failed to create pipeline\n");
        return;
    }

    /* Configure source */
    printf("Configuring pipeline...\n");
    int result = dsdpipe_set_source_sacd(pipe, "virtual_symphony.iso", DSDPIPE_CHANNEL_STEREO);
    printf("  Source: virtual_symphony.iso (stereo)\n");
    printf("  Result: %s\n", result == DSDPIPE_OK ? "OK" : dsdpipe_get_error_message(pipe));

    /* Select all tracks */
    result = dsdpipe_select_tracks_str(pipe, "all");
    printf("  Tracks: all\n");
    printf("  Result: %s\n", result == DSDPIPE_OK ? "OK" : dsdpipe_get_error_message(pipe));

    /* Add DSDIFF Edit Master sink */
    result = dsdpipe_add_sink_dsdiff(pipe, "output/symphony_master.dff", false, true);
    printf("  Sink: DSDIFF Edit Master -> output/symphony_master.dff\n");
    printf("  Result: %s\n", result == DSDPIPE_OK ? "OK" : dsdpipe_get_error_message(pipe));

    /* Set progress callback */
    dsdpipe_set_progress_callback(pipe, progress_callback, (void *)"EDIT_MASTER");

    printf("\nStarting pipeline...\n");
    printf(SUBSEP "\n");

    result = dsdpipe_run(pipe);

    printf(SUBSEP "\n");
    printf("Pipeline finished: %s (result=%d)\n",
           result == DSDPIPE_OK ? "SUCCESS" : "FAILED", result);

    if (result != DSDPIPE_OK) {
        printf("Error: %s\n", dsdpipe_get_error_message(pipe));
    }

    dsdpipe_destroy(pipe);
    printf("\n");
}

/*============================================================================
 * Scenario 4: DSD to PCM Conversion (WAV Output)
 *============================================================================*/

static void test_scenario_dsd_to_wav(void)
{
    printf(SEPARATOR "\n");
    printf("SCENARIO 4: DSD -> PCM Conversion -> WAV Files\n");
    printf(SEPARATOR "\n");
    printf("Testing: SACD source -> DST decoder -> DSD2PCM -> WAV sink\n\n");

    dsdpipe_t *pipe = dsdpipe_create();
    if (!pipe) {
        printf("ERROR: Failed to create pipeline\n");
        return;
    }

    /* Configure source */
    printf("Configuring pipeline...\n");
    int result = dsdpipe_set_source_sacd(pipe, "virtual_vocal.iso", DSDPIPE_CHANNEL_STEREO);
    printf("  Source: virtual_vocal.iso (stereo)\n");
    printf("  Result: %s\n", result == DSDPIPE_OK ? "OK" : dsdpipe_get_error_message(pipe));

    /* Select tracks */
    result = dsdpipe_select_tracks_str(pipe, "1-3");
    printf("  Tracks: 1-3\n");
    printf("  Result: %s\n", result == DSDPIPE_OK ? "OK" : dsdpipe_get_error_message(pipe));

    /* Add WAV sink (24-bit, 88.2kHz) */
    result = dsdpipe_add_sink_wav(pipe, "output/vocal", 24, 88200);
    printf("  Sink: WAV 24-bit @ 88.2kHz -> output/vocal_trackNN.wav\n");
    printf("  Result: %s\n", result == DSDPIPE_OK ? "OK" : dsdpipe_get_error_message(pipe));

    /* Configure PCM conversion */
    dsdpipe_set_pcm_quality(pipe, DSDPIPE_PCM_QUALITY_HIGH);
    dsdpipe_set_pcm_use_fp64(pipe, false);
    printf("  PCM Quality: HIGH, FP64: false\n");

    /* Set progress callback */
    dsdpipe_set_progress_callback(pipe, progress_callback, (void *)"DSD2WAV");

    printf("\nStarting pipeline...\n");
    printf(SUBSEP "\n");

    result = dsdpipe_run(pipe);

    printf(SUBSEP "\n");
    printf("Pipeline finished: %s (result=%d)\n",
           result == DSDPIPE_OK ? "SUCCESS" : "FAILED", result);

    if (result != DSDPIPE_OK) {
        printf("Error: %s\n", dsdpipe_get_error_message(pipe));
    }

    dsdpipe_destroy(pipe);
    printf("\n");
}

/*============================================================================
 * Scenario 5: Multiple Simultaneous Sinks
 *============================================================================*/

static void test_scenario_multi_sink(void)
{
    printf(SEPARATOR "\n");
    printf("SCENARIO 5: Multiple Simultaneous Sinks\n");
    printf(SEPARATOR "\n");
    printf("Testing: SACD source -> DST decoder -> DSF + DSDIFF + WAV sinks\n\n");

    dsdpipe_t *pipe = dsdpipe_create();
    if (!pipe) {
        printf("ERROR: Failed to create pipeline\n");
        return;
    }

    /* Configure source */
    printf("Configuring pipeline...\n");
    int result = dsdpipe_set_source_sacd(pipe, "virtual_rock.iso", DSDPIPE_CHANNEL_STEREO);
    printf("  Source: virtual_rock.iso (stereo)\n");

    /* Select tracks */
    result = dsdpipe_select_tracks_str(pipe, "1-2");
    printf("  Tracks: 1-2\n");

    /* Add multiple sinks */
    result = dsdpipe_add_sink_dsf(pipe, "output/rock_dsf", true);
    printf("  Sink 1: DSF with ID3\n");

    result = dsdpipe_add_sink_dsdiff(pipe, "output/rock_master.dff", false, true);
    printf("  Sink 2: DSDIFF Edit Master\n");

    result = dsdpipe_add_sink_wav(pipe, "output/rock_wav", 24, 96000);
    printf("  Sink 3: WAV 24-bit @ 96kHz\n");

    printf("  Total sinks: %d\n", dsdpipe_get_sink_count(pipe));

    /* Configure PCM conversion for WAV sink */
    dsdpipe_set_pcm_quality(pipe, DSDPIPE_PCM_QUALITY_NORMAL);

    /* Set progress callback */
    dsdpipe_set_progress_callback(pipe, progress_callback, (void *)"MULTI");

    printf("\nStarting pipeline...\n");
    printf(SUBSEP "\n");

    result = dsdpipe_run(pipe);

    printf(SUBSEP "\n");
    printf("Pipeline finished: %s (result=%d)\n",
           result == DSDPIPE_OK ? "SUCCESS" : "FAILED", result);

    if (result != DSDPIPE_OK) {
        printf("Error: %s\n", dsdpipe_get_error_message(pipe));
    }

    dsdpipe_destroy(pipe);
    printf("\n");
}

/*============================================================================
 * Scenario 6: Pipeline Reset and Reuse
 *============================================================================*/

static void test_scenario_reset_reuse(void)
{
    printf(SEPARATOR "\n");
    printf("SCENARIO 6: Pipeline Reset and Reuse\n");
    printf(SEPARATOR "\n");

    dsdpipe_t *pipe = dsdpipe_create();
    if (!pipe) {
        printf("ERROR: Failed to create pipeline\n");
        return;
    }

    /* First run: tracks 1-2 to DSF */
    printf("First run: tracks 1-2 to DSF\n");
    dsdpipe_set_source_sacd(pipe, "virtual_album.iso", DSDPIPE_CHANNEL_STEREO);
    dsdpipe_select_tracks_str(pipe, "1-2");
    dsdpipe_add_sink_dsf(pipe, "output/run1", true);
    dsdpipe_set_progress_callback(pipe, progress_callback, (void *)"RUN1");

    printf(SUBSEP "\n");
    int result = dsdpipe_run(pipe);
    printf(SUBSEP "\n");
    printf("Run 1 result: %s\n\n", result == DSDPIPE_OK ? "SUCCESS" : "FAILED");

    /* Reset and reuse */
    printf("Resetting pipeline...\n\n");
    dsdpipe_reset(pipe);

    /* Second run: tracks 3-4 to DSDIFF Edit Master */
    printf("Second run: tracks 3-4 to DSDIFF Edit Master\n");
    dsdpipe_select_tracks_str(pipe, "3-4");
    dsdpipe_add_sink_dsdiff(pipe, "output/run2_master.dff", false, true);
    dsdpipe_set_progress_callback(pipe, progress_callback, (void *)"RUN2");

    printf(SUBSEP "\n");
    result = dsdpipe_run(pipe);
    printf(SUBSEP "\n");
    printf("Run 2 result: %s\n\n", result == DSDPIPE_OK ? "SUCCESS" : "FAILED");

    /* Reset and third run */
    printf("Resetting pipeline...\n\n");
    dsdpipe_reset(pipe);

    /* Third run: track 5 to WAV */
    printf("Third run: track 5 to WAV\n");
    dsdpipe_select_tracks_str(pipe, "5");
    dsdpipe_add_sink_wav(pipe, "output/run3", 24, 88200);
    dsdpipe_set_progress_callback(pipe, progress_callback, (void *)"RUN3");

    printf(SUBSEP "\n");
    result = dsdpipe_run(pipe);
    printf(SUBSEP "\n");
    printf("Run 3 result: %s\n", result == DSDPIPE_OK ? "SUCCESS" : "FAILED");

    dsdpipe_destroy(pipe);
    printf("\n");
}

/*============================================================================
 * Scenario 7: DST Passthrough to DSDIFF
 *============================================================================*/

static void test_scenario_dst_passthrough(void)
{
    printf(SEPARATOR "\n");
    printf("SCENARIO 7: DST Passthrough to DSDIFF\n");
    printf(SEPARATOR "\n");
    printf("Testing: SACD source (DST) -> DSDIFF sink (DST passthrough)\n");
    printf("         (No DST decoding - compressed data written directly)\n\n");

    dsdpipe_t *pipe = dsdpipe_create();
    if (!pipe) {
        printf("ERROR: Failed to create pipeline\n");
        return;
    }

    /* Configure source */
    printf("Configuring pipeline...\n");
    int result = dsdpipe_set_source_sacd(pipe, "virtual_dst_album.iso", DSDPIPE_CHANNEL_STEREO);
    printf("  Source: virtual_dst_album.iso (stereo, DST encoded)\n");

    /* Select tracks */
    result = dsdpipe_select_tracks_str(pipe, "1-3");
    printf("  Tracks: 1-3\n");

    /* Add DSDIFF sink with DST passthrough (write_dst=true) */
    result = dsdpipe_add_sink_dsdiff(pipe, "output/dst_passthrough.dff", true, true);
    printf("  Sink: DSDIFF with DST passthrough (edit master)\n");
    printf("  Result: %s\n", result == DSDPIPE_OK ? "OK" : dsdpipe_get_error_message(pipe));

    /* Set progress callback */
    dsdpipe_set_progress_callback(pipe, progress_callback, (void *)"DST_PASS");

    printf("\nStarting pipeline...\n");
    printf(SUBSEP "\n");

    result = dsdpipe_run(pipe);

    printf(SUBSEP "\n");
    printf("Pipeline finished: %s (result=%d)\n",
           result == DSDPIPE_OK ? "SUCCESS" : "FAILED", result);

    dsdpipe_destroy(pipe);
    printf("\n");
}

/*============================================================================
 * Main
 *============================================================================*/

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("\n");
    printf(SEPARATOR "\n");
    printf("   libdsdpipe Comprehensive Test Suite\n");
    printf(SEPARATOR "\n");
    printf("\n");

    /* Basic tests */
    test_version();
    test_error_strings();
    test_track_selection();
    test_metadata();

    /* Pipeline scenarios */
    test_scenario_multiple_tracks_dsf();
    test_scenario_multiple_tracks_dsdiff();
    test_scenario_edit_master();
    test_scenario_dsd_to_wav();
    test_scenario_multi_sink();
    test_scenario_reset_reuse();
    test_scenario_dst_passthrough();

    printf(SEPARATOR "\n");
    printf("   All tests completed!\n");
    printf(SEPARATOR "\n");
    printf("\n");

    return 0;
}

/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Track selection parsing and management
 * Supports track selection specifications like:
 * - "all" - All tracks
 * - "1" - Single track
 * - "1,3,5" - Specific tracks
 * - "1-5" - Range of tracks
 * - "1-3,5,7-9" - Combination
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


#include "dsdpipe_internal.h"

#include <libsautil/mem.h>

#include <string.h>
#include <ctype.h>

/*============================================================================
 * Constants
 *============================================================================*/

#define INITIAL_CAPACITY 16

/*============================================================================
 * Track Selection Lifecycle
 *============================================================================*/

int dsdpipe_track_selection_init(dsdpipe_track_selection_t *sel)
{
    if (!sel) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    sel->tracks = (uint8_t *)sa_calloc(INITIAL_CAPACITY, sizeof(uint8_t));
    if (!sel->tracks) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    sel->count = 0;
    sel->capacity = INITIAL_CAPACITY;
    sel->current_idx = 0;

    return DSDPIPE_OK;
}

void dsdpipe_track_selection_free(dsdpipe_track_selection_t *sel)
{
    if (!sel) {
        return;
    }

    if (sel->tracks) {
        sa_free(sel->tracks);
        sel->tracks = NULL;
    }

    sel->count = 0;
    sel->capacity = 0;
    sel->current_idx = 0;
}

void dsdpipe_track_selection_clear(dsdpipe_track_selection_t *sel)
{
    if (!sel) {
        return;
    }

    sel->count = 0;
    sel->current_idx = 0;
}

/*============================================================================
 * Track Selection Operations
 *============================================================================*/

/**
 * @brief Ensure capacity for at least one more track
 */
static int dsdpipe_track_selection_ensure_capacity(dsdpipe_track_selection_t *sel)
{
    if (sel->count < sel->capacity) {
        return DSDPIPE_OK;
    }

    size_t new_capacity = sel->capacity * 2;
    if (new_capacity > DSDPIPE_MAX_TRACKS) {
        new_capacity = DSDPIPE_MAX_TRACKS;
    }

    if (sel->count >= new_capacity) {
        return DSDPIPE_ERROR_INVALID_ARG;  /* Too many tracks */
    }

    uint8_t *new_tracks = (uint8_t *)sa_realloc(sel->tracks,
                                                new_capacity * sizeof(uint8_t));
    if (!new_tracks) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    sel->tracks = new_tracks;
    sel->capacity = new_capacity;

    return DSDPIPE_OK;
}

/**
 * @brief Check if a track is already in the selection
 */
static bool dsdpipe_track_selection_contains(const dsdpipe_track_selection_t *sel,
                                              uint8_t track)
{
    for (size_t i = 0; i < sel->count; i++) {
        if (sel->tracks[i] == track) {
            return true;
        }
    }
    return false;
}

int dsdpipe_track_selection_add(dsdpipe_track_selection_t *sel, uint8_t track)
{
    if (!sel) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (track == 0) {
        return DSDPIPE_ERROR_INVALID_ARG;  /* Tracks are 1-based */
    }

    /* Don't add duplicates */
    if (dsdpipe_track_selection_contains(sel, track)) {
        return DSDPIPE_OK;
    }

    int result = dsdpipe_track_selection_ensure_capacity(sel);
    if (result != DSDPIPE_OK) {
        return result;
    }

    sel->tracks[sel->count++] = track;
    return DSDPIPE_OK;
}

/*============================================================================
 * Track Selection Parsing
 *============================================================================*/

/**
 * @brief Skip whitespace in string
 */
static const char *skip_whitespace(const char *str)
{
    while (*str && isspace((unsigned char)*str)) {
        str++;
    }
    return str;
}

/**
 * @brief Parse an unsigned integer from string
 *
 * @param str Input string pointer (updated on success)
 * @param value Output value
 * @return true on success, false if no number found
 */
static bool parse_uint(const char **str, unsigned int *value)
{
    const char *p = *str;
    unsigned int v = 0;
    bool found = false;

    p = skip_whitespace(p);

    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (unsigned int)(*p - '0');
        p++;
        found = true;
    }

    if (found) {
        *str = p;
        *value = v;
    }

    return found;
}

int dsdpipe_track_selection_parse(dsdpipe_track_selection_t *sel,
                                   const char *str, uint8_t max_track)
{
    if (!sel || !str) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    dsdpipe_track_selection_clear(sel);

    const char *p = skip_whitespace(str);

    /* Check for "all" keyword */
    if (strncmp(p, "all", 3) == 0) {
        const char *after = p + 3;
        after = skip_whitespace(after);
        if (*after == '\0') {
            /* Add all tracks */
            for (uint8_t t = 1; t <= max_track; t++) {
                int result = dsdpipe_track_selection_add(sel, t);
                if (result != DSDPIPE_OK) {
                    return result;
                }
            }
            return DSDPIPE_OK;
        }
        /* "all" followed by something else - continue parsing as normal */
    }

    /* Parse comma-separated items */
    while (*p) {
        p = skip_whitespace(p);

        if (*p == '\0') {
            break;
        }

        /* Parse first number */
        unsigned int start;
        if (!parse_uint(&p, &start)) {
            return DSDPIPE_ERROR_INVALID_TRACK_SPEC;
        }

        if (start == 0 || start > max_track) {
            return DSDPIPE_ERROR_TRACK_NOT_FOUND;
        }

        p = skip_whitespace(p);

        /* Check for range (dash) */
        if (*p == '-') {
            p++;  /* Skip dash */
            p = skip_whitespace(p);

            unsigned int end;
            if (!parse_uint(&p, &end)) {
                return DSDPIPE_ERROR_INVALID_TRACK_SPEC;
            }

            if (end == 0 || end > max_track) {
                return DSDPIPE_ERROR_TRACK_NOT_FOUND;
            }

            /* Add range */
            if (start <= end) {
                for (unsigned int t = start; t <= end; t++) {
                    int result = dsdpipe_track_selection_add(sel, (uint8_t)t);
                    if (result != DSDPIPE_OK) {
                        return result;
                    }
                }
            } else {
                /* Reverse range (e.g., "5-1") */
                for (unsigned int t = start; t >= end; t--) {
                    int result = dsdpipe_track_selection_add(sel, (uint8_t)t);
                    if (result != DSDPIPE_OK) {
                        return result;
                    }
                }
            }
        } else {
            /* Single track */
            int result = dsdpipe_track_selection_add(sel, (uint8_t)start);
            if (result != DSDPIPE_OK) {
                return result;
            }
        }

        p = skip_whitespace(p);

        /* Expect comma or end of string */
        if (*p == ',') {
            p++;  /* Skip comma */
        } else if (*p != '\0') {
            return DSDPIPE_ERROR_INVALID_TRACK_SPEC;
        }
    }

    /* Must have at least one track */
    if (sel->count == 0) {
        return DSDPIPE_ERROR_INVALID_TRACK_SPEC;
    }

    return DSDPIPE_OK;
}

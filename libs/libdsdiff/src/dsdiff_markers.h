/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief DSDIFF marker list management
 * This module manages DSD markers using a linked list. It provides
 * operations for adding, deleting, retrieving, and sorting markers.
 * Markers are used for track starts/stops, program boundaries, and
 * index points within DSDIFF files.
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

#ifndef LIBDSDIFF_DSDIFF_MARKERS_H
#define LIBDSDIFF_DSDIFF_MARKERS_H

#include <libdsdiff/dsdiff.h>
#include <libsautil/ll.h>

#include "dsdiff_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Structures
 * ===========================================================================*/

/**
 * @brief Marker list entry
 *
 * This structure contains a DSD marker and is embedded in an intrusive
 * linked list. The ll_t structure allows efficient list operations
 * without separate memory allocations.
 */
struct dsdiff_marker_entry_s {
    ll_t              list;         /**< Intrusive list linkage */
    dsdiff_marker_t   marker;       /**< The marker data */
    uint32_t          sample_rate;  /**< Sample frequency for this marker */
};
typedef struct dsdiff_marker_entry_s dsdiff_marker_entry_t;

/**
 * @brief Marker list head
 *
 * Manages a collection of DSD markers with efficient iteration and sorting.
 */
struct dsdiff_marker_list_s {
    ll_t     list_head;     /**< List head */
    uint32_t count;         /**< Number of markers in list */
};
typedef struct dsdiff_marker_list_s dsdiff_marker_list_t;

/* =============================================================================
 * List Management Functions
 * ===========================================================================*/

/**
 * @brief Initialize marker list
 *
 * Must be called before using the list.
 *
 * @param list Marker list to initialize
 */
void dsdiff_marker_list_init(dsdiff_marker_list_t *list);

/**
 * @brief Free all markers in list
 *
 * Frees all marker entries and their associated text data.
 * List can be reused after calling this function.
 *
 * @param list Marker list to clear
 */
void dsdiff_marker_list_free(dsdiff_marker_list_t *list);

/**
 * @brief Get number of markers in list
 *
 * @param list Marker list
 * @return Number of markers
 */
uint32_t dsdiff_marker_list_get_count(const dsdiff_marker_list_t *list);

/**
 * @brief Check if list is empty
 *
 * @param list Marker list
 * @return 1 if empty, 0 if contains markers
 */
int dsdiff_marker_list_is_empty(const dsdiff_marker_list_t *list);

/* =============================================================================
 * Marker Operations
 * ===========================================================================*/

/**
 * @brief Add marker to list
 *
 * Creates a copy of the marker and adds it to the end of the list.
 * The marker text is duplicated.
 *
 * @param list Marker list
 * @param marker Marker to add
 * @param sample_rate Sample frequency for time calculations
 * @return 0 on success, negative error code on failure
 */
int dsdiff_marker_list_add(dsdiff_marker_list_t *list,
                            const dsdiff_marker_t *marker,
                            uint32_t sample_rate);

/**
 * @brief Delete marker at index
 *
 * Removes and frees the marker at the specified index (0-based).
 *
 * @param list Marker list
 * @param index Index of marker to delete (0-based)
 * @return 0 on success, negative error code on failure
 */
int dsdiff_marker_list_remove(dsdiff_marker_list_t *list, uint32_t index);

/**
 * @brief Retrieve marker at index
 *
 * Gets a copy of the marker at the specified index (0-based).
 * The caller is responsible for freeing marker->marker_text.
 *
 * @param list Marker list
 * @param index Index of marker to retrieve (0-based)
 * @param marker Pointer to receive marker copy
 * @return 0 on success, negative error code on failure
 */
int dsdiff_marker_list_get(const dsdiff_marker_list_t *list, uint32_t index,
                            dsdiff_marker_t *marker, uint32_t *sample_rate);

/**
 * @brief Sort markers by timestamp
 *
 * Sorts markers in chronological order based on their timecode.
 * When timestamps are equal, TrackStart markers come before others.
 *
 * @param list Marker list to sort
 */
void dsdiff_marker_list_sort(dsdiff_marker_list_t *list);

/* =============================================================================
 * Marker Entry Helper Functions (internal use)
 * ===========================================================================*/

/**
 * @brief Create a marker entry
 *
 * Allocates and initializes a marker entry. The marker text is duplicated.
 *
 * @param marker Marker data to copy
 * @param sample_rate Sample frequency
 * @return Pointer to new entry, or NULL on failure
 */
dsdiff_marker_entry_t *dsdiff_marker_entry_create(const dsdiff_marker_t *marker,
                                                    uint32_t sample_rate);

/**
 * @brief Free a marker entry
 *
 * Frees the marker entry and its associated text data.
 *
 * @param entry Marker entry to free
 */
void dsdiff_marker_entry_free(dsdiff_marker_entry_t *entry);

/**
 * @brief Create a marker
 *
 * Allocates and initializes an empty marker.
 *
 * @return Pointer to new marker, or NULL on failure
 */
dsdiff_marker_t *dsdiff_marker_create(void);

/**
 * @brief Free a marker
 *
 * Frees the marker and its associated text data.
 *
 * @param marker Marker to free
 */
void dsdiff_marker_free(dsdiff_marker_t *marker);

#ifdef __cplusplus
}
#endif

#endif /* LIBDSDIFF_DSDIFF_MARKERS_H */

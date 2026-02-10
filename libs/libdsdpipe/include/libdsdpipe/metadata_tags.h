/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief AVTree-based key-value metadata tag storage
 * Provides O(log n) performance for set/get/remove operations.
 * Used for storing arbitrary metadata tags (ID3 frames, custom fields).
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

#ifndef LIBDSDPIPE_METADATA_TAGS_H
#define LIBDSDPIPE_METADATA_TAGS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <libdsdpipe/dsdpipe_export.h>

/*============================================================================
 * Types
 *============================================================================*/

/**
 * @brief Opaque handle to metadata tag storage
 */
typedef struct metadata_tags_s metadata_tags_t;

/**
 * @brief Callback function for tag enumeration
 *
 * @param ctx User-provided context
 * @param key Tag key (e.g., "TIT2", "artist")
 * @param value Tag value (UTF-8 string)
 * @return 0 to continue enumeration, non-zero to stop
 */
typedef int (*metadata_tags_callback_t)(void *ctx, const char *key, const char *value);

/*============================================================================
 * Lifecycle Functions
 *============================================================================*/

/**
 * @brief Create a new metadata tags container
 *
 * @return New container, or NULL on allocation failure
 */
DSDPIPE_API metadata_tags_t *metadata_tags_create(void);

/**
 * @brief Destroy a metadata tags container and free all memory
 *
 * @param tags Container to destroy (may be NULL)
 */
void DSDPIPE_API metadata_tags_destroy(metadata_tags_t *tags);

/**
 * @brief Create a deep copy of a metadata tags container
 *
 * @param src Source container to copy
 * @return New container with copied data, or NULL on failure
 */
DSDPIPE_API metadata_tags_t *metadata_tags_copy(const metadata_tags_t *src);

/*============================================================================
 * Tag Operations (O(log n) complexity)
 *============================================================================*/

/**
 * @brief Set a tag value
 *
 * If the key already exists, its value is replaced.
 * Both key and value are copied internally.
 *
 * @param tags Container
 * @param key Tag key (must not be NULL or empty)
 * @param value Tag value (must not be NULL)
 * @return 0 on success, negative error code on failure
 */
int DSDPIPE_API metadata_tags_set(metadata_tags_t *tags, const char *key, const char *value);

/**
 * @brief Get a tag value
 *
 * @param tags Container
 * @param key Tag key to look up
 * @return Tag value (do not free), or NULL if not found
 */
DSDPIPE_API const char *metadata_tags_get(const metadata_tags_t *tags, const char *key);

/**
 * @brief Check if a tag exists
 *
 * @param tags Container
 * @param key Tag key to check
 * @return 1 if tag exists, 0 otherwise
 */
int DSDPIPE_API metadata_tags_has(const metadata_tags_t *tags, const char *key);

/**
 * @brief Remove a tag
 *
 * @param tags Container
 * @param key Tag key to remove
 * @return 0 on success, negative if not found
 */
int DSDPIPE_API metadata_tags_remove(metadata_tags_t *tags, const char *key);

/**
 * @brief Get the number of tags
 *
 * @param tags Container
 * @return Number of tags, or 0 if tags is NULL
 */
size_t DSDPIPE_API metadata_tags_count(const metadata_tags_t *tags);

/**
 * @brief Clear all tags
 *
 * @param tags Container
 */
void DSDPIPE_API metadata_tags_clear(metadata_tags_t *tags);

/*============================================================================
 * Enumeration
 *============================================================================*/

/**
 * @brief Enumerate all tags in sorted order by key
 *
 * Calls the callback for each tag. Enumeration can be stopped early
 * by returning non-zero from the callback.
 *
 * @param tags Container
 * @param ctx User-provided context passed to callback
 * @param callback Function to call for each tag
 */
void DSDPIPE_API metadata_tags_enumerate(const metadata_tags_t *tags,
                              void *ctx,
                              metadata_tags_callback_t callback);

/*============================================================================
 * Bulk Operations
 *============================================================================*/

/**
 * @brief Merge tags from source into destination
 *
 * Tags from src are copied to dest. If a key already exists in dest,
 * it is overwritten if overwrite is true, otherwise skipped.
 *
 * @param dest Destination container
 * @param src Source container
 * @param overwrite If true, overwrite existing keys
 * @return 0 on success, negative error code on failure
 */
int DSDPIPE_API metadata_tags_merge(metadata_tags_t *dest,
                         const metadata_tags_t *src,
                         int overwrite);

#ifdef __cplusplus
}
#endif

#endif /* LIBDSDPIPE_METADATA_TAGS_H */

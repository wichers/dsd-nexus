/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief AVTree-based key-value metadata tag storage implementation
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


#include <libdsdpipe/metadata_tags.h>
#include <libsautil/tree.h>
#include <libsautil/mem.h>
#include <libsautil/sastring.h>

#include <string.h>
#include <stdlib.h>

/*============================================================================
 * Internal Types
 *============================================================================*/

/**
 * @brief Tag entry stored in the AVTree
 */
typedef struct metadata_tag_entry_s {
    char *key;      /**< Tag key (owned) */
    char *value;    /**< Tag value (owned) */
} metadata_tag_entry_t;

/**
 * @brief Metadata tags container
 */
struct metadata_tags_s {
    struct AVTreeNode *root;    /**< AVTree root node */
    size_t count;               /**< Number of tags */
};

/*============================================================================
 * Internal Functions
 *============================================================================*/

/**
 * @brief Comparison function for AVTree
 */
static int tag_entry_cmp(const void *a, const void *b)
{
    const metadata_tag_entry_t *ta = (const metadata_tag_entry_t *)a;
    const metadata_tag_entry_t *tb = (const metadata_tag_entry_t *)b;
    return strcmp(ta->key, tb->key);
}

/**
 * @brief Create a new tag entry
 */
static metadata_tag_entry_t *tag_entry_create(const char *key, const char *value)
{
    metadata_tag_entry_t *entry;

    if (!key || !value) {
        return NULL;
    }

    entry = sa_mallocz(sizeof(metadata_tag_entry_t));
    if (!entry) {
        return NULL;
    }

    entry->key = sa_strdup(key);
    if (!entry->key) {
        sa_free(entry);
        return NULL;
    }

    entry->value = sa_strdup(value);
    if (!entry->value) {
        sa_free(entry->key);
        sa_free(entry);
        return NULL;
    }

    return entry;
}

/**
 * @brief Free a tag entry
 */
static void tag_entry_free(metadata_tag_entry_t *entry)
{
    if (entry) {
        sa_free(entry->key);
        sa_free(entry->value);
        sa_free(entry);
    }
}

/**
 * @brief Context for destroy enumeration
 */
typedef struct {
    int dummy;
} destroy_ctx_t;

/**
 * @brief Enumeration callback to free entries during destroy
 */
static int destroy_enum_cb(void *opaque, void *elem)
{
    (void)opaque;
    tag_entry_free((metadata_tag_entry_t *)elem);
    return 0;
}

/**
 * @brief Comparison function for enumeration (matches all)
 */
static int enum_all_cmp(void *opaque, void *elem)
{
    (void)opaque;
    (void)elem;
    return 0;  /* Return 0 to visit all nodes */
}

/*============================================================================
 * Lifecycle Functions
 *============================================================================*/

metadata_tags_t *metadata_tags_create(void)
{
    metadata_tags_t *tags = sa_mallocz(sizeof(metadata_tags_t));
    if (!tags) {
        return NULL;
    }
    tags->root = NULL;
    tags->count = 0;
    return tags;
}

void metadata_tags_destroy(metadata_tags_t *tags)
{
    if (!tags) {
        return;
    }

    /* Free all entries */
    if (tags->root) {
        destroy_ctx_t ctx = {0};
        sa_tree_enumerate(tags->root, &ctx, enum_all_cmp, destroy_enum_cb);
        sa_tree_destroy(tags->root);
    }

    sa_free(tags);
}

/**
 * @brief Context for copy enumeration
 */
typedef struct {
    metadata_tags_t *dest;
    int error;
} copy_ctx_t;

/**
 * @brief Enumeration callback to copy entries
 */
static int copy_enum_cb(void *opaque, void *elem)
{
    copy_ctx_t *ctx = (copy_ctx_t *)opaque;
    metadata_tag_entry_t *entry = (metadata_tag_entry_t *)elem;

    if (metadata_tags_set(ctx->dest, entry->key, entry->value) < 0) {
        ctx->error = -1;
        return 1;  /* Stop enumeration */
    }
    return 0;
}

metadata_tags_t *metadata_tags_copy(const metadata_tags_t *src)
{
    metadata_tags_t *dest;
    copy_ctx_t ctx;

    if (!src) {
        return NULL;
    }

    dest = metadata_tags_create();
    if (!dest) {
        return NULL;
    }

    ctx.dest = dest;
    ctx.error = 0;

    if (src->root) {
        sa_tree_enumerate(src->root, &ctx, enum_all_cmp, copy_enum_cb);
    }

    if (ctx.error) {
        metadata_tags_destroy(dest);
        return NULL;
    }

    return dest;
}

/*============================================================================
 * Tag Operations
 *============================================================================*/

int metadata_tags_set(metadata_tags_t *tags, const char *key, const char *value)
{
    metadata_tag_entry_t search_key;
    metadata_tag_entry_t *existing;
    metadata_tag_entry_t *new_entry;
    struct AVTreeNode *node = NULL;
    void *result;

    if (!tags || !key || !key[0] || !value) {
        return -1;
    }

    /* Check if key already exists */
    search_key.key = (char *)key;
    search_key.value = NULL;

    existing = sa_tree_find(tags->root, &search_key, tag_entry_cmp, NULL);
    if (existing) {
        /* Update existing entry */
        char *new_value = sa_strdup(value);
        if (!new_value) {
            return -1;
        }
        sa_free(existing->value);
        existing->value = new_value;
        return 0;
    }

    /* Create new entry */
    new_entry = tag_entry_create(key, value);
    if (!new_entry) {
        return -1;
    }

    /* Allocate tree node */
    node = sa_tree_node_alloc();
    if (!node) {
        tag_entry_free(new_entry);
        return -1;
    }

    /* Insert into tree */
    result = sa_tree_insert(&tags->root, new_entry, tag_entry_cmp, &node);
    (void)result;

    if (node) {
        /* Node was not consumed: element already exists (shouldn't happen
         * since we checked above, but handle defensively) */
        sa_free(node);
        tag_entry_free(new_entry);
        return -1;
    }

    /* Node was consumed: insertion succeeded */
    tags->count++;
    return 0;
}

const char *metadata_tags_get(const metadata_tags_t *tags, const char *key)
{
    metadata_tag_entry_t search_key;
    metadata_tag_entry_t *found;

    if (!tags || !key || !key[0]) {
        return NULL;
    }

    search_key.key = (char *)key;
    search_key.value = NULL;

    found = sa_tree_find(tags->root, &search_key, tag_entry_cmp, NULL);
    if (found) {
        return found->value;
    }

    return NULL;
}

int metadata_tags_has(const metadata_tags_t *tags, const char *key)
{
    return metadata_tags_get(tags, key) != NULL;
}

int metadata_tags_remove(metadata_tags_t *tags, const char *key)
{
    metadata_tag_entry_t search_key;
    metadata_tag_entry_t *found;
    struct AVTreeNode *node = NULL;

    if (!tags || !key || !key[0]) {
        return -1;
    }

    search_key.key = (char *)key;
    search_key.value = NULL;

    /* Check if key exists */
    found = sa_tree_find(tags->root, &search_key, tag_entry_cmp, NULL);
    if (!found) {
        return -1;  /* Not found */
    }

    /* Remove from tree (pass NULL node to delete) */
    (void)sa_tree_insert(&tags->root, found, tag_entry_cmp, &node);

    /* Free the removed node if returned */
    if (node) {
        sa_free(node);
    }

    /* Free the entry */
    tag_entry_free(found);
    tags->count--;

    return 0;
}

size_t metadata_tags_count(const metadata_tags_t *tags)
{
    if (!tags) {
        return 0;
    }
    return tags->count;
}

void metadata_tags_clear(metadata_tags_t *tags)
{
    if (!tags) {
        return;
    }

    /* Free all entries */
    if (tags->root) {
        destroy_ctx_t ctx = {0};
        sa_tree_enumerate(tags->root, &ctx, enum_all_cmp, destroy_enum_cb);
        sa_tree_destroy(tags->root);
        tags->root = NULL;
    }

    tags->count = 0;
}

/*============================================================================
 * Enumeration
 *============================================================================*/

/**
 * @brief Context for user enumeration
 */
typedef struct {
    void *user_ctx;
    metadata_tags_callback_t callback;
    int stopped;
} enum_user_ctx_t;

/**
 * @brief Internal enumeration callback
 */
static int enum_user_cb(void *opaque, void *elem)
{
    enum_user_ctx_t *ctx = (enum_user_ctx_t *)opaque;
    metadata_tag_entry_t *entry = (metadata_tag_entry_t *)elem;

    if (ctx->stopped) {
        return 1;  /* Already stopped */
    }

    if (ctx->callback(ctx->user_ctx, entry->key, entry->value) != 0) {
        ctx->stopped = 1;
        return 1;  /* Stop enumeration */
    }

    return 0;
}

void metadata_tags_enumerate(const metadata_tags_t *tags,
                              void *ctx,
                              metadata_tags_callback_t callback)
{
    enum_user_ctx_t enum_ctx;

    if (!tags || !callback) {
        return;
    }

    if (!tags->root) {
        return;
    }

    enum_ctx.user_ctx = ctx;
    enum_ctx.callback = callback;
    enum_ctx.stopped = 0;

    sa_tree_enumerate(tags->root, &enum_ctx, enum_all_cmp, enum_user_cb);
}

/*============================================================================
 * Bulk Operations
 *============================================================================*/

/**
 * @brief Context for merge enumeration
 */
typedef struct {
    metadata_tags_t *dest;
    int overwrite;
    int error;
} merge_ctx_t;

/**
 * @brief Enumeration callback for merge
 */
static int merge_enum_cb(void *opaque, void *elem)
{
    merge_ctx_t *ctx = (merge_ctx_t *)opaque;
    metadata_tag_entry_t *entry = (metadata_tag_entry_t *)elem;

    /* Skip if exists and not overwriting */
    if (!ctx->overwrite && metadata_tags_has(ctx->dest, entry->key)) {
        return 0;
    }

    if (metadata_tags_set(ctx->dest, entry->key, entry->value) < 0) {
        ctx->error = -1;
        return 1;  /* Stop on error */
    }

    return 0;
}

int metadata_tags_merge(metadata_tags_t *dest,
                         const metadata_tags_t *src,
                         int overwrite)
{
    merge_ctx_t ctx;

    if (!dest || !src) {
        return -1;
    }

    if (!src->root) {
        return 0;  /* Nothing to merge */
    }

    ctx.dest = dest;
    ctx.overwrite = overwrite;
    ctx.error = 0;

    sa_tree_enumerate(src->root, &ctx, enum_all_cmp, merge_enum_cb);

    return ctx.error;
}

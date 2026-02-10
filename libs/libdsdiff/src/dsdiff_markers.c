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

#include <libdsdiff/dsdiff.h>
#include <libsautil/ll.h>
#include <libsautil/mem.h>

#include "dsdiff_markers.h"

#include <stdlib.h>
#include <string.h>

/* =============================================================================
 * List Initialization and Cleanup
 * ===========================================================================*/

void dsdiff_marker_list_init(dsdiff_marker_list_t *list) {
    if (!list) {
        return;
    }

    INIT_LIST_HEAD(&list->list_head);
    list->count = 0;
}

void dsdiff_marker_list_free(dsdiff_marker_list_t *list) {
    dsdiff_marker_entry_t *entry, *tmp = NULL;

    if (!list) {
        return;
    }

    ll_for_each_entry_safe(entry, tmp, &(list->list_head), list) {
        list_del(&entry->list);
        dsdiff_marker_entry_free(entry);
    }

    list->count = 0;
}

/* =============================================================================
 * List Query Functions
 * ===========================================================================*/

uint32_t dsdiff_marker_list_get_count(const dsdiff_marker_list_t *list) {
    if (!list) {
        return 0;
    }

    return list->count;
}

int dsdiff_marker_list_is_empty(const dsdiff_marker_list_t *list) {
    if (!list) {
        return 1;
    }

    return list->count == 0;
}

/* =============================================================================
 * List Modification Functions
 * ===========================================================================*/

int dsdiff_marker_list_add(dsdiff_marker_list_t *list,
                            const dsdiff_marker_t *marker,
                            uint32_t sample_rate) {
    dsdiff_marker_entry_t *entry;

    if (!list || !marker) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    entry = dsdiff_marker_entry_create(marker, sample_rate);
    if (!entry) {
        return DSDIFF_ERROR_OUT_OF_MEMORY;
    }

    list_add_tail(&entry->list, &list->list_head);
    list->count++;

    return DSDIFF_SUCCESS;
}

int dsdiff_marker_list_remove(dsdiff_marker_list_t *list, uint32_t index) {
    dsdiff_marker_entry_t *entry;
    uint32_t i = 0;

    if (!list || index >= list->count) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    list_for_each_entry(entry, &(list->list_head), list) {
        if (i == index) {
            list_del(&entry->list);
            dsdiff_marker_entry_free(entry);
            list->count--;
            return DSDIFF_SUCCESS;
        }
        i++;
    }

    return DSDIFF_ERROR_INVALID_ARG;
}

int dsdiff_marker_list_get(const dsdiff_marker_list_t *list, uint32_t index,
                            dsdiff_marker_t *marker, uint32_t *sample_rate) {
    dsdiff_marker_entry_t *entry;
    uint32_t i = 0;

    if (!list || !marker || index > list->count - 1) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    list_for_each_entry(entry, &(list->list_head), list) {
        if (i == index) {
            marker->mark_type = entry->marker.mark_type;
            marker->time.hours = entry->marker.time.hours;
            marker->time.minutes = entry->marker.time.minutes;
            marker->time.seconds = entry->marker.time.seconds;
            marker->time.samples = entry->marker.time.samples;

            if (entry->marker.marker_text) {
                marker->marker_text = sa_strdup(entry->marker.marker_text);
                if (marker->marker_text == 0) {
                    return DSDIFF_ERROR_OUT_OF_MEMORY;
                }
            } else {
                marker->marker_text = NULL;
            }

            if (sample_rate) {
                *sample_rate = entry->sample_rate;
            }

            return DSDIFF_SUCCESS;
        }
        i++;
    }

    return DSDIFF_ERROR_INVALID_ARG;
}

/* =============================================================================
 * Sorting Functions (Merge Sort Implementation)
 *
 * These functions implement a stable merge sort for the marker linked list.
 * The sort is based on marker timestamps, with TrackStart markers sorted
 * before other marker types at equal timestamps.
 * ===========================================================================*/

/**
 * Convert marker timecode to total sample count for comparison.
 *
 * Converts the hours:minutes:seconds:samples format to a single
 * sample count value for easier comparison during sorting.
 */
static uint64_t dsdiff_marker_to_samples(const dsdiff_marker_t *marker,
                                         uint32_t sample_rate) {
    uint64_t total_samples;

    if (!marker) {
        return 0;
    }

    total_samples = (uint64_t)marker->time.hours * 3600 * sample_rate;
    total_samples += (uint64_t)marker->time.minutes * 60 * sample_rate;
    total_samples += (uint64_t)marker->time.seconds * sample_rate;
    total_samples += marker->time.samples;

    return total_samples;
}

/**
 * Compare two marker entries for sorting.
 *
 * Primary sort key is the timestamp (converted to samples).
 * Secondary sort key: TrackStart markers come before other types
 * at the same timestamp position.
 *
 * @return -1 if entry1 < entry2, 0 if equal, 1 if entry1 > entry2
 */
static int dsdiff_marker_compare(const dsdiff_marker_entry_t *entry1,
                                 const dsdiff_marker_entry_t *entry2) {
    uint64_t samples1, samples2;

    if (!entry1 || !entry2) {
        return 0;
    }

    samples1 = dsdiff_marker_to_samples(&entry1->marker, entry1->sample_rate);
    samples2 = dsdiff_marker_to_samples(&entry2->marker, entry2->sample_rate);

    if (samples1 < samples2) {
        return -1;
    } else if (samples1 > samples2) {
        return 1;
    }

    /* Equal timestamps: TrackStart markers come first */
    if (entry1->marker.mark_type == DSDIFF_MARK_TRACK_START &&
        entry2->marker.mark_type != DSDIFF_MARK_TRACK_START) {
        return -1;
    } else if (entry1->marker.mark_type != DSDIFF_MARK_TRACK_START &&
               entry2->marker.mark_type == DSDIFF_MARK_TRACK_START) {
        return 1;
    }

    return 0;
}

/**
 * Merge two sorted singly-linked lists into one sorted list.
 *
 * Uses a dummy head node to simplify the merge logic. Both input
 * lists are consumed and their nodes are relinked into the result.
 *
 * @param left  First sorted list (will be consumed)
 * @param right Second sorted list (will be consumed)
 * @return Head of merged sorted list
 */
static ll_t *dsdiff_marker_list_merge(ll_t *left, ll_t *right) {
    ll_t head;
    ll_t *tail = &head;
    dsdiff_marker_entry_t *entry_left, *entry_right;

    while (left && right) {
        entry_left = list_entry(left, dsdiff_marker_entry_t, list);
        entry_right = list_entry(right, dsdiff_marker_entry_t, list);

        if (dsdiff_marker_compare(entry_left, entry_right) <= 0) {
            tail->next = left;
            left = left->next;
        } else {
            tail->next = right;
            right = right->next;
        }
        tail = tail->next;
    }

    /* Attach remaining elements */
    tail->next = left ? left : right;
    return head.next;
}

/**
 * Split a singly-linked list at its midpoint.
 *
 * Uses the slow/fast pointer technique to find the middle.
 * The original list is split in place.
 *
 * @param head Head of list to split
 * @return Head of the second half (original list is truncated)
 */
static ll_t *dsdiff_marker_list_split(ll_t *head) {
    ll_t *slow = head;
    ll_t *fast = head->next;

    /* Fast pointer moves twice as fast, so when it reaches
     * the end, slow pointer is at the midpoint */
    while (fast && fast->next) {
        slow = slow->next;
        fast = fast->next->next;
    }

    ll_t *mid = slow->next;
    slow->next = NULL;
    return mid;
}

/**
 * Recursively sort a singly-linked list using merge sort.
 *
 * Time complexity: O(n log n)
 * Space complexity: O(log n) for recursion stack
 *
 * @param head Head of list to sort
 * @return Head of sorted list
 */
static ll_t *dsdiff_marker_list_merge_sort(ll_t *head) {
    ll_t *mid, *sorted_left, *sorted_right;

    /* Base case: empty or single element */
    if (!head || !head->next) {
        return head;
    }

    /* Split, recursively sort each half, then merge */
    mid = dsdiff_marker_list_split(head);
    sorted_left = dsdiff_marker_list_merge_sort(head);
    sorted_right = dsdiff_marker_list_merge_sort(mid);

    return dsdiff_marker_list_merge(sorted_left, sorted_right);
}

/**
 * Sort all markers in the list by timestamp.
 *
 * This function converts the circular doubly-linked list to a singly-linked
 * list for sorting, then reconstructs the circular list from the sorted result.
 *
 * Algorithm:
 *   1. Extract nodes into a singly-linked NULL-terminated list
 *   2. Apply merge sort (O(n log n))
 *   3. Rebuild the circular doubly-linked list from sorted nodes
 */
void dsdiff_marker_list_sort(dsdiff_marker_list_t *list) {
    ll_t *first_node, *sorted_head, *list_sentinel;
    ll_t *current_node;
    dsdiff_marker_entry_t *entry;

    if (!list || list->count <= 1) {
        return;
    }

    /* Step 1: Convert circular list to NULL-terminated singly-linked list */
    first_node = list->list_head.next;
    list_sentinel = &list->list_head;

    current_node = first_node;
    while (current_node != list_sentinel) {
        ll_t *next_node = current_node->next;
        current_node->next = (next_node == list_sentinel) ? NULL : next_node;
        current_node = next_node;
    }

    /* Step 2: Sort using merge sort */
    sorted_head = dsdiff_marker_list_merge_sort(first_node);

    /* Step 3: Rebuild circular doubly-linked list */
    INIT_LIST_HEAD(&list->list_head);
    current_node = sorted_head;

    while (current_node) {
        ll_t *next_node = current_node->next;
        entry = list_entry(current_node, dsdiff_marker_entry_t, list);
        list_add_tail(&entry->list, &list->list_head);
        current_node = next_node;
    }
}

/* =============================================================================
 * Marker Entry Management
 * ===========================================================================*/

dsdiff_marker_entry_t *dsdiff_marker_entry_create(const dsdiff_marker_t *marker,
                                                  uint32_t sample_rate) {
    dsdiff_marker_entry_t *entry;

    if (!marker) {
        return NULL;
    }

    entry = (dsdiff_marker_entry_t *)sa_malloc(sizeof(dsdiff_marker_entry_t));
    if (!entry) {
        return NULL;
    }

    INIT_LIST_HEAD(&entry->list);

    entry->marker.mark_type = marker->mark_type;
    entry->marker.time.hours = marker->time.hours;
    entry->marker.time.minutes = marker->time.minutes;
    entry->marker.time.seconds = marker->time.seconds;
    entry->marker.time.samples = marker->time.samples;

    if (marker->marker_text) {
        entry->marker.marker_text = sa_strdup(marker->marker_text);
        if (entry->marker.marker_text == 0) {
            sa_free(entry);
            return NULL;
        }
    } else {
        entry->marker.marker_text = NULL;
    }

    entry->sample_rate = sample_rate;

    return entry;
}

void dsdiff_marker_entry_free(dsdiff_marker_entry_t *entry) {
    if (!entry) {
        return;
    }

    if (entry->marker.marker_text) {
        sa_free(entry->marker.marker_text);
    }

    sa_free(entry);
}

/* =============================================================================
 * Standalone Marker Creation/Destruction
 * ===========================================================================*/

dsdiff_marker_t *dsdiff_marker_create(void) {
    dsdiff_marker_t *marker;

    marker = (dsdiff_marker_t *)sa_malloc(sizeof(dsdiff_marker_t));
    if (!marker) {
        return NULL;
    }
    memset(marker, 0, sizeof(dsdiff_marker_t));

    return marker;
}

void dsdiff_marker_free(dsdiff_marker_t *marker) {
    if (!marker) {
        return;
    }

    if (marker->marker_text) {
        sa_free(marker->marker_text);
    }

    sa_free(marker);
}

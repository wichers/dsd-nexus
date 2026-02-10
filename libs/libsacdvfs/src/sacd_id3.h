/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief SACD ID3 Tag Renderer - Internal Header
 * Renders ID3v2.4 tags from SACD metadata for embedding in DSF virtual files.
 * This header is NOT part of the public API.
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

#ifndef LIBSACDVFS_SACD_ID3_H
#define LIBSACDVFS_SACD_ID3_H

#include <libsacd/sacd.h>

/**
 * @brief Renders an ID3v2.4 tag for a track
 *
 * @param ctx           Pointer to the sacd_t context
 * @param buffer        Buffer to receive the rendered ID3 tag data
 * @param track_num     Track number (1-based)
 *
 * @return Length of the rendered ID3 tag in bytes, or 0 on error
 */
int sacd_id3_tag_render(sacd_t *ctx, uint8_t *buffer, uint8_t track_num);

#endif /* LIBSACDVFS_SACD_ID3_H */

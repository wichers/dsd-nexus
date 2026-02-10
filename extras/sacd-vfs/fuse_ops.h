/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief SACD FUSE Operations - Header
 * FUSE callback implementations for the SACD overlay VFS.
 * Works with both native libfuse3 (Linux/macOS) and WinFSP (Windows).
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

#ifndef SACD_VFS_FUSE_OPS_H
#define SACD_VFS_FUSE_OPS_H

#include "fuse_compat.h"

#include <libsacdvfs/sacd_overlay.h>

/**
 * Initialize FUSE operations structure.
 *
 * @param ops Output operations structure to fill
 */
void sacd_fuse_init_ops(struct fuse_operations *ops);

/**
 * Set the overlay context for FUSE operations.
 * Must be called before mounting.
 *
 * @param ctx Overlay context
 */
void sacd_fuse_set_context(sacd_overlay_ctx_t *ctx);

/**
 * Get the current overlay context.
 *
 * @return Overlay context
 */
sacd_overlay_ctx_t *sacd_fuse_get_context(void);

#endif /* SACD_VFS_FUSE_OPS_H */

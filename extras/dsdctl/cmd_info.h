/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Info command for displaying disc/file metadata
 * Displays metadata information for SACD ISO images, DSF files,
 * DSDIFF files, PS3 drives, or PS3 network streaming servers.
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

#ifndef DSDCTL_CMD_INFO_H
#define DSDCTL_CMD_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Execute the info command.
 *
 * Usage: dsdctl info [options] [input]
 *
 * Options:
 *   -i, --input <path>      Input file, device, or network address
 *   --json                  Output in JSON format
 *   -a, --area <type>       stereo, multichannel (SACD only)
 *   -v, --verbose           Show detailed track listing
 *   -h, --help              Show help
 *
 * @param argc Argument count (including "info")
 * @param argv Argument vector
 * @return Exit code (0 on success)
 */
int cmd_info(int argc, char *argv[]);

#ifdef __cplusplus
}
#endif

#endif /* DSDCTL_CMD_INFO_H */

/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Extract command for raw SACD ISO extraction
 * Extracts a raw SACD ISO image from a PS3 BluRay drive or PS3
 * network streaming server. The output is a complete disc image
 * that can be used with the convert or info commands.
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

#ifndef DSDCTL_CMD_EXTRACT_H
#define DSDCTL_CMD_EXTRACT_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Execute the extract command.
 *
 * Usage: dsdctl extract [options]
 *
 * Input Source (one required):
 *   -d, --device <path>     PS3 drive path (/dev/sr0, D:)
 *   -n, --network <addr>    PS3 network address (host:port)
 *
 * Output:
 *   -o, --output <path>     Output ISO file path (required)
 *
 * Options:
 *   --no-progress           Disable progress display
 *   -v, --verbose           Verbose output
 *   -h, --help              Show help
 *
 * @param argc Argument count (including "extract")
 * @param argv Argument vector
 * @return Exit code (0 on success)
 */
int cmd_extract(int argc, char *argv[]);

#ifdef __cplusplus
}
#endif

#endif /* DSDCTL_CMD_EXTRACT_H */

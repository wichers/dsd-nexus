/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief SACD VFS - FUSE Platform Compatibility Header
 * Provides abstraction between native libfuse3 (Linux/macOS) and
 * WinFSP FUSE3 compatibility layer (Windows).
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

#ifndef SACD_VFS_FUSE_COMPAT_H
#define SACD_VFS_FUSE_COMPAT_H

/* FUSE version must be defined before including fuse headers */
#define FUSE_USE_VERSION 35

#ifdef _WIN32
/*
 * WinFSP FUSE3 compatibility layer
 * WinFSP uses its own types: fuse_stat, fuse_off_t, fuse_mode_t, etc.
 */
#include <fuse3/fuse.h>

/* WinFSP already defines these - just create aliases for our code */
typedef struct fuse_stat     fuse_compat_stat_t;
typedef fuse_off_t           fuse_compat_off_t;
typedef fuse_mode_t          fuse_compat_mode_t;
typedef fuse_uid_t           fuse_compat_uid_t;
typedef fuse_gid_t           fuse_compat_gid_t;

/* WinFSP uses fuse_timespec for stat times */
#define FUSE_STAT_ATIME(st)  ((st)->st_atim.tv_sec)
#define FUSE_STAT_MTIME(st)  ((st)->st_mtim.tv_sec)
#define FUSE_STAT_CTIME(st)  ((st)->st_ctim.tv_sec)

#define FUSE_SET_STAT_ATIME(st, t) do { (st)->st_atim.tv_sec = (t); (st)->st_atim.tv_nsec = 0; } while(0)
#define FUSE_SET_STAT_MTIME(st, t) do { (st)->st_mtim.tv_sec = (t); (st)->st_mtim.tv_nsec = 0; } while(0)
#define FUSE_SET_STAT_CTIME(st, t) do { (st)->st_ctim.tv_sec = (t); (st)->st_ctim.tv_nsec = 0; } while(0)

/* WinFSP does not have getuid/getgid - return fixed values */
static inline fuse_uid_t fuse_compat_getuid(void) { return 0; }
static inline fuse_gid_t fuse_compat_getgid(void) { return 0; }

/* O_ACCMODE may not be defined on Windows */
#ifndef O_ACCMODE
#define O_ACCMODE (O_RDONLY | O_WRONLY | O_RDWR)
#endif

#ifndef O_RDONLY
#define O_RDONLY 0
#endif

/* Access mode flags for access() - may not be defined on Windows */
#ifndef F_OK
#define F_OK 0  /* Check existence */
#endif

#ifndef R_OK
#define R_OK 4  /* Check read permission */
#endif

#ifndef W_OK
#define W_OK 2  /* Check write permission */
#endif

#ifndef X_OK
#define X_OK 1  /* Check execute permission */
#endif

/* WinFSP errno handling */
#ifndef ENOENT
#define ENOENT 2
#endif

#ifndef EIO
#define EIO 5
#endif

#ifndef EBADF
#define EBADF 9
#endif

#ifndef EACCES
#define EACCES 13
#endif

#ifndef ENOTSUP
#define ENOTSUP 129
#endif

#else
/*
 * Native libfuse3 (Linux/macOS)
 * Uses standard POSIX types directly.
 */
#define _FILE_OFFSET_BITS 64

#ifdef __APPLE__
/*
 * macFUSE defines FUSE_DARWIN_ENABLE_EXTENSIONS=1 by default, which replaces
 * struct stat with struct fuse_darwin_attr and fuse_fill_dir_t with
 * fuse_darwin_fill_dir_t in all FUSE callbacks. Disable this to use the same
 * standard POSIX types as Linux, keeping our code platform-agnostic.
 */
#define FUSE_DARWIN_ENABLE_EXTENSIONS 0
#endif

#include <fuse3/fuse.h>
#include <sys/stat.h>
#include <unistd.h>

/* Map to standard POSIX types */
typedef struct stat          fuse_compat_stat_t;
typedef off_t                fuse_compat_off_t;
typedef mode_t               fuse_compat_mode_t;
typedef uid_t                fuse_compat_uid_t;
typedef gid_t                fuse_compat_gid_t;

/* Standard stat time access macros */
#ifdef __APPLE__
/* macOS uses st_atimespec, st_mtimespec, st_ctimespec */
#define FUSE_STAT_ATIME(st)  ((st)->st_atimespec.tv_sec)
#define FUSE_STAT_MTIME(st)  ((st)->st_mtimespec.tv_sec)
#define FUSE_STAT_CTIME(st)  ((st)->st_ctimespec.tv_sec)
#define FUSE_SET_STAT_ATIME(st, t) do { (st)->st_atimespec.tv_sec = (t); (st)->st_atimespec.tv_nsec = 0; } while(0)
#define FUSE_SET_STAT_MTIME(st, t) do { (st)->st_mtimespec.tv_sec = (t); (st)->st_mtimespec.tv_nsec = 0; } while(0)
#define FUSE_SET_STAT_CTIME(st, t) do { (st)->st_ctimespec.tv_sec = (t); (st)->st_ctimespec.tv_nsec = 0; } while(0)
#else
/* Linux uses st_atime, st_mtime, st_ctime (or st_atim.tv_sec etc.) */
#define FUSE_STAT_ATIME(st)  ((st)->st_atime)
#define FUSE_STAT_MTIME(st)  ((st)->st_mtime)
#define FUSE_STAT_CTIME(st)  ((st)->st_ctime)
#define FUSE_SET_STAT_ATIME(st, t) ((st)->st_atime = (t))
#define FUSE_SET_STAT_MTIME(st, t) ((st)->st_mtime = (t))
#define FUSE_SET_STAT_CTIME(st, t) ((st)->st_ctime = (t))
#endif

/* POSIX getuid/getgid */
static inline fuse_compat_uid_t fuse_compat_getuid(void) { return getuid(); }
static inline fuse_compat_gid_t fuse_compat_getgid(void) { return getgid(); }

#endif /* _WIN32 */

/* Common stat mode bits - these should be consistent */
#ifndef S_IFDIR
#define S_IFDIR  0040000
#endif

#ifndef S_IFREG
#define S_IFREG  0100000
#endif

#endif /* SACD_VFS_FUSE_COMPAT_H */

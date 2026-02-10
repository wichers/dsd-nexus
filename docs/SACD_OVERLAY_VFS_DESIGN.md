# SACD Overlay VFS Design Document

## 1. Overview

This document describes the architecture for a virtual filesystem overlay that:
- Shadow copies a source directory tree to a mount point
- Automatically detects SACD ISO files and presents them as expandable folders
- Performs on-the-fly conversion of SACD content to DSF format
- Supports writing to the ID3 tag region of virtual DSF files (stored in XML sidecar)
- Supports both FUSE (Linux/macOS) and WinFSP (Windows)

---

## 2. Architecture

### 2.1 Layer Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        User Applications                                 │
│                    (File managers, audio players)                        │
├─────────────────────────────────────────────────────────────────────────┤
│                    Operating System VFS Interface                        │
│         ┌─────────────────────┐    ┌─────────────────────┐              │
│         │   FUSE (Linux/Mac)  │    │   WinFSP (Windows)  │              │
│         └──────────┬──────────┘    └──────────┬──────────┘              │
├────────────────────┴──────────────────────────┴─────────────────────────┤
│                   Platform Adapters (extras/)                            │
│         ┌─────────────────────┐    ┌─────────────────────┐              │
│         │     sacd-vfs-fuse/        │    │     sacd-vfs-winfsp/       │              │
│         │  FUSE callbacks     │    │  WinFSP callbacks   │              │
│         └──────────┬──────────┘    └──────────┬──────────┘              │
│                    └──────────┬───────────────┘                         │
├───────────────────────────────┴─────────────────────────────────────────┤
│                    libsacdvfs_overlay (libs/libsacdvfs/)                │
│   ┌─────────────────────────────────────────────────────────────────┐   │
│   │  - Source directory scanning and shadowing                      │   │
│   │  - SACD ISO detection (.iso, .ISO extension check)             │   │
│   │  - Name collision resolution (Album, Album (1), Album (2)...)   │   │
│   │  - Multi-ISO context management with reference counting        │   │
│   │  - Thread-safe handle tables (readers and writers locks)        │   │
│   │  - Unified path resolution (passthrough vs virtual)            │   │
│   │  - Write routing for ID3 tag modifications                     │   │
│   └─────────────────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────────────────┤
│                         libsacdvfs (existing)                           │
│   ┌─────────────────────────────────────────────────────────────────┐   │
│   │  - Single SACD ISO virtual filesystem                          │   │
│   │  - On-the-fly DSD-to-DSF transformation                        │   │
│   │  - Multi-threaded DST decompression (ring buffer)              │   │
│   │  - ID3 metadata generation and overlay support                 │   │
│   │  - XML sidecar persistence (sxmlc + base64)                    │   │
│   └─────────────────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────────────────┤
│                    libsacd / libdst / libdsf                            │
│   ┌─────────────────────────────────────────────────────────────────┐   │
│   │  - SACD disc parsing (TOC, tracks, metadata)                   │   │
│   │  - DST lossless decompression                                  │   │
│   │  - DSF file format handling                                    │   │
│   └─────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
```

### 2.2 Key Design Principles

1. **Separation of Concerns**: libsacdvfs_overlay handles filesystem mapping logic; platform adapters only translate OS-specific callbacks.

2. **Platform Independence**: All shared logic resides in libsacdvfs_overlay. Platform-specific code is minimal and isolated.

3. **Thread Safety**: The overlay layer must be thread-safe as FUSE/WinFSP can call operations concurrently.

4. **Lazy Loading**: SACD ISOs are only parsed when their virtual directories are accessed.

5. **Resource Management**: Reference counting ensures ISOs stay open while files are in use.

6. **ID3 Writability**: Virtual DSF files appear writable. Writes to the ID3 tag region are captured and persisted to XML sidecar files.

---

## 3. ID3 Tag Write Support

### 3.1 Write Semantics

Virtual DSF files support limited write operations:

- **Writable Region**: Only the ID3 tag region (at the end of the virtual file) can be modified
- **Read-Only Regions**: DSF header and audio data regions reject write attempts (return `EACCES`)
- **Atomic Updates**: ID3 tag changes are buffered in memory until explicitly flushed

### 3.2 XML Sidecar Storage

ID3 tag modifications are stored in XML sidecar files:

```
Source Directory:
  Album.iso               # SACD ISO file
  Album.iso.xml           # ID3 overlay sidecar (created on first write)
```

**XML Format** (using libsautil/sxmlc.h):

```xml
<?xml version="1.0" encoding="UTF-8"?>
<SacdId3Overlay version="1.0" iso="Album.iso">
  <Area type="stereo">
    <Track number="1">
      <Id3>BASE64_ENCODED_ID3V2_TAG_DATA</Id3>
    </Track>
    <Track number="2">
      <Id3>BASE64_ENCODED_ID3V2_TAG_DATA</Id3>
    </Track>
  </Area>
  <Area type="multichannel">
    <Track number="1">
      <Id3>BASE64_ENCODED_ID3V2_TAG_DATA</Id3>
    </Track>
  </Area>
</SacdId3Overlay>
```

**Base64 Encoding**: Uses libsautil/base64.h (`sa_base64_encode`, `sa_base64_decode`)

### 3.3 Existing libsacdvfs API for ID3

The existing libsacdvfs already provides ID3 overlay support:

```c
/* Set ID3 overlay for a track (in memory) */
int sacd_vfs_set_id3_overlay(sacd_vfs_ctx_t *ctx, sacd_vfs_area_t area,
                             uint8_t track_num, const uint8_t *buffer, size_t size);

/* Save all ID3 overlays to XML sidecar file */
int sacd_vfs_save_id3_overlay(sacd_vfs_ctx_t *ctx);

/* Check for unsaved changes */
bool sacd_vfs_has_unsaved_id3_changes(sacd_vfs_ctx_t *ctx);

/* Clear overlay (revert to disc metadata) */
int sacd_vfs_clear_id3_overlay(sacd_vfs_ctx_t *ctx, sacd_vfs_area_t area,
                               uint8_t track_num);
```

### 3.4 Write Flow

```
┌─────────────┐     ┌─────────────────┐     ┌─────────────────┐
│  Write()    │────►│ Check offset    │────►│ In ID3 region?  │
│  syscall    │     │ against layout  │     │                 │
└─────────────┘     └─────────────────┘     └────────┬────────┘
                                                     │
                           ┌─────────────────────────┴─────────────────┐
                           │                                           │
                           ▼                                           ▼
                    ┌─────────────┐                           ┌─────────────┐
                    │   YES       │                           │   NO        │
                    │             │                           │             │
                    └──────┬──────┘                           └──────┬──────┘
                           │                                         │
                           ▼                                         ▼
                    ┌─────────────────┐                       ┌─────────────┐
                    │ Buffer write    │                       │ Return      │
                    │ in ID3 cache    │                       │ -EACCES     │
                    └────────┬────────┘                       └─────────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │ Mark as dirty   │
                    └────────┬────────┘
                             │
                             ▼ (on flush/close)
                    ┌─────────────────┐
                    │ Parse ID3 tag   │
                    │ Call set_id3_   │
                    │ overlay()       │
                    └────────┬────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │ save_id3_       │
                    │ overlay()       │
                    │ -> XML sidecar  │
                    └─────────────────┘
```

### 3.5 Conflict Handling

If an XML file with a different structure already exists next to the ISO:
- The file will be **overwritten** with the new SACD ID3 overlay format
- No backup is created (user should back up manually if needed)
- The `SacdId3Overlay` root element and `version` attribute identify the format

---

## 4. libsacdvfs_overlay API Design

### 4.1 Core Types

```c
/* Opaque overlay context handle */
typedef struct sacd_overlay_ctx sacd_overlay_ctx_t;

/* Opaque file handle (for opened files) */
typedef struct sacd_overlay_file sacd_overlay_file_t;

/* Entry type enumeration */
typedef enum {
    OVERLAY_ENTRY_FILE,           /* Regular file (passthrough or virtual) */
    OVERLAY_ENTRY_DIRECTORY,      /* Directory (passthrough or virtual) */
    OVERLAY_ENTRY_ISO_FOLDER      /* SACD ISO presented as folder */
} overlay_entry_type_t;

/* File source type */
typedef enum {
    OVERLAY_SOURCE_PASSTHROUGH,   /* Direct passthrough to source */
    OVERLAY_SOURCE_VIRTUAL        /* Virtual file from libsacdvfs */
} overlay_source_type_t;

/* Directory entry for readdir */
typedef struct {
    char name[512];
    overlay_entry_type_t type;
    overlay_source_type_t source;
    uint64_t size;                /* File size (0 for directories) */
    uint64_t mtime;               /* Modification time (Unix timestamp) */
    uint64_t atime;               /* Access time */
    uint64_t ctime;               /* Creation time */
    uint32_t mode;                /* Unix permission mode */
    int writable;                 /* True if writes are supported */
} overlay_entry_t;

/* Configuration options */
typedef struct {
    const char *source_dir;       /* Root source directory to shadow */
    int detect_iso_extensions;    /* Bitmask: 1=.iso, 2=.ISO */
    int thread_pool_size;         /* DST decoder threads (0=auto) */
    int max_open_isos;            /* Max concurrent ISOs (0=unlimited) */
    int cache_timeout_seconds;    /* ISO metadata cache timeout */
} overlay_config_t;
```

### 4.2 Context Management

```c
/**
 * Create overlay context with configuration.
 * @param config Configuration options
 * @return New context or NULL on failure
 */
sacd_overlay_ctx_t *sacd_overlay_create(const overlay_config_t *config);

/**
 * Destroy overlay context and release all resources.
 * Saves any pending ID3 overlay changes before cleanup.
 * @param ctx Context to destroy
 */
void sacd_overlay_destroy(sacd_overlay_ctx_t *ctx);
```

### 4.3 Path Resolution

```c
/**
 * Resolve a virtual path to determine its type and source.
 *
 * Path resolution rules:
 * 1. If path points to a real file/directory in source: PASSTHROUGH
 * 2. If path matches "Album Name" where "Album Name.iso" exists: ISO_FOLDER
 * 3. If path is inside an ISO_FOLDER: VIRTUAL (delegate to libsacdvfs)
 *
 * @param ctx      Overlay context
 * @param path     Virtual path (relative to mount point)
 * @param entry    Output entry information
 * @return 0 on success, -errno on failure
 */
int sacd_overlay_stat(sacd_overlay_ctx_t *ctx, const char *path,
                      overlay_entry_t *entry);

/**
 * Translate virtual path to source filesystem path.
 * Only valid for PASSTHROUGH entries.
 *
 * @param ctx         Overlay context
 * @param path        Virtual path
 * @param source_path Output buffer for source path
 * @param size        Buffer size
 * @return 0 on success, -errno on failure
 */
int sacd_overlay_get_source_path(sacd_overlay_ctx_t *ctx, const char *path,
                                  char *source_path, size_t size);
```

### 4.4 Directory Operations

```c
/* Readdir callback signature */
typedef int (*overlay_readdir_cb)(const overlay_entry_t *entry, void *userdata);

/**
 * List directory contents.
 *
 * For passthrough directories: lists source directory entries
 * For ISO folders: lists virtual SACD contents (Stereo/, Multi-channel/)
 *
 * Special handling:
 * - If source directory contains "Album.iso", entry "Album.iso" is hidden
 *   and entry "Album" (directory) is added
 * - If "Album" directory already exists, the ISO folder becomes "Album (1)"
 *
 * @param ctx       Overlay context
 * @param path      Directory path
 * @param callback  Called for each entry
 * @param userdata  Passed to callback
 * @return Number of entries, or -errno on failure
 */
int sacd_overlay_readdir(sacd_overlay_ctx_t *ctx, const char *path,
                         overlay_readdir_cb callback, void *userdata);
```

### 4.5 File Operations

```c
/**
 * Open a file for reading (and writing for virtual DSF files).
 *
 * For passthrough files: returns handle to source file
 * For virtual files: returns handle to libsacdvfs file
 *
 * @param ctx   Overlay context
 * @param path  File path
 * @param flags Open flags (O_RDONLY, O_RDWR, etc.)
 * @param file  Output file handle
 * @return 0 on success, -errno on failure
 */
int sacd_overlay_open(sacd_overlay_ctx_t *ctx, const char *path,
                      int flags, sacd_overlay_file_t **file);

/**
 * Close a file handle.
 * For virtual files: saves any pending ID3 overlay changes.
 */
int sacd_overlay_close(sacd_overlay_file_t *file);

/**
 * Read from file.
 * @param file       File handle
 * @param buffer     Output buffer
 * @param size       Bytes to read
 * @param offset     File offset
 * @param bytes_read Actual bytes read
 * @return 0 on success, -errno on failure
 */
int sacd_overlay_read(sacd_overlay_file_t *file, void *buffer, size_t size,
                      uint64_t offset, size_t *bytes_read);

/**
 * Write to file.
 *
 * For passthrough files: direct write to source
 * For virtual DSF files: only ID3 region is writable, returns -EACCES for
 *                        attempts to write header or audio data
 *
 * @param file          File handle
 * @param buffer        Input buffer
 * @param size          Bytes to write
 * @param offset        File offset
 * @param bytes_written Actual bytes written
 * @return 0 on success, -errno on failure
 */
int sacd_overlay_write(sacd_overlay_file_t *file, const void *buffer,
                       size_t size, uint64_t offset, size_t *bytes_written);

/**
 * Flush pending writes.
 * For virtual files: saves ID3 overlay changes to XML sidecar.
 */
int sacd_overlay_flush(sacd_overlay_file_t *file);

/**
 * Get file attributes from open handle.
 */
int sacd_overlay_fstat(sacd_overlay_file_t *file, overlay_entry_t *entry);
```

---

## 5. Name Collision Resolution

### 5.1 Algorithm

When scanning a directory that contains both `Album.iso` and an `Album/` folder:

```
Source Directory:
  Album.iso        (SACD ISO file)
  Album/           (existing directory)
  Other.iso        (another SACD ISO)

Virtual Directory (after overlay):
  Album/           (passthrough to source Album/)
  Album (1)/       (virtual: contents of Album.iso)
  Other/           (virtual: contents of Other.iso)
```

### 5.2 Implementation

```c
typedef struct {
    char original_name[256];      /* Base name without extension */
    char display_name[256];       /* Name shown in VFS (with suffix if needed) */
    int collision_index;          /* 0=no collision, 1="(1)", 2="(2)", etc. */
    char iso_path[512];           /* Full path to ISO file */
} iso_entry_t;

/* Collision resolution process:
 * 1. Scan directory for all entries
 * 2. Build set of existing directory names
 * 3. For each ISO file:
 *    a. Strip extension to get base name
 *    b. If base name exists in set, append (1), (2), etc.
 *    c. Add final name to set
 * 4. Return merged entry list
 */
```

---

## 6. Thread Safety Model

### 6.1 Locking Strategy

```c
/* Per-context locks */
typedef struct sacd_overlay_ctx {
    /* Reader-writer lock for directory cache */
    pthread_rwlock_t dir_cache_lock;

    /* Mutex for ISO context table modifications */
    pthread_mutex_t iso_table_lock;

    /* Per-ISO locks (within ISO context) */
    /* ... managed by libsacdvfs */

    /* Atomic reference counts for open handles */
    /* ... */
} sacd_overlay_ctx_t;
```

### 6.2 Concurrency Rules

1. **Directory listings**: Multiple concurrent readers allowed; writer blocks all.
2. **File reads**: Fully concurrent on different files; per-file locking for seeks.
3. **File writes**: Serialized per-file for ID3 modifications.
4. **ISO mounting**: Serialized to prevent duplicate mounts.
5. **ID3 save**: Atomic write to XML sidecar file.
6. **File handle creation/destruction**: Protected by handle table mutex.

---

## 7. Platform Adapters

### 7.1 FUSE Adapter (sacd-vfs-fuse/)

```c
/* FUSE callback implementations */

static int fuse_getattr(const char *path, struct stat *stbuf,
                        struct fuse_file_info *fi)
{
    overlay_entry_t entry;
    int ret = sacd_overlay_stat(g_ctx, path, &entry);
    if (ret < 0) return ret;

    memset(stbuf, 0, sizeof(*stbuf));
    stbuf->st_mode = entry.mode;
    stbuf->st_size = entry.size;
    stbuf->st_mtime = entry.mtime;
    /* ... */
    return 0;
}

static int fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi,
                        enum fuse_readdir_flags flags)
{
    /* Delegate to sacd_overlay_readdir with filler callback adapter */
}

static int fuse_open(const char *path, struct fuse_file_info *fi)
{
    sacd_overlay_file_t *file;
    int flags = fi->flags & O_ACCMODE;
    int ret = sacd_overlay_open(g_ctx, path, flags, &file);
    if (ret < 0) return ret;
    fi->fh = (uint64_t)file;
    return 0;
}

static int fuse_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
    sacd_overlay_file_t *file = (sacd_overlay_file_t *)fi->fh;
    size_t bytes_read;
    int ret = sacd_overlay_read(file, buf, size, offset, &bytes_read);
    if (ret < 0) return ret;
    return (int)bytes_read;
}

static int fuse_write(const char *path, const char *buf, size_t size,
                      off_t offset, struct fuse_file_info *fi)
{
    sacd_overlay_file_t *file = (sacd_overlay_file_t *)fi->fh;
    size_t bytes_written;
    int ret = sacd_overlay_write(file, buf, size, offset, &bytes_written);
    if (ret < 0) return ret;
    return (int)bytes_written;
}

static int fuse_flush(const char *path, struct fuse_file_info *fi)
{
    sacd_overlay_file_t *file = (sacd_overlay_file_t *)fi->fh;
    return sacd_overlay_flush(file);
}

/* FUSE operations structure */
static struct fuse_operations fuse_ops = {
    .getattr  = fuse_getattr,
    .readdir  = fuse_readdir,
    .open     = fuse_open,
    .read     = fuse_read,
    .write    = fuse_write,
    .flush    = fuse_flush,
    .release  = fuse_release,
    /* ... */
};
```

### 7.2 WinFSP/Dokan Adapter (sacd-vfs-winfsp/)

```c
/* WinFSP callback implementations */

static NTSTATUS DOKAN_CALLBACK dokan_getfileinfo(
    LPCWSTR FileName,
    LPBY_HANDLE_FILE_INFORMATION HandleFileInfo,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    /* Convert wide path to UTF-8 */
    char path_utf8[512];
    WideCharToMultiByte(CP_UTF8, 0, FileName, -1, path_utf8, sizeof(path_utf8), NULL, NULL);

    overlay_entry_t entry;
    int ret = sacd_overlay_stat(g_ctx, path_utf8, &entry);
    if (ret < 0) return STATUS_OBJECT_NAME_NOT_FOUND;

    /* Populate HandleFileInfo from entry */
    HandleFileInfo->dwFileAttributes =
        (entry.type == OVERLAY_ENTRY_FILE) ? FILE_ATTRIBUTE_NORMAL : FILE_ATTRIBUTE_DIRECTORY;
    /* ... */
    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK dokan_writefile(
    LPCWSTR FileName,
    LPCVOID Buffer,
    DWORD NumberOfBytesToWrite,
    LPDWORD NumberOfBytesWritten,
    LONGLONG Offset,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    sacd_overlay_file_t *file = (sacd_overlay_file_t *)DokanFileInfo->Context;
    size_t bytes_written;
    int ret = sacd_overlay_write(file, Buffer, NumberOfBytesToWrite, Offset, &bytes_written);
    if (ret < 0) return STATUS_ACCESS_DENIED;
    *NumberOfBytesWritten = (DWORD)bytes_written;
    return STATUS_SUCCESS;
}

/* Similar implementations for:
 * - dokan_findfirst / dokan_findnext (readdir)
 * - dokan_create (open)
 * - dokan_read
 * - dokan_flushfilebuffers
 * - dokan_close
 */
```

---

## 8. ISO Detection and Lazy Loading

### 8.1 Detection Strategy

```c
/* File extensions to detect as SACD ISO */
#define ISO_EXT_ISO       0x01   /* .iso */
#define ISO_EXT_ISO_UPPER 0x02   /* .ISO */

/* Check if file is SACD ISO (by extension and magic bytes) */
static int is_sacd_iso(const char *path, int ext_mask)
{
    /* 1. Check extension */
    const char *ext = strrchr(path, '.');
    if (!ext) return 0;

    if ((ext_mask & ISO_EXT_ISO) && strcmp(ext, ".iso") == 0) { /* continue */ }
    else if ((ext_mask & ISO_EXT_ISO_UPPER) && strcmp(ext, ".ISO") == 0) { /* continue */ }
    else return 0;

    /* 2. Verify SACD magic (optional, for speed can skip) */
    /* Read bytes at offset 0x8001: should be "SACD" */
    /* ... */

    return 1;
}
```

### 8.2 Lazy ISO Mounting

```c
typedef struct {
    char iso_path[512];
    char display_name[256];
    sacd_vfs_ctx_t *vfs;        /* NULL until first access */
    pthread_mutex_t mount_lock;
    int ref_count;
    time_t last_access;
} iso_mount_t;

/* Get or create VFS context for ISO */
static sacd_vfs_ctx_t *get_iso_vfs(sacd_overlay_ctx_t *ctx,
                                    iso_mount_t *mount)
{
    pthread_mutex_lock(&mount->mount_lock);

    if (!mount->vfs) {
        mount->vfs = sacd_vfs_create();
        if (mount->vfs) {
            if (sacd_vfs_open(mount->vfs, mount->iso_path) != SACD_VFS_OK) {
                sacd_vfs_destroy(mount->vfs);
                mount->vfs = NULL;
            }
        }
    }

    if (mount->vfs) {
        mount->ref_count++;
        mount->last_access = time(NULL);
    }

    pthread_mutex_unlock(&mount->mount_lock);
    return mount->vfs;
}
```

---

## 9. Memory and Resource Management

### 9.1 ISO Context Lifecycle

```
                    ┌─────────────────┐
                    │   UNLOADED      │
                    │  (vfs = NULL)   │
                    └────────┬────────┘
                             │ First access (readdir/stat/open)
                             ▼
                    ┌─────────────────┐
                    │    LOADING      │
                    │ (mount_lock)    │
                    └────────┬────────┘
                             │ sacd_vfs_open() success
                             ▼
                    ┌─────────────────┐
        ◄───────────│    MOUNTED      │◄──────────┐
        │           │ (vfs != NULL)   │           │
        │           └────────┬────────┘           │
        │                    │                    │
        │ File close         │ File open          │ Timeout & ref_count == 0
        │ (ref_count--)      │ (ref_count++)      │
        │                    │                    │
        └───────────►────────┴───────────►────────┘
                             │
                             │ Cache eviction (saves pending ID3)
                             ▼
                    ┌─────────────────┐
                    │   UNLOADING     │
                    │ (save + cleanup)│
                    └────────┬────────┘
                             │ sacd_vfs_save_id3_overlay()
                             │ sacd_vfs_close()
                             ▼
                    ┌─────────────────┐
                    │   UNLOADED      │
                    └─────────────────┘
```

### 9.2 File Handle Tracking

```c
typedef struct sacd_overlay_file {
    overlay_source_type_t source;
    int open_flags;               /* O_RDONLY, O_RDWR, etc. */
    union {
        struct {
            int fd;               /* Native file descriptor */
        } passthrough;
        struct {
            iso_mount_t *mount;        /* Reference to ISO mount */
            sacd_vfs_file_t *vfs_file; /* libsacdvfs file handle */
            uint8_t *id3_write_buf;    /* Buffer for ID3 writes */
            size_t id3_write_len;      /* Length of buffered data */
            int id3_dirty;             /* True if ID3 modified */
        } virtual;
    };
} sacd_overlay_file_t;
```

---

## 10. Configuration and Mount Options

### 10.1 Command-Line Interface (Linux/macOS)

```
sacd-mount [options] <source_dir> <mount_point>

Options:
  -o allow_other       Allow other users to access the mount
  -o iso_extensions=.iso,.ISO   File extensions to treat as SACD ISOs
  -o threads=N         Number of DST decoder threads (default: auto)
  -o cache_timeout=N   Seconds before unmounting idle ISOs (default: 300)
  -o max_isos=N        Maximum concurrent ISO mounts (default: unlimited)
  -f                   Foreground mode (don't daemonize)
  -d                   Debug mode (implies -f, verbose logging)
```

### 10.2 Windows Mount (WinFSP)

```
sacd-mount.exe [options] <source_dir> <drive_letter>

Options:
  /iso_extensions:.iso,.ISO   File extensions to treat as SACD ISOs
  /threads:N                  Number of DST decoder threads
  /cache_timeout:N            Seconds before unmounting idle ISOs
```

---

## 11. Error Handling

### 11.1 Error Codes

```c
/* Overlay-specific error codes (negative values, errno-compatible) */
#define OVERLAY_OK                    0
#define OVERLAY_ERROR_NOT_FOUND      -2    /* ENOENT */
#define OVERLAY_ERROR_IO             -5    /* EIO */
#define OVERLAY_ERROR_MEMORY        -12    /* ENOMEM */
#define OVERLAY_ERROR_ACCESS        -13    /* EACCES */
#define OVERLAY_ERROR_NOT_DIR       -20    /* ENOTDIR */
#define OVERLAY_ERROR_IS_DIR        -21    /* EISDIR */
#define OVERLAY_ERROR_INVALID       -22    /* EINVAL */
#define OVERLAY_ERROR_TOO_MANY_ISO  -24    /* EMFILE (max ISOs reached) */
#define OVERLAY_ERROR_NOT_SACD      -79    /* Not a valid SACD ISO */
```

### 11.2 Graceful Degradation

- If an ISO fails to parse, it remains visible as a regular .iso file
- If DST decoding fails for a track, return error for that file only
- If source directory becomes inaccessible, return ENOENT for new accesses
- If ID3 write fails, return error but keep in-memory state

---

## 12. Testing Strategy

### 12.1 Unit Tests

- Path resolution (passthrough vs virtual)
- Name collision algorithm
- ISO detection
- File handle lifecycle
- ID3 write buffering and XML serialization

### 12.2 Integration Tests

- Mount with FUSE, verify directory listings
- Open and read virtual DSF files
- Write ID3 tag, verify XML sidecar created
- Concurrent access from multiple processes
- ISO cache eviction with pending writes

### 12.3 Platform-Specific Tests

- FUSE: Test on Linux and macOS
- WinFSP: Test on Windows 10/11

---

## 13. File Structure

```
libs/
  libsacdvfs/
    include/libsacdvfs/
      sacd_vfs.h              (existing)
      sacd_overlay.h          (new: overlay API)
    src/
      sacd_vfs.c              (existing)
      sacd_overlay.c          (new: overlay implementation)
      sacd_overlay_iso.c      (new: ISO management)
      sacd_overlay_path.c     (new: path resolution)

extras/
  sacd-vfs-fuse/
    CMakeLists.txt
    fuse_main.c               (FUSE entry point and option parsing)
    fuse_ops.c                (FUSE operation callbacks)
  sacd-vfs-winfsp/
    CMakeLists.txt
    dokan_main.c              (WinFSP entry point and option parsing)
    dokan_ops.c               (WinFSP operation callbacks)
```

---

## 14. Dependencies

### 14.1 Linux/macOS

- libfuse3 (FUSE 3.x API)
- pthreads

### 14.2 Windows

- WinFSP SDK (https://winfsp.dev/)
- Windows SDK

### 14.3 Common

- libsacdvfs (this project)
- libsacd (this project)
- libdst (this project)
- libsautil (this project) - includes sxmlc and base64

This expanded design document provides a technical roadmap for implementing a virtual filesystem that maps SACD ISO structures to the Sony DSF format. This system allows high-resolution audio players to treat an ISO as a collection of individual tracks without requiring physical extraction or storage duplication.

---

## 1. Project Overview

The objective is to develop a wrapper library that can be used in **FUSE (Filesystem in Userspace)** or similar virtual interface that presents the contents of an SACD ISO as a structured directory of `.dsf` files. The system performs on-the-fly bitstream transformation, decompression, and metadata injection. The wrapper library should contain
virtual directory functions, read functions (seek, read, etc.) and write functionality to update the ID3 tag.

### 1.1 Architecture Layers

The VFS implementation consists of multiple layers:

1. **libsacdvfs** (`libs/libsacdvfs/`) - Core VFS for a single SACD ISO
   - On-the-fly DSD-to-DSF transformation
   - Multi-threaded DST decompression with ring buffer
   - ID3 metadata generation and overlay support
   - XML sidecar persistence for ID3 modifications

2. **libsacdvfs_overlay** (`libs/libsacdvfs/`) - Directory overlay layer
   - Shadow copies source directories
   - Automatically presents SACD ISOs as expandable folders
   - Name collision resolution (Album, Album (1), Album (2)...)
   - Multi-ISO context management with reference counting
   - Thread-safe operations

3. **Platform Adapters** (`extras/`)
   - `sacd-vfs-fuse/` - FUSE adapter for Linux/macOS
   - `sacd-vfs-winfsp/` - WinFSP adapter for Windows

> **See Also:** [SACD_OVERLAY_VFS_DESIGN.md](SACD_OVERLAY_VFS_DESIGN.md) for the complete overlay VFS design.

### 1.2 Virtual Directory Hierarchy

**Per-ISO Structure** (libsacdvfs):

* `/[Album Name]/Stereo/01. Track Name.dsf`
* `/[Album Name]/Multi-channel/01. Track Name.dsf`

**Overlay Structure** (libsacdvfs_overlay):

When mounting a source directory containing SACD ISOs:

```
Source Directory:              Virtual Mount:
  Album1.iso          -->        Album1/
  Album1.iso.xml                   Stereo/
  Album2.iso                         01. Track.dsf
  Other Folder/                    Multi-channel/
    Album3.iso                       01. Track.dsf
                                 Album2/
                                   Stereo/
                                     01. Track.dsf
                                 Other Folder/
                                   Album3/
                                     Stereo/
                                       01. Track.dsf
```

* ISO files (`.iso`, `.ISO`) are hidden and replaced with virtual folders
* The `.xml` sidecar files (ID3 overlays) are also hidden
* Real directories are passed through unchanged
* Name collisions are resolved with `(1)`, `(2)` suffixes

---

## 2. Technical Specifications: DSD Transformation

The core of the project involves a real-time translation layer between the raw SACD sector data and the DSF file format.

### 2.1 Bit-Order and Interleaving

The transformation must account for two primary differences in data layout:

1. **Bit Order:** SACD DSD data is stored **MSB-first**, while the DSF specification requires **LSB-first**.
2. **Interleaving:** SACD uses byte-interleaving, whereas DSF uses block-interleaving (typically 4096-byte blocks).

**The Transformation Logic:**
For a stereo stream, the mapping follows this logic:

* **Input (SACD):**  (1 byte per channel)
* **Output (DSF):** A block of 4096 "L" bytes followed by 4096 "R" bytes.

**Mathematical Mapping:**
The bit-reversal can be optimized using a 256-byte Look-Up Table (LUT) where the index is the input byte and the value is the bit-reversed output.

### 2.2 Sector Assembly (3-in-14 and 3-in-16)

SACD data is organized into blocks of 3 frames. Depending on the disc density, these use different sector mappings:

* **3-in-14:** 3 frames spread across 14 sectors.
* **3-in-16:** 3 frames spread across 16 sectors.

Because these layouts are deterministic and defined by `rdstate` tables in the SACD Scarlet Book, we calculate the byte offset within the ISO using a fixed formula rather than parsing packets.

> **Note:** This process is I/O bound but computationally light, so it is implemented as a **single-threaded** read process to maintain linear disk access patterns.

---

## 3. Lossless Compression: DST Handling

Direct Stream Transfer (DST) is the lossless compression used on many SACDs to fit both stereo and multi-channel tracks on one disc.

### 3.1 Multi-threaded Decompression

Unlike raw DSD, DST decompression is CPU-intensive. To support real-time playback without stuttering:

* **Thread Pooling:** A background worker pool handles the decompression of upcoming frames.
* **Look-ahead Buffering:** The filesystem will decompress the *next* X (configurable in #define) amount of seconds of audio in advance.

### 3.2 Random Access via Access List

Since DST frames have variable sizes, the **SACD Access List** (stored in the ISO metadata) must be parsed. This list provides the byte offsets for every frame, allowing the virtual filesystem to support seeking within a track without decompressing the entire file from the beginning.

---

## 4. Virtual Metadata and ID3 Tagging

DSF files support ID3v2 tags, which the original SACD ISO does not store in a standard format.

| Feature | Implementation Method |
| --- | --- |
| **Virtual Header** | Generate a valid DSF header (92 bytes) and DSD chunk on-the-fly. |
| **Metadata Injection** | Map SACD Master Text (Title, Artist, Album) to ID3v2 frames. |
| **Virtual "Editing"** | Any writes to the ID3 tag area are stored in XML sidecar files next to the ISO. |

### 4.1 ID3 Tag Write Support

Virtual DSF files support limited write operations:

* **Writable Region:** Only the ID3 tag region (at the end of the file) can be modified
* **Read-Only Regions:** DSF header and audio data regions reject write attempts (`EACCES`)
* **Atomic Updates:** ID3 changes are buffered until flush/close

### 4.2 XML Sidecar Storage

ID3 modifications are persisted in XML sidecar files stored next to the ISO:

```
Album.iso           # SACD ISO file (read-only)
Album.iso.xml       # ID3 overlay sidecar (created on first write)
```

**XML Format** (using `libsautil/sxmlc.h`):

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

**Implementation Notes:**
* Base64 encoding uses `libsautil/base64.h` (`sa_base64_encode`, `sa_base64_decode`)
* XML parsing uses `libsautil/sxmlc.h`
* If an XML file with a different structure exists, it will be **overwritten**
* The `SacdId3Overlay` root element and `version` attribute identify the format

### 4.3 API (libsacdvfs)

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

---

## 5. Implementation Architecture

### 5.1 The Read Pipeline

1. **Request:** Player asks for bytes  through  of `Track01.dsf`.
2. **Mapping:** The VFS determines if this range falls in the **Header**, **Metadata**, or **Data** chunk.
3. **Fetch:**
* If **Data**, calculate the necessary SACD sectors.
* If **DST**, check the cache; if not cached, dispatch a decompression task to the thread pool.


4. **Transform:** Perform bit-reversal and re-interleaving.
5. **Deliver:** Return the transformed bytes to the player.

### 5.2 Performance Targets

* **Latency:** Time-to-first-byte should be as low as possible.
* **Concurrency:** Support at least two simultaneous streams (e.g., for cross-fading players).

### 5.3 Re-using existing code

* **SACD to ID3:** see tools/sacd_id3.c
* **SACD parsing and reading:** see the src/ and include/ folders.
* **DSF reading and writing:** see lib/libdsf, if new DSF code 'streaming' code needs to be written and designed specifically for this VFS, so be it.
* **DST decoding:** see lib/libdst.

---

## 6. Overlay VFS Layer

The overlay layer (`libsacdvfs_overlay`) provides directory-level functionality on top of the per-ISO `libsacdvfs`.

### 6.1 Key Features

* **Directory Shadowing:** Source directories are mirrored to the mount point
* **ISO Auto-Detection:** Files with `.iso` or `.ISO` extensions are checked for SACD format
* **Virtual Folder Expansion:** SACD ISOs appear as folders containing DSF files
* **Name Collision Resolution:** When ISO basename conflicts with existing directory
* **Lazy Loading:** ISOs are only parsed when their contents are accessed
* **Multi-Threading:** Thread-safe for concurrent FUSE/WinFSP operations
* **ID3 Write Support:** Virtual DSF files can have their ID3 tags modified

### 6.2 Name Collision Resolution

When a directory contains both `Album.iso` and an `Album/` folder:

```
Source:                    Virtual Mount:
  Album.iso       -->        Album/           (passthrough)
  Album/                     Album (1)/       (virtual ISO)
  Other.iso                  Other/           (virtual ISO)
```

Algorithm:
1. Scan directory for all entries
2. Build set of existing directory names
3. For each ISO: strip extension, check for collision, append `(1)`, `(2)`, etc. if needed

### 6.3 Platform Adapters

**FUSE (Linux/macOS)** - `extras/sacd-vfs-fuse/`:
* Implements FUSE 3.x callbacks (`getattr`, `readdir`, `open`, `read`, `write`, `release`)
* Maps FUSE paths to `sacd_overlay_*` API calls

**WinFSP (Windows)** - `extras/sacd-vfs-winfsp/`:
* Implements WinFSP/Dokan callbacks
* Handles UTF-8 ↔ wide string conversion for Windows paths

### 6.4 Thread Safety

The overlay layer uses:
* Reader-writer locks for directory cache
* Mutex for ISO mount table
* Per-ISO locking (managed by libsacdvfs)
* Atomic reference counting for file handles

### 6.5 Resource Management

* **Reference Counting:** ISO contexts stay open while files are in use
* **Cache Timeout:** Idle ISOs are unmounted after configurable timeout
* **Pending Saves:** ID3 overlay changes are saved before unmounting

> **See Also:** [SACD_OVERLAY_VFS_DESIGN.md](SACD_OVERLAY_VFS_DESIGN.md) for complete API and implementation details.

---

## 7. File Structure

```
libs/
  libsacdvfs/
    include/libsacdvfs/
      sacd_vfs.h              # Single-ISO VFS API
      sacd_overlay.h          # Directory overlay API
    src/
      sacd_vfs.c              # Single-ISO VFS implementation
      sacd_overlay.c          # Overlay implementation
      sacd_overlay_iso.c      # ISO management
      sacd_overlay_path.c     # Path resolution

extras/
  sacd-vfs-fuse/
    CMakeLists.txt
    fuse_main.c               # FUSE entry point
    fuse_ops.c                # FUSE callbacks

  sacd-vfs-winfsp/
    CMakeLists.txt
    dokan_main.c              # WinFSP entry point
    dokan_ops.c               # WinFSP callbacks
```

---

## 8. Dependencies

| Platform | Dependencies |
| --- | --- |
| Linux/macOS | libfuse3, pthreads |
| Windows | WinFSP SDK |
| Common | libsacdvfs, libsacd, libdst, libsautil |

---

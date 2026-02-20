# DSD Nexus

Cross-platform C/C++ libraries and tools for working with DSD (Direct Stream Digital) audio and SACD (Super Audio CD) discs.

### Tools

- **sacd-vfs** — A FUSE/WinFSP virtual filesystem that mounts SACD ISO images as browsable directories. Tracks appear as standard DSF files with on-the-fly conversion from DSD/DST, enabling direct playback in any compatible audio player without prior extraction. Supports ID3 tag editing (persisted to XML sidecars) and exposes both stereo and multichannel areas.

- **Nexus Forge** — A Qt6 desktop GUI for batch DSD audio conversion and SACD extraction. Supports drag-and-drop, format conversion between DSF, DSDIFF, WAV, and FLAC, DSD-to-PCM transcoding with configurable quality, and direct SACD ripping from PS3 drives or network servers.

- **dsdctl** — A unified command-line tool with three subcommands: `convert` (transcode between DSD and PCM formats), `extract` (rip SACD ISOs from PS3 drives or network servers), and `info` (inspect metadata with optional JSON output).

- **ps3-drive** — A utility for communicating with PS3 Blu-ray drives over SCSI to read and decrypt SACD discs. Implements the full BD authentication and SAC key exchange protocol to derive AES decryption keys, enabling direct ripping of physical SACDs. Supports Linux and Windows.

### Libraries

- **libsautil** — Shared utility functions used across all libraries (memory allocation, string handling, logging, threading).
- **libdsdiff** — Reader/writer for the DSDIFF container format.
- **libdsf** — Reader/writer for the DSF container format.
- **libdst** — DST (Direct Stream Transfer) lossless decoder for compressed DSD streams.
- **libdsdpcm** — DSD-to-PCM conversion engine with configurable sample rate and bit depth.
- **libsacd** — SACD disc image and network stream parser (TOC, track layout, metadata).
- **libsacdvfs** — Virtual filesystem layer that presents SACD ISOs as directories of DSF files with on-the-fly conversion.
- **libdsdpipe** — High-level DSD audio pipeline coordinating format detection, decoding, and output.
- **libsg3** — Cross-platform SCSI generic command interface (Linux, FreeBSD, Windows).
- **libps3drive** — PS3 Blu-ray drive communication with BD authentication and SAC key exchange for SACD decryption.

## Project Structure (Pitchfork Layout)

```
libs/               Library subprojects
  libsautil/        Shared utility functions (memory, string, logging, threading)
  libdsdiff/        DSDIFF container format
  libdsf/           DSF container format
  libdst/           DST decoder
  libdsdpcm/        DSD to PCM conversion (C wrapper for C++ core)
  libsacd/          SACD ISO / network reader
  libsacdvfs/       SACD virtual filesystem
  libdsdpipe/       DSD audio pipeline
  libsg3/           SCSI generic interface
  libps3drive/      PS3 drive communication
extras/             Applications
  nexus-forge/      Qt6 desktop GUI for DSD audio conversion and SACD extraction
  dsdctl/           Unified DSD command-line tool (convert, extract, info)
  ps3-drive/        PS3 BluRay drive utility
  sacd-vfs/         FUSE filesystem mount for SACD images
external/           Third-party vendored code
```

All library subprojects are compiled as OBJECT libraries and combined into a single umbrella shared library (`libdsd` / `dsd.dll`).

## Requirements

- CMake 3.20+
- MSVC or GCC 11+
- Qt6 (Core, Gui, Widgets) for nexus-forge GUI (optional -- skipped if not found)
- WinFSP (Windows), libfuse3 (Linux), or macFUSE (macOS) for sacd-vfs

### Optional System Dependencies (Linux)

The build system will automatically fetch and compile external libraries (FLAC, TBB, mbedTLS) if they are not found on your system. However, using system packages is **recommended** to avoid compilation overhead and reduce build times.

**Ubuntu/Debian:**
```bash
sudo apt install pkg-config libflac-dev libtbb-dev libmbedtls-dev libfuse3-dev
```

**Fedora/RHEL:**
```bash
sudo dnf install pkgconfig flac-devel tbb-devel mbedtls-devel fuse3-devel
```

**Arch Linux:**
```bash
sudo pacman -S pkgconf flac tbb mbedtls fuse3
```

When these packages are installed, CMake will automatically detect and use them instead of fetching from GitHub. Note: `libfuse3-dev` is required for building the `sacd-vfs` FUSE filesystem tool.

### Intel TBB (Optional, Highly Recommended for Performance)

Intel TBB (Threading Building Blocks) provides **highly optimized** parallel programming utilities for multi-threaded performance. Using the official Intel oneAPI TBB can result in **better threading performance** and stability compared to older system package versions.

**Installation:**

Download and install Intel TBB from:
- https://www.intel.com/content/www/us/en/developer/tools/oneapi/onetbb-download.html

Or on Ubuntu/Debian via APT:
```bash
# Add Intel repository (if not already added)
wget -O- https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB | gpg --dearmor | sudo tee /usr/share/keyrings/oneapi-archive-keyring.gpg > /dev/null
echo "deb [signed-by=/usr/share/keyrings/oneapi-archive-keyring.gpg] https://apt.repos.intel.com/oneapi all main" | sudo tee /etc/apt/sources.list.d/oneAPI.list
sudo apt update

# Install TBB
sudo apt install intel-oneapi-tbb-devel
```

After installation, source the environment:
```bash
source /opt/intel/oneapi/setvars.sh
```

CMake will automatically detect and use Intel TBB if available.

### Intel IPP (Optional, Highly Recommended for Performance)

Intel IPP (Integrated Performance Primitives) provides **highly optimized** signal processing routines for DSD to PCM conversion. Using IPP can result in **significantly faster** conversion speeds compared to the standard implementation.

**Installation:**

Download and install Intel IPP from:
- https://www.intel.com/content/www/us/en/developer/tools/oneapi/ipp-download.html

Or on Ubuntu/Debian via APT:
```bash
# Add Intel repository
wget -O- https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB | gpg --dearmor | sudo tee /usr/share/keyrings/oneapi-archive-keyring.gpg > /dev/null
echo "deb [signed-by=/usr/share/keyrings/oneapi-archive-keyring.gpg] https://apt.repos.intel.com/oneapi all main" | sudo tee /etc/apt/sources.list.d/oneAPI.list
sudo apt update

# Install IPP
sudo apt install intel-oneapi-ipp-devel
```

After installation, source the environment:
```bash
source /opt/intel/oneapi/setvars.sh
```

CMake will automatically detect and use IPP if available. You'll see:
```
-- Found Intel IPP at: /opt/intel/oneapi/ipp/latest
-- Building dsdpcm_core with Intel IPP optimization (static)
```

### Qt6 for Nexus Forge GUI (Optional)

Qt6 is required to build the **Nexus Forge** desktop GUI application. If Qt6 is not found, CMake will skip building the GUI and only build the command-line tools and libraries.

**Installation:**

**Ubuntu/Debian:**
```bash
sudo apt install qt6-base-dev qt6-tools-dev qt6-tools-dev-tools
```

**Fedora/RHEL:**
```bash
sudo dnf install qt6-qtbase-devel qt6-qttools-devel
```

**Arch Linux:**
```bash
sudo pacman -S qt6-base qt6-tools
```

After installing Qt6, reconfigure CMake to detect it:
```bash
cmake -B build
```

CMake will automatically detect Qt6 and enable building nexus-forge. If Qt6 is not automatically detected, you may need to specify the Qt6 installation path:
```bash
cmake -B build -DCMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/cmake
```

## Building

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64 \
    -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64

cmake --build build --config Release
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_SHARED_LIBS` | `ON` | Build shared libraries (required for LGPL compliance) |
| `BUILD_EXTRAS` | `ON` | Build applications (nexus-forge, dsdctl, etc.) |
| `BUILD_EXAMPLES` | `ON` | Build example programs |
| `BUILD_TESTS` | `ON` | Build test suite |
| `ENABLE_COVERAGE` | `OFF` | Enable code coverage (GCC only) |
| `ENABLE_ASAN` | `OFF` | Enable Address Sanitizer |

## Installation

```bash
cmake --install build --config Release --prefix /path/to/install
```

## Creating an Installer (Windows)

The project uses [Qt Installer Framework](https://doc.qt.io/qtinstallerframework/) (QtIFW) via CMake's CPack IFW generator to produce a Windows installer with component selection.

### Prerequisites

1. **Qt6** -- shared libraries (already required for building nexus-forge)
2. **Qt Installer Framework** -- install via Qt Maintenance Tool or download from [qt.io](https://download.qt.io/official_releases/qt-installer-framework/)

### Building the Installer

```bash
# Configure with Qt and QtIFW paths
cmake -B build -G "Visual Studio 17 2022" -A x64 \
    -DCMAKE_PREFIX_PATH=C:/Qt/6.10.2/msvc2022_64 \
    -DCPACK_IFW_ROOT=C:/Qt/Tools/QtInstallerFramework/4.10

# Build
cmake --build build --config Release

# Create installer
cd build
cpack -G IFW -C Release
```

The installer is output as `build/nexus-forge-<version>-win64-setup.exe`.

### Installer Components

The installer presents three selectable components:

| Component | Contents | Default |
|-----------|----------|---------|
| **Nexus Forge Application** | nexus-forge.exe, dsd.dll, Qt6 runtime DLLs and plugins | Always installed |
| **Command-Line Tools** | dsdctl.exe, ps3drive-tool.exe, sacd-vfs.exe | Selected |
| **Development Files** | C headers, dsd.lib import library, pkg-config and CMake config files | Not selected |

### How It Works

- **windeployqt** runs automatically as a post-build step when nexus-forge is compiled. It scans the executable for Qt module dependencies and stages all required Qt DLLs and plugins into `build/qt_deploy/`.
- **CPack** collects all install targets (executables, libraries, headers) organized by component and feeds them to the QtIFW `binarycreator` tool to produce the final installer.
- The packaging configuration lives in [cmake/packaging.cmake](cmake/packaging.cmake).

### Fallback

If QtIFW is not installed, you can still create a tarball/zip:

```bash
cd build
cpack -G TGZ -C Release
```

## Building on macOS

### Prerequisites

- **Xcode Command Line Tools** (`xcode-select --install`)
- **CMake** -- install from [cmake.org](https://cmake.org/download/) or via Homebrew (`brew install cmake`)
- **macFUSE** -- required for sacd-vfs ([osxfuse.github.io](https://osxfuse.github.io/))
- **Qt6** -- required for nexus-forge GUI. Install via [Qt Online Installer](https://www.qt.io/download-qt-installer)
- **Qt Installer Framework** -- required for creating the installer. Install via Qt Maintenance Tool

PS3 drive support is automatically disabled on macOS (no SCSI pass-through).

### Building

**ARM64 only** (Apple Silicon):
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_EXTRAS=ON \
    -DCMAKE_PREFIX_PATH=$HOME/Qt/6.10.2/macos

cmake --build build --config Release -j$(sysctl -n hw.ncpu)
```

**Universal binary** (ARM64 + Intel x86_64):
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_EXTRAS=ON \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_PREFIX_PATH=$HOME/Qt/6.10.2/macos

cmake --build build --config Release -j$(sysctl -n hw.ncpu)
```

### Creating the macOS Installer

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_EXTRAS=ON \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_PREFIX_PATH=$HOME/Qt/6.10.2/macos \
    -DCPACK_IFW_ROOT=$HOME/Qt/Tools/QtInstallerFramework/4.10

cmake --build build --config Release -j$(sysctl -n hw.ncpu)

cd build
cpack -G IFW -C Release
```

The installer is output as `build/nexus-forge-<version>-macos-universal-setup.dmg`.

Nexus Forge is built as a self-contained `.app` bundle with all Qt frameworks embedded via `macdeployqt`. The CLI tools (dsdctl, sacd-vfs) and libdsd.dylib are installed alongside.

## Building Linux Packages (sacd-vfs)

The `sacd-vfs` FUSE daemon can be packaged as `.deb` (Debian/Ubuntu), `.rpm` (Fedora/RHEL/openSUSE), or Arch Linux packages. A devcontainer is provided for building packages from Windows or any Docker-capable host.

### Using the Devcontainer

Build the Docker image once:

```bash
docker build -t sacd-vfs-pkg .devcontainer/
```

Then build packages inside the container:

```bash
docker run --rm -v "$(pwd):/workspace" sacd-vfs-pkg bash -c '
    dos2unix packaging/linux/deb/* packaging/linux/rpm/* packaging/linux/*.sh \
             packaging/linux/*.service packaging/linux/*.conf
    cmake -B build-pkg -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
          -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=OFF -DSACD_VFS_ONLY=ON
    cmake --build build-pkg --target sacd-vfs -j$(nproc)
    cd build-pkg && cpack -G DEB -C Release
'
```

Replace `-G DEB` with `-G RPM` for an RPM package, or `-G TGZ` for a tarball.

### Building on a Native Linux Host

```bash
sudo apt install libfuse3-dev   # Debian/Ubuntu
cmake -B build-pkg -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
      -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=OFF -DSACD_VFS_ONLY=ON
cmake --build build-pkg --target sacd-vfs -j$(nproc)
cd build-pkg && cpack -G DEB -C Release
```

### Building the Arch Package

```bash
cd packaging/arch
makepkg -s
```

Or from a non-Arch host with Docker:

```bash
docker run --rm -v "$(pwd):/workspace:ro" archlinux:latest bash -c '
    pacman -Syu --noconfirm base-devel cmake fuse3 mbedtls
    useradd -m builder && echo "builder ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers
    cp -r /workspace /home/builder/src && chown -R builder:builder /home/builder/src
    su - builder -c "cd /home/builder/src/packaging/arch && makepkg -s --noconfirm"
'
```

### Package Features

The `.deb` package includes interactive configuration via debconf:
- Source directory (SACD ISO location)
- Mount point
- Service user
- Stereo/multichannel area visibility

Configuration is stored in `/etc/sacd-vfs/sacd-vfs.conf` and can be reconfigured with `dpkg-reconfigure sacd-vfs`. The systemd service (`sacd-vfs.service`) is enabled automatically on install.

## License

All components are licensed under LGPL-2.1, except for the GUI -- see [LICENSE](LICENSE) for details.

Nexus Forge (the GUI) is licensed under the GPL and was originally derived from [MystiQ](https://github.com/swl-x/MystiQ).

## Support my work
Thank you for thinking about supporting my work.

[!["Buy Me A Coffee"](https://www.buymeacoffee.com/assets/img/custom_images/orange_img.png)](https://www.buymeacoffee.com/wichers)

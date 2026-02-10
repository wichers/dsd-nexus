# libdsdpipe - DSD Audio Pipeline Library

A simple, composable pipeline library for processing DSD audio from various sources to multiple output formats.

## Architecture Overview

```
+----------+     +------------+     +----------+
| Sources  | --> | Transforms | --> |  Sinks   |
+----------+     +------------+     +----------+
  - SACD ISO      - DST Decode       - DSF
  - DSDIFF        - DSD->PCM         - DSDIFF
  - DSF                              - DSDIFF Edit Master
                                     - WAV
                                     - FLAC
```

## Key Design Principles

### 1. Pipeline Level Handles Track Selection

Track selection (`"1-3,5,7-9"`, `"all"`) is managed by the pipeline, not by sources or sinks.

- User calls `dsdpipe_select_tracks_str(pipe, "1-3,5")`
- Pipeline stores selection in `pipe->tracks`
- During `dsdpipe_run()`, pipeline iterates over selected tracks
- For each track, pipeline calls `source->seek_track(track_number)`

Sources and sinks only deal with individual track numbers - they don't parse or store track selections.

### 2. Sources Know Track Boundaries

Each source is responsible for:
- Knowing how many tracks exist (`get_track_count`)
- Seeking to a specific track (`seek_track`)
- Reading frames sequentially (`read_frame`)
- Signaling track end via `dsdpipe_BUF_FLAG_TRACK_END`

Track boundary information comes from the source's native format:
- **SACD**: Master TOC / Area TOC contains track start positions
- **DSDIFF**: Marker chunks define track boundaries
- **DSF**: Single track per file (track count = 1)

### 3. Sinks Receive Track Notifications

Sinks are notified of track boundaries via callbacks:
- `track_start(ctx, track_number, metadata)` - Before first frame of track
- `write_frame(ctx, buffer)` - For each audio frame
- `track_end(ctx, track_number)` - After last frame of track

This allows sinks to:
- **Per-track mode**: Create new file for each track
- **Edit master mode**: Add markers at track boundaries, write to single file

### 4. Transforms Are Auto-Inserted

The pipeline automatically inserts transforms based on source format and sink requirements:

```c
/* If source=DST and sinks need DSD, insert DST decoder */
if (source_format == DST && needs_dsd && !can_accept_dst) {
    pipe->dst_decoder = create_dst_decoder();
}

/* If any sink needs PCM, insert DSD-to-PCM converter */
if (needs_pcm) {
    pipe->dsd2pcm = create_dsd2pcm_converter();
}
```

### 5. Multiple Simultaneous Sinks

A single pipeline can write to multiple sinks simultaneously:

```c
dsdpipe_add_sink_dsf(pipe, "output.dsf", true);      // DSF with ID3
dsdpipe_add_sink_dsdiff(pipe, "master.dff", 0, 1);   // Edit master
dsdpipe_add_sink_wav(pipe, "output.wav", 24, 88200); // 24-bit WAV
```

Each frame is routed to sinks based on their capabilities:
- DSD sinks receive DSD data
- PCM sinks receive PCM data (after DSD2PCM transform)
- DST sinks can receive compressed DST data directly

## Data Flow

```
dsdpipe_run():
    for each selected track:
        source->seek_track(track_number)

        for each sink:
            sink->track_start(track_number, metadata)

        while not end_of_track:
            buffer = source->read_frame()

            if dst_decoder and buffer is DST:
                buffer = dst_decoder->process(buffer)

            write_to_dsd_sinks(buffer)

            if dsd2pcm:
                pcm_buffer = dsd2pcm->process(buffer)
                write_to_pcm_sinks(pcm_buffer)

        for each sink:
            sink->track_end(track_number)

    for each sink:
        sink->finalize()
```

## Buffer Management

Buffers use reference counting via `libsautil/buffer.h` (`sa_buffer_pool_t`):

```c
buffer = dsdpipe_buffer_alloc_dsd(pipe);  // Get from pool
dsdpipe_buffer_unref(buffer);              // Release (returns to pool automatically)
```

## Error Handling

- Functions return `dsdpipe_OK` (0) on success, negative error codes on failure
- `dsdpipe_get_error_message(pipe)` returns detailed error string
- `dsdpipe_error_string(code)` converts error code to string

## Thread Safety

- `dsdpipe_cancel(pipe)` is thread-safe (uses atomics)
- Pipeline checks cancellation flag between frames
- Progress callback can return non-zero to cancel

## API Summary

```c
// Lifecycle
dsdpipe_t *dsdpipe_create(void);
void dsdpipe_destroy(dsdpipe_t *pipe);
int dsdpipe_reset(dsdpipe_t *pipe);

// Source
int dsdpipe_set_source_sacd(pipe, path, channel_type);
int dsdpipe_set_source_dsdiff(pipe, path);
int dsdpipe_set_source_dsf(pipe, path);

// Track Selection
int dsdpipe_select_tracks_str(pipe, "1-3,5,7-9");
int dsdpipe_select_all_tracks(pipe);

// Sinks
int dsdpipe_add_sink_dsf(pipe, path, write_id3);
int dsdpipe_add_sink_dsdiff(pipe, path, write_dst, edit_master);
int dsdpipe_add_sink_wav(pipe, path, bit_depth, sample_rate);
int dsdpipe_add_sink_flac(pipe, path, bit_depth, compression);

// Execution
int dsdpipe_set_progress_callback(pipe, callback, userdata);
int dsdpipe_run(pipe);
void dsdpipe_cancel(pipe);
```

## Example Usage

```c
dsdpipe_t *pipe = dsdpipe_create();

// Configure source
dsdpipe_set_source_sacd(pipe, "album.iso", dsdpipe_CHANNEL_STEREO);
dsdpipe_select_tracks_str(pipe, "1-5");

// Add outputs
dsdpipe_add_sink_dsf(pipe, "output/album", true);
dsdpipe_add_sink_dsdiff(pipe, "output/master.dff", false, true);

// Run
dsdpipe_set_progress_callback(pipe, my_progress_handler, NULL);
int result = dsdpipe_run(pipe);

dsdpipe_destroy(pipe);
```

## File Structure

```
libs/libdsdpipe/
├── include/libdsdpipe/
│   ├── dsdpipe.h          # Public API
│   └── version.h           # Version macros
└── src/
    ├── dsdpipe.c          # Pipeline implementation
    ├── dsdpipe_internal.h # Internal structures
    ├── track_select.c      # Track selection parsing
    ├── metadata.c          # Metadata utilities
    ├── source_sacd.c       # SACD ISO source
    ├── source_dsdiff.c     # DSDIFF source
    ├── source_dsf.c        # DSF source
    ├── sink_dsf.c          # DSF sink
    ├── sink_dsdiff.c       # DSDIFF sink (+ edit master)
    ├── sink_wav.c          # WAV sink
    ├── sink_flac.c         # FLAC sink
    ├── transform_dst.c     # DST decoder wrapper
    └── transform_dsd2pcm.c # DSD-to-PCM wrapper
```

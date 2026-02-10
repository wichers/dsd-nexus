/**
 * @file dsdpcm_wrapper.cpp
 * @brief C wrapper implementation for libdsdpcm
 *
 * This file provides the C API that wraps the C++ dsdpcm_decoder_t class.
 *
 * IMPORTANT: We avoid including libdsdpcm/dsdpcm.h directly because it
 * defines a typedef that conflicts with the C++ class name. Instead, we
 * forward declare and manually define types here.
 */

#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <new>

// Include C++ core library headers FIRST
#include <dsdpcm_decoder.h>

// Re-declare types from the C API header without the typedef conflict
// These match the definitions in libdsdpcm/dsdpcm.h
extern "C" {

// Error codes (from dsdpcm.h)
#define DSDPCM_OK                    0
#define DSDPCM_ERR_NULL_POINTER     -1
#define DSDPCM_ERR_INVALID_PARAM    -2
#define DSDPCM_ERR_ALLOC_FAILED     -3
#define DSDPCM_ERR_NOT_INITIALIZED  -4
#define DSDPCM_ERR_UNSUPPORTED      -5
#define DSDPCM_ERR_FIR_REQUIRED     -6
#define DSDPCM_ERR_PRECISION_MISMATCH -7
#define DSDPCM_ERR_FILE_OPEN        -10
#define DSDPCM_ERR_FILE_READ        -11
#define DSDPCM_ERR_FILE_WRITE       -12
#define DSDPCM_ERR_FILE_FORMAT      -13
#define DSDPCM_ERR_BUFFER_TOO_SMALL -14

// Conversion types (from dsdpcm.h)
typedef enum dsdpcm_conv_type_e {
    DSDPCM_CONV_UNKNOWN    = -1,
    DSDPCM_CONV_MULTISTAGE = 0,
    DSDPCM_CONV_DIRECT     = 1,
    DSDPCM_CONV_USER       = 2
} dsdpcm_conv_type_t;

// Precision modes (from dsdpcm.h)
typedef enum dsdpcm_precision_e {
    DSDPCM_PRECISION_FP32 = 0,
    DSDPCM_PRECISION_FP64 = 1
} dsdpcm_precision_t;

// Decimation factors (from dsdpcm.h)
typedef enum dsdpcm_decimation_e {
    DSDPCM_DECIMATION_AUTO = 0,
    DSDPCM_DECIMATION_8    = 8,
    DSDPCM_DECIMATION_16   = 16,
    DSDPCM_DECIMATION_32   = 32,
    DSDPCM_DECIMATION_64   = 64,
    DSDPCM_DECIMATION_128  = 128,
    DSDPCM_DECIMATION_256  = 256,
    DSDPCM_DECIMATION_512  = 512,
    DSDPCM_DECIMATION_1024 = 1024
} dsdpcm_decimation_t;

// FIR structure (from dsdpcm.h)
typedef struct dsdpcm_fir_s {
    double              *coefficients;
    size_t               count;
    dsdpcm_decimation_t  decimation;
    char                *name;
} dsdpcm_fir_t;

// Audio sample types
typedef float dsdpcm_sample32_t;
typedef double dsdpcm_sample64_t;

#if defined(_M_X64) || defined(_M_ARM64) || defined(__x86_64__) || defined(__aarch64__) || defined(__LP64__)
typedef double dsdpcm_sample_t;
#define DSDPCM_DEFAULT_FP64 1
#else
typedef float dsdpcm_sample_t;
#define DSDPCM_DEFAULT_FP64 0
#endif

// Constants from dsdpcm_internal.h
#define DSDPCM_FIR_MAX_NAME_LENGTH 256
#define DSDPCM_FIR_MAX_COEFFICIENTS 8192

// Forward declaration of struct
struct dsdpcm_decoder_s;

// Forward declarations of functions
int dsdpcm_init(struct dsdpcm_decoder_s *decoder,
                size_t channels,
                size_t framerate,
                size_t dsd_samplerate,
                size_t pcm_samplerate,
                dsdpcm_conv_type_t conv_type,
                dsdpcm_precision_t precision,
                const dsdpcm_fir_t *fir);

} // extern "C"

/* ==========================================================================
 * Internal Decoder Structure
 * ========================================================================== */

/**
 * @brief Internal decoder structure holding the C++ implementation
 *
 * The C API's dsdpcm_decoder_t is a typedef to this struct.
 * We use the C++ ::dsdpcm_decoder_t class as the implementation.
 */
struct dsdpcm_decoder_s {
    ::dsdpcm_decoder_t *impl;        // C++ decoder instance
    dsdpcm_conv_type_t  conv_type;   // Cached conversion type
    dsdpcm_precision_t  precision;   // Cached precision mode
    size_t              channels;    // Number of channels
    size_t              framerate;   // Frame rate
    size_t              dsd_samplerate; // DSD sample rate
    size_t              pcm_samplerate; // PCM sample rate
    bool                initialized; // Initialization flag

    // Cached conversion buffer for FP32 mode on 64-bit platforms
    double             *fp32_conv_buffer;      // Temp buffer for double->float conversion
    size_t              fp32_conv_buffer_size; // Buffer size in samples
};

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

/**
 * @brief Convert C enum to C++ enum class
 */
static inline ::conv_type_e to_cpp_conv_type(dsdpcm_conv_type_t type)
{
    switch (type) {
        case DSDPCM_CONV_MULTISTAGE: return ::conv_type_e::MULTISTAGE;
        case DSDPCM_CONV_DIRECT:     return ::conv_type_e::DIRECT;
        case DSDPCM_CONV_USER:       return ::conv_type_e::USER;
        default:                     return ::conv_type_e::UNKNOWN;
    }
}

/* ==========================================================================
 * Decoder Lifecycle Functions
 * ========================================================================== */

extern "C" dsdpcm_decoder_s *dsdpcm_create(void)
{
    dsdpcm_decoder_s *decoder = nullptr;

    try {
        decoder = new dsdpcm_decoder_s();
        decoder->impl = new ::dsdpcm_decoder_t();
        decoder->conv_type = DSDPCM_CONV_UNKNOWN;
        decoder->precision = DSDPCM_PRECISION_FP64;
        decoder->channels = 0;
        decoder->framerate = 0;
        decoder->dsd_samplerate = 0;
        decoder->pcm_samplerate = 0;
        decoder->initialized = false;
        decoder->fp32_conv_buffer = nullptr;
        decoder->fp32_conv_buffer_size = 0;
    } catch (const std::bad_alloc&) {
        if (decoder) {
            delete decoder->impl;
            delete decoder;
        }
        return nullptr;
    }

    return decoder;
}

extern "C" void dsdpcm_destroy(dsdpcm_decoder_s *decoder)
{
    if (!decoder) {
        return;
    }

    if (decoder->impl) {
        decoder->impl->free();
        delete decoder->impl;
    }

    // Free cached conversion buffer
    free(decoder->fp32_conv_buffer);

    delete decoder;
}

/* ==========================================================================
 * Initialization Functions
 * ========================================================================== */

extern "C" int dsdpcm_init_multistage(dsdpcm_decoder_s *decoder,
                                      size_t channels,
                                      size_t framerate,
                                      size_t dsd_samplerate,
                                      size_t pcm_samplerate,
                                      dsdpcm_precision_t precision)
{
    return dsdpcm_init(decoder, channels, framerate, dsd_samplerate,
                       pcm_samplerate, DSDPCM_CONV_MULTISTAGE, precision, nullptr);
}

extern "C" int dsdpcm_init_direct(dsdpcm_decoder_s *decoder,
                                  size_t channels,
                                  size_t framerate,
                                  size_t dsd_samplerate,
                                  size_t pcm_samplerate,
                                  dsdpcm_precision_t precision)
{
    return dsdpcm_init(decoder, channels, framerate, dsd_samplerate,
                       pcm_samplerate, DSDPCM_CONV_DIRECT, precision, nullptr);
}

extern "C" int dsdpcm_init_user_fir(dsdpcm_decoder_s *decoder,
                                    size_t channels,
                                    size_t framerate,
                                    size_t dsd_samplerate,
                                    size_t pcm_samplerate,
                                    dsdpcm_precision_t precision,
                                    const dsdpcm_fir_t *fir)
{
    return dsdpcm_init(decoder, channels, framerate, dsd_samplerate,
                       pcm_samplerate, DSDPCM_CONV_USER, precision, fir);
}

extern "C" int dsdpcm_init(dsdpcm_decoder_s *decoder,
                           size_t channels,
                           size_t framerate,
                           size_t dsd_samplerate,
                           size_t pcm_samplerate,
                           dsdpcm_conv_type_t conv_type,
                           dsdpcm_precision_t precision,
                           const dsdpcm_fir_t *fir)
{
    if (!decoder) {
        return DSDPCM_ERR_NULL_POINTER;
    }

    if (!decoder->impl) {
        return DSDPCM_ERR_NULL_POINTER;
    }

    if (channels == 0 || framerate == 0 || dsd_samplerate == 0 || pcm_samplerate == 0) {
        return DSDPCM_ERR_INVALID_PARAM;
    }

    if (conv_type == DSDPCM_CONV_UNKNOWN) {
        return DSDPCM_ERR_INVALID_PARAM;
    }

    // For USER mode, FIR data is required
    if (conv_type == DSDPCM_CONV_USER) {
        if (!fir || !fir->coefficients || fir->count == 0) {
            return DSDPCM_ERR_FIR_REQUIRED;
        }
    }

    // Free any existing state
    if (decoder->initialized) {
        decoder->impl->free();
        decoder->initialized = false;
    }

    // Prepare FIR data for USER mode
    double *fir_data = nullptr;
    size_t fir_size = 0;
    size_t fir_decimation = 0;

    if (conv_type == DSDPCM_CONV_USER && fir) {
        fir_data = fir->coefficients;
        fir_size = fir->count;
        fir_decimation = static_cast<size_t>(fir->decimation);
    }

    // Convert precision to bool
    bool conv_fp64 = (precision == DSDPCM_PRECISION_FP64);

    // Initialize the C++ decoder
    int result = decoder->impl->init(
        channels,
        framerate,
        dsd_samplerate,
        pcm_samplerate,
        to_cpp_conv_type(conv_type),
        conv_fp64,
        fir_data,
        fir_size,
        fir_decimation
    );

    if (result != 0) {
        return DSDPCM_ERR_INVALID_PARAM;
    }

    // Cache parameters
    decoder->conv_type = conv_type;
    decoder->precision = precision;
    decoder->channels = channels;
    decoder->framerate = framerate;
    decoder->dsd_samplerate = dsd_samplerate;
    decoder->pcm_samplerate = pcm_samplerate;
    decoder->initialized = true;

    return DSDPCM_OK;
}

extern "C" void dsdpcm_free(dsdpcm_decoder_s *decoder)
{
    if (!decoder || !decoder->impl) {
        return;
    }

    decoder->impl->free();
    decoder->initialized = false;

    // Free cached conversion buffer
    free(decoder->fp32_conv_buffer);
    decoder->fp32_conv_buffer = nullptr;
    decoder->fp32_conv_buffer_size = 0;
}

/* ==========================================================================
 * Query Functions
 * ========================================================================== */

extern "C" int dsdpcm_get_delay(dsdpcm_decoder_s *decoder, double *delay)
{
    if (!decoder || !delay) {
        return DSDPCM_ERR_NULL_POINTER;
    }

    if (!decoder->impl || !decoder->initialized) {
        return DSDPCM_ERR_NOT_INITIALIZED;
    }

    *delay = decoder->impl->get_delay();
    return DSDPCM_OK;
}

extern "C" int dsdpcm_get_conv_type(dsdpcm_decoder_s *decoder, dsdpcm_conv_type_t *conv_type)
{
    if (!decoder || !conv_type) {
        return DSDPCM_ERR_NULL_POINTER;
    }

    *conv_type = decoder->conv_type;
    return DSDPCM_OK;
}

extern "C" int dsdpcm_get_precision(dsdpcm_decoder_s *decoder, dsdpcm_precision_t *precision)
{
    if (!decoder || !precision) {
        return DSDPCM_ERR_NULL_POINTER;
    }

    *precision = decoder->precision;
    return DSDPCM_OK;
}

extern "C" int dsdpcm_is_initialized(dsdpcm_decoder_s *decoder)
{
    if (!decoder) {
        return 0;
    }
    return decoder->initialized ? 1 : 0;
}

/* ==========================================================================
 * Conversion Functions
 * ========================================================================== */

extern "C" int dsdpcm_convert(dsdpcm_decoder_s *decoder,
                              const uint8_t *dsd_data,
                              size_t dsd_size,
                              dsdpcm_sample_t *pcm_data,
                              size_t *pcm_samples)
{
    if (!decoder || !dsd_data || !pcm_data || !pcm_samples) {
        return DSDPCM_ERR_NULL_POINTER;
    }

    if (!decoder->impl || !decoder->initialized) {
        return DSDPCM_ERR_NOT_INITIALIZED;
    }

    // Check precision matches platform default
#if DSDPCM_DEFAULT_FP64
    if (decoder->precision != DSDPCM_PRECISION_FP64) {
        return DSDPCM_ERR_PRECISION_MISMATCH;
    }
#else
    if (decoder->precision != DSDPCM_PRECISION_FP32) {
        return DSDPCM_ERR_PRECISION_MISMATCH;
    }
#endif

    *pcm_samples = decoder->impl->convert(dsd_data, dsd_size, pcm_data);
    return DSDPCM_OK;
}

extern "C" int dsdpcm_convert_fp32(dsdpcm_decoder_s *decoder,
                                   const uint8_t *dsd_data,
                                   size_t dsd_size,
                                   dsdpcm_sample32_t *pcm_data,
                                   size_t *pcm_samples)
{
    if (!decoder || !dsd_data || !pcm_data || !pcm_samples) {
        return DSDPCM_ERR_NULL_POINTER;
    }

    if (!decoder->impl || !decoder->initialized) {
        return DSDPCM_ERR_NOT_INITIALIZED;
    }

    // The original C++ engine only handles single-frame inputs.
    // Process frame-by-frame.
    size_t frame_dsd_bytes = (decoder->dsd_samplerate / 8 / decoder->framerate) * decoder->channels;
    size_t frame_pcm_samples = (decoder->pcm_samplerate / decoder->framerate) * decoder->channels;

#if defined(_M_X64) || defined(_M_ARM64) || defined(__x86_64__) || defined(__aarch64__) || defined(__LP64__)
    // On 64-bit, audio_sample is double. We need a temp buffer for each frame.
    // Ensure cached buffer is large enough for one frame
    if (decoder->fp32_conv_buffer_size < frame_pcm_samples) {
        double *new_buffer = static_cast<double*>(realloc(decoder->fp32_conv_buffer,
                                                          frame_pcm_samples * sizeof(double)));
        if (!new_buffer) {
            return DSDPCM_ERR_ALLOC_FAILED;
        }
        decoder->fp32_conv_buffer = new_buffer;
        decoder->fp32_conv_buffer_size = frame_pcm_samples;
    }

    size_t total_pcm_samples = 0;
    size_t offset = 0;

    while (offset + frame_dsd_bytes <= dsd_size) {
        // Convert one frame
        size_t samples_out = decoder->impl->convert(
            dsd_data + offset,
            frame_dsd_bytes,
            decoder->fp32_conv_buffer
        );

        // Convert double to float
        for (size_t i = 0; i < samples_out; i++) {
            pcm_data[total_pcm_samples + i] = static_cast<float>(decoder->fp32_conv_buffer[i]);
        }

        total_pcm_samples += samples_out;
        offset += frame_dsd_bytes;
    }

    *pcm_samples = total_pcm_samples;
    return DSDPCM_OK;
#else
    // On 32-bit, audio_sample is float - process frame by frame directly
    size_t total_pcm_samples = 0;
    size_t offset = 0;

    while (offset + frame_dsd_bytes <= dsd_size) {
        size_t samples_out = decoder->impl->convert(
            dsd_data + offset,
            frame_dsd_bytes,
            pcm_data + total_pcm_samples
        );
        total_pcm_samples += samples_out;
        offset += frame_dsd_bytes;
    }

    *pcm_samples = total_pcm_samples;
    return DSDPCM_OK;
#endif
}

extern "C" int dsdpcm_convert_fp64(dsdpcm_decoder_s *decoder,
                                   const uint8_t *dsd_data,
                                   size_t dsd_size,
                                   dsdpcm_sample64_t *pcm_data,
                                   size_t *pcm_samples)
{
    if (!decoder || !dsd_data || !pcm_data || !pcm_samples) {
        return DSDPCM_ERR_NULL_POINTER;
    }

    if (!decoder->impl || !decoder->initialized) {
        return DSDPCM_ERR_NOT_INITIALIZED;
    }

    // Check precision
    if (decoder->precision != DSDPCM_PRECISION_FP64) {
        return DSDPCM_ERR_PRECISION_MISMATCH;
    }

    // The original C++ engine only handles single-frame inputs.
    // Process frame-by-frame.
    size_t frame_dsd_bytes = (decoder->dsd_samplerate / 8 / decoder->framerate) * decoder->channels;

#if defined(_M_X64) || defined(_M_ARM64) || defined(__x86_64__) || defined(__aarch64__) || defined(__LP64__)
    // On 64-bit, audio_sample is double - process frame by frame directly
    size_t total_pcm_samples = 0;
    size_t offset = 0;

    while (offset + frame_dsd_bytes <= dsd_size) {
        size_t samples_out = decoder->impl->convert(
            dsd_data + offset,
            frame_dsd_bytes,
            pcm_data + total_pcm_samples
        );
        total_pcm_samples += samples_out;
        offset += frame_dsd_bytes;
    }

    *pcm_samples = total_pcm_samples;
    return DSDPCM_OK;
#else
    // On 32-bit, audio_sample is float, but we want double output.
    // This means the decoder was initialized with conv_fp64=true, which
    // may not work correctly with the engine. For safety, return an error.
    return DSDPCM_ERR_PRECISION_MISMATCH;
#endif
}

/* ==========================================================================
 * FIR Coefficient Management
 * ========================================================================== */

extern "C" dsdpcm_fir_t *dsdpcm_fir_create(void)
{
    dsdpcm_fir_t *fir = static_cast<dsdpcm_fir_t*>(calloc(1, sizeof(dsdpcm_fir_t)));
    if (fir) {
        fir->decimation = DSDPCM_DECIMATION_AUTO;
    }
    return fir;
}

extern "C" void dsdpcm_fir_destroy(dsdpcm_fir_t *fir)
{
    if (!fir) {
        return;
    }

    free(fir->coefficients);
    free(fir->name);
    free(fir);
}

extern "C" int dsdpcm_fir_set_coefficients(dsdpcm_fir_t *fir,
                                           const double *coefficients,
                                           size_t count,
                                           dsdpcm_decimation_t decimation)
{
    if (!fir) {
        return DSDPCM_ERR_NULL_POINTER;
    }

    if (!coefficients || count == 0) {
        return DSDPCM_ERR_INVALID_PARAM;
    }

    if (count > DSDPCM_FIR_MAX_COEFFICIENTS) {
        return DSDPCM_ERR_INVALID_PARAM;
    }

    // Allocate new buffer
    double *new_coefs = static_cast<double*>(malloc(count * sizeof(double)));
    if (!new_coefs) {
        return DSDPCM_ERR_ALLOC_FAILED;
    }

    // Copy coefficients
    memcpy(new_coefs, coefficients, count * sizeof(double));

    // Free old coefficients
    free(fir->coefficients);

    // Update structure
    fir->coefficients = new_coefs;
    fir->count = count;
    fir->decimation = decimation;

    return DSDPCM_OK;
}

extern "C" int dsdpcm_fir_get_coefficients(const dsdpcm_fir_t *fir,
                                           const double **coefficients,
                                           size_t *count)
{
    if (!fir || !coefficients || !count) {
        return DSDPCM_ERR_NULL_POINTER;
    }

    *coefficients = fir->coefficients;
    *count = fir->count;
    return DSDPCM_OK;
}

extern "C" int dsdpcm_fir_set_name(dsdpcm_fir_t *fir, const char *name)
{
    if (!fir) {
        return DSDPCM_ERR_NULL_POINTER;
    }

    // Free old name
    free(fir->name);
    fir->name = nullptr;

    if (name) {
        size_t len = strlen(name);
        if (len > DSDPCM_FIR_MAX_NAME_LENGTH) {
            len = DSDPCM_FIR_MAX_NAME_LENGTH;
        }

        fir->name = static_cast<char*>(malloc(len + 1));
        if (!fir->name) {
            return DSDPCM_ERR_ALLOC_FAILED;
        }

        memcpy(fir->name, name, len);
        fir->name[len] = '\0';
    }

    return DSDPCM_OK;
}

extern "C" const char *dsdpcm_fir_get_name(const dsdpcm_fir_t *fir)
{
    if (!fir) {
        return nullptr;
    }
    return fir->name;
}

extern "C" int dsdpcm_fir_set_decimation(dsdpcm_fir_t *fir, dsdpcm_decimation_t decimation)
{
    if (!fir) {
        return DSDPCM_ERR_NULL_POINTER;
    }

    fir->decimation = decimation;
    return DSDPCM_OK;
}

extern "C" dsdpcm_decimation_t dsdpcm_fir_get_decimation(const dsdpcm_fir_t *fir)
{
    if (!fir) {
        return DSDPCM_DECIMATION_AUTO;
    }
    return fir->decimation;
}

/* ==========================================================================
 * Utility Functions
 * ========================================================================== */

extern "C" const char *dsdpcm_error_string(int error)
{
    switch (error) {
        case DSDPCM_OK:                    return "Success";
        case DSDPCM_ERR_NULL_POINTER:      return "Null pointer argument";
        case DSDPCM_ERR_INVALID_PARAM:     return "Invalid parameter value";
        case DSDPCM_ERR_ALLOC_FAILED:      return "Memory allocation failed";
        case DSDPCM_ERR_NOT_INITIALIZED:   return "Decoder not initialized";
        case DSDPCM_ERR_UNSUPPORTED:       return "Unsupported operation";
        case DSDPCM_ERR_FIR_REQUIRED:      return "FIR data required for USER mode";
        case DSDPCM_ERR_PRECISION_MISMATCH: return "Precision mismatch";
        case DSDPCM_ERR_FILE_OPEN:         return "File open failed";
        case DSDPCM_ERR_FILE_READ:         return "File read failed";
        case DSDPCM_ERR_FILE_WRITE:        return "File write failed";
        case DSDPCM_ERR_FILE_FORMAT:       return "Invalid file format";
        case DSDPCM_ERR_BUFFER_TOO_SMALL:  return "Output buffer too small";
        default:                           return "Unknown error";
    }
}

extern "C" const char *dsdpcm_version_string(void)
{
    static const char version[] = "1.0.0";
    return version;
}

extern "C" size_t dsdpcm_calc_pcm_buffer_size(size_t channels,
                                              size_t framerate,
                                              size_t dsd_samplerate,
                                              size_t pcm_samplerate,
                                              size_t dsd_bytes)
{
    if (channels == 0 || framerate == 0 || dsd_samplerate == 0 ||
        pcm_samplerate == 0 || dsd_bytes == 0) {
        return 0;
    }

    // DSD samples per byte = 8 (1 bit per sample)
    // DSD samples total = dsd_bytes * 8 / channels
    size_t dsd_samples_per_channel = (dsd_bytes * 8) / channels;

    // PCM samples = DSD samples * (pcm_samplerate / dsd_samplerate)
    // Add some margin for filter delay
    size_t pcm_samples_per_channel = (dsd_samples_per_channel * pcm_samplerate) / dsd_samplerate;
    pcm_samples_per_channel += 1024; // Margin for filter delay

    // Total samples (all channels interleaved)
    return pcm_samples_per_channel * channels;
}

extern "C" int dsdpcm_decimation_is_valid(dsdpcm_decimation_t decimation)
{
    switch (decimation) {
        case DSDPCM_DECIMATION_AUTO:
        case DSDPCM_DECIMATION_8:
        case DSDPCM_DECIMATION_16:
        case DSDPCM_DECIMATION_32:
        case DSDPCM_DECIMATION_64:
        case DSDPCM_DECIMATION_128:
        case DSDPCM_DECIMATION_256:
        case DSDPCM_DECIMATION_512:
        case DSDPCM_DECIMATION_1024:
            return 1;
        default:
            return 0;
    }
}

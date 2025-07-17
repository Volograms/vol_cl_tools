// basis_encoder_wrapper.h
// Header file for BASIS Universal encoder C wrapper functions

#ifndef BASIS_ENCODER_WRAPPER_H
#define BASIS_ENCODER_WRAPPER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize BASIS Universal encoder with OpenCL support
 * Must be called before using any other encoder functions
 * 
 * @param use_opencl True to enable OpenCL acceleration, false for CPU only
 * @return True on success, false on error
 */
bool basis_encoder_init_wrapper(bool use_opencl);

/**
 * Check if OpenCL is available and working
 * 
 * @return True if OpenCL is available, false otherwise
 */
bool basis_encoder_opencl_available(void);

/**
 * Encode RGBA texture data to BASIS format with optional resizing
 * 
 * @param rgba_data    Input RGBA texture data (4 bytes per pixel)
 * @param src_width    Source texture width in pixels
 * @param src_height   Source texture height in pixels
 * @param dst_width    Destination texture width in pixels (0 = no resize)
 * @param dst_height   Destination texture height in pixels (0 = no resize)
 * @param use_uastc    True for UASTC format, false for ETC1S format
 * @param use_opencl   True to use OpenCL acceleration (if available), false for CPU only
 * @param output_data  Pointer to store output BASIS data (caller must free with free())
 * @param output_size  Pointer to store output BASIS data size in bytes
 * @return             True on success, false on error
 */
bool basis_encode_texture_with_resize(const uint8_t* rgba_data, int src_width, int src_height,
                                      int dst_width, int dst_height, bool use_uastc, bool use_opencl,
                                      uint8_t** output_data, uint32_t* output_size);

#ifdef __cplusplus
}
#endif

#endif // BASIS_ENCODER_WRAPPER_H 
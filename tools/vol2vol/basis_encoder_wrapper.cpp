// basis_encoder_wrapper.cpp
// C++ wrapper for BASIS Universal encoder to be called from C code

#include "basis_universal/encoder/basisu_enc.h"
#include "basis_universal/encoder/basisu_comp.h"
#include <cstring>
#include <cstdlib>

extern "C" {

/**
 * Initialize BASIS Universal encoder
 */
bool basis_encoder_init_wrapper(void) {
    try {
        basisu::basisu_encoder_init();
        return true;
    } catch (...) {
        return false;
    }
}

/**
 * Encode RGBA texture data to BASIS format with resizing
 * 
 * @param rgba_data    Input RGBA texture data (4 bytes per pixel)
 * @param src_width    Source texture width
 * @param src_height   Source texture height  
 * @param dst_width    Destination texture width (0 = no resize)
 * @param dst_height   Destination texture height (0 = no resize)
 * @param use_uastc    True for UASTC format, false for ETC1S
 * @param output_data  Pointer to store output BASIS data (caller must free)
 * @param output_size  Pointer to store output BASIS data size
 * @return             True on success, false on error
 */
bool basis_encode_texture_with_resize(const uint8_t* rgba_data, int src_width, int src_height,
                                      int dst_width, int dst_height, bool use_uastc,
                                      uint8_t** output_data, uint32_t* output_size) {
    if (!rgba_data || !output_data || !output_size || src_width <= 0 || src_height <= 0) {
        return false;
    }
    
    *output_data = nullptr;
    *output_size = 0;
    
    try {
        // Create BASIS image from RGBA data
        basisu::image source_image(rgba_data, src_width, src_height, 4);
        
        // Set up compression parameters
        basisu::basis_compressor_params params;
        params.m_source_images.push_back(source_image);
        params.m_status_output = false;  // Disable status output to avoid clutter
        
        // Configure resizing if requested
        if (dst_width > 0 && dst_height > 0) {
            params.m_resample_width = dst_width;
            params.m_resample_height = dst_height;
        }
        
        // Set compression format
        params.m_uastc = use_uastc;
        
        // Set quality for better results
        if (use_uastc) {
            params.m_pack_uastc_flags = basisu::cPackUASTCLevelDefault;
        } else {
            params.m_quality_level = 128; // Mid-range quality for ETC1S
        }
        
        // Create job pool for multithreading (required by BASIS Universal)
        basisu::job_pool jpool(4); // Use 4 threads for reasonable performance
        params.m_pJob_pool = &jpool;
        
        // Create and initialize compressor
        basisu::basis_compressor compressor;
        if (!compressor.init(params)) {
            return false;
        }
        
        // Compress the texture
        basisu::basis_compressor::error_code result = compressor.process();
        if (result != basisu::basis_compressor::cECSuccess) {
            return false;
        }
        
        // Get the compressed output
        const basisu::uint8_vec& output_basis_file = compressor.get_output_basis_file();
        if (output_basis_file.empty()) {
            return false;
        }
        
        // Allocate output buffer and copy data
        *output_size = (uint32_t)output_basis_file.size();
        *output_data = (uint8_t*)malloc(*output_size);
        if (!*output_data) {
            return false;
        }
        
        memcpy(*output_data, output_basis_file.data(), *output_size);
        return true;
        
    } catch (...) {
        if (*output_data) {
            free(*output_data);
            *output_data = nullptr;
        }
        *output_size = 0;
        return false;
    }
}

} // extern "C" 
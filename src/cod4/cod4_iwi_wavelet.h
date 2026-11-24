#ifndef COD4_IWI_WAVELET_H
#define COD4_IWI_WAVELET_H
/* Format 6..10 IWI -> top-origin 32-bit TGA. Caller frees output.
 * Returns zero and clears outputs for malformed/unsupported input. */
int cod4_iwi_wavelet_to_tga(const unsigned char* data,unsigned size,
    unsigned char** out,unsigned* out_size);
#endif

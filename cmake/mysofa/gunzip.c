/* Replaces libmysofa's src/hdf/gunzip.c: inflates an HDF5 chunk (a zlib
   stream) with miniz's tinfl instead of zlib, which would clash with the
   zlib inside JUCE. Succeeds only when the whole stream fits in `out`; the
   caller checks that `*outlen` is the size it expected. */

#include "reader.h"

#include <miniz_tinfl.h>

int gunzip(int inlen, char *in, int *outlen, char *out) {
  size_t written;

  if (inlen < 0 || *outlen < 0)
    return MYSOFA_INVALID_FORMAT;

  written = tinfl_decompress_mem_to_mem(out, (size_t)*outlen, in, (size_t)inlen, TINFL_FLAG_PARSE_ZLIB_HEADER);

  if (written == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED)
    return MYSOFA_INVALID_FORMAT;

  *outlen = (int)written;
  return MYSOFA_OK;
}

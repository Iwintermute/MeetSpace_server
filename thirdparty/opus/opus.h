#ifndef OPUS_H
#define OPUS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int16_t opus_int16;
typedef int32_t opus_int32;

typedef struct OpusEncoder OpusEncoder;
typedef struct OpusDecoder OpusDecoder;

#define OPUS_OK 0
#define OPUS_APPLICATION_VOIP 2048

#define OPUS_SET_BITRATE_REQUEST 4002
#define OPUS_SET_COMPLEXITY_REQUEST 4010

#define OPUS_SET_BITRATE(x) OPUS_SET_BITRATE_REQUEST, (x)
#define OPUS_SET_COMPLEXITY(x) OPUS_SET_COMPLEXITY_REQUEST, (x)

OpusEncoder *opus_encoder_create(opus_int32 Fs, int channels, int application, int *error);
void opus_encoder_destroy(OpusEncoder *encoder);
opus_int32 opus_encode(OpusEncoder *st, const opus_int16 *pcm, int frame_size, unsigned char *data, opus_int32 max_data_bytes);
int opus_encoder_ctl(OpusEncoder *st, int request, ...);

OpusDecoder *opus_decoder_create(opus_int32 Fs, int channels, int *error);
void opus_decoder_destroy(OpusDecoder *decoder);
int opus_decode(OpusDecoder *st, const unsigned char *data, opus_int32 len, opus_int16 *pcm, int frame_size, int decode_fec);
int opus_decoder_ctl(OpusDecoder *st, int request, ...);

const char *opus_strerror(int error);

#ifdef __cplusplus
}
#endif

#endif // OPUS_H

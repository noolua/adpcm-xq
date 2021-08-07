#ifndef __decoder_h__
#define __decoder_h__
#include <stdint.h>

enum{
  ADPCM_ERR_CONTINUE = 1,
  ADPCM_ERR_OK = 0,
  ADPCM_ERR_UNKNOWN = -1,
  ADPCM_ERR_ARGS  = -2,
  ADPCM_ERR_INVALID_FILE = -3,
  ADPCM_ERR_NO_SAMPLES = -4,
  ADPCM_ERR_ALLOC_MEMORY = -5,
  ADPCM_ERR_DECODE_BLOCK = -6
};

typedef struct pcm_block_s {
  int16_t *samples;
  int32_t num_samples, num_channels, sample_rate;
}pcm_block_t;

typedef struct adpcm_progm_s{
  const uint32_t content_size;
  uint8_t content[0];
}adpcm_progm_t;

typedef struct adpcm_decoder_s adpcm_decoder_t;


/*
  success return adpcm_decoder_t* pointer, error return NULL.
*/
adpcm_decoder_t *decoder_create();

/*
  return ADPCM_ERR_OK mean source in valid, other mean a error.
*/
int decoder_init(adpcm_decoder_t *decoder, adpcm_progm_t *source);

/*
return ADPCM_ERR_OK mean decode complete, ADPCM_ERR_CONTIUNE mean have next block.
pass pcm_block_t to hold decode pcm.
*/
int decoder_next_block(adpcm_decoder_t *decoder, pcm_block_t *block);

/*
  destory decoder, return ADPCM_ERR_OK
*/
int decoder_destroy(adpcm_decoder_t *decoder);



#endif // __decoder_h__

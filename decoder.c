#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "adpcm-lib.h"
#include "decoder.h"

#define ADPCM_FLAG_NOISE_SHAPING    0x1
#define ADPCM_FLAG_RAW_OUTPUT       0x2

typedef struct {
    char ckID [4];
    uint32_t ckSize;
    char formType [4];
} RiffChunkHeader;

typedef struct {
    char ckID [4];
    uint32_t ckSize;
} ChunkHeader;

#define ChunkHeaderFormat "4L"

typedef struct {
    uint16_t FormatTag, NumChannels;
    uint32_t SampleRate, BytesPerSecond;
    uint16_t BlockAlign, BitsPerSample;
    uint16_t cbSize;
    union {
        uint16_t ValidBitsPerSample;
        uint16_t SamplesPerBlock;
        uint16_t Reserved;
    } Samples;
    int32_t ChannelMask;
    uint16_t SubFormat;
    char GUID [14];
} WaveHeader;

#define WaveHeaderFormat "SSLLSSSSLS"

#define WAVE_FORMAT_PCM         0x1
#define WAVE_FORMAT_IMA_ADPCM   0x11
#define WAVE_FORMAT_EXTENSIBLE  0xfffe

#ifdef __DBG_MALLOC__
static void *malloc_p(size_t sz){
    void *p = malloc(sz);
    fprintf(stderr, "malloc_p: %p, size: %zu\n", p, sz);
    return p;
}

static void free_p(void *p){
    fprintf(stderr, "free_p: %p\n", p);
    free(p);
}
#else

#define malloc_p malloc
#define free_p free

#endif

typedef struct adpcm_decoder_s{
    size_t num_samples, sample_cousume;
    int num_channels, block_size, samples_per_block, sample_rate;
    uint32_t source_cosume;
    uint8_t *adpcm_block;
    int16_t *pcm_block;
    adpcm_progm_t *source;
}adpcm_decoder_t;

static void* decoder_source_map2type(adpcm_decoder_t *decoder, uint32_t type_sz){
    uint8_t *hold = decoder->source->content + decoder->source_cosume;
    decoder->source_cosume += type_sz;
    return hold;
}

static int decoder_source_read2type(adpcm_decoder_t *decoder, void *ty_buffer, uint32_t type_sz){
    void *hold = decoder->source->content + decoder->source_cosume;
    memcpy(ty_buffer, hold, type_sz);
    decoder->source_cosume += type_sz;
    return (int)type_sz;
}

static void little_endian_to_native (void *data, char *format) {
    unsigned char *cp = (unsigned char *) data;
    int32_t temp;

    while (*format) {
        switch (*format) {
            case 'L':
                temp = cp [0] + ((int32_t) cp [1] << 8) + ((int32_t) cp [2] << 16) + ((int32_t) cp [3] << 24);
                * (int32_t *) cp = temp;
                cp += 4;
                break;

            case 'S':
                temp = cp [0] + (cp [1] << 8);
                * (short *) cp = (short) temp;
                cp += 2;
                break;

            default:
                if (isdigit ((unsigned char) *format))
                    cp += *format - '0';

                break;
        }

        format++;
    }
}


/*
    PUBLIC API IMPL
*/

adpcm_decoder_t *decoder_create(){
    adpcm_decoder_t *decoder = malloc_p(sizeof(adpcm_decoder_t));
    return decoder;
}

// return ADPCM_ERR_XXX
int decoder_init(adpcm_decoder_t *decoder, adpcm_progm_t *source){
    int format = 0, bits_per_sample, sample_rate, num_channels, samples_per_block;
    uint32_t fact_samples = 0;
    size_t num_samples = 0;
    RiffChunkHeader *riff_chunk_header;
    ChunkHeader chunk_header;
    WaveHeader wave_header;

    if(!decoder || !source) return ADPCM_ERR_ARGS;
    memset(decoder, 0, sizeof(adpcm_progm_t));
    decoder->source = source;

    // PARSE HEADER
    riff_chunk_header = (RiffChunkHeader*)decoder_source_map2type(decoder, sizeof(RiffChunkHeader));
    if(strncmp(riff_chunk_header->ckID, "RIFF", 4) || strncmp(riff_chunk_header->formType, "WAVE", 4))
        return ADPCM_ERR_INVALID_FILE;

    // read initial RIFF form header
    // loop through all elements of the RIFF wav header (until the data chuck)
    while (1) {
        decoder_source_read2type(decoder, &chunk_header, sizeof(ChunkHeader));
        little_endian_to_native (&chunk_header, ChunkHeaderFormat);
        // if it's the format chunk, we want to get some info out of there and
        // make sure it's a .wav file we can handle
        if (!strncmp (chunk_header.ckID, "fmt ", 4)) {
            int supported = 1;

            if (chunk_header.ckSize < 16 || chunk_header.ckSize > sizeof (WaveHeader) ||
                !decoder_source_read2type(decoder, &wave_header, chunk_header.ckSize)) {
                return ADPCM_ERR_INVALID_FILE;
            }
            little_endian_to_native (&wave_header, WaveHeaderFormat);

            format = (wave_header.FormatTag == WAVE_FORMAT_EXTENSIBLE && chunk_header.ckSize == 40) ? wave_header.SubFormat : wave_header.FormatTag;

            bits_per_sample = (chunk_header.ckSize == 40 && wave_header.Samples.ValidBitsPerSample) ? wave_header.Samples.ValidBitsPerSample : wave_header.BitsPerSample;

            if (wave_header.NumChannels < 1 || wave_header.NumChannels > 2)
                supported = 0;
            else if (format == WAVE_FORMAT_PCM) {
                supported = 0;
            }
            else if (format == WAVE_FORMAT_IMA_ADPCM) {
                if (bits_per_sample != 4)
                    supported = 0;
                if (wave_header.Samples.SamplesPerBlock != (wave_header.BlockAlign - wave_header.NumChannels * 4) * (wave_header.NumChannels ^ 3) + 1) {
                    return ADPCM_ERR_INVALID_FILE;
                }
            }
            else
                supported = 0;

            if (!supported) {
                return ADPCM_ERR_INVALID_FILE;
            }
        }
        else if (!strncmp (chunk_header.ckID, "fact", 4)) {

            if (chunk_header.ckSize < 4 || !decoder_source_read2type (decoder, &fact_samples, sizeof (fact_samples))) {
                return ADPCM_ERR_INVALID_FILE;
            }

            if (chunk_header.ckSize > 4) {
                int bytes_to_skip = chunk_header.ckSize - 4;
                decoder_source_map2type(decoder, bytes_to_skip);
            }
        }
        else if (!strncmp (chunk_header.ckID, "data", 4)) {

            // on the data chunk, get size and exit parsing loop

            if (!wave_header.NumChannels) {      // make sure we saw a "fmt" chunk...
                return ADPCM_ERR_INVALID_FILE;
            }

            if (!chunk_header.ckSize) {
                return ADPCM_ERR_NO_SAMPLES;
            }

            if (format == WAVE_FORMAT_PCM) {
                return ADPCM_ERR_INVALID_FILE;
            } else {
                int complete_blocks = chunk_header.ckSize / wave_header.BlockAlign;
                int leftover_bytes = chunk_header.ckSize % wave_header.BlockAlign;
                int samples_last_block;

                num_samples = complete_blocks * wave_header.Samples.SamplesPerBlock;

                if (leftover_bytes) {
                    if (leftover_bytes % (wave_header.NumChannels * 4)) {
                        return ADPCM_ERR_INVALID_FILE;
                    }
                    samples_last_block = (leftover_bytes - (wave_header.NumChannels * 4)) * (wave_header.NumChannels ^ 3) + 1;
                    num_samples += samples_last_block;
                }
                else
                    samples_last_block = wave_header.Samples.SamplesPerBlock;

                if (fact_samples) {
                    if (fact_samples < num_samples && fact_samples > num_samples - samples_last_block) {
                        num_samples = fact_samples;
                    }
                    else if (wave_header.NumChannels == 2 && (fact_samples >>= 1) < num_samples && fact_samples > num_samples - samples_last_block) {
                        num_samples = fact_samples;
                    }
                }
            }

            if (!num_samples) {
                return ADPCM_ERR_NO_SAMPLES;
            }

            num_channels = wave_header.NumChannels;
            sample_rate = wave_header.SampleRate;
            break;
        }
        else {          // just ignore unknown chunks
            int bytes_to_eat = (chunk_header.ckSize + 1) & ~1L;
            decoder_source_map2type(decoder, bytes_to_eat);
        }
    }

    if(format != WAVE_FORMAT_IMA_ADPCM)
        return ADPCM_ERR_INVALID_FILE;


    samples_per_block = (wave_header.BlockAlign - num_channels * 4) * (num_channels ^ 3) + 1;
    void *pcm_block = malloc_p(samples_per_block * num_channels * 2);
    if(!pcm_block)
        return ADPCM_ERR_ALLOC_MEMORY;
    void *adpcm_block = malloc_p(wave_header.BlockAlign);
    if(!adpcm_block){
        if(pcm_block)
            free(pcm_block);
        return ADPCM_ERR_ALLOC_MEMORY;
    }

    decoder->samples_per_block = samples_per_block;
    decoder->num_channels = num_channels;
    decoder->num_samples = num_samples;
    decoder->sample_rate = sample_rate;
    decoder->block_size = wave_header.BlockAlign;
    decoder->pcm_block = pcm_block;
    decoder->adpcm_block = adpcm_block;
    return ADPCM_ERR_OK;
}


int decoder_next_block(adpcm_decoder_t *decoder, pcm_block_t *block){
    int samples_per_block, num_samples;
    if(!decoder || !block)
        return ADPCM_ERR_ARGS;
    samples_per_block = decoder->samples_per_block;
    num_samples = decoder->num_samples - decoder->sample_cousume;

    if(num_samples) {
        int this_block_adpcm_samples = samples_per_block;
        int this_block_pcm_samples = samples_per_block;

        if (this_block_adpcm_samples > num_samples) {
            this_block_adpcm_samples = ((num_samples + 6) & ~7) + 1;
            decoder->block_size = (this_block_adpcm_samples - 1) / (decoder->num_channels ^ 3) + (decoder->num_channels * 4);
            this_block_pcm_samples = num_samples;
        }
        // decoder->adpcm_block = decoder_source_map2type(decoder, decoder->block_size);
        decoder_source_read2type(decoder, decoder->adpcm_block, decoder->block_size);

        if (adpcm_decode_block (decoder->pcm_block, decoder->adpcm_block, decoder->block_size, decoder-> num_channels) != this_block_adpcm_samples) {
            return ADPCM_ERR_DECODE_BLOCK;
        }
        decoder->sample_cousume += this_block_pcm_samples;

        block->samples = decoder->pcm_block;
        block->num_channels = decoder->num_channels;
        block->num_samples = this_block_pcm_samples;
        block->sample_rate = decoder->sample_rate;
        if(decoder->num_samples > decoder->sample_cousume)
            return ADPCM_ERR_CONTINUE;
    }

    return ADPCM_ERR_OK;
}

int decoder_destroy(adpcm_decoder_t *decoder){
    if(!decoder) return ADPCM_ERR_ARGS;
    if(decoder->pcm_block) {
        free_p(decoder->pcm_block);
        decoder->pcm_block = NULL;
    }
    if(decoder->adpcm_block){
        free_p(decoder->adpcm_block);
        decoder->adpcm_block = NULL;
    }
    free_p(decoder);
    return ADPCM_ERR_OK;
}

#ifdef __TEST_DECODER__
// gcc -O2 -D__DBG_MALLOC__ -D__TEST_DECODER__ adpcm-lib.c decoder.c -o decoder
#include "data/one.h"
int main () {
    int ret;
    adpcm_progm_t *source = (adpcm_progm_t *)adpcm_one;
    pcm_block_t block;
    adpcm_decoder_t *decoder = decoder_create();
    if(!decoder){
        fprintf(stderr, "decoder_create fail\n");
        return ADPCM_ERR_ALLOC_MEMORY;
    }
    ret = decoder_init(decoder, source);
    if(ret != ADPCM_ERR_OK){
        fprintf(stderr, "decoder_init error: %d\n", ret);
        return ret;
    }
    while((ret = decoder_next_block(decoder, &block)) >= ADPCM_ERR_OK){
        fprintf(stderr, "block -> rate: %d,  samples: %d, num_channels: %d\n", block.sample_rate, block.num_samples, block.num_channels);
        if(ret == ADPCM_ERR_OK){
            fprintf(stderr, "decoder_next_block done\n");
            break;
        }else{
            fprintf(stderr, "decoder_next_block continue\n");
        }
    }
    ret = decoder_destroy(decoder);
    if(ret != ADPCM_ERR_OK){
        fprintf(stderr, "decoder_destroy error: %d\n", ret);
        return ret;
    }
    return ret;
}
#endif // __TEST_DECODER__

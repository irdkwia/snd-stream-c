/**
 * @file snd_stream_lib.c
 * @author Irdkwia & Adakite
 * @brief SND Stream Library C Edition
 * @details Port of SND Stream Library to C
 * @version 0.8.6
 * @date 2026-02-22
 */
#include <pmdsky.h>
#include <cot.h>

#define CLOCK_RATE 0xFFB0FF
#define CHANNEL_RANGE_START 14

#define ADPCM_BUFFER_SIZE 0x2000
#define PCM_PLAYBACK_BUFFER_SIZE ADPCM_BUFFER_SIZE*4
#define BUFFER_SIZE ADPCM_BUFFER_SIZE*2+PCM_PLAYBACK_BUFFER_SIZE*2 // Two ADPCM buffers, two PCM playback buffers.
#define LAG_BUFFER ADPCM_BUFFER_SIZE

extern int HookCheckOverlayArm9;
extern struct mem_arena* SoundMemoryArenaPtr;
extern uint16_t ChannelsStruct[];

void SetChannelVolume(int channel, int volume, int unk);
void SetChannelGlobal(int channel_id, int stype, int16_t* snd_addr, int repeat, int pnt, int len, int volume, int unk, int tmr, int pan);

extern uint32_t TMR2[];

int8_t ADPCM_INDEX_TABLE[] = {
    -1,-1,-1,-1,2,4,6,8,
    -1,-1,-1,-1,2,4,6,8
};
uint16_t ADPCM_TABLE[] = {
    0x0007,0x0008,0x0009,0x000A,0x000B,0x000C,0x000D,0x000E,
    0x0010,0x0011,0x0013,0x0015,0x0017,0x0019,0x001C,0x001F,
    0x0022,0x0025,0x0029,0x002D,0x0032,0x0037,0x003C,0x0042,
    0x0049,0x0050,0x0058,0x0061,0x006B,0x0076,0x0082,0x008F,
    0x009D,0x00AD,0x00BE,0x00D1,0x00E6,0x00FD,0x0117,0x0133,
    0x0151,0x0173,0x0198,0x01C1,0x01EE,0x0220,0x0256,0x0292,
    0x02D4,0x031C,0x036C,0x03C3,0x0424,0x048E,0x0502,0x0583,
    0x0610,0x06AB,0x0756,0x0812,0x08E0,0x09C3,0x0ABD,0x0BD0,
    0x0CFF,0x0E4C,0x0FBA,0x114C,0x1307,0x14EE,0x1706,0x1954,
    0x1BDC,0x1EA5,0x21B6,0x2515,0x28CA,0x2CDF,0x315B,0x364B,
    0x3BB9,0x41B2,0x4844,0x4F7E,0x5771,0x602F,0x69CE,0x7462,
    0x7FFF
};

struct adpcm_decoder {
    uint8_t* ptr_adpcm;
    int32_t adpcm_buf_pos;
    int32_t adpcm_buf_len;
    int16_t* ptr_pcm;
    int32_t pcm_buf_pos;
    int32_t pcm_buf_len;
    int32_t global_sample_i;
    int16_t cur_adpcm_predictor;
    uint8_t cur_adpcm_index;
    int8_t cur_nibble;
    int32_t loop_start;
    int16_t loop_adpcm_predictor;
    uint8_t loop_adpcm_index;
};

struct wave_file_streamer {
    struct file_stream fstream;
    struct adpcm_decoder adpcm_decoder;
    int32_t data_start;
    int32_t data_end;
    int32_t smplrate;
    int32_t adpcm_block_size;
    int32_t loop_start;
    int32_t cursor_pos;
    int32_t max_read_len;
    int8_t loop_skip_first_nibble;
    int8_t ofstream;
    int8_t is_looped;
    int8_t just_looped;
};

struct wave_player {
    struct wave_file_streamer wave_stream_left;
    struct wave_file_streamer wave_stream_right;
    void* snd_addr;
    int32_t timer;
    int32_t volume;
    int32_t fade_to;
    int32_t fade_time;
    int32_t fade_play;
    int32_t old_timer;
    int32_t bgm_id;
    int8_t channel_start;
    int8_t playing;
};

struct wave_player player[1];

// ******************** Timer Calculation **********************
int GetTimer(int smplrate) {
    long long ret = _s32_div_f(CLOCK_RATE, smplrate);
    int res = ret;
    int rem = (ret >> 32);
    if (rem >= (smplrate>>1)) ++res;
    return res;
}
// *************************************************************

// ********************* Buffer Handling ***********************
void AllocBuffer(struct wave_player* p) {
    if (p->snd_addr == NULL) {
        p->snd_addr = MemLocateSet(SoundMemoryArenaPtr, BUFFER_SIZE, 2);
    }
}

void FreeBuffer(struct wave_player* p) {
    if (p->snd_addr != NULL) {
        MemLocateUnset(SoundMemoryArenaPtr, p->snd_addr);
        p->snd_addr = NULL;
    }
}
// *************************************************************

// ********************* Channel Setting ***********************
void SetChannelVolumeQuick(struct wave_player* p) {
    for (int channel_id=p->channel_start; channel_id<(p->channel_start+2);++channel_id) {
        SetChannelVolume(1<<channel_id, p->volume, 0);
        ChannelsStruct[26] &= ~(1<<channel_id);
    }
}
void SetChannelQuick(struct wave_player* p) {
    for (int channel_id=p->channel_start; channel_id<(p->channel_start+2);++channel_id) {
        ChannelsStruct[25] |= (1<<channel_id);
        ChannelsStruct[27] &= ~(1<<channel_id);
        struct wave_file_streamer* wfs = (channel_id==p->channel_start)?&(p->wave_stream_right):&(p->wave_stream_left);
        int snd_pan = (channel_id==p->channel_start)?0x7F:0;
        SetChannelGlobal(channel_id, 1, wfs->adpcm_decoder.ptr_pcm, 1, 0, wfs->adpcm_decoder.pcm_buf_len>>2, p->volume, 0, GetTimer(wfs->smplrate), snd_pan);
    }
}
void ResetChannelQuick(struct wave_player* p) {
    for (int channel_id=p->channel_start; channel_id<(p->channel_start+2);++channel_id) {
        ChannelsStruct[25] &= ~(1<<channel_id);
        ChannelsStruct[26] |= (1<<channel_id);
        ChannelsStruct[27] |= (1<<channel_id);
    }
}
// *************************************************************

// ***************** Wav File Initialization *******************
int ReadWAVChunk(struct wave_file_streamer* wfs, int* current_pos, int eof) {
    uint32_t cur_chunk_info[2];
    FileSeek(&(wfs->fstream), *current_pos, 0);
    FileRead(&(wfs->fstream), cur_chunk_info, 8);
    switch (cur_chunk_info[0]) {
    case 0x20746D66: // fmt
        FileSeek(&(wfs->fstream), 4, 1);
        FileRead(&(wfs->fstream), &(wfs->smplrate), 4);
        FileSeek(&(wfs->fstream), 4, 1);
        wfs->adpcm_block_size = 0;
        FileRead(&(wfs->fstream), &(wfs->adpcm_block_size), 2);
        break;
    case 0x6C706D73: // smpl
        FileSeek(&(wfs->fstream), 0x1C, 1);
        FileRead(&(wfs->fstream), cur_chunk_info, 4);
        if (cur_chunk_info[0]>0) {
            FileSeek(&(wfs->fstream), 12, 1);
            FileRead(&(wfs->fstream), &(wfs->loop_start), 4);
            wfs->is_looped = 1;
        }
        break;
    case 0x61746164: // data
        wfs->data_start = *current_pos + 8;
        wfs->data_end = wfs->data_start + cur_chunk_info[1];
        break;
    }
    *current_pos += 8 + cur_chunk_info[1];
    return (*current_pos < eof) ? 1 : 0;
}

void CloseWaveFileStream(struct wave_file_streamer* wfs) {
    if (!wfs->ofstream) return;
    wfs->ofstream = 0;
    FileClose(&(wfs->fstream));
}
void OpenWaveFileStream(struct wave_file_streamer* wfs, char* filename) {
    CloseWaveFileStream(wfs);
    FileInitVeneer(&(wfs->fstream));
    FileOpen(&(wfs->fstream), filename);
    FileSeek(&(wfs->fstream), 4, 0);
	int eof;
    FileRead(&(wfs->fstream), &eof, 4);
	int current_pos = 0xc;
    wfs->ofstream = 1;
    wfs->is_looped = 0;
	wfs->just_looped = 0;
	wfs->loop_skip_first_nibble = 0;
	wfs->loop_start = 0;
    while (ReadWAVChunk(wfs, &current_pos, eof + 8));
    wfs->cursor_pos = 0;
}
// *************************************************************

// ********************** ADPCM Decoding ***********************
void InitAdpcmDecoder(struct adpcm_decoder* dec, uint8_t* ptr_adpcm, int adpcm_buf_len, int16_t* ptr_pcm, int pcm_buf_len) {
	dec->ptr_adpcm = ptr_adpcm;
    dec->global_sample_i = 0;
    dec->adpcm_buf_pos = adpcm_buf_len;
    dec->adpcm_buf_len = adpcm_buf_len;
    dec->cur_adpcm_predictor = 0;
    dec->cur_adpcm_index = 0;
    dec->cur_nibble = 0;
    dec->ptr_pcm = ptr_pcm;
    dec->pcm_buf_pos = 0;
    dec->pcm_buf_len = pcm_buf_len;
    dec->loop_start = 0;
    dec->loop_adpcm_predictor = 0;
    dec->loop_adpcm_index = 0;
}

int AdpcmDecode(struct adpcm_decoder* dec, int nb_samples) {
    int adpcm_buf_pos = dec->adpcm_buf_pos;
    int cur_nibble = dec->cur_nibble;
    int cur_adpcm_index = dec->cur_adpcm_index;
    int pcm_buf_pos = dec->pcm_buf_pos;
    int cur_adpcm_predictor = dec->cur_adpcm_predictor;
    int global_sample_i = dec->global_sample_i;
    while (nb_samples>0) {
        if (adpcm_buf_pos >= dec->adpcm_buf_len) break;
        int sample;
        if (adpcm_buf_pos == 0) {
            cur_adpcm_index = dec->ptr_adpcm[2];
            adpcm_buf_pos = 4;
            cur_nibble = 0;
            sample = (int16_t)(dec->ptr_adpcm[0]|(dec->ptr_adpcm[1]<<8));
        } else {
            int nibble = dec->ptr_adpcm[adpcm_buf_pos];
            nibble = cur_nibble ? nibble >> 4 : nibble & 0xF;
            adpcm_buf_pos += cur_nibble;
            cur_nibble = 1 - cur_nibble;
            int index = ADPCM_TABLE[cur_adpcm_index];
            int diff = index>>3;
            if (nibble&1) diff += index>>2;
            if (nibble&2) diff += index>>1;
            if (nibble&4) diff += index;
            sample = cur_adpcm_predictor;
            if (nibble&8) {
                sample -= diff;
                if (sample<-0x7FFF)sample = -0x7FFF;
            } else {
                sample += diff;
                if (sample>0x7FFF)sample = 0x7FFF;
            }
            cur_adpcm_index += ADPCM_INDEX_TABLE[nibble];
            if (cur_adpcm_index < 0) cur_adpcm_index = 0;
            if (cur_adpcm_index > 88) cur_adpcm_index = 88;
        }
        cur_adpcm_predictor = sample;
        dec->ptr_pcm[pcm_buf_pos] = sample;
        ++pcm_buf_pos;
        if (pcm_buf_pos >= (dec->pcm_buf_len >> 1)) pcm_buf_pos = 0;
        if (global_sample_i == dec->loop_start) {
            dec->loop_adpcm_predictor = cur_adpcm_predictor;
            dec->loop_adpcm_index = cur_adpcm_index;
        }
        if (global_sample_i <= dec->loop_start) {
            ++global_sample_i;
        }
        --nb_samples;
    }
    dec->adpcm_buf_pos = adpcm_buf_pos;
    dec->cur_nibble = cur_nibble;
    dec->cur_adpcm_index = cur_adpcm_index;
    dec->pcm_buf_pos = pcm_buf_pos;
    dec->cur_adpcm_predictor = cur_adpcm_predictor;
    dec->global_sample_i = global_sample_i;
    return nb_samples;
}

int LoadNextAdpcmBlock(struct wave_file_streamer* wfs) {
   int nb_bytes = wfs->adpcm_block_size;
   int data_len = wfs->data_end - wfs->data_start;
    if (wfs->cursor_pos >= data_len) {
        wfs->adpcm_decoder.adpcm_buf_len = 4;
        wfs->adpcm_decoder.cur_nibble = 0;
        return 1;
    }

    FileSeek(&(wfs->fstream), wfs->data_start + wfs->cursor_pos, 0);

    if (wfs->just_looped) {
        wfs->adpcm_decoder.ptr_adpcm[0] = wfs->adpcm_decoder.loop_adpcm_predictor & 0xFF;
        wfs->adpcm_decoder.ptr_adpcm[1] = wfs->adpcm_decoder.loop_adpcm_predictor >> 8;
        wfs->adpcm_decoder.ptr_adpcm[2] = wfs->adpcm_decoder.loop_adpcm_index;
        wfs->adpcm_decoder.ptr_adpcm[3] = 0;
    } else {
        FileRead(&(wfs->fstream), wfs->adpcm_decoder.ptr_adpcm, 4);
        nb_bytes -= 4;
        wfs->cursor_pos += 4;
    }
    wfs->just_looped = 0;

    int block_len = data_len - wfs->cursor_pos;
    wfs->cursor_pos += nb_bytes;
    if (wfs->cursor_pos < data_len) {
        block_len = nb_bytes;
    }
    if (wfs->max_read_len) {
        if (block_len > wfs->max_read_len) block_len = wfs->max_read_len;
        wfs->max_read_len = 0;
        wfs->cursor_pos += block_len - nb_bytes;
    }
    int skip_nibble = 0;
    if (wfs->cursor_pos >= data_len && wfs->is_looped) {
        wfs->just_looped = 1;
        int nb_samples = (wfs->adpcm_block_size - 4) << 1;
        uint64_t tst = _s32_div_f(wfs->loop_start, nb_samples+1);
        int rem = tst>>32;
        int res = tst;
        skip_nibble = rem & 1;
        wfs->max_read_len = (nb_samples - rem + skip_nibble) >> 1;
        wfs->cursor_pos = res * wfs->adpcm_block_size + 4 + (rem >> 1);
    }
    wfs->adpcm_decoder.cur_nibble = wfs->loop_skip_first_nibble;
    wfs->loop_skip_first_nibble = skip_nibble;

    FileRead(&(wfs->fstream), wfs->adpcm_decoder.ptr_adpcm + 4, block_len);

    wfs->adpcm_decoder.adpcm_buf_len = block_len + 4;
    wfs->adpcm_decoder.adpcm_buf_pos = 0;
    return 0;
}
// *************************************************************

// *************************************************************
int LoadSamples(struct wave_file_streamer* wfs, int nb_samples) {
    if (nb_samples == 0) return 0;
    do {
        nb_samples = AdpcmDecode(&(wfs->adpcm_decoder), nb_samples);
        if (nb_samples == 0) return 0;
    } while (!LoadNextAdpcmBlock(wfs));
    return 1;
}
// *************************************************************

// *********************** Main Actions ************************
void StopSound(struct wave_player* p) {
    p->playing = 0;
    __asm__ __volatile__("":::"memory");
    TMR2[0] = 0;
    __asm__ __volatile__("":::"memory");
    ResetChannelQuick(p);
    CloseWaveFileStream(&(p->wave_stream_left));
    CloseWaveFileStream(&(p->wave_stream_right));
    FreeBuffer(p);
}

void StartSound(struct wave_player* p) {
    StopSound(p);
    AllocBuffer(p);
    p->channel_start = CHANNEL_RANGE_START;
    char filename[64];
    sprintf(filename, "SOUND/BGM/bgm%04d_left.wav", p->bgm_id);
    OpenWaveFileStream(&(p->wave_stream_left), filename);
    sprintf(filename, "SOUND/BGM/bgm%04d_right.wav", p->bgm_id);
    OpenWaveFileStream(&(p->wave_stream_right), filename);
    InitAdpcmDecoder(&(p->wave_stream_left.adpcm_decoder), p->snd_addr, ADPCM_BUFFER_SIZE, p->snd_addr+ADPCM_BUFFER_SIZE, PCM_PLAYBACK_BUFFER_SIZE);
    InitAdpcmDecoder(&(p->wave_stream_right.adpcm_decoder), p->snd_addr + ADPCM_BUFFER_SIZE + PCM_PLAYBACK_BUFFER_SIZE, ADPCM_BUFFER_SIZE, p->snd_addr + ADPCM_BUFFER_SIZE *2 + PCM_PLAYBACK_BUFFER_SIZE, PCM_PLAYBACK_BUFFER_SIZE);
    p->wave_stream_left.adpcm_decoder.loop_start = p->wave_stream_left.loop_start;
    p->wave_stream_right.adpcm_decoder.loop_start = p->wave_stream_right.loop_start;
    if (LoadSamples(&(p->wave_stream_left), LAG_BUFFER) || LoadSamples(&(p->wave_stream_right), LAG_BUFFER)) {
        StopSound(p);
        return;
    }
    SetChannelQuick(p);
    p->old_timer = 0;
    p->timer = 0;
        __asm__ __volatile__("":::"memory");
    TMR2[0] = 0x830000;
        __asm__ __volatile__("":::"memory");
    p->playing = 1;
}

int HandleFading(struct wave_player* p) {
    int fade_left = p->fade_to-p->volume;
    int fade_part = _s32_div_f((fade_left<0) ? -fade_left : fade_left, p->fade_time);
    if (fade_left<0) fade_part = -fade_part;
    p->volume += fade_part;
    --p->fade_time;
    if (!p->fade_time) {
        if (!p->fade_play) {
            StopSound(p);
            return 0;
        }
    }
    return 1;
}
void HandleSoundProcess(struct wave_player* p) {
    __asm__ __volatile__("":::"memory");
    int tmr = TMR2[0];
        __asm__ __volatile__("":::"memory");
    tmr &= 0xFFFF;
    int elapsed = (uint16_t)(tmr - p->old_timer);
    p->old_timer = tmr;
    long long res = _s32_div_f(p->timer + (elapsed<<9), GetTimer(p->wave_stream_left.smplrate));
    int nb_samples = res;
    p->timer = (res>>32);
    SetChannelVolumeQuick(p);
    if (LoadSamples(&(p->wave_stream_left), nb_samples) || LoadSamples(&(p->wave_stream_right), nb_samples)) {
        StopSound(p);
        p->bgm_id = -1;
    }
}
// *************************************************************

// *********************** Entry Points ************************
int CheckSnd(int bgm_id) {
    int flag;
    char fn[64];
    struct file_stream f;
    sprintf(fn, "SOUND/BGM/bgm%04d.smd", bgm_id);
    FileInitVeneer(&f);
    FileOpen(&f, fn);
    FileSeek(&f, 4, 0);
    FileRead(&f, &flag, 4);
    FileClose(&f);
    return (flag&4)>>2;
}
void HookSetMusicInfo();
__attribute__((used)) void SNDStream_StartBGM(int player_id, int bgm_id, int fade_in, int volume) {
    if (HookCheckOverlayArm9 != (int)HookSetMusicInfo) {
        HookCheckOverlayArm9 = (int)HookSetMusicInfo;
        player[0].bgm_id = -1;
    }
    struct wave_player* p = player+player_id;
    if (p->bgm_id == bgm_id) return;
    if (CheckSnd(bgm_id)) {
        p->bgm_id = bgm_id;
        p->fade_to = ((volume<<7)-volume)>>8;
        p->volume = 0;
        p->fade_play = 1;
        p->fade_time = _s32_div_f(fade_in*1000, 600);
        if (p->fade_time==0) p->fade_time = 1;
        StartSound(p);
    } else {
        p->bgm_id = -1;
        p->fade_play = 0;
        p->fade_to = 0;
        p->fade_time = 1;
    }
}
__attribute__((used)) void SNDStream_StopBGM(int player_id, int fade_out) {
    struct wave_player* p = player+player_id;
    if (p->bgm_id != -1) {
        p->bgm_id = -1;
        p->fade_time = _s32_div_f(fade_out*1000, 600);
        if (p->fade_time==0) p->fade_time = 1;
        p->fade_to = 0;
        p->fade_play = 0;
    }
}
__attribute__((used)) void SNDStream_ChangeBGM(int player_id, int duration, int volume) {
    struct wave_player* p = player+player_id;
    if (p->bgm_id != -1) {
        p->fade_to = ((volume<<7)-volume)>>8;
        p->fade_time = _s32_div_f(duration*1000, 600);
        if (p->fade_time==0) p->fade_time = 1;
    }
}

__attribute__((used)) void SNDStream_SetMusicInfo(int player_id) {
    struct wave_player* p = player+player_id;
    if (p->playing) {
        if (p->fade_time) {
            if (!HandleFading(p)) return;
        }
        HandleSoundProcess(p);
    }
}
// *************************************************************

// ************************** HOOKS ****************************
__attribute__((naked)) void HookSetMusicInfo() {
    asm volatile("stmdb r13!,{r0-r12}");
    asm volatile("mov r0,#0");
    asm volatile("bl SNDStream_SetMusicInfo");
    asm volatile("ldmia r13!,{r0-r12}");
    asm volatile("b EndHookSoundProcess");
}

__attribute__((naked)) void HookStartBGM() {
    asm volatile("stmdb r13!,{r0-r12}");
    asm volatile("mov r0,#0");
    asm volatile("mov r1,r6");
    asm volatile("mov r2,r5");
    asm volatile("mov r3,r4");
    asm volatile("bl SNDStream_StartBGM");
    asm volatile("ldmia r13!,{r0-r12}");
    asm volatile("bl FunctionUnk1");
    asm volatile("b EndHookStartBGM");
}
__attribute__((naked)) void HookStopBGM() {
    asm volatile("mov r4,r0");
    asm volatile("stmdb r13!,{r0-r12}");
    asm volatile("mov r0,#0");
    asm volatile("mov r1,r4");
    asm volatile("bl SNDStream_StopBGM");
    asm volatile("ldmia r13!,{r0-r12}");
    asm volatile("b EndHookStopBGM");
}
__attribute__((naked)) void HookChangeBGM() {
    asm volatile("bl FunctionUnk1");
    asm volatile("stmdb r13!,{r0-r12}");
    asm volatile("mov r0,#0");
    asm volatile("mov r1,r5");
    asm volatile("mov r2,r4");
    asm volatile("bl SNDStream_ChangeBGM");
    asm volatile("ldmia r13!,{r0-r12}");
    asm volatile("b EndHookChangeBGM");
}
// *************************************************************
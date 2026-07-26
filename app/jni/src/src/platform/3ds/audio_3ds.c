// ndsp audio streaming for the 3DS port.
// Double-buffered PCM16 stereo stream at the configured AudioFreq
// (default 32 kHz; 44.1/48 kHz for MSU-1). Buffers are filled on the
// main thread once per frame (Audio3DS_Update) and consumed by the DSP via
// DMA, so no locking is needed (ZeldaApuLock/Unlock stay no-ops).
//
// On real hardware ndspInit() requires a DSP firmware dump at
// sd:/3ds/dspfirm.cdc; if it is missing the game keeps running muted.

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "types.h"
#include "config.h"
#include "audio.h"

#define NUM_BUFS 4

static bool        g_audio_ok;
static int16      *g_audio_mem;
static ndspWaveBuf g_wave_bufs[NUM_BUFS];
static int         g_samples_per_buf;
static int         g_buf_bytes;

void Audio3DS_Init(void) {
  if (!g_config.enable_audio) return;

  // Honour the configured rate: MSU-1 packs need it (.pcm = 44100 Hz,
  // .opuz = 48000 Hz); the DSP resamples any channel rate to its native
  // mixer rate in hardware. Default to 32000 Hz otherwise (cheapest for
  // the SPC emulation).
  int sample_rate = g_config.audio_freq;
  if (sample_rate < 8000 || sample_rate > 48000)
    sample_rate = 32000;

  g_samples_per_buf = sample_rate / 60;
  g_buf_bytes = g_samples_per_buf * 2 * sizeof(int16);

  if (R_FAILED(ndspInit())) {
    return; // Soft-fail: play without audio
  }

  g_audio_mem = (int16 *)linearAlloc(NUM_BUFS * g_buf_bytes);
  if (!g_audio_mem) {
    ndspExit();
    return;
  }
  memset(g_audio_mem, 0, NUM_BUFS * g_buf_bytes);

  ndspSetOutputMode(NDSP_OUTPUT_STEREO);
  ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
  ndspChnSetRate(0, sample_rate);
  ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
  float mix[12] = {0};
  mix[0] = 1.0f;
  mix[1] = 1.0f;
  ndspChnSetMix(0, mix);

  // Queue all buffers (silence) to start the stream.
  for (int i = 0; i < NUM_BUFS; i++) {
    memset(&g_wave_bufs[i], 0, sizeof(ndspWaveBuf));
    g_wave_bufs[i].data_pcm16 = g_audio_mem + i * g_samples_per_buf * 2;
    g_wave_bufs[i].nsamples   = g_samples_per_buf;
    ndspChnWaveBufAdd(0, &g_wave_bufs[i]);
  }
  DSP_FlushDataCache(g_audio_mem, NUM_BUFS * g_buf_bytes);
  ndspChnSetPaused(0, false);

  g_audio_ok = true;
}

void Audio3DS_Update(void) {
  if (!g_audio_ok) return;

  // Refill every buffer the DSP has finished with. At 60 fps one buffer
  // is consumed per frame on average; catching up on all
  // finished buffers here absorbs slow frames.
  for (int i = 0; i < NUM_BUFS; i++) {
    ndspWaveBuf *buf = &g_wave_bufs[i];
    if (buf->status != NDSP_WBUF_DONE) continue;
    ZeldaRenderAudio(buf->data_pcm16, g_samples_per_buf, 2);
    DSP_FlushDataCache(buf->data_pcm16, g_buf_bytes);
    buf->nsamples = g_samples_per_buf;
    ndspChnWaveBufAdd(0, buf);
  }
}

void Audio3DS_Shutdown(void) {
  if (!g_audio_ok) return;
  ndspChnReset(0);
  ndspExit();
  if (g_audio_mem) {
    linearFree(g_audio_mem);
    g_audio_mem = NULL;
  }
  g_audio_ok = false;
}

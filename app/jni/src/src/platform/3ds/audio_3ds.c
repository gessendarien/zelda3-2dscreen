// ndsp audio streaming for the 3DS port.
// Double-buffered PCM16 stereo stream at 32 kHz. Buffers are filled on the
// main thread once per frame (Audio3DS_Update) and consumed by the DSP via
// DMA, so no locking is needed (ZeldaApuLock/Unlock stay no-ops).
//
// On real hardware ndspInit() requires a DSP firmware dump at
// sd:/3ds/dspfirm.cdc; if it is missing the game keeps running muted.

#include <3ds.h>
#include <stdio.h>
#include <string.h>

#include "types.h"
#include "config.h"
#include "audio.h"

#define SAMPLE_RATE     32000
#define SAMPLES_PER_BUF 512
#define NUM_BUFS        4
#define BUF_BYTES       (SAMPLES_PER_BUF * 2 * sizeof(int16))  // stereo

static bool        g_audio_ok;
static int16      *g_audio_mem;
static ndspWaveBuf g_wave_bufs[NUM_BUFS];

void Audio3DS_Init(void) {
  if (!g_config.enable_audio) return;

  if (R_FAILED(ndspInit())) {
    printf("\x1b[5;1Hndsp init failed (no dspfirm.cdc?) - muted\x1b[K");
    return;
  }

  g_audio_mem = (int16 *)linearAlloc(NUM_BUFS * BUF_BYTES);
  if (!g_audio_mem) {
    ndspExit();
    return;
  }
  memset(g_audio_mem, 0, NUM_BUFS * BUF_BYTES);

  ndspSetOutputMode(NDSP_OUTPUT_STEREO);
  ndspChnReset(0);
  ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
  ndspChnSetRate(0, SAMPLE_RATE);
  ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
  float mix[12] = {0};
  mix[0] = mix[1] = 1.0f;
  ndspChnSetMix(0, mix);

  // Queue all buffers (silence) to start the stream.
  for (int i = 0; i < NUM_BUFS; i++) {
    memset(&g_wave_bufs[i], 0, sizeof(ndspWaveBuf));
    g_wave_bufs[i].data_pcm16 = g_audio_mem + i * SAMPLES_PER_BUF * 2;
    g_wave_bufs[i].nsamples   = SAMPLES_PER_BUF;
    ndspChnWaveBufAdd(0, &g_wave_bufs[i]);
  }
  DSP_FlushDataCache(g_audio_mem, NUM_BUFS * BUF_BYTES);

  g_audio_ok = true;
}

void Audio3DS_Update(void) {
  if (!g_audio_ok) return;

  // Refill every buffer the DSP has finished with. At 60 fps one buffer
  // (512 samples) is consumed per frame on average; catching up on all
  // finished buffers here absorbs slow frames.
  for (int i = 0; i < NUM_BUFS; i++) {
    ndspWaveBuf *buf = &g_wave_bufs[i];
    if (buf->status != NDSP_WBUF_DONE) continue;
    ZeldaRenderAudio(buf->data_pcm16, SAMPLES_PER_BUF, 2);
    DSP_FlushDataCache(buf->data_pcm16, BUF_BYTES);
    buf->nsamples = SAMPLES_PER_BUF;
    ndspChnWaveBufAdd(0, buf);
  }
}

void Audio3DS_Shutdown(void) {
  if (!g_audio_ok) return;
  ndspChnReset(0);
  ndspExit();
  linearFree(g_audio_mem);
  g_audio_mem = NULL;
  g_audio_ok = false;
}

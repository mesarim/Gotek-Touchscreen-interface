// omega_audio.h — chiptune for the OMEGA cracktro, through the JC3248W535C's
// onboard NS4168 I2S amplifier (speaker on P6, amp at U4).
//
// Pin truth comes from Dimmy's own barista project (esp32-ai-aimelo), which
// drives this exact board's speaker: BCLK=42, LRCLK=2, DOUT=41 — none of
// which this firmware uses (LCD 45/47/21/48/1, touch 4/8, SD 11/12/13).
//
// No samples, no files: a three-voice tracker synthesizes the tune —
// a 25%-duty square lead running 50 Hz arpeggios (the cracktro sound),
// a square bass with decay, and an LFSR hi-hat. Rendering happens in its
// own task on core 0, so the animation loop on core 1 never waits on audio.
#pragma once
#include <ESP_I2S.h>

#define OA_BCLK 42
#define OA_LRCK 2
#define OA_DOUT 41
#define OA_RATE 22050

static I2SClass      oaI2S;
static bool          oaI2SUp  = false;
static volatile bool oaRun    = false;
static TaskHandle_t  oaTaskH  = NULL;

static inline float oaNoteHz(int midi) { return 440.0f * powf(2.0f, (midi - 69) / 12.0f); }

static void oaWorker(void *arg) {
  // Am — F — C — G, the eternal progression. MIDI notes, root/third/fifth.
  static const int CH[4][3] = {{57,60,64},{53,57,60},{60,64,67},{55,59,62}};
  const int ROW    = (int)(OA_RATE * 0.120f);   // one 16th at 125 BPM
  const int TICK   = OA_RATE / 50;              // 50 Hz arpeggio, PAL forever
  const float VOL_LEAD = 0.16f, VOL_BASS = 0.24f, VOL_HAT = 0.07f;

  uint32_t phL = 0, phB = 0, lfsr = 0xACE1u;
  uint32_t stepL = 0, stepB = 0;
  int   row = 0, rowSmp = 0, tick = 0, tickSmp = 0;
  float envB = 0, envH = 0;
  int16_t buf[512];

  while (oaRun) {
    for (int i = 0; i < 512; i++) {
      // sequencer: rows and ticks
      if (rowSmp == 0) {
        const int chord = (row / 8) & 3;
        if ((row & 1) == 0) {                                     // bass on 8ths
          const int oct = (row & 2) ? 12 : 0;
          stepB = (uint32_t)(oaNoteHz(CH[chord][0] - 24 + oct) * 4294967296.0f / OA_RATE);
          envB = 1.0f;
        }
        envH = 1.0f;                                              // hat every row
      }
      if (tickSmp == 0) {                                         // arpeggio tick
        const int chord = (row / 8) & 3;
        const int n = CH[chord][tick % 3] + ((tick % 6) >= 3 ? 12 : 0);
        stepL = (uint32_t)(oaNoteHz(n) * 4294967296.0f / OA_RATE);
      }
      // voices
      phL += stepL; phB += stepB;
      const float lead = (phL < 0x40000000u) ? VOL_LEAD : -VOL_LEAD;      // 25% duty
      const float bass = ((phB >> 31) ? VOL_BASS : -VOL_BASS) * envB;
      lfsr = (lfsr >> 1) ^ (uint32_t)(-(int32_t)(lfsr & 1u) & 0xB400u);
      const float hat  = (((int)(lfsr & 0xFF) - 128) / 128.0f) * VOL_HAT * envH;
      envB *= 0.99993f;
      envH *= 0.9985f;
      float s = lead + bass + hat;
      if (s > 0.95f) s = 0.95f; else if (s < -0.95f) s = -0.95f;
      buf[i] = (int16_t)(s * 18000.0f);
      // counters
      if (++rowSmp >= ROW)  { rowSmp = 0; row = (row + 1) & 31; }
      if (++tickSmp >= TICK){ tickSmp = 0; tick++; }
    }
    oaI2S.write((uint8_t *)buf, sizeof(buf));                     // blocking, own task
  }
  // short fade so the amp doesn't pop on exit
  for (int f = 0; f < 8; f++) {
    for (int i = 0; i < 512; i++) buf[i] = (int16_t)(buf[i] * (7 - f) / 8);
    oaI2S.write((uint8_t *)buf, sizeof(buf));
  }
  memset(buf, 0, sizeof(buf));
  oaI2S.write((uint8_t *)buf, sizeof(buf));
  oaTaskH = NULL;
  vTaskDelete(NULL);
}

static void omegaAudioStart() {
  if (oaTaskH) return;
  if (!oaI2SUp) {
    oaI2S.setPins(OA_BCLK, OA_LRCK, OA_DOUT, -1, -1);
    if (!oaI2S.begin(I2S_MODE_STD, OA_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
      Serial.println("omega audio: i2s init failed, intro stays silent");
      return;
    }
    oaI2SUp = true;
  }
  oaRun = true;
  xTaskCreatePinnedToCore(oaWorker, "oaud", 8192, NULL, 1, &oaTaskH, 0);
}

static void omegaAudioStop() {
  oaRun = false;                       // worker fades out and deletes itself
  for (int i = 0; i < 40 && oaTaskH; i++) delay(5);
}

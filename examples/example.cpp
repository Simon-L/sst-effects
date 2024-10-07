#include <memory>

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#include "../tests/simd-test-include.h"
#include "sst/voice-effects/distortion/BitCrusher.h"
#include "sst/voice-effects/utilities/VolumeAndPan.h"

struct DbToLinearProvider
{
  static constexpr size_t nPoints{512};
  float table_dB[nPoints];
  void init() { for (auto i = 0U; i < nPoints; i++) table_dB[i] = powf(10.f, 0.05f * ((float)i - 384.f)); }
  float dbToLinear(float db) const { db += 384;int e = (int)db;float a = db - (float)e;
    return (1.f - a) * table_dB[e & (nPoints - 1)] + a * table_dB[(e + 1) & (nPoints - 1)];
  }
};


struct SSTFX {
  std::array<float, 256> fb{};
  std::array<int, 256> ib{};
  float sampleRate;
  DbToLinearProvider dbtlp;
  
  struct FxConfig
  {
    using BaseClass = SSTFX;
    static constexpr int blockSize{16};
    static void setFloatParam(BaseClass *b, int i, float f) { b->fb[i] = f; }
    static float getFloatParam(const BaseClass *b, int i) { return b->fb[i]; }
    
    static void setIntParam(BaseClass *b, int i, int v) { b->ib[i] = v; }
    static int getIntParam(const BaseClass *b, int i) { return b->ib[i]; }
    
    static float dbToLinear(const BaseClass *b, float f) { return b->dbtlp.dbToLinear(f); }
    static float equalNoteToPitch(const BaseClass *, float f) { return pow(2.f, (f + 69) / 12.f); }
    static float getSampleRate(const BaseClass *b) { return b->sampleRate; }
    static float getSampleRateInv(const BaseClass *b) { return 1.0 / b->sampleRate; }
    
    static void preReservePool(BaseClass *, size_t) {}
    static void preReserveSingleInstancePool(BaseClass *, size_t) {}
    static uint8_t *checkoutBlock(BaseClass *, size_t n) {
      printf("checkoutBlock %d\n", n);
      uint8_t* ptr = (uint8_t*)malloc(n);
      return ptr;
    }
    static void returnBlock(BaseClass *, uint8_t * ptr, size_t n) {
      printf("returnBlock %d\n", n);
      free(ptr);
    }
  };
  
  std::unique_ptr<sst::voice_effects::utilities::VolumeAndPan<FxConfig>> fx;
  
  SSTFX()
  {
    dbtlp.init();
  }
  
  void init(float sampleRate) {
    sampleRate = sampleRate;
    dbtlp.init();
    
    fx = std::make_unique<sst::voice_effects::utilities::VolumeAndPan<FxConfig>>();
    fx->initVoiceEffectParams();
    fx->setFloatParam(sst::voice_effects::utilities::VolumeAndPan<FxConfig>::fpVolume, 8);
    fx->setFloatParam(sst::voice_effects::utilities::VolumeAndPan<FxConfig>::fpPan, -0.4);
  }
  
  void process(const float *const datainL, const float *const datainR, float *dataoutL,
                       float *dataoutR, float pitch)
  {
    fx->processStereo(datainL,datainR,dataoutL,dataoutR,pitch);
  }
  
};

int main(int argc, char const *argv[]) {
  unsigned int channels;
  unsigned int sampleRate;
  drwav_uint64 totalPCMFrameCount;
  float* pSampleData = drwav_open_file_and_read_pcm_frames_f32(argv[1], &channels, &sampleRate, &totalPCMFrameCount, NULL);
  
  printf("sampleRate: %d channels: %d, totalPCMFrameCount: %d\n", sampleRate, channels, totalPCMFrameCount);
  
  if (channels > 2) {
    printf("Only 1 or 2 channels wav files supported, exiting.\n");
    exit(0);
  }
  
  SSTFX fx;
  fx.init(sampleRate);
  auto blockSize = SSTFX::FxConfig::blockSize;
  
  uint32_t total_blocks = totalPCMFrameCount / blockSize;
  
  uint32_t sample_count = 0;
  
  float outputSamples[totalPCMFrameCount * 2];
  
  FILE* datFile = fopen("/tmp/voice-effect-example.dat", "w" );
  for (size_t block = 0; block < total_blocks; block++) {
    float inputL[blockSize];
    float inputR[blockSize];
    float outputL[blockSize];
    float outputR[blockSize];
    
    for (size_t s = 0; s < blockSize; s++) {
      if (channels == 2) {
        inputL[s] = pSampleData[(block * (blockSize * 2)) + (s * 2)];
        inputR[s] = pSampleData[(block * (blockSize * 2)) + (s * 2) + 1];
      } else {
        inputL[s] = pSampleData[(block * blockSize) + s];
        inputR[s] = inputL[s];
      }
    }
    
    fx.process((const float *)&inputL[0], (const float *)&inputR[0], &outputL[0], &outputR[0], 1);
    
    for (size_t sample_index = 0; sample_index < blockSize; sample_index++) {
      outputSamples[(block * (blockSize * 2)) + (sample_index * 2)] = outputL[sample_index];
      outputSamples[(block * (blockSize * 2)) + (sample_index * 2) + 1] = outputR[sample_index];
      
      fprintf(datFile, "%d %f %f\n", sample_count, inputL[sample_index], outputL[sample_index]);
      sample_count++;
    }
    
  }
  fclose(datFile);
  
  drwav wav;
  drwav_data_format format;
  format.container = drwav_container_riff;
  format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
  format.channels = 2;
  format.sampleRate = 44100;
  format.bitsPerSample = 32;
  drwav_init_file_write(&wav, "/tmp/voice-effect-example.wav", &format, NULL);
  drwav_uint64 framesWritten = drwav_write_pcm_frames(&wav, sample_count, outputSamples);

  system("gnuplot -p -e \"plot '/tmp/voice-effect-example.dat' using 1:2 with lines, '' using 1:3 with lines\"");
  return 0;
}
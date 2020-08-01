#include "XSFConfig.h"
#include "XSFPlayer_NCSF.h"
#include <cstdio>
XSFConfig *xSFConfig;
// http://soundfile.sapp.org/doc/WaveFormat/
void write_wav_header(FILE *outf, int nsamples) {
#define w16(x) do { \
	uint16_t tmp[] = { htole16(x) }; \
	fwrite(tmp, 1, 2, outf); \
} while(0)
#define w32(x) do { \
	uint32_t tmp[] = { htole32(x) }; \
	fwrite(tmp, 1, 4, outf); \
} while(0)
	// Write wav header
	// ChunkID
	fwrite("RIFF", 4, 1, outf);
	// ChunkSize
	int subchunk2size = nsamples * 2 * 2;
	w32(subchunk2size + 36);
	// Format
	fwrite("WAVE", 4, 1, outf);

	// Subchunk1ID
	fwrite("fmt ", 4, 1, outf);
	// Subchunk1Size
	w32(16);
	// Audio format
	w16(1);
	// NChannels
	w16(2);
	// SampleRate
	w32(48000);
	// ByteRate
	w32(48000 * 2 * 2);
	// BlockAlign
	w16(2 * 2);
	// BitsPerSample
	w16(16);

	// Subchunk2ID
	fwrite("data", 4, 1, outf);
	w32(subchunk2size);
}
int main(int argc, const char **argv){
	auto cfg = XSFConfig_NCSF();
	auto fn = std::string(argv[1]);
	FILE *outf = fopen(argv[2], "w");

	XSFPlayer_NCSF player{fn, cfg};
	player.Load();

	std::vector<uint8_t> buf;
	buf.resize(4096);

	unsigned written;

	auto length = player.GetLengthInSamples();
	write_wav_header(outf, length);
	while(!player.FillBuffer(buf, written)){
		fwrite(buf.data(), 1, 4096, outf);
	}
	fclose(outf);
}

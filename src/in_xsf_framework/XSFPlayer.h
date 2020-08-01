/*
 * xSF - Core Player
 * By Naram Qashat (CyberBotX) [cyberbotx@cyberbotx.com]
 * Last modification on 2014-09-24
 *
 * Partially based on the vio*sf framework
 */

#pragma once

#include <memory>
#include "XSFConfig.h"
#include "XSFFile.h"

// This is a base class, a player for a specific type of xSF should inherit from this.
class XSFPlayer
{
protected:
	static const uint32_t CHECK_SILENCE_BIAS = 0x8000000;
	static const uint32_t CHECK_SILENCE_LEVEL = 7;

	std::unique_ptr<XSFFile> xSF;
	unsigned detectedSilenceSample, detectedSilenceSec, skipSilenceOnStartSec, lengthSample, fadeSample, currentSample;
	uint32_t prevSampleL, prevSampleR;
	int lengthInMS, fadeInMS;
	double volume;
	bool ignoreVolume, uses32BitSamples;
	const XSFConfig &xSFConfig;

	XSFPlayer(const XSFConfig &);
	XSFPlayer(const XSFPlayer &xSFPLayer, const XSFConfig &);
public:
	// These are not defined in XSFPlayer.cpp, they should be defined in your own player's source.  The Create functions should return a pointer to your player's class.
	static const char *WinampDescription;
	static const char *WinampExts;

	virtual ~XSFPlayer() { }
	XSFFile *GetXSFFile() { return this->xSF.get(); }
	const XSFFile *GetXSFFile() const { return this->xSF.get(); }
	unsigned GetLengthInSamples() const { return this->lengthSample + this->fadeSample; }
	unsigned GetSampleRate() const { return this->xSFConfig.sampleRate; }
	void IgnoreVolume() { this->ignoreVolume = true; }
	virtual bool Load();
	bool FillBuffer(std::vector<uint8_t> &buf, unsigned &samplesWritten);
	virtual void GenerateSamples(std::vector<uint8_t> &buf, unsigned offset, unsigned samples) = 0;
	void SeekTop();
#ifdef WINAMP_PLUGIN
	int Seek(unsigned seekPosition, volatile int *killswitch, std::vector<uint8_t> &buf, Out_Module *outMod);
#endif
	virtual void Terminate() = 0;
};

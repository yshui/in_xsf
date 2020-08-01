/*
 * xSF - Core Player
 * By Naram Qashat (CyberBotX) [cyberbotx@cyberbotx.com]
 * Last modification on 2014-09-24
 *
 * Partially based on the vio*sf framework
 */

#include <cstring>
#include "XSFPlayer.h"
#include "XSFConfig.h"
#include "XSFCommon.h"

XSFPlayer::XSFPlayer(const XSFConfig &cfg) : xSF(), detectedSilenceSample(0), detectedSilenceSec(0), skipSilenceOnStartSec(5), lengthSample(0), fadeSample(0), currentSample(0),
	prevSampleL(CHECK_SILENCE_BIAS), prevSampleR(CHECK_SILENCE_BIAS), lengthInMS(-1), fadeInMS(-1), volume(1.0), ignoreVolume(false), uses32BitSamples(false), xSFConfig(cfg)
{
}

bool XSFPlayer::FillBuffer(std::vector<uint8_t> &buf, unsigned &samplesWritten)
{
	bool endFlag = false;
	unsigned detectSilence = xSFConfig.GetDetectSilenceSec();
	unsigned pos = 0, bufsize = buf.size() >> (this->uses32BitSamples ? 3 : 2);
	while (pos < bufsize)
	{
		unsigned remain = bufsize - pos, offset = pos;
		this->GenerateSamples(buf, pos << (this->uses32BitSamples ? 3 : 2), remain);

		pos += remain;
	}

	/* Detect end of song */
	if (!xSFConfig.GetPlayInfinitely())
	{
		if (this->currentSample >= this->lengthSample + this->fadeSample)
		{
			samplesWritten = 0;
			return true;
		}
		if (this->currentSample + bufsize >= this->lengthSample + this->fadeSample)
		{
			bufsize = this->lengthSample + this->fadeSample - this->currentSample;
			endFlag = true;
		}
	}

	this->currentSample += bufsize;
	samplesWritten = bufsize;
	return endFlag;
}

bool XSFPlayer::Load()
{
	this->lengthInMS = this->xSF->GetLengthMS(xSFConfig.GetDefaultLength());
	this->fadeInMS = this->xSF->GetFadeMS(xSFConfig.GetDefaultFade());
	this->lengthSample = static_cast<uint64_t>(this->lengthInMS) * this->GetSampleRate() / 1000;
	this->fadeSample = static_cast<uint64_t>(this->fadeInMS) * this->GetSampleRate() / 1000;
	this->volume = this->xSF->GetVolume(xSFConfig.GetVolumeType(), xSFConfig.GetPeakType());
	return true;
}

void XSFPlayer::SeekTop()
{
	this->skipSilenceOnStartSec = xSFConfig.GetSkipSilenceOnStartSec();
	this->currentSample = this->detectedSilenceSec = this->detectedSilenceSample = 0;
	this->prevSampleL = this->prevSampleR = CHECK_SILENCE_BIAS;
}


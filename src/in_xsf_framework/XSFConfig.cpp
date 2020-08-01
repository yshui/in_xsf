/*
 * xSF - Core configuration handler
 * By Naram Qashat (CyberBotX) [cyberbotx@cyberbotx.com]
 * Last modification on 2014-10-05
 *
 * Partially based on the vio*sf framework
 */

#include "XSFConfig.h"
#include "XSFPlayer.h"
#include "convert.h"

enum
{
	idPlayInfinitely = 500,
	idDefaultLength,
	idDefaultFade,
	idSkipSilenceOnStartSec,
	idDetectSilenceSec,
	idVolume,
	idReplayGain,
	idClipProtect,
	idSampleRate,
	idTitleFormat,
	idResetDefaults,
	idInfoTitle = 600,
	idInfoArtist,
	idInfoGame,
	idInfoYear,
	idInfoGenre,
	idInfoCopyright,
	idInfoComment
};

bool XSFConfig::initPlayInfinitely = false;
std::string XSFConfig::initSkipSilenceOnStartSec = "5";
std::string XSFConfig::initDetectSilenceSec = "5";
std::string XSFConfig::initDefaultLength = "1:55";
std::string XSFConfig::initDefaultFade = "5";
std::string XSFConfig::initTitleFormat = "%game%[ - [%disc%.]%track%] - %title%";
double XSFConfig::initVolume = 1.0;
VolumeType XSFConfig::initVolumeType = VOLUMETYPE_REPLAYGAIN_ALBUM;
PeakType XSFConfig::initPeakType = PEAKTYPE_REPLAYGAIN_TRACK;

XSFConfig::XSFConfig() : playInfinitely(false), skipSilenceOnStartSec(0), detectSilenceSec(0), defaultLength(0), defaultFade(0), volume(1.0), volumeType(VOLUMETYPE_NONE), peakType(PEAKTYPE_NONE),
	sampleRate(0), titleFormat(""), supportedSampleRates()
{
}

extern XSFFile *xSFFileInInfo;

bool XSFConfig::GetPlayInfinitely() const
{
	return this->playInfinitely;
}

unsigned long XSFConfig::GetSkipSilenceOnStartSec() const
{
	return this->skipSilenceOnStartSec;
}

unsigned long XSFConfig::GetDetectSilenceSec() const
{
	return this->detectSilenceSec;
}

unsigned long XSFConfig::GetDefaultLength() const
{
	return this->defaultLength;
}

unsigned long XSFConfig::GetDefaultFade() const
{
	return this->defaultFade;
}

double XSFConfig::GetVolume() const
{
	return this->volume;
}

VolumeType XSFConfig::GetVolumeType() const
{
	return this->volumeType;
}

PeakType XSFConfig::GetPeakType() const
{
	return this->peakType;
}

const std::string &XSFConfig::GetTitleFormat() const
{
	return this->titleFormat;
}

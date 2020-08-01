/*
 * xSF - NCSF configuration
 * By Naram Qashat (CyberBotX) [cyberbotx@cyberbotx.com]
 * Last modification on 2014-12-09
 *
 * Partially based on the vio*sf framework
 */

#include "XSFConfig_NCSF.h"

enum
{
	idInterpolation = 1000,
	idMutes
};

unsigned XSFConfig::initSampleRate = 44100;
std::string XSFConfig::commonName = "NCSF Decoder";
std::string XSFConfig::versionNumber = "1.11.1";
unsigned XSFConfig_NCSF::initInterpolation = 4;
std::string XSFConfig_NCSF::initMutes = "0000000000000000";

XSFConfig *XSFConfig::Create()
{
	return new XSFConfig_NCSF();
}

XSFConfig_NCSF::XSFConfig_NCSF() : XSFConfig(), interpolation(0), mutes()
{
	this->supportedSampleRates.push_back(8000);
	this->supportedSampleRates.push_back(11025);
	this->supportedSampleRates.push_back(16000);
	this->supportedSampleRates.push_back(22050);
	this->supportedSampleRates.push_back(32000);
	this->supportedSampleRates.push_back(44100);
	this->supportedSampleRates.push_back(48000);
	this->supportedSampleRates.push_back(88200);
	this->supportedSampleRates.push_back(96000);
	this->supportedSampleRates.push_back(176400);
	this->supportedSampleRates.push_back(192000);
	this->sampleRate = 48000;
}

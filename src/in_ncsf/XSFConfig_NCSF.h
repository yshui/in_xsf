/*
 * xSF - NCSF configuration
 * By Naram Qashat (CyberBotX) [cyberbotx@cyberbotx.com]
 * Last modification on 2014-10-05
 *
 * Partially based on the vio*sf framework
 */

#pragma once

#include <bitset>
#include <memory>
#include "XSFConfig.h"

class XSFConfig_NCSF : public XSFConfig
{
protected:
	static unsigned initInterpolation;
	static std::string initMutes;

	unsigned interpolation;
	std::bitset<16> mutes;
public:
	XSFConfig_NCSF();
};

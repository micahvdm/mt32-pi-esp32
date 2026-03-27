//
// config.cpp
//
// mt32-pi - A baremetal MIDI synthesizer for Raspberry Pi
// Copyright (C) 2020-2023 Dale Whinham <daleyo@gmail.com>
//
// This file is part of mt32-pi.
//
// mt32-pi is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// mt32-pi is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// mt32-pi. If not, see <http://www.gnu.org/licenses/>.
//

#include <cstdlib>

#include <circle/logger.h>
#include <circle/util.h>
#include <fatfs/ff.h>
#include <ini.h>

#include "config.h"
#include "utility.h"

LOGMODULE("config");
const char* TrueStrings[]  = {"true", "on", "1"};
const char* FalseStrings[] = {"false", "off", "0"};

// Templated function that converts a string to an enum
template <class T, const char* pEnumStrings[], size_t N> static bool ParseEnum(const char* pString, T* pOut)
{
	for (size_t i = 0; i < N; ++i)
	{
		if (!strcasecmp(pString, pEnumStrings[i]))
		{
			*pOut = static_cast<T>(i);
			return true;
		}
	}

	return false;
}

// Macro to expand templated enum parser into an overloaded definition of ParseOption()
#define CONFIG_ENUM_PARSER(ENUM_NAME)                                                                           \
	bool CConfig::ParseOption(const char* pString, ENUM_NAME* pOut)                                             \
	{                                                                                                           \
		return ParseEnum<ENUM_NAME, ENUM_NAME##Strings, Utility::ArraySize(ENUM_NAME##Strings)>(pString, pOut); \
	}

// Enum string tables
CONFIG_ENUM_STRINGS(TSystemDefaultSynth, ENUM_SYSTEMDEFAULTSYNTH);
CONFIG_ENUM_STRINGS(TAudioOutputDevice, ENUM_AUDIOOUTPUTDEVICE);
CONFIG_ENUM_STRINGS(TMT32EmuResamplerQuality, ENUM_RESAMPLERQUALITY);
CONFIG_ENUM_STRINGS(TMT32EmuMIDIChannels, ENUM_MIDICHANNELS);
CONFIG_ENUM_STRINGS(TMT32EmuROMSet, ENUM_MT32ROMSET);
CONFIG_ENUM_STRINGS(TLCDType, ENUM_LCDTYPE);
CONFIG_ENUM_STRINGS(TControlScheme, ENUM_CONTROLSCHEME);
CONFIG_ENUM_STRINGS(TEncoderType, ENUM_ENCODERTYPE);
CONFIG_ENUM_STRINGS(TLCDRotation, ENUM_LCDROTATION);
CONFIG_ENUM_STRINGS(TLCDMirror, ENUM_LCDMIRROR);
CONFIG_ENUM_STRINGS(TNetworkMode, ENUM_NETWORKMODE);
CONFIG_ENUM_STRINGS(TMixerMode, ENUM_MIXERMODE);
CONFIG_ENUM_STRINGS(TYmfmChip, ENUM_YMFMCHIP);

CConfig* CConfig::s_pThis = nullptr;

CConfig::CConfig()
{
	// Expand assignment of all default values from definition file
	#define CFG(_1, _2, MEMBER_NAME, DEFAULT, _3...) MEMBER_NAME = DEFAULT;
	#include "config.def"

	s_pThis = this;
	for (unsigned i = 0; i < 128; ++i)
	{
		MIDICCMap[i].MT32Action = TMIDICCAction::None;
		MIDICCMap[i].SoundFontAction = TMIDICCAction::None;
	}

	MIDICCMap[104] = { TMIDICCAction::SelectMT32,         TMIDICCAction::SelectMT32 };
	MIDICCMap[105] = { TMIDICCAction::SelectSoundFont,    TMIDICCAction::SelectSoundFont };
	MIDICCMap[106] = { TMIDICCAction::PrevRomOrSoundFont, TMIDICCAction::PrevRomOrSoundFont };
	MIDICCMap[107] = { TMIDICCAction::NextRomOrSoundFont, TMIDICCAction::NextRomOrSoundFont };

	MIDICCMap[21] = { TMIDICCAction::MainReverb,        TMIDICCAction::MainReverb };
	MIDICCMap[22] = { TMIDICCAction::MT32ReverbEnable,  TMIDICCAction::SFReverbRoomSize };
	MIDICCMap[23] = { TMIDICCAction::MT32MIDIDelayMode, TMIDICCAction::SFReverbDamping };
	MIDICCMap[24] = { TMIDICCAction::MT32AnalogMode,    TMIDICCAction::SFReverbWidth };
	MIDICCMap[25] = { TMIDICCAction::MT32DACMode,       TMIDICCAction::SFChorusLevel };
	MIDICCMap[26] = { TMIDICCAction::MT32RendererType,  TMIDICCAction::SFChorusDepth };
	MIDICCMap[27] = { TMIDICCAction::MT32PartialCount,  TMIDICCAction::SFChorusSpeed };
	MIDICCMap[28] = { TMIDICCAction::MasterVolume,      TMIDICCAction::SoundFontGain };
}

bool CConfig::Initialize(const char* pPath)
{
	FIL File;
	if (f_open(&File, pPath, FA_READ) != FR_OK)
	{
		LOGERR("Couldn't open '%s' for reading", pPath);
		return false;
	}

	// +1 byte for null terminator
	const UINT nSize = f_size(&File);
	char Buffer[nSize + 1];
	UINT nRead;

	if (f_read(&File, Buffer, nSize, &nRead) != FR_OK)
	{
		LOGERR("Error reading config file", pPath);
		f_close(&File);
		return false;
	}

	// Ensure null-terminated
	Buffer[nRead] = '\0';

	const int nResult = ini_parse_string(Buffer, INIHandler, this);
	if (nResult > 0)
		LOGWARN("Config parse error on line %d", nResult);

	f_close(&File);
	return nResult >= 0;

}

int CConfig::INIHandler(void* pUser, const char* pSection, const char* pName, const char* pValue)
{
	CConfig* const pConfig = static_cast<CConfig*>(pUser);

	if (strcasecmp(pSection, "midi_cc_map") == 0)
	{
		if (strncasecmp(pName, "cc", 2) == 0)
		{
			const int nCC = atoi(pName + 2);
			if (nCC >= 0 && nCC < 128)
			{
				TMIDICCMapping Mapping;
				if (ParseMIDICCMapping(pValue, &Mapping))
				{
					s_pThis->MIDICCMap[nCC] = Mapping;
					return 1;
				}
			}
		}

		return 1;
	}

	//LOGDBG("'%s', '%s', '%s'", pSection, pName,  pValue);

	// Automatically generate ParseOption() calls from macro definition file
	#define BEGIN_SECTION(SECTION)       \
		if (!strcmp(#SECTION, pSection)) \
		{

	#define CFG(INI_NAME, TYPE, MEMBER_NAME, _2, ...) \
		if (!strcmp(#INI_NAME, pName))                \
			return ParseOption(pValue, &pConfig->MEMBER_NAME __VA_OPT__(, ) __VA_ARGS__);

	#define END_SECTION \
		return 0;       \
		}

	#include "config.def"

	return 0;
}

bool CConfig::ParseOption(const char* pString, bool* pOutBool)
{
	for (auto pTrueString : TrueStrings)
	{
		if (!strcasecmp(pString, pTrueString))
		{
			*pOutBool = true;
			return true;
		}
	}

	for (auto pFalseString : FalseStrings)
	{
		if (!strcasecmp(pString, pFalseString))
		{
			*pOutBool = false;
			return true;
		}
	}

	return false;
}

bool CConfig::ParseOption(const char* pString, int* pOutInt, bool bHex)
{
	*pOutInt = strtol(pString, nullptr, bHex ? 16 : 10);
	return true;
}

bool CConfig::ParseOption(const char* pString, float* pOutFloat)
{
	*pOutFloat = strtof(pString, nullptr);
	return true;
}

bool CConfig::ParseOption(const char* pString, CString* pOut)
{
	*pOut = CString(pString);
	return true;
}

bool CConfig::ParseOption(const char* pString, CIPAddress* pOut)
{
	// Space for 4 period-separated groups of 3 digits plus null terminator
	char Buffer[16];
	u8 IPAddress[4];

	strncpy(Buffer, pString, sizeof(Buffer) - 1);
	Buffer[sizeof(Buffer) - 1] = '\0';
	char* pToken = strtok(Buffer, ".");

	for (uint8_t i = 0; i < 4; ++i)
	{
		if (!pToken)
			return false;

		IPAddress[i] = atoi(pToken);
		pToken = strtok(nullptr, ".");
	}

	pOut->Set(IPAddress);
	return true;
}

CConfig::TMIDICCAction CConfig::ParseMIDICCAction(const char* pValue)
{
	if (!pValue || !*pValue)
		return TMIDICCAction::None;

	if (strcasecmp(pValue, "none") == 0) return TMIDICCAction::None;

	if (strcasecmp(pValue, "select_mt32") == 0) return TMIDICCAction::SelectMT32;
	if (strcasecmp(pValue, "select_soundfont") == 0) return TMIDICCAction::SelectSoundFont;
	if (strcasecmp(pValue, "prev_rom_or_soundfont") == 0) return TMIDICCAction::PrevRomOrSoundFont;
	if (strcasecmp(pValue, "next_rom_or_soundfont") == 0) return TMIDICCAction::NextRomOrSoundFont;

	if (strcasecmp(pValue, "main_reverb") == 0) return TMIDICCAction::MainReverb;
	if (strcasecmp(pValue, "master_volume") == 0) return TMIDICCAction::MasterVolume;
	if (strcasecmp(pValue, "sf_gain") == 0) return TMIDICCAction::SoundFontGain;

	if (strcasecmp(pValue, "mt32_reverb_enable") == 0) return TMIDICCAction::MT32ReverbEnable;
	if (strcasecmp(pValue, "mt32_midi_delay_mode") == 0) return TMIDICCAction::MT32MIDIDelayMode;
	if (strcasecmp(pValue, "mt32_analog_mode") == 0) return TMIDICCAction::MT32AnalogMode;
	if (strcasecmp(pValue, "mt32_dac_mode") == 0) return TMIDICCAction::MT32DACMode;
	if (strcasecmp(pValue, "mt32_renderer_type") == 0) return TMIDICCAction::MT32RendererType;
	if (strcasecmp(pValue, "mt32_partial_count") == 0) return TMIDICCAction::MT32PartialCount;

	if (strcasecmp(pValue, "sf_reverb_roomsize") == 0) return TMIDICCAction::SFReverbRoomSize;
	if (strcasecmp(pValue, "sf_reverb_damping") == 0) return TMIDICCAction::SFReverbDamping;
	if (strcasecmp(pValue, "sf_reverb_width") == 0) return TMIDICCAction::SFReverbWidth;
	if (strcasecmp(pValue, "sf_chorus_level") == 0) return TMIDICCAction::SFChorusLevel;
	if (strcasecmp(pValue, "sf_chorus_depth") == 0) return TMIDICCAction::SFChorusDepth;
	if (strcasecmp(pValue, "sf_chorus_speed") == 0) return TMIDICCAction::SFChorusSpeed;

	return TMIDICCAction::None;
}

bool CConfig::ParseMIDICCMapping(const char* pValue, TMIDICCMapping* pOut)
{
	if (!pValue || !pOut || !*pValue)
		return false;

	const char* pSep = strchr(pValue, '|');
	if (!pSep)
	{
		const TMIDICCAction Action = ParseMIDICCAction(pValue);
		pOut->MT32Action = Action;
		pOut->SoundFontAction = Action;
		return true;
	}

	char szLeft[64];
	char szRight[64];

	const size_t nLeftLen = static_cast<size_t>(pSep - pValue);
	if (nLeftLen >= sizeof(szLeft))
		return false;

	strncpy(szLeft, pValue, nLeftLen);
	szLeft[nLeftLen] = '\0';

	strncpy(szRight, pSep + 1, sizeof(szRight) - 1);
	szRight[sizeof(szRight) - 1] = '\0';

	pOut->MT32Action = ParseMIDICCAction(szLeft);
	pOut->SoundFontAction = ParseMIDICCAction(szRight);
	return true;
}

// Define template function wrappers for parsing enums
CONFIG_ENUM_PARSER(TSystemDefaultSynth);
CONFIG_ENUM_PARSER(TAudioOutputDevice);
CONFIG_ENUM_PARSER(TMT32EmuResamplerQuality);
CONFIG_ENUM_PARSER(TMT32EmuMIDIChannels);
CONFIG_ENUM_PARSER(TMT32EmuROMSet);
CONFIG_ENUM_PARSER(TLCDType);
CONFIG_ENUM_PARSER(TControlScheme);
CONFIG_ENUM_PARSER(TEncoderType);
CONFIG_ENUM_PARSER(TLCDRotation);
CONFIG_ENUM_PARSER(TLCDMirror);
CONFIG_ENUM_PARSER(TNetworkMode);
CONFIG_ENUM_PARSER(TMixerMode);CONFIG_ENUM_PARSER(TYmfmChip)
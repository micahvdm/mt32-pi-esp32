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
#include <type_traits>

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
	MIDICCMap[109] = { TMIDICCAction::SustainCC64,        TMIDICCAction::SustainCC64 };
	
	MIDICCMap[108] = { TMIDICCAction::LooperArmStop,      TMIDICCAction::LooperArmStop };
	MIDICCMap[110] = { TMIDICCAction::LooperBPM,          TMIDICCAction::LooperBPM };
	MIDICCMap[111] = { TMIDICCAction::LooperQuantize,     TMIDICCAction::LooperQuantize };
	MIDICCMap[112] = { TMIDICCAction::LooperSave,         TMIDICCAction::LooperSave };
	MIDICCMap[113] = { TMIDICCAction::LooperClear,        TMIDICCAction::LooperClear };

	MIDICCMap[21] = { TMIDICCAction::MainReverb,        TMIDICCAction::MainReverb };
	MIDICCMap[22] = { TMIDICCAction::MT32ReverbEnable,  TMIDICCAction::SFReverbRoomSize };
	MIDICCMap[23] = { TMIDICCAction::MT32MIDIDelayMode, TMIDICCAction::SFReverbDamping };
	MIDICCMap[24] = { TMIDICCAction::MT32AnalogMode,    TMIDICCAction::SFReverbWidth };
	MIDICCMap[25] = { TMIDICCAction::MT32DACMode,       TMIDICCAction::SFChorusLevel };
	MIDICCMap[26] = { TMIDICCAction::MT32RendererType,  TMIDICCAction::SFChorusDepth };
	MIDICCMap[27] = { TMIDICCAction::MT32PartialCount,  TMIDICCAction::SFChorusSpeed };
	MIDICCMap[28] = { TMIDICCAction::MasterVolume,      TMIDICCAction::MasterVolume };

	for (unsigned i = 0; i < static_cast<unsigned>(TMIDICCBindingID::Count); ++i)
		MIDICCBindingCC[i] = -1;

	MIDICCBindingCC[static_cast<unsigned>(TMIDICCBindingID::MainReverb)]        = 21;
	MIDICCBindingCC[static_cast<unsigned>(TMIDICCBindingID::ReverbParam1)]      = 22;
	MIDICCBindingCC[static_cast<unsigned>(TMIDICCBindingID::ReverbParam2)]      = 23;
	MIDICCBindingCC[static_cast<unsigned>(TMIDICCBindingID::ReverbParam3)]      = 24;
	MIDICCBindingCC[static_cast<unsigned>(TMIDICCBindingID::ChorusParam1)]      = 25;
	MIDICCBindingCC[static_cast<unsigned>(TMIDICCBindingID::ChorusParam2)]      = 26;
	MIDICCBindingCC[static_cast<unsigned>(TMIDICCBindingID::ChorusParam3)]      = 27;
	MIDICCBindingCC[static_cast<unsigned>(TMIDICCBindingID::MasterOrGain)]      = 28;

	MIDICCBindingCC[static_cast<unsigned>(TMIDICCBindingID::SelectMT32)]        = 104;
	MIDICCBindingCC[static_cast<unsigned>(TMIDICCBindingID::SelectSoundFont)]   = 105;
	MIDICCBindingCC[static_cast<unsigned>(TMIDICCBindingID::PrevRomOrSoundFont)] = 106;
	MIDICCBindingCC[static_cast<unsigned>(TMIDICCBindingID::NextRomOrSoundFont)] = 107;
	MIDICCBindingCC[static_cast<unsigned>(TMIDICCBindingID::LooperArmStop)]     = 108;
	MIDICCBindingCC[static_cast<unsigned>(TMIDICCBindingID::LooperBPM)]         = 110;
	MIDICCBindingCC[static_cast<unsigned>(TMIDICCBindingID::LooperQuantize)]    = 111;
	MIDICCBindingCC[static_cast<unsigned>(TMIDICCBindingID::LooperSave)]        = 112;
	MIDICCBindingCC[static_cast<unsigned>(TMIDICCBindingID::LooperMetronome)]   = 114;
	MIDICCBindingCC[static_cast<unsigned>(TMIDICCBindingID::LooperClear)]       = 113;
	MIDICCBindingCC[static_cast<unsigned>(TMIDICCBindingID::SustainCC64)]       = 109;

	RebuildMIDICCMap();
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
	char* pBuffer = new char[nSize + 1];
	if (!pBuffer)
	{
		f_close(&File);
		return false;
	}

	UINT nRead;
	if (f_read(&File, pBuffer, nSize, &nRead) != FR_OK)
	{
		LOGERR("Error reading config file: %s", pPath);
		f_close(&File);
		delete[] pBuffer;
		return false;
	}

	// Ensure null-terminated
	pBuffer[nRead] = '\0';

	const int nResult = ini_parse_string(pBuffer, INIHandler, this);
	if (nResult > 0)
		LOGWARN("Config parse error on line %d", nResult);

	f_close(&File);
	delete[] pBuffer;
	return nResult >= 0;
}

// Helper functions for CConfig::Write overloads
static void WriteCfg(FIL* fp, const char* pName, bool bVal) { f_printf(fp, "%s = %s\n", pName, bVal ? "on" : "off"); }
static void WriteCfg(FIL* fp, const char* pName, int nVal) { f_printf(fp, "%s = %d\n", pName, nVal); }
static void WriteCfg(FIL* fp, const char* pName, float fVal)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(fVal));
	f_printf(fp, "%s = %s\n", pName, buf);
}
static void WriteCfg(FIL* fp, const char* pName, const CString& sVal) { f_printf(fp, "%s = %s\n", pName, static_cast<const char*>(sVal)); }
static void WriteCfg(FIL* fp, const char* pName, const CIPAddress& ipVal)
{
	CString sIP;
	const_cast<CIPAddress&>(ipVal).Format(&sIP);
	f_printf(fp, "%s = %s\n", pName, static_cast<const char*>(sIP));
}

// Enum overloads for CConfig::Write
static void WriteCfg(FIL *fp, const char *pName, CConfig::TSystemDefaultSynth v) { f_printf(fp, "%s = %s\n", pName, TSystemDefaultSynthStrings[static_cast<int>(v)]); }
static void WriteCfg(FIL *fp, const char *pName, CConfig::TAudioOutputDevice v)  { f_printf(fp, "%s = %s\n", pName, TAudioOutputDeviceStrings[static_cast<int>(v)]); }
static void WriteCfg(FIL *fp, const char *pName, CConfig::TMT32EmuResamplerQuality v) { f_printf(fp, "%s = %s\n", pName, TMT32EmuResamplerQualityStrings[static_cast<int>(v)]); }
static void WriteCfg(FIL *fp, const char *pName, CConfig::TMT32EmuMIDIChannels v) { f_printf(fp, "%s = %s\n", pName, TMT32EmuMIDIChannelsStrings[static_cast<int>(v)]); }
static void WriteCfg(FIL *fp, const char *pName, CConfig::TMT32EmuROMSet v)      { f_printf(fp, "%s = %s\n", pName, TMT32EmuROMSetStrings[static_cast<int>(v)]); }
static void WriteCfg(FIL *fp, const char *pName, CConfig::TLCDType v)           { f_printf(fp, "%s = %s\n", pName, TLCDTypeStrings[static_cast<int>(v)]); }
static void WriteCfg(FIL *fp, const char *pName, CConfig::TControlScheme v)     { f_printf(fp, "%s = %s\n", pName, TControlSchemeStrings[static_cast<int>(v)]); }
static void WriteCfg(FIL *fp, const char *pName, CConfig::TEncoderType v)       { f_printf(fp, "%s = %s\n", pName, TEncoderTypeStrings[static_cast<int>(v)]); }
static void WriteCfg(FIL *fp, const char *pName, CConfig::TLCDRotation v)       { f_printf(fp, "%s = %s\n", pName, TLCDRotationStrings[static_cast<int>(v)]); }
static void WriteCfg(FIL *fp, const char *pName, CConfig::TLCDMirror v)         { f_printf(fp, "%s = %s\n", pName, TLCDMirrorStrings[static_cast<int>(v)]); }
static void WriteCfg(FIL *fp, const char *pName, CConfig::TNetworkMode v)       { f_printf(fp, "%s = %s\n", pName, TNetworkModeStrings[static_cast<int>(v)]); }
static void WriteCfg(FIL *fp, const char *pName, CConfig::TMixerMode v)         { f_printf(fp, "%s = %s\n", pName, TMixerModeStrings[static_cast<int>(v)]); }
static void WriteCfg(FIL *fp, const char *pName, CConfig::TYmfmChip v)          { f_printf(fp, "%s = %s\n", pName, TYmfmChipStrings[static_cast<int>(v)]); }

bool CConfig::Write(const char* pPath)
{
	FIL fp;
	if (f_open(&fp, pPath, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
		return false;

	// 1. Write macro-defined sections from config.def
	#undef BEGIN_SECTION
	#undef CFG
	#undef END_SECTION
	
	#define BEGIN_SECTION(SECTION) f_printf(&fp, "[" #SECTION "]\n");
	
	// Call the overloaded WriteCfg based on the member's type
	#define CFG(INI_NAME, TYPE, MEMBER_NAME, ...) WriteCfg(&fp, #INI_NAME, MEMBER_NAME);

	#define END_SECTION f_printf(&fp, "\n");
	
	#include "config.def"

	#undef BEGIN_SECTION
	#undef CFG
	#undef END_SECTION

	// 2. Write manual sections
	f_printf(&fp, "[midi_cc_map]\n");
	static const char* const BindingKeys[] = {
		"main_reverb", "reverb_param1", "reverb_param2", "reverb_param3",
		"chorus_param1", "chorus_param2", "chorus_param3", "master_or_gain",
		"select_mt32", "select_soundfont", "prev_rom_or_soundfont", "next_rom_or_soundfont",
		"looper_arm_stop", "looper_bpm", "looper_quantize", "looper_save",
		"looper_metronome", "looper_clear", "sustain_cc64"
	};
	for (int i = 0; i < static_cast<int>(TMIDICCBindingID::Count); ++i)
	{
		if (MIDICCBindingCC[i] != -1)
			f_printf(&fp, "%s = %d\n", BindingKeys[i], MIDICCBindingCC[i]);
	}

	f_close(&fp);
	return true;
}

int CConfig::INIHandler(void* pUser, const char* pSection, const char* pName, const char* pValue)
{
	CConfig* const pConfig = static_cast<CConfig*>(pUser);

	if (strcasecmp(pSection, "midi_cc_map") == 0)
	{
		TMIDICCBindingID BindingID;
		int nCC;
		if (pConfig->ParseMIDICCBindingKey(pName, &BindingID) && pConfig->ParseMIDICCNumber(pValue, &nCC))
			pConfig->MIDICCBindingCC[static_cast<unsigned>(BindingID)] = nCC;

		pConfig->RebuildMIDICCMap();
		return 1;
	}

	//LOGDBG("'%s', '%s', '%s'", pSection, pName,  pValue);

	// Automatically generate ParseOption() calls from macro definition file
	#define BEGIN_SECTION(SECTION)       \
		if (!strcmp(#SECTION, pSection)) \
		{

	#define CFG(INI_NAME, TYPE, MEMBER_NAME, _2, ...) \
		if (!strcmp(#INI_NAME, pName))                \
		{                                             \
			if (!ParseOption(pValue, &pConfig->MEMBER_NAME __VA_OPT__(, ) __VA_ARGS__)) \
				LOGWARN("Failed to parse config option '%s' in section '%s'", pName, pSection); \
			return 1;                                 \
		}

	#define END_SECTION \
		return 1;       \
		}

	#include "config.def"

	return 1;
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

	if (strcasecmp(pValue, "sustain_cc64") == 0) return TMIDICCAction::SustainCC64;

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

	if (strcasecmp(pValue, "looper_arm_stop") == 0) return TMIDICCAction::LooperArmStop;
	if (strcasecmp(pValue, "looper_bpm") == 0) return TMIDICCAction::LooperBPM;
	if (strcasecmp(pValue, "looper_quantize") == 0) return TMIDICCAction::LooperQuantize;
	if (strcasecmp(pValue, "looper_save") == 0) return TMIDICCAction::LooperSave;
	if (strcasecmp(pValue, "looper_clear") == 0) return TMIDICCAction::LooperClear;
	if (strcasecmp(pValue, "looper_metronome") == 0) return TMIDICCAction::LooperMetronome;


	return TMIDICCAction::None;
}

void CConfig::RebuildMIDICCMap()
{
	for (unsigned i = 0; i < 128; ++i)
	{
		MIDICCMap[i].MT32Action = TMIDICCAction::None;
		MIDICCMap[i].SoundFontAction = TMIDICCAction::None;
	}

	bool Used[128] = {};

	auto Bind = [this, &Used](TMIDICCBindingID id, TMIDICCAction mt32, TMIDICCAction sf)
	{
		const int cc = MIDICCBindingCC[static_cast<unsigned>(id)];
		if (cc < 0 || cc > 127)
			return;

		if (Used[cc])
		{
			LOGWARN("Ignoring duplicate MIDI CC binding on CC %d", cc);
			return;
		}

		Used[cc] = true;
		MIDICCMap[cc].MT32Action = mt32;
		MIDICCMap[cc].SoundFontAction = sf;
	};

	Bind(TMIDICCBindingID::MainReverb,         TMIDICCAction::MainReverb,         TMIDICCAction::MainReverb);
	Bind(TMIDICCBindingID::ReverbParam1,       TMIDICCAction::MT32ReverbEnable,   TMIDICCAction::SFReverbRoomSize);
	Bind(TMIDICCBindingID::ReverbParam2,       TMIDICCAction::MT32MIDIDelayMode,  TMIDICCAction::SFReverbDamping);
	Bind(TMIDICCBindingID::ReverbParam3,       TMIDICCAction::MT32AnalogMode,     TMIDICCAction::SFReverbWidth);
	Bind(TMIDICCBindingID::ChorusParam1,       TMIDICCAction::MT32DACMode,        TMIDICCAction::SFChorusLevel);
	Bind(TMIDICCBindingID::ChorusParam2,       TMIDICCAction::MT32RendererType,   TMIDICCAction::SFChorusDepth);
	Bind(TMIDICCBindingID::ChorusParam3,       TMIDICCAction::MT32PartialCount,   TMIDICCAction::SFChorusSpeed);
	Bind(TMIDICCBindingID::MasterOrGain,       TMIDICCAction::MasterVolume,       TMIDICCAction::MasterVolume);

	Bind(TMIDICCBindingID::SelectMT32,         TMIDICCAction::SelectMT32,         TMIDICCAction::SelectMT32);
	Bind(TMIDICCBindingID::SelectSoundFont,    TMIDICCAction::SelectSoundFont,    TMIDICCAction::SelectSoundFont);
	Bind(TMIDICCBindingID::PrevRomOrSoundFont, TMIDICCAction::PrevRomOrSoundFont, TMIDICCAction::PrevRomOrSoundFont);
	Bind(TMIDICCBindingID::NextRomOrSoundFont, TMIDICCAction::NextRomOrSoundFont, TMIDICCAction::NextRomOrSoundFont);
	Bind(TMIDICCBindingID::SustainCC64,        TMIDICCAction::SustainCC64,        TMIDICCAction::SustainCC64);
	
	Bind(TMIDICCBindingID::LooperArmStop,     TMIDICCAction::LooperArmStop,      TMIDICCAction::LooperArmStop);
	Bind(TMIDICCBindingID::LooperBPM,         TMIDICCAction::LooperBPM,          TMIDICCAction::LooperBPM);
	Bind(TMIDICCBindingID::LooperQuantize,    TMIDICCAction::LooperQuantize,     TMIDICCAction::LooperQuantize);
	Bind(TMIDICCBindingID::LooperSave,        TMIDICCAction::LooperSave,         TMIDICCAction::LooperSave);
	Bind(TMIDICCBindingID::LooperMetronome,   TMIDICCAction::LooperMetronome,    TMIDICCAction::LooperMetronome);
	Bind(TMIDICCBindingID::LooperClear,       TMIDICCAction::LooperClear,        TMIDICCAction::LooperClear);
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

bool CConfig::ParseMIDICCNumber(const char* pValue, int* pOut)
{
	if (!pValue || !pOut)
		return false;

	char* pEnd = nullptr;
	const long nValue = std::strtol(pValue, &pEnd, 10);
	if (pEnd == pValue)
		return false;

	while (pEnd && *pEnd)
	{
		if (!std::isspace(static_cast<unsigned char>(*pEnd)))
			return false;
		++pEnd;
	}

	if (nValue < -1 || nValue > 127)
		return false;

	*pOut = static_cast<int>(nValue);
	return true;
}

bool CConfig::ParseMIDICCBindingKey(const char* pName, TMIDICCBindingID* pOut)
{
	if (!pName || !pOut)
		return false;

	if (strcasecmp(pName, "main_reverb") == 0)
		*pOut = TMIDICCBindingID::MainReverb;
	else if (strcasecmp(pName, "reverb_param1") == 0)
		*pOut = TMIDICCBindingID::ReverbParam1;
	else if (strcasecmp(pName, "reverb_param2") == 0)
		*pOut = TMIDICCBindingID::ReverbParam2;
	else if (strcasecmp(pName, "reverb_param3") == 0)
		*pOut = TMIDICCBindingID::ReverbParam3;
	else if (strcasecmp(pName, "chorus_param1") == 0)
		*pOut = TMIDICCBindingID::ChorusParam1;
	else if (strcasecmp(pName, "chorus_param2") == 0)
		*pOut = TMIDICCBindingID::ChorusParam2;
	else if (strcasecmp(pName, "chorus_param3") == 0)
		*pOut = TMIDICCBindingID::ChorusParam3;
	else if (strcasecmp(pName, "master_or_gain") == 0)
		*pOut = TMIDICCBindingID::MasterOrGain;
	else if (strcasecmp(pName, "select_mt32") == 0)
		*pOut = TMIDICCBindingID::SelectMT32;
	else if (strcasecmp(pName, "select_soundfont") == 0)
		*pOut = TMIDICCBindingID::SelectSoundFont;
	else if (strcasecmp(pName, "prev_rom_or_soundfont") == 0)
		*pOut = TMIDICCBindingID::PrevRomOrSoundFont;
	else if (strcasecmp(pName, "next_rom_or_soundfont") == 0)
		*pOut = TMIDICCBindingID::NextRomOrSoundFont;
	else if (strcasecmp(pName, "looper_arm_stop") == 0)
		*pOut = TMIDICCBindingID::LooperArmStop;
	else if (strcasecmp(pName, "looper_bpm") == 0)
		*pOut = TMIDICCBindingID::LooperBPM;
	else if (strcasecmp(pName, "looper_quantize") == 0)
		*pOut = TMIDICCBindingID::LooperQuantize;
	else if (strcasecmp(pName, "looper_save") == 0)
		*pOut = TMIDICCBindingID::LooperSave;
	else if (strcasecmp(pName, "looper_metronome") == 0)
		*pOut = TMIDICCBindingID::LooperMetronome;
	else if (strcasecmp(pName, "looper_clear") == 0)
		*pOut = TMIDICCBindingID::LooperClear;
	else if (strcasecmp(pName, "sustain_cc64") == 0)
		*pOut = TMIDICCBindingID::SustainCC64;
	else
		return false;

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
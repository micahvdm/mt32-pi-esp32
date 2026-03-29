// ui_menu.cpp — Encoder rotary menu for CUserInterface
// Compiled as a separate translation unit; included in the build via Makefile.
//
// IMPORTANT: mt32synth.h must come BEFORE soundfontsynth.h to avoid a
// placement-new exception-specifier conflict.
// mt32synth.h → mt32emu.h → <fstream> → <new> (with noexcept, first)
// soundfontsynth.h → optional.h → circle/new.h (without noexcept, second)
// If circle/new.h comes first, the STL <new> conflicts when it adds noexcept.

#include <circle/logger.h>

#include "lcd/ui.h"
#include <circle/serial.h>
#include "mt32pi.h"
#include "synth/mt32synth.h"
#include "synth/soundfontsynth.h"
#include "synth/ymfmsynth.h"
#include "utility.h"

#include <stdio.h>


// ---------------------------------------------------------------------------
// Helpers (file-static)
// ---------------------------------------------------------------------------
enum class TMenuLevel : u8
{
	Main,
	Synth,
	Mixer,
	Looper,
	MIDI,
	AudioFX,
	MIDICC,
	Sequencer,
	Recorder,
	Network,
	System
};

static TMenuLevel s_nMenuLevel = TMenuLevel::Main;
static size_t s_nMenuMainCursor = 0;
static size_t s_nMenuSubCursor = 0;

static constexpr size_t MenuVisibleRows = 16;
static constexpr size_t MixerMenuItems  = 7;

static size_t GetMenuItemCount(TMenuLevel Level, const CSynthBase* pCurrent, CSoundFontSynth* pSF, CMT32Synth* pMT32);
static const char* GetMixerMenuItemLabel(size_t nMixerIdx);
static const char* GetMenuItemLabel(TMenuLevel Level, const CSynthBase* pCurrent, CSoundFontSynth* pSF, CMT32Synth* pMT32, size_t nItem);
static void FormatMenuValue(char* pBuf, size_t nBufSize, const CSynthBase* pCurrent, CSoundFontSynth* pSF, CMT32Synth* pMT32, size_t nItem, float fGain, bool bReverbActive, float fReverbRoomSize, float fReverbLevel, float fReverbDamping, float fReverbWidth, bool bChorusActive, float fChorusDepth, float fChorusLevel, int nChorusVoices, float fChorusSpeed, int nROMSet, int nSoundFont, float fMT32Gain, float fMT32ReverbGain, bool bMT32ReverbEnabled, bool bMT32NiceAmp, bool bMT32NicePan, bool bMT32NiceMix, int nMT32DACMode, int nMT32MIDIDelay, int nMT32AnalogMode, int nMT32RendererType, int nMT32PartialCount);

static size_t GetMenuItemCount(TMenuLevel Level, const CSynthBase* pCurrent,
                               CSoundFontSynth* pSF, CMT32Synth* pMT32)
{
	switch (Level)
	{
	case TMenuLevel::Main:    return 11;
	case TMenuLevel::Mixer:   return MixerMenuItems;
	case TMenuLevel::Looper:  return 7;
	case TMenuLevel::MIDI:    return 3;
	case TMenuLevel::AudioFX: return 8;
	case TMenuLevel::MIDICC:  return 14;
	case TMenuLevel::Sequencer: return 7;
	case TMenuLevel::Recorder:  return 2;
	case TMenuLevel::Network: return 5;
	case TMenuLevel::System:  return 7;
	case TMenuLevel::Synth:
		if (pCurrent == pSF && pSF)    return 14;
		if (pCurrent == pMT32 && pMT32) return 12;
		if (pCurrent && pCurrent->GetType() == TSynth::Ymfm) return 3;
		return 0;
	default: return 0;
	}
}

static const char* GetMixerMenuItemLabel(size_t nMixerIdx)
{
	static const char* mixerLabels[] =
		{ "Mixer", "Preset", "MT32 Vol", "MT32 Pan", "Fluid Vol", "Fluid Pan", "OPL3 Vol" };
	return (nMixerIdx < MixerMenuItems) ? mixerLabels[nMixerIdx] : nullptr;
}

static const char* GetMenuItemLabel(TMenuLevel Level, const CSynthBase* pCurrent,
                                    CSoundFontSynth* pSF, CMT32Synth* pMT32,
                                    size_t nItem)
{
	if (Level == TMenuLevel::Synth)
	{
		if (pCurrent == pSF && pSF)
	{
		static const char* sfLabels[] =
			{ "SoundFont", "Gain",
			  "Reverb", "Rev.Room", "Rev.Level", "Rev.Damp", "Rev.Width",
			  "Chorus", "Cho.Depth", "Cho.Level", "Cho.Voices", "Cho.Speed",
			  "Tuning", "Polyphony" };
		return (nItem < 14) ? sfLabels[nItem] : nullptr;
	}
		if (pCurrent == pMT32 && pMT32)
	{
		static const char* mt32Labels[] = {
			"ROM Set", "Gain", "Rev.Gain",
			"Reverb", "NiceAmp", "NicePan",
			"NiceMix", "DAC", "MIDIDelay",
				"Analog", "Render", "Partials"
		};
		return (nItem < 12) ? mt32Labels[nItem] : nullptr;
	}
		if (pCurrent && pCurrent->GetType() == TSynth::Ymfm)
	{
		static const char* opl3Labels[] = { "Bank", "Chip", "Volume" };
		return (nItem < 3) ? opl3Labels[nItem] : nullptr;
	}
	}

	switch (Level)
	{
	case TMenuLevel::Main:
	{
		static const char* mainLabels[] = { "Active Synth", "Synth FX", "Mixer", "Audio FX", "Looper", "Sequencer", "Recorder", "Network", "System", "Reboot Pi", "Exit" };
		return (nItem < 11) ? mainLabels[nItem] : nullptr;
	}
	case TMenuLevel::Mixer:   return GetMixerMenuItemLabel(nItem);
	case TMenuLevel::Looper:
	{
		static const char* looperLabels[] = { "Status", "BPM", "Quantize", "Metronome", "Gain", "Clear Loop", "Save Loop" };
		return (nItem < 7) ? looperLabels[nItem] : nullptr;
	}
	case TMenuLevel::MIDI:    { static const char* labels[] = { "MIDI Thru", "GPIO Baud", "USB Baud" }; return (nItem < 3) ? labels[nItem] : nullptr; }
	case TMenuLevel::AudioFX:
	{
		static const char* labels[] = { "EQ", "Bass", "Treble", "Limiter", "Reverb", "Rev.Room", "Rev.Damp", "Rev.Wet" };
		return (nItem < 8) ? labels[nItem] : nullptr;
	}
	case TMenuLevel::MIDICC:
	{
		static const char* labels[] = {
			"Main Reverb", "Rev Param 1", "Rev Param 2", "Rev Param 3",
			"Cho Param 1", "Cho Param 2", "Cho Param 3", "Master/Gain",
			"Select MT32", "Select SF", "Prev ROM/SF", "Next ROM/SF",
			"Looper Arm", "Sustain CC64"
		};
		return (nItem < 14) ? labels[nItem] : nullptr;
	}
	case TMenuLevel::Recorder:
	{
		static const char* labels[] = { "Status", "Control" };
		return (nItem < 2) ? labels[nItem] : nullptr;
	}
	case TMenuLevel::Sequencer:
	{
		static const char* labels[] = { "Status", "Control", "Next File", "Prev File", "Loop", "Auto-Next", "Speed" };
		return (nItem < 7) ? labels[nItem] : nullptr;
	}
	case TMenuLevel::Network: { static const char* labels[] = { "IP Addr", "Host", "Status", "Mode", "DHCP" }; return (nItem < 5) ? labels[nItem] : nullptr; }
	case TMenuLevel::System:  { static const char* labels[] = { "Panic", "MIDI Settings", "MIDI CC Map", "Verbose", "USB", "I2C Baud", "Power Save" }; return (nItem < 7) ? labels[nItem] : nullptr; }
	default: return nullptr;
	}
}

// Formatted value string for item nItem
static void FormatMenuValue(char* pBuf, size_t nBufSize,
                             const CSynthBase* pCurrent,
                             CSoundFontSynth* pSF,
                             CMT32Synth* pMT32,
                             size_t nItem,
							float fGain,
                             bool bReverbActive, float fReverbRoomSize,
							float fReverbLevel,
							float fReverbDamping,
							float fReverbWidth,
							bool bChorusActive, float fChorusDepth,
							float fChorusLevel, int nChorusVoices,
							float fChorusSpeed,
							int nROMSet, int /*nSoundFont*/,
							float fMT32Gain, float fMT32ReverbGain,
							bool bMT32ReverbEnabled,
							bool bMT32NiceAmp, bool bMT32NicePan, bool bMT32NiceMix,
							int nMT32DACMode, int nMT32MIDIDelay,
							int nMT32AnalogMode, int nMT32RendererType,
							int nMT32PartialCount)
{
	(void)nROMSet; (void)nMT32DACMode; (void)nMT32MIDIDelay; (void)nMT32AnalogMode; (void)nMT32RendererType; // Silence unused variable warnings
	pBuf[0] = '\0';

	// Logic remains valid but shared across submenu renders
	if (pCurrent == pSF && pSF)
	{
		if (nItem >= 12) return;
		switch (nItem)
		{
		case 0:
		{
			const size_t nIdx   = pSF->GetSoundFontIndex();
			const size_t nTotal = pSF->GetSoundFontManager().GetSoundFontCount();
			snprintf(pBuf, nBufSize, "%zu/%zu", nIdx + 1, nTotal);
			break;
		}
		case 1:  snprintf(pBuf, nBufSize, "%.2f", static_cast<double>(fGain));            break;
		case 2:  snprintf(pBuf, nBufSize, "%s",   bReverbActive   ? "ON" : "OFF");      break;
		case 3:  snprintf(pBuf, nBufSize, "%.1f", static_cast<double>(fReverbRoomSize));  break;
		case 4:  snprintf(pBuf, nBufSize, "%.1f", static_cast<double>(fReverbLevel));     break;
		case 5:  snprintf(pBuf, nBufSize, "%.1f", static_cast<double>(fReverbDamping));   break;
		case 6:  snprintf(pBuf, nBufSize, "%.1f", static_cast<double>(fReverbWidth));     break;
		case 7:  snprintf(pBuf, nBufSize, "%s",   bChorusActive   ? "ON" : "OFF");      break;
		case 8:  snprintf(pBuf, nBufSize, "%.1f", static_cast<double>(fChorusDepth));     break;
		case 9:  snprintf(pBuf, nBufSize, "%.1f", static_cast<double>(fChorusLevel));     break;
		case 10: snprintf(pBuf, nBufSize, "%d",   nChorusVoices);                         break;
		case 11: snprintf(pBuf, nBufSize, "%.2f", static_cast<double>(fChorusSpeed));     break;
		default: break;
		}
	}
	else if (pCurrent == pMT32 && pMT32)
	{
		if (nItem >= 12) return;
		switch (nItem)
		{
		case 0:
		{
			static const char* romNames[] = { "MT32old", "MT32new", "CM-32L" };
			const char* pName = (nROMSet >= 0 && nROMSet < 3) ? romNames[nROMSet] : "?";
			snprintf(pBuf, nBufSize, "%s", pName);
			break;
		}
		case 1:  snprintf(pBuf, nBufSize, "%.2f", static_cast<double>(fMT32Gain));       break;
		case 2:  snprintf(pBuf, nBufSize, "%.2f", static_cast<double>(fMT32ReverbGain)); break;
		case 3:  snprintf(pBuf, nBufSize, "%s",   bMT32ReverbEnabled ? "ON" : "OFF");   break;
		case 4:  snprintf(pBuf, nBufSize, "%s",   bMT32NiceAmp ? "ON" : "OFF");         break;
		case 5:  snprintf(pBuf, nBufSize, "%s",   bMT32NicePan ? "ON" : "OFF");         break;
		case 6:  snprintf(pBuf, nBufSize, "%s",   bMT32NiceMix ? "ON" : "OFF");         break;
		case 7:
		{
			static const char* dacNames[] = { "NICE", "PURE", "GEN1", "GEN2" };
			snprintf(pBuf, nBufSize, "%s", (nMT32DACMode >= 0 && nMT32DACMode < 4) ? dacNames[nMT32DACMode] : "?");
			break;
		}
		case 8:
		{
			static const char* delayNames[] = { "IMMD", "SHORT", "ALL" };
			snprintf(pBuf, nBufSize, "%s", (nMT32MIDIDelay >= 0 && nMT32MIDIDelay < 3) ? delayNames[nMT32MIDIDelay] : "?");
			break;
		}
		case 9:
		{
			static const char* analogNames[] = { "DIG", "COARSE", "ACCUR", "OVR" };
			snprintf(pBuf, nBufSize, "%s", (nMT32AnalogMode >= 0 && nMT32AnalogMode < 4) ? analogNames[nMT32AnalogMode] : "?");
			break;
		}
		case 10:
		{
			static const char* rendererNames[] = { "I16", "F32" };
			snprintf(pBuf, nBufSize, "%s", (nMT32RendererType >= 0 && nMT32RendererType < 2) ? rendererNames[nMT32RendererType] : "?");
			break;
		}
		case 11:
			snprintf(pBuf, nBufSize, "%d", nMT32PartialCount);
			break;
		default: break;
		}
	}
}

// ---------------------------------------------------------------------------
// CUserInterface menu methods
// ---------------------------------------------------------------------------

void CUserInterface::EnterMenu(CSoundFontSynth* pSF, CMT32Synth* pMT32,
                               CSynthBase* pCurrent, CMT32Pi* pMT32Pi,
                               CYmfmSynth* pYmfm)
{
	m_pMenuSF           = pSF;
	m_pMenuMT32         = pMT32;
	m_pMenuYmfm         = pYmfm;
	m_pMenuCurrentSynth = pCurrent;
	m_pMenuMT32Pi       = pMT32Pi;
	m_nMenuCursor       = 0;
	m_nMenuScroll       = 0;
	s_nMenuLevel        = TMenuLevel::Main;
	s_nMenuMainCursor   = 0;
	m_bMenuEditing      = false;

	// Snapshot current values from active synth
	if (pCurrent == pSF && pSF)
	{
		m_fMenuGain           = pSF->GetGain();
		m_fMenuReverbDamping  = pSF->GetReverbDamping();
		m_fMenuReverbRoomSize = pSF->GetReverbRoomSize();
		m_fMenuReverbLevel    = pSF->GetReverbLevel();
		m_fMenuReverbWidth    = pSF->GetReverbWidth();
		m_bMenuChorusActive   = pSF->GetChorusActive();
		m_fMenuChorusDepth    = pSF->GetChorusDepth();
		m_fMenuChorusLevel    = pSF->GetChorusLevel();
		m_nMenuChorusVoices   = pSF->GetChorusVoices();
		m_fMenuChorusSpeed    = pSF->GetChorusSpeed();
		m_bMenuReverbActive   = pSF->GetReverbActive();
		m_nMenuSoundFont      = static_cast<int>(pSF->GetSoundFontIndex());
		m_nMenuROMSet         = 0;
	}
	else if (pCurrent == pMT32 && pMT32)
	{
		m_fMenuGain           = 0.0f;
		m_bMenuReverbActive   = false;
		m_fMenuReverbDamping  = 0.0f;
		m_fMenuReverbRoomSize = 0.0f;
		m_fMenuReverbLevel    = 0.0f;
		m_fMenuReverbWidth    = 0.0f;
		m_bMenuChorusActive   = false;
		m_fMenuChorusDepth    = 0.0f;
		m_fMenuChorusLevel    = 0.0f;
		m_nMenuChorusVoices   = 0;
		m_fMenuChorusSpeed    = 0.0f;
		m_nMenuSoundFont      = 0;
		m_nMenuROMSet         = static_cast<int>(pMT32->GetROMSet());
		m_fMenuMT32Gain          = pMT32->GetOutputGain();
		m_fMenuMT32ReverbGain    = pMT32->GetReverbOutputGain();
		m_bMenuMT32ReverbEnabled = pMT32->GetReverbEnabled();
		m_bMenuMT32NiceAmpRamp   = pMT32->GetNiceAmpRamp();
		m_bMenuMT32NicePanning   = pMT32->GetNicePanning();
		m_bMenuMT32NicePartMix   = pMT32->GetNicePartialMixing();
		m_nMenuMT32DACMode       = static_cast<int>(pMT32->GetDACInputMode());
		m_nMenuMT32MIDIDelay     = static_cast<int>(pMT32->GetMIDIDelayMode());
		m_nMenuMT32AnalogMode    = static_cast<int>(pMT32->GetAnalogOutputMode());
		m_nMenuMT32RendererType  = static_cast<int>(pMT32->GetRendererType());
		m_nMenuMT32PartialCount  = static_cast<int>(pMT32->GetPartialCount());
	}
	else if (pCurrent == pYmfm && pYmfm)
	{
		m_nMenuYmfmVol = pMT32Pi ? pMT32Pi->GetMasterVolume() : 100;
	}

	// Initialize mixer values
	if (pMT32Pi)
	{
		const auto ms = pMT32Pi->GetMixerStatus();
		m_bMenuMixerEnabled  = ms.bEnabled;
		m_nMenuMixerPreset   = ms.nPreset;
		m_nMenuMixerMT32Vol  = static_cast<int>(ms.fMT32Volume * 100.0f + 0.5f);
		m_nMenuMixerFluidVol = static_cast<int>(ms.fFluidVolume * 100.0f + 0.5f);
		m_nMenuMixerYmfmVol  = static_cast<int>(ms.fYmfmVolume * 100.0f + 0.5f);
	}

	m_State = TState::InMenu;
}

void CUserInterface::ExitMenu()
{
	m_State = TState::None;
}

bool CUserInterface::MenuEncoderEvent(s8 nDelta)
{
	if (m_State != TState::InMenu)
		return false;

	const size_t nItems = GetMenuItemCount(s_nMenuLevel, m_pMenuCurrentSynth, m_pMenuSF, m_pMenuMT32);
	if (nItems == 0)
		return true;

	if (m_bMenuEditing)
	{
		if (s_nMenuLevel == TMenuLevel::Mixer && m_pMenuMT32Pi)
		{
			switch (m_nMenuCursor)
			{
			case 0: // Mixer ON/OFF
				m_bMenuMixerEnabled = !m_bMenuMixerEnabled;
				m_pMenuMT32Pi->SetMixerEnabled(m_bMenuMixerEnabled);
				break;
			case 1: // Preset (0-3)
				m_nMenuMixerPreset = (m_nMenuMixerPreset + nDelta + 4) % 4;
				m_pMenuMT32Pi->SetMixerPreset(m_nMenuMixerPreset);
				break;
			case 2: // MT32 Vol [0-100]
				m_nMenuMixerMT32Vol = Utility::Clamp(m_nMenuMixerMT32Vol + nDelta, 0, 100);
				m_pMenuMT32Pi->SetMixerEngineVolume("mt32", m_nMenuMixerMT32Vol);
				break;
			case 3: // MT32 Pan
				m_pMenuMT32Pi->SetMixerEnginePan("mt32", Utility::Clamp(static_cast<int>(m_pMenuMT32Pi->GetMixerStatus().fMT32Pan * 100.0f) + nDelta, -100, 100));
				break;
			case 4: // Fluid Vol [0-100]
				m_nMenuMixerFluidVol = Utility::Clamp(m_nMenuMixerFluidVol + nDelta, 0, 100);
				m_pMenuMT32Pi->SetMixerEngineVolume("fluidsynth", m_nMenuMixerFluidVol);
				break;
			case 5: // Fluid Pan
				m_pMenuMT32Pi->SetMixerEnginePan("fluidsynth", Utility::Clamp(static_cast<int>(m_pMenuMT32Pi->GetMixerStatus().fFluidPan * 100.0f) + nDelta, -100, 100));
				break;
			case 6: // OPL3 Vol [0-100]
				m_nMenuMixerYmfmVol = Utility::Clamp(m_nMenuMixerYmfmVol + nDelta, 0, 100);
				m_pMenuMT32Pi->SetMixerEngineVolume("ymfm", m_nMenuMixerYmfmVol);
				break;
			default: break;
			}
		}
		else if (s_nMenuLevel == TMenuLevel::Synth && m_pMenuYmfm && m_pMenuCurrentSynth == m_pMenuYmfm && m_pMenuMT32Pi)
		{
			switch (m_nMenuCursor)
			{
			case 0: // Bank — cycle through scanned WOPL banks
			{
				const size_t nBanks = m_pMenuYmfm->GetBankManager().GetBankCount();
				if (nBanks > 0)
				{
					const size_t nNext = static_cast<size_t>(
						(static_cast<int>(m_pMenuYmfm->GetCurrentBankIndex()) + nDelta
						 + static_cast<int>(nBanks)) % static_cast<int>(nBanks));
					m_pMenuYmfm->SwitchBank(nNext);
				}
				break;
			}
			case 1: // Chip — toggle OPL2/OPL3
				if (nDelta != 0)
				{
					const TOplChipMode eCurrent = m_pMenuYmfm->GetChipMode();
					const TOplChipMode eNew = (eCurrent == TOplChipMode::OPL3)
						? TOplChipMode::OPL2 : TOplChipMode::OPL3;
					m_pMenuYmfm->SetChipMode(eNew);
				}
				break;
			case 2: // Volume 0-100
				m_nMenuYmfmVol = Utility::Clamp(m_nMenuYmfmVol + nDelta, 0, 100);
				m_pMenuMT32Pi->SetMasterVolumePercent(m_nMenuYmfmVol);
				break;
			default: break;
			}
		}
		else if (s_nMenuLevel == TMenuLevel::Synth && m_pMenuCurrentSynth == m_pMenuSF && m_pMenuSF)
		{
			switch (m_nMenuCursor)
			{
			case 0: // SoundFont index
			{
				const size_t nCount = m_pMenuSF->GetSoundFontManager().GetSoundFontCount();
				if (nCount > 0)
				{
					m_nMenuSoundFont = static_cast<int>(
						(m_nMenuSoundFont + nDelta + static_cast<int>(nCount))
						% static_cast<int>(nCount));
					m_pMenuSF->SwitchSoundFont(static_cast<size_t>(m_nMenuSoundFont));
				}
				break;
			}
			case 1: // Gain [0.0 – 5.0]
				m_fMenuGain = Utility::Clamp(m_fMenuGain + nDelta * 0.05f, 0.0f, 5.0f);
				m_pMenuSF->SetGain(m_fMenuGain);
				break;
			case 2: // Reverb ON/OFF
				m_bMenuReverbActive = !m_bMenuReverbActive;
				m_pMenuSF->SetReverbActive(m_bMenuReverbActive);
				break;
			case 3: // Reverb room size  [0.0 – 1.0]
				m_fMenuReverbRoomSize = Utility::Clamp(
					m_fMenuReverbRoomSize + nDelta * 0.1f, 0.0f, 1.0f);
				m_pMenuSF->SetReverbRoomSize(m_fMenuReverbRoomSize);
				break;
			case 4: // Reverb level      [0.0 – 1.0]
				m_fMenuReverbLevel = Utility::Clamp(
					m_fMenuReverbLevel + nDelta * 0.1f, 0.0f, 1.0f);
				m_pMenuSF->SetReverbLevel(m_fMenuReverbLevel);
				break;
			case 5: // Reverb damping    [0.0 – 1.0]
				m_fMenuReverbDamping = Utility::Clamp(
					m_fMenuReverbDamping + nDelta * 0.1f, 0.0f, 1.0f);
				m_pMenuSF->SetReverbDamping(m_fMenuReverbDamping);
				break;
			case 6: // Reverb width      [0.0 – 100.0]
				m_fMenuReverbWidth = Utility::Clamp(
					m_fMenuReverbWidth + nDelta * 1.0f, 0.0f, 100.0f);
				m_pMenuSF->SetReverbWidth(m_fMenuReverbWidth);
				break;
			case 7: // Chorus ON/OFF
				m_bMenuChorusActive = !m_bMenuChorusActive;
				m_pMenuSF->SetChorusActive(m_bMenuChorusActive);
				break;
			case 8: // Chorus depth      [0.0 – 20.0]
				m_fMenuChorusDepth = Utility::Clamp(
					m_fMenuChorusDepth + nDelta * 0.5f, 0.0f, 20.0f);
				m_pMenuSF->SetChorusDepth(m_fMenuChorusDepth);
				break;
			case 9: // Chorus level      [0.0 – 10.0]
				m_fMenuChorusLevel = Utility::Clamp(
					m_fMenuChorusLevel + nDelta * 0.1f, 0.0f, 10.0f);
				m_pMenuSF->SetChorusLevel(m_fMenuChorusLevel);
				break;
			case 10: // Chorus voices     [0 – 99]
				m_nMenuChorusVoices = Utility::Clamp(m_nMenuChorusVoices + nDelta, 0, 99);
				m_pMenuSF->SetChorusVoices(m_nMenuChorusVoices);
				break;
			case 11: // Chorus speed      [0.01 – 5.0]
				m_fMenuChorusSpeed = Utility::Clamp(
					m_fMenuChorusSpeed + nDelta * 0.05f, 0.01f, 5.0f);
				m_pMenuSF->SetChorusSpeed(m_fMenuChorusSpeed);
				break;
			case 12: // Tuning
				m_pMenuSF->SetTuning((m_pMenuSF->GetTuning() + nDelta + 7) % 7);
				break;
			case 13: // Polyphony
				m_pMenuSF->SetPolyphony(Utility::Clamp(m_pMenuSF->GetPolyphony() + nDelta * 10, 1, 512));
				break;
			default:
				break;
			}
		}
		else if (s_nMenuLevel == TMenuLevel::Synth && m_pMenuCurrentSynth == m_pMenuMT32 && m_pMenuMT32)
		{
			switch (m_nMenuCursor)
			{
			case 0: // ROM set (3 options)
				m_nMenuROMSet = (m_nMenuROMSet + nDelta + 3) % 3;
				m_pMenuMT32->SwitchROMSet(static_cast<TMT32ROMSet>(m_nMenuROMSet));
				break;
			case 1: // Gain [0.0 – 5.0]
				m_fMenuMT32Gain = Utility::Clamp(m_fMenuMT32Gain + nDelta * 0.1f, 0.0f, 5.0f);
				m_pMenuMT32->SetOutputGain(m_fMenuMT32Gain);
				break;
			case 2: // Reverb gain [0.0 – 5.0]
				m_fMenuMT32ReverbGain = Utility::Clamp(m_fMenuMT32ReverbGain + nDelta * 0.1f, 0.0f, 5.0f);
				m_pMenuMT32->SetReverbOutputGain(m_fMenuMT32ReverbGain);
				break;
			case 3: // Reverb ON/OFF
				m_bMenuMT32ReverbEnabled = !m_bMenuMT32ReverbEnabled;
				m_pMenuMT32->SetReverbEnabled(m_bMenuMT32ReverbEnabled);
				break;
			case 4: // NiceAmp ON/OFF
				m_bMenuMT32NiceAmpRamp = !m_bMenuMT32NiceAmpRamp;
				m_pMenuMT32->SetNiceAmpRamp(m_bMenuMT32NiceAmpRamp);
				break;
			case 5: // NicePan ON/OFF
				m_bMenuMT32NicePanning = !m_bMenuMT32NicePanning;
				m_pMenuMT32->SetNicePanning(m_bMenuMT32NicePanning);
				break;
			case 6: // NiceMix ON/OFF
				m_bMenuMT32NicePartMix = !m_bMenuMT32NicePartMix;
				m_pMenuMT32->SetNicePartialMixing(m_bMenuMT32NicePartMix);
				break;
			case 7: // DAC mode (4 options)
				m_nMenuMT32DACMode = (m_nMenuMT32DACMode + nDelta + 4) % 4;
				m_pMenuMT32->SetDACInputMode(static_cast<MT32Emu::DACInputMode>(m_nMenuMT32DACMode));
				break;
			case 8: // MIDI delay (3 options)
				m_nMenuMT32MIDIDelay = (m_nMenuMT32MIDIDelay + nDelta + 3) % 3;
				m_pMenuMT32->SetMIDIDelayMode(static_cast<MT32Emu::MIDIDelayMode>(m_nMenuMT32MIDIDelay));
				break;
			case 9: // Analog mode (4 options)
				m_nMenuMT32AnalogMode = (m_nMenuMT32AnalogMode + nDelta + 4) % 4;
				m_pMenuMT32->SetAnalogOutputMode(static_cast<MT32Emu::AnalogOutputMode>(m_nMenuMT32AnalogMode));
				break;
			case 10: // Renderer type (2 options)
				m_nMenuMT32RendererType = (m_nMenuMT32RendererType + nDelta + 2) % 2;
				m_pMenuMT32->SetRendererType(static_cast<MT32Emu::RendererType>(m_nMenuMT32RendererType));
				break;
			case 11: // Partial count [8 - 256]
				m_nMenuMT32PartialCount = Utility::Clamp(m_nMenuMT32PartialCount + nDelta, 8, 256);
				m_pMenuMT32->SetPartialCount(static_cast<u32>(m_nMenuMT32PartialCount));
				break;
			default:
				break;
			}
		}
		else if (s_nMenuLevel == TMenuLevel::Main)
		{
			if (m_nMenuCursor == 0) // Switch active synth
			{
				const TSynth eCurrent = m_pMenuCurrentSynth->GetType();
				const TSynth eNew = (eCurrent == TSynth::MT32) ? TSynth::SoundFont :
				                    (eCurrent == TSynth::SoundFont) ? TSynth::Ymfm : TSynth::MT32;
				m_pMenuMT32Pi->SetActiveSynth(eNew);
				ExitMenu(); // Exit to allow main logic to refresh synth pointers
			}
		}
		else if (s_nMenuLevel == TMenuLevel::Looper)
		{
			switch (m_nMenuCursor)
			{
			case 1: m_pMenuMT32Pi->LooperSetBPM(m_pMenuMT32Pi->GetLooperStatus().nBPM + nDelta); break;
			case 2:
			{
				static const int q[] = { 4, 8, 16, 32 };
				int cur = m_pMenuMT32Pi->GetLooperStatus().nQuantize;
				int idx = 0; for (int i = 0; i < 4; i++) if (q[i] == cur) idx = i;
				m_pMenuMT32Pi->LooperSetQuantize(q[(idx + nDelta + 4) % 4]);
				break;
			}
			case 3: m_pMenuMT32Pi->LooperSetMetronomeEnabled(!m_pMenuMT32Pi->GetLooperStatus().bMetronomeEnabled); break;
			case 4: m_pMenuMT32Pi->LooperSetPlaybackGain(Utility::Clamp(m_pMenuMT32Pi->GetLooperStatus().fPlaybackGain + nDelta * 0.05f, 0.0f, 1.0f)); break;
			default: break;
			}
		}
		else if (s_nMenuLevel == TMenuLevel::Sequencer)
		{
			switch (m_nMenuCursor)
			{
			case 4: m_pMenuMT32Pi->SetSequencerLoop(!m_pMenuMT32Pi->GetSequencerStatus().bLoopEnabled); break;
			case 5: m_pMenuMT32Pi->SetSequencerAutoNext(!m_pMenuMT32Pi->GetSequencerStatus().bAutoNext); break;
			case 6: m_pMenuMT32Pi->SequencerSetTempoMultiplier(Utility::Clamp(m_pMenuMT32Pi->GetSequencerStatus().nTempoMultiplier + nDelta * 0.1, 0.1, 4.0)); break;
			}
		}
		else if (s_nMenuLevel == TMenuLevel::MIDI)
		{
			CConfig* pCfg = const_cast<CConfig*>(m_pMenuMT32Pi->GetConfig());
			switch (m_nMenuCursor)
			{
			case 0: m_pMenuMT32Pi->SetMIDIThruEnabled(!m_pMenuMT32Pi->GetMixerStatus().bMIDIThruEnabled); break;
			case 1: pCfg->MIDIGPIOBaudRate = Utility::Clamp(pCfg->MIDIGPIOBaudRate + nDelta * 100, 300, 4000000); break;
			case 2: pCfg->MIDIUSBSerialBaudRate = Utility::Clamp(pCfg->MIDIUSBSerialBaudRate + nDelta * 100, 9600, 115200); break;
			}
		}
		else if (s_nMenuLevel == TMenuLevel::AudioFX)
		{
			const auto ms = m_pMenuMT32Pi->GetMixerStatus();
			switch (m_nMenuCursor)
			{
			case 0: m_pMenuMT32Pi->SetEffectEQEnabled(!ms.bEffectsEQEnabled); break;
			case 1: m_pMenuMT32Pi->SetEffectEQBass(Utility::Clamp(ms.nEffectsEQBass + nDelta, -12, 12)); break;
			case 2: m_pMenuMT32Pi->SetEffectEQTreble(Utility::Clamp(ms.nEffectsEQTreble + nDelta, -12, 12)); break;
			case 3: m_pMenuMT32Pi->SetEffectLimiterEnabled(!ms.bEffectsLimiterEnabled); break;
			case 4: m_pMenuMT32Pi->SetEffectReverbEnabled(!ms.bEffectsReverbEnabled); break;
			case 5: m_pMenuMT32Pi->SetEffectReverbRoom(Utility::Clamp(ms.nEffectsReverbRoom + nDelta, 0, 100)); break;
			case 6: m_pMenuMT32Pi->SetEffectReverbDamp(Utility::Clamp(ms.nEffectsReverbDamp + nDelta, 0, 100)); break;
			case 7: m_pMenuMT32Pi->SetEffectReverbWet(Utility::Clamp(ms.nEffectsReverbWet + nDelta, 0, 100)); break;
			}
		}
		else if (s_nMenuLevel == TMenuLevel::MIDICC)
		{
			CConfig* pCfg = const_cast<CConfig*>(m_pMenuMT32Pi->GetConfig());
			int bindingIdx = m_nMenuCursor;
			// Handle the mapping mismatch for Sustain CC64 (which is at index 18 in the binding enum)
			if (bindingIdx == 13) bindingIdx = 18;

			if (bindingIdx < static_cast<int>(CConfig::TMIDICCBindingID::Count))
			{
				int cc = pCfg->MIDICCBindingCC[bindingIdx];

				// CC -1 is disabled
				if (cc == -1 && nDelta < 0) cc = 127;
				else if (cc == 127 && nDelta > 0) cc = -1;
				else cc = Utility::Clamp(cc + nDelta, -1, 127);

				pCfg->MIDICCBindingCC[bindingIdx] = cc;

				// Rebuild internal map so it takes effect immediately
				pCfg->RebuildMIDICCMap();
			}
		}
		else if (s_nMenuLevel == TMenuLevel::Network)
		{
			CConfig* pCfg = const_cast<CConfig*>(m_pMenuMT32Pi->GetConfig());
			switch (m_nMenuCursor)
			{
			case 1:
			{
				int m = static_cast<int>(pCfg->NetworkMode);
				pCfg->NetworkMode = static_cast<CConfig::TNetworkMode>((m + nDelta + 3) % 3);
				break;
			}
			case 2: pCfg->NetworkDHCP = !pCfg->NetworkDHCP; break;
			}
		}
		else if (s_nMenuLevel == TMenuLevel::System)
		{
			CConfig* pCfg = const_cast<CConfig*>(m_pMenuMT32Pi->GetConfig());
			switch (m_nMenuCursor)
			{
			case 3: pCfg->SystemVerbose = !pCfg->SystemVerbose; break;
			case 4: pCfg->SystemUSB = !pCfg->SystemUSB; break;
			case 5:
			{
				static const int speeds[] = { 100000, 400000, 1000000 };
				int cur = pCfg->SystemI2CBaudRate;
				int idx = 1; for (int i = 0; i < 3; i++) if (speeds[i] == cur) idx = i;
				pCfg->SystemI2CBaudRate = speeds[(idx + nDelta + 3) % 3];
				break;
			}
			case 6: pCfg->SystemPowerSaveTimeout = Utility::Clamp(pCfg->SystemPowerSaveTimeout + nDelta * 10, 0, 3600); break;
			}
		}

		// Snapshot current values again to reflect immediate changes in the UI formatting
		if (s_nMenuLevel != TMenuLevel::Main)
		{
			if (m_pMenuCurrentSynth == m_pMenuSF && m_pMenuSF)
			{
				m_fMenuGain = m_pMenuSF->GetGain();
				m_bMenuReverbActive = m_pMenuSF->GetReverbActive();
			}
			else if (m_pMenuCurrentSynth == m_pMenuMT32 && m_pMenuMT32)
			{
				m_fMenuMT32Gain = m_pMenuMT32->GetOutputGain();
				m_fMenuMT32ReverbGain = m_pMenuMT32->GetReverbOutputGain();
			}
			if (m_pMenuMT32Pi)
			{
				const auto ms = m_pMenuMT32Pi->GetMixerStatus();
				m_bMenuMixerEnabled = ms.bEnabled;
				m_nMenuMixerMT32Vol = static_cast<int>(ms.fMT32Volume * 100.0f + 0.5f);
			}
		}
	}
	else
	{
		// Navigation mode: move cursor
		if (nDelta > 0)
		{
			if (m_nMenuCursor + 1 < nItems)
			{
				++m_nMenuCursor;
				if (m_nMenuCursor >= m_nMenuScroll + MenuVisibleRows)
					m_nMenuScroll = m_nMenuCursor - MenuVisibleRows + 1;
			}
		}
		else if (nDelta < 0)
		{
			if (m_nMenuCursor > 0)
			{
				--m_nMenuCursor;
				if (m_nMenuCursor < m_nMenuScroll)
					m_nMenuScroll = m_nMenuCursor;
			}
		}
	}

	return true;
}

bool CUserInterface::MenuSelectEvent()
{
	if (m_State != TState::InMenu)
		return false;

	if (s_nMenuLevel == TMenuLevel::Main)
	{
		switch (m_nMenuCursor)
		{
		case 0: m_bMenuEditing = true; break; // Toggle Synth
		case 1: s_nMenuLevel = TMenuLevel::Synth;   s_nMenuMainCursor = m_nMenuCursor; m_nMenuCursor = 0; m_nMenuScroll = 0; break;
		case 2: s_nMenuLevel = TMenuLevel::Mixer;   s_nMenuMainCursor = m_nMenuCursor; m_nMenuCursor = 0; m_nMenuScroll = 0; break;
		case 3: s_nMenuLevel = TMenuLevel::AudioFX; s_nMenuMainCursor = m_nMenuCursor; m_nMenuCursor = 0; m_nMenuScroll = 0; break;
		case 4: s_nMenuLevel = TMenuLevel::Looper;  s_nMenuMainCursor = m_nMenuCursor; m_nMenuCursor = 0; m_nMenuScroll = 0; break;
		case 5: s_nMenuLevel = TMenuLevel::Sequencer; s_nMenuMainCursor = m_nMenuCursor; m_nMenuCursor = 0; m_nMenuScroll = 0; break;
		case 6: s_nMenuLevel = TMenuLevel::Recorder;  s_nMenuMainCursor = m_nMenuCursor; m_nMenuCursor = 0; m_nMenuScroll = 0; break;
		case 7: s_nMenuLevel = TMenuLevel::Network; s_nMenuMainCursor = m_nMenuCursor; m_nMenuCursor = 0; m_nMenuScroll = 0; break;
		case 8: s_nMenuLevel = TMenuLevel::System;  s_nMenuMainCursor = m_nMenuCursor; m_nMenuCursor = 0; m_nMenuScroll = 0; break;
		case 9: m_pMenuMT32Pi->RequestReboot(); ExitMenu(); break;
		case 10: ExitMenu(); break;
		}
		return true;
	}

	if (s_nMenuLevel == TMenuLevel::System && m_nMenuCursor == 0) { m_pMenuMT32Pi->SendRawMIDI((const u8*)"\xB0\x7B\x00", 3); return true; }

	if (s_nMenuLevel == TMenuLevel::System)
	{
		switch (m_nMenuCursor)
		{
		case 1: s_nMenuLevel = TMenuLevel::MIDI;   s_nMenuSubCursor = m_nMenuCursor; m_nMenuCursor = 0; m_nMenuScroll = 0; return true;
		case 2: s_nMenuLevel = TMenuLevel::MIDICC; s_nMenuSubCursor = m_nMenuCursor; m_nMenuCursor = 0; m_nMenuScroll = 0; return true;
		default: break;
		}
	}

	// Action items (non-editing)
	if (s_nMenuLevel == TMenuLevel::Looper)
	{
		if (m_nMenuCursor == 5) { m_pMenuMT32Pi->LooperClear(); return true; }
		if (m_nMenuCursor == 6) { m_pMenuMT32Pi->LooperSave(); return true; }
	}

	if (s_nMenuLevel == TMenuLevel::Recorder)
	{
		if (m_nMenuCursor == 1) // Control
		{
			if (m_pMenuMT32Pi->GetSystemState().bMidiRecording) m_pMenuMT32Pi->StopMidiRecording();
			else m_pMenuMT32Pi->StartMidiRecording();
			return true;
		}
	}

	if (s_nMenuLevel == TMenuLevel::Sequencer)
	{
		auto s = m_pMenuMT32Pi->GetSequencerStatus();
		if (m_nMenuCursor == 1) { if (s.bPlaying) m_pMenuMT32Pi->SequencerPause(); else if (s.bPaused) m_pMenuMT32Pi->SequencerResume(); else m_pMenuMT32Pi->SequencerNext(); return true; }
		if (m_nMenuCursor == 2) { m_pMenuMT32Pi->SequencerNext(); return true; }
		if (m_nMenuCursor == 3) { m_pMenuMT32Pi->SequencerPrev(); return true; }
	}

	// Toggle edit mode for the selected item
	const char* pLabel = GetMenuItemLabel(s_nMenuLevel, m_pMenuCurrentSynth, m_pMenuSF, m_pMenuMT32, m_nMenuCursor);
	if (pLabel)
	{
		// Don't allow editing "Status" or "Clear" items
		if (strcmp(pLabel, "Status") == 0 || strstr(pLabel, "Clear") != nullptr)
			return true;
		m_bMenuEditing = !m_bMenuEditing;
	}
	return true;
}

bool CUserInterface::MenuBackEvent()
{
	if (m_State != TState::InMenu)
		return false;

	if (m_bMenuEditing)
		m_bMenuEditing = false;
	else if (s_nMenuLevel == TMenuLevel::MIDI || s_nMenuLevel == TMenuLevel::MIDICC)
	{
		s_nMenuLevel = TMenuLevel::System;
		m_nMenuCursor = s_nMenuSubCursor;
		m_nMenuScroll = 0;
	}
	else if (s_nMenuLevel != TMenuLevel::Main)
	{
		s_nMenuLevel = TMenuLevel::Main;
		m_nMenuCursor = s_nMenuMainCursor;
		m_nMenuScroll = 0;
	}
	else
		ExitMenu();

	return true;
}

void CUserInterface::DrawMenu(CLCD& LCD) const
{
	if (LCD.GetType() != CLCD::TType::Graphical)
	{
		LCD.Print("[MENU]", 0, 0, true);
		return;
	}

	const size_t nItems = GetMenuItemCount(s_nMenuLevel, m_pMenuCurrentSynth, m_pMenuSF, m_pMenuMT32);

	// Draw Submenu Header
	const char* pTitle = "Main Menu";
	switch (s_nMenuLevel)
	{
	case TMenuLevel::Synth:   pTitle = "Synth FX"; break;
	case TMenuLevel::Mixer:   pTitle = "Mixer";    break;
	case TMenuLevel::Looper:  pTitle = "Looper";   break;
	case TMenuLevel::MIDI:    pTitle = "MIDI";     break;
	case TMenuLevel::Recorder: pTitle = "Recorder"; break;
	case TMenuLevel::Sequencer: pTitle = "Sequencer"; break;
	case TMenuLevel::AudioFX: pTitle = "Audio FX"; break;
	case TMenuLevel::MIDICC:  pTitle = "MIDI CC";  break;
	case TMenuLevel::Network: pTitle = "Network";  break;
	case TMenuLevel::System:  pTitle = "System";   break;
	default: break;
	}
	char titleBuf[22]; snprintf(titleBuf, sizeof(titleBuf), "[ %s ]", pTitle);
	LCD.Print(titleBuf, CenterMessageOffset(LCD, titleBuf), 0, true, false);

	// Display up to 7 items (reserving row 0 for the title)
	const size_t nVisibleRows = (LCD.Height() / 8) - 1;
	for (size_t i = 0; i < nVisibleRows; ++i)
	{
		const size_t nItemIdx = m_nMenuScroll + i;
		if (nItemIdx >= nItems)
			break;

		const char* pLabel = nullptr;
		char valBuf[16] = "";

		if (s_nMenuLevel == TMenuLevel::Mixer && m_pMenuMT32Pi)
		{
			// Mixer item
			const size_t nMixerIdx = nItemIdx;
			pLabel = GetMixerMenuItemLabel(nMixerIdx);
			switch (nMixerIdx)
			{
			case 0: snprintf(valBuf, sizeof(valBuf), "%s", m_bMenuMixerEnabled ? "ON" : "OFF"); break;
			case 1:
			{
				static const char* presets[] = { "MT32", "Fluid", "Split", "Cust" };
				snprintf(valBuf, sizeof(valBuf), "%s",
					(m_nMenuMixerPreset >= 0 && m_nMenuMixerPreset < 4) ? presets[m_nMenuMixerPreset] : "?");
				break;
			}
			case 2: snprintf(valBuf, sizeof(valBuf), "%d%%", m_nMenuMixerMT32Vol);  break;
			case 3: snprintf(valBuf, sizeof(valBuf), "%d", static_cast<int>(m_pMenuMT32Pi->GetMixerStatus().fMT32Pan * 100.0f)); break;
			case 4: snprintf(valBuf, sizeof(valBuf), "%d%%", m_nMenuMixerFluidVol); break;
			case 5: snprintf(valBuf, sizeof(valBuf), "%d", static_cast<int>(m_pMenuMT32Pi->GetMixerStatus().fFluidPan * 100.0f)); break;
			case 6: snprintf(valBuf, sizeof(valBuf), "%d%%", m_nMenuMixerYmfmVol);  break;
			default: break;
			}
		}
		else if (s_nMenuLevel == TMenuLevel::Main)
		{
			pLabel = GetMenuItemLabel(s_nMenuLevel, m_pMenuCurrentSynth, m_pMenuSF, m_pMenuMT32, nItemIdx);
			if (nItemIdx == 0) snprintf(valBuf, sizeof(valBuf), "%s", m_pMenuCurrentSynth->GetName());
			else if (nItemIdx >= 1 && nItemIdx <= 8) snprintf(valBuf, sizeof(valBuf), ">");
		}
		else if (s_nMenuLevel == TMenuLevel::Looper)
		{
			pLabel = GetMenuItemLabel(s_nMenuLevel, m_pMenuCurrentSynth, m_pMenuSF, m_pMenuMT32, nItemIdx);
			const auto ls = m_pMenuMT32Pi->GetLooperStatus();
			switch (nItemIdx)
			{
			case 0: { static const char* states[] = { "Idle", "Arm", "Rec", "Play", "Ovd", "Stop" }; snprintf(valBuf, sizeof(valBuf), "%s", states[static_cast<int>(ls.nState)]); break; }
			case 1: snprintf(valBuf, sizeof(valBuf), "%d", ls.nBPM); break;
			case 2: snprintf(valBuf, sizeof(valBuf), "1/%d", ls.nQuantize); break;
			case 3: snprintf(valBuf, sizeof(valBuf), "%s", ls.bMetronomeEnabled ? "ON" : "OFF"); break;
			case 4: snprintf(valBuf, sizeof(valBuf), "%.1f", static_cast<double>(ls.fPlaybackGain)); break;
			default: break;
			}
		}
		else if (s_nMenuLevel == TMenuLevel::Recorder)
		{
			pLabel = GetMenuItemLabel(s_nMenuLevel, m_pMenuCurrentSynth, m_pMenuSF, m_pMenuMT32, nItemIdx);
			const auto st = m_pMenuMT32Pi->GetSystemState();
			switch (nItemIdx)
			{
			case 0: snprintf(valBuf, sizeof(valBuf), "%s", st.bMidiRecording ? "Rec" : "Stop"); break;
			case 1: snprintf(valBuf, sizeof(valBuf), "%s", st.bMidiRecording ? "STOP" : "START"); break;
			}
		}
		else if (s_nMenuLevel == TMenuLevel::Sequencer)
		{
			pLabel = GetMenuItemLabel(s_nMenuLevel, m_pMenuCurrentSynth, m_pMenuSF, m_pMenuMT32, nItemIdx);
			const auto ss = m_pMenuMT32Pi->GetSequencerStatus();
			switch (nItemIdx)
			{
			case 0: snprintf(valBuf, sizeof(valBuf), "%s", ss.bPlaying ? "Play" : (ss.bPaused ? "Paus" : "Stop")); break;
			case 1: snprintf(valBuf, sizeof(valBuf), "%s", ss.bPlaying ? "PAUS" : "PLAY"); break;
			case 4: snprintf(valBuf, sizeof(valBuf), "%s", ss.bLoopEnabled ? "ON" : "OFF"); break;
			case 5: snprintf(valBuf, sizeof(valBuf), "%s", ss.bAutoNext ? "ON" : "OFF"); break;
			case 6: snprintf(valBuf, sizeof(valBuf), "%.1fx", ss.nTempoMultiplier); break;
			default: break;
			}
		}
		else if (s_nMenuLevel == TMenuLevel::MIDI)
		{
			pLabel = GetMenuItemLabel(s_nMenuLevel, m_pMenuCurrentSynth, m_pMenuSF, m_pMenuMT32, nItemIdx);
			const auto ms = m_pMenuMT32Pi->GetMixerStatus();
			const auto cfg = m_pMenuMT32Pi->GetConfig();
			if (nItemIdx == 0) snprintf(valBuf, sizeof(valBuf), "%s", ms.bMIDIThruEnabled ? "ON" : "OFF");
			else if (nItemIdx == 1) snprintf(valBuf, sizeof(valBuf), "%d", cfg->MIDIGPIOBaudRate);
			else if (nItemIdx == 2) snprintf(valBuf, sizeof(valBuf), "%d", cfg->MIDIUSBSerialBaudRate);
		}
		else if (s_nMenuLevel == TMenuLevel::AudioFX)
		{
			pLabel = GetMenuItemLabel(s_nMenuLevel, m_pMenuCurrentSynth, m_pMenuSF, m_pMenuMT32, nItemIdx);
			const auto ms = m_pMenuMT32Pi->GetMixerStatus();
			switch (nItemIdx)
			{
			case 0: snprintf(valBuf, sizeof(valBuf), "%s", ms.bEffectsEQEnabled ? "ON" : "OFF"); break;
			case 1: snprintf(valBuf, sizeof(valBuf), "%ddB", ms.nEffectsEQBass); break;
			case 2: snprintf(valBuf, sizeof(valBuf), "%ddB", ms.nEffectsEQTreble); break;
			case 3: snprintf(valBuf, sizeof(valBuf), "%s", ms.bEffectsLimiterEnabled ? "ON" : "OFF"); break;
			case 4: snprintf(valBuf, sizeof(valBuf), "%s", ms.bEffectsReverbEnabled ? "ON" : "OFF"); break;
			case 5: snprintf(valBuf, sizeof(valBuf), "%d%%", ms.nEffectsReverbRoom); break;
			case 6: snprintf(valBuf, sizeof(valBuf), "%d%%", ms.nEffectsReverbDamp); break;
			case 7: snprintf(valBuf, sizeof(valBuf), "%d%%", ms.nEffectsReverbWet); break;
			}
		}
		else if (s_nMenuLevel == TMenuLevel::MIDICC)
		{
			pLabel = GetMenuItemLabel(s_nMenuLevel, m_pMenuCurrentSynth, m_pMenuSF, m_pMenuMT32, nItemIdx);
			const auto cfg = m_pMenuMT32Pi->GetConfig();
			int bindingIdx = nItemIdx;
			if (bindingIdx == 13) bindingIdx = 18;

			if (bindingIdx < static_cast<int>(CConfig::TMIDICCBindingID::Count))
			{
				const int cc = cfg->MIDICCBindingCC[bindingIdx];
				if (cc == -1) snprintf(valBuf, sizeof(valBuf), "OFF");
				else snprintf(valBuf, sizeof(valBuf), "%d", cc);
			}
		}
		else if (s_nMenuLevel == TMenuLevel::Network)
		{
			pLabel = GetMenuItemLabel(s_nMenuLevel, m_pMenuCurrentSynth, m_pMenuSF, m_pMenuMT32, nItemIdx);
			const auto cfg = m_pMenuMT32Pi->GetConfig();
			if (nItemIdx == 0) { CString ip; m_pMenuMT32Pi->FormatIPAddress(ip); snprintf(valBuf, sizeof(valBuf), "%s", (const char*)ip); }
			else if (nItemIdx == 1) snprintf(valBuf, sizeof(valBuf), "%.6s", (const char*)cfg->NetworkHostname);
			else if (nItemIdx == 2) snprintf(valBuf, sizeof(valBuf), "%s", m_pMenuMT32Pi->IsNetworkReady() ? "OK" : "NO");
			else if (nItemIdx == 3) { static const char* modes[] = { "OFF", "Eth", "WiFi" }; snprintf(valBuf, sizeof(valBuf), "%s", modes[static_cast<int>(cfg->NetworkMode)]); }
			else if (nItemIdx == 4) snprintf(valBuf, sizeof(valBuf), "%s", cfg->NetworkDHCP ? "ON" : "OFF");
		}
		else if (s_nMenuLevel == TMenuLevel::System)
		{
			pLabel = GetMenuItemLabel(s_nMenuLevel, m_pMenuCurrentSynth, m_pMenuSF, m_pMenuMT32, nItemIdx);
			const auto cfg = m_pMenuMT32Pi->GetConfig();
			if (nItemIdx == 0) snprintf(valBuf, sizeof(valBuf), "EXEC");
			else if (nItemIdx == 1 || nItemIdx == 2) snprintf(valBuf, sizeof(valBuf), ">");
			else if (nItemIdx == 3) snprintf(valBuf, sizeof(valBuf), "%s", cfg->SystemVerbose ? "ON" : "OFF");
			else if (nItemIdx == 4) snprintf(valBuf, sizeof(valBuf), "%s", cfg->SystemUSB ? "ON" : "OFF");
			else if (nItemIdx == 5)
			{
				if (cfg->SystemI2CBaudRate >= 1000000) snprintf(valBuf, sizeof(valBuf), "%dM", cfg->SystemI2CBaudRate / 1000000);
				else snprintf(valBuf, sizeof(valBuf), "%dk", cfg->SystemI2CBaudRate / 1000);
			}
			else if (nItemIdx == 6) snprintf(valBuf, sizeof(valBuf), "%ds", cfg->SystemPowerSaveTimeout);
		}
		else if (m_pMenuYmfm && m_pMenuCurrentSynth == m_pMenuYmfm && s_nMenuLevel == TMenuLevel::Synth)
		{
			// OPL3 items — label + value inline
			pLabel = GetMenuItemLabel(s_nMenuLevel, m_pMenuCurrentSynth, m_pMenuSF, m_pMenuMT32, nItemIdx);
			if (nItemIdx == 0)
				snprintf(valBuf, sizeof(valBuf), "%.6s", m_pMenuYmfm->GetBankName());
			else if (nItemIdx == 1)
				snprintf(valBuf, sizeof(valBuf), "%s",
					m_pMenuYmfm->GetChipMode() == TOplChipMode::OPL3 ? "OPL3" : "OPL2");
			else if (nItemIdx == 2)
				snprintf(valBuf, sizeof(valBuf), "%d%%", m_nMenuYmfmVol);
		}
		else if (s_nMenuLevel == TMenuLevel::Synth)
		{
			pLabel = GetMenuItemLabel(s_nMenuLevel, m_pMenuCurrentSynth, m_pMenuSF, m_pMenuMT32, nItemIdx);
			if (nItemIdx == 12) snprintf(valBuf, sizeof(valBuf), "%.4s", m_pMenuSF->GetTuningName(m_pMenuSF->GetTuning()));
			else if (nItemIdx == 13) snprintf(valBuf, sizeof(valBuf), "%d", m_pMenuSF->GetPolyphony());
			else FormatMenuValue(valBuf, sizeof(valBuf),
			                m_pMenuCurrentSynth, m_pMenuSF, m_pMenuMT32,
			                nItemIdx,
			                m_fMenuGain,
			                m_bMenuReverbActive, m_fMenuReverbRoomSize, m_fMenuReverbLevel,
			                m_fMenuReverbDamping, m_fMenuReverbWidth,
			                m_bMenuChorusActive, m_fMenuChorusDepth,
			                m_fMenuChorusLevel, m_nMenuChorusVoices, m_fMenuChorusSpeed,
			                m_nMenuROMSet, m_nMenuSoundFont,
			                m_fMenuMT32Gain, m_fMenuMT32ReverbGain,
			                m_bMenuMT32ReverbEnabled,
			                m_bMenuMT32NiceAmpRamp, m_bMenuMT32NicePanning, m_bMenuMT32NicePartMix,
			                m_nMenuMT32DACMode, m_nMenuMT32MIDIDelay,
			                m_nMenuMT32AnalogMode, m_nMenuMT32RendererType,
			                m_nMenuMT32PartialCount);
		}

		if (!pLabel)
			break;

		// 20-char row: selector(1) + label(13) + value(6)
		char rowBuf[21];
		const bool bSelected = (nItemIdx == m_nMenuCursor);
		const char cSel = bSelected ? (m_bMenuEditing ? '*' : '>') : ' ';
		snprintf(rowBuf, sizeof(rowBuf), "%c%-13.13s%6.6s", cSel, pLabel, valBuf);

		LCD.Print(rowBuf, 0, static_cast<u8>(i + 1), /*bClearLine=*/false, /*bImmediate=*/false);
	}
}

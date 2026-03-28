#include <circle/logger.h>
#include <circle/timer.h>
#include <circle/util.h>
#include "rhythmlooper.h"
#include "midirouter.h"
#include "synth/synthbase.h"

LOGMODULE("looper");

CRhythmLooper::CRhythmLooper()
	: m_State(TState::Idle),
	  m_bEnabled(true),
	  m_nChannel(10),
	  m_nBPM(120),
	  m_nQuantize(16),
	  m_fPlaybackGain(0.8f),
	  m_nMaxBars(8),
	  m_nLoopStartSystemTick(0),
	  m_nLoopLengthMidiTicks(0),
	  m_nLastProcessedMidiTick(0),
	  m_nEventCount(0),
	  m_pRouter(nullptr),
	  m_pSynth(nullptr)
{
}

CRhythmLooper::~CRhythmLooper()
{
}

void CRhythmLooper::SetBPM(int nBPM)
{
	m_nBPM = Utility::Clamp(nBPM, 20, 300);
}

u32 CRhythmLooper::GetCurrentMidiTick(u32 nTicksNow) const
{
	if (m_nLoopStartSystemTick == 0) return 0;
	
	const u32 nDeltaUs = nTicksNow - m_nLoopStartSystemTick;
	// ticks = delta_us * (BPM/60) * PPQN / 1,000,000
	// Simplifies to: (delta_us * BPM * PPQN) / 60,000,000
	return static_cast<u32>((static_cast<u64>(nDeltaUs) * m_nBPM * PPQN) / 60000000ULL);
}

u32 CRhythmLooper::QuantizeTick(u32 nTick) const
{
	if (m_nQuantize <= 0) return nTick;
	
	// step = PPQN * 4 / quantize (e.g. 480 * 4 / 16 = 120 ticks per 16th note)
	const u32 step = (PPQN * 4) / m_nQuantize;
	return ((nTick + step / 2) / step) * step;
}

void CRhythmLooper::ArmStop()
{
	switch (m_State)
	{
		case TState::Idle:
			m_State = TState::Armed;
			LOGNOTE("Looper Armed");
			break;

		case TState::StoppedWithLoop:
			m_nLoopStartSystemTick = CTimer::GetClockTicks();
			m_nLastProcessedMidiTick = 0;
			m_State = TState::Playing;
			LOGNOTE("Looper Resumed");
			break;

		case TState::Armed:
			m_State = TState::Idle;
			LOGNOTE("Looper Disarmed");
			break;
			
		case TState::Recording:
		{
			// Close the loop
			u32 nTicksNow = CTimer::GetClockTicks();
			u32 totalTicks = GetCurrentMidiTick(nTicksNow);
			
			m_nLoopLengthMidiTicks = QuantizeTick(totalTicks);
			if (m_nLoopLengthMidiTicks == 0) m_nLoopLengthMidiTicks = (PPQN * 4); // Min 1 bar

			// Quantization wrap-around: ensure all events are within [0, LoopLength-1]
			// This handles notes played slightly "late" that snap to the start of the next cycle
			for (u32 i = 0; i < m_nEventCount; ++i)
			{
				m_Events[i].nTick %= m_nLoopLengthMidiTicks;
			}
			
			m_nLastProcessedMidiTick = 0;
			m_State = TState::Playing;
			LOGNOTE("Loop Recorded: %u ticks", m_nLoopLengthMidiTicks);
			break;
		}
			
		case TState::Playing:
			m_State = TState::StoppedWithLoop;
			// Kill any ringing notes on the drum channel
			PlayEvent({0, 0x007B00B0u | (static_cast<u32>(m_nChannel) - 1)}); // All Notes Off
			PlayEvent({0, 0x007800B0u | (static_cast<u32>(m_nChannel) - 1)}); // All Sound Off
			LOGNOTE("Looper Stopped");
			break;
	}
}

void CRhythmLooper::Clear()
{
	m_nEventCount = 0;
	m_nLoopLengthMidiTicks = 0;
	m_State = TState::Idle;
	LOGNOTE("Looper Cleared");
}

void CRhythmLooper::OnShortMessage(u32 nMessage, u32 nTicksNow)
{
	if (!m_bEnabled) return;
	
	u8 status = nMessage & 0xFF;
	u8 type = status & 0xF0;
	u8 chan = status & 0x0F;
	
	if (chan != m_nChannel - 1) return;
	if (type != 0x90 && type != 0x80) return;

	if (m_State == TState::Armed)
	{
		m_State = TState::Recording;
		m_nLoopStartSystemTick = nTicksNow;
		m_nEventCount = 0;
		LOGNOTE("Looper Recording started");
	}
	
	if (m_State == TState::Recording)
	{
		u32 midiTick = GetCurrentMidiTick(nTicksNow);
		if (midiTick >= m_nMaxBars * PPQN * 4)
		{
			LOGNOTE("Looper: Max bars reached, stopping recording");
			ArmStop();
			return;
		}

		if (m_nEventCount < MaxEvents)
		{
			m_Events[m_nEventCount++] = { QuantizeTick(midiTick), nMessage };
		}
	}
}

void CRhythmLooper::Update(u32 nTicksNow)
{
	if (m_State != TState::Playing || m_nLoopLengthMidiTicks == 0) return;
	
	u32 currentMidiTick = GetCurrentMidiTick(nTicksNow) % m_nLoopLengthMidiTicks;
	
	// Handle loop wrap-around
	if (currentMidiTick < m_nLastProcessedMidiTick)
	{
		// Play remaining events in previous loop cycle
		for (u32 i = 0; i < m_nEventCount; ++i)
		{
			if (m_Events[i].nTick >= m_nLastProcessedMidiTick && m_Events[i].nTick < m_nLoopLengthMidiTicks)
				PlayEvent(m_Events[i]);
		}
		m_nLastProcessedMidiTick = 0;
	}
	
	for (u32 i = 0; i < m_nEventCount; ++i)
	{
		if (m_Events[i].nTick >= m_nLastProcessedMidiTick && m_Events[i].nTick <= currentMidiTick)
		{
			PlayEvent(m_Events[i]);
		}
	}
	
	m_nLastProcessedMidiTick = currentMidiTick;
}

void CRhythmLooper::PlayEvent(const TLoopEvent& Event)
{
	u32 nMessage = Event.nMessage;

	// Scale velocity for Note On messages to prevent "insanely loud" playback
	if ((nMessage & 0xF0) == 0x90)
	{
		u8 vel = (nMessage >> 16) & 0x7F;
		if (vel > 0)
		{
			vel = static_cast<u8>(vel * m_fPlaybackGain);
			if (vel == 0) vel = 1; // Don't turn a Note On into a Note Off (vel 0)
			nMessage = (nMessage & 0xFF00FFFF) | (static_cast<u32>(vel) << 16);
		}
	}

	if (m_pRouter)
		m_pRouter->RouteShortMessage(nMessage);
	else if (m_pSynth)
		m_pSynth->HandleMIDIShortMessage(nMessage);
}

void CRhythmLooper::SaveToMIDI()
{
	// Implementation for SMF export will go here
	LOGNOTE("SaveToMIDI not yet implemented");
}
#include <circle/logger.h>
#include <circle/timer.h>
#include <circle/util.h>
#include "rhythmlooper.h"
#include "midirouter.h"
#include <fatfs/ff.h>
#include <cstdio>
#include <new>
#include "synth/synthbase.h"

LOGMODULE("looper");

CRhythmLooper::CRhythmLooper()
	: m_State(TState::Idle),
	  m_bEnabled(true),
	  m_nChannel(10),
	  m_nBPM(120),
	  m_nQuantize(16),
	  m_bMetronomeEnabled(false),
	  m_fPlaybackGain(0.8f),
	  m_nMaxBars(8),
	  m_nLoopStartSystemTick(0),
	  m_nLoopLengthMidiTicks(0),
	  m_nLastProcessedMidiTick(0),
	  m_nEventCount(0),
	  m_pRouter(nullptr),
	  m_nLastMetronomeBeatTick(0),
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
		case TState::Overdubbing:
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
			u32 totalTicks = GetCurrentMidiTick(CTimer::GetClockTicks());
			
			// Determine loop length based on the last recorded event, not button press time
			u32 maxEventTick = 0;
			if (m_nEventCount > 0)
			{
				for (u32 i = 0; i < m_nEventCount; ++i)
				{
					if (m_Events[i].nTick > maxEventTick) maxEventTick = m_Events[i].nTick;
				}
			}
			m_nLoopLengthMidiTicks = QuantizeTick(maxEventTick + 1); // +1 tick to ensure the last note is fully included
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
			m_State = TState::Overdubbing;
			LOGNOTE("Looper Overdubbing");
			break;
	}
}

void CRhythmLooper::StopPlayback()
{
	if (m_State == TState::Playing || m_State == TState::Overdubbing)
	{
		m_State = TState::StoppedWithLoop;
		// Kill any ringing notes on the drum channel
		PlayEvent({0, 0x007B00B0u | (static_cast<u32>(m_nChannel) - 1)}); // All Notes Off
		PlayEvent({0, 0x007800B0u | (static_cast<u32>(m_nChannel) - 1)}); // All Sound Off
		LOGNOTE("Looper Stopped");
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
	
	if (m_State == TState::Recording || m_State == TState::Overdubbing)
	{
		u32 midiTick = GetCurrentMidiTick(nTicksNow);
		if (midiTick >= m_nMaxBars * PPQN * 4)
		{
			LOGNOTE("Looper: Max bars reached, stopping recording");
			ArmStop();
			return;
		}

		// Apply loop wrap-around for overdubbing
		if (m_State == TState::Overdubbing)
		{
			midiTick %= m_nLoopLengthMidiTicks;
		}

		if (m_nEventCount < MaxEvents)
		{
			u32 quantizedTick = QuantizeTick(midiTick);
			if (m_nLoopLengthMidiTicks > 0) quantizedTick %= m_nLoopLengthMidiTicks;
			m_Events[m_nEventCount++] = { quantizedTick, nMessage };
		}
	}
}

void CRhythmLooper::Update(u32 nTicksNow)
{
	// Metronome logic
	if (m_bMetronomeEnabled && (m_State == TState::Armed || m_State == TState::Recording || m_State == TState::Overdubbing))
	{
		u32 currentMidiTick = GetCurrentMidiTick(nTicksNow);
		u32 beatLength = PPQN; // 1 beat = 1 quarter note

		// If recording, loop the metronome based on loop length
		if (m_State == TState::Recording || m_State == TState::Overdubbing)
		{
			if (m_nLoopLengthMidiTicks > 0)
			{
				currentMidiTick %= m_nLoopLengthMidiTicks;
			}
		}

		u32 nextBeatTick = QuantizeTick(currentMidiTick); // Quantize to the nearest beat

		if (nextBeatTick >= m_nLastMetronomeBeatTick + beatLength || (nextBeatTick < m_nLastMetronomeBeatTick && m_nLoopLengthMidiTicks > 0))
		{
			// Play metronome click
			u8 note = (nextBeatTick % (PPQN * 4) == 0) ? 36 : 38; // Note 36 (Bass Drum 1) for beat 1, 38 (Snare Drum 1) for others
			u8 vel = (nextBeatTick % (PPQN * 4) == 0) ? 100 : 80;
			PlayEvent({0, (u32)(0x90 | (m_nChannel - 1)) | (u32)(note << 8) | (u32)(vel << 16)}); // Note On
			PlayEvent({0, (u32)(0x80 | (m_nChannel - 1)) | (u32)(note << 8) | (u32)(0 << 16)});  // Note Off (immediate)
			m_nLastMetronomeBeatTick = nextBeatTick;
		}
	}
	if ((m_State != TState::Playing && m_State != TState::Overdubbing) || m_nLoopLengthMidiTicks == 0) return;
	
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
	if (m_nEventCount == 0 || m_nLoopLengthMidiTicks == 0)
	{
		LOGWARN("SaveToMIDI: Loop is empty");
		return;
	}

	// Ensure the output directory exists
	f_mkdir("SD:/loops");

	// Find the next available filename
	char szPath[32];
	bool bFound = false;
	for (int i = 1; i <= 999; ++i)
	{
		snprintf(szPath, sizeof(szPath), "SD:/loops/loop_%03d.mid", i);
		FILINFO fno;
		if (f_stat(szPath, &fno) != FR_OK)
		{
			bFound = true;
			break;
		}
	}
	if (!bFound) strcpy(szPath, "SD:/loops/loop_999.mid");

	// Sort events by tick (required because overdubbing appends events out of chronological order)
	for (u32 i = 0; i < m_nEventCount - 1; ++i)
	{
		for (u32 j = i + 1; j < m_nEventCount; ++j)
		{
			if (m_Events[j].nTick < m_Events[i].nTick)
			{
				TLoopEvent temp = m_Events[i];
				m_Events[i] = m_Events[j];
				m_Events[j] = temp;
			}
		}
	}

	// Buffer for SMF construction (64KB is plenty for 2048 events)
	const u32 nBufSize = 64 * 1024;
	u8* pBuf = new (std::nothrow) u8[nBufSize];
	if (!pBuf) return;

	u32 nPos = 0;
	auto WriteByte = [&](u8 b) { if (nPos < nBufSize) pBuf[nPos++] = b; };
	auto WriteBytes = [&](const u8* d, u32 l) { for (u32 i=0; i<l; ++i) WriteByte(d[i]); };
	auto WriteU16BE = [&](u16 v) { WriteByte(v >> 8); WriteByte(v & 0xFF); };
	auto WriteU32BE = [&](u32 v) { WriteU16BE(v >> 16); WriteU16BE(v & 0xFFFF); };
	auto WriteVarLen = [&](u32 v) {
		u8 b[4]; int n = 0;
		b[n++] = v & 0x7F; v >>= 7;
		while (v > 0) { b[n++] = (v & 0x7F) | 0x80; v >>= 7; }
		for (int i = n - 1; i >= 0; --i) WriteByte(b[i]);
	};

	// SMF Header (MThd)
	WriteBytes((const u8*)"MThd", 4);
	WriteU32BE(6);
	WriteU16BE(0); // Type 0 (single track)
	WriteU16BE(1);
	WriteU16BE(PPQN);

	// Track Header (MTrk)
	WriteBytes((const u8*)"MTrk", 4);
	u32 nLenOffset = nPos;
	WriteU32BE(0); // Placeholder for track length

	// Set Tempo meta-event (microseconds per quarter note = 60,000,000 / BPM)
	u32 nTempoUs = 60000000 / m_nBPM;
	WriteVarLen(0);
	WriteByte(0xFF); WriteByte(0x51); WriteByte(0x03);
	WriteByte((nTempoUs >> 16) & 0xFF); WriteByte((nTempoUs >> 8) & 0xFF); WriteByte(nTempoUs & 0xFF);

	// MIDI Events
	u32 nLastTick = 0;
	for (u32 i = 0; i < m_nEventCount; ++i)
	{
		WriteVarLen(m_Events[i].nTick - nLastTick);
		nLastTick = m_Events[i].nTick;
		u32 msg = m_Events[i].nMessage;
		WriteByte(msg & 0xFF);
		WriteByte((msg >> 8) & 0x7F);
		WriteByte((msg >> 16) & 0x7F);
	}

	// End of Track meta-event at exactly the loop boundary
	WriteVarLen(m_nLoopLengthMidiTicks - nLastTick);
	WriteByte(0xFF); WriteByte(0x2F); WriteByte(0x00);

	// Back-fill track length
	u32 nTrackLen = nPos - (nLenOffset + 4);
	pBuf[nLenOffset] = (nTrackLen >> 24) & 0xFF; pBuf[nLenOffset + 1] = (nTrackLen >> 16) & 0xFF;
	pBuf[nLenOffset + 2] = (nTrackLen >> 8) & 0xFF; pBuf[nLenOffset + 3] = nTrackLen & 0xFF;

	FIL f;
	if (f_open(&f, szPath, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
	{
		UINT nWritten;
		f_write(&f, pBuf, nPos, &nWritten);
		f_close(&f);
		LOGNOTE("Loop saved to %s", szPath);
	}

	delete[] pBuf;
}
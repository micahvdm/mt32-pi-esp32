#ifndef _rhythmlooper_h
#define _rhythmlooper_h

#include <circle/types.h>

class CSynthBase;
class CMIDIRouter;

class CRhythmLooper
{
public:
	enum class TState
	{
		Idle,
		Armed,
		Recording,
		Playing,
		Overdubbing,
		StoppedWithLoop,
	};

	struct TLoopEvent
	{
		u32 nTick;
		u32 nMessage;
	};

	CRhythmLooper();
	~CRhythmLooper();

	void Update(u32 nTicksNow);
	void OnShortMessage(u32 nMessage, u32 nTicksNow);

	void ArmStop();
	void StopPlayback();
	void SaveToMIDI();
	void Clear();

	TState GetState() const { return m_State; }
	
	void SetEnabled(bool bEnabled) { m_bEnabled = bEnabled; }
	bool IsEnabled() const { return m_bEnabled; }

	void SetBPM(int nBPM);
	int  GetBPM() const { return m_nBPM; }
	void SetQuantize(int nQuantize) { m_nQuantize = nQuantize; }
	int  GetQuantize() const { return m_nQuantize; }
	
	void SetMetronomeEnabled(bool bEnabled) { m_bMetronomeEnabled = bEnabled; }
	bool GetMetronomeEnabled() const { return m_bMetronomeEnabled; }

	void  SetPlaybackGain(float fGain) { m_fPlaybackGain = fGain; }
	float GetPlaybackGain() const { return m_fPlaybackGain; }

	void SetMixerEnabled(bool bEnabled) { m_bMixerEnabled = bEnabled; }
	bool IsMixerEnabled() const { return m_bMixerEnabled; }

	void SetRouter(CMIDIRouter* pRouter) { m_pRouter = pRouter; }
	void SetSynth(CSynthBase* pSynth) { m_pSynth = pSynth; }

private:
	u32 GetCurrentMidiTick(u32 nTicksNow) const;
	u32 QuantizeTick(u32 nTick) const;
	void PlayEvent(const TLoopEvent& Event);

	TState m_State;
	bool   m_bEnabled;
	u8     m_nChannel;
	int    m_nBPM;
	int    m_nQuantize;
	bool   m_bMixerEnabled;
	bool   m_bMetronomeEnabled;
	float  m_fPlaybackGain;
	u32    m_nMaxBars;

	u32 m_nLoopStartSystemTick;
	u32 m_nLoopLengthMidiTicks;
	u32 m_nLastProcessedMidiTick;

	static constexpr u32 MaxEvents = 4096;
	TLoopEvent m_Events[MaxEvents];
	u32 m_nEventCount;

	CMIDIRouter* m_pRouter;
	u32          m_nLastMetronomeBeatTick;
	CSynthBase*  m_pSynth;

	static constexpr u32 PPQN = 480;
};

#endif
#include "midiClock.h"

#include "midiTypes.h"

#include <cmath>

#include "plugin.h"

#include "dsp56kBase/logging.h"

#if 0
#define LOGMC(S)	LOG(S)
#else
#define LOGMC(S)	do{}while(false)
#endif

namespace synthLib
{
	static constexpr double ClockTicksPerQuarter = 24.0;

	void MidiClock::process(const float _bpm, const float _ppqPos, const bool _isPlaying, const size_t _sampleCount)
	{
		if(_bpm < 1.0f)
			return;

		const double quartersPerSecond = _bpm / 60.0;
		const double clockTicksPerSecond = ClockTicksPerQuarter * quartersPerSecond;

		const double hostSamplerateInv = m_plugin.getHostSamplerateInv();

		const double clocksPerSample = clockTicksPerSecond * hostSamplerateInv;

		if(_isPlaying && !m_isPlaying)
		{
			start(_ppqPos);
		}
		else if(m_isPlaying && !_isPlaying)
		{
			LOGMC("Stop at ppqPos=" << _ppqPos);
			stop();
		}
		else if(m_isPlaying && _isPlaying && _ppqPos < m_lastPpqPos - 0.5)
		{
			// Host looped back or seeked backward — re-sync the clock
			LOGMC("Loop/seek detected: ppqPos " << m_lastPpqPos << " -> " << _ppqPos);
			stop();
			start(_ppqPos);
		}

		m_lastPpqPos = _ppqPos;

		for(uint32_t i=0; i<static_cast<uint32_t>(_sampleCount); ++i)
		{
			m_clockTickPos += clocksPerSample;

			if (m_clockTickPos < 0.0f)
				continue;

			m_clockTickPos -= 1.0;

			LOGMC("insert tick at " << i);

			SMidiEvent evClock(MidiEventSource::Internal);
			evClock.a = M_TIMINGCLOCK;
			evClock.offset = i;
			m_plugin.insertMidiEvent(evClock);
		}
	}

	void MidiClock::restart()
	{
		stop();
	}

	void MidiClock::start(const float _ppqPos)
	{
		const double ppqPos = _ppqPos;

		// Compute how far through the current clock tick period we already are,
		// so the first tick fires at the correct phase rather than always at the
		// start of the next full tick period.
		// m_clockTickPos must be in [-1, 0): the loop fires a tick each time it
		// crosses 0, so starting at (fracTick - 1.0) fires the first tick after
		// exactly (1 - fracTick) of a tick period.
		const double absoluteTicks = ppqPos * ClockTicksPerQuarter;
		const double fracTick = absoluteTicks - std::floor(absoluteTicks);
		m_clockTickPos = fracTick - 1.0;

		m_lastPpqPos = ppqPos;
		m_isPlaying = true;

		LOGMC("Start at ppqPos=" << ppqPos << ", clock tick offset " << m_clockTickPos);

		SMidiEvent evClock(MidiEventSource::Internal);
		evClock.a = M_START;
		evClock.offset = 0;
		m_plugin.insertMidiEvent(evClock);
	}

	void MidiClock::stop()
	{
		m_isPlaying = false;

		SMidiEvent evStop(MidiEventSource::Internal);
		evStop.a = M_STOP;
		m_plugin.insertMidiEvent(evStop);
	}
}

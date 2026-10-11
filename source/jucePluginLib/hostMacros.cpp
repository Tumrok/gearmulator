#include "hostMacros.h"

#include <cctype>
#include <cmath>
#include <sstream>
#include <vector>

#include "controller.h"
#include "parameter.h"
#include "processor.h"

#include "baseLib/binarystream.h"

#include "synthLib/midiTypes.h"

namespace pluginLib
{
	namespace
	{
		// set while we report a value to the host ourselves: the host's echo must not be written back
		thread_local bool t_pushing = false;

		constexpr size_t g_maxValueText = 8;

		// short parameter name for the host / Push: "Filter1 Cutoff" style
		juce::String shortName(const std::string& _name)
		{
			auto s = juce::String(_name)
				.replace("Amplifier", "Amp").replace("Oscillator", "Osc")
				.replace("Envelope", "Env").replace("Filter ", "F").replace("Resonance", "Reso")
				.replace("Keyfollow", "Kf").replace("Velocity", "Vel").replace("Amount", "Amt")
				.replace("Distortion", "Dist").replace("/", " ");
			while (s.contains("  "))
				s = s.replace("  ", " ");
			return s.trim();
		}

		std::vector<std::string> splitWords(const std::string& _s)
		{
			std::vector<std::string> words;
			std::istringstream ss(_s);
			std::string w;
			while (ss >> w)
				words.push_back(w);
			return words;
		}

		std::string joinWords(const std::vector<std::string>& _words)
		{
			std::string r;
			for (const auto& w : _words)
			{
				if (w.empty())
					continue;
				if (!r.empty())
					r += ' ';
				r += w;
			}
			return r;
		}

		bool hasDigit(const std::string& _s)
		{
			for (const char c : _s)
				if (std::isdigit(static_cast<unsigned char>(c)))
					return true;
			return false;
		}

		// value text that fits Push: "L< 68.8 %" -> "L< 69%", "Saw Pulse 10 %" -> "SP 10%"
		juce::String abbreviate(const juce::String& _text)
		{
			auto words = splitWords(_text.toStdString());

			// "10 %" -> "10%"
			std::vector<std::string> joined;
			for (const auto& w : words)
			{
				if (!joined.empty() && !w.empty() && w[0] == '%')
					joined.back() += w;
				else
					joined.push_back(w);
			}
			words = std::move(joined);

			auto t = joinWords(words);
			if (t.size() <= g_maxValueText)
				return t;

			// round decimals
			for (auto& w : words)
			{
				const auto dot = w.find('.');
				if (dot == std::string::npos || !hasDigit(w))
					continue;
				size_t start = dot;
				while (start > 0 && (std::isdigit(static_cast<unsigned char>(w[start - 1])) || w[start - 1] == '-'))
					--start;
				size_t end = dot + 1;
				while (end < w.size() && std::isdigit(static_cast<unsigned char>(w[end])))
					++end;
				if (end == dot + 1)
					continue;
				const auto v = std::strtod(w.substr(start, end - start).c_str(), nullptr);
				w = w.substr(0, start) + std::to_string(static_cast<int>(std::lround(v))) + w.substr(end);
			}

			t = joinWords(words);
			if (t.size() <= g_maxValueText)
				return t;

			// letter words -> initials, words with digits stay
			std::string initials;
			std::vector<std::string> rest;
			for (const auto& w : words)
			{
				if (hasDigit(w))
				{
					rest.push_back(w);
					continue;
				}
				bool inRun = false;
				bool anyLetter = false;
				for (const char c : w)
				{
					const bool letter = std::isalpha(static_cast<unsigned char>(c)) != 0;
					if (letter && !inRun)
						initials += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
					inRun = letter;
					anyLetter |= letter;
				}
				if (!anyLetter)
					rest.push_back(w);
			}
			auto r = initials;
			if (!r.empty() && !rest.empty())
				r += ' ';
			r += joinWords(rest);
			return r;
		}
	}

	// ---- Knob -------------------------------------------------------------------------

	HostMacros::Knob::Knob(HostMacros& _owner, const int _index)
		: AudioProcessorParameterWithID(juce::ParameterID("hmKnob" + juce::String(_index + 1), 1), "Knob " + juce::String(_index + 1))
		, m_owner(_owner)
		, m_index(_index)
	{
	}

	void HostMacros::Knob::setValue(const float _newValue)
	{
		m_value.store(_newValue);
		if (!t_pushing)
			m_owner.onKnobFromHost(m_index, _newValue);
	}

	juce::String HostMacros::Knob::getName(const int _maximumStringLength) const
	{
		const auto n = m_owner.getKnobName(m_index);
		return _maximumStringLength > 0 ? n.substring(0, _maximumStringLength) : n;
	}

	juce::String HostMacros::Knob::getText(const float _normalisedValue, const int _maximumStringLength) const
	{
		return m_owner.getKnobText(m_index, _normalisedValue, _maximumStringLength);
	}

	float HostMacros::Knob::getValueForText(const juce::String& _text) const
	{
		return juce::jlimit(0.0f, 1.0f, _text.getFloatValue() / 127.0f);
	}

	void HostMacros::Knob::setFromTarget(const float _value)
	{
		t_pushing = true;
		setValueNotifyingHost(_value);
		t_pushing = false;
	}

	// ---- Layer ------------------------------------------------------------------------

	HostMacros::LayerParam::LayerParam(HostMacros& _owner)
		: AudioProcessorParameterWithID(juce::ParameterID("hmLayer", 1), "Layer")
		, m_owner(_owner)
	{
	}

	int HostMacros::LayerParam::toLayer(const float _value)
	{
		return juce::jlimit(1, LayerCount, 1 + static_cast<int>(std::lround(_value * static_cast<float>(LayerCount - 1))));
	}

	void HostMacros::LayerParam::setValue(const float _newValue)
	{
		m_value.store(_newValue);
		m_owner.m_layer.store(toLayer(_newValue));
	}

	// ---- HostMacros -------------------------------------------------------------------

	HostMacros::HostMacros(Processor& _processor) : m_processor(_processor)
	{
		for (int i = 0; i < KnobCount; ++i)
		{
			auto* k = new Knob(*this, i);
			m_knobs[static_cast<size_t>(i)] = k;
			m_knobNames[static_cast<size_t>(i)] = "Knob " + juce::String(i + 1);
			_processor.addParameter(k);	// owned by the processor
		}

		m_layerParam = new LayerParam(*this);
		_processor.addParameter(m_layerParam);

		m_noteChannel.fill(0xff);

		startTimerHz(30);
	}

	HostMacros::~HostMacros()
	{
		stopTimer();
	}

	bool HostMacros::remapMidi(synthLib::SMidiEvent& _ev)
	{
		if (!_ev.sysex.empty())
			return true;

		const auto status = _ev.a;
		if (status < 0x80 || status >= 0xf0)
			return true;

		const auto type = static_cast<uint8_t>(status & 0xf0);

		if (type == 0xb0 && _ev.b == LayerCC)
		{
			m_layer.store(juce::jlimit(1, LayerCount, static_cast<int>(_ev.c)));
			return false;
		}

		auto ch = static_cast<uint8_t>(m_layer.load() - 1);
		const auto note = static_cast<uint8_t>(_ev.b & 0x7f);

		if (type == 0x90 && _ev.c > 0)
		{
			m_noteChannel[note] = ch;
		}
		else if (type == 0x80 || type == 0x90)
		{
			// note-off: to the part that got the note-on, even if Layer changed meanwhile
			if (m_noteChannel[note] != 0xff)
				ch = m_noteChannel[note];
			m_noteChannel[note] = 0xff;
		}

		_ev.a = static_cast<uint8_t>(type | ch);
		return true;
	}

	void HostMacros::saveChunkData(baseLib::BinaryStream& _s) const
	{
		baseLib::ChunkWriter cw(_s, "HMAC", 1);

		std::lock_guard lock(m_mutex);

		_s.write(static_cast<uint32_t>(m_layer.load()));

		for (int l = 0; l < LayerCount; ++l)
		{
			for (int k = 0; k < KnobCount; ++k)
			{
				_s.write(m_targets[l][k]);
				_s.write(m_customNames[l][k]);
			}
		}
	}

	void HostMacros::loadChunkData(baseLib::ChunkReader& _cr)
	{
		_cr.add("HMAC", 1, [this](baseLib::BinaryStream& _s, uint32_t)
		{
			const auto layer = _s.read<uint32_t>();
			{
				std::lock_guard lock(m_mutex);

				for (int l = 0; l < LayerCount; ++l)
				{
					for (int k = 0; k < KnobCount; ++k)
					{
						m_targets[l][k] = _s.readString();
						m_customNames[l][k] = _s.readString();
					}
				}
			}
			if (layer >= 1 && layer <= LayerCount)
			{
				m_layer.store(static_cast<int>(layer));
				m_layerParam->setSilently(static_cast<int>(layer));
			}
			m_resolveRequest = true;
		});
	}

	int HostMacros::getLayer() const
	{
		return m_layer.load();
	}

	std::string HostMacros::getTarget(const int _layer, const int _knob) const
	{
		if (_layer < 1 || _layer > LayerCount || _knob < 0 || _knob >= KnobCount)
			return {};
		std::lock_guard lock(m_mutex);
		return m_targets[_layer - 1][_knob];
	}

	void HostMacros::setTarget(const int _layer, const int _knob, const std::string& _paramName)
	{
		if (_layer < 0 || _layer > LayerCount || _knob < 0 || _knob >= KnobCount)
			return;
		{
			std::lock_guard lock(m_mutex);
			for (int l = 1; l <= LayerCount; ++l)
				if (_layer == 0 || _layer == l)
					m_targets[l - 1][_knob] = _paramName;
		}
		m_resolveRequest = true;
		timerCallback();
	}

	std::string HostMacros::getCustomName(const int _layer, const int _knob) const
	{
		if (_layer < 1 || _layer > LayerCount || _knob < 0 || _knob >= KnobCount)
			return {};
		std::lock_guard lock(m_mutex);
		return m_customNames[_layer - 1][_knob];
	}

	void HostMacros::setCustomName(const int _layer, const int _knob, const std::string& _name)
	{
		if (_layer < 0 || _layer > LayerCount || _knob < 0 || _knob >= KnobCount)
			return;
		{
			std::lock_guard lock(m_mutex);
			for (int l = 1; l <= LayerCount; ++l)
				if (_layer == 0 || _layer == l)
					m_customNames[l - 1][_knob] = juce::String(_name).trim().toStdString();
		}
		m_resolveRequest = true;
		timerCallback();
	}

	juce::String HostMacros::getKnobName(const int _knob) const
	{
		std::lock_guard lock(m_mutex);
		return m_knobNames[static_cast<size_t>(_knob)];
	}

	juce::String HostMacros::getKnobText(const int _knob, const float _value, const int _maxLen) const
	{
		const auto* p = m_active[static_cast<size_t>(_knob)].load();
		if (!p)
			return "-";

		const auto& range = p->getNormalisableRange();
		const auto raw = std::round(range.convertFrom0to1(juce::jlimit(0.0f, 1.0f, _value)));
		auto text = abbreviate(p->getText(range.convertTo0to1(raw), 0));
		if (_maxLen > 0)
			text = text.substring(0, _maxLen);
		return text;
	}

	void HostMacros::onKnobFromHost(const int _knob, const float _value)
	{
		if (auto* p = m_active[static_cast<size_t>(_knob)].load())
			p->setValue(_value);	// same path as host automation of the parameter itself
	}

	void HostMacros::setLayer(const int _layer)
	{
		if (_layer < 1 || _layer > LayerCount)
			return;
		m_layer.store(_layer);
		m_layerParam->setSilently(_layer);
		timerCallback();
	}

	juce::String HostMacros::getValueText(const int _knob) const
	{
		if (_knob < 0 || _knob >= KnobCount)
			return {};
		return getKnobText(_knob, m_knobs[static_cast<size_t>(_knob)]->getValue(), 0);
	}

	std::string HostMacros::getTargetDisplayName(const int _layer, const int _knob) const
	{
		const auto name = getTarget(_layer, _knob);
		if (name.empty() || !m_processor.hasController())
			return name;
		if (const auto* p = m_processor.getController().getParameter(name, static_cast<uint8_t>(_layer - 1)))
			return p->getDescription().displayName;
		return name;
	}

	void HostMacros::announceToHost()
	{
		m_announceStep = 0;
	}

	void HostMacros::timerCallback()
	{
		if (!m_processor.hasController())
			return;

		const auto layer = m_layer.load();

		if (m_resolveRequest)
		{
			applyLayer(layer);
		}
		else if (layer != m_appliedLayer)
		{
			// switch knobs + values right away; names (Live re-reads ALL parameters on a
			// title change) and the editor part only once Layer stops moving
			m_appliedLayer = layer;
			resolveTargets();
			++m_revision;
			syncKnobsFromTargets(true);
			m_settleTicks = 9;	// ~300 ms
		}
		else
		{
			syncKnobsFromTargets(false);
		}

		if (m_settleTicks > 0 && --m_settleTicks == 0)
		{
			namesChanged();
			m_processor.onHostSelectPart(static_cast<uint8_t>(m_appliedLayer - 1));
		}

		// for Live's "Configure": touch the knobs one by one
		if (m_announceStep >= 0)
		{
			auto* knob = m_knobs[static_cast<size_t>(m_announceStep)];
			knob->beginChangeGesture();
			knob->setFromTarget(knob->getValue());
			knob->endChangeGesture();

			if (++m_announceStep >= KnobCount)
				m_announceStep = -1;
		}
	}

	void HostMacros::applyLayer(const int _layer)
	{
		const bool layerChanged = _layer != m_appliedLayer;
		m_appliedLayer = _layer;
		m_resolveRequest = false;

		resolveTargets();
		namesChanged();
		++m_revision;
		syncKnobsFromTargets(true);

		if (layerChanged)
			m_processor.onHostSelectPart(static_cast<uint8_t>(_layer - 1));
	}

	void HostMacros::resolveTargets()
	{
		auto& controller = m_processor.getController();
		const auto part = static_cast<uint8_t>(m_appliedLayer - 1);

		std::lock_guard lock(m_mutex);

		for (int k = 0; k < KnobCount; ++k)
		{
			const auto& name = m_targets[static_cast<size_t>(part)][k];
			Parameter* p = name.empty() ? nullptr : controller.getParameter(name, part);
			m_active[static_cast<size_t>(k)].store(p);

			const auto& custom = m_customNames[static_cast<size_t>(part)][k];
			if (!custom.empty())
				m_knobNames[static_cast<size_t>(k)] = custom;
			else if (p)
				m_knobNames[static_cast<size_t>(k)] = shortName(p->getDescription().displayName);
			else
				m_knobNames[static_cast<size_t>(k)] = "Knob " + juce::String(k + 1);
		}
	}

	void HostMacros::namesChanged()
	{
		{
			std::lock_guard lock(m_mutex);
			if (m_reportedNames == m_knobNames)
				return;
			m_reportedNames = m_knobNames;
		}
		m_processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails().withParameterInfoChanged(true));
	}

	void HostMacros::syncKnobsFromTargets(const bool _force)
	{
		for (int k = 0; k < KnobCount; ++k)
		{
			auto* knob = m_knobs[static_cast<size_t>(k)];
			const auto* p = m_active[static_cast<size_t>(k)].load();

			if (!p)
			{
				if (_force)
					knob->setFromTarget(0.0f);
				continue;
			}

			const auto& range = p->getNormalisableRange();
			const auto target = static_cast<float>(p->getUnnormalizedValue());
			const auto shown = std::round(range.convertFrom0to1(knob->getValue()));

			// compared in the parameter's own steps: a knob between two steps is not "changed"
			if (_force || static_cast<int>(shown) != static_cast<int>(target))
				knob->setFromTarget(range.convertTo0to1(target));
		}
	}
}

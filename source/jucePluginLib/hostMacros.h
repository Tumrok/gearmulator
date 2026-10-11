#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <string>

#include <juce_audio_processors/juce_audio_processors.h>

namespace baseLib
{
	class BinaryStream;
	class ChunkReader;
}

namespace synthLib
{
	struct SMidiEvent;
}

namespace pluginLib
{
	class Processor;
	class Parameter;
	class HostMacros;

	// OsTIrus_mod_v2: 16 host knobs + "Layer", the first parameters the plugin shows the host.
	//
	// Layer (1..16) picks the part: incoming host MIDI is re-channelled to it (a note-off
	// goes to the channel of its note-on), and the editor shows that part.
	// Every layer has its own knob layout: knob i of layer L controls one parameter of
	// part L (assigned in the editor: right click on a parameter -> Push knob).
	// The host sees the knob's name as the assigned parameter's name and its value as
	// that parameter's value text, so Push shows "F1 Cutoff 64" instead of "Knob 1 0.5".
	// Knobs follow their parameter both ways (editor / preset changes show on Push).
	class HostMacros : juce::Timer
	{
	public:
		static constexpr int KnobCount = 16;
		static constexpr int LayerCount = 16;

		class Knob final : public juce::AudioProcessorParameterWithID
		{
		public:
			Knob(HostMacros& _owner, int _index);

			float getValue() const override { return m_value.load(); }
			void setValue(float _newValue) override;
			float getDefaultValue() const override { return 0.0f; }
			juce::String getName(int _maximumStringLength) const override;
			juce::String getLabel() const override { return {}; }
			juce::String getText(float _normalisedValue, int _maximumStringLength) const override;
			float getValueForText(const juce::String& _text) const override;

			void setFromTarget(float _value);	// message thread: show the target's value on the host

		private:
			HostMacros& m_owner;
			const int m_index;
			std::atomic<float> m_value{0.0f};
		};

		explicit HostMacros(Processor& _processor);	// adds the parameters to the processor
		~HostMacros() override;

		// audio thread, host MIDI before it reaches the device
		void remapMidi(synthLib::SMidiEvent& _ev);

		void saveChunkData(baseLib::BinaryStream& _s) const;
		void loadChunkData(baseLib::ChunkReader& _cr);

		// editor
		int getLayer() const;	// 1..16
		std::string getTarget(int _layer, int _knob) const;	// parameter name, empty = none
		void setTarget(int _layer, int _knob, const std::string& _paramName);	// _layer 0 = all layers
		std::string getCustomName(int _layer, int _knob) const;
		void setCustomName(int _layer, int _knob, const std::string& _name);
		juce::String getKnobName(int _knob) const;	// as the host shows it (current layer)
		void announceToHost();	// touch every knob + Layer, in order (for Live's plugin "Configure")

	private:
		void timerCallback() override;
		void onKnobFromHost(int _knob, float _value);
		juce::String getKnobText(int _knob, float _value, int _maxLen) const;
		void applyLayer(int _layer);
		void resolveTargets();
		void syncKnobsFromTargets(bool _force);
		void namesChanged();

		Processor& m_processor;
		std::array<Knob*, KnobCount> m_knobs{};
		juce::AudioParameterInt* m_layerParam = nullptr;

		mutable std::mutex m_mutex;	// guards the layout and names below
		std::array<std::array<std::string, KnobCount>, LayerCount> m_targets;
		std::array<std::array<std::string, KnobCount>, LayerCount> m_customNames;
		std::array<juce::String, KnobCount> m_knobNames;	// current layer, as shown to the host

		std::array<std::atomic<Parameter*>, KnobCount> m_active{};	// targets of the current layer
		int m_appliedLayer = 0;	// message thread
		std::atomic<bool> m_resolveRequest{true};
		std::atomic<int> m_storedLayer{0};	// from a loaded state, applied by the timer

		std::array<uint8_t, 128> m_noteChannel{};	// audio thread: channel of each held note, 0xff = none
		int m_announceStep = -1;
	};
}

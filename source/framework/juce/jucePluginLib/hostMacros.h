#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

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

	// a preset of a user bank of the patch manager, for HostMacros' User Bank / Preset knobs
	struct HostPreset
	{
		std::string name;
		std::vector<uint8_t> sysex;	// as stored, may hold several messages (multi + singles)
		std::string hash;			// patch manager hash (hex), to find a rename in the bank's json
	};

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
		static constexpr int KnobCount = 32;
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

		// Layer for the mouse / automation: 16 steps over the whole range
		class LayerParam final : public juce::AudioProcessorParameterWithID
		{
		public:
			explicit LayerParam(HostMacros& _owner);

			static float toValue(const int _layer) { return static_cast<float>(_layer - 1) / static_cast<float>(LayerCount - 1); }
			static int toLayer(float _value);

			float getValue() const override { return m_value.load(); }
			void setValue(float _newValue) override;
			void setSilently(int _layer) { m_value.store(toValue(_layer)); }	// not reported to the host
			float getDefaultValue() const override { return 0.0f; }
			int getNumSteps() const override { return LayerCount; }
			bool isDiscrete() const override { return true; }
			juce::String getText(float _normalisedValue, int) const override { return juce::String(toLayer(_normalisedValue)); }
			float getValueForText(const juce::String& _text) const override { return toValue(_text.getIntValue()); }

		private:
			HostMacros& m_owner;
			std::atomic<float> m_value{0.0f};
		};

		// Which layer the knobs show and edit (Push bank 3). Follows the playing layer
		// whenever that changes; turning it previews another layer's knobs while only the
		// playing layer gets the notes. Value text: "2. [C] Lead" ([C] = playing layer).
		class EditLayerParam final : public juce::AudioProcessorParameterWithID
		{
		public:
			explicit EditLayerParam(HostMacros& _owner);

			float getValue() const override { return m_value.load(); }
			void setValue(float _newValue) override;
			float getDefaultValue() const override { return 0.0f; }
			int getNumSteps() const override { return LayerCount; }
			bool isDiscrete() const override { return true; }
			juce::String getText(float _normalisedValue, int) const override;
			float getValueForText(const juce::String& _text) const override { return LayerParam::toValue(_text.getIntValue()); }

			void setFromOwner(int _layer);	// message thread: report to the host, no write back

		private:
			HostMacros& m_owner;
			std::atomic<float> m_value{0.0f};
		};

		// User Bank / Preset (Push bank 5, after Edit Layer): a user bank of the patch manager
		// and one of its presets, loaded into the Edit Layer's part. Continuous 0..1 over
		// the list; active only with "User preset bank" switched on.
		class ListParam final : public juce::AudioProcessorParameterWithID
		{
		public:
			ListParam(HostMacros& _owner, const juce::String& _id, const juce::String& _name, bool _isPreset);

			float getValue() const override { return m_value.load(); }
			void setValue(float _newValue) override;
			float getDefaultValue() const override { return 0.0f; }
			juce::String getText(float _normalisedValue, int) const override;
			float getValueForText(const juce::String&) const override { return getValue(); }

			void setFromOwner(float _value);	// message thread: report to the host, no write back
			static int toIndex(float _value, size_t _count);

		private:
			HostMacros& m_owner;
			const bool m_isPreset;
			std::atomic<float> m_value{0.0f};
		};

		// MIDI CC that selects the layer from a clip (MIDI Ctrl envelope): value 1..16 = layer.
		// Free in the Virus CC map; consumed, it never reaches the synth.
		static constexpr uint8_t LayerCC = 3;

		explicit HostMacros(Processor& _processor);	// adds the parameters to the processor
		~HostMacros() override;

		// audio thread, host MIDI before it reaches the device. false = consumed (Layer CC)
		bool remapMidi(synthLib::SMidiEvent& _ev);

		void saveChunkData(baseLib::BinaryStream& _s) const;
		void saveLayerNames(baseLib::BinaryStream& _s) const;
		void loadChunkData(baseLib::ChunkReader& _cr);

		// editor
		int getLayer() const;	// 1..16, the playing layer
		int getEditLayer() const;	// 1..16, the layer the knobs show
		void setEditLayer(int _layer);
		std::string getLayerName(int _layer) const;	// custom name, empty = none
		void setLayerName(int _layer, const std::string& _name);
		std::string getLayerDisplayName(int _layer) const;	// custom name or "Layer N"
		juce::String getLayerListText(int _layer) const;	// "2. [C] Lead"
		std::string getTarget(int _layer, int _knob) const;	// parameter name, empty = none
		void setTarget(int _layer, int _knob, const std::string& _paramName);	// _layer 0 = all layers
		std::string getCustomName(int _layer, int _knob) const;
		void setCustomName(int _layer, int _knob, const std::string& _name);
		juce::String getKnobName(int _knob) const;	// as the host shows it (current layer)
		void announceToHost();	// touch every knob, then Edit Layer, in order (for Live's plugin "Configure")
		void setLayer(int _layer);	// UI: not reported to the host (would switch off Layer automation)
		juce::String getValueText(int _knob) const;	// current layer
		std::string getTargetDisplayName(int _layer, int _knob) const;
		uint32_t getRevision() const { return m_revision.load(); }	// changes with layout / names / layer

		// user preset banks (patch manager "User" banks, read from its folder)
		bool getPresetMode() const { return m_presetMode.load(); }
		void setPresetMode(bool _on);
		void saveExtraChunks(baseLib::BinaryStream& _s) const;

	private:
		void timerCallback() override;
		void onKnobFromHost(int _knob, float _value);
		juce::String getKnobText(int _knob, float _value, int _maxLen) const;
		void applyLayer(int _layer);
		void resolveTargets();
		void syncKnobsFromTargets(bool _force);
		void namesChanged();
		void scanBanks();
		void loadBankPresets(int _bank);
		void updatePresetSelection();
		juce::String getListText(bool _isPreset, float _value) const;

		Processor& m_processor;
		std::array<Knob*, KnobCount> m_knobs{};
		LayerParam* m_layerParam = nullptr;
		EditLayerParam* m_editLayerParam = nullptr;
		ListParam* m_bankParam = nullptr;
		ListParam* m_presetParam = nullptr;

		mutable std::mutex m_mutex;	// guards the layout and names below
		std::array<std::array<std::string, KnobCount>, LayerCount> m_targets;
		std::array<std::array<std::string, KnobCount>, LayerCount> m_customNames;
		std::array<std::string, LayerCount> m_layerNames;
		std::array<juce::String, KnobCount> m_knobNames;	// current layer, as shown to the host
		std::array<juce::String, KnobCount> m_reportedNames;	// names the host was last told about

		std::array<std::atomic<Parameter*>, KnobCount> m_active{};	// targets of the current layer
		int m_appliedLayer = 0;	// message thread
		std::atomic<bool> m_resolveRequest{true};
		std::atomic<int> m_layer{1};	// playing layer: last of Layer parameter / Layer CC / UI
		std::atomic<int> m_editLayer{1};	// layer the knobs show
		int m_lastPlayLayer = 0;	// message thread
		bool m_editTextChanged = false;	// layer names changed: report the Edit Layer text again

		std::array<uint8_t, 128> m_noteChannel{};	// audio thread: channel of each held note, 0xff = none
		int m_announceStep = -1;
		int m_settleTicks = 0;	// countdown to the name / editor update after a Layer change
		std::atomic<uint32_t> m_revision{0};

		// presets (lists guarded by m_mutex)
		std::atomic<bool> m_presetMode{false};
		std::vector<std::string> m_bankNames;
		int m_loadedBank = -2;				// bank whose presets are in m_presets, -2 = scan again
		std::vector<HostPreset> m_presets;
		std::atomic<bool> m_presetTouched{false};	// Preset knob moved by the host
		std::atomic<bool> m_bankTouched{false};		// User Bank knob moved by the host
		int m_presetSettleTicks = 0;
		int m_startTicks = 0;				// no preset loads while the host restores the plugin
	};
}

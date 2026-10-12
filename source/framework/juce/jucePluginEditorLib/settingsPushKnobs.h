#pragma once

#include <array>
#include <cstdint>

#include "settingsPlugin.h"

#include "juce_events/juce_events.h"

namespace Rml
{
	class Element;
	class ElementFormControlInput;
}

namespace pluginLib
{
	class HostMacros;
}

namespace jucePluginEditorLib
{
	// OsTIrus_mod_v2: Layer switch + the knob layout of the current layer (see pluginLib::HostMacros)
	class SettingsPushKnobs : public SettingsPlugin, juce::Timer
	{
	public:
		static constexpr int Count = 16;

		SettingsPushKnobs(Processor& _processor);
		~SettingsPushKnobs() override;

		std::string getCategoryName() const override { return "Push Knobs"; }
		std::string getTemplateName() const override { return "tus_settings_pushknobs"; }

		void createUi(Rml::Element* _root) override;

	private:
		void timerCallback() override;
		void refreshLayout();
		void refreshValues();

		pluginLib::HostMacros& m_macros;

		std::array<Rml::Element*, Count> m_layerButtons{};
		Rml::Element* m_layerNameLabel = nullptr;
		Rml::ElementFormControlInput* m_layerName = nullptr;
		int m_shownPlayLayer = 0;

		struct Row
		{
			Rml::Element* row = nullptr;
			Rml::Element* param = nullptr;
			Rml::ElementFormControlInput* name = nullptr;
			Rml::Element* value = nullptr;
			Rml::Element* clear = nullptr;
		};
		std::array<Row, Count> m_rows{};
		std::array<std::string, Count> m_shownValues;

		uint32_t m_seenRevision = 0xffffffff;
		int m_shownLayer = 0;
	};
}

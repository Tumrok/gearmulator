#include "settingsPushKnobs.h"

#include "pluginProcessor.h"

#include "jucePluginLib/hostMacros.h"

#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"
#include "juceRmlUi/rmlElemButton.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/Elements/ElementFormControlInput.h"

namespace jucePluginEditorLib
{
	SettingsPushKnobs::SettingsPushKnobs(Processor& _processor) : SettingsPlugin(_processor), m_macros(*_processor.getHostMacros())
	{
	}

	SettingsPushKnobs::~SettingsPushKnobs()
	{
		stopTimer();
	}

	void SettingsPushKnobs::createUi(Rml::Element* _root)
	{
		// 16 layer buttons from the first one
		auto* firstLayer = juceRmlUi::helper::findChild(_root, "btLayer");
		for (int l = 0; l < Count; ++l)
		{
			auto* bt = l == 0 ? firstLayer : firstLayer->GetParentNode()->AppendChild(firstLayer->Clone());
			bt->SetInnerRML(std::to_string(l + 1));
			m_layerButtons[static_cast<size_t>(l)] = bt;

			juceRmlUi::EventListener::AddClick(bt, [this, l]
			{
				m_macros.setLayer(l + 1);
				timerCallback();
			});
		}

		// name of the shown layer (empty = "Layer N")
		m_layerNameLabel = juceRmlUi::helper::findChild(_root, "lbLayerName");
		m_layerName = juceRmlUi::helper::findChildT<Rml::ElementFormControlInput>(_root, "edLayerName");
		juceRmlUi::EventListener::Add(m_layerName, Rml::EventId::Change, [this](Rml::Event&)
		{
			auto text = m_layerName->GetValue();
			if (text == "Layer " + std::to_string(m_shownLayer))
				text.clear();
			if (text == m_macros.getLayerName(m_shownLayer))
				return;
			m_macros.setLayerName(m_shownLayer, text);
			m_seenRevision = m_macros.getRevision();	// don't overwrite the field while typing
		});

		// knob rows from the first one
		auto* firstRow = juceRmlUi::helper::findChild(_root, "row");
		for (int k = 0; k < KnobRows; ++k)
		{
			auto* row = k == 0 ? firstRow : firstRow->GetParentNode()->AppendChild(firstRow->Clone());

			auto& r = m_rows[static_cast<size_t>(k)];
			r.row = row;
			r.param = juceRmlUi::helper::findChild(row, "lbParam");
			r.name = juceRmlUi::helper::findChildT<Rml::ElementFormControlInput>(row, "edName");
			r.value = juceRmlUi::helper::findChild(row, "lbValue");
			r.clear = juceRmlUi::helper::findChild(row, "btClear");

			juceRmlUi::helper::findChild(row, "lbKnob")->SetInnerRML(std::to_string(k + 1));

			juceRmlUi::EventListener::Add(r.name, Rml::EventId::Change, [this, k](Rml::Event&)
			{
				const auto layer = m_shownLayer;
				auto text = m_rows[static_cast<size_t>(k)].name->GetValue();

				// the shown auto name (= parameter name) is not a custom name
				if (text == m_macros.getKnobName(k).toStdString() && m_macros.getCustomName(layer, k).empty())
					return;

				m_macros.setCustomName(layer, k, text);
				m_seenRevision = m_macros.getRevision();	// don't overwrite the field while typing
			});

			juceRmlUi::EventListener::AddClick(r.clear, [this, k]
			{
				const auto layer = m_shownLayer;
				m_macros.setTarget(layer, k, {});
				m_macros.setCustomName(layer, k, {});
				timerCallback();
			});
		}

		// "User preset bank": User Bank / Preset knobs on (stored with the plugin state)
		if (auto* modeRoot = juceRmlUi::helper::findChild(_root, "btPresetMode", false))
		{
			m_presetModeButton = juceRmlUi::helper::findChild(modeRoot, "button");
			juceRmlUi::EventListener::AddClick(modeRoot, [this]
			{
				m_macros.setPresetMode(!m_macros.getPresetMode());
				timerCallback();
			});
		}

		if (auto* btConfigure = juceRmlUi::helper::findChild(_root, "btConfigure", false))
			juceRmlUi::EventListener::AddClick(btConfigure, [this] { m_macros.announceToHost(); });

		timerCallback();
		startTimerHz(10);
	}

	void SettingsPushKnobs::timerCallback()
	{
		if (m_macros.getRevision() != m_seenRevision || m_macros.getEditLayer() != m_shownLayer || m_macros.getLayer() != m_shownPlayLayer)
			refreshLayout();
		refreshValues();
	}

	void SettingsPushKnobs::refreshLayout()
	{
		m_seenRevision = m_macros.getRevision();
		m_shownLayer = m_macros.getEditLayer();
		m_shownPlayLayer = m_macros.getLayer();

		for (int l = 0; l < Count; ++l)
		{
			auto* bt = m_layerButtons[static_cast<size_t>(l)];
			bt->SetPseudoClass("checked", l + 1 == m_shownPlayLayer);
			bt->SetPseudoClass("editing", l + 1 == m_shownLayer && m_shownLayer != m_shownPlayLayer);
		}

		if (m_presetModeButton)
			juceRmlUi::ElemButton::setChecked(m_presetModeButton, m_macros.getPresetMode());

		m_layerNameLabel->SetInnerRML("Layer " + std::to_string(m_shownLayer) + " name");
		m_layerName->SetValue(m_macros.getLayerDisplayName(m_shownLayer));

		for (int k = 0; k < KnobRows; ++k)
		{
			auto& r = m_rows[static_cast<size_t>(k)];
			const auto target = m_macros.getTargetDisplayName(m_shownLayer, k);

			r.param->SetInnerRML(Rml::StringUtilities::EncodeRml(target.empty() ? std::string("-") : target));
			r.row->SetPseudoClass("empty", target.empty());
			// hidden, not removed: rows keep their layout
			const bool canClear = !target.empty() || !m_macros.getCustomName(m_shownLayer, k).empty();
			r.clear->SetProperty(Rml::PropertyId::Visibility, Rml::Property(canClear ? Rml::Style::Visibility::Visible : Rml::Style::Visibility::Hidden));

			const auto custom = m_macros.getCustomName(m_shownLayer, k);
			r.name->SetValue(custom.empty() && target.empty() ? std::string() : m_macros.getKnobName(k).toStdString());

			m_shownValues[static_cast<size_t>(k)].clear();
		}
	}

	void SettingsPushKnobs::refreshValues()
	{
		for (int k = 0; k < KnobRows; ++k)
		{
			const auto v = m_macros.getValueText(k).toStdString();
			auto& shown = m_shownValues[static_cast<size_t>(k)];
			if (v == shown)
				continue;
			shown = v;
			m_rows[static_cast<size_t>(k)].value->SetInnerRML(Rml::StringUtilities::EncodeRml(v));
		}
	}
}

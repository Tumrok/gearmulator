#include "settingsPushKnobs.h"

#include "pluginProcessor.h"

#include "jucePluginLib/hostMacros.h"

#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

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

		// 16 knob rows from the first one
		auto* firstRow = juceRmlUi::helper::findChild(_root, "row");
		for (int k = 0; k < Count; ++k)
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
				const auto layer = m_macros.getLayer();
				auto text = m_rows[static_cast<size_t>(k)].name->GetValue();

				// the shown auto name (= parameter name) is not a custom name
				if (text == m_macros.getKnobName(k).toStdString() && m_macros.getCustomName(layer, k).empty())
					return;

				m_macros.setCustomName(layer, k, text);
				m_seenRevision = m_macros.getRevision();	// don't overwrite the field while typing
			});

			juceRmlUi::EventListener::AddClick(r.clear, [this, k]
			{
				const auto layer = m_macros.getLayer();
				m_macros.setTarget(layer, k, {});
				m_macros.setCustomName(layer, k, {});
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
		if (m_macros.getRevision() != m_seenRevision || m_macros.getLayer() != m_shownLayer)
			refreshLayout();
		refreshValues();
	}

	void SettingsPushKnobs::refreshLayout()
	{
		m_seenRevision = m_macros.getRevision();
		m_shownLayer = m_macros.getLayer();

		for (int l = 0; l < Count; ++l)
			m_layerButtons[static_cast<size_t>(l)]->SetPseudoClass("checked", l + 1 == m_shownLayer);

		for (int k = 0; k < Count; ++k)
		{
			auto& r = m_rows[static_cast<size_t>(k)];
			const auto target = m_macros.getTargetDisplayName(m_shownLayer, k);

			r.param->SetInnerRML(Rml::StringUtilities::EncodeRml(target.empty() ? std::string("-") : target));
			r.row->SetPseudoClass("empty", target.empty());
			juceRmlUi::helper::setVisible(r.clear, !target.empty() || !m_macros.getCustomName(m_shownLayer, k).empty());

			const auto custom = m_macros.getCustomName(m_shownLayer, k);
			r.name->SetValue(custom.empty() && target.empty() ? std::string() : m_macros.getKnobName(k).toStdString());

			m_shownValues[static_cast<size_t>(k)].clear();
		}
	}

	void SettingsPushKnobs::refreshValues()
	{
		for (int k = 0; k < Count; ++k)
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

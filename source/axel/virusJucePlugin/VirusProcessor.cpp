#include "VirusProcessor.h"
#include "VirusEditorState.h"
#include "ParameterNames.h"
#include "VirusController.h"

#include "baseLib/binarystream.h"
#include "baseLib/filesystem.h"

#include "juceUiLib/messageBox.h"

#include "synthLib/deviceException.h"
#include "synthLib/lv2PresetExport.h"
#include "synthLib/midiToSysex.h"

#include "virusLib/microcontrollerTypes.h"

#include "juce_cryptography/hashing/juce_MD5.h"

namespace virus
{
	VirusProcessor::VirusProcessor(const BusesProperties& _busesProperties, const juce::PropertiesFile::Options& _configOptions, const pluginLib::Processor::Properties& _properties, const virusLib::DeviceModel _defaultModel)
		: Processor(_busesProperties, _configOptions, _properties)
		, m_defaultModel(_defaultModel)
	{
	}

	VirusProcessor::~VirusProcessor()
	{
		destroyController();
		destroyEditorState();
	}

	//==============================================================================

	void VirusProcessor::processBpm(const float _bpm)
	{
		// clamp to virus range, 63-190
		const auto bpmValue = juce::jmin(127, juce::jmax(0, static_cast<int>(_bpm)-63));
		const auto clockParam = getController().getParameter(m_clockTempoParam, 0);

		if (clockParam == nullptr || clockParam->getUnnormalizedValue() == bpmValue)
			return;

		clockParam->setUnnormalizedValue(bpmValue, pluginLib::Parameter::Origin::HostAutomation);
	}

	bool VirusProcessor::setSelectedRom(const uint32_t _index)
	{
		if(_index >= m_roms.size())
			return false;
		if(_index == m_selectedRom)
			return true;
		m_selectedRom = _index;

		try
		{
			synthLib::Device* device = pluginLib::Processor::createDevice(getDeviceType());
			getPlugin().setDevice(device);
			(void)m_device.release();
			m_device.reset(device);

			evRomChanged.retain(getSelectedRom());

			zynthianExportLv2Presets();

			return true;
		}
		catch(const synthLib::DeviceException& e)
		{
			genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
				"Device creation failed:",
				std::string("Failed to create device:\n\n") + 
				e.what() + "\n\n"
				"Will continue using old ROM");
			return false;
		}
	}

	void VirusProcessor::postConstruct(std::vector<virusLib::ROMFile>&& _roms)
	{
		m_roms = std::move(_roms);

		evRomChanged.retain(getSelectedRom());

		m_clockTempoParam = getController().getParameterIndexByName(virus::g_paramClockTempo);

		const auto latencyBlocks = getConfig().getIntValue("latencyBlocks", static_cast<int>(getPlugin().getLatencyBlocks()));
		Processor::setLatencyBlocks(latencyBlocks);

		zynthianExportLv2Presets();
	}

	synthLib::Device* VirusProcessor::createDevice()
	{
		synthLib::DeviceCreateParams p;
		getRemoteDeviceParams(p);
		return new virusLib::Device(p, true);
	}

	void VirusProcessor::getRemoteDeviceParams(synthLib::DeviceCreateParams& _params) const
	{
		pluginLib::Processor::getRemoteDeviceParams(_params);

		const auto* rom = getSelectedRom();

		if(!rom || !rom->isValid())
		{
			if(isTIFamily(m_defaultModel))
				throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing, "A Virus TI firmware (.bin) is required, but was not found.");
			throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing, "A Virus A/B/C operating system (.bin or .mid) is required, but was not found.");
		}

		_params.romName = rom->getFilename();
		_params.romData = rom->getRomFileData();
		_params.customData = static_cast<uint32_t>(rom->getModel());
	}

	pluginLib::Controller* VirusProcessor::createController()
	{
		// force creation of device as the controller decides how to initialize based on the used ROM
		getPlugin();

		return new virus::Controller(*this, m_defaultModel);
	}

	void VirusProcessor::saveChunkData(baseLib::BinaryStream& s)
	{
		auto* rom = getSelectedRom();
		if(rom)
		{
			baseLib::ChunkWriter cw(s, "ROM ", 2);
			const auto romName = baseLib::filesystem::getFilenameWithoutPath(rom->getFilename());
			s.write<uint8_t>(static_cast<uint8_t>(rom->getModel()));
			s.write(romName);
		}
		Processor::saveChunkData(s);
	}

	void VirusProcessor::loadChunkData(baseLib::ChunkReader& _cr)
	{
		_cr.add("ROM ", 2, [this](baseLib::BinaryStream& _binaryStream, unsigned _version)
		{
			auto model = virusLib::DeviceModel::ABC;

			if(_version > 1)
				model = static_cast<virusLib::DeviceModel>(_binaryStream.read<uint8_t>());

			const auto romName = _binaryStream.readString();

			const auto& roms = getRoms();
			for(uint32_t i=0; i<static_cast<uint32_t>(roms.size()); ++i)
			{
				const auto& rom = roms[i];
				if(rom.getModel() == model && baseLib::filesystem::getFilenameWithoutPath(rom.getFilename()) == romName)
					setSelectedRom(i);
			}
		});

		Processor::loadChunkData(_cr);
	}

	void VirusProcessor::zynthianExportLv2Presets() const
	{
		const auto* rom = getSelectedRom();
		if(!rom)
			return;

#ifdef ZYNTHIAN
		constexpr const char* const rootPath = "/zynthian/zynthian-my-data/presets/lv2";
		exportLv2Presets(*rom, rootPath);
#elif defined(_WIN32) && defined(_DEBUG)
//		const auto rootPath = synthLib::getModulePath();
//		exportLv2Presets(*rom, rootPath);
#endif
	}

	void VirusProcessor::exportLv2Presets(const virusLib::ROMFile& _rom, const std::string& _rootPath) const
	{
		const auto bankCount = virusLib::ROMFile::getRomBankCount(_rom.getModel());

		for(uint32_t b=0; b<bankCount; ++b)
		{
			synthLib::Lv2PresetExport::Bank bank;

			switch (_rom.getModel())
			{
			case virusLib::DeviceModel::A:
			case virusLib::DeviceModel::B:
			case virusLib::DeviceModel::C:				bank.name = std::string("Rom "     ) + static_cast<char>('C' + b);	break;
			case virusLib::DeviceModel::Snow:			bank.name = std::string("Snow Rom ") + static_cast<char>('A' + b);	break;
			case virusLib::DeviceModel::TI:				bank.name = std::string("TI Rom "  ) + static_cast<char>('A' + b);	break;
			case virusLib::DeviceModel::TI2:			bank.name = std::string("TI2 Rom " ) + static_cast<char>('A' + b);	break;
			case virusLib::DeviceModel::Invalid:		bank.name = std::string("Unknown " ) + static_cast<char>('A' + b);	assert(false); break;
			}

			const auto path = baseLib::filesystem::validatePath(_rootPath) + getProperties().name + '_' + synthLib::Lv2PresetExport::getBankFilename(bank.name) + ".lv2/";

			if(synthLib::Lv2PresetExport::manifestFileExists(path))
				continue;

			for(uint8_t p=0; p<_rom.getPresetsPerBank(); ++p)
			{
				virusLib::ROMFile::TPreset preset;

				if(!_rom.getSingle(static_cast<uint8_t>(b), p, preset))
					continue;

				synthLib::Lv2PresetExport::Preset lv2Preset;

				lv2Preset.data = {0xf0, 0x00, 0x20, 0x33, 01, virusLib::OMNI_DEVICE_ID, virusLib::DUMP_SINGLE, virusLib::toMidiByte(virusLib::BankNumber::EditBuffer), virusLib::SINGLE};
				lv2Preset.data.insert(lv2Preset.data.end(), preset.begin(), preset.begin() + virusLib::ROMFile::getSinglePresetSize(_rom.getModel()));
				lv2Preset.data.push_back(virusLib::Microcontroller::calcChecksum(lv2Preset.data));
				lv2Preset.data.push_back(0xf7);

				lv2Preset.name = virusLib::ROMFile::getSingleName(preset);

				bank.presets.push_back(lv2Preset);
			}

			if(bank.presets.empty())
				continue;

			synthLib::Lv2PresetExport::exportPresets(path, getProperties().lv2Uri, bank);
		}
	}

	// ---- OsTIrus_mod_v5: user bank presets ------------------------------------------------

	namespace
	{
		// as the patch manager hashes a dump (PatchManager::initializePatch): without the header,
		// bank/program, checksum and f7, so a rename stored for it in the bank's json matches
		std::string presetHash(const synthLib::SysexBufferList& _msgs)
		{
			std::vector<uint8_t> temp;
			constexpr size_t frontOffset = 9;
			constexpr size_t backOffset = 2;
			for (const auto& m : _msgs)
				if (m.size() > frontOffset + backOffset)
					temp.insert(temp.end(), m.begin() + frontOffset, m.end() - backOffset);
			const juce::MD5 md5(temp.data(), temp.size());
			return juce::String::toHexString(md5.getChecksumDataArray(), 16, 0).toLowerCase().toStdString();
		}

		std::string multiName(const synthLib::SysexBuffer& _multi)
		{
			constexpr size_t offset = 9 + virusLib::MultiDump::MD_NAME_CHAR_0;
			if (_multi.size() < offset + 10)
				return "Multi";
			std::string name(reinterpret_cast<const char*>(_multi.data()) + offset, 10);
			while (!name.empty() && name.back() == ' ')
				name.pop_back();
			return name.empty() ? "Multi" : name;
		}

		synthLib::SysexBuffer toEditBuffer(synthLib::SysexBuffer _multi)
		{
			if (_multi.size() > 8)
			{
				_multi[7] = virusLib::toMidiByte(virusLib::BankNumber::EditBuffer);
				_multi[8] = 0;
			}
			return _multi;
		}
	}

	bool VirusProcessor::parseHostPresets(const std::vector<uint8_t>& _fileData, std::vector<pluginLib::HostPreset>& _results)
	{
		synthLib::SysexBufferList msgs;
		synthLib::MidiToSysex::splitMultipleSysex(msgs, synthLib::SysexBuffer(_fileData.begin(), _fileData.end()));

		const auto& c = static_cast<const Controller&>(getController());

		for (size_t i = 0; i < msgs.size(); ++i)
		{
			const auto& msg = msgs[i];
			if (msg.size() < 10)
				continue;

			const auto cmd = msg[6];

			if (cmd == virusLib::SysexMessageType::DUMP_MULTI)
			{
				// an arrangement is stored as the multi followed by its 16 singles
				size_t singles = 0;
				while (singles < 16 && i + 1 + singles < msgs.size() && msgs[i + 1 + singles].size() > 6 && msgs[i + 1 + singles][6] == virusLib::SysexMessageType::DUMP_SINGLE)
					++singles;

				synthLib::SysexBufferList parts(msgs.begin() + static_cast<std::ptrdiff_t>(i), msgs.begin() + static_cast<std::ptrdiff_t>(i + 1 + (singles == 16 ? 16 : 0)));

				pluginLib::HostPreset preset;
				preset.name = multiName(msg);
				preset.hash = presetHash(parts);
				for (const auto& m : parts)
					preset.sysex.insert(preset.sysex.end(), m.begin(), m.end());
				_results.push_back(std::move(preset));

				if (singles == 16)
					i += 16;
				continue;
			}

			if (cmd != virusLib::SysexMessageType::DUMP_SINGLE || msg.size() < 267)
				continue;

			pluginLib::MidiPacket::Data data;
			pluginLib::MidiPacket::AnyPartParamValues values;
			if (!c.parseSingle(data, values, msg))
				continue;

			pluginLib::HostPreset preset;
			preset.name = c.getSinglePresetName(values);
			preset.hash = presetHash({msg});
			preset.sysex.assign(msg.begin(), msg.end());
			_results.push_back(std::move(preset));
		}
		return !_results.empty();
	}

	bool VirusProcessor::activateHostPreset(const pluginLib::HostPreset& _preset, const uint8_t _part)
	{
		synthLib::SysexBufferList msgs;
		synthLib::MidiToSysex::splitMultipleSysex(msgs, synthLib::SysexBuffer(_preset.sysex.begin(), _preset.sysex.end()));
		if (msgs.empty() || msgs.front().size() < 10)
			return false;

		auto& c = static_cast<Controller&>(getController());

		if (msgs.front()[6] == virusLib::SysexMessageType::DUMP_SINGLE)
			return c.activatePatch(msgs.front(), _part);

		if (msgs.front()[6] != virusLib::SysexMessageType::DUMP_MULTI)
			return false;

		// a multi / arrangement replaces all parts, as in the patch manager
		const auto multi = toEditBuffer(msgs.front());
		c.sendSysEx(pluginLib::SysEx(multi.begin(), multi.end()));
		if (msgs.size() == 17)
		{
			for (uint8_t i = 0; i < 16; ++i)
				c.activatePatch(msgs[i + 1], i);
		}
		c.requestArrangement();
		return true;
	}
}

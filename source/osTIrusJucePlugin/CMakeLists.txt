cmake_minimum_required(VERSION 3.15)
project(osTIrusJucePlugin VERSION ${CMAKE_PROJECT_VERSION}) 

set(SOURCES
	skins/TrancyTI/VirusTI_Trancy.rml
	skins/TrancyTI/VirusTI_Trancy.rcss
	parameterDescriptions_TI.json
	OsTIrusEditorState.cpp
	OsTIrusEditorState.h
	OsTIrusProcessor.cpp
	OsTIrusProcessor.h

	tus_settings_gui_OsTIrus.rcss
	tus_settings_gui_OsTIrus.rml
	tus_settings_dspaudio_OsTIrus.rcss
	tus_settings_dspaudio_OsTIrus.rml
)

SET(ASSETS 
	"parameterDescriptions_TI.json"
	"tus_settings_gui_OsTIrus.rml"
	"tus_settings_gui_OsTIrus.rcss"
	"tus_settings_dspaudio_OsTIrus.rml"
	"tus_settings_dspaudio_OsTIrus.rcss"
)

addSkin("OsTIrus" "TrancyTI" "skins/TrancyTI" "VirusTI_Trancy.rml")

buildSkinHeader()

juce_add_binary_data(osTIrusJucePlugin_BinaryData SOURCES ${ASSETS} ${ASSETS_SKINS})

# Product name / plugin codes can be overridden to build a side-by-side variant,
# e.g. -DOSTIRUS_PRODUCT_NAME=OsTIrus_mod -DOSTIRUS_CODE_SYNTH=Ttim -DOSTIRUS_CODE_FX=Ttin
set(OSTIRUS_PRODUCT_NAME "OsTIrus" CACHE STRING "OsTIrus product name")
set(OSTIRUS_CODE_SYNTH "Ttip" CACHE STRING "OsTIrus synth plugin 4CC")
set(OSTIRUS_CODE_FX "Ttif" CACHE STRING "OsTIrus FX plugin 4CC")

createJucePluginWithFX(osTIrusJucePlugin "${OSTIRUS_PRODUCT_NAME}" "${OSTIRUS_CODE_SYNTH}" "${OSTIRUS_CODE_FX}" osTIrusJucePlugin_BinaryData virusJucePlugin)

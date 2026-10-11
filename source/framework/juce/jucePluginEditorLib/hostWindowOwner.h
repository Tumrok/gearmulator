#pragma once

namespace juce
{
	class Component;
}

namespace jucePluginEditorLib
{
	// Max (vst~ in Ableton Live) opens the plugin editor in a plain top-level window:
	// a click in Live sends it behind Live's main window. Keep it above the host the
	// way the host's own plugin windows stay: Windows - owned by the host's main window,
	// macOS - floating level, hidden while the host is in the background.
	// Does nothing if the host already owns / floats the window.
	// Returns false while the editor is not on a native window yet (try again later).
	bool keepHostWindowOnTop(juce::Component& _editor);
}

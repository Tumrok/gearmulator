#include "hostWindowOwner.h"

#include <juce_gui_basics/juce_gui_basics.h>

#import <AppKit/AppKit.h>

namespace jucePluginEditorLib
{
	bool keepHostWindowOnTop(juce::Component& _editor)
	{
		auto* peer = _editor.getPeer();
		if (!peer)
			return false;

		auto* view = static_cast<NSView*>(peer->getNativeHandle());
		NSWindow* window = view ? [view window] : nil;
		if (!window)
			return false;

		if ([window level] != NSNormalWindowLevel || [window parentWindow])
			return true;	// the host floats it already

		[window setLevel:NSFloatingWindowLevel];
		[window setHidesOnDeactivate:YES];
		return true;
	}
}

#include "hostWindowOwner.h"

#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_WINDOWS
#include <windows.h>
#endif

namespace jucePluginEditorLib
{
#if JUCE_WINDOWS
	namespace
	{
		struct MainWindowSearch
		{
			HWND exclude = nullptr;
			HWND best = nullptr;
			LONGLONG bestArea = 0;
		};

		BOOL CALLBACK findMainWindow(HWND _hwnd, LPARAM _param)
		{
			auto& s = *reinterpret_cast<MainWindowSearch*>(_param);

			DWORD pid = 0;
			GetWindowThreadProcessId(_hwnd, &pid);

			if (pid != GetCurrentProcessId() || _hwnd == s.exclude || !IsWindowVisible(_hwnd) || GetWindow(_hwnd, GW_OWNER))
				return TRUE;

			RECT r;
			if (!GetWindowRect(_hwnd, &r))
				return TRUE;

			const auto area = static_cast<LONGLONG>(r.right - r.left) * static_cast<LONGLONG>(r.bottom - r.top);
			if (area > s.bestArea)
			{
				s.bestArea = area;
				s.best = _hwnd;
			}
			return TRUE;
		}
	}

	bool keepHostWindowOnTop(juce::Component& _editor)
	{
		auto* peer = _editor.getPeer();
		if (!peer)
			return false;

		const auto root = GetAncestor(static_cast<HWND>(peer->getNativeHandle()), GA_ROOT);
		if (!root)
			return false;

		if (GetWindow(root, GW_OWNER))
			return true;	// the host owns it already

		// the host's main window = the biggest visible unowned top-level window of this process
		MainWindowSearch s;
		s.exclude = root;
		EnumWindows(findMainWindow, reinterpret_cast<LPARAM>(&s));

		if (!s.best)
			return true;

		SetWindowLongPtr(root, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(s.best));
		return true;
	}
#elif !JUCE_MAC
	bool keepHostWindowOnTop(juce::Component&)
	{
		return true;
	}
#endif
}

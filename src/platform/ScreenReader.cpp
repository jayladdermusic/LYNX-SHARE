#include "platform/ScreenReader.h"

#include <juce_core/juce_core.h>

#if JUCE_WINDOWS
 #include <windows.h>
#elif JUCE_MAC
 #include <objc/message.h>
 #include <objc/runtime.h>
#endif

namespace lynx::platform
{
bool screenReaderRunning()
{
#if JUCE_WINDOWS
    BOOL running = FALSE;
    return SystemParametersInfoW (SPI_GETSCREENREADER, 0, &running, 0) && running;
#elif JUCE_MAC
    // [[NSWorkspace sharedWorkspace] isVoiceOverEnabled] (macOS 10.13+)
    auto* workspaceClass = (id) objc_getClass ("NSWorkspace");
    if (workspaceClass == nullptr)
        return false;
    auto* workspace = ((id (*) (id, SEL)) objc_msgSend) (workspaceClass, sel_registerName ("sharedWorkspace"));
    return workspace != nullptr && ((BOOL (*) (id, SEL)) objc_msgSend) (workspace, sel_registerName ("isVoiceOverEnabled"));
#else
    return false;
#endif
}
} // namespace lynx::platform

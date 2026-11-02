#pragma once

namespace lynx::platform
{
// Whether a screen reader is running now: VoiceOver on macOS, any reader that sets the system's screen reader flag
// on Windows (NVDA, JAWS, Narrator). Always false on Linux, where JUCE has no accessibility support.
bool screenReaderRunning();
} // namespace lynx::platform

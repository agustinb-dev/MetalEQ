/*
    MetalEQ - serving of the embedded WebView UI assets.

    The React front-end is built by Vite into dist/ (index.html +
    assets/app.js + assets/app.css), zipped by CMake and embedded with
    juce_add_binary_data as BinaryData::metaleq_ui_zip. This helper looks up
    URLs in that archive and returns them as WebBrowserComponent resources.

    Security: the URL path is strictly validated (no "..", no backslashes,
    no scheme, single leading slash stripped) before it is handed to the
    archive, so a hostile page can never escape the archive root.

    When the UI was not embedded at build time (npm unavailable), a small
    inline placeholder page is served instead; the editor detects that the
    real app never called uiReady() and falls back to the native editor.
*/

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <optional>

namespace metaleq::ui
{

/** Maps a resource-provider URL to an embedded UI file, or std::nullopt.

    "/" resolves to "index.html". Unknown paths and any path that fails
    validation return std::nullopt (the WebBrowserComponent then reports a
    404 to the page).
*/
std::optional<juce::WebBrowserComponent::Resource> findWebUiResource (const juce::String& url);

/** True when the Vite build output was embedded into this binary. */
bool isWebUiEmbedded() noexcept;

/** MIME type for a file extension; "application/octet-stream" when unknown. */
const char* webUiMimeTypeFor (const juce::String& extension) noexcept;

} // namespace metaleq::ui

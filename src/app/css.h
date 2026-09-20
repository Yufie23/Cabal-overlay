// ─────────────────────────────────────────────────────────────
// css.h — the app's stylesheet
//
// Extracted from main.cpp: the entire CSS payload lives here, and
// the composition root only says "apply it".
// ─────────────────────────────────────────────────────────────
#pragma once

namespace css {

// Registers the app's stylesheet on the default display
// (GTK_STYLE_PROVIDER_PRIORITY_APPLICATION).
void apply();

} // namespace css

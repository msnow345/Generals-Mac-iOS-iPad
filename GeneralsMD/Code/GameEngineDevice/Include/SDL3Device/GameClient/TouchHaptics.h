/*
** GeneralsX @feature Claude 31/08/2026 Touch haptic feedback (iOS).
**
** Touch gestures that change mode have no physical affordance -- nothing depresses, and a
** timed hold in particular gives the player no way to tell a deliberate mode change from a
** misfire. A haptic tap supplies that.
**
** Safe to call on any device: UIFeedbackGenerator is simply inert on hardware with no
** Taptic Engine, which includes every iPad. No device check is needed or wanted -- this is
** iPhone-only in effect, not in code.
*/

#pragma once

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE

enum GXHapticKind
{
	GX_HAPTIC_LIGHT = 0,  ///< a mode armed or engaged
	GX_HAPTIC_MEDIUM,     ///< an action committed
	GX_HAPTIC_HEAVY       ///< an action cancelled or refused
};

#ifdef __cplusplus
extern "C" {
#endif

/// Fire a haptic tap. Must be called on the main thread; a no-op anywhere else.
void GX_TouchHaptic(int kind);

#ifdef __cplusplus
}
#endif

#else

// Non-iOS builds compile the call sites away entirely.
#define GX_TouchHaptic(kind) ((void)0)

#endif

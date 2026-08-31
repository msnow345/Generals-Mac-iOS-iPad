/*
** GeneralsX @feature Claude 31/08/2026 Touch haptic feedback (iOS).
** See TouchHaptics.h for why this needs no device capability check.
*/

#include "SDL3Device/GameClient/TouchHaptics.h"

#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE

#import <UIKit/UIKit.h>

extern "C" void GX_TouchHaptic(int kind)
{
	// UIFeedbackGenerator is main-thread only. The game loop is the main thread, but bail
	// rather than risk it if this is ever called from elsewhere.
	if (![NSThread isMainThread]) {
		return;
	}

	if (@available(iOS 10.0, *)) {
		// Generators are kept alive between taps: constructing one per tap loses the
		// latency benefit of prepare(), which is the difference between a tap that lands
		// with the gesture and one that trails it.
		static UIImpactFeedbackGenerator *lightGen = nil;
		static UIImpactFeedbackGenerator *mediumGen = nil;
		static UIImpactFeedbackGenerator *heavyGen = nil;
		static dispatch_once_t onceToken;
		dispatch_once(&onceToken, ^{
			lightGen  = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
			mediumGen = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleMedium];
			heavyGen  = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleHeavy];
		});

		UIImpactFeedbackGenerator *gen = nil;
		switch (kind) {
			case GX_HAPTIC_LIGHT:  gen = lightGen;  break;
			case GX_HAPTIC_MEDIUM: gen = mediumGen; break;
			case GX_HAPTIC_HEAVY:  gen = heavyGen;  break;
			default: return;
		}

		[gen impactOccurred];
		// Re-arm for the next tap; the engine powers down between uses otherwise.
		[gen prepare];
	}
}

#endif // TARGET_OS_IPHONE

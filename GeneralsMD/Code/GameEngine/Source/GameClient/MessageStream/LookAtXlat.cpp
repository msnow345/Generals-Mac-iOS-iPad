/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// LookAtXlat.cpp
// Translate raw input events into camera movement commands
// Author: Michael S. Booth, April 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/FramePacer.h"
#include "Common/GameType.h"
#include "Common/GameEngine.h"
#include "Common/MessageStream.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Recorder.h"
#include "Common/StatsCollector.h"
#include "Common/OptionPreferences.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameClient/Display.h"
#include "GameClient/GameText.h"
#include "GameClient/Mouse.h"
#include "GameClient/Shell.h"
#include "GameClient/GameClient.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/View.h"
#include "GameClient/Drawable.h"
#include "GameClient/LookAtXlat.h"
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#include <chrono>
#endif
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/GameLogic.h"

#include "Common/GlobalData.h"			// for camera pitch angle only

LookAtTranslator *TheLookAtTranslator = nullptr;

enum
{
	DIR_UP = 0,
	DIR_DOWN,
	DIR_LEFT,
	DIR_RIGHT
};

static Bool scrollDir[4] = { false, false, false, false };

// TheSuperHackers @tweak Introduces the SCROLL_MULTIPLIER for all scrolling to
//
//  1. bring the RMB scroll speed back to how it was at 30 FPS in the retail game version
//  2. increase the upper limit of the Scroll Factor when set from the Options Menu (0.20 to 2.90 instead of 0.10 to 1.45)
//  3. increase the scroll speed for Edge/Key scrolling to better fit the high speeds of RMB scrolling
//
// The multiplier of 2 was logically chosen because originally the Scroll Factor did practically not affect the RMB scroll speed
// and because the default Scroll Factor is/was 0.5, it needs to be doubled to get to a neutral 1x multiplier.

constexpr const Real SCROLL_MULTIPLIER = 2.0f;
constexpr const Real SCROLL_AMT = 100.0f * SCROLL_MULTIPLIER;

static const Int edgeScrollSize = 3;

// GeneralsX @tweak Claude 31/08/2026 A 3px band is a fine mouse target but is physically
// unreachable with a fingertip (~1mm on this display), and it overlaps the iOS system
// edge-swipe gestures. Give touch a band it can actually hit, scaled to the display.
static Int getEdgeScrollSize()
{
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
	const Int height = (TheDisplay != NULL) ? (Int)TheDisplay->getHeight() : 0;
	const Int touchBand = height / 20;
	return (touchBand > edgeScrollSize) ? touchBand : edgeScrollSize;
#else
	return edgeScrollSize;
#endif
}

static Mouse::MouseCursor prevCursor = Mouse::ARROW;

#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
// GeneralsX @tweak Claude 31/08/2026 Touch pan momentum.
// Velocity is world units per MILLISECOND and the decay is applied per millisecond, so the
// glide is identical whatever the frame rate -- a per-frame decay makes the same flick travel
// further on a faster machine. The rate matches UIScrollView's normal deceleration (0.998/ms,
// roughly a 1.5s coast), which is what makes it read as native rather than merely damped.
static Coord2D s_panVelocity = { 0.0f, 0.0f };
static Bool s_panGliding = false;
static double s_panLastMsec = 0.0;

static const Real PAN_DECAY_PER_MS   = 0.998f;
// GeneralsX @tweak Claude 31/08/2026 If the camera was moving, it coasts. No cliff.
//
// These two express one rule: arm a glide whenever the camera was actually moving at release,
// and end it when it is no longer moving perceptibly. They are therefore set as low as is
// useful and only just apart -- FLICK marginally above GLIDE, so arming always buys at least
// one real step.
//
// They were 0.15 and 0.02, which made the first a gate rather than a floor: releases below
// 0.15 got nothing at all, and releases just above it decayed to 0.02 within ~280ms having
// travelled about 7 world units, which is invisible. That left the entire gentle-release band
// feeling like a dead stop. With these values, coast distance is (v0 - GLIDE) / decay rate --
// linear in release speed all the way down, so a slow drag ends with a small drift and a hard
// flick still runs the length of the map.
static const Real PAN_MIN_FLICK      = 0.006f; // units/ms; a release slower than this was a stop
static const Real PAN_MIN_GLIDE      = 0.005f; // units/ms at which the coast is spent
static const double PAN_MAX_DT      = 50.0;   // ms clamp, so a frame hitch cannot teleport the map

// GeneralsX @tweak Claude 31/08/2026 Compounding flicks.
// UIScrollView adds a new flick's velocity to whatever is left of the previous one, which is
// why repeatedly swiping a long list accelerates instead of restarting at the same speed each
// time. Catching the content still cancels the motion -- the residual is only carried if the
// next flick follows soon after and pushes the same way, so a deliberate stop stays a stop.
// Velocity is measured over a real time window from position samples, the way
// UIPanGestureRecognizer does it, rather than smoothed per frame. A fixed-alpha filter has a
// frame-rate dependent time constant and lags -- and since people ease off slightly as they
// lift, that lag systematically under-reports the flick, which is exactly the speed that
// matters. Sampling the last ~100ms of travel measures what the hand actually did.
// GeneralsX @bugfix Claude 31/08/2026 Sub-millisecond clock for touch motion.
// timeGetTime() returns whole milliseconds. At 120Hz a frame is 8.33ms, so dt comes back as
// 8,8,9,8,9... -- a persistent 6% jitter in every glide step, which reads as stutter however
// even the real frame pacing is. Accurate dt is exactly what makes uneven frames look smooth;
// quantised dt makes even frames look uneven, so the smoother the display the worse it got.
static double gxNowMs()
{
	return (double)std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count() / 1000.0;
}

struct PanSample { double msec; Real x; Real y; };
static const Int PAN_SAMPLE_COUNT = 16;
static PanSample s_panSamples[PAN_SAMPLE_COUNT];
static Int s_panSampleNext = 0;
static Int s_panSamplesHeld = 0;
static Coord2D s_panTravel = { 0.0f, 0.0f };
// GeneralsX @tweak Claude 31/08/2026 Measured over the tail of the gesture, not all of it.
// 100ms is UIKit's rough figure, but at 120fps that is a dozen frames -- and people ease off
// as they lift, so a flat average over that much of the tail lets the deceleration outweigh
// the throw and a real flick reads as a stop. A shorter window tracks the speed the hand
// actually left at, while still averaging enough frames to reject noise.
static const UnsignedInt PAN_VELOCITY_WINDOW_MS = 60;

static void gxPanSamplesReset()
{
	s_panSampleNext = 0;
	s_panSamplesHeld = 0;
	s_panTravel.x = s_panTravel.y = 0.0f;
}

static void gxPanSampleAdd(const Coord2D &worldDelta, double nowMsec)
{
	s_panTravel.x += worldDelta.x;
	s_panTravel.y += worldDelta.y;
	s_panSamples[s_panSampleNext].msec = nowMsec;
	s_panSamples[s_panSampleNext].x = s_panTravel.x;
	s_panSamples[s_panSampleNext].y = s_panTravel.y;
	s_panSampleNext = (s_panSampleNext + 1) % PAN_SAMPLE_COUNT;
	if (s_panSamplesHeld < PAN_SAMPLE_COUNT)
		++s_panSamplesHeld;
}

// Average velocity over the newest samples spanning up to the window, in units/ms.
static Coord2D gxPanSampledVelocity(double nowMsec)
{
	Coord2D v = { 0.0f, 0.0f };
	if (s_panSamplesHeld < 2)
		return v;

	const Int newest = (s_panSampleNext - 1 + PAN_SAMPLE_COUNT) % PAN_SAMPLE_COUNT;
	Int oldest = newest;
	for (Int i = 1; i < s_panSamplesHeld; ++i)
	{
		const Int idx = (s_panSampleNext - 1 - i + 2 * PAN_SAMPLE_COUNT) % PAN_SAMPLE_COUNT;
		if (nowMsec - s_panSamples[idx].msec > (double)PAN_VELOCITY_WINDOW_MS)
			break;
		oldest = idx;
	}

	const double span = s_panSamples[newest].msec - s_panSamples[oldest].msec;
	if (span <= 0.0)
		return v;

	v.x = (Real)((s_panSamples[newest].x - s_panSamples[oldest].x) / span);
	v.y = (Real)((s_panSamples[newest].y - s_panSamples[oldest].y) / span);
	return v;
}

static Coord2D s_panCarryVelocity = { 0.0f, 0.0f };
static double s_panCarryMsec = 0.0;
static const UnsignedInt PAN_CARRY_WINDOW_MS = 300;
static const Real PAN_CARRY_MAX_FACTOR = 3.0f;   // ceiling, so compounding cannot run away

// True while a flick is still worth building on. Used to skip gesture re-recognition, so a
// follow-up flick does not lose its opening travel to the dead zone -- UIScrollView captures
// immediately when you touch during deceleration, with no slop to re-cross.
Bool GX_PanFlickIsRecent()
{
	return (s_panCarryMsec != 0.0) && ((gxNowMs() - s_panCarryMsec) < (double)PAN_CARRY_WINDOW_MS);
}

void GX_StopPanGlide()
{
	// Remember what the coast still had in it, so a follow-up flick can build on it.
	if (s_panGliding)
	{
		s_panCarryVelocity = s_panVelocity;
		s_panCarryMsec = gxNowMs();
	}
	s_panGliding = false;
	s_panVelocity.x = s_panVelocity.y = 0.0f;
}

static Bool s_touchActive = false;

// GeneralsX @tweak Claude 31/08/2026 Continuous pinch zoom.
// The stock wheel path ratchets the camera in fixed steps about the screen centre. Native
// pinch does neither: it tracks the fingers exactly and keeps the point between them
// pinned. Zoom is therefore accumulated as a ratio here and applied on the frame tick.
//
// The anchor correction is computed analytically, in the same frame as the zoom.
//
// The obvious approach -- re-project the pinch point after zooming and undo the difference --
// cannot work here: screenToTerrain goes through m_3DCamera, which is only rebuilt during
// the view's own update, so an immediate re-projection still sees the pre-zoom camera. Doing
// it a frame later does correct the position, but then every frame displays the uncorrected
// zoom before being yanked back on the next one, which reads as wobble.
//
// Instead, note that changing only the camera height scales the ground footprint about the
// look-at point, which is what the screen centre projects to. So if the pinch point sits at
// world offset D from the centre and the height scales by 1/r, that ground point moves to
// D/r, and holding it under the fingers means moving the camera by D * (1 - 1/r). Both
// projections are taken before the zoom, with the camera that is actually current.
static Real s_pendingZoomRatio = 1.0f;

// GeneralsX @tweak Claude 31/08/2026 Zoom response damping.
// Scaling camera height by 1/ratio is 1:1 only if the visible ground span is proportional to
// that height. Under this game's tilted camera it is not quite -- the pitch means a given
// height change covers more ground than a flat overhead view would -- so an undamped pinch
// zooms further than the fingers travelled. Applied as an exponent rather than a multiplier
// so the response stays multiplicative: pinching in and back out returns to the same zoom
// instead of drifting. Lower is less sensitive; 1.0 restores the raw geometric mapping.
static const Real TOUCH_ZOOM_GAIN = 0.65f;

// GeneralsX @bugfix Claude 31/08/2026 Smooth the zoom anchor offset.
// screenToTerrain raycasts the terrain MESH, so the world offset between the pinch point and
// the screen centre depends on the elevation under each -- and lurches whenever a ray crosses
// a slope, a cliff edge or a building. Measured on device, consecutive frames of a smooth
// pinch produced offsets of (44,78), (-31,104), (107,216): multiplying that by the height
// change turns terrain relief directly into visible jitter.
//
// The true offset changes slowly, so filtering it costs nothing perceptible while rejecting
// both the noise and the occasional badly-missed raycast. Reset when the fingers lift so a
// new gesture starts from its own geometry rather than the last one's.
static Coord2D s_zoomAnchorOffset = { 0.0f, 0.0f };
static Bool s_zoomAnchorOffsetValid = false;
static const Real ZOOM_OFFSET_SMOOTHING = 0.35f;

static Bool gxTerrainHit(const Coord3D &c)
{
	// screenToTerrain leaves its output all-zero when the pick ray misses the terrain.
	return !(c.x == 0.0f && c.y == 0.0f && c.z == 0.0f);
}

void GX_AccumulateTouchZoom(Real ratio)
{
	if (ratio > 0.0f)
	{
		s_pendingZoomRatio *= ratio;
	}
}

void GX_SetTouchActive(Bool active)
{
	s_touchActive = active;
	if (!active)
	{
		s_zoomAnchorOffsetValid = false;
	}
}
#endif

//-----------------------------------------------------------------------------
void LookAtTranslator::setScrolling(ScrollType scrollType)
{
	if (!TheInGameUI->getInputEnabled())
		return;

#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
	gxPanSamplesReset();

	// A new scroll supersedes any coast in progress.
	// Deliberately NOT keyed off mouse motion: ending a pan emits a position message of its
	// own at the release point, so a motion-based rule cancelled the flick it had just
	// armed, every time, exactly one frame in.
	GX_StopPanGlide();
#endif

	prevCursor = TheMouse->getMouseCursor();
	m_isScrolling = true;
	TheInGameUI->setScrolling( TRUE );
	TheTacticalView->setMouseLock( TRUE );
	m_scrollType = scrollType;
	if(TheStatsCollector)
		TheStatsCollector->startScrollTime();
}

//-----------------------------------------------------------------------------
void LookAtTranslator::stopScrolling()
{
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
	// GeneralsX @tweak Claude 31/08/2026 Let a flick coast. Below the threshold the finger
	// was effectively parked, and coasting from that reads as drift rather than momentum.
	s_panVelocity = gxPanSampledVelocity(gxNowMs());
	Real speed = (Real)sqrt(s_panVelocity.x * s_panVelocity.x + s_panVelocity.y * s_panVelocity.y);

	// GeneralsX @tweak Claude 31/08/2026 Flick velocities deliberately do NOT compound.
	// UIScrollView adds them, but it renders at display rate; in-game we are GPU-bound at
	// ~30fps, so a faster glide simply means larger jumps between frames. Compounding made
	// repeated flicks measurably quicker and visibly choppier, which is the wrong trade.
	// The carry timestamp is still kept -- it is what lets a follow-up flick skip gesture
	// re-recognition, which is the part that genuinely helped.

	s_panGliding = (m_scrollType == SCROLL_RMB) && (speed > PAN_MIN_FLICK);
	if (!s_panGliding) {
		s_panVelocity.x = s_panVelocity.y = 0.0f;
	} else {
		s_panLastMsec = gxNowMs();
	}
#endif
	m_isScrolling = false;
	TheInGameUI->setScrolling( FALSE );
	TheTacticalView->setMouseLock( FALSE );
	TheMouse->setCursor(prevCursor);
	m_scrollType = SCROLL_NONE;

	// increment the stats if we have a stats collector
	if(TheStatsCollector)
		TheStatsCollector->endScrollTime();

}

//-----------------------------------------------------------------------------
Bool LookAtTranslator::canScrollAtScreenEdge() const
{
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
	// GeneralsX @tweak Claude 31/08/2026 There is no cursor capture on touch, so the check
	// below would disable edge scrolling outright. Enable it only while a structure is
	// being placed: that is the one case with no alternative, because the finger carrying
	// the ghost cannot also two-finger pan. During normal play the two-finger pan covers
	// map movement, and an always-on edge band would scroll on any tap near the border.
	// Also requires a finger down: the cursor is frozen once the last one lifts, so an edge
	// scroll started by a gesture that has since ended would never see the cursor leave the
	// band and would scroll the map away indefinitely.
	return s_touchActive && TheInGameUI != NULL && TheInGameUI->getPendingPlaceType() != NULL;
#else
	if (!TheMouse->isCursorCaptured())
		return false;

	if (TheDisplay->getWindowed())
	{
		if ((m_screenEdgeScrollMode & ScreenEdgeScrollMode_EnabledInWindowedApp) == 0)
			return false;
	}
	else
	{
		if ((m_screenEdgeScrollMode & ScreenEdgeScrollMode_EnabledInFullscreenApp) == 0)
			return false;
	}

	return true;
#endif
}

//-----------------------------------------------------------------------------
LookAtTranslator::LookAtTranslator() :
	m_isScrolling(false),
	m_isRotating(false),
	m_isPitching(false),
	m_isPitchingToDefault(false),
	m_isChangingFOV(false),
	m_middleButtonDownTimeMsec(0),
	m_lastPlaneID(INVALID_DRAWABLE_ID),
	m_lastMouseMoveTimeMsec(0),
	m_scrollType(SCROLL_NONE)
{
	m_anchor.x = m_anchor.y = 0;
	m_currentPos.x = m_currentPos.y = 0;
	m_lastScrollPos.x = m_lastScrollPos.y = 0;

	m_originalAnchor.x = m_originalAnchor.y = 0;

	OptionPreferences prefs;
	m_screenEdgeScrollMode = prefs.getScreenEdgeScrollMode();

	DEBUG_ASSERTCRASH(!TheLookAtTranslator, ("Already have a LookAtTranslator - why do you need two?"));
	TheLookAtTranslator = this;
}

//-----------------------------------------------------------------------------
LookAtTranslator::~LookAtTranslator()
{
	if (TheLookAtTranslator == this)
		TheLookAtTranslator = nullptr;
}

const ICoord2D* LookAtTranslator::getRMBScrollAnchor()
{
	if (m_isScrolling && m_scrollType == SCROLL_RMB)
	{
		return &m_anchor;
	}
	return nullptr;
}

Bool LookAtTranslator::hasMouseMovedRecently()
{
	const UnsignedInt now = timeGetTime();
	const UnsignedInt lastMove = m_lastMouseMoveTimeMsec;

	const UnsignedInt elapsedMsec = now - lastMove;

	return elapsedMsec <= MSEC_PER_SECOND;
}

void LookAtTranslator::setCurrentPos( const ICoord2D& pos )
{
	m_currentPos = pos;
}

void LookAtTranslator::setScreenEdgeScrollMode(ScreenEdgeScrollMode mode)
{
	m_screenEdgeScrollMode = mode;
}

//-----------------------------------------------------------------------------
/**
 * The LookAt Translator is responsible for camera movements. It is directly responsible for
 * right mouse button scrolling, and CTRL-<F key> bookmarking. It also responds to certain
 * LOOKAT message on the message stream.
 */
GameMessageDisposition LookAtTranslator::translateGameMessage(const GameMessage *msg)
{
	GameMessageDisposition disp = KEEP_MESSAGE;

	GameMessage::Type t = msg->getType();
	switch (t)
	{
		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_KEY_DOWN:
		case GameMessage::MSG_RAW_KEY_UP:
		{
			// get key and state from args
			UnsignedByte key		= msg->getArgument( 0 )->integer;
			UnsignedByte state	= msg->getArgument( 1 )->integer;
			Bool isPressed = !(BitIsSet( state, KEY_STATE_UP ));

			if (TheShell && TheShell->isShellActive())
				break;

			switch (key)
			{
			case KEY_UP:
				scrollDir[DIR_UP] = isPressed;
				break;
			case KEY_DOWN:
				scrollDir[DIR_DOWN] = isPressed;
				break;
			case KEY_LEFT:
				scrollDir[DIR_LEFT] = isPressed;
				break;
			case KEY_RIGHT:
				scrollDir[DIR_RIGHT] = isPressed;
				break;
			}

			if (TheInGameUI->isSelecting() || (m_isScrolling && m_scrollType != SCROLL_KEY))
				break;

			// see if we need to start/stop scrolling
			Int numDirs = 0;
			for (Int i=0; i<4; ++i)
			{
				if (scrollDir[i])
					numDirs++;
			}

			if (numDirs && !m_isScrolling)
			{
				setScrolling( SCROLL_KEY );
			}
			else if (!numDirs && m_isScrolling)
			{
				stopScrolling();
			}
			break;
		}

		//-----------------------------------------------------------------------------
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
		//-----------------------------------------------------------------------------
		// GeneralsX @tweak Claude 31/08/2026 A tap catches a coasting map. Handled here
		// purely to stop the glide; the message is kept for the translators below.
		case GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN:
		{
			GX_StopPanGlide();
			break;
		}
#endif

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN:
		{
			m_lastMouseMoveTimeMsec = timeGetTime();

			m_anchor = msg->getArgument( 0 )->pixel;
			m_currentPos = msg->getArgument( 0 )->pixel;
			// GeneralsX @tweak Claude 31/08/2026 Seed the 1:1 touch drag so the first
			// scroll tick measures travel from the press, not from a stale position.
			m_lastScrollPos = msg->getArgument( 0 )->pixel;

			if (!TheInGameUI->isSelecting() && !m_isScrolling)
			{
				setScrolling(SCROLL_RMB);
			}
			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP:
		{
			m_lastMouseMoveTimeMsec = timeGetTime();

			if (m_scrollType == SCROLL_RMB)
			{
				stopScrolling();
			}
			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_MIDDLE_BUTTON_DOWN:
		{
			const UnsignedInt now = timeGetTime();
			m_lastMouseMoveTimeMsec = now;
			m_middleButtonDownTimeMsec = now;

			m_isRotating = true;
			m_anchor = msg->getArgument( 0 )->pixel;
			m_anchorAngle = TheTacticalView->getAngle();
			m_originalAnchor = msg->getArgument( 0 )->pixel;
			m_currentPos = msg->getArgument( 0 )->pixel;
			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_MIDDLE_BUTTON_UP:
		{
			const UnsignedInt now = timeGetTime();
			m_lastMouseMoveTimeMsec = now;

			const UnsignedInt CLICK_DURATION_MSEC = 167;
			const UnsignedInt PIXEL_OFFSET = 5;

			m_isRotating = false;
			Int dx = m_currentPos.x-m_originalAnchor.x;
			if (dx<0) dx = -dx;
			Int dy = m_currentPos.y-m_originalAnchor.y;
			Bool didMove = dx>PIXEL_OFFSET || dy>PIXEL_OFFSET;

			const UnsignedInt elapsedMsec = now - m_middleButtonDownTimeMsec;

			// if middle button is "clicked", reset to "home" orientation
			if (!didMove && elapsedMsec < CLICK_DURATION_MSEC)
			{
				TheTacticalView->userResetPivotToGround();
				TheTacticalView->userSetAngleToDefault();
				TheTacticalView->userSetPitchToDefault();
				TheTacticalView->userSetZoomToDefault();
			}

			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_POSITION:
		{
			if (m_currentPos.x != msg->getArgument( 0 )->pixel.x || m_currentPos.y != msg->getArgument( 0 )->pixel.y)
				m_lastMouseMoveTimeMsec = timeGetTime();

			m_currentPos = msg->getArgument( 0 )->pixel;

			UnsignedInt height = TheDisplay->getHeight();
			UnsignedInt width  = TheDisplay->getWidth();

			if (TheInGameUI->getInputEnabled() == FALSE) {
				// We don't care how we're scrolling, just stop.
				if (m_isScrolling)
					stopScrolling();
				break;
			}

			if (canScrollAtScreenEdge())
			{
				if (m_isScrolling)
				{
					if ( m_scrollType == SCROLL_SCREENEDGE && (m_currentPos.x >= getEdgeScrollSize() && m_currentPos.y >= getEdgeScrollSize() && m_currentPos.y < height-getEdgeScrollSize() && m_currentPos.x < width-getEdgeScrollSize()) )
					{
						stopScrolling();
					}
				}
				else
				{
					if ( m_currentPos.x < getEdgeScrollSize() || m_currentPos.y < getEdgeScrollSize() || m_currentPos.y >= height-getEdgeScrollSize() || m_currentPos.x >= width-getEdgeScrollSize() )
					{
						setScrolling(SCROLL_SCREENEDGE);
					}
				}
			}

			// rotate the view
			if (m_isRotating)
			{
				const Real FACTOR = 0.01f;
				const Real angle = FACTOR * (m_currentPos.x - m_originalAnchor.x);
				Real targetAngle = m_anchorAngle + angle;

				// TheSuperHackers @tweak Stubbjax 13/11/2025 Snap angle to nearest 45 degrees
				// while using force attack mode for convenience.
				if (TheInGameUI->isInForceAttackMode())
				{
					const Real snapRadians = DEG_TO_RADF(45);
					targetAngle = WWMath::Round(targetAngle / snapRadians) * snapRadians;
				}

				TheTacticalView->userSetAngle(targetAngle);
				m_anchor = msg->getArgument( 0 )->pixel;
			}

			// rotate the view up/down
			if (m_isPitching)
			{
				constexpr const Real Scale = 0.01f;
				const Real angle = Scale * (m_currentPos.y - m_anchor.y);
				TheTacticalView->userSetPitch( TheTacticalView->getPitch() - angle );
				m_anchor = msg->getArgument( 0 )->pixel;
			}

#if defined(RTS_DEBUG)
			if (m_isPitchingToDefault)
			{
				constexpr const Real Scale = 0.01f;
				const Real angle = Scale * (m_currentPos.y - m_anchor.y);
				TheTacticalView->userSetDefaultPitch( TheTacticalView->getDefaultPitch() - angle );
				TheTacticalView->userSetPitchToDefault();
				m_anchor = msg->getArgument( 0 )->pixel;
			}

			// adjust the field of view
			if (m_isChangingFOV)
			{
				constexpr const Real Scale = 0.01f;
				const Real angle = Scale * (m_currentPos.y - m_anchor.y);
				TheTacticalView->userSetFieldOfView( TheTacticalView->getFieldOfView() + angle );
				m_anchor = msg->getArgument( 0 )->pixel;
			}
#endif
			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_WHEEL:
		{
			m_lastMouseMoveTimeMsec = timeGetTime();

			const Real spin = msg->getArgument( 1 )->real;
			const Real zoom = -spin * View::ZoomHeightPerSecond;
			TheTacticalView->userZoom(zoom);

			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_META_OPTIONS:
		{
			// stop the scrolling
			stopScrolling();
			// let the message drop through, cause we need to process this message for
			// selection as well.
			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_FRAME_TICK:
		{
			Coord2D offset = {0, 0};

#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
			// GeneralsX @tweak Claude 31/08/2026 Settle the previous pinch step, then apply the
			// next one. Order matters: the correction has to happen while it still refers to
			// the camera the anchor was captured against.
			if (s_pendingZoomRatio != 1.0f)
			{
				const Real ratio = s_pendingZoomRatio;
				s_pendingZoomRatio = 1.0f;

				// Both projections use the current camera, before anything moves.
				ICoord2D centreScreen;
				centreScreen.x = (Int)(TheDisplay->getWidth() / 2);
				centreScreen.y = (Int)(TheDisplay->getHeight() / 2);

				Coord3D anchorWorld, centreWorld;
				anchorWorld.zero();
				centreWorld.zero();
				TheTacticalView->screenToTerrain(&m_currentPos, &anchorWorld);
				TheTacticalView->screenToTerrain(&centreScreen, &centreWorld);
				const Bool canAnchor = gxTerrainHit(anchorWorld) && gxTerrainHit(centreWorld);

				// Fingers apart (ratio > 1) means zoom in, i.e. a lower camera. View::zoom
				// takes a height delta, so scaling height by 1/ratio keeps the gesture
				// proportional at every zoom level instead of stepping.
				const Real heightBefore = TheTacticalView->getHeightAboveGround();
				const Real scale = (Real)pow((double)(1.0f / ratio), (double)TOUCH_ZOOM_GAIN);
				TheTacticalView->userZoom(heightBefore * (scale - 1.0f));
				const Real heightAfter = TheTacticalView->getHeightAboveGround();

				// Correct by the height change actually achieved, not the one requested: the
				// engine clamps height at its zoom limits, and correcting for a zoom that did
				// not happen would drag the map sideways at the ends of the range.
				//
				// Expressed as (before - after) / before rather than 1 - 1/(before/after).
				// They are algebraically identical, but the latter divides two nearly equal
				// numbers and then subtracts from one, which loses most of its significant
				// digits per frame -- and that error is then multiplied by the distance from
				// the screen centre, so it shows up as jitter that grows the further the
				// pinch is from the middle.
				const Real heightDelta = heightBefore - heightAfter;
				if (canAnchor && heightBefore > 0.0f && WWMath::Fabs(heightDelta) > 0.001f)
				{
					Coord2D rawOffset;
					rawOffset.x = anchorWorld.x - centreWorld.x;
					rawOffset.y = anchorWorld.y - centreWorld.y;

					if (!s_zoomAnchorOffsetValid)
					{
						s_zoomAnchorOffset = rawOffset;
						s_zoomAnchorOffsetValid = true;
					}
					else
					{
						s_zoomAnchorOffset.x += (rawOffset.x - s_zoomAnchorOffset.x) * ZOOM_OFFSET_SMOOTHING;
						s_zoomAnchorOffset.y += (rawOffset.y - s_zoomAnchorOffset.y) * ZOOM_OFFSET_SMOOTHING;
					}

					const Real k = heightDelta / heightBefore;
					Coord2D shift;
					shift.x = s_zoomAnchorOffset.x * k;
					shift.y = s_zoomAnchorOffset.y * k;
					TheTacticalView->userScrollByWorld(&shift);
				}
			}

			// GeneralsX @bugfix Claude 31/08/2026 Stop a scroll that can no longer be valid.
			//
			// Every scroll on touch is driven by fingers, so none of them can legitimately
			// outlive the last one lifting. Both stop paths are unreliable here:
			//
			//  - Edge scrolling starts and stops under MSG_RAW_MOUSE_POSITION, nested inside
			//    canScrollAtScreenEdge(). The cursor stops moving the moment the last finger
			//    lifts, and that condition goes false when the build is placed or cancelled,
			//    so the stop branch simply becomes unreachable.
			//  - RMB scrolling ends on MSG_RAW_MOUSE_RIGHT_BUTTON_UP, but SelectionXlat runs
			//    first (50 vs 60) and destroys right-button messages while a build is
			//    pending. Select something to build just before a two-finger pan releases and
			//    the up never arrives, leaving the camera panning 1:1 with the cursor forever
			//    -- which reads as the map being glued to your finger.
			//
			// Enforcing the invariant here is far more robust than trying to predict which
			// translator consumes which message. A frame tick still arrives when nothing else
			// does, which is exactly the case that strands these scrolls.
			if (m_isScrolling &&
			    (!s_touchActive ||
			     (m_scrollType == SCROLL_SCREENEDGE && !canScrollAtScreenEdge())))
			{
				stopScrolling();
			}
#endif

			if (m_isScrolling && !TheInGameUI->isScrolling())
			{
				// If we've been forced to stop scrolling (script action?)
				TheInGameUI->setScrollAmount(offset);
				TheTacticalView->scrollBy(&offset);
				stopScrolling();
			}
			else if (m_isScrolling)
			{
				// Scroll the view
				// TheSuperHackers @bugfix Mauller 07/06/2025 The camera scrolling is now decoupled from the render update.
				const Real fpsRatio = TheFramePacer->getBaseOverUpdateFpsRatio();

				switch (m_scrollType)
				{
				case SCROLL_RMB:
					{
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
						// GeneralsX @tweak Claude 31/08/2026 1:1 drag panning for touch.
						// Every mouse event on iOS is synthesized from a two-finger gesture
						// (SDL3GameEngine.cpp), so RMB scrolling here IS the touch camera pan.
						// The joystick model below -- scroll every frame at a speed set by the
						// cursor's distance from its anchor -- reads as the camera accelerating
						// away from the fingers, because the map keeps moving while the fingers
						// are merely held still off-centre.
						//
						// Instead, project this frame's cursor travel onto the terrain and shift
						// the view by the world-space difference, which pins the ground point
						// under the fingers: the map tracks them exactly and stops dead when they
						// stop. Projecting both endpoints against the same camera also makes this
						// correct at any zoom and under the tilted camera, where a pixel of
						// vertical travel covers more ground than a horizontal one.
						Coord3D prevWorld, curWorld;
						prevWorld.zero();
						curWorld.zero();
						TheTacticalView->screenToTerrain(&m_lastScrollPos, &prevWorld);
						TheTacticalView->screenToTerrain(&m_currentPos, &curWorld);

						// screenToTerrain leaves the result all-zero when the pick ray misses the
						// terrain (a cursor above the horizon, say). Taking that as a real point
						// would fling the camera to the map origin, so skip the tick instead and
						// resync, losing one frame of pan rather than the whole gesture.
						const Bool prevHit = !(prevWorld.x == 0.0f && prevWorld.y == 0.0f && prevWorld.z == 0.0f);
						const Bool curHit  = !(curWorld.x == 0.0f && curWorld.y == 0.0f && curWorld.z == 0.0f);
						if (prevHit && curHit)
						{
							// Apply in world space. offset is deliberately left at {0,0}: the shared
							// userScrollBy() after this switch reads its delta as device space, so
							// handing it a world-space value there would apply the move a second time
							// through the wrong transform (inverted on Y, scaled by camera distance).
							Coord2D worldDelta;
							worldDelta.x = prevWorld.x - curWorld.x;
							worldDelta.y = prevWorld.y - curWorld.y;
							TheTacticalView->userScrollByWorld(&worldDelta);
							// Record travel against real time; the release reads velocity back
							// over a fixed window rather than trusting any single frame.
							gxPanSampleAdd(worldDelta, gxNowMs());
						}
						m_lastScrollPos = m_currentPos;
#else
						if (TheInGameUI->shouldMoveRMBScrollAnchor())
						{
							Int maxX = TheDisplay->getWidth()/2;
							Int maxY = TheDisplay->getHeight()/2;

							if (m_currentPos.x + maxX < m_anchor.x)
								m_anchor.x = m_currentPos.x + maxX;
							else if (m_currentPos.x - maxX > m_anchor.x)
								m_anchor.x = m_currentPos.x - maxX;

							if (m_currentPos.y + maxY < m_anchor.y)
								m_anchor.y = m_currentPos.y + maxY;
							else if (m_currentPos.y - maxY > m_anchor.y)
								m_anchor.y = m_currentPos.y - maxY;
						}

						// TheSuperHackers @fix Mauller 16/06/2025 fix RMB scrolling to allow it to scale with the user adjusted scroll factor
						Coord2D vec;
						vec.x = (m_currentPos.x - m_anchor.x);
						vec.y = (m_currentPos.y - m_anchor.y);
						// TheSuperHackers @info calculate the length of the vector to obtain the movement speed before the vector is normalized
						float vecLength = vec.length();
						vec.normalize();
						offset.x = TheGlobalData->m_horizontalScrollSpeedFactor * fpsRatio * vecLength * vec.x * SCROLL_MULTIPLIER * TheGlobalData->m_keyboardScrollFactor;
						offset.y = TheGlobalData->m_verticalScrollSpeedFactor * fpsRatio * vecLength * vec.y * SCROLL_MULTIPLIER * TheGlobalData->m_keyboardScrollFactor;
#endif
					}
					break;
				case SCROLL_KEY:
					{
						if (scrollDir[DIR_UP])
						{
							offset.y -= TheGlobalData->m_verticalScrollSpeedFactor * fpsRatio * SCROLL_AMT * TheGlobalData->m_keyboardScrollFactor;
						}
						if (scrollDir[DIR_DOWN])
						{
							offset.y += TheGlobalData->m_verticalScrollSpeedFactor * fpsRatio * SCROLL_AMT * TheGlobalData->m_keyboardScrollFactor;
						}
						if (scrollDir[DIR_LEFT])
						{
							offset.x -= TheGlobalData->m_horizontalScrollSpeedFactor * fpsRatio * SCROLL_AMT * TheGlobalData->m_keyboardScrollFactor;
						}
						if (scrollDir[DIR_RIGHT])
						{
							offset.x += TheGlobalData->m_horizontalScrollSpeedFactor * fpsRatio * SCROLL_AMT * TheGlobalData->m_keyboardScrollFactor;
						}
					}
					break;
				case SCROLL_SCREENEDGE:
					{
						UnsignedInt height = TheDisplay->getHeight();
						UnsignedInt width  = TheDisplay->getWidth();
						if (m_currentPos.y < getEdgeScrollSize())
						{
							offset.y -= TheGlobalData->m_verticalScrollSpeedFactor * fpsRatio * SCROLL_AMT * TheGlobalData->m_keyboardScrollFactor;
						}
						if (m_currentPos.y >= height-getEdgeScrollSize())
						{
							offset.y += TheGlobalData->m_verticalScrollSpeedFactor * fpsRatio * SCROLL_AMT * TheGlobalData->m_keyboardScrollFactor;
						}
						if (m_currentPos.x < getEdgeScrollSize())
						{
							offset.x -= TheGlobalData->m_horizontalScrollSpeedFactor * fpsRatio * SCROLL_AMT * TheGlobalData->m_keyboardScrollFactor;
						}
						if (m_currentPos.x >= width-getEdgeScrollSize())
						{
							offset.x += TheGlobalData->m_horizontalScrollSpeedFactor * fpsRatio * SCROLL_AMT * TheGlobalData->m_keyboardScrollFactor;
						}
					}
					break;
				}

				TheInGameUI->setScrollAmount(offset);
				TheTacticalView->userScrollBy( &offset );
			}
			else
			{
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
				if (s_panGliding)
				{
					// Coast. Distance is velocity x elapsed time and the decay is applied per
					// millisecond, so the curve is identical at any frame rate. If the view
					// did not actually move, the camera is pinned against a map edge, so drop
					// the glide rather than grind on it for the rest of the decay.
					const double nowMsec = gxNowMs();
					double dt = nowMsec - s_panLastMsec;
					if (dt > PAN_MAX_DT) dt = PAN_MAX_DT;
					if (dt < 0.0) dt = 0.0;
					s_panLastMsec = nowMsec;

					if (dt > 0.0)
					{
						Coord2D step;
						step.x = s_panVelocity.x * (Real)dt;
						step.y = s_panVelocity.y * (Real)dt;

						const Coord3D before = TheTacticalView->getPosition();
						TheTacticalView->userScrollByWorld(&step);
						const Coord3D after = TheTacticalView->getPosition();
						const Real movedSq = (after.x - before.x) * (after.x - before.x) +
						                     (after.y - before.y) * (after.y - before.y);

						const Real decay = (Real)pow((double)PAN_DECAY_PER_MS, dt);
						s_panVelocity.x *= decay;
						s_panVelocity.y *= decay;
						const Real speed = (Real)sqrt(s_panVelocity.x * s_panVelocity.x +
						                              s_panVelocity.y * s_panVelocity.y);
						// GeneralsX @bugfix Claude 31/08/2026 Judge "pinned" relative to the step.
						//
						// This compared movement against a FIXED threshold, so a frame with
						// almost no elapsed time -- which asks the camera to move almost
						// nothing and therefore moves almost nothing -- looked identical to a
						// camera jammed against the map edge. The first glide frame lands in
						// the same frame the release armed it, so dt is ~0 and every single
						// flick was killed on frame one, before it had decayed at all.
						//
						// Comparing against the distance actually requested tells the two
						// apart: genuinely pinned means "asked to move a real distance and did
						// not", which a zero-length step can never satisfy.
						const Real asked = (Real)sqrt(step.x * step.x + step.y * step.y);
						const Bool pinned = (asked > 0.05f) && (sqrt(movedSq) < asked * 0.25f);
						if (speed < PAN_MIN_GLIDE || pinned)
						{
							s_panGliding = false;
							s_panVelocity.x = s_panVelocity.y = 0.0f;
						}
					}
				}
				else
#endif
				{
					//not scrolling so reset amount
					TheInGameUI->setScrollAmount(offset);
					TheTacticalView->scrollBy(&offset);
				}
			}

			//if (TheGlobalData->m_saveCameraInReplay /*&& TheRecorder->getMode() != RECORDERMODETYPE_PLAYBACK *//**/&& (TheGameLogic->isInSinglePlayerGame() || TheGameLogic->isInSkirmishGame())/**/)
			//if (TheGlobalData->m_saveCameraInReplay && (TheGameLogic->isInMultiplayerGame() || TheGameLogic->isInSinglePlayerGame() || TheGameLogic->isInSkirmishGame()))
			if (TheGlobalData->m_saveCameraInReplay && (TheGameLogic->isInSinglePlayerGame() || TheGameLogic->isInSkirmishGame()))
			{
				ViewLocation currentView;
				TheTacticalView->getLocation(&currentView);
				GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_SET_REPLAY_CAMERA );
				msg->appendLocationArgument( currentView.getPosition() );
				msg->appendRealArgument( currentView.getAngle() );
				msg->appendRealArgument( currentView.getPitch() );
				msg->appendRealArgument( currentView.getZoom() );
				msg->appendIntegerArgument( (Int)TheMouse->getMouseCursor() );
				msg->appendPixelArgument( m_currentPos );
				// TheSuperHackers @tweak Save 3D camera position and direction to recover optimal playback precision
				msg->appendLocationArgument( TheTacticalView->get3DCameraPosition() );
				msg->appendLocationArgument( TheTacticalView->get3DCameraDirection() );
			}
			break;
		}

		// ------------------------------------------------------------------------
#if defined(RTS_DEBUG)
		case GameMessage::MSG_META_DEMO_BEGIN_ADJUST_PITCH:
		{
			DEBUG_ASSERTCRASH(!m_isPitching, ("hmm, mismatched m_isPitching"));
			m_isPitching = true;
			m_anchor = m_currentPos;
			disp = DESTROY_MESSAGE;
			break;
		}
#endif // #if defined(RTS_DEBUG)

		// ------------------------------------------------------------------------
#if defined(RTS_DEBUG)
		case GameMessage::MSG_META_DEMO_END_ADJUST_PITCH:
		{
			DEBUG_ASSERTCRASH(m_isPitching, ("hmm, mismatched m_isPitching"));
			m_isPitching = false;
			disp = DESTROY_MESSAGE;
			break;
		}
#endif // #if defined(RTS_DEBUG)

		// ------------------------------------------------------------------------
#if defined(RTS_DEBUG)
		case GameMessage::MSG_META_DEMO_BEGIN_ADJUST_DEFAULTPITCH:
		{
			DEBUG_ASSERTCRASH(!m_isPitchingToDefault, ("hmm, mismatched m_isPitchingToDefault"));
			m_isPitchingToDefault = true;
			m_anchor = m_currentPos;
			disp = DESTROY_MESSAGE;
			break;
		}
#endif // #if defined(RTS_DEBUG)

		// ------------------------------------------------------------------------
#if defined(RTS_DEBUG)
		case GameMessage::MSG_META_DEMO_END_ADJUST_DEFAULTPITCH:
		{
			DEBUG_ASSERTCRASH(m_isPitchingToDefault, ("hmm, mismatched m_isPitchingToDefault"));
			m_isPitchingToDefault = false;
			disp = DESTROY_MESSAGE;
			break;
		}
#endif // #if defined(RTS_DEBUG)

		// ------------------------------------------------------------------------
#if defined(RTS_DEBUG)
		case GameMessage::MSG_META_DEMO_DESHROUD:
		{
			ThePartitionManager->revealMapForPlayerPermanently( ThePlayerList->getLocalPlayer()->getPlayerIndex() );
			break;
		}
#endif // #if defined(RTS_DEBUG)

		// ------------------------------------------------------------------------
#if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
		case GameMessage::MSG_CHEAT_DESHROUD:
		{
			if (!TheGameLogic->isInMultiplayerGame())
			{
				ThePartitionManager->revealMapForPlayerPermanently( ThePlayerList->getLocalPlayer()->getPlayerIndex() );
			}
			break;
		}
#endif // #if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)

		// ------------------------------------------------------------------------
#if defined(RTS_DEBUG)
		case GameMessage::MSG_META_DEMO_ENSHROUD:
		{
			// Need to first undo the permanent Look laid down by DEMO_DESHROUD, then blast a shroud dollop.
			ThePartitionManager->undoRevealMapForPlayerPermanently( ThePlayerList->getLocalPlayer()->getPlayerIndex() );
			ThePartitionManager->shroudMapForPlayer( ThePlayerList->getLocalPlayer()->getPlayerIndex() );
			break;
		}
#endif // #if defined(RTS_DEBUG)

		// ------------------------------------------------------------------------
#if defined(RTS_DEBUG)
		case GameMessage::MSG_META_DEMO_BEGIN_ADJUST_FOV:
		{
			//DEBUG_ASSERTCRASH(!m_isChangingFOV, ("hmm, mismatched m_isChangingFOV"));
			m_isChangingFOV = true;
			m_anchor = m_currentPos;
			disp = DESTROY_MESSAGE;
			break;
		}
#endif // #if defined(RTS_DEBUG)

		// ------------------------------------------------------------------------
#if defined(RTS_DEBUG)
		case GameMessage::MSG_META_DEMO_END_ADJUST_FOV:
		{
		//	DEBUG_ASSERTCRASH(m_isChangingFOV, ("hmm, mismatched m_isChangingFOV"));
			m_isChangingFOV = false;
			disp = DESTROY_MESSAGE;
			break;
		}
#endif // #if defined(RTS_DEBUG)

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_SAVE_VIEW1:
		case GameMessage::MSG_META_SAVE_VIEW2:
		case GameMessage::MSG_META_SAVE_VIEW3:
		case GameMessage::MSG_META_SAVE_VIEW4:
		case GameMessage::MSG_META_SAVE_VIEW5:
		case GameMessage::MSG_META_SAVE_VIEW6:
		case GameMessage::MSG_META_SAVE_VIEW7:
		case GameMessage::MSG_META_SAVE_VIEW8:
		{
			Int slot = t - GameMessage::MSG_META_SAVE_VIEW1 + 1;
			if ( slot > 0 && slot <= MAX_VIEW_LOCS )
			{
				TheTacticalView->getLocation( &m_viewLocation[slot-1] );
				UnicodeString msg;
				msg.format( TheGameText->fetch( "GUI:BookmarkXSet" ), slot );
				TheInGameUI->message( msg );
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_VIEW_VIEW1:
		case GameMessage::MSG_META_VIEW_VIEW2:
		case GameMessage::MSG_META_VIEW_VIEW3:
		case GameMessage::MSG_META_VIEW_VIEW4:
		case GameMessage::MSG_META_VIEW_VIEW5:
		case GameMessage::MSG_META_VIEW_VIEW6:
		case GameMessage::MSG_META_VIEW_VIEW7:
		case GameMessage::MSG_META_VIEW_VIEW8:
		{
			Int slot = t - GameMessage::MSG_META_VIEW_VIEW1 + 1;
			if ( slot > 0 && slot <= MAX_VIEW_LOCS )
			{
				TheTacticalView->userSetLocation( &m_viewLocation[slot-1] );
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//-----------------------------------------------------------------------------
#if defined(RTS_DEBUG)
		case GameMessage::MSG_META_DEMO_LOCK_CAMERA_TO_PLANES:
		{
			Drawable *first = nullptr;

			if (m_lastPlaneID)
				first = TheGameClient->findDrawableByID( m_lastPlaneID );

			if (first == nullptr)
				first = TheGameClient->firstDrawable();

			if (first)
			{
				Drawable *d = first;
				Bool done = false;

				while(!done)
				{
					// get next Drawable, wrapping around to head of list if necessary
					d = d->getNextDrawable();
					if (d == nullptr)
						d = TheGameClient->firstDrawable();

					// if we've found an airborne object, lock onto it
// "isAboveTerrain" only indicates that we are currently in the air, but that
// could be the case if we are a buggy jumping a hill, or a unit being paradropped.
// the right thing would be to look at the locomotors.
// so this isn't really right, but will suffice for demo purposes.
					if (d->getObject() && d->getObject()->isAboveTerrain() )
					{
						Bool doLock = true;

						// but don't lock onto projectiles
						ProjectileUpdateInterface* pui = nullptr;
						for (BehaviorModule** u = d->getObject()->getBehaviorModules(); *u; ++u)
						{
							if ((pui = (*u)->getProjectileUpdateInterface()) != nullptr)
							{
								doLock = false;
								break;
							}
						}

						if (doLock)
						{
							TheTacticalView->userSetCameraLock( d->getObject()->getID() );
							m_lastPlaneID = d->getID();
							done = true;
							break;
						}
					}

					// if we're back to the first, quit
					if (d == first)
						break;
				}
			}

			disp = DESTROY_MESSAGE;
			break;
		}
#endif // #if defined(RTS_DEBUG)

	}

	return disp;

}

void LookAtTranslator::resetModes()
{
	m_isScrolling = FALSE;
	m_isRotating = FALSE;
	m_isPitching = FALSE;
	m_isPitchingToDefault = FALSE;
	m_isChangingFOV = FALSE;
}

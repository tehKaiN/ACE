/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _ACE_MANAGERS_VIEWPORT_SCROLLBUFFER_H_
#define _ACE_MANAGERS_VIEWPORT_SCROLLBUFFER_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef AMIGA

/**
 * Scrollable buffer manager
 * Uses scrolling-trick from aminet to achieve memory-efficient scroll
 * Must be processed as last, because it calls WaitTOF
 * Requires viewport managers:
 * 	- camera
 * TODO: make it work without tileBuffer manager
 */

#include <ace/types.h>
#include <ace/macros.h>
#include <ace/types.h>
#include <ace/utils/custom.h>
#include <ace/utils/extview.h>
#include <ace/utils/bitmap.h>

#include <ace/managers/blit.h>
#include <ace/managers/copper.h>
#include <ace/managers/viewport/camera.h>

// vPort ptr
#define TAG_SCROLLBUFFER_VPORT          (TAG_USER|1)
// Scrollable area bounds, in pixels
#define TAG_SCROLLBUFFER_BOUND_WIDTH    (TAG_USER|2)
#define TAG_SCROLLBUFFER_BOUND_HEIGHT   (TAG_USER|3)
// Buffer bitmap creation flags
#define TAG_SCROLLBUFFER_BITMAP_FLAGS   (TAG_USER|4)
#define TAG_SCROLLBUFFER_IS_DBLBUF      (TAG_USER|5)
// If in raw mode, offset on copperlist for placing required copper
// instructions, specified in copper instruction count since beginning.
#define TAG_SCROLLBUFFER_COPLIST_OFFSET_START (TAG_USER|6)
#define TAG_SCROLLBUFFER_COPLIST_OFFSET_BREAK (TAG_USER|7)
#define TAG_SCROLLBUFFER_MARGIN_WIDTH   (TAG_USER|8)

#define SCROLLBUFFER_FLAG_COPLIST_RAW 1

/* Types */

typedef struct _tScrollBufferManager {
	tVpManager sCommon;
	tCameraManager *pCamera;       ///< Quick ref to camera

	tBitMap *pFront;               ///< Front buffer in double buffering
	tBitMap *pBack;                ///< Back buffer in double buffering
	union {
		tCopBlock *pStartBlock;    ///< Initial data fetch
		UWORD uwCopperOffsetStart; ///< Start offset on copperlist in COP_RAW mode.
	} copStart;                    ///< Select which field is valid based on SCROLLBUFFER_FLAG_COPLIST_RAW
	union {
		tCopBlock *pBreakBlock;    ///< Bitplane ptr reset
		UWORD uwCopperOffsetBreak; ///< Break offset on copperlist in COP_RAW mode.
	} copBreak;                    ///< Select which field is valid based on SCROLLBUFFER_FLAG_COPLIST_RAW
	tUwCoordYX uBfrBounds;         ///< Real bounds of buffer (includes height reserved for x-scroll)
	UWORD uwBmAvailHeight;         ///< Avail height of buffer to blit (excludes height reserved for x-scroll)
	UWORD uwVpHeightPrev;          ///< Prev height of related VPort, used to force refresh on change
	UWORD uwModulo;                ///< Bitplane modulo
	UBYTE ubFlags;                 ///< Read only. See SCROLLBUFFER_FLAG_*.
} tScrollBufferManager;

/* Globals */

/* Functions */

/**
 * Creates scroll manager
 */
tScrollBufferManager *scrollBufferCreate(void *pTags, ...);

void scrollBufferDestroy(tScrollBufferManager *pManager);

/**
 * Scroll buffer process function
 */
void scrollBufferProcess(tScrollBufferManager *pManager);

void scrollBufferReset(
	tScrollBufferManager *pManager, UBYTE ubMarginWidth,
	UWORD uwBoundWidth, UWORD uwBoundHeight, UBYTE ubBitmapFlags, UBYTE isDblBfr
);

/**
 * Uses unsafe blit copy for using out-of-bound X ccord
 */
void scrollBufferBlitMask(
	tBitMap *pSrc, WORD wSrcX, WORD wSrcY,
	tScrollBufferManager *pDstManager,
	WORD wDstX, WORD wDstY,
	WORD wWidth, WORD wHeight,
	UWORD *pMsk
);

UBYTE scrollBufferGetRawCopperlistInstructionCountStart(UBYTE ubBpp);

UBYTE scrollBufferGetRawCopperlistInstructionCountBreak(UBYTE ubBpp);

#endif // AMIGA

#ifdef __cplusplus
}
#endif

#endif // _ACE_MANAGERS_VIEWPORT_SCROLLBUFFER_H_

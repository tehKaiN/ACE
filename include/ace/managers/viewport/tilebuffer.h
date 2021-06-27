/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _ACE_MANAGERS_VIEWPORT_TILEBUFFER_H_
#define _ACE_MANAGERS_VIEWPORT_TILEBUFFER_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef AMIGA

/**
 * Tilemap buffer manager
 * Provides speed- and memory-efficient tilemap buffer
 * Redraws only 1-tile margin beyond viewport in all dirs
 * Requires viewport managers:
 * 	- camera
 * 	- scroll
 */

#include <ace/types.h>
#include <ace/utils/extview.h>
#include <ace/managers/viewport/camera.h>
#include <ace/managers/viewport/scrollbuffer.h>

/**
 * @brief Pointer to parent vPort. Mandatory.
 */
#define TAG_TILEBUFFER_VPORT (TAG_USER|1)

/**
 * @brief Scrollable area bounds, in pixels. Mandatory.
 */
#define TAG_TILEBUFFER_BOUND_TILE_X     (TAG_USER|2)
#define TAG_TILEBUFFER_BOUND_TILE_Y     (TAG_USER|3)
/**
 * @brief Size of tile, given in bitshift. Set to 4 for 16px, 5 for 32px, etc. Mandatory.
 */
#define TAG_TILEBUFFER_TILE_SHIFT (TAG_USER|4)

/**
 * @brief Buffer bitmap creation flags. Defaults to BMF_CLEAR.
 */
#define TAG_TILEBUFFER_BITMAP_FLAGS (TAG_USER|5)

/**
 * @brief Set this flag to 1 to enable double buffering. Defaults to 0.
 */
#define TAG_TILEBUFFER_IS_DBLBUF    (TAG_USER|6)

/**
 * @brief If in raw copper mode, offset on copperlist for placing required
 * copper instructions, specified in copper instruction count since beginning.
 */
#define TAG_TILEBUFFER_COPLIST_OFFSET_START (TAG_USER|7)
#define TAG_TILEBUFFER_COPLIST_OFFSET_BREAK (TAG_USER|8)

/**
 * @brief Pointer to tileset bitmap. Expects it to have all tiles in single
 * column. Mandatory.
 */
#define TAG_TILEBUFFER_TILESET             (TAG_USER|9)

/**
 * @brief Pointer to callback which gets called in case any tile gets drawn.
 *
 * Use this to draw extra stuff on top of your tiles.
 *
 * @see tTileDrawCallback
 */
#define TAG_TILEBUFFER_CALLBACK_TILE_DRAW  (TAG_USER|10)

/**
 * @brief Max length of tile redraw queue. Mandatory, must be non-zero.
 *
 * @see tileBufferQueueProcess()
 */
#define TAG_TILEBUFFER_REDRAW_QUEUE_LENGTH (TAG_USER|11)

/* types */

typedef void (*tTileDrawCallback)(
	UWORD uwTileX, UWORD uwTileY,
	tBitMap *pBitMap, UWORD uwBitMapX, UWORD uwBitMapY
);

typedef struct _tMarginState {
	WORD wTilePos; ///< Index of row/col to update
	WORD wTileCurr; ///< Index of current tile to update in row/col
	WORD wTileEnd;  ///< Index of last+1  tile to update in row/col
} tMarginState;

typedef struct _tRedrawState {
	tMarginState sMarginL; ///< Data for left margin
	tMarginState sMarginR; ///< Ditto, right
	tMarginState sMarginU; ///< Ditto, up
	tMarginState sMarginD; ///< Ditto, down
	// Vars needed in Process, reset in Create
	tMarginState *pMarginX;         ///< Idx of X margin to be redrawn
	tMarginState *pMarginOppositeX; ///< Opposite margin of pMarginX
	tMarginState *pMarginY;         ///< Idx of Y margin to be redrawn
	tMarginState *pMarginOppositeY; ///< Opposite margin of pMarginY
	// Tile redraw queue
	tUwCoordYX *pPendingQueue;
	UBYTE ubPendingCount;
} tRedrawState;

typedef struct _tTileBufferManager {
	tVpManager sCommon;
	tCameraManager *pCamera;       ///< Quick ref to Camera
	tScrollBufferManager *pScroll; ///< Quick ref to Scroll
	// Manager vars
	tUwCoordYX uTileBounds;       ///< Tile count in x,y
	UBYTE ubTileSize;             ///< Tile size in pixels
	UBYTE ubTileShift;            ///< Tile size in shift, e.g. 4 for 16: 1 << 4 == 16
	UWORD uwMarginedWidth;        ///< Width of visible area + margins
	UWORD uwMarginedHeight;       ///< Height of visible area + margins
	                              ///  TODO: refresh when scrollbuffer changes
	tTileDrawCallback cbTileDraw; ///< Called when tile is redrawn
	UBYTE **pTileData;            ///< 2D array of tile indices
	tBitMap *pTileSet;            ///< Tileset - one tile beneath another
	// Margin & queue geometry
	UBYTE ubMarginXLength; ///< Tile number in margins: left & right
	UBYTE ubMarginYLength; ///< Ditto, up & down
	UBYTE ubQueueSize;
	// Redraw state and double buffering
	tRedrawState pRedrawStates[2];
	UBYTE ubStateIdx;
	UBYTE ubWidthShift;
} tTileBufferManager;

/* globals */

/* functions */

/**
 * @brief Processes tile queue. Typically should be called once per game loop,
 * but other refreshing strategies can be used for better load balancing.
 *
 * @param pManager The tile manager to be processed.
 * @see tileBufferProcess()
 */
void tileBufferQueueProcess(tTileBufferManager *pManager);

/**
 * Tilemap buffer manager create fn. See TAG_TILEBUFFER_* for available options.
 *
 * After calling this function, be sure to do the following:
 * - set initial pos in camera manager,
 * - fill tilemap on .pTileData with tile indices,
 * - call tileBufferRedrawAll()
 *
 * @see tileBufferRedrawAll()
 * @see tileBufferDestroy()
 */
tTileBufferManager *tileBufferCreate(void *pTags, ...);

void tileBufferDestroy(tTileBufferManager *pManager);

/**
 * @brief Processes given tile buffer manager.
 *
 * Typically, redraws one tile for X and one for Y margins.
 * In case of impending display of a margin, redraws all of its remaining tiles.
 * It doesn't redraw manually invalidated/changed tiles!
 *
 * @see tileBufferQueueProcess()
 */
void tileBufferProcess(tTileBufferManager *pManager);

/**
 * Redraws tiles on whole screen.
 * Use for init or something like that, as it's slooooooooow.
 * Be sure to have display turned off or palette dimmed since even on double
 * buffering it will redraw both buffers.
 */
void tileBufferRedrawAll(tTileBufferManager *pManager);

/**
 * Redraws selected tile, calls custom redraw callback
 * Calculates destination on buffer
 * Use for single redraws
 */
void tileBufferDrawTile(
	const tTileBufferManager *pManager, UWORD uwTileIdxX, UWORD uwTileIdxY
);

/**
 * Redraws selected tile, calls custom redraw callback
 * Destination coord on buffer must be calculated externally - avoids recalc
 * Use for batch redraws with smart uwBfrXY update
 */
void tileBufferDrawTileQuick(
	const tTileBufferManager *pManager,
	UWORD uwTileIdxX, UWORD uwTileIdxY, UWORD uwBfrX, UWORD uwBfrY
);

/**
 * Redraws all tiles intersecting with given rectangle
 * Only tiles currently on buffer are redrawn
 */
void tileBufferInvalidateRect(
	tTileBufferManager *pManager, UWORD uwX, UWORD uwY,
	UWORD uwWidth, UWORD uwHeight
);

void tileBufferInvalidateTile(
	tTileBufferManager *pManager, UWORD uwTileX, UWORD uwTileY
);

UBYTE tileBufferIsTileOnBuffer(
	const tTileBufferManager *pManager, UWORD uwTileX, UWORD uwTileY
);

/**
 * @brief Changes tile at given position to another tile and schedules its
 * redraw using redraw queue.
 *
 * @param pManager The tile manager to be used.
 * @param uwX The X coordinate of tile, in tile-space.
 * @param uwY The Y coordinate of tile, in tile-space.
 * @param uwIdx Index of tile to be placed on given position.
 */
void tileBufferSetTile(
	tTileBufferManager *pManager, UWORD uwX, UWORD uwY, UWORD uwIdx
);

static inline UBYTE tileBufferGetRawCopperlistInstructionCountStart(UBYTE ubBpp) {
    return scrollBufferGetRawCopperlistInstructionCountStart(ubBpp);
}

static inline UBYTE tileBufferGetRawCopperlistInstructionCountBreak(UBYTE ubBpp) {
    return scrollBufferGetRawCopperlistInstructionCountBreak(ubBpp);
}

#endif // AMIGA

#ifdef __cplusplus
}
#endif

#endif // _ACE_MANAGERS_VIEWPORT_TILEBUFFER_H_

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <ace/managers/viewport/scrollbuffer.h>
#include <ace/utils/tag.h>
#include <limits.h>

#ifdef AMIGA

static UWORD nearestPowerOf2(UWORD uwVal) {
	// https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
	// Decrease by one and fill result with ones, then increase by one
	--uwVal;
	uwVal |= uwVal >> 1;
	uwVal |= uwVal >> 2;
	uwVal |= uwVal >> 4;
	uwVal |= uwVal >> 8;
	++uwVal;
	return uwVal;
}

tScrollBufferManager *scrollBufferCreate(void *pTags, ...) {
	va_list vaTags;
	tCopList *pCopList = 0;
	tScrollBufferManager *pManager;
	UBYTE ubMarginWidth;
	UWORD uwBoundWidth, uwBoundHeight;
	UBYTE ubBitmapFlags;
	UBYTE isCameraCreated = 0;
	UBYTE isDblBuf;

	logBlockBegin("scrollBufferCreate(pTags: %p, ...)", pTags);
	va_start(vaTags, pTags);

	// Init manager
	pManager = memAllocFastClear(sizeof(tScrollBufferManager));
	pManager->sCommon.process = (tVpManagerFn)scrollBufferProcess;
	pManager->sCommon.destroy = (tVpManagerFn)scrollBufferDestroy;
	pManager->sCommon.ubId = VPM_SCROLL;
	logWrite("Addr: %p\n", pManager);

	tVPort *pVPort = (tVPort*)tagGet(pTags, vaTags, TAG_SCROLLBUFFER_VPORT, 0);
	if(!pVPort) {
		logWrite("ERR: No parent viewport (TAG_SCROLLBUFFER_VPORT) specified!\n");
		goto fail;
	}
	pManager->sCommon.pVPort = pVPort;
	logWrite("Parent VPort: %p\n", pVPort);

	ubMarginWidth = tagGet(
		pTags, vaTags, TAG_SCROLLBUFFER_MARGIN_WIDTH, UCHAR_MAX
	);
	if(ubMarginWidth == UCHAR_MAX) {
		logWrite(
			"ERR: No margin width (TAG_SCROLLBUFFER_MARGIN_WIDTH) specified!\n"
		);
		goto fail;
	}

	// Buffer bitmap
	uwBoundWidth = tagGet(
		pTags, vaTags, TAG_SCROLLBUFFER_BOUND_WIDTH, pVPort->uwWidth
	);
	uwBoundHeight = tagGet(
		pTags, vaTags, TAG_SCROLLBUFFER_BOUND_HEIGHT, pVPort->uwHeight
	);
	ubBitmapFlags = tagGet(
		pTags, vaTags, TAG_SCROLLBUFFER_BITMAP_FLAGS, BMF_CLEAR
	);
	logWrite("Bounds: %ux%u\n", uwBoundWidth, uwBoundHeight);

	isDblBuf = tagGet(pTags, vaTags, TAG_SCROLLBUFFER_IS_DBLBUF, 0);

	// Create copperlist entries
	pCopList = pVPort->pView->pCopList;
	if(pCopList->ubMode == COPPER_MODE_BLOCK) {
		pManager->copStart.pStartBlock = copBlockCreate(
			pVPort->pView->pCopList, 2 * pVPort->ubBPP + 8,
			// Vertically addition from DiWStrt, horizontally a bit before last fetch.
			// First to set are ddf, modulos & shift so they are changed during fetch.
			0xE2-7*4, pVPort->uwOffsY + 0x2C-1
		);
		pManager->copBreak.pBreakBlock = copBlockCreate(
			pVPort->pView->pCopList, 2 * pVPort->ubBPP + 2,
			// Dummy position - will be updated
			0x7F, 0xFF
		);
	}
	else {
		const UWORD uwInvalidCopOffs = -1;
		pManager->ubFlags |= SCROLLBUFFER_FLAG_COPLIST_RAW;
		pManager->copStart.uwCopperOffsetStart = tagGet(
			pTags, vaTags, TAG_SCROLLBUFFER_COPLIST_OFFSET_START, uwInvalidCopOffs
		);
		if(pManager->copStart.uwCopperOffsetStart == uwInvalidCopOffs) {
			logWrite(
				"ERR: Copperlist offset (TAG_SCROLLBUFFER_COPLIST_OFFSET_START) not specified!\n"
			);
			goto fail;
		}
		pManager->copBreak.uwCopperOffsetBreak = tagGet(
			pTags, vaTags, TAG_SCROLLBUFFER_COPLIST_OFFSET_BREAK, uwInvalidCopOffs
		);
		if(pManager->copBreak.uwCopperOffsetBreak == uwInvalidCopOffs) {
			logWrite(
				"ERR: Copperlist offset (TAG_SCROLLBUFFER_COPLIST_OFFSET_BREAK) not specified!\n"
			);
			goto fail;
		}
		logWrite("Copperlist offsets: start: %u, break: %u\n",
				 pManager->copStart.uwCopperOffsetStart,
				 pManager->copBreak.uwCopperOffsetBreak);
	}

	scrollBufferReset(
		pManager, ubMarginWidth, uwBoundWidth, uwBoundHeight,
		ubBitmapFlags, isDblBuf
	);

	// Must be before camera? Shouldn't be as there are priorities on manager list
	vPortAddManager(pVPort, (tVpManager*)pManager);

	// Find camera manager, create if not exists
	pManager->pCamera = (tCameraManager*)vPortGetManager(pVPort, VPM_CAMERA);
	if(!pManager->pCamera) {
		pManager->pCamera = cameraCreate(
			pVPort, 0, 0, uwBoundWidth, uwBoundHeight, isDblBuf
		);
		isCameraCreated = 1;
	}
	else {
		cameraReset(pManager->pCamera, 0,0, uwBoundWidth, uwBoundHeight, isDblBuf);
	}

	// TODO: Update copperlist with current camera pos?

	va_end(vaTags);
	logBlockEnd("scrollBufferCreate");
	return pManager;
fail:
	if(isCameraCreated) {
		cameraDestroy(pManager->pCamera);
	}
	if(pCopList && pCopList->ubMode == COPPER_MODE_BLOCK && pManager->copStart.pStartBlock) {
		copBlockDestroy(pCopList, pManager->copStart.pStartBlock);
		if(pManager->copBreak.pBreakBlock) {
			copBlockDestroy(pCopList, pManager->copBreak.pBreakBlock);
		}
	}

	memFree(pManager, sizeof(tScrollBufferManager));
	va_end(vaTags);
	logBlockEnd("scrollBufferCreate");
	return 0;
}

void scrollBufferDestroy(tScrollBufferManager *pManager) {
	logBlockBegin("scrollBufferDestroy(pManager: %p)", pManager);

	if(!(pManager->ubFlags & SCROLLBUFFER_FLAG_COPLIST_RAW)) {
		copBlockDestroy(pManager->sCommon.pVPort->pView->pCopList, pManager->copStart.pStartBlock);
		copBlockDestroy(pManager->sCommon.pVPort->pView->pCopList, pManager->copBreak.pBreakBlock);
	}

	if(pManager->pFront && pManager->pFront != pManager->pBack) {
		bitmapDestroy(pManager->pFront);
	}
	if(pManager->pBack) {
		bitmapDestroy(pManager->pBack);
	}
	memFree(pManager, sizeof(tScrollBufferManager));

	logBlockEnd("scrollBufferDestroy()");
}

// All functions to update the raw copperlist below must be matched for the copperlist layout and size

UBYTE scrollBufferGetRawCopperlistInstructionCountStart(UBYTE ubBpp) {
	return (
		1 + // initial WAIT
	 	1 + 2 * ubBpp + // bitplane ptrs & bplcon commands
		4 // After bitplane ptrs & bplcon
	);
}

UBYTE scrollBufferGetRawCopperlistInstructionCountBreak(UBYTE ubBpp) {
	return (
		4 + // copper jump location, skip, strobe
	 	1 + 2 * ubBpp // WAIT & bitplane ptrs
	);
}

static void resetStartCopperlist(tCopCmd *pCmds, const UWORD uwOffsY, const UBYTE ubBPP, const UWORD uwModulo) {
	UBYTE i = 0;
	copSetWait(&pCmds[i++].sWait, 0, 0x2C + uwOffsY);
	// prepare bitplane ptrs & bplcon commands. will be updated in process
	copSetMove(&pCmds[i++].sMove, &g_pCustom->bplcon1, 0);
	for(UBYTE j = 0; j < ubBPP; j++) {
		copSetMove(&pCmds[i++].sMove, &g_pBplFetch[j].uwHi, 0);
		copSetMove(&pCmds[i++].sMove, &g_pBplFetch[j].uwLo, 0);
	}
	// After bitplane ptrs & bplcon
	copSetMove(&pCmds[i++].sMove, &g_pCustom->ddfstrt, 0x0030);   // Fetch start
	copSetMove(&pCmds[i++].sMove, &g_pCustom->bpl1mod, uwModulo); // Odd planes modulo
	copSetMove(&pCmds[i++].sMove, &g_pCustom->bpl2mod, uwModulo); // Even planes modulo
	copSetMove(&pCmds[i++].sMove, &g_pCustom->ddfstop, 0x00D0);   // Fetch stop
}

static void updateStartCopperlist(tCopCmd *pCmds, const tBitMap *pBitmap, const UWORD uwShift, const ULONG ulPlaneOffs) {
	UBYTE i = 1; // the wait at the beginning of the start block doesn't change
	copSetMoveVal(&pCmds[i++].sMove, uwShift);
	for(UBYTE j = 0; j < pBitmap->Depth; j++) {
		ULONG ulPlaneAddr = (ULONG)(pBitmap->Planes[j]) + ulPlaneOffs;
		copSetMoveVal(&pCmds[i++].sMove, ulPlaneAddr >> 16);
		copSetMoveVal(&pCmds[i++].sMove, ulPlaneAddr & 0xFFFF);
	}
}

static void resetBreakCopperlist(tCopCmd *pCmds, const UWORD uwOffsY, const UBYTE ubBPP) {
	UBYTE i = 0;
	// copper jump location & strobe to jump past the break block
	UBYTE offset = scrollBufferGetRawCopperlistInstructionCountBreak(ubBPP);
	copSetMove(&pCmds[i++].sMove, &g_pCop2Lc->uwHi, (ULONG)(pCmds + offset) >> 16);
	copSetMove(&pCmds[i++].sMove, &g_pCop2Lc->uwLo, (ULONG)(pCmds + offset) & 0xFFFF);
	// prepare a wait/skip instruction that will always be true immediately
	copSetWait(&pCmds[i++].sWait, 0, 0);
	// strobe
	copSetMove(&pCmds[i++].sMove, &g_pCustom->copjmp2, 1);

	// wait & bitplane ptrs
	copSetWait(&pCmds[i++].sWait, 0, 0x2C + uwOffsY);
	for(UBYTE j = 0; j < ubBPP; j++) {
		copSetMove(&pCmds[i++].sMove, &g_pBplFetch[j].uwHi, 0);
		copSetMove(&pCmds[i++].sMove, &g_pBplFetch[j].uwLo, 0);
	}
}

static void updateBreakCopperlist(tCopCmd *pCmds, const tBitMap *pBitmap, const UWORD uwSplitPos, const ULONG ulBplAddX) {
	pCmds[2].sWait.bfIsSkip = 1; // skip the jump so we have this block enabled

	UBYTE i = 4; // the first 4 bytes are cop2lch, cop2lcl, SKIP/WAIT, cop2jmp
	pCmds[i++].sWait.bfWaitY = uwSplitPos;
	for(UBYTE j = 0; j < pBitmap->Depth; j++) {
		ULONG ulPlaneAddr = (ULONG)(pBitmap->Planes[j]) + ulBplAddX;
		copSetMoveVal(&pCmds[i++].sMove, ulPlaneAddr >> 16);
		copSetMoveVal(&pCmds[i++].sMove, ulPlaneAddr & 0xFFFF);
	}
}

static void disableBreakCopperlist(tCopCmd *pCmds) {
	// disable the block by setting the block to skip the jump if we're past the beginning
	// of the start block (which we always are)
	pCmds[2].sWait.bfIsSkip = 0; // do not skip the jump
}

FN_HOTSPOT
void scrollBufferProcess(tScrollBufferManager *pManager) {
	UWORD uwVpHeight = pManager->sCommon.pVPort->uwHeight;

	// TODO: use deltaX and deltaY to decide if we need to update anything

	// convert camera pos to scroll pos
	UWORD uwScrollX = pManager->pCamera->uPos.uwX;
	UWORD uwScrollY = pManager->pCamera->uPos.uwY & (pManager->uwBmAvailHeight - 1);

	// preparations for new copperlist
	UWORD uwShift = (16 - (uwScrollX & 0xF)) & 0xF; // Bitplane shift - single
	uwShift = (uwShift << 4) | uwShift;             // Bitplane shift - PF1 | PF2
	ULONG ulBplAddX = ((uwScrollX - 1) >> 4) << 1;  // must be ULONG!

	tCopList *pCopList = pManager->sCommon.pVPort->pView->pCopList;

	ULONG ulPlaneOffs = ulBplAddX + (pManager->pBack->BytesPerRow*uwScrollY);
	if(pManager->ubFlags & SCROLLBUFFER_FLAG_COPLIST_RAW) {
		updateStartCopperlist(&pCopList->pBackBfr->pList[pManager->copStart.uwCopperOffsetStart], pManager->pBack, uwShift, ulPlaneOffs);

		tCopCmd *pCmdListBreak = &pCopList->pBackBfr->pList[pManager->copBreak.uwCopperOffsetBreak];
		if(pManager->uwBmAvailHeight - uwScrollY <= uwVpHeight) {
			updateBreakCopperlist(
				pCmdListBreak, pManager->pBack,
				0x2C + pManager->sCommon.pVPort->uwOffsY + pManager->uwBmAvailHeight - uwScrollY,
				ulBplAddX
			);
		}
		else {
			disableBreakCopperlist(pCmdListBreak);
		}
	}
	else {
		// Initial copper block
		tCopBlock *pBlock = pManager->copStart.pStartBlock;
		pBlock->uwCurrCount = 0; // Rewind copBlock
		copMove(pCopList, pBlock, &g_pCustom->bplcon1, uwShift);
		for(UBYTE i = pManager->sCommon.pVPort->ubBPP; i--;) {
			ULONG ulPlaneAddr = (ULONG)(pManager->pBack->Planes[i]) + ulPlaneOffs;
			copMove(pCopList, pBlock, &g_pBplFetch[i].uwHi, ulPlaneAddr >> 16);
			copMove(pCopList, pBlock, &g_pBplFetch[i].uwLo, ulPlaneAddr & 0xFFFF);
		}
		// TODO setting colors before and after copper instructions moved viewport
		// one line lower on 4bpp - there will be problem on 5 & 6bpp
		pBlock->uwCurrCount += 4; // Add constant part

		// Copper block after Y-break
		pBlock = pManager->copBreak.pBreakBlock;

		pBlock->uwCurrCount = 0; // Rewind copBlock
		if(pManager->uwBmAvailHeight - uwScrollY <= uwVpHeight) {
			if(pBlock->ubDisabled) {
				copBlockEnable(pCopList, pBlock);
			}
			copBlockWait(pCopList, pBlock, 0, 0x2C + pManager->sCommon.pVPort->uwOffsY + pManager->uwBmAvailHeight - uwScrollY);
			for(UBYTE i = pManager->sCommon.pVPort->ubBPP; i--;) {
				ULONG ulPlaneAddr = (ULONG)(pManager->pBack->Planes[i]) + ulBplAddX;
				copMove(pCopList, pBlock, &g_pBplFetch[i].uwHi, ulPlaneAddr >> 16);
				copMove(pCopList, pBlock, &g_pBplFetch[i].uwLo, ulPlaneAddr & 0xFFFF);
			}
		}
		else {
			copBlockDisable(pCopList, pBlock);
		}
	}

	pManager->uwVpHeightPrev = uwVpHeight;

	// Swap buffers if needed
	if(pManager->pBack != pManager->pFront) {
		tBitMap *pTmp = pManager->pBack;
		pManager->pBack = pManager->pFront;
		pManager->pFront = pTmp;
	}
}

void scrollBufferReset(
	tScrollBufferManager *pManager, UBYTE ubMarginWidth,
	UWORD uwBoundWidth, UWORD uwBoundHeight, UBYTE ubBitmapFlags, UBYTE isDblBuf
) {
	UWORD uwVpWidth, uwVpHeight;
	UWORD uwCalcWidth, uwCalcHeight;
	logBlockBegin(
		"scrollBufferReset(pManager: %p, ubMarginWidth: %hu, uwBoundWidth: %u, uwBoundHeight: %u)",
		pManager, ubMarginWidth, uwBoundWidth, uwBoundHeight
	);
	// Helper vars
	uwVpWidth = pManager->sCommon.pVPort->uwWidth;
	uwVpHeight = pManager->sCommon.pVPort->uwHeight;

	// Reset manager fields
	pManager->uwVpHeightPrev = 0;
	pManager->uBfrBounds.uwX = uwBoundWidth;
	pManager->uBfrBounds.uwY = uwBoundHeight;
	// Optimize avail height to power of two so that modulo can be an AND
	pManager->uwBmAvailHeight = nearestPowerOf2(ubMarginWidth * (blockCountCeil(uwVpHeight, ubMarginWidth) + 4));

	// Destroy old buffer bitmap
	if(pManager->pFront && pManager->pFront != pManager->pBack) {
		bitmapDestroy(pManager->pFront);
	}
	if(pManager->pBack) {
		bitmapDestroy(pManager->pBack);
	}

	// Create new buffer bitmap
	uwCalcWidth = nearestPowerOf2(uwVpWidth + ubMarginWidth*4);
	uwCalcHeight = pManager->uwBmAvailHeight + blockCountCeil(uwBoundWidth, uwVpWidth) - 1;
	pManager->pBack = bitmapCreate(
		uwCalcWidth, uwCalcHeight, pManager->sCommon.pVPort->ubBPP, ubBitmapFlags
	);
	if(isDblBuf) {
		pManager->pFront = bitmapCreate(
			uwCalcWidth, uwCalcHeight, pManager->sCommon.pVPort->ubBPP, ubBitmapFlags
		);
	}
	else {
		pManager->pFront = pManager->pBack;
	}
	pManager->uwModulo = pManager->pBack->BytesPerRow - (uwVpWidth >> 3) - 2;

	// Constant stuff in copperlist
	tCopList *pCopList = pManager->sCommon.pVPort->pView->pCopList;
	if(pManager->ubFlags & SCROLLBUFFER_FLAG_COPLIST_RAW) {
		resetStartCopperlist(
				&pCopList->pBackBfr->pList[pManager->copStart.uwCopperOffsetStart],
				pManager->sCommon.pVPort->uwOffsY,
				pManager->sCommon.pVPort->ubBPP,
				pManager->uwModulo);
		resetBreakCopperlist(
				&pCopList->pBackBfr->pList[pManager->copBreak.uwCopperOffsetBreak],
				pManager->sCommon.pVPort->uwOffsY,
				pManager->sCommon.pVPort->ubBPP);
		// again for double bufferred
		resetStartCopperlist(
				&pCopList->pFrontBfr->pList[pManager->copStart.uwCopperOffsetStart],
				pManager->sCommon.pVPort->uwOffsY,
				pManager->sCommon.pVPort->ubBPP,
				pManager->uwModulo);
		resetBreakCopperlist(
				&pCopList->pFrontBfr->pList[pManager->copBreak.uwCopperOffsetBreak],
				pManager->sCommon.pVPort->uwOffsY,
				pManager->sCommon.pVPort->ubBPP);
	}
	else {
		tCopBlock *pBlock = pManager->copStart.pStartBlock;
		// Set initial WAIT
		copBlockWait(pCopList, pBlock, 0, 0x2C + pManager->sCommon.pVPort->uwOffsY);
		// After bitplane ptrs & bplcon
		pBlock->uwCurrCount = 2 * pManager->sCommon.pVPort->ubBPP + 1;
		copMove(pCopList, pBlock, &g_pCustom->ddfstrt, 0x0030);             // Fetch start
		copMove(pCopList, pBlock, &g_pCustom->bpl1mod, pManager->uwModulo); // Odd planes modulo
		copMove(pCopList, pBlock, &g_pCustom->bpl2mod, pManager->uwModulo); // Even planes modulo
		copMove(pCopList, pBlock, &g_pCustom->ddfstop, 0x00D0);             // Fetch stop
	}

	// Refresh bitplane pointers in copperlist - 2x for double buffered and with swapped cop front and back buffers
	scrollBufferProcess(pManager);
	tCopBfr *pBackBfr = pCopList->pBackBfr;
	pCopList->pBackBfr = pCopList->pFrontBfr;
	scrollBufferProcess(pManager);
	pCopList->pBackBfr = pBackBfr;

	logBlockEnd("scrollBufferReset()");
}

void scrollBufferBlitMask(
	tBitMap *pSrc, WORD wSrcX, WORD wSrcY,
	tScrollBufferManager *pDstManager, WORD wDstX, WORD wDstY,
	WORD wWidth, WORD wHeight, UWORD *pMsk
) {
	// TODO: if area is visible
	wDstY %= pDstManager->uwBmAvailHeight;
	wDstY %= (pDstManager->pBack->BytesPerRow<<3);

	if(wDstY + wHeight <= pDstManager->uwBmAvailHeight) {
		// single blit
		blitUnsafeCopyMask(
			pSrc, wSrcX, wSrcY,
			pDstManager->pBack, wDstX, wDstY,
			wWidth, wHeight, pMsk
		);
	}
	else {
		// split blit in two
		WORD wPartHeight;
		wPartHeight = pDstManager->uwBmAvailHeight - wDstY;
		blitUnsafeCopyMask(
			pSrc, wSrcX, wSrcY,
			pDstManager->pBack, wDstX, wDstY,
			wWidth, wPartHeight, pMsk
		);
		blitUnsafeCopyMask(
			pSrc, wSrcX, wSrcY + wPartHeight,
			pDstManager->pBack, wDstX, 0,
			wWidth, wHeight - wPartHeight, pMsk
		);
	}
}

#endif // AMIGA

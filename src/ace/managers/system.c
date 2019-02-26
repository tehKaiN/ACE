/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <ace/managers/system.h>
#include <stdlib.h>
#include <clib/graphics_protos.h>
#include <hardware/intbits.h>
#include <hardware/dmabits.h>
#include <ace/utils/custom.h>
#include <ace/managers/log.h>
#include <ace/managers/timer.h>

// There are hardware interrupt vectors
// Some may be triggered by more than one event - there are 15 events
// http://eab.abime.net/showthread.php?p=1081007
// http://ada.untergrund.net/?p=boardthread&id=31
// http://palbo.dk/dataskolen/maskinsprog/english/letter_09.pdf

//---------------------------------------------------------------------- DEFINES

// First OS interrupt is at offset 0x64 - each is 4 bytes wide
#define SYSTEM_INT_VECTOR_FIRST (0x64/4)
#define SYSTEM_INT_VECTOR_COUNT 7
#define SYSTEM_INT_HANDLER_COUNT 15

//------------------------------------------------------------------------ TYPES

typedef void (*tHwIntVector)(void);

typedef struct _tAceInterrupt {
	volatile tAceIntHandler pHandler;
	volatile void *pData;
} tAceInterrupt;

//---------------------------------------------------------------------- GLOBALS

// Saved regs
UWORD s_uwOsIntEna;
UWORD s_uwOsDmaCon;
UWORD s_uwAceDmaCon = 0;
UWORD s_uwOsInitialDma;

// Interrupts
void HWINTERRUPT int1Handler(void);
void HWINTERRUPT int2Handler(void);
void HWINTERRUPT int3Handler(void);
void HWINTERRUPT int4Handler(void);
void HWINTERRUPT int5Handler(void);
void HWINTERRUPT int6Handler(void);
void HWINTERRUPT int7Handler(void);
static const tHwIntVector s_pAceHwInterrupts[SYSTEM_INT_VECTOR_COUNT] = {
	int1Handler, int2Handler, int3Handler, int4Handler,
	int5Handler, int6Handler, int7Handler
};
static volatile tHwIntVector s_pOsHwInterrupts[SYSTEM_INT_VECTOR_COUNT] = {0};
static volatile tHwIntVector * const s_pHwVectors = 0;
static volatile tAceInterrupt s_pAceInterrupts[SYSTEM_INT_HANDLER_COUNT] = {{0}};
static volatile UWORD s_uwAceInts = 0;

// Manager logic vars
WORD s_wSystemUses;
struct GfxBase *GfxBase = 0;
struct View *s_pOsView;
const UWORD s_uwOsMinDma = DMAF_DISK | DMAF_BLITTER;

//----------------------------------------------------------- INTERRUPT HANDLERS

FN_HOTSPOT
void HWINTERRUPT int1Handler(void) {
	// Soft / diskBlk / TBE (RS232 TX end)
}

FN_HOTSPOT
void HWINTERRUPT int2Handler(void) {
	// Parallel, keyboard, mouse, "some of disk functions"
	// ACE only uses it for keyboard so no decision taking here atm
	// TODO: this could be re-enabled with vblank since we don't need it
	// to retrigger during same frame.
	UBYTE ubIcrA = g_pCiaA->icr; // Read clears interrupt flags
	if(ubIcrA & CIAICR_SERIAL) {
		// Keyboard
		if(s_pAceInterrupts[INTB_PORTS].pHandler) {
			s_pAceInterrupts[INTB_PORTS].pHandler(
				g_pCustom, s_pAceInterrupts[INTB_PORTS].pData
			);
		}
	}
	if(ubIcrA & CIAICR_FLAG) {

	}
	if(ubIcrA & CIAICR_TIMER_A) {

	}
	if(ubIcrA & CIAICR_TIMER_B) {

	}
	if(ubIcrA & CIAICR_TOD) {

	}
	g_pCustom->intreq = INTF_PORTS;
}

FN_HOTSPOT
void HWINTERRUPT int3Handler(void) {
	// VBL / Copper / Blitter
	UWORD uwIntReq = g_pCustom->intreqr;
	UWORD uwReqClr = 0;

	// Vertical blanking
	if(uwIntReq & INTF_VERTB) {
		// Do ACE-specific stuff
		// TODO when ACE gets ported to C++ this could be constexpr if'ed
		timerOnInterrupt();

		// Process handlers
		if(s_pAceInterrupts[INTB_VERTB].pHandler) {
			s_pAceInterrupts[INTB_VERTB].pHandler(
				g_pCustom, s_pAceInterrupts[INTB_VERTB].pData
			);
		}
		uwReqClr = INTF_VERTB;
	}

	// Copper
	if(uwIntReq & INTF_COPER) {
		if(s_pAceInterrupts[INTB_COPER].pHandler) {
			s_pAceInterrupts[INTB_COPER].pHandler(
				g_pCustom, s_pAceInterrupts[INTB_VERTB].pData
			);
		}
		uwReqClr |= INTF_COPER;
	}

	// Blitter
	if(uwIntReq & INTF_BLIT) {
		if(s_pAceInterrupts[INTB_BLIT].pHandler) {
			s_pAceInterrupts[INTB_BLIT].pHandler(
				g_pCustom, s_pAceInterrupts[INTB_VERTB].pData
			);
		}
		uwReqClr |= INTF_BLIT;
	}
	g_pCustom->intreq = uwReqClr;
}

FN_HOTSPOT
void HWINTERRUPT int4Handler(void) {
	// Audio
}

FN_HOTSPOT
void HWINTERRUPT int5Handler(void) {
	// DskSyn / RBF
}

FN_HOTSPOT
void HWINTERRUPT int6Handler(void) {
	// CIA B
}

FN_HOTSPOT
void HWINTERRUPT int7Handler(void) {
	// EXTERNAL
}

//-------------------------------------------------------------------- FUNCTIONS

void systemKill(const char *szMsg) {
	printf("ERR: SYSKILL: %s", szMsg);
	logWrite("ERR: SYSKILL: %s", szMsg);

	if(GfxBase) {
		CloseLibrary((struct Library *) GfxBase);
	}
	exit(EXIT_FAILURE);
}

/**
 * @brief The startup code to give ACE somewhat initial state.
 */
void systemCreate(void) {
	// Disable as much of OS stuff as possible so that it won't trash stuff when
	// re-enabled periodically.
	// Save the system copperlists and flush the view
	GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 0L);
	if (!GfxBase) {
		systemKill("Can't open Gfx Library!\n");
		return;
	}
	s_pOsView = GfxBase->ActiView;
	WaitTOF();
	LoadView(0);
	WaitTOF();

	// get VBR location on 68010+ machine
	// TODO VBR

	// save the state of the hardware registers (INTENA, DMA, ADKCON etc.)
	s_uwOsIntEna = g_pCustom->intenar;
	s_uwOsDmaCon = g_pCustom->dmaconr;
	s_uwOsInitialDma = s_uwOsDmaCon;

	// Disable interrupts (this is the actual "kill system/OS" part)
	g_pCustom->intena = 0x7FFF;
	g_pCustom->intreq = 0x7FFF;

	// Disable all DMA - only once
	// Wait for vbl before disabling sprite DMA
	while (!(g_pCustom->intreqr & INTF_VERTB)) {}
	g_pCustom->dmacon = 0x07FF;

	// Unuse system so that it gets backed up once and then re-enable
	// as little as needed
	s_wSystemUses = 1;
	systemUnuse();
	systemUse();
}

/**
 * @brief Cleanup after app, restore anything that systemCreate took over.
 */
void systemDestroy(void) {
	// disable all interrupts
	g_pCustom->intena = 0x7FFF;
	g_pCustom->intreq = 0x7FFF;

	// Wait for vbl before disabling sprite DMA
	while (!(g_pCustom->intreqr & INTF_VERTB)) {}
	g_pCustom->dmacon = 0x07FF;
	g_pCustom->intreq = 0x7FFF;

	// Start waking up OS
	if(s_wSystemUses != 0) {
		logWrite("ERR: unclosed system usage count: %hd", s_wSystemUses);
		s_wSystemUses = 0;
	}
	systemUse();

	// Restore all OS DMA
	g_pCustom->dmacon = DMAF_SETCLR | DMAF_MASTER | s_uwOsInitialDma;

	// restore old view
	WaitTOF();
	LoadView(s_pOsView);
	WaitTOF();

	logWrite("Closing graphics.library...");
	CloseLibrary((struct Library *) GfxBase);
	logWrite("OK\n");
}

void systemUnuse(void) {
	--s_wSystemUses;
	if(!s_wSystemUses) {
		// Disable interrupts (this is the actual "kill system/OS" part)
		g_pCustom->intena = 0x7FFF;
		g_pCustom->intreq = 0x7FFF;

		// Game's bitplanes & copperlists are still used so don't disable them
		// Wait for vbl before disabling sprite DMA
		while (!(g_pCustom->intreqr & INTF_VERTB)) {}
		g_pCustom->dmacon = s_uwOsMinDma;

		// Save OS interrupt vectors and enable ACE's
		g_pCustom->intreq = 0x7FFF;
		for(UWORD i = 0; i < SYSTEM_INT_VECTOR_COUNT; ++i) {
			s_pOsHwInterrupts[i] = s_pHwVectors[SYSTEM_INT_VECTOR_FIRST + i];
			s_pHwVectors[SYSTEM_INT_VECTOR_FIRST + i] = s_pAceHwInterrupts[i];
		}

		// Enable needed DMA (and interrupt) channels
		g_pCustom->dmacon = DMAF_SETCLR | DMAF_MASTER | s_uwAceDmaCon;
		// Everything that's supported by ACE to simplify things for now
		g_pCustom->intena = INTF_SETCLR | INTF_INTEN | (
			INTF_BLIT | INTF_COPER | INTF_VERTB |
			INTF_PORTS
		);
	}
#if defined(ACE_DEBUG)
	if(s_wSystemUses < 0) {
		logWrite("ERR: System uses less than 0!\n");
		s_wSystemUses = 0;
	}
#endif
}

void systemUse(void) {
	if(!s_wSystemUses) {
		// Disable app interrupts/dma, keep display-related DMA
		g_pCustom->intena = 0x7FFF;
		g_pCustom->intreq = 0x7FFF;
		g_pCustom->dmacon = s_uwOsMinDma;
		while (!(g_pCustom->intreqr & INTF_VERTB)) {}

		// Restore interrupt vectors
		for(UWORD i = 0; i < SYSTEM_INT_VECTOR_COUNT; ++i) {
			s_pHwVectors[SYSTEM_INT_VECTOR_FIRST + i] = s_pOsHwInterrupts[i];
		}
		// restore old DMA/INTENA/ADKCON etc. settings
		// All interrupts but only needed DMA
		g_pCustom->dmacon = DMAF_SETCLR | DMAF_MASTER | (s_uwOsDmaCon & s_uwOsMinDma);
		g_pCustom->intena = INTF_SETCLR | INTF_INTEN  | s_uwOsIntEna;
	}
	++s_wSystemUses;
}

void systemSetInt(
	UBYTE ubIntNumber, tAceIntHandler pHandler, volatile void *pIntData
) {
	// Disable ACE handler during data swap to ensure atomic op
	s_pAceInterrupts[ubIntNumber].pHandler = 0;
	s_pAceInterrupts[ubIntNumber].pData = pIntData;

	// Re-enable handler or disable it if 0 was passed
	s_pAceInterrupts[ubIntNumber].pHandler = pHandler;
}

void systemSetDma(UBYTE ubDmaBit, UBYTE isEnabled) {
	if(isEnabled) {
		s_uwAceDmaCon |= BV(ubDmaBit);
		s_uwOsDmaCon |= BV(ubDmaBit);
		if(!s_wSystemUses) {
			g_pCustom->dmacon = DMAF_SETCLR | BV(ubDmaBit);
		}
		else {
			if(!(BV(ubDmaBit) & s_uwOsMinDma)) {
				g_pCustom->dmacon = DMAF_SETCLR | BV(ubDmaBit);
			}
		}
	}
	else {
		s_uwAceDmaCon &= ~BV(ubDmaBit);
		if(!(BV(ubDmaBit) & s_uwOsMinDma)) {
			s_uwOsDmaCon &= ~BV(ubDmaBit);
		}
		if(!s_wSystemUses) {
			g_pCustom->dmacon = BV(ubDmaBit);
		}
		else {
			if(!(BV(ubDmaBit) & s_uwOsMinDma)) {
				g_pCustom->dmacon = BV(ubDmaBit);
			}
		}
	}
}

void systemDump(void) {
	// logBlockBegin("systemDump()");

	logWrite("OS Usage counter: %hd\n", s_wSystemUses);

	// Print handlers
	// logWrite("ACE handlers:\n");
	// for(UWORD i = 0; i < SYSTEM_INT_HANDLER_COUNT; ++i) {
	// 	logWrite(
	// 		"Int %hu: code %p, data %p\n",
	// 		i, s_pAceInterrupts[i].pHandler, s_pAceInterrupts[i].pData
	// 	);
	// }

	// Print vectors
	// logWrite("Vectors:\n");
	// for(UWORD i = 0; i < SYSTEM_INT_VECTOR_COUNT; ++i) {
	// 	logWrite(
	// 		"vec %hu: ACE %p, OS %p, curr %p\n",
	// 		i, s_pAceHwInterrupts[i], s_pOsHwInterrupts[i],
	// 		s_pHwVectors[SYSTEM_INT_VECTOR_FIRST+ i]
	// 	);
	// }

	// logBlockEnd("systemDump()");
}

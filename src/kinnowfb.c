#include <SDL.h>
#include <getopt.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ebus.h"
#include "kinnowfb.h"
#include "kinnowpal.h"

#include "lsic.h"

#include "screen.h"

uint8_t *KinnowFB = 0;

uint32_t FBSize;

uint32_t SlotInfo[64];

uint32_t KinnowRegisters[64];

enum KINNOWREGISTERS {
	REGSIZE   = 0,
	REGVRAM   = 1,
	REGSTATUS = 5,
	REGMODE   = 6,
	REGCAUSE  = 7,
};

uint32_t DirtyRectX1 = -1;
uint32_t DirtyRectX2 = 0;
uint32_t DirtyRectY1 = -1;
uint32_t DirtyRectY2 = 0;

bool IsDirty = false;

static inline void MakeDirty(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2) {
	IsDirty = true;

	if (x1 < DirtyRectX1)
		DirtyRectX1 = x1;

	if (y1 < DirtyRectY1)
		DirtyRectY1 = y1;

	if (x2 > DirtyRectX2) {
		DirtyRectX2 = x2;
	}

	if (y2 > DirtyRectY2) {
		DirtyRectY2 = y2;
	}
}

int KinnowWrite(uint32_t address, void *src, uint32_t length) {
	if ((address >= 0x3000) && (address < 0x3100)) {
		address -= 0x3000;

		KinnowRegisters[address/4] = *(uint32_t*)src;

		return EBUSSUCCESS;
	} else if (address >= 0x100000) {
		address -= 0x100000;

		if (address+length > FBSize)
			return EBUSERROR;

		uint32_t pix = address;
		uint32_t x = pix%KINNOW_FRAMEBUFFER_WIDTH;
		uint32_t y = pix/KINNOW_FRAMEBUFFER_WIDTH;

		uint32_t pix1 = (address+length)-1;
		uint32_t x1 = pix1%KINNOW_FRAMEBUFFER_WIDTH;
		uint32_t y1 = pix1/KINNOW_FRAMEBUFFER_WIDTH;

		MakeDirty(x, y, x1, y1);

		CopyWithLength(&KinnowFB[address], src, length);

		return EBUSSUCCESS;
	}

	return EBUSERROR;
}

int KinnowRead(uint32_t address, void *dest, uint32_t length) {
	if (address < 0x100) { // SlotInfo
		CopyWithLength(dest, &((char*)SlotInfo)[address], length);

		return EBUSSUCCESS;
	} else if ((address >= 0x3000) && (address < 0x3100)) {
		address -= 0x3000;

		*(uint32_t*)dest = KinnowRegisters[address/4];

		return EBUSSUCCESS;
	} else if (address >= 0x100000) {
		address -= 0x100000;

		if (address+length > FBSize)
			return EBUSERROR;

		CopyWithLength(dest, &KinnowFB[address], length);

		return EBUSSUCCESS;
	}

	return EBUSERROR;
}

uint32_t PixelBuffer[KINNOW_FRAMEBUFFER_WIDTH*KINNOW_FRAMEBUFFER_HEIGHT];

void KinnowDraw(struct Screen *screen) {
	LockIoMutex();

	if (!IsDirty) {
		UnlockIoMutex();

		return;
	}

	SDL_Texture *texture = ScreenGetTexture(screen);

	uint32_t dirtyaddr = (DirtyRectY1*KINNOW_FRAMEBUFFER_WIDTH)+DirtyRectX1;

	uint32_t pixbufindex = 0;

	int width = DirtyRectX2-DirtyRectX1+1;
	int height = DirtyRectY2-DirtyRectY1+1;

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			PixelBuffer[pixbufindex++] = KinnowPalette[((uint8_t*)KinnowFB)[dirtyaddr+x]];
		}

		dirtyaddr += KINNOW_FRAMEBUFFER_WIDTH;
	}

	SDL_Rect rect = {
		.x = DirtyRectX1,
		.y = DirtyRectY1,
		.w = width,
		.h = height,
	};

	SDL_UpdateTexture(texture, &rect, PixelBuffer, rect.w * 4);

	DirtyRectX1 = -1;
	DirtyRectX2 = 0;
	DirtyRectY1 = -1;
	DirtyRectY2 = 0;

	IsDirty = false;

	UnlockIoMutex();
}

int KinnowInit() {
	FBSize = KINNOW_FRAMEBUFFER_WIDTH * KINNOW_FRAMEBUFFER_HEIGHT;

	KinnowFB = malloc(FBSize);

	if (KinnowFB == 0)
		return -1;

	memset(KinnowFB, 0, FBSize);

	EBusBranches[24].Present = 1;
	EBusBranches[24].Write = KinnowWrite;
	EBusBranches[24].Read = KinnowRead;
	EBusBranches[24].Reset = 0;

	memset(&SlotInfo, 0, 256);

	SlotInfo[0] = 0x0C007CA1; // ebus magic number
	SlotInfo[1] = 0x4B494E36; // board ID

	strcpy(&((char*)SlotInfo)[8], "kinnowfb,8");

	KinnowRegisters[REGSIZE] = (KINNOW_FRAMEBUFFER_HEIGHT << 12) | KINNOW_FRAMEBUFFER_WIDTH;
	KinnowRegisters[REGVRAM] = FBSize;

	return 0;
}
// Actually now a libctru output

#include <types.h>
#include <timers.h>
#include <stdlib.h>
#include <3ds.h>
#include <string.h>

#define SCREEN_ID GFX_TOP
#define CONSOLE_ID GFX_BOTTOM
static u16 matrix_w, matrix_h;
static u32 *current_fb = NULL;

int init(void) {
	gfxInitDefault();
	consoleInit(CONSOLE_ID, NULL);
	gfxSet3D(false);
	gfxSetScreenFormat(SCREEN_ID, GSP_RGBA8_OES);
	gfxSetDoubleBuffering(SCREEN_ID, true);
	// W/H are deliberately in reverse order.
	current_fb = (u32*) gfxGetFramebuffer(SCREEN_ID, GFX_LEFT, &matrix_h, &matrix_w);
	return 0;
}

int getx(void) {
	return matrix_w;
}
int gety(void) {
	return matrix_h;
}

int set(int x, int y, RGB *color) {
	// Ideally we'd want this to be here, however, since we want decent performance even on the old 3DS,
	// this check just isn't in the budget.
	/*
		if (x < 0 || y < 0)
		return 1;
		if (x >= matrix_w || y >= matrix_h)
		return 2; */
	// W/H are in reverse order and this occurs for 90-degree rotation.
	y = matrix_h - (1 + y);
	current_fb[y + (x * matrix_h)] = (color->red << 24) | (color->green << 16) | (color->blue << 8);
	return 0;
}

int clear(void) {
	memset(current_fb, 0, matrix_w * (size_t) matrix_h * (size_t) 4);
	return 0;
};

int render(void) {
	gspWaitForVBlank();
	gfxFlushBuffers();
	u8 * cfb = gfxGetFramebuffer(SCREEN_ID, GFX_LEFT, NULL, NULL);
	gfxSwapBuffers();
	u8 * nfb = gfxGetFramebuffer(SCREEN_ID, GFX_LEFT, NULL, NULL);
	memcpy(nfb, cfb, matrix_w * (size_t) matrix_h * (size_t) 4);
	current_fb = (u32 *) nfb;
	hidScanInput();
	// Raise the shutdown alarm.
	if ((!aptMainLoop()) || (hidKeysDown() & KEY_START))
		timers_doquit();
	return 0;
}

ulong wait_until(ulong desired_usec) {
	// Hey, we can just delegate work to someone else. Yay!
	return wait_until_core(desired_usec);
}

void wait_until_break(void) {
	wait_until_break_core();
}

int deinit(void) {
	// Can we just.. chill for a moment, please?
	gfxExit();
	return 0;
}

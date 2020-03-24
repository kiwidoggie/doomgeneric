#ifndef DOOM_GENERIC
#define DOOM_GENERIC

#include <stdint.h>

#if defined(__PS4__)
#define DOOMGENERIC_RESX 1280
#define DOOMGENERIC_RESY 720
#else
#define DOOMGENERIC_RESX 640
#define DOOMGENERIC_RESY 400
#endif

extern uint32_t* DG_ScreenBuffer;


void DG_Init();
void DG_DrawFrame();
void DG_SleepMs(uint32_t ms);
uint32_t DG_GetTicksMs();
int DG_GetKey(int* pressed, unsigned char* key);
void DG_SetWindowTitle(const char * title);

#endif //DOOM_GENERIC

#ifdef __PS4__
#include "doomgeneric.h"


#include <orbis/libkernel.h>
#include <orbis/VideoOut.h>
#include <orbis/libc.h>
#include <orbis/UserService.h>
#include <orbis/Pad.h>

#include "doomkeys.h"

// Video memory crap
void* g_VideoMemory = NULL;
void** g_FrameBuffers = NULL;

uintptr_t g_VideoMemSp;
OrbisKernelEqueue g_FlipQueue;
OrbisVideoOutBufferAttribute g_Attribute;
off_t g_DirectMemoryOffset = 0;
size_t g_DirectMemoryAllocationSize = 0;

int32_t g_Width = DOOMGENERIC_RESX;
int32_t g_Height = DOOMGENERIC_RESY;
int32_t g_Depth = 4;

int32_t g_ActiveFrameBufferIndex = 0;
int32_t g_FrameBufferSize = 0;
int32_t g_FrameBufferCount = 0;

int32_t g_VideoHandle = -1;

uint64_t g_CurrentFrame = 0;

typedef struct _Color
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} Color;

// Input bull-ish
int32_t g_UserId = -1;
int32_t g_PadHandle = -1;
uint32_t g_CurrentButtons = 0;
uint32_t g_PreviousButtons = 0;
OrbisPadData g_PadData = { 0 };


// drawPixel draws the given color to the given x-y coordinates in the frame buffer. Returns nothing.
void drawPixel(int x, int y, Color color)
{
    // Get pixel location based on pitch
    int pixel = (y * g_Width) + x;

    // Encode to 24-bit color
    uint32_t encodedColor = 0x80000000 + (color.r << 16) + (color.g << 8) + color.b;

    // Draw to the frame buffer
    ((uint32_t *)g_FrameBuffers[g_ActiveFrameBufferIndex])[pixel] = encodedColor;
}

// drawRectangle draws a rectangle at the given x-y xoordinates with the given width, height, and color to the frame
// buffer. Returns nothing.
void drawRectangle(int x, int y, int width, int height, Color color)
{
    int xPos, yPos;

    // Draw row-by-row, column-by-column
    for (yPos = y; yPos < y + height; yPos++)
    {
        for (xPos = x; xPos < x + width; xPos++)
        {
            drawPixel(xPos, yPos, color);
        }
    }
}

void DG_Init()
{
    // Get the current resolution

    // Initialize sce libs

    // Initialize video out
    int32_t s_RetVal = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, NULL);
    if (s_RetVal < 0)
    {
        printf("err: sceVideoOutOpen returned (%d).\n", s_RetVal);
        return;
    }
    g_VideoHandle = s_RetVal;

    // Create flip queue
    s_RetVal = sceKernelCreateEqueue(&g_FlipQueue, "doom_queue");
    if (s_RetVal < 0)
    {
        printf("err: sceKernelCreateEqueue returned (%d).\n", s_RetVal);
        return;
    }

    // Add the flip event
    s_RetVal = sceVideoOutAddFlipEvent(g_FlipQueue, g_VideoHandle, NULL);
    if (s_RetVal < 0)
    {
        printf("err: sceVideoOutAddFlipEvent returned (%d).\n", s_RetVal);
        return;
    }

    // Allocate video memory
    g_FrameBufferSize = g_Width * g_Height * g_Depth;

    g_DirectMemoryAllocationSize = (0xC000000 + 0x200000 - 1) / 0x200000 * 0x200000;

    s_RetVal = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), g_DirectMemoryAllocationSize, 0x200000, 3, &g_DirectMemoryOffset);
    if (s_RetVal < 0)
    {
        g_DirectMemoryAllocationSize = 0;
        printf("err: sceKernelAllocateDirectMemory returned (%d).\n", s_RetVal);
        return;
    }

    // Map the direct memory
    s_RetVal = sceKernelMapDirectMemory(&g_VideoMemory, g_DirectMemoryAllocationSize, 0x33/*???*/, 0, g_DirectMemoryOffset, 0x200000);
    if (s_RetVal < 0)
    {
        sceKernelReleaseDirectMemory(g_DirectMemoryOffset, g_DirectMemoryAllocationSize);

        g_DirectMemoryOffset = 0;
        g_DirectMemoryAllocationSize = 0;

        printf("err: sceKernelMapDirectMemory returned (%d).\n", s_RetVal);
        return;
    }

    g_VideoMemSp = (uintptr_t)g_VideoMemory;

    printf("info: video memory created successfully!\n");

    // Allocate frame buffers
    g_FrameBuffers = (void**)calloc(2, sizeof(void*));
    if (g_FrameBuffers == NULL)
    {
        // TODO: Free resources

        printf("err: could not allocate framebuffer\n");
        return;
    }

    // Initialze the framebuffers
    for (int32_t l_FbIndex = 0; l_FbIndex < 2; ++l_FbIndex)
    {
        g_FrameBuffers[l_FbIndex] = (void*)g_VideoMemSp;
        g_VideoMemSp += g_FrameBufferSize;
    }

    // Set SRGB pixel format
    sceVideoOutSetBufferAttribute(&g_Attribute, 0x80000000/*???*/, 1, 0, g_Width, g_Height, g_Width);

    g_FrameBufferCount = 2;

    s_RetVal = sceVideoOutRegisterBuffers(g_VideoHandle, 0, g_FrameBuffers, 2, &g_Attribute);
    if (s_RetVal < 0)
    {
        // TODO: Free resources
        printf("err: sceVideoOutRegisterBuffers returned (%d).\n", s_RetVal);
        return;
    }

    // Set the active frame buffer
    g_ActiveFrameBufferIndex = 0;

    // Set the flip rate
    s_RetVal = sceVideoOutSetFlipRate(g_VideoHandle, 0);
    if (s_RetVal < 0)
    {
        // TODO: Free resources
        printf("err: sceVideoOutSetFlipRate returned (%d).\n", s_RetVal);
        return;
    }

    // Clear the screen
    Color s_ClearColor = { 255, 255, 255 };
    drawRectangle(0, 0, g_Width, g_Height, s_ClearColor);

    // Setup input
    s_RetVal = scePadInit();
    if (s_RetVal != 0)
    {
        // TODO: Cleanup resources

        printf("err: scePadInit returned (%d).\n", s_RetVal);
        return;
    }

    // Get the user id information
    OrbisUserServiceInitializeParams s_UserServiceParams = { 0 };
    s_UserServiceParams.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;

    sceUserServiceInitialize(&s_UserServiceParams);
    sceUserServiceGetInitialUser(&g_UserId);

    s_RetVal = scePadOpen(g_UserId, 0, 0, NULL);
    if (s_RetVal < 0)
    {
        // TODO: Free resources

        printf("err: scePadOpen returned (%d).\n", s_RetVal);
        return;
    }

}
void DG_DrawFrame()
{
    // Check to see if our video handle is valid
    if (g_VideoHandle <= 0)
        return;
    
    // Hold our return value
    int s_RetVal = 0;

    // Update the framebuffer
    Color s_Color = { 255, 255, 255 };
    for (int l_Row = 0; l_Row < g_Height; ++l_Row)
    {
        for (int l_Column = 0; l_Column < g_Width; ++l_Column)
        {
            // Get the framebuffer
            unsigned int pixel = DG_ScreenBuffer[l_Row * g_Width + l_Column];
            
            // Set the individual colors, (SRGB, or RGBA?)
            s_Color.r = (pixel & 0xFF000000) >> 3;
            s_Color.g = (pixel & 0x00FF0000) >> 2;
            s_Color.b = (pixel & 0x0000FF00) >> 1;

            drawPixel(l_Row, l_Column, s_Color);
        }
    }

    // Flip the framebuffer
    uint64_t s_FrameId = g_CurrentFrame;

    #ifndef VIDEO_FLIP_MODE_VSYNC
    #define VIDEO_FLIP_MODE_VSYNC 1
    #endif

    sceVideoOutSubmitFlip(g_VideoHandle, g_ActiveFrameBufferIndex, VIDEO_FLIP_MODE_VSYNC, s_FrameId);

    OrbisKernelEvent s_FlipEvent;
    int32_t s_Count = 0;
    
    // Wait for the flip event to happen
    for (;;)
    {
        OrbisVideoOutFlipStatus l_FlipStatus;

        s_RetVal = sceVideoOutGetFlipStatus(g_VideoHandle, &l_FlipStatus);
        if (s_RetVal < 0)
        {
            printf("err: sceVideoOutGetFlipStatus returned (%d).\n", s_RetVal);
            break;
        }

        if (l_FlipStatus.flipArg == s_FrameId)
            break;
        
        // Wait on next flip event
        s_RetVal = sceKernelWaitEqueue(g_FlipQueue, &s_FlipEvent, 1, &s_Count, NULL);
        if (s_RetVal != 0)
        {
            printf("err: sceKernelWaitEqueue returned (%d).\n", s_RetVal);
            break;
        }
    }

    // Swap the framebuffers
    g_ActiveFrameBufferIndex = g_ActiveFrameBufferIndex + 1 % (g_FrameBufferCount - 1);

    // Update the frame count
    g_CurrentFrame++;
}

void DG_SleepMs(uint32_t ms)
{
    // Sleep (in nano-seconds)
    sceKernelUsleep(ms * 1000);
}

uint32_t DG_GetTicksMs()
{
    // Get current tick count
    struct timeval  tp;
    struct timezone tzp;

    int32_t s_RetVal = gettimeofday(&tp, &tzp);
    if (s_RetVal < 0)
    {
        printf("err: gettimeofday returned (%d).\n", s_RetVal);
        return 0;
    }

    return (tp.tv_sec * 1000) + (tp.tv_usec / 1000); /* return milliseconds */
}

int DG_GetKey(int* pressed, unsigned char* key)
{
    // Check that we have already opened our pad
    if (g_PadHandle < 0)
        return 0;

    // Get pad state
    int32_t s_RetVal = scePadReadState(g_PadHandle, &g_PadData);
    if (s_RetVal < 0)
    {
        printf("err: scePadReadState returned (%d).\n", s_RetVal);
        return 0;
    }

    g_CurrentButtons = g_PadData.buttons;

    if (g_CurrentButtons != g_PreviousButtons)
        g_PreviousButtons = g_CurrentButtons;
    
    // TODO: determine if this is good enough
    if (g_CurrentButtons & PAD_BUTTON_DPAD_UP)
        return KEY_UPARROW;
    if (g_CurrentButtons & PAD_BUTTON_DPAD_DOWN)
        return KEY_DOWNARROW;
    if (g_CurrentButtons & PAD_BUTTON_DPAD_LEFT)
        return KEY_LEFTARROW;
    if (g_CurrentButtons & PAD_BUTTON_DPAD_RIGHT)
        return KEY_RIGHTARROW;
    if (g_CurrentButtons & PAD_BUTTON_X)
        return KEY_FIRE;
    if (g_CurrentButtons & PAD_BUTTON_SQUARE)
        return KEY_USE;
    if (g_CurrentButtons & PAD_BUTTON_START)
        return KEY_ESCAPE;

    // TODO: Joystick input, lx, ly, rx, ry
}

void DG_SetWindowTitle(const char * title)
{
    // Unimplemented on PS4
    (void)title;
}

#endif
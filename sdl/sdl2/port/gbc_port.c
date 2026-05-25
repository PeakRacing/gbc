/*
 * Copyright PeakRacing
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <SDL.h>
#include <stdarg.h>
#include <stdlib.h>

#include "gbc.h"

static SDL_Window *sdl_window;
static SDL_Renderer *sdl_renderer;
static SDL_Texture *sdl_texture;
static uint64_t sdl_next_frame_tick;
#if (GBC_ENABLE_SOUND == 1)
static SDL_AudioDeviceID sdl_audio_device;
#endif

void *gbc_malloc(int num)
{
    return malloc((size_t)num);
}

void gbc_free(void *address)
{
    free(address);
}

void *gbc_memcpy(void *str1, const void *str2, size_t n)
{
    return memcpy(str1, str2, n);
}

void *gbc_memset(void *str, int c, size_t n)
{
    return memset(str, c, n);
}

int gbc_memcmp(const void *str1, const void *str2, size_t n)
{
    return memcmp(str1, str2, n);
}

#if (GBC_USE_FS == 1)
FILE *gbc_fopen(const char *filename, const char *mode)
{
    return fopen(filename, mode);
}

size_t gbc_fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    return fread(ptr, size, nmemb, stream);
}

size_t gbc_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    return fwrite(ptr, size, nmemb, stream);
}

int gbc_fseek(FILE *stream, long int offset, int whence)
{
    return fseek(stream, offset, whence);
}

long gbc_ftell(FILE *stream)
{
    return ftell(stream);
}

int gbc_fclose(FILE *stream)
{
    return fclose(stream);
}
#endif

int gbc_log_printf(const char *format, ...)
{
    int ret;
    va_list args;

    va_start(args, format);
    ret = vprintf(format, args);
    va_end(args);
    return ret;
}

uint32_t gbc_get_ticks(void)
{
    return SDL_GetTicks();
}

void gbc_delay(uint32_t ms)
{
    SDL_Delay(ms);
}

static void sdl_set_key(gbc_t *gbc, SDL_Scancode scancode, uint8_t pressed)
{
    switch (scancode) {
    case SDL_SCANCODE_D:
    case SDL_SCANCODE_RIGHT: gbc_joypad_set(gbc, GBC_KEY_RIGHT, pressed); break;
    case SDL_SCANCODE_A:
    case SDL_SCANCODE_LEFT: gbc_joypad_set(gbc, GBC_KEY_LEFT, pressed); break;
    case SDL_SCANCODE_W:
    case SDL_SCANCODE_UP: gbc_joypad_set(gbc, GBC_KEY_UP, pressed); break;
    case SDL_SCANCODE_S:
    case SDL_SCANCODE_DOWN: gbc_joypad_set(gbc, GBC_KEY_DOWN, pressed); break;
    case SDL_SCANCODE_J: gbc_joypad_set(gbc, GBC_KEY_A, pressed); break;
    case SDL_SCANCODE_K: gbc_joypad_set(gbc, GBC_KEY_B, pressed); break;
    case SDL_SCANCODE_V: gbc_joypad_set(gbc, GBC_KEY_SELECT, pressed); break;
    case SDL_SCANCODE_B: gbc_joypad_set(gbc, GBC_KEY_START, pressed); break;
    default: break;
    }
}

int gbc_initex(gbc_t *gbc)
{
    (void)gbc;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        GBC_LOG_ERROR("SDL_Init failed: %s\n", SDL_GetError());
        return GBC_ERROR;
    }
    sdl_window = SDL_CreateWindow("gbc", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  GBC_WIDTH * 4, GBC_HEIGHT * 4, SDL_WINDOW_SHOWN);
    if (sdl_window == NULL) {
        GBC_LOG_ERROR("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return GBC_ERROR;
    }
    sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_ACCELERATED);
    if (sdl_renderer == NULL) {
        sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (sdl_renderer == NULL) {
        GBC_LOG_ERROR("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(sdl_window);
        SDL_Quit();
        return GBC_ERROR;
    }
    sdl_texture = SDL_CreateTexture(sdl_renderer,
#if (GBC_COLOR_DEPTH == 16)
                                    SDL_PIXELFORMAT_RGB565,
#else
                                    SDL_PIXELFORMAT_ARGB8888,
#endif
                                    SDL_TEXTUREACCESS_STREAMING, GBC_WIDTH, GBC_HEIGHT);
    if (sdl_texture == NULL) {
        GBC_LOG_ERROR("SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(sdl_renderer);
        SDL_DestroyWindow(sdl_window);
        SDL_Quit();
        return GBC_ERROR;
    }
    sdl_next_frame_tick = 0;
#if (GBC_ENABLE_SOUND == 1)
    {
        SDL_AudioSpec desired;

        SDL_zero(desired);
        desired.freq = GBC_APU_SAMPLE_RATE;
        desired.format = AUDIO_S16LSB;
        desired.channels = 1;
        desired.samples = GBC_APU_BUFFER_SAMPLES;
        desired.callback = NULL;
        sdl_audio_device = SDL_OpenAudioDevice(NULL, SDL_FALSE, &desired, NULL, 0);
        if (sdl_audio_device == 0) {
            GBC_LOG_ERROR("SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
            SDL_DestroyTexture(sdl_texture);
            sdl_texture = NULL;
            SDL_DestroyRenderer(sdl_renderer);
            sdl_renderer = NULL;
            SDL_DestroyWindow(sdl_window);
            sdl_window = NULL;
            SDL_Quit();
            return GBC_ERROR;
        }
        SDL_PauseAudioDevice(sdl_audio_device, SDL_FALSE);
    }
#endif
    return GBC_OK;
}

int gbc_deinitex(gbc_t *gbc)
{
    (void)gbc;
#if (GBC_ENABLE_SOUND == 1)
    if (sdl_audio_device != 0) {
        SDL_CloseAudioDevice(sdl_audio_device);
        sdl_audio_device = 0;
    }
#endif
    if (sdl_texture != NULL) {
        SDL_DestroyTexture(sdl_texture);
        sdl_texture = NULL;
    }
    if (sdl_renderer != NULL) {
        SDL_DestroyRenderer(sdl_renderer);
        sdl_renderer = NULL;
    }
    if (sdl_window != NULL) {
        SDL_DestroyWindow(sdl_window);
        sdl_window = NULL;
    }
    SDL_Quit();
    return GBC_OK;
}

int gbc_draw(int x1, int y1, int x2, int y2, gbc_color_t *color_data)
{
    SDL_Rect rect;

    if (sdl_texture == NULL || color_data == NULL) {
        return GBC_ERROR;
    }
    rect.x = x1;
    rect.y = y1;
    rect.w = x2 - x1 + 1;
    rect.h = y2 - y1 + 1;
    if (rect.x < 0 || rect.y < 0 || rect.w <= 0 || rect.h <= 0 || x2 >= GBC_WIDTH || y2 >= GBC_HEIGHT) {
        return GBC_ERROR;
    }
    SDL_UpdateTexture(sdl_texture, &rect, color_data, GBC_WIDTH * (int)sizeof(gbc_color_t));
    return GBC_OK;
}

int gbc_sound_output(uint8_t *buffer, size_t len)
{
#if (GBC_ENABLE_SOUND == 1)
    uint32_t max_queue = GBC_APU_BUFFER_SAMPLES * GBC_APU_SAMPLE_BYTES * 8;

    if (sdl_audio_device == 0 || buffer == NULL || len == 0) {
        return GBC_ERROR;
    }
    if (SDL_GetQueuedAudioSize(sdl_audio_device) > max_queue) {
        SDL_ClearQueuedAudio(sdl_audio_device);
    }
    if (SDL_QueueAudio(sdl_audio_device, buffer, (uint32_t)len) < 0) {
        GBC_LOG_WARN("SDL_QueueAudio failed: %s\n", SDL_GetError());
        return GBC_ERROR;
    }
#else
    (void)buffer;
    (void)len;
#endif
    return GBC_OK;
}

void gbc_frame(gbc_t *gbc)
{
    SDL_Event event;
    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t frame_ticks = freq / 60;
    uint64_t now;

    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            gbc->gbc_quit = 1;
        } else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
            if (event.key.keysym.sym == SDLK_ESCAPE && event.type == SDL_KEYDOWN) {
                gbc->gbc_quit = 1;
            }
            sdl_set_key(gbc, event.key.keysym.scancode, event.type == SDL_KEYDOWN);
        }
    }

    SDL_RenderClear(sdl_renderer);
    SDL_RenderCopy(sdl_renderer, sdl_texture, NULL, NULL);
    SDL_RenderPresent(sdl_renderer);

    if (sdl_next_frame_tick == 0) {
        sdl_next_frame_tick = SDL_GetPerformanceCounter();
    }
    sdl_next_frame_tick += frame_ticks;

    now = SDL_GetPerformanceCounter();
    if (now < sdl_next_frame_tick) {
        uint32_t delay_ms = (uint32_t)((sdl_next_frame_tick - now) * 1000 / freq);
        if (delay_ms > 0) {
            SDL_Delay(delay_ms);
        }
    } else if ((now - sdl_next_frame_tick) > (frame_ticks * 2)) {
        sdl_next_frame_tick = now;
    }
}


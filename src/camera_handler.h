#pragma once

#include "esp_err.h"
#include "esp_camera.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define CAMERA_WIDTH 320
#define CAMERA_HEIGHT 240
#define CAMERA_PIXFORMAT 2  // Grayscale format

// Simple frame buffer structure
typedef struct {
    uint8_t *buf;
    size_t len;
    size_t width;
    size_t height;
    int format;
} camera_fb_t;

esp_err_t camera_init(void);
esp_err_t camera_capture_frame(camera_fb_t **fb);
void camera_return_frame(camera_fb_t *fb);

// Camera configuration functions
esp_err_t camera_set_pixel_format(pixformat_t format);
esp_err_t camera_set_frame_size(framesize_t size);
bool camera_is_real_camera_active(void);
#pragma once

#include "esp_err.h"
#include "esp_camera.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define CAMERA_WIDTH 320
#define CAMERA_HEIGHT 240
#define CAMERA_PIXFORMAT_MONO8 2  // Grayscale format
#define CAMERA_PIXFORMAT_JPEG 7   // JPEG format
#define CAMERA_PIXFORMAT_RGB565 3 // RGB565 format
#define CAMERA_PIXFORMAT_YUV422 4 // YUV422 format
#define CAMERA_PIXFORMAT_RGB888 5 // RGB888 format

// Current pixel format (runtime configurable)
extern int current_camera_pixformat;

// Simple frame buffer structure
typedef struct
{
    uint8_t *buf;
    size_t len;
    size_t width;
    size_t height;
    int format;
} local_camera_fb_t;

esp_err_t camera_init(void);
esp_err_t camera_capture_frame(local_camera_fb_t **fb);
void camera_return_frame(local_camera_fb_t *fb);

// Camera configuration functions
esp_err_t camera_set_pixel_format(pixformat_t format);
esp_err_t camera_set_frame_size(framesize_t size);
bool camera_is_real_camera_active(void);

// Format control
esp_err_t camera_set_genicam_pixformat(int genicam_format);
int camera_get_genicam_pixformat(void);
size_t camera_get_max_payload_size(void);

// JPEG quality control
esp_err_t camera_set_jpeg_quality(int quality);
int camera_get_jpeg_quality(void);

// Camera sensor controls
esp_err_t camera_set_exposure_time(uint32_t exposure_us);
uint32_t camera_get_exposure_time(void);
esp_err_t camera_set_gain(int gain);
int camera_get_gain(void);
esp_err_t camera_set_brightness(int brightness);
int camera_get_brightness(void);
esp_err_t camera_set_contrast(int contrast);
int camera_get_contrast(void);
esp_err_t camera_set_saturation(int saturation);
int camera_get_saturation(void);
esp_err_t camera_set_white_balance_mode(int mode);
int camera_get_white_balance_mode(void);
esp_err_t camera_set_trigger_mode(int mode);
int camera_get_trigger_mode(void);

// Trigger modes
#define TRIGGER_MODE_OFF 0
#define TRIGGER_MODE_ON 1
#define TRIGGER_MODE_SOFTWARE 2

// White balance modes
#define WB_MODE_OFF 0
#define WB_MODE_AUTO 1

// NVS storage functions
esp_err_t camera_settings_save_to_nvs(void);
esp_err_t camera_settings_load_from_nvs(void);
esp_err_t camera_settings_reset_to_defaults(void);
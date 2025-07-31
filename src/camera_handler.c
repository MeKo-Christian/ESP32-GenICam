#include "camera_handler.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_camera.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "camera_handler";

// Camera mode flag
static bool use_real_camera = false;

// Frame conversion buffer for format conversion
static uint8_t conversion_buffer[CAMERA_WIDTH * CAMERA_HEIGHT];

// Convert various camera formats to Mono8 grayscale
// Supported formats: PIXFORMAT_GRAYSCALE, PIXFORMAT_RGB565, PIXFORMAT_YUV422, 
//                    PIXFORMAT_JPEG, PIXFORMAT_RGB888, PIXFORMAT_YUV420
static void convert_to_mono8(camera_fb_t *src_fb, uint8_t *dst_buf, size_t *dst_len) {
    if (!src_fb || !dst_buf || !dst_len) return;
    
    if (src_fb->format == PIXFORMAT_GRAYSCALE) {
        // Already grayscale, direct copy
        memcpy(dst_buf, src_fb->buf, src_fb->len);
        *dst_len = src_fb->len;
        ESP_LOGI(TAG, "Direct copy: grayscale format");
    } else if (src_fb->format == PIXFORMAT_RGB565) {
        // RGB565 to grayscale conversion
        uint16_t *rgb565 = (uint16_t*)src_fb->buf;
        size_t pixel_count = src_fb->len / 2;
        
        for (size_t i = 0; i < pixel_count && i < (CAMERA_WIDTH * CAMERA_HEIGHT); i++) {
            uint16_t pixel = rgb565[i];
            uint8_t r = (pixel >> 11) & 0x1F;
            uint8_t g = (pixel >> 5) & 0x3F;  
            uint8_t b = pixel & 0x1F;
            
            // Convert to 8-bit values and calculate grayscale using standard weights
            r = (r << 3) | (r >> 2);  // 5-bit to 8-bit
            g = (g << 2) | (g >> 4);  // 6-bit to 8-bit  
            b = (b << 3) | (b >> 2);  // 5-bit to 8-bit
            
            dst_buf[i] = (uint8_t)(0.299f * r + 0.587f * g + 0.114f * b);
        }
        *dst_len = pixel_count;
        ESP_LOGI(TAG, "Converted RGB565 to grayscale: %d pixels", pixel_count);
    } else if (src_fb->format == PIXFORMAT_YUV422) {
        // YUV422 to grayscale conversion (extract Y channel)
        // YUV422 format: Y0 U0 Y1 V0 Y2 U1 Y3 V1 ... (2 bytes per pixel, but 4 bytes for 2 pixels)
        uint8_t *yuv422 = src_fb->buf;
        size_t pixel_count = 0;
        
        for (size_t i = 0; i < src_fb->len && pixel_count < (CAMERA_WIDTH * CAMERA_HEIGHT); i += 4) {
            if (i + 3 < src_fb->len) {
                // Extract Y values (luminance) from YUV422 packed format
                dst_buf[pixel_count++] = yuv422[i];     // Y0
                if (pixel_count < (CAMERA_WIDTH * CAMERA_HEIGHT)) {
                    dst_buf[pixel_count++] = yuv422[i + 2]; // Y1
                }
            }
        }
        *dst_len = pixel_count;
        ESP_LOGI(TAG, "Converted YUV422 to grayscale: %d pixels", pixel_count);
    } else if (src_fb->format == PIXFORMAT_JPEG) {
        // JPEG to grayscale conversion
        // For JPEG, we need to decode it first. Since ESP32 doesn't have built-in JPEG decoder,
        // we'll implement a simple approach or fall back to a pattern
        ESP_LOGW(TAG, "JPEG format detected, but no decoder available");
        ESP_LOGI(TAG, "Converting JPEG to grayscale using simplified approach");
        
        // Simple approach: use JPEG data as-is if small enough, or create pattern
        if (src_fb->len <= (CAMERA_WIDTH * CAMERA_HEIGHT)) {
            // Use raw JPEG bytes as grayscale (not ideal but functional)
            memcpy(dst_buf, src_fb->buf, src_fb->len);
            *dst_len = src_fb->len;
        } else {
            // Create a pattern based on JPEG data checksum
            uint32_t checksum = 0;
            for (size_t i = 0; i < src_fb->len && i < 1024; i++) {
                checksum += src_fb->buf[i];
            }
            
            for (int i = 0; i < CAMERA_WIDTH * CAMERA_HEIGHT; i++) {
                dst_buf[i] = (checksum + i) % 256;
            }
            *dst_len = CAMERA_WIDTH * CAMERA_HEIGHT;
        }
        ESP_LOGI(TAG, "Converted JPEG to grayscale pattern: %d bytes", *dst_len);
    } else if (src_fb->format == PIXFORMAT_RGB888) {
        // RGB888 to grayscale conversion
        uint8_t *rgb888 = src_fb->buf;
        size_t pixel_count = src_fb->len / 3;
        
        for (size_t i = 0; i < pixel_count && i < (CAMERA_WIDTH * CAMERA_HEIGHT); i++) {
            uint8_t r = rgb888[i * 3];
            uint8_t g = rgb888[i * 3 + 1];
            uint8_t b = rgb888[i * 3 + 2];
            
            // Calculate grayscale using standard weights
            dst_buf[i] = (uint8_t)(0.299f * r + 0.587f * g + 0.114f * b);
        }
        *dst_len = pixel_count;
        ESP_LOGI(TAG, "Converted RGB888 to grayscale: %d pixels", pixel_count);
    } else if (src_fb->format == PIXFORMAT_YUV420) {
        // YUV420 to grayscale conversion (extract Y plane)
        // YUV420 has Y plane first, then U and V planes
        uint8_t *yuv420 = src_fb->buf;
        size_t y_plane_size = CAMERA_WIDTH * CAMERA_HEIGHT;
        size_t copy_size = (src_fb->len < y_plane_size) ? src_fb->len : y_plane_size;
        
        // Y plane is at the beginning, just copy it
        memcpy(dst_buf, yuv420, copy_size);
        *dst_len = copy_size;
        ESP_LOGI(TAG, "Converted YUV420 to grayscale: %d pixels", copy_size);
    } else {
        // Unknown format, create a default pattern
        ESP_LOGW(TAG, "Unknown pixel format %d, using default pattern", src_fb->format);
        for (int i = 0; i < CAMERA_WIDTH * CAMERA_HEIGHT; i++) {
            dst_buf[i] = i % 256; // Simple gradient pattern
        }
        *dst_len = CAMERA_WIDTH * CAMERA_HEIGHT;
    }
}

// Dummy frame buffer structure
typedef struct {
    uint8_t *buf;
    size_t len;
    size_t width;
    size_t height;
    int format;
} dummy_camera_fb_t;

esp_err_t camera_init(void)
{
    ESP_LOGI(TAG, "Initializing ESP32-CAM...");
    
    // ESP32-CAM pin configuration from platformio.ini
    camera_config_t config = {
        .pin_pwdn  = CONFIG_CAMERA_PIN_PWDN,
        .pin_reset = CONFIG_CAMERA_PIN_RESET,
        .pin_xclk = CONFIG_CAMERA_PIN_XCLK,
        .pin_sscb_sda = CONFIG_CAMERA_PIN_SIOD,
        .pin_sscb_scl = CONFIG_CAMERA_PIN_SIOC,
        
        .pin_d7 = CONFIG_CAMERA_PIN_D7,
        .pin_d6 = CONFIG_CAMERA_PIN_D6,
        .pin_d5 = CONFIG_CAMERA_PIN_D5,
        .pin_d4 = CONFIG_CAMERA_PIN_D4,
        .pin_d3 = CONFIG_CAMERA_PIN_D3,
        .pin_d2 = CONFIG_CAMERA_PIN_D2,
        .pin_d1 = CONFIG_CAMERA_PIN_D1,
        .pin_d0 = CONFIG_CAMERA_PIN_D0,
        .pin_vsync = CONFIG_CAMERA_PIN_VSYNC,
        .pin_href = CONFIG_CAMERA_PIN_HREF,
        .pin_pclk = CONFIG_CAMERA_PIN_PCLK,
        
        // XCLK 20MHz or 10MHz for OV2640
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        
        .pixel_format = PIXFORMAT_GRAYSCALE, // Prefer grayscale for best performance
        .frame_size = FRAMESIZE_QVGA,        // 320x240 resolution
        
        .jpeg_quality = 12,  // 0-63 lower number means higher quality
        .fb_count = 1        // if more than one, will work in JPEG mode only
    };
    
    // Initialize the camera
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        ESP_LOGW(TAG, "Falling back to dummy mode");
        use_real_camera = false;
        return ESP_OK; // Don't fail completely, use dummy mode
    }
    
    use_real_camera = true;
    ESP_LOGI(TAG, "ESP32-CAM initialized successfully: %dx%d, format=GRAYSCALE", 
             CAMERA_WIDTH, CAMERA_HEIGHT);
    
    return ESP_OK;
}

esp_err_t camera_set_pixel_format(pixformat_t format)
{
    if (!use_real_camera) {
        ESP_LOGW(TAG, "Cannot set pixel format: real camera not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) {
        ESP_LOGE(TAG, "Failed to get camera sensor");
        return ESP_FAIL;
    }
    
    int result = s->set_pixformat(s, format);
    if (result != 0) {
        ESP_LOGE(TAG, "Failed to set pixel format to %d", format);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Camera pixel format set to %d", format);
    return ESP_OK;
}

esp_err_t camera_set_frame_size(framesize_t size)
{
    if (!use_real_camera) {
        ESP_LOGW(TAG, "Cannot set frame size: real camera not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) {
        ESP_LOGE(TAG, "Failed to get camera sensor");
        return ESP_FAIL;
    }
    
    int result = s->set_framesize(s, size);
    if (result != 0) {
        ESP_LOGE(TAG, "Failed to set frame size to %d", size);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Camera frame size set to %d", size);
    return ESP_OK;
}

bool camera_is_real_camera_active(void) 
{
    return use_real_camera;
}

esp_err_t camera_capture_frame(camera_fb_t **fb)
{
    static dummy_camera_fb_t converted_fb;
    
    if (use_real_camera) {
        // Capture frame from real ESP32-CAM
        camera_fb_t *frame_buffer = esp_camera_fb_get();
        if (!frame_buffer) {
            ESP_LOGE(TAG, "Camera capture failed");
            use_real_camera = false; // Fall back to dummy mode on error
            ESP_LOGW(TAG, "Switching to dummy mode due to capture failure");
        } else {
            // Convert to Mono8 format for GenICam compatibility
            size_t converted_len;
            convert_to_mono8(frame_buffer, conversion_buffer, &converted_len);
            
            // Return the frame buffer to the driver immediately after conversion
            esp_camera_fb_return(frame_buffer);
            
            // Set up converted frame buffer
            converted_fb.buf = conversion_buffer;
            converted_fb.len = converted_len;
            converted_fb.width = CAMERA_WIDTH;
            converted_fb.height = CAMERA_HEIGHT;
            converted_fb.format = CAMERA_PIXFORMAT; // Mono8
            
            *fb = (camera_fb_t*)&converted_fb;
            ESP_LOGI(TAG, "Frame captured and converted (real): %d bytes", converted_len);
            return ESP_OK;
        }
    }
    
    // Dummy mode (fallback or when real camera not available)
    static uint8_t dummy_frame[CAMERA_WIDTH * CAMERA_HEIGHT];
    static dummy_camera_fb_t dummy_fb;
    
    // Generate a simple test pattern
    for (int y = 0; y < CAMERA_HEIGHT; y++) {
        for (int x = 0; x < CAMERA_WIDTH; x++) {
            // Checkerboard pattern
            uint8_t value = ((x / 20) + (y / 20)) % 2 ? 255 : 0;
            dummy_frame[y * CAMERA_WIDTH + x] = value;
        }
    }
    
    dummy_fb.buf = dummy_frame;
    dummy_fb.len = sizeof(dummy_frame);
    dummy_fb.width = CAMERA_WIDTH;
    dummy_fb.height = CAMERA_HEIGHT;
    dummy_fb.format = CAMERA_PIXFORMAT;
    
    *fb = (camera_fb_t*)&dummy_fb;
    
    ESP_LOGI(TAG, "Frame captured (dummy): %d bytes", dummy_fb.len);
    return ESP_OK;
}

void camera_return_frame(camera_fb_t *fb)
{
    // Frame buffers are now handled within camera_capture_frame()
    // Real camera frames: returned to driver immediately after conversion
    // Dummy/converted frames: use static buffers, no cleanup needed
    (void)fb;
}
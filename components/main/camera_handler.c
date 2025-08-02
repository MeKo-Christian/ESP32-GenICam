#include "camera_handler.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_camera.h"
#include "jpeg_decoder.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/ledc.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "camera_handler";

// Camera mode flag
static bool use_real_camera = false;

// Current pixel format (default to Mono8)
int current_camera_pixformat = CAMERA_PIXFORMAT_MONO8;

// JPEG quality (0-63, lower is higher quality, default 12)
static int jpeg_quality = 12;

// Camera control parameters with defaults
static uint32_t exposure_time_us = 10000;     // 10ms default
static int gain_value = 0;                    // 0 dB default
static int brightness_value = 0;              // 0 default (-2 to +2)
static int contrast_value = 0;                // 0 default (-2 to +2)
static int saturation_value = 0;              // 0 default (-2 to +2)
static int white_balance_mode = WB_MODE_AUTO; // Auto white balance default
static int trigger_mode = TRIGGER_MODE_OFF;   // Free running default

// Frame conversion buffer for format conversion (Mono8) - allocated in PSRAM
static uint8_t *conversion_buffer = NULL;

// JPEG frame buffer for direct JPEG streaming - allocated in PSRAM
static uint8_t *jpeg_buffer = NULL;

// Shared multi-format buffer - allocated in PSRAM and reused for different formats
static uint8_t *format_buffer = NULL;
static size_t format_buffer_size = CAMERA_WIDTH * CAMERA_HEIGHT * 3; // Max size (RGB888)

// RGB buffer for JPEG decoding (3 bytes per pixel)
static uint8_t *rgb_decode_buffer = NULL;

// Convert various camera formats to Mono8 grayscale
// Supported formats: PIXFORMAT_GRAYSCALE, PIXFORMAT_RGB565, PIXFORMAT_YUV422,
//                    PIXFORMAT_JPEG, PIXFORMAT_RGB888, PIXFORMAT_YUV420
static void convert_to_mono8(camera_fb_t *src_fb, uint8_t *dst_buf, size_t *dst_len)
{
    if (!src_fb || !dst_buf || !dst_len)
        return;

    if (src_fb->format == PIXFORMAT_GRAYSCALE)
    {
        // Already grayscale, direct copy
        memcpy(dst_buf, src_fb->buf, src_fb->len);
        *dst_len = src_fb->len;
        ESP_LOGI(TAG, "Direct copy: grayscale format");
    }
    else if (src_fb->format == PIXFORMAT_RGB565)
    {
        // RGB565 to grayscale conversion
        uint16_t *rgb565 = (uint16_t *)src_fb->buf;
        size_t pixel_count = src_fb->len / 2;

        for (size_t i = 0; i < pixel_count && i < (CAMERA_WIDTH * CAMERA_HEIGHT); i++)
        {
            uint16_t pixel = rgb565[i];
            uint8_t r = (pixel >> 11) & 0x1F;
            uint8_t g = (pixel >> 5) & 0x3F;
            uint8_t b = pixel & 0x1F;

            // Convert to 8-bit values and calculate grayscale using standard weights
            r = (r << 3) | (r >> 2); // 5-bit to 8-bit
            g = (g << 2) | (g >> 4); // 6-bit to 8-bit
            b = (b << 3) | (b >> 2); // 5-bit to 8-bit

            dst_buf[i] = (uint8_t)(0.299f * r + 0.587f * g + 0.114f * b);
        }
        *dst_len = pixel_count;
        ESP_LOGI(TAG, "Converted RGB565 to grayscale: %d pixels", pixel_count);
    }
    else if (src_fb->format == PIXFORMAT_YUV422)
    {
        // YUV422 to grayscale conversion (extract Y channel)
        // YUV422 format: Y0 U0 Y1 V0 Y2 U1 Y3 V1 ... (2 bytes per pixel, but 4 bytes for 2 pixels)
        uint8_t *yuv422 = src_fb->buf;
        size_t pixel_count = 0;

        for (size_t i = 0; i < src_fb->len && pixel_count < (CAMERA_WIDTH * CAMERA_HEIGHT); i += 4)
        {
            if (i + 3 < src_fb->len)
            {
                // Extract Y values (luminance) from YUV422 packed format
                dst_buf[pixel_count++] = yuv422[i]; // Y0
                if (pixel_count < (CAMERA_WIDTH * CAMERA_HEIGHT))
                {
                    dst_buf[pixel_count++] = yuv422[i + 2]; // Y1
                }
            }
        }
        *dst_len = pixel_count;
        ESP_LOGI(TAG, "Converted YUV422 to grayscale: %d pixels", pixel_count);
    }
    else if (src_fb->format == PIXFORMAT_JPEG)
    {
        // JPEG to grayscale conversion using new esp_jpeg_decode API
        ESP_LOGI(TAG, "Converting JPEG to grayscale using new decoder");

        // Allocate RGB buffer if not already done
        if (!rgb_decode_buffer)
        {
            rgb_decode_buffer = malloc(CAMERA_WIDTH * CAMERA_HEIGHT * 3);
            if (!rgb_decode_buffer)
            {
                ESP_LOGE(TAG, "Failed to allocate RGB decode buffer");
                // Fallback to pattern
                for (int i = 0; i < CAMERA_WIDTH * CAMERA_HEIGHT; i++)
                {
                    dst_buf[i] = i % 256;
                }
                *dst_len = CAMERA_WIDTH * CAMERA_HEIGHT;
                return;
            }
        }

        // Clear RGB buffer
        memset(rgb_decode_buffer, 0, CAMERA_WIDTH * CAMERA_HEIGHT * 3);

        // Set up JPEG decoder configuration
        esp_jpeg_image_cfg_t jpeg_cfg = {
            .indata = src_fb->buf,
            .indata_size = src_fb->len,
            .outbuf = rgb_decode_buffer,
            .outbuf_size = CAMERA_WIDTH * CAMERA_HEIGHT * 3,
            .out_format = JPEG_IMAGE_FORMAT_RGB888,
            .out_scale = JPEG_IMAGE_SCALE_0,
            .flags = {0},
            .advanced = {
                .working_buffer = NULL,
                .working_buffer_size = 0}};

        esp_jpeg_image_output_t img_info;

        // Decode JPEG to RGB
        esp_err_t ret = esp_jpeg_decode(&jpeg_cfg, &img_info);
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "JPEG decoded successfully: %dx%d", img_info.width, img_info.height);

            // Convert RGB to grayscale using the actual decoded size
            size_t pixels = img_info.width * img_info.height;
            if (pixels > CAMERA_WIDTH * CAMERA_HEIGHT)
            {
                pixels = CAMERA_WIDTH * CAMERA_HEIGHT;
            }

            for (size_t i = 0; i < pixels; i++)
            {
                uint8_t r = rgb_decode_buffer[i * 3];
                uint8_t g = rgb_decode_buffer[i * 3 + 1];
                uint8_t b = rgb_decode_buffer[i * 3 + 2];
                dst_buf[i] = (uint8_t)(0.299f * r + 0.587f * g + 0.114f * b);
            }
            *dst_len = pixels;
            ESP_LOGI(TAG, "Successfully decoded JPEG to grayscale: %d pixels", *dst_len);
        }
        else
        {
            ESP_LOGW(TAG, "JPEG decode failed (ret=%d), using fallback pattern", ret);
            // Fallback to pattern based on JPEG data
            uint32_t checksum = 0;
            for (size_t i = 0; i < src_fb->len && i < 1024; i++)
            {
                checksum += src_fb->buf[i];
            }

            for (int i = 0; i < CAMERA_WIDTH * CAMERA_HEIGHT; i++)
            {
                dst_buf[i] = (checksum + i) % 256;
            }
            *dst_len = CAMERA_WIDTH * CAMERA_HEIGHT;
        }
    }
    else if (src_fb->format == PIXFORMAT_RGB888)
    {
        // RGB888 to grayscale conversion
        uint8_t *rgb888 = src_fb->buf;
        size_t pixel_count = src_fb->len / 3;

        for (size_t i = 0; i < pixel_count && i < (CAMERA_WIDTH * CAMERA_HEIGHT); i++)
        {
            uint8_t r = rgb888[i * 3];
            uint8_t g = rgb888[i * 3 + 1];
            uint8_t b = rgb888[i * 3 + 2];

            // Calculate grayscale using standard weights
            dst_buf[i] = (uint8_t)(0.299f * r + 0.587f * g + 0.114f * b);
        }
        *dst_len = pixel_count;
        ESP_LOGI(TAG, "Converted RGB888 to grayscale: %d pixels", pixel_count);
    }
    else if (src_fb->format == PIXFORMAT_YUV420)
    {
        // YUV420 to grayscale conversion (extract Y plane)
        // YUV420 has Y plane first, then U and V planes
        uint8_t *yuv420 = src_fb->buf;
        size_t y_plane_size = CAMERA_WIDTH * CAMERA_HEIGHT;
        size_t copy_size = (src_fb->len < y_plane_size) ? src_fb->len : y_plane_size;

        // Y plane is at the beginning, just copy it
        memcpy(dst_buf, yuv420, copy_size);
        *dst_len = copy_size;
        ESP_LOGI(TAG, "Converted YUV420 to grayscale: %d pixels", copy_size);
    }
    else
    {
        // Unknown format, create a default pattern
        ESP_LOGW(TAG, "Unknown pixel format %d, using default pattern", src_fb->format);
        for (int i = 0; i < CAMERA_WIDTH * CAMERA_HEIGHT; i++)
        {
            dst_buf[i] = i % 256; // Simple gradient pattern
        }
        *dst_len = CAMERA_WIDTH * CAMERA_HEIGHT;
    }
}

// Dummy frame buffer structure
typedef struct
{
    uint8_t *buf;
    size_t len;
    size_t width;
    size_t height;
    int format;
} dummy_camera_fb_t;

esp_err_t camera_init(void)
{
    ESP_LOGI(TAG, "Initializing ESP32-CAM...");

    // Initialize LEDC peripheral for camera XCLK generation
    ESP_LOGI(TAG, "Initializing LEDC peripheral...");
    esp_err_t ledc_err = ledc_fade_func_install(0);
    if (ledc_err != ESP_OK)
    {
        ESP_LOGE(TAG, "LEDC fade func install failed: %s", esp_err_to_name(ledc_err));
        return ledc_err;
    }

    // ESP32-CAM pin configuration from platformio.ini
    camera_config_t config = {
        .pin_pwdn = CONFIG_CAMERA_PIN_PWDN,
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

        .jpeg_quality = jpeg_quality, // Use current JPEG quality setting
        .fb_count = 1                 // if more than one, will work in JPEG mode only
    };

    // Initialize the camera
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        ESP_LOGW(TAG, "Falling back to dummy mode");
        use_real_camera = false;
        return ESP_OK; // Don't fail completely, use dummy mode
    }

    use_real_camera = true;
    ESP_LOGI(TAG, "ESP32-CAM initialized successfully: %dx%d, format=GRAYSCALE",
             CAMERA_WIDTH, CAMERA_HEIGHT);

    // Allocate camera buffers in PSRAM to save DRAM
    ESP_LOGI(TAG, "Allocating camera buffers in PSRAM...");

    // Conversion buffer for Mono8 format
    conversion_buffer = heap_caps_malloc(CAMERA_WIDTH * CAMERA_HEIGHT, MALLOC_CAP_SPIRAM);
    if (!conversion_buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate conversion buffer in PSRAM");
        return ESP_ERR_NO_MEM;
    }

    // JPEG buffer (32KB)
    jpeg_buffer = heap_caps_malloc(32768, MALLOC_CAP_SPIRAM);
    if (!jpeg_buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate JPEG buffer in PSRAM");
        free(conversion_buffer);
        return ESP_ERR_NO_MEM;
    }

    // Shared format buffer (maximum size for RGB888)
    format_buffer = heap_caps_malloc(format_buffer_size, MALLOC_CAP_SPIRAM);
    if (!format_buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate format buffer in PSRAM");
        free(conversion_buffer);
        free(jpeg_buffer);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Camera buffers allocated successfully: Conv=%dKB, JPEG=32KB, Format=%dKB",
             (CAMERA_WIDTH * CAMERA_HEIGHT) / 1024, format_buffer_size / 1024);

    // Load settings from NVS
    esp_err_t nvs_err = camera_settings_load_from_nvs();
    if (nvs_err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to load settings from NVS, using defaults");
    }

    return ESP_OK;
}

esp_err_t camera_set_pixel_format(pixformat_t format)
{
    if (!use_real_camera)
    {
        ESP_LOGW(TAG, "Cannot set pixel format: real camera not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL)
    {
        ESP_LOGE(TAG, "Failed to get camera sensor");
        return ESP_FAIL;
    }

    int result = s->set_pixformat(s, format);
    if (result != 0)
    {
        ESP_LOGE(TAG, "Failed to set pixel format to %d", format);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Camera pixel format set to %d", format);
    return ESP_OK;
}

esp_err_t camera_set_frame_size(framesize_t size)
{
    if (!use_real_camera)
    {
        ESP_LOGW(TAG, "Cannot set frame size: real camera not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL)
    {
        ESP_LOGE(TAG, "Failed to get camera sensor");
        return ESP_FAIL;
    }

    int result = s->set_framesize(s, size);
    if (result != 0)
    {
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

esp_err_t camera_capture_frame(local_camera_fb_t **fb)
{
    static dummy_camera_fb_t converted_fb;

    if (use_real_camera)
    {
        // Capture frame from real ESP32-CAM
        camera_fb_t *frame_buffer = esp_camera_fb_get();
        if (!frame_buffer)
        {
            ESP_LOGE(TAG, "Camera capture failed");
            use_real_camera = false; // Fall back to dummy mode on error
            ESP_LOGW(TAG, "Switching to dummy mode due to capture failure");
        }
        else
        {
            // Handle direct format streaming when possible
            if (current_camera_pixformat == CAMERA_PIXFORMAT_JPEG && frame_buffer->format == PIXFORMAT_JPEG)
            {
                // Stream JPEG directly without conversion
                size_t copy_len = (frame_buffer->len < sizeof(jpeg_buffer)) ? frame_buffer->len : sizeof(jpeg_buffer);
                memcpy(jpeg_buffer, frame_buffer->buf, copy_len);

                // Return the frame buffer to the driver
                esp_camera_fb_return(frame_buffer);

                // Set up JPEG frame buffer
                converted_fb.buf = jpeg_buffer;
                converted_fb.len = copy_len;
                converted_fb.width = CAMERA_WIDTH;
                converted_fb.height = CAMERA_HEIGHT;
                converted_fb.format = CAMERA_PIXFORMAT_JPEG;

                *fb = (local_camera_fb_t *)&converted_fb;
                ESP_LOGI(TAG, "Frame captured JPEG (real): %d bytes", copy_len);
                return ESP_OK;
            }
            else if (current_camera_pixformat == CAMERA_PIXFORMAT_RGB565 && frame_buffer->format == PIXFORMAT_RGB565)
            {
                // Stream RGB565 directly
                size_t max_rgb565_size = CAMERA_WIDTH * CAMERA_HEIGHT * 2;
                size_t copy_len = (frame_buffer->len < max_rgb565_size) ? frame_buffer->len : max_rgb565_size;
                memcpy(format_buffer, frame_buffer->buf, copy_len);

                esp_camera_fb_return(frame_buffer);

                converted_fb.buf = format_buffer;
                converted_fb.len = copy_len;
                converted_fb.width = CAMERA_WIDTH;
                converted_fb.height = CAMERA_HEIGHT;
                converted_fb.format = CAMERA_PIXFORMAT_RGB565;

                *fb = (local_camera_fb_t *)&converted_fb;
                ESP_LOGI(TAG, "Frame captured RGB565 (real): %d bytes", copy_len);
                return ESP_OK;
            }
            else if (current_camera_pixformat == CAMERA_PIXFORMAT_YUV422 && frame_buffer->format == PIXFORMAT_YUV422)
            {
                // Stream YUV422 directly
                size_t max_yuv422_size = CAMERA_WIDTH * CAMERA_HEIGHT * 2;
                size_t copy_len = (frame_buffer->len < max_yuv422_size) ? frame_buffer->len : max_yuv422_size;
                memcpy(format_buffer, frame_buffer->buf, copy_len);

                esp_camera_fb_return(frame_buffer);

                converted_fb.buf = format_buffer;
                converted_fb.len = copy_len;
                converted_fb.width = CAMERA_WIDTH;
                converted_fb.height = CAMERA_HEIGHT;
                converted_fb.format = CAMERA_PIXFORMAT_YUV422;

                *fb = (local_camera_fb_t *)&converted_fb;
                ESP_LOGI(TAG, "Frame captured YUV422 (real): %d bytes", copy_len);
                return ESP_OK;
            }
            else if (current_camera_pixformat == CAMERA_PIXFORMAT_RGB888 && frame_buffer->format == PIXFORMAT_RGB888)
            {
                // Stream RGB888 directly
                size_t max_rgb888_size = CAMERA_WIDTH * CAMERA_HEIGHT * 3;
                size_t copy_len = (frame_buffer->len < max_rgb888_size) ? frame_buffer->len : max_rgb888_size;
                memcpy(format_buffer, frame_buffer->buf, copy_len);

                esp_camera_fb_return(frame_buffer);

                converted_fb.buf = format_buffer;
                converted_fb.len = copy_len;
                converted_fb.width = CAMERA_WIDTH;
                converted_fb.height = CAMERA_HEIGHT;
                converted_fb.format = CAMERA_PIXFORMAT_RGB888;

                *fb = (local_camera_fb_t *)&converted_fb;
                ESP_LOGI(TAG, "Frame captured RGB888 (real): %d bytes", copy_len);
                return ESP_OK;
            }

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
            converted_fb.format = CAMERA_PIXFORMAT_MONO8;

            *fb = (local_camera_fb_t *)&converted_fb;
            ESP_LOGI(TAG, "Frame captured and converted (real): %d bytes", converted_len);
            return ESP_OK;
        }
    }

    // Dummy mode (fallback or when real camera not available)
    static uint8_t dummy_frame[CAMERA_WIDTH * CAMERA_HEIGHT];
    static dummy_camera_fb_t dummy_fb;

    // Generate a simple test pattern
    for (int y = 0; y < CAMERA_HEIGHT; y++)
    {
        for (int x = 0; x < CAMERA_WIDTH; x++)
        {
            // Checkerboard pattern
            uint8_t value = ((x / 20) + (y / 20)) % 2 ? 255 : 0;
            dummy_frame[y * CAMERA_WIDTH + x] = value;
        }
    }

    dummy_fb.buf = dummy_frame;
    dummy_fb.len = sizeof(dummy_frame);
    dummy_fb.width = CAMERA_WIDTH;
    dummy_fb.height = CAMERA_HEIGHT;
    dummy_fb.format = current_camera_pixformat; // Use current format

    *fb = (local_camera_fb_t *)&dummy_fb;

    ESP_LOGI(TAG, "Frame captured (dummy): %d bytes", dummy_fb.len);
    return ESP_OK;
}

void camera_return_frame(local_camera_fb_t *fb)
{
    // Frame buffers are now handled within camera_capture_frame()
    // Real camera frames: returned to driver immediately after conversion
    // Dummy/converted frames: use static buffers, no cleanup needed
    (void)fb;
}

esp_err_t camera_set_genicam_pixformat(int genicam_format)
{
    if (genicam_format == 0x01080001)
    { // Mono8
        current_camera_pixformat = CAMERA_PIXFORMAT_MONO8;
        if (use_real_camera)
        {
            // Set camera to grayscale mode
            esp_err_t ret = camera_set_pixel_format(PIXFORMAT_GRAYSCALE);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set camera to grayscale mode");
                return ret;
            }
        }
        ESP_LOGI(TAG, "Pixel format set to Mono8");
        return ESP_OK;
    }
    else if (genicam_format == 0x02100005)
    { // RGB565Packed
        current_camera_pixformat = CAMERA_PIXFORMAT_RGB565;
        if (use_real_camera)
        {
            // Set camera to RGB565 mode
            esp_err_t ret = camera_set_pixel_format(PIXFORMAT_RGB565);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set camera to RGB565 mode");
                return ret;
            }
        }
        ESP_LOGI(TAG, "Pixel format set to RGB565");
        return ESP_OK;
    }
    else if (genicam_format == 0x02100004)
    { // YUV422Packed
        current_camera_pixformat = CAMERA_PIXFORMAT_YUV422;
        if (use_real_camera)
        {
            // Set camera to YUV422 mode
            esp_err_t ret = camera_set_pixel_format(PIXFORMAT_YUV422);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set camera to YUV422 mode");
                return ret;
            }
        }
        ESP_LOGI(TAG, "Pixel format set to YUV422");
        return ESP_OK;
    }
    else if (genicam_format == 0x02180014)
    { // RGB8Packed
        current_camera_pixformat = CAMERA_PIXFORMAT_RGB888;
        if (use_real_camera)
        {
            // Set camera to RGB888 mode
            esp_err_t ret = camera_set_pixel_format(PIXFORMAT_RGB888);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set camera to RGB888 mode");
                return ret;
            }
        }
        ESP_LOGI(TAG, "Pixel format set to RGB888");
        return ESP_OK;
    }
    else if (genicam_format == 0x80000001)
    { // JPEG
        current_camera_pixformat = CAMERA_PIXFORMAT_JPEG;
        if (use_real_camera)
        {
            // Set camera to JPEG mode
            esp_err_t ret = camera_set_pixel_format(PIXFORMAT_JPEG);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set camera to JPEG mode");
                return ret;
            }
        }
        ESP_LOGI(TAG, "Pixel format set to JPEG");
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Unsupported GenICam pixel format: 0x%08X", genicam_format);
    return ESP_ERR_NOT_SUPPORTED;
}

int camera_get_genicam_pixformat(void)
{
    if (current_camera_pixformat == CAMERA_PIXFORMAT_MONO8)
    {
        return 0x01080001; // Mono8
    }
    else if (current_camera_pixformat == CAMERA_PIXFORMAT_RGB565)
    {
        return 0x02100005; // RGB565Packed
    }
    else if (current_camera_pixformat == CAMERA_PIXFORMAT_YUV422)
    {
        return 0x02100004; // YUV422Packed
    }
    else if (current_camera_pixformat == CAMERA_PIXFORMAT_RGB888)
    {
        return 0x02180014; // RGB8Packed
    }
    else if (current_camera_pixformat == CAMERA_PIXFORMAT_JPEG)
    {
        return 0x80000001; // JPEG
    }
    return 0x01080001; // Default to Mono8
}

size_t camera_get_max_payload_size(void)
{
    if (current_camera_pixformat == CAMERA_PIXFORMAT_JPEG)
    {
        return sizeof(jpeg_buffer); // Variable size for JPEG
    }
    else if (current_camera_pixformat == CAMERA_PIXFORMAT_RGB565)
    {
        return CAMERA_WIDTH * CAMERA_HEIGHT * 2; // 2 bytes per pixel for RGB565
    }
    else if (current_camera_pixformat == CAMERA_PIXFORMAT_YUV422)
    {
        return CAMERA_WIDTH * CAMERA_HEIGHT * 2; // 2 bytes per pixel for YUV422
    }
    else if (current_camera_pixformat == CAMERA_PIXFORMAT_RGB888)
    {
        return CAMERA_WIDTH * CAMERA_HEIGHT * 3; // 3 bytes per pixel for RGB888
    }
    else
    {
        return CAMERA_WIDTH * CAMERA_HEIGHT; // Fixed size for Mono8
    }
}

esp_err_t camera_set_jpeg_quality(int quality)
{
    if (quality < 0 || quality > 63)
    {
        ESP_LOGE(TAG, "JPEG quality out of range: %d (0-63)", quality);
        return ESP_ERR_INVALID_ARG;
    }

    jpeg_quality = quality;

    if (use_real_camera)
    {
        // Set JPEG quality on camera sensor
        sensor_t *s = esp_camera_sensor_get();
        if (s != NULL)
        {
            s->set_quality(s, quality);
            ESP_LOGI(TAG, "JPEG quality set to %d on camera sensor", quality);
        }
        else
        {
            ESP_LOGW(TAG, "Could not get camera sensor to set JPEG quality");
        }
    }

    ESP_LOGI(TAG, "JPEG quality set to %d", quality);
    return ESP_OK;
}

int camera_get_jpeg_quality(void)
{
    return jpeg_quality;
}

// Camera sensor control implementations
esp_err_t camera_set_exposure_time(uint32_t exposure_us)
{
    if (exposure_us < 1 || exposure_us > 1000000)
    {
        ESP_LOGE(TAG, "Exposure time out of range: %lu us (1-1000000)", exposure_us);
        return ESP_ERR_INVALID_ARG;
    }

    exposure_time_us = exposure_us;

    if (use_real_camera)
    {
        sensor_t *s = esp_camera_sensor_get();
        if (s != NULL && s->set_aec_value != NULL)
        {
            // Convert microseconds to sensor units (approximate)
            int aec_value = exposure_time_us / 100; // Rough conversion
            if (aec_value > 1200)
                aec_value = 1200;
            s->set_aec_value(s, aec_value);
            ESP_LOGI(TAG, "Exposure time set to %lu us (aec_value=%d)", exposure_us, aec_value);
        }
        else
        {
            ESP_LOGW(TAG, "Could not set exposure on camera sensor");
        }
    }

    ESP_LOGI(TAG, "Exposure time set to %lu us", exposure_us);

    // Auto-save to NVS
    camera_settings_save_to_nvs();

    return ESP_OK;
}

uint32_t camera_get_exposure_time(void)
{
    return exposure_time_us;
}

esp_err_t camera_set_gain(int gain)
{
    if (gain < 0 || gain > 30)
    {
        ESP_LOGE(TAG, "Gain out of range: %d (0-30 dB)", gain);
        return ESP_ERR_INVALID_ARG;
    }

    gain_value = gain;

    if (use_real_camera)
    {
        sensor_t *s = esp_camera_sensor_get();
        if (s != NULL && s->set_agc_gain != NULL)
        {
            s->set_agc_gain(s, gain);
            ESP_LOGI(TAG, "Gain set to %d dB on camera sensor", gain);
        }
        else
        {
            ESP_LOGW(TAG, "Could not set gain on camera sensor");
        }
    }

    ESP_LOGI(TAG, "Gain set to %d dB", gain);
    return ESP_OK;
}

int camera_get_gain(void)
{
    return gain_value;
}

esp_err_t camera_set_brightness(int brightness)
{
    if (brightness < -2 || brightness > 2)
    {
        ESP_LOGE(TAG, "Brightness out of range: %d (-2 to +2)", brightness);
        return ESP_ERR_INVALID_ARG;
    }

    brightness_value = brightness;

    if (use_real_camera)
    {
        sensor_t *s = esp_camera_sensor_get();
        if (s != NULL && s->set_brightness != NULL)
        {
            s->set_brightness(s, brightness);
            ESP_LOGI(TAG, "Brightness set to %d on camera sensor", brightness);
        }
        else
        {
            ESP_LOGW(TAG, "Could not set brightness on camera sensor");
        }
    }

    ESP_LOGI(TAG, "Brightness set to %d", brightness);
    return ESP_OK;
}

int camera_get_brightness(void)
{
    return brightness_value;
}

esp_err_t camera_set_contrast(int contrast)
{
    if (contrast < -2 || contrast > 2)
    {
        ESP_LOGE(TAG, "Contrast out of range: %d (-2 to +2)", contrast);
        return ESP_ERR_INVALID_ARG;
    }

    contrast_value = contrast;

    if (use_real_camera)
    {
        sensor_t *s = esp_camera_sensor_get();
        if (s != NULL && s->set_contrast != NULL)
        {
            s->set_contrast(s, contrast);
            ESP_LOGI(TAG, "Contrast set to %d on camera sensor", contrast);
        }
        else
        {
            ESP_LOGW(TAG, "Could not set contrast on camera sensor");
        }
    }

    ESP_LOGI(TAG, "Contrast set to %d", contrast);
    return ESP_OK;
}

int camera_get_contrast(void)
{
    return contrast_value;
}

esp_err_t camera_set_saturation(int saturation)
{
    if (saturation < -2 || saturation > 2)
    {
        ESP_LOGE(TAG, "Saturation out of range: %d (-2 to +2)", saturation);
        return ESP_ERR_INVALID_ARG;
    }

    saturation_value = saturation;

    if (use_real_camera)
    {
        sensor_t *s = esp_camera_sensor_get();
        if (s != NULL && s->set_saturation != NULL)
        {
            s->set_saturation(s, saturation);
            ESP_LOGI(TAG, "Saturation set to %d on camera sensor", saturation);
        }
        else
        {
            ESP_LOGW(TAG, "Could not set saturation on camera sensor");
        }
    }

    ESP_LOGI(TAG, "Saturation set to %d", saturation);
    return ESP_OK;
}

int camera_get_saturation(void)
{
    return saturation_value;
}

esp_err_t camera_set_white_balance_mode(int mode)
{
    if (mode != WB_MODE_OFF && mode != WB_MODE_AUTO)
    {
        ESP_LOGE(TAG, "Invalid white balance mode: %d", mode);
        return ESP_ERR_INVALID_ARG;
    }

    white_balance_mode = mode;

    if (use_real_camera)
    {
        sensor_t *s = esp_camera_sensor_get();
        if (s != NULL && s->set_awb_gain != NULL)
        {
            s->set_awb_gain(s, mode == WB_MODE_AUTO ? 1 : 0);
            ESP_LOGI(TAG, "White balance mode set to %s on camera sensor",
                     mode == WB_MODE_AUTO ? "AUTO" : "OFF");
        }
        else
        {
            ESP_LOGW(TAG, "Could not set white balance on camera sensor");
        }
    }

    ESP_LOGI(TAG, "White balance mode set to %s", mode == WB_MODE_AUTO ? "AUTO" : "OFF");
    return ESP_OK;
}

int camera_get_white_balance_mode(void)
{
    return white_balance_mode;
}

esp_err_t camera_set_trigger_mode(int mode)
{
    if (mode != TRIGGER_MODE_OFF && mode != TRIGGER_MODE_ON && mode != TRIGGER_MODE_SOFTWARE)
    {
        ESP_LOGE(TAG, "Invalid trigger mode: %d", mode);
        return ESP_ERR_INVALID_ARG;
    }

    trigger_mode = mode;
    ESP_LOGI(TAG, "Trigger mode set to %s",
             mode == TRIGGER_MODE_OFF ? "OFF" : mode == TRIGGER_MODE_ON ? "ON"
                                                                        : "SOFTWARE");

    return ESP_OK;
}

int camera_get_trigger_mode(void)
{
    return trigger_mode;
}

// NVS storage functions
#define NVS_NAMESPACE "camera_settings"

esp_err_t camera_settings_save_to_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    ESP_LOGI(TAG, "Saving camera settings to NVS");

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    // Save all camera control parameters
    err = nvs_set_u32(nvs_handle, "exposure_time", exposure_time_us);
    if (err != ESP_OK)
        goto nvs_error;

    err = nvs_set_i32(nvs_handle, "gain", gain_value);
    if (err != ESP_OK)
        goto nvs_error;

    err = nvs_set_i32(nvs_handle, "brightness", brightness_value);
    if (err != ESP_OK)
        goto nvs_error;

    err = nvs_set_i32(nvs_handle, "contrast", contrast_value);
    if (err != ESP_OK)
        goto nvs_error;

    err = nvs_set_i32(nvs_handle, "saturation", saturation_value);
    if (err != ESP_OK)
        goto nvs_error;

    err = nvs_set_i32(nvs_handle, "wb_mode", white_balance_mode);
    if (err != ESP_OK)
        goto nvs_error;

    err = nvs_set_i32(nvs_handle, "trigger_mode", trigger_mode);
    if (err != ESP_OK)
        goto nvs_error;

    err = nvs_set_i32(nvs_handle, "jpeg_quality", jpeg_quality);
    if (err != ESP_OK)
        goto nvs_error;

    err = nvs_set_i32(nvs_handle, "pixel_format", current_camera_pixformat);
    if (err != ESP_OK)
        goto nvs_error;

    // Commit the changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK)
        goto nvs_error;

    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Camera settings saved to NVS successfully");
    return ESP_OK;

nvs_error:
    ESP_LOGE(TAG, "Error saving to NVS: %s", esp_err_to_name(err));
    nvs_close(nvs_handle);
    return err;
}

esp_err_t camera_settings_load_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    ESP_LOGI(TAG, "Loading camera settings from NVS");

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "NVS namespace not found, using defaults: %s", esp_err_to_name(err));
        return ESP_OK; // Not an error, just use defaults
    }

    // Load all camera control parameters
    uint32_t temp_u32;
    int32_t temp_i32;

    err = nvs_get_u32(nvs_handle, "exposure_time", &temp_u32);
    if (err == ESP_OK && temp_u32 >= 1 && temp_u32 <= 1000000)
    {
        exposure_time_us = temp_u32;
        camera_set_exposure_time(exposure_time_us);
    }

    err = nvs_get_i32(nvs_handle, "gain", &temp_i32);
    if (err == ESP_OK && temp_i32 >= 0 && temp_i32 <= 30)
    {
        gain_value = temp_i32;
        camera_set_gain(gain_value);
    }

    err = nvs_get_i32(nvs_handle, "brightness", &temp_i32);
    if (err == ESP_OK && temp_i32 >= -2 && temp_i32 <= 2)
    {
        brightness_value = temp_i32;
        camera_set_brightness(brightness_value);
    }

    err = nvs_get_i32(nvs_handle, "contrast", &temp_i32);
    if (err == ESP_OK && temp_i32 >= -2 && temp_i32 <= 2)
    {
        contrast_value = temp_i32;
        camera_set_contrast(contrast_value);
    }

    err = nvs_get_i32(nvs_handle, "saturation", &temp_i32);
    if (err == ESP_OK && temp_i32 >= -2 && temp_i32 <= 2)
    {
        saturation_value = temp_i32;
        camera_set_saturation(saturation_value);
    }

    err = nvs_get_i32(nvs_handle, "wb_mode", &temp_i32);
    if (err == ESP_OK && (temp_i32 == WB_MODE_OFF || temp_i32 == WB_MODE_AUTO))
    {
        white_balance_mode = temp_i32;
        camera_set_white_balance_mode(white_balance_mode);
    }

    err = nvs_get_i32(nvs_handle, "trigger_mode", &temp_i32);
    if (err == ESP_OK && temp_i32 >= TRIGGER_MODE_OFF && temp_i32 <= TRIGGER_MODE_SOFTWARE)
    {
        trigger_mode = temp_i32;
        camera_set_trigger_mode(trigger_mode);
    }

    err = nvs_get_i32(nvs_handle, "jpeg_quality", &temp_i32);
    if (err == ESP_OK && temp_i32 >= 0 && temp_i32 <= 63)
    {
        jpeg_quality = temp_i32;
        camera_set_jpeg_quality(jpeg_quality);
    }

    err = nvs_get_i32(nvs_handle, "pixel_format", &temp_i32);
    if (err == ESP_OK && (temp_i32 == CAMERA_PIXFORMAT_MONO8 ||
                          temp_i32 == CAMERA_PIXFORMAT_JPEG ||
                          temp_i32 == CAMERA_PIXFORMAT_RGB565 ||
                          temp_i32 == CAMERA_PIXFORMAT_YUV422 ||
                          temp_i32 == CAMERA_PIXFORMAT_RGB888))
    {
        current_camera_pixformat = temp_i32;
        // Note: pixel format changes require camera restart, so we just store the setting
    }

    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Camera settings loaded from NVS successfully");
    return ESP_OK;
}

esp_err_t camera_settings_reset_to_defaults(void)
{
    ESP_LOGI(TAG, "Resetting camera settings to defaults");

    // Reset all parameters to their default values
    exposure_time_us = 10000;                          // 10ms default
    gain_value = 0;                                    // 0 dB default
    brightness_value = 0;                              // 0 default
    contrast_value = 0;                                // 0 default
    saturation_value = 0;                              // 0 default
    white_balance_mode = WB_MODE_AUTO;                 // Auto white balance default
    trigger_mode = TRIGGER_MODE_OFF;                   // Free running default
    jpeg_quality = 12;                                 // Default JPEG quality
    current_camera_pixformat = CAMERA_PIXFORMAT_MONO8; // Default to Mono8

    // Apply the settings to the camera
    camera_set_exposure_time(exposure_time_us);
    camera_set_gain(gain_value);
    camera_set_brightness(brightness_value);
    camera_set_contrast(contrast_value);
    camera_set_saturation(saturation_value);
    camera_set_white_balance_mode(white_balance_mode);
    camera_set_trigger_mode(trigger_mode);
    camera_set_jpeg_quality(jpeg_quality);

    // Save the defaults to NVS
    esp_err_t err = camera_settings_save_to_nvs();
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to save default settings to NVS: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Camera settings reset to defaults and saved to NVS");
    return ESP_OK;
}
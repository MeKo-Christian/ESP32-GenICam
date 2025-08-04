#include "web_server.h"
#include "camera_handler.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include <string.h>
#include <sys/param.h> // For MIN macro

static const char *TAG = "web_server";
static httpd_handle_t web_server_handle = NULL;

// HTML interface content
static const char *web_interface_html =
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "    <title>ESP32-CAM GenICam Control</title>\n"
    "    <meta charset=\"UTF-8\">\n"
    "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
    "    <style>\n"
    "        body { font-family: Arial, sans-serif; margin: 20px; background-color: #f0f0f0; }\n"
    "        .container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }\n"
    "        .header { text-align: center; color: #333; border-bottom: 2px solid #007acc; padding-bottom: 10px; margin-bottom: 20px; }\n"
    "        .section { margin-bottom: 20px; padding: 15px; border: 1px solid #ddd; border-radius: 5px; }\n"
    "        .control-group { margin-bottom: 15px; }\n"
    "        label { display: inline-block; width: 150px; font-weight: bold; color: #555; }\n"
    "        input[type=\"range\"] { width: 200px; margin: 0 10px; }\n"
    "        input[type=\"number\"] { width: 100px; padding: 5px; border: 1px solid #ccc; border-radius: 3px; }\n"
    "        select { width: 150px; padding: 5px; border: 1px solid #ccc; border-radius: 3px; }\n"
    "        button { background-color: #007acc; color: white; padding: 10px 20px; border: none; border-radius: 5px; cursor: pointer; margin: 5px; }\n"
    "        button:hover { background-color: #005a99; }\n"
    "        .status { padding: 10px; border-radius: 5px; margin-bottom: 10px; }\n"
    "        .status.success { background-color: #d4edda; color: #155724; border: 1px solid #c3e6cb; }\n"
    "        .status.error { background-color: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }\n"
    "        .value-display { display: inline-block; width: 60px; text-align: center; font-weight: bold; color: #007acc; }\n"
    "    </style>\n"
    "</head>\n"
    "<body>\n"
    "    <div class=\"container\">\n"
    "        <div class=\"header\">\n"
    "            <h1>🎥 ESP32-CAM GenICam Control</h1>\n"
    "            <p>Real-time camera parameter control interface</p>\n"
    "        </div>\n"
    "        \n"
    "        <div id=\"status\" class=\"status\" style=\"display: none;\"></div>\n"
    "        \n"
    "        <div class=\"section\">\n"
    "            <h3>📷 Camera Controls</h3>\n"
    "            <div class=\"control-group\">\n"
    "                <label>Exposure Time:</label>\n"
    "                <input type=\"range\" id=\"exposure\" min=\"1\" max=\"100000\" value=\"10000\" oninput=\"updateExposure()\">\n"
    "                <span class=\"value-display\" id=\"exposureValue\">10000</span> μs\n"
    "            </div>\n"
    "            <div class=\"control-group\">\n"
    "                <label>Gain:</label>\n"
    "                <input type=\"range\" id=\"gain\" min=\"0\" max=\"30\" value=\"0\" oninput=\"updateGain()\">\n"
    "                <span class=\"value-display\" id=\"gainValue\">0</span> dB\n"
    "            </div>\n"
    "            <div class=\"control-group\">\n"
    "                <label>Brightness:</label>\n"
    "                <input type=\"range\" id=\"brightness\" min=\"-2\" max=\"2\" value=\"0\" oninput=\"updateBrightness()\">\n"
    "                <span class=\"value-display\" id=\"brightnessValue\">0</span>\n"
    "            </div>\n"
    "            <div class=\"control-group\">\n"
    "                <label>Contrast:</label>\n"
    "                <input type=\"range\" id=\"contrast\" min=\"-2\" max=\"2\" value=\"0\" oninput=\"updateContrast()\">\n"
    "                <span class=\"value-display\" id=\"contrastValue\">0</span>\n"
    "            </div>\n"
    "            <div class=\"control-group\">\n"
    "                <label>Saturation:</label>\n"
    "                <input type=\"range\" id=\"saturation\" min=\"-2\" max=\"2\" value=\"0\" oninput=\"updateSaturation()\">\n"
    "                <span class=\"value-display\" id=\"saturationValue\">0</span>\n"
    "            </div>\n"
    "        </div>\n"
    "        \n"
    "        <div class=\"section\">\n"
    "            <h3>⚙️ Advanced Controls</h3>\n"
    "            <div class=\"control-group\">\n"
    "                <label>White Balance:</label>\n"
    "                <select id=\"whiteBalance\" onchange=\"updateWhiteBalance()\">\n"
    "                    <option value=\"0\">Off</option>\n"
    "                    <option value=\"1\" selected>Auto</option>\n"
    "                </select>\n"
    "            </div>\n"
    "            <div class=\"control-group\">\n"
    "                <label>Trigger Mode:</label>\n"
    "                <select id=\"triggerMode\" onchange=\"updateTriggerMode()\">\n"
    "                    <option value=\"0\" selected>Off (Free Running)</option>\n"
    "                    <option value=\"1\">On (Hardware)</option>\n"
    "                    <option value=\"2\">Software</option>\n"
    "                </select>\n"
    "            </div>\n"
    "        </div>\n"
    "        \n"
    "        <div class=\"section\">\n"
    "            <h3>🔧 Actions</h3>\n"
    "            <button onclick=\"loadCurrentSettings()\">📥 Load Current Settings</button>\n"
    "            <button onclick=\"resetToDefaults()\">🔄 Reset to Defaults</button>\n"
    "        </div>\n"
    "        \n"
    "        <div class=\"section\">\n"
    "            <h3>ℹ️ Current Status</h3>\n"
    "            <div id=\"cameraStatus\">Loading...</div>\n"
    "        </div>\n"
    "    </div>\n"
    "    \n"
    "    <script>\n"
    "        function showStatus(message, isError = false) {\n"
    "            const status = document.getElementById('status');\n"
    "            status.textContent = message;\n"
    "            status.className = 'status ' + (isError ? 'error' : 'success');\n"
    "            status.style.display = 'block';\n"
    "            setTimeout(() => { status.style.display = 'none'; }, 3000);\n"
    "        }\n"
    "        \n"
    "        function updateExposure() {\n"
    "            const value = document.getElementById('exposure').value;\n"
    "            document.getElementById('exposureValue').textContent = value;\n"
    "            updateParameter('exposure_time', parseInt(value));\n"
    "        }\n"
    "        \n"
    "        function updateGain() {\n"
    "            const value = document.getElementById('gain').value;\n"
    "            document.getElementById('gainValue').textContent = value;\n"
    "            updateParameter('gain', parseInt(value));\n"
    "        }\n"
    "        \n"
    "        function updateBrightness() {\n"
    "            const value = document.getElementById('brightness').value;\n"
    "            document.getElementById('brightnessValue').textContent = value;\n"
    "            updateParameter('brightness', parseInt(value));\n"
    "        }\n"
    "        \n"
    "        function updateContrast() {\n"
    "            const value = document.getElementById('contrast').value;\n"
    "            document.getElementById('contrastValue').textContent = value;\n"
    "            updateParameter('contrast', parseInt(value));\n"
    "        }\n"
    "        \n"
    "        function updateSaturation() {\n"
    "            const value = document.getElementById('saturation').value;\n"
    "            document.getElementById('saturationValue').textContent = value;\n"
    "            updateParameter('saturation', parseInt(value));\n"
    "        }\n"
    "        \n"
    "        function updateWhiteBalance() {\n"
    "            const value = document.getElementById('whiteBalance').value;\n"
    "            updateParameter('white_balance_mode', parseInt(value));\n"
    "        }\n"
    "        \n"
    "        function updateTriggerMode() {\n"
    "            const value = document.getElementById('triggerMode').value;\n"
    "            updateParameter('trigger_mode', parseInt(value));\n"
    "        }\n"
    "        \n"
    "        function updateParameter(param, value) {\n"
    "            fetch('/api/camera/control', {\n"
    "                method: 'POST',\n"
    "                headers: { 'Content-Type': 'application/json' },\n"
    "                body: JSON.stringify({ [param]: value })\n"
    "            })\n"
    "            .then(response => response.json())\n"
    "            .then(data => {\n"
    "                if (data.success) {\n"
    "                    showStatus('✅ ' + param.replace('_', ' ') + ' updated successfully');\n"
    "                } else {\n"
    "                    showStatus('❌ Failed to update ' + param.replace('_', ' '), true);\n"
    "                }\n"
    "            })\n"
    "            .catch(error => {\n"
    "                showStatus('❌ Network error: ' + error.message, true);\n"
    "            });\n"
    "        }\n"
    "        \n"
    "        function loadCurrentSettings() {\n"
    "            fetch('/api/camera/control')\n"
    "            .then(response => response.json())\n"
    "            .then(data => {\n"
    "                document.getElementById('exposure').value = data.exposure_time || 10000;\n"
    "                document.getElementById('exposureValue').textContent = data.exposure_time || 10000;\n"
    "                document.getElementById('gain').value = data.gain || 0;\n"
    "                document.getElementById('gainValue').textContent = data.gain || 0;\n"
    "                document.getElementById('brightness').value = data.brightness || 0;\n"
    "                document.getElementById('brightnessValue').textContent = data.brightness || 0;\n"
    "                document.getElementById('contrast').value = data.contrast || 0;\n"
    "                document.getElementById('contrastValue').textContent = data.contrast || 0;\n"
    "                document.getElementById('saturation').value = data.saturation || 0;\n"
    "                document.getElementById('saturationValue').textContent = data.saturation || 0;\n"
    "                document.getElementById('whiteBalance').value = data.white_balance_mode || 1;\n"
    "                document.getElementById('triggerMode').value = data.trigger_mode || 0;\n"
    "                showStatus('📥 Settings loaded from camera');\n"
    "            })\n"
    "            .catch(error => {\n"
    "                showStatus('❌ Failed to load settings: ' + error.message, true);\n"
    "            });\n"
    "        }\n"
    "        \n"
    "        function resetToDefaults() {\n"
    "            document.getElementById('exposure').value = 10000;\n"
    "            document.getElementById('exposureValue').textContent = '10000';\n"
    "            document.getElementById('gain').value = 0;\n"
    "            document.getElementById('gainValue').textContent = '0';\n"
    "            document.getElementById('brightness').value = 0;\n"
    "            document.getElementById('brightnessValue').textContent = '0';\n"
    "            document.getElementById('contrast').value = 0;\n"
    "            document.getElementById('contrastValue').textContent = '0';\n"
    "            document.getElementById('saturation').value = 0;\n"
    "            document.getElementById('saturationValue').textContent = '0';\n"
    "            document.getElementById('whiteBalance').value = 1;\n"
    "            document.getElementById('triggerMode').value = 0;\n"
    "            \n"
    "            // Apply all defaults\n"
    "            updateParameter('exposure_time', 10000);\n"
    "            updateParameter('gain', 0);\n"
    "            updateParameter('brightness', 0);\n"
    "            updateParameter('contrast', 0);\n"
    "            updateParameter('saturation', 0);\n"
    "            updateParameter('white_balance_mode', 1);\n"
    "            updateParameter('trigger_mode', 0);\n"
    "            showStatus('🔄 Reset to default settings');\n"
    "        }\n"
    "        \n"
    "        function updateCameraStatus() {\n"
    "            fetch('/api/camera/status')\n"
    "            .then(response => response.json())\n"
    "            .then(data => {\n"
    "                const status = document.getElementById('cameraStatus');\n"
    "                status.innerHTML = `\n"
    "                    <strong>Camera Type:</strong> ${data.real_camera ? 'ESP32-CAM Hardware' : 'Dummy Mode'}<br>\n"
    "                    <strong>Pixel Format:</strong> ${data.pixel_format_name}<br>\n"
    "                    <strong>Resolution:</strong> ${data.width} x ${data.height}<br>\n"
    "                    <strong>Max Payload:</strong> ${data.max_payload_size} bytes\n"
    "                `;\n"
    "            })\n"
    "            .catch(error => {\n"
    "                document.getElementById('cameraStatus').textContent = 'Failed to load status';\n"
    "            });\n"
    "        }\n"
    "        \n"
    "        // Load current settings and status on page load\n"
    "        window.onload = function() {\n"
    "            loadCurrentSettings();\n"
    "            updateCameraStatus();\n"
    "            // Update status every 5 seconds\n"
    "            setInterval(updateCameraStatus, 5000);\n"
    "        };\n"
    "    </script>\n"
    "</body>\n"
    "</html>";

// Camera status API handler
esp_err_t handle_camera_status_get(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Camera status GET request");

    cJSON *json = cJSON_CreateObject();
    if (json == NULL)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Get camera information
    cJSON_AddBoolToObject(json, "real_camera", camera_is_real_camera_active());
    cJSON_AddNumberToObject(json, "width", CAMERA_WIDTH);
    cJSON_AddNumberToObject(json, "height", CAMERA_HEIGHT);
    cJSON_AddNumberToObject(json, "max_payload_size", camera_get_max_payload_size());

    // Get pixel format info
    int pixel_format = camera_get_genicam_pixformat();
    const char *format_name;
    switch (pixel_format)
    {
    case 0x01080001:
        format_name = "Mono8";
        break;
    case 0x02100005:
        format_name = "RGB565Packed";
        break;
    case 0x02100004:
        format_name = "YUV422Packed";
        break;
    case 0x02180014:
        format_name = "RGB8Packed";
        break;
    case 0x80000001:
        format_name = "JPEG";
        break;
    default:
        format_name = "Unknown";
        break;
    }
    cJSON_AddStringToObject(json, "pixel_format_name", format_name);
    cJSON_AddNumberToObject(json, "pixel_format", pixel_format);

    char *json_string = cJSON_Print(json);
    if (json_string == NULL)
    {
        cJSON_Delete(json);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);

    free(json_string);
    cJSON_Delete(json);
    return ESP_OK;
}

// Camera control GET handler (return current values)
esp_err_t handle_camera_control_get(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Camera control GET request");

    cJSON *json = cJSON_CreateObject();
    if (json == NULL)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Get current camera control values
    cJSON_AddNumberToObject(json, "exposure_time", camera_get_exposure_time());
    cJSON_AddNumberToObject(json, "gain", camera_get_gain());
    cJSON_AddNumberToObject(json, "brightness", camera_get_brightness());
    cJSON_AddNumberToObject(json, "contrast", camera_get_contrast());
    cJSON_AddNumberToObject(json, "saturation", camera_get_saturation());
    cJSON_AddNumberToObject(json, "white_balance_mode", camera_get_white_balance_mode());
    cJSON_AddNumberToObject(json, "trigger_mode", camera_get_trigger_mode());
    cJSON_AddNumberToObject(json, "jpeg_quality", camera_get_jpeg_quality());

    char *json_string = cJSON_Print(json);
    if (json_string == NULL)
    {
        cJSON_Delete(json);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);

    free(json_string);
    cJSON_Delete(json);
    return ESP_OK;
}

// Camera control POST handler (set values)
esp_err_t handle_camera_control_post(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Camera control POST request");

    char content[WEB_SERVER_MAX_POST_LEN];
    size_t recv_size = MIN(req->content_len, sizeof(content) - 1);

    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0)
    {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        {
            httpd_resp_send_408(req);
        }
        else
        {
            httpd_resp_send_500(req);
        }
        return ESP_FAIL;
    }
    content[ret] = '\0';

    ESP_LOGI(TAG, "Received JSON: %s", content);

    cJSON *json = cJSON_Parse(content);
    if (json == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *response = cJSON_CreateObject();
    bool success = true;

    // Process each parameter that might be in the JSON
    cJSON *param;

    param = cJSON_GetObjectItem(json, "exposure_time");
    if (param && cJSON_IsNumber(param))
    {
        esp_err_t err = camera_set_exposure_time((uint32_t)param->valueint);
        if (err != ESP_OK)
            success = false;
        ESP_LOGI(TAG, "Set exposure_time to %d: %s", param->valueint, err == ESP_OK ? "OK" : "FAIL");
    }

    param = cJSON_GetObjectItem(json, "gain");
    if (param && cJSON_IsNumber(param))
    {
        esp_err_t err = camera_set_gain(param->valueint);
        if (err != ESP_OK)
            success = false;
        ESP_LOGI(TAG, "Set gain to %d: %s", param->valueint, err == ESP_OK ? "OK" : "FAIL");
    }

    param = cJSON_GetObjectItem(json, "brightness");
    if (param && cJSON_IsNumber(param))
    {
        esp_err_t err = camera_set_brightness(param->valueint);
        if (err != ESP_OK)
            success = false;
        ESP_LOGI(TAG, "Set brightness to %d: %s", param->valueint, err == ESP_OK ? "OK" : "FAIL");
    }

    param = cJSON_GetObjectItem(json, "contrast");
    if (param && cJSON_IsNumber(param))
    {
        esp_err_t err = camera_set_contrast(param->valueint);
        if (err != ESP_OK)
            success = false;
        ESP_LOGI(TAG, "Set contrast to %d: %s", param->valueint, err == ESP_OK ? "OK" : "FAIL");
    }

    param = cJSON_GetObjectItem(json, "saturation");
    if (param && cJSON_IsNumber(param))
    {
        esp_err_t err = camera_set_saturation(param->valueint);
        if (err != ESP_OK)
            success = false;
        ESP_LOGI(TAG, "Set saturation to %d: %s", param->valueint, err == ESP_OK ? "OK" : "FAIL");
    }

    param = cJSON_GetObjectItem(json, "white_balance_mode");
    if (param && cJSON_IsNumber(param))
    {
        esp_err_t err = camera_set_white_balance_mode(param->valueint);
        if (err != ESP_OK)
            success = false;
        ESP_LOGI(TAG, "Set white_balance_mode to %d: %s", param->valueint, err == ESP_OK ? "OK" : "FAIL");
    }

    param = cJSON_GetObjectItem(json, "trigger_mode");
    if (param && cJSON_IsNumber(param))
    {
        esp_err_t err = camera_set_trigger_mode(param->valueint);
        if (err != ESP_OK)
            success = false;
        ESP_LOGI(TAG, "Set trigger_mode to %d: %s", param->valueint, err == ESP_OK ? "OK" : "FAIL");
    }

    cJSON_AddBoolToObject(response, "success", success);

    char *response_string = cJSON_Print(response);
    if (response_string == NULL)
    {
        cJSON_Delete(json);
        cJSON_Delete(response);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_string, HTTPD_RESP_USE_STRLEN);

    free(response_string);
    cJSON_Delete(json);
    cJSON_Delete(response);
    return ESP_OK;
}

// Web interface handler (serve HTML page)
esp_err_t handle_web_interface_get(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Web interface GET request");

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, web_interface_html, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

// URI handlers
static const httpd_uri_t uri_get_web_interface = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = handle_web_interface_get,
    .user_ctx = NULL};

static const httpd_uri_t uri_get_camera_status = {
    .uri = "/api/camera/status",
    .method = HTTP_GET,
    .handler = handle_camera_status_get,
    .user_ctx = NULL};

static const httpd_uri_t uri_get_camera_control = {
    .uri = "/api/camera/control",
    .method = HTTP_GET,
    .handler = handle_camera_control_get,
    .user_ctx = NULL};

static const httpd_uri_t uri_post_camera_control = {
    .uri = "/api/camera/control",
    .method = HTTP_POST,
    .handler = handle_camera_control_post,
    .user_ctx = NULL};

esp_err_t web_server_init(void)
{
    ESP_LOGI(TAG, "Initializing HTTP web server");
    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    if (web_server_handle != NULL)
    {
        ESP_LOGW(TAG, "Web server already running");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WEB_SERVER_PORT;
    config.max_uri_handlers = 8;
    config.max_resp_headers = 8;
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Starting HTTP web server on port %d", config.server_port);

    esp_err_t ret = httpd_start(&web_server_handle, &config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register URI handlers
    httpd_register_uri_handler(web_server_handle, &uri_get_web_interface);
    httpd_register_uri_handler(web_server_handle, &uri_get_camera_status);
    httpd_register_uri_handler(web_server_handle, &uri_get_camera_control);
    httpd_register_uri_handler(web_server_handle, &uri_post_camera_control);

    ESP_LOGI(TAG, "HTTP web server started successfully");
    ESP_LOGI(TAG, "Web interface available at: http://[ESP32_IP_ADDRESS]/");
    ESP_LOGI(TAG, "API endpoints:");
    ESP_LOGI(TAG, "  GET /api/camera/status - Camera status information");
    ESP_LOGI(TAG, "  GET /api/camera/control - Current camera control values");
    ESP_LOGI(TAG, "  POST /api/camera/control - Set camera control values");

    return ESP_OK;
}

esp_err_t web_server_stop(void)
{
    if (web_server_handle == NULL)
    {
        ESP_LOGW(TAG, "Web server not running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping HTTP web server");
    esp_err_t ret = httpd_stop(web_server_handle);
    web_server_handle = NULL;

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "HTTP web server stopped");
    }
    else
    {
        ESP_LOGE(TAG, "Failed to stop HTTP server: %s", esp_err_to_name(ret));
    }

    return ret;
}

bool web_server_is_running(void)
{
    return (web_server_handle != NULL);
}
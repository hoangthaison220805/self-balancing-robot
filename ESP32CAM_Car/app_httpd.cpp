#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "Arduino.h"
#include <WiFi.h>

extern String WiFiAddr;

typedef struct {
        size_t size; 
        size_t index; 
        size_t count; 
        int sum;
        int * values; 
} ra_filter_t;

static ra_filter_t ra_filter;
httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

static ra_filter_t * ra_filter_init(ra_filter_t * filter, size_t sample_size){
    memset(filter, 0, sizeof(ra_filter_t));
    filter->values = (int *)malloc(sample_size * sizeof(int));
    if(!filter->values) return NULL;
    memset(filter->values, 0, sample_size * sizeof(int));
    filter->size = sample_size;
    return filter;
}

static int ra_filter_run(ra_filter_t * filter, int value){
    if(!filter->values) return value;
    filter->sum -= filter->values[filter->index];
    filter->values[filter->index] = value;
    filter->sum += filter->values[filter->index];
    filter->index++;
    filter->index = filter->index % filter->size;
    if (filter->count < filter->size) filter->count++;
    return filter->sum / filter->count;
}

static esp_err_t stream_handler(httpd_req_t *req){
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t * _jpg_buf = NULL;
    char part_buf[64];
    static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=123456789";
    static const char* _STREAM_BOUNDARY = "\r\n--123456789\r\n";
    static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if(res != ESP_OK) return res;

    while(true){
        fb = esp_camera_fb_get();
        if (!fb) {
            res = ESP_FAIL;
        } else {
            _jpg_buf_len = fb->len;
            _jpg_buf = fb->buf;
            size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
            if(res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
            if(res == ESP_OK) res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
            esp_camera_fb_return(fb);
        }
        if(res != ESP_OK) break;
        delay(1); 
    }
    return res;
}

static esp_err_t index_handler(httpd_req_t *req){
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    String ip = WiFi.softAPIP().toString(); 
    
    String page = "<html><head><meta charset='UTF-8'>";
    // BẢO VỆ 1: Cấu hình Viewport ngăn chặn zoom ở mức hệ thống trình duyệt
    page += "<meta name='viewport' content='width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no, viewport-fit=cover'>";
    
    page += "<style>";
    // BẢO VỆ 2: CSS Khóa chặt hành vi người dùng
    page += "*{margin:0;padding:0;box-sizing:border-box;-webkit-tap-highlight-color:transparent;}";
    page += "body{display:flex;flex-direction:column;align-items:center;justify-content:center;background:#000;color:#00ff00;font-family:sans-serif;height:100vh;width:100vw;overflow:hidden;position:fixed;";
    page += "user-select:none;-webkit-user-select:none;-webkit-touch-callout:none;touch-action:none;}";
    
    // BẢO VỆ 3: Khóa hình ảnh camera (Chống kéo, chống copy)
    page += ".stream-container{width:95%; max-width:400px; border:3px solid #00ff00; background:#000; box-shadow:0 0 20px #00ff00; pointer-events:none;}";
    page += ".stream-img{width:100%; height:auto; display:block;  -webkit-user-drag:none; user-drag:none;}";
    
    page += ".btn{width:95px;height:95px;margin:10px;font-size:40px;background:#222;color:#fff;border:4px solid #444;border-radius:50%;display:flex;align-items:center;justify-content:center;outline:none;transition:0.1s;}";
    page += ".btn:active{background:#004400;border-color:#00ff00;box-shadow:0 0 20px #00ff00;} div{display:flex;}";
    page += "</style></head><body>";

    page += "<div class='stream-container'><img src='http://" + ip + ":81/stream' class='stream-img'></div>";
    page += "<h2>MASTER CONTROL</h2>";

    // BẢO VỆ 4: JAVASCRIPT CHỐNG XÂM NHẬP & CHỐNG SAI LỖI THAO TÁC
    page += "<script>";
    page += "function s(a){fetch('/'+a);}";
    
    // Chặn menu chuột phải & chạm giữ (Mobile)
    page += "document.addEventListener('contextmenu',function(e){e.preventDefault();});";
    
    // Chặn kéo thả bất kỳ cái gì trên trang
    page += "document.addEventListener('dragstart',function(e){e.preventDefault();});";
    
    // Chặn Zoom bằng tổ hợp phím Ctrl + Cuộn chuột hoặc phím +/-
    page += "document.addEventListener('wheel',function(e){if(e.ctrlKey)e.preventDefault();},{passive:false});";
    page += "document.onkeydown=function(e){if(e.keyCode==123||(e.ctrlKey&&(e.keyCode==85||e.keyCode==67||e.keyCode==73||e.keyCode==83||e.keyCode==107||e.keyCode==109||e.keyCode==187||e.keyCode==189)))return false;};";
    
    // Chặn Zoom 2 ngón tay (Pinch)
    page += "document.addEventListener('touchstart',function(e){if(e.touches.length>1)e.preventDefault();},{passive:false});";
    
    // Chặn Zoom khi nhấn đúp (Double tap) - Cực kỳ quan trọng khi bấm nút nhanh
    page += "var lastTouch=0; document.addEventListener('touchend',function(e){var now=new Date().getTime(); if(now-lastTouch<=300)e.preventDefault(); lastTouch=now;},false);";
    
    page += "</script>";

    // Hệ thống nút bấm an toàn (Pointer Out tự dừng xe)
    page += "<button class='btn' onpointerdown=\"s('go')\" onpointerup=\"s('stop')\" onpointerout=\"s('stop')\">▲</button>";
    page += "<div>";
    page += "<button class='btn' onpointerdown=\"s('left')\" onpointerup=\"s('stop')\" onpointerout=\"s('stop')\">◀</button>";
    page += "<button class='btn' onpointerdown=\"s('right')\" onpointerup=\"s('stop')\" onpointerout=\"s('stop')\">▶</button>";
    page += "</div>";
    page += "<button class='btn' onpointerdown=\"s('back')\" onpointerup=\"s('stop')\" onpointerout=\"s('stop')\">▼</button>";

    page += "</body></html>";
    return httpd_resp_send(req, page.c_str(), page.length());
}
// Handler gửi ký tự sang Serial2
static esp_err_t cmd_handler(httpd_req_t *req) {
    String uri = String(req->uri);
    if(uri.indexOf("go") != -1) Serial2.print('F');
    else if(uri.indexOf("back") != -1) Serial2.print('B');
    else if(uri.indexOf("left") != -1) Serial2.print('L');
    else if(uri.indexOf("right") != -1) Serial2.print('R');
    else if(uri.indexOf("stop") != -1) Serial2.print('S');
    
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "OK", 2);
}

void startCameraServer(){
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    
    httpd_uri_t index_uri = { "/", HTTP_GET, index_handler, NULL };
    httpd_uri_t go_uri = { "/go", HTTP_GET, cmd_handler, NULL };
    httpd_uri_t back_uri = { "/back", HTTP_GET, cmd_handler, NULL };
    httpd_uri_t left_uri = { "/left", HTTP_GET, cmd_handler, NULL };
    httpd_uri_t right_uri = { "/right", HTTP_GET, cmd_handler, NULL };
    httpd_uri_t stop_uri = { "/stop", HTTP_GET, cmd_handler, NULL };
    httpd_uri_t stream_uri = { "/stream", HTTP_GET, stream_handler, NULL };

    ra_filter_init(&ra_filter, 20);

    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &go_uri);
        httpd_register_uri_handler(camera_httpd, &back_uri);
        httpd_register_uri_handler(camera_httpd, &left_uri);
        httpd_register_uri_handler(camera_httpd, &right_uri);
        httpd_register_uri_handler(camera_httpd, &stop_uri);
    }

    config.server_port = 81;
    config.ctrl_port = 81;
    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}
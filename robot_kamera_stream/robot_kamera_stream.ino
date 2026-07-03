/*
  ===========================================================
  Robot auto - KAMERA (ESP32-CAM, AI-Thinker)
  -----------------------------------------------------------
  Zaseban uredjaj od glavnog kontrolera (ESP32-S3). Povezuje se
  na ISTU WiFi mrezu (hotspot telefona) i emituje MJPEG video.

  Aplikacija ga cita na:  http://<IP-kamere>:81/stream
  (isto sto app trazi u polju "IP adrese kamere")
  Ako u browseru otvoris  http://<IP-kamere>:81/  vidis i probni
  prikaz uzivo (zgodno za test bez aplikacije).

  -----------------------------------------------------------
  ARDUINO IDE PODESAVANJA:
   * Board: "AI Thinker ESP32-CAM"
   * PSRAM: Enabled
   * Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"
   * Za upload: AI-Thinker nema USB -> koristi FTDI (5V), a
     GPIO0 spoji na GND SAMO za vreme uploada, pa otkaci i
     resetuj. Za normalan rad GPIO0 mora da "visi" (slobodan).

  HARDVER (iz ranijeg iskustva sa ovom kamerom):
   * Kondenzator 470-1000uF na 5V liniji kamere protiv
     brownout reseta tokom WiFi slanja.
   * Antena: jumper (0-ohm otpornik) na IPEX strani ako koristis
     eksternu antenu.
   * Napajaj sa stabilnih 5V (dovoljno struje) - slab izvor je
     najcesci uzrok pregrevanja/resetovanja.
  ===========================================================
*/

#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "soc/soc.h"           // za iskljucenje brownout detektora
#include "soc/rtc_cntl_reg.h"
#include "secrets.h" 

// ---------- Pinovi kamere: AI-Thinker ESP32-CAM ----------
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ---------- MJPEG stream ----------
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY     = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART         = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;

// Probna stranica (otvoris IP:81 u browseru)
static const char INDEX_HTML[] =
  "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
  "<body style='margin:0;background:#000'>"
  "<img src='/stream' style='width:100%;height:auto;display:block'>"
  "</body>";

esp_err_t index_handler(httpd_req_t *req){
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, INDEX_HTML, strlen(INDEX_HTML));
}

esp_err_t stream_handler(httpd_req_t *req){
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t * _jpg_buf = NULL;
  char part_buf[64];

  res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if(res != ESP_OK) return res;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "X-Framerate", "60");

  while(true){
    fb = esp_camera_fb_get();
    if(!fb){
      Serial.println("Neuspeo capture frejma");
      res = ESP_FAIL;
    } else {
      if(fb->format != PIXFORMAT_JPEG){
        bool ok = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
        esp_camera_fb_return(fb); fb = NULL;
        if(!ok){ Serial.println("JPEG konverzija pala"); res = ESP_FAIL; }
      } else {
        _jpg_buf_len = fb->len;
        _jpg_buf = fb->buf;
      }
    }

    if(res == ESP_OK)
      res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    if(res == ESP_OK){
      size_t hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, part_buf, hlen);
    }
    if(res == ESP_OK)
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);

    if(fb){ esp_camera_fb_return(fb); fb = NULL; _jpg_buf = NULL; }
    else if(_jpg_buf){ free(_jpg_buf); _jpg_buf = NULL; }

    if(res != ESP_OK) break;   // klijent zatvorio vezu
  }
  return res;
}

void startCameraServer(){
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 81;      // app trazi stream na portu 81
  config.ctrl_port   = 32769;   // da ne smeta ako kasnije dodas server na 80
  config.max_uri_handlers = 4;

  httpd_uri_t index_uri  = { .uri="/",       .method=HTTP_GET, .handler=index_handler,  .user_ctx=NULL };
  httpd_uri_t stream_uri = { .uri="/stream", .method=HTTP_GET, .handler=stream_handler, .user_ctx=NULL };

  if(httpd_start(&stream_httpd, &config) == ESP_OK){
    httpd_register_uri_handler(stream_httpd, &index_uri);
    httpd_register_uri_handler(stream_httpd, &stream_uri);
    Serial.println("Stream server pokrenut na portu 81 (/stream).");
  } else {
    Serial.println("Nije uspeo start stream servera!");
  }
}

void setup(){
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);  // gasi brownout reset
  // NAPOMENA: ovo je "flaster" - pravo resenje je stabilno napajanje
  // (kondenzator + dobrih 5V). Ako se resetuje, sredi napajanje.

  Serial.begin(115200);
  Serial.setDebugOutput(false);
  Serial.println();

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_VGA;        // 640x480 (4:3) - dobar balans
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;                 // manji broj = bolji kvalitet, veci fajl
  config.fb_count = 2;

  // Bez PSRAM-a: manja rezolucija i jedan bafer
  if(!psramFound()){
    Serial.println("PSRAM nije nadjen - snizavam rezoluciju.");
    config.frame_size = FRAMESIZE_CIF;      // 400x296
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.fb_count = 1;
    config.grab_mode = CAMERA_GRAB_LATEST;
  }

  esp_err_t err = esp_camera_init(&config);
  if(err != ESP_OK){
    Serial.printf("Init kamere pao: 0x%x\n", err);
    Serial.println("Proveri model ploce, napajanje i da GPIO0 nije na GND.");
    delay(3000);
    ESP.restart();
  }

  // Fina podesavanja slike
  sensor_t * s = esp_camera_sensor_get();
  s->set_brightness(s, 1);
  s->set_saturation(s, 0);
  // Ako je slika naopako (zavisi kako je kamera montirana), otkomentarisi:
  s->set_vflip(s, 1);      // gore-dole
  // s->set_hmirror(s, 1);    // levo-desno

  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  WiFi.setSleep(false);
  Serial.print("Povezivanje na WiFi");
  unsigned long start = millis();
  while(WiFi.status() != WL_CONNECTED && millis() - start < 20000){
    delay(400); Serial.print(".");
  }
  Serial.println();

  if(WiFi.status() == WL_CONNECTED){
    startCameraServer();
    Serial.print("Kamera IP adresa: ");
    Serial.println(WiFi.localIP());   // OVO unosis u app (polje "IP adrese kamere")
    Serial.print("Stream: http://");
    Serial.print(WiFi.localIP());
    Serial.println(":81/stream");
  } else {
    Serial.println("WiFi: nema veze (proveri SSID/lozinku/hotspot). Restart za 3 s...");
    delay(3000);
    ESP.restart();
  }
}

void loop(){
  // Sve radi stream server u pozadini. Samo drzimo WiFi zivim.
  static unsigned long lastPrint = 0;
  if(millis() - lastPrint > 5000){
    lastPrint = millis();
    if(WiFi.status() != WL_CONNECTED){
      Serial.println("WiFi pao - restart...");
      delay(500);
      ESP.restart();
    } else {
      Serial.print("Kamera OK. IP: ");
      Serial.print(WiFi.localIP());
      Serial.println(":81/stream");
    }
  }
  delay(100);
}

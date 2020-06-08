/*************************************

	M5camera dnn Project

**************************************/

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_log.h"

#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "esp_event_loop.h"
#include "esp_http_server.h"


/**************************** 
	app_dnn.c 
	DATE:2019.12.17
	Author:Shouhei
*****************************/

#include <math.h>
#include "esp_system.h"
#include "dl_matrix.h"

#include "MainRuntime_inference.h"
#include "MainRuntime_parameters.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"

static const char* TAG = "camera";

//M5STACK_CAM PIN Map
#define CAM_PIN_RESET   15 //software reset will be performed
#define CAM_PIN_XCLK    27
#define CAM_PIN_SIOD    22
#define CAM_PIN_SIOC    23

#define CAM_PIN_D7      19
#define CAM_PIN_D6      36
#define CAM_PIN_D5      18
#define CAM_PIN_D4      39
#define CAM_PIN_D3      5
#define CAM_PIN_D2      34
#define CAM_PIN_D1      35
#define CAM_PIN_D0      32

#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    26
#define CAM_PIN_PCLK    21

#define CAM_XCLK_FREQ   10000000

#define CAM_USE_WIFI

#define ESP_WIFI_SSID "m5stack-cam-4"
#define ESP_WIFI_PASS ""
#define MAX_STA_CONN  1

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

SemaphoreHandle_t print_mux = NULL;
static EventGroupHandle_t s_wifi_event_group;
static ip4_addr_t s_ip_addr;
const int CONNECTED_BIT = BIT0;

static camera_config_t camera_config = {
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sscb_sda = CAM_PIN_SIOD,
    .pin_sscb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,

    //XCLK 20MHz or 10MHz
    .xclk_freq_hz = CAM_XCLK_FREQ,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
  //  .pixel_format = PIXFORMAT_JPEG, //YUV422,GRAYSCALE,RGB565,JPEG
  .pixel_format = PIXFORMAT_JPEG,
  .frame_size = FRAMESIZE_QQVGA, //QQVGA-UXGA Do not use sizes above QVGA when not JPEG

    .jpeg_quality =64, //0-63 lower number means higher quality
    .fb_count = 1 //if more than one, i2s runs in continuous mode. Use only with JPEG
};

//extern void image_resize_linear(uint8_t *, uint8_t *, int , int ,int , int , int );

extern void app_dnn_main();
static void wifi_init_softap();
static esp_err_t http_server_init();

/*******************************
�@�j���[�����l�b�g���[�N�p�ϐ�
********************************/
void *_context = NULL;

/*******************************
�@�J�����Z���T���W���[���ݒ�p
********************************/
sensor_t *sensor = NULL;
void camera_setting(){
  sensor->set_exposure_ctrl(sensor, 1);
  sensor->set_aec2(sensor, 1);
  //sensor->set_aec_value(sensor, 100);
  //sensor->set_special_effect(sensor, 5);
  //sensor->set_agc_gain(sensor, 30);
  //sensor->set_hmirror(sensor, 1);
  //sensor->set_vflip(sensor, 1);
  //sensor->set_raw_gma(sensor, 1);
  sensor->set_whitebal(sensor, 1);
  //sensor->set_awb_gain(sensor, 1);
  sensor->set_gain_ctrl(sensor, 1);
  //sensor->set_lenc(sensor, 0);
  //sensor->set_dcw(sensor, 1);
  //sensor->set_bpc(sensor, 0);
  //sensor->set_wpc(sensor, 1);
}


/*************************************
�@���C���֐��A
�@�@�E�����ݒ�@
�@	�E�J�����̏����ݒ�
	�E�j���[�����l�b�g���[�N�֐�
	�EWIFI�ݒ�
**************************************/
void app_main()
{
    esp_log_level_set("wifi", ESP_LOG_INFO);
    
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        ESP_ERROR_CHECK( nvs_flash_init() );
    }

    err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera Init Failed");
    }
    
  
	sensor = esp_camera_sensor_get();
	//sensor->set_colorbar(sensor, 1);  //Color Bar display ON
	camera_setting();

    print_mux = xSemaphoreCreateMutex();
  
  	app_dnn_main();
  
	wifi_init_softap();

    vTaskDelay(100 / portTICK_PERIOD_MS);
    http_server_init();
}

/*************************************
�@JPEG�Î~��pHTTP�z�M�֐��A
**************************************/
esp_err_t jpg_httpd_handler(httpd_req_t *req){
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    size_t fb_len = 0;
    int64_t fr_start = esp_timer_get_time();

    fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    res = httpd_resp_set_type(req, "image/jpeg");
    if(res == ESP_OK){
        res = httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    }

    if(res == ESP_OK){
        fb_len = fb->len;
        res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    }
    esp_camera_fb_return(fb);
    int64_t fr_end = esp_timer_get_time();
    ESP_LOGI(TAG, "JPG: %uKB %ums", (uint32_t)(fb_len/1024), (uint32_t)((fr_end - fr_start)/1000));
    return res;
}

/*************************************
�@JPEG����pHTTP�z�M�֐��A
**************************************/
esp_err_t jpg_stream_httpd_handler(httpd_req_t *req){
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len;
    uint8_t * _jpg_buf;
    char * part_buf[64];
    static int64_t last_frame = 0;
    if(!last_frame) {
        last_frame = esp_timer_get_time();
    }

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if(res != ESP_OK){
        return res;
    }
   
    while(true){

        xSemaphoreTake(print_mux, portMAX_DELAY);
        fb = esp_camera_fb_get();
		xSemaphoreGive(print_mux);
       

        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            res = ESP_FAIL;
        } else {
            if(fb->format != PIXFORMAT_JPEG){
                bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
                if(!jpeg_converted){
                    ESP_LOGE(TAG, "JPEG compression failed");
                    esp_camera_fb_return(fb);
                    res = ESP_FAIL;
                }
            } else {
                _jpg_buf_len = fb->len;
                _jpg_buf = fb->buf;
            }
        }
        
        if(res == ESP_OK){
            size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);

            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req,_STREAM_BOUNDARY,strlen(_STREAM_BOUNDARY));
        }
        if(fb->format != PIXFORMAT_JPEG){
            free(_jpg_buf);
        }
      
        esp_camera_fb_return(fb);
       


        if(res != ESP_OK){
            break;
        }
        int64_t fr_end = esp_timer_get_time();
        int64_t frame_time = fr_end - last_frame;
        last_frame = fr_end;
        frame_time /= 1000;
        //ESP_LOGI(TAG, "MJPG: %uKB %ums (%.1ffps)",
            //(uint32_t)(_jpg_buf_len/1024),
            //(uint32_t)frame_time, 1000.0 / (uint32_t)frame_time);
    }

    last_frame = 0;
    return res;
}


static esp_err_t http_server_init(){
    httpd_handle_t server;
    httpd_uri_t jpeg_uri = {
        .uri = "/jpg",
        .method = HTTP_GET,
        .handler = jpg_httpd_handler,
        .user_ctx = NULL
    };

    httpd_uri_t jpeg_stream_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = jpg_stream_httpd_handler,
        .user_ctx = NULL
    };

    httpd_config_t http_options = HTTPD_DEFAULT_CONFIG();

    ESP_ERROR_CHECK(httpd_start(&server, &http_options));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &jpeg_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &jpeg_stream_uri));

    return ESP_OK;
}

static esp_err_t event_handler(void* ctx, system_event_t* event) 
{
  switch (event->event_id) {
    case SYSTEM_EVENT_STA_START:
      esp_wifi_connect();
      break;
    case SYSTEM_EVENT_STA_GOT_IP:
      ESP_LOGI(TAG, "got ip:%s", ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
      s_ip_addr = event->event_info.got_ip.ip_info.ip;
      xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
      break;
    case SYSTEM_EVENT_AP_STACONNECTED:
      ESP_LOGI(TAG, "station:" MACSTR " join, AID=%d", MAC2STR(event->event_info.sta_connected.mac),
               event->event_info.sta_connected.aid);
      xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
      break;
    case SYSTEM_EVENT_AP_STADISCONNECTED:
      ESP_LOGI(TAG, "station:" MACSTR "leave, AID=%d", MAC2STR(event->event_info.sta_disconnected.mac),
               event->event_info.sta_disconnected.aid);
      xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
      break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
      esp_wifi_connect();
      xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
      break;
    default:
      break;
  }
  return ESP_OK;
}

static void wifi_init_softap() 
{
  	s_wifi_event_group = xEventGroupCreate();

  	tcpip_adapter_init();
  	ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

  	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  	wifi_config_t wifi_config = {
    	.ap = 	{.ssid = ESP_WIFI_SSID,
    	        .ssid_len = strlen(ESP_WIFI_SSID),
             	.password = ESP_WIFI_PASS,
             	.max_connection = MAX_STA_CONN,
             	.authmode = WIFI_AUTH_WPA_WPA2_PSK},
  	};
  	
  	if (strlen(ESP_WIFI_PASS) == 0) {
    	wifi_config.ap.authmode = WIFI_AUTH_OPEN;
  	}

  	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
  	ESP_ERROR_CHECK(esp_wifi_start());

	/*********************/
	uint8_t addr[4] = {192, 168, 4, 1};
	union{
		uint8_t ip[4];
		ip4_addr_t ip4;
	} address ;
	
	address.ip[0]=addr[0];
	address.ip[1]=addr[1];
	address.ip[2]=addr[2];	
	address.ip[3]=addr[3];
	
	//s_ip_addr = *(ip4_addr_t*)&addr;
	s_ip_addr = (ip4_addr_t)address.ip4;
	
	ESP_LOGI(TAG, "wifi_init_softap finished.SSID:%s password:%s",
	ESP_WIFI_SSID, ESP_WIFI_PASS);
}

/********************************************************
�@�@�擾�����f����O�������A�j���[�����l�b�g���[�N�����{
	���_���s�����ʂ�\������B
  		by syano 2020.04.29
*********************************************************/
void task_process (void *arg)
{
    dl_matrix3du_t *image_matrix = NULL;	//�R�F�����J�����f���ϊ��p�ϐ�
    camera_fb_t *fb = NULL;					//�J��������󂯎��o�b�t�@

    	//static uint8_t resized_img[NNABLART_MAINRUNTIME_INPUT0_SIZE*3];//�j���[�����l�b�g���[�N���͗p�̕ϐ��@RGB�p
	static uint8_t resized_img[NNABLART_MAINRUNTIME_INPUT0_SIZE];//�j���[�����l�b�g���[�N���͗p�̕ϐ� �����p

	float *probs;	//�j���[�����l�b�g���[�N�o�͂̎���m��

	//�j���[�����l�b�g���[�N�ւ̓��̓o�b�t�@
 	float *nn_input_buffer = nnablart_mainruntime_input_buffer(_context, 0);
	
	//Output Labels ���_�������ʂɑ΂��郉�x��
	static char *Label[NNABLART_MAINRUNTIME_OUTPUT0_SIZE] ={
		"A0","B1","C2","D3","E4","F5","G6","H7","I8","J9"}; 

	
    do {
      
		ESP_LOGI("NN BUF ", " %d",sizeof(nn_input_buffer) );


		int64_t start_time = esp_timer_get_time();
        xSemaphoreTake(print_mux, portMAX_DELAY);
        fb = esp_camera_fb_get();
        xSemaphoreGive(print_mux);
        
		if (!fb){
            ESP_LOGE(TAG, "Camera capture failed");
            //return;
			continue;
        }
        int64_t fb_get_time = esp_timer_get_time();
        
		//ESP_LOGI(TAG, "Get one frame in %lld ms.", (fb_get_time - start_time) / 1000);
		ESP_LOGI(TAG, " w:%d h:%d",fb->width, fb->height);

		//�J�����f���i�[�p��dl_matrix3du�̃������̈�̊m��
        image_matrix = dl_matrix3du_alloc(1, fb->width, fb->height, 3);

		//�J�����f����RGB888�֕ϊ����� �ǂ̃t�H�[�}�b�g�ł�RGB888�֕ϊ��ł���B
        uint32_t res = fmt2rgb888(fb->buf, fb->len, fb->format, image_matrix->item);
		ESP_LOGI("dl_matrix3du_t", "w=%d,h=%d,x=%d,n=%d,stride=%d,item=%x",
			image_matrix->w,
			image_matrix->h,
			image_matrix->c,
			image_matrix->n,
			image_matrix->stride,
			image_matrix->item[0] 
		);
			
	
        if (true != res)
        {
            ESP_LOGE(TAG, "fmt2rgb888 failed, fb: %d", fb->len);
            dl_matrix3du_free(image_matrix);
			
			//return;
            continue;
        }
		//�J�����f�����\�[�X���J��
		esp_camera_fb_return(fb);

       	//Resize����
 		dl_matrix3du_t *resize_image_matrix = NULL;						//���T�C�Y�f���ϐ�
		resize_image_matrix = dl_matrix3du_alloc(1, 28, 28, 3);					//���T�C�Y��̉f���i�[�p��dl_matrix3du�̃������̈�̊m��
		stbir_resize_uint8(image_matrix->item, 160, 120, 0, resize_image_matrix->item, 28, 28, 0, 3);//���T�C�Y����
	    	//stbir_resize_uint8(fb->buf, 160, 120, 0, resized_img, 28, 28, 0, 1);
	
		//RGB�ϐ��\���̂��쐬���A���T�C�Y��̉�f������
		struct RGB {
   			char r, g, b;
		} ;
		struct RGB *rgb_data;
		rgb_data = (struct RGB *)&resize_image_matrix->item[0];

		ESP_LOGI("RGB","%d*3=%d",sizeof(resized_img)/3,sizeof(resized_img));
		ESP_LOGI("JPG","NNABLART_MAINRUNTIME_INPUT0_SIZE = %d",NNABLART_MAINRUNTIME_INPUT0_SIZE);
	
		
		//float p;
		char mozaics[28*3+1],mozaicc[4];
		// Red�̉f�����m�F
	/*
		for (int i = 0; i < NNABLART_MAINRUNTIME_INPUT0_SIZE; i++) {
			if( i%28 == 0 && (i+28) < NNABLART_MAINRUNTIME_INPUT0_SIZE){
				for(int n=0;n<28;n++){
					sprintf(mozaicc," %2x",rgb_data[i+n].r);
					strcat(mozaics,mozaicc );
				}
				mozaics[28*3]='\0';
				ESP_LOGI("JPG_R","%3d:%s, %d",i,mozaics,sizeof(mozaics));
				mozaics[0]='\0';
			}
		}
		// Green�̉f�����m�F
		for (int i = 0; i < NNABLART_MAINRUNTIME_INPUT0_SIZE; i++) {
			if( i%28 == 0 && (i+28) < NNABLART_MAINRUNTIME_INPUT0_SIZE){
				for(int n=0;n<28;n++){
					sprintf(mozaicc," %2x",rgb_data[i+n].g);
					strcat(mozaics,mozaicc );
				}
				mozaics[28*3]='\0';
				ESP_LOGI("JPG_G","%3d:%s, %d",i,mozaics,sizeof(mozaics));
				mozaics[0]='\0';
			}
		}
		// Blue�̉f�����m�F
		for (int i = 0; i < NNABLART_MAINRUNTIME_INPUT0_SIZE; i++) {
			if( i%28 == 0 && (i+28) < NNABLART_MAINRUNTIME_INPUT0_SIZE){
				for(int n=0;n<28;n++){
					sprintf(mozaicc," %2x",rgb_data[i+n].b);
					strcat(mozaics,mozaicc );
				}
				mozaics[28*3]='\0';
				ESP_LOGI("JPG_B","%3d:%s, %d",i,mozaics,sizeof(mozaics));
				mozaics[0]='\0';
			}
		}*/
		// GlayScale�̉f�����쐬
		int pp;
		for (int i = 0; i < NNABLART_MAINRUNTIME_INPUT0_SIZE; i++) {
			if( i%28 == 0 && (i+28) < NNABLART_MAINRUNTIME_INPUT0_SIZE){
				for(int n=0;n<28;n++){
					pp = 255 - ( rgb_data[i+n].r+rgb_data[i+n].g+rgb_data[i+n].b)/3  ;
					if (pp < 180) pp = 0;
					sprintf(mozaicc,"%3d",pp);
					strcat(mozaics,mozaicc );
				}
				mozaics[28*3]='\0';
				ESP_LOGI("JPG ","%3d:%s",i,mozaics);
				mozaics[0]='\0';
			}
			// GLAYSCALE�̉�f���j���[�����l�b�g���[�N�̓��͕ϐ��ɑ��
			pp = 255 - ( rgb_data[i].r+rgb_data[i].g+rgb_data[i].b)/3  ;
			if (pp < 180) pp = 0;
			nn_input_buffer[i] = (unsigned char)pp ;
		}
	

    	// Infer image
    	int64_t infer_time = esp_timer_get_time();

	    nnablart_mainruntime_inference(_context);

        
    	// Fetch inference result

    	probs = nnablart_mainruntime_output_buffer(_context, 0);

		for (int class = 0; class < NNABLART_MAINRUNTIME_OUTPUT0_SIZE; class++) {
       		ESP_LOGI(TAG, "class=%d, probability:[%5.2f]\n",class,probs[class]);
        }
        int top_class = 0;
        float top_probability = 0.0f;
        for (int class = 0; class < NNABLART_MAINRUNTIME_OUTPUT0_SIZE; class++) {

            if (top_probability < probs[class]) {
                top_probability = probs[class];
                top_class = class;
            }
        }

		infer_time = (esp_timer_get_time() - infer_time) / 1000;
		ESP_LOGI(TAG, "Result %s  \n  Frame-time %ums (Inferrence-time %ums)",
        	Label[top_class], 
			(uint32_t)( (fb_get_time - start_time) / 1000 ), 
			(uint32_t)infer_time);

        dl_matrix3du_free(image_matrix);
    
	} while(1);

}

void app_dnn_main()
{
 	_context = nnablart_mainruntime_allocate_context(MainRuntime_parameters);
    	xTaskCreatePinnedToCore(task_process, "process", 4 * 1024, NULL, 6, NULL, 1);
}

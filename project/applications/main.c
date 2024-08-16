#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <msh.h>
#include <wlan_mgnt.h>
#include <wlan_prot.h>
#include <wlan_cfg.h>
#include <stdio.h>
#include <stdlib.h>
#include <drv_lcd.h>
#include <rttlogo.h>
#include "AHT10.h"
#define DBG_TAG "main"
#define DBG_LVL         DBG_LOG
#include <rtdbg.h>

#include "rtthread.h"
#include "dev_sign_api.h"
#include "mqtt_api.h"

#define PIN_BEEP        GET_PIN(B, 0) //蜂鸣器引脚号


//----------wifi------
#define WLAN_SSID "0YSLY0"
#define WLAN_PASSWORD "qwer1234"
#define NET_READY_TIME_OUT (rt_tick_from_millisecond(15 * 1000))
void wlan_scan_report_hander(int event,struct rt_wlan_buff *buff,void *parameter);
void wlan_scan_done_hander(int event,struct rt_wlan_buff *buff,void *parameter);
void wlan_ready_handler(int event, struct rt_wlan_buff *buff, void *parameter);
void wlan_station_disconnect_handler(int event, struct rt_wlan_buff *buff, void *parameter);
static void wlan_connect_handler(int event, struct rt_wlan_buff *buff, void *parameter);
static void wlan_connect_fail_handler(int event, struct rt_wlan_buff *buff, void *parameter);
static void print_wlan_information(struct rt_wlan_info *info,int index);
static int wifi_autoconnect(void);
static void print_wlan_information(struct rt_wlan_info *info,int index);
static int wifi_autoconnect(void);

static struct rt_semaphore net_ready;
static struct rt_semaphore scan_done;
//----------led,beep,aly---------------

#define STACK   1024
#define PRIOR   30
#define TICK    20
rt_thread_t led_thread_t;
rt_thread_t beep_thread_t;
rt_thread_t key_thread_t;
rt_thread_t aly_thread_t;
aht10_device_t dev;
void led_entry(void* parameter);
void beep_entry(void* parameter);
void aly_entry(void* parameter);
void key_entry(void* parameter);
//----------------------------------
char DEMO_PRODUCT_KEY[IOTX_PRODUCT_KEY_LEN + 1] = {0};
char DEMO_DEVICE_NAME[IOTX_DEVICE_NAME_LEN + 1] = {0};
char DEMO_DEVICE_SECRET[IOTX_DEVICE_SECRET_LEN + 1] = {0};
void *HAL_Malloc(uint32_t size);
void HAL_Free(void *ptr);
void HAL_Printf(const char *fmt, ...);
int HAL_GetProductKey(char product_key[IOTX_PRODUCT_KEY_LEN + 1]);
int HAL_GetDeviceName(char device_name[IOTX_DEVICE_NAME_LEN + 1]);
int HAL_GetDeviceSecret(char device_secret[IOTX_DEVICE_SECRET_LEN]);
uint64_t HAL_UptimeMs(void);
int HAL_Snprintf(char *str, const int len, const char *fmt, ...);
#define EXAMPLE_TRACE(fmt, ...)  \
    do { \
        HAL_Printf("%s|%03d :: ", __func__, __LINE__); \
        HAL_Printf(fmt, ##__VA_ARGS__); \
        HAL_Printf("%s", "\r\n"); \
    } while(0)

char payload []= "{\"params\":{\"temperature\":00.0}}";
int Warning_T = 40;

//------------------key-------------------
#define PIN_WK_DOWN        GET_PIN(C, 1)
#define PIN_WK_UP          GET_PIN(C, 5)

//---------------------------------------
int main(void)
{
    //----------------------------------------

    /* 设置按键引脚为输入模式 */
    rt_pin_mode(PIN_WK_DOWN, PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(PIN_WK_UP, PIN_MODE_INPUT_PULLUP);

    key_thread_t =  rt_thread_create("key",key_entry,NULL, STACK,PRIOR,TICK);

    /*---------LCD初始化-----------------------*/
    lcd_clear(WHITE);
    lcd_show_image(0, 0, 240, 69, image_rttlogo);
    lcd_set_color(WHITE, BLACK);
    lcd_draw_line(0, 69+20, 240, 69 +20);
    lcd_show_string(0,69+30,24,"temperature");
    lcd_show_string(12*11,69+30,24,":");
    lcd_show_string(12*14,69+30,24,".");


    lcd_show_string(0,69+70,24,"Warning_T");
    lcd_show_string(12*11,69+70,24,":");

    led_thread_t = rt_thread_create("led",led_entry,NULL, STACK,PRIOR,TICK);
    /*---------AHT10初始化-----------------------*/
    const char *i2c_bus_name = "i2c3";

    /* 等待传感器正常工作 */
    rt_thread_mdelay(2000);

    /* 初始化 aht10 */
    dev = aht10_init(i2c_bus_name);
    if (dev == RT_NULL)
    {
        LOG_E(" The sensor initializes failure");
        return 0;
    }

    //-----------蜂鸣器初始化----------------------------
    rt_pin_mode(PIN_BEEP, PIN_MODE_OUTPUT);
    beep_thread_t = rt_thread_create("beep",beep_entry,NULL, STACK,PRIOR,TICK);

    //------------wifi初始化-----------------------------
        static int i = 0;
        /* 等待 500 ms 以便 wifi 完成初始化 */
        rt_thread_mdelay(500);
        /* 扫描热点 */
       rt_kprintf("扫描热点\n");
        /* 执行扫描 */
       rt_sem_init(&scan_done,"scan_done",0,RT_IPC_FLAG_FIFO);
       rt_wlan_register_event_handler(RT_WLAN_EVT_SCAN_REPORT, wlan_scan_report_hander,&i);
        rt_wlan_register_event_handler(RT_WLAN_EVT_SCAN_DONE, wlan_scan_done_hander,RT_NULL);

       if(rt_wlan_scan() == RT_EOK)
        {
            LOG_D("the scan is started... ");
       }else
        {
           LOG_E("scan failed");
       }
        /*等待扫描完毕 */
       rt_sem_take(&scan_done,RT_WAITING_FOREVER);

        /* 热点连接 */
        rt_kprintf("热点连接 \n");
        rt_sem_init(&net_ready, "net_ready", 0, RT_IPC_FLAG_FIFO);

        /* 注册 wlan ready 回调函数 */
        rt_wlan_register_event_handler(RT_WLAN_EVT_READY, wlan_ready_handler, RT_NULL);
        /* 注册 wlan 断开回调函数 */
        rt_wlan_register_event_handler(RT_WLAN_EVT_STA_DISCONNECTED, wlan_station_disconnect_handler, RT_NULL);
        /* 同步连接热点 */
        rt_wlan_connect(WLAN_SSID, WLAN_PASSWORD);

        rt_thread_mdelay(5000);

//        LOG_D("ready to disconect from ap ...");
//        rt_wlan_disconnect();

        /* 自动连接 */


        rt_wlan_disconnect();
        rt_kprintf("测试自动连接 .....\n");
        wifi_autoconnect();

    //-----------------aly-----------------------------------
       aly_thread_t = rt_thread_create("aly",aly_entry,NULL, 1024*4,PRIOR,TICK);
    //------------开启线程----------------------------------
    rt_thread_startup(key_thread_t);//开启key线程
    rt_thread_startup(led_thread_t);//开启led线程
    rt_thread_startup(beep_thread_t);//开启beep线程
    rt_thread_mdelay(5000);
    rt_thread_startup(aly_thread_t);//开启aly线程

    return 0;
}


















void wlan_scan_report_hander(int event,struct rt_wlan_buff *buff,void *parameter)
{
    struct rt_wlan_info *info = RT_NULL;
    int index = 0;
    RT_ASSERT(event == RT_WLAN_EVT_SCAN_REPORT);
    RT_ASSERT(buff != RT_NULL);
    RT_ASSERT(parameter != RT_NULL);

    info = (struct rt_wlan_info *)buff->data;
    index = *((int *)(parameter));
    print_wlan_information(info,index);
    ++ *((int *)(parameter));
}

void wlan_scan_done_hander(int event,struct rt_wlan_buff *buff,void *parameter)
{
    RT_ASSERT(event == RT_WLAN_EVT_SCAN_DONE);
    rt_sem_release(&scan_done);
}

void wlan_ready_handler(int event, struct rt_wlan_buff *buff, void *parameter)
{
    rt_sem_release(&net_ready);
}

/* 断开连接回调函数 */

void wlan_station_disconnect_handler(int event, struct rt_wlan_buff *buff, void *parameter)
{
    rt_kprintf("断开连接 \n");
}


static void wlan_connect_handler(int event, struct rt_wlan_buff *buff, void *parameter)
{

    rt_kprintf("自动连接成功");
}

static void wlan_connect_fail_handler(int event, struct rt_wlan_buff *buff, void *parameter)
{

}

static void print_wlan_information(struct rt_wlan_info *info,int index)
{
        char *security;

        if(index == 0)
        {
            rt_kprintf("             SSID                      MAC            security    rssi chn Mbps\n");
            rt_kprintf("------------------------------- -----------------  -------------- ---- --- ----\n");
        }

        {
            rt_kprintf("%-32.32s", &(info->ssid.val[0]));
            rt_kprintf("%02x:%02x:%02x:%02x:%02x:%02x  ",
                    info->bssid[0],
                    info->bssid[1],
                    info->bssid[2],
                    info->bssid[3],
                    info->bssid[4],
                    info->bssid[5]
                    );
            switch (info->security)
            {
            case SECURITY_OPEN:
                security = "OPEN";
                break;
            case SECURITY_WEP_PSK:
                security = "WEP_PSK";
                break;
            case SECURITY_WEP_SHARED:
                security = "WEP_SHARED";
                break;
            case SECURITY_WPA_TKIP_PSK:
                security = "WPA_TKIP_PSK";
                break;
            case SECURITY_WPA_AES_PSK:
                security = "WPA_AES_PSK";
                break;
            case SECURITY_WPA2_AES_PSK:
                security = "WPA2_AES_PSK";
                break;
            case SECURITY_WPA2_TKIP_PSK:
                security = "WPA2_TKIP_PSK";
                break;
            case SECURITY_WPA2_MIXED_PSK:
                security = "WPA2_MIXED_PSK";
                break;
            case SECURITY_WPS_OPEN:
                security = "WPS_OPEN";
                break;
            case SECURITY_WPS_SECURE:
                security = "WPS_SECURE";
                break;
            default:
                security = "UNKNOWN";
                break;
            }
            rt_kprintf("%-14.14s ", security);
            rt_kprintf("%-4d ", info->rssi);
            rt_kprintf("%3d ", info->channel);
            rt_kprintf("%4d\n", info->datarate / 1000000);
        }
}

static int wifi_autoconnect(void)
{
    /* Configuring WLAN device working mode */
    rt_wlan_set_mode(RT_WLAN_DEVICE_STA_NAME, RT_WLAN_STATION);
    /* Start automatic connection */
    rt_wlan_config_autoreconnect(RT_TRUE);
    /* register event */
    rt_wlan_register_event_handler(RT_WLAN_EVT_STA_CONNECTED, wlan_connect_handler, RT_NULL);
    rt_wlan_register_event_handler(RT_WLAN_EVT_STA_CONNECTED_FAIL, wlan_connect_fail_handler, RT_NULL);
    return 0;
}
void led_entry(void* parameter)
{
    float temperature;
    int a,b,c;
    while(1)
    {
        temperature = aht10_read_temperature(dev);
        a = (int)temperature/10;
        b = (int)temperature%10;
        c = (int)(temperature * 10) % 10;
        lcd_show_num(12*12, 69+30, temperature, 2, 24);
        lcd_show_num(12*15, 69+30, (int)(temperature * 10) % 10, 2, 24);

        lcd_show_num(12*12, 69+70, Warning_T, 2, 24);
//        rt_kprintf("OK\n");
        rt_thread_mdelay(100);

        payload[25] =a+'0';
        payload[26] =b+'0';
        payload[28] =c+'0';
    }

}

void beep_entry(void* parameter)
{
    while(1)
    {
        float temperature;
        temperature = aht10_read_temperature(dev);
        if(temperature>Warning_T)
        {
            rt_pin_write(PIN_BEEP,PIN_HIGH);
        }
        else
        {
            rt_pin_write(PIN_BEEP,PIN_LOW);
        }

    }

}


void example_message_arrive(void *pcontext, void *pclient, iotx_mqtt_event_msg_pt msg)
{
    iotx_mqtt_topic_info_t     *topic_info = (iotx_mqtt_topic_info_pt) msg->msg;

    switch (msg->event_type) {
        case IOTX_MQTT_EVENT_PUBLISH_RECEIVED:
            /* print topic name and topic message */
            EXAMPLE_TRACE("Message Arrived:");
            EXAMPLE_TRACE("Topic  : %.*s", topic_info->topic_len, topic_info->ptopic);
            EXAMPLE_TRACE("Payload: %.*s", topic_info->payload_len, topic_info->payload);
            EXAMPLE_TRACE("\n");
            break;
        default:
            break;
    }
}

int example_subscribe(void *handle)
{
    int res = 0;
    const char *fmt = "/%s/%s/user/get";
    char *topic = NULL;
    int topic_len = 0;

    topic_len = strlen(fmt) + strlen(DEMO_PRODUCT_KEY) + strlen(DEMO_DEVICE_NAME) + 1;
    topic = HAL_Malloc(topic_len);
    if (topic == NULL) {
        EXAMPLE_TRACE("memory not enough");
        return -1;
    }
    memset(topic, 0, topic_len);
    HAL_Snprintf(topic, topic_len, fmt, DEMO_PRODUCT_KEY, DEMO_DEVICE_NAME);

    res = IOT_MQTT_Subscribe(handle, topic, IOTX_MQTT_QOS0, example_message_arrive, NULL);
    if (res < 0) {
        EXAMPLE_TRACE("subscribe failed");
        HAL_Free(topic);
        return -1;
    }

    HAL_Free(topic);
    return 0;
}

int example_publish(void *handle)
{
    int             res = 0;
    const char     *fmt = "/sys/%s/%s/thing/event/property/post";
    char           *topic = NULL;
    int             topic_len = 0;
    //-------------------------------------------------

//    char payload []= "{\"params\":{\"temperature\":00.9}}";
//    payload[25] ='0';
//    payload[26] ='6';
//      payload[28] ='6';
    //----------------------------------------------------
    topic_len = strlen(fmt) + strlen(DEMO_PRODUCT_KEY) + strlen(DEMO_DEVICE_NAME) + 1;
    topic = HAL_Malloc(topic_len);
    if (topic == NULL) {
        EXAMPLE_TRACE("memory not enough");
        return -1;
    }
    memset(topic, 0, topic_len);
    HAL_Snprintf(topic, topic_len, fmt, DEMO_PRODUCT_KEY, DEMO_DEVICE_NAME);

    res = IOT_MQTT_Publish_Simple(0, topic, IOTX_MQTT_QOS0, payload, strlen(payload));
    if (res < 0) {
        EXAMPLE_TRACE("publish failed, res = %d", res);
        HAL_Free(topic);
        return -1;
    }

    HAL_Free(topic);
    return 0;
}

void example_event_handle(void *pcontext, void *pclient, iotx_mqtt_event_msg_pt msg)
{
    EXAMPLE_TRACE("msg->event_type : %d", msg->event_type);
}



int mqtt_example_main()
{
    void                   *pclient = NULL;
    int                     res = 0;
    int                     loop_cnt = 0;
    iotx_mqtt_param_t       mqtt_params;

    HAL_GetProductKey(DEMO_PRODUCT_KEY);
    HAL_GetDeviceName(DEMO_DEVICE_NAME);
    HAL_GetDeviceSecret(DEMO_DEVICE_SECRET);

    EXAMPLE_TRACE("mqtt example");


    memset(&mqtt_params, 0x0, sizeof(mqtt_params));


    mqtt_params.handle_event.h_fp = example_event_handle;

    pclient = IOT_MQTT_Construct(&mqtt_params);
    if (NULL == pclient) {
        EXAMPLE_TRACE("MQTT construct failed");
        return -1;
    }

    res = example_subscribe(pclient);
    if (res < 0) {
        IOT_MQTT_Destroy(&pclient);
        return -1;
    }

    while (1) {
        if (0 == loop_cnt % 20) {
            example_publish(pclient);
        }

        IOT_MQTT_Yield(pclient, 200);

        loop_cnt += 1;
    }

    return 0;
}
void aly_entry(void* parameter)
{

     mqtt_example_main();
}
//
void key_entry(void* parameter)
{
    while(1)
    {
        if(rt_pin_read(PIN_WK_UP) ==PIN_LOW)
          {
              while(rt_pin_read(PIN_WK_UP) ==PIN_LOW);
              Warning_T++;
          }
        if(rt_pin_read(PIN_WK_DOWN) ==PIN_LOW)
          {
              while(rt_pin_read(PIN_WK_DOWN) ==PIN_LOW);
              Warning_T--;
          }
    }

}





















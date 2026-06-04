#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/i2c.h"

#include "esp_adc/adc_oneshot.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gatt.h"

#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
    
// ---------------- OUTPUT DEVICES ----------------
#define LED1 12
#define LED2 23
#define LED3 14
#define LED4 18
#define LED5 19

// ---------------- BUTTONS ----------------
#define BTN1 15
#define BTN2 2
#define BTN3 26
#define BTN4 27

// ---------------- LDR ----------------
#define LDR_ADC_CHANNEL ADC_CHANNEL_6   // GPIO34

// ---------------- I2C LCD ----------------
#define I2C_MASTER_SCL_IO       22
#define I2C_MASTER_SDA_IO       21
#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_FREQ_HZ      100000

#define LCD_ADDR                0x27
#define LCD_BACKLIGHT           0x08
#define ENABLE                  0x04
#define RS                      0x01

// ---------------- BLE ----------------
#define SERVICE_UUID      0xFFF0
#define WRITE_CHAR_UUID   0xFFF1
#define SENSOR_CHAR_UUID  0xFFF2

static const char *device_name = "ESP32_HOME";

static uint8_t own_addr_type;
static uint16_t sensor_val_handle = 0;
static int device_connected = 0;

adc_oneshot_unit_handle_t adc1_handle;

char sensor_data[100] = "T:29.0,H:62.0,L:2450,STATUS:BRIGHT,ALERT:NORMAL";

void ble_app_advertise(void);

// ---------------- LCD FUNCTIONS ----------------

void lcd_write_nibble(uint8_t nibble, uint8_t rs)
{
    uint8_t data = nibble | LCD_BACKLIGHT;

    if(rs)
    {
        data |= RS;
    }

    uint8_t data_en = data | ENABLE;

    i2c_master_write_to_device(
        I2C_MASTER_NUM,
        LCD_ADDR,
        &data_en,
        1,
        pdMS_TO_TICKS(100)
    );

    vTaskDelay(pdMS_TO_TICKS(2));
    i2c_master_write_to_device(
        I2C_MASTER_NUM,
        LCD_ADDR,
        &data,
        1,
        pdMS_TO_TICKS(100)
    );

   vTaskDelay(pdMS_TO_TICKS(2));
}

void lcd_send_byte(uint8_t data, uint8_t rs)
{
    lcd_write_nibble(data & 0xF0, rs);
    lcd_write_nibble((data << 4) & 0xF0, rs);
}

void lcd_send_cmd(uint8_t cmd)
{
    lcd_send_byte(cmd, 0);
}

void lcd_send_data(uint8_t data)
{
    lcd_send_byte(data, 1);
}

void lcd_send_string(const char *str)
{
    while(*str)
    {
        lcd_send_data((uint8_t)*str);
        str++;
    }
}

void lcd_clear(void)
{
    lcd_send_cmd(0x01);
    vTaskDelay(pdMS_TO_TICKS(5));
}

void lcd_init(void)
{
    vTaskDelay(pdMS_TO_TICKS(50));

    lcd_write_nibble(0x30, 0);
    vTaskDelay(pdMS_TO_TICKS(5));

    lcd_write_nibble(0x30, 0);
    vTaskDelay(pdMS_TO_TICKS(5));

    lcd_write_nibble(0x30, 0);
    vTaskDelay(pdMS_TO_TICKS(5));

    lcd_write_nibble(0x20, 0);
    vTaskDelay(pdMS_TO_TICKS(5));

    lcd_send_cmd(0x28);
    lcd_send_cmd(0x0C);
    lcd_send_cmd(0x06);
    lcd_clear();
}
void lcd_print_line(uint8_t line, const char *text)
{
    char buffer[17];

    snprintf(buffer, sizeof(buffer), "%-16s", text);

    if(line == 0)
    {
        lcd_send_cmd(0x80);
    }
    else
    {
        lcd_send_cmd(0xC0);
    }

    lcd_send_string(buffer);
}

void i2c_lcd_init(void)
{
    i2c_config_t conf =
    {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    i2c_param_config(I2C_MASTER_NUM, &conf);

    i2c_driver_install(
        I2C_MASTER_NUM,
        conf.mode,
        0,
        0,
        0
    );

    lcd_init();

    lcd_clear();
    lcd_send_cmd(0x80);
    lcd_send_string("ESP32 SMART");
    lcd_send_cmd(0xC0);
    lcd_send_string("BLE READY");
}

// ---------------- GPIO INIT ----------------

void output_devices_init(void)
{
    int leds[] = {LED1, LED2, LED3, LED4, LED5};

    for(int i = 0; i < 5; i++)
    {
        gpio_reset_pin(leds[i]);
        gpio_set_direction(leds[i], GPIO_MODE_OUTPUT);
        gpio_set_level(leds[i], 0);
    }
}

void buttons_init(void)
{
    int buttons[] = {BTN1, BTN2, BTN3, BTN4};

    for(int i = 0; i < 4; i++)
    {
        gpio_reset_pin(buttons[i]);
        gpio_set_direction(buttons[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(buttons[i], GPIO_PULLUP_ONLY);
    }
}

void ldr_adc_init(void)
{
    adc_oneshot_unit_init_cfg_t init_config =
    {
        .unit_id = ADC_UNIT_1,
    };

    adc_oneshot_new_unit(&init_config, &adc1_handle);

    adc_oneshot_chan_cfg_t config =
    {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };

    adc_oneshot_config_channel(
        adc1_handle,
        LDR_ADC_CHANNEL,
        &config
    );
}

// ---------------- DEVICE CONTROL ----------------

void set_device(const char *cmd)
{
    if(strcmp(cmd, "L1_ON") == 0)
    {
        gpio_set_level(LED1, 1);
        printf("Light 1 ON\n");
    }
    else if(strcmp(cmd, "L1_OFF") == 0)
    {
        gpio_set_level(LED1, 0);
        printf("Light 1 OFF\n");
    }
    else if(strcmp(cmd, "L2_ON") == 0)
    {
        gpio_set_level(LED2, 1);
        printf("Fan ON\n");
    }
    else if(strcmp(cmd, "L2_OFF") == 0)
    {
        gpio_set_level(LED2, 0);
        printf("Fan OFF\n");
    }
    else if(strcmp(cmd, "L3_ON") == 0)
    {
        gpio_set_level(LED3, 1);
        printf("TV ON\n");
    }
    else if(strcmp(cmd, "L3_OFF") == 0)
    {
        gpio_set_level(LED3, 0);
        printf("TV OFF\n");
    }
    else if(strcmp(cmd, "L4_ON") == 0)
    {
        gpio_set_level(LED4, 1);
        printf("AC ON\n");
    }
    else if(strcmp(cmd, "L4_OFF") == 0)
    {
        gpio_set_level(LED4, 0);
        printf("AC OFF\n");
    }
    else if(strcmp(cmd, "L5_ON") == 0)
    {
        gpio_set_level(LED5, 1);
        printf("Pump ON\n");
    }
    else if(strcmp(cmd, "L5_OFF") == 0)
    {
        gpio_set_level(LED5, 0);
        printf("Pump OFF\n");
    }
    else if(strcmp(cmd, "ALL_ON") == 0)
    {
        gpio_set_level(LED1, 1);
        gpio_set_level(LED2, 1);
        gpio_set_level(LED3, 1);
        gpio_set_level(LED4, 1);
        gpio_set_level(LED5, 1);
        printf("ALL DEVICES ON\n");
    }
    else if(strcmp(cmd, "ALL_OFF") == 0)
    {
        gpio_set_level(LED1, 0);
        gpio_set_level(LED2, 0);
        gpio_set_level(LED3, 0);
        gpio_set_level(LED4, 0);
        gpio_set_level(LED5, 0);
        printf("ALL DEVICES OFF\n");
    }
}

// ---------------- BLE CALLBACKS ----------------

static int write_callback(
    uint16_t conn_handle,
    uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt,
    void *arg
)
{
    char data[30];

    int len = ctxt->om->om_len;

    if(len >= sizeof(data))
    {
        len = sizeof(data) - 1;
    }

    memcpy(data, ctxt->om->om_data, len);
    data[len] = '\0';

    printf("Received Data: %s\n", data);

    set_device(data);

    return 0;
}

static int sensor_callback(
    uint16_t conn_handle,
    uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt,
    void *arg
)
{
    os_mbuf_append(ctxt->om, sensor_data, strlen(sensor_data));

    return 0;
}

static const struct ble_gatt_svc_def gatt_svcs[] =
{
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(SERVICE_UUID),

        .characteristics = (struct ble_gatt_chr_def[])
        {
            {
                .uuid = BLE_UUID16_DECLARE(WRITE_CHAR_UUID),
                .access_cb = write_callback,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },

            {
                .uuid = BLE_UUID16_DECLARE(SENSOR_CHAR_UUID),
                .access_cb = sensor_callback,
                .val_handle = &sensor_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },

            {0}
        },
    },

    {0}
};

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch(event->type)
    {
        case BLE_GAP_EVENT_CONNECT:
            if(event->connect.status == 0)
            {
                printf("BLE Connected\n");
                device_connected = 1;
            }
            else
            {
                printf("BLE Connect Failed\n");
                ble_app_advertise();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            printf("BLE Disconnected\n");
            device_connected = 0;
            ble_app_advertise();
            break;

        default:
            break;
    }

    return 0;
}

void ble_app_on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);

    if(rc != 0)
    {
        printf("BLE address infer failed, rc=%d\n", rc);
        return;
    }

    printf("BLE address configured\n");

    ble_app_advertise();
}

void ble_app_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;

    memset(&fields, 0, sizeof(fields));

    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;

    ble_gap_adv_set_fields(&fields);

    memset(&adv_params, 0, sizeof(adv_params));

    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    ble_gap_adv_start(
        own_addr_type,
        NULL,
        BLE_HS_FOREVER,
        &adv_params,
        ble_gap_event,
        NULL
    );

    printf("BLE Advertising Started: %s\n", device_name);
}

void host_task(void *param)
{
    nimble_port_run();
}

// ---------------- SENSOR + LCD TASK ----------------

void sensor_task(void *arg)
{
    while(1)
    {
        int ldr_value = 2450;

        adc_oneshot_read(
            adc1_handle,
            LDR_ADC_CHANNEL,
            &ldr_value
        );

        float temperature = 29.0;
        float humidity = 62.0;

        char light_status[10];

        if(ldr_value < 1500)
        {
            strcpy(light_status, "DARK");
        }
        else
        {
            strcpy(light_status, "BRIGHT");
        }

      char alert_status[5] = "N";

if(gpio_get_level(BTN1) == 0)
{
    strcpy(alert_status, "B1");
}
else if(gpio_get_level(BTN2) == 0)
{
    strcpy(alert_status, "B2");
}
else if(gpio_get_level(BTN3) == 0)
{
    strcpy(alert_status, "EM");
}
else if(gpio_get_level(BTN4) == 0)
{
    strcpy(alert_status, "RS");
}
      snprintf(
    sensor_data,
    sizeof(sensor_data),
    "T%.0f,H%.0f,L%d,S%s,A%s",
    temperature,
    humidity,
    ldr_value,
    light_status,
    alert_status
);
        printf("Temperature = %.1f C\n", temperature);
        printf("Humidity    = %.1f %%\n", humidity);
        printf("LDR Value   = %d\n", ldr_value);
        printf("Light       = %s\n", light_status);
        printf("Alert       = %s\n", alert_status);
      printf("BLE Data    = %s\n", sensor_data);
printf("-----------------------------\n");

char line1[17];
char line2[17];

snprintf(line1, sizeof(line1), "T:%2.0fC H:%2.0f%%", temperature, humidity);
snprintf(line2, sizeof(line2), "L:%4d %s", ldr_value, light_status);

lcd_print_line(0, line1);
lcd_print_line(1, line2);

if(device_connected)
{
    ble_gatts_chr_updated(sensor_val_handle);
}

vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ---------------- MAIN ----------------

void app_main(void)
{
    printf("ESP32 Final Smart Home Project Started\n");

    output_devices_init();

    buttons_init();

    ldr_adc_init();

    i2c_lcd_init();

    nimble_port_init();

    ble_svc_gap_init();
    ble_svc_gatt_init();

    ble_svc_gap_device_name_set(device_name);

    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    ble_hs_cfg.sync_cb = ble_app_on_sync;

    nimble_port_freertos_init(host_task);

    xTaskCreate(
        sensor_task,
        "sensor_task",
        4096,
        NULL,
        5,
        NULL
    );
}

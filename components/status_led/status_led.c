#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "status_led.h"

#if CONFIG_KBD_LED_WS2812
#include "driver/rmt_tx.h"
#else
#include "driver/ledc.h"
#endif

/* Common brightness scale (13-bit, matching the previous LEDC resolution). */
#define LED_MAX_BRIGHTNESS 8191

#if CONFIG_KBD_LED_WS2812
#define WS2812_RES_HZ 10000000 /* 0.1 us per RMT tick */
static rmt_channel_handle_t s_rmt_chan;
static rmt_encoder_handle_t s_rmt_encoder;
#else
#define LEDC_TIMER_NUM   LEDC_TIMER_0
#define LEDC_CHANNEL     LEDC_CHANNEL_0
#define LEDC_FREQ_HZ     5000
#define LEDC_RESOLUTION  LEDC_TIMER_13_BIT
static bool s_active_low;
#endif

#define BREATH_PERIOD_MS 2000   /* one full breathing cycle */
#define BLINK_PERIOD_MS  500    /* 2 Hz blink: 250 ms on, 250 ms off */
#define ERROR_PERIOD_MS  100    /* 10 Hz red error flash: 50 ms on/off */
#define STEP_MS          10

static const char *TAG = "status_led";

static volatile led_mode_t s_mode = LED_MODE_BOOT;

static void led_task(void *arg);

#if CONFIG_KBD_LED_WS2812

static esp_err_t ws2812_init(int gpio)
{
    rmt_tx_channel_config_t chan_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = gpio,
        .mem_block_symbols = 64,
        .resolution_hz = WS2812_RES_HZ,
        .trans_queue_depth = 4,
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&chan_cfg, &s_rmt_chan), TAG, "rmt channel");

    /* WS2812: '0' = 0.4 us high / 0.9 us low, '1' = 0.8 us high / 0.5 us low */
    rmt_bytes_encoder_config_t enc_cfg = {
        .bit0 = {
            .level0 = 1, .duration0 = 4,
            .level1 = 0, .duration1 = 9,
        },
        .bit1 = {
            .level0 = 1, .duration0 = 8,
            .level1 = 0, .duration1 = 5,
        },
        .flags = { .msb_first = 1 },
    };
    ESP_RETURN_ON_ERROR(rmt_new_bytes_encoder(&enc_cfg, &s_rmt_encoder), TAG, "rmt encoder");
    return rmt_enable(s_rmt_chan);
}

/* Single WS2812 pixel: white normally, red for errors. Idle-low between
 * frames supplies the >50 us reset pulse. */
static void ws2812_write(uint32_t brightness, bool red)
{
    uint8_t level = (uint8_t)((brightness * 255u) / LED_MAX_BRIGHTNESS);
    uint8_t grb[3] = { level, level, level }; /* wire order is G, R, B */
    if (red) {
        grb[0] = 0;
        grb[1] = level;
        grb[2] = 0;
    }
    rmt_transmit_config_t tx_cfg = { .loop_count = 0 };
    if (rmt_transmit(s_rmt_chan, s_rmt_encoder, grb, sizeof(grb), &tx_cfg) == ESP_OK) {
        rmt_tx_wait_all_done(s_rmt_chan, 100);
    }
}

#endif /* CONFIG_KBD_LED_WS2812 */

esp_err_t status_led_start(int gpio, bool active_low)
{
#if CONFIG_KBD_LED_WS2812
    (void)active_low;
    ESP_RETURN_ON_ERROR(ws2812_init(gpio), TAG, "ws2812 init");
    ws2812_write(0, false); /* off until the task takes over */
#else
    s_active_low = active_low;

    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_RESOLUTION,
        .timer_num = LEDC_TIMER_NUM,
        .freq_hz = LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "ledc timer");

    ledc_channel_config_t ch = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER_NUM,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = gpio,
        .duty = active_low ? LED_MAX_BRIGHTNESS : 0, /* off */
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ch), TAG, "ledc channel");
#endif

    BaseType_t ok = xTaskCreate(led_task, "status_led", 2048, NULL, tskIDLE_PRIORITY + 2, NULL);
    return ok == pdTRUE ? ESP_OK : ESP_FAIL;
}

void status_led_set_mode(led_mode_t mode)
{
    s_mode = mode;
}

static void apply_duty(uint32_t brightness, bool red) /* 0..LED_MAX_BRIGHTNESS, 0 = dark */
{
#if CONFIG_KBD_LED_WS2812
    ws2812_write(brightness, red);
#else
    (void)red; /* a plain LED can only show brightness, not colour */
    uint32_t duty = s_active_low ? LED_MAX_BRIGHTNESS - brightness : brightness;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL);
#endif
}

static void led_task(void *arg)
{
    uint32_t t_ms = 0;
    while (1) {
        led_mode_t mode = s_mode;
        uint32_t brightness;
        switch (mode) {
        case LED_MODE_BOOT:
            brightness = LED_MAX_BRIGHTNESS;
            break;
        case LED_MODE_WAITING: {
            /* half-wave sine: smooth fade 0 -> max -> 0 */
            float phase = (float)(t_ms % BREATH_PERIOD_MS) / (float)BREATH_PERIOD_MS; /* 0..1 */
            brightness = (uint32_t)((1.0f - cosf(phase * 2.0f * (float)M_PI)) * 0.5f * LED_MAX_BRIGHTNESS);
            break;
        }
        case LED_MODE_ERROR:
            /* fast red flash: 10 Hz */
            brightness = (t_ms % ERROR_PERIOD_MS) < (ERROR_PERIOD_MS / 2) ? LED_MAX_BRIGHTNESS : 0;
            break;
        case LED_MODE_ACTIVE:
        default:
            brightness = (t_ms % BLINK_PERIOD_MS) < (BLINK_PERIOD_MS / 2) ? LED_MAX_BRIGHTNESS : 0;
            break;
        }
        apply_duty(brightness, mode == LED_MODE_ERROR);
        vTaskDelay(pdMS_TO_TICKS(STEP_MS));
        t_ms += STEP_MS;
    }
}

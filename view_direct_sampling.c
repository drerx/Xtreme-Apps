/* Copyright (C) 2022-2023 Salvatore Sanfilippo -- All Rights Reserved
 * See the LICENSE file for information about the license. */

#include "app.h"

#include <cc1101.h>

#define CAPTURED_BITMAP_SIZE 128*64/8
#define DEFAULT_USEC_PER_PIXEL 50
#define USEC_PER_PIXEL_SMALL_CHANGE 5
#define USEC_PER_PIXEL_LARGE_CHANGE 25
#define USEC_PER_PIXEL_MIN 5
#define USEC_PER_PIXEL_MAX 300
typedef struct {
    uint8_t *captured; // Bitmap with the last captured screen.
    uint32_t usec_per_pixel; // Number of useconds a pixel should represent
} DirectSamplingViewPrivData;

/* Read directly from the G0 CC1101 pin, and draw a black or white
 * dot depending on the level. */
void render_view_direct_sampling(Canvas *const canvas, ProtoViewApp *app) {
    DirectSamplingViewPrivData *privdata = app->view_privdata;

    if (!app->direct_sampling_enabled && privdata->captured == NULL) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas,2,9,"Direct sampling is a special");
        canvas_draw_str(canvas,2,18,"mode that displays the signal");
        canvas_draw_str(canvas,2,27,"captured in real time. Like in");
        canvas_draw_str(canvas,2,36,"a old CRT TV. It's very slow.");
        canvas_draw_str(canvas,2,45,"Can crash your Flipper.");
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas,14,60,"To enable press OK");
        return;
    }

    /* Allocate the bitmap only the first time. */
    if (privdata->captured == NULL)
        privdata->captured = malloc(CAPTURED_BITMAP_SIZE);

    /* Read from data from GPIO */
    if (app->direct_sampling_enabled) {
        for (int j = 0; j < CAPTURED_BITMAP_SIZE*8; j++) {
            uint32_t start_time = DWT->CYCCNT;
            bool level = furi_hal_gpio_read(&gpio_cc1101_g0);
            bitmap_set(privdata->captured,CAPTURED_BITMAP_SIZE,j,level);
            uint32_t period =
                furi_hal_cortex_instructions_per_microsecond() *
                privdata->usec_per_pixel;
            while(DWT->CYCCNT - start_time < period);
        }
    }

    /* Draw on screen. */
    int idx = 0;
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 128; x++) {
            bool level = bitmap_get(privdata->captured,
                CAPTURED_BITMAP_SIZE,idx++);
            if (level) canvas_draw_dot(canvas,x,y);
        }
    }

    char buf[32];
    snprintf(buf,sizeof(buf),"%lu usec/px", privdata->usec_per_pixel);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_with_border(canvas,0,60,buf,ColorWhite,ColorBlack);
}

/* Handle input */
void process_input_direct_sampling(ProtoViewApp *app, InputEvent input) {
    DirectSamplingViewPrivData *privdata = app->view_privdata;

    if (input.type == InputTypePress && input.key == InputKeyOk) {
        app->direct_sampling_enabled = !app->direct_sampling_enabled;
    }

    if ((input.key == InputKeyUp || input.key == InputKeyDown) &&
        (input.type == InputTypePress || input.type == InputTypeRepeat))
    {
        uint32_t change = input.type == InputTypePress ?
            USEC_PER_PIXEL_SMALL_CHANGE :
            USEC_PER_PIXEL_LARGE_CHANGE;
        if (input.key == InputKeyUp) change = -change;
        privdata->usec_per_pixel += change;
        if (privdata->usec_per_pixel < USEC_PER_PIXEL_MIN)
            privdata->usec_per_pixel = USEC_PER_PIXEL_MIN;
        else if (privdata->usec_per_pixel > USEC_PER_PIXEL_MAX)
            privdata->usec_per_pixel = USEC_PER_PIXEL_MAX;
    }
}

/* Enter view. Stop the subghz thread to prevent access as we read
 * the CC1101 data directly. */
void view_enter_direct_sampling(ProtoViewApp *app) {
    /* Set view defaults. */
    DirectSamplingViewPrivData *privdata = app->view_privdata;
    privdata->usec_per_pixel = DEFAULT_USEC_PER_PIXEL;

    if (app->txrx->txrx_state == TxRxStateRx &&
        !app->txrx->debug_timer_sampling)
    {
        furi_hal_subghz_stop_async_rx();

        /* To read data asynchronously directly from the view, we need
         * to put the CC1101 back into reception mode (the previous call
         * to stop the async RX will put it into idle) and configure the
         * G0 pin for reading. */
        furi_hal_subghz_rx();
        furi_hal_gpio_init(&gpio_cc1101_g0, GpioModeInput, GpioPullNo,
                           GpioSpeedLow);
    } else {
        raw_sampling_worker_stop(app);
    }
}

/* Exit view. Restore the subghz thread. */
void view_exit_direct_sampling(ProtoViewApp *app) {
    DirectSamplingViewPrivData *privdata = app->view_privdata;
    if (privdata->captured) free(privdata->captured);
    app->direct_sampling_enabled = false;

    /* Restart normal data feeding. */
    if (app->txrx->txrx_state == TxRxStateRx &&
        !app->txrx->debug_timer_sampling)
    {
        furi_hal_subghz_start_async_rx(protoview_rx_callback, NULL);
    } else {
        raw_sampling_worker_start(app);
    }
}

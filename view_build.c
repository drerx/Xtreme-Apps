/* Copyright (C) 2022-2023 Salvatore Sanfilippo -- All Rights Reserved
 * See the LICENSE file for information about the license. */

#include "app.h"

extern ProtoViewDecoder *Decoders[];    // Defined in signal.c.

/* Our view private data. */
typedef struct {
    ProtoViewDecoder *decoder;      // Decoder we are using to create a message.
    uint32_t cur_decoder;           // Decoder index when we are yet selecting
                                    // a decoder. Used when decoder is NULL.
    ProtoViewFieldSet *fieldset;    // The fields to populate.
    uint32_t cur_field;             // Field we are editing right now. This
                                    // is the index inside the 'fieldset'
                                    // fields.
} BuildViewPrivData;

/* Not all the decoders support message bulding, so we can't just
 * increment / decrement the cur_decoder index here. */
static void select_next_decoder(ProtoViewApp *app) {
    BuildViewPrivData *privdata = app->view_privdata;
    do { 
        privdata->cur_decoder++;
        if (Decoders[privdata->cur_decoder] == NULL)
            privdata->cur_decoder = 0;
    } while(Decoders[privdata->cur_decoder]->get_fields == NULL);
}

/* Like select_next_decoder() but goes backward. */
static void select_prev_decoder(ProtoViewApp *app) {
    BuildViewPrivData *privdata = app->view_privdata;
    do {
        if (privdata->cur_decoder == 0) {
            /* Go one after the last one to wrap around. */
            while(Decoders[privdata->cur_decoder]) privdata->cur_decoder++;
        }
        privdata->cur_decoder--;
    } while(Decoders[privdata->cur_decoder]->get_fields == NULL);
}

/* Render the view to select the decoder, among the ones that
 * support message building. */
static void render_view_select_decoder(Canvas *const canvas, ProtoViewApp *app) {
    BuildViewPrivData *privdata = app->view_privdata;
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 9, "Signal builder");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 19, "up/down: select, ok: choose");

    // When entering the view, the current decoder is just set to zero.
    // Seek the next valid if needed.
    if (Decoders[privdata->cur_decoder]->get_fields == NULL)
        select_next_decoder(app);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 9, "Signal builder");

    canvas_draw_str_aligned(canvas,64,40,AlignCenter,AlignCenter,
        Decoders[privdata->cur_decoder]->name);
}

/* Render the view that allows the user to populate the fields needed
 * for the selected decoder to build a message. */
static void render_view_set_fields(Canvas *const canvas, ProtoViewApp *app) {
    BuildViewPrivData *privdata = app->view_privdata;
    char buf[32];
    snprintf(buf,sizeof(buf), "%s field %d/%d",
        privdata->decoder->name, (int)privdata->cur_field,
        (int)privdata->fieldset->numfields);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 9, buf);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 19, "up/down: next field, ok: edit");
    canvas_draw_str(canvas, 0, 62, "Long press ok: create signal");
}

/* Render the build message view. */
void render_view_build_message(Canvas *const canvas, ProtoViewApp *app) {
    BuildViewPrivData *privdata = app->view_privdata;

    if (privdata->decoder)
        render_view_set_fields(canvas,app);
    else
        render_view_select_decoder(canvas,app);
}

/* Handle input for the decoder selection. */
static void process_input_select_decoder(ProtoViewApp *app, InputEvent input) {
    BuildViewPrivData *privdata = app->view_privdata;
    if (input.type == InputTypeShort) {
        if (input.key == InputKeyOk) {
            privdata->decoder = Decoders[privdata->cur_decoder];
            privdata->fieldset = fieldset_new();
            privdata->decoder->get_fields(privdata->fieldset);
        } else if (input.key == InputKeyDown) {
            select_next_decoder(app);
        } else if (input.key == InputKeyUp) {
            select_prev_decoder(app);
        }
    }
}

/* Handle input for fields editing mode. */
static void process_input_set_fields(ProtoViewApp *app, InputEvent input) {
    UNUSED(app);
    UNUSED(input);
}

/* Handle input for the build message view. */
void process_input_build_message(ProtoViewApp *app, InputEvent input) {
    BuildViewPrivData *privdata = app->view_privdata;
    if (privdata->decoder)
        process_input_set_fields(app,input);
    else
        process_input_select_decoder(app,input);
}

/* Called on exit for cleanup. */
void view_exit_build_message(ProtoViewApp *app) {
    BuildViewPrivData *privdata = app->view_privdata;
    if (privdata->fieldset) fieldset_free(privdata->fieldset);
}

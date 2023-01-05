/* Copyright (C) 2023 Salvatore Sanfilippo -- All Rights Reserved
 * See the LICENSE file for information about the license. */

#include <furi.h>
#include <furi_hal.h>
#include <input/input.h>
#include <gui/gui.h>
#include <stdlib.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <math.h>

#define TAG "Asteroids" // Used for logging
#define DEBUG_MSG 1
#define SCREEN_XRES 128
#define SCREEN_YRES 64
#ifndef PI
#define PI 3.14159265358979f
#endif

/* ============================ Data structures ============================= */

typedef struct Ship {
    float x,            /* Ship x position. */
    y,                  /* Ship y position. */
    vx,                 /* x velocity. */
    vy,                 /* y velocity. */
    rot;                /* Current rotation. 2*PI full ortation. */
} Ship;

typedef struct Bullet {
    float x, y, vx, vy;     /* Fields like in ship. */
    uint32_t ttl;           /* Time to live, in ticks. */
} Bullet;

typedef struct Asteroid {
    float x, y, vx, vy, rot,    /* Fields like ship. */
    rot_speed,                  /* Angular velocity (rot speed and sense). */
    size;                       /* Asteroid size. */
    uint8_t shape_seed;         /* Seed to give random shape. */
} Asteroid;

#define MAXBUL 10   /* Max bullets on the screen. */
#define MAXAST 8    /* Max asteroids on the screen. */
typedef struct AsteroidsApp {
    /* GUI */
    Gui *gui;
    ViewPort *view_port;     /* We just use a raw viewport and we render
                                everything into the low level canvas. */
    FuriMessageQueue *event_queue;  /* Keypress events go here. */

    /* Game state. */
    int running;            /* Once false exists the app. */
    uint32_t ticks;         /* Game ticks. Increments at each refresh. */

    /* Ship state. */
    struct Ship ship;

    /* Bullets state. */
    struct Bullet bullets[MAXBUL];  /* Each bullet state. */
    int bullets_num;            /* Active bullets. */
    uint32_t last_bullet_tick;  /* Tick the last bullet was fired. */

    /* Asteroids state. */
    Asteroid asteroids[MAXAST];     /* Each asteroid state. */
    int asteroids_num;              /* Active asteroids. */

    uint32_t pressed[InputKeyMAX]; /* pressed[id] is true if pressed.
                                      Each array item contains the time
                                      in milliseconds the key was pressed. */
    bool fire;                 /* Short press detected: fire a bullet. */
} AsteroidsApp;

/* ============================ 2D drawing ================================== */

/* This structure represents a polygon of at most POLY_MAX points.
 * The function draw_poly() is able to render it on the screen, rotated
 * by the amount specified. */
#define POLY_MAX 8
typedef struct Poly {
    float x[POLY_MAX];
    float y[POLY_MAX];
    uint32_t points; /* Number of points actually populated. */
} Poly;

/* Define the polygons we use. */
Poly ShipPoly = {
    {-3, 0, 3},
    {-3, 6, -3},
    3
};

/* Rotate the point of the poligon 'poly' and store the new rotated
 * polygon in 'rot'. The polygon is rotated by an angle 'a', with
 * center at 0,0. */
void rotate_poly(Poly *rot, Poly *poly, float a) {
    /* We want to compute sin(a) and cos(a) only one time
     * for every point to rotate. It's a slow operation. */
    float sin_a = (float)sin(a);
    float cos_a = (float)cos(a);
    for (uint32_t j = 0; j < poly->points; j++) {
        rot->x[j] = poly->x[j]*cos_a - poly->y[j]*sin_a;
        rot->y[j] = poly->y[j]*cos_a + poly->x[j]*sin_a;
    }
    rot->points = poly->points;
}

/* This is an 8 bit LFSR we use to generate a predictable and fast
 * pseudorandom sequence of numbers, to give a different shape to
 * each asteroid. */
void lfsr_next(unsigned char *prev) {
    unsigned char lsb = *prev & 1;
    *prev = *prev >> 1;
    if (lsb == 1) *prev ^= 0b11000111;
    *prev ^= *prev<<7; /* Mix things a bit more. */
}

/* Render the polygon 'poly' at x,y, rotated by the specified angle. */
void draw_poly(Canvas *const canvas, Poly *poly, uint8_t x, uint8_t y, float a)
{
    Poly rot;
    rotate_poly(&rot,poly,a);
    canvas_set_color(canvas, ColorBlack);
    for (uint32_t j = 0; j < rot.points; j++) {
        uint32_t a = j;
        uint32_t b = j+1;
        if (b == rot.points) b = 0;
        canvas_draw_line(canvas,x+rot.x[a],y+rot.y[a],
                                x+rot.x[b],y+rot.y[b]);
    }
}

/* A bullet is just a + pixels pattern. A single pixel is not
 * visible enough. */
void draw_bullet(Canvas *const canvas, Bullet *b) {
    canvas_draw_dot(canvas,b->x-1,b->y);
    canvas_draw_dot(canvas,b->x+1,b->y);
    canvas_draw_dot(canvas,b->x,b->y);
    canvas_draw_dot(canvas,b->x,b->y-1);
    canvas_draw_dot(canvas,b->x,b->y+1);
}

/* Draw an asteroid. The asteroid shapes is computed on the fly and
 * is not stored in a permanent shape structure. In order to generate
 * the shape, we use an initial fixed shape that we resize according
 * to the asteroid size, perturbate according to the asteroid shape
 * seed, and finally draw it rotated of the right amount. */
void draw_asteroid(Canvas *const canvas, Asteroid *ast) {
    Poly ap;

    /* Start with what is kinda of a circle. Note that this could be
     * stored into a template and copied here, to avoid computing
     * sin() / cos(). But the Flipper can handle it without problems. */
    uint8_t r = ast->shape_seed;
    for (int j = 0; j < 8; j++) {
        float a = (PI*2)/8*j;

        /* Before generating the point, to make the shape unique generate
         * a random factor between .7 and 1.3 to scale the distance from
         * the center. However this asteroid should have its unique shape
         * that remains always the same, so we use a predictable PRNG
         * implemented by an 8 bit shift register. */
        lfsr_next(&r);
        float scaling = .7+((float)r/255*.6);

        ap.x[j] = (float)sin(a) * ast->size * scaling;
        ap.y[j] = (float)cos(a) * ast->size * scaling;
    }
    ap.points = 8;
    draw_poly(canvas,&ap,ast->x,ast->y,ast->rot);
}

/* Given the current position, update it according to the velocity and
 * wrap it back to the other side if the object went over the screen. */
void update_pos_by_velocity(float *x, float *y, float vx, float vy) {
    /* Return back from one side to the other of the screen. */
    *x += vx;
    *y += vy;
    if (*x >= SCREEN_XRES) *x = 0;
    else if (*x < 0) *x = SCREEN_XRES-1;
    if (*y >= SCREEN_YRES) *y = 0;
    else if (*y < 0) *y = SCREEN_YRES-1;
}

/* Render the current game screen. */
void render_callback(Canvas *const canvas, void *ctx) {
    AsteroidsApp *app = ctx;

    /* Clear screen. */
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 0, 0, 127, 63);

    /* Draw ship, asteroids, bullets. */
    draw_poly(canvas,&ShipPoly,app->ship.x,app->ship.y,app->ship.rot);

    for (int j = 0; j < app->bullets_num; j++)
        draw_bullet(canvas,&app->bullets[j]);

    for (int j = 0; j < app->asteroids_num; j++)
        draw_asteroid(canvas,&app->asteroids[j]);
}

/* ============================ Game logic ================================== */

float distance(float x1, float y1, float x2, float y2) {
    float dx = x1-x2;
    float dy = y1-y2;
    return sqrt(dx*dx+dy*dy);
}

/* Detect a collision between the object at x1,y1 of radius r1 and
 * the object at x2, y2 of radius r2. A factor < 1 will make the
 * function detect the collision even if the objects are yet not
 * relly touching, while a factor > 1 will make it detect the collision
 * only after they are a bit overlapping. It basically is used to
 * rescale the distance.
 *
 * Note that in this simplified 2D world, objects are all considered
 * spheres (this is why this function only takes the radius). This
 * is, after all, kinda accurate for asteroids, for bullets, and
 * even for the ship "core" itself. */
bool detect_collision(float x1, float y1, float r1,
                      float x2, float y2, float r2,
                      float factor)
{
    /* The objects are colliding if the distance between object 1 and 2
     * is smaller than the sum of the two radiuses r1 and r2.
     * So it would be like: sqrt((x1-x2)^2+(y1-y2)^2) < r1+r2.
     * However we can avoid computing the sqrt (which is slow) by
     * squaring the second term and removing the square root, making
     * the comparison like this:
     *
     * (x1-x2)^2+(y1-y2)^2 < (r1+r2)^2. */
    float dx = (x1-x2)*factor;
    float dy = (y1-y2)*factor;
    float rsum = r1+r2;
    return dx*dx+dy*dy < rsum*rsum;
}

/* Create a new bullet headed in the same direction of the ship. */
void ship_fire_bullet(AsteroidsApp *app) {
    if (app->bullets_num == MAXBUL) return;
    Bullet *b = &app->bullets[app->bullets_num];
    b->x = app->ship.x;
    b->y = app->ship.y;
    b->vx = -sin(app->ship.rot);
    b->vy = cos(app->ship.rot);

    /* Ship should fire from its head, not in the middle. */
    b->x += b->vx*5;
    b->y += b->vy*5;

    /* Give the bullet some velocity (for now the vector is just
     * normalized to 1). */
    b->vx *= 2;
    b->vy *= 2;

    /* It's more realistic if we add the velocity vector of the
     * ship, too. Otherwise if the ship is going fast the bullets
     * will be slower, which is not how the world works. */
    b->vx += app->ship.vx;
    b->vy += app->ship.vy;

    b->ttl = 50; /* The bullet will disappear after N ticks. */
    app->bullets_num++;
}

/* Remove the specified bullet by id (index in the array). */
void remove_bullet(AsteroidsApp *app, int bid) {
    /* Replace the top bullet with the empty space left
     * by the removal of this bullet. This way we always take the
     * array dense, which is an advantage when looping. */
    int n = --app->bullets_num;
    if (n && bid != n) app->bullets[bid] = app->bullets[n];
}

/* Create a new asteroid, away from the ship. */
void add_asteroid(AsteroidsApp *app) {
    if (app->asteroids_num == MAXAST) return;
    float size = 4+rand()%15;
    float min_distance = 20;
    float x,y;
    do {
        x = rand() % SCREEN_XRES;
        y = rand() % SCREEN_YRES;
    } while(distance(app->ship.x,app->ship.y,x,y) < min_distance+size);
    Asteroid *a = &app->asteroids[app->asteroids_num++];
    a->x = x;
    a->y = y;
    a->vx = ((float)rand()/RAND_MAX);
    a->vy = ((float)rand()/RAND_MAX);
    a->size = size;
    a->rot = 0;
    a->rot_speed = ((float)rand()/RAND_MAX)/10;
    if (app->ticks & 1) a->rot_speed = -(a->rot_speed);
    a->shape_seed = rand() & 255;
}

/* Remove the specified asteroid by id (index in the array). */
void remove_asteroid(AsteroidsApp *app, int id) {
    /* Replace the top asteroid with the empty space left
     * by the removal of this one. This way we always take the
     * array dense, which is an advantage when looping. */
    int n = --app->asteroids_num;
    if (n && id != n) app->asteroids[id] = app->asteroids[n];
}

/* This is the main game execution function, called 10 times for
 * second (with the Flipper screen latency, an higher FPS does not
 * make sense). In this function we update the position of objects based
 * on velocity. Detect collisions. Update the score and so forth.
 *
 * Each time this function is called, app->tick is incremented. */
void game_tick(void *ctx) {
    AsteroidsApp *app = ctx;

    /* Handle keypresses. */
    if (app->pressed[InputKeyLeft]) app->ship.rot -= .35;
    if (app->pressed[InputKeyRight]) app->ship.rot += .35;
    if (app->pressed[InputKeyOk]) {
        app->ship.vx -= 0.35*(float)sin(app->ship.rot);
        app->ship.vy += 0.35*(float)cos(app->ship.rot);
    }

    /* Fire a bullet if needed. app->fire is set in
     * asteroids_update_keypress_state() since depends on exact
     * pressure timing. */
    if (app->fire) {
        ship_fire_bullet(app);
        app->fire = false;
    }

    /* Update ship position according to its velocity. */
    update_pos_by_velocity(&app->ship.x,&app->ship.y,app->ship.vx,app->ship.vy);

    /* Update bullets position. */
    for (int j = 0; j < app->bullets_num; j++) {
        update_pos_by_velocity(&app->bullets[j].x,&app->bullets[j].y,
                               app->bullets[j].vx,app->bullets[j].vy);
        if (--app->bullets[j].ttl == 0) {
            remove_bullet(app,j);
            j--; /* Process this bullet index again: the removal will
                    fill it with the top bullet to take the array dense. */
        }
    }

    /* Update asteroids position. */
    for (int j = 0; j < app->asteroids_num; j++) {
        update_pos_by_velocity(&app->asteroids[j].x,&app->asteroids[j].y,
                               app->asteroids[j].vx,app->asteroids[j].vy);
        app->asteroids[j].rot += app->asteroids[j].rot_speed;
        if (app->asteroids[j].rot < 0) app->asteroids[j].rot = 2*PI;
        else if (app->asteroids[j].rot > 2*PI) app->asteroids[j].rot = 0;
    }

    /* Detect collision between bullet and asteroid. */
    for (int j = 0; j < app->bullets_num; j++) {
        Bullet *b = &app->bullets[j];
        for (int i = 0; i < app->asteroids_num; i++) {
            Asteroid *a = &app->asteroids[i];
            if (detect_collision(a->x, a->y, a->size,
                                 b->x, b->y, 1, 1))
            {
                remove_asteroid(app,i);
                remove_bullet(app,j);
                /* The bullet no longer exist. Break the loop.
                 * However we want to start processing from the
                 * same bullet index, since now it is used by
                 * another bullet (see remove_bullet()). */
                j--; /* Scan this j value again. */
                break;
            }
        }
    }

    /* From time to time, create a new asteroid. The more asteroids
     * already on the screen, the smaller probability of creating
     * a new one. */
    if (app->asteroids_num == 0 ||
        (random() % 5000) < (30/(1+app->asteroids_num)))
    {
        add_asteroid(app);
    }

    app->ticks++;
    view_port_update(app->view_port);
}

/* ======================== Flipper specific code =========================== */

/* Here all we do is putting the events into the queue that will be handled
 * in the while() loop of the app entry point function. */
void input_callback(InputEvent* input_event, void* ctx)
{
    AsteroidsApp *app = ctx;
    furi_message_queue_put(app->event_queue,input_event,FuriWaitForever);
}

/* Allocate the application state and initialize a number of stuff.
 * This is called in the entry point to create the application state. */
AsteroidsApp* asteroids_app_alloc() {
    AsteroidsApp *app = malloc(sizeof(AsteroidsApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, render_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
    app->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    app->running = 1;
    app->ticks = 0;
    app->ship.x = SCREEN_XRES / 2;
    app->ship.y = SCREEN_YRES / 2;
    app->ship.rot = PI; /* Start headed towards top. */
    app->ship.vx = 0;
    app->ship.vy = 0;
    app->bullets_num = 0;
    app->last_bullet_tick = 0;
    app->asteroids_num = 0;
    memset(app->pressed,0,sizeof(app->pressed));
    return app;
}

/* Free what the application allocated. It is not clear to me if the
 * Flipper OS, once the application exits, will be able to reclaim space
 * even if we forget to free something here. */
void asteroids_app_free(AsteroidsApp *app) {
    furi_assert(app);

    // View related.
    view_port_enabled_set(app->view_port, false);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(app->event_queue);
    app->gui = NULL;

    free(app);
}

/* Handle keys interaction. */
void asteroids_update_keypress_state(AsteroidsApp *app, InputEvent input) {
    if (input.type == InputTypePress) {
        app->pressed[input.key] = furi_get_tick();
    } else if (input.type == InputTypeRelease) {
        uint32_t dur = furi_get_tick() - app->pressed[input.key];
        app->pressed[input.key] = 0;
        if (dur < 200 && input.key == InputKeyOk) app->fire = true;
    }
}

int32_t asteroids_app_entry(void* p) {
    UNUSED(p);
    AsteroidsApp *app = asteroids_app_alloc();

    /* Create a timer. We do data analysis in the callback. */
    FuriTimer *timer = furi_timer_alloc(game_tick, FuriTimerTypePeriodic, app);
    furi_timer_start(timer, furi_kernel_get_tick_frequency() / 10);

    /* This is the main event loop: here we get the events that are pushed
     * in the queue by input_callback(), and process them one after the
     * other. The timeout is 100 milliseconds, so if not input is received
     * before such time, we exit the queue_get() function and call
     * view_port_update() in order to refresh our screen content. */
    InputEvent input;
    while(app->running) {
        FuriStatus qstat = furi_message_queue_get(app->event_queue, &input, 100);
        if (qstat == FuriStatusOk) {
            if (DEBUG_MSG) FURI_LOG_E(TAG, "Main Loop - Input: type %d key %u",
                    input.type, input.key);

            /* Handle navigation here. Then handle view-specific inputs
             * in the view specific handling function. */
            if (input.type == InputTypeShort &&
                input.key == InputKeyBack)
            {
                app->running = 0;
            } else {
                asteroids_update_keypress_state(app,input);
            }
        } else {
            /* Useful to understand if the app is still alive when it
             * does not respond because of bugs. */
            if (DEBUG_MSG) {
                static int c = 0; c++;
                if (!(c % 20)) FURI_LOG_E(TAG, "Loop timeout");
            }
        }
    }

    furi_timer_free(timer);
    asteroids_app_free(app);
    return 0;
}

#include "wokwi-api.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Standard libc function declarations without including headers that pull in C++ features.h */
void *malloc(size_t size);
void free(void *ptr);
void *memset(void *s, int c, size_t n);
double sin(double x);
double cos(double x);
int rand(void);

#define abs(x) (((x) < 0) ? -(x) : (x))

/* ADXL345 Register Map */
#define ADXL345_DEVID_VAL    0xE5
#define REG_DEVID            0x00
#define REG_THRESH_TAP       0x1D
#define REG_OFSX             0x1E
#define REG_OFSY             0x1F
#define REG_OFSZ             0x20
#define REG_DUR              0x21
#define REG_LATENT           0x22
#define REG_WINDOW           0x23
#define REG_THRESH_ACT       0x24
#define REG_THRESH_INACT     0x25
#define REG_TIME_INACT       0x26
#define REG_ACT_INACT_CTL    0x27
#define REG_THRESH_FF        0x28
#define REG_TIME_FF          0x29
#define REG_TAP_AXES         0x2A
#define REG_ACT_TAP_STATUS   0x2B
#define REG_BW_RATE          0x2C
#define REG_POWER_CTL        0x2D
#define REG_INT_ENABLE       0x2E
#define REG_INT_MAP          0x2F
#define REG_INT_SOURCE       0x30
#define REG_DATA_FORMAT      0x31
#define REG_DATAX0           0x32
#define REG_DATAX1           0x33
#define REG_DATAY0           0x34
#define REG_DATAY1           0x35
#define REG_DATAZ0           0x36
#define REG_DATAZ1           0x37
#define REG_FIFO_CTL         0x38
#define REG_FIFO_STATUS      0x39

#define SPI_BUFFER_SIZE      16
#define TWO_PI               6.28318530717958647692

/* Framebuffer Canvas Dimensions (160x100 matching 21x16mm PCB aspect ratio) */
#define FB_WIDTH             160
#define FB_HEIGHT            100
#define FB_TOTAL_PIXELS      (FB_WIDTH * FB_HEIGHT)

/* 32-bit RGBA Color Macros (Little-endian byte layout: R, G, B, A) */
#define RGBA(r, g, b, a)     (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))

/* Palette */
#define COLOR_PCB_BLUE       RGBA(21, 101, 192, 255)   /* Royal Blue #1565C0 */
#define COLOR_PCB_DARK       RGBA(13, 71, 161, 255)    /* Dark Border #0D47A1 */
#define COLOR_PCB_SHEEN      RGBA(30, 136, 229, 255)   /* Surface Sheen #1E88E5 */
#define COLOR_GOLD_PAD       RGBA(229, 193, 88, 255)   /* ENIG Gold Pad #E5C158 */
#define COLOR_GOLD_HIGHLIGHT RGBA(255, 223, 128, 255)  /* Gold Highlight #FFDF80 */
#define COLOR_DRILL_HOLE     RGBA(24, 24, 27, 255)     /* Dark Plated Hole #18181B */
#define COLOR_SOLDER_RIM     RGBA(209, 213, 219, 255)  /* Solder Rim #D1D5DB */
#define COLOR_SILK_WHITE     RGBA(250, 250, 250, 255)  /* Crisp White Silkscreen #FAFAFA */
#define COLOR_IC_BODY        RGBA(32, 34, 38, 255)     /* Charcoal IC Body #202226 */
#define COLOR_IC_BORDER      RGBA(55, 65, 81, 255)     /* IC Chamfer #374151 */
#define COLOR_IC_TEXT        RGBA(156, 163, 175, 255)  /* Laser Etch Text #9CA3AF */
#define COLOR_CAP_BROWN      RGBA(180, 126, 83, 255)   /* Ceramic 0603 #B47E53 */
#define COLOR_RES_BODY       RGBA(30, 32, 36, 255)     /* Resistor Body #1E2024 */
#define COLOR_SILVER_PIN     RGBA(229, 231, 235, 255)  /* Silver Solder Pin #E5E7EB */
#define COLOR_TRACE_BLUE     RGBA(26, 115, 210, 255)   /* Copper Trace Under Mask */

typedef struct {
    pin_t pin_gnd;
    pin_t pin_vcc;
    pin_t pin_cs;
    pin_t pin_sck;
    pin_t pin_mosi;
    pin_t pin_miso;
    pin_t pin_int1;
    pin_t pin_int2;

    spi_dev_t spi_device;
    buffer_t fb;

    uint32_t attr_fault_level;
    uint32_t attr_fault_level_camel;

    uint8_t registers[0x40];
    uint8_t spi_rx_buf[SPI_BUFFER_SIZE];
    uint8_t spi_tx_buf[SPI_BUFFER_SIZE];

    uint8_t current_reg;
    bool is_read;
    bool multi_byte;
    uint32_t byte_counter;

    double time_sec;
    uint32_t sample_count;
} chip_state_t;

/* Framebuffer pixel buffer allocated dynamically in render_gy291_board */

/* ========================================================================= */
/* 2D Graphic Primitives & Bitmap Font Renderers                             */
/* ========================================================================= */

static void draw_pixel(uint32_t *fb, int w, int h, int x, int y, uint32_t color) {
    if (x >= 0 && x < w && y >= 0 && y < h) {
        fb[y * w + x] = color;
    }
}

static void draw_rect_fill(uint32_t *fb, int w, int h, int x0, int y0, int rw, int rh, uint32_t color) {
    for (int y = y0; y < y0 + rh; y++) {
        if (y < 0 || y >= h) continue;
        for (int x = x0; x < x0 + rw; x++) {
            if (x >= 0 && x < w) {
                fb[y * w + x] = color;
            }
        }
    }
}

static void draw_rect_outline(uint32_t *fb, int w, int h, int x0, int y0, int rw, int rh, uint32_t color) {
    for (int x = x0; x < x0 + rw; x++) {
        draw_pixel(fb, w, h, x, y0, color);
        draw_pixel(fb, w, h, x, y0 + rh - 1, color);
    }
    for (int y = y0; y < y0 + rh; y++) {
        draw_pixel(fb, w, h, x0, y, color);
        draw_pixel(fb, w, h, x0 + rw - 1, y, color);
    }
}

static void draw_circle_fill(uint32_t *fb, int w, int h, int cx, int cy, int r, uint32_t color) {
    int r2 = r * r;
    for (int dy = -r; dy <= r; dy++) {
        int y = cy + dy;
        if (y < 0 || y >= h) continue;
        for (int dx = -r; dx <= r; dx++) {
            int x = cx + dx;
            if (x < 0 || x >= w) continue;
            if (dx * dx + dy * dy <= r2) {
                fb[y * w + x] = color;
            }
        }
    }
}

static void draw_circle_ring(uint32_t *fb, int w, int h, int cx, int cy, int r_in, int r_out, uint32_t color) {
    int r_in2 = r_in * r_in;
    int r_out2 = r_out * r_out;
    for (int dy = -r_out; dy <= r_out; dy++) {
        int y = cy + dy;
        if (y < 0 || y >= h) continue;
        for (int dx = -r_out; dx <= r_out; dx++) {
            int x = cx + dx;
            if (x < 0 || x >= w) continue;
            int d2 = dx * dx + dy * dy;
            if (d2 >= r_in2 && d2 <= r_out2) {
                fb[y * w + x] = color;
            }
        }
    }
}

static void draw_line(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    while (1) {
        draw_pixel(fb, w, h, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* 3x5 Ultra-Compact Bitmap Font (3 columns, 5 rows) */
static void draw_char_3x5(uint32_t *fb, int w, int h, int x, int y, char c, uint32_t color) {
    uint8_t cols[3] = {0, 0, 0};
    switch (c) {
        case 'G': cols[0] = 0x1F; cols[1] = 0x11; cols[2] = 0x1D; break;
        case 'N': cols[0] = 0x1F; cols[1] = 0x02; cols[2] = 0x1F; break;
        case 'D': cols[0] = 0x1F; cols[1] = 0x11; cols[2] = 0x0E; break;
        case 'V': cols[0] = 0x0F; cols[1] = 0x10; cols[2] = 0x0F; break;
        case 'C': cols[0] = 0x0E; cols[1] = 0x11; cols[2] = 0x11; break;
        case 'S': cols[0] = 0x12; cols[1] = 0x15; cols[2] = 0x09; break;
        case 'I': cols[0] = 0x11; cols[1] = 0x1F; cols[2] = 0x11; break;
        case 'T': cols[0] = 0x01; cols[1] = 0x1F; cols[2] = 0x01; break;
        case '1': cols[0] = 0x00; cols[1] = 0x1F; cols[2] = 0x00; break;
        case '2': cols[0] = 0x1D; cols[1] = 0x15; cols[2] = 0x17; break;
        case 'O': cols[0] = 0x0E; cols[1] = 0x11; cols[2] = 0x0E; break;
        case 'A': cols[0] = 0x1E; cols[1] = 0x05; cols[2] = 0x1E; break;
        case 'L': cols[0] = 0x1F; cols[1] = 0x10; cols[2] = 0x10; break;
        case 'X': cols[0] = 0x11; cols[1] = 0x0A; cols[2] = 0x11; break;
        case 'Y': cols[0] = 0x03; cols[1] = 0x1C; cols[2] = 0x03; break;
        case 'Z': cols[0] = 0x19; cols[1] = 0x15; cols[2] = 0x13; break;
        case '+': cols[0] = 0x04; cols[1] = 0x0E; cols[2] = 0x04; break;
        case '-': cols[0] = 0x04; cols[1] = 0x04; cols[2] = 0x04; break;
        case '3': cols[0] = 0x11; cols[1] = 0x15; cols[2] = 0x1F; break;
        case '4': cols[0] = 0x07; cols[1] = 0x04; cols[2] = 0x1F; break;
        case '5': cols[0] = 0x17; cols[1] = 0x15; cols[2] = 0x1D; break;
        case '6': cols[0] = 0x1F; cols[1] = 0x15; cols[2] = 0x1C; break;
        case 'K': cols[0] = 0x1F; cols[1] = 0x04; cols[2] = 0x1B; break;
        case '0': cols[0] = 0x1F; cols[1] = 0x11; cols[2] = 0x1F; break;
        case 'B': cols[0] = 0x1F; cols[1] = 0x15; cols[2] = 0x0A; break;
        case ' ': default: return;
    }
    for (int col = 0; col < 3; col++) {
        for (int row = 0; row < 5; row++) {
            if (cols[col] & (1 << row)) {
                int px = x + col;
                int py = y + row;
                if (px >= 0 && px < w && py >= 0 && py < h) {
                    fb[py * w + px] = color;
                }
            }
        }
    }
}

static void draw_string_3x5(uint32_t *fb, int w, int h, int x, int y, const char *str, uint32_t color) {
    while (*str) {
        draw_char_3x5(fb, w, h, x, y, *str, color);
        x += 4; /* 3 pixels glyph + 1 pixel inter-character spacing */
        str++;
    }
}

/* 5x7 Standard Crisp Silkscreen Bitmap Font (5 columns, 7 rows) */
static void draw_char_5x7(uint32_t *fb, int w, int h, int x, int y, char c, uint32_t color) {
    uint8_t cols[5] = {0, 0, 0, 0, 0};
    switch (c) {
        case ' ': break;
        case '+': cols[0] = 0x08; cols[1] = 0x08; cols[2] = 0x3E; cols[3] = 0x08; cols[4] = 0x08; break;
        case '-': cols[0] = 0x08; cols[1] = 0x08; cols[2] = 0x08; cols[3] = 0x08; cols[4] = 0x08; break;
        case '0': cols[0] = 0x3E; cols[1] = 0x51; cols[2] = 0x49; cols[3] = 0x45; cols[4] = 0x3E; break;
        case '1': cols[0] = 0x00; cols[1] = 0x42; cols[2] = 0x7F; cols[3] = 0x40; cols[4] = 0x00; break;
        case '2': cols[0] = 0x42; cols[1] = 0x61; cols[2] = 0x51; cols[3] = 0x49; cols[4] = 0x46; break;
        case '3': cols[0] = 0x21; cols[1] = 0x41; cols[2] = 0x45; cols[3] = 0x4B; cols[4] = 0x31; break;
        case '4': cols[0] = 0x18; cols[1] = 0x14; cols[2] = 0x12; cols[3] = 0x7F; cols[4] = 0x10; break;
        case '5': cols[0] = 0x27; cols[1] = 0x45; cols[2] = 0x45; cols[3] = 0x45; cols[4] = 0x39; break;
        case '6': cols[0] = 0x3C; cols[1] = 0x4A; cols[2] = 0x49; cols[3] = 0x49; cols[4] = 0x30; break;
        case '7': cols[0] = 0x01; cols[1] = 0x71; cols[2] = 0x09; cols[3] = 0x05; cols[4] = 0x03; break;
        case '8': cols[0] = 0x36; cols[1] = 0x49; cols[2] = 0x49; cols[3] = 0x49; cols[4] = 0x36; break;
        case '9': cols[0] = 0x06; cols[1] = 0x49; cols[2] = 0x49; cols[3] = 0x29; cols[4] = 0x1E; break;
        case 'A': cols[0] = 0x7E; cols[1] = 0x11; cols[2] = 0x11; cols[3] = 0x11; cols[4] = 0x7E; break;
        case 'B': cols[0] = 0x7F; cols[1] = 0x49; cols[2] = 0x49; cols[3] = 0x49; cols[4] = 0x36; break;
        case 'C': cols[0] = 0x3E; cols[1] = 0x41; cols[2] = 0x41; cols[3] = 0x41; cols[4] = 0x22; break;
        case 'D': cols[0] = 0x7F; cols[1] = 0x41; cols[2] = 0x41; cols[3] = 0x22; cols[4] = 0x1C; break;
        case 'E': cols[0] = 0x7F; cols[1] = 0x49; cols[2] = 0x49; cols[3] = 0x49; cols[4] = 0x41; break;
        case 'F': cols[0] = 0x7F; cols[1] = 0x09; cols[2] = 0x09; cols[3] = 0x09; cols[4] = 0x01; break;
        case 'G': cols[0] = 0x3E; cols[1] = 0x41; cols[2] = 0x49; cols[3] = 0x49; cols[4] = 0x7A; break;
        case 'H': cols[0] = 0x7F; cols[1] = 0x08; cols[2] = 0x08; cols[3] = 0x08; cols[4] = 0x7F; break;
        case 'I': cols[0] = 0x00; cols[1] = 0x41; cols[2] = 0x7F; cols[3] = 0x41; cols[4] = 0x00; break;
        case 'K': cols[0] = 0x7F; cols[1] = 0x08; cols[2] = 0x14; cols[3] = 0x22; cols[4] = 0x41; break;
        case 'L': cols[0] = 0x7F; cols[1] = 0x40; cols[2] = 0x40; cols[3] = 0x40; cols[4] = 0x40; break;
        case 'M': cols[0] = 0x7F; cols[1] = 0x02; cols[2] = 0x0C; cols[3] = 0x02; cols[4] = 0x7F; break;
        case 'N': cols[0] = 0x7F; cols[1] = 0x04; cols[2] = 0x08; cols[3] = 0x10; cols[4] = 0x7F; break;
        case 'O': cols[0] = 0x3E; cols[1] = 0x41; cols[2] = 0x41; cols[3] = 0x41; cols[4] = 0x3E; break;
        case 'P': cols[0] = 0x7F; cols[1] = 0x09; cols[2] = 0x09; cols[3] = 0x09; cols[4] = 0x06; break;
        case 'R': cols[0] = 0x7F; cols[1] = 0x09; cols[2] = 0x19; cols[3] = 0x29; cols[4] = 0x46; break;
        case 'S': cols[0] = 0x46; cols[1] = 0x49; cols[2] = 0x49; cols[3] = 0x49; cols[4] = 0x31; break;
        case 'T': cols[0] = 0x01; cols[1] = 0x01; cols[2] = 0x7F; cols[3] = 0x01; cols[4] = 0x01; break;
        case 'V': cols[0] = 0x1F; cols[1] = 0x20; cols[2] = 0x40; cols[3] = 0x20; cols[4] = 0x1F; break;
        case 'X': cols[0] = 0x63; cols[1] = 0x14; cols[2] = 0x08; cols[3] = 0x14; cols[4] = 0x63; break;
        case 'Y': cols[0] = 0x07; cols[1] = 0x08; cols[2] = 0x70; cols[3] = 0x08; cols[4] = 0x07; break;
        case 'Z': cols[0] = 0x61; cols[1] = 0x51; cols[2] = 0x49; cols[3] = 0x45; cols[4] = 0x43; break;
        default: return;
    }
    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 7; row++) {
            if (cols[col] & (1 << row)) {
                int px = x + col;
                int py = y + row;
                if (px >= 0 && px < w && py >= 0 && py < h) {
                    fb[py * w + px] = color;
                }
            }
        }
    }
}

static void draw_string_5x7(uint32_t *fb, int w, int h, int x, int y, const char *str, uint32_t color) {
    while (*str) {
        draw_char_5x7(fb, w, h, x, y, *str, color);
        x += 6; /* 5 pixels glyph + 1 pixel inter-character spacing */
        str++;
    }
}

/* ========================================================================= */
/* Authentic Blue GY-291 Breakout Board Visual Rendering                     */
/* ========================================================================= */

static void render_gy291_board(chip_state_t *chip) {
    if (!chip->fb) return;

    int w = FB_WIDTH;
    int h = FB_HEIGHT;

    uint32_t *fb_pixels = (uint32_t *)malloc(FB_TOTAL_PIXELS * sizeof(uint32_t));
    if (!fb_pixels) return;

    /* 1. Glossy Royal Blue Soldermask Background */
    draw_rect_fill(fb_pixels, w, h, 0, 0, w, h, COLOR_PCB_BLUE);

    /* Subtle PCB Bevel Border & Highlights */
    draw_rect_outline(fb_pixels, w, h, 0, 0, w, h, COLOR_PCB_DARK);
    draw_rect_outline(fb_pixels, w, h, 1, 1, w - 2, h - 2, COLOR_PCB_SHEEN);

    /* Corner rounding (radius 4px bevel cut) */
    draw_pixel(fb_pixels, w, h, 0, 0, RGBA(0, 0, 0, 0));
    draw_pixel(fb_pixels, w, h, 1, 0, RGBA(0, 0, 0, 0));
    draw_pixel(fb_pixels, w, h, 0, 1, RGBA(0, 0, 0, 0));
    draw_pixel(fb_pixels, w, h, w - 1, 0, RGBA(0, 0, 0, 0));
    draw_pixel(fb_pixels, w, h, w - 2, 0, RGBA(0, 0, 0, 0));
    draw_pixel(fb_pixels, w, h, w - 1, 1, RGBA(0, 0, 0, 0));
    draw_pixel(fb_pixels, w, h, 0, h - 1, RGBA(0, 0, 0, 0));
    draw_pixel(fb_pixels, w, h, 1, h - 1, RGBA(0, 0, 0, 0));
    draw_pixel(fb_pixels, w, h, 0, h - 2, RGBA(0, 0, 0, 0));
    draw_pixel(fb_pixels, w, h, w - 1, h - 1, RGBA(0, 0, 0, 0));
    draw_pixel(fb_pixels, w, h, w - 2, h - 1, RGBA(0, 0, 0, 0));
    draw_pixel(fb_pixels, w, h, w - 1, h - 2, RGBA(0, 0, 0, 0));

    /* 2. Copper Traces (realistic routed lines on PCB) */
    draw_line(fb_pixels, w, h, 20, 22, 48, 30, COLOR_TRACE_BLUE);
    draw_line(fb_pixels, w, h, 86, 26, 98, 26, COLOR_TRACE_BLUE);
    draw_line(fb_pixels, w, h, 86, 32, 128, 22, COLOR_TRACE_BLUE);
    draw_line(fb_pixels, w, h, 66, 52, 66, 68, COLOR_TRACE_BLUE);
    draw_line(fb_pixels, w, h, 48, 48, 17, 78, COLOR_TRACE_BLUE);
    draw_line(fb_pixels, w, h, 54, 52, 35, 78, COLOR_TRACE_BLUE);
    draw_line(fb_pixels, w, h, 72, 52, 107, 78, COLOR_TRACE_BLUE);
    draw_line(fb_pixels, w, h, 78, 52, 125, 78, COLOR_TRACE_BLUE);
    draw_line(fb_pixels, w, h, 84, 52, 143, 78, COLOR_TRACE_BLUE);

    /* 3. Plated Ø3.0mm Mounting Hole (Top-Left) */
    int hole_cx = 20;
    int hole_cy = 22;
    /* Gold annular grounding ring (outer radius 9, inner 5) */
    draw_circle_ring(fb_pixels, w, h, hole_cx, hole_cy, 5, 9, COLOR_GOLD_PAD);
    /* Outer highlight */
    draw_circle_ring(fb_pixels, w, h, hole_cx, hole_cy, 9, 10, COLOR_GOLD_HIGHLIGHT);
    /* Dark through-hole */
    draw_circle_fill(fb_pixels, w, h, hole_cx, hole_cy, 4, COLOR_DRILL_HOLE);
    /* Plated barrel inner rim */
    draw_circle_ring(fb_pixels, w, h, hole_cx, hole_cy, 4, 5, COLOR_SOLDER_RIM);

    /* 4. Molded Black LGA-14 Sensor IC Package (Center) */
    int ic_x = 48;
    int ic_y = 18;
    int ic_w = 38;
    int ic_h = 34;
    /* Gold LGA terminal pads on PCB perimeter */
    for (int i = 0; i < 4; i++) {
        draw_rect_fill(fb_pixels, w, h, ic_x + 5 + i * 8, ic_y - 2, 4, 2, COLOR_GOLD_PAD);
        draw_rect_fill(fb_pixels, w, h, ic_x + 5 + i * 8, ic_y + ic_h, 4, 2, COLOR_GOLD_PAD);
    }
    for (int i = 0; i < 3; i++) {
        draw_rect_fill(fb_pixels, w, h, ic_x - 2, ic_y + 6 + i * 9, 2, 5, COLOR_GOLD_PAD);
        draw_rect_fill(fb_pixels, w, h, ic_x + ic_w, ic_y + 6 + i * 9, 2, 5, COLOR_GOLD_PAD);
    }
    /* Molded black epoxy package body */
    draw_rect_fill(fb_pixels, w, h, ic_x, ic_y, ic_w, ic_h, COLOR_IC_BODY);
    draw_rect_outline(fb_pixels, w, h, ic_x, ic_y, ic_w, ic_h, COLOR_IC_BORDER);
    /* Pin 1 Gold Orientation Dimple */
    draw_circle_fill(fb_pixels, w, h, ic_x + 5, ic_y + 5, 2, COLOR_GOLD_HIGHLIGHT);
    /* Laser-etched IC Markings */
    draw_string_3x5(fb_pixels, w, h, ic_x + 11, ic_y + 8, "ADXL", COLOR_IC_TEXT);
    draw_string_3x5(fb_pixels, w, h, ic_x + 13, ic_y + 16, "345", COLOR_IC_TEXT);
    draw_string_3x5(fb_pixels, w, h, ic_x + 10, ic_y + 24, "BCCZ", COLOR_IC_TEXT);

    /* 5. SOT-23/SOT-89 3.3V LDO Voltage Regulator (Labeled "662K") */
    int ldo_x = 98;
    int ldo_y = 18;
    int ldo_w = 23;
    int ldo_h = 16;
    /* Silver solder pins */
    draw_rect_fill(fb_pixels, w, h, ldo_x + 3, ldo_y - 2, 4, 2, COLOR_SILVER_PIN);
    draw_rect_fill(fb_pixels, w, h, ldo_x + 16, ldo_y - 2, 4, 2, COLOR_SILVER_PIN);
    draw_rect_fill(fb_pixels, w, h, ldo_x + 8, ldo_y + ldo_h, 7, 3, COLOR_SILVER_PIN);
    /* Package body */
    draw_rect_fill(fb_pixels, w, h, ldo_x, ldo_y, ldo_w, ldo_h, COLOR_IC_BODY);
    draw_rect_outline(fb_pixels, w, h, ldo_x, ldo_y, ldo_w, ldo_h, COLOR_IC_BORDER);
    /* Silkscreen text "662K" */
    draw_string_3x5(fb_pixels, w, h, ldo_x + 4, ldo_y + 6, "662K", COLOR_SILK_WHITE);

    /* 6. SMD Decoupling Capacitors (C1, C2) */
    /* C1 (0.1 uF high-freq bypass) */
    draw_rect_fill(fb_pixels, w, h, 128, 18, 12, 6, COLOR_CAP_BROWN);
    draw_rect_fill(fb_pixels, w, h, 127, 18, 2, 6, COLOR_SILVER_PIN);
    draw_rect_fill(fb_pixels, w, h, 139, 18, 2, 6, COLOR_SILVER_PIN);
    draw_string_3x5(fb_pixels, w, h, 143, 19, "C1", COLOR_SILK_WHITE);

    /* C2 (10 uF bulk tantalum/ceramic filter) */
    draw_rect_fill(fb_pixels, w, h, 128, 28, 12, 6, COLOR_CAP_BROWN);
    draw_rect_fill(fb_pixels, w, h, 127, 28, 2, 6, COLOR_SILVER_PIN);
    draw_rect_fill(fb_pixels, w, h, 139, 28, 2, 6, COLOR_SILVER_PIN);
    draw_string_3x5(fb_pixels, w, h, 143, 29, "C2", COLOR_SILK_WHITE);

    /* 7. SMD Pull-Up Resistors (R1, R2) */
    /* R1 (10k pull-up) */
    draw_rect_fill(fb_pixels, w, h, 96, 40, 11, 6, COLOR_RES_BODY);
    draw_rect_fill(fb_pixels, w, h, 95, 40, 2, 6, COLOR_SILVER_PIN);
    draw_rect_fill(fb_pixels, w, h, 106, 40, 2, 6, COLOR_SILVER_PIN);
    draw_string_3x5(fb_pixels, w, h, 97, 41, "103", COLOR_SILVER_PIN);

    /* R2 (10k pull-up) */
    draw_rect_fill(fb_pixels, w, h, 114, 40, 11, 6, COLOR_RES_BODY);
    draw_rect_fill(fb_pixels, w, h, 113, 40, 2, 6, COLOR_SILVER_PIN);
    draw_rect_fill(fb_pixels, w, h, 124, 40, 2, 6, COLOR_SILVER_PIN);
    draw_string_3x5(fb_pixels, w, h, 115, 41, "103", COLOR_SILVER_PIN);

    /* 8. Crisp White Silkscreen Labels & Graphics */
    /* Board Model "GY-291" */
    draw_string_5x7(fb_pixels, w, h, 96, 54, "GY-291", COLOR_SILK_WHITE);

    /* Sensor Name "ADXL345" */
    draw_string_5x7(fb_pixels, w, h, 8, 54, "ADXL345", COLOR_SILK_WHITE);

    /* Silkscreen Component Boundary Box */
    draw_rect_outline(fb_pixels, w, h, 6, 12, 148, 52, COLOR_SILK_WHITE);

    /* Coordinate Axes (+X, +Y, +Z) */
    int triad_x = 138;
    int triad_y = 52;
    /* +X Arrow (Right) */
    draw_line(fb_pixels, w, h, triad_x, triad_y, triad_x + 10, triad_y, COLOR_SILK_WHITE);
    draw_pixel(fb_pixels, w, h, triad_x + 9, triad_y - 1, COLOR_SILK_WHITE);
    draw_pixel(fb_pixels, w, h, triad_x + 9, triad_y + 1, COLOR_SILK_WHITE);
    draw_pixel(fb_pixels, w, h, triad_x + 8, triad_y - 2, COLOR_SILK_WHITE);
    draw_pixel(fb_pixels, w, h, triad_x + 8, triad_y + 2, COLOR_SILK_WHITE);
    draw_string_3x5(fb_pixels, w, h, triad_x + 12, triad_y - 2, "+X", COLOR_SILK_WHITE);

    /* +Y Arrow (Up) */
    draw_line(fb_pixels, w, h, triad_x, triad_y, triad_x, triad_y - 10, COLOR_SILK_WHITE);
    draw_pixel(fb_pixels, w, h, triad_x - 1, triad_y - 9, COLOR_SILK_WHITE);
    draw_pixel(fb_pixels, w, h, triad_x + 1, triad_y - 9, COLOR_SILK_WHITE);
    draw_pixel(fb_pixels, w, h, triad_x - 2, triad_y - 8, COLOR_SILK_WHITE);
    draw_pixel(fb_pixels, w, h, triad_x + 2, triad_y - 8, COLOR_SILK_WHITE);
    draw_string_3x5(fb_pixels, w, h, triad_x - 2, triad_y - 17, "+Y", COLOR_SILK_WHITE);

    /* +Z Indicator (Normal out of board: circle with center dot) */
    draw_circle_ring(fb_pixels, w, h, triad_x + 14, triad_y - 12, 2, 4, COLOR_SILK_WHITE);
    draw_pixel(fb_pixels, w, h, triad_x + 14, triad_y - 12, COLOR_SILK_WHITE);
    draw_string_3x5(fb_pixels, w, h, triad_x + 11, triad_y - 6, "+Z", COLOR_SILK_WHITE);

    /* Silkscreen Separator Line above Pin Header */
    draw_line(fb_pixels, w, h, 6, 68, 153, 68, COLOR_SILK_WHITE);

    /* 9. 8 Gold-Plated Solder Annular Pads along bottom edge */
    static const int pad_x[8] = { 17, 35, 53, 71, 89, 107, 125, 143 };
    static const char *pin_labels[8] = { "GND", "VCC", "CS", "INT1", "INT2", "SDO", "SDA", "SCL" };
    static const int label_x_offset[8] = { -5, -5, -3, -7, -7, -5, -5, -5 };

    int pad_y = 86;
    for (int i = 0; i < 8; i++) {
        int px = pad_x[i];

        /* Solder neck extending from connector entry */
        draw_line(fb_pixels, w, h, px, 79, px, pad_y, COLOR_GOLD_PAD);

        /* Gold annular ring (outer radius 6, inner 3) */
        draw_circle_ring(fb_pixels, w, h, px, pad_y, 3, 6, COLOR_GOLD_PAD);
        draw_circle_ring(fb_pixels, w, h, px, pad_y, 6, 7, COLOR_GOLD_HIGHLIGHT);

        /* Dark drill hole (centered through-hole) */
        draw_circle_fill(fb_pixels, w, h, px, pad_y, 2, COLOR_DRILL_HOLE);

        /* Inner solder rim highlight */
        draw_circle_ring(fb_pixels, w, h, px, pad_y, 2, 3, COLOR_SOLDER_RIM);

        /* Silkscreen Pin Label centered above pad */
        int lx = px + label_x_offset[i];
        draw_string_3x5(fb_pixels, w, h, lx, 72, pin_labels[i], COLOR_SILK_WHITE);
    }

    /* Transfer rendered pixel buffer to Wokwi Host Framebuffer */
    buffer_write(chip->fb, 0, (uint8_t *)fb_pixels, FB_TOTAL_PIXELS * sizeof(uint32_t));
    free(fb_pixels);
}

/* ========================================================================= */
/* ADXL345 Kinematics & Triaxial Vibration Dynamics Engine                   */
/* ========================================================================= */

static void update_sensor_dynamics(chip_state_t *chip) {
    /* POWER_CTL: Measurement Mode is controlled by Bit 3 (0x08) */
    bool measuring = (chip->registers[REG_POWER_CTL] & 0x08) != 0;

    uint32_t fault = attr_read(chip->attr_fault_level);
    if (fault == 0) {
        fault = attr_read(chip->attr_fault_level_camel);
    }
    double t = chip->time_sec;

    /* N20 Motor Kinematics: 600 RPM = 10.0 Hz fundamental vibration */
    double f0 = 10.0;
    double omega = TWO_PI * f0;

    double ax = 0.0;
    double ay = 0.0;
    double az = 1.0; /* 1.0g static Earth gravity aligned with Z axis */

    if (measuring) {
        /* Pseudo-random gear mesh noise floor (+/- 0.05g) */
        double noise_x = (((double)(rand() % 100)) / 1000.0) - 0.05;
        double noise_y = (((double)(rand() % 100)) / 1000.0) - 0.05;
        double noise_z = (((double)(rand() % 100)) / 1000.0) - 0.05;

        /* Baseline nominal vibration (gear teeth mesh harmonics) */
        ax = 0.08 * sin(omega * t) + 0.02 * sin(2.0 * omega * t) + noise_x;
        ay = 0.08 * cos(omega * t) + 0.02 * cos(2.0 * omega * t) + noise_y;
        az = 1.00 + 0.04 * sin(3.0 * omega * t) + noise_z;

        /* Controlled mechanical imbalance fault injection */
        if (fault > 0) {
            double severity = (double)fault / 100.0;
            /* Centrifugal excitation: F_u = m * r * omega^2 */
            double unbalance_amplitude = 1.85 * severity;

            /* 1X Rotational Fundamental Unbalance */
            ax += unbalance_amplitude * sin(omega * t);
            ay += unbalance_amplitude * cos(omega * t);

            /* 2X Harmonic (Shaft deflection & mechanical looseness) */
            ax += (0.25 * unbalance_amplitude) * sin(2.0 * omega * t);
            ay += (0.25 * unbalance_amplitude) * cos(2.0 * omega * t);
            az += (0.35 * unbalance_amplitude) * sin(2.0 * omega * t);

            /* 3X Harmonic (Bearing cage & race excitation) */
            ax += (0.10 * unbalance_amplitude) * sin(3.0 * omega * t);
            ay += (0.10 * unbalance_amplitude) * cos(3.0 * omega * t);
            az += (0.10 * unbalance_amplitude) * sin(3.0 * omega * t);
        }
    }

    /* Scale to ADXL345 Full-Res Mode (fixed 3.9 mg/LSB -> 1.0 / 0.0039 = ~256.41 LSB/g) */
    int16_t raw_x = (int16_t)(ax * 256.41);
    int16_t raw_y = (int16_t)(ay * 256.41);
    int16_t raw_z = (int16_t)(az * 256.41);

    /* Store little-endian 16-bit signed registers */
    chip->registers[REG_DATAX0] = (uint8_t)(raw_x & 0xFF);
    chip->registers[REG_DATAX1] = (uint8_t)((raw_x >> 8) & 0xFF);
    chip->registers[REG_DATAY0] = (uint8_t)(raw_y & 0xFF);
    chip->registers[REG_DATAY1] = (uint8_t)((raw_y >> 8) & 0xFF);
    chip->registers[REG_DATAZ0] = (uint8_t)(raw_z & 0xFF);
    chip->registers[REG_DATAZ1] = (uint8_t)((raw_z >> 8) & 0xFF);

    /* Advance simulation timestep by 1/800 Hz (1250 us) */
    chip->time_sec += 0.00125;
    chip->sample_count++;
}

/* ========================================================================= */
/* 4-Wire SPI Slave Protocol Engine                                          */
/* ========================================================================= */

static void on_spi_done(void *user_data, uint8_t *buffer, uint32_t count) {
    chip_state_t *chip = (chip_state_t *)user_data;
    if (count == 0) return;

    if (chip->byte_counter == 0) {
        /* Parse 8-bit SPI command header */
        uint8_t header = buffer[0];
        chip->is_read = (header & 0x80) != 0;
        chip->multi_byte = (header & 0x40) != 0;
        chip->current_reg = header & 0x3F;
        chip->byte_counter = 1;

        if (chip->is_read) {
            /* If reading acceleration data registers, latch fresh sample */
            if (chip->current_reg >= REG_DATAX0 && chip->current_reg <= REG_DATAZ1) {
                update_sensor_dynamics(chip);
            }
            chip->spi_tx_buf[0] = chip->registers[chip->current_reg];
            spi_start(chip->spi_device, chip->spi_tx_buf, 1);
        } else {
            spi_start(chip->spi_device, chip->spi_rx_buf, 1);
        }
    } else {
        if (!chip->is_read) {
            /* DEVID (0x00) is strictly read-only */
            if (chip->current_reg != REG_DEVID && chip->current_reg < 0x40) {
                chip->registers[chip->current_reg] = buffer[0];
            }
        }

        /* Auto-increment register address during multi-byte burst read/write */
        if (chip->multi_byte && chip->current_reg < 0x3F) {
            chip->current_reg++;
        }

        if (chip->is_read) {
            chip->spi_tx_buf[0] = chip->registers[chip->current_reg];
            spi_start(chip->spi_device, chip->spi_tx_buf, 1);
        } else {
            spi_start(chip->spi_device, chip->spi_rx_buf, 1);
        }
        chip->byte_counter++;
    }
}

static void on_cs_change(void *user_data, pin_t pin, uint32_t value) {
    (void)pin;
    chip_state_t *chip = (chip_state_t *)user_data;
    if (value == LOW) {
        /* Chip Select asserted LOW: Begin SPI transaction */
        chip->byte_counter = 0;
        spi_start(chip->spi_device, chip->spi_rx_buf, 1);
    } else {
        /* Chip Select de-asserted HIGH: Terminate SPI transaction */
        spi_stop(chip->spi_device);
    }
}

/* Helper to initialize pins with fallback alias support */
static pin_t init_pin_with_alias(const char *primary, const char *secondary, uint32_t mode) {
    pin_t p = pin_init(primary, mode);
    if (p == NO_PIN && secondary != NULL) {
        p = pin_init(secondary, mode);
    }
    return p;
}

/* ========================================================================= */
/* Chip Lifecycle Entrypoint (Exported as chipInit)                          */
/* ========================================================================= */

void chip_init(void) {
    chip_state_t *chip = (chip_state_t *)malloc(sizeof(chip_state_t));
    if (!chip) return;
    memset(chip, 0, sizeof(chip_state_t));

    /* Initialize Pins: support physical Robocraze names with SPI aliases */
    chip->pin_gnd  = init_pin_with_alias("GND", NULL, INPUT_PULLDOWN);
    chip->pin_vcc  = init_pin_with_alias("VCC", NULL, INPUT_PULLUP);
    chip->pin_cs   = init_pin_with_alias("CS", NULL, INPUT_PULLUP);
    chip->pin_int1 = init_pin_with_alias("INT1", NULL, OUTPUT_LOW);
    chip->pin_int2 = init_pin_with_alias("INT2", NULL, OUTPUT_LOW);
    chip->pin_miso = init_pin_with_alias("SDO", "MISO", OUTPUT);
    chip->pin_mosi = init_pin_with_alias("SDA", "MOSI", INPUT);
    chip->pin_sck  = init_pin_with_alias("SCL", "SCK", INPUT);

    /* Attributes & Controls: support both canonical faultLevel and legacy fault_level */
    chip->attr_fault_level       = attr_init("fault_level", 0);
    chip->attr_fault_level_camel = attr_init("faultLevel", 0);

    /* Power-on Reset ADXL345 Default Registers */
    chip->registers[REG_DEVID]       = ADXL345_DEVID_VAL; /* 0xE5 */
    chip->registers[REG_BW_RATE]     = 0x0A;              /* 100 Hz default ODR */
    chip->registers[REG_POWER_CTL]   = 0x00;              /* Standby mode */
    chip->registers[REG_DATA_FORMAT] = 0x00;              /* 10-bit mode, +/-2g */
    chip->registers[REG_FIFO_CTL]    = 0x00;              /* Bypass mode */

    /* Preload static 1.0g gravity sample on Z axis */
    int16_t z_init = (int16_t)(1.0 * 256.41);
    chip->registers[REG_DATAZ0] = (uint8_t)(z_init & 0xFF);
    chip->registers[REG_DATAZ1] = (uint8_t)((z_init >> 8) & 0xFF);

    /* 4-Wire Hardware SPI Slave Configuration (Mode 3 with Mode 0 fallback) */
    const spi_config_t spi_config = {
        .sck = chip->pin_sck,
        .mosi = chip->pin_mosi,
        .miso = chip->pin_miso,
        .mode = 3,
        .done = on_spi_done,
        .user_data = chip,
        .reserved = {0}
    };
    chip->spi_device = spi_init(&spi_config);

    /* Watch CS for active-LOW transitions */
    const pin_watch_config_t watch_cs = {
        .edge = BOTH,
        .pin_change = on_cs_change,
        .user_data = chip
    };
    pin_watch(chip->pin_cs, &watch_cs);

    /* Initialize Framebuffer Canvas Display (160x100 32-bit RGBA) */
    uint32_t fb_w = FB_WIDTH;
    uint32_t fb_h = FB_HEIGHT;
    chip->fb = framebuffer_init(&fb_w, &fb_h);
    if (chip->fb) {
        render_gy291_board(chip);
    }
}

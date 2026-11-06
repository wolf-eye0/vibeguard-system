#include "wokwi-api.h"

typedef unsigned long size_t;
void *calloc(size_t nmemb, size_t size);


#define FB_WIDTH  140
#define FB_HEIGHT 90

/* 5x7 Monospace Bitmap Font (ASCII 32 to 90) */
static const uint8_t font5x7[59][5] = {
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 32 ' '
  {0x00, 0x00, 0x5F, 0x00, 0x00}, // 33 '!'
  {0x00, 0x07, 0x00, 0x07, 0x00}, // 34 '"'
  {0x14, 0x7F, 0x14, 0x7F, 0x14}, // 35 '#'
  {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // 36 '$'
  {0x23, 0x13, 0x08, 0x64, 0x62}, // 37 '%'
  {0x36, 0x49, 0x55, 0x22, 0x50}, // 38 '&'
  {0x00, 0x05, 0x03, 0x00, 0x00}, // 39 '''
  {0x00, 0x1C, 0x22, 0x41, 0x00}, // 40 '('
  {0x00, 0x41, 0x22, 0x1C, 0x00}, // 41 ')'
  {0x14, 0x08, 0x3E, 0x08, 0x14}, // 42 '*'
  {0x08, 0x08, 0x3E, 0x08, 0x08}, // 43 '+'
  {0x00, 0x50, 0x30, 0x00, 0x00}, // 44 ','
  {0x08, 0x08, 0x08, 0x08, 0x08}, // 45 '-'
  {0x00, 0x60, 0x60, 0x00, 0x00}, // 46 '.'
  {0x20, 0x10, 0x08, 0x04, 0x02}, // 47 '/'
  {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 48 '0'
  {0x00, 0x42, 0x7F, 0x40, 0x00}, // 49 '1'
  {0x42, 0x61, 0x51, 0x49, 0x46}, // 50 '2'
  {0x21, 0x41, 0x45, 0x4B, 0x31}, // 51 '3'
  {0x18, 0x14, 0x12, 0x7F, 0x10}, // 52 '4'
  {0x27, 0x45, 0x45, 0x45, 0x39}, // 53 '5'
  {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 54 '6'
  {0x01, 0x71, 0x09, 0x05, 0x03}, // 55 '7'
  {0x36, 0x49, 0x49, 0x49, 0x36}, // 56 '8'
  {0x06, 0x49, 0x49, 0x29, 0x1E}, // 57 '9'
  {0x00, 0x36, 0x36, 0x00, 0x00}, // 58 ':'
  {0x00, 0x56, 0x36, 0x00, 0x00}, // 59 ';'
  {0x08, 0x14, 0x22, 0x41, 0x00}, // 60 '<'
  {0x14, 0x14, 0x14, 0x14, 0x14}, // 61 '='
  {0x00, 0x41, 0x22, 0x14, 0x08}, // 62 '>'
  {0x02, 0x01, 0x51, 0x09, 0x06}, // 63 '?'
  {0x32, 0x49, 0x79, 0x41, 0x3E}, // 64 '@'
  {0x7E, 0x11, 0x11, 0x11, 0x7E}, // 65 'A'
  {0x7F, 0x49, 0x49, 0x49, 0x36}, // 66 'B'
  {0x3E, 0x41, 0x41, 0x41, 0x22}, // 67 'C'
  {0x7F, 0x41, 0x41, 0x22, 0x1C}, // 68 'D'
  {0x7F, 0x49, 0x49, 0x49, 0x41}, // 69 'E'
  {0x7F, 0x09, 0x09, 0x09, 0x01}, // 70 'F'
  {0x3E, 0x41, 0x49, 0x49, 0x7A}, // 71 'G'
  {0x7F, 0x08, 0x08, 0x08, 0x7F}, // 72 'H'
  {0x00, 0x41, 0x7F, 0x41, 0x00}, // 73 'I'
  {0x20, 0x40, 0x41, 0x3F, 0x01}, // 74 'J'
  {0x7F, 0x08, 0x14, 0x22, 0x41}, // 75 'K'
  {0x7F, 0x40, 0x40, 0x40, 0x40}, // 76 'L'
  {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // 77 'M'
  {0x7F, 0x04, 0x08, 0x10, 0x7F}, // 78 'N'
  {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 79 'O'
  {0x7F, 0x09, 0x09, 0x09, 0x06}, // 80 'P'
  {0x3E, 0x41, 0x51, 0x21, 0x5E}, // 81 'Q'
  {0x7F, 0x09, 0x19, 0x29, 0x46}, // 82 'R'
  {0x46, 0x49, 0x49, 0x49, 0x31}, // 83 'S'
  {0x01, 0x01, 0x7F, 0x01, 0x01}, // 84 'T'
  {0x3F, 0x40, 0x40, 0x40, 0x3F}, // 85 'U'
  {0x1F, 0x20, 0x40, 0x20, 0x1F}, // 86 'V'
  {0x7F, 0x20, 0x18, 0x20, 0x7F}, // 87 'W'
  {0x63, 0x14, 0x08, 0x14, 0x63}, // 88 'X'
  {0x07, 0x08, 0x70, 0x08, 0x07}, // 89 'Y'
  {0x61, 0x51, 0x49, 0x45, 0x43}  // 90 'Z'
};

typedef struct {
  pin_t pin_12v_out;
  pin_t pin_gnd;
  pin_t pin_12v; // alias for 12V_OUT

  uint32_t attr_power;
  bool power_state;

  buffer_t fb;
  uint32_t width;
  uint32_t height;

  timer_t poll_timer;

  uint32_t pixels[FB_WIDTH * FB_HEIGHT];
} pwr_supply_t;

static inline uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
}

static inline void set_pixel(pwr_supply_t *chip, int x, int y, uint32_t color) {
  if (x >= 0 && x < FB_WIDTH && y >= 0 && y < FB_HEIGHT) {
    chip->pixels[y * FB_WIDTH + x] = color;
  }
}

static inline void blend_pixel(pwr_supply_t *chip, int x, int y, uint32_t color) {
  if (x < 0 || x >= FB_WIDTH || y < 0 || y >= FB_HEIGHT) return;

  uint32_t a_fg = (color >> 24) & 0xFF;
  if (a_fg == 0) return;
  if (a_fg == 255) {
    chip->pixels[y * FB_WIDTH + x] = color;
    return;
  }

  uint32_t bg = chip->pixels[y * FB_WIDTH + x];
  uint32_t r_bg = bg & 0xFF;
  uint32_t g_bg = (bg >> 8) & 0xFF;
  uint32_t b_bg = (bg >> 16) & 0xFF;

  uint32_t r_fg = color & 0xFF;
  uint32_t g_fg = (color >> 8) & 0xFF;
  uint32_t b_fg = (color >> 16) & 0xFF;

  uint32_t inv_a = 255 - a_fg;
  uint8_t r = (uint8_t)((r_fg * a_fg + r_bg * inv_a) / 255);
  uint8_t g = (uint8_t)((g_fg * a_fg + g_bg * inv_a) / 255);
  uint8_t b = (uint8_t)((b_fg * a_fg + b_bg * inv_a) / 255);

  chip->pixels[y * FB_WIDTH + x] = rgba(r, g, b, 255);
}

static void draw_rect_filled(pwr_supply_t *chip, int x1, int y1, int x2, int y2, uint32_t color) {
  int min_x = x1 < x2 ? x1 : x2;
  int max_x = x1 < x2 ? x2 : x1;
  int min_y = y1 < y2 ? y1 : y2;
  int max_y = y1 < y2 ? y2 : y1;

  for (int y = min_y; y <= max_y; y++) {
    for (int x = min_x; x <= max_x; x++) {
      set_pixel(chip, x, y, color);
    }
  }
}

static void draw_rect_outline(pwr_supply_t *chip, int x1, int y1, int x2, int y2, uint32_t color) {
  int min_x = x1 < x2 ? x1 : x2;
  int max_x = x1 < x2 ? x2 : x1;
  int min_y = y1 < y2 ? y1 : y2;
  int max_y = y1 < y2 ? y2 : y1;

  for (int x = min_x; x <= max_x; x++) {
    set_pixel(chip, x, min_y, color);
    set_pixel(chip, x, max_y, color);
  }
  for (int y = min_y; y <= max_y; y++) {
    set_pixel(chip, min_x, y, color);
    set_pixel(chip, max_x, y, color);
  }
}

static void draw_circle_filled(pwr_supply_t *chip, int cx, int cy, int radius, uint32_t color) {
  int r2 = radius * radius;
  for (int dy = -radius; dy <= radius; dy++) {
    for (int dx = -radius; dx <= radius; dx++) {
      if (dx * dx + dy * dy <= r2) {
        set_pixel(chip, cx + dx, cy + dy, color);
      }
    }
  }
}

static void draw_circle_outline(pwr_supply_t *chip, int cx, int cy, int radius, uint32_t color) {
  int x = radius;
  int y = 0;
  int err = 0;

  while (x >= y) {
    set_pixel(chip, cx + x, cy + y, color);
    set_pixel(chip, cx + y, cy + x, color);
    set_pixel(chip, cx - y, cy + x, color);
    set_pixel(chip, cx - x, cy + y, color);
    set_pixel(chip, cx - x, cy - y, color);
    set_pixel(chip, cx - y, cy - x, color);
    set_pixel(chip, cx + y, cy - x, color);
    set_pixel(chip, cx + x, cy - y, color);

    if (err <= 0) {
      y += 1;
      err += 2 * y + 1;
    }
    if (err > 0) {
      x -= 1;
      err -= 2 * x + 1;
    }
  }
}

static void draw_glow(pwr_supply_t *chip, int cx, int cy, int radius, uint8_t r, uint8_t g, uint8_t b, uint8_t max_alpha) {
  int r2 = radius * radius;
  for (int dy = -radius; dy <= radius; dy++) {
    int y = cy + dy;
    if (y < 0 || y >= FB_HEIGHT) continue;
    for (int dx = -radius; dx <= radius; dx++) {
      int x = cx + dx;
      if (x < 0 || x >= FB_WIDTH) continue;
      int dist2 = dx * dx + dy * dy;
      if (dist2 <= r2) {
        float dist = __builtin_sqrtf((float)dist2);
        float factor = 1.0f - (dist / (float)radius);
        if (factor > 0.0f) {
          uint8_t a = (uint8_t)(factor * factor * (float)max_alpha);
          blend_pixel(chip, x, y, rgba(r, g, b, a));
        }
      }
    }
  }
}

static void draw_char_5x7(pwr_supply_t *chip, int x, int y, char c, uint32_t color) {
  if (c >= 'a' && c <= 'z') {
    c = (char)(c - 'a' + 'A');
  }

  if (c < 32 || c > 90) return;
  const uint8_t *bitmap = font5x7[c - 32];

  for (int col = 0; col < 5; col++) {
    uint8_t line = bitmap[col];
    for (int row = 0; row < 7; row++) {
      if ((line >> row) & 1) {
        set_pixel(chip, x + col, y + row, color);
      }
    }
  }
}

static void draw_string_5x7(pwr_supply_t *chip, int x, int y, const char *str, uint32_t color) {
  int cur_x = x;
  while (*str) {
    draw_char_5x7(chip, cur_x, y, *str, color);
    cur_x += 6; // 5 pixels width + 1 pixel spacing
    str++;
  }
}

/* Draws small mounting screw at corner */
static void draw_screw(pwr_supply_t *chip, int cx, int cy) {
  draw_circle_filled(chip, cx, cy, 3, rgba(14, 15, 17, 255));
  draw_circle_filled(chip, cx, cy, 2, rgba(135, 140, 145, 255));
  set_pixel(chip, cx, cy, rgba(40, 42, 45, 255));
  set_pixel(chip, cx - 1, cy, rgba(40, 42, 45, 255));
  set_pixel(chip, cx + 1, cy, rgba(40, 42, 45, 255));
  set_pixel(chip, cx, cy - 1, rgba(40, 42, 45, 255));
  set_pixel(chip, cx, cy + 1, rgba(40, 42, 45, 255));
}

/* 5.5 x 2.1 mm metal female DC barrel jack */
static void draw_barrel_jack(pwr_supply_t *chip, int cx, int cy) {
  // Outer chassis hex nut / mounting collar
  draw_circle_filled(chip, cx, cy, 14, rgba(70, 74, 78, 255));
  draw_circle_filled(chip, cx, cy, 12, rgba(115, 120, 126, 255));
  draw_circle_outline(chip, cx, cy, 12, rgba(160, 166, 172, 255));

  // Outer metal barrel sleeve
  draw_circle_filled(chip, cx, cy, 9, rgba(140, 145, 150, 255));
  draw_circle_filled(chip, cx, cy, 7, rgba(25, 26, 28, 255)); // dark insulator rim
  draw_circle_filled(chip, cx, cy, 5, rgba(185, 190, 195, 255)); // inner metal contact

  // Inner cavity socket hole
  draw_circle_filled(chip, cx, cy, 3, rgba(8, 8, 10, 255));

  // Solid brass/gold 2.1mm center pin
  draw_circle_filled(chip, cx, cy, 1, rgba(255, 215, 0, 255));
  set_pixel(chip, cx, cy, rgba(255, 245, 160, 255)); // center reflection
}

/* Polarity Marking Diagram: + ---o)--- - */
static void draw_polarity_diagram(pwr_supply_t *chip, int cx, int cy) {
  uint32_t gold = rgba(255, 215, 0, 255);

  // Left '+' symbol at cx - 14
  for (int dy = -2; dy <= 2; dy++) set_pixel(chip, cx - 14, cy + dy, gold);
  for (int dx = -2; dx <= 2; dx++) set_pixel(chip, cx - 14 + dx, cy, gold);

  // Wire from '+' to center pin dot
  for (int x = cx - 10; x <= cx - 3; x++) set_pixel(chip, x, cy, gold);

  // Center pin dot 'o'
  draw_circle_filled(chip, cx, cy, 1, gold);

  // C-cup around center pin (opening to right)
  for (int dy = -3; dy <= 3; dy++) set_pixel(chip, cx + 3, cy + dy, gold);
  set_pixel(chip, cx + 1, cy - 3, gold);
  set_pixel(chip, cx + 2, cy - 3, gold);
  set_pixel(chip, cx + 1, cy + 3, gold);
  set_pixel(chip, cx + 2, cy + 3, gold);

  // Wire from C-cup to '-' symbol
  for (int x = cx + 3; x <= cx + 10; x++) set_pixel(chip, x, cy, gold);

  // Right '-' symbol at cx + 14
  for (int dx = -2; dx <= 2; dx++) set_pixel(chip, cx + 14 + dx, cy, gold);
}

/* Heavy-duty SPST illuminated rocker switch (ASW-07D) */
static void draw_rocker_switch(pwr_supply_t *chip, int cx, int cy, bool is_on) {
  int x1 = cx - 12;
  int y1 = cy - 20;
  int x2 = cx + 12;
  int y2 = cy + 20;

  // Outer switch frame (black nylon)
  draw_rect_filled(chip, x1 - 2, y1 - 2, x2 + 2, y2 + 2, rgba(16, 17, 18, 255));
  draw_rect_outline(chip, x1 - 2, y1 - 2, x2 + 2, y2 + 2, rgba(42, 44, 48, 255));

  // Rocker well recess
  draw_rect_filled(chip, x1, y1, x2, y2, rgba(8, 8, 10, 255));

  int div_y = cy;

  if (is_on) {
    // Red pilot lamp illumination glow across bezel
    draw_glow(chip, cx, cy - 10, 20, 255, 30, 30, 110);

    // Top section: pressed down into switch & glowing brightly
    draw_rect_filled(chip, x1 + 1, y1 + 1, x2 - 1, div_y - 1, rgba(235, 20, 20, 255));
    // Center pilot lamp filament hot-spot
    draw_rect_filled(chip, cx - 4, cy - 14, cx + 4, cy - 6, rgba(255, 110, 110, 255));
    draw_rect_filled(chip, cx - 2, cy - 12, cx + 2, cy - 8, rgba(255, 230, 230, 255));

    // Embossed 'I' (white)
    draw_char_5x7(chip, cx - 2, cy - 13, 'I', rgba(255, 255, 255, 255));

    // Bottom section: tilted upward and unlit dark maroon
    draw_rect_filled(chip, x1 + 1, div_y + 1, x2 - 1, y2 - 1, rgba(75, 10, 10, 255));
    draw_rect_outline(chip, x1 + 1, div_y + 1, x2 - 1, y2 - 1, rgba(100, 15, 15, 255));
    // Embossed 'O' (muted grey)
    draw_char_5x7(chip, cx - 2, cy + 6, 'O', rgba(150, 150, 150, 255));

  } else {
    // Switch OFF: dark, unlit translucent red
    // Top section: elevated
    draw_rect_filled(chip, x1 + 1, y1 + 1, x2 - 1, div_y - 1, rgba(65, 10, 10, 255));
    draw_rect_outline(chip, x1 + 1, y1 + 1, x2 - 1, div_y - 1, rgba(90, 15, 15, 255));
    draw_char_5x7(chip, cx - 2, cy - 13, 'I', rgba(120, 120, 120, 255));

    // Bottom section: pressed down
    draw_rect_filled(chip, x1 + 1, div_y + 1, x2 - 1, y2 - 1, rgba(90, 14, 14, 255));
    draw_char_5x7(chip, cx - 2, cy + 6, 'O', rgba(220, 220, 220, 255));
  }
}

/* 5mm amber/yellow 12V active power rail indicator LED */
static void draw_active_led(pwr_supply_t *chip, int cx, int cy, bool is_on) {
  // LED chrome bezel holder
  draw_circle_filled(chip, cx, cy, 6, rgba(45, 48, 52, 255));
  draw_circle_outline(chip, cx, cy, 6, rgba(150, 155, 160, 255));

  if (is_on) {
    // Glowing radiant amber halo
    draw_glow(chip, cx, cy, 14, 255, 180, 20, 140);

    // 5mm LED dome (intense amber-yellow hot-spot)
    draw_circle_filled(chip, cx, cy, 4, rgba(240, 135, 0, 255));
    draw_circle_filled(chip, cx, cy, 3, rgba(255, 195, 0, 255));
    draw_circle_filled(chip, cx, cy, 1, rgba(255, 255, 210, 255)); // white-hot filament center
  } else {
    // Dark unlit amber tinted resin
    draw_circle_filled(chip, cx, cy, 4, rgba(58, 34, 14, 255));
    draw_circle_outline(chip, cx, cy, 4, rgba(38, 20, 8, 255));
    // Specular glint
    set_pixel(chip, cx - 1, cy - 1, rgba(120, 85, 55, 255));
  }
}

/* 1A inline fast-acting fuse representation */
static void draw_fuse(pwr_supply_t *chip, int cx, int cy) {
  int w = 28;
  int h = 8;
  int x1 = cx - w / 2;
  int y1 = cy - h / 2;
  int x2 = cx + w / 2;
  int y2 = cy + h / 2;

  // Fuse clips / background holder
  draw_rect_filled(chip, x1 - 1, y1 - 1, x2 + 1, y2 + 1, rgba(14, 15, 17, 255));

  // Left nickel cap
  draw_rect_filled(chip, x1, y1, x1 + 5, y2, rgba(175, 180, 186, 255));
  draw_rect_outline(chip, x1, y1, x1 + 5, y2, rgba(210, 215, 220, 255));

  // Right nickel cap
  draw_rect_filled(chip, x2 - 5, y1, x2, y2, rgba(175, 180, 186, 255));
  draw_rect_outline(chip, x2 - 5, y1, x2, y2, rgba(210, 215, 220, 255));

  // Center glass cartridge
  draw_rect_filled(chip, x1 + 6, y1 + 1, x2 - 6, y2 - 1, rgba(24, 28, 32, 255));
  // Glass reflection streak
  for (int x = x1 + 7; x <= x2 - 7; x++) {
    set_pixel(chip, x, y1 + 2, rgba(90, 120, 150, 140));
  }

  // Internal fuse wire
  for (int x = x1 + 5; x <= x2 - 5; x++) {
    set_pixel(chip, x, cy, rgba(225, 228, 232, 255));
  }
  // Fuse blowout bead / arc element in center
  set_pixel(chip, cx, cy, rgba(255, 215, 0, 255));
  set_pixel(chip, cx, cy - 1, rgba(255, 215, 0, 255));
}

/* Full faceplate rendering pass */
static void render_faceplate(pwr_supply_t *chip) {
  // 1. Black anodized aluminum enclosure / faceplate base
  for (int y = 0; y < FB_HEIGHT; y++) {
    for (int x = 0; x < FB_WIDTH; x++) {
      uint8_t tone = (uint8_t)(20 + ((x ^ y) & 1) * 2);
      chip->pixels[y * FB_WIDTH + x] = rgba(tone, tone + 1, tone + 2, 255);
    }
  }

  // Beveled outer edge
  for (int x = 0; x < FB_WIDTH; x++) {
    set_pixel(chip, x, 0, rgba(62, 65, 70, 255));
    set_pixel(chip, x, 1, rgba(42, 45, 48, 255));
    set_pixel(chip, x, FB_HEIGHT - 1, rgba(10, 11, 12, 255));
  }
  for (int y = 0; y < FB_HEIGHT; y++) {
    set_pixel(chip, 0, y, rgba(62, 65, 70, 255));
    set_pixel(chip, 1, y, rgba(42, 45, 48, 255));
    set_pixel(chip, FB_WIDTH - 1, y, rgba(10, 11, 12, 255));
  }

  // Corner mounting screws
  draw_screw(chip, 5, 5);
  draw_screw(chip, FB_WIDTH - 6, 5);
  draw_screw(chip, 5, FB_HEIGHT - 6);
  draw_screw(chip, FB_WIDTH - 6, FB_HEIGHT - 6);

  // Dividing decorative groove line
  for (int x = 12; x < FB_WIDTH - 12; x++) {
    set_pixel(chip, x, 14, rgba(12, 13, 14, 255));
    set_pixel(chip, x, 15, rgba(42, 45, 48, 255));
  }

  // 2. 5.5x2.1mm metal female DC barrel jack (Center at X=28, Y=34)
  draw_barrel_jack(chip, 28, 34);

  // Gold polarity marking: + ---o)--- - (at Y=53)
  draw_polarity_diagram(chip, 28, 53);

  // Clear white silkscreen labels for DC barrel jack
  draw_string_5x7(chip, 7, 63, "12V DC IN", rgba(235, 238, 242, 255));
  draw_string_5x7(chip, 13, 73, "(2.1mm)", rgba(200, 205, 210, 255));

  // 3. Heavy-duty SPST illuminated rocker switch (ASW-07D) (Center at X=70, Y=42)
  draw_rocker_switch(chip, 70, 42, chip->power_state);
  // Silkscreen label above switch: "MAIN POWER" (centered around X=70)
  draw_string_5x7(chip, 40, 5, "MAIN POWER", rgba(235, 238, 242, 255));

  // 4. 5mm amber/yellow 12V live rail LED (Center at X=113, Y=26)
  draw_active_led(chip, 113, 26, chip->power_state);
  // Silkscreen label below LED: "12V LIVE"
  draw_string_5x7(chip, 89, 38, "12V LIVE", rgba(235, 238, 242, 255));

  // 5. 1A inline fuse representation (Center at X=113, Y=56)
  draw_fuse(chip, 113, 56);
  // Silkscreen label below fuse: "FUSE 1A"
  draw_string_5x7(chip, 92, 68, "FUSE 1A", rgba(235, 238, 242, 255));

  // Push to Wokwi framebuffer
  if (chip->fb) {
    buffer_write(chip->fb, 0, (uint8_t *)chip->pixels, FB_WIDTH * FB_HEIGHT * 4);
  }
}

static bool is_power_active(pwr_supply_t *chip) {
  uint32_t val = attr_read(chip->attr_power);
  float fval = attr_read_float(chip->attr_power);
  return (val > 0) || (fval > 0.5f);
}

static void update_circuit_state(pwr_supply_t *chip) {
  bool new_state = is_power_active(chip);

  if (new_state != chip->power_state) {
    chip->power_state = new_state;

    // Drive 12V_OUT HIGH (1) when closed/ON, LOW (0) when open/OFF
    uint32_t pin_val = chip->power_state ? HIGH : LOW;
    pin_write(chip->pin_12v_out, pin_val);
    if (chip->pin_12v != NO_PIN) {
      pin_write(chip->pin_12v, pin_val);
    }

    // Common ground passes through to GND (0V)
    pin_write(chip->pin_gnd, LOW);

    // Re-render display with updated switch glow and LED state
    render_faceplate(chip);
  }
}

static void on_poll_timer(void *user_data) {
  pwr_supply_t *chip = (pwr_supply_t *)user_data;
  update_circuit_state(chip);
  timer_start(chip->poll_timer, 20000, false); // 20ms poll (50 Hz)
}

void chip_init(void) {
  pwr_supply_t *chip = (pwr_supply_t *)calloc(1, sizeof(pwr_supply_t));

  // Initialize output and ground pins
  chip->pin_12v_out = pin_init("12V_OUT", OUTPUT_HIGH);
  chip->pin_gnd     = pin_init("GND", INPUT_PULLDOWN);
  chip->pin_12v     = pin_init("12V", OUTPUT_HIGH);

  // Interactive control 'power' (toggle switch, range 0/1, default 1)
  chip->attr_power = attr_init("power", 1);
  chip->power_state = is_power_active(chip);

  // Set initial electrical pin state
  uint32_t init_val = chip->power_state ? HIGH : LOW;
  pin_write(chip->pin_12v_out, init_val);
  if (chip->pin_12v != NO_PIN) {
    pin_write(chip->pin_12v, init_val);
  }
  pin_write(chip->pin_gnd, LOW);

  // Initialize attached 140x90 framebuffer display
  chip->width = FB_WIDTH;
  chip->height = FB_HEIGHT;
  chip->fb = framebuffer_init(&chip->width, &chip->height);

  // Initial graphic render
  render_faceplate(chip);

  // Periodic poll timer to monitor interactive switch changes
  const timer_config_t timer_cfg = {
    .callback = on_poll_timer,
    .user_data = chip,
  };
  chip->poll_timer = timer_init(&timer_cfg);
  timer_start(chip->poll_timer, 20000, false);

  }

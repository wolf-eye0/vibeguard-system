static void simple_itoa(int val, char *buf) {
  if (val == 0) { buf[0] = '0'; buf[1] = 0; return; }
  char temp[16];
  int i = 0, j = 0;
  if (val < 0) { buf[j++] = '-'; val = -val; }
  while (val > 0) { temp[i++] = (char)('0' + (val % 10)); val /= 10; }
  while (i > 0) { buf[j++] = temp[--i]; }
  buf[j] = 0;
}
#include "wokwi-api.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Standard libc function declarations avoiding local features.h collision */
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void free(void *ptr);
void *memset(void *s, int c, size_t n);


float sinf(float x);
float cosf(float x);
float atan2f(float y, float x);
float sqrtf(float x);
float fabsf(float x);
float roundf(float x);
float fmodf(float x, float y);

static inline char to_upper_char(char c) {
  if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
  return c;
}

#define FB_WIDTH   160
#define FB_HEIGHT  100

#define COLOR_RGBA(r, g, b, a) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))
#define COLOR_RGB(r, g, b)     COLOR_RGBA(r, g, b, 255)

// Basic 5x7 ASCII bitmap font (characters 32 to 90: ' ' to 'Z')
static const uint8_t FONT_5X7[59][5] = {
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 32 ' '
  {0x00, 0x00, 0x5f, 0x00, 0x00}, // 33 '!'
  {0x00, 0x07, 0x00, 0x07, 0x00}, // 34 '"'
  {0x14, 0x7f, 0x14, 0x7f, 0x14}, // 35 '#'
  {0x24, 0x2a, 0x7f, 0x2a, 0x12}, // 36 '$'
  {0x23, 0x13, 0x08, 0x64, 0x62}, // 37 '%'
  {0x36, 0x49, 0x55, 0x22, 0x50}, // 38 '&'
  {0x00, 0x05, 0x03, 0x00, 0x00}, // 39 '\''
  {0x00, 0x1c, 0x22, 0x41, 0x00}, // 40 '('
  {0x00, 0x41, 0x22, 0x1c, 0x00}, // 41 ')'
  {0x14, 0x08, 0x3e, 0x08, 0x14}, // 42 '*'
  {0x08, 0x08, 0x3e, 0x08, 0x08}, // 43 '+'
  {0x00, 0x50, 0x30, 0x00, 0x00}, // 44 ','
  {0x08, 0x08, 0x08, 0x08, 0x08}, // 45 '-'
  {0x00, 0x60, 0x60, 0x00, 0x00}, // 46 '.'
  {0x20, 0x10, 0x08, 0x04, 0x02}, // 47 '/'
  {0x3e, 0x51, 0x49, 0x45, 0x3e}, // 48 '0'
  {0x00, 0x42, 0x7f, 0x40, 0x00}, // 49 '1'
  {0x42, 0x61, 0x51, 0x49, 0x46}, // 50 '2'
  {0x21, 0x41, 0x45, 0x4b, 0x31}, // 51 '3'
  {0x18, 0x14, 0x12, 0x7f, 0x10}, // 52 '4'
  {0x27, 0x45, 0x45, 0x45, 0x39}, // 53 '5'
  {0x3c, 0x4a, 0x49, 0x49, 0x30}, // 54 '6'
  {0x01, 0x71, 0x09, 0x05, 0x03}, // 55 '7'
  {0x36, 0x49, 0x49, 0x49, 0x36}, // 56 '8'
  {0x06, 0x49, 0x49, 0x29, 0x1e}, // 57 '9'
  {0x00, 0x36, 0x36, 0x00, 0x00}, // 58 ':'
  {0x00, 0x56, 0x36, 0x00, 0x00}, // 59 ';'
  {0x08, 0x14, 0x22, 0x41, 0x00}, // 60 '<'
  {0x14, 0x14, 0x14, 0x14, 0x14}, // 61 '='
  {0x00, 0x41, 0x22, 0x14, 0x08}, // 62 '>'
  {0x02, 0x01, 0x51, 0x09, 0x06}, // 63 '?'
  {0x32, 0x49, 0x79, 0x41, 0x3e}, // 64 '@'
  {0x7e, 0x11, 0x11, 0x11, 0x7e}, // 65 'A'
  {0x7f, 0x49, 0x49, 0x49, 0x36}, // 66 'B'
  {0x3e, 0x41, 0x41, 0x41, 0x22}, // 67 'C'
  {0x7f, 0x41, 0x41, 0x22, 0x1c}, // 68 'D'
  {0x7f, 0x49, 0x49, 0x49, 0x41}, // 69 'E'
  {0x7f, 0x09, 0x09, 0x09, 0x01}, // 70 'F'
  {0x3e, 0x41, 0x49, 0x49, 0x7a}, // 71 'G'
  {0x7f, 0x08, 0x08, 0x08, 0x7f}, // 72 'H'
  {0x00, 0x41, 0x7f, 0x41, 0x00}, // 73 'I'
  {0x20, 0x40, 0x41, 0x3f, 0x01}, // 74 'J'
  {0x7f, 0x08, 0x14, 0x22, 0x41}, // 75 'K'
  {0x7f, 0x40, 0x40, 0x40, 0x40}, // 76 'L'
  {0x7f, 0x02, 0x0c, 0x02, 0x7f}, // 77 'M'
  {0x7f, 0x04, 0x08, 0x10, 0x7f}, // 78 'N'
  {0x3e, 0x41, 0x41, 0x41, 0x3e}, // 79 'O'
  {0x7f, 0x09, 0x09, 0x09, 0x06}, // 80 'P'
  {0x3e, 0x41, 0x51, 0x21, 0x5e}, // 81 'Q'
  {0x7f, 0x09, 0x19, 0x29, 0x46}, // 82 'R'
  {0x46, 0x49, 0x49, 0x49, 0x31}, // 83 'S'
  {0x01, 0x01, 0x7f, 0x01, 0x01}, // 84 'T'
  {0x3f, 0x40, 0x40, 0x40, 0x3f}, // 85 'U'
  {0x1f, 0x20, 0x40, 0x20, 0x1f}, // 86 'V'
  {0x3f, 0x40, 0x38, 0x40, 0x3f}, // 87 'W'
  {0x63, 0x14, 0x08, 0x14, 0x63}, // 88 'X'
  {0x07, 0x08, 0x70, 0x08, 0x07}, // 89 'Y'
  {0x61, 0x51, 0x49, 0x45, 0x43}  // 90 'Z'
};

typedef struct {
  pin_t pin_12v_in;
  pin_t pin_12v;
  pin_t pin_gnd;
  pin_t pin_tach_out;
  pin_t pin_tach;

  uint32_t attr_rpm;
  uint32_t attr_imbalance;

  timer_t pulse_timer;
  bool tach_state;
  bool is_running;
  float rotation_angle;

  buffer_t fb;
  uint32_t fb_width;
  uint32_t fb_height;
  uint32_t pixels[FB_WIDTH * FB_HEIGHT];
} n20_motor_t;

// Forward declarations
static void render_motor_display(n20_motor_t *chip);
static void on_pulse_timer(void *user_data);
static void on_12v_change(void *user_data, pin_t pin, uint32_t value);

static inline void set_pixel(n20_motor_t *chip, int x, int y, uint32_t color) {
  if (x >= 0 && x < FB_WIDTH && y >= 0 && y < FB_HEIGHT) {
    chip->pixels[y * FB_WIDTH + x] = color;
  }
}

static void fill_rect(n20_motor_t *chip, int x0, int y0, int w, int h, uint32_t color) {
  int x1 = x0 + w;
  int y1 = y0 + h;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > FB_WIDTH) x1 = FB_WIDTH;
  if (y1 > FB_HEIGHT) y1 = FB_HEIGHT;

  for (int y = y0; y < y1; y++) {
    for (int x = x0; x < x1; x++) {
      chip->pixels[y * FB_WIDTH + x] = color;
    }
  }
}

static void fill_circle(n20_motor_t *chip, int cx, int cy, int r, uint32_t color) {
  int r2 = r * r;
  for (int dy = -r; dy <= r; dy++) {
    for (int dx = -r; dx <= r; dx++) {
      if (dx * dx + dy * dy <= r2) {
        set_pixel(chip, cx + dx, cy + dy, color);
      }
    }
  }
}

static void draw_circle(n20_motor_t *chip, int cx, int cy, int r, uint32_t color) {
  for (int a = 0; a < 360; a += 3) {
    float rad = (float)a * 0.0174532925f;
    int x = (int)roundf((float)cx + (float)r * cosf(rad));
    int y = (int)roundf((float)cy + (float)r * sinf(rad));
    set_pixel(chip, x, y, color);
  }
}

static void draw_char(n20_motor_t *chip, int x, int y, char c, uint32_t color) {
  char uc = to_upper_char(c);
  if (uc < 32 || uc > 90) return;
  int idx = uc - 32;
  for (int col = 0; col < 5; col++) {
    uint8_t line = FONT_5X7[idx][col];
    for (int row = 0; row < 7; row++) {
      if (line & (1 << row)) {
        set_pixel(chip, x + col, y + row, color);
      }
    }
  }
}

static void draw_string(n20_motor_t *chip, int x, int y, const char *str, uint32_t color) {
  if (!str) return;
  int cur_x = x;
  while (*str) {
    draw_char(chip, cur_x, y, *str, color);
    cur_x += 6;
    str++;
  }
}

static bool is_12v_active(n20_motor_t *chip) {
  if (chip->pin_12v_in != NO_PIN && pin_read(chip->pin_12v_in) == HIGH) {
    return true;
  }
  if (chip->pin_12v != NO_PIN && pin_read(chip->pin_12v) == HIGH) {
    return true;
  }
  return false;
}

static void write_tach(n20_motor_t *chip, uint32_t val) {
  if (chip->pin_tach_out != NO_PIN) {
    pin_write(chip->pin_tach_out, val);
  }
  if (chip->pin_tach != NO_PIN) {
    pin_write(chip->pin_tach, val);
  }
}

static void render_motor_display(n20_motor_t *chip) {
  if (!chip) return;

  // 1. Clear backdrop: dark slate background with engineering grid
  fill_rect(chip, 0, 0, FB_WIDTH, FB_HEIGHT, COLOR_RGB(15, 23, 42));

  for (int y = 14; y < 86; y += 8) {
    for (int x = 4; x < FB_WIDTH - 4; x += 8) {
      set_pixel(chip, x, y, COLOR_RGB(30, 41, 59));
    }
  }

  // 2. Header and Footer Panels
  fill_rect(chip, 0, 0, FB_WIDTH, 13, COLOR_RGB(10, 15, 30));
  fill_rect(chip, 0, 13, FB_WIDTH, 1, COLOR_RGB(51, 65, 85));

  fill_rect(chip, 0, 87, FB_WIDTH, 13, COLOR_RGB(10, 15, 30));
  fill_rect(chip, 0, 86, FB_WIDTH, 1, COLOR_RGB(51, 65, 85));

  // Header Title
  draw_string(chip, 4, 3, "12V N20 MOTOR", COLOR_RGB(56, 189, 248));

  // Read current RPM and Imbalance
  float rpm_val = attr_read_float(chip->attr_rpm);
  if (rpm_val < 100.0f) rpm_val = 600.0f;
  if (rpm_val < 300.0f) rpm_val = 300.0f;
  if (rpm_val > 1200.0f) rpm_val = 1200.0f;

  float imb_val = attr_read_float(chip->attr_imbalance);
  if (imb_val < 0.0f) imb_val = 0.0f;
  if (imb_val > 5.0f) imb_val = 5.0f;

  char rpm_buf[20];
  char num_buf[16];
  simple_itoa((int)roundf(rpm_val), num_buf);
  int r_idx = 0;
  for (int k = 0; num_buf[k]; k++) rpm_buf[r_idx++] = num_buf[k];
  rpm_buf[r_idx++] = ' ';
  rpm_buf[r_idx++] = 'R';
  rpm_buf[r_idx++] = 'P';
  rpm_buf[r_idx++] = 'M';
  rpm_buf[r_idx] = 0;
  draw_string(chip, 108, 3, rpm_buf, COLOR_RGB(251, 191, 36));

  // Footer Status Indicator
  if (chip->is_running) {
    fill_circle(chip, 8, 93, 3, COLOR_RGB(34, 197, 94));
    draw_string(chip, 15, 90, "RUNNING", COLOR_RGB(34, 197, 94));
  } else {
    fill_circle(chip, 8, 93, 3, COLOR_RGB(239, 68, 68));
    draw_string(chip, 15, 90, "STOPPED", COLOR_RGB(239, 68, 68));
  }

  char imb_buf[20];
  int imb_int = (int)imb_val;
  int imb_dec = (int)((imb_val - (float)imb_int) * 10.0f + 0.5f);
  char i_int[8], i_dec[8];
  simple_itoa(imb_int, i_int);
  simple_itoa(imb_dec, i_dec);
  int m_idx = 0;
  imb_buf[m_idx++] = 'M';
  imb_buf[m_idx++] = '=';
  for (int k = 0; i_int[k]; k++) imb_buf[m_idx++] = i_int[k];
  imb_buf[m_idx++] = '.';
  for (int k = 0; i_dec[k]; k++) imb_buf[m_idx++] = i_dec[k];
  imb_buf[m_idx++] = 'g';
  imb_buf[m_idx] = 0;
  draw_string(chip, 110, 90, imb_buf, COLOR_RGB(203, 213, 225));

  // =========================================================================
  // VIEW 1: Side Elevation Profile of N20 Motor Assembly (Left Half: X = 4..92)
  // =========================================================================

  // 1a. Robocraze JST Wire Harness (Red 12V, Black GND)
  // Red wire (12V)
  fill_rect(chip, 4, 43, 13, 2, COLOR_RGB(220, 38, 38));
  fill_rect(chip, 4, 43, 13, 1, COLOR_RGB(248, 113, 113));
  // Black wire (GND)
  fill_rect(chip, 4, 55, 13, 2, COLOR_RGB(30, 41, 59));
  fill_rect(chip, 4, 55, 13, 1, COLOR_RGB(71, 85, 105));

  // JST Connector housing (ivory nylon)
  fill_rect(chip, 15, 40, 5, 20, COLOR_RGB(241, 245, 249));
  fill_rect(chip, 19, 42, 1, 16, COLOR_RGB(203, 213, 225));

  // 1b. Copper Terminal Solder Lugs & Solder Joints
  fill_rect(chip, 20, 42, 4, 3, COLOR_RGB(180, 83, 9));  // Upper lug
  fill_rect(chip, 20, 55, 4, 3, COLOR_RGB(180, 83, 9));  // Lower lug
  fill_circle(chip, 20, 43, 2, COLOR_RGB(203, 213, 225)); // Solder fillet
  fill_circle(chip, 20, 56, 2, COLOR_RGB(203, 213, 225)); // Solder fillet

  // 1c. Black Composite Rear End-Cap & Bearing Boss
  fill_rect(chip, 24, 34, 4, 32, COLOR_RGB(30, 35, 45));
  fill_rect(chip, 22, 47, 2, 6, COLOR_RGB(45, 52, 65));

  // 1d. Cylindrical Brushed Steel Motor Casing (Nickel/Steel Specular Look)
  // Dimensions: X = 28 to 58, Y = 32 to 68 (Height: 36px ~ 12mm)
  for (int y = 32; y <= 68; y++) {
    float norm_y = (float)(y - 50) / 18.0f; // -1.0 to +1.0
    float abs_y = fabsf(norm_y);

    uint8_t r_col, g_col, b_col;
    if (norm_y >= -0.55f && norm_y <= -0.15f) {
      // Specular highlight band
      float t = 1.0f - fabsf((norm_y + 0.35f) / 0.20f);
      if (t < 0.0f) t = 0.0f;
      r_col = (uint8_t)(210 + 40 * t);
      g_col = (uint8_t)(220 + 35 * t);
      b_col = (uint8_t)(235 + 20 * t);
    } else if (abs_y > 0.85f) {
      // Cylinder top/bottom edge shadow
      r_col = 70;
      g_col = 80;
      b_col = 95;
    } else {
      // Midtone brushed steel
      float t = 1.0f - abs_y;
      r_col = (uint8_t)(110 + 90 * t);
      g_col = (uint8_t)(120 + 90 * t);
      b_col = (uint8_t)(135 + 90 * t);
    }

    // Horizontal micro-striations for brushed look
    for (int x = 28; x <= 58; x++) {
      int jitter = ((x * 7 + y * 13) % 7) - 3;
      int r_j = r_col + jitter;
      int g_j = g_col + jitter;
      int b_j = b_col + jitter;
      if (r_j < 0) r_j = 0; else if (r_j > 255) r_j = 255;
      if (g_j < 0) g_j = 0; else if (g_j > 255) g_j = 255;
      if (b_j < 0) b_j = 0; else if (b_j > 255) b_j = 255;
      set_pixel(chip, x, y, COLOR_RGB(r_j, g_j, b_j));
    }
  }
  // Motor casing bevel lines
  fill_rect(chip, 28, 32, 31, 1, COLOR_RGB(50, 60, 75));
  fill_rect(chip, 28, 68, 31, 1, COLOR_RGB(50, 60, 75));

  // 1e. Steel-to-Brass Seam Collar
  fill_rect(chip, 59, 33, 2, 34, COLOR_RGB(78, 65, 30));

  // 1f. Solid Extruded Machined Brass Gearbox Housing
  // Dimensions: X = 61 to 81, Y = 34 to 66 (Height: 32px ~ 10mm)
  for (int y = 34; y <= 66; y++) {
    float norm_y = (float)(y - 50) / 16.0f;
    float abs_y = fabsf(norm_y);

    uint8_t r_b, g_b, b_b;
    if (norm_y >= -0.45f && norm_y <= -0.10f) {
      // Golden brass highlight
      r_b = 255; g_b = 238; b_b = 130;
    } else if (abs_y > 0.82f) {
      // Edge bronze shadow
      r_b = 140; g_b = 105; b_b = 25;
    } else {
      // Polished rich brass #D4AF37
      float t = 1.0f - abs_y;
      r_b = (uint8_t)(175 + 50 * t);
      g_b = (uint8_t)(135 + 50 * t);
      b_b = (uint8_t)(35 + 25 * t);
    }
    for (int x = 61; x <= 81; x++) {
      set_pixel(chip, x, y, COLOR_RGB(r_b, g_b, b_b));
    }
  }
  // Gearbox front faceplate ridge
  fill_rect(chip, 80, 33, 2, 34, COLOR_RGB(245, 215, 95));
  fill_rect(chip, 61, 34, 21, 1, COLOR_RGB(130, 95, 20));
  fill_rect(chip, 61, 66, 21, 1, COLOR_RGB(130, 95, 20));

  // 1g. Stainless Steel Output D-Shaft & Clamped Eccentric Hub (Side Profile)
  // D-shaft extending forward: X = 82 to 90, Y = 47 to 53
  fill_rect(chip, 82, 47, 8, 6, COLOR_RGB(203, 213, 225));
  fill_rect(chip, 82, 48, 8, 2, COLOR_RGB(248, 250, 252)); // Highlight

  // Brass Eccentric Hub Cylinder in side view: X = 85 to 92, Y = 36 to 64
  fill_rect(chip, 85, 36, 8, 28, COLOR_RGB(200, 160, 40));
  fill_rect(chip, 85, 39, 8, 4, COLOR_RGB(250, 230, 120)); // Brass sheen
  fill_rect(chip, 85, 36, 8, 1, COLOR_RGB(130, 95, 20));
  fill_rect(chip, 85, 63, 8, 1, COLOR_RGB(130, 95, 20));

  // M3 Grub Screw tapped into hub
  fill_rect(chip, 88, 35, 2, 3, COLOR_RGB(60, 60, 70));

  // Off-axis bolt oscillating head in side view
  float theta = chip->rotation_angle;
  int bolt_y_side = 50 + (int)roundf(10.5f * sinf(theta));
  fill_rect(chip, 92, bolt_y_side - 3, 3, 6, COLOR_RGB(225, 29, 72)); // Anodized red bolt head
  fill_rect(chip, 94, bolt_y_side - 1, 1, 3, COLOR_RGB(37, 99, 235)); // Nyloc ring

  // Divider line between side elevation and front axial view
  fill_rect(chip, 98, 16, 1, 68, COLOR_RGB(51, 65, 85));

  // =========================================================================
  // VIEW 2: Front Axial View (Looking Down Shaft Axis) (Center: X=128, Y=50)
  // =========================================================================
  int cx = 128;
  int cy = 50;

  // 2a. Extruded Brass Gearbox Front Faceplate (48 x 40 px ~ 12 x 10 mm)
  fill_rect(chip, cx - 24, cy - 20, 48, 40, COLOR_RGB(195, 155, 45));
  fill_rect(chip, cx - 23, cy - 19, 46, 38, COLOR_RGB(212, 175, 55));
  // Faceplate inner chamfer boundary
  fill_rect(chip, cx - 21, cy - 17, 42, 34, COLOR_RGB(225, 190, 70));

  // 2b. Two M1.6 Front Mounting Screw Holes Spaced 9mm Apart (at +-18px = 36px span)
  int hole1_x = cx - 18;
  int hole2_x = cx + 18;
  // Left M1.6 hole
  fill_circle(chip, hole1_x, cy, 4, COLOR_RGB(160, 125, 30));
  fill_circle(chip, hole1_x, cy, 3, COLOR_RGB(15, 15, 20));
  draw_circle(chip, hole1_x, cy, 2, COLOR_RGB(71, 85, 105)); // Internal screw thread ridge
  draw_string(chip, hole1_x - 7, cy + 9, "M1.6", COLOR_RGB(160, 130, 40));

  // Right M1.6 hole
  fill_circle(chip, hole2_x, cy, 4, COLOR_RGB(160, 125, 30));
  fill_circle(chip, hole2_x, cy, 3, COLOR_RGB(15, 15, 20));
  draw_circle(chip, hole2_x, cy, 2, COLOR_RGB(71, 85, 105)); // Internal screw thread ridge
  draw_string(chip, hole2_x - 7, cy + 9, "M1.6", COLOR_RGB(160, 130, 40));

  // 2c. Central Bearing Boss
  fill_circle(chip, cx, cy, 8, COLOR_RGB(180, 140, 35));
  fill_circle(chip, cx, cy, 7, COLOR_RGB(100, 115, 130)); // Steel bearing race
  fill_circle(chip, cx, cy, 6, COLOR_RGB(203, 213, 225));

  // 2d. Solid Machined Brass Eccentric Unbalance Hub (Rotating Cylinder, Radius 15px)
  for (int dy = -15; dy <= 15; dy++) {
    for (int dx = -15; dx <= 15; dx++) {
      int dist2 = dx * dx + dy * dy;
      if (dist2 <= 15 * 15) {
        float phi = atan2f((float)dy, (float)dx);
        float delta = phi - theta;
        float spec = 0.5f + 0.5f * cosf(delta);

        uint8_t r_h = (uint8_t)(180 + 70 * spec);
        uint8_t g_h = (uint8_t)(140 + 65 * spec);
        uint8_t b_h = (uint8_t)(30 + 45 * spec);

        // Darken outer chamfer rim
        if (dist2 > 13 * 13) {
          r_h = (uint8_t)(r_h * 0.75f);
          g_h = (uint8_t)(g_h * 0.75f);
          b_h = (uint8_t)(b_h * 0.75f);
        }
        set_pixel(chip, cx + dx, cy + dy, COLOR_RGB(r_h, g_h, b_h));
      }
    }
  }

  // 2e. Stainless Steel Precision D-Shaft (Diameter 3mm ~ Radius 4.5px with Flat Profile)
  for (int dy = -4; dy <= 4; dy++) {
    for (int dx = -4; dx <= 4; dx++) {
      int dist2 = dx * dx + dy * dy;
      if (dist2 <= 18) {
        // Compute distance perpendicular to flat oriented along theta
        float d_proj = (float)dx * cosf(theta) + (float)dy * sinf(theta);
        if (d_proj <= 2.5f) {
          // Steel shaft body
          set_pixel(chip, cx + dx, cy + dy, COLOR_RGB(226, 232, 240));
        } else {
          // Flat profile chord ground notch
          set_pixel(chip, cx + dx, cy + dy, COLOR_RGB(100, 116, 139));
        }
      }
    }
  }

  // 2f. Radial M3 Set-Screw Hole on Brass Hub
  int grub_x = cx - (int)roundf(8.0f * cosf(theta));
  int grub_y = cy - (int)roundf(8.0f * sinf(theta));
  fill_circle(chip, grub_x, grub_y, 2, COLOR_RGB(40, 45, 55));
  set_pixel(chip, grub_x, grub_y, COLOR_RGB(15, 15, 20));

  // 2g. Off-Axis M3 Hex Bolt with Nyloc Nut at Radius r = 6.5mm (11 px)
  float bolt_fx = (float)cx + 11.0f * cosf(theta);
  float bolt_fy = (float)cy + 11.0f * sinf(theta);
  int bx = (int)roundf(bolt_fx);
  int by = (int)roundf(bolt_fy);

  // Draw hexagonal M3 bolt head with anodized red unbalance mass coloring
  for (int dy = -5; dy <= 5; dy++) {
    for (int dx = -5; dx <= 5; dx++) {
      int bdist2 = dx * dx + dy * dy;
      if (bdist2 <= 20) {
        float beta = atan2f((float)dy, (float)dx) - theta;
        float angle_mod = fmodf(beta + 3.14159265f, 1.04719755f) - 0.52359877f;
        float r_hex = 4.2f / cosf(angle_mod);

        if (sqrtf((float)bdist2) <= r_hex) {
          // Anodized Red Hex Bolt Head
          uint8_t r_bolt = 225;
          uint8_t g_bolt = 29;
          uint8_t b_bolt = 72;

          if (dx * cosf(theta) + dy * sinf(theta) < -1.0f) {
            r_bolt = 251; g_bolt = 113; b_bolt = 133; // Specular highlight
          } else if (dx * cosf(theta) + dy * sinf(theta) > 1.0f) {
            r_bolt = 159; g_bolt = 18; b_bolt = 57;   // Shadow edge
          }

          set_pixel(chip, bx + dx, by + dy, COLOR_RGB(r_bolt, g_bolt, b_bolt));
        }
      }
    }
  }

  // Nyloc Nut Ring (Blue Nylon locking collar)
  fill_circle(chip, bx, by, 2, COLOR_RGB(37, 99, 235));
  // Center bolt threaded core
  set_pixel(chip, bx, by, COLOR_RGB(51, 65, 85));

  // 2h. Radial Centrifugal Excitation Vector Tick Mark on Hub Rim
  int tick_x = cx + (int)roundf(14.0f * cosf(theta));
  int tick_y = cy + (int)roundf(14.0f * sinf(theta));
  set_pixel(chip, tick_x, tick_y, COLOR_RGB(255, 255, 255));

  // 3. Commit Framebuffer to Host Display
  if (chip->fb) {
    buffer_write(chip->fb, 0, (uint8_t *)chip->pixels, FB_WIDTH * FB_HEIGHT * sizeof(uint32_t));
  }
}

static void on_pulse_timer(void *user_data) {
  n20_motor_t *chip = (n20_motor_t *)user_data;
  if (!chip) return;

  // Check if 12V power is active
  bool active = is_12v_active(chip);
  if (!active) {
    chip->is_running = false;
    chip->tach_state = false;
    write_tach(chip, LOW);
    // Arm timer to re-check power every 50ms
    timer_start(chip->pulse_timer, 50000, false);
    render_motor_display(chip);
    return;
  }

  chip->is_running = true;

  // Read current RPM slider
  float rpm_val = attr_read_float(chip->attr_rpm);
  if (rpm_val < 100.0f) rpm_val = 600.0f; // Underflow clamp (tested by T2.15)
  if (rpm_val < 300.0f) rpm_val = 300.0f;
  if (rpm_val > 1200.0f) rpm_val = 1200.0f;

  // Toggle tachometer pin to simulate 1 pulse per revolution (PPR = 1)
  chip->tach_state = !chip->tach_state;
  write_tach(chip, chip->tach_state ? HIGH : LOW);

  // Dynamic visual rotation: update rotation angle on timer ticks
  chip->rotation_angle += 0.5235987756f; // ~30 degrees per tick
  if (chip->rotation_angle >= 6.283185307f) {
    chip->rotation_angle -= 6.283185307f;
  }

  // Precision square-wave half-period: (30 / RPM) * 1,000,000 us
  // At 600 RPM: (30 / 600) * 1e6 = 50,000 us (10.0 Hz)
  uint32_t half_period_us = (uint32_t)((30.0f / rpm_val) * 1000000.0f);
  if (half_period_us < 5000) half_period_us = 5000;

  timer_start(chip->pulse_timer, half_period_us, false);
  render_motor_display(chip);
}

static void on_12v_change(void *user_data, pin_t pin, uint32_t value) {
  (void)pin;
  (void)value;
  n20_motor_t *chip = (n20_motor_t *)user_data;
  if (!chip) return;

  if (is_12v_active(chip)) {
    if (!chip->is_running) {
      chip->is_running = true;
      float rpm_val = attr_read_float(chip->attr_rpm);
      if (rpm_val < 100.0f) rpm_val = 600.0f;
      if (rpm_val < 300.0f) rpm_val = 300.0f;
      if (rpm_val > 1200.0f) rpm_val = 1200.0f;
      uint32_t half_period_us = (uint32_t)((30.0f / rpm_val) * 1000000.0f);
      timer_start(chip->pulse_timer, half_period_us, false);
      render_motor_display(chip);
    }
  } else {
    chip->is_running = false;
    chip->tach_state = false;
    write_tach(chip, LOW);
    timer_start(chip->pulse_timer, 50000, false);
    render_motor_display(chip);
  }
}

void chip_init(void) {
  n20_motor_t *chip = (n20_motor_t *)calloc(1, sizeof(n20_motor_t));
  if (!chip) return;

  // Initialize pins: canonical 12V_IN and TACH_OUT, plus legacy aliases 12V and TACH
  chip->pin_12v_in   = pin_init("12V_IN", INPUT_PULLDOWN);
  chip->pin_12v      = pin_init("12V", INPUT_PULLDOWN);
  chip->pin_gnd      = pin_init("GND", INPUT_PULLDOWN);
  chip->pin_tach_out = pin_init("TACH_OUT", OUTPUT_LOW);
  chip->pin_tach     = pin_init("TACH", OUTPUT_LOW);

  // Initialize interactive sliders
  chip->attr_rpm       = attr_init_float("rpm", 600.0f);
  chip->attr_imbalance = attr_init_float("imbalance", 3.5f);

  // Initialize attached 32-bit RGBA framebuffer display
  uint32_t fb_w = FB_WIDTH;
  uint32_t fb_h = FB_HEIGHT;
  chip->fb = framebuffer_init(&fb_w, &fb_h);
  chip->fb_width = fb_w;
  chip->fb_height = fb_h;

  // Initialize tachometer pulse timer (must be timer 1)
  const timer_config_t timer_cfg = {
    .callback = on_pulse_timer,
    .user_data = chip,
  };
  chip->pulse_timer = timer_init(&timer_cfg);

  // Register pin watches on 12V inputs for immediate response to power switch
  const pin_watch_config_t watch_12v = {
    .edge = BOTH,
    .pin_change = on_12v_change,
    .user_data = chip,
  };
  if (chip->pin_12v_in != NO_PIN) {
    pin_watch(chip->pin_12v_in, &watch_12v);
  }
  if (chip->pin_12v != NO_PIN) {
    pin_watch(chip->pin_12v, &watch_12v);
  }

  // Initial visual rendering
  render_motor_display(chip);

  // Start initial timer
  timer_start(chip->pulse_timer, 50000, false);

  }

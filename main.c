#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>

typedef enum {
    RGB_NONE = 0,
    RGB_RED,
    RGB_GREEN,
    RGB_BLUE,
    RGB_WHITE
} rgb_color_t;

typedef enum {
    LED_RED = 0,
    LED_GREEN,
    LED_BLUE,
    LED_WHITE,
    LED_COUNT
} led_index_t;

typedef enum {
    LED_BLINK = 0,
    LED_TOGGLE_OFF,
    LED_TOGGLE_ON
} led_mode_t;

typedef struct {
    uint8_t stable_state;
    uint8_t last_reading;
    uint32_t last_change_ms;
} debounce_t;

volatile uint32_t g_millis = 0;

/* ===== Program state ===== */
static rgb_color_t selected_color = RGB_NONE;
static uint8_t rgb_active = 0;

static led_mode_t led_modes[LED_COUNT] = {
    LED_BLINK, LED_BLINK, LED_BLINK, LED_BLINK
};

static uint8_t normal_blink_state = 0;
static uint8_t rgb_blink_state = 0;

static uint32_t last_normal_blink_ms = 0;
static uint32_t last_rgb_blink_ms = 0;

/* ===== Debounce objects ===== */
/* D9 = toggle button, D5 = reset button, D4 = encoder button */
static debounce_t btn_toggle = {1, 1, 0};
static debounce_t btn_reset  = {1, 1, 0};
static debounce_t btn_enc    = {1, 1, 0};

/* ===== Encoder ===== */
static uint8_t encoder_prev_ab = 0;
static int8_t encoder_acc = 0;

/* =========================================================
   TIMER0 -> millis()
   ========================================================= */
ISR(TIMER0_COMPA_vect) {
    g_millis++;
}

static uint32_t millis(void) {
    uint32_t ms;
    cli();
    ms = g_millis;
    sei();
    return ms;
}

static void timer0_init(void) {
    /* 16 MHz / 64 = 250 kHz
       250 counts = 1 ms => OCR0A = 249
    */
    TCCR0A = (1 << WGM01);                 /* CTC */
    TCCR0B = (1 << CS01) | (1 << CS00);   /* prescaler 64 */
    OCR0A = 249;
    TIMSK0 = (1 << OCIE0A);
}

/* =========================================================
   ADC
   ========================================================= */
static void adc_init(void) {
    ADMUX = (1 << REFS0); /* AVcc reference */
    ADCSRA = (1 << ADEN)  /* ADC enable */
           | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0); /* prescaler 128 */
}

static uint16_t adc_read(uint8_t channel) {
    ADMUX = (1 << REFS0) | (channel & 0x07);

    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC)) {
        /* wait for ADC conversion */
    }

    return ADC;
}

/* =========================================================
   IO init
   Pin mapping used here:
   D13 = PB5 = normal red LED
   D12 = PB4 = normal green LED
   D11 = PB3 = normal blue LED
   D10 = PB2 = normal white LED

   D8  = PB0 = RGB red
   D7  = PD7 = RGB green
   D6  = PD6 = RGB blue

   D2  = PD2 = encoder CLK
   D3  = PD3 = encoder DT
   D4  = PD4 = encoder SW

   D9  = PB1 = toggle button
   D5  = PD5 = reset button

   A0 = pot for normal blink speed
   ========================================================= */
static void io_init(void) {
    /* Normal LEDs output */
    DDRB |= (1 << DDB5) | (1 << DDB4) | (1 << DDB3) | (1 << DDB2);

    /* RGB output */
    DDRB |= (1 << DDB0);
    DDRD |= (1 << DDD7) | (1 << DDD6);

    /* Encoder + reset button inputs with pull-up */
    DDRD &= ~((1 << DDD2) | (1 << DDD3) | (1 << DDD4) | (1 << DDD5));
    PORTD |= (1 << PORTD2) | (1 << PORTD3) | (1 << PORTD4) | (1 << PORTD5);

    /* Toggle button input with pull-up */
    DDRB &= ~(1 << DDB1);
    PORTB |= (1 << PORTB1);

    /* A0 input */
    DDRC &= ~(1 << DDC0);

    /* Everything off at start */
    PORTB &= ~((1 << PORTB5) | (1 << PORTB4) | (1 << PORTB3) | (1 << PORTB2) | (1 << PORTB0));
    PORTD &= ~((1 << PORTD7) | (1 << PORTD6));
}

/* =========================================================
   Helper functions
   ========================================================= */
static void set_led_pin(uint8_t led, uint8_t on) {
    switch (led) {
        case LED_RED:
            if (on) PORTB |= (1 << PORTB5);
            else    PORTB &= ~(1 << PORTB5);
            break;

        case LED_GREEN:
            if (on) PORTB |= (1 << PORTB4);
            else    PORTB &= ~(1 << PORTB4);
            break;

        case LED_BLUE:
            if (on) PORTB |= (1 << PORTB3);
            else    PORTB &= ~(1 << PORTB3);
            break;

        case LED_WHITE:
            if (on) PORTB |= (1 << PORTB2);
            else    PORTB &= ~(1 << PORTB2);
            break;

        default:
            break;
    }
}

static void set_rgb_output(uint8_t r_on, uint8_t g_on, uint8_t b_on) {
    if (r_on) PORTB |= (1 << PORTB0);
    else      PORTB &= ~(1 << PORTB0);

    if (g_on) PORTD |= (1 << PORTD7);
    else      PORTD &= ~(1 << PORTD7);

    if (b_on) PORTD |= (1 << PORTD6);
    else      PORTD &= ~(1 << PORTD6);
}

static void show_rgb_color(rgb_color_t color, uint8_t on) {
    if (!on || color == RGB_NONE) {
        set_rgb_output(0, 0, 0);
        return;
    }

    switch (color) {
        case RGB_RED:
            set_rgb_output(1, 0, 0);
            break;
        case RGB_GREEN:
            set_rgb_output(0, 1, 0);
            break;
        case RGB_BLUE:
            set_rgb_output(0, 0, 1);
            break;
        case RGB_WHITE:
            set_rgb_output(1, 1, 1);
            break;
        default:
            set_rgb_output(0, 0, 0);
            break;
    }
}

static int8_t color_to_led_index(rgb_color_t color) {
    switch (color) {
        case RGB_RED:   return LED_RED;
        case RGB_GREEN: return LED_GREEN;
        case RGB_BLUE:  return LED_BLUE;
        case RGB_WHITE: return LED_WHITE;
        default:        return -1;
    }
}

static rgb_color_t next_color(rgb_color_t c) {
    switch (c) {
        case RGB_NONE:  return RGB_RED;
        case RGB_RED:   return RGB_GREEN;
        case RGB_GREEN: return RGB_BLUE;
        case RGB_BLUE:  return RGB_WHITE;
        case RGB_WHITE: return RGB_NONE;
        default:        return RGB_NONE;
    }
}

static rgb_color_t prev_color(rgb_color_t c) {
    switch (c) {
        case RGB_NONE:  return RGB_WHITE;
        case RGB_WHITE: return RGB_BLUE;
        case RGB_BLUE:  return RGB_GREEN;
        case RGB_GREEN: return RGB_RED;
        case RGB_RED:   return RGB_NONE;
        default:        return RGB_NONE;
    }
}

/* =========================================================
   Debounce
   raw_state = 1 when not pressed
   raw_state = 0 when pressed (pull-up)
   returns 1 only on a new stable press
   ========================================================= */
static uint8_t debounce_pressed_event(debounce_t *btn, uint8_t raw_state, uint32_t now_ms) {
    const uint32_t debounce_ms = 25;

    if (raw_state != btn->last_reading) {
        btn->last_reading = raw_state;
        btn->last_change_ms = now_ms;
    }

    if ((now_ms - btn->last_change_ms) >= debounce_ms && raw_state != btn->stable_state) {
        btn->stable_state = raw_state;

        if (btn->stable_state == 0) {
            return 1;
        }
    }

    return 0;
}

/* =========================================================
   Encoder handling
   ========================================================= */
static void handle_encoder_rotation(void) {
    /* if selected color is active, do not allow color changes */
    if (rgb_active) {
        encoder_prev_ab = (uint8_t)((((PIND >> PD2) & 1u) << 1) | ((PIND >> PD3) & 1u));
        encoder_acc = 0;
        return;
    }

    uint8_t a = (uint8_t)((PIND >> PD2) & 1u); /* CLK */
    uint8_t b = (uint8_t)((PIND >> PD3) & 1u); /* DT */
    uint8_t current_ab = (uint8_t)((a << 1) | b);

    uint8_t transition = (uint8_t)((encoder_prev_ab << 2) | current_ab);

    static const int8_t table[16] = {
         0, -1,  1,  0,
         1,  0,  0, -1,
        -1,  0,  0,  1,
         0,  1, -1,  0
    };

    encoder_acc += table[transition];

    if (encoder_acc >= 4) {
        selected_color = next_color(selected_color);
        encoder_acc = 0;
    } else if (encoder_acc <= -4) {
        selected_color = prev_color(selected_color);
        encoder_acc = 0;
    }

    encoder_prev_ab = current_ab;
}

/* =========================================================
   Blink logic
   ========================================================= */
static void update_normal_blink(uint32_t now_ms) {
    uint16_t pot = adc_read(0); /* A0 */

    /* 0..1023 -> 0..2550 ms */
    uint32_t extra_ms = ((uint32_t)pot * 2550UL) / 1023UL;
    uint32_t blink_delay_ms = 250UL + extra_ms;

    if ((now_ms - last_normal_blink_ms) >= blink_delay_ms) {
        last_normal_blink_ms = now_ms;
        normal_blink_state ^= 1u;
    }
}

static void update_rgb_blink(uint32_t now_ms) {
    if (!rgb_active || selected_color == RGB_NONE) {
        rgb_blink_state = 0;
        return;
    }

    if ((now_ms - last_rgb_blink_ms) >= 250UL) {
        last_rgb_blink_ms = now_ms;
        rgb_blink_state ^= 1u;
    }
}

/* =========================================================
   Apply outputs
   ========================================================= */
static void apply_normal_leds(void) {
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        uint8_t on = 0;

        switch (led_modes[i]) {
            case LED_BLINK:
                on = normal_blink_state;
                break;
            case LED_TOGGLE_OFF:
                on = 0;
                break;
            case LED_TOGGLE_ON:
                on = 1;
                break;
            default:
                on = 0;
                break;
        }

        set_led_pin(i, on);
    }
}

static void apply_rgb_led(void) {
    if (selected_color == RGB_NONE) {
        show_rgb_color(RGB_NONE, 0);
        return;
    }

    if (rgb_active) {
        show_rgb_color(selected_color, rgb_blink_state);
    } else {
        /* show selected color steadily when not active */
        show_rgb_color(selected_color, 1);
    }
}

/* =========================================================
   Main
   ========================================================= */
int main(void) {
    io_init();
    adc_init();
    timer0_init();

    encoder_prev_ab = (uint8_t)((((PIND >> PD2) & 1u) << 1) | ((PIND >> PD3) & 1u));

    sei();

    while (1) {
        uint32_t now = millis();

        /* encoder rotation */
        handle_encoder_rotation();

        /* encoder button on D4 */
        if (debounce_pressed_event(&btn_enc, (uint8_t)((PIND >> PD4) & 1u), now)) {
            if (selected_color != RGB_NONE) {
                rgb_active ^= 1u;
                last_rgb_blink_ms = now;
                rgb_blink_state = 1;
            }
        }

        /* toggle button on D9 */
        if (debounce_pressed_event(&btn_toggle, (uint8_t)((PINB >> PB1) & 1u), now)) {
            if (rgb_active) {
                int8_t idx = color_to_led_index(selected_color);

                if (idx >= 0) {
                    if (led_modes[(uint8_t)idx] == LED_TOGGLE_ON) {
                        led_modes[(uint8_t)idx] = LED_TOGGLE_OFF;
                    } else {
                        led_modes[(uint8_t)idx] = LED_TOGGLE_ON;
                    }
                }
            }
        }

        /* reset button on D5 */
        if (debounce_pressed_event(&btn_reset, (uint8_t)((PIND >> PD5) & 1u), now)) {
            if (rgb_active) {
                int8_t idx = color_to_led_index(selected_color);

                if (idx >= 0) {
                    led_modes[(uint8_t)idx] = LED_BLINK;
                }
            }
        }

        update_normal_blink(now);
        update_rgb_blink(now);

        apply_normal_leds();
        apply_rgb_led();
    }
}
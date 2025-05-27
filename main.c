#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// I2C
#define I2C_SCL_PIN 5  // PC5 (A5)
#define I2C_SDA_PIN 4  // PC4 (A4)
#define LCD_ADDR 0x27

// Pin definitions (Arduino to AVR mapping)
#define MENU_BTN_PIN 3     // PD3 
#define SELECT_BTN_PIN 4   // PD4
#define PLAYER_BTN1_PIN 3  // PD3
#define PLAYER_BTN2_PIN 4  // PD4
#define PLAYER_BTN3_PIN 5  // PD5
#define PLAYER_BTN4_PIN 6  // PD6
#define LED1_PIN 2         // PD2
#define LED2_PIN 1         // PB1 (D9)
#define LED3_PIN 7         // PD7
#define LED4_PIN 0         // PB0 (D8)
#define BUZZER_PIN 2       // PB2 (D10)

// Game enums
typedef enum {
    MODE_REFLEX,
    MODE_TIMING
} GameMode;

typedef enum {
    SINGLE_PLAYER,
    MULTIPLAYER
} ReflexMode;

typedef enum {
    MENU,
    SELECT_REFLEX_MODE,
    SELECT_DIFFICULTY,
    WAIT_SIGNAL,
    SHOW_SIGNAL,
    RESULT,
    SELECT_TIMING_PLAYERS,
    WAIT_TARGET,
    TARGET_RESULT
} GameState;

// Global variables
volatile uint32_t millis_counter = 0;
GameState state = MENU;
GameMode mode = MODE_REFLEX;
ReflexMode reflexMode = SINGLE_PLAYER;

const char* modeNames[2] = {"Reflex", "TimeTarget"};
const char* difficultyNames[4] = {"Easy", "Medium", "Hard", "Insane"};
float difficultyLimits[4] = {0.400, 0.250, 0.160, 0.130};

int selectedDifficulty = 0;
float reflexTimeLimit = 0;
uint32_t reflexSignalStartTime = 0;
uint32_t reflexReactionTime = 0;
int reflexButtonPressed = -1;
int winnerPlayer = -1;
int cheaterPlayer = -1;
uint8_t signalGiven = 0;
uint8_t hasCheated = 0;
uint8_t hasWon = 0;

// Time target variables
uint32_t timingStartTime = 0;
const float targetTime = 11.0;
uint32_t timingReactionTime = 0;
int timingButtonPressed = -1;
int selectedTimingPlayers = 1;
uint32_t playerTimes[4] = {0, 0, 0, 0};
uint8_t playerPressed[4] = {0, 0, 0, 0};
int playersFinished = 0;
int bestPlayer = -1;
float bestDifference = 999.0;

// Timer interrupt for millis() equivalent
ISR(TIMER0_OVF_vect) {
    millis_counter++;
}

// Initialize timer for millis counter
void timer_init(void) {
    TCCR0A = 0;
    TCCR0B = (1 << CS01) | (1 << CS00); // Prescaler 64
    TIMSK0 = (1 << TOIE0);
    sei();
}

uint32_t millis(void) {
    uint32_t m;
    cli();
    m = millis_counter;
    sei();
    return m;
}

void delay_ms(uint16_t ms) {
    while(ms--) {
        _delay_ms(1);
    }
}

// Random number generator
uint32_t lfsr = 1;
uint16_t random_range(uint16_t min, uint16_t max) {
    lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB400u);
    return min + (lfsr % (max - min));
}

// I2C Functions
void i2c_init(void) {
    TWSR = 0x00;
    TWBR = 0x20; // 100kHz at 16MHz
    TWCR = (1 << TWEN);
}

void i2c_start(void) {
    TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));
}

void i2c_stop(void) {
    TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
}

void i2c_write(uint8_t data) {
    TWDR = data;
    TWCR = (1 << TWINT) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));
}

// LCD Functions
void lcd_send_nibble(uint8_t nibble) {
    uint8_t data = nibble | 0x08; // backlight on
    i2c_start();
    i2c_write((LCD_ADDR << 1));
    i2c_write(data | 0x04); // EN high
    i2c_write(data & ~0x04); // EN low
    i2c_stop();
    _delay_us(50);
}

void lcd_send_byte(uint8_t byte, uint8_t mode) {
    uint8_t highnib = byte & 0xF0;
    uint8_t lownib = (byte << 4) & 0xF0;
    lcd_send_nibble(highnib | mode);
    lcd_send_nibble(lownib | mode);
}

void lcd_init(void) {
    i2c_init();
    delay_ms(50);
    
    // Initialize LCD in 4-bit mode
    lcd_send_nibble(0x30);
    delay_ms(5);
    lcd_send_nibble(0x30);
    _delay_us(150);
    lcd_send_nibble(0x30);
    _delay_us(150);
    lcd_send_nibble(0x20); // 4-bit mode
    
    lcd_send_byte(0x28, 0); // 4-bit, 2 lines, 5x8 font
    lcd_send_byte(0x0C, 0); // Display on, cursor off
    lcd_send_byte(0x06, 0); // Entry mode
    lcd_send_byte(0x01, 0); // Clear display
    delay_ms(2);
}

void lcd_clear(void) {
    lcd_send_byte(0x01, 0);
    delay_ms(2);
}

void lcd_set_cursor(uint8_t col, uint8_t row) {
    uint8_t address = (row == 0) ? 0x80 + col : 0xC0 + col;
    lcd_send_byte(address, 0);
}

void lcd_print(const char* str) {
    while (*str) {
        lcd_send_byte(*str++, 1);
    }
}

void lcd_print_int(int num) {
    char buffer[12];
    itoa(num, buffer, 10);
    lcd_print(buffer);
}

void lcd_print_float(float num, int precision) {
    char buffer[16];
    int whole = (int)num;
    int frac = (int)((num - whole) * pow(10, precision));
    
    itoa(whole, buffer, 10);
    lcd_print(buffer);
    lcd_print(".");
    
    // Handle leading zeros in fractional part
    for (int i = precision - 1; i > 0 && frac < pow(10, i); i--) {
        lcd_print("0");
    }
    itoa(frac, buffer, 10);
    lcd_print(buffer);
}

// GPIO Functions
void gpio_init(void) {
    // Set button pins as inputs (pull-down resistors)
    DDRD &= ~((1 << PLAYER_BTN1_PIN) | (1 << PLAYER_BTN2_PIN) | 
              (1 << PLAYER_BTN3_PIN) | (1 << PLAYER_BTN4_PIN));
    
    // Set LED pins as outputs
    DDRD |= (1 << LED1_PIN) | (1 << LED3_PIN);
    DDRB |= (1 << LED2_PIN) | (1 << LED4_PIN) | (1 << BUZZER_PIN);
    
    // Initialize all LEDs off
    PORTD &= ~((1 << LED1_PIN) | (1 << LED3_PIN));
    PORTB &= ~((1 << LED2_PIN) | (1 << LED4_PIN));
}

uint8_t digital_read_btn(int btn) {
    switch(btn) {
        case 0: return (PIND & (1 << PLAYER_BTN1_PIN)) != 0;
        case 1: return (PIND & (1 << PLAYER_BTN2_PIN)) != 0;
        case 2: return (PIND & (1 << PLAYER_BTN3_PIN)) != 0;
        case 3: return (PIND & (1 << PLAYER_BTN4_PIN)) != 0;
        default: return 0;
    }
}

void digital_write_led(int led, uint8_t state) {
    switch(led) {
        case 0:
            if (state) PORTD |= (1 << LED1_PIN);
            else PORTD &= ~(1 << LED1_PIN);
            break;
        case 1:
            if (state) PORTB |= (1 << LED2_PIN);
            else PORTB &= ~(1 << LED2_PIN);
            break;
        case 2:
            if (state) PORTD |= (1 << LED3_PIN);
            else PORTD &= ~(1 << LED3_PIN);
            break;
        case 3:
            if (state) PORTB |= (1 << LED4_PIN);
            else PORTB &= ~(1 << LED4_PIN);
            break;
    }
}

// Buzzer functions using Timer1 for PWM
void play_tone_start(uint16_t frequency) {
    if (frequency == 0) {
        TCCR1A = 0;
        TCCR1B = 0;
        PORTB &= ~(1 << BUZZER_PIN);
        return;
    }
    
    TCCR1A = (1 << COM1B1) | (1 << WGM11);
    TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS11);
    
    // Calculate frequency: f = F_CPU / (2 * prescaler * (1 + ICR1))
    ICR1 = (F_CPU / (8UL * frequency)) - 1;
    OCR1B = ICR1 / 2;
}

void play_tone_stop(void) {
    // Stop PWM
    TCCR1A = 0;
    TCCR1B = 0;
    PORTB &= ~(1 << BUZZER_PIN);
}

void play_tone_blocking(uint16_t frequency, uint16_t duration) {
    play_tone_start(frequency);
    delay_ms(duration);
    play_tone_stop();
}

// Game Functions
void handle_menu(void) {
    lcd_set_cursor(0, 0);
    lcd_print("Selecteaza mod:");
    lcd_set_cursor(0, 1);
    lcd_print(modeNames[mode]);

    if (digital_read_btn(0)) {  // menu button
        mode = (mode == MODE_REFLEX) ? MODE_TIMING : MODE_REFLEX;
        delay_ms(300);
        lcd_clear();
    }

    if (digital_read_btn(1)) { // select button
        lcd_clear();
        if (mode == MODE_REFLEX) {
            state = SELECT_REFLEX_MODE;
        } else {
            state = SELECT_TIMING_PLAYERS;
        }
        delay_ms(500);
        lcd_clear();
    }
}

void select_reflex_mode(void) {
    lcd_set_cursor(0, 0);
    lcd_print("Reflex Mode:");
    lcd_set_cursor(0, 1);
    lcd_print(reflexMode == SINGLE_PLAYER ? "Single Player" : "Multiplayer");

    if (digital_read_btn(0)) {
        reflexMode = (reflexMode == SINGLE_PLAYER) ? MULTIPLAYER : SINGLE_PLAYER;
        delay_ms(300);
        lcd_clear();
    }

    if (digital_read_btn(1)) {
        lcd_clear();
        if (reflexMode == SINGLE_PLAYER) {
            state = SELECT_DIFFICULTY;
        } else {
            reflexTimeLimit = 0;
            state = WAIT_SIGNAL;
        }
        delay_ms(500);
        lcd_clear();
    }
}

void select_timing_players(void) {
    lcd_set_cursor(0, 0);
    lcd_print("Timing Players:");
    lcd_set_cursor(0, 1);
    lcd_print_int(selectedTimingPlayers);
    lcd_print(" jucatori");

    if (digital_read_btn(0)) {
        selectedTimingPlayers = (selectedTimingPlayers % 4) + 1;
        delay_ms(300);
        lcd_clear();
    }

    if (digital_read_btn(1)) {
        lcd_clear();
        lcd_set_cursor(0, 0);
        lcd_print("Tinta: ");
        lcd_print_float(targetTime, 1);
        lcd_print("s");
        delay_ms(2000);
        lcd_clear();
        
        // Reset variables
        for (int i = 0; i < 4; i++) {
            playerTimes[i] = 0;
            playerPressed[i] = 0;
        }
        playersFinished = 0;
        bestPlayer = -1;
        bestDifference = 999.0;
        
        lcd_print("Start");
        timingStartTime = millis();
        state = WAIT_TARGET;
        delay_ms(500);
        lcd_clear();
    }
}

void select_difficulty(void) {
    lcd_set_cursor(0, 0);
    lcd_print("Dificultate:");
    lcd_set_cursor(0, 1);
    lcd_print(difficultyNames[selectedDifficulty]);

    if (digital_read_btn(0)) {
        selectedDifficulty = (selectedDifficulty + 1) % 4;
        delay_ms(300);
        lcd_clear();
    }

    if (digital_read_btn(1)) {
        reflexTimeLimit = difficultyLimits[selectedDifficulty];
        lcd_clear();
        lcd_print("Nivel: ");
        lcd_print(difficultyNames[selectedDifficulty]);
        delay_ms(1000);
        state = WAIT_SIGNAL;
        lcd_clear();
    }
}

void wait_signal(void) {
    lcd_set_cursor(0, 0);
    lcd_print("Pregatiti-va...");
    delay_ms(1000);
    lcd_set_cursor(0, 1);
    lcd_print("Nu apasati!");

    uint32_t start = millis();
    uint32_t waitTime = random_range(2000, 5000);
    signalGiven = 0;
    hasCheated = 0;
    cheaterPlayer = -1;
    winnerPlayer = -1;
    hasWon = 0;

    while (millis() - start < waitTime) {
        for (int i = 0; i < 4; i++) {
            if (digital_read_btn(i)) {
                cheaterPlayer = i;
                hasCheated = 1;
                state = RESULT;
                return;
            }
        }
    }

    signalGiven = 1;
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_print("GO! Apasa acum!");
    
    play_tone_start(1500);
    reflexSignalStartTime = millis();
    
    delay_ms(100);
    play_tone_stop();
    
    state = SHOW_SIGNAL;
}

void handle_signal(void) {
    for (int i = 0; i < 4; i++) {
        if (digital_read_btn(i)) {
            reflexReactionTime = millis() - reflexSignalStartTime;
            winnerPlayer = i;
            digital_write_led(i, 1);
            
            if (reflexMode == SINGLE_PLAYER && reflexTimeLimit > 0) {
                float reactionTimeInSec = reflexReactionTime / 1000.0;
                hasWon = (reactionTimeInSec <= reflexTimeLimit);
            }
            
            state = RESULT;
            break;
        }
    }
    
    if (reflexMode == SINGLE_PLAYER && reflexTimeLimit > 0) {
        if ((millis() - reflexSignalStartTime) > (reflexTimeLimit * 1000)) {
            state = RESULT;
        }
    }
}

void show_reflex_result(void) {
    lcd_clear();
    if (hasCheated) {
        lcd_set_cursor(0, 0);
        lcd_print("Jucator ");
        lcd_print_int(cheaterPlayer + 1);
        lcd_set_cursor(0, 1);
        lcd_print("TRISOR!");
        play_tone_blocking(400, 500);

        for (int i = 0; i < 3; i++) {
            digital_write_led(cheaterPlayer, 1);
            delay_ms(300);
            digital_write_led(cheaterPlayer, 0);
            delay_ms(300);
        }
        delay_ms(2000);
    } else if (winnerPlayer != -1) {
        lcd_set_cursor(0, 0);
        lcd_print("Jucator ");
        lcd_print_int(winnerPlayer + 1);
        
        if (hasWon) {
            lcd_print(" WIN");
            play_tone_blocking(2500, 500);
        } else {
            play_tone_blocking(2000, 200);
        }
        
        lcd_set_cursor(0, 1);
        lcd_print("Timp: ");
        lcd_print_float(reflexReactionTime / 1000.0, 3);
        lcd_print("s");
        
        if (!hasWon && reflexMode == SINGLE_PLAYER && reflexTimeLimit > 0) {
            play_tone_blocking(800, 300);
        }
        
        delay_ms(3000);
        digital_write_led(winnerPlayer, 0);
    } else if (reflexMode == SINGLE_PLAYER && reflexTimeLimit > 0) {
        lcd_set_cursor(0, 0);
        lcd_print("Timp expirat!");
        lcd_set_cursor(0, 1);
        lcd_print("Limita: ");
        lcd_print_float(reflexTimeLimit, 3);
        lcd_print("s");
        play_tone_blocking(300, 800);
        delay_ms(3000);
    }

    hasCheated = 0;
    signalGiven = 0;
    reflexButtonPressed = -1;
    hasWon = 0;
    state = MENU;
    lcd_clear();
}

void wait_target(void) {
    if (selectedTimingPlayers == 1) {
        for (int i = 0; i < 4; i++) {
            if (digital_read_btn(i)) {
                timingReactionTime = millis() - timingStartTime;
                timingButtonPressed = i;
                digital_write_led(i, 1);
                state = TARGET_RESULT;
                break;
            }
        }
    } else {
        for (int i = 0; i < selectedTimingPlayers; i++) {
            if (digital_read_btn(i) && !playerPressed[i]) {
                playerTimes[i] = millis() - timingStartTime;
                playerPressed[i] = 1;
                digital_write_led(i, 1);
                playersFinished++;
                
                float playerTimeInSec = playerTimes[i] / 1000.0;
                float difference = fabs(playerTimeInSec - targetTime);
                
                if (difference < bestDifference) {
                    bestDifference = difference;
                    bestPlayer = i;
                }
                
                if (playersFinished >= selectedTimingPlayers) {
                    state = TARGET_RESULT;
                    break;
                }
            }
        }
    }
}

void show_timing_result(void) {
    if (selectedTimingPlayers == 1) {
        float timeInSec = timingReactionTime / 1000.0;
        float diff = fabs(timeInSec - targetTime);

        lcd_clear();
        lcd_set_cursor(0, 0);
        lcd_print("Ai apasat la:");
        lcd_set_cursor(0, 1);
        lcd_print_float(timeInSec, 2);
        lcd_print("s (dif: ");
        lcd_print_float(diff, 2);
        lcd_print("s)");

        play_tone_blocking((uint16_t)(1000 + 500 * (1 - diff / targetTime)), 300);

        delay_ms(4000);
        digital_write_led(timingButtonPressed, 0);
    } else {
        lcd_clear();
        lcd_set_cursor(0, 0);
        lcd_print("Castigator:");
        lcd_set_cursor(0, 1);
        lcd_print("Jucator ");
        lcd_print_int(bestPlayer + 1);
        
        delay_ms(2000);
        
        lcd_clear();
        lcd_set_cursor(0, 0);
        lcd_print("Timp: ");
        lcd_print_float(playerTimes[bestPlayer] / 1000.0, 2);
        lcd_print("s");
        lcd_set_cursor(0, 1);
        lcd_print("Dif: ");
        lcd_print_float(bestDifference, 2);
        lcd_print("s");
        
        play_tone_blocking((uint16_t)(1000 + 500 * (1 - bestDifference / targetTime)), 300);
        
        delay_ms(3000);
        
        for (int i = 0; i < selectedTimingPlayers; i++) {
            digital_write_led(i, 0);
        }
    }
    
    state = MENU;
    lcd_clear();
}

int main(void) {
    gpio_init();
    lcd_init();
    timer_init();
    
    // Random seed
    lfsr = 12345;
    
    lcd_set_cursor(0, 0);
    lcd_print(" Reflex & Timing ");
    delay_ms(2000);
    lcd_clear();
    
    while (1) {
        switch (state) {
            case MENU:
                handle_menu();
                break;
            case SELECT_REFLEX_MODE:
                select_reflex_mode();
                break;
            case SELECT_DIFFICULTY:
                select_difficulty();
                break;
            case WAIT_SIGNAL:
                wait_signal();
                break;
            case SHOW_SIGNAL:
                handle_signal();
                break;
            case RESULT:
                show_reflex_result();
                break;
            case SELECT_TIMING_PLAYERS:
                select_timing_players();
                break;
            case WAIT_TARGET:
                wait_target();
                break;
            case TARGET_RESULT:
                show_timing_result();
                break;
        }
    }
    
    return 0;
}

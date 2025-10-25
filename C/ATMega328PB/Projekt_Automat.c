/*
 PROJECT TMP_1 VENDING MACHINE SEBASTIAN SKOWRON 
 */

#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <stdio.h>
#include <avr/interrupt.h>

// LCD I2C address 
#define LCD_ADDR 0x27

// LCD control bits on PCF8574
#define LCD_RS  0x01    // Register select bit
#define LCD_RW  0x02    // Read/Write bit
#define LCD_EN  0x04    // Enable bit
#define LCD_BL  0x08    // Backlight bit

// LCD Commands
#define LCD_CLEAR       0x01
#define LCD_HOME        0x02
#define LCD_ENTRYMODE   0x04
#define LCD_DISPLAYCTRL 0x08
#define LCD_CURSORSHIFT 0x10
#define LCD_FUNCTIONSET 0x20
#define LCD_SETCGRAMADDR 0x40
#define LCD_SETDDRAMADDR 0x80

// LCD Entry mode flags
#define LCD_ENTRY_RIGHT 0x00
#define LCD_ENTRY_LEFT  0x02
#define LCD_ENTRY_SHIFT_INC 0x01
#define LCD_ENTRY_SHIFT_DEC 0x00

// LCD Display control flags
#define LCD_DISPLAY_ON  0x04
#define LCD_DISPLAY_OFF 0x00
#define LCD_CURSOR_ON   0x02
#define LCD_CURSOR_OFF  0x00
#define LCD_BLINK_ON    0x01
#define LCD_BLINK_OFF   0x00

// LCD Function set flags
#define LCD_8BITMODE    0x10
#define LCD_4BITMODE    0x00
#define LCD_2LINE       0x08
#define LCD_1LINE       0x00
#define LCD_5x10DOTS    0x04
#define LCD_5x8DOTS     0x00

// Keypad configuration - updated for 3x4 matrix
#define KEY_PORT        PORTC
#define KEY_PIN         PINC
#define KEY_DDR         DDRC
#define KEY_ROW1        PC4     // Row 1 on PC4
#define KEY_ROW2        PB2     // Row 2 on PB2
#define KEY_ROW3        PB3     // Row 3 on PB3
#define KEY_COL1        PC0     // Column 1 on PC0
#define KEY_COL2        PC1     // Column 2 on PC1
#define KEY_COL3        PC2     // Column 3 on PC2
#define KEY_COL4        PC3     // Column 4 on PC3

// 7-Segment Display configuration
#define SEG_PORT_CTRL   PORTE
#define SEG_DDR_CTRL    DDRE
#define SEG_DIGIT1      PE0     // First digit control pin
#define SEG_DIGIT2      PE1     // Second digit control pin
#define SEG_DIGIT3      PE2     // Third digit control pin
#define SEG_DIGIT4      PE3     // Fourth digit control pin

// 7-Segment display segment pins
// Segments are connected to PD2-PD7, PB0-PB1
#define SEG_DDR_DATA1   DDRD    // For segments on Port D
#define SEG_PORT_DATA1  PORTD   // For segments on Port D
#define SEG_DDR_DATA2   DDRB    // For segments on Port B
#define SEG_PORT_DATA2  PORTB   // For segments on Port B

#define SUCCESS_LED_PIN PC5
#define SUCCESS_LED_DDR DDRC
#define SUCCESS_LED_PORT PORTC

// Menu state definitions
typedef enum {
    MENU_JUICE = 0,
    MENU_SNACK = 1,
    MENU_CHIPS = 2,
    MENU_WATER = 3,
    MENU_PIWO = 4,
    MENU_MAX   = 5    
} MenuState;

// Add a new display mode for payment input
typedef enum {
	DISPLAY_MENU,      // Regular menu cycling
	DISPLAY_SELECTION, // Item selected by button press
	DISPLAY_PAYMENT    // Accepting payment input
} DisplayMode;

// Menu item information
typedef struct {
    char name[10];
    char price[10];
} MenuItem;

// Global variables
int runtime = 0;
MenuState currentMenu = MENU_JUICE;  // Start with Juice selected
DisplayMode currentMode = DISPLAY_MENU;
MenuItem menuItems[MENU_MAX] = {
    {"Juice", "$2.50"},
    {"Snack", "$3.20"},
    {"Chips", "$9.30"},
    {"Water", "$7.50"},
    {"Piwo", "$7.50"}
};
MenuState selectedItem;

// 7-Segment display variables
volatile uint8_t seg_digit = 0;    // Current digit being displayed (0-3)
uint8_t seg_values[4] = {0, 0, 0, 0}; // Values to display on each digit
// Add variables for payment mode
uint8_t payment_digits[3] = {0, 0, 0};
uint8_t current_digit = 0;

// Bits mapped as: F E D C B A DP G
const uint8_t seven_seg_digits[] = {
     0b00000011,  // 0: A B C D E F
     0b11100111,  // 1: B C
     0b10010010,  // 2: A B D E G
     0b11000010,  // 3: A B C D G
     0b01100110,  // 4: B C F G
     0b01001010,  // 5: A C D F G
     0b00001010,  // 6: A C D E F G
     0b11100011,  // 7: A B C
     0b00000010,  // 8: A B C D E F G
     0b01000010,   // 9: A B C D F G
     0b10010000,  // 2.: A B D E G
     0b11000000,  // 3: A B C D G
     0b11100001,  // 7: A B C
     0b01000000   // 9: A B C D F G   
};

// Software I2C implementation
void I2C_Init(void) {
    // Configure PD0 (SDA) and PD1 (SCL) as output (initially high)
    DDRD |= (1 << PD0) | (1 << PD1);
    PORTD |= (1 << PD0) | (1 << PD1);  // Pull-up (idle state)
}

void I2C_Start(void) {
    // SDA high to low while SCL is high
    PORTD |= (1 << PD1);    // SCL high
    _delay_us(5);
    PORTD |= (1 << PD0);    // SDA high
    _delay_us(5);
    PORTD &= ~(1 << PD0);   // SDA low
    _delay_us(5);
    PORTD &= ~(1 << PD1);   // SCL low
    _delay_us(5);
}

void I2C_Stop(void) {
    // SDA low to high while SCL is high
    PORTD &= ~(1 << PD0);   // SDA low
    _delay_us(5);
    PORTD |= (1 << PD1);    // SCL high
    _delay_us(5);
    PORTD |= (1 << PD0);    // SDA high
    _delay_us(5);
}

uint8_t I2C_Write(uint8_t data) {
    uint8_t i;
    uint8_t ack;
    
    // Send 8 bits, MSB first
    for (i = 0; i < 8; i++) {
        if (data & 0x80)
            PORTD |= (1 << PD0);   // SDA high
        else
            PORTD &= ~(1 << PD0);  // SDA low
            
        _delay_us(5);
        PORTD |= (1 << PD1);       // SCL high
        _delay_us(5);
        PORTD &= ~(1 << PD1);      // SCL low
        _delay_us(5);
        
        data <<= 1;  // Shift left for next bit
    }
    
    // Release SDA for ACK bit (input)
    DDRD &= ~(1 << PD0);   // SDA as input
    PORTD |= (1 << PD0);   // Enable pull-up
    _delay_us(5);
    
    // Clock pulse for ACK
    PORTD |= (1 << PD1);   // SCL high
    _delay_us(5);
    
    // Read ACK bit
    ack = !(PIND & (1 << PD0));  // Inverted because ACK is active low
    
    PORTD &= ~(1 << PD1);   // SCL low
    _delay_us(5);
    
    // Reconfigure SDA as output
    DDRD |= (1 << PD0);
    
    return ack;  // Return 1 if ACK received, 0 if not
}

// Write a byte to the LCD via I2C
void LCD_WriteI2C(uint8_t data) {
    I2C_Start();
    I2C_Write(LCD_ADDR << 1);  // Address with write bit (0)
    I2C_Write(data);
    I2C_Stop();
}

// LCD initialization sequence for 4-bit mode via PCF8574
void LCD_Init(void) {
    // Initialize I2C
    I2C_Init();
    _delay_ms(50);  // Wait for LCD power-up
    
    // Send reset sequence
    LCD_WriteI2C(0x30 | LCD_BL);
    _delay_ms(5);
    LCD_WriteI2C(0x30 | LCD_EN | LCD_BL);
    _delay_us(10);
    LCD_WriteI2C(0x30 | LCD_BL);
    _delay_ms(5);
    
    LCD_WriteI2C(0x30 | LCD_BL);
    _delay_ms(5);
    LCD_WriteI2C(0x30 | LCD_EN | LCD_BL);
    _delay_us(10);
    LCD_WriteI2C(0x30 | LCD_BL);
    _delay_ms(5);
    
    LCD_WriteI2C(0x30 | LCD_BL);
    _delay_ms(5);
    LCD_WriteI2C(0x30 | LCD_EN | LCD_BL);
    _delay_us(10);
    LCD_WriteI2C(0x30 | LCD_BL);
    _delay_ms(5);
    
    // Switch to 4-bit mode
    LCD_WriteI2C(0x20 | LCD_BL);
    _delay_ms(5);
    LCD_WriteI2C(0x20 | LCD_EN | LCD_BL);
    _delay_us(10);
    LCD_WriteI2C(0x20 | LCD_BL);
    _delay_ms(5);
    
    // Now in 4-bit mode, send the rest of the init sequence
    LCD_Command(LCD_FUNCTIONSET | LCD_2LINE);      // 4-bit mode, 2 lines, 5x8 dots
    LCD_Command(LCD_DISPLAYCTRL | LCD_DISPLAY_ON); // Display on, cursor off, blink off
    LCD_Command(LCD_CLEAR);                        // Clear display
    _delay_ms(2);                                  // Clear takes a long time
    LCD_Command(LCD_ENTRYMODE | LCD_ENTRY_LEFT);   // Left to right, no shifting
}

// Send a command to the LCD
void LCD_Command(uint8_t cmd) {
    // High nibble first with backlight on, RS=0
    uint8_t high = (cmd & 0xF0) | LCD_BL;
    uint8_t low = ((cmd << 4) & 0xF0) | LCD_BL;
    
    // Send high nibble
    LCD_WriteI2C(high);
    LCD_WriteI2C(high | LCD_EN);  // Pulse EN
    _delay_us(1);
    LCD_WriteI2C(high);
    _delay_us(50);
    
    // Send low nibble
    LCD_WriteI2C(low);
    LCD_WriteI2C(low | LCD_EN);   // Pulse EN
    _delay_us(1);
    LCD_WriteI2C(low);
    _delay_us(50);
}

// Send data to the LCD
void LCD_Data(uint8_t data) {
    // High nibble first with backlight on, RS=1
    uint8_t high = (data & 0xF0) | LCD_RS | LCD_BL;
    uint8_t low = ((data << 4) & 0xF0) | LCD_RS | LCD_BL;
    
    // Send high nibble
    LCD_WriteI2C(high);
    LCD_WriteI2C(high | LCD_EN);  // Pulse EN
    _delay_us(1);
    LCD_WriteI2C(high);
    _delay_us(50);
    
    // Send low nibble
    LCD_WriteI2C(low);
    LCD_WriteI2C(low | LCD_EN);   // Pulse EN
    _delay_us(1);
    LCD_WriteI2C(low);
    _delay_us(50);
}

// Print a string to the LCD
void LCD_Print(const char *str) {
    while (*str)
        LCD_Data(*str++);
}

// Set the LCD cursor position
void LCD_SetCursor(uint8_t row, uint8_t col) {
    uint8_t row_offsets[] = {0x00, 0x40, 0x14, 0x54}; // For 16x2 LCDs
    LCD_Command(LCD_SETDDRAMADDR | (col + row_offsets[row]));
}

// Clear LCD display
void LCD_Clear(void) {
    LCD_Command(LCD_CLEAR);
    _delay_ms(2);  // Clear command needs time
}

// Print at a specific position
void LCD_PrintPos(uint8_t row, uint8_t col, const char *str) {
    LCD_SetCursor(row, col);
    LCD_Print(str);
}

// Initialize keypad - updated for 3x4 matrix
void Keypad_Init(void) {
	// Configure row pins as outputs (initially high)
	KEY_DDR |= (1 << KEY_ROW1);
	KEY_PORT |= (1 << KEY_ROW1);  // Set to high
	
	// Configure row 2 and 3 on PORTB
	DDRB |= (1 << KEY_ROW2) | (1 << KEY_ROW3);
	PORTB |= (1 << KEY_ROW2) | (1 << KEY_ROW3);  // Set to high
	
	// Configure column pins as inputs with pull-ups
	KEY_DDR &= ~((1 << KEY_COL1) | (1 << KEY_COL2) | (1 << KEY_COL3) | (1 << KEY_COL4));
	KEY_PORT |= ((1 << KEY_COL1) | (1 << KEY_COL2) | (1 << KEY_COL3) | (1 << KEY_COL4));
}

// Scan keypad to detect which button is pressed - updated for 3x4 matrix
uint8_t Keypad_Scan(void) {
	static uint8_t debounce_count[12] = {0};  // 12 buttons total
	static uint8_t button_state[12] = {0};
	uint8_t current_state[12] = {0};
	uint8_t key_pressed = 0;
	uint8_t button_index = 0;
    
  // Scan Row 1 (PC4)
  KEY_PORT &= ~(1 << KEY_ROW1);  // Set Row 1 low
  PORTB |= (1 << KEY_ROW2) | (1 << KEY_ROW3);  // Set other rows high
  _delay_us(10);
   
    // Read column pins to detect which button is pressed in Row 1
    current_state[0] = !(KEY_PIN & (1 << KEY_COL1)); // Button 1
    current_state[1] = !(KEY_PIN & (1 << KEY_COL2)); // Button 2
    current_state[2] = !(KEY_PIN & (1 << KEY_COL3)); // Button 3
    current_state[3] = !(KEY_PIN & (1 << KEY_COL4)); // Button A
    
    KEY_PORT |= (1 << KEY_ROW1);  // Set Row 1 back high
	
	  // Scan Row 2 (PB2)
	  PORTB &= ~(1 << KEY_ROW2);  // Set Row 2 low
	  _delay_us(10);
	  
	  // Read column pins to detect which button is pressed in Row 2
	  current_state[4] = !(KEY_PIN & (1 << KEY_COL1)); // Button 4
	  current_state[5] = !(KEY_PIN & (1 << KEY_COL2)); // Button 5
	  current_state[6] = !(KEY_PIN & (1 << KEY_COL3)); // Button 6
	  current_state[7] = !(KEY_PIN & (1 << KEY_COL4)); // Button B
	  
	  PORTB |= (1 << KEY_ROW2);  // Set Row 2 back high
	  
	  // Scan Row 3 (PB3)
	  PORTB &= ~(1 << KEY_ROW3);  // Set Row 3 low
	  _delay_us(10);
	  
	  // Read column pins to detect which button is pressed in Row 3
	  current_state[8] = !(KEY_PIN & (1 << KEY_COL1));  // Button 7
	  current_state[9] = !(KEY_PIN & (1 << KEY_COL2));  // Button 8
	  current_state[10] = !(KEY_PIN & (1 << KEY_COL3)); // Button 9
	  current_state[11] = !(KEY_PIN & (1 << KEY_COL4)); // Button C (confirm)
	  
	  PORTB |= (1 << KEY_ROW3);  // Set Row 3 back high
    
    // Debounce each button
    for (uint8_t i = 0; i < 12; i++) {
	    // If state changed, reset the counter
	    if (current_state[i] != button_state[i]) {
		    debounce_count[i] = 0;
		    button_state[i] = current_state[i];
	    }
	    
	    // If state is stable for some time
	    if (debounce_count[i] < 5) {
		    debounce_count[i]++;
	    }
        
        // If state is stable for some time
        if (debounce_count[i] < 5) {
            debounce_count[i]++;
        }
        
        // Button press detected and debounced
        if (current_state[i] && debounce_count[i] == 5) {
	        debounce_count[i]++; // Ensure we only detect once per press
	        key_pressed = i + 1; // Return 1-12 for each button
        }
     }
        
     return key_pressed;
}
// Map keypad value to actual key value
uint8_t Keypad_GetValue(uint8_t key) {
	switch(key) {
		case 1: return 1;  // Row 1, Col 1: 1
		case 2: return 2;  // Row 1, Col 2: 2
		case 3: return 3;  // Row 1, Col 3: 3
		case 4: return 4;  // Row 2, Col 1: 4
		case 5: return 5;  // Row 2, Col 2: 5
		case 6: return 6;  // Row 2, Col 3: 6
		case 7: return 7;  // Row 3, Col 1: 7
		case 8: return 8;  // Row 3, Col 2: 8
		case 9: return 9;  // Row 3, Col 3: 9
		case 10: return 0; // Row 1, Col 4: A
		case 11: return 10;  // Row 2, Col 4: 0
		case 12: return 12; // Row 3, Col 4: # (Confirm button)
		default: return 0;
	}
}
// Handle payment input
void Handle_Payment_Input(uint8_t key_value) {
	// "Confirm" button, switch to payment mode
	if (key_value == 12 && currentMode == DISPLAY_SELECTION) {
		// Switch to payment mode
		currentMode = DISPLAY_PAYMENT;
		
		// Reset payment input
		for (uint8_t i = 0; i < 4; i++) {
			payment_digits[i] = 0;
		}
		current_digit = 0;
		
		// Update display to show zeros
		for (uint8_t i = 0; i < 4; i++) {
			seg_values[i] = 0;
		}
		
		// Update LCD to show payment mode
		LCD_Clear();
		LCD_PrintPos(0, 0, "Enter Amount:");
		LCD_PrintPos(1, 0, "0.00");
		
		return;
	}
	// If in payment mode and a numeric key is pressed (0-9)
	if (currentMode == DISPLAY_PAYMENT && key_value <= 9) {
		// Shift digits left
		for (uint8_t i = 0; i < 3; i++) {
			payment_digits[i] = payment_digits[i+1];
		}
		payment_digits[3] = key_value;
		
		// Update the 7-segment display
		for (uint8_t i = 0; i < 4; i++) {
			seg_values[i] = payment_digits[i];
		}
		
		// Update LCD to show current input
		char buffer[20];
		sprintf(buffer, "%d.%d%d", payment_digits[1], payment_digits[2], payment_digits[3]);
		LCD_PrintPos(1, 0, buffer);
	}
	
	// If "Confirm" button is pressed while in payment mode, process payment
	if (key_value == 12 && currentMode == DISPLAY_PAYMENT) {
		
		
	if((selectedItem == MENU_JUICE) && (seg_values[1] == 2) && (seg_values[2] == 5) && (seg_values[3] == 0)){
			LCD_PrintPos(0, 0, "Payment completed");
			LCD_PrintPos(1, 0, "Collect the item");
			 SUCCESS_LED_DDR |= (1 << SUCCESS_LED_PIN);
			 SUCCESS_LED_PORT &= ~(1 << SUCCESS_LED_PIN);
			 SUCCESS_LED_PORT |= (1 << SUCCESS_LED_PIN); 
			_delay_ms(10000);
	}
	else if((selectedItem == MENU_SNACK) && (seg_values[1] == 3) && (seg_values[2] == 2) && (seg_values[3] == 0)){
			LCD_PrintPos(0, 0, "Payment completed");
			LCD_PrintPos(1, 0, "Collect the item");
			 SUCCESS_LED_DDR |= (1 << SUCCESS_LED_PIN);
			 SUCCESS_LED_PORT &= ~(1 << SUCCESS_LED_PIN);
			 SUCCESS_LED_PORT |= (1 << SUCCESS_LED_PIN); 
			_delay_ms(10000);
			SUCCESS_LED_PORT &= ~(1 << SUCCESS_LED_PIN);
	}
	else if((selectedItem == MENU_CHIPS) && (seg_values[1] == 9) && (seg_values[2] == 3) && (seg_values[3] == 0)){
			LCD_PrintPos(0, 0, "Payment completed");
			LCD_PrintPos(1, 0, "Collect the item");
			 SUCCESS_LED_DDR |= (1 << SUCCESS_LED_PIN);
			 SUCCESS_LED_PORT &= ~(1 << SUCCESS_LED_PIN);
			 SUCCESS_LED_PORT |= (1 << SUCCESS_LED_PIN); 
			_delay_ms(10000);
			SUCCESS_LED_PORT &= ~(1 << SUCCESS_LED_PIN);
	}
	else if((selectedItem == MENU_WATER) && (seg_values[1] == 7) && (seg_values[2] == 5) && (seg_values[3] == 0)){
			LCD_PrintPos(0, 0, "Payment completed");
			LCD_PrintPos(1, 0, "Collect the item");
			 SUCCESS_LED_DDR |= (1 << SUCCESS_LED_PIN);
			 SUCCESS_LED_PORT &= ~(1 << SUCCESS_LED_PIN);
			 SUCCESS_LED_PORT |= (1 << SUCCESS_LED_PIN); 
			_delay_ms(10000);
			SUCCESS_LED_PORT &= ~(1 << SUCCESS_LED_PIN);
	}
	else{
			LCD_PrintPos(0, 0, "Payment failure");
			LCD_PrintPos(1, 0, "Try again");
			_delay_ms(10000);
		}
		currentMode = DISPLAY_MENU;
		Update_Display();
	}
}
// Update the display based on current menu state and mode
void Update_Display(void) {
    char buffer[20];
    LCD_Clear();
    
    if (currentMode == DISPLAY_MENU) {
        // First line shows title
        LCD_PrintPos(0, 0, "Menu Selection:");
        
        // Second line shows current menu item
        switch (currentMenu) {
            case MENU_JUICE:
                LCD_PrintPos(1, 0, "1. Juice $2.50");
                break;
            case MENU_SNACK:
                LCD_PrintPos(1, 0, "2. Snack $3.20");
                break;
            case MENU_CHIPS:
                LCD_PrintPos(1, 0, "3. Chips $9.30");
                break;
            case MENU_WATER:
                LCD_PrintPos(1, 0, "4. Water $7.50");
                break;
            case MENU_PIWO:
                LCD_PrintPos(1, 0, "5. Piwo $7.50");
                break;
            default:
                LCD_PrintPos(1, 0, "Invalid Option");
                break;
        }
		if (currentMode == DISPLAY_PAYMENT) {
			
			return;
		}
		
    } else if (currentMode == DISPLAY_SELECTION) {
        // Display the selected item
        _delay_ms(100);
        LCD_PrintPos(0, 0, "Selected item:");
        
        // Format: "X. Name Price"
        switch(selectedItem){
            case MENU_JUICE:
                LCD_PrintPos(1, 0, "1. Juice $2.50");
                break;
            case MENU_SNACK:
                LCD_PrintPos(1, 0, "2. Snack $3.20");
                break;
            case MENU_CHIPS:
                LCD_PrintPos(1, 0, "3. Chips $9.30");
                break;
            case MENU_WATER:
                LCD_PrintPos(1, 0, "4. Water $7.50");
                break;
            case MENU_PIWO:
                LCD_PrintPos(1, 0, "5. Piwo $7.50");
                break;
        }
    }
   
    if (currentMode == DISPLAY_MENU) {
        seg_values[0] = currentMenu + 1;  // Show menu number (1-5)
        seg_values[1] = 0;  // Clear other digits
        seg_values[2] = 0;
        seg_values[3] = 0;
    } else if (currentMode == DISPLAY_SELECTION) {
        seg_values[0] = selectedItem + 1;  // Show selected item number (1-5)
        
        switch(selectedItem) {
            case MENU_JUICE:
                seg_values[1] = 10;  // $2.50 
                seg_values[2] = 5;
                seg_values[3] = 0;
                break;
            case MENU_SNACK:
                seg_values[1] = 11;  // $3.20
                seg_values[2] = 2;
                seg_values[3] = 0;
                break;
            case MENU_CHIPS:
                seg_values[1] = 13;  // $9.30
                seg_values[2] = 3;
                seg_values[3] = 0;
                break;
            case MENU_WATER:
				seg_values[1] = 12;  // $7.50
                seg_values[2] = 5;
                seg_values[3] = 0;
                break;
            case MENU_PIWO:
                seg_values[1] = 17;  // $7.50
                seg_values[2] = 5;
                seg_values[3] = 0;
                break;
        }
    }
}

// Initialize 7-Segment Display
void SevenSeg_Init(void) {
    // Configure digit control pins as outputs (active LOW, initially high)
    SEG_DDR_CTRL |= (1 << SEG_DIGIT1) | (1 << SEG_DIGIT2) | (1 << SEG_DIGIT3) | (1 << SEG_DIGIT4);
    SEG_PORT_CTRL |= (1 << SEG_DIGIT1) | (1 << SEG_DIGIT2) | (1 << SEG_DIGIT3) | (1 << SEG_DIGIT4);
    
    // Configure segment pins as outputs (active LOW, initially high)
    // PD2-PD7 for segments
    SEG_DDR_DATA1 |= 0xFC;  // 0b11111100 = bits 2-7
    SEG_PORT_DATA1 |= 0xFC; // All segments off initially
    
    // PB0-PB1 for remaining segments
    SEG_DDR_DATA2 |= 0x03;  // 0b00000011 = bits 0-1
    SEG_PORT_DATA2 |= 0x03; // All segments off initially
    
    // Initialize display with all segments off
    for (uint8_t i = 0; i < 4; i++) {
        seg_values[i] = 0;
    }
    
    // Set up Timer0 for display multiplexing
    TCCR0A = (1 << WGM01);              // CTC mode
    TCCR0B = (1 << CS01) | (1 << CS00); // Prescaler 64
    OCR0A = 249;                        // For 1ms interrupt at 16MHz: 16MHz/64/250 = 1kHz
    TIMSK0 = (1 << OCIE0A);             // Enable compare match interrupt
}

// Update a specific digit of the 7-segment display
void SevenSeg_Update(void) {
    // Turn off all digits
    SEG_PORT_CTRL |= (1 << SEG_DIGIT1) | (1 << SEG_DIGIT2) | (1 << SEG_DIGIT3) | (1 << SEG_DIGIT4);
    
    // Get pattern for current digit value (0-15)
    uint8_t pattern = 0xFF;  // All segments off by default
    if (seg_values[seg_digit] < 16) {
        pattern = seven_seg_digits[seg_values[seg_digit]];
    }
    
    // Output segment pattern to Port D (PD2-PD7)
    SEG_PORT_DATA1 &= 0x03;              // Clear segment bits
    SEG_PORT_DATA1 |= (pattern & 0xFC);  // Set new segment bits (bits 2-7)
    
    // Output segment pattern to Port B (PB0-PB1)
    SEG_PORT_DATA2 &= 0xFC;              // Clear segment bits
    SEG_PORT_DATA2 |= (pattern & 0x03);  // Set new segment bits (bits 0-1)
    
    // Select the current digit (active LOW)
    switch (seg_digit) {
        case 0:
            SEG_PORT_CTRL &= ~(1 << SEG_DIGIT1);
            break;
        case 1:
            SEG_PORT_CTRL &= ~(1 << SEG_DIGIT2);
            break;
        case 2:
            SEG_PORT_CTRL &= ~(1 << SEG_DIGIT3);
            break;
        case 3:
            SEG_PORT_CTRL &= ~(1 << SEG_DIGIT4);
            break;
    }
    
    // Move to next digit
    seg_digit = (seg_digit + 1) % 4;
}

// Timer0 Compare A interrupt for 7-segment display multiplexing
ISR(TIMER0_COMPA_vect) {
    SevenSeg_Update();
}

// Handle selection mode timing
void Process_Selection(uint16_t *selection_timer) {
    // If we're in selection display mode
    if (currentMode == DISPLAY_SELECTION) {
        // Increment selection timer (each iteration is 10ms)
        (*selection_timer)++;
        
        // Show selection for 3 seconds (300 iterations), then go back to menu
        if (*selection_timer >= 1000) {
            *selection_timer = 0;
            currentMode = DISPLAY_MENU;
            Update_Display();
        }
    }
}

int main(void) {
	uint16_t timer_counter = 0;
	uint16_t selection_timer = 0;
	uint8_t key_raw;
	uint8_t key_value;
	
	// Initialize LCD, keypad, and 7-segment display
	LCD_Init();
	Keypad_Init();
	SevenSeg_Init();
	
	// Enable global interrupts
	sei();
	
	_delay_ms(100);
	
	// Initial display update
	Update_Display();
	
	// Main loop
	while (1) {
		// Check if any key is pressed
		key_raw = Keypad_Scan();
		if (key_raw) {
			key_value = Keypad_GetValue(key_raw);
			
			  // Handle regular menu selection (works in both MENU and SELECTION modes)
			  if ((currentMode == DISPLAY_MENU || currentMode == DISPLAY_SELECTION) && key_value >= 1 && key_value <= 5) {
				  selectedItem = key_value - 1; // Convert key to menu index
				  currentMode = DISPLAY_SELECTION;
				  selection_timer = 0; // Reset timer for new selection
				  Update_Display();
			  }
			
			// Handle payment mode or confirm button
			Handle_Payment_Input(key_value);
		}
		
		// Handle selection mode timing
		Process_Selection(&selection_timer);
		
		// If in menu mode, handle menu cycling
		if (currentMode == DISPLAY_MENU) {
			// Increment timer counter (each iteration is 10ms)
			timer_counter++;
			
			// Change menu every 10 seconds (1000 iterations)
			if (timer_counter >= 1000) {
				// Reset counter
				timer_counter = 0;
				
				// Move to next menu item
				currentMenu = (currentMenu + 1) % MENU_MAX;
				
				// Update display
				Update_Display();
			}
			} else {
			// Reset timer when not in menu mode
			timer_counter = 0;
		}
		
		// Small delay of 10ms
		_delay_ms(10);
	}
	
	return 0;  // Never reached
}

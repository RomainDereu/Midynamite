/**
 * This Library was originally written by Olivier Van den Eede (4ilo) in 2016.
 * Some refactoring was done and SPI support was added by Aleksander Alekseev (afiskon) in 2018.
 *
 * https://github.com/afiskon/stm32-screen_driver
 *
 * Further modifications by Romain Dereu to implement inside Midynamite
 *
 *
 */


#include "screen_driver.h"
#include "screen_driver_fonts.h"
#include "memory_main.h" //For SETTINGS_BRIGHTNESS
#include <math.h>
#include <stdlib.h>
#include <string.h>  // For memcpy

#if defined(screen_driver_USE_I2C)

void screen_driver_Reset(void) {
    /* for I2C - do nothing */
}

// Send a byte to the command register
void screen_driver_WriteCommand(uint8_t byte) {
    HAL_I2C_Mem_Write(&screen_driver_I2C_PORT, screen_driver_I2C_ADDR, 0x00, 1, &byte, 1, HAL_MAX_DELAY);
}

// Send data
void screen_driver_WriteData(uint8_t* buffer, size_t buff_size) {
    HAL_I2C_Mem_Write(&screen_driver_I2C_PORT, screen_driver_I2C_ADDR, 0x40, 1, buffer, buff_size, HAL_MAX_DELAY);
}

#elif defined(screen_driver_USE_SPI)

void screen_driver_Reset(void) {
    // CS = High (not selected)
    HAL_GPIO_WritePin(screen_driver_CS_Port, screen_driver_CS_Pin, GPIO_PIN_SET);

    // Reset the OLED
    HAL_GPIO_WritePin(screen_driver_Reset_Port, screen_driver_Reset_Pin, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(screen_driver_Reset_Port, screen_driver_Reset_Pin, GPIO_PIN_SET);
    HAL_Delay(10);
}

// Send a byte to the command register
void screen_driver_WriteCommand(uint8_t byte) {
    HAL_GPIO_WritePin(screen_driver_CS_Port, screen_driver_CS_Pin, GPIO_PIN_RESET); // select OLED
    HAL_GPIO_WritePin(screen_driver_DC_Port, screen_driver_DC_Pin, GPIO_PIN_RESET); // command
    HAL_SPI_Transmit(&screen_driver_SPI_PORT, (uint8_t *) &byte, 1, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(screen_driver_CS_Port, screen_driver_CS_Pin, GPIO_PIN_SET); // un-select OLED
}

// Send data
void screen_driver_WriteData(uint8_t* buffer, size_t buff_size) {
    HAL_GPIO_WritePin(screen_driver_CS_Port, screen_driver_CS_Pin, GPIO_PIN_RESET); // select OLED
    HAL_GPIO_WritePin(screen_driver_DC_Port, screen_driver_DC_Pin, GPIO_PIN_SET); // data
    HAL_SPI_Transmit(&screen_driver_SPI_PORT, buffer, buff_size, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(screen_driver_CS_Port, screen_driver_CS_Pin, GPIO_PIN_SET); // un-select OLED
}

#else
#error "You should define screen_driver_USE_SPI or screen_driver_USE_I2C macro"
#endif


// Screenbuffer
static uint8_t screen_driver_Buffer[screen_driver_BUFFER_SIZE];

// Screen object
static screen_driver_t screen_driver;

/* Fills the Screenbuffer with values from a given buffer of a fixed length */
screen_driver_Error_t screen_driver_FillBuffer(uint8_t* buf, uint32_t len) {
    screen_driver_Error_t ret = screen_driver_ERR;
    if (len <= screen_driver_BUFFER_SIZE) {
        memcpy(screen_driver_Buffer,buf,len);
        ret = screen_driver_OK;
    }
    return ret;
}

/* Initialize the oled screen */
void screen_driver_Init(void) {
    // Reset OLED
    screen_driver_Reset();

    // Wait for the screen to boot
    HAL_Delay(100);

    // Init OLED
    screen_driver_SetDisplayOn(0); //display off

    screen_driver_WriteCommand(0x20); //Set Memory Addressing Mode
    screen_driver_WriteCommand(0x00); // 00b,Horizontal Addressing Mode; 01b,Vertical Addressing Mode;
                                // 10b,Page Addressing Mode (RESET); 11b,Invalid

    screen_driver_WriteCommand(0xB0); //Set Page Start Address for Page Addressing Mode,0-7

#ifdef screen_driver_MIRROR_VERT
    screen_driver_WriteCommand(0xC0); // Mirror vertically
#else
    screen_driver_WriteCommand(0xC8); //Set COM Output Scan Direction
#endif

#ifdef screen_driver_MIRROR_HORIZ
    screen_driver_WriteCommand(0xA0); // Mirror horizontally
#else
    screen_driver_WriteCommand(0xA1); //--set segment re-map 0 to 127 - CHECK
#endif

    //Romain test

    //A0 A1: Segment remap
    //A1: Normal
    //A0 Mirror horizontaly

    //C0 C8 Scan direction
    //C0 Mirror Verticaly

    //Default A1
    //Default C8
    //Default 20

    screen_driver_WriteCommand(0xA1);
    screen_driver_WriteCommand(0xC8);


    //Default low 00
    //Default high 10
    screen_driver_WriteCommand(0x00); //---set low column address
    screen_driver_WriteCommand(0x10); //---set high column address

    screen_driver_WriteCommand(0x40); //--set start line address - CHECK

    screen_driver_SetContrast(0xFF);



#ifdef screen_driver_INVERSE_COLOR
    screen_driver_WriteCommand(0xA7); //--set inverse color
#else
    screen_driver_WriteCommand(0xA6); //--set normal color
#endif

// Set multiplex ratio.
#if (screen_driver_HEIGHT == 128)
    // Found in the Luma Python lib for SH1106.
    screen_driver_WriteCommand(0xFF);
#else
    screen_driver_WriteCommand(0xA8); //--set multiplex ratio(1 to 64) - CHECK
#endif

#if (screen_driver_HEIGHT == 32)
    screen_driver_WriteCommand(0x1F); //
#elif (screen_driver_HEIGHT == 64)
    screen_driver_WriteCommand(0x3F); //
#elif (screen_driver_HEIGHT == 128)
    screen_driver_WriteCommand(0x3F); // Seems to work for 128px high displays too.
#else
#error "Only 32, 64, or 128 lines of height are supported!"
#endif

    screen_driver_WriteCommand(0xA4); //0xa4,Output follows RAM content;0xa5,Output ignores RAM content

    screen_driver_WriteCommand(0xD3); //-set display offset - CHECK
    screen_driver_WriteCommand(0x00); //-not offset

    screen_driver_WriteCommand(0xD5); //--set display clock divide ratio/oscillator frequency
    screen_driver_WriteCommand(0xF0); //--set divide ratio

    screen_driver_WriteCommand(0xD9); //--set pre-charge period
    screen_driver_WriteCommand(0x22); //

    screen_driver_WriteCommand(0xDA); //--set com pins hardware configuration - CHECK
#if (screen_driver_HEIGHT == 32)
    screen_driver_WriteCommand(0x02);
#elif (screen_driver_HEIGHT == 64)
    screen_driver_WriteCommand(0x12);
#elif (screen_driver_HEIGHT == 128)
    screen_driver_WriteCommand(0x12);
#else
#error "Only 32, 64, or 128 lines of height are supported!"
#endif

    screen_driver_WriteCommand(0xDB); //--set vcomh


    screen_driver_WriteCommand(0x8D); //--set DC-DC enable
    screen_driver_WriteCommand(0x14); //
    screen_driver_SetDisplayOn(1); //--turn on screen_driver panel

    // Clear screen
    screen_driver_Fill(Black);
    
    // Flush buffer to screen
    screen_driver_UpdateScreen();
    
    // Set default values for screen object
    screen_driver.CurrentX = 0;
    screen_driver.CurrentY = 0;
    
    screen_driver.Initialized = 1;
}

/* Fill the whole screen with the given color */
void screen_driver_Fill(screen_driver_COLOR color) {
    memset(screen_driver_Buffer, (color == Black) ? 0x00 : 0xFF, sizeof(screen_driver_Buffer));
}

/* Write the screenbuffer with changed to the screen */
void screen_driver_UpdateScreen(void) {
    // Write data to each page of RAM. Number of pages
    // depends on the screen height:
    //
    //  * 32px   ==  4 pages
    //  * 64px   ==  8 pages
    //  * 128px  ==  16 pages
    for(uint8_t i = 0; i < screen_driver_HEIGHT/8; i++) {
        screen_driver_WriteCommand(0xB0 + i); // Set the current RAM page address.
        screen_driver_WriteCommand(0x00 + screen_driver_X_OFFSET_LOWER);
        screen_driver_WriteCommand(0x10 + screen_driver_X_OFFSET_UPPER);
        screen_driver_WriteData(&screen_driver_Buffer[screen_driver_WIDTH*i],screen_driver_WIDTH);
    }
}

/*
 * Draw one pixel in the screenbuffer
 * X => X Coordinate
 * Y => Y Coordinate
 * color => Pixel color
 */
void screen_driver_DrawPixel(uint8_t x, uint8_t y, screen_driver_COLOR color) {
    if(x >= screen_driver_WIDTH || y >= screen_driver_HEIGHT) {
        // Don't write outside the buffer
        return;
    }
   
    // Draw in the right color
    if(color == White) {
        screen_driver_Buffer[x + (y / 8) * screen_driver_WIDTH] |= 1 << (y % 8);
    } else { 
        screen_driver_Buffer[x + (y / 8) * screen_driver_WIDTH] &= ~(1 << (y % 8));
    }
}

/*
 * Draw 1 char to the screen buffer
 * ch       => char om weg te schrijven
 * Font     => Font waarmee we gaan schrijven
 * color    => Black or White
 */
char screen_driver_WriteChar(char ch, screen_driver_Font_t Font, screen_driver_COLOR color) {
    uint32_t i, b, j;
    
    // Check if character is valid
    if (ch < 32 || ch > 126)
        return 0;
    
    // Check remaining space on current line
    if (screen_driver_WIDTH < (screen_driver.CurrentX + Font.width) ||
        screen_driver_HEIGHT < (screen_driver.CurrentY + Font.height))
    {
        // Not enough space on current line
        return 0;
    }
    
    // Use the font to write
    for(i = 0; i < Font.height; i++) {
        b = Font.data[(ch - 32) * Font.height + i];
        for(j = 0; j < Font.width; j++) {
            if((b << j) & 0x8000)  {
                screen_driver_DrawPixel(screen_driver.CurrentX + j, (screen_driver.CurrentY + i), (screen_driver_COLOR) color);
            } else {
                screen_driver_DrawPixel(screen_driver.CurrentX + j, (screen_driver.CurrentY + i), (screen_driver_COLOR)!color);
            }
        }
    }
    
    // The current space is now taken
    screen_driver.CurrentX += Font.char_width ? Font.char_width[ch - 32] : Font.width;
    
    // Return written char for validation
    return ch;
}

/* Write full string to screenbuffer */
char screen_driver_WriteString(const char* str, screen_driver_Font_t Font, screen_driver_COLOR color) {
    while (*str) {
        if (screen_driver_WriteChar(*str, Font, color) != *str) {
            // Char could not be written
            return *str;
        }
        str++;
    }
    
    // Everything ok
    return *str;
}

/* Position the cursor */
void screen_driver_SetCursor(uint8_t x, uint8_t y) {
    screen_driver.CurrentX = x;
    screen_driver.CurrentY = y;
}

/* Draw line by Bresenhem's algorithm */
void screen_driver_Line(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, screen_driver_COLOR color) {
    int32_t deltaX = abs(x2 - x1);
    int32_t deltaY = abs(y2 - y1);
    int32_t signX = ((x1 < x2) ? 1 : -1);
    int32_t signY = ((y1 < y2) ? 1 : -1);
    int32_t error = deltaX - deltaY;
    int32_t error2;
    
    screen_driver_DrawPixel(x2, y2, color);

    while((x1 != x2) || (y1 != y2)) {
        screen_driver_DrawPixel(x1, y1, color);
        error2 = error * 2;
        if(error2 > -deltaY) {
            error -= deltaY;
            x1 += signX;
        }
        
        if(error2 < deltaX) {
            error += deltaX;
            y1 += signY;
        }
    }
    return;
}



/* Draw polyline */
void screen_driver_Polyline(const screen_driver_VERTEX *par_vertex, uint16_t par_size, screen_driver_COLOR color) {
    uint16_t i;
    if(par_vertex == NULL) {
        return;
    }

    for(i = 1; i < par_size; i++) {
        screen_driver_Line(par_vertex[i - 1].x, par_vertex[i - 1].y, par_vertex[i].x, par_vertex[i].y, color);
    }

    return;
}

/* Convert Degrees to Radians */
static float screen_driver_DegToRad(float par_deg) {
    return par_deg * (3.14f / 180.0f);
}

/* Normalize degree to [0;360] */
static uint16_t screen_driver_NormalizeTo0_360(uint16_t par_deg) {
    uint16_t loc_angle;
    if(par_deg <= 360) {
        loc_angle = par_deg;
    } else {
        loc_angle = par_deg % 360;
        loc_angle = (loc_angle ? loc_angle : 360);
    }
    return loc_angle;
}

/*
 * DrawArc. Draw angle is beginning from 4 quart of trigonometric circle (3pi/2)
 * start_angle in degree
 * sweep in degree
 */
void screen_driver_DrawArc(uint8_t x, uint8_t y, uint8_t radius, uint16_t start_angle, uint16_t sweep, screen_driver_COLOR color) {
    static const uint8_t CIRCLE_APPROXIMATION_SEGMENTS = 36;
    float approx_degree;
    uint32_t approx_segments;
    uint8_t xp1,xp2;
    uint8_t yp1,yp2;
    uint32_t count;
    uint32_t loc_sweep;
    float rad;
    
    loc_sweep = screen_driver_NormalizeTo0_360(sweep);
    
    count = (screen_driver_NormalizeTo0_360(start_angle) * CIRCLE_APPROXIMATION_SEGMENTS) / 360;
    approx_segments = (loc_sweep * CIRCLE_APPROXIMATION_SEGMENTS) / 360;
    approx_degree = loc_sweep / (float)approx_segments;
    while(count < approx_segments)
    {
        rad = screen_driver_DegToRad(count*approx_degree);
        xp1 = x + (int8_t)(sinf(rad)*radius);
        yp1 = y + (int8_t)(cosf(rad)*radius);    
        count++;
        if(count != approx_segments) {
            rad = screen_driver_DegToRad(count*approx_degree);
        } else {
            rad = screen_driver_DegToRad(loc_sweep);
        }
        xp2 = x + (int8_t)(sinf(rad)*radius);
        yp2 = y + (int8_t)(cosf(rad)*radius);    
        screen_driver_Line(xp1,yp1,xp2,yp2,color);
    }
    
    return;
}

/*
 * Draw arc with radius line
 * Angle is beginning from 4 quart of trigonometric circle (3pi/2)
 * start_angle: start angle in degree
 * sweep: finish angle in degree
 */
void screen_driver_DrawArcWithRadiusLine(uint8_t x, uint8_t y, uint8_t radius, uint16_t start_angle, uint16_t sweep, screen_driver_COLOR color) {
    const uint32_t CIRCLE_APPROXIMATION_SEGMENTS = 36;
    float approx_degree;
    uint32_t approx_segments;
    uint8_t xp1;
    uint8_t xp2 = 0;
    uint8_t yp1;
    uint8_t yp2 = 0;
    uint32_t count;
    uint32_t loc_sweep;
    float rad;
    
    loc_sweep = screen_driver_NormalizeTo0_360(sweep);
    
    count = (screen_driver_NormalizeTo0_360(start_angle) * CIRCLE_APPROXIMATION_SEGMENTS) / 360;
    approx_segments = (loc_sweep * CIRCLE_APPROXIMATION_SEGMENTS) / 360;
    approx_degree = loc_sweep / (float)approx_segments;

    rad = screen_driver_DegToRad(count*approx_degree);
    uint8_t first_point_x = x + (int8_t)(sinf(rad)*radius);
    uint8_t first_point_y = y + (int8_t)(cosf(rad)*radius);   
    while (count < approx_segments) {
        rad = screen_driver_DegToRad(count*approx_degree);
        xp1 = x + (int8_t)(sinf(rad)*radius);
        yp1 = y + (int8_t)(cosf(rad)*radius);    
        count++;
        if (count != approx_segments) {
            rad = screen_driver_DegToRad(count*approx_degree);
        } else {
            rad = screen_driver_DegToRad(loc_sweep);
        }
        xp2 = x + (int8_t)(sinf(rad)*radius);
        yp2 = y + (int8_t)(cosf(rad)*radius);    
        screen_driver_Line(xp1,yp1,xp2,yp2,color);
    }
    
    // Radius line
    screen_driver_Line(x,y,first_point_x,first_point_y,color);
    screen_driver_Line(x,y,xp2,yp2,color);
    return;
}

/* Draw circle by Bresenhem's algorithm */
void screen_driver_DrawCircle(uint8_t par_x,uint8_t par_y,uint8_t par_r,screen_driver_COLOR par_color) {
    int32_t x = -par_r;
    int32_t y = 0;
    int32_t err = 2 - 2 * par_r;
    int32_t e2;

    if (par_x >= screen_driver_WIDTH || par_y >= screen_driver_HEIGHT) {
        return;
    }

    do {
        screen_driver_DrawPixel(par_x - x, par_y + y, par_color);
        screen_driver_DrawPixel(par_x + x, par_y + y, par_color);
        screen_driver_DrawPixel(par_x + x, par_y - y, par_color);
        screen_driver_DrawPixel(par_x - x, par_y - y, par_color);
        e2 = err;

        if (e2 <= y) {
            y++;
            err = err + (y * 2 + 1);
            if(-x == y && e2 <= x) {
                e2 = 0;
            }
        }

        if (e2 > x) {
            x++;
            err = err + (x * 2 + 1);
        }
    } while (x <= 0);

    return;
}

/* Draw filled circle. Pixel positions calculated using Bresenham's algorithm */
void screen_driver_FillCircle(uint8_t par_x,uint8_t par_y,uint8_t par_r,screen_driver_COLOR par_color) {
    int32_t x = -par_r;
    int32_t y = 0;
    int32_t err = 2 - 2 * par_r;
    int32_t e2;

    if (par_x >= screen_driver_WIDTH || par_y >= screen_driver_HEIGHT) {
        return;
    }

    do {
        for (uint8_t _y = (par_y + y); _y >= (par_y - y); _y--) {
            for (uint8_t _x = (par_x - x); _x >= (par_x + x); _x--) {
                screen_driver_DrawPixel(_x, _y, par_color);
            }
        }

        e2 = err;
        if (e2 <= y) {
            y++;
            err = err + (y * 2 + 1);
            if (-x == y && e2 <= x) {
                e2 = 0;
            }
        }

        if (e2 > x) {
            x++;
            err = err + (x * 2 + 1);
        }
    } while (x <= 0);

    return;
}

/* Draw a rectangle */
void screen_driver_DrawRectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, screen_driver_COLOR color) {
    screen_driver_Line(x1,y1,x2,y1,color);
    screen_driver_Line(x2,y1,x2,y2,color);
    screen_driver_Line(x2,y2,x1,y2,color);
    screen_driver_Line(x1,y2,x1,y1,color);

    return;
}

static void screen_driver_SetCursor_WriteString(const char* str, screen_driver_Font_t font,
										 screen_driver_COLOR color,
										 uint8_t x_align,
										 uint8_t y_align){
	screen_driver_SetCursor(x_align, y_align);
	screen_driver_WriteString(str, font , color);
}



static void screen_driver_underline_WriteString(const char* str, screen_driver_Font_t font,
										  screen_driver_COLOR color,
										  uint8_t x_align,
										  uint8_t y_align,
										  uint8_t underlined){

	uint8_t line_height = font.height +1;
	screen_driver_SetCursor(x_align, y_align);
	screen_driver_WriteString(str, font , color);
	if(underlined == 1){
		screen_driver_SetCursor(x_align, y_align +1);
		uint8_t line_length = font.width * strlen(str);
		screen_driver_Line(x_align, y_align + line_height,
						   x_align+ line_length, y_align + line_height, White);

	}
}



void write_68(const char* str, uint8_t x_align, uint8_t y_align){

	screen_driver_SetCursor_WriteString(str, Font_6x8 , White, x_align, y_align);
}

void write_1118(const char* str, uint8_t x_align, uint8_t y_align){
	screen_driver_SetCursor_WriteString(str, Font_11x18 , White, x_align, y_align);
}


void write_underline_68(const char* str, uint8_t x_align, uint8_t y_align, uint8_t underlined){
	screen_driver_underline_WriteString(str, Font_6x8 , White, x_align, y_align, underlined);
}

void write_underline_68_2(const char* str, uint8_t x_align, uint8_t y_align, uint8_t underlined){
	screen_driver_underline_WriteString(str, Font_6x8_2 , White, x_align, y_align, underlined);
}

void write_underline_1118(const char* str, uint8_t x_align, uint8_t y_align, uint8_t underlined){
	screen_driver_underline_WriteString(str, Font_11x18 , White, x_align, y_align, underlined);
}


void write_underline_1624(const char* str, uint8_t x_align, uint8_t y_align, uint8_t underlined){
	screen_driver_underline_WriteString(str, Font_16x24 , White, x_align, y_align, underlined);
}





/* Draw a filled rectangle */
void screen_driver_FillRectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, screen_driver_COLOR color) {
    uint8_t x_start = ((x1<=x2) ? x1 : x2);
    uint8_t x_end   = ((x1<=x2) ? x2 : x1);
    uint8_t y_start = ((y1<=y2) ? y1 : y2);
    uint8_t y_end   = ((y1<=y2) ? y2 : y1);

    for (uint8_t y= y_start; (y<= y_end)&&(y<screen_driver_HEIGHT); y++) {
        for (uint8_t x= x_start; (x<= x_end)&&(x<screen_driver_WIDTH); x++) {
            screen_driver_DrawPixel(x, y, color);
        }
    }
    return;
}

screen_driver_Error_t screen_driver_InvertRectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2) {
  if ((x2 >= screen_driver_WIDTH) || (y2 >= screen_driver_HEIGHT)) {
    return screen_driver_ERR;
  }
  if ((x1 > x2) || (y1 > y2)) {
    return screen_driver_ERR;
  }
  uint32_t i;
  if ((y1 / 8) != (y2 / 8)) {
    /* if rectangle doesn't lie on one 8px row */
    for (uint32_t x = x1; x <= x2; x++) {
      i = x + (y1 / 8) * screen_driver_WIDTH;
      screen_driver_Buffer[i] ^= 0xFF << (y1 % 8);
      i += screen_driver_WIDTH;
      for (; i < x + (y2 / 8) * screen_driver_WIDTH; i += screen_driver_WIDTH) {
        screen_driver_Buffer[i] ^= 0xFF;
      }
      screen_driver_Buffer[i] ^= 0xFF >> (7 - (y2 % 8));
    }
  } else {
    /* if rectangle lies on one 8px row */
    const uint8_t mask = (0xFF << (y1 % 8)) & (0xFF >> (7 - (y2 % 8)));
    for (i = x1 + (y1 / 8) * screen_driver_WIDTH;
         i <= (uint32_t)x2 + (y2 / 8) * screen_driver_WIDTH; i++) {
      screen_driver_Buffer[i] ^= mask;
    }
  }
  return screen_driver_OK;
}

/* Draw a bitmap */
void screen_driver_DrawBitmap(uint8_t x, uint8_t y, const unsigned char* bitmap, uint8_t w, uint8_t h, screen_driver_COLOR color) {
    int16_t byteWidth = (w + 7) / 8; // Bitmap scanline pad = whole byte
    uint8_t byte = 0;

    if (x >= screen_driver_WIDTH || y >= screen_driver_HEIGHT) {
        return;
    }

    for (uint8_t j = 0; j < h; j++, y++) {
        for (uint8_t i = 0; i < w; i++) {
            if (i & 7) {
                byte <<= 1;
            } else {
                byte = (*(const unsigned char *)(&bitmap[j * byteWidth + i / 8]));
            }

            if (byte & 0x80) {
                screen_driver_DrawPixel(x + i, y, color);
            }
        }
    }
    return;
}

void screen_driver_SetContrast(const uint8_t value) {
    const uint8_t kSetContrastControlRegister = 0x81;
    screen_driver_WriteCommand(kSetContrastControlRegister);
    screen_driver_WriteCommand(value);
}

void screen_driver_UpdateContrast(){
	  uint8_t brightness = (uint8_t)save_get(SETTINGS_BRIGHTNESS);
	  static const uint8_t contrast_values[11] =
	      {0x39, 0x39, 0x53, 0x6D, 0x87, 0xA1, 0xBB, 0xD5, 0xEF, 0xF9, 0xFF};

	  if (brightness > 10) {
	      brightness = 10;
	  }

	  const uint8_t new_contrast = contrast_values[brightness];

	  screen_driver_SetContrast(new_contrast);
}

void screen_driver_SetDisplayOn(const uint8_t on) {
    uint8_t value;
    if (on) {
        value = 0xAF;   // Display on
        screen_driver.DisplayOn = 1;
    } else {
        value = 0xAE;   // Display off
        screen_driver.DisplayOn = 0;
    }
    screen_driver_WriteCommand(value);
}

uint8_t screen_driver_GetDisplayOn() {
    return screen_driver.DisplayOn;
}

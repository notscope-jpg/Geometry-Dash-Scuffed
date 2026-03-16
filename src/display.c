#include <stm32f031x6.h>
#include "font5x7.h"
#include "display.h"
#define SCREEN_WIDTH 160
#define SCREEN_HEIGHT 128


void clear(void);
static uint32_t mystrlen(const char *s);
static void drawLineLowSlope(uint16_t x0, uint16_t y0, uint16_t x1,uint16_t y1, uint16_t Colour);
static void drawLineHighSlope(uint16_t x0, uint16_t y0, uint16_t x1,uint16_t y1, uint16_t Colour);
static int iabs(int x);
static void openAperture(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
static void CSLow(void);
static void CSHigh(void);
static void DCLow(void);
static void DCHigh(void);
static void initSPI(void);
static uint8_t transferSPI8(uint8_t data);
static uint16_t transferSPI16(uint16_t data);
static void command(uint8_t cmd);
static void data(uint8_t data);
static void ResetLow(void);
static void ResetHigh(void);

void display_begin()
{

	RCC->AHBENR |= (1 << 17);  // Turn on GPIO A
	// Configure PA3 for Reset pin
	GPIOA->MODER |= (1 << 6);
	GPIOA->MODER &= ~(1u << 7);
	// Configure PA4 for CS pin
	GPIOA->MODER |= (1 << 8);
	GPIOA->MODER &= ~(1u << 9);
	// Configure PA6 for D/C pin
	GPIOA->MODER |= (1 << 12);
	GPIOA->MODER &= ~(1u << 13);
	initSPI();
	//  hw_test();
	// Lots of CS toggling here seems to have made the boot up more reliable
	CSHigh();
	ResetHigh();	
	delay(10);
	ResetLow();
	delay(200);
	ResetHigh();
	CSHigh();
	delay(200);
	CSLow();
	delay(20);
	command(0x01); // software reset
	delay(100);
	CSHigh();
	delay(1);
	CSLow();
	delay(1);
	command(0x11); // exit sleep
	delay(120);
	// set frame rate
	CSHigh();
	delay(1);
	CSLow();
	delay(1);
	command(0xb1);
	data(0x05);
	data(0x3c);
	data(0x3c);
	CSHigh();
	delay(1);
	CSLow();
	delay(1);
	command(0xb2);
	data(0x05);
	data(0x3c);
	data(0x3c);
	CSHigh();
	delay(1);
	CSLow();
	delay(1);
	command(0xb3);
	data(0x05);
	data(0x3c);
	data(0x3c);	
	data(0x05);
	data(0x3c);
	data(0x3c);
	CSHigh();
	delay(1);
	CSLow();
	delay(1);
	command(0xb4); // dot invert
	data(0x03);
	CSHigh();
	delay(1);			
	CSHigh();
	delay(1);
	CSLow();
	delay(1);
	command(0x36);// Set pixel and RGB order
	data(0xa8); 	
	CSHigh();
	delay(1);
	CSLow();
	delay(1);		
	CSHigh();
	delay(1);
	CSLow();
	delay(1);
	command(0x3a);// Set colour mode        
	data(0x5); 	
	CSHigh();
	delay(1);
	CSLow();
	delay(1);
	command(0x29);    // display on
	delay(100);
	CSHigh();
	delay(1);
	CSLow();
	delay(1);
	command(0x2c);   // put display in to write mode

	fillRectangle(0,0,SCREEN_WIDTH, SCREEN_HEIGHT, 0x0);  // black out the screen
}
void ResetLow()
{
	GPIOA->ODR &= ~(1u << 3);
}
void ResetHigh()
{
	GPIOA->ODR |= (1 << 3);
}
void CSLow()
{
	GPIOA->ODR &= ~(1u << 4);
}
void CSHigh()
{
	GPIOA->ODR |= (1 << 4);
}
void DCLow()
{
	GPIOA->ODR &= ~(1u << 6);
}
void DCHigh()
{
	GPIOA->ODR |= (1 << 6);
}
void initSPI(void)
{
	uint32_t  drain_count,drain;
	
	RCC->APB2ENR |= (1 << 12);		// turn on SPI1 	
	
	
	// GPIOA bits 5 and 7 are used for SPI1 (Alternative functions 0)
    GPIOA->MODER &= ~( (1u << 14)+(1u << 10)); // select Alternative function
    GPIOA->MODER |= ((1 << 15)+(1 << 11));  // for bits 5,7 (not using MISO)
    GPIOA->AFR[0] &= 0x000fffff;		     // select Alt. Function 0
	
	// Now configure the SPI interface	
	drain = SPI1->SR;				// dummy read of SR to clear MODF	
	// enable SSM, set SSI, enable SPI, PCLK/2, MSB First Master, Clock = 1 when idle
	SPI1->CR1 = (1 << 9)+(1 << 8)+(1 << 6)+(1 << 2) +(1 << 1) + (1 << 0); // Might get away with removing bit 3 here and get 24MHz clock
	SPI1->CR2 = (1 << 10)+(1 << 9)+(1 << 8); 	// configure for 8 bit operation
   
  for (drain_count = 0; drain_count < 32; drain_count++)
	drain = transferSPI8((uint8_t)0x00);
}

uint8_t transferSPI8(uint8_t data)
{
    unsigned Timeout = 1000000;
    uint8_t ReturnValue;
    volatile uint8_t *preg=(volatile uint8_t*)&SPI1->DR;
	
    while (((SPI1->SR & (1 << 7))!=0)&&(Timeout--));
    *preg = data;
    Timeout = 1000000;
    while (((SPI1->SR & (1 << 7))!=0)&&(Timeout--));        
	  ReturnValue = *preg;	
    return ReturnValue;
}

uint16_t transferSPI16(uint16_t data)
{
    unsigned Timeout = 1000000;
    uint32_t ReturnValue;    
	
    while (((SPI1->SR & (1 << 7))!=0)&&(Timeout--));
    SPI1->DR = data;
    Timeout = 1000000;
    while (((SPI1->SR & (1 << 7))!=0)&&(Timeout--));        
	  ReturnValue = SPI1->DR;
	
    return (uint16_t)ReturnValue;
}
void command(uint8_t cmd)
{
	DCLow();
	transferSPI8(cmd);
}

void data(uint8_t data)
{
	DCHigh();
	transferSPI8(data);
}


void openAperture(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    // open up an area for drawing on the display    

	command(0x2A); // Set X limits    	
    data(x1>>8);
    data(x1&0xff);        
    data(x2>>8);
    data(x2&0xff);
    
    command(0x2B);// Set Y limits
    data(y1>>8);
    data(y1&0xff);        
    data(y2>>8);
    data(y2&0xff);    
        
    command(0x2c); // put display in to data write mode
	
}
void fillRectangle(uint16_t x,uint16_t y,uint16_t width, uint16_t height, uint16_t colour)
{
	uint32_t pixelcount = height * width;
	openAperture(x, y, x + width - 1, y + height - 1);
	DCHigh();
	while(pixelcount--) 
	{
		transferSPI16(colour);
	}	
}
void putPixel(uint16_t x, uint16_t y, uint16_t colour)
{
	openAperture(x, y, x + 1, y + 1);	
	DCHigh();
	transferSPI16(colour);
}
void putImage(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint16_t *Image, int hOrientation, int vOrientation)
{
    uint16_t Colour;
	uint32_t offset = 0;
    
    openAperture(x, y, x + width - 1, y + height - 1);
    DCHigh();
	  if (hOrientation == 0)
		{
			if (vOrientation == 0)
			{
				for (y = 0; y < height; y++)
				{
						offset=y*width;
						for (x = 0; x < width; x++)
						{
								Colour = Image[offset+x];
								transferSPI16(Colour);
						}
				}
			}
			else
			{
				for (y = 0; y < height; y++)
				{
						offset=(height-(y+1))*width;
						for (x = 0; x < width; x++)
						{
								Colour = Image[offset+x];
								transferSPI16(Colour);
						}
				}
			}
		}
		else
		{
			if (vOrientation == 0)
			{
				for (y = 0; y < height; y++)
				{
						offset=y*width;
						for (x = 0; x < width; x++)
						{
								Colour = Image[offset+(width-x-1)];
								transferSPI16(Colour);
						}
				}
			}
			else
			{
				for (y = 0; y < height; y++)
				{
						offset=(height-(y+1))*width;
						for (x = 0; x < width; x++)
						{
								Colour = Image[offset+(width-x-1)];
								transferSPI16(Colour);
						}
				}
			}
		}
}
void drawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t Colour)
{
	// Reference : https://en.wikipedia.org/wiki/Bresenham%27s_line_algorithm    
    if ( iabs(y1 - y0) < iabs(x1 - x0) )
    {
        if (x0 > x1)
        {
            drawLineLowSlope(x1, y1, x0, y0, Colour);
        }
        else
        {
            drawLineLowSlope(x0, y0, x1, y1, Colour);
        }
    }
    else
    {
        if (y0 > y1) 
        {
            drawLineHighSlope(x1, y1, x0, y0, Colour);
        }
        else
        {
            drawLineHighSlope(x0, y0, x1, y1, Colour);
        }
        
    }    
}
int iabs(int x) // simple integer version of abs for use by graphics functions        
{
	if (x < 0)
		x = -x;
	return x;
}
void drawRectangle(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t Colour)
{
	drawLine(x,y,x+w,y,Colour);
  drawLine(x,y,x,y+h,Colour);
  drawLine(x+w,y,x+w,y+h,Colour);
  drawLine(x,y+h,x+w,y+h,Colour);
}
void drawCircle(uint16_t x0, uint16_t y0, uint16_t radius, uint16_t Colour)
{
// Reference : https://en.wikipedia.org/wiki/Midpoint_circle_algorithm
    uint16_t x = radius-1;
    uint16_t y = 0;
    int dx = 1;
    int dy = 1;
    int err = dx - (radius << 1);
    if (radius > x0)
        return; // don't draw even parially off-screen circles
    if (radius > y0)
        return; // don't draw even parially off-screen circles
        
    if ((x0+radius) > SCREEN_WIDTH)
        return; // don't draw even parially off-screen circles
    if ((y0+radius) > SCREEN_HEIGHT)
        return; // don't draw even parially off-screen circles    
    while (x >= y)
    {
        putPixel(x0 + x, y0 + y, Colour);
        putPixel(x0 + y, y0 + x, Colour);
        putPixel(x0 - y, y0 + x, Colour);
        putPixel(x0 - x, y0 + y, Colour);
        putPixel(x0 - x, y0 - y, Colour);
        putPixel(x0 - y, y0 - x, Colour);
        putPixel(x0 + y, y0 - x, Colour);
        putPixel(x0 + x, y0 - y, Colour);

        if (err <= 0)
        {
            y++;
            err += dy;
            dy += 2;
        }
        
        if (err > 0)
        {
            x--;
            dx += 2;
            err += dx - (radius << 1);
        }
    }
}
void fillCircle(uint16_t x0, uint16_t y0, uint16_t radius, uint16_t Colour)
{
	// Reference : https://en.wikipedia.org/wiki/Midpoint_circle_algorithm
	// Similar to drawCircle but fills the circle with lines instead
    uint16_t x = radius-1;
    uint16_t y = 0;
    int dx = 1;
    int dy = 1;
    int err = dx - (radius << 1);

    if (radius > x0)
        return; // don't draw even parially off-screen circles
    if (radius > y0)
        return; // don't draw even parially off-screen circles
        
    if ((x0+radius) > SCREEN_WIDTH)
        return; // don't draw even parially off-screen circles
    if ((y0+radius) > SCREEN_HEIGHT)
        return; // don't draw even parially off-screen circles        
    while (x >= y)
    {
        drawLine(x0 - x, y0 + y,x0 + x, y0 + y, Colour);        
        drawLine(x0 - y, y0 + x,x0 + y, y0 + x, Colour);        
        drawLine(x0 - x, y0 - y,x0 + x, y0 - y, Colour);        
        drawLine(x0 - y, y0 - x,x0 + y, y0 - x, Colour);        

        if (err <= 0)
        {
            y++;
            err += dy;
            dy += 2;
        }
        
        if (err > 0)
        {
            x--;
            dx += 2;
            err += dx - (radius << 1);
        }
    }
}
void printText(const char *Text,uint16_t x, uint16_t y, uint16_t ForeColour, uint16_t BackColour)
{
	// This function draws each character individually.  It uses an array called TextBox as a temporary storage
    // location to hold the dots for the character in question.  It constructs the image of the character and then
    // calls on putImage to place it on the screen
    uint8_t Index = 0;
    uint8_t Row, Col;
    const uint8_t *CharacterCode = 0;    
    uint16_t TextBox[FONT_WIDTH * FONT_HEIGHT];
	uint16_t len;
	len=(uint16_t) mystrlen(Text);
    for (Index = 0; Index < len; Index++)
    {
        CharacterCode = &Font5x7[FONT_WIDTH * (Text[Index] - 32)];
        Col = 0;
        while (Col < FONT_WIDTH)
        {
            Row = 0;
            while (Row < FONT_HEIGHT)
            {
                if (CharacterCode[Col] & (1 << Row))
                {
                    TextBox[(Row * FONT_WIDTH) + Col] = ForeColour;
                }
                else
                {
                    TextBox[(Row * FONT_WIDTH) + Col] = BackColour;
                }
                Row++;
            }
            Col++;
        }
        putImage(x, y, FONT_WIDTH, FONT_HEIGHT, (uint16_t *)TextBox,0,0);
        x = x + FONT_WIDTH + 2;
    }
}
void printTextX2(const char *Text, uint16_t x, uint16_t y, uint16_t ForeColour, uint16_t BackColour)
{
	#define Scale 2
	// This function draws each character individually scaled up by a factor of 2.  It uses an array called TextBox as a temporary storage
    // location to hold the dots for the character in question.  It constructs the image of the character and then
    // calls on putImage to place it on the screen
    uint8_t Index = 0;
    uint8_t Row, Col;
    const uint8_t *CharacterCode = 0;    
	uint16_t len;
	len=(uint16_t)mystrlen(Text);
    uint16_t TextBox[FONT_WIDTH * FONT_HEIGHT*Scale * Scale];

    for (Index = 0; Index < len; Index++)
    {
        CharacterCode = &Font5x7[FONT_WIDTH * (Text[Index] - 32)];
        Col = 0;
        while (Col < FONT_WIDTH)
        {
            Row = 0;
            while (Row < FONT_HEIGHT)
            {
                if (CharacterCode[Col] & (1 << Row))
                {
                    TextBox[((Row*Scale) * FONT_WIDTH*Scale) + (Col*Scale)] = ForeColour;
					TextBox[((Row*Scale) * FONT_WIDTH*Scale) + (Col*Scale)+1] = ForeColour;
					TextBox[(((Row*Scale)+1) * FONT_WIDTH*Scale) + (Col*Scale)] = ForeColour;
					TextBox[(((Row*Scale)+1) * FONT_WIDTH*Scale) + (Col*Scale)+1] = ForeColour;
                }
                else
                {
                    TextBox[((Row*Scale) * FONT_WIDTH*Scale) + (Col*Scale)] = BackColour;
					TextBox[((Row*Scale) * FONT_WIDTH*Scale) + (Col*Scale)+1] = BackColour;
					TextBox[(((Row*Scale)+1) * FONT_WIDTH*Scale) + (Col*Scale)] = BackColour;
					TextBox[(((Row*Scale)+1) * FONT_WIDTH*Scale) + (Col*Scale)+1] = BackColour;
                }
                Row++;
            }
            Col++;
        }
        putImage(x, y, FONT_WIDTH*Scale, FONT_HEIGHT*Scale, (uint16_t *)TextBox,0,0);
        x = x + FONT_WIDTH*Scale + 2;
    }
}
void printNumber(uint16_t Number, uint16_t x, uint16_t y, uint16_t ForeColour, uint16_t BackColour)
{
	// This function converts the supplied number into a character string and then calls on puText to
    // write it to the display
    char Buffer[6]; // Maximum value = 65535
    Buffer[5]=0;
    Buffer[4] = Number % 10 + '0';
    Number = Number / 10;
    Buffer[3] = Number % 10 + '0';
    Number = Number / 10;
    Buffer[2] = Number % 10 + '0';
    Number = Number / 10;
    Buffer[1] = Number % 10 + '0';
    Number = Number / 10;
    Buffer[0] = Number % 10 + '0';
    printText(Buffer, x, y, ForeColour, BackColour);
}
void printNumberX2(uint16_t Number, uint16_t x, uint16_t y, uint16_t ForeColour, uint16_t BackColour)
{
     // This function converts the supplied number into a character string and then calls on puText to
    // write it to the display
    char Buffer[6]; // Maximum value = 65535
    Buffer[5]=0;
    Buffer[4] = Number % 10 + '0';
    Number = Number / 10;
    Buffer[3] = Number % 10 + '0';
    Number = Number / 10;
    Buffer[2] = Number % 10 + '0';
    Number = Number / 10;
    Buffer[1] = Number % 10 + '0';
    Number = Number / 10;
    Buffer[0] = Number % 10 + '0';
    printTextX2(Buffer, x, y, ForeColour, BackColour);	
}
uint16_t RGBToWord(uint16_t R, uint16_t G, uint16_t B)
{
	uint16_t rvalue = 0;
    rvalue += G >> 5;
    rvalue += (G & (7)) << 13;
    rvalue += (R >> 3) << 8;
    rvalue += (B >> 3) << 3;
    return rvalue;
}
void drawLineLowSlope(uint16_t x0, uint16_t y0, uint16_t x1,uint16_t y1, uint16_t Colour)
{
   // Reference : https://en.wikipedia.org/wiki/Bresenham%27s_line_algorithm    
  int dx = x1 - x0;
  int dy = y1 - y0;
  int yi = 1;
  if (dy < 0)
  {
    yi = -1;
    dy = -dy;
  }
  int D = 2*dy - dx;
  
  int y = y0;

  for (int x=x0; x <= x1;x++)
  {
    putPixel((uint16_t)x,(uint16_t)y,Colour);    
    if (D > 0)
    {
       y = y + yi;
       D = D - 2*dx;
    }
    D = D + 2*dy;
    
  }
}
void drawLineHighSlope(uint16_t x0, uint16_t y0, uint16_t x1,uint16_t y1, uint16_t Colour)
{
  // Reference : https://en.wikipedia.org/wiki/Bresenham%27s_line_algorithm
  int dx = x1 - x0;
  int dy = y1 - y0;
  int xi = 1;
  if (dx < 0)
  {
    xi = -1;
    dx = -dx;
  }  
  int D = 2*dx - dy;
  int x = x0;

  for (int y=y0; y <= y1; y++)
  {
    putPixel((uint16_t)x,(uint16_t)y,Colour);
    if (D > 0)
    {
       x = x + xi;
       D = D - 2*dy;
    }
    D = D + 2*dx;
  }
}
void clear()
{
	fillRectangle(0,0,SCREEN_WIDTH, SCREEN_HEIGHT, 0x0000);  // black out the screen
}
uint32_t mystrlen(const char *s)
{
	uint32_t len=0;
	while(*s)
	{
		s++;
		len++;
	}
	return len;
}


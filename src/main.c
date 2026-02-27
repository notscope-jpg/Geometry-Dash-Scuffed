#include <stm32f031x6.h>
#include "display.h"
#include "sprite_map.h"

// Sizing data
#define MAIN_CHARACTER_SPRITE_SIZE_X 16
#define MAIN_CHARACTER_SPRITE_SIZE_Y 16
#define FLOOR_LEVEL_Y 17

// Physics settings
#define JUMP_POWER 0.31f
#define GRAVITY 0.0012
#define JUMP_PAD_JUMP_POWER 0.35f

// General game data
#define OBSTACLE_SIZE 16
#define SCROLL_SPEED 2.5f

// Exit portal
#define PORTAL_WIDTH 20
#define PORTAL_HEIGHT 112

// Death particle presets
#define MAX_PARTICLES 128
#define SCATTER_FRAMES 40

void initClock(void);
void initSysTick(void);
void SysTick_Handler(void);
void delay(volatile uint32_t dly);
void setupIO();
int isInside(uint16_t x1, uint16_t y1, uint16_t w, uint16_t h, uint16_t px, uint16_t py);
void enablePullUp(GPIO_TypeDef *Port, uint32_t BitNumber);
void pinMode(GPIO_TypeDef *Port, uint32_t BitNumber, uint32_t Mode);

volatile uint32_t milliseconds;

// ============================================
// LEVEL SYSTEM
// Levels stored in separate .h files
// 0 = empty, 1 = kill triangle, 2 = platform block, 3 = jump pad
// ============================================
#define LEVEL_ROWS 4
#define NUM_LEVELS 2

#include "levels/level_0.h"
#include "levels/level_1.h"

// Level table: each entry has row pointers and length
typedef struct {
	const uint8_t *rows[LEVEL_ROWS]; // pointer to each row
	int length;                       // number of columns
} LevelInfo;

const LevelInfo levels[NUM_LEVELS] = {
	{ { level_0_data[0], level_0_data[1], level_0_data[2], level_0_data[3] }, LEVEL_0_LENGTH },
	{ { level_1_data[0], level_1_data[1], level_1_data[2], level_1_data[3] }, LEVEL_1_LENGTH },
};

// Runtime level state
int currentLevel = 0;
int levelLength;
const uint8_t *levelRows[LEVEL_ROWS];

void loadLevel(int lvl)
{
	if (lvl >= NUM_LEVELS) lvl = 0; // wrap around
	currentLevel = lvl;
	levelLength = levels[lvl].length;
	for (int i = 0; i < LEVEL_ROWS; i++)
		levelRows[i] = levels[lvl].rows[i];
}

// Compute a rotated version of a square sprite directly from the original
// rot: 0=0°, 1=90°CW, 2=180°, 3=270°CW
void computeRotatedSprite(const uint16_t *src, uint16_t *dst, int size, int rot)
{
	for (int row = 0; row < size; row++)
	{
		for (int col = 0; col < size; col++)
		{
			int srcRow, srcCol;
			switch (rot & 3)
			{
				case 0: srcRow = row; srcCol = col; break;
				case 1: srcRow = size-1-col; srcCol = row; break;
				case 2: srcRow = size-1-row; srcCol = size-1-col; break;
				default: srcRow = col; srcCol = size-1-row; break;
			}
			dst[row * size + col] = src[srcRow * size + srcCol];
		}
	}
}

uint16_t currentSprite[MAIN_CHARACTER_SPRITE_SIZE_X * MAIN_CHARACTER_SPRITE_SIZE_Y];
int rotation = 0;

// Particle system for death explosion
typedef struct {
	int16_t x, y;       // current position (fixed point: *256)
	int16_t vx, vy;     // velocity
	uint16_t color;
} Particle;

Particle particles[MAX_PARTICLES];
int numParticles = 0;

// Simple pseudo-random number generator
static uint32_t rngState = 12345;
uint32_t quickRand(void)
{
	rngState ^= rngState << 13;
	rngState ^= rngState >> 17;
	rngState ^= rngState << 5;
	return rngState;
}

// Collect non-zero pixels from the current sprite and give them random velocities
void scatterSprite(uint16_t spX, uint16_t spY)
{
	numParticles = 0;
	rngState = milliseconds; // seed from time for variety
	for (int row = 0; row < MAIN_CHARACTER_SPRITE_SIZE_Y && numParticles < MAX_PARTICLES; row++)
	{
		for (int col = 0; col < MAIN_CHARACTER_SPRITE_SIZE_X && numParticles < MAX_PARTICLES; col++)
		{
			uint16_t c = currentSprite[row * MAIN_CHARACTER_SPRITE_SIZE_X + col];
			if (c != 0)
			{
				Particle *p = &particles[numParticles++];
				p->x = (int16_t)((spX + col) << 8);
				p->y = (int16_t)((spY + row) << 8);
				p->vx = (int16_t)((quickRand() % 512) - 256); // -256..255
				p->vy = (int16_t)(-(int16_t)(quickRand() % 384) - 64); // mostly upward
				p->color = c;
			}
		}
	}
}

void animateScatter(void)
{
	for (int frame = 0; frame < SCATTER_FRAMES; frame++)
	{
		for (int i = 0; i < numParticles; i++)
		{
			Particle *p = &particles[i];
			// Erase old position
			int16_t sx = p->x >> 8;
			int16_t sy = p->y >> 8;
			if (sx >= 0 && sx < 160 && sy >= 0 && sy < 128)
				putPixel((uint16_t)sx, (uint16_t)sy, 0);
			// Update position
			p->x += p->vx;
			p->y += p->vy;
			p->vy += 12; // gravity on particles
			// Draw new position
			sx = p->x >> 8;
			sy = p->y >> 8;
			if (sx >= 0 && sx < 160 && sy >= 0 && sy < (128 - FLOOR_LEVEL_Y))
				putPixel((uint16_t)sx, (uint16_t)sy, p->color);
		}
		delay(20);
	}
}

void drawMenu(void)
{
	// Black background
	fillRectangle(0, 0, 160, 128, 0);
	// Ground
	fillRectangle(0, (128 - FLOOR_LEVEL_Y), 160, FLOOR_LEVEL_Y, 5466766u & 0xFFFF);
	// Title
	printTextX2("GEOMETRY", 28, 14, RGBToWord(0x00, 0xff, 0x00), 0);
	printTextX2("DASH", 52, 36, RGBToWord(0x00, 0xcc, 0xff), 0);
	printText("SCUFFED", 52, 56, RGBToWord(0xff, 0x00, 0x00), 0);
	// Level number
	printText("LEVEL ", 45, 68, RGBToWord(0xff, 0xff, 0x00), 0);
	printNumber(currentLevel + 1, 88, 68, RGBToWord(0xff, 0xff, 0x00), 0);
	// Player character on the ground
	putImage(72, (128 - FLOOR_LEVEL_Y - MAIN_CHARACTER_SPRITE_SIZE_Y), MAIN_CHARACTER_SPRITE_SIZE_X, MAIN_CHARACTER_SPRITE_SIZE_Y, mainChar, 0, 0);
	// Prompt
	printText("PRESS ANY BUTTON", 20, 90, RGBToWord(0xff, 0xff, 0xff), 0);
	// Decorative triangles
	putImage(20, (128 - FLOOR_LEVEL_Y - OBSTACLE_SIZE), OBSTACLE_SIZE, OBSTACLE_SIZE, triangle1, 0, 0);
	putImage(124, (128 - FLOOR_LEVEL_Y - OBSTACLE_SIZE), OBSTACLE_SIZE, OBSTACLE_SIZE, triangle1, 0, 0);
}

int main()
{
	int hinverted = 0;
	int vinverted = 0;
	int hmoved = 0;
	int vmoved = 0;

	// Our main booleans that handle game logic
	int isInAir = 0;
	int isJumping = 0;
	int paused = 0;
	int dead = 0;
	int won = 0;
	int inMenu = 1;
	int menuWaitRelease = 1; // must release all buttons before menu accepts input

	// Subsidiary values
	int pauseButtonLast = 1; // track previous state for edge detection
	uint32_t pauseDebounce = 0; // debounce timer

	uint16_t x = 60;
	int groundY = 128 - FLOOR_LEVEL_Y - MAIN_CHARACTER_SPRITE_SIZE_Y;
	double jumpHeight = 0.0;
	uint16_t drawY = (uint16_t)groundY;
	uint16_t oldDrawY = drawY;
	uint16_t deaths = 0;
	initClock();
	initSysTick();
	setupIO();
	delay(100); // let buttons settle after flash/reset
	//putImage(20,80,12,16,dg1,0,0);
	loadLevel(0);
	drawMenu();
	computeRotatedSprite(mainChar, currentSprite, MAIN_CHARACTER_SPRITE_SIZE_X, rotation);

	uint32_t lastTime = milliseconds;
	double currentVelocity = 0;
	float scrollOffset = -76.0f; // start negative so level column 0 begins at player's right edge

	// Our main render loop
	while(1)
	{
		hmoved = vmoved = 0;
		hinverted = vinverted = 0;

	    uint32_t now = milliseconds;
   		uint32_t deltaTime = now - lastTime;
    	lastTime = now;

		// Menu state: wait for all buttons released, then any press to start
		if (inMenu)
		{
			int anyBtn = (((GPIOB->IDR) & (1 << 4)) == 0 ||
			              ((GPIOB->IDR) & (1 << 5)) == 0 ||
			              ((GPIOA->IDR) & (1 << 11)) == 0 ||
			              ((GPIOA->IDR) & (1 << 8)) == 0 ||
			              ((GPIOA->IDR >> 12) & 1) == 0);
			if (menuWaitRelease)
			{
				if (!anyBtn) menuWaitRelease = 0;
				delay(10);
				continue;
			}
			if (anyBtn)
			{
				inMenu = 0;
				loadLevel(currentLevel);
				scrollOffset = -76.0f;
				jumpHeight = 0.0;
				currentVelocity = 0.0;
				isInAir = 0;
				dead = 0;
				won = 0;
				deaths = 0;
				rotation = 0;
				computeRotatedSprite(mainChar, currentSprite, MAIN_CHARACTER_SPRITE_SIZE_X, rotation);
				fillRectangle(0, 0, 160, (128 - FLOOR_LEVEL_Y), 0);
				fillRectangle(0, (128 - FLOOR_LEVEL_Y), 160, FLOOR_LEVEL_Y, 5466766u & 0xFFFF);
				oldDrawY = (uint16_t)groundY;
				drawY = (uint16_t)groundY;
				lastTime = milliseconds;
				pauseDebounce = milliseconds;
				pauseButtonLast = 1;
			}
			delay(10);
			continue;
		}

		// Pause handling on PA12 (falling edge with debounce)
		int pauseButtonNow = (GPIOA->IDR >> 12) & 1;

		if (paused)
		{
			// While paused: pause button again → main menu, any other button → unpause
			if (pauseButtonLast == 1 && pauseButtonNow == 0 && (milliseconds - pauseDebounce) > 200)
			{
				// Pause pressed again → go to main menu
				pauseDebounce = milliseconds;
				paused = 0;
				inMenu = 1;
				menuWaitRelease = 1;
				drawMenu();
			}
			else if (((GPIOB->IDR) & (1 << 4)) == 0 ||
			         ((GPIOB->IDR) & (1 << 5)) == 0 ||
			         ((GPIOA->IDR) & (1 << 11)) == 0 ||
			         ((GPIOA->IDR) & (1 << 8)) == 0)
			{
				// Any jump button → unpause
				paused = 0;
				fillRectangle(44, 55, 76, 16, 0);
				lastTime = milliseconds;
			}
			pauseButtonLast = pauseButtonNow;
			delay(10);
			continue;
		}

		if (pauseButtonLast == 1 && pauseButtonNow == 0 && (milliseconds - pauseDebounce) > 200 && dead == 0)
		{
			pauseDebounce = milliseconds;
			paused = 1;
			printTextX2("PAUSED", 44, 55, RGBToWord(0xff, 0xff, 0xff), 0);
			pauseButtonLast = pauseButtonNow;
			delay(10);
			continue;
		}
		pauseButtonLast = pauseButtonNow;

		if (((GPIOB->IDR) & (1 << 4)) == 0 ||
		    ((GPIOB->IDR) & (1 << 5)) == 0 ||
		    ((GPIOA->IDR) & (1 << 11)) == 0 ||
		    ((GPIOA->IDR) & (1 << 8)) == 0)
		{
			isJumping = 1;
		} else {
			isJumping = 0;
		}

		// Jump physics
		if (isJumping == 1 && isInAir == 0)
		{
			isInAir = 1;
			currentVelocity = JUMP_POWER;
			rotation = (rotation + 1) & 3;
			computeRotatedSprite(mainChar, currentSprite, MAIN_CHARACTER_SPRITE_SIZE_X, rotation);
		}

		if (isInAir)
		{
			currentVelocity -= GRAVITY * (double)deltaTime;
			jumpHeight += currentVelocity * (double)deltaTime;

			// Check if landing on a platform block
			int landed = 0;
			{
				int scrollInt = (int)scrollOffset;
				int pixelOffset = ((scrollInt % OBSTACLE_SIZE) + OBSTACLE_SIZE) % OBSTACLE_SIZE;
				int firstTile = (scrollInt - pixelOffset) / OBSTACLE_SIZE;
				for (int row = 0; row < LEVEL_ROWS && !landed; row++)
				{
					int rowY = 128 - FLOOR_LEVEL_Y - (row + 1) * OBSTACLE_SIZE;
					for (int i = 0; i <= 10; i++)
					{
						int tileIdx = firstTile + i;
						if (tileIdx < 0 || tileIdx >= levelLength) continue;
						if (levelRows[row][tileIdx] == 2)
						{
							int screenX = i * OBSTACLE_SIZE - pixelOffset;
							if (screenX < (int)(x + MAIN_CHARACTER_SPRITE_SIZE_X) && screenX + OBSTACLE_SIZE > (int)x)
							{
								int charBottom = groundY - (int)jumpHeight + MAIN_CHARACTER_SPRITE_SIZE_Y;
								if (currentVelocity <= 0 && charBottom >= rowY && charBottom <= rowY + OBSTACLE_SIZE)
								{
									jumpHeight = (double)(groundY - rowY + MAIN_CHARACTER_SPRITE_SIZE_Y);
									currentVelocity = 0.0;
									isInAir = 0;
									landed = 1;
									break;
								}
							}
						}
					}
				}
			}

			if (!landed && jumpHeight <= 0.0)
			{
				jumpHeight = 0.0;
				currentVelocity = 0.0;
				isInAir = 0;
			}
		}
		else
		{
			// Not in air — check if still standing on a platform
			if (jumpHeight > 0.0)
			{
				int onBlock = 0;
				int scrollInt = (int)scrollOffset;
				int pixelOffset = ((scrollInt % OBSTACLE_SIZE) + OBSTACLE_SIZE) % OBSTACLE_SIZE;
				int firstTile = (scrollInt - pixelOffset) / OBSTACLE_SIZE;
				int charBottom = groundY - (int)jumpHeight + MAIN_CHARACTER_SPRITE_SIZE_Y;
				for (int row = 0; row < LEVEL_ROWS && !onBlock; row++)
				{
					int rowY = 128 - FLOOR_LEVEL_Y - (row + 1) * OBSTACLE_SIZE;
					if (charBottom >= rowY && charBottom <= rowY + 2)
					{
						for (int i = 0; i <= 10; i++)
						{
							int tileIdx = firstTile + i;
							if (tileIdx < 0 || tileIdx >= levelLength) continue;
							if (levelRows[row][tileIdx] == 2)
							{
								int screenX = i * OBSTACLE_SIZE - pixelOffset;
								if (screenX < (int)(x + MAIN_CHARACTER_SPRITE_SIZE_X) && screenX + OBSTACLE_SIZE > (int)x)
								{
									onBlock = 1;
									break;
								}
							}
						}
					}
				}
				if (!onBlock)
				{
					isInAir = 1;
					currentVelocity = 0.0;
				}
			}
		}

		drawY = (uint16_t)(groundY - (int)jumpHeight);

		// Scroll the level
		scrollOffset += (float)SCROLL_SPEED ;

		// Only erase character if it moved vertically
		if (drawY != oldDrawY)
		{
			// Erase only the slivers that the old position occupied but the new one doesn't
			if (drawY > oldDrawY)
			{
				// Moved down — erase top strip of old position
				int strip = drawY - oldDrawY;
				if (strip > MAIN_CHARACTER_SPRITE_SIZE_Y) strip = MAIN_CHARACTER_SPRITE_SIZE_Y;
				fillRectangle(x, oldDrawY, MAIN_CHARACTER_SPRITE_SIZE_X, (uint16_t)strip, 0);
			}
			else
			{
				// Moved up — erase bottom strip of old position
				int strip = oldDrawY - drawY;
				if (strip > MAIN_CHARACTER_SPRITE_SIZE_Y) strip = MAIN_CHARACTER_SPRITE_SIZE_Y;
				fillRectangle(x, (uint16_t)(oldDrawY + MAIN_CHARACTER_SPRITE_SIZE_Y - strip), MAIN_CHARACTER_SPRITE_SIZE_X, (uint16_t)strip, 0);
			}
		}

		// Draw visible obstacles from level data
		{
			int scrollInt = (int)scrollOffset;
			int pixelOffset = ((scrollInt % OBSTACLE_SIZE) + OBSTACLE_SIZE) % OBSTACLE_SIZE;
			int firstTile = (scrollInt - pixelOffset) / OBSTACLE_SIZE;
			for (int row = 0; row < LEVEL_ROWS; row++)
			{
				int rowY = 128 - FLOOR_LEVEL_Y - (row + 1) * OBSTACLE_SIZE;
				for (int i = 0; i <= 10; i++)
				{
					int screenX = i * OBSTACLE_SIZE - pixelOffset;
					if (screenX >= 0 && screenX + OBSTACLE_SIZE <= 160)
					{
						int tileIdx = firstTile + i;
						if (tileIdx < 0 || tileIdx >= levelLength) continue;
						if (levelRows[row][tileIdx] == 1)
						{
							putImage((uint16_t)screenX, (uint16_t)rowY, OBSTACLE_SIZE, OBSTACLE_SIZE, triangle1, 0, 0);
							// Spike hitbox — shrink top by 6px so only the actual triangle kills
							int spikeTop = rowY + 6;
							if (screenX < (int)(x + MAIN_CHARACTER_SPRITE_SIZE_X) && screenX + OBSTACLE_SIZE > (int)x &&
								(int)drawY < rowY + OBSTACLE_SIZE && (int)drawY + MAIN_CHARACTER_SPRITE_SIZE_Y > spikeTop)
							{
								dead = 1;
							}
						}
						else if (levelRows[row][tileIdx] == 2)
						{
							putImage((uint16_t)screenX, (uint16_t)rowY, OBSTACLE_SIZE, OBSTACLE_SIZE, block1, 0, 0);
							// Side/embedded collision — kill unless player is standing on top
							if (screenX < (int)(x + MAIN_CHARACTER_SPRITE_SIZE_X) && screenX + OBSTACLE_SIZE > (int)x &&
								(int)drawY < rowY + OBSTACLE_SIZE && (int)drawY + MAIN_CHARACTER_SPRITE_SIZE_Y > rowY)
							{
								int charBottom = (int)drawY + MAIN_CHARACTER_SPRITE_SIZE_Y;
								// If feet are more than 4px into the block, it's a side hit — die
								// If feet are only 0-4px in, the landing physics should handle it
								if (charBottom > rowY + 4)
								{
									dead = 1;
								}
							}
						}
						else if (levelRows[row][tileIdx] == 3)
						{
							putImage((uint16_t)screenX, (uint16_t)rowY, OBSTACLE_SIZE, OBSTACLE_SIZE, jumpPad, 0, 0);
							// Jump pad collision — force a super jump
							if (screenX < (int)(x + MAIN_CHARACTER_SPRITE_SIZE_X) && screenX + OBSTACLE_SIZE > (int)x &&
								(int)drawY < rowY + OBSTACLE_SIZE && (int)drawY + MAIN_CHARACTER_SPRITE_SIZE_Y > rowY)
							{
								isInAir = 1;
								currentVelocity = JUMP_PAD_JUMP_POWER;
								rotation = (rotation + 1) & 3;
								computeRotatedSprite(mainChar, currentSprite, MAIN_CHARACTER_SPRITE_SIZE_X, rotation);
							}
						}
						else
						{
							// Empty tile — only clear if NOT overlapping the character
							if (!(screenX < (int)(x + MAIN_CHARACTER_SPRITE_SIZE_X) &&
							      screenX + OBSTACLE_SIZE > (int)x &&
							      (int)drawY < rowY + OBSTACLE_SIZE &&
							      (int)drawY + MAIN_CHARACTER_SPRITE_SIZE_Y > rowY))
							{
								fillRectangle((uint16_t)screenX, (uint16_t)rowY, OBSTACLE_SIZE, OBSTACLE_SIZE, 0);
							}
						}
					}
				}
				// Clear partial strip at left edge for this row
				if (pixelOffset > 0)
					fillRectangle(0, (uint16_t)rowY, (uint16_t)pixelOffset, OBSTACLE_SIZE, 0);
			}

			if (dead)
			{
				// Erase character and play scatter animation
				fillRectangle(x, drawY, MAIN_CHARACTER_SPRITE_SIZE_X, MAIN_CHARACTER_SPRITE_SIZE_Y, 0);
				scatterSprite(x, drawY);
				animateScatter();
				// Update death counter
				deaths++;
				// Show death text
				printTextX2("YOU DIED", 40, 50, RGBToWord(0xff, 0, 0), 0);
				printTextX2("DUMBASS", 45, 75, RGBToWord(0xff, 0, 0), 0);
				delay(1000);
				scrollOffset = -76.0f;
				jumpHeight = 0.0;
				currentVelocity = 0.0;
				isInAir = 0;
				lastTime = milliseconds;
				fillRectangle(0, 0, 160, (128 - FLOOR_LEVEL_Y), 0);
				fillRectangle(0, (128 - FLOOR_LEVEL_Y), 160, FLOOR_LEVEL_Y, 5466766u & 0xFFFF);
				oldDrawY = (uint16_t)groundY;
				rotation = 0;
				computeRotatedSprite(mainChar, currentSprite, MAIN_CHARACTER_SPRITE_SIZE_X, rotation);
				printNumber(deaths, 2, 2, RGBToWord(0xff, 0xff, 0xff), 0);
				dead = 0;
				continue;
			}

			if (won)
			{
				// Suck-in animation — move player toward portal center
				int portalCenterX = levelLength * OBSTACLE_SIZE - (int)scrollOffset + PORTAL_WIDTH / 2;
				int portalCenterY = (128 - FLOOR_LEVEL_Y - PORTAL_HEIGHT) + PORTAL_HEIGHT / 2;
				int animX = (int)x;
				int animY = (int)drawY;

				// Portal screen coords (constant during animation since scroll stopped)
				int pWorldX = levelLength * OBSTACLE_SIZE;
				int pScreenX = pWorldX - (int)scrollOffset;
				int pSrcCol = 0;
				int pDrawX = pScreenX;
				int pVisW = PORTAL_WIDTH;
				if (pDrawX < 0) { pSrcCol = -pDrawX; pVisW += pDrawX; pDrawX = 0; }
				if (pDrawX + pVisW > 160) { pVisW = 160 - pDrawX; }
				int pY = 128 - FLOOR_LEVEL_Y - PORTAL_HEIGHT;

				for (int frame = 0; frame < 20; frame++)
				{
					// Erase old character position by restoring portal or black per-pixel
					for (int ey = 0; ey < MAIN_CHARACTER_SPRITE_SIZE_Y; ey++)
					{
						for (int ex = 0; ex < MAIN_CHARACTER_SPRITE_SIZE_X; ex++)
						{
							int scrX = animX + ex;
							int scrY = animY + ey;
							if (scrX < 0 || scrX >= 160 || scrY < 0 || scrY >= 128) continue;
							// Check if this pixel is inside the portal
							int relPX = scrX - pDrawX;
							int relPY = scrY - pY;
							if (relPX >= 0 && relPX < pVisW && relPY >= 0 && relPY < PORTAL_HEIGHT)
							{
								uint16_t pc = exitPortal[relPY * PORTAL_WIDTH + pSrcCol + relPX];
								putPixel((uint16_t)scrX, (uint16_t)scrY, pc);
							}
							else
							{
								putPixel((uint16_t)scrX, (uint16_t)scrY, 0);
							}
						}
					}

					animX += (portalCenterX - animX) / 3;
					animY += (portalCenterY - animY) / 3;
					if (animX < 0) animX = 0;
					if (animX > 159) animX = 159;
					if (animY < 0) animY = 0;
					if (animY > 127 - MAIN_CHARACTER_SPRITE_SIZE_Y) animY = 127 - MAIN_CHARACTER_SPRITE_SIZE_Y;

					if (frame < 19)
					{
						putImage((uint16_t)animX, (uint16_t)animY,
						         MAIN_CHARACTER_SPRITE_SIZE_X, MAIN_CHARACTER_SPRITE_SIZE_Y, currentSprite, 0, 0);
					}
					else
					{
						// Last frame: redraw full portal, character vanishes
						for (int py = 0; py < PORTAL_HEIGHT; py++)
						{
							for (int px = 0; px < pVisW; px++)
							{
								uint16_t c = exitPortal[py * PORTAL_WIDTH + pSrcCol + px];
								if (c != 0)
									putPixel((uint16_t)(pDrawX + px), (uint16_t)(pY + py), c);
							}
						}
					}
					delay(40);
				}
				// Redraw full portal with putImage to clean up any character remnants
				putImage((uint16_t)pDrawX, (uint16_t)pY, PORTAL_WIDTH, PORTAL_HEIGHT, exitPortal, 0, 0);
				// Show win text
				printTextX2("YOU WIN!", 36, 55, RGBToWord(0, 0xff, 0), 0);
				delay(2000);
				// Advance to next level and go to menu
				currentLevel++;
				if (currentLevel >= NUM_LEVELS) currentLevel = 0;
				loadLevel(currentLevel);
				scrollOffset = -76.0f;
				jumpHeight = 0.0;
				currentVelocity = 0.0;
				isInAir = 0;
				won = 0;
				inMenu = 1;
				menuWaitRelease = 1;
				drawMenu();
				oldDrawY = (uint16_t)groundY;
				rotation = 0;
				computeRotatedSprite(mainChar, currentSprite, MAIN_CHARACTER_SPRITE_SIZE_X, rotation);
				continue;
			}
		}

		// Draw exit portal if it's on screen (after obstacle clearing, before character)
		{
			int portalWorldX = levelLength * OBSTACLE_SIZE;
			int portalScreenX = portalWorldX - (int)scrollOffset;
			if (portalScreenX < 160 && portalScreenX + PORTAL_WIDTH > 0)
			{
				// Clip portal to visible screen region
				int srcCol = 0;
				int drawX = portalScreenX;
				int visibleW = PORTAL_WIDTH;
				if (drawX < 0) { srcCol = -drawX; visibleW += drawX; drawX = 0; }
				if (drawX + visibleW > 160) { visibleW = 160 - drawX; }
				// Draw visible slice row by row using putPixel (portal sits on floor)
				int portalY = 128 - FLOOR_LEVEL_Y - PORTAL_HEIGHT;
				for (int py = 0; py < PORTAL_HEIGHT; py++)
				{
					for (int px = 0; px < visibleW; px++)
					{
						uint16_t c = exitPortal[py * PORTAL_WIDTH + srcCol + px];
						if (c != 0)
							putPixel((uint16_t)(drawX + px), (uint16_t)(portalY + py), c);
					}
				}
				// Check if full portal is on screen (all 20px visible)
				if (portalScreenX <= 160 - PORTAL_WIDTH)
				{
					won = 1;
				}
			}
		}

		// Draw character on top
		if (drawY != oldDrawY || 1) // always redraw since obstacles scroll behind
		{
			putImage(x, drawY, MAIN_CHARACTER_SPRITE_SIZE_X, MAIN_CHARACTER_SPRITE_SIZE_Y, currentSprite, 0, 0);
			oldDrawY = drawY;
		}
		delay(5);
	}
	return 0;
}
void initSysTick(void)
{
	SysTick->LOAD = 48000;
	SysTick->CTRL = 7;
	SysTick->VAL = 10;
	__asm(" cpsie i "); // enable interrupts
}
void SysTick_Handler(void)
{
	milliseconds++;
}
void initClock(void)
{
// This is potentially a dangerous function as it could
// result in a system with an invalid clock signal - result: a stuck system
        // Set the PLL up
        // First ensure PLL is disabled
        RCC->CR &= ~(1u<<24);
        while( (RCC->CR & (1 <<25))); // wait for PLL ready to be cleared
        
// Warning here: if system clock is greater than 24MHz then wait-state(s) need to be
// inserted into Flash memory interface
				
        FLASH->ACR |= (1 << 0);
        FLASH->ACR &=~((1u << 2) | (1u<<1));
        // Turn on FLASH prefetch buffer
        FLASH->ACR |= (1 << 4);
        // set PLL multiplier to 12 (yielding 48MHz)
        RCC->CFGR &= ~((1u<<21) | (1u<<20) | (1u<<19) | (1u<<18));
        RCC->CFGR |= ((1<<21) | (1<<19) ); 

        // Need to limit ADC clock to below 14MHz so will change ADC prescaler to 4
        RCC->CFGR |= (1<<14);

        // and turn the PLL back on again
        RCC->CR |= (1<<24);        
        // set PLL as system clock source 
        RCC->CFGR |= (1<<1);
}
void delay(volatile uint32_t dly)
{
	uint32_t end_time = dly + milliseconds;
	while(milliseconds != end_time)
		__asm(" wfi "); // sleep
}

void enablePullUp(GPIO_TypeDef *Port, uint32_t BitNumber)
{
	Port->PUPDR = Port->PUPDR &~(3u << BitNumber*2); // clear pull-up resistor bits
	Port->PUPDR = Port->PUPDR | (1u << BitNumber*2); // set pull-up bit
}
void pinMode(GPIO_TypeDef *Port, uint32_t BitNumber, uint32_t Mode)
{
	/*
	*/
	uint32_t mode_value = Port->MODER;
	Mode = Mode << (2 * BitNumber);
	mode_value = mode_value & ~(3u << (BitNumber * 2));
	mode_value = mode_value | Mode;
	Port->MODER = mode_value;
}
int isInside(uint16_t x1, uint16_t y1, uint16_t w, uint16_t h, uint16_t px, uint16_t py)
{
	// checks to see if point px,py is within the rectange defined by x,y,w,h
	uint16_t x2,y2;
	x2 = x1+w;
	y2 = y1+h;
	int rvalue = 0;
	if ( (px >= x1) && (px <= x2))
	{
		// ok, x constraint met
		if ( (py >= y1) && (py <= y2))
			rvalue = 1;
	}
	return rvalue;
}

void setupIO()
{
	RCC->AHBENR |= (1 << 18) | (1 << 17); // enable Ports A and B
	display_begin();
	pinMode(GPIOB,4,0);
	pinMode(GPIOB,5,0);
	pinMode(GPIOA,8,0);
	pinMode(GPIOA,11,0);
	pinMode(GPIOB, 0, 0);
	pinMode(GPIOA, 12, 0);

	enablePullUp(GPIOB,4);
	enablePullUp(GPIOB,5);
	enablePullUp(GPIOA,11);
	enablePullUp(GPIOA,8);
	enablePullUp(GPIOB, 0);
	enablePullUp(GPIOA, 12);
}

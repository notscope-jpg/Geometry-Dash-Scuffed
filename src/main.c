/*

Wire reference:
	Purple: Left Button: PA11
	Yellow: Right Button: PA8
	Blue: Down Button: PB4
	Green: Up Button: PB5
*/

#include <stm32f031x6.h>
#include "display.h"
#include "sprite_map.h"

// Sizing data
#define MAIN_CHARACTER_SPRITE_SIZE_X 16
#define MAIN_CHARACTER_SPRITE_SIZE_Y 16
#define ROT_SIZE 20
#define ROT_PAD ((ROT_SIZE - MAIN_CHARACTER_SPRITE_SIZE_X) / 2)
#define FLOOR_LEVEL_Y 17

// Physics settings
#define JUMP_POWER 0.26f
#define GRAVITY 0.001f
#define JUMP_PAD_JUMP_POWER 0.35f

// General game data
#define OBSTACLE_SIZE 16
#define SCROLL_SPEED 2.8f

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

void eputchar(char c);
char egetchar(void);
void eputs(char *String); 

void initSerial()

volatile uint32_t milliseconds;

// ============================================
// LEVEL SYSTEM
// Levels stored in separate .h files
// 0 = empty, 1 = kill triangle, 2 = platform block, 3 = jump pad
// ============================================
#define LEVEL_ROWS 4
#define NUM_LEVELS 3

#include "levels/level_0.h"
#include "levels/level_1.h"
#include "levels/level_2.h"

// Level table: each entry has row pointers and length
typedef struct {
	const uint8_t *rows[LEVEL_ROWS]; // pointer to each row
	int length;                       // number of columns
} LevelInfo;

const LevelInfo levels[NUM_LEVELS] = {
	{ { level_0_data[0], level_0_data[1], level_0_data[2], level_0_data[3] }, LEVEL_0_LENGTH },
	{ { level_1_data[0], level_1_data[1], level_1_data[2], level_1_data[3] }, LEVEL_1_LENGTH },
	{ { level_2_data[0], level_2_data[1], level_2_data[2], level_2_data[3] }, LEVEL_2_LENGTH },
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

// Sin lookup table for 0-90 degrees, scaled by 256
static const int16_t sinLUT[91] = {
	0,4,9,13,18,22,27,31,36,40,44,49,53,58,62,66,71,75,79,83,
	88,92,96,100,104,108,112,116,120,124,128,132,135,139,143,147,
	150,154,158,161,164,168,171,175,178,181,184,187,190,193,196,199,
	201,204,207,210,212,215,217,219,222,224,226,228,230,232,234,236,
	237,239,241,242,243,245,246,247,248,249,250,252,252,253,254,254,
	255,255,255,256,256,256,256
};

static int fixSin(int deg)
{
	deg = ((deg % 360) + 360) % 360;
	if (deg <= 90) return sinLUT[deg];
	if (deg <= 180) return sinLUT[180 - deg];
	if (deg <= 270) return -sinLUT[deg - 180];
	return -sinLUT[360 - deg];
}
static int fixCos(int deg) { return fixSin(deg + 90); }

// Arbitrary-angle sprite rotation using nearest-neighbor sampling
// Uses doubled coordinates to correctly center even-sized sprites
void computeSmoothRotatedSprite(const uint16_t *src, uint16_t *dst, int srcSize, int angleDeg)
{
	int cs = fixCos(angleDeg);
	int sn = fixSin(angleDeg);
	int srcS1 = srcSize - 1;
	int dstS1 = ROT_SIZE - 1;
	for (int row = 0; row < ROT_SIZE; row++)
	{
		int dy2 = 2 * row - dstS1;
		for (int col = 0; col < ROT_SIZE; col++)
		{
			int dx2 = 2 * col - dstS1;
			int rx = dx2 * cs + dy2 * sn;
			int ry = -dx2 * sn + dy2 * cs;
			int srcCol = (rx + srcS1 * 256 + 256) / 512;
			int srcRow = (ry + srcS1 * 256 + 256) / 512;
			if (srcCol >= 0 && srcCol < srcSize && srcRow >= 0 && srcRow < srcSize)
				dst[row * ROT_SIZE + col] = src[srcRow * srcSize + srcCol];
			else
				dst[row * ROT_SIZE + col] = 0;
		}
	}
}

uint16_t currentSprite[ROT_SIZE * ROT_SIZE];

// Draw a sprite buffer row-by-row, trimming zero-valued pixels at left/right edges.
// This avoids drawing the black rotation corners while keeping interior black pixels.
void drawSpriteNoCorners(uint16_t px, uint16_t py, int size, const uint16_t *sprite)
{
	for (int r = 0; r < size; r++)
	{
		const uint16_t *row = &sprite[r * size];
		int left = -1, right = -1;
		for (int c = 0; c < size; c++)
		{
			if (row[c] != 0)
			{
				if (left == -1) left = c;
				right = c;
			}
		}
		if (left >= 0)
		{
			int w = right - left + 1;
			putImage((uint16_t)(px + left), (uint16_t)(py + r), (uint16_t)w, 1, &row[left], 0, 0);
		}
	}
}
int rotation = 0;
int rotAngle = 0;       // current smooth angle in degrees
int targetRotAngle = 0; // target angle to interpolate toward

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
	for (int row = 0; row < ROT_SIZE && numParticles < MAX_PARTICLES; row++)
	{
		for (int col = 0; col < ROT_SIZE && numParticles < MAX_PARTICLES; col++)
		{
			uint16_t c = currentSprite[row * ROT_SIZE + col];
			if (c != 0)
			{
				Particle *p = &particles[numParticles++];
				p->x = (int16_t)((spX - ROT_PAD + col) << 8);
				p->y = (int16_t)((spY - ROT_PAD + row) << 8);
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

// =====================
// Procedural portal system
// =====================
#define PORTAL_A  (PORTAL_WIDTH / 2)
#define PORTAL_B  (PORTAL_HEIGHT / 2)
#define PORTAL_A2 (PORTAL_A * PORTAL_A)
#define PORTAL_B2 (PORTAL_B * PORTAL_B)
#define PORTAL_A2B2 (PORTAL_A2 * PORTAL_B2)
#define PORTAL_T1 (PORTAL_A2B2 / 4)
#define PORTAL_T2 (PORTAL_A2B2 / 2)
#define PORTAL_T3 (PORTAL_A2B2 * 3 / 4)

static const uint16_t portalColors[4] = { 0xE607, 0xE02F, 0xF08F, 0x6005 };

#define MAX_PORTAL_PARTICLES 20
#define PORTAL_PARTICLE_LIFE 25

typedef struct {
	int16_t x, y;     // screen position << 8
	int16_t vx, vy;
	uint8_t life;
	uint16_t color;
} PortalParticle;

PortalParticle portalParts[MAX_PORTAL_PARTICLES];

void drawProceduralPortal(int screenX, int portalY)
{
	int pulse = (int)(milliseconds / 150) & 3;
	for (int py = 0; py < PORTAL_HEIGHT; py++)
	{
		int dy = py - PORTAL_B;
		int dy2a2 = dy * dy * PORTAL_A2;
		if (dy2a2 > PORTAL_A2B2) continue;
		for (int px = 0; px < PORTAL_WIDTH; px++)
		{
			int sx = screenX + px;
			if (sx < 0 || sx >= 160) continue;
			int dx = px - PORTAL_A;
			int val = dx * dx * PORTAL_B2 + dy2a2;
			if (val <= PORTAL_A2B2)
			{
				int inner = PORTAL_A2B2 - val;
				int ci;
				if (inner < PORTAL_T1) ci = 0;
				else if (inner < PORTAL_T2) ci = 1;
				else if (inner < PORTAL_T3) ci = 2;
				else ci = 3;
				putPixel((uint16_t)sx, (uint16_t)(portalY + py), portalColors[(ci + pulse) & 3]);
			}
		}
	}
}

void spawnPortalParticle(int portalScreenX, int portalY)
{
	for (int i = 0; i < MAX_PORTAL_PARTICLES; i++)
	{
		PortalParticle *p = &portalParts[i];
		if (p->life == 0)
		{
			int ry = (int)(quickRand() % PORTAL_HEIGHT);
			int dy = ry - PORTAL_B;
			int absdy = dy < 0 ? -dy : dy;
			int hw = PORTAL_A * (PORTAL_B - absdy) / PORTAL_B;
			if (hw < 1) hw = 1;
			int cx = portalScreenX + PORTAL_A;
			int side = (quickRand() & 1) ? 1 : -1;
			p->x = (int16_t)((cx + side * hw) << 8);
			p->y = (int16_t)((portalY + ry) << 8);
			p->vx = (int16_t)(side * (int16_t)(quickRand() % 128 + 48));
			p->vy = (int16_t)((int16_t)(quickRand() % 128) - 96);
			p->life = (uint8_t)(PORTAL_PARTICLE_LIFE - (quickRand() % 10));
			p->color = portalColors[quickRand() & 3];
			break;
		}
	}
}

void updatePortalParticles(void)
{
	for (int i = 0; i < MAX_PORTAL_PARTICLES; i++)
	{
		PortalParticle *p = &portalParts[i];
		if (p->life == 0) continue;
		int16_t sx = p->x >> 8;
		int16_t sy = p->y >> 8;
		if (sx >= 0 && sx < 160 && sy >= 0 && sy < (128 - FLOOR_LEVEL_Y))
			putPixel((uint16_t)sx, (uint16_t)sy, 0);
		p->x += p->vx;
		p->y += p->vy;
		p->vy += 4;
		p->life--;
		if (p->life > 0) {
			sx = p->x >> 8;
			sy = p->y >> 8;
			if (sx >= 0 && sx < 160 && sy >= 0 && sy < (128 - FLOOR_LEVEL_Y))
				putPixel((uint16_t)sx, (uint16_t)sy, p->color);
		}
	}
}

void resetPortalParticles(void)
{
	for (int i = 0; i < MAX_PORTAL_PARTICLES; i++)
		portalParts[i].life = 0;
}

// =====================
// Character selection system
// =====================
#define NUM_CHARACTERS 4
const uint16_t *characterTable[NUM_CHARACTERS] = { mainChar, characterOne, characterTwo, characterThree };
const char *characterNames[NUM_CHARACTERS] = { "CLASSIC", "BLUE", "IDIOT", "CHECKER" };
int selectedChar = 0;
const uint16_t *selectedCharPtr = 0;

void drawArrowLeft(int x, int y, uint16_t color)
{
	for (int i = 0; i < 5; i++)
		drawLine((uint16_t)(x + i), (uint16_t)(y + i), (uint16_t)(x + i), (uint16_t)(y + 8 - i), color);
}

void drawArrowRight(int x, int y, uint16_t color)
{
	for (int i = 0; i < 5; i++)
		drawLine((uint16_t)(x + 4 - i), (uint16_t)(y + i), (uint16_t)(x + 4 - i), (uint16_t)(y + 8 - i), color);
}

void drawArrowDown(int x, int y, uint16_t color)
{
	for (int i = 0; i < 5; i++)
		drawLine((uint16_t)(x + i), (uint16_t)(y + 4 - i), (uint16_t)(x + 8 - i), (uint16_t)(y + 4 - i), color);
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
	putImage(72, (128 - FLOOR_LEVEL_Y - MAIN_CHARACTER_SPRITE_SIZE_Y), MAIN_CHARACTER_SPRITE_SIZE_X, MAIN_CHARACTER_SPRITE_SIZE_Y, characterTable[selectedChar], 0, 0);
	// Decorative triangles
	putImage(20, (128 - FLOOR_LEVEL_Y - OBSTACLE_SIZE), OBSTACLE_SIZE, OBSTACLE_SIZE, triangle1, 0, 0);
	putImage(124, (128 - FLOOR_LEVEL_Y - OBSTACLE_SIZE), OBSTACLE_SIZE, OBSTACLE_SIZE, triangle1, 0, 0);
	// Control hints
	drawArrowDown(40, 88, RGBToWord(0xff, 0xff, 0x00));
	printText("START", 52, 86, RGBToWord(0xff, 0xff, 0xff), 0);
	drawArrowLeft(40, 100, RGBToWord(0x00, 0xcc, 0xff));
	printText("CHARACTER", 52, 98, RGBToWord(0xff, 0xff, 0xff), 0);
}

void drawCharSelect(void)
{
	fillRectangle(0, 0, 160, 128, 0);
	fillRectangle(0, (128 - FLOOR_LEVEL_Y), 160, FLOOR_LEVEL_Y, 5466766u & 0xFFFF);
	printTextX2("SELECT", 36, 6, RGBToWord(0x00, 0xcc, 0xff), 0);
	// Character preview
	putImage(72, 40, MAIN_CHARACTER_SPRITE_SIZE_X, MAIN_CHARACTER_SPRITE_SIZE_Y, characterTable[selectedChar], 0, 0);
	// Character name
	fillRectangle(20, 62, 120, 10, 0);
	printText(characterNames[selectedChar], 52, 62, RGBToWord(0xff, 0xff, 0x00), 0);
	// Left/right arrows
	drawArrowLeft(50, 44, RGBToWord(0xff, 0xff, 0xff));
	drawArrowRight(100, 44, RGBToWord(0xff, 0xff, 0xff));
	// Number
	printNumber(selectedChar + 1, 74, 78, RGBToWord(0xff, 0xff, 0xff), 0);
	printNumber(NUM_CHARACTERS, 90, 78, RGBToWord(0xff, 0xff, 0xff), 0);
	// Exit hint
	printText("DOWN TO GO BACK", 28, 98, RGBToWord(0xff, 0xff, 0xff), 0);
}

void turnRedLEDOn()
{
	GPIOB->ODR |= (1 << 0);
}

void turnRedLEDOff()
{
	GPIOB->ODR &= ~(1 << 0);
}

void turnGreenLEDOn()
{
	GPIOA->ODR |= (1 << 2);
}

void turnGreenLEDOff()
{
	GPIOA->ODR &= ~(1 << 2);
}

void flashRedThreeTimes()
{
	for (int i = 0; i < 3; i++)
	{
		turnRedLEDOn();
		delay(175);
		turnRedLEDOff();
		delay(175);
	}
}

int main()
{
	// Our main booleans that handle game logic
	int isInAir = 0;
	int isJumping = 0;
	int paused = 0;
	int dead = 0;
	int won = 0;
	int inMenu = 1;
	int menuWaitRelease = 1; // must release all buttons before menu accepts input
	int gameWaitRelease = 0;

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
	selectedCharPtr = characterTable[selectedChar];
	rotAngle = 0;
	targetRotAngle = 0;
	computeSmoothRotatedSprite(selectedCharPtr, currentSprite, MAIN_CHARACTER_SPRITE_SIZE_X, 0);

	uint32_t lastTime = milliseconds;
	double currentVelocity = 0;
	float scrollOffset = -76.0f; // start negative so level column 0 begins at player's right edge

	// Our main render loop
	while(1)
	{
	    uint32_t now = milliseconds;
   		uint32_t deltaTime = now - lastTime;
    	lastTime = now;

		// Menu state
		if (inMenu)
		{
			turnGreenLEDOff();
			int btnDown  = (((GPIOB->IDR) & (1 << 5)) == 0);
			int btnUp    = (((GPIOB->IDR) & (1 << 4)) == 0);
			int btnLeft  = (((GPIOA->IDR) & (1 << 8)) == 0);
			int btnRight = (((GPIOA->IDR) & (1 << 11)) == 0);
			int anyBtn   = (btnDown || btnUp || btnLeft || btnRight || ((GPIOA->IDR >> 12) & 1) == 0);
			if (menuWaitRelease)
			{
				if (!anyBtn) menuWaitRelease = 0;
				delay(10);
				continue;
			}
			if (btnLeft)
			{
				// Enter character select
				int inCharSel = 1;
				int csWaitRelease = 1;
				drawCharSelect();
				while (inCharSel)
				{
					int cLeft  = (((GPIOA->IDR) & (1 << 8)) == 0);
					int cRight = (((GPIOA->IDR) & (1 << 11)) == 0);
					int cUp    = (((GPIOB->IDR) & (1 << 4)) == 0);
					int cAny   = (cLeft || cRight || cUp ||
					              ((GPIOB->IDR) & (1 << 4)) == 0 ||
					              ((GPIOA->IDR >> 12) & 1) == 0);
					if (csWaitRelease)
					{
						if (!cAny) csWaitRelease = 0;
						delay(10);
						continue;
					}
					if (cUp)
					{
						// Exit char select, return to menu
						inCharSel = 0;
					}
					else if (cRight)
					{
						selectedChar = (selectedChar + 1) % NUM_CHARACTERS;
						drawCharSelect();
						csWaitRelease = 1;
					}
					else if (cLeft)
					{
						selectedChar = (selectedChar + NUM_CHARACTERS - 1) % NUM_CHARACTERS;
						drawCharSelect();
						csWaitRelease = 1;
					}
					delay(10);
				}
				selectedCharPtr = characterTable[selectedChar];
				menuWaitRelease = 1;
				drawMenu();
			}
			else if (btnDown)
			{
				// Start game
				inMenu = 0;
				gameWaitRelease = 1;
				selectedCharPtr = characterTable[selectedChar];
				loadLevel(currentLevel);
				scrollOffset = -76.0f;
				jumpHeight = 0.0;
				currentVelocity = 0.0;
				isInAir = 0;
				dead = 0;
				won = 0;
				deaths = 0;
				rotation = 0;
				rotAngle = 0;
				targetRotAngle = 0;
				computeSmoothRotatedSprite(selectedCharPtr, currentSprite, MAIN_CHARACTER_SPRITE_SIZE_X, 0);
				resetPortalParticles();
				fillRectangle(0, 0, 160, (128 - FLOOR_LEVEL_Y), 0);
				fillRectangle(0, (128 - FLOOR_LEVEL_Y), 160, FLOOR_LEVEL_Y, 5466766u & 0xFFFF);
				oldDrawY = (uint16_t)groundY;
				drawY = (uint16_t)groundY;

				// Camera pan-in effect
				{
					float panOffset = -40.0f;
					for (int pf = 0; pf < 20; pf++)
					{
						int charScreenX = (int)x + (int)panOffset - ROT_PAD;
						int cy = (int)drawY - ROT_PAD;
						int ch = ROT_SIZE;
						int ft = 128 - FLOOR_LEVEL_Y;
						if (cy + ch > ft) ch = ft - cy;

						// Draw character at new position
						if (ch > 0 && charScreenX >= 0 && charScreenX + ROT_SIZE <= 160)
							putImage((uint16_t)charScreenX, (uint16_t)cy, ROT_SIZE, (uint16_t)ch, currentSprite, 0, 0);

						// Black out everything to the right of the character
						/*int trailX = charScreenX + ROT_SIZE;
						if (trailX < 0) trailX = 0;
						int trailW = 160 - trailX;
						if (trailW > 0 && ch > 0)
							fillRectangle((uint16_t)trailX, (uint16_t)cy, (uint16_t)trailW, (uint16_t)ch, 0);
						*/

						fillRectangle(0, (128 - FLOOR_LEVEL_Y) - MAIN_CHARACTER_SPRITE_SIZE_Y, charScreenX, MAIN_CHARACTER_SPRITE_SIZE_Y, 0);
						panOffset -= panOffset / 4.0f;
						if (panOffset > -1.0f) panOffset = 0.0f;
						delay(30);
					}
				}

				lastTime = milliseconds;
				pauseDebounce = milliseconds;
				pauseButtonLast = 1;
			}
			delay(10);
			continue;
		} else {
			turnGreenLEDOn();
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

		int anyGameBtn = (((GPIOB->IDR) & (1 << 4)) == 0 ||
		    ((GPIOB->IDR) & (1 << 5)) == 0 ||
		    ((GPIOA->IDR) & (1 << 11)) == 0 ||
		    ((GPIOA->IDR) & (1 << 8)) == 0);
		if (gameWaitRelease)
		{
			if (!anyGameBtn) gameWaitRelease = 0;
			isJumping = 0;
		}
		else if (anyGameBtn)
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
			rotation++;
			targetRotAngle = rotation * 90;
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
									rotAngle = targetRotAngle;
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
				rotAngle = targetRotAngle;
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
			int eraseX = (int)x - ROT_PAD;
			int floorTop = 128 - FLOOR_LEVEL_Y;
			if (drawY > oldDrawY)
			{
				int ey = (int)oldDrawY - ROT_PAD;
				int strip = drawY - oldDrawY;
				if (strip > ROT_SIZE) strip = ROT_SIZE;
				if (ey + strip > floorTop) strip = floorTop - ey;
				if (strip > 0)
					fillRectangle((uint16_t)eraseX, (uint16_t)ey, ROT_SIZE, (uint16_t)strip, 0);
			}
			else
			{
				int strip = oldDrawY - drawY;
				if (strip > ROT_SIZE) strip = ROT_SIZE;
				int ey = (int)oldDrawY - ROT_PAD + ROT_SIZE - strip;
				if (ey + strip > floorTop) strip = floorTop - ey;
				if (ey < 0) { strip += ey; ey = 0; }
				if (strip > 0)
					fillRectangle((uint16_t)eraseX, (uint16_t)ey, ROT_SIZE, (uint16_t)strip, 0);
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
								rotation++;
							targetRotAngle = rotation * 90;
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
				turnGreenLEDOff();
				// Erase character and play scatter animation
				{
					int ey = (int)drawY - ROT_PAD;
					int eh = ROT_SIZE;
					int ft = 128 - FLOOR_LEVEL_Y;
					if (ey + eh > ft) eh = ft - ey;
					if (eh > 0)
						fillRectangle((uint16_t)((int)x - ROT_PAD), (uint16_t)ey, ROT_SIZE, (uint16_t)eh, 0);
				}
				computeSmoothRotatedSprite(selectedCharPtr, currentSprite, MAIN_CHARACTER_SPRITE_SIZE_X, rotAngle);
				scatterSprite(x, drawY);
				animateScatter();
				// Update death counter
				deaths++;
				// Show death text
				flashRedThreeTimes();
				printTextX2("YOU DIED", 40, 50, RGBToWord(0xff, 0, 0), 0);
				printTextX2("DUMBASS", 45, 75, RGBToWord(0xff, 0, 0), 0);

				// ---------------------------
				delay(1000);

				// -- Continue the game
				scrollOffset = -76.0f;
				jumpHeight = 0.0;
				currentVelocity = 0.0;
				isInAir = 0;
				lastTime = milliseconds;
				fillRectangle(0, 0, 160, (128 - FLOOR_LEVEL_Y), 0);
				fillRectangle(0, (128 - FLOOR_LEVEL_Y), 160, FLOOR_LEVEL_Y, 5466766u & 0xFFFF);
				oldDrawY = (uint16_t)groundY;
				rotation = 0;
				rotAngle = 0;
				targetRotAngle = 0;
				computeSmoothRotatedSprite(selectedCharPtr, currentSprite, MAIN_CHARACTER_SPRITE_SIZE_X, 0);
				resetPortalParticles();
				printNumber(deaths, 2, 2, RGBToWord(0xff, 0xff, 0xff), 0);
				dead = 0;
				continue;
			}

			if (won)
			{
				// Bezier curve suck-in animation toward portal center
				int portalCenterX = levelLength * OBSTACLE_SIZE - (int)scrollOffset + PORTAL_WIDTH / 2;
				int portalCenterY = (128 - FLOOR_LEVEL_Y - PORTAL_HEIGHT) + PORTAL_HEIGHT / 2;
				int pScreenX = levelLength * OBSTACLE_SIZE - (int)scrollOffset;
				int pY = 128 - FLOOR_LEVEL_Y - PORTAL_HEIGHT;

				// Bezier: P0=start, P1=control (arc upward), P2=portal center

				// start
				int p0x = (int)x;
				int p0y = (int)drawY;

				// control arm
				int p1x = 35;
				int p1y = 25; // arc upward

				if (p1y < 2) p1y = 2;

				// End point
				int p2x = portalCenterX - 12;
				int p2y = portalCenterY;

				int prevAnimX = p0x, prevAnimY = p0y;
				#define WIN_FRAMES 12

				for (int frame = 0; frame <= WIN_FRAMES; frame++)
				{
					// Quadratic bezier: B(t) = (1-t)^2*P0 + 2*(1-t)*t*P1 + t^2*P2
					int t = frame * 256 / WIN_FRAMES;
					int omt = 256 - t;
					int a = (omt * omt) >> 8;
					int b = (2 * omt * t) >> 8;
					int c = (t * t) >> 8;
					int animX = (a * p0x + b * p1x + c * p2x) >> 8;
					int animY = (a * p0y + b * p1y + c * p2y) >> 8;

					if (animX < 0) animX = 0;
					if (animX > 159) animX = 159;
					if (animY < 0) animY = 0;
					if (animY > 127 - MAIN_CHARACTER_SPRITE_SIZE_Y) animY = 127 - MAIN_CHARACTER_SPRITE_SIZE_Y;

					// Erase old position then immediately draw new (no portal redraw in between)
					{
						int ey = prevAnimY - ROT_PAD;
						int eh = ROT_SIZE;
						int ft = 128 - FLOOR_LEVEL_Y;
						if (ey + eh > ft) eh = ft - ey;
						if (ey < 0) { eh += ey; ey = 0; }
						if (eh > 0)
							fillRectangle((uint16_t)(prevAnimX - ROT_PAD), (uint16_t)ey, ROT_SIZE, (uint16_t)eh, 0);
					}

					if (frame < WIN_FRAMES)
					{
						int wy = animY - ROT_PAD;
						int wh = ROT_SIZE;
						int ft = 128 - FLOOR_LEVEL_Y;
						if (wy + wh > ft) wh = ft - wy;
						if (wy < 0) { wh += wy; wy = 0; }
						if (wh > 0)
							putImage((uint16_t)(animX - ROT_PAD), (uint16_t)wy, ROT_SIZE, (uint16_t)wh, currentSprite, 0, 0);
					}

					// Repair portal after character draw (portal behind character is fine)
					drawProceduralPortal(pScreenX, pY);

					prevAnimX = animX;
					prevAnimY = animY;
					delay(5);
				}

				// Break apart at portal center, then disappear
				scatterSprite((uint16_t)prevAnimX, (uint16_t)prevAnimY);
				animateScatter();

				drawProceduralPortal(pScreenX, pY);
				resetPortalParticles();
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
				rotAngle = 0;
				targetRotAngle = 0;
				computeSmoothRotatedSprite(selectedCharPtr, currentSprite, MAIN_CHARACTER_SPRITE_SIZE_X, 0);
				continue;
			}
		}

		// Smooth rotation while airborne
		if (isInAir)
		{
			if (rotAngle != targetRotAngle)
			{
				int diff = targetRotAngle - rotAngle;
				int sign = (diff > 0) ? 1 : -1;
				int step = sign * 6;  // 6 degrees per frame — smooth constant speed
				if (sign * diff < 6) step = diff; // snap if close enough
				rotAngle += step;
			}
			computeSmoothRotatedSprite(selectedCharPtr, currentSprite, MAIN_CHARACTER_SPRITE_SIZE_X, rotAngle);
		}

		// Draw exit portal procedurally + emit particles (before character)
		{
			int portalWorldX = levelLength * OBSTACLE_SIZE;
			int portalScreenX = portalWorldX - (int)scrollOffset;
			int portalY = 128 - FLOOR_LEVEL_Y - PORTAL_HEIGHT;
			if (portalScreenX < 160 && portalScreenX + PORTAL_WIDTH > 0)
			{
				drawProceduralPortal(portalScreenX, portalY);
				spawnPortalParticle(portalScreenX, portalY);
				if (portalScreenX <= 160 - PORTAL_WIDTH)
				{
					won = 1;
				}
			}
			updatePortalParticles();
		}

		// Draw character on top
		if (drawY != oldDrawY || 1) // always redraw since obstacles scroll behind
		{
			int cx = (int)x - ROT_PAD;
			int cy = (int)drawY - ROT_PAD;
			int ch = ROT_SIZE;
			int floorTop = 128 - FLOOR_LEVEL_Y;
			if (cy + ch > floorTop) ch = floorTop - cy;
			if (ch > 0)
				putImage((uint16_t)cx, (uint16_t)cy, ROT_SIZE, (uint16_t)ch, currentSprite, 0, 0);
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

	// -- Buttons set to input
	pinMode(GPIOB,4,0);
	pinMode(GPIOB,5,0);
	pinMode(GPIOA,8,0);
	pinMode(GPIOA,11,0);
	pinMode(GPIOB, 0, 0);
	pinMode(GPIOA, 12, 0);

	// -- LED's set to output
	pinMode(GPIOB, 0, 1);
	pinMode(GPIOA, 2, 1);

	enablePullUp(GPIOB, 0);
	enablePullUp(GPIOA, 2);
	enablePullUp(GPIOB,4);
	enablePullUp(GPIOB,5);
	enablePullUp(GPIOA,11);
	enablePullUp(GPIOA,8);
	enablePullUp(GPIOB, 0);
	enablePullUp(GPIOA, 12);
}

void eputchar(char c)
{
	while( (USART2->ISR & (1 << 6))==0); // wait for ongoing transmission to finish
	USART2->TDR = c;
}
char egetchar()
{
	while( (USART2->ISR & (1 << 5))==0); // wait for character to arrive
	return (char)USART2->RDR;
}

void eputs(char *String)
{
	while(*String) // keep printing until a NULL is found
	{
		eputchar(*String);
		String++;
	}
}

void initClockHSI16()
{
    // Use the HSI16 clock as the system clock - allows operation down to 1.5V
    RCC->CR &= ~(1 << 24);
    RCC->CR |= (1 << 0); // turn on HSI16 (16MHz clock)
    while ((RCC->CR & (1 << 2))==0); // wait for HSI to be ready
    // set HSI16 as system clock source 
    RCC->CFGR |= (1<<0);
}

void initSerial()
{
	const uint32_t CLOCK_SPEED=16000000;
	const uint32_t BAUD_RATE = 9600;
    initClockHSI16();
	uint32_t BaudRateDivisor;
	RCC->IOPENR |= (1 << 0);  // Turn on GPIOA
	RCC->APB1ENR |= (1 << 17); // Turn on USART2
	GPIOA->MODER |= ( (1 << 5) | ((uint32_t)1 << 31));
	GPIOA->MODER &= (uint32_t)~(1 << 4);
	GPIOA->MODER &= (uint32_t)~(1 << 30);
	
	GPIOA->PUPDR |= ( (1 << 4) | (1 << 30));
	GPIOA->PUPDR &= (uint32_t)~(1 << 5);
	GPIOA->PUPDR &= ~((uint32_t)1 << 31);
	GPIOA->AFR[0] &= (uint32_t)( (1 << 11) | (1 << 10) | (1 << 9) | (1 << 8));
	GPIOA->AFR[0] |= (1 << 10);
	GPIOA->AFR[1] &= (uint32_t)( ((uint32_t)1 << 31) | (1 << 30) | (1 << 29) | (1 << 28));
	GPIOA->AFR[1] |= (1 << 30);
	BaudRateDivisor = CLOCK_SPEED/BAUD_RATE;
	RCC->APB1RSTR &= (uint32_t)~(1 << 17);
	USART2->CR1 = 0;
	USART2->CR2 = 0;
	USART2->CR3 = (1 << 12); // disable over-run errors
	USART2->BRR = BaudRateDivisor;
	USART2->CR1 = ( (1 << 2) | (1 << 3) );
	USART2->CR1 |= (1 << 0);
}

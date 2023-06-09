/***************************************************************************

Pengo memory map (preliminary)

0000-7fff ROM
8000-83ff Video RAM
8400-87ff Color RAM
8800-8fff RAM

memory mapped ports:

read:
9000      DSW2
9040      DSW1
9080      IN1
90c0      IN0

*
 * IN0 (all bits are inverted)
 * bit 7 : PUSH player 1
 * bit 6 : CREDIT
 * bit 5 : COIN B
 * bit 4 : COIN A
 * bit 3 : RIGHT player 1
 * bit 2 : LEFT player 1
 * bit 1 : DOWN player 1
 * bit 0 : UP player 1
 *
*
 * IN1 (all bits are inverted)
 * bit 7 : PUSH player 2 (TABLE only)
 * bit 6 : START 2
 * bit 5 : START 1
 * bit 4 : TEST SWITCH
 * bit 3 : RIGHT player 2 (TABLE only)
 * bit 2 : LEFT player 2 (TABLE only)
 * bit 1 : DOWN player 2 (TABLE only)
 * bit 0 : UP player 2 (TABLE only)
 *
*
 * DSW1 (all bits are inverted)
 * bit 7 : DIP SWITCH 8\ difficulty level
 * bit 6 : DIP SWITCH 7/ 00 = Hardest  01 = Hard  10 = Medium  11 = Easy
 * bit 5 : DIP SWITCH 6  RACK TEST
 * bit 4 : DIP SWITCH 5\ nr of lives
 * bit 3 : DIP SWITCH 4/ 00 = 5  01 = 4  10 = 3  11 = 2
 * bit 2 : DIP SWITCH 3  TABLE or UPRIGHT cabinet select (0 = UPRIGHT)
 * bit 1 : DIP SWITCH 2  Attract mode sounds  1 = off  0 = on
 * bit 0 : DIP SWITCH 1  Bonus  0 = 30000  1 = 50000
 *
*
 * DSW2 (all bits are inverted)
 * bit 7 : DIP SWITCH 8  not used
 * bit 6 : DIP SWITCH 7  not used
 * bit 5 : DIP SWITCH 6  not used
 * bit 4 : DIP SWITCH 5  not used
 * bit 3 : DIP SWITCH 4  not used
 * bit 2 : DIP SWITCH 3  not used
 * bit 1 : DIP SWITCH 2  not used
 * bit 0 : DIP SWITCH 1  not used
 *

write:
8ff2-8ffd 6 pairs of two bytes:
          the first byte contains the sprite image number (bits 2-7), Y flip (bit 0),
		  X flip (bit 1); the second byte the color
9005      sound voice 1 waveform (nibble)
9011-9013 sound voice 1 frequency (nibble)
9015      sound voice 1 volume (nibble)
900a      sound voice 2 waveform (nibble)
9016-9018 sound voice 2 frequency (nibble)
901a      sound voice 2 volume (nibble)
900f      sound voice 3 waveform (nibble)
901b-901d sound voice 3 frequency (nibble)
901f      sound voice 3 volume (nibble)
9022-902d Sprite coordinates, x/y pairs for 6 sprites
9040      interrupt enable
9041      sound enable
9042      palette bank selector
9043      flip screen
9044-9045 coin counters
9046      color lookup table bank selector
9047      character/sprite bank selector
9070      watchdog reset

***************************************************************************/

#include "driver.h"
#include "machine.h"
#include "common.h"

int pengo_IN0_r(int address,int offset);
int pengo_IN1_r(int address,int offset);
int pengo_DSW1_r(int address,int offset);

int pengo_videoram_r(int address,int offset);
int pengo_colorram_r(int address,int offset);
void pengo_videoram_w(int address,int offset,int data);
void pengo_colorram_w(int address,int offset,int data);
void pengo_spritecode_w(int address,int offset,int data);
void pengo_spritepos_w(int address,int offset,int data);
void pengo_gfxbank_w(int address,int offset,int data);
int pengo_vh_start(void);
void pengo_vh_stop(void);
void pengo_vh_screenrefresh(void);

void pengo_sound_enable_w(int address,int offset,int data);
void pengo_sound_w(int address,int offset,int data);
void pengo_sh_update(void);



static struct MemoryReadAddress readmem[] =
{
	{ 0x8800, 0x8fff, ram_r },
	{ 0x8000, 0x83ff, pengo_videoram_r },
	{ 0x8400, 0x87ff, pengo_colorram_r },
	{ 0x0000, 0x7fff, rom_r },
	{ 0x90c0, 0x90ff, pengo_IN0_r },
	{ 0x9080, 0x90bf, pengo_IN1_r },
	{ 0x9040, 0x907f, pengo_DSW1_r },
	{ 0x9000, 0x903f, 0 },
	{ -1 }	/* end of table */
};

static struct MemoryWriteAddress writemem[] =
{
	{ 0x8800, 0x8fff, ram_w },					/* note that the sprite codes */
	{ 0x8ff0, 0x8fff, pengo_spritecode_w },	/* overlap standard memory. */
	{ 0x8000, 0x83ff, pengo_videoram_w },
	{ 0x8400, 0x87ff, pengo_colorram_w },
	{ 0x9000, 0x901f, pengo_sound_w },
	{ 0x9020, 0x902f, pengo_spritepos_w },
	{ 0x9040, 0x9040, interrupt_enable_w },
	{ 0x9070, 0x9070, 0 },
	{ 0x9041, 0x9041, pengo_sound_enable_w },
	{ 0x9047, 0x9047, pengo_gfxbank_w },
	{ 0x9042, 0x9046, 0 },
	{ 0x0000, 0x7fff, rom_w },
	{ -1 }	/* end of table */
};



static struct DSW dsw[] =
{
	{ 0, 0x18, "LIVES", { "5", "4", "3", "2" }, 1 },
	{ 0, 0x01, "BONUS", { "30000", "50000" } },
	{ 0, 0xc0, "DIFFICULTY", { "HARDEST", "HARD", "MEDIUM", "EASY" }, 1 },
	{ 0, 0x02, "DEMO SOUNDS", { "ON", "OFF" }, 1 },
	{ -1 }
};



static struct RomModule rom[] =
{
	/* code */
	{ "pengopop.u8",  0x00000, 0x1000 },
	{ "pengopop.u7",  0x01000, 0x1000 },
	{ "pengopop.u15", 0x02000, 0x1000 },
	{ "pengopop.u14", 0x03000, 0x1000 },
	{ "pengopop.u21", 0x04000, 0x1000 },
	{ "pengopop.u20", 0x05000, 0x1000 },
	{ "pengopop.u32", 0x06000, 0x1000 },
	{ "pengopop.u31", 0x07000, 0x1000 },
	/* gfx */
	{ "pengopop.u92", 0x10000, 0x2000 },
	{ "pengopop.105", 0x12000, 0x2000 },
	{ 0 }	/* end of table */
};



static struct GfxLayout charlayout =
{
	8,8,	/* 8*8 characters */
	256,	/* 256 characters */
	2,	/* 2 bits per pixel */
	4,	/* the two bitplanes for 4 pixels are packed into one byte */
	{ 7*8, 6*8, 5*8, 4*8, 3*8, 2*8, 1*8, 0*8 }, /* characters are rotated 90 degrees */
	{ 8*8+0, 8*8+1, 8*8+2, 8*8+3, 0, 1, 2, 3 },	/* bits are packed in groups of four */
	16*8	/* every char takes 16 bytes */
};
static struct GfxLayout spritelayout =
{
	16,16,	/* 16*16 sprites */
	64,	/* 64 sprites */
	2,	/* 2 bits per pixel */
	4,	/* the two bitplanes for 4 pixels are packed into one byte */
	{ 39 * 8, 38 * 8, 37 * 8, 36 * 8, 35 * 8, 34 * 8, 33 * 8, 32 * 8,
			7 * 8, 6 * 8, 5 * 8, 4 * 8, 3 * 8, 2 * 8, 1 * 8, 0 * 8 },
	{ 8*8, 8*8+1, 8*8+2, 8*8+3, 16*8+0, 16*8+1, 16*8+2, 16*8+3,
			24*8+0, 24*8+1, 24*8+2, 24*8+3, 0, 1, 2, 3 },
	64*8	/* every sprite takes 64 bytes */
};



static struct GfxDecodeInfo gfxdecodeinfo[] =
{
	{ 0x10000, &charlayout,   0, 31 },	/* first bank */
	{ 0x11000, &spritelayout, 0, 31 },
	{ 0x12000, &charlayout,   32, 63 },	/* second bank */
	{ 0x13000, &spritelayout, 32, 63 },
	{ -1 } /* end of array */
};



static unsigned char palette[] =
{
/* first bank */
	0x00,0x00,0x00,	/* BLACK */
	0xdb,0xdb,0xdb,	/* WHITE */
	0xff,0x00,0x00,	/* RED */
	0x00,0xff,0x00,	/* GREEN */
	0x24,0x24,0xdb,	/* BLUE */
	0x00,0xff,0xdb,	/* CYAN, */
	0xff,0xff,0x00,	/* YELLOW, */
	0xff,0xb6,0xdb,	/* PINK */
	0xff,0xb6,0x49,	/* ORANGE */
	0xdb,0x24,0x00,	/* DKRED */
	0xff,0xb6,0x00,	/* DKORANGE */
	0xff,0xff,0x49,	/* YELLOW2 */
	0x00,0xdb,0xdb,	/* DKCYAN */
	0xdb,0xdb,0x00,	/* DKYELLOW */
	0x6d,0x6d,0xdb,	/* BLUISH */
	0xdb,0x00,0xdb,	/* PURPLE */

/* second bank */
	0x00,0x00,0x00,
	0xdb,0xdb,0xdb,
	0x00,0x6d,0xdb,
	0x00,0xdb,0xdb,
	0x00,0xff,0xdb,
	0xdb,0x24,0x00,
	0xff,0x00,0x00,
	0xff,0xb6,0x00,
	0xdb,0xdb,0x00,
	0xff,0xff,0x00,
	0xff,0xff,0x49,
	0x00,0xb6,0x00,
	0x24,0xdb,0x00,
	0x00,0xff,0x00,
	0xff,0xb6,0xdb,
	0xdb,0x00,0xdb
};

enum {BLACK,WHITE,RED,GREEN,BLUE,CYAN,YELLOW,PINK,ORANGE,DKRED,DKORANGE,
		YELLOW2,DKCYAN,DKYELLOW,BLUISH,PURPLE};
#define UNUSED BLACK

static unsigned char colortable[] =
{
	/* first bank */
	BLACK,BLACK,BLACK,BLACK,
	BLACK,CYAN,GREEN,WHITE,
	BLACK,CYAN,RED,WHITE,
	BLACK,CYAN,YELLOW,WHITE,	/* dancing penguin #6 */
	BLACK,CYAN,PINK,WHITE,		/* dancing penguin #5 */
	BLACK,CYAN,DKORANGE,WHITE,	/* dancing penguin #4 */
	BLACK,CYAN,YELLOW2,WHITE,	/* dancing penguin #3 */
	BLACK,CYAN,DKCYAN,WHITE,	/* dancing penguin #2 */
	BLACK,CYAN,DKYELLOW,WHITE,	/* dancing penguin #1 */
	BLACK,CYAN,BLUE,WHITE,		/* ice cubes */
	BLACK,GREEN,YELLOW,WHITE,	/* title sno-bee 3 */
	BLACK,GREEN,RED,WHITE,		/* title sno-bee 1 */
	BLACK,GREEN,PINK,WHITE,		/* title sno-bee 4 */
	BLACK,GREEN,CYAN,WHITE,		/* title sno-bee 2 */
	BLACK,RED,GREEN,WHITE,		/* title */
	UNUSED,UNUSED,UNUSED,UNUSED,
	BLACK,ORANGE,GREEN,WHITE,
	BLACK,DKRED,RED,CYAN,
	BLACK,ORANGE,CYAN,DKYELLOW,	/* desk (intermission) */
	BLUE,BLUE,BLUE,BLUE,
	UNUSED,UNUSED,UNUSED,UNUSED,
	UNUSED,UNUSED,UNUSED,UNUSED,
	BLACK,RED,RED,RED,
	BLACK,GREEN,GREEN,GREEN,
	BLACK,YELLOW,YELLOW,YELLOW,
	BLACK,PINK,PINK,PINK,
	BLACK,DKORANGE,DKORANGE,DKORANGE,
	BLACK,YELLOW2,YELLOW2,YELLOW2,
	BLACK,WHITE,WHITE,WHITE,
	BLACK,CYAN,CYAN,CYAN,
	ORANGE,DKRED,DKORANGE,YELLOW2,
	DKCYAN,DKYELLOW,BLUISH,PURPLE,

	/* second bank */
	0x00,0x00,0x00,0x00,
	0x00,0x13,0x17,0x1D,
	0x00,0x1C,0x1F,0x1B,
	0x00,0x1C,0x1E,0x1B,
	0x00,0x1C,0x16,0x1B,
	0x00,0x1C,0x17,0x1B,
	0x00,0x1C,0x13,0x1B,
	0x00,0x1C,0x18,0x1B,
	0x00,0x1C,0x1D,0x1B,
	0x00,0x1C,0x14,0x1B,
	0x00,0x1C,0x19,0x1B,
	0x00,0x1C,0x15,0x1B,
	0x00,0x1C,0x12,0x1B,
	0x00,0x1C,0x1B,0x12,
	0x00,0x18,0x1C,0x12,
	0x00,0x18,0x1F,0x12,
	0x00,0x13,0x12,0x11,
	0x00,0x12,0x1F,0x13,
	0x00,0x1F,0x1E,0x12,
	0x00,0x1E,0x17,0x1F,
	0x00,0x17,0x16,0x1E,
	0x00,0x16,0x15,0x17,
	0x00,0x15,0x00,0x16,
	0x00,0x00,0x1B,0x15,
	0x00,0x1B,0x1C,0x00,
	0x00,0x1C,0x1D,0x1B,
	0x00,0x1D,0x18,0x1C,
	0x00,0x18,0x19,0x1D,
	0x00,0x19,0x1A,0x18,
	0x00,0x1A,0x11,0x19,
	0x00,0x11,0x14,0x1A,
	0x00,0x14,0x13,0x11
};



/* waveforms for the audio hardware */
static unsigned char samples[8*32] =
{
	0x00,0x00,0x00,0x00,0x00,0x77,0x77,0x00,0x00,0x88,0x88,0x88,0x00,0x00,0x00,0x00,
	0x77,0x77,0x77,0x00,0x88,0x88,0x00,0x00,0x00,0x00,0x77,0x77,0x00,0x00,0x88,0x88,

	0xff,0x11,0x22,0x33,0xff,0x55,0x55,0xff,0x66,0xff,0x55,0x55,0xff,0x33,0x22,0x11,
	0xff,0xdd,0xff,0xbb,0xff,0x99,0xff,0x88,0xff,0x88,0xff,0x99,0xff,0xbb,0xff,0xdd,

	0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,
	0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,

	0x00,0x00,0x00,0x88,0x00,0x00,0x77,0x77,0x88,0x88,0x00,0x00,0x00,0x77,0x77,0x77,
	0x88,0x00,0x00,0x88,0x77,0x77,0x00,0x00,0x00,0x00,0x77,0x00,0x88,0x88,0x88,0x00,

	0xff,0x22,0x44,0x55,0x66,0x55,0x44,0x22,0xff,0xcc,0xaa,0x99,0x88,0x99,0xaa,0xcc,
	0xff,0x33,0x55,0x66,0x55,0x33,0xff,0xbb,0x99,0x88,0x99,0xbb,0xff,0x66,0xff,0x88,

	0xff,0x66,0x44,0x11,0x44,0x66,0x22,0xff,0x44,0x77,0x55,0x00,0x22,0x33,0xff,0xaa,
	0x00,0x55,0x11,0xcc,0xdd,0xff,0xaa,0x88,0xbb,0x00,0xdd,0x99,0xbb,0xee,0xbb,0x99,

	0xff,0x00,0x22,0x44,0x66,0x55,0x44,0x44,0x33,0x22,0x00,0xff,0xdd,0xee,0xff,0x00,
	0x00,0x11,0x22,0x33,0x11,0x00,0xee,0xdd,0xcc,0xcc,0xbb,0xaa,0xcc,0xee,0x00,0x11,

	0x22,0x44,0x44,0x22,0xff,0xff,0x00,0x33,0x55,0x66,0x55,0x22,0xee,0xdd,0xdd,0xff,
	0x11,0x11,0x00,0xcc,0x99,0x88,0x99,0xbb,0xee,0xff,0xff,0xcc,0xaa,0xaa,0xcc,0xff
};



const struct MachineDriver pengo_driver =
{
	"pengo",
	rom,
	/* basic machine hardware */
	3072000,	/* 3.072 Mhz */
	60,
	readmem,
	writemem,
	dsw, { 0xb0 },
	0,
	interrupt,
	0,

	/* video hardware */
	224,288,
	gfxdecodeinfo,
	palette,sizeof(palette)/3,
	colortable,sizeof(colortable)/4,
	'0','A',
	0x01,0x18,
	8*11,8*20,0x16,
	0,
	pengo_vh_start,
	pengo_vh_stop,
	pengo_vh_screenrefresh,

	/* sound hardware */
	samples,
	0,
	0,
	0,
	0,
	0,
	pengo_sh_update
};

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "emu.h"
#include "media.h"

extern "C" {
#include "nofrendo/osd.h"
#include "nofrendo/event.h"
};
#include "math.h"
#include <unordered_map>


//#define calc_palettes	//enabling this will overwrite the palette values at boot time

using namespace std;

// https://wiki.nesdev.com/w/index.php/NTSC_video
// NES/SMS have pixel rates of 5.3693175, or 2/3 color clock
// in 3 phase mode each pixel gets 2 DAC values written, 2 color clocks = 3 nes pixels

static uint32_t nes_3_phase[64] = {
    0x2C2C2C00,0x241D2400,0x221D2600,0x1F1F2700,0x1D222600,0x1D242400,0x1D262200,0x1F271F00,
    0x22261D00,0x24241D00,0x26221D00,0x271F1F00,0x261D2200,0x14141400,0x14141400,0x14141400,
    0x38383800,0x2C252C00,0x2A252E00,0x27272F00,0x252A2E00,0x252C2C00,0x252E2A00,0x272F2700,
    0x2A2E2500,0x2C2C2500,0x2E2A2500,0x2F272700,0x2E252A00,0x1F1F1F00,0x15151500,0x15151500,
    0x45454500,0x3A323A00,0x37333C00,0x35353C00,0x33373C00,0x323A3A00,0x333C3700,0x353C3500,
    0x373C3300,0x3A3A3200,0x3C373300,0x3C353500,0x3C333700,0x2B2B2B00,0x16161600,0x16161600,
    0x45454500,0x423B4200,0x403B4400,0x3D3D4500,0x3B404400,0x3B424200,0x3B444000,0x3D453D00,
    0x40443B00,0x42423B00,0x44403B00,0x453D3D00,0x443B4000,0x39393900,0x17171700,0x17171700,
};

//RGB palette from http://drag.wootest.net/misc/palgen.html -> YUV -> QAM on color carrier -> 4 phases sampled
static uint32_t nes_4_phase[64] = {
	0x27272727,0x1B16191E,0x1D151921,0x1C151920,0x1A1A1F20,0x171E231C,0x171E231C,0x171C201A,
	0x17191A18,0x1B1A1819,0x1D1C191A,0x1D1C191A,0x1C1A191C,0x17171717,0x17171717,0x17171717,
	0x3A3A3A3A,0x2C1F1F2C,0x281A1E2D,0x231D282E,0x1E212F2C,0x1A263428,0x192A3423,0x1D2A3023,
	0x23292822,0x28261F20,0x2A281F21,0x2B271F23,0x2D241F28,0x17171717,0x17171717,0x17171717,
	0x50505050,0x3E2D2A3B,0x362B2F3A,0x332E373C,0x33374441,0x313A443C,0x303D4437,0x2F414332,
	0x34413D30,0x393E342E,0x3D3A2B2F,0x41352733,0x43312839,0x21212121,0x17171717,0x17171717,
	0x50505050,0x48414047,0x443F4146,0x43404447,0x44454B4A,0x43464B47,0x42484B45,0x424A4B43,
	0x444A4842,0x46494442,0x48474142,0x4A453F44,0x4A433F46,0x3C3C3C3C,0x17171717,0x17171717
};

//RGB palette from http://drag.wootest.net/misc/palgen.html -> YUV -> QAM on color carrier -> 4 phases sampled
static uint32_t _nes_yuv_4_phase_pal[] = {
	0x26262626,0x1A10151F,0x1B0E1523,0x1B0F1622,0x16161F20,0x101C261A,0x101E2618,0x111B2117,
	0x14161815,0x19181516,0x1C1A1517,0x1C1A1517,0x1B16151A,0x14141414,0x14141414,0x14141414,
	0x3C3C3C3C,0x30181931,0x2B111933,0x21152834,0x161C3630,0x0F253E28,0x0D2C3E1F,0x152D371F,
	0x202B291E,0x2A281A1C,0x2E29191D,0x2F281920,0x3121192A,0x14141414,0x14141414,0x14141414,
	0x55555555,0x47292442,0x3B262C41,0x332A3942,0x2E354E47,0x2C3B4E3F,0x2A414D36,0x29494D2E,
	0x3249422B,0x3C453329,0x463F252C,0x4D371E34,0x4F2F1E3E,0x1F1F1F1F,0x14141414,0x14141414,
	0x55555555,0x4F42404D,0x493F424C,0x4541484C,0x4548524F,0x434A524C,0x424C5248,0x43505245,
	0x46514D42,0x4A4F4742,0x4E4C4144,0x52483E47,0x52453E4B,0x3F3F3F3F,0x14141414,0x14141414,
	
	0x26262626,0x15101A1F,0x150E1B23,0x160F1B22,0x1F161620,0x261C101A,0x261E1018,0x211B1117,
	0x18161415,0x15181916,0x151A1C17,0x151A1C17,0x15161B1A,0x14141414,0x14141414,0x14141414,
	0x3C3C3C3C,0x19183031,0x19112B33,0x28152134,0x361C1630,0x3E250F28,0x3E2C0D1F,0x372D151F,
	0x292B201E,0x1A282A1C,0x19292E1D,0x19282F20,0x1921312A,0x14141414,0x14141414,0x14141414,
	0x55555555,0x24294742,0x2C263B41,0x392A3342,0x4E352E47,0x4E3B2C3F,0x4D412A36,0x4D49292E,
	0x4249322B,0x33453C29,0x253F462C,0x1E374D34,0x1E2F4F3E,0x1F1F1F1F,0x14141414,0x14141414,
	0x55555555,0x40424F4D,0x423F494C,0x4841454C,0x5248454F,0x524A434C,0x524C4248,0x52504345,
	0x4D514642,0x474F4A42,0x414C4E44,0x3E485247,0x3E45524B,0x3F3F3F3F,0x14141414,0x14141414
};

#ifdef calc_palettes
// RGB palette from http://drag.wootest.net/misc/palgen.html
static const uint8_t _nes_r[] = {
	0x46,0x00,0x00,0x02,0x35,0x57,0x5A,0x41,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x9D,0x00,0x05,0x57,0x9F,0xCC,0xCF,0xA4,0x5C,0x0B,0x00,0x00,0x00,0x00,0x00,0x00,
	0xFE,0x1F,0x53,0x98,0xFC,0xFF,0xFF,0xFF,0xC4,0x71,0x28,0x00,0x00,0x2B,0x00,0x00,
	0xFE,0x9E,0xAF,0xD0,0xFE,0xFF,0xFF,0xFF,0xE7,0xC5,0xA6,0x94,0x92,0xA7,0x00,0x00
};
static const uint8_t _nes_g[] = {
	0x46,0x06,0x06,0x06,0x03,0x00,0x00,0x00,0x02,0x14,0x1E,0x1E,0x15,0x00,0x00,0x00,
	0x9D,0x4A,0x30,0x18,0x07,0x02,0x0B,0x23,0x3F,0x58,0x66,0x67,0x5E,0x00,0x00,0x00,
	0xFF,0x9E,0x76,0x65,0x67,0x6C,0x74,0x80,0x9A,0xB3,0xC4,0xC8,0xBF,0x2B,0x00,0x00,
	0xFF,0xD5,0xC0,0xB8,0xBF,0xC0,0xC3,0xCA,0xD5,0xDF,0xE6,0xE8,0xE4,0xA7,0x00,0x00
};
static const uint8_t _nes_b[] = {
	0x46,0x5A,0x78,0x73,0x4C,0x0E,0x00,0x00,0x00,0x00,0x00,0x00,0x21,0x00,0x00,0x00,
	0x9D,0xB9,0xE1,0xDA,0xA7,0x55,0x00,0x00,0x00,0x00,0x00,0x13,0x6E,0x00,0x00,0x00,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xB3,0x66,0x14,0x00,0x00,0x21,0x74,0xD0,0x2B,0x00,0x00,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xE0,0xBD,0x9C,0x8B,0x8E,0xA3,0xC5,0xEB,0xA7,0x00,0x00
};

//Makes four samples (one cycle) using QAM modulation on the color carrier for NTSC from a RGB palette
// Precompute constants outside the loop
const float inv_255 = 1.0f / 255.0f;
const float pi_div_2 = M_PI / 2.0f;

//NTSC Palette
void make_ntsc_palette()
{
    const float ofs = 23.f;
    const float amp = 57.f;
    const float hue = M_PI;
    const float saturation = 0.5f;

    for (int j = 0; j < 64; ++j)
    {
        float r = _nes_r[j];
        float g = _nes_g[j];
        float b = _nes_b[j];
        
        float y = (0.299f * r + 0.587f * g + 0.114f * b) * inv_255;
        float u = (-0.147407f * r - 0.289391f * g + 0.436798f * b) * inv_255;
        float v = (0.614777f * r - 0.514799f * g - 0.099978f * b) * inv_255;

        uint32_t sampl = 0;
        for (int i = 3; i >= 0; --i)
        {
            float wt = i * pi_div_2;
            float color = y + saturation * (u * sinf(wt + hue) + v * cosf(wt + hue));
            sampl |= static_cast<uint32_t>(round(ofs + amp * color)) << (i * 8);
        }
        nes_4_phase[j] = sampl;
    }
}

//PAL Palette
void make_pal_palette()
{
    const float ofs = 20.f;
    const float amp = 65.f;
    const float hue = M_PI;
    const float saturation = 0.8f;

    for (int j = 0; j < 64; ++j)
    {
        float r = _nes_r[j];
        float g = _nes_g[j];
        float b = _nes_b[j];

        float y = (0.299f * r + 0.587f * g + 0.114f * b) * inv_255;
        float u = (-0.147407f * r - 0.289391f * g + 0.436798f * b) * inv_255;
        float v = (0.614777f * r - 0.514799f * g - 0.099978f * b) * inv_255;

        uint32_t e_sampl = 0;
        uint32_t o_sampl = 0;
        for (int i = 3; i >= 0; --i)
        {
            float wt = i * pi_div_2;
            e_sampl |= static_cast<uint32_t>(round(ofs + amp * (y + saturation * (u * sinf(wt + hue) + v * cosf(wt + hue))))) << (i * 8);
            o_sampl |= static_cast<uint32_t>(round(ofs + amp * (y + saturation * (u * sinf(wt - hue) - v * cosf(wt - hue))))) << (i * 8);
        }
        _nes_yuv_4_phase_pal[j] = e_sampl;
        _nes_yuv_4_phase_pal[j + 64] = o_sampl;
    }
}
#endif

uint8_t* _nofrendo_rom = 0;
extern "C"
char *osd_getromdata()
{
    return (char *)_nofrendo_rom;
}

extern "C"
int nes_emulate_init(const char* path, int width, int height);

extern "C"
uint8_t** nes_emulate_frame(bool draw_flag);

static void (*nes_sound_cb)(void *buffer, int length) = 0;

extern uint32_t nes_pal[256];

const char* _nes_help[] = {
    "Keyboard:",
    "  Arrow Keys - D-Pad",
    "  Left Shift - Button A",
    "  Option     - Button B",
    "  Return     - Start",
    "  Tab        - Select",
    "",
    "Wiimote (held sideways):",
    "  +          - Start",
    "  -          - Select",
    "  + & -      - Reset",
    "  A,1        - Button A",
    "  B,2        - Button B",
    0
};

const char* _nes_ext[] = {
    "nes",
    0
};

int _audio_frequency;
extern "C"
void osd_getsoundinfo(sndinfo_t *info)
{
    info->sample_rate = _audio_frequency;
    info->bps = 8;
}

extern "C"
void osd_setsound(void (*playfunc)(void *buffer, int length))
{
    nes_sound_cb = playfunc;
}

std::string to_string(int i);
class EmuNofrendo : public Emu {
    uint8_t** _lines;
public:
    EmuNofrendo(int ntsc) : Emu("nofrendo",256,240,ntsc,(16 | (1 << 8)),4,EMU_NES)    // audio is 16bit, 3 or 6 cc width
    {
        _lines = 0;
        _ext = _nes_ext;
        _help = _nes_help;
        _audio_frequency = audio_frequency;
		gen_palettes();
    }

    virtual void gen_palettes()
    {
#ifdef calc_palettes
		make_ntsc_palette();
        make_pal_palette();
#endif
    }

    virtual int info(const string& file, vector<string>& strs)
    {
        string ext = get_ext(file);
        uint8_t hdr[15];
        int len = Emu::head(file,hdr,sizeof(hdr));
        string name = file.substr(file.find_last_of("/") + 1);
        strs.push_back(name);
        strs.push_back(::to_string(len/1024) + "k NES Cartridge");
        strs.push_back("");
        if (hdr[0] == 'N' && hdr[1] == 'E' && hdr[2] == 'S') {
            int prg = hdr[4] * 16;
            int chr = hdr[5] * 8;
            int mapper = (hdr[6] >> 4) | (hdr[7] & 0xF0);
            char buf[64];
            sprintf(buf,"MAP:%d",mapper);
            strs.push_back(buf);
            sprintf(buf,"PRG:%dk",prg);
            strs.push_back(buf);
            sprintf(buf,"CHR:%dk",chr);
            strs.push_back(buf);
        }
        return 0;
    }

    void pad(int pressed, int index)
    {
        event_t e = event_get(index);
        e(pressed);
    }

    enum {
        event_joypad1_up_ = 1,
        event_joypad1_down_ = 2,
        event_joypad1_left_ = 4,
        event_joypad1_right_ = 8,
        event_joypad1_start_ = 16,
        event_joypad1_select_ = 32,
        event_joypad1_a_ = 64,
        event_joypad1_b_ = 128,

        event_soft_reset_ = 256,
        event_hard_reset_ = 512
    };

    const int _nes_1[11] = {
        event_joypad1_up,
        event_joypad1_down,
        event_joypad1_left,
        event_joypad1_right,
        event_joypad1_start,
        event_joypad1_select,
        event_joypad1_a,
        event_joypad1_b,

        event_soft_reset,
        event_hard_reset,
        0
    };

    const int _nes_2[9] = {
        event_joypad2_up,
        event_joypad2_down,
        event_joypad2_left,
        event_joypad2_right,
        event_joypad2_start,
        event_joypad2_select,
        event_joypad2_a,
        event_joypad2_b,
        0
    };

    // Rotated 90%
    const uint32_t _common_nes[16] = {
        0,  // msb
        0,
        0,
        event_joypad1_start_,       // PLUS
        event_joypad1_left_,        // UP
        event_joypad1_right_,       // DOWN
        event_joypad1_up_,          // RIGHT
        event_joypad1_down_,        // LEFT

        0, // HOME
        0,
        0,
        event_joypad1_select_,  // MINUS
        event_joypad1_a_,      // A
        event_joypad1_b_,      // B
        event_joypad1_b_,      // ONE
        event_joypad1_a_,      // TWO
    };

    const uint32_t _classic_nes[16] = {
        event_joypad1_right_,    // RIGHT
        event_joypad1_down_,     // DOWN
        0,                       // LEFT_TOP
        event_joypad1_select_,    // MINUS
        0,                        // HOME
        event_joypad1_start_,     // PLUS
        0,                    // RIGHT_TOP
        0,

        0,                  // LOWER_LEFT
        event_joypad1_b_,   // B
        0,                  // Y
        event_joypad1_a_,   // A
        0,                  // X
        0,                  // LOWER_RIGHT
        event_joypad1_left_, // LEFT
        event_joypad1_up_   // UP
    };

    const uint32_t _generic_nes[16] = {
        0,                  // GENERIC_OTHER   0x8000
        0,                  // GENERIC_FIRE_X  0x4000  // RETCON
        0,                  // GENERIC_FIRE_Y  0x2000
        0,                  // GENERIC_FIRE_Z  0x1000

        event_joypad1_a_,      //GENERIC_FIRE_A  0x0800
        event_joypad1_b_,      //GENERIC_FIRE_B  0x0400
        0,                      //GENERIC_FIRE_C  0x0200
        0,                      //GENERIC_RESET   0x0100     // ATARI FLASHBACK

        event_joypad1_start_,   //GENERIC_START   0x0080
        event_joypad1_select_,  //GENERIC_SELECT  0x0040
        event_joypad1_a_,      //GENERIC_FIRE    0x0020
        event_joypad1_right_,  //GENERIC_RIGHT   0x0010

        event_joypad1_left_,   //GENERIC_LEFT    0x0008
        event_joypad1_down_,   //GENERIC_DOWN    0x0004
        event_joypad1_up_,      //GENERIC_UP      0x0002
        0,                      //GENERIC_MENU    0x0001
    };

    // raw HID data. handle WII/IR mappings
	virtual void hid(const uint8_t* d, int len)
	{
		if (d[0] != 0x32 && d[0] != 0x42)
			return;

		bool ir = *d++ == 0x42;

		for (int i = 0; i < 2; ++i) {
			uint32_t p;
			if (ir) {
				int m = d[0] + (d[1] << 8);
				p = generic_map(m, _generic_nes);
				d += 2;
			} else {
				p = wii_map(i, _common_nes, _classic_nes);
			}

			if ((p & event_joypad1_select_) && (p & event_joypad1_start_))
				pad(1, event_soft_reset);

			const int* m = (i ? _nes_2 : _nes_1);
			for (int e = 0; m[e]; ++e) {
				pad((p & 1), m[e]);
				p >>= 1;
			}
		}
	}

	virtual void key(int keycode, int pressed, int mods)
	{
		static const std::unordered_map<int, int> key_map = {
			{82, event_joypad1_up}, {81, event_joypad1_down}, {80, event_joypad1_left}, {79, event_joypad1_right},
			{21, event_soft_reset}, {23, event_hard_reset}, {61, event_joypad1_start},
			{62, ((KEY_MOD_LSHIFT | KEY_MOD_RSHIFT) & mods) ? event_hard_reset : event_soft_reset},
			{40, event_joypad1_start}, {43, event_joypad1_select},
			{225, event_joypad1_a}, {226, event_joypad1_b}
		};

		auto it = key_map.find(keycode);
		if (it != key_map.end()) {
			pad(pressed, it->second);
		}
	}


    virtual int insert(const std::string& path, int flags, int disk_index)
    {
        unmap_file(_nofrendo_rom);
        _nofrendo_rom = 0;
        printf("nofrendo inserting %s\n",path.c_str());

        uint8_t h[16];
        int len = head(path,h,sizeof(h));
        if (len < 0) {
            printf("nofrendo can't open %s\n",path.c_str());
            return -1;
        }

        printf("nofrendo %s is %d bytes\n",path.c_str(),len);
        _nofrendo_rom = map_file(path.c_str(),len);
        if (!_nofrendo_rom) {
            printf("nofrendo can't map %s\n",path.c_str());
            return -1;
        }

        if (nes_emulate_init(path.c_str(),width,height))
			printf("NES init failed\n");
		
		vTaskDelay(100 / portTICK_RATE_MS);
        _lines = nes_emulate_frame(true);   // first frame!
        return 0;
    }

    virtual int update()
    {
        if (_nofrendo_rom)
            _lines = nes_emulate_frame(true);
        return 0;
    }

    virtual uint8_t** video_buffer()
    {
        return _lines;
    }
    
    virtual int audio_buffer(int16_t* b, int len)
    {
        int n = frame_sample_count();
        if (nes_sound_cb) {
            nes_sound_cb(b,n);  // 8 bit unsigned
            uint8_t* b8 = (uint8_t*)b;
            for (int i = n-1; i >= 0; i--)
                b[i] = (b8[i] ^ 0x80) << 8;  // turn it back into signed 16
        }
        else
            memset(b,0,2*n);
        return n;
    }


    virtual const uint32_t* ntsc_palette() { return cc_width == 3 ? nes_3_phase : nes_4_phase; };
    virtual const uint32_t* pal_palette() { return _nes_yuv_4_phase_pal; };
    virtual const uint32_t* rgb_palette() { return nes_pal; };

    virtual int make_default_media(const string& path)
    {

        unpack((path + "/chase.nes").c_str(),chase_nes,sizeof(chase_nes));

        return 0;
    }
};

Emu* NewNofrendo(int ntsc)
{
    return new EmuNofrendo(ntsc);
}



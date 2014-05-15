/*
  Copyright 2013-2014 bcstack.org

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "bluetooth.h"

#ifdef EXPERIMENTAL

#define RFCOMM_SEND_UA_FLAG    0x01

static struct {
    u8 output_mask;
    u8 initiator;
    struct {
        u8 flags;
    } channels[CFG_RFCOMM_NUM_CHANNELS];
} rfcomm;

#define RFCOMM_MARK_OUTPUT(ch, flag)            \
    do {                                        \
        rfcomm.channels[(ch)].flags |= (flag);  \
        rfcomm.output_mask |= (1 << (ch));      \
    } while(0)

#define RFCOMM_CLEAR_OUTPUT(ch, flag)           \
    do {                                        \
        rfcomm.channels[(ch)].flags &= ~(flag); \
        rfcomm.output_mask &= ~(1 << (ch));     \
    } while(0)

static void rfcomm_input_sabm(u8* input, u16 isize);
static void rfcomm_input_ua(u8* input, u16 isize);
static void rfcomm_input_disc(u8* input, u16 isize);
static void rfcomm_input_dm(u8* input, u16 isize);
static void rfcomm_input_uih(u8* input, u16 isize);

static void rfcomm_output_sabm(u8 ch, u8* output, u16* osize);
static void rfcomm_output_ua(u8 ch, u8* output, u16* osize);
static void rfcomm_output_disc(u8 ch, u8* output, u16* osize);
static void rfcomm_output_dm(u8 ch, u8* output, u16* osize);
static void rfcomm_output_uih(u8 ch, u8* output, u16* osize);

/* reversed, 8-bit, poly=0x07 */
static u8 rfcomm_crc_table[256] = {
	0x00, 0x91, 0xe3, 0x72, 0x07, 0x96, 0xe4, 0x75,
	0x0e, 0x9f, 0xed, 0x7c, 0x09, 0x98, 0xea, 0x7b,
	0x1c, 0x8d, 0xff, 0x6e, 0x1b, 0x8a, 0xf8, 0x69,
	0x12, 0x83, 0xf1, 0x60, 0x15, 0x84, 0xf6, 0x67,

	0x38, 0xa9, 0xdb, 0x4a, 0x3f, 0xae, 0xdc, 0x4d,
	0x36, 0xa7, 0xd5, 0x44, 0x31, 0xa0, 0xd2, 0x43,
	0x24, 0xb5, 0xc7, 0x56, 0x23, 0xb2, 0xc0, 0x51,
	0x2a, 0xbb, 0xc9, 0x58, 0x2d, 0xbc, 0xce, 0x5f,

	0x70, 0xe1, 0x93, 0x02, 0x77, 0xe6, 0x94, 0x05,
	0x7e, 0xef, 0x9d, 0x0c, 0x79, 0xe8, 0x9a, 0x0b,
	0x6c, 0xfd, 0x8f, 0x1e, 0x6b, 0xfa, 0x88, 0x19,
	0x62, 0xf3, 0x81, 0x10, 0x65, 0xf4, 0x86, 0x17,

	0x48, 0xd9, 0xab, 0x3a, 0x4f, 0xde, 0xac, 0x3d,
	0x46, 0xd7, 0xa5, 0x34, 0x41, 0xd0, 0xa2, 0x33,
	0x54, 0xc5, 0xb7, 0x26, 0x53, 0xc2, 0xb0, 0x21,
	0x5a, 0xcb, 0xb9, 0x28, 0x5d, 0xcc, 0xbe, 0x2f,

	0xe0, 0x71, 0x03, 0x92, 0xe7, 0x76, 0x04, 0x95,
	0xee, 0x7f, 0x0d, 0x9c, 0xe9, 0x78, 0x0a, 0x9b,
	0xfc, 0x6d, 0x1f, 0x8e, 0xfb, 0x6a, 0x18, 0x89,
	0xf2, 0x63, 0x11, 0x80, 0xf5, 0x64, 0x16, 0x87,

	0xd8, 0x49, 0x3b, 0xaa, 0xdf, 0x4e, 0x3c, 0xad,
	0xd6, 0x47, 0x35, 0xa4, 0xd1, 0x40, 0x32, 0xa3,
	0xc4, 0x55, 0x27, 0xb6, 0xc3, 0x52, 0x20, 0xb1,
	0xca, 0x5b, 0x29, 0xb8, 0xcd, 0x5c, 0x2e, 0xbf,

	0x90, 0x01, 0x73, 0xe2, 0x97, 0x06, 0x74, 0xe5,
	0x9e, 0x0f, 0x7d, 0xec, 0x99, 0x08, 0x7a, 0xeb,
	0x8c, 0x1d, 0x6f, 0xfe, 0x8b, 0x1a, 0x68, 0xf9,
	0x82, 0x13, 0x61, 0xf0, 0x85, 0x14, 0x66, 0xf7,

	0xa8, 0x39, 0x4b, 0xda, 0xaf, 0x3e, 0x4c, 0xdd,
	0xa6, 0x37, 0x45, 0xd4, 0xa1, 0x30, 0x42, 0xd3,
	0xb4, 0x25, 0x57, 0xc6, 0xb3, 0x22, 0x50, 0xc1,
	0xba, 0x2b, 0x59, 0xc8, 0xbd, 0x2c, 0x5e, 0xcf
};

#define CRC8_INIT  0xFF

static u8 rfcomm_crc(u8 *data, u16 len)
{
    u16 i;
    u8 crc = CRC8_INIT;

    for (i = 0; i < len; i++)
        crc = rfcomm_crc_table[crc ^ data[i]];
    return crc;
}

#define rfcomm_fcs(_data, _len)                 \
    (0xFF - rfcomm_crc(_data, _len))

u8 rfcomm_input(u8* input, u16 isize)
{
    u8 ch = input[0] >> 3;
    u8 type = input[1];

    if (ch > 1) return;

    switch (type) {
    case RFCOMM_SABM: rfcomm_input_sabm(input, isize); break;
    case RFCOMM_UA:   rfcomm_input_ua(input, isize);   break;
    case RFCOMM_DISC: rfcomm_input_disc(input, isize); break;
    case RFCOMM_DM:   rfcomm_input_dm(input, isize);   break;
    case RFCOMM_UIH:  rfcomm_input_uih(input, isize);  break;
    }

    if (rfcomm.output_mask) return 1;
    else return 0;
}

static void rfcomm_input_sabm(u8* input, u16 isize)
{
    u8 ch = input[0] >> 3;

    RFCOMM_MARK_OUTPUT(ch, RFCOMM_SEND_UA_FLAG);
}

static void rfcomm_input_ua(u8* input, u16 isize)
{
}

static void rfcomm_input_disc(u8* input, u16 isize)
{
}

static void rfcomm_input_dm(u8* input, u16 isize)
{
}

static void rfcomm_input_uih(u8* input, u16 isize)
{
}

u8 rfcomm_output(u8* output, u16* osize)
{
    u8 ch;

    if (rfcomm.output_mask & 1) ch = 0;
    else ch = 1;

    if (rfcomm.channels[ch].flags & RFCOMM_SEND_UA_FLAG) {
        RFCOMM_CLEAR_OUTPUT(ch, RFCOMM_SEND_UA_FLAG);
        rfcomm_output_ua(ch, output, osize);
    }

    if (rfcomm.output_mask) return 1;
    else return 0;
}

static void rfcomm_output_sabm(u8 ch, u8* output, u16* osize)
{
}

static void rfcomm_output_ua(u8 ch, u8* output, u16* osize)
{
    if (ch == 0) {
        output[0] = 3 | (ch << 3);
    } else {
        output[0] = 7 | (ch << 3);
    }

	output[1] = RFCOMM_UA;
	output[2] = 1;
	output[3] = rfcomm_fcs(output, 3);

    *osize = 4;
}

static void rfcomm_output_disc(u8 ch, u8* output, u16* osize)
{
}

static void rfcomm_output_dm(u8 ch, u8* output, u16* osize)
{
}

static void rfcomm_output_uih(u8 ch, u8* output, u16* osize)
{
}


#endif // EXPERIMENTAL

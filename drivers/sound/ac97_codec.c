/*
 * ac97_codec.c: Generic AC97 mixer/modem module
 *
 * Derived from ac97 mixer in maestro and trident driver.
 *
 * Copyright 2000 Silicon Integrated System Corporation
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 **************************************************************************
 *
 * The Intel Audio Codec '97 specification is available at the Intel
 * audio homepage: http://developer.intel.com/ial/scalableplatforms/audio/
 *
 * The specification itself is currently available at:
 * ftp://download.intel.com/ial/scalableplatforms/ac97r22.pdf
 *
 **************************************************************************
 *
 * History
 * May 02, 2003 Liam Girdwood <liam.girdwood@wolfsonmicro.com>
 *	Removed non existant WM9700
 *	Added support for WM9705, WM9708, WM9709, WM9710, WM9711
 *	WM9712 and WM9717
 * Mar 28, 2002 Randolph Bentson <bentson@holmsjoen.com>
 *	corrections to support WM9707 in ViewPad 1000
 * v0.4 Mar 15 2000 Ollie Lho
 *	dual codecs support verified with 4 channels output
 * v0.3 Feb 22 2000 Ollie Lho
 *	bug fix for record mask setting
 * v0.2 Feb 10 2000 Ollie Lho
 *	add ac97_read_proc for /proc/driver/{vendor}/ac97
 * v0.1 Jan 14 2000 Ollie Lho <ollie@sis.com.tw> 
 *	Isolated from trident.c to support multiple ac97 codec
 */
#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/ac97_codec.h>
#include <asm/uaccess.h>

#define CODEC_ID_BUFSZ 14

static int ac97_read_mixer(struct ac97_codec *codec, int oss_channel);
static void ac97_write_mixer(struct ac97_codec *codec, int oss_channel, 
			     unsigned int left, unsigned int right);
static void ac97_set_mixer(struct ac97_codec *codec, unsigned int oss_mixer, unsigned int val );
static int ac97_recmask_io(struct ac97_codec *codec, int rw, int mask);
static int ac97_mixer_ioctl(struct ac97_codec *codec, unsigned int cmd, unsigned long arg);

static int ac97_init_mixer(struct ac97_codec *codec);

static int wolfson_init03(struct ac97_codec * codec);
static int wolfson_init04(struct ac97_codec * codec);
static int wolfson_init05(struct ac97_codec * codec);
static int wolfson_init11(struct ac97_codec * codec);
static int tritech_init(struct ac97_codec * codec);
static int tritech_maestro_init(struct ac97_codec * codec);
static int sigmatel_9708_init(struct ac97_codec *codec);
static int sigmatel_9721_init(struct ac97_codec *codec);
static int sigmatel_9744_init(struct ac97_codec *codec);
static int ad1886_init(struct ac97_codec *codec);
static int eapd_control(struct ac97_codec *codec, int);
static int crystal_digital_control(struct ac97_codec *codec, int slots, int rate, int mode);
static int cmedia_init(struct ac97_codec * codec);
static int cmedia_digital_control(struct ac97_codec *codec, int slots, int rate, int mode);
static int generic_digital_control(struct ac97_codec *codec, int slots, int rate, int mode);


/*
 *	AC97 operations.
 *
 *	If you are adding a codec then you should be able to use
 *		eapd_ops - any codec that supports EAPD amp control (most)
 *		null_ops - any ancient codec that supports nothing
 *
 *	The three functions are
 *		init - used for non AC97 standard initialisation
 *		amplifier - used to do amplifier control (1=on 0=off)
 *		digital - switch to digital modes (0 = analog)
 *
 *	Not all codecs support all features, not all drivers use all the
 *	operations yet
 */
 
static struct ac97_ops null_ops = { NULL, NULL, NULL };
static struct ac97_ops default_ops = { NULL, eapd_control, NULL };
static struct ac97_ops default_digital_ops = { NULL, eapd_control, generic_digital_control};
static struct ac97_ops wolfson_ops03 = { wolfson_init03, NULL, NULL };
static struct ac97_ops wolfson_ops04 = { wolfson_init04, NULL, NULL };
static struct ac97_ops wolfson_ops05 = { wolfson_init05, NULL, NULL };
static struct ac97_ops wolfson_ops11 = { wolfson_init11, NULL, NULL };
static struct ac97_ops tritech_ops = { tritech_init, NULL, NULL };
static struct ac97_ops tritech_m_ops = { tritech_maestro_init, NULL, NULL };
static struct ac97_ops sigmatel_9708_ops = { sigmatel_9708_init, NULL, NULL };
static struct ac97_ops sigmatel_9721_ops = { sigmatel_9721_init, NULL, NULL };
static struct ac97_ops sigmatel_9744_ops = { sigmatel_9744_init, NULL, NULL };
static struct ac97_ops crystal_digital_ops = { NULL, eapd_control, crystal_digital_control };
static struct ac97_ops ad1886_ops = { ad1886_init, eapd_control, NULL };
static struct ac97_ops cmedia_ops = { NULL, eapd_control, NULL};
static struct ac97_ops cmedia_digital_ops = { cmedia_init, eapd_control, cmedia_digital_control};

/* sorted by vendor/device id */
static const struct {
	u32 id;
	char *name;
	struct ac97_ops *ops;
	int flags;
} ac97_codec_ids[] = {
	{0x41445303, "Analog Devices AD1819",	&null_ops},
	{0x41445340, "Analog Devices AD1881",	&null_ops},
	{0x41445348, "Analog Devices AD1881A",	&null_ops},
	{0x41445360, "Analog Devices AD1885",	&default_ops},
	{0x41445361, "Analog Devices AD1886",	&ad1886_ops},
	{0x41445370, "Analog Devices AD1981",	&null_ops},
	{0x41445372, "Analog Devices AD1981A",	&null_ops},
	{0x41445374, "Analog Devices AD1981B",	&null_ops},
	{0x41445460, "Analog Devices AD1885",	&default_ops},
	{0x41445461, "Analog Devices AD1886",	&ad1886_ops},
	{0x414B4D00, "Asahi Kasei AK4540",	&null_ops},
	{0x414B4D01, "Asahi Kasei AK4542",	&null_ops},
	{0x414B4D02, "Asahi Kasei AK4543",	&null_ops},
	{0x414C4326, "ALC100P",			&null_ops},
	{0x414C4710, "ALC200/200P",		&null_ops},
	{0x414C4720, "ALC650",			&default_digital_ops},
	{0x434D4941, "CMedia",			&cmedia_ops,		AC97_NO_PCM_VOLUME },
	{0x434D4942, "CMedia",			&cmedia_ops,		AC97_NO_PCM_VOLUME },
	{0x434D4961, "CMedia",			&cmedia_digital_ops,	AC97_NO_PCM_VOLUME },
	{0x43525900, "Cirrus Logic CS4297",	&default_ops},
	{0x43525903, "Cirrus Logic CS4297",	&default_ops},
	{0x43525913, "Cirrus Logic CS4297A rev A", &default_ops},
	{0x43525914, "Cirrus Logic CS4297A rev B", &default_ops},
	{0x43525923, "Cirrus Logic CS4298",	&null_ops},
	{0x4352592B, "Cirrus Logic CS4294",	&null_ops},
	{0x4352592D, "Cirrus Logic CS4294",	&null_ops},
	{0x43525931, "Cirrus Logic CS4299 rev A", &crystal_digital_ops},
	{0x43525933, "Cirrus Logic CS4299 rev C", &crystal_digital_ops},
	{0x43525934, "Cirrus Logic CS4299 rev D", &crystal_digital_ops},
	{0x43585442, "CXT66",			&default_ops,		AC97_DELUDED_MODEM },
	{0x44543031, "Diamond Technology DT0893", &default_ops},
	{0x45838308, "ESS Allegro ES1988",	&null_ops},
	{0x49434511, "ICE1232",			&null_ops}, /* I hope --jk */
	{0x4e534331, "National Semiconductor LM4549", &null_ops},
	{0x53494c22, "Silicon Laboratory Si3036", &null_ops},
	{0x53494c23, "Silicon Laboratory Si3038", &null_ops},
	{0x545200FF, "TriTech TR?????",		&tritech_m_ops},
	{0x54524102, "TriTech TR28022",		&null_ops},
	{0x54524103, "TriTech TR28023",		&null_ops},
	{0x54524106, "TriTech TR28026",		&null_ops},
	{0x54524108, "TriTech TR28028",		&tritech_ops},
	{0x54524123, "TriTech TR A5",		&null_ops},
	{0x574D4C03, "Wolfson WM9703/07/08/17",	&wolfson_ops03},
	{0x574D4C04, "Wolfson WM9704M/WM9704Q",	&wolfson_ops04},
	{0x574D4C05, "Wolfson WM9705/WM9710",   &wolfson_ops05},
	{0x574D4C09, "Wolfson WM9709",		&null_ops},
	{0x574D4C12, "Wolfson WM9711/9712",	&wolfson_ops11},
	{0x83847600, "SigmaTel STAC????",	&null_ops},
	{0x83847604, "SigmaTel STAC9701/3/4/5", &null_ops},
	{0x83847605, "SigmaTel STAC9704",	&null_ops},
	{0x83847608, "SigmaTel STAC9708",	&sigmatel_9708_ops},
	{0x83847609, "SigmaTel STAC9721/23",	&sigmatel_9721_ops},
	{0x83847644, "SigmaTel STAC9744/45",	&sigmatel_9744_ops},
	{0x83847652, "SigmaTel STAC9752/53",	&default_ops},
	{0x83847656, "SigmaTel STAC9756/57",	&sigmatel_9744_ops},
	{0x83847666, "SigmaTel STAC9750T",	&sigmatel_9744_ops},
	{0x83847684, "SigmaTel STAC9783/84?",	&null_ops},
	{0x57454301, "Winbond 83971D",		&null_ops},
};

static const char *ac97_stereo_enhancements[] =
{
	/*   0 */ "No 3D Stereo Enhancement",
	/*   1 */ "Analog Devices Phat Stereo",
	/*   2 */ "Creative Stereo Enhancement",
	/*   3 */ "National Semi 3D Stereo Enhancement",
	/*   4 */ "YAMAHA Ymersion",
	/*   5 */ "BBE 3D Stereo Enhancement",
	/*   6 */ "Crystal Semi 3D Stereo Enhancement",
	/*   7 */ "Qsound QXpander",
	/*   8 */ "Spatializer 3D Stereo Enhancement",
	/*   9 */ "SRS 3D Stereo Enhancement",
	/*  10 */ "Platform Tech 3D Stereo Enhancement",
	/*  11 */ "AKM 3D Audio",
	/*  12 */ "Aureal Stereo Enhancement",
	/*  13 */ "Aztech 3D Enhancement",
	/*  14 */ "Binaura 3D Audio Enhancement",
	/*  15 */ "ESS Technology Stereo Enhancement",
	/*  16 */ "Harman International VMAx",
	/*  17 */ "Nvidea 3D Stereo Enhancement",
	/*  18 */ "Philips Incredible Sound",
	/*  19 */ "Texas Instruments 3D Stereo Enhancement",
	/*  20 */ "VLSI Technology 3D Stereo Enhancement",
	/*  21 */ "TriTech 3D Stereo Enhancement",
	/*  22 */ "Realtek 3D Stereo Enhancement",
	/*  23 */ "Samsung 3D Stereo Enhancement",
	/*  24 */ "Wolfson Microelectronics 3D Enhancement",
	/*  25 */ "Delta Integration 3D Enhancement",
	/*  26 */ "SigmaTel 3D Enhancement",
	/*  27 */ "Winbond 3D Stereo Enhancement",
	/*  28 */ "Rockwell 3D Stereo Enhancement",
	/*  29 */ "Reserved 29",
	/*  30 */ "Reserved 30",
	/*  31 */ "Reserved 31"
};

/* this table has default mixer values for all OSS mixers. */
static struct mixer_defaults {
	int mixer;
	unsigned int value;
} mixer_defaults[SOUND_MIXER_NRDEVICES] = {
	/* all values 0 -> 100 in bytes */
	{SOUND_MIXER_VOLUME,	0x4343},
	{SOUND_MIXER_BASS,	0x4343},
	{SOUND_MIXER_TREBLE,	0x4343},
	{SOUND_MIXER_PCM,	0x4343},
	{SOUND_MIXER_SPEAKER,	0x4343},
	{SOUND_MIXER_LINE,	0x4343},
	{SOUND_MIXER_MIC,	0x0000},
	{SOUND_MIXER_CD,	0x4343},
	{SOUND_MIXER_ALTPCM,	0x4343},
	{SOUND_MIXER_IGAIN,	0x4343},
	{SOUND_MIXER_LINE1,	0x4343},
	{SOUND_MIXER_PHONEIN,	0x4343},
	{SOUND_MIXER_PHONEOUT,	0x4343},
	{SOUND_MIXER_VIDEO,	0x4343},
	{-1,0}
};

/* table to scale scale from OSS mixer value to AC97 mixer register value */	
static struct ac97_mixer_hw {
	unsigned char offset;
	int scale;
} ac97_hw[SOUND_MIXER_NRDEVICES]= {
	[SOUND_MIXER_VOLUME]	=	{AC97_MASTER_VOL_STEREO,64},
	[SOUND_MIXER_BASS]	=	{AC97_MASTER_TONE,	16},
	[SOUND_MIXER_TREBLE]	=	{AC97_MASTER_TONE,	16},
	[SOUND_MIXER_PCM]	=	{AC97_PCMOUT_VOL,	32},
	[SOUND_MIXER_SPEAKER]	=	{AC97_PCBEEP_VOL,	16},
	[SOUND_MIXER_LINE]	=	{AC97_LINEIN_VOL,	32},
	[SOUND_MIXER_MIC]	=	{AC97_MIC_VOL,		32},
	[SOUND_MIXER_CD]	=	{AC97_CD_VOL,		32},
	[SOUND_MIXER_ALTPCM]	=	{AC97_HEADPHONE_VOL,	64},
	[SOUND_MIXER_IGAIN]	=	{AC97_RECORD_GAIN,	16},
	[SOUND_MIXER_LINE1]	=	{AC97_AUX_VOL,		32},
	[SOUND_MIXER_PHONEIN]	= 	{AC97_PHONE_VOL,	32},
	[SOUND_MIXER_PHONEOUT]	= 	{AC97_MASTER_VOL_MONO,	64},
	[SOUND_MIXER_VIDEO]	=	{AC97_VIDEO_VOL,	32},
};

/* the following tables allow us to go from OSS <-> ac97 quickly. */
enum ac97_recsettings {
	AC97_REC_MIC=0,
	AC97_REC_CD,
	AC97_REC_VIDEO,
	AC97_REC_AUX,
	AC97_REC_LINE,
	AC97_REC_STEREO, /* combination of all enabled outputs..  */
	AC97_REC_MONO,	      /*.. or the mono equivalent */
	AC97_REC_PHONE
};

static const unsigned int ac97_rm2oss[] = {
	[AC97_REC_MIC] 	 = SOUND_MIXER_MIC,
	[AC97_REC_CD] 	 = SOUND_MIXER_CD,
	[AC97_REC_VIDEO] = SOUND_MIXER_VIDEO,
	[AC97_REC_AUX] 	 = SOUND_MIXER_LINE1,
	[AC97_REC_LINE]  = SOUND_MIXER_LINE,
	[AC97_REC_STEREO]= SOUND_MIXER_IGAIN,
	[AC97_REC_PHONE] = SOUND_MIXER_PHONEIN
};

/* indexed by bit position */
static const unsigned int ac97_oss_rm[] = {
	[SOUND_MIXER_MIC] 	= AC97_REC_MIC,
	[SOUND_MIXER_CD] 	= AC97_REC_CD,
	[SOUND_MIXER_VIDEO] 	= AC97_REC_VIDEO,
	[SOUND_MIXER_LINE1] 	= AC97_REC_AUX,
	[SOUND_MIXER_LINE] 	= AC97_REC_LINE,
	[SOUND_MIXER_IGAIN]	= AC97_REC_STEREO,
	[SOUND_MIXER_PHONEIN] 	= AC97_REC_PHONE
};

static LIST_HEAD(codecs);
static LIST_HEAD(codec_drivers);
static DECLARE_MUTEX(codec_sem);

/* reads the given OSS mixer from the ac97 the caller must have insured that the ac97 knows
   about that given mixer, and should be holding a spinlock for the card */
static int ac97_read_mixer(struct ac97_codec *codec, int oss_channel) 
{
	u16 val;
	int ret = 0;
	int scale;
	struct ac97_mixer_hw *mh = &ac97_hw[oss_channel];

	val = codec->codec_read(codec , mh->offset);

	if (val & AC97_MUTE) {
		ret = 0;
	} else if (AC97_STEREO_MASK & (1 << oss_channel)) {
		/* nice stereo mixers .. */
		int left,right;

		left = (val >> 8)  & 0x7f;
		right = val  & 0x7f;

		if (oss_channel == SOUND_MIXER_IGAIN) {
			right = (right * 100) / mh->scale;
			left = (left * 100) / mh->scale;
		} else {
			/* these may have 5 or 6 bit resolution */
			if(oss_channel == SOUND_MIXER_VOLUME || oss_channel == SOUND_MIXER_ALTPCM)
				scale = (1 << codec->bit_resolution);
			else
				scale = mh->scale;

			right = 100 - ((right * 100) / scale);
			left = 100 - ((left * 100) / scale);
		}
		ret = left | (right << 8);
	} else if (oss_channel == SOUND_MIXER_SPEAKER) {
		ret = 100 - ((((val & 0x1e)>>1) * 100) / mh->scale);
	} else if (oss_channel == SOUND_MIXER_PHONEIN) {
		ret = 100 - (((val & 0x1f) * 100) / mh->scale);
	} else if (oss_channel == SOUND_MIXER_PHONEOUT) {
		scale = (1 << codec->bit_resolution);
		ret = 100 - (((val & 0x1f) * 100) / scale);
	} else if (oss_channel == SOUND_MIXER_MIC) {
		ret = 100 - (((val & 0x1f) * 100) / mh->scale);
		/*  the low bit is optional in the tone sliders and masking
		    it lets us avoid the 0xf 'bypass'.. */
	} else if (oss_channel == SOUND_MIXER_BASS) {
		ret = 100 - ((((val >> 8) & 0xe) * 100) / mh->scale);
	} else if (oss_channel == SOUND_MIXER_TREBLE) {
		ret = 100 - (((val & 0xe) * 100) / mh->scale);
	}

#ifdef DEBUG
	printk("ac97_codec: read OSS mixer %2d (%s ac97 register 0x%02x), "
	       "0x%04x -> 0x%04x\n",
	       oss_channel, codec->id ? "Secondary" : "Primary",
	       mh->offset, val, ret);
#endif

	return ret;
}

/* write the OSS encoded volume to the given OSS encoded mixer, again caller's job to
   make sure all is well in arg land, call with spinlock held */
static void ac97_write_mixer(struct ac97_codec *codec, int oss_channel,
		      unsigned int left, unsigned int right)
{
	u16 val = 0;
	int scale;
	struct ac97_mixer_hw *mh = &ac97_hw[oss_channel];

#ifdef DEBUG
	printk("ac97_codec: wrote OSS mixer %2d (%s ac97 register 0x%02x), "
	       "left vol:%2d, right vol:%2d:",
	       oss_channel, codec->id ? "Secondary" : "Primary",
	       mh->offset, left, right);
#endif

	if (AC97_STEREO_MASK & (1 << oss_channel)) {
		/* stereo mixers */
		if (left == 0 && right == 0) {
			val = AC97_MUTE;
		} else {
			if (oss_channel == SOUND_MIXER_IGAIN) {
				right = (right * mh->scale) / 100;
				left = (left * mh->scale) / 100;
				if (right >= mh->scale)
					right = mh->scale-1;
				if (left >= mh->scale)
					left = mh->scale-1;
			} else {
				/* these may have 5 or 6 bit resolution */
				if (oss_channel == SOUND_MIXER_VOLUME ||
				    oss_channel == SOUND_MIXER_ALTPCM)
					scale = (1 << codec->bit_resolution);
				else
					scale = mh->scale;

				right = ((100 - right) * scale) / 100;
				left = ((100 - left) * scale) / 100;
				if (right >= scale)
					right = scale-1;
				if (left >= scale)
					left = scale-1;
			}
			val = (left << 8) | right;
		}
	} else if (oss_channel == SOUND_MIXER_BASS) {
		val = codec->codec_read(codec , mh->offset) & ~0x0f00;
		left = ((100 - left) * mh->scale) / 100;
		if (left >= mh->scale)
			left = mh->scale-1;
		val |= (left << 8) & 0x0e00;
	} else if (oss_channel == SOUND_MIXER_TREBLE) {
		val = codec->codec_read(codec , mh->offset) & ~0x000f;
		left = ((100 - left) * mh->scale) / 100;
		if (left >= mh->scale)
			left = mh->scale-1;
		val |= left & 0x000e;
	} else if(left == 0) {
		val = AC97_MUTE;
	} else if (oss_channel == SOUND_MIXER_SPEAKER) {
		left = ((100 - left) * mh->scale) / 100;
		if (left >= mh->scale)
			left = mh->scale-1;
		val = left << 1;
	} else if (oss_channel == SOUND_MIXER_PHONEIN) {
		left = ((100 - left) * mh->scale) / 100;
		if (left >= mh->scale)
			left = mh->scale-1;
		val = left;
	} else if (oss_channel == SOUND_MIXER_PHONEOUT) {
		scale = (1 << codec->bit_resolution);
		left = ((100 - left) * scale) / 100;
		if (left >= mh->scale)
			left = mh->scale-1;
		val = left;
	} else if (oss_channel == SOUND_MIXER_MIC) {
		val = codec->codec_read(codec , mh->offset) & ~0x801f;
		left = ((100 - left) * mh->scale) / 100;
		if (left >= mh->scale)
			left = mh->scale-1;
		val |= left;
		/*  the low bit is optional in the tone sliders and masking
		    it lets us avoid the 0xf 'bypass'.. */
	}
#ifdef DEBUG
	printk(" 0x%04x", val);
#endif

	codec->codec_write(codec, mh->offset, val);

#ifdef DEBUG
	val = codec->codec_read(codec, mh->offset);
	printk(" -> 0x%04x\n", val);
#endif
}

/* a thin wrapper for write_mixer */
static void ac97_set_mixer(struct ac97_codec *codec, unsigned int oss_mixer, unsigned int val ) 
{
	unsigned int left,right;

	/* cleanse input a little */
	right = ((val >> 8)  & 0xff) ;
	left = (val  & 0xff) ;

	if (right > 100) right = 100;
	if (left > 100) left = 100;

	codec->mixer_state[oss_mixer] = (right << 8) | left;
	codec->write_mixer(codec, oss_mixer, left, right);
}

/* read or write the recmask, the ac97 can really have left and right recording
   inputs independantly set, but OSS doesn't seem to want us to express that to
   the user. the caller guarantees that we have a supported bit set, and they
   must be holding the card's spinlock */
static int ac97_recmask_io(struct ac97_codec *codec, int rw, int mask) 
{
	unsigned int val;

	if (rw) {
		/* read it from the card */
		val = codec->codec_read(codec, AC97_RECORD_SELECT);
#ifdef DEBUG
		printk("ac97_codec: ac97 recmask to set to 0x%04x\n", val);
#endif
		return (1 << ac97_rm2oss[val & 0x07]);
	}

	/* else, write the first set in the mask as the
	   output */	
	/* clear out current set value first (AC97 supports only 1 input!) */
	val = (1 << ac97_rm2oss[codec->codec_read(codec, AC97_RECORD_SELECT) & 0x07]);
	if (mask != val)
	    mask &= ~val;
       
	val = ffs(mask); 
	val = ac97_oss_rm[val-1];
	val |= val << 8;  /* set both channels */

#ifdef DEBUG
	printk("ac97_codec: setting ac97 recmask to 0x%04x\n", val);
#endif

	codec->codec_write(codec, AC97_RECORD_SELECT, val);

	return 0;
};

static int ac97_mixer_ioctl(struct ac97_codec *codec, unsigned int cmd, unsigned long arg)
{
	int i, val = 0;

	if (cmd == SOUND_MIXER_INFO) {
		mixer_info info;
		strncpy(info.id, codec->name, sizeof(info.id));
		strncpy(info.name, codec->name, sizeof(info.name));
		info.modify_counter = codec->modcnt;
		if (copy_to_user((void *)arg, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	if (cmd == SOUND_OLD_MIXER_INFO) {
		_old_mixer_info info;
		strncpy(info.id, codec->name, sizeof(info.id));
		strncpy(info.name, codec->name, sizeof(info.name));
		if (copy_to_user((void *)arg, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}

	if (_IOC_TYPE(cmd) != 'M' || _SIOC_SIZE(cmd) != sizeof(int))
		return -EINVAL;

	if (cmd == OSS_GETVERSION)
		return put_user(SOUND_VERSION, (int *)arg);

	if (_SIOC_DIR(cmd) == _SIOC_READ) {
		switch (_IOC_NR(cmd)) {
		case SOUND_MIXER_RECSRC: /* give them the current record source */
			if (!codec->recmask_io) {
				val = 0;
			} else {
				val = codec->recmask_io(codec, 1, 0);
			}
			break;

		case SOUND_MIXER_DEVMASK: /* give them the supported mixers */
			val = codec->supported_mixers;
			break;

		case SOUND_MIXER_RECMASK: /* Arg contains a bit for each supported recording source */
			val = codec->record_sources;
			break;

		case SOUND_MIXER_STEREODEVS: /* Mixer channels supporting stereo */
			val = codec->stereo_mixers;
			break;

		case SOUND_MIXER_CAPS:
			val = SOUND_CAP_EXCL_INPUT;
			break;

		default: /* read a specific mixer */
			i = _IOC_NR(cmd);

			if (!supported_mixer(codec, i)) 
				return -EINVAL;

			/* do we ever want to touch the hardware? */
		        /* val = codec->read_mixer(codec, i); */
			val = codec->mixer_state[i];
 			break;
		}
		return put_user(val, (int *)arg);
	}

	if (_SIOC_DIR(cmd) == (_SIOC_WRITE|_SIOC_READ)) {
		codec->modcnt++;
		if (get_user(val, (int *)arg))
			return -EFAULT;

		switch (_IOC_NR(cmd)) {
		case SOUND_MIXER_RECSRC: /* Arg contains a bit for each recording source */
			if (!codec->recmask_io) return -EINVAL;
			if (!val) return 0;
			if (!(val &= codec->record_sources)) return -EINVAL;

			codec->recmask_io(codec, 0, val);

			return 0;
		default: /* write a specific mixer */
			i = _IOC_NR(cmd);

			if (!supported_mixer(codec, i)) 
				return -EINVAL;

			ac97_set_mixer(codec, i, val);

			return 0;
		}
	}
	return -EINVAL;
}

/* entry point for /proc/driver/controller_vendor/ac97/%d */
int ac97_read_proc (char *page, char **start, off_t off,
		    int count, int *eof, void *data)
{
	int len = 0, cap, extid, val, id1, id2;
	struct ac97_codec *codec;
	int is_ac97_20 = 0;

	if ((codec = data) == NULL)
		return -ENODEV;

	id1 = codec->codec_read(codec, AC97_VENDOR_ID1);
	id2 = codec->codec_read(codec, AC97_VENDOR_ID2);
	len += sprintf (page+len, "Vendor name      : %s\n", codec->name);
	len += sprintf (page+len, "Vendor id        : %04X %04X\n", id1, id2);

	extid = codec->codec_read(codec, AC97_EXTENDED_ID);
	extid &= ~((1<<2)|(1<<4)|(1<<5)|(1<<10)|(1<<11)|(1<<12)|(1<<13));
	len += sprintf (page+len, "AC97 Version     : %s\n",
			extid ? "2.0 or later" : "1.0");
	if (extid) is_ac97_20 = 1;

	cap = codec->codec_read(codec, AC97_RESET);
	len += sprintf (page+len, "Capabilities     :%s%s%s%s%s%s\n",
			cap & 0x0001 ? " -dedicated MIC PCM IN channel-" : "",
			cap & 0x0002 ? " -reserved1-" : "",
			cap & 0x0004 ? " -bass & treble-" : "",
			cap & 0x0008 ? " -simulated stereo-" : "",
			cap & 0x0010 ? " -headphone out-" : "",
			cap & 0x0020 ? " -loudness-" : "");
	val = cap & 0x00c0;
	len += sprintf (page+len, "DAC resolutions  :%s%s%s\n",
			" -16-bit-",
			val & 0x0040 ? " -18-bit-" : "",
			val & 0x0080 ? " -20-bit-" : "");
	val = cap & 0x0300;
	len += sprintf (page+len, "ADC resolutions  :%s%s%s\n",
			" -16-bit-",
			val & 0x0100 ? " -18-bit-" : "",
			val & 0x0200 ? " -20-bit-" : "");
	len += sprintf (page+len, "3D enhancement   : %s\n",
			ac97_stereo_enhancements[(cap >> 10) & 0x1f]);

	val = codec->codec_read(codec, AC97_GENERAL_PURPOSE);
	len += sprintf (page+len, "POP path         : %s 3D\n"
			"Sim. stereo      : %s\n"
			"3D enhancement   : %s\n"
			"Loudness         : %s\n"
			"Mono output      : %s\n"
			"MIC select       : %s\n"
			"ADC/DAC loopback : %s\n",
			val & 0x8000 ? "post" : "pre",
			val & 0x4000 ? "on" : "off",
			val & 0x2000 ? "on" : "off",
			val & 0x1000 ? "on" : "off",
			val & 0x0200 ? "MIC" : "MIX",
			val & 0x0100 ? "MIC2" : "MIC1",
			val & 0x0080 ? "on" : "off");

	extid = codec->codec_read(codec, AC97_EXTENDED_ID);
	cap = extid;
	len += sprintf (page+len, "Ext Capabilities :%s%s%s%s%s%s%s\n",
			cap & 0x0001 ? " -var rate PCM audio-" : "",
			cap & 0x0002 ? " -2x PCM audio out-" : "",
			cap & 0x0008 ? " -var rate MIC in-" : "",
			cap & 0x0040 ? " -PCM center DAC-" : "",
			cap & 0x0080 ? " -PCM surround DAC-" : "",
			cap & 0x0100 ? " -PCM LFE DAC-" : "",
			cap & 0x0200 ? " -slot/DAC mappings-" : "");
	if (is_ac97_20) {
		len += sprintf (page+len, "Front DAC rate   : %d\n",
				codec->codec_read(codec, AC97_PCM_FRONT_DAC_RATE));
	}

	return len;
}

/**
 *	codec_id	-  Turn id1/id2 into a PnP string
 *	@id1: Vendor ID1
 *	@id2: Vendor ID2
 *	@buf: CODEC_ID_BUFSZ byte buffer
 *
 *	Fills buf with a zero terminated PnP ident string for the id1/id2
 *	pair. For convenience the return is the passed in buffer pointer.
 */
 
static char *codec_id(u16 id1, u16 id2, char *buf)
{
	if(id1&0x8080) {
		snprintf(buf, CODEC_ID_BUFSZ, "0x%04x:0x%04x", id1, id2);
	} else {
		buf[0] = (id1 >> 8);
		buf[1] = (id1 & 0xFF);
		buf[2] = (id2 >> 8);
		snprintf(buf+3, CODEC_ID_BUFSZ - 3, "%d", id2&0xFF);
	}
	return buf;
}
 
/**
 *	ac97_check_modem - Check if the Codec is a modem
 *	@codec: codec to check
 *
 *	Return true if the device is an AC97 1.0 or AC97 2.0 modem
 */
 
static int ac97_check_modem(struct ac97_codec *codec)
{
	/* Check for an AC97 1.0 soft modem (ID1) */
	if(codec->codec_read(codec, AC97_RESET) & 2)
		return 1;
	/* Check for an AC97 2.x soft modem */
	codec->codec_write(codec, AC97_EXTENDED_MODEM_ID, 0L);
	if(codec->codec_read(codec, AC97_EXTENDED_MODEM_ID) & 1)
		return 1;
	return 0;
}


/**
 *	ac97_alloc_codec - Allocate an AC97 codec
 *
 *	Returns a new AC97 codec structure. AC97 codecs may become
 *	refcounted soon so this interface is needed. Returns with
 *	one reference taken.
 */
 
struct ac97_codec *ac97_alloc_codec(void)
{
	struct ac97_codec *codec = kmalloc(sizeof(struct ac97_codec), GFP_KERNEL);
	if(!codec)
		return NULL;

	memset(codec, 0, sizeof(*codec));
	spin_lock_init(&codec->lock);
	INIT_LIST_HEAD(&codec->list);
	return codec;
}

EXPORT_SYMBOL(ac97_alloc_codec);

/**
 *	ac97_release_codec -	Release an AC97 codec
 *	@codec: codec to release
 *
 *	Release an allocated AC97 codec. This will be refcounted in
 *	time but for the moment is trivial. Calls the unregister
 *	handler if the codec is now defunct.
 */
 
void ac97_release_codec(struct ac97_codec *codec)
{
	/* Remove from the list first, we don't want to be
	   "rediscovered" */
	down(&codec_sem);
	list_del(&codec->list);
	up(&codec_sem);
	/*
	 *	The driver needs to deal with internal
	 *	locking to avoid accidents here. 
	 */
	if(codec->driver)
		codec->driver->remove(codec, codec->driver);
	kfree(codec);
}

EXPORT_SYMBOL(ac97_release_codec);

/**
 *	ac97_probe_codec - Initialize and setup AC97-compatible codec
 *	@codec: (in/out) Kernel info for a single AC97 codec
 *
 *	Reset the AC97 codec, then initialize the mixer and
 *	the rest of the @codec structure.
 *
 *	The codec_read and codec_write fields of @codec are
 *	required to be setup and working when this function
 *	is called.  All other fields are set by this function.
 *
 *	codec_wait field of @codec can optionally be provided
 *	when calling this function.  If codec_wait is not %NULL,
 *	this function will call codec_wait any time it is
 *	necessary to wait for the audio chip to reach the
 *	codec-ready state.  If codec_wait is %NULL, then
 *	the default behavior is to call schedule_timeout.
 *	Currently codec_wait is used to wait for AC97 codec
 *	reset to complete. 
 *
 *	Returns 1 (true) on success, or 0 (false) on failure.
 */
 
int ac97_probe_codec(struct ac97_codec *codec)
{
	u16 id1, id2;
	u16 audio;
	int i;
	char cidbuf[CODEC_ID_BUFSZ];
	u16 f;
	struct list_head *l;
	struct ac97_driver *d;
	
	/* probing AC97 codec, AC97 2.0 says that bit 15 of register 0x00 (reset) should 
	 * be read zero.
	 *
	 * FIXME: is the following comment outdated?  -jgarzik 
	 * Probing of AC97 in this way is not reliable, it is not even SAFE !!
	 */
	codec->codec_write(codec, AC97_RESET, 0L);

	/* also according to spec, we wait for codec-ready state */	
	if (codec->codec_wait)
		codec->codec_wait(codec);
	else
		udelay(10);

	if ((audio = codec->codec_read(codec, AC97_RESET)) & 0x8000) {
		printk(KERN_ERR "ac97_codec: %s ac97 codec not present\n",
		       (codec->id & 0x2) ? (codec->id&1 ? "4th" : "Tertiary") 
		       : (codec->id&1 ? "Secondary":  "Primary"));
		return 0;
	}

	/* probe for Modem Codec */
	codec->modem = ac97_check_modem(codec);
	codec->name = NULL;
	codec->codec_ops = &default_ops;

	id1 = codec->codec_read(codec, AC97_VENDOR_ID1);
	id2 = codec->codec_read(codec, AC97_VENDOR_ID2);
	for (i = 0; i < ARRAY_SIZE(ac97_codec_ids); i++) {
		if (ac97_codec_ids[i].id == ((id1 << 16) | id2)) {
			codec->type = ac97_codec_ids[i].id;
			codec->name = ac97_codec_ids[i].name;
			codec->codec_ops = ac97_codec_ids[i].ops;
			codec->flags = ac97_codec_ids[i].flags;
			break;
		}
	}

	codec->model = (id1 << 16) | id2;
	
	f = codec->codec_read(codec, AC97_EXTENDED_STATUS);
	if(f & 4)
		codec->codec_ops = &default_digital_ops;
	
	/* A device which thinks its a modem but isnt */
	if(codec->flags & AC97_DELUDED_MODEM)
		codec->modem = 0;
		
	if (codec->name == NULL)
		codec->name = "Unknown";
	printk(KERN_INFO "ac97_codec: AC97 %s codec, id: %s (%s)\n", 
		codec->modem ? "Modem" : (audio ? "Audio" : ""),
	       codec_id(id1, id2, cidbuf), codec->name);

	if(!ac97_init_mixer(codec))
		return 0;
		
	/* 
	 *	Attach last so the caller can override the mixer
	 *	callbacks.
	 */
	 
	down(&codec_sem);
	list_add(&codec->list, &codecs);

	list_for_each(l, &codec_drivers) {
		d = list_entry(l, struct ac97_driver, list);
		if ((codec->model ^ d->codec_id) & d->codec_mask)
			continue;
		if(d->probe(codec, d) == 0)
		{
			codec->driver = d;
			break;
		}
	}

	up(&codec_sem);
	return 1;
}

static int ac97_init_mixer(struct ac97_codec *codec)
{
	u16 cap;
	int i;

	cap = codec->codec_read(codec, AC97_RESET);

	/* mixer masks */
	codec->supported_mixers = AC97_SUPPORTED_MASK;
	codec->stereo_mixers = AC97_STEREO_MASK;
	codec->record_sources = AC97_RECORD_MASK;
	if (!(cap & 0x04))
		codec->supported_mixers &= ~(SOUND_MASK_BASS|SOUND_MASK_TREBLE);
	if (!(cap & 0x10))
		codec->supported_mixers &= ~SOUND_MASK_ALTPCM;


	/* detect bit resolution */
	codec->codec_write(codec, AC97_MASTER_VOL_STEREO, 0x2020);
	if(codec->codec_read(codec, AC97_MASTER_VOL_STEREO) == 0x2020)
		codec->bit_resolution = 6;
	else
		codec->bit_resolution = 5;

	/* generic OSS to AC97 wrapper */
	codec->read_mixer = ac97_read_mixer;
	codec->write_mixer = ac97_write_mixer;
	codec->recmask_io = ac97_recmask_io;
	codec->mixer_ioctl = ac97_mixer_ioctl;

	/* codec specific initialization for 4-6 channel output or secondary codec stuff */
	if (codec->codec_ops->init != NULL) {
		codec->codec_ops->init(codec);
	}

	/* initialize mixer channel volumes */
	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		struct mixer_defaults *md = &mixer_defaults[i];
		if (md->mixer == -1) 
			break;
		if (!supported_mixer(codec, md->mixer)) 
			continue;
		ac97_set_mixer(codec, md->mixer, md->value);
	}

	/*
	 *	Volume is MUTE only on this device. We have to initialise
	 *	it but its useless beyond that.
	 */
	if(codec->flags & AC97_NO_PCM_VOLUME)
	{
		codec->supported_mixers &= ~SOUND_MASK_PCM;
		printk(KERN_WARNING "AC97 codec does not have proper volume support.\n");
	}
	return 1;
}

#define AC97_SIGMATEL_ANALOG    0x6c	/* Analog Special */
#define AC97_SIGMATEL_DAC2INVERT 0x6e
#define AC97_SIGMATEL_BIAS1     0x70
#define AC97_SIGMATEL_BIAS2     0x72
#define AC97_SIGMATEL_MULTICHN  0x74	/* Multi-Channel programming */
#define AC97_SIGMATEL_CIC1      0x76
#define AC97_SIGMATEL_CIC2      0x78


static int sigmatel_9708_init(struct ac97_codec * codec)
{
	u16 codec72, codec6c;

	codec72 = codec->codec_read(codec, AC97_SIGMATEL_BIAS2) & 0x8000;
	codec6c = codec->codec_read(codec, AC97_SIGMATEL_ANALOG);

	if ((codec72==0) && (codec6c==0)) {
		codec->codec_write(codec, AC97_SIGMATEL_CIC1, 0xabba);
		codec->codec_write(codec, AC97_SIGMATEL_CIC2, 0x1000);
		codec->codec_write(codec, AC97_SIGMATEL_BIAS1, 0xabba);
		codec->codec_write(codec, AC97_SIGMATEL_BIAS2, 0x0007);
	} else if ((codec72==0x8000) && (codec6c==0)) {
		codec->codec_write(codec, AC97_SIGMATEL_CIC1, 0xabba);
		codec->codec_write(codec, AC97_SIGMATEL_CIC2, 0x1001);
		codec->codec_write(codec, AC97_SIGMATEL_DAC2INVERT, 0x0008);
	} else if ((codec72==0x8000) && (codec6c==0x0080)) {
		/* nothing */
	}
	codec->codec_write(codec, AC97_SIGMATEL_MULTICHN, 0x0000);
	return 0;
}


static int sigmatel_9721_init(struct ac97_codec * codec)
{
	/* Only set up secondary codec */
	if (codec->id == 0)
		return 0;

	codec->codec_write(codec, AC97_SURROUND_MASTER, 0L);

	/* initialize SigmaTel STAC9721/23 as secondary codec, decoding AC link
	   sloc 3,4 = 0x01, slot 7,8 = 0x00, */
	codec->codec_write(codec, AC97_SIGMATEL_MULTICHN, 0x00);

	/* we don't have the crystal when we are on an AMR card, so use
	   BIT_CLK as our clock source. Write the magic word ABBA and read
	   back to enable register 0x78 */
	codec->codec_write(codec, AC97_SIGMATEL_CIC1, 0xabba);
	codec->codec_read(codec, AC97_SIGMATEL_CIC1);

	/* sync all the clocks*/
	codec->codec_write(codec, AC97_SIGMATEL_CIC2, 0x3802);

	return 0;
}


static int sigmatel_9744_init(struct ac97_codec * codec)
{
	// patch for SigmaTel
	codec->codec_write(codec, AC97_SIGMATEL_CIC1, 0xabba);
	codec->codec_write(codec, AC97_SIGMATEL_CIC2, 0x0000); // is this correct? --jk
	codec->codec_write(codec, AC97_SIGMATEL_BIAS1, 0xabba);
	codec->codec_write(codec, AC97_SIGMATEL_BIAS2, 0x0002);
	codec->codec_write(codec, AC97_SIGMATEL_MULTICHN, 0x0000);
	return 0;
}

static int cmedia_init(struct ac97_codec *codec)
{
	/* Initialise the CMedia 9739 */
	/*
		We could set various options here
		Register 0x20 bit 0x100 sets mic as center bass
		Also do multi_channel_ctrl &=~0x3000 |=0x1000
		
		For now we set up the GPIO and PC beep 
	*/
	
	u16 v;
	
	/* MIC */
	codec->codec_write(codec, 0x64, 0x3000);
	v = codec->codec_read(codec, 0x64);
	v &= ~0x8000;
	codec->codec_write(codec, 0x64, v);
	codec->codec_write(codec, 0x70, 0x0100);
	codec->codec_write(codec, 0x72, 0x0020);
	return 0;
}
	
#define AC97_WM97XX_FMIXER_VOL 0x72
#define AC97_WM97XX_RMIXER_VOL 0x74
#define AC97_WM97XX_TEST 0x5a
#define AC97_WM9704_RPCM_VOL 0x70
#define AC97_WM9711_OUT3VOL 0x16

static int wolfson_init03(struct ac97_codec * codec)
{
	/* this is known to work for the ViewSonic ViewPad 1000 */
	codec->codec_write(codec, AC97_WM97XX_FMIXER_VOL, 0x0808);
	codec->codec_write(codec, AC97_GENERAL_PURPOSE, 0x8000);
	return 0;
}

static int wolfson_init04(struct ac97_codec * codec)
{
	codec->codec_write(codec, AC97_WM97XX_FMIXER_VOL, 0x0808);
	codec->codec_write(codec, AC97_WM97XX_RMIXER_VOL, 0x0808);

	// patch for DVD noise
	codec->codec_write(codec, AC97_WM97XX_TEST, 0x0200);

	// init vol as PCM vol
	codec->codec_write(codec, AC97_WM9704_RPCM_VOL,
		codec->codec_read(codec, AC97_PCMOUT_VOL));

	/* set rear surround volume */
	codec->codec_write(codec, AC97_SURROUND_MASTER, 0x0000);
	return 0;
}

/* WM9705, WM9710 */
static int wolfson_init05(struct ac97_codec * codec)
{
	/* set front mixer volume */
	codec->codec_write(codec, AC97_WM97XX_FMIXER_VOL, 0x0808);
	return 0;
}

/* WM9711, WM9712 */
static int wolfson_init11(struct ac97_codec * codec)
{
	/* stop pop's during suspend/resume */
	codec->codec_write(codec, AC97_WM97XX_TEST, codec->codec_read(codec, AC97_WM97XX_TEST) & 0xffbf);

	/* set out3 volume */
	codec->codec_write(codec, AC97_WM9711_OUT3VOL, 0x0808);
	return 0;
}

static int tritech_init(struct ac97_codec * codec)
{
	codec->codec_write(codec, 0x26, 0x0300);
	codec->codec_write(codec, 0x26, 0x0000);
	codec->codec_write(codec, AC97_SURROUND_MASTER, 0x0000);
	codec->codec_write(codec, AC97_RESERVED_3A, 0x0000);
	return 0;
}


/* copied from drivers/sound/maestro.c */
static int tritech_maestro_init(struct ac97_codec * codec)
{
	/* no idea what this does */
	codec->codec_write(codec, 0x2A, 0x0001);
	codec->codec_write(codec, 0x2C, 0x0000);
	codec->codec_write(codec, 0x2C, 0XFFFF);
	return 0;
}



/* 
 *	Presario700 workaround 
 * 	for Jack Sense/SPDIF Register mis-setting causing
 *	no audible output
 *	by Santiago Nullo 04/05/2002
 */

#define AC97_AD1886_JACK_SENSE 0x72

static int ad1886_init(struct ac97_codec * codec)
{
	/* from AD1886 Specs */
	codec->codec_write(codec, AC97_AD1886_JACK_SENSE, 0x0010);
	return 0;
}




/*
 *	This is basically standard AC97. It should work as a default for
 *	almost all modern codecs. Note that some cards wire EAPD *backwards*
 *	That side of it is up to the card driver not us to cope with.
 *
 */

static int eapd_control(struct ac97_codec * codec, int on)
{
	if(on)
		codec->codec_write(codec, AC97_POWER_CONTROL,
			codec->codec_read(codec, AC97_POWER_CONTROL)|0x8000);
	else
		codec->codec_write(codec, AC97_POWER_CONTROL,
			codec->codec_read(codec, AC97_POWER_CONTROL)&~0x8000);
	return 0;
}

static int generic_digital_control(struct ac97_codec *codec, int slots, int rate, int mode)
{
	u16 reg;
	
	reg = codec->codec_read(codec, AC97_SPDIF_CONTROL);
	
	switch(rate)
	{
		/* Off by default */
		default:
		case 0:
			reg = codec->codec_read(codec, AC97_EXTENDED_STATUS);
			codec->codec_write(codec, AC97_EXTENDED_STATUS, (reg & ~AC97_EA_SPDIF));
			if(rate == 0)
				return 0;
			return -EINVAL;
		case 1:
			reg = (reg & AC97_SC_SPSR_MASK) | AC97_SC_SPSR_48K;
			break;
		case 2:
			reg = (reg & AC97_SC_SPSR_MASK) | AC97_SC_SPSR_44K;
			break;
		case 3:
			reg = (reg & AC97_SC_SPSR_MASK) | AC97_SC_SPSR_32K;
			break;
	}
	
	reg &= ~AC97_SC_CC_MASK;
	reg |= (mode & AUDIO_CCMASK) << 6;
	
	if(mode & AUDIO_DIGITAL)
		reg |= 2;
	if(mode & AUDIO_PRO)
		reg |= 1;
	if(mode & AUDIO_DRS)
		reg |= 0x4000;

	codec->codec_write(codec, AC97_SPDIF_CONTROL, reg);

	reg = codec->codec_read(codec, AC97_EXTENDED_STATUS);
	reg &= (AC97_EA_SLOT_MASK);
	reg |= AC97_EA_VRA | AC97_EA_SPDIF | slots;
	codec->codec_write(codec, AC97_EXTENDED_STATUS, reg);
	
	reg = codec->codec_read(codec, AC97_EXTENDED_STATUS);
	if(!(reg & 0x0400))
	{
		codec->codec_write(codec, AC97_EXTENDED_STATUS, reg & ~ AC97_EA_SPDIF);
		return -EINVAL;
	}
	return 0;
}

/*
 *	Crystal digital audio control (CS4299)
 */
 
static int crystal_digital_control(struct ac97_codec *codec, int slots, int rate, int mode)
{
	u16 cv;

	if(mode & AUDIO_DIGITAL)
		return -EINVAL;
		
	switch(rate)
	{
		case 0: cv = 0x0; break;	/* SPEN off */
		case 48000: cv = 0x8004; break;	/* 48KHz digital */
		case 44100: cv = 0x8104; break;	/* 44.1KHz digital */
		case 32768: 			/* 32Khz */
		default:
			return -EINVAL;
	}
	codec->codec_write(codec, 0x68, cv);
	return 0;
}

/*
 *	CMedia digital audio control
 *	Needs more work.
 */
 
static int cmedia_digital_control(struct ac97_codec *codec, int slots, int rate, int mode)
{
	u16 cv;

	if(mode & AUDIO_DIGITAL)
		return -EINVAL;
		
	switch(rate)
	{
		case 0:		cv = 0x0001; break;	/* SPEN off */
		case 48000:	cv = 0x0009; break;	/* 48KHz digital */
		default:
			return -EINVAL;
	}
	codec->codec_write(codec, 0x2A, 0x05c4);
	codec->codec_write(codec, 0x6C, cv);
	
	/* Switch on mix to surround */
	cv = codec->codec_read(codec, 0x64);
	cv &= ~0x0200;
	if(mode)
		cv |= 0x0200;
	codec->codec_write(codec, 0x64, cv);
	return 0;
}


/* copied from drivers/sound/maestro.c */
#if 0  /* there has been 1 person on the planet with a pt101 that we
        know of.  If they care, they can put this back in :) */
static int pt101_init(struct ac97_codec * codec)
{
	printk(KERN_INFO "ac97_codec: PT101 Codec detected, initializing but _not_ installing mixer device.\n");
	/* who knows.. */
	codec->codec_write(codec, 0x2A, 0x0001);
	codec->codec_write(codec, 0x2C, 0x0000);
	codec->codec_write(codec, 0x2C, 0xFFFF);
	codec->codec_write(codec, 0x10, 0x9F1F);
	codec->codec_write(codec, 0x12, 0x0808);
	codec->codec_write(codec, 0x14, 0x9F1F);
	codec->codec_write(codec, 0x16, 0x9F1F);
	codec->codec_write(codec, 0x18, 0x0404);
	codec->codec_write(codec, 0x1A, 0x0000);
	codec->codec_write(codec, 0x1C, 0x0000);
	codec->codec_write(codec, 0x02, 0x0404);
	codec->codec_write(codec, 0x04, 0x0808);
	codec->codec_write(codec, 0x0C, 0x801F);
	codec->codec_write(codec, 0x0E, 0x801F);
	return 0;
}
#endif
	

EXPORT_SYMBOL(ac97_read_proc);
EXPORT_SYMBOL(ac97_probe_codec);

/*
 *	AC97 library support routines
 */	
 
/**
 *	ac97_set_dac_rate	-	set codec rate adaption
 *	@codec: ac97 code
 *	@rate: rate in hertz
 *
 *	Set the DAC rate. Assumes the codec supports VRA. The caller is
 *	expected to have checked this little detail.
 */
 
unsigned int ac97_set_dac_rate(struct ac97_codec *codec, unsigned int rate)
{
	unsigned int new_rate = rate;
	u32 dacp;
	u32 mast_vol, phone_vol, mono_vol, pcm_vol;
	u32 mute_vol = 0x8000;	/* The mute volume? */

	if(rate != codec->codec_read(codec, AC97_PCM_FRONT_DAC_RATE))
	{
		/* Mute several registers */
		mast_vol = codec->codec_read(codec, AC97_MASTER_VOL_STEREO);
		mono_vol = codec->codec_read(codec, AC97_MASTER_VOL_MONO);
		phone_vol = codec->codec_read(codec, AC97_HEADPHONE_VOL);
		pcm_vol = codec->codec_read(codec, AC97_PCMOUT_VOL);
		codec->codec_write(codec, AC97_MASTER_VOL_STEREO, mute_vol);
		codec->codec_write(codec, AC97_MASTER_VOL_MONO, mute_vol);
		codec->codec_write(codec, AC97_HEADPHONE_VOL, mute_vol);
		codec->codec_write(codec, AC97_PCMOUT_VOL, mute_vol);
		
		/* Power down the DAC */
		dacp=codec->codec_read(codec, AC97_POWER_CONTROL);
		codec->codec_write(codec, AC97_POWER_CONTROL, dacp|0x0200);
		/* Load the rate and read the effective rate */
		codec->codec_write(codec, AC97_PCM_FRONT_DAC_RATE, rate);
		new_rate=codec->codec_read(codec, AC97_PCM_FRONT_DAC_RATE);
		/* Power it back up */
		codec->codec_write(codec, AC97_POWER_CONTROL, dacp);

		/* Restore volumes */
		codec->codec_write(codec, AC97_MASTER_VOL_STEREO, mast_vol);
		codec->codec_write(codec, AC97_MASTER_VOL_MONO, mono_vol);
		codec->codec_write(codec, AC97_HEADPHONE_VOL, phone_vol);
		codec->codec_write(codec, AC97_PCMOUT_VOL, pcm_vol);
	}
	return new_rate;
}

EXPORT_SYMBOL(ac97_set_dac_rate);

/**
 *	ac97_set_adc_rate	-	set codec rate adaption
 *	@codec: ac97 code
 *	@rate: rate in hertz
 *
 *	Set the ADC rate. Assumes the codec supports VRA. The caller is
 *	expected to have checked this little detail.
 */

unsigned int ac97_set_adc_rate(struct ac97_codec *codec, unsigned int rate)
{
	unsigned int new_rate = rate;
	u32 dacp;

	if(rate != codec->codec_read(codec, AC97_PCM_LR_ADC_RATE))
	{
		/* Power down the ADC */
		dacp=codec->codec_read(codec, AC97_POWER_CONTROL);
		codec->codec_write(codec, AC97_POWER_CONTROL, dacp|0x0100);
		/* Load the rate and read the effective rate */
		codec->codec_write(codec, AC97_PCM_LR_ADC_RATE, rate);
		new_rate=codec->codec_read(codec, AC97_PCM_LR_ADC_RATE);
		/* Power it back up */
		codec->codec_write(codec, AC97_POWER_CONTROL, dacp);
	}
	return new_rate;
}

EXPORT_SYMBOL(ac97_set_adc_rate);

int ac97_save_state(struct ac97_codec *codec)
{
	return 0;	
}

EXPORT_SYMBOL(ac97_save_state);

int ac97_restore_state(struct ac97_codec *codec)
{
	int i;
	unsigned int left, right, val;

	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		if (!supported_mixer(codec, i)) 
			continue;

		val = codec->mixer_state[i];
		right = val >> 8;
		left = val  & 0xff;
		codec->write_mixer(codec, i, left, right);
	}
	return 0;
}

EXPORT_SYMBOL(ac97_restore_state);

/**
 *	ac97_register_driver	-	register a codec helper
 *	@driver: Driver handler
 *
 *	Register a handler for codecs matching the codec id. The handler
 *	attach function is called for all present codecs and will be 
 *	called when new codecs are discovered.
 */
 
int ac97_register_driver(struct ac97_driver *driver)
{
	struct list_head *l;
	struct ac97_codec *c;
	
	down(&codec_sem);
	INIT_LIST_HEAD(&driver->list);
	list_add(&driver->list, &codec_drivers);
	
	list_for_each(l, &codecs)
	{
		c = list_entry(l, struct ac97_codec, list);
		if(c->driver != NULL || ((c->model ^ driver->codec_id) & driver->codec_mask))
			continue;
		if(driver->probe(c, driver))
			continue;
		c->driver = driver;
	}
	up(&codec_sem);
	return 0;
}

EXPORT_SYMBOL_GPL(ac97_register_driver);

/**
 *	ac97_unregister_driver	-	unregister a codec helper
 *	@driver: Driver handler
 *
 *	Register a handler for codecs matching the codec id. The handler
 *	attach function is called for all present codecs and will be 
 *	called when new codecs are discovered.
 */
 
void ac97_unregister_driver(struct ac97_driver *driver)
{
	struct list_head *l;
	struct ac97_codec *c;
	
	down(&codec_sem);
	list_del_init(&driver->list);
	
	list_for_each(l, &codecs)
	{
		c = list_entry(l, struct ac97_codec, list);
		if(c->driver == driver)
			driver->remove(c, driver);
		c->driver = NULL;
	}
	
	up(&codec_sem);
}

EXPORT_SYMBOL_GPL(ac97_unregister_driver);

static int swap_headphone(int remove_master)
{
	struct list_head *l;
	struct ac97_codec *c;
	
	if (remove_master) {
		down(&codec_sem);
		list_for_each(l, &codecs)
		{
			c = list_entry(l, struct ac97_codec, list);
			if (supported_mixer(c, SOUND_MIXER_PHONEOUT))
				c->supported_mixers &= ~SOUND_MASK_PHONEOUT;
		}
		up(&codec_sem);
	} else
		ac97_hw[SOUND_MIXER_PHONEOUT].offset = AC97_MASTER_VOL_STEREO;

	/* Scale values already match */
	ac97_hw[SOUND_MIXER_VOLUME].offset = AC97_MASTER_VOL_MONO;
	return 0;
}

static int apply_quirk(int quirk)
{
	switch (quirk) {
	case AC97_TUNE_NONE:
		return 0;
	case AC97_TUNE_HP_ONLY:
		return swap_headphone(1);
	case AC97_TUNE_SWAP_HP:
		return swap_headphone(0);
	case AC97_TUNE_SWAP_SURROUND:
		return -ENOSYS; /* not yet implemented */
	case AC97_TUNE_AD_SHARING:
		return -ENOSYS; /* not yet implemented */
	case AC97_TUNE_ALC_JACK:
		return -ENOSYS; /* not yet implemented */
	}
	return -EINVAL;
}

/**
 *	ac97_tune_hardware - tune up the hardware
 *	@pdev: pci_dev pointer
 *	@quirk: quirk list
 *	@override: explicit quirk value (overrides if not AC97_TUNE_DEFAULT)
 *	
 *	Do some workaround for each pci device, such as renaming of the
 *	headphone (true line-out) control as "Master".
 *	The quirk-list must be terminated with a zero-filled entry.
 *	
 *	Returns zero if successful, or a negative error code on failure.
 */

int ac97_tune_hardware(struct pci_dev *pdev, struct ac97_quirk *quirk, int override)
{
	int result;

	if (!quirk)
		return -EINVAL;

	if (override != AC97_TUNE_DEFAULT) {
		result = apply_quirk(override);
		if (result < 0)
			printk(KERN_ERR "applying quirk type %d failed (%d)\n", override, result);
		return result;
	}

	for (; quirk->vendor; quirk++) {
		if (quirk->vendor != pdev->subsystem_vendor)
			continue;
		if ((! quirk->mask && quirk->device == pdev->subsystem_device) ||
		    quirk->device == (quirk->mask & pdev->subsystem_device)) {
#ifdef DEBUG
			printk("ac97 quirk for %s (%04x:%04x)\n", quirk->name, ac97->subsystem_vendor, pdev->subsystem_device);
#endif
			result = apply_quirk(quirk->type);
			if (result < 0)
				printk(KERN_ERR "applying quirk type %d for %s failed (%d)\n", quirk->type, quirk->name, result);
			return result;
		}
	}
	return 0;
}

EXPORT_SYMBOL_GPL(ac97_tune_hardware);

MODULE_LICENSE("GPL");

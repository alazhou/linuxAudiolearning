/*
 * sound\soc\sunxi\sun8iw11_codec.c
 * (C) Copyright 2014-2017
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * huangxin <huangxin@allwinnertech.com>
 * wolfgang huang <huangjinhui@allwinnertech.com>
 *
 * some simple description for this code
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/pm.h>
#include <linux/regulator/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <linux/sys_config.h>

#include "sunxi_rw_func.h"
#include "sun8iw11_codec.h"

#define	SUNXI_DAPM_HPPA
#define	DRV_NAME	"sunxi-internal-codec"

struct codec_hw_config {
	u32 adcagc_cfg:1;
	u32 adcdrc_cfg:1;
	u32 dacdrc_cfg:1;
	u32 adchpf_cfg:1;
	u32 dachpf_cfg:1;
};

struct sunxi_codec {
	struct device *dev;
	struct regmap *regmap;
	void __iomem *analogbase;
	struct clk *pllclk;
	struct clk *moduleclk;
	/* self user config params */
	u32 headphonevol;
	u32 maingain;
	u32 spkervol;
	u32 pa_sleep_time;
	u32 spk_gpio;
	bool hp_dirused;
	bool spk_gpio_used;
	struct codec_hw_config hwconfig;

};

struct sample_rate {
	unsigned int samplerate;
	unsigned int rate_bit;
};

static const struct sample_rate sample_rate_conv[] = {
	{44100, 0},
	{48000, 0},
	{8000, 5},
	{32000, 1},
	{22050, 2},
	{24000, 2},
	{16000, 3},
	{11025, 4},
	{12000, 4},
	{192000, 6},
	{96000, 7},
};

static const DECLARE_TLV_DB_SCALE(digital_tlv, -7424, 116, 0);
static const DECLARE_TLV_DB_SCALE(headphone_tlv, -6300, 100, 1);
static const DECLARE_TLV_DB_SCALE(linein_tlv, -450, 150, 0);
static const DECLARE_TLV_DB_SCALE(fm_tlv, -450, 150, 0);
static const DECLARE_TLV_DB_SCALE(mic_gain_tlv, -450, 150, 0);
static const DECLARE_TLV_DB_SCALE(phoneout_tlv, -450, 150, 0);
static const DECLARE_TLV_DB_SCALE(adc_gain_tlv, -450, 150, 0);
static const unsigned int mic_boost_tlv[] = {
	TLV_DB_RANGE_HEAD(2),
	0, 0, TLV_DB_SCALE_ITEM(0, 0, 0),
	1, 7, TLV_DB_SCALE_ITEM(2400, 300, 0),
};

static void adcagc_config(struct snd_soc_codec *codec)
{
}

static void adcdrc_config(struct snd_soc_codec *codec)
{
}

static void adchpf_config(struct snd_soc_codec *codec)
{
	snd_soc_update_bits(codec, SUNXI_ADC_DRC_HHPFC, (0xFF<<ADC_HHPF_CONF), (0xFF<<ADC_HHPF_CONF));
	snd_soc_update_bits(codec, SUNXI_ADC_DRC_LHPFC, (0xFFFF<<ADC_LHPF_CONF), (0xFAC1<<ADC_LHPF_CONF));
}

static void adchpf_enable(struct snd_soc_codec *codec, bool on)
{
	if (on) {
		snd_soc_update_bits(codec, SUNXI_ADC_DAP_CTR, (0x01<<ENADC_DRC), (0x01<<ENADC_DRC));
		snd_soc_update_bits(codec, SUNXI_ADC_DAP_CTR, (0x01<<ADC_DRC_HPF_EN), (0x01<<ADC_DRC_HPF_EN));
	} else {
		snd_soc_update_bits(codec, SUNXI_ADC_DAP_CTR, (0x01<<ENADC_DRC), (0x0<<ENADC_DRC));
		snd_soc_update_bits(codec, SUNXI_ADC_DAP_CTR, (0x01<<ADC_DRC_HPF_EN), (0x0<<ADC_DRC_HPF_EN));
	}
}

static void dacdrc_config(struct snd_soc_codec *codec)
{
}

static void dachpf_config(struct snd_soc_codec *codec)
{
}

static void adcdrc_enable(struct snd_soc_codec *codec, bool on)
{
	if (on) {
	} else {
	}
}

static void dacdrc_enable(struct snd_soc_codec *codec, bool on)
{
	if (on) {
	} else {
	}
}

static void adcagc_enable(struct snd_soc_codec *codec, bool on)
{
	if (on) {
	} else {
	}
}

static void dachpf_enable(struct snd_soc_codec *codec, bool on)
{
	if (on) {
	} else {
	}
}

static int sunxi_codec_get_hub_mode(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int reg_val;

	reg_val = snd_soc_read(codec, SUNXI_DAC_DPC);

	ucontrol->value.integer.value[0] = ((reg_val & (1<<HUB_EN)) ? 2 : 1);
	return 0;
}

static int sunxi_codec_set_hub_mode(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	switch (ucontrol->value.integer.value[0]) {
	case	0:
	case	1:
		snd_soc_update_bits(codec, SUNXI_DAC_DPC,
				(0x1<<HUB_EN), (0x0<<HUB_EN));
		break;
	case	2:
		snd_soc_update_bits(codec, SUNXI_DAC_DPC,
				(0x1<<HUB_EN), (0x1<<HUB_EN));
		break;
	default:
		return -EINVAL;
	}
	return 0;
}


/* sunxi codec hub mdoe select */
static const char *sunxi_codec_hub_function[] = {"null",
			"hub_disable", "hub_enable"};

static const struct soc_enum sunxi_codec_hub_mode_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sunxi_codec_hub_function),
			sunxi_codec_hub_function),
};

#ifdef	SUNXI_DAPM_HPPA
static int sunxi_hppa_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct sunxi_codec *sunxi_internal_codec =
			snd_soc_codec_get_drvdata(codec);
	switch (event) {
	case	SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, SUNXI_PAEN_HP_CTR,
				(1<<HPPAEN), (1<<HPPAEN));
		/* time delay to wait headphone pa work fine */
		mdelay(sunxi_internal_codec->pa_sleep_time + 150);
		break;
	case	SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, SUNXI_PAEN_HP_CTR,
				(1<<HPPAEN), (0<<HPPAEN));
		break;
	default:
		break;
	}
	return 0;
}
#else
static inline int sunxi_hppa_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *k, int event)
{
	return 0;
}
#endif

static int sunxi_spkpa_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	struct sunxi_codec *sunxi_internal_codec =
			snd_soc_codec_get_drvdata(w->codec);

	switch (event) {
	case	SND_SOC_DAPM_POST_PMU:
		if (sunxi_internal_codec->spk_gpio_used) {
			gpio_set_value(sunxi_internal_codec->spk_gpio, 1);
			/* time delay to wait spk pa work fine, general setting 50ms */
			mdelay(50);
		}
		break;
	case	SND_SOC_DAPM_PRE_PMD:
		if (sunxi_internal_codec->spk_gpio_used)
			gpio_set_value(sunxi_internal_codec->spk_gpio, 0);
		break;
	default:
		break;
	}
	return 0;
}

static int sunxi_playback_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	struct snd_soc_codec *codec = w->codec;
	switch (event) {
	case	SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, SUNXI_DAC_DPC,
				(0x1<<EN_DAC), (0x1<<EN_DAC));
		break;
	case	SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(codec, SUNXI_DAC_DPC,
				(0x1<<EN_DAC), (0x0<<EN_DAC));
		break;
	default:
		break;
	}
	return 0;
}

static int sunxi_capture_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	struct snd_soc_codec *codec = w->codec;
	switch (event) {
	case	SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, SUNXI_ADC_FIFO_CTR,
				(0x1<<EN_AD), (0x1<<EN_AD));
		break;
	case	SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(codec, SUNXI_ADC_FIFO_CTR,
				(0x1<<EN_AD), (0x0<<EN_AD));
		break;
	default:
		break;
	}
	return 0;
}

static const struct snd_kcontrol_new sunxi_codec_controls[] = {
	SOC_ENUM_EXT("codec hub mode" , sunxi_codec_hub_mode_enum[0],
				sunxi_codec_get_hub_mode,
				sunxi_codec_set_hub_mode),
	SOC_SINGLE_TLV("digital volume", SUNXI_DAC_DPC,
					DVOL, 0x3F, 1, digital_tlv),
	SOC_SINGLE_TLV("Headphone volume", SUNXI_HP_VOLC,
					HPVOL, 0x3F, 0, headphone_tlv),
	SOC_DOUBLE_TLV("LINEIN Mixer volume", SUNXI_LINEIN_GCTR,
				LINEINLG, LINEINRG, 0x7, 0, linein_tlv),
	SOC_SINGLE_TLV("FM gain volume", SUNXI_FM_GCTR, FMG, 0x7, 0, fm_tlv),
	SOC_SINGLE_TLV("LINEIN gain volume", SUNXI_FM_GCTR,
					LINEING, 0x7, 0, linein_tlv),
	SOC_DOUBLE_TLV("MIC gain volume", SUNXI_MICIN_GCTR,
				MIC1_GAIN, MIC2_GAIN, 0x7, 0, mic_gain_tlv),
	SOC_SINGLE_TLV("phoneout volume", SUNXI_PHONEOUT_CTR,
				PHONEOUTG, 0x7, 0, phoneout_tlv),
	SOC_SINGLE_TLV("MIC1 boost volume", SUNXI_MIC1G_MICBIAS_CTR,
				MIC1_BOOST, 0x7, 0, mic_boost_tlv),
	SOC_SINGLE_TLV("MIC2 boost volume", SUNXI_MIC2G_LINEEN_CTR,
				MIC2BOOST, 0x7, 0, mic_boost_tlv),
	SOC_SINGLE_TLV("ADC gain volume", SUNXI_ADC_AP_EN,
				ADCG, 0x7, 0, adc_gain_tlv),
};

static const char *hp_mux_text[] = {
	"DAC",
	"OMIX",
};

static const struct soc_enum hpr_enum =
	SOC_ENUM_SINGLE(SUNXI_DAC_PA_SRC, RHPIS, 2, hp_mux_text);

static const struct snd_kcontrol_new hpr_mux =
	SOC_DAPM_ENUM("HPR Mux", hpr_enum);

static const struct soc_enum hpl_enum =
	SOC_ENUM_SINGLE(SUNXI_DAC_PA_SRC, LHPIS, 2, hp_mux_text);

static const struct snd_kcontrol_new hpl_mux =
	SOC_DAPM_ENUM("HPL Mux", hpl_enum);

static const char *mic2_mux_text[] = {
	"MIC3IN",
	"MIC2IN",
};

static const struct soc_enum mic2_enum =
	SOC_ENUM_SINGLE(SUNXI_MIC1G_MICBIAS_CTR, MIC2_SS, 2, mic2_mux_text);

static const struct snd_kcontrol_new mic2_mux =
	SOC_DAPM_ENUM("MIC2SRC Mux", mic2_enum);

static const struct snd_kcontrol_new left_output_mixer[] = {
	SOC_DAPM_SINGLE("DACR Switch", SUNXI_LOMIX_SRC, LMIX_RDAC, 1, 0),
	SOC_DAPM_SINGLE("DACL Switch", SUNXI_LOMIX_SRC, LMIX_LDAC, 1, 0),
	SOC_DAPM_SINGLE("FML Switch", SUNXI_LOMIX_SRC, LMIX_FML, 1, 0),
	SOC_DAPM_SINGLE("LINEINL Switch", SUNXI_LOMIX_SRC , LMIX_LINEINL, 1, 0),
	SOC_DAPM_SINGLE("LINEINLR Switch", SUNXI_LOMIX_SRC, LMIX_LINEINLR, 1, 0),
	SOC_DAPM_SINGLE("MIC2 Boost Switch", SUNXI_LOMIX_SRC, LMIX_MIC2_BST, 1, 0),
	SOC_DAPM_SINGLE("MIC1 Boost Switch", SUNXI_LOMIX_SRC, LMIX_MIC1_BST, 1, 0),
};

static const struct snd_kcontrol_new right_output_mixer[] = {
	SOC_DAPM_SINGLE("DACL Switch", SUNXI_ROMIX_SRC, RMIX_LDAC, 1, 0),
	SOC_DAPM_SINGLE("DACR Switch", SUNXI_ROMIX_SRC, RMIX_RDAC, 1, 0),
	SOC_DAPM_SINGLE("FMR Switch", SUNXI_ROMIX_SRC, RMIX_FMR, 1, 0),
	SOC_DAPM_SINGLE("LINEINR Switch", SUNXI_ROMIX_SRC, RMIX_LINEINR, 1, 0),
	SOC_DAPM_SINGLE("LINEINLR Switch", SUNXI_ROMIX_SRC, RMIX_LINEINLR, 1, 0),
	SOC_DAPM_SINGLE("MIC2 Boost Switch", SUNXI_ROMIX_SRC, RMIX_MIC2_BST, 1, 0),
	SOC_DAPM_SINGLE("MIC1 Boost Switch", SUNXI_ROMIX_SRC, RMIX_MIC1_BST, 1, 0),
};

static const struct snd_kcontrol_new left_input_mixer[] = {
	SOC_DAPM_SINGLE("ROMIX Switch", SUNXI_LADCMIX_SRC, LADC_ROUT_MIX, 1, 0),
	SOC_DAPM_SINGLE("LOMIX Switch", SUNXI_LADCMIX_SRC, LADC_LOUT_MIX, 1, 0),
	SOC_DAPM_SINGLE("FML Switch", SUNXI_LADCMIX_SRC, LADC_FML, 1, 0),
	SOC_DAPM_SINGLE("LINEINL Switch", SUNXI_LADCMIX_SRC, LADC_LINEINL, 1, 0),
	SOC_DAPM_SINGLE("LINEINLR Switch", SUNXI_LADCMIX_SRC, LADC_LINEINLR, 1, 0),
	SOC_DAPM_SINGLE("MIC2 Boost Switch", SUNXI_LADCMIX_SRC, LADC_MIC2_BST, 1, 0),
	SOC_DAPM_SINGLE("MIC1 Boost Switch", SUNXI_LADCMIX_SRC, LADC_MIC1_BST, 1, 0),
};

static const struct snd_kcontrol_new right_input_mixer[] = {
	SOC_DAPM_SINGLE("LOMIX Switch", SUNXI_RADCMIX_SRC, RADC_LOUT_MIX, 1, 0),
	SOC_DAPM_SINGLE("ROMIX Switch", SUNXI_RADCMIX_SRC, RADC_ROUT_MIX, 1, 0),
	SOC_DAPM_SINGLE("FMR Switch", SUNXI_RADCMIX_SRC, RADC_FMR, 1, 0),
	SOC_DAPM_SINGLE("LINEINR Switch", SUNXI_RADCMIX_SRC, RADC_LINEINR, 1, 0),
	SOC_DAPM_SINGLE("LINEINLR Switch", SUNXI_RADCMIX_SRC, RADC_LINEINLR, 1, 0),
	SOC_DAPM_SINGLE("MIC2 Boost Switch", SUNXI_RADCMIX_SRC, RADC_MIC2_BST, 1, 0),
	SOC_DAPM_SINGLE("MIC1 Boost Switch", SUNXI_RADCMIX_SRC, RADC_MIC1_BST, 1, 0),
};

static const struct snd_kcontrol_new phoneout_mixer[] = {
	SOC_DAPM_SINGLE("LOMIX Switch", SUNXI_PHONEOUT_CTR, PHONEOUTS0, 1, 0),
	SOC_DAPM_SINGLE("ROMIX Switch", SUNXI_PHONEOUT_CTR, PHONEOUTS1, 1, 0),
	SOC_DAPM_SINGLE("MIC2 Boost Switch", SUNXI_PHONEOUT_CTR, PHONEOUTS2, 1, 0),
	SOC_DAPM_SINGLE("MIC1 Boost Switch", SUNXI_PHONEOUT_CTR, PHONEOUTS3, 1, 0),
};

static const struct snd_soc_dapm_widget sunxi_codec_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN_E("DACL", "Playback", 0, SUNXI_DAC_PA_SRC, DACALEN, 0,
				sunxi_playback_event,
				SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_IN_E("DACR", "Playback", 0, SUNXI_DAC_PA_SRC, DACAREN, 0,
				sunxi_playback_event,
				SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_OUT_E("ADCL", "Capture", 0, SUNXI_ADC_AP_EN, ADCLEN, 0,
				sunxi_capture_event,
				SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_OUT_E("ADCR", "Capture", 0, SUNXI_ADC_AP_EN, ADCREN, 0,
				sunxi_capture_event,
				SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MIXER("Left Output Mixer", SUNXI_DAC_PA_SRC, LMIXEN, 0,
			left_output_mixer, ARRAY_SIZE(left_output_mixer)),
	SND_SOC_DAPM_MIXER("Right Output Mixer", SUNXI_DAC_PA_SRC, RMIXEN, 0,
			right_output_mixer, ARRAY_SIZE(right_output_mixer)),
	SND_SOC_DAPM_MIXER("Left Input Mixer", SND_SOC_NOPM, 0, 0,
			left_input_mixer, ARRAY_SIZE(left_input_mixer)),
	SND_SOC_DAPM_MIXER("Right Input Mixer", SND_SOC_NOPM, 0, 0,
			right_input_mixer, ARRAY_SIZE(right_input_mixer)),
	SND_SOC_DAPM_MIXER("Phone Out Mixer", SUNXI_PHONEOUT_CTR, PHONEOUTEN, 0,
			phoneout_mixer, ARRAY_SIZE(phoneout_mixer)),

	SND_SOC_DAPM_MUX("HPR Mux", SUNXI_DAC_PA_SRC, RHPPAMUTE, 0, &hpr_mux),
	SND_SOC_DAPM_MUX("HPL Mux", SUNXI_DAC_PA_SRC, LHPPAMUTE, 0, &hpl_mux),
	SND_SOC_DAPM_PGA("MIC1 PGA", SUNXI_MIC1G_MICBIAS_CTR,
			MIC1_AMPEN, 0, NULL, 0),
	SND_SOC_DAPM_PGA("MIC2 PGA", SUNXI_MIC2G_LINEEN_CTR,
			MIC2AMPEN, 0, NULL, 0),
	SND_SOC_DAPM_MUX("MIC2 Mux", SND_SOC_NOPM, 0, 0, &mic2_mux),

	SND_SOC_DAPM_MICBIAS("MainMic Bias", SUNXI_MIC1G_MICBIAS_CTR,
				MMICBIASEN, 0),
	SND_SOC_DAPM_MICBIAS("HMic Bias", SUNXI_MIC1G_MICBIAS_CTR,
				HMICBIASEN, 0),
	SND_SOC_DAPM_INPUT("MIC1"),
	SND_SOC_DAPM_INPUT("MIC2"),
	SND_SOC_DAPM_INPUT("MIC3"),
	SND_SOC_DAPM_INPUT("LINEINL"),
	SND_SOC_DAPM_INPUT("LINEINR"),
	SND_SOC_DAPM_INPUT("LINEINLR"),
	SND_SOC_DAPM_INPUT("FML"),
	SND_SOC_DAPM_INPUT("FMR"),

	SND_SOC_DAPM_OUTPUT("PHONEOUTP"),
	SND_SOC_DAPM_OUTPUT("PHONEOUTN"),
	SND_SOC_DAPM_OUTPUT("LINEOUTL"),
	SND_SOC_DAPM_OUTPUT("LINEOUTR"),
	SND_SOC_DAPM_OUTPUT("HPOUTL"),
	SND_SOC_DAPM_OUTPUT("HPOUTR"),
	SND_SOC_DAPM_OUT_DRV_E("HPPA DRV", SND_SOC_NOPM, 0, 0, NULL, 0,
		sunxi_hppa_event, SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_OUT_DRV_E("SPKPA DRV", SND_SOC_NOPM, 0, 0, NULL, 0,
		sunxi_spkpa_event, SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
};

static const struct snd_soc_dapm_route sunxi_codec_dapm_routes[] = {
	{"MIC1 PGA", NULL, "MIC1"},
	{"MIC2 Mux", "MIC2IN", "MIC2"},
	{"MIC2 Mux", "MIC3IN", "MIC3"},
	{"MIC2 PGA", NULL, "MIC2 Mux"},

	{"Left Output Mixer", "DACR Switch", "DACR"},
	{"Left Output Mixer", "DACL Switch", "DACL"},
	{"Left Output Mixer", "FML Switch", "FML"},
	{"Left Output Mixer", "LINEINL Switch", "LINEINL"},
	{"Left Output Mixer", "LINEINLR Switch", "LINEINLR"},
	{"Left Output Mixer", "MIC2 Boost Switch", "MIC2 PGA"},
	{"Left Output Mixer", "MIC1 Boost Switch", "MIC1 PGA"},

	{"Right Output Mixer", "DACL Switch", "DACL"},
	{"Right Output Mixer", "DACR Switch", "DACR"},
	{"Right Output Mixer", "FMR Switch", "FMR"},
	{"Right Output Mixer", "LINEINR Switch", "LINEINR"},
	{"Right Output Mixer", "LINEINLR Switch", "LINEINLR"},
	{"Right Output Mixer", "MIC2 Boost Switch", "MIC2 PGA"},
	{"Right Output Mixer", "MIC1 Boost Switch", "MIC1 PGA"},

	{"HPL Mux", "DAC", "DACL"},
	{"HPL Mux", "OMIX", "Left Output Mixer"},
	{"HPR Mux", "DAC", "DACR"},
	{"HPR Mux", "OMIX", "Right Output Mixer"},
	{"HPPA DRV", NULL, "HPL Mux"},
	{"HPPA DRV", NULL, "HPR Mux"},
	{"HPOUTL", NULL, "HPPA DRV"},
	{"HPOUTR", NULL, "HPPA DRV"},

	{"LINEOUTL", NULL, "Left Output Mixer"},
	{"LINEOUTR", NULL, "Right Output Mixer"},

	{"Left Input Mixer", "ROMIX Switch", "Right Output Mixer"},
	{"Left Input Mixer", "LOMIX Switch", "Left Output Mixer"},
	{"Left Input Mixer", "FML Switch", "FML"},
	{"Left Input Mixer", "LINEINL Switch", "LINEINL"},
	{"Left Input Mixer", "LINEINLR Switch", "LINEINLR"},
	{"Left Input Mixer", "MIC2 Boost Switch", "MIC2 PGA"},
	{"Left Input Mixer", "MIC1 Boost Switch", "MIC1 PGA"},

	{"Right Input Mixer", "LOMIX Switch", "Left Output Mixer"},
	{"Right Input Mixer", "ROMIX Switch", "Right Output Mixer"},
	{"Right Input Mixer", "FMR Switch", "FMR"},
	{"Right Input Mixer", "LINEINR Switch", "LINEINR"},
	{"Right Input Mixer", "LINEINLR Switch", "LINEINLR"},
	{"Right Input Mixer", "MIC2 Boost Switch", "MIC2 PGA"},
	{"Right Input Mixer", "MIC1 Boost Switch", "MIC1 PGA"},

	{"ADCL", NULL, "Left Input Mixer"},
	{"ADCR", NULL, "Right Input Mixer"},

	{"Phone Out Mixer", "LOMIX Switch", "Left Output Mixer"},
	{"Phone Out Mixer", "ROMIX Switch", "Right Output Mixer"},
	{"Phone Out Mixer", "MIC2 Boost Switch", "MIC2 PGA"},
	{"Phone Out Mixer", "MIC1 Boost Switch", "MIC1 PGA"},

	{"SPKPA DRV", NULL, "Phone Out Mixer"},
	{"PHONEOUTP", NULL, "SPKPA DRV"},
	{"PHONEOUTN", NULL, "SPKPA DRV"},

	{"LINEINLR", NULL, "LINEINL"},
	{"LINEINLR", NULL, "LINEINR"},
};

static void sunxi_codec_init(struct snd_soc_codec *codec)
{
	struct sunxi_codec *sunxi_internal_codec = snd_soc_codec_get_drvdata(codec);

	/* Disable DRC function for playback */
	snd_soc_write(codec, SUNXI_DAC_DAP_CTR, 0);

	/* Enable HPF(high passed filter) */
	snd_soc_update_bits(codec, SUNXI_DAC_DPC, (1<<HPF_EN), (1<<HPF_EN));

	/* Enable ADCFDT to overcome niose at the beginning */
	snd_soc_update_bits(codec, SUNXI_ADC_FIFO_CTR,
			(7<<ADCDFEN), (7<<ADCDFEN));

	/* Enable Hardware ZeroCross overcomm volume update clicks noise */
	snd_soc_update_bits(codec, SUNXI_ADDA_APT2,
			(1<<ZERO_CROSS_EN), (1<<ZERO_CROSS_EN));

	/* FIXME, When Enable Headphone PA fast setup mode, arise click noise,
	 * so just cut down this way. In PA slow setup mode, the pop noise
	 * appear, so we just slope this time to disable pop noise,
	 * and latency longer. but we must chosen one way to cut down noise,
	 * just pratice many times, we chosen slope time to be 390ms,
	 * so when enable PA, we should delay almost 400ms,
	 * or we lost th data at the beginning.
	 */
	snd_soc_write(codec, SUNXI_PA_POP_CTR, 0x2);

	/* Setting the anti-pop time(rise wave time) to be 393ms */
	snd_soc_update_bits(codec, SUNXI_PAEN_HP_CTR,
			3<<PA_ANTI_POP_CTRL, 2<<PA_ANTI_POP_CTRL);

	/* After setting pop-time to be 393ms, then just enable
	 * headphone PA at probe time. says it just waste little
	 * power consume, but can kill the click noise after probe.
	 */
#ifndef	SUNXI_DAPM_HPPA
	snd_soc_update_bits(codec, SUNXI_PAEN_HP_CTR,
				(1<<HPPAEN), (1<<HPPAEN));
#endif
	/* Default setting user config volume */
	snd_soc_update_bits(codec, SUNXI_HP_VOLC, (0x3F<<HPVOL),
			(sunxi_internal_codec->headphonevol<<HPVOL));
	/* LINEOUT just disable, so just setting PHONEOUT */
	snd_soc_update_bits(codec, SUNXI_PHONEOUT_CTR, (0x7<<PHONEOUTG),
			(sunxi_internal_codec->spkervol<<PHONEOUTG));
	if (sunxi_internal_codec->hp_dirused) {
		snd_soc_update_bits(codec, SUNXI_PAEN_HP_CTR,
				(0x3<<HPCOM_FC), (0x3<<HPCOM_FC));
		snd_soc_update_bits(codec, SUNXI_PAEN_HP_CTR,
				(0x1<<HPCOM_PT), (0x1<<HPCOM_PT));
	} else {
		snd_soc_update_bits(codec, SUNXI_PAEN_HP_CTR,
				(0x3<<HPCOM_FC), (0x0<<HPCOM_FC));
		snd_soc_update_bits(codec, SUNXI_PAEN_HP_CTR,
				(0x1<<HPCOM_PT), (0x0<<HPCOM_PT));
	}

	if (sunxi_internal_codec->hwconfig.adcagc_cfg)
		adcagc_config(codec);

	if (sunxi_internal_codec->hwconfig.adcdrc_cfg)
		adcdrc_config(codec);

	if (sunxi_internal_codec->hwconfig.adchpf_cfg)
		adchpf_config(codec);

	if (sunxi_internal_codec->hwconfig.dacdrc_cfg)
		dacdrc_config(codec);

	if (sunxi_internal_codec->hwconfig.dachpf_cfg)
		dachpf_config(codec);

}

static int sunxi_codec_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	int i = 0;

	switch (params_format(params)) {
	case	SNDRV_PCM_FORMAT_S16_LE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			snd_soc_update_bits(codec, SUNXI_DAC_FIFO_CTR,
					(3<<FIFO_MODE), (3<<FIFO_MODE));
			snd_soc_update_bits(codec, SUNXI_DAC_FIFO_CTR,
				(1<<TX_SAMPLE_BITS), (0<<TX_SAMPLE_BITS));
		} else {
			snd_soc_update_bits(codec, SUNXI_ADC_FIFO_CTR,
				(1<<RX_FIFO_MODE), (1<<RX_FIFO_MODE));
			snd_soc_update_bits(codec, SUNXI_ADC_FIFO_CTR,
				(1<<RX_SAMPLE_BITS), (0<<RX_SAMPLE_BITS));
		}
		break;
	case	SNDRV_PCM_FORMAT_S24_LE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			snd_soc_update_bits(codec, SUNXI_DAC_FIFO_CTR,
					(3<<FIFO_MODE), (0<<FIFO_MODE));
			snd_soc_update_bits(codec, SUNXI_DAC_FIFO_CTR,
				(1<<TX_SAMPLE_BITS), (1<<TX_SAMPLE_BITS));
		} else {
			snd_soc_update_bits(codec, SUNXI_ADC_FIFO_CTR,
				(1<<RX_FIFO_MODE), (0<<RX_FIFO_MODE));
			snd_soc_update_bits(codec, SUNXI_ADC_FIFO_CTR,
				(1<<RX_SAMPLE_BITS), (1<<RX_SAMPLE_BITS));
		}
		break;
	default:
		break;
	}

	for (i = 0; i < ARRAY_SIZE(sample_rate_conv); i++) {
		if (sample_rate_conv[i].samplerate == params_rate(params)) {
			if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
				snd_soc_update_bits(codec, SUNXI_DAC_FIFO_CTR,
					(0x7<<DAC_FS),
					(sample_rate_conv[i].rate_bit<<DAC_FS));
			} else {
				if (sample_rate_conv[i].samplerate > 48000)
					return -EINVAL;
				snd_soc_update_bits(codec, SUNXI_ADC_FIFO_CTR,
					(0x7<<ADC_FS),
					(sample_rate_conv[i].rate_bit<<ADC_FS));
			}
		}
	}

	if (params_channels(params) == 1) {
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			snd_soc_update_bits(codec, SUNXI_DAC_FIFO_CTR,
					(1<<DAC_MONO_EN), 1<<DAC_MONO_EN);
		} else {
			snd_soc_update_bits(codec, SUNXI_ADC_FIFO_CTR,
					(1<<ADC_MONO_EN), (1<<ADC_MONO_EN));
		}
	} else {
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			snd_soc_update_bits(codec, SUNXI_DAC_FIFO_CTR,
					(1<<DAC_MONO_EN), (0<<DAC_MONO_EN));
		} else {
			snd_soc_update_bits(codec, SUNXI_ADC_FIFO_CTR,
					(1<<ADC_MONO_EN), (0<<ADC_MONO_EN));
		}
	}

	return 0;
}

static int sunxi_codec_set_sysclk(struct snd_soc_dai *dai,
			int clk_id, unsigned int freq, int dir)
{
	struct sunxi_codec *sunxi_internal_codec = snd_soc_dai_get_drvdata(dai);

	if (clk_set_rate(sunxi_internal_codec->pllclk, freq)) {
		dev_err(sunxi_internal_codec->dev, "set pllclk rate failed\n");
		return -EINVAL;
	}
	return 0;
}

static void sunxi_codec_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct sunxi_codec *sunxi_internal_codec = snd_soc_codec_get_drvdata(codec);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (sunxi_internal_codec->hwconfig.dacdrc_cfg)
			dacdrc_enable(codec, 0);
		if (sunxi_internal_codec->hwconfig.dachpf_cfg)
			dachpf_enable(codec, 0);
	} else {
		if (sunxi_internal_codec->hwconfig.adcagc_cfg)
			adcagc_enable(codec, 0);

		if (sunxi_internal_codec->hwconfig.adcdrc_cfg)
			adcdrc_enable(codec, 0);

		if (sunxi_internal_codec->hwconfig.adchpf_cfg)
			adchpf_enable(codec, 0);
	}
}

static int sunxi_codec_digital_mute(struct snd_soc_dai *dai, int mute)
{
	return 0;
}

static int sunxi_codec_prepare(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		snd_soc_update_bits(codec, SUNXI_DAC_FIFO_CTR,
				(1<<FIFO_FLUSH), (1<<FIFO_FLUSH));
		snd_soc_write(codec, SUNXI_DAC_FIFO_STA,
				(1<<TXE_INT|1<<TXU_INT|1<<TXO_INT));
		snd_soc_write(codec, SUNXI_DAC_CNT, 0);
	} else {
		snd_soc_update_bits(codec, SUNXI_ADC_FIFO_CTR,
				(1<<FIFO_FLUSH), (1<<FIFO_FLUSH));
		snd_soc_write(codec, SUNXI_ADC_FIFO_STA,
				(1<<RXA_INT|1<<RXO_INT));
		snd_soc_write(codec, SUNXI_ADC_CNT, 0);
	}
	return 0;
}

static int sunxi_codec_trigger(struct snd_pcm_substream *substream,
				int cmd, struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;

	switch (cmd) {
	case	SNDRV_PCM_TRIGGER_START:
	case	SNDRV_PCM_TRIGGER_RESUME:
	case	SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			snd_soc_update_bits(codec, SUNXI_DAC_FIFO_CTR,
					(1<<DAC_DRQ_EN), (1<<DAC_DRQ_EN));
		else
			snd_soc_update_bits(codec, SUNXI_ADC_FIFO_CTR,
					(1<<ADC_DRQ_EN), (1<<ADC_DRQ_EN));
		break;
	case	SNDRV_PCM_TRIGGER_STOP:
	case	SNDRV_PCM_TRIGGER_SUSPEND:
	case	SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			snd_soc_update_bits(codec, SUNXI_DAC_FIFO_CTR,
					(1<<DAC_DRQ_EN), (0<<DAC_DRQ_EN));
		else
			snd_soc_update_bits(codec, SUNXI_ADC_FIFO_CTR,
					(1<<ADC_DRQ_EN), (0<<ADC_DRQ_EN));
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct snd_soc_dai_ops sunxi_codec_dai_ops = {
	.hw_params	= sunxi_codec_hw_params,
	.shutdown	= sunxi_codec_shutdown,
	.digital_mute	= sunxi_codec_digital_mute,
	.set_sysclk	= sunxi_codec_set_sysclk,
	.trigger	= sunxi_codec_trigger,
	.prepare	= sunxi_codec_prepare,
};

static struct snd_soc_dai_driver sunxi_codec_dai[] = {
	{
		.name	= "sun8iw11codec",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates	= SNDRV_PCM_RATE_8000_192000
				| SNDRV_PCM_RATE_KNOT,
			.formats = SNDRV_PCM_FMTBIT_S16_LE
				| SNDRV_PCM_FMTBIT_S24_LE,
		},
		.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000
				| SNDRV_PCM_RATE_KNOT,
			.formats = SNDRV_PCM_FMTBIT_S16_LE
				| SNDRV_PCM_FMTBIT_S24_LE,
		},
		.ops = &sunxi_codec_dai_ops,
	},
};

static const struct snd_soc_component_driver sunxi8iw11_codec_component = {
        .name           = "internal-codec",
};


static int sunxi_codec_probe(struct snd_soc_codec *codec)
{
	sunxi_codec_init(codec);
	return 0;
}

static int sunxi_codec_remove(struct snd_soc_codec *codec)
{
#ifndef	SUNXI_DAPM_HPPA
	snd_soc_update_bits(codec, SUNXI_PAEN_HP_CTR,
				(1<<HPPAEN), (0<<HPPAEN));
#endif
	return 0;
}

static int sunxi_gpio_iodisable(u32 gpio)
{
	char pin_name[8];
	u32 config, ret;
	sunxi_gpio_to_name(gpio, pin_name);
	config = 7 << 16;
	ret = pin_config_set(SUNXI_PINCTRL, pin_name, config);
	return ret;
}

static int sunxi_codec_suspend(struct snd_soc_codec *codec)
{
	struct sunxi_codec *sunxi_internal_codec = snd_soc_codec_get_drvdata(codec);

	pr_debug("Enter %s\n", __func__);

	if (sunxi_internal_codec->spk_gpio_used)
		sunxi_gpio_iodisable(sunxi_internal_codec->spk_gpio);

	clk_disable_unprepare(sunxi_internal_codec->moduleclk);

	clk_disable_unprepare(sunxi_internal_codec->pllclk);

	pr_debug("End %s\n", __func__);

	return 0;
}

static int sunxi_codec_resume(struct snd_soc_codec *codec)
{
	struct sunxi_codec *sunxi_internal_codec = snd_soc_codec_get_drvdata(codec);

	pr_debug("Enter %s\n", __func__);

	if (clk_prepare_enable(sunxi_internal_codec->pllclk)) {
		dev_err(sunxi_internal_codec->dev, "enable pllclk failed, resume exit\n");
		return -EBUSY;
	}

	if (clk_prepare_enable(sunxi_internal_codec->moduleclk)) {
		dev_err(sunxi_internal_codec->dev, "enable  moduleclk failed, resume exit\n");
		clk_disable_unprepare(sunxi_internal_codec->pllclk);
		return -EBUSY;
	}

	if (sunxi_internal_codec->spk_gpio_used) {
		gpio_direction_output(sunxi_internal_codec->spk_gpio, 1);
		gpio_set_value(sunxi_internal_codec->spk_gpio, 0);
	}

	sunxi_codec_init(codec);
	pr_debug("End %s\n", __func__);

	return 0;
}

static unsigned int sunxi_codec_read(struct snd_soc_codec *codec,
					unsigned int reg)
{
	struct sunxi_codec *sunxi_internal_codec = snd_soc_codec_get_drvdata(codec);
	unsigned int reg_val;

	if (reg >= SUNXI_PR_CFG) {
		/* Analog part */
		reg = reg - SUNXI_PR_CFG;
		return read_prcm_wvalue(reg, sunxi_internal_codec->analogbase);
	} else {
		regmap_read(sunxi_internal_codec->regmap, reg, &reg_val);
	}
	return reg_val;
}

static int sunxi_codec_write(struct snd_soc_codec *codec,
				unsigned int reg, unsigned int val)
{
	struct sunxi_codec *sunxi_internal_codec = snd_soc_codec_get_drvdata(codec);

	if (reg >= SUNXI_PR_CFG) {
		/* Analog part */
		reg = reg - SUNXI_PR_CFG;
		write_prcm_wvalue(reg, val, sunxi_internal_codec->analogbase);
	} else {
		regmap_write(sunxi_internal_codec->regmap, reg, val);
	}
	return 0;
};

static struct snd_soc_codec_driver soc_codec_dev_sunxi = {
	.probe = sunxi_codec_probe,
	.remove = sunxi_codec_remove,
	.suspend = sunxi_codec_suspend,
	.resume = sunxi_codec_resume,
	.read = sunxi_codec_read,
	.write = sunxi_codec_write,
	.ignore_pmdown_time = 1,
	.controls = sunxi_codec_controls,
	.num_controls = ARRAY_SIZE(sunxi_codec_controls),
	.dapm_widgets = sunxi_codec_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sunxi_codec_dapm_widgets),
	.dapm_routes = sunxi_codec_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(sunxi_codec_dapm_routes),
};

struct label {
	const char *name;
	int value;
};

#define LABEL(constant) { #constant, constant }
#define LABEL_END { NULL, -1 }

static struct label reg_labels[] = {
	LABEL(SUNXI_DAC_DPC),
	LABEL(SUNXI_DAC_FIFO_CTR),
	LABEL(SUNXI_DAC_FIFO_STA),
	LABEL(SUNXI_ADC_FIFO_CTR),
	LABEL(SUNXI_ADC_FIFO_STA),
	LABEL(SUNXI_ADC_RXDATA),
	LABEL(SUNXI_DAC_TXDATA),
	LABEL(SUNXI_DAC_CNT),
	LABEL(SUNXI_ADC_CNT),
	LABEL(SUNXI_DAC_DG),
	LABEL(SUNXI_ADC_DG),
	LABEL(SUNXI_HMIC_CTRL),
	LABEL(SUNXI_HMIC_DATA),

	LABEL(SUNXI_HP_VOLC),
	LABEL(SUNXI_LOMIX_SRC),
	LABEL(SUNXI_ROMIX_SRC),
	LABEL(SUNXI_DAC_PA_SRC),
	LABEL(SUNXI_LINEIN_GCTR),
	LABEL(SUNXI_FM_GCTR),
	LABEL(SUNXI_MICIN_GCTR),
	LABEL(SUNXI_PAEN_HP_CTR),
	LABEL(SUNXI_PHONEOUT_CTR),
	LABEL(SUNXI_MIC2G_LINEEN_CTR),
	LABEL(SUNXI_MIC1G_MICBIAS_CTR),
	LABEL(SUNXI_LADCMIX_SRC),
	LABEL(SUNXI_RADCMIX_SRC),

	LABEL(SUNXI_PA_POP_CTR),
	LABEL(SUNXI_ADC_AP_EN),
	LABEL(SUNXI_ADDA_APT0),
	LABEL(SUNXI_ADDA_APT1),
	LABEL(SUNXI_ADDA_APT2),
	LABEL(SUNXI_CHOP_CAL_CTR),
	LABEL(SUNXI_BIAS_DA16_CAL_CTR),
	LABEL(SUNXI_DA16_CALI_DATA),
	LABEL(SUNXI_BIAS_CALI_DATA),
	LABEL(SUNXI_BIAS_CALI_SET),
	LABEL_END,
};

static ssize_t show_audio_reg(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct sunxi_codec *sunxi_internal_codec = dev_get_drvdata(dev);
	int count = 0, i = 0;
	int reg_group = 1;
	int reg_offset;
	unsigned int reg_val;

	count += sprintf(buf, "dump audio reg:\n");

	while (reg_labels[i].name != NULL) {
		if (reg_labels[i].value == SUNXI_PR_CFG)
			reg_group++;

		if (reg_group == 1) {
			regmap_read(sunxi_internal_codec->regmap,
					reg_labels[i].value, &reg_val);
			count += sprintf(buf + count, "%s 0x%x: 0x%08x\n",
			reg_labels[i].name, (reg_labels[i].value), reg_val);
		} else if (reg_group == 2) {
			reg_offset = reg_labels[i].value - SUNXI_PR_CFG;
			reg_val = read_prcm_wvalue(reg_offset,
					sunxi_internal_codec->analogbase);
			count += sprintf(buf + count, "%s 0x%x: 0x%x\n",
			reg_labels[i].name, reg_labels[i].value, reg_val);
		}
		i++;
	}

	return count;
}

/* ex:
*param 1: 0 read;1 write
*param 2: 1 digital reg; 2 analog reg
*param 3: reg value;
*param 4: write value;
	read:
		echo 0,1,0x00> audio_reg
   		echo 0,2,0x00> audio_reg
	write:
		echo 1,1,0x00,0xa > audio_reg
		echo 1,2,0x00,0xff > audio_reg
*/
static ssize_t store_audio_reg(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	int ret;
	int rw_flag;
	int input_reg_val =0;
	int input_reg_group =0;
	int input_reg_offset =0;
	struct sunxi_codec *sunxi_internal_codec = dev_get_drvdata(dev);

	ret = sscanf(buf, "%d,%d,0x%x,0x%x", &rw_flag, &input_reg_group,
			&input_reg_offset, &input_reg_val);
	dev_info(dev, "ret:%d, reg_group:%d, reg_offset:%d, reg_val:0x%x\n",
			ret, input_reg_group, input_reg_offset, input_reg_val);

	if (!(input_reg_group == 1 || input_reg_group == 2)) {
		pr_err("not exist reg group\n");
		ret = count;
		goto out;
	}
	if (!(rw_flag == 1 || rw_flag == 0)) {
		pr_err("not rw_flag\n");
		ret = count;
		goto out;
	}
	if (input_reg_group == 1) {
		if (rw_flag) {
			regmap_write(sunxi_internal_codec->regmap,
					input_reg_offset, input_reg_val);
		} else {
			regmap_read(sunxi_internal_codec->regmap,
					input_reg_offset, &input_reg_val);
			dev_info(dev, "\n\n Reg[0x%x] : 0x%08x\n\n",
					input_reg_offset, input_reg_val);
		}
	} else if (input_reg_group == 2) {
		if (rw_flag) {
			write_prcm_wvalue(input_reg_offset,
			input_reg_val & 0xff, sunxi_internal_codec->analogbase);
		} else {
			 input_reg_val = read_prcm_wvalue(input_reg_offset,
					sunxi_internal_codec->analogbase);
			 dev_info(dev, "\n\n Reg[0x%02x] : 0x%02x\n\n",
					input_reg_offset, input_reg_val);
		}
	}
	ret = count;

out:
	return ret;
}

static DEVICE_ATTR(audio_reg, 0644, show_audio_reg, store_audio_reg);

static struct attribute *audio_debug_attrs[] = {
	&dev_attr_audio_reg.attr,
	NULL,
};

static struct attribute_group audio_debug_attr_group = {
	.name   = "audio_reg_debug",
	.attrs  = audio_debug_attrs,
};

static const struct regmap_config sunxi_codec_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = SUNXI_ADC_DAP_CTR,
	.cache_type = REGCACHE_NONE,
};

static int  sunxi_internal_codec_probe(struct platform_device *pdev)
{
	struct sunxi_codec *sunxi_internal_codec;
	struct device_node *np = pdev->dev.of_node;
	void __iomem *sunxi_digibase;
	int ret;
	unsigned int temp_val;

	sunxi_internal_codec = devm_kzalloc(&pdev->dev, sizeof(struct sunxi_codec), GFP_KERNEL);
	if (!sunxi_internal_codec) {
		dev_err(&pdev->dev, "Can't allocate sunxi codec memory\n");
		ret = -ENOMEM;
		goto err_node_put;
	}
	dev_err(&pdev->dev, "internal codec probe! =====>\n");
	dev_set_drvdata(&pdev->dev, sunxi_internal_codec);
	sunxi_internal_codec->dev = &pdev->dev;

	sunxi_internal_codec->pllclk = of_clk_get(np, 0);
	sunxi_internal_codec->moduleclk = of_clk_get(np, 1);
	if (IS_ERR_OR_NULL(sunxi_internal_codec->pllclk)) {
		dev_err(&pdev->dev, "pllclk not exist or invaild\n");
		ret = PTR_ERR(sunxi_internal_codec->pllclk);
		goto err_devm_kfree;
	} else {
		if (IS_ERR_OR_NULL(sunxi_internal_codec->moduleclk)) {
			dev_err(&pdev->dev, "moduleclk not exist or invaild\n");
			ret = PTR_ERR(sunxi_internal_codec->moduleclk);
			goto err_devm_kfree;
		} else {
			if (clk_set_parent(sunxi_internal_codec->moduleclk,
					sunxi_internal_codec->pllclk)) {
				dev_err(&pdev->dev, "set parent of moduleclk to pllclk failed\n");
				ret = -EBUSY;
				goto err_devm_kfree;
			}
			if (clk_prepare_enable(sunxi_internal_codec->pllclk)) {
				dev_err(&pdev->dev, "pllclk enable failed\n");
				ret = -EBUSY;
				goto err_devm_kfree;
			}
			if (clk_prepare_enable(sunxi_internal_codec->moduleclk)) {
				dev_err(&pdev->dev, "moduleclk enable failed\n");
				ret = -EBUSY;
				goto err_pllclk_put;
			}
		}
	}

	sunxi_digibase = of_iomap(np, 0);
	if (sunxi_digibase == NULL) {
		dev_err(&pdev->dev, "digital register iomap failed\n");
		ret = -EINVAL;
		goto err_moduleclk_put;
	}

	/* Analog register part, not using regmap */
	sunxi_internal_codec->analogbase = of_iomap(np, 1);
	if (sunxi_internal_codec->analogbase == NULL) {
		dev_err(&pdev->dev, "analog register iomap failed\n");
		ret = -EINVAL;
		goto err_digi_iounmap;
	}

	sunxi_internal_codec->regmap = devm_regmap_init_mmio(&pdev->dev,
				sunxi_digibase, &sunxi_codec_regmap_config);
	if (IS_ERR(sunxi_internal_codec->regmap)) {
		dev_err(&pdev->dev, "regmap init failed\n");
		ret = PTR_ERR(sunxi_internal_codec->regmap);
		goto err_analog_iounmap;
	}

/*ZSJ register component internal codec*/
        /* register voice DAI here */
        /*ret = snd_soc_register_component(&pdev->dev, &sunxi8iw11_codec_component,
                                                        sunxi_codec_dai, ARRAY_SIZE(sunxi_codec_dai));

*/
	ret = of_property_read_u32(np, "headphonevol", &temp_val);
	if (ret < 0) {
		dev_warn(&pdev->dev, "headphone volume get failed\n");
		sunxi_internal_codec->headphonevol = 0;
	} else {
		sunxi_internal_codec->headphonevol = temp_val;
	}

	ret = of_property_read_u32(np, "spkervol", &temp_val);
	if (ret < 0) {
		dev_warn(&pdev->dev, "speaker volume get failed\n");
		sunxi_internal_codec->spkervol = 0;
	} else {
		sunxi_internal_codec->spkervol = temp_val;
	}

	ret = of_property_read_u32(np, "maingain", &temp_val);
	if (ret < 0) {
		dev_warn(&pdev->dev, "main gain get failed\n");
		sunxi_internal_codec->maingain = 0;
	} else {
		sunxi_internal_codec->maingain = temp_val;
	}

	ret = of_property_read_u32(np, "hp_dirused", &temp_val);
	if (ret < 0) {
		dev_warn(&pdev->dev, "hp_dirused get failed\n");
		sunxi_internal_codec->hp_dirused = 0;
	} else {
		sunxi_internal_codec->hp_dirused = temp_val;
	}

	ret = of_property_read_u32(np, "pa_sleep_time", &temp_val);
	if (ret < 0) {
		dev_warn(&pdev->dev, "pa_sleep_time get failed\n");
		sunxi_internal_codec->pa_sleep_time = 350;
	} else {
		sunxi_internal_codec->pa_sleep_time = temp_val;
	}

	pr_debug("headphonevol:%d, spkervol:%d, maingain:%d, pa_sleep_time:%d\n",
		sunxi_internal_codec->headphonevol,
		sunxi_internal_codec->spkervol,
		sunxi_internal_codec->maingain,
		sunxi_internal_codec->pa_sleep_time
	);
	ret = of_property_read_u32(np, "adcagc_cfg", &temp_val);
	if (ret < 0) {
		pr_err("[audio-codec]adcagc_cfg configurations missing or invalid.\n");
		ret = -EINVAL;
	} else {
		sunxi_internal_codec->hwconfig.adcagc_cfg = temp_val;
	}

	ret = of_property_read_u32(np, "adcdrc_cfg", &temp_val);
	if (ret < 0) {
		pr_err("[audio-codec]adcdrc_cfg configurations missing or invalid.\n");
		ret = -EINVAL;
	} else {
		sunxi_internal_codec->hwconfig.adcdrc_cfg = temp_val;
	}

	ret = of_property_read_u32(np, "adchpf_cfg", &temp_val);
	if (ret < 0) {
		pr_err("[audio-codec]adchpf_cfg configurations missing or invalid.\n");
		ret = -EINVAL;
	} else {
		sunxi_internal_codec->hwconfig.adchpf_cfg = temp_val;
	}

	ret = of_property_read_u32(np, "dacdrc_cfg", &temp_val);
	if (ret < 0) {
		pr_err("[audio-codec]dacdrc_cfg configurations missing or invalid.\n");
		ret = -EINVAL;
	} else {
		sunxi_internal_codec->hwconfig.dacdrc_cfg = temp_val;
	}

	ret = of_property_read_u32(np, "dachpf_cfg", &temp_val);
	if (ret < 0) {
		pr_err("[audio-codec]dachpf_cfg configurations missing or invalid.\n");
		ret = -EINVAL;
	} else {
		sunxi_internal_codec->hwconfig.dachpf_cfg = temp_val;
	}

	ret = of_get_named_gpio(np, "gpio-spk", 0);
	if (ret >= 0) {
		sunxi_internal_codec->spk_gpio_used = 1;
		sunxi_internal_codec->spk_gpio = ret;
		if (!gpio_is_valid(sunxi_internal_codec->spk_gpio)) {
			dev_err(&pdev->dev, "gpio-spk is valid\n");
			ret = -EINVAL;
			goto err_analog_iounmap;
		} else {
			ret = devm_gpio_request(&pdev->dev,
					sunxi_internal_codec->spk_gpio, "SPK");
			if (ret) {
				dev_err(&pdev->dev, "failed to request gpio-spk gpio\n");
				ret = -EBUSY;
				goto err_analog_iounmap;
			} else {
				gpio_direction_output(sunxi_internal_codec->spk_gpio, 1);
				gpio_set_value(sunxi_internal_codec->spk_gpio, 0);
			}
		}
	} else {
		sunxi_internal_codec->spk_gpio_used = 0;
	}

	ret = snd_soc_register_codec(&pdev->dev, &soc_codec_dev_sunxi,
				sunxi_codec_dai, ARRAY_SIZE(sunxi_codec_dai));
	dev_err(&pdev->dev, "internal codec register! ret = 0x%x\n",ret);
	if (ret < 0) {
		dev_err(&pdev->dev, "register codec failed\n");
		goto err_analog_iounmap;
	}

	ret  = sysfs_create_group(&pdev->dev.kobj, &audio_debug_attr_group);
	if (ret){
		dev_warn(&pdev->dev, "failed to create attr group\n");
	}
	return 0;

err_analog_iounmap:
	iounmap(sunxi_internal_codec->analogbase);
err_digi_iounmap:
	iounmap(sunxi_digibase);
err_moduleclk_put:
	clk_disable_unprepare(sunxi_internal_codec->moduleclk);
err_pllclk_put:
	clk_disable_unprepare(sunxi_internal_codec->pllclk);
err_devm_kfree:
	devm_kfree(&pdev->dev, sunxi_internal_codec);
err_node_put:
	of_node_put(np);
	return ret;
}

static int  __exit sunxi_internal_codec_remove(struct platform_device *pdev)
{
	struct sunxi_codec *sunxi_internal_codec = dev_get_drvdata(&pdev->dev);

	snd_soc_unregister_codec(&pdev->dev);
	clk_put(sunxi_internal_codec->moduleclk);
	clk_put(sunxi_internal_codec->pllclk);
	devm_kfree(&pdev->dev, sunxi_internal_codec);
	return 0;
}

/*static const struct of_device_id sunxi_internal_codec_of_match[] = {
	{ .compatible = "allwinner,sunxi-internal-codec", },
	{},
};*/
static const struct of_device_id sunxi_internal_codec_of_match[] = {
	{ .compatible = "allwinner,1c22c00.codec", },
	{},
};

static struct platform_driver sunxi_internal_codec_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = sunxi_internal_codec_of_match,
	},
	.probe = sunxi_internal_codec_probe,
	.remove = __exit_p(sunxi_internal_codec_remove),
};

module_platform_driver(sunxi_internal_codec_driver);

MODULE_DESCRIPTION("SUNXI Codec ASoC driver");
MODULE_AUTHOR("wolfgang huang <huangjinhui@allwinnertech.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sunxi-internal-codec");

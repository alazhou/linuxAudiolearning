/*
 * sound\soc\sunxi\sunx8iw11_sndcodec.c
 * (C) Copyright 2014-2017
 * allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * huangxin <huangxin@allwinnertech.com>
 * wolfgang huang <huangjinhui@allwinnetech.com>
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
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/io.h>
#include <linux/input.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <linux/of.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <linux/delay.h>

struct sunxi_card_priv {
	int jack_gpio;
	int jack_invert;	/* 0->high is plug_in, 1->high is plug_out */
	struct snd_soc_jack jack_detect;
};

/* Headphone jack detection DAPM pins */
static struct snd_soc_jack_pin sunxi_jack_pins[] = {
	{
		.pin = "Headphone",
		.mask = SND_JACK_HEADPHONE,
	},
};

/* Headphone jack detection gpios */
static struct snd_soc_jack_gpio sunxi_jack_gpios[] = {
	{
		.name = "jack-det",
		.report = SND_JACK_HEADPHONE,
		.debounce_time = 200,
	},
};

/* we only support Headphone & PHONEOUT, so just left LINEOUT no used */
static const struct snd_kcontrol_new sunxi_card_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Phoneout Speaker"),
};

static const struct snd_soc_dapm_widget sunxi_card_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("Main Mic", NULL),
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_LINE("Radio", NULL),
	SND_SOC_DAPM_SPK("Phoneout Speaker", NULL),
	SND_SOC_DAPM_LINE("LINEIN", NULL),
	SND_SOC_DAPM_LINE("LINEOUT", NULL),
};

static const struct snd_soc_dapm_route sunxi_card_routes[] = {
	{"MainMic Bias", NULL, "Main Mic"},
	{"MIC1", NULL, "MainMic Bias"},
	{"MIC2", NULL, "MainMic Bias"},
	{"Headphone", NULL, "HPOUTL"},
	{"Headphone", NULL, "HPOUTR"},
	{"Phoneout Speaker", NULL, "PHONEOUTP"},
	{"Phoneout Speaker", NULL, "PHONEOUTN"},
	{"LINEINL", NULL, "LINEIN"},
	{"LINEINR", NULL, "LINEIN"},
	{"LINEOUT", NULL, "LINEOUTL"},
	{"LINEOUT", NULL, "LINEOUTR"},
	{"FML", NULL, "Radio"},
	{"FMR", NULL, "Radio"},
/*        {"DAC", NULL, "ASI1"},
        {"DAC", NULL, "ASI2"},
        {"DAC", NULL, "ASIM"},
        {"ClassD", NULL, "DAC"},
        {"OUT", NULL, "ClassD"},
        {"DAC", NULL, "PLL"},
        {"DAC", NULL, "NDivider"},*/
};

/*
 * Card initialization
 */
static int sunxi_card_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_card *card = codec->card;
	struct sunxi_card_priv *priv = snd_soc_card_get_drvdata(card);

	snd_soc_dapm_disable_pin(&codec->dapm, "Radio");
	snd_soc_dapm_disable_pin(&codec->dapm, "Headphone");
	snd_soc_dapm_disable_pin(&codec->dapm, "Phoneout Speaker");
	snd_soc_dapm_disable_pin(&codec->dapm, "LINEIN");
	snd_soc_dapm_disable_pin(&codec->dapm, "LINEOUT");

	/* Headset jack detection only if it is supported */
	if (priv->jack_gpio > 0) {
		sunxi_jack_gpios[0].gpio = priv->jack_gpio;
		sunxi_jack_gpios[0].invert = priv->jack_invert;

		ret = snd_soc_jack_new(codec, "Headphone Jack",
					SND_JACK_HEADPHONE,
				       &priv->jack_detect);
		if (ret)
			return ret;

		ret = snd_soc_jack_add_pins(&priv->jack_detect,
					    ARRAY_SIZE(sunxi_jack_pins),
					    sunxi_jack_pins);
		if (ret)
			return ret;

		ret = snd_soc_jack_add_gpios(&priv->jack_detect,
					     ARRAY_SIZE(sunxi_jack_gpios),
					     sunxi_jack_gpios);
		if (ret) {
			dev_err(card->dev, "jack_add_gpios failed: %d,"
				"maybe gpio has't external interrupt ability\n",
				ret);
			return ret;
		}
	}

	snd_soc_dapm_sync(&codec->dapm);
	return 0;
}

static int sunxi_card_hw_params(struct snd_pcm_substream *substream,
                                       struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_card *card = rtd->codec->card;
	unsigned int freq;
	int ret;

	switch (params_rate(params)) {
	case	8000:
	case	12000:
	case	16000:
	case	24000:
	case	32000:
	case	48000:
	case	96000:
	case	192000:
		freq = 24576000;
		break;
	case	11025:
	case	22050:
	case	44100:
		freq = 22579200;
		break;
	default:
		dev_err(card->dev, "invalid rate setting\n");
		return -EINVAL;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, 0, freq, 0);
	if (ret < 0) {
		dev_err(card->dev, "set codec dai sysclk faided.\n");
		return ret;
	}

	return 0;
}

static struct snd_soc_ops sunxi_card_ops = {
	.hw_params	= sunxi_card_hw_params,
};

static int sunxi_card_suspend(struct snd_soc_card *card)
{
	struct sunxi_card_priv *priv = snd_soc_card_get_drvdata(card);

	if (priv->jack_gpio > 0)
		snd_soc_jack_free_gpios(&priv->jack_detect,
					ARRAY_SIZE(sunxi_jack_gpios),
					sunxi_jack_gpios);
	return 0;
}

static int sunxi_card_resume(struct snd_soc_card *card)
{
	struct sunxi_card_priv *priv = snd_soc_card_get_drvdata(card);
	int ret;

	if (priv->jack_gpio > 0) {
		ret = snd_soc_jack_add_gpios(&priv->jack_detect,
				ARRAY_SIZE(sunxi_jack_gpios), sunxi_jack_gpios);
		if (ret < 0) {
			dev_err(card->dev, "jack_add_gpios failed: %d,"
				"maybe gpio has't external interrupt ability\n",
				ret);
			return ret;
		}
	}
	return 0;
}

/*
 *  TEST Zhou Shijie 
 */
static int sunxi_tas2555_init(void)
{
        int ret = 0;

        printk("CODEC Tas2555 inited !!\n");

        return ret;
}
static int sunxi_tas2555_hw_params(struct snd_pcm_substream *substream,
        struct snd_pcm_hw_params *params)
{
        struct snd_soc_pcm_runtime *rtd = substream->private_data;
        struct snd_soc_dai *codec_dai = rtd->codec_dai;

        /* Set the codec system clock for DAC and ADC */
        return 0;
        /*snd_soc_dai_set_sysclk(codec_dai, 0, 19200000,
                                      SND_SOC_CLOCK_IN);*/
}

static struct snd_soc_ops sunxi_snd_tas2555_ops = {
        .hw_params              = sunxi_tas2555_hw_params,
};
/*
 *  TEST Zhou Shijie
 */



static struct snd_soc_dai_link sunxi_card_dai_link[] = {
	{
		.name		= "audiocodec",
		.stream_name	= "SUNXI-CODEC",
		.cpu_dai_name	= "sunxi-internal-cpudai",
		.codec_dai_name = "sun8iw11codec",
		.platform_name	= "sunxi-internal-cpudai",
		//.codec_name	= "sunxi-internal-codec",
		.codec_name	= "1c22c00.codec",
		.init		= sunxi_card_init,
		.ops		= &sunxi_card_ops,
	},
/* ZSJ fucking shit*/
	{
        .name           = "tas2555 ASI1",
        .stream_name    = "ASI1 Playback",
        .cpu_dai_name   = "sunxi_daudio",//"sunxi-internal-cpudai",
        //.cpu_dai_name   = "1c22000.daudio",//"sunxi-internal-cpudai",
        .codec_dai_name = "tas2555 ASI1",
        .codec_name     = "tas2555.1-004c",
        .init           = sunxi_tas2555_init,
        .platform_name        = "sunxi-daudio",//"sunxi-internal-cpudai",
        .ops            = &sunxi_snd_tas2555_ops,
        },/*{
                .name           = "tas2555 ASI2",
                .stream_name    = "ASI2 Playback",
                .cpu_dai_name   = "sunxi-internal-cpudai",
                .codec_dai_name = "tas2555 ASI2",
                .codec_name     = "tas2555.1-004c",
                .init           = sunxi_tas2555_init,
                .platform_name        = "sunxi-internal-cpudai",
                .ops            = &sunxi_snd_tas2555_ops,
        },{
                .name           = "tas2555 ASIM",
                .stream_name    = "ASIM Playback",
                .cpu_dai_name   = "sunxi-internal-cpudai",
                .codec_dai_name = "tas2555 ASIM",
                .codec_name     = "tas2555.1-004c",
                .init           = sunxi_tas2555_init,
                .platform_name        = "sunxi-internal-cpudai",
                .ops            = &sunxi_snd_tas2555_ops,
        },*/
};

static struct snd_soc_card snd_soc_sunxi_card = {
	.name		= "audiocodec",
	.owner		= THIS_MODULE,
	.dai_link	= sunxi_card_dai_link,
	.num_links	= ARRAY_SIZE(sunxi_card_dai_link),
	.controls	= sunxi_card_controls,
	.num_controls	= ARRAY_SIZE(sunxi_card_controls),
	.dapm_widgets	= sunxi_card_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sunxi_card_dapm_widgets),
	.dapm_routes = sunxi_card_routes,
	.num_dapm_routes = ARRAY_SIZE(sunxi_card_routes),
	.suspend_post	= sunxi_card_suspend,
	.resume_post	= sunxi_card_resume,
};

static int sunxi_card_dev_probe(struct platform_device *pdev)
{
	int ret = 0;
	unsigned int temp_val = 0;
	struct device_node *np = pdev->dev.of_node;
	struct snd_soc_card *card = &snd_soc_sunxi_card;
	struct sunxi_card_priv *priv;

	/* register the soc card */
	card->dev = &pdev->dev;

	priv = devm_kzalloc(&pdev->dev,
		sizeof(struct sunxi_card_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->jack_gpio = of_get_named_gpio(np,
				"jack_det_gpio", 0);
	ret = of_property_read_u32(np, "jack_invert", &temp_val);
	if (ret < 0) {
		dev_err(&pdev->dev, "jack_invert not setting, default setting 0\n");
		priv->jack_invert = 0;
	} else {
		priv->jack_invert = temp_val;
	}

	sunxi_card_dai_link[0].cpu_dai_name = NULL;
	sunxi_card_dai_link[0].cpu_of_node = of_parse_phandle(np,
					"sunxi,cpudai-controller", 0);
	if (!sunxi_card_dai_link[0].cpu_of_node) {
		dev_err(&pdev->dev, "Property 'sunxi,cpudai-controller' missing or invalid\n");
		ret = -EINVAL;
		goto err_devm_kfree;
	} else {
		sunxi_card_dai_link[0].platform_name = NULL;
		sunxi_card_dai_link[0].platform_of_node =
				sunxi_card_dai_link[0].cpu_of_node;
	}
	
	sunxi_card_dai_link[1].cpu_dai_name = NULL;
	sunxi_card_dai_link[1].cpu_of_node = of_parse_phandle(np,
					"sunxi,daudio0-controller", 0);
	if (!sunxi_card_dai_link[1].cpu_of_node) {
		dev_err(&pdev->dev, "Property 'sunxi,daudio0-controller' missing or invalid\n");
		ret = -EINVAL;
		goto err_devm_kfree;
	} else {
		sunxi_card_dai_link[1].platform_name = NULL;
		sunxi_card_dai_link[1].platform_of_node =
				sunxi_card_dai_link[1].cpu_of_node;
	}
	/*sunxi_card_dai_link[1].codec_name = NULL;
	sunxi_card_dai_link[1].codec_of_node = of_parse_phandle(np,
						"sunxi,audio-codec0", 0);
	if (!sunxi_card_dai_link[1].codec_of_node) {
		dev_err(&pdev->dev, "Property 'sunxi,audio-codec' missing or invalid\n");
		ret = -EINVAL;
		goto err_devm_kfree;
	}*/
	
	sunxi_card_dai_link[2].codec_name = NULL;
	sunxi_card_dai_link[2].codec_of_node = of_parse_phandle(np,
						"sunxi,audio-codec0", 0);
	if (!sunxi_card_dai_link[2].codec_of_node) {
		dev_err(&pdev->dev, "Property 'sunxi,audio-codec0' missing or invalid\n");
		ret = -EINVAL;
		goto err_devm_kfree;
	}
	
	sunxi_card_dai_link[3].codec_name = NULL;
	sunxi_card_dai_link[3].codec_of_node = of_parse_phandle(np,
						"sunxi,audio-codec1", 0);
	if (!sunxi_card_dai_link[3].codec_of_node) {
		dev_err(&pdev->dev, "Property 'sunxi,audio-codec' missing or invalid\n");
		ret = -EINVAL;
		goto err_devm_kfree;
	}
	snd_soc_card_set_drvdata(card, priv);
	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed %d\n", ret);
		goto err_devm_kfree;
	}

	return 0;

err_devm_kfree:
	devm_kfree(&pdev->dev, priv);
	return ret;
}

static int __exit sunxi_card_dev_remove(struct platform_device *pdev)
{
	struct sunxi_card_priv *priv = dev_get_drvdata(&pdev->dev);

	devm_kfree(&pdev->dev, priv);
	return 0;
}

static const struct of_device_id sunxi_card_of_match[] = {
	{ .compatible = "allwinner,sunxi-codec-machine", },
	{},
};

static struct platform_driver sunxi_machine_driver = {
	.driver = {
		.name = "sunxi-codec-machine",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = sunxi_card_of_match,
	},
	.probe = sunxi_card_dev_probe,
	.remove = __exit_p(sunxi_card_dev_remove),
};

module_platform_driver(sunxi_machine_driver);

MODULE_AUTHOR("wolfgang huang <huangjinhui@allwinnertech.com>");
MODULE_DESCRIPTION("SUNXI Codec Machine ASoC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sunxi-codec-machine");

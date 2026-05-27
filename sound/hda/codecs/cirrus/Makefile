# SPDX-License-Identifier: GPL-2.0
subdir-ccflags-y += -I$(src)/../../common

snd-hda-codec-cs420x-y :=	cs420x.o
snd-hda-codec-cs421x-y :=	cs421x.o
snd-hda-codec-cs8409-y :=	cs8409.o cs8409-tables.o cirrus_apple.o \
				cirrus_apple_coef.o \
				cirrus_apple_stream.o cirrus_apple_l83_i2c.o \
				cirrus_apple_boot.o cirrus_apple_amp.o \
				cirrus_apple_input.o cirrus_apple_play.o \
				cirrus_apple_jack.o

obj-$(CONFIG_SND_HDA_CODEC_CS420X) += snd-hda-codec-cs420x.o
obj-$(CONFIG_SND_HDA_CODEC_CS421X) += snd-hda-codec-cs421x.o
obj-$(CONFIG_SND_HDA_CODEC_CS8409) += snd-hda-codec-cs8409.o

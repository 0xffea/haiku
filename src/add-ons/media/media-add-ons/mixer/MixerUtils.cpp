#include <MediaDefs.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "MixerUtils.h"
#include "debug.h"

void
string_for_channel_mask(char *str, uint32 mask)
{
	str[0] = 0;
	if (mask == 0) {
		strcpy(str, "<none>");
		return;
	}
	#define DECODE(type, text)	if (mask & (type)) \
		 do { strcat(str, text); mask &= ~(type); if (mask != 0) strcat(str, ", "); } while (0)
	DECODE(B_CHANNEL_LEFT, "Left");
	DECODE(B_CHANNEL_RIGHT, "Right");
	DECODE(B_CHANNEL_CENTER, "Center");
	DECODE(B_CHANNEL_SUB, "Sub");
	DECODE(B_CHANNEL_REARLEFT, "Rear-Left");
	DECODE(B_CHANNEL_REARRIGHT, "Rear-Right");
	DECODE(B_CHANNEL_FRONT_LEFT_CENTER, "Front-Left-Center");
	DECODE(B_CHANNEL_FRONT_RIGHT_CENTER, "Front-Right-Center");
	DECODE(B_CHANNEL_BACK_CENTER, "Back-Center");
	DECODE(B_CHANNEL_SIDE_LEFT, "Side-Left");
	DECODE(B_CHANNEL_SIDE_RIGHT, "Side-Right");
	DECODE(B_CHANNEL_TOP_CENTER, "Top-Center");
	DECODE(B_CHANNEL_TOP_FRONT_LEFT, "Top-Front-Left");
	DECODE(B_CHANNEL_TOP_FRONT_CENTER, "Top-Front-Center");
	DECODE(B_CHANNEL_TOP_FRONT_RIGHT, "Top-Front-Right");
	DECODE(B_CHANNEL_TOP_BACK_LEFT, "Top-Back-Left");
	DECODE(B_CHANNEL_TOP_BACK_CENTER, "Top-Back-Center");
	DECODE(B_CHANNEL_TOP_BACK_RIGHT, "Top-Back-Right");
	#undef DECODE
	if (mask)
		sprintf(str + strlen(str), "0x%08X", mask);
}

int
count_nonzero_bits(uint32 value)
{
	int count = 0;
	for (int i = 0; i < 32; i++)
		if (value & (1 << i))
			count++;
	return count;
}

void
fix_multiaudio_format(media_multi_audio_format *format)
{
	if (format->format == media_raw_audio_format::B_AUDIO_INT) {
		if (format->valid_bits != 0 && (format->valid_bits < 16 || format->valid_bits >= 32))
			format->valid_bits = 0;
	}
	switch (format->channel_count) {
		case 0:
			format->channel_mask = 0;
			format->matrix_mask = 0;
			break;
		case 1:
			if (count_nonzero_bits(format->channel_mask) != 1) {
				format->channel_mask = B_CHANNEL_LEFT;
				format->matrix_mask = 0;
			}
			break;
		case 2:
			if (count_nonzero_bits(format->channel_mask) != 2) {
				format->channel_mask = B_CHANNEL_LEFT | B_CHANNEL_RIGHT;
				format->matrix_mask = 0;
			}
			break;
		case 3:
			if (count_nonzero_bits(format->channel_mask) != 3) {
				format->channel_mask = B_CHANNEL_LEFT | B_CHANNEL_RIGHT | B_CHANNEL_CENTER;
				format->matrix_mask = 0;
			}
			break;
		case 4:
			if (count_nonzero_bits(format->channel_mask) != 4) {
				format->channel_mask = B_CHANNEL_LEFT | B_CHANNEL_RIGHT | B_CHANNEL_REARLEFT | B_CHANNEL_REARRIGHT;
				format->matrix_mask = 0;
			}
			break;
		case 5:
			if (count_nonzero_bits(format->channel_mask) != 5) {
				format->channel_mask = B_CHANNEL_LEFT | B_CHANNEL_RIGHT | B_CHANNEL_REARLEFT | B_CHANNEL_REARRIGHT | B_CHANNEL_CENTER;
				format->matrix_mask = 0;
			}
			break;
		case 6:
			if (count_nonzero_bits(format->channel_mask) != 6) {
				format->channel_mask = B_CHANNEL_LEFT | B_CHANNEL_RIGHT | B_CHANNEL_REARLEFT | B_CHANNEL_REARRIGHT | B_CHANNEL_CENTER | B_CHANNEL_SUB;
				format->matrix_mask = 0;
			}
			break;
		case 7:
			if (count_nonzero_bits(format->channel_mask) != 7) {
				format->channel_mask = B_CHANNEL_LEFT | B_CHANNEL_RIGHT | B_CHANNEL_REARLEFT | B_CHANNEL_REARRIGHT | B_CHANNEL_CENTER | B_CHANNEL_SUB | B_CHANNEL_BACK_CENTER;
				format->matrix_mask = 0;
			}
			break;
		
		default:
			break;
	}

	// XXX Workaround for broken BeOS R5 quicktime extractor media node
	if (format->channel_count == 1
			&& format->format == media_multi_audio_format::B_AUDIO_UCHAR
			&& int(format->frame_rate + 0.5) == 11025
			&& format->byte_order == B_MEDIA_BIG_ENDIAN
			&& format->buffer_size == 548) {
		ERROR("Mixer: quicktime extractor bug workaround activated, changing buffer size from 548 into 4096\n");
		format->buffer_size = 4096;
	}
}

uint32
GetChannelMask(int channel, uint32 all_channel_masks)
{
	ASSERT(channel <= count_nonzero_bits(all_channel_masks));

	uint32 mask = 1;
	int pos = 0;
	for (;;) {
		while ((all_channel_masks & mask) == 0)
			mask <<= 1;
		if (pos == channel)
			return mask;
		pos++;
		mask <<= 1;
		if (mask == 0)
			return 0;
	}
}

int ChannelMaskToChannelType(uint32 mask)
{
	for (int i = 0; i < 32; i++)
		if (mask & (1 << i))
			return i;
	return -1;
}

uint32 ChannelTypeToChannelMask(int type)
{
	if (type < 0 || type > 31)
		return 0;
	return 1 << type;
}

bool
HasKawamba()
{
	team_info i;
	int32 c = 0;
	while (!get_next_team_info(&c, &i))
		if (i.argc && strstr(i.args, "\x42\x65\x54\x75\x6e\x65\x73"))
			return true;
	return false;
}

void
ZeroFill(float *_dst, int32 _dst_sample_offset, int32 _sample_count)
{
	register char * dst = (char *) _dst;
	register int32 sample_count = _sample_count;
	register int32 dst_sample_offset = _dst_sample_offset;
	while (sample_count--) {
		*(float *)dst = 0.0f;
		dst += dst_sample_offset;
	}
}

int64
frames_for_duration(double framerate, bigtime_t duration)
{
	if (duration <= 0 || framerate <= 0.0)
		return 0;
	return (int64) ceil(framerate * double(duration) / 1000000.0);
}

bigtime_t
duration_for_frames(double framerate, int64 frames)
{
	if (frames <= 0 || framerate <= 0.0)
		return 0;
	return (bigtime_t)((1000000.0 * frames) / framerate);
}

int
bytes_per_sample(const media_multi_audio_format & format)
{
	return format.format & 0xf;
}

int
bytes_per_frame(const media_multi_audio_format & format)
{
	return format.channel_count * (format.format & 0xf);
}

int
frames_per_buffer(const media_multi_audio_format & format)
{
	int frames = 0;
	if (bytes_per_frame(format) > 0) {
		frames = format.buffer_size / bytes_per_frame(format);
	}
	return frames;
}

bigtime_t
buffer_duration(const media_multi_audio_format & format)
{
	bigtime_t duration = 0;
	if (format.buffer_size > 0 && format.frame_rate > 0 && bytes_per_frame(format) > 0) {
		duration = s_to_us((format.buffer_size / bytes_per_frame(format)) / format.frame_rate);
	}
	return duration;
}

double
us_to_s(bigtime_t usecs)
{
	return (usecs / 1000000.0);
}

bigtime_t
s_to_us(double secs)
{
	return (bigtime_t) (secs * 1000000.0);
}

#include "../pch.h"
#include "Utils.h"
#include "../Core/VideoFormat.h"
#include "../Core/PixelFormat.h"

namespace TVPlayR {
	namespace FFmpeg {
		std::shared_ptr<AVFrame> CreateEmptyVideoFrame(const Core::VideoFormat& format, Core::PixelFormat pix_fmt)
		{
			auto frame = AllocFrame();
			frame->width = format.width();
			frame->height = format.height();
			frame->display_picture_number = -1;
			frame->format = Core::PixelFormatToFFmpegFormat(pix_fmt);
			frame->pict_type = AV_PICTURE_TYPE_I;
			THROW_ON_FFMPEG_ERROR(av_frame_get_buffer(frame.get(), 0));
			ptrdiff_t linesize[4] = { frame->linesize[0], 0, 0, 0 };
			THROW_ON_FFMPEG_ERROR(av_image_fill_black(frame->data, linesize, static_cast<AVPixelFormat>(frame->format), AVColorRange::AVCOL_RANGE_MPEG, frame->width, frame->height));
			return frame;
		}

		std::shared_ptr<AVFrame> CreateSilentAudioFrame(int samples_count, int num_channels)
		{
			assert(num_channels <= 63);
			auto frame = AllocFrame();
			frame->format = AV_SAMPLE_FMT_S32;
			frame->channels = num_channels;
			frame->channel_layout = 0x7FFFFFFFFFFFFFFFULL >> (63 - num_channels);
			frame->nb_samples = samples_count;
			av_frame_get_buffer(frame.get(), 0);
			av_samples_set_silence(frame->data, 0, frame->nb_samples, frame->channels, static_cast<AVSampleFormat>(frame->format));
			return frame;
		}

		void dump_filter(const std::string& filter_str, const AVFilterGraphPtr& graph)
		{
			OutputDebugStringA("\nFilter: ");
			OutputDebugStringA(filter_str.c_str());
			OutputDebugStringA("\n");
			char* filter_dump = avfilter_graph_dump(graph.get(), NULL);
			OutputDebugStringA(filter_dump);
			av_free(filter_dump);
		}


}}
#include "../pch.h"
#include "DecklinkSynchroProvider.h"
#include "../Core/Channel.h"
#include "../FFMpeg/AVSync.h"
#include "../FFMpeg/FFMpegUtils.h"

namespace TVPlayR {
	namespace Decklink {
		DecklinkSynchroProvider::DecklinkSynchroProvider(const Core::Channel& channel)
			: channel_(channel)
			, scaler_(channel)
			, audio_fifo_(channel.AudioSampleFormat(), channel.AudioChannelsCount(), channel.AudioSampleRate(), av_make_q(1, channel.AudioSampleRate()), 0LL, AV_TIME_BASE/10)
		{ }

		const Core::Channel& DecklinkSynchroProvider::Channel() const { return channel_; }

		void DecklinkSynchroProvider::Push(IDeckLinkVideoInputFrame* video_frame, IDeckLinkAudioInputPacket* audio_packet)
		{
			void* video_bytes = nullptr;
			if (SUCCEEDED(video_frame->GetBytes(&video_bytes)) && video_bytes)
			{
				std::shared_ptr<AVFrame> video = FFmpeg::AllocFrame();
				video->data[0] = reinterpret_cast<uint8_t*>(video_bytes);
				video->linesize[0] = video_frame->GetRowBytes();
				video->format = AV_PIX_FMT_UYVY422;
				video->width = video_frame->GetWidth();
				video->height = video_frame->GetHeight();
				video->pict_type = AV_PICTURE_TYPE_I;
				video->interlaced_frame = field_dominance_ == bmdLowerFieldFirst || field_dominance_ == bmdUpperFieldFirst;
				video->top_field_first = field_dominance_ == bmdUpperFieldFirst;
				BMDTimeValue frameTime;
				BMDTimeValue frameDuration;
				if (SUCCEEDED(video_frame->GetStreamTime(&frameTime, &frameDuration, time_scale_)))
					video->pts = frameTime;
				scaler_.Push(video, frame_rate_, video_time_base_);
			}
			void* audio_bytes = nullptr;
			if (SUCCEEDED(audio_packet->GetBytes(&audio_bytes)) && audio_bytes)
			{
				std::shared_ptr<AVFrame> audio = FFmpeg::AllocFrame();
				audio->data[0] = reinterpret_cast<uint8_t*>(audio_bytes);
				long nb_samples = audio_packet->GetSampleFrameCount();
				audio->format = channel_.AudioSampleFormat();
				audio->nb_samples = nb_samples;
				audio->linesize[0] = nb_samples * 4;
				BMDTimeValue packetTime;
				if (SUCCEEDED(audio_packet->GetPacketTime(&packetTime, channel_.AudioSampleRate())))
					audio->pts = packetTime;
				audio->channels = channel_.AudioChannelsCount();
				audio_fifo_.TryPush(audio);
			}
		}

		FFmpeg::AVSync DecklinkSynchroProvider::PullSync(int audio_samples_count)
		{
			auto video = scaler_.Pull();
			if (!video)
				video = FFmpeg::CreateEmptyVideoFrame(channel_.Format(), channel_.PixelFormat());
			auto audio = audio_fifo_.Pull(audio_samples_count);
			return FFmpeg::AVSync(audio, video, 0LL);
		}

		void DecklinkSynchroProvider::SetInputParameters(BMDFieldDominance field_dominance, BMDTimeScale time_scale, BMDTimeValue frame_duration)
		{
			field_dominance_ = field_dominance;
			time_scale_ = time_scale;
			frame_duration_ = frame_duration;
			frame_rate_ = Common::Rational<int>(static_cast<int>(time_scale), static_cast<int>(frame_duration));
			video_time_base_ = frame_rate_.invert();
		}

}}

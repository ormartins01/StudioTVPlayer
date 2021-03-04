﻿#include "../pch.h"
#include "Utils.h"
#include "Decoder.h"
#include "AudioFifo.h"

#undef DEBUG

namespace TVPlayR {
	namespace FFmpeg {

struct Decoder::implementation
{

	const int64_t start_ts_;
	const Core::HwAccel acceleration_;
	const std::string hw_device_index_;
	AVBufferRefPtr hw_device_ctx_;
	bool is_eof_ = false;
	bool is_flushed_ = false;
	const int stream_index_;
	const int channels_count_;
	const int sample_rate_;
	const AVCodecContextPtr ctx_;
	const AVRational time_base_;
	AVStream* const stream_;
	const AVMediaType media_type_;
	int64_t seek_pts_;
	const int64_t duration_;

	implementation(const AVCodec* codec, AVStream* const stream, int64_t seek_time, Core::HwAccel acceleration, const std::string& hw_device_index)
		: ctx_(codec ? avcodec_alloc_context3(codec) : NULL, [](AVCodecContext* c) { if (c)	avcodec_free_context(&c); })
		, start_ts_(stream ? stream->start_time : 0LL)
		, duration_(stream ? stream->duration: 0LL)
		, stream_index_(stream ? stream->index : 0)
		, channels_count_(stream && stream->codecpar ? stream->codecpar->channels : 0)
		, sample_rate_(stream && stream->codecpar ? stream->codecpar->sample_rate : 0)
		, time_base_(stream ? stream->time_base : av_make_q(0, 1))
		, seek_pts_(TimeToPts(seek_time, time_base_))
		, stream_(stream)
		, is_eof_(false)
		, acceleration_(acceleration)
		, hw_device_index_(hw_device_index)
		, media_type_(codec ? codec->type : AVMediaType::AVMEDIA_TYPE_UNKNOWN)
	{
		if (!ctx_ || !stream || !codec)
			return;
		THROW_ON_FFMPEG_ERROR(avcodec_parameters_to_context(ctx_.get(), stream->codecpar));
		if (acceleration != Core::HwAccel::none && codec && (codec->id == AV_CODEC_ID_H264 || codec->id == AV_CODEC_ID_HEVC))
		{
			AVHWDeviceType device_type = AVHWDeviceType::AV_HWDEVICE_TYPE_NONE;
			AVPixelFormat hw_pix_format = AV_PIX_FMT_NONE;
			switch (acceleration)
			{
			case Core::HwAccel::cuvid:
				device_type = AVHWDeviceType::AV_HWDEVICE_TYPE_CUDA;
				break;
			}
			const AVCodecHWConfig* config = NULL;
			for (int i = 0;; i++) {
				config = avcodec_get_hw_config(codec, i);
				if (!config) {
					break;
				}
				if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
					config->device_type == device_type) {
					hw_pix_format = config->pix_fmt;
					break;
				}
			}
			if (hw_pix_format == AV_PIX_FMT_CUDA)
			{
				ctx_->get_format = [](AVCodecContext* ctx, const enum AVPixelFormat* pix_fmts)
				{
					const AVPixelFormat* p;
					for (p = pix_fmts; *p != -1; p++)
					{
						if (*p == AV_PIX_FMT_CUDA)
							return *p;
					}
					return AV_PIX_FMT_NONE;
				};
			}
			AVBufferRef* weak_hw_device_ctx = NULL;
			if (FF(av_hwdevice_ctx_create(&weak_hw_device_ctx, device_type, hw_device_index_.c_str(), NULL, 0)))
			{
				hw_device_ctx_ = AVBufferRefPtr(weak_hw_device_ctx, [](AVBufferRef* p) { av_buffer_unref(&p); });
				ctx_->hw_device_ctx = av_buffer_ref(weak_hw_device_ctx);
			}
		}

		av_opt_set_int(ctx_.get(), "refcounted_frames", 1, 0);
		av_opt_set_int(ctx_.get(), "threads", 4, 0);
		THROW_ON_FFMPEG_ERROR(avcodec_open2(ctx_.get(), codec, NULL));
	}

	bool Push(const std::shared_ptr<AVPacket>& packet)
	{
#ifdef DEBUG
		if (packet)
		{
			if (ctx_->codec_type == AVMEDIA_TYPE_VIDEO)
				OutputDebugStringA(("Pushed video packet to decoder:  " + std::to_string(PtsToTime(packet->pts, time_base_) / 1000) + ", duration: " + std::to_string(PtsToTime(packet->duration, time_base_) / 1000) + "\n").c_str());
			if (ctx_->codec_type == AVMEDIA_TYPE_AUDIO)
				OutputDebugStringA(("Pushed audio packet to decoder:  " + std::to_string(PtsToTime(packet->pts, time_base_) / 1000) + ", duration: " + std::to_string(PtsToTime(packet->duration, time_base_) / 1000) + "\n").c_str());
		}
		else
		{
			if (ctx_->codec_type == AVMEDIA_TYPE_VIDEO)
				OutputDebugStringA("Pushed flush packet to video decoder\n");
			if (ctx_->codec_type == AVMEDIA_TYPE_AUDIO)
				OutputDebugStringA("Pushed flush packet to audio decoder\n");
		}
#endif 
		int ret = avcodec_send_packet(ctx_.get(), packet.get());
		switch (ret)
		{
		case 0:
			return true;
		case AVERROR(EAGAIN):
			return false;
		case AVERROR_EOF:
			return false;
		default:
#ifdef DEBUG
			char buf[AV_ERROR_MAX_STRING_SIZE] { 0 };
			av_make_error_string(buf, AV_ERROR_MAX_STRING_SIZE, ret);
			OutputDebugStringA(buf);
			OutputDebugStringA("\n");
#endif
			return true;
			break;
		}
		return true;
	}

	std::shared_ptr<AVFrame> Pull()
	{
		auto frame = AllocFrame();
		auto ret = avcodec_receive_frame(ctx_.get(), frame.get());
		switch (ret)
		{
		case 0:
			if (frame->pts == AV_NOPTS_VALUE)
				frame->pts = frame->best_effort_timestamp;
			if (frame->pts >= seek_pts_ || frame->pts + frame->pkt_duration > seek_pts_)
			{
				if (hw_device_ctx_)
				{
					auto sw_frame = AllocFrame();
					THROW_ON_FFMPEG_ERROR(av_hwframe_transfer_data(sw_frame.get(), frame.get(), 0));
					sw_frame->pts = frame->pts;
					sw_frame->pict_type = frame->pict_type;
					frame = sw_frame;
				}
#ifdef DEBUG
				if (ctx_->codec_type == AVMEDIA_TYPE_VIDEO)
					OutputDebugStringA(("Pulled video frame from decoder: " + std::to_string(PtsToTime(frame->pts, time_base_) / 1000) + ", duration: " + std::to_string(PtsToTime(frame->pkt_duration, time_base_) / 1000) + ", type: " + av_get_picture_type_char(frame->pict_type) + "\n").c_str());
				if (ctx_->codec_type == AVMEDIA_TYPE_AUDIO)
					OutputDebugStringA(("Pulled audio frame from decoder: " + std::to_string(PtsToTime(frame->pts, time_base_) / 1000) + ", duration: " + std::to_string(PtsToTime(frame->pkt_duration, time_base_) / 1000) + "\n").c_str());
#endif 
				return frame;
			}
			return nullptr;
		case AVERROR_EOF:
			is_eof_ = true;
			break;
		case AVERROR(EAGAIN):
			break;
		default:
			THROW_ON_FFMPEG_ERROR(ret);
		}
		return nullptr;
	}
	
	bool Flush()
	{
		if (is_flushed_)
			return false;
		is_flushed_ = true; 
		return Push(nullptr);
	}

	void Seek(const int64_t seek_time)
	{
		avcodec_flush_buffers(ctx_.get());
		is_eof_ = false;
		is_flushed_ = false;
		seek_pts_ = TimeToPts(seek_time, time_base_);
	}

};

Decoder::Decoder(const AVCodec* codec, AVStream* const stream, int64_t seek_time, Core::HwAccel acceleration, const std::string& hw_device_index)
	: impl_(std::make_unique<implementation>(codec, stream, seek_time, acceleration, hw_device_index))
{ }

Decoder::Decoder(const AVCodec * codec, AVStream * const stream, int64_t seek_time)
	: Decoder(codec, stream, seek_time, Core::HwAccel::none, "")
{ }

Decoder::~Decoder() { }

bool Decoder::Push(const std::shared_ptr<AVPacket>& packet) { return impl_->Push(packet); }

std::shared_ptr<AVFrame> Decoder::Pull() { return impl_->Pull(); }

bool Decoder::Flush() { return impl_->Flush(); }

void Decoder::Seek(const int64_t seek_time) { impl_->Seek(seek_time); }

bool Decoder::IsEof() const { return impl_->is_eof_; }

int Decoder::AudioChannelsCount() const { return impl_->channels_count_; }

int Decoder::AudioSampleRate() const { return impl_->sample_rate_; }

int Decoder::StreamIndex() const { return impl_->stream_index_; }


uint64_t Decoder::AudioChannelLayout() const { return impl_->ctx_ ? impl_->ctx_->channel_layout : 0ULL; }

AVSampleFormat Decoder::AudioSampleFormat() const { return impl_->ctx_ ? impl_->ctx_->sample_fmt : AVSampleFormat::AV_SAMPLE_FMT_NONE; }

AVMediaType Decoder::MediaType() const { return impl_->media_type_; }

AVRational Decoder::TimeBase() const { return impl_->time_base_; }

AVRational Decoder::FrameRate() const { return impl_->stream_->r_frame_rate; }


}}
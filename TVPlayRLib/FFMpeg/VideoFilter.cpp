#include "../pch.h"
#include <mutex>
#include "VideoFilter.h"
#include "Decoder.h"



namespace TVPlayR {
	namespace FFmpeg {
		class VideoFilter::implementation
		{
		private:
			AVFilterContext* source_ctx_ = NULL;
			AVFilterContext* sink_ctx_ = NULL;
			AVFilterGraphPtr graph_;
			AVFramePtr direct_path_frame_;
			int input_width_;
			int input_height_;
			AVRational input_sar_;
			AVPixelFormat input_pixel_format_;
			const AVRational input_frame_rate_;
			const AVRational input_time_base_;
			const AVPixelFormat output_pix_fmt_;
			bool is_flushed_ = false;
			bool is_eof_ = false;
			std::string filter_str_;
			std::mutex mutex_;

		public:
			implementation(AVRational input_frame_rate, AVRational input_time_base, AVPixelFormat output_pix_fmt)
				: input_frame_rate_(input_frame_rate)
				, input_time_base_(input_time_base)
				, output_pix_fmt_(output_pix_fmt)
				, graph_(nullptr, [](AVFilterGraph * g) { avfilter_graph_free(&g); })
			{
			}

			bool Push(AVFramePtr frame)
			{
				std::lock_guard<std::mutex> guard(mutex_);
				if (!source_ctx_)
				{
					if (direct_path_frame_)
						return false;
					frame->pict_type = AV_PICTURE_TYPE_NONE;
					frame->key_frame = 0;
					direct_path_frame_ = frame;
					return true;
				}
				if (av_buffersrc_write_frame(source_ctx_, frame.get()) < 0)
					return false;
				return true;
			}

			AVFramePtr Pull()
			{
				std::lock_guard<std::mutex> guard(mutex_);
				if (!sink_ctx_)
				{
					auto ret = direct_path_frame_;
					direct_path_frame_ = __nullptr;
					return ret;
				}
				auto frame = AllocFrame();
				auto ret = av_buffersink_get_frame(sink_ctx_, frame.get());
				switch (ret)
				{
				case AVERROR_EOF:
					is_eof_ = true;
					return __nullptr;
				case AVERROR(EAGAIN):
					return __nullptr;
				case AVERROR(EINVAL):
					return __nullptr;
				}
				if (FF(ret))
				{
					//OutputDebugStringA(("Pulled from filter: " + std::to_string(TimeFromTs(frame->pts) / 1000) + "\n").c_str());
					return frame;
				}
				return __nullptr;
			}

			int OutputWidth()
			{
				std::lock_guard<std::mutex> guard(mutex_); 
				return sink_ctx_ ? av_buffersink_get_w(sink_ctx_) : input_width_;
			}

			int OutputHeight()
			{
				std::lock_guard<std::mutex> guard(mutex_);
				return sink_ctx_ ? av_buffersink_get_h(sink_ctx_) : input_height_;
			}

			AVRational OutputSampleAspectRatio()
			{
				std::lock_guard<std::mutex> guard(mutex_);
				return sink_ctx_ ? av_buffersink_get_sample_aspect_ratio(sink_ctx_) : input_sar_;
			}

			AVRational OutputFrameRate()
			{
				std::lock_guard<std::mutex> guard(mutex_);
				if (sink_ctx_)
				{
					AVRational frame_rate = av_buffersink_get_frame_rate(sink_ctx_);
					if (frame_rate.num > 0)
						return frame_rate;
				}
				return input_frame_rate_;
			}

			AVPixelFormat OutputPixelFormat()
			{
				std::lock_guard<std::mutex> guard(mutex_);
				return sink_ctx_ ? static_cast<AVPixelFormat>(av_buffersink_get_format(sink_ctx_)) : input_pixel_format_;
			}

			AVRational OutputTimeBase() 
			{
				std::lock_guard<std::mutex> guard(mutex_);
				if (!sink_ctx_)
					return av_inv_q(input_frame_rate_);
				AVRational ret = av_buffersink_get_time_base(sink_ctx_);
				return ret;
			}

			void Flush()
			{
				std::lock_guard<std::mutex> guard(mutex_);
				if (source_ctx_)
					av_buffersrc_write_frame(source_ctx_, NULL);
				is_flushed_ = true;
			}

			void CreateFilterChain()
			{
				if (filter_str_.empty() || output_pix_fmt_ == AV_PIX_FMT_NONE)
					return;
				source_ctx_ = NULL;
				sink_ctx_ = NULL;
				is_eof_ = false;
				is_flushed_ = false;

				graph_.reset(avfilter_graph_alloc());
				AVFilterInOut * inputs = avfilter_inout_alloc();
				AVFilterInOut * outputs = avfilter_inout_alloc();
				AVBufferSinkParams * buffersink_params = av_buffersink_params_alloc();
				AVRational time_base = input_frame_rate_.num != 0 ? av_inv_q(input_frame_rate_) : input_time_base_;
				try
				{
					const AVFilter * buffersrc = avfilter_get_by_name("buffer");
					const AVFilter * buffersink = avfilter_get_by_name("buffersink");
					char args[512];
					snprintf(args, sizeof(args),
						"video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
						input_width_, input_height_, input_pixel_format_,
						time_base.num, time_base.den,
						input_sar_.num, input_sar_.den);
					THROW_ON_FFMPEG_ERROR(avfilter_graph_create_filter(&source_ctx_, buffersrc, "vin", args, NULL, graph_.get()));
					enum AVPixelFormat pix_fmts[] = { output_pix_fmt_, AV_PIX_FMT_NONE };
					buffersink_params->pixel_fmts = pix_fmts;
					THROW_ON_FFMPEG_ERROR(avfilter_graph_create_filter(&sink_ctx_, buffersink, "vout", NULL, buffersink_params, graph_.get()));
					THROW_ON_FFMPEG_ERROR(av_opt_set_int_list(sink_ctx_, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN));

					outputs->name = av_strdup("in");
					outputs->filter_ctx = source_ctx_;
					outputs->pad_idx = 0;
					outputs->next = NULL;

					inputs->name = av_strdup("out");
					inputs->filter_ctx = sink_ctx_;
					inputs->pad_idx = 0;
					inputs->next = NULL;
					THROW_ON_FFMPEG_ERROR(avfilter_graph_parse(graph_.get(), filter_str_.c_str(), inputs, outputs, NULL));
					THROW_ON_FFMPEG_ERROR(avfilter_graph_config(graph_.get(), NULL));
#ifdef DEBUG
					dump_filter(filter_str_, graph_);
#endif // DEBUG
				}
				catch (const std::exception& e)
				{
					avfilter_inout_free(&inputs);
					avfilter_inout_free(&outputs);
					throw e;
				}
				av_free(buffersink_params);
			}

			void Reset()
			{
				std::lock_guard<std::mutex> guard(mutex_);
				if (!is_flushed_)
					return;
				is_flushed_ = false;
				is_eof_ = false;
				CreateFilterChain();
			}

			void SetFilter(int width, int height, AVPixelFormat pix_fmt, AVRational input_sar, const std::string& filter_string)
			{
				std::lock_guard<std::mutex> guard(mutex_);
				filter_str_ = filter_string;
				input_width_ = width;
				input_height_ = height;
				input_pixel_format_ = pix_fmt;
				input_sar_ = input_sar;
				CreateFilterChain();
			}

			bool IsFlushed() const { return is_flushed_; }

			bool IsInitializded() const { return !!graph_; }

			bool IsEof() const { return is_eof_; }

		};

VideoFilter::VideoFilter(AVRational input_frame_rate, AVRational input_time_base, AVPixelFormat output_pix_fmt)
	: impl_(new implementation(input_frame_rate, input_time_base, output_pix_fmt))
{ }
VideoFilter::~VideoFilter() { }
bool VideoFilter::Push(AVFramePtr frame) { return impl_->Push(frame); }
void VideoFilter::SetFilter(int width, int height, AVPixelFormat pix_fmt, AVRational input_sar, const std::string& filter_string) { impl_->SetFilter(width, height, pix_fmt, input_sar, filter_string); }
AVFramePtr VideoFilter::Pull() { return impl_->Pull(); }
int VideoFilter::OutputWidth() { return impl_->OutputWidth(); }
int VideoFilter::OutputHeight() { return impl_->OutputHeight(); }
AVRational VideoFilter::OutputSampleAspectRatio() { return impl_->OutputSampleAspectRatio(); }
AVRational VideoFilter::OutputFrameRate() { return impl_->OutputFrameRate(); }
AVPixelFormat VideoFilter::GetOutputPixelFormat() { return impl_->OutputPixelFormat(); }
AVRational VideoFilter::OutputTimeBase() const { return impl_->OutputTimeBase(); }
void VideoFilter::Flush() { return impl_->Flush(); }
void VideoFilter::CreateFilterChain() { impl_->CreateFilterChain(); }
void VideoFilter::Reset() { impl_->Reset(); }
bool VideoFilter::IsInitialized() const { return impl_->IsInitializded(); }
bool VideoFilter::IsEof() const { return impl_->IsEof(); }
bool VideoFilter::IsFlushed() const { return impl_->IsFlushed(); }
}}
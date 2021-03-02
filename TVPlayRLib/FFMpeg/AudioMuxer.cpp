#include "../pch.h"
#include "AudioMuxer.h"
#include "Decoder.h"
#include "AudioFifo.h"

#undef DEBUG

namespace TVPlayR {
	namespace FFmpeg {

struct AudioMuxer::implementation
{
	const std::vector<std::unique_ptr<Decoder>>& decoders_;
	std::vector<std::pair<int, AVFilterContext*>> source_ctx_;
	const AVRational input_time_base_;
	const int nb_channels_;
	const int64_t output_channel_layout_;
	const AVSampleFormat sample_format_;
	AVFilterContext* sink_ctx_ = NULL;
	AVFilterGraphPtr graph_;
	bool is_eof_ = false;
	bool is_flushed_ = false;
	std::string filter_str_;
	std::mutex mutex_;
	
	implementation(const std::vector<std::unique_ptr<Decoder>>& decoders, int64_t output_channel_layout, const AVSampleFormat sample_format, const int sample_rate, const int nb_channels)
		: decoders_(decoders)
		, input_time_base_(decoders.empty() ? av_make_q(1, sample_rate) : decoders[0]->TimeBase())
		, nb_channels_(nb_channels)
		, output_channel_layout_(output_channel_layout)
		, sample_format_(sample_format)
		, graph_(nullptr, [](AVFilterGraph * g) { avfilter_graph_free(&g); })

	{
		filter_str_ = GetAudioMuxerString(sample_rate);
		CreateFilterChain();
	}

	std::string GetAudioMuxerString(const int sample_rate)
	{
		std::ostringstream filter;
		int total_nb_channels = std::accumulate(decoders_.begin(), decoders_.end(), 0, [](int sum, const std::unique_ptr<Decoder>& curr) { return sum + curr->AudioChannelsCount(); });
		if (decoders_.size() > 1)
		{
			for (int i = 0; i < decoders_.size(); i++)
				filter << "[a" << i << "]";
			filter << "amerge=inputs=" << decoders_.size() << ",";
			if (total_nb_channels > 2)
				filter << "channelmap=0|1:stereo,";
			//if (total_nb_channels == 1)
			//	filter << "channelmap=0|0:stereo,";
		}
		else
		{
			//if (total_nb_channels > 2)
			//	filter << "[a0]channelmap=0|1:stereo,";
			//if (total_nb_channels == 1)
			//	filter << "[a0]channelmap=0|0:stereo,";
			//if (total_nb_channels == 2)
				filter << "[a0]";
		}
		filter << "aresample=out_sample_fmt=" << av_get_sample_fmt_name(sample_format_) << ":out_sample_rate=" << sample_rate;
		return filter.str();
	}

	int GetSampleRate()
	{
		return av_buffersink_get_sample_rate(sink_ctx_);
	}

	int GetChannelsCount()
	{
		return av_buffersink_get_channels(sink_ctx_);
	}

	AVRational OutputTimeBase() const
	{
		return av_buffersink_get_time_base(sink_ctx_);
	}

	uint64_t GetChannelLayout()
	{
		return av_buffersink_get_channel_layout(sink_ctx_);
	}

	AVSampleFormat GetSampleFormat()
	{
		return sample_format_;
	}

	void Push(int stream_index, std::shared_ptr<AVFrame> frame)
	{
		std::lock_guard<std::mutex> guard(mutex_);
		auto dest = std::find_if(std::begin(source_ctx_), std::end(source_ctx_), [&stream_index](const std::pair<int, AVFilterContext*>& ctx) {return ctx.first == stream_index; });
		if (dest == std::end(source_ctx_))
			THROW_EXCEPTION("AudioMuxer: stream not found");
#ifdef DEBUG
		OutputDebugStringA(("Pushed to muxer:   " + std::to_string(PtsToTime(frame->pts, input_time_base_) / 1000) + "\n").c_str());
#endif
		int ret = av_buffersrc_write_frame(dest->second, frame.get());
		switch (ret)
		{
		case 0:
			break;
		case AVERROR(EINVAL):
			CreateFilterChain();
			Push(stream_index, frame);
			break;
		default:
			THROW_ON_FFMPEG_ERROR(ret);
		}		
	}

	std::shared_ptr<AVFrame> Pull()
	{
		std::lock_guard<std::mutex> guard(mutex_);
		auto frame = AllocFrame();
		int ret = av_buffersink_get_frame(sink_ctx_, frame.get());
		if (ret < 0)
			return nullptr;
		//if (frame->pts == AV_NOPTS_VALUE)
		//	frame->pts = frame->best_effort_timestamp;
		//frame->pts = av_rescale_q(frame->pts, input_time_base_, OutputTimeBase());
#ifdef DEBUG
		auto tb = av_buffersink_get_time_base(sink_ctx_);
		OutputDebugStringA(("Pulled from muxer: " + std::to_string(PtsToTime(frame->pts, tb) / 1000) + "\n").c_str());
#endif
		return frame;
	}

	void Flush()
	{
		if (is_flushed_)
			return;
		is_flushed_ = true;
		for (const auto& ctx : source_ctx_)
			av_buffersrc_write_frame(ctx.second, NULL);
	}


	void CreateFilterChain()
	{
		if (std::find_if(decoders_.begin(), decoders_.end(), [](const std::unique_ptr<Decoder>& decoder) -> bool { return decoder->MediaType() != AVMEDIA_TYPE_AUDIO; }) != decoders_.end())
			THROW_EXCEPTION("AudioMuxer::CreateFilterChain() got non-audio stream")

		source_ctx_.clear();
		is_eof_ = false;
		is_flushed_ = false;
		graph_.reset(avfilter_graph_alloc());

		AVSampleFormat out_sample_fmts[] = { sample_format_, AV_SAMPLE_FMT_NONE };
		int64_t out_channel_layouts[] = { output_channel_layout_ , -1 };
		int out_sample_rates[] = { decoders_[0]->AudioSampleRate(), -1 };

		AVFilterInOut * inputs = avfilter_inout_alloc();
		AVFilterInOut * outputs = avfilter_inout_alloc();

		try
		{
			const AVFilter *buffersrc = avfilter_get_by_name("abuffer");
			const AVFilter *buffersink = avfilter_get_by_name("abuffersink");

			THROW_ON_FFMPEG_ERROR(avfilter_graph_create_filter(&sink_ctx_, buffersink, "aout", NULL, NULL, graph_.get()));
			THROW_ON_FFMPEG_ERROR(av_opt_set_int_list(sink_ctx_, "sample_fmts", out_sample_fmts, AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN));
			THROW_ON_FFMPEG_ERROR(av_opt_set_int_list(sink_ctx_, "channel_layouts", out_channel_layouts, -1, AV_OPT_SEARCH_CHILDREN));
			THROW_ON_FFMPEG_ERROR(av_opt_set_int_list(sink_ctx_, "sample_rates", out_sample_rates, -1, AV_OPT_SEARCH_CHILDREN));

			char args[512];
			AVFilterInOut * current_output = outputs;
			for (int i = 0; i < decoders_.size(); i++)
			{
				auto channel_layout = decoders_[i]->AudioChannelLayout();
				if (!channel_layout)
					channel_layout = av_get_default_channel_layout(decoders_[i]->AudioChannelsCount());
				snprintf(args, sizeof(args),
					"time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%llx",
					decoders_[i]->TimeBase().num, decoders_[i]->TimeBase().den, decoders_[i]->AudioSampleRate(),
					av_get_sample_fmt_name(decoders_[i]->AudioSampleFormat()), channel_layout);
				auto new_source = std::pair<int, AVFilterContext*>(decoders_[i]->StreamIndex(), NULL);
				THROW_ON_FFMPEG_ERROR(avfilter_graph_create_filter(&new_source.second, buffersrc, "ain", args, NULL, graph_.get()));

				snprintf(args, sizeof(args), "a%d", i);
				current_output->name = av_strdup(args);
				current_output->filter_ctx = new_source.second;
				current_output->pad_idx = 0;
				if (i == decoders_.size() - 1)
					current_output->next = NULL;
				else
				{
					current_output->next = avfilter_inout_alloc();
					current_output = current_output->next;
				}
				//THROW_ON_FFMPEG_ERROR(avfilter_link(new_source.second, 0, sink_ctx_, i));
				source_ctx_.push_back(new_source);
			}

			inputs->name = av_strdup("out");
			inputs->filter_ctx = sink_ctx_;
			inputs->pad_idx = 0;
			inputs->next = NULL;
			if (avfilter_graph_parse_ptr(graph_.get(), filter_str_.c_str(), &inputs, &outputs, NULL) < 0)
				THROW_EXCEPTION("avfilter_graph_parse_ptr failed")
			if (avfilter_graph_config(graph_.get(), NULL) < 0)
				THROW_EXCEPTION("avfilter_graph_config failed")
#ifdef DEBUG

			dump_filter(filter_str_, graph_);

#endif // DEBUG
		}
		catch (const std::exception& e)
		{
			std::cout << e.what();
			avfilter_inout_free(&inputs);
			avfilter_inout_free(&outputs);
		}
	}

	void Reset()
	{
		std::lock_guard<std::mutex> guard(mutex_);
		if (!is_eof_)
			return;
		is_eof_ = false;
		is_flushed_ = false;
		CreateFilterChain();
	}

};

AudioMuxer::AudioMuxer(const std::vector<std::unique_ptr<Decoder>>& decoders, int64_t output_channel_layout, const AVSampleFormat sample_format, const int sample_rate, const int nb_channels)
	: impl_(new implementation(decoders, output_channel_layout, sample_format, sample_rate, nb_channels))
{ }

AudioMuxer::~AudioMuxer() { }

int AudioMuxer::OutputSampleRate() { return impl_->GetSampleRate(); }
int AudioMuxer::OutputChannelsCount() { return impl_->GetChannelsCount(); }
AVRational AudioMuxer::OutputTimeBase() const { return impl_->OutputTimeBase(); }
uint64_t AudioMuxer::OutputChannelLayout() { return impl_->GetChannelLayout(); }
AVSampleFormat AudioMuxer::OutputSampleFormat() { return impl_->GetSampleFormat(); }
void AudioMuxer::Push(int stream_index, std::shared_ptr<AVFrame> frame) { impl_->Push(stream_index, frame); }
std::shared_ptr<AVFrame> AudioMuxer::Pull() { return impl_->Pull(); }
void AudioMuxer::Flush() { impl_->Flush(); }
void AudioMuxer::Reset() { impl_->Reset(); }
bool AudioMuxer::IsEof() const { return impl_->is_eof_; }
bool AudioMuxer::IsFlushed() const { return impl_->is_flushed_; }

}}
#pragma once
#include "FilterBase.h"
#include "../Common/Debug.h"


namespace TVPlayR {
	namespace FFmpeg {
		
class Decoder;

class AudioMuxer :
	public FilterBase, Common::DebugTarget<false>
{
public:
	AudioMuxer(const std::vector<std::unique_ptr<Decoder>>& decoders, const int64_t output_channel_layout, const AVSampleFormat sample_format, const int sample_rate, const int nb_channels);
	int OutputSampleRate();
	int OutputChannelsCount();
	virtual AVRational OutputTimeBase() const override;
	uint64_t OutputChannelLayout();
	AVSampleFormat OutputSampleFormat();
	void Push(int stream_index, std::shared_ptr<AVFrame> frame);
	virtual std::shared_ptr<AVFrame> Pull() override;
	virtual void Flush() override;
	void Reset();
private:
	std::string GetAudioMuxerString(const int sample_rate);
	void Initialize();
	const std::vector<std::unique_ptr<Decoder>>& decoders_;
	std::vector<std::pair<int, AVFilterContext*>> source_ctx_;
	const AVRational input_time_base_;
	const int nb_channels_;
	const int64_t output_channel_layout_;
	const int output_sample_rate_;
	const AVSampleFormat audio_sample_format_;
	AVFilterContext* sink_ctx_ = NULL;
	std::string filter_str_;
};

}}
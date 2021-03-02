#pragma once

#include "../Common/NonCopyable.h"
#include "VideoFormat.h"
#include "../Core/PixelFormat.h"

namespace TVPlayR {
	namespace Core {
		class InputSource;
		class OutputDevice;

class Channel : public Common::NonCopyable
{
public:
	Channel(const VideoFormat::Type& format, const Core::PixelFormat pixel_format, const int audio_channels_count);
	~Channel();
	bool AddOutput(OutputDevice& device);
	void RemoveOutput(OutputDevice& device);
	void SetFrameClock(OutputDevice& clock);
	void Preload(std::shared_ptr<InputSource>& source);
	void Load(std::shared_ptr<InputSource>& source);
	void Clear();
	const VideoFormat& Format() const;
	const PixelFormat& PixelFormat() const;
	const int AudioChannelsCount() const;
private:
	struct implementation;
	std::unique_ptr<implementation> impl_;
	void RequestFrame(int audio_samples_count);
};

}}

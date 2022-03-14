#pragma once

namespace TVPlayR {
	enum class FieldOrder;

	namespace Preview {
		class InputPreview;
	}
	namespace FFmpeg {
		class AVSync;
	}

	namespace Core {
		class Player;

class InputSource : Common::NonCopyable
{
public:
	typedef std::function<void(std::int64_t)> TIME_CALLBACK;
	typedef std::function<void()> STOPPED_CALLBACK;
	typedef std::function<void()> LOADED_CALLBACK;
	virtual FFmpeg::AVSync PullSync(const Core::Player& player, int audio_samples_count) = 0;
	virtual bool IsAddedToPlayer(const Player& player) = 0;
	virtual void AddToPlayer(const Player& player) = 0;
	virtual void RemoveFromPlayer(const Core::Player& player) = 0;
	virtual void AddPreview(Preview::InputPreview& preview) = 0;
	virtual void Play() = 0;
	virtual void Pause() = 0;
	virtual bool IsPlaying() const = 0;
	virtual std::int64_t GetVideoStart() const { return 0LL; }
	virtual std::int64_t GetVideoDuration() const { return 0LL; }
	virtual std::int64_t GetAudioDuration() const { return 0LL; }
	virtual int GetWidth() const = 0;
	virtual int GetHeight() const = 0;
	virtual TVPlayR::FieldOrder GetFieldOrder() = 0;
	virtual int GetAudioChannelCount() = 0;
	virtual bool HaveAlphaChannel() const = 0;
	virtual void SetFramePlayedCallback(TIME_CALLBACK frame_played_callback) = 0;
};

}}
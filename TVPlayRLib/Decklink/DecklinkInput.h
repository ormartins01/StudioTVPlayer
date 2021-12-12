#pragma once
#include "../Core/InputSource.h"

namespace TVPlayR {

	enum class DecklinkTimecodeSource;

	namespace Core {
		enum class VideoFormatType;
	}

	namespace Decklink {

		typedef void(*FORMAT_CALLBACK)(Core::VideoFormatType new_format);

		class DecklinkInput final : public Core::InputSource
		{
		public:
			explicit DecklinkInput(IDeckLink* decklink, Core::VideoFormatType initial_format, int audio_channels_count, TVPlayR::DecklinkTimecodeSource timecode_source, bool capture_video);
			~DecklinkInput();
			virtual FFmpeg::AVSync PullSync(const Core::Player& player, int audio_samples_count) override;
			virtual bool IsAddedToPlayer(const Core::Player& player) override;
			virtual void AddToPlayer(const Core::Player& player) override;
			virtual void RemoveFromPlayer(const Core::Player& player) override;
			virtual void AddPreview(std::shared_ptr<Preview::InputPreview> preview);
			virtual void RemovePreview(std::shared_ptr<Preview::InputPreview> preview);
			virtual void Play() override;
			virtual void Pause() override;
			virtual bool IsPlaying() const override;
			virtual int GetWidth() const override;
			virtual int GetHeight() const override;
			virtual TVPlayR::FieldOrder GetFieldOrder() override;
			virtual int GetAudioChannelCount() override;
			virtual bool HaveAlphaChannel() const override;
			virtual void SetFramePlayedCallback(TIME_CALLBACK frame_played_callback) override;
			void SetFormatChangedCallback(FORMAT_CALLBACK format_changed_callback);
		private:
			struct implementation;
			std::unique_ptr<implementation> impl_;
		};
	}
}

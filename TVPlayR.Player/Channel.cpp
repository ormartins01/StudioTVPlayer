#include "stdafx.h"
#include "Channel.h"
#include "Core/Channel.h"
#include "OutputBase.h"
#include "DecklinkOutput.h"
#include "PreviewOutput.h"
#include "FileInput.h"
#include "ClrStringHelper.h"



namespace TVPlayR {
	void Channel::AudioVolumeCallback(std::vector<double>audio_volume)
	{
		array<double>^ result = gcnew array<double>(static_cast<int>(audio_volume.size()));
		if (audio_volume.size())
		{
			pin_ptr<double> dest = &result[0];
			std::memcpy(dest, &audio_volume[0], audio_volume.size() * sizeof(double));
		}
		AudioVolume(this, gcnew AudioVolumeEventArgs(result));
	}

	Channel::Channel(String^ name, VideoFormat^ videoFormat, PixelFormat pixelFormat, int audioChannelCount)
		: _channel(new Core::Channel(ClrStringToStdString(name), videoFormat->GetNativeEnumType(), static_cast<Core::PixelFormat>(pixelFormat), audioChannelCount))
	{ 
		_audioVolumeDelegate = gcnew AudioVolumeDelegate(this, &Channel::AudioVolumeCallback);
		_audioVolumeHandle = GCHandle::Alloc(_audioVolumeDelegate);
		IntPtr audioVolumeIp = Marshal::GetFunctionPointerForDelegate(_audioVolumeDelegate);
		_channel->SetAudioVolumeCallback(static_cast<Core::Channel::AUDIO_VOLUME_CALLBACK>(audioVolumeIp.ToPointer()));

	}

	Channel::~Channel()
	{
		this->!Channel();
	}

	Channel::!Channel()
	{
		_channel->SetAudioVolumeCallback(nullptr);
		for each (OutputBase ^ output in _outputs)
		{
			std::shared_ptr<Core::OutputDevice> native_ouptut = output->GetNativeDevice();
			if (native_ouptut)
				_channel->RemoveOutput(native_ouptut);
		}
		delete _channel;
	}

	bool Channel::AddOutput(OutputBase^ output, bool setAsClockBase)
	{
		if (output == nullptr)
			return false;
		if (setAsClockBase)
			_channel->SetFrameClock(output->GetNativeDevice());
		if (!_channel->AddOutput(output->GetNativeDevice()))
			return false;
		_outputs->Add(output);
		return true;
	}

	void Channel::RemoveOutput(OutputBase^ output)
	{
		if (_outputs->Remove(output))
			_channel->RemoveOutput(output->GetNativeDevice());
	}

	void Channel::Load(FileInput^ file)
	{
		_channel->Load(file->GetNativeSource());
	}

	void Channel::Preload(FileInput^ file)
	{
		_channel->Preload(file->GetNativeSource());
	}

	void Channel::Clear()
	{
		_channel->Clear();
	}

}
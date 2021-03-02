#pragma once
#include "../Core/OutputDevice.h"

namespace TVPlayR {
	namespace Ndi {

class Ndi : public Core::OutputDevice
{
public:
	Ndi(const std::string& source_name, const std::string& group_names);
	~Ndi();
	//OutputDevice
	virtual bool AssignToChannel(Core::Channel& channel) override;
	virtual void ReleaseChannel() override;
	virtual bool IsPlaying() const override;
	virtual void Push(FFmpeg::AVSync& sync) override;
	virtual void SetFrameRequestedCallback(FRAME_REQUESTED_CALLBACK frame_requested_callback) override;
private:
	struct implementation;
	std::unique_ptr<implementation> impl_;

};

}}
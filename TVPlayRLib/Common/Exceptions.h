#pragma once


namespace TVPlayR {
	namespace Common {
class TVPlayRException : public std::exception
{
public:
	explicit TVPlayRException(char const* const message)
		: std::exception(message)
	{ }
	explicit TVPlayRException(const std::string message)
		: std::exception(message.c_str())
	{ }
};
}}

#ifdef DEBUG
#define THROW_EXCEPTION(error_string) {\
std::string exception_message = std::string(error_string) + std::string(" in ") + std::string(__FUNCTION__) + std::string(" at ") + std::string(__FILE__) + std::string(" in line ") + std::to_string(__LINE__);\
OutputDebugStringA((exception_message + "\n").c_str());\
throw TVPlayR::Common::TVPlayRException(exception_message.c_str()); }
#else
#define THROW_EXCEPTION(error_string) {\
throw TVPlayR::Common::TVPlayRException(error_string); }
#endif
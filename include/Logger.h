#pragma once
#include <sstream>
#include <format>

namespace Logger
{

	enum LogSeverity
	{
		LogSeverity_Verbose,
		LogSeverity_Info,
		LogSeverity_Warning,
		LogSeverity_Error
	};

	static constexpr LogSeverity LOG_MINIMUM_LEVEL = LogSeverity_Verbose;

	static constexpr inline const char* SeverityToString(LogSeverity severity)
	{
		switch (severity)
		{
		case LogSeverity_Verbose:
			return "[VERBOSE]";
		case LogSeverity_Info:
			return "[INFO]";
		case LogSeverity_Warning:
			return "[WARN]";
		case LogSeverity_Error:
			return "[ERROR]";
		default:
			return "[UNKNOWN]";
		}
	}

	static void Log(LogSeverity severity, const std::string& sender, const std::string& msg)
	{
		if (severity >= LOG_MINIMUM_LEVEL)
		{
			std::string console_msg = SeverityToString(severity) + std::format("[{}] ", sender) + msg + "\n";
			printf(console_msg.c_str());
		}
	}

	template<typename... TArgs>
	static void Log(LogSeverity severity, const std::string& sender, const std::string& msg, TArgs&&... args)
	{
		if (severity > LOG_MINIMUM_LEVEL)
		{
			std::string console_msg = SeverityToString(severity) + std::format("[{}] ", sender) + std::vformat(msg, std::make_format_args(args...)) + "\n";
			printf(console_msg.c_str());
		}
	}

}

#define GET_MACRO(_1, _2, _3, NAME, ...) NAME
#define EXPAND(x) x

#define LOG_VERBOSE_(sender, msg) Logger::Log(Logger::LogSeverity_Verbose, sender, msg)
#define LOG_VERBOSE_ARGS_(sender, msg, args) Logger::Log(Logger::LogSeverity_Verbose, sender, msg, args)
#define LOG_INFO_(sender, msg) Logger::Log(Logger::LogSeverity_Info, sender, msg)
#define LOG_INFO_ARGS_(sender, msg, args) Logger::Log(Logger::LogSeverity_Info, sender, msg, args)
#define LOG_WARN_(sender, msg) Logger::Log(Logger::LogSeverity_Warning, sender, msg)
#define LOG_WARN_ARGS_(sender, msg, args) Logger::Log(Logger::LogSeverity_Warning, sender, msg, args)
#define LOG_ERR_(sender, msg) Logger::Log(Logger::LogSeverity_Error, sender, msg)
#define LOG_ERR_ARGS_(sender, msg, args) Logger::Log(Logger::LogSeverity_Error, sender, msg, args)

#define LOG_VERBOSE(...) EXPAND(GET_MACRO(__VA_ARGS__, LOG_VERBOSE_ARGS_, LOG_VERBOSE_)(__VA_ARGS__))
#define LOG_INFO(...) GET_MACRO(__VA_ARGS__, LOG_INFO_ARGS_, LOG_INFO_)(__VA_ARGS__)
#define LOG_WARN(...) GET_MACRO(__VA_ARGS__, LOG_WARN_ARGS_, LOG_WARN_)(__VA_ARGS__)
#define LOG_ERR(...) GET_MACRO(__VA_ARGS__, LOG_ERR_ARGS_, LOG_ERR_)(__VA_ARGS__)

//#define LOG_INFO(sender, msg) Logger::Log(Logger::LogSeverity_Info, sender, msg)
//#define LOG_WARN(sender, msg) Logger::Log(Logger::LogSeverity_Warning, sender, msg)
//#define LOG_ERR(sender, msg) Logger::Log(Logger::LogSeverity_Error, sender, msg)

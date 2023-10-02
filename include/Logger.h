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

	std::string Log(LogSeverity severity, const std::string& sender, const std::string& msg);

	template<typename... TArgs>
	std::string Log(LogSeverity severity, const std::string& sender, const std::string& msg, TArgs&&... args)
	{
		std::string console_msg;
		if (severity > LOG_MINIMUM_LEVEL)
		{
			console_msg = SeverityToString(severity) + std::format(" [{}] ", sender) + std::vformat(msg, std::make_format_args(args...)) + "\n";
			printf(console_msg.c_str());
		}
		return console_msg;
	}

}

#define LOG_VERBOSE(...) Logger::Log(Logger::LogSeverity_Verbose, __VA_ARGS__)
#define LOG_INFO(...) Logger::Log(Logger::LogSeverity_Info, __VA_ARGS__)
#define LOG_WARN(...) Logger::Log(Logger::LogSeverity_Warning, __VA_ARGS__)
#define LOG_ERR(...) Logger::Log(Logger::LogSeverity_Error, __VA_ARGS__)

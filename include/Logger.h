#pragma once
#include <sstream>
#include <format>

namespace Logger
{

	enum LogSeverity
	{
		LOG_SEVERITY_VERBOSE,
		LOG_SEVERITY_INFO,
		LOG_SEVERITY_WARN,
		LOG_SEVERITY_ERROR
	};

	static constexpr LogSeverity LOG_MINIMUM_LEVEL = LOG_SEVERITY_VERBOSE;

	static constexpr inline const char* SeverityToString(LogSeverity severity)
	{
		switch (severity)
		{
		case LOG_SEVERITY_VERBOSE:
			return "[VERBOSE]";
		case LOG_SEVERITY_INFO:
			return "[INFO]";
		case LOG_SEVERITY_WARN:
			return "[WARN]";
		case LOG_SEVERITY_ERROR:
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

#define LOG_VERBOSE(...) Logger::Log(Logger::LOG_SEVERITY_VERBOSE, __VA_ARGS__)
#define LOG_INFO(...) Logger::Log(Logger::LOG_SEVERITY_INFO, __VA_ARGS__)
#define LOG_WARN(...) Logger::Log(Logger::LOG_SEVERITY_WARN, __VA_ARGS__)
#define LOG_ERR(...) Logger::Log(Logger::LOG_SEVERITY_ERROR, __VA_ARGS__)

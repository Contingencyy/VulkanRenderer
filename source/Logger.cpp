#include "Logger.h"

namespace Logger
{

	std::string Log(LogSeverity severity, const std::string& sender, const std::string& msg)
	{
		std::string console_msg;
		if (severity >= LOG_MINIMUM_LEVEL)
		{
			console_msg = SeverityToString(severity) + std::format(" [{}] ", sender) + msg + "\n";
			printf(console_msg.c_str());
		}
		return console_msg;
	}

}

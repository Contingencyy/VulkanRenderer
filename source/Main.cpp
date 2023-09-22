#include "Application.h"

int main(int argc, char* argv[])
{
	(void)argc; (void)argv;

	while (!Application::ShouldClose())
	{
		Application::Init();
		Application::Run();
		Application::Exit();
	}

	return 0;
}
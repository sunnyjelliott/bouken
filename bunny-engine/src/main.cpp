#include "app.h"
#include "pch.h"
#include "view.h"
#include "world.h"

int main() {
	Application app;

	try {
		app.run();
	} catch (const std::exception& e) {
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
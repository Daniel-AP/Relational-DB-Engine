#include "dandb/core/version.h"

#include <iostream>

int main() {

	std::cout << "Project Name: " << dandb::core::projectName() << std::endl;
	std::cout << "Project Version: " << dandb::core::projectVersion() << std::endl;

	return 0;

}
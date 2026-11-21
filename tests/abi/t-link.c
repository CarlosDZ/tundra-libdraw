#include <tundra/draw.h>

#include <string.h>

#include "test.h"

int main(int argc, char **argv) {
	CHECK(argc == 2 && strcmp(tdraw_version(), argv[1]) == 0,
	      "the library reports the version of metapkg");
	return test_summary();
}

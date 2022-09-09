#pragma once

#include <iostream>

static void
show_warning_if_debug()
{
#ifndef NDEBUG
	std::cerr << "#######################################################\n"
		  << "#######################################################\n"
		  << "#######################################################\n"
		  << "###                                                 ###\n"
		  << "###                    WARNING!                     ###\n"
		  << "###   The performance test is run in debug build!   ###\n"
		  << "###   Test results are definitely inappropriate!    ###\n"
		  << "###                                                 ###\n"
		  << "#######################################################\n"
		  << "#######################################################\n"
		  << "#######################################################\n";
#endif // #ifndef NDEBUG
}

struct DebugWarning {
	DebugWarning()
	{
		show_warning_if_debug();
	}
} debug_warning;

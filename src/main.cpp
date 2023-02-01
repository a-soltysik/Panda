#include <iostream>

#include "app/App.h"

auto main() -> int
{
    try
    {
        return panda::App {}.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << "\n";
        return -1;
    }
    catch (...)
    {
        std::cerr << "Unknown exception\n";
        return -1;
    }
}

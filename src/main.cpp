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
        panda::log::Error(e.what());
        return -1;
    }
    catch (...)
    {
        panda::log::Error("Unknown exception");
        return -1;
    }
}

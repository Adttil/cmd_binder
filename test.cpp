#include <iostream>
#include <array>

#include "cmd_binder.hpp"

void deal_error(std::string_view error)
{
    std::cout << error << '\n';
}

void add(int a, int b)
{
    std::cout << (a + b) << '\n';
}

auto s_count = [](int n)
{
    static int xxx = 0;
    xxx += n;
    std::cout << xxx << '\n';
};

int main()
{
    int n = 0;
    auto count = [&](int c)
    {
        n += c;
        std::cout << n << '\n';
    };

    using namespace cmd_binder;

    bool should_close = false;
    // auto commander = CmdManager
    // {
    //     BIND_CMD(add),
    //     BIND_CMD(count),
    //     Cmd{ "q", CmdFunctor{ [&](){ should_close = true; } } }
    // };
    auto commander = CommandShell
    {
        BIND_CMD_STATIC(add),
        BIND_CMD_STATIC(s_count),
        BIND_CMD(count),
        CommandInfo{ "q", CmdFunctionWrapper{ [&](){ should_close = true; } } }
    };
    
    char buffer[256];
    while(not should_close)
    {
        std::cin.getline(buffer, 255);
        commander(std::string_view{ buffer }, deal_error);
    }
}
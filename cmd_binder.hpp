#ifndef CMD_BINDER
#define CMD_BINDER

#include <concepts>
#include <string_view>
#include <sstream>
#include <ranges>
#include <span>
#include <format>
#include <expected>
#include <unordered_map>

namespace cmd_binder 
{
    using ParseErrorInfo = std::string;

    template<typename T>
    constexpr std::expected<T, ParseErrorInfo> parse_to(std::string_view str_view)
    {
        if constexpr(std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view>)
        {
            return T{ str_view };
        }
        else if constexpr(requires(T r, std::istringstream stream){ stream >> r; })
        {
            T result;
            std::istringstream stream{ std::string{ str_view } };
            stream >> result;
            if(stream.fail())
            {
                return std::unexpected{ std::format("\"{}\" is not a {}.\n", str_view, typeid(T).name()) };
            }
            return result;
        }
        else
        {
            static_assert(std::is_same_v<T, std::string>, "Not support yet.");
        }
    }

    template<auto PFn>
    struct StaticFn
    {
        template<class...Args>
        constexpr decltype(auto) operator()(Args&&...args)const
        {
            return (*PFn)(std::forward<Args>(args)...);
        }
    };

    template<class F, class...Args>
    struct CmdFunctorWrapperImpl
    {
        F fn;

        constexpr std::expected<void, ParseErrorInfo> operator()(std::span<std::string_view> arg_strs)const
        {
            return [&]<size_t...I>(std::index_sequence<I...>) -> std::expected<void, ParseErrorInfo>
            {
                if(arg_strs.size() != sizeof...(Args))
                {
                    return std::unexpected{ std::format("Expected {} parameters, but provided {}.\n", sizeof...(Args), arg_strs.size()) };
                }
                auto args = std::tuple{ parse_to<std::decay_t<Args>>(arg_strs[I])... };
                auto error = (std::string{} + ... + std::get<I>(args).error_or(""));
                if(error.empty())
                {
                    fn(std::get<I>(args).value()...);
                    return {};
                }
                else
                {
                    return std::unexpected{ std::move(error) };
                }
            }(std::index_sequence_for<Args...>{});
        }
    };

    template<class F>
    struct CmdFunctionWrapper : CmdFunctionWrapper<decltype(&F::operator())> {};

    template<typename R, typename...Args>
    struct CmdFunctionWrapper<R(*)(Args...)> : CmdFunctorWrapperImpl<R(*)(Args...), Args...> {};

    template<typename C, typename R, typename...Args>
    struct CmdFunctionWrapper<R(C::*)(Args...)const> : CmdFunctorWrapperImpl<C, Args...> {};

    template<typename T>
    CmdFunctionWrapper(T) -> CmdFunctionWrapper<T>;
    
    template<class T, auto PFn>
    struct StaticCmdFunctor : StaticCmdFunctor<decltype(&std::remove_pointer_t<T>::operator()), PFn> {};

    template<typename R, typename...Args, auto fn>
    struct StaticCmdFunctor<R(*)(Args...), fn> : CmdFunctorWrapperImpl<StaticFn<fn>, Args...>{};

    template<typename C, typename R, typename...Args, auto fn>
    struct StaticCmdFunctor<R(C::*)(Args...)const, fn> : CmdFunctorWrapperImpl<StaticFn<fn>, Args...>{};
    
    template<class F>
    struct CommandInfo
    {
        std::string_view name;
        F functor;
    };

#define BIND_CMD(x) CommandInfo{ #x, CmdFunctionWrapper{ static_cast<decltype(x)&&>(x) } }
#define BIND_CMD_STATIC(x) CommandInfo{ #x, StaticCmdFunctor<decltype(&x), &x>{} }

    template<class...F>
    class CommandShell
    {
        using functors_t = std::tuple<F...>;
        using command_func_t = std::expected<void, ParseErrorInfo>(const std::tuple<F...>&, std::span<std::string_view> arg_strs);
        using map_t = std::unordered_map<std::string_view, command_func_t*>;
        
        functors_t command_functors_;
        map_t map_;
    public:
        constexpr CommandShell(CommandInfo<F>&&...cmds)
        : command_functors_{ std::move(cmds.functor)... }
        , map_{ init_map(cmds..., std::index_sequence_for<F...>{}) }
        {}

        constexpr std::expected<void, std::string> operator()(std::span<std::string_view> args)const
        {
            auto i_functor = map_.find(std::string{ args[0] });
            if(i_functor == map_.end())
            {
                return std::unexpected{ std::format("Unknown command: {}", args[0]) };
            }
            auto real_args = std::span{ args.begin() + 1, args.end() };
            return (i_functor->second)(command_functors_, real_args);
        }

        constexpr std::expected<void, std::string> operator()(std::string_view args)const
        {
            auto splited_args = std::views::split(args, std::string_view{ " " })
                                | std::views::transform([](auto&& x){ return std::string_view{ x }; })
                                | std::ranges::to<std::vector>();
            return (*this)(splited_args);
        }

        constexpr void operator()(std::span<std::string_view> args, auto&& deal_error)const
        {
            auto result = (*this)(args);
            if(not result.has_value())
            {
                deal_error(result.error());
            }
        }

        constexpr void operator()(std::string_view args, auto&& deal_error)const
        {
            auto result = (*this)(args);
            if(not result.has_value())
            {
                deal_error(result.error());
            }
        }

    private:
        template<size_t...I>
        static constexpr map_t init_map(const CommandInfo<F>&...cmds, std::index_sequence<I...>)
        {
            return
            {
                std::pair
                { 
                    cmds.name,
                    +[](const functors_t& fns, std::span<std::string_view> args){ return std::get<I>(fns)(args); } 
                }...
            }; 
        }
    };
}

#endif
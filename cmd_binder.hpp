#ifndef CMD_BINDER
#define CMD_BINDER

#include <concepts>
#include <string_view>
#include <sstream>
#include <iostream>
#include <ranges>
#include <span>
#include <format>
#include <expected>
#include <unordered_map>

namespace cmd_binder 
{
    using ParseErrorInfo = std::string;

    using CmdFunction = std::expected<void, ParseErrorInfo>(const void*, std::span<std::string_view>);

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

    namespace detail
    {
        struct RTTI
        {
            std::expected<void, ParseErrorInfo>(*invoke)(const void*, std::span<std::string_view>);
            void*(*copy)(const void*);
            void (*destroy)(void*)noexcept;
        };

        template<typename T>
        struct GetRTTI : GetRTTI<decltype(&T::operator())>{};

        template<typename R, typename...Args>
        struct GetRTTI<R(Args...)>
        {
            static constexpr void do_nothing(void*)noexcept{}

            static constexpr void* copy_ptr(const void* p)noexcept{ return const_cast<void*>(p); }
            
            static constexpr std::expected<void, ParseErrorInfo> wrapped_invoke(const void* fn, std::span<std::string_view> arg_strs)
            {
                return [&]<size_t...I>(std::index_sequence<I...>) -> std::expected<void, ParseErrorInfo>
                {
                    auto args = std::tuple{ parse_to<std::decay_t<Args>>(arg_strs[I])... };
                    auto error = (std::string{} + ... + std::get<I>(args).error_or(""));
                    if(error.empty())
                    {
                        reinterpret_cast<R(* const)(Args...)>(const_cast<void*>(fn))(std::get<I>(args).value()...);
                        return {};
                    }
                    else
                    {
                        return std::unexpected{ std::move(error) };
                    }
                }(std::index_sequence_for<Args...>{});
            }

            static constexpr RTTI value()
            {
                return { wrapped_invoke, copy_ptr, do_nothing };
            }
        };

        template<typename C, typename R, typename...Args>
        struct GetRTTI<R(C::*)(Args...)const>
        {
            static constexpr std::expected<void, ParseErrorInfo> wrapped_invoke(const void* fn, std::span<std::string_view> arg_strs)
            {
                return [&]<size_t...I>(std::index_sequence<I...>) -> std::expected<void, ParseErrorInfo>
                {
                    auto args = std::tuple{ parse_to<std::decay_t<Args>>(arg_strs[I])... };
                    auto error = (std::string{} + ... + std::get<I>(args).error_or(""));
                    if(error.empty())
                    {
                        (*reinterpret_cast<const C*>(fn))(std::get<I>(args).value()...);
                        return {};
                    }
                    else
                    {
                        return std::unexpected{ std::move(error) };
                    }
                }(std::index_sequence_for<Args...>{});
            }

            static constexpr RTTI value()
            {
                return { 
                    wrapped_invoke, 
                    +[](const void* p){ return reinterpret_cast<void*>(new C{ *reinterpret_cast<const C*>(p) }); },
                    +[](void* p)noexcept{ delete reinterpret_cast<C*>(p); } };
            }
        };

        
    }

    class CmdFunctor
    {
        detail::RTTI rtti_;
        void* ptr_ = nullptr;

    public:
        constexpr CmdFunctor() = default;

        template<typename T>
        constexpr explicit CmdFunctor(T&& fn) 
        : rtti_{ detail::GetRTTI<std::remove_cvref_t<T>>::value() }
        , ptr_{ get_ptr(std::forward<T>(fn)) }
        {}

        constexpr CmdFunctor(const CmdFunctor& other)
        : rtti_{ other.rtti_ }
        , ptr_{ rtti_.copy(other.ptr_) }
        {}

        constexpr CmdFunctor(CmdFunctor&& other) noexcept
        {
           swap(*this, other);
        }

        constexpr CmdFunctor& operator=(CmdFunctor other_copy) noexcept
        {
            swap(*this, other_copy);
            return *this;
        }

        constexpr std::expected<void, ParseErrorInfo> operator()(std::span<std::string_view> args)const
        {
            return rtti_.invoke(ptr_, args);
        }

        constexpr ~CmdFunctor() noexcept
        {
            if(ptr_)
            {
                rtti_.destroy(ptr_);
            }
        }

        friend void swap(CmdFunctor& l, CmdFunctor& r)noexcept
        {
            using std::swap;
            swap(l.rtti_, r.rtti_);
            swap(l.ptr_, r.ptr_);
        }
    private:
        template<typename T>
        void* get_ptr(T&& fn)
        {
            if constexpr(std::is_function_v<std::remove_cvref_t<T>>)
            {
                return reinterpret_cast<void*>(&fn);
            }
            else
            {
                return new std::remove_cvref_t<T>{ std::forward<T>(fn) };
            }
        }

    };

    struct Cmd
    {
        std::string name;
        CmdFunctor functor;
    };

#define BIND_CMD(x) Cmd{ #x, CmdFunctor{ static_cast<decltype(x)&&>(x) } }

    class CmdManager
    {
        std::unordered_map<std::string, CmdFunctor> cmds_;

    public:
        template<std::same_as<Cmd>...C>
        constexpr explicit CmdManager(C&&...c)
        : cmds_{ std::pair{ std::move(c.name), std::move(c.functor) }... }
        {}

        std::expected<void, std::string> operator()(std::span<std::string_view> args)const
        {
            auto i_functor = cmds_.find(std::string{ args[0] });
            if(i_functor == cmds_.end())
            {
                return std::unexpected{ std::format("Unknown command: {}", args[0]) };
            }
            auto real_args = std::span{ args.begin() + 1, args.end() };
            return (i_functor->second)(real_args);
        }

        std::expected<void, std::string> operator()(std::string_view args)const
        {
            auto splited_args = std::views::split(args, std::string_view{ " " })
                                | std::views::transform([](auto&& x){ return std::string_view{ x }; })
                                | std::ranges::to<std::vector>();
            return (*this)(splited_args);
        }

        void operator()(std::span<std::string_view> args, auto&& deal_error)const
        {
            auto result = (*this)(args);
            if(not result.has_value())
            {
                deal_error(result.error());
            }
        }

        void operator()(std::string_view args, auto&& deal_error)const
        {
            auto result = (*this)(args);
            if(not result.has_value())
            {
                deal_error(result.error());
            }
        }
    };
}

#endif
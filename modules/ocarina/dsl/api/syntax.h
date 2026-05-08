//
// Created by Zero on 20/06/2022.
//

#pragma once

#include "stmt_builder.h"

#define $source_location ocarina::format("{},{}", __FILE__, __LINE__)

#define $sign_source_location ocarina::comment($source_location);

#define $if(...) ::ocarina::detail::IfStmtBuilder::create_with_source_location("if: " + $source_location, __VA_ARGS__) / [&]() noexcept
#define $else % [&]() noexcept
#define $elif(...) *[&] {                                                                                           \
    return ::ocarina::detail::IfStmtBuilder::create_with_source_location("elif: " + $source_location, __VA_ARGS__); \
} / [&]

#define $comment(...) ::ocarina::comment(#__VA_ARGS__);

#define $switch(...) ::ocarina::detail::SwitchStmtBuilder::create_with_source_location("switch: " + $source_location, __VA_ARGS__) \
    *[&](::ocarina::Case case_,                                                                                                    \
         ::ocarina::Default default_) noexcept

#define $case(...) case_("switch case: " + $source_location, __VA_ARGS__) \
    *[&](::ocarina::Break break_) noexcept

#define $break break_("break: " + $source_location)
#define $default default_("default: " + $source_location) *[&](::ocarina::Break break_) noexcept
#define $continue continue_("continue: " + $source_location)

#define $super_break ::ocarina::syntax::break_("break: " + $source_location)
#define $super_continue ::ocarina::syntax::continue_("break: " + $source_location)

#define $loop ::ocarina::detail::LoopStmtBuilder::create_with_source_location("loop: " + $source_location) \
    *[&](::ocarina::Continue continue_,                                                                    \
         ::ocarina::Break break_) noexcept

#define $while(...) ::ocarina::detail::LoopStmtBuilder::create_with_source_location("while: " + $source_location) / \
                        [&]() noexcept {                                                                            \
                            ::ocarina::if_(!(__VA_ARGS__), [&] {                                                    \
                                ocarina::syntax::break_();                                                          \
                            });                                                                                     \
                        } *[&](::ocarina::Continue continue_,                                                       \
                               ::ocarina::Break break_) noexcept

#define $for(v, ...) ::ocarina::detail::range_with_source_location("for: " + $source_location, __VA_ARGS__) \
                             .set_var_symbol(#v) /                                                          \
                         [&](auto v,                                                                        \
                             ::ocarina::Continue continue_,                                                 \
                             ::ocarina::Break break_) noexcept

#define $return(...) ::ocarina::return_(__VA_ARGS__)

#define $scope ::ocarina::detail::ScopeStmtBuilder("scope " + $source_location) + [&]() noexcept

#define $outline ::ocarina::detail::CallableOutlineBuilder("callable " + $source_location) % [&]() noexcept

#define $debug(...) Env::printer().set_description("debug " + $source_location).debug(__VA_ARGS__);
#define $info(...) Env::printer().set_description("info " + $source_location).info(__VA_ARGS__);
#define $warn(...) Env::printer().set_description("warn " + $source_location).warn(__VA_ARGS__);
#define $err(...) Env::printer().set_description("err " + $source_location).err(__VA_ARGS__);

#define $debug_if(cond, fmt, ...)         \
    $if(cond) {                           \
        $debug(string(fmt), __VA_ARGS__); \
    };
#define $info_if(cond, fmt, ...)         \
    $if(cond) {                          \
        $info(string(fmt), __VA_ARGS__); \
    };
#define $warn_if(cond, fmt, ...)         \
    $if(cond) {                          \
        $warn(string(fmt), __VA_ARGS__); \
    };
#define $err_if(cond, fmt, ...)         \
    $if(cond) {                         \
        $err(string(fmt), __VA_ARGS__); \
    };

#define $debug_with_location(...) Env::printer().set_description("debug " + $source_location).debug_with_location(__VA_ARGS__);
#define $info_with_location(...) Env::printer().set_description("info " + $source_location).info_with_location(__VA_ARGS__);
#define $warn_with_location(...) Env::printer().set_description("warn " + $source_location).warn_with_location(__VA_ARGS__);
#define $err_with_location(...) Env::printer().set_description("err " + $source_location).err_with_location(__VA_ARGS__);

#define $debug_with_location_if(cond, fmt, ...)         \
    $if(cond) {                                         \
        $debug_with_location(string(fmt), __VA_ARGS__); \
    };
#define $info_with_location_if(cond, fmt, ...)         \
    $if(cond) {                                        \
        $info_with_location(string(fmt), __VA_ARGS__); \
    };
#define $warn_with_location_if(cond, fmt, ...)         \
    $if(cond) {                                        \
        $warn_with_location(string(fmt), __VA_ARGS__); \
    };
#define $err_with_location_if(cond, fmt, ...)         \
    $if(cond) {                                       \
        $err_with_location(string(fmt), __VA_ARGS__); \
    };

#define $condition_execute Env::debugger().set_description("condition execute " + $source_location) *[&]() noexcept

#define $condition_debug(...) \
    $condition_execute { $debug(__VA_ARGS__); }
#define $condition_debug_with_location(...) \
    $condition_execute { $debug_with_location(__VA_ARGS__); }

#define $condition_info(...) \
    $condition_execute { $info(__VA_ARGS__); }
#define $condition_info_with_location(...) \
    $condition_execute { $info_with_location(__VA_ARGS__); }

#define $condition_warn(...) \
    $condition_execute { $warn(__VA_ARGS__); }
#define $condition_warn_with_location(...) \
    $condition_execute { $warn_with_location(__VA_ARGS__); }

#define $condition_err(...) \
    $condition_execute { $err(__VA_ARGS__); }
#define $condition_err_with_location(...) \
    $condition_execute { $err_with_location(__VA_ARGS__); }

#define $debug_with_traceback(fmt, ...) \
    $debug(fmt + string(" with ") + traceback_string(), __VA_ARGS__);
#define $info_with_traceback(fmt, ...) \
    $info(fmt + string(" with ") + traceback_string(), __VA_ARGS__);
#define $warn_with_traceback(fmt, ...) \
    $warn(fmt + string(" with ") + traceback_string(), __VA_ARGS__);
#define $err_with_traceback(fmt, ...) \
    $err(fmt + string(" with ") + traceback_string(), __VA_ARGS__);
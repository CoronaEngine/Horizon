//
// Created by Zero on 20/06/2022.
//

#pragma once

#include "stmt_builder.h"

#define $source_location horizon::dsl::format("{},{}", __FILE__, __LINE__)

#define $sign_source_location horizon::dsl::comment($source_location);

#define $if(...) ::horizon::dsl::detail::IfStmtBuilder::create_with_source_location("if: " + $source_location, __VA_ARGS__) / [&]() noexcept
#define $else % [&]() noexcept
#define $elif(...) *[&] {                                                                                           \
    return ::horizon::dsl::detail::IfStmtBuilder::create_with_source_location("elif: " + $source_location, __VA_ARGS__); \
} / [&]

#define $comment(...) ::horizon::dsl::comment(#__VA_ARGS__);

#define $switch(...) ::horizon::dsl::detail::SwitchStmtBuilder::create_with_source_location("switch: " + $source_location, __VA_ARGS__) \
    *[&](::horizon::dsl::Case case_,                                                                                                    \
         ::horizon::dsl::Default default_) noexcept

#define $case(...) case_("switch case: " + $source_location, __VA_ARGS__) \
    *[&](::horizon::dsl::Break break_) noexcept

#define $break break_("break: " + $source_location)
#define $default default_("default: " + $source_location) *[&](::horizon::dsl::Break break_) noexcept
#define $continue continue_("continue: " + $source_location)

#define $super_break ::horizon::dsl::syntax::break_("break: " + $source_location)
#define $super_continue ::horizon::dsl::syntax::continue_("break: " + $source_location)

#define $loop ::horizon::dsl::detail::LoopStmtBuilder::create_with_source_location("loop: " + $source_location) \
    *[&](::horizon::dsl::Continue continue_,                                                                    \
         ::horizon::dsl::Break break_) noexcept

#define $while(...) ::horizon::dsl::detail::LoopStmtBuilder::create_with_source_location("while: " + $source_location) / \
                        [&]() noexcept {                                                                            \
                            ::horizon::dsl::if_(!(__VA_ARGS__), [&] {                                                    \
                                horizon::dsl::syntax::break_();                                                          \
                            });                                                                                     \
                        } *[&](::horizon::dsl::Continue continue_,                                                       \
                               ::horizon::dsl::Break break_) noexcept

#define $for(v, ...) ::horizon::dsl::detail::range_with_source_location("for: " + $source_location, __VA_ARGS__) \
                             .set_var_symbol(#v) /                                                          \
                         [&](auto v,                                                                        \
                             ::horizon::dsl::Continue continue_,                                                 \
                             ::horizon::dsl::Break break_) noexcept

#define $return(...) ::horizon::dsl::return_(__VA_ARGS__)

#define $scope ::horizon::dsl::detail::ScopeStmtBuilder("scope " + $source_location) + [&]() noexcept

#define $outline ::horizon::dsl::detail::CallableOutlineBuilder("callable " + $source_location) % [&]() noexcept

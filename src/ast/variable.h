//
// Created by Zero on 21/04/2022.
//

#pragma once

#include <source_location>
#include <utility>
#include "core/stl.h"
#include "core/util/hash.h"
#include "math/storage_traits.h"

namespace horizon::ast {
using namespace horizon::core;
using namespace horizon::math;

enum struct Usage : uint32_t {
    None = 0u,
    Read = 1 << 0,
    Write = 1 << 1,
    ReadWrite = Read | Write
};

[[nodiscard]] inline bool is_write(Usage usage) {
    return (to_underlying(usage) & to_underlying(Usage::Write)) == to_underlying(Usage::Write);
}

class Function;

class OC_AST_API Variable : public Hashable {
public:
    enum struct Tag : uint32_t {
        // data
        Local,
        Shared,
        Member,
        Uniform,

        // reference
        Reference,

        // resources
        Buffer,
        ByteBuffer,
        Texture3D,
        Texture2D,
        BindlessArray,
        Accel,

        // builtins
        ThreadIdx,
        BlockIdx,
        ThreadId,
        DispatchIdx,
        DispatchId,
        DispatchDim
    };

    struct Data {
    private:
        Usage usage;
        Variable::Tag tag{};
        string name{};
        string suffix{};
        bool used{false};
        explicit Data(Usage u,
                      Variable::Tag t = Variable::Tag::Local)
            : usage(u), tag(t) {}

        friend class Function;
        friend class Variable;
    };

private:
    const Type *type_{};
    const Function *context_{nullptr};
    uint32_t uid_{};
    std::source_location src_location_{};

    [[nodiscard]] uint64_t compute_hash() const noexcept override;
    friend class Function;
    Variable(const Function *context,
             const Type *type, Tag tag, uint uid,
             string name = "",
             string suffix = "") noexcept;

private:
    [[nodiscard]] string name() const noexcept;
    [[nodiscard]] string suffix() const noexcept;
    void set_tag(Tag tag) noexcept;

public:
    Variable() noexcept = default;
    OC_MAKE_MEMBER_GETTER_SETTER(src_location, &)
    [[nodiscard]] constexpr const Type *type() const noexcept { return type_; }
    [[nodiscard]] Tag tag() const noexcept;
    [[nodiscard]] constexpr uint uid() const noexcept { return uid_; }
    [[nodiscard]] constexpr bool operator==(const Variable &rhs) const noexcept { return uid_ == rhs.uid_; }
    [[nodiscard]] string final_name() const noexcept;
    [[nodiscard]] Usage usage() const noexcept;
    void set_name(string name) noexcept;
    void set_suffix(string suffix) noexcept;
    void mark_usage(Usage usage) const noexcept;
    void mark_used(bool uesd = true) const noexcept;
    [[nodiscard]] bool used() const noexcept;
};

}// namespace horizon::ast

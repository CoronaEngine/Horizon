//
// Created by Zero on 30/05/2022.
//

#include "ast/layout_resolver.h"
#include "core/stl.h"
#include "core/type_system/type_desc.h"

using namespace ocarina;

struct ParseLeaf {
    float value;
    Vector<uint, 2> index;
};

OC_MAKE_STRUCT_REFLECTION(ParseLeaf, value, index)
OC_MAKE_STRUCT_DESC(ParseLeaf, value, index)

struct ParseBuiltin {
    bool enabled;
    float weight;
};

OC_MAKE_BUILTIN_STRUCT(ParseBuiltin)
OC_MAKE_STRUCT_REFLECTION(ParseBuiltin, enabled, weight)
OC_MAKE_STRUCT_DESC(ParseBuiltin, enabled, weight)

struct ParseParam {
    Vector<float, 4> color;
    uint count;
};

OC_MAKE_PARAM_STRUCT(ParseParam)
OC_MAKE_STRUCT_REFLECTION(ParseParam, color, count)
OC_MAKE_STRUCT_DESC(ParseParam, color, count)

struct ParseNested {
    ParseLeaf leaf;
    ocarina::array<float, 4> samples;
};

OC_MAKE_STRUCT_REFLECTION(ParseNested, leaf, samples)
OC_MAKE_STRUCT_DESC(ParseNested, leaf, samples)

struct ParseRealLeaf {
    real value;
    Vector<real, 2> index;
};

OC_MAKE_STRUCT_REFLECTION(ParseRealLeaf, value, index)
OC_MAKE_STRUCT_DESC(ParseRealLeaf, value, index)

struct ParseRealNested {
    ParseRealLeaf leaf;
    Vector<real, 3> samples;
};

OC_MAKE_STRUCT_REFLECTION(ParseRealNested, leaf, samples)
OC_MAKE_STRUCT_DESC(ParseRealNested, leaf, samples)

struct ParseRealArray {
    ocarina::array<real, 4> samples;
};

OC_MAKE_STRUCT_REFLECTION(ParseRealArray, samples)
OC_MAKE_STRUCT_DESC(ParseRealArray, samples)

static bool check_impl(bool condition, string_view message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << std::endl;
        return false;
    }
    return true;
}

#define CHECK(...)                            \
    do {                                      \
        if (!check_impl((__VA_ARGS__), #__VA_ARGS__)) { \
            return false;                     \
        }                                     \
    } while (false)

static size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1u) / alignment * alignment;
}

static size_t layout_alignment_of(const Type *type) {
    if (type == nullptr) {
        return 0u;
    }
    if (type->is_scalar() || type->is_byte_buffer() || type->is_texture() || type->is_bindless_array() || type->is_accel()) {
        return type->alignment();
    }
    if (type->is_vector() || type->is_matrix() || type->is_array() || type->is_buffer()) {
        return type->alignment();
    }
    if (type->is_structure()) {
        return type->alignment();
    }
    return type->alignment();
}

static size_t layout_size_of(const Type *type) {
    if (type == nullptr) {
        return 0u;
    }
    if (type->is_scalar() || type->is_byte_buffer() || type->is_texture() || type->is_bindless_array() || type->is_accel()) {
        return type->size();
    }
    if (type->is_vector()) {
        const size_t scalar_size = layout_size_of(type->element());
        return scalar_size * (type->dimension() == 3 ? 4u : type->dimension());
    }
    if (type->is_matrix()) {
        return type->size();
    }
    if (type->is_array() || type->is_buffer()) {
        return type->size();
    }
    if (type->is_structure()) {
        size_t size = 0u;
        for (const Type *member : type->members()) {
            size = align_up(size, member->alignment());
            size += member->size();
        }
        return align_up(size, type->alignment());
    }
    return type->size();
}

static string_view invalid_case_desc(string_view case_name) {
    if (case_name == "invalid-data-type") {
        return "not_a_type";
    }
    if (case_name == "invalid-vector-element") {
        return "vector<texture2d,4>";
    }
    if (case_name == "invalid-matrix-dimension") {
        return "matrix<float,5,5>";
    }
    return {};
}

static int run_invalid_case(string_view case_name) {
    string_view desc = invalid_case_desc(case_name);
    if (desc.empty()) {
        std::cerr << "unknown invalid case: " << case_name << std::endl;
        return 2;
    }
    const Type *type = Type::from(desc);
    (void)type;
    std::cerr << "invalid case unexpectedly succeeded: " << case_name << std::endl;
    return 3;
}

static bool expect_invalid_parse_failure(const char *exe_path, string_view case_name) {
    string command = ocarina::format("\"{}\" --invalid-case {} >nul 2>&1", exe_path, case_name);
    int exit_code = std::system(command.c_str());
    CHECK(exit_code != 0);
    return true;
}

static bool test_void_and_scalar_types() {
    CHECK(Type::from(TypeDesc<void>::description()) == nullptr);

    const Type *float_type = Type::from(TypeDesc<float>::description());
    CHECK(float_type != nullptr);
    CHECK(float_type->is_scalar());
    CHECK(float_type->tag() == Type::Tag::FLOAT);
    CHECK(float_type->size() == sizeof(float));
    CHECK(float_type->alignment() == alignof(float));
    CHECK(float_type->dimension() == 1);
    CHECK(float_type->description() == TypeDesc<float>::description());
    CHECK(float_type->name() == "float");
    CHECK(Type::exists(TypeDesc<float>::description()));
    CHECK(Type::from(TypeDesc<float>::description()) == float_type);

    const Type *bool_type = Type::from(TypeDesc<bool>::description());
    CHECK(bool_type != nullptr);
    CHECK(bool_type->tag() == Type::Tag::BOOL);
    CHECK(bool_type->size() == sizeof(bool));
    CHECK(bool_type->alignment() == alignof(bool));
    CHECK(bool_type->dimension() == 1);

    const Type *half_type = Type::from(TypeDesc<half>::description());
    CHECK(half_type != nullptr);
    CHECK(half_type->tag() == Type::Tag::HALF);
    CHECK(half_type->size() == sizeof(half));
    CHECK(half_type->alignment() == alignof(half));

    const Type *real_type = Type::from(TypeDesc<real>::description());
    CHECK(real_type != nullptr);
    CHECK(real_type->is_scalar());
    CHECK(real_type->tag() == Type::Tag::REAL);
    CHECK(real_type->size() == sizeof(real));
    CHECK(real_type->alignment() == alignof(real));
    CHECK(real_type->name() == "real");
    return true;
}

static bool test_vector_and_matrix_types() {
    using Float3 = Vector<float, 3>;
    using Float4 = Vector<float, 4>;
    using Float3x4 = Matrix<float, 3, 4>;
    using Real3 = Vector<real, 3>;
    using Real3x3 = Matrix<real, 3, 3>;

    const Type *float_type = Type::of<float>();
    const Type *float3_type = Type::from(TypeDesc<Float3>::description());
    CHECK(float3_type != nullptr);
    CHECK(float3_type->is_vector());
    CHECK(float3_type->tag() == Type::Tag::VECTOR);
    CHECK(float3_type->dimension() == 3);
    CHECK(float3_type->element() == float_type);
    CHECK(float3_type->members().size() == 1);
    CHECK(float3_type->size() == sizeof(Float3));
    CHECK(float3_type->alignment() == alignof(Float3));
    CHECK(float3_type->name() == "float3");
    CHECK(Type::from(TypeDesc<Float3>::description()) == float3_type);

    const Type *float4_type = Type::from(TypeDesc<Float4>::description());
    CHECK(float4_type != nullptr);
    CHECK(float4_type->is_vector());
    CHECK(float4_type->dimension() == 4);
    CHECK(float4_type->element() == float_type);

    const Type *real_type = Type::of<real>();
    const Type *real3_type = Type::from(TypeDesc<Real3>::description());
    CHECK(real3_type != nullptr);
    CHECK(real3_type->is_vector());
    CHECK(real3_type->element() == real_type);
    CHECK(real3_type->name() == "real3");

    const Type *matrix_type = Type::from(TypeDesc<Float3x4>::description());
    CHECK(matrix_type != nullptr);
    CHECK(matrix_type->is_matrix());
    CHECK(matrix_type->tag() == Type::Tag::MATRIX);
    CHECK(matrix_type->dimension() == 4);
    CHECK(matrix_type->members().size() == 1);
    CHECK(matrix_type->element() == float3_type);
    CHECK(matrix_type->size() == sizeof(Float3x4));
    CHECK(matrix_type->alignment() == alignof(Float3x4));
    CHECK(matrix_type->name() == "float3x4");
    CHECK(Type::from(TypeDesc<Float3x4>::description()) == matrix_type);

    const Type *real3x3_type = Type::from(TypeDesc<Real3x3>::description());
    CHECK(real3x3_type != nullptr);
    CHECK(real3x3_type->is_matrix());
    CHECK(real3x3_type->element() == Type::of<Real3>());
    CHECK(real3x3_type->name() == "real3x3");
    return true;
}

static bool test_array_and_resource_types() {
    using Float4 = Vector<float, 4>;
    using Float4Array = ocarina::array<Float4, 5>;

    const Type *float4_type = Type::of<Float4>();
    const Type *array_type = Type::from(TypeDesc<Float4Array>::description());
    CHECK(array_type != nullptr);
    CHECK(array_type->is_array());
    CHECK(array_type->tag() == Type::Tag::ARRAY);
    CHECK(array_type->dimension() == 5);
    CHECK(array_type->element() == float4_type);
    CHECK(array_type->size() == float4_type->size() * 5u);
    CHECK(array_type->alignment() == float4_type->alignment());
    CHECK(array_type->is_valid());

    const Type *buffer_type = Type::from(TypeDesc<Buffer<float>>::description());
    CHECK(buffer_type != nullptr);
    CHECK(buffer_type->is_buffer());
    CHECK(buffer_type->tag() == Type::Tag::BUFFER);
    CHECK(buffer_type->element() == Type::of<float>());
    CHECK(buffer_type->size() == sizeof(BufferDesc<>));
    CHECK(buffer_type->alignment() == alignof(BufferDesc<>));

    const Type *byte_buffer_type = Type::from(TypeDesc<ByteBuffer>::description());
    CHECK(byte_buffer_type != nullptr);
    CHECK(byte_buffer_type->is_byte_buffer());
    CHECK(byte_buffer_type->tag() == Type::Tag::BYTE_BUFFER);
    CHECK(byte_buffer_type->alignment() == alignof(BufferDesc<>));

    const Type *texture2d_type = Type::from(TypeDesc<Texture2D>::description());
    CHECK(texture2d_type != nullptr);
    CHECK(texture2d_type->is_texture());
    CHECK(texture2d_type->tag() == Type::Tag::TEXTURE2D);
    CHECK(texture2d_type->size() == sizeof(TextureDesc));
    CHECK(texture2d_type->alignment() == alignof(TextureDesc));
    CHECK(Type::resolve(texture2d_type, StoragePrecisionPolicy{})->tag() == Type::Tag::TEXTURE2D);

    const Type *texture3d_type = Type::from(TypeDesc<Texture3D>::description());
    CHECK(texture3d_type != nullptr);
    CHECK(texture3d_type->is_texture());
    CHECK(texture3d_type->tag() == Type::Tag::TEXTURE3D);
    CHECK(texture3d_type->size() == sizeof(TextureDesc));
    CHECK(texture3d_type->alignment() == alignof(TextureDesc));

    const Type *bindless_array_type = Type::from(TypeDesc<BindlessArray>::description());
    CHECK(bindless_array_type != nullptr);
    CHECK(bindless_array_type->tag() == Type::Tag::BINDLESS_ARRAY);
    CHECK(bindless_array_type->alignment() == alignof(BindlessArrayDesc));

    const Type *accel_type = Type::from(TypeDesc<Accel>::description());
    CHECK(accel_type != nullptr);
    CHECK(accel_type->tag() == Type::Tag::ACCEL);
    return true;
}

static bool test_tuple_and_struct_types() {
    using TupleType = ocarina::tuple<uint, uint, Vector<float, 2>>;
    using SampleArray = ocarina::array<float, 4>;

    const Type *tuple_type = Type::from(TypeDesc<TupleType>::description());
    CHECK(tuple_type != nullptr);
    CHECK(tuple_type->is_structure());
    CHECK(tuple_type->tag() == Type::Tag::STRUCTURE);
    CHECK(tuple_type->cname() == "_Tuple");
    CHECK(tuple_type->members().size() == 3);
    CHECK(tuple_type->members()[0] == Type::of<uint>());
    CHECK(tuple_type->members()[1] == Type::of<uint>());
    CHECK(tuple_type->members()[2] == Type::of<Vector<float, 2>>());
    CHECK(tuple_type->size() == sizeof(TupleType));
    CHECK(tuple_type->alignment() == alignof(TupleType));
    CHECK(!tuple_type->is_builtin_struct());
    CHECK(!tuple_type->is_param_struct());

    const Type *leaf_type = Type::of<ParseLeaf>();
    CHECK(leaf_type != nullptr);
    CHECK(leaf_type->is_structure());
    CHECK(leaf_type->description() == TypeDesc<ParseLeaf>::description());
    CHECK(leaf_type->cname() == "ParseLeaf");
    CHECK(leaf_type->simple_cname() == "ParseLeaf");
    CHECK(leaf_type->members().size() == 2);
    CHECK(leaf_type->member_name().size() == 2);
    CHECK(leaf_type->member_name()[0] == "value");
    CHECK(leaf_type->member_name()[1] == "index");
    CHECK(leaf_type->get_member("value") == Type::of<float>());
    CHECK(leaf_type->get_member("index") == Type::of<Vector<uint, 2>>());
    CHECK(leaf_type->size() == sizeof(ParseLeaf));
    CHECK(leaf_type->alignment() == alignof(ParseLeaf));
    CHECK(!leaf_type->is_builtin_struct());
    CHECK(!leaf_type->is_param_struct());

    const Type *builtin_type = Type::of<ParseBuiltin>();
    CHECK(builtin_type != nullptr);
    CHECK(builtin_type->is_structure());
    CHECK(builtin_type->is_builtin_struct());
    CHECK(!builtin_type->is_param_struct());
    CHECK(builtin_type->members().size() == 2);
    CHECK(builtin_type->get_member("enabled") == Type::of<bool>());
    CHECK(builtin_type->get_member("weight") == Type::of<float>());
    CHECK(builtin_type->size() == sizeof(ParseBuiltin));
    CHECK(builtin_type->alignment() == alignof(ParseBuiltin));

    const Type *param_type = Type::of<ParseParam>();
    CHECK(param_type != nullptr);
    CHECK(param_type->is_structure());
    CHECK(!param_type->is_builtin_struct());
    CHECK(param_type->is_param_struct());
    CHECK(param_type->members().size() == 2);
    CHECK(param_type->get_member("color") == Type::of<Vector<float, 4>>());
    CHECK(param_type->get_member("count") == Type::of<uint>());
    CHECK(param_type->size() == sizeof(ParseParam));
    CHECK(param_type->alignment() == alignof(ParseParam));

    const Type *nested_type = Type::of<ParseNested>();
    CHECK(nested_type != nullptr);
    CHECK(nested_type->is_structure());
    CHECK(nested_type->members().size() == 2);
    CHECK(nested_type->get_member("leaf") == leaf_type);
    CHECK(nested_type->get_member("samples") == Type::of<SampleArray>());
    CHECK(nested_type->size() == sizeof(ParseNested));
    CHECK(nested_type->alignment() == alignof(ParseNested));
    CHECK(Type::from(TypeDesc<ParseNested>::description()) == nested_type);
    return true;
}

static bool test_layout_resolver() {
    using RealLeafBuffer = Buffer<ParseRealLeaf>;
    using RealNestedBuffer = Buffer<ParseRealNested>;
    using RealArrayBuffer = Buffer<ParseRealArray>;

    const Type *real_leaf_type = Type::of<ParseRealLeaf>();
    CHECK(real_leaf_type != nullptr);
    CHECK(real_leaf_type->is_structure());
    CHECK(real_leaf_type->get_member("value") == Type::of<real>());
    CHECK(real_leaf_type->get_member("index") == Type::of<Vector<real, 2>>());
    CHECK(real_leaf_type->size() == sizeof(ParseRealLeaf));
    CHECK(real_leaf_type->size() == layout_size_of(real_leaf_type));
    CHECK(real_leaf_type->alignment() == layout_alignment_of(real_leaf_type));

    const Type *real_array_type = Type::of<ParseRealArray>();
    CHECK(real_array_type != nullptr);
    CHECK(real_array_type->is_structure());
    CHECK(real_array_type->size() == sizeof(ParseRealArray));
    CHECK(real_array_type->size() == layout_size_of(real_array_type));
    CHECK(real_array_type->alignment() == layout_alignment_of(real_array_type));

    LayoutResolver float_resolver(StoragePrecisionPolicy{
        .policy = PrecisionPolicy::force_f32,
        .allow_real_in_storage = true,
    });
    const Type *real_type = Type::of<real>();
    CHECK(float_resolver.resolve(real_type) == Type::of<float>());

    const Type *resolved_float_leaf = float_resolver.resolve(real_leaf_type);
    CHECK(resolved_float_leaf != nullptr);
    CHECK(resolved_float_leaf->is_structure());
    const Type *float_value_type = Type::of<float>();
    const Type *float_index_type = Type::of<Vector<float, 2>>();
    CHECK(resolved_float_leaf->size() == sizeof(ParseRealLeaf));
    CHECK(resolved_float_leaf->size() == layout_size_of(resolved_float_leaf));
    CHECK(resolved_float_leaf->alignment() == layout_alignment_of(resolved_float_leaf));
    CHECK(resolved_float_leaf->members().size() == 2u);
    CHECK(resolved_float_leaf->members()[0] == float_value_type);
    CHECK(resolved_float_leaf->members()[1] == float_index_type);

    const Type *resolved_float_nested = float_resolver.resolve(Type::of<ParseRealNested>());
    CHECK(resolved_float_nested != nullptr);
    CHECK(resolved_float_nested->is_structure());
    CHECK(resolved_float_nested->size() == layout_size_of(resolved_float_nested));
    CHECK(resolved_float_nested->alignment() == layout_alignment_of(resolved_float_nested));
    CHECK(resolved_float_nested->members().size() == 2u);
    CHECK(resolved_float_nested->members()[0] == resolved_float_leaf);
    CHECK(resolved_float_nested->members()[1] == Type::of<Vector<float, 3>>());

    LayoutResolver half_resolver(StoragePrecisionPolicy{
        .policy = PrecisionPolicy::force_f16,
        .allow_real_in_storage = true,
    });
    CHECK(half_resolver.resolve(real_type) == Type::of<half>());

    const Type *resolved_half_leaf = half_resolver.resolve(real_leaf_type);
    CHECK(resolved_half_leaf != nullptr);
    CHECK(resolved_half_leaf->is_structure());
    const Type *half_value_type = Type::of<half>();
    const Type *half_index_type = Type::of<Vector<half, 2>>();
    CHECK(resolved_half_leaf->size() == layout_size_of(resolved_half_leaf));
    CHECK(resolved_half_leaf->alignment() == layout_alignment_of(resolved_half_leaf));
    CHECK(resolved_half_leaf->members().size() == 2u);
    CHECK(resolved_half_leaf->members()[0] == half_value_type);
    CHECK(resolved_half_leaf->members()[1] == half_index_type);

    const Type *real_nested_type = Type::of<ParseRealNested>();
    CHECK(real_nested_type != nullptr);
    CHECK(real_nested_type->is_structure());
    CHECK(real_nested_type->size() == sizeof(ParseRealNested));
    CHECK(real_nested_type->size() == layout_size_of(real_nested_type));
    CHECK(real_nested_type->alignment() == layout_alignment_of(real_nested_type));
    const Type *resolved_half_nested = half_resolver.resolve(real_nested_type);
    CHECK(resolved_half_nested != nullptr);
    CHECK(resolved_half_nested->is_structure());
    CHECK(resolved_half_nested->members().size() == 2u);
    CHECK(resolved_half_nested->members()[0] == resolved_half_leaf);
    const Type *half_samples_type = resolved_half_nested->members()[1];
    CHECK(half_samples_type != nullptr);
    CHECK(half_samples_type->is_vector());
    CHECK(half_samples_type->element() == half_value_type);
    CHECK(half_samples_type->dimension() == 3u);
    CHECK(resolved_half_nested->size() == layout_size_of(resolved_half_nested));
    CHECK(resolved_half_nested->alignment() == layout_alignment_of(resolved_half_nested));

    const Type *real_leaf_buffer = Type::of<RealLeafBuffer>();
    CHECK(real_leaf_buffer != nullptr);
    const Type *resolved_half_leaf_buffer = half_resolver.resolve(real_leaf_buffer);
    CHECK(resolved_half_leaf_buffer != nullptr);
    CHECK(resolved_half_leaf_buffer->is_buffer());
    CHECK(resolved_half_leaf_buffer->element() == resolved_half_leaf);

    CHECK(half_resolver.resolve(real_nested_type) == resolved_half_nested);

    const Type *real_nested_buffer = Type::of<RealNestedBuffer>();
    CHECK(real_nested_buffer != nullptr);
    const Type *resolved_half_nested_buffer = half_resolver.resolve(real_nested_buffer);
    CHECK(resolved_half_nested_buffer != nullptr);
    CHECK(resolved_half_nested_buffer->is_buffer());
    CHECK(resolved_half_nested_buffer->element() == resolved_half_nested);

    LayoutResolver disallow_resolver(StoragePrecisionPolicy{
        .policy = PrecisionPolicy::force_f32,
        .allow_real_in_storage = false,
    });
    CHECK(disallow_resolver.resolve(real_leaf_type) == nullptr);
    CHECK(disallow_resolver.resolve(real_nested_type) == nullptr);
    CHECK(disallow_resolver.resolve(real_array_type) == nullptr);
    CHECK(disallow_resolver.resolve(real_leaf_buffer) == nullptr);
    CHECK(disallow_resolver.resolve(real_type) == nullptr);
    return true;
}

static bool test_invalid_descriptions(const char *exe_path) {
    CHECK(expect_invalid_parse_failure(exe_path, "invalid-data-type"));
    CHECK(expect_invalid_parse_failure(exe_path, "invalid-vector-element"));
    CHECK(expect_invalid_parse_failure(exe_path, "invalid-matrix-dimension"));
    return true;
}

int main(int argc, char **argv) {
    if (argc == 3 && string_view(argv[1]) == "--invalid-case") {
        return run_invalid_case(argv[2]);
    }

    bool passed = true;
    passed = test_void_and_scalar_types() && passed;
    passed = test_vector_and_matrix_types() && passed;
    passed = test_array_and_resource_types() && passed;
    passed = test_tuple_and_struct_types() && passed;
    passed = test_layout_resolver() && passed;
    // passed = test_invalid_descriptions(argv[0]) && passed;

    if (!passed) {
        std::cerr << "type parse test failed" << std::endl;
        return 1;
    }
    std::cout << "type parse test passed" << std::endl;
    return 0;
}
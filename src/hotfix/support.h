#pragma once

#include "core/concurrency/thread_safety.h"
#include "core/runtime/dynamic_module.h"
#include "core/stl.h"
#include "core/type.h"
#include "core/type_system/type_desc.h"
#include "core/util/element_trait.h"
#include "core/util/hash.h"
#include "core/util/logging.h"
#include "core/util/string_util.h"
#include "dsl/types/polymorphic.h"
#include "rhi/context.h"

namespace vision {

using ocarina::array;
using ocarina::DynamicModule;
using ocarina::format;
using ocarina::function;
using ocarina::Hashable;
using ocarina::make_pair;
using ocarina::make_shared;
using ocarina::map;
using ocarina::optional;
using ocarina::pair;
using ocarina::parent_path;
using ocarina::Polymorphic;
using ocarina::ptr_t;
using ocarina::queue;
using ocarina::RTTI;
using ocarina::set;
using ocarina::SP;
using ocarina::string;
using ocarina::string_view;
using ocarina::thread_safety;
using ocarina::tuple;
using ocarina::uint;
using ocarina::unique_ptr;
using ocarina::UP;
using ocarina::vector;

using ocarina::RHIContext;

namespace fs = ocarina::fs;
namespace concepts = ocarina::concepts;

}// namespace vision

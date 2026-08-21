//
// Created by Zero on 11/05/2022.
//

#pragma once

#include "api/builtin.h"
#include "core/type_trait.h"
#include "core/expr.h"
#include "core/var.h"
#include "data/dynamic_array.h"
#include "api/stmt_builder.h"
#include "api/syntax.h"
#include "api/operators.h"
#include "api/func.h"
#include "types/soa.h"
#include "types/struct.h"

// Backend-facing tensor, RTX, diagnostics and polymorphic-resource adapters
// remain opt-in headers.  Keeping them out of the base aggregate makes the
// embedded DSL layer usable without an RHI or code-generation backend.

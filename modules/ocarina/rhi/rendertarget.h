//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/concepts.h"
#include "core/concurrency/thread_pool.h"
#include "params.h"
#include "graphics_descriptions.h"

namespace ocarina {


class RenderTarget : concepts::Noncopyable {
public:
    RenderTarget(const RenderTargetCreation &creation){};
    virtual ~RenderTarget(){};

    
protected:

    std::string name_ = "RenderTarget";
};

}// namespace ocarina
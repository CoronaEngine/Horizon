//
// Created by Zero on 2024/9/26.
//

#include "core/image/image.h"
#include "core/stl.h"
#include "dsl/dsl.h"
#include "rhi/common.h"
#include "math/base.h"
#include "base/scattering/interaction.h"
#include "core/runtime/platform.h"
#include "rhi/context.h"

using namespace ocarina;

void test_list(Device &device, Stream& stream) {
    using Elm = float4x4;
    size_t size = 10;
    auto list = device.create_list<Elm, SOA>(size);

    RegistrableCountedBuffer<Elm, SOA> rl{};
    vector<Elm> host;
    rl.set_buffer(std::move(list));

    host.resize(size , make_float4x4(2));

    BindlessArray bindless_array = device.create_bindless_array();
    rl.set_bindless_array(bindless_array);
    rl.register_self();
    stream << bindless_array->upload_buffer_handles(true) ;
    stream << bindless_array->upload_texture3d_handles(true) ;

    rl.super().clear_immediately();

    float3x3 mmm(-2.f);
    auto m22 = abs(mmm);

    auto m333 = max(mmm, m22);

    auto m = isnan(mmm);

    Kernel kernel = [&](Uint i) {



      Var m = make_float4x4(dispatch_id() + 1);
      Var m2 = rcp(m);
      m = max(m, m2);
      $info("\n {} {} {} {}  \n"
                           "{} {} {} {}  \n"
                           "{} {} {} {}  \n"
                           "{} {} {} {}  \n",
                           m[0], m[1], m[2], m[3]);
      return ;
      DynamicArray<float> arr(3, 3);
      arr *= arr;

      auto a2 = expand_to_array(arr, 3);

      Var a = rl.read(dispatch_id());
      DynamicArray<bool> bb{1, false};
      auto nb = detail::expand_to_array(bb, 3);
      nb[0] = true;
      arr = ocarina::select(0, arr, 5.f);
      $info("{} {} {}  ", arr.as_vec3());
      return ;
        $info_with_traceback("\n {} {} {} {}  \n"
              "{} {} {} {}  \n"
              "{} {} {} {}  \n"
              "{} {} {} {}  {}\n",
              a[0], a[1], a[2], a[3], rl.advance_index());

//        rl.write(dispatch_id(), mat);
//        rl.at(dispatch_id()) = mat;

        a = rl.read(dispatch_id());

        $info("\nnew  {} {} {} {}  \n"
              "{} {} {} {}  \n"
              "{} {} {} {}  \n"
              "{} {} {} {}  {}\n",
              a[0], a[1], a[2], a[3], rl.advance_index());

    };
    auto shader = device.compile(kernel, "test_list");
    stream << rl->storage_segment().upload(host.data());
    stream << shader(6).dispatch(3);
    stream << rl->storage_segment().download(host.data());
    stream << Env::printer().retrieve();
    stream << synchronize() << commit();

    auto hs = rl.super().host_count();

    int i = 0;
}

template <typename C, typename D>
auto def_readwrite(D C::* pm) {
    int i = 0;
    cout << typeid(C).name() << endl;
    cout << typeid(D).name() << endl;
}

int main(int argc, char *argv[]) {
    def_readwrite(&float3::x);


    ocarina::TriangleHit h;

    struct_member_tuple_t<TriangleHit> a;
    traverse_tuple(a, []<typename T>(T t) {
        cout << typeid(T).name() << endl;
    });

    cout << TypeDesc<decltype(h)>::name() << endl;
    cout << Var<TriangleHit>::cname << endl;
    cout << to_str(float4{}) << endl;
    cout << to_str(1.5f) << endl;
    h.bary = float2(1,9);
    cout << to_str(h) << endl;
    return 0;
    fs::path path(argv[0]);
    RHIContext &context = RHIContext::instance();

    /**
     * Conventional scheme
     * create device and stream
     * stream used for process some command,e.g buffer upload and download, dispatch shader
     * default is asynchronous operation
     */
    Device device = context.create_device("cuda");
    Stream stream = device.create_stream();
    Env::printer().init(device);
    Env::debugger().init(device);

    //    Env::set_code_obfuscation(true);
    Env::set_valid_check(false);

    test_list(device, stream);

    return 0;
}
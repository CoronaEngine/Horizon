//
// Created by Zero on 06/11/2022.
//

#pragma once

#include "../core/type_trait.h"
#include "../api/stmt_builder.h"
#include "core/stl.h"
#include "core/util/util.h"
#include "../data/encodable.h"
#include "core/util/hash.h"
#include "../data/registrable.h"
#include "../diagnostics/env.h"

namespace ocarina {

enum PolymorphicMode {
    EInstance = 0,
    ETopology = 1
};

template<EPort p>
[[nodiscard]] inline oc_uint<p> encode_id(oc_uint<p> inst_id, oc_uint<p> type_id) noexcept {
    inst_id = inst_id << 8;
    return inst_id | type_id;
}

template<EPort p>
[[nodiscard]] inline pair<oc_uint<p>, oc_uint<p>> decode_id(oc_uint<p> id) noexcept {
    oc_uint<p> inst_id = (0xffffff00 & id) >> 8;
    oc_uint<p> type_id = 0x000000ff & id;
    return std::make_pair(inst_id, type_id);
}

template<typename T>
requires std::is_base_of_v<Hashable, T>
class PolyEvaluator : public vector<UP<T>> {
public:
    using Super = vector<UP<T>>;
    using element_ty = T;

protected:
    Uint tag_{};
    ocarina::unordered_map<uint64_t, uint> tags_;

public:
    template<typename Derive>
    requires std::is_base_of_v<element_ty, Derive>
    Derive *link(uint64_t hash, UP<Derive> elm) noexcept {
        auto [iter, first] = tags_.try_emplace(hash, static_cast<uint>(Super::size()));
        tag_ = tags_[hash];
        uint index = tags_[hash];
        Derive *ret = nullptr;
        if (first) {
            ret = elm.get();
            Super::push_back(ocarina::move(elm));
        } else {
            ret = dynamic_cast<Derive *>(Super::at(index).get());
            OC_ASSERT(ret != nullptr);
            *ret = *elm;
        }
        return ret;
    }

    void clear() noexcept {
        Super::clear();
        tags_.clear();
    }

    template<typename Derive>
    requires std::is_base_of_v<element_ty, Derive>
    Derive *link(UP<Derive> elm) noexcept {
        uint64_t hash = elm->topology_hash();
        return link(hash, ocarina::move(elm));
    }

    template<typename Func>
    void dispatch(Func &&func) const noexcept {
        comment(ocarina::format("PolyEvaluator dispatch {}, case num = {}", typeid(T).name(), Super::size()));
        auto index = detail::correct_index(tag_, static_cast<uint>(Super::size()),
                                           ocarina::format("PolyEvaluator dispatch {}", typeid(*this).name()),
                                           traceback_string(1));
        if (Super::size() == 1) {
            comment(typeid(*Super::at(0u)).name());
            func(raw_ptr(Super::at(0u)));
            return;
        }
        switch_(index, [&](Case case_, Default default_) {
            for (int i = 0; i < Super::size(); ++i) {
                comment(typeid(*Super::at(i)).name());
                case_(i, [&](Break break_) {
                    func(raw_ptr(Super::at(i)));
                    break_();
                });
            }
            default_([&](Break break_) {
                unreachable();
                break_();
            });
        });
    }

    template<typename Func>
    void dispatch(Func &&func) noexcept {
        comment(ocarina::format("PolyEvaluator dispatch {}, case num = {}", typeid(T).name(), Super::size()));
        auto index = detail::correct_index(tag_, static_cast<uint>(Super::size()),
                                           ocarina::format("PolyEvaluator dispatch {}", typeid(*this).name()),
                                           traceback_string(1));
        if (Super::size() == 1) {
            comment(typeid(*Super::at(0u)).name());
            func(raw_ptr(Super::at(0u)));
            return;
        }
        switch_(index, [&](Case case_, Default default_) {
            for (int i = 0; i < Super::size(); ++i) {
                comment(typeid(*Super::at(i)).name());
                case_(i, [&](Break break_) {
                    func(raw_ptr(Super::at(i)));
                    break_();
                });
            }
            default_([&](Break break_) {
                unreachable();
                break_();
            });
        });
    }
};

template<typename T, typename U = buffer_ty>
class Polymorphic : public vector<T> {
public:
    using Super = vector<T>;
    using data_type = U;
    using ptr_type = ptr_t<T>;
    using datas_type = RegistrableManaged<U>;

protected:
    struct GroupData {
        string class_name;
        datas_type data_set;
        vector<ptr_type *> objects;
        void fill_elements_offset() const noexcept {
            uint size = 0;
            for (const ptr_type *object : objects) {
                size = object->cal_offset(size);
                uint alignment = object->alignment();
                size = mem_offset(size, alignment);
            }
        }
    };

    struct {
        /// key : topology hash, value : GroupData
        map<uint64_t, GroupData> group_map;

        void add_object(T t) noexcept {
            uint64_t hash_code = t->topology_hash();
            if (auto iter = group_map.find(hash_code); iter == group_map.cend()) {
                group_map[hash_code] = GroupData();
                group_map[hash_code].class_name = typeid(*t).name();
            }
            group_map[hash_code].objects.push_back(raw_ptr(t));
        }

        [[nodiscard]] uint topology_index(uint64_t hash_code) const noexcept {
            uint cursor = 0;
            for (auto iter = group_map.cbegin(); iter != group_map.cend(); ++iter, ++cursor) {
                if (hash_code == iter->first) {
                    return cursor;
                }
            }
            OC_ASSERT(false);
            return InvalidUI32;
        }

        [[nodiscard]] uint64_t topology_hash(uint topology_index) const noexcept {
            uint cursor = 0;
            for (auto iter = group_map.cbegin(); iter != group_map.cend(); ++iter, ++cursor) {
                if (cursor == topology_index) {
                    return iter->first;
                }
            }
            OC_ASSERT(false);
            return InvalidUI64;
        }

        template<typename Func>
        void for_each_group(Func &&func) const noexcept {
            if constexpr (std::invocable<Func, GroupData, uint>) {
                uint cursor = 0u;
                for (auto iter = group_map.cbegin(); iter != group_map.cend(); ++iter, ++cursor) {
                    func(iter->second, cursor);
                }
            } else {
                for (const auto &it : group_map) {
                    func(it.second);
                }
            }
        }

        template<typename Func>
        void for_each_group(Func &&func) noexcept {
            if constexpr (std::invocable<Func, GroupData, uint>) {
                uint cursor = 0u;
                for (auto iter = group_map.begin(); iter != group_map.end(); ++iter, ++cursor) {
                    func(iter->second, cursor);
                }
            } else {
                for (auto &it : group_map) {
                    func(it.second);
                }
            }
        }

        template<typename Func>
        void for_each_representative(Func &&func) const noexcept {
            if constexpr (std::invocable<Func, ptr_type *, uint>) {
                uint cursor = 0u;
                for (auto iter = group_map.cbegin(); iter != group_map.cend(); ++iter, ++cursor) {
                    func(iter->second.objects[0], cursor);
                }
            } else {
                for (const auto &it : group_map) {
                    func(it.second.objects[0]);
                }
            }
        }

        template<typename Func>
        void for_each_representative(Func &&func) noexcept {
            if constexpr (std::invocable<Func, ptr_type *, uint>) {
                uint cursor = 0u;
                for (auto iter = group_map.begin(); iter != group_map.end(); ++iter, ++cursor) {
                    func(iter->second.objects[0], cursor);
                }
            } else {
                for (const auto &it : group_map) {
                    func(it.second.objects[0]);
                }
            }
        }

        void erase(T t) noexcept {
            uint64_t hash_code = t->topology_hash();
            if (auto iter = group_map.find(hash_code); iter == group_map.cend()) {
                OC_ASSERT(false);
            }
            auto &lst = group_map[hash_code].objects;

            erase_if(lst, [&](auto elm) {
                return elm == raw_ptr(t);
            });

            if (lst.size() == 0) {
                group_map.erase(hash_code);
            }
        }

        void clear() noexcept {
            group_map.clear();
        }

        [[nodiscard]] bool empty() const noexcept { return group_map.empty(); }
        [[nodiscard]] auto size() const noexcept { return group_map.size(); }
    } group_mgr_;
    PolymorphicMode mode_{EInstance};

public:
    explicit Polymorphic(PolymorphicMode mode = EInstance)
        : mode_(mode) {}

    void push_back(T arg) noexcept {
        group_mgr_.add_object(arg);
        Super::push_back(arg);
        OC_ERROR_IF(topology_num() >= 256, "topology num must be less than 256");
    }

    Super::iterator erase(Super::iterator iter) noexcept {
        group_mgr_.erase(*iter);
        return Super::erase(iter);
    }

    void clear() {
        Super ::clear();
        group_mgr_.clear();
    }

    void remedy() noexcept {
        group_mgr_.clear();
        for (auto &elm : *this) {
            elm->invalidate();
            elm->reset_topology_hash();
            group_mgr_.add_object(elm);
        }
    }

    template<typename Index>
    requires concepts::integral<Index>
    [[nodiscard]] const T &operator[](Index i) const {
        return Super::operator[](i);
    }

    template<typename Index>
    requires concepts::integral<Index>
    [[nodiscard]] const T &at(Index i) const {
        return Super::at(i);
    }

    template<typename Index>
    requires concepts::integral<Index>
    [[nodiscard]] T &operator[](Index i) {
        return Super::operator[](i);
    }

    template<typename Index>
    requires concepts::integral<Index>
    [[nodiscard]] T &at(Index i) {
        return Super::at(i);
    }

    bool replace(int index, T new_obj) noexcept {
        ptr_type *ptr = Super::at(index).get();
        for (auto &item : group_mgr_.group_map) {
            GroupData &type_data = item.second;
            for (int i = 0; i < type_data.objects.size(); ++i) {
                ptr_type *obj = type_data.objects[i];
                if (obj == ptr) {
                    type_data.objects[i] = raw_ptr(new_obj);
                    Super::at(index) = std::move(new_obj);
                    return true;
                }
            }
        }
        return false;
    }

    [[nodiscard]] uint all_instance_num() const noexcept { return Super::size(); }
    [[nodiscard]] uint instance_num(const ptr_type *object) const noexcept {
        uint64_t hash_code = object->topology_hash();
        return group_mgr_.group_map.at(hash_code).objects.size();
    }
    [[nodiscard]] uint topology_num() const noexcept { return group_mgr_.size(); }
    [[nodiscard]] uint instance_num(uint type_idx) const noexcept {
        uint64_t hash_code = group_mgr_.topology_hash(type_idx);
        return group_mgr_.group_map.at(hash_code).objects.size();
    }
    [[nodiscard]] uint topology_index(const ptr_type *object) const noexcept {
        uint64_t hash_code = object->topology_hash();
        return group_mgr_.topology_index(hash_code);
    }
    [[nodiscard]] uint data_index(const ptr_type *object) const noexcept {
        uint64_t hash_code = object->topology_hash();
        return ocarina::get_index(group_mgr_.group_map.at(hash_code).objects, [&](auto obj) {
            return object == raw_ptr(obj);
        });
    }
    [[nodiscard]] DataAccessor data_accessor(const ptr_type *object,
                                             const Uint &data_index) noexcept {
        return {data_index * object->aligned_size(), get_datas(object)};
    }
    [[nodiscard]] DataAccessor data_accessor(const ptr_type *object,
                                             const Uint &data_index) const noexcept {
        return {data_index * object->aligned_size(), get_datas(object)};
    }
    [[nodiscard]] datas_type &get_datas(const ptr_type *object) noexcept {
        return group_mgr_.group_map.at(object->topology_hash()).data_set;
    }
    [[nodiscard]] const datas_type &get_datas(const ptr_type *object) const noexcept {
        return group_mgr_.group_map.at(object->topology_hash()).data_set;
    }

    template<typename Func>
    [[nodiscard]] auto find_if(Func &&func) const noexcept {
        return std::find_if(Super::cbegin(), Super::cend(), OC_FORWARD(func));
    }

    template<typename Func>
    [[nodiscard]] auto find_if(Func &&func) noexcept {
        return std::find_if(Super::begin(), Super::end(), OC_FORWARD(func));
    }

    /**
     * update data to managed memory
     * tips: Called on the host side code
     */
    void update(const ptr_type *object) noexcept {
        // todo
    }

    /**
     * update data to managed memory
     * tips: Called on the host side code
     */
    void upload_immediately() noexcept {
        group_mgr_.for_each_group([&](GroupData &group_data) {
            if (group_data.data_set.empty()) {
                return;
            }
            for (ptr_type *object : group_data.objects) {
                object->encode(group_data.data_set);
            }
            group_data.data_set.upload_immediately();
        });
    }

    [[nodiscard]] CommandBatch upload(bool async = true) noexcept {
        CommandBatch ret;
        group_mgr_.for_each_group([&](GroupData &group_data) {
            if (group_data.data_set.empty()) {
                return;
            }
            for (ptr_type *object : group_data.objects) {
                object->encode(group_data.data_set);
            }
            ret << group_data.data_set.upload(async);
        });
        return ret;
    }

    void set_datas(const ptr_type *object, datas_type &&datas) noexcept {
        group_mgr_.group_map.at(object->topology_hash()).data_set = ocarina::move(datas);
    }
    void set_mode(PolymorphicMode mode) noexcept { mode_ = mode; }
    [[nodiscard]] PolymorphicMode mode() const noexcept { return mode_; }
    [[nodiscard]] uint encode_id(uint id, const ptr_type *object) const noexcept {
        switch (mode_) {
            case EInstance:
                return ocarina::encode_id<H>(id, topology_index(object));
            case ETopology:
                return ocarina::encode_id<H>(data_index(object), topology_index(object));
        }
        OC_ASSERT(false);
        return InvalidUI32;
    }

    void fill_elements_offset() const noexcept {
        group_mgr_.for_each_group([&](const GroupData &group_data) {
            group_data.fill_elements_offset();
        });
    }

    void prepare(BindlessArray &bindless_array, Device &device) noexcept {
        switch (mode_) {
            case EInstance: break;
            case ETopology: {
                group_mgr_.for_each_group([&](GroupData &group_data) {
                    group_data.fill_elements_offset();
                    group_data.data_set.set_bindless_array(bindless_array);
                    group_data.data_set.resize(group_data.objects.size() * group_data.objects[0]->aligned_size() / sizeof(float));
                    for (ptr_type *object : group_data.objects) {
                        object->encode(group_data.data_set);
                    }
                    auto desc = ocarina::format("polymorphic: {}::type_buffer", group_data.class_name.c_str());
                    group_data.data_set.reset_device_buffer_immediately(device, desc);
                    group_data.data_set.register_self();
                    if (!group_data.data_set.empty()) {
                        group_data.data_set.upload_immediately();
                    }
                });
                break;
            }
            default: OC_ASSERT(false);
        }
    }

    template<typename ObjectID, typename Func>
    requires is_integral_expr_v<ObjectID>
    void dispatch(ObjectID &&object_id, const Func &func) const noexcept {
        auto [inst_id, type_id] = ocarina::decode_id<D>(OC_FORWARD(object_id));
        dispatch(type_id, inst_id, func);
    }

    template<typename TypeID, typename InstanceID, typename Func>
    requires is_all_integral_expr_v<TypeID, InstanceID>
    void dispatch(TypeID &&type_id, InstanceID &&inst_id, const Func &func) const noexcept {
        switch (mode_) {
            case EInstance: {
                dispatch_instance(OC_FORWARD(inst_id), [&](auto object) {
                    func(object);
                    object->after_decode();
                });
                break;
            }
            case ETopology: {
                dispatch_topology(OC_FORWARD(type_id), [&](auto object) {
                    DataAccessor da = data_accessor(object, OC_FORWARD(inst_id));
                    object->decode(da.load_dynamic_array<buffer_ty>(object->aligned_size() / 4));
                    func(object);
                    object->after_decode();
                });
                break;
            }
            default: OC_ASSERT(false);
        }
    }

    template<typename Index>
    requires is_integral_expr_v<Index>
    void dispatch_instance(Index index, const std::function<void(const ptr_type *)> &func) const noexcept {
        if (Super::empty()) [[unlikely]] {
            return;
        }
        Uint corrected = detail::correct_index(index, all_instance_num(),
                                               ocarina::format("dispatch_instance {}", typeid(*this).name()),
                                               traceback_string(1));
        comment(ocarina::format("const dispatch_instance, case num = {}", Super::size()));
        comment(typeid(*this).name());
        if (Super::size() == 1) {
            comment(typeid(*Super::at(0u)).name());
            func(raw_ptr(Super::at(0u)));
            return;
        }
        switch_(corrected, [&](Case case_, Default default_) {
            for (int i = 0; i < Super::size(); ++i) {
                comment(typeid(*Super::at(i)).name());
                case_(i, [&](Break break_) {
                    func(raw_ptr(Super::at(i)));
                    break_();
                });
            }
            default_([&](Break break_) {
                unreachable();
                break_();
            });
        });
    }

    template<typename Index>
    requires is_integral_expr_v<Index>
    void dispatch_topology(Index index, const std::function<void(const ptr_type *)> &func) const noexcept {
        if (group_mgr_.empty()) [[unlikely]] {
            return;
        }
        Uint corrected = detail::correct_index(index, topology_num(),
                                               ocarina::format("dispatch_topology {}", typeid(*this).name()),
                                               traceback_string(1));
        comment(ocarina::format("const dispatch_topology, case num = {}", topology_num()));
        comment(typeid(*this).name());
        if (group_mgr_.size() == 1) {
            ptr_type *elm = group_mgr_.group_map.begin()->second.objects[0];
            comment(typeid(*elm).name());
            func(elm);
            return;
        }
        switch_(corrected, [&](Case case_, Default default_) {
            group_mgr_.for_each_representative([&](ptr_type *elm, uint i) {
                comment(typeid(*elm).name());
                case_(i, [&](Break break_) {
                    func(elm);
                    break_();
                });
            });
            default_([&](Break break_) {
                unreachable();
                break_();
            });
        });
    }

    template<typename Func>
    void for_each_instance(Func &&func) const noexcept {
        if constexpr (std::invocable<Func, T, uint>) {
            for (uint i = 0; i < Super::size(); ++i) {
                func(Super::at(i), i);
            }
        } else {
            for (uint i = 0; i < Super::size(); ++i) {
                func(Super::at(i));
            }
        }
    }

    template<typename Func>
    [[nodiscard]] uint get_index(Func &&func) const noexcept {
        return ocarina::get_index(*this, OC_FORWARD(func));
    }

    template<typename Func>
    void for_each_instance(Func &&func) noexcept {
        if constexpr (std::invocable<Func, T, uint>) {
            for (uint i = 0; i < Super::size(); ++i) {
                func(Super::at(i), i);
            }
        } else {
            for (uint i = 0; i < Super::size(); ++i) {
                func(Super::at(i));
            }
        }
    }

    template<typename Func>
    void for_each_representative(Func &&func) const noexcept {
        group_mgr_.for_each_representative(OC_FORWARD(func));
    }

    template<typename Func>
    void for_each_representative(Func &&func) noexcept {
        group_mgr_.for_each_representative(OC_FORWARD(func));
    }
};

}// namespace ocarina
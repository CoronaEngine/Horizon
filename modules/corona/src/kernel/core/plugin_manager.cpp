#include <map>
#include <memory>
#include <mutex>

#include "corona/kernel/core/i_plugin_manager.h"
#include "corona/pal/i_dynamic_library.h"

namespace Corona::PAL {
std::unique_ptr<IDynamicLibrary> create_dynamic_library();
}

namespace Corona::Kernel {

using CreatePluginFunc = IPlugin* (*)();
using DestroyPluginFunc = void (*)(IPlugin*);

// Custom deleter for shared_ptr that uses the plugin's destroy function
class PluginDeleter {
   public:
    explicit PluginDeleter(DestroyPluginFunc destroy_func) : destroy_func_(destroy_func) {}

    void operator()(IPlugin* plugin) const {
        if (plugin && destroy_func_) {
            destroy_func_(plugin);
        }
    }

   private:
    DestroyPluginFunc destroy_func_;
};

class PluginManager : public IPluginManager {
   public:
    PluginManager() = default;

    ~PluginManager() override {
        shutdown_all();
    }

    bool load_plugin(std::string_view path) override {
        std::lock_guard<std::mutex> lock(mutex_);

        auto library = PAL::create_dynamic_library();
        if (!library->load(path)) {
            return false;
        }

        auto create_func = reinterpret_cast<CreatePluginFunc>(library->get_symbol("create_plugin"));
        auto destroy_func = reinterpret_cast<DestroyPluginFunc>(library->get_symbol("destroy_plugin"));

        if (!create_func || !destroy_func) {
            return false;
        }

        IPlugin* plugin = create_func();
        if (!plugin) {
            return false;
        }

        PluginInfo info = plugin->get_info();
        plugins_.emplace(info.name, PluginEntry{
                                        std::shared_ptr<IPlugin>(plugin, PluginDeleter(destroy_func)),
                                        std::move(library),
                                        false});

        return true;
    }

    void unload_plugin(std::string_view name) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = plugins_.find(std::string(name));
        if (it != plugins_.end()) {
            if (it->second.initialized) {
                it->second.plugin->shutdown();
            }
            plugins_.erase(it);
        }
    }

    std::shared_ptr<IPlugin> get_plugin(std::string_view name) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = plugins_.find(std::string(name));
        if (it != plugins_.end()) {
            return it->second.plugin;
        }
        return nullptr;
    }

    std::vector<std::string> get_loaded_plugins() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> names;
        names.reserve(plugins_.size());
        for (const auto& [name, entry] : plugins_) {
            names.push_back(name);
        }
        return names;
    }

    bool initialize_all() override {
        std::lock_guard<std::mutex> lock(mutex_);

        // TODO: Sort plugins by dependencies
        for (auto& [name, entry] : plugins_) {
            if (!entry.initialized) {
                if (!entry.plugin->initialize()) {
                    return false;
                }
                entry.initialized = true;
            }
        }
        return true;
    }

    void shutdown_all() override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [name, entry] : plugins_) {
            if (entry.initialized) {
                entry.plugin->shutdown();
                entry.initialized = false;
            }
        }
    }

   private:
    struct PluginEntry {
        std::shared_ptr<IPlugin> plugin;
        std::unique_ptr<PAL::IDynamicLibrary> library;
        bool initialized;
    };

    std::map<std::string, PluginEntry> plugins_;
    mutable std::mutex mutex_;
};

// Factory function
std::unique_ptr<IPluginManager> create_plugin_manager() {
    return std::make_unique<PluginManager>();
}

}  // namespace Corona::Kernel

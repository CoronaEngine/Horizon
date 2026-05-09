#include <memory>

#include "corona/pal/i_dynamic_library.h"
#include "corona/pal/i_file_system.h"

namespace Corona::PAL {

// Forward declarations of factory functions defined in implementation files
std::unique_ptr<IFileSystem> create_file_system();

#ifdef _WIN32
std::unique_ptr<IDynamicLibrary> create_dynamic_library();
#endif

}  // namespace Corona::PAL

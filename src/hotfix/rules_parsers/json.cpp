//
// Created by Zero on 2024/8/27.
//

#include "hotfix/export.h"
#include "hotfix/build_rules.h"
#include "hotfix/hotfix.h"

namespace vision::inline hotfix {

class JsonParser : public BuildRules {
public:
    JsonParser();
    void parse(const std::string &content) override;
};
JsonParser::JsonParser() {
}

void JsonParser::parse(const std::string &content) {
}

}// namespace vision::inline hotfix

VS_EXPORT_API vision::hotfix::BuildRules *create() {
    return ocarina::new_with_allocator<vision::hotfix::JsonParser>();
}

VS_EXPORT_API void destroy(vision::hotfix::BuildRules *obj) {
    ocarina::delete_with_allocator(obj);
}
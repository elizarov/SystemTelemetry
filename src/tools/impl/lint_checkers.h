#pragma once

#include <memory>
#include <vector>

#include "tools/impl/lint_common.h"
#include "tools/impl/lint_json.h"

namespace tools::lint {

class Checker {
public:
    virtual ~Checker();
    virtual void ProcessFile(const FileRecord& record) = 0;
    virtual CheckResult Finish(bool verbose) = 0;
};

std::vector<std::unique_ptr<Checker>> CreateCheckers(const JsonValue& config, const CheckerContext& context);

}  // namespace tools::lint

#pragma once

#include <string>

#include "contracts/ids.hpp"

namespace cse {

class ICommand {
public:
    virtual ~ICommand() = default;

    virtual CommandId id() const = 0;
    virtual std::string name() const = 0;
    virtual bool Execute() = 0;
    virtual void Undo() = 0;
};

}  // namespace cse

#pragma once

#include <string>

namespace rx::ir {

enum class Type { Void, I1, I8, I32, Ptr };

std::string type_name(Type type);
bool is_value_type(Type type);
unsigned integer_width(Type type);

}

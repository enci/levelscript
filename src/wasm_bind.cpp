#include <emscripten/bind.h>
#include "inspect.hpp"

using namespace emscripten;

EMSCRIPTEN_BINDINGS(ls_module) {
    function("inspect_json", &ls::inspect_json);
}

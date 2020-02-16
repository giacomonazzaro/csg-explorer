#include <pybind11/pybind11.h>

#include "../source/csg.h"
#include "../source/parser.h"
//
#include "../source/ext/yocto-gl/apps/yscnitraces.cpp"

float eval(const CsgTree& csg, float x, float y, float z) {
  return eval_csg(csg, {x, y, z});
}

namespace py = pybind11;

PYBIND11_MODULE(pycsg, m) {
  m.doc() = "pybind11 csg plugin";

  m.def("eval", &eval);
  m.def("load_csg", &load_csg);
  m.def("run_app", &run_app);

  py::class_<CsgTree>(m, "CsgTree")
      .def(py::init<>())
      .def("__repr__", [](const CsgTree& a) {
        string result = "";
        for (int i = 0; i < a.nodes.size(); i++) {
          auto& node = a.nodes[i];
          if (node.children == vec2i{-1, -1}) {
            for (int k = 0; k < 4; k++) {
              result += std::to_string(node.primitive.params[i]) + " ";
            }
          } else {
            result += "[" + std::to_string(node.children.x) + " " +
                      std::to_string(node.children.y) + "] ";
            result += std::to_string(node.operation.blend) + " " +
                      std::to_string(node.operation.softness);
          }
          result += "\n";
        }
        return result;
      });
}
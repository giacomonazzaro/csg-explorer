#include <pybind11/pybind11.h>

#include "../source/csg.h"
#include "../source/parser.h"
//
#include "../source/main.cpp"

float eval(const CsgTree& csg, float x, float y, float z) {
  return eval_csg(csg, {x, y, z});
}

void render(const CsgTree& csg) {
  auto app = make_shared<app_state>();
  app->csg = csg;
  run_app(app);
}

string print(const CsgTree& a) {
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
}

namespace py = pybind11;

PYBIND11_MODULE(pycsg, m) {
  m.doc() = "pybind11 csg plugin";

  m.def("eval", &eval);
  m.def("load_csg", &load_csg);
  m.def("render", &render);

  py::class_<CsgTree>(m, "CsgTree").def(py::init<>()).def("__repr__", &print);
}
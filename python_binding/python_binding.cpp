#include <pybind11/pybind11.h>

#include "../source/csg.h"
#include "../source/parser.h"

float eval(const CsgTree& csg, float x, float y, float z) {
  return eval_csg(csg, {x, y, z});
}

namespace py = pybind11;

PYBIND11_MODULE(pycsg, m) {
  m.doc() = "pybind11 csg plugin";

  m.def("eval", &eval);
  m.def("load_csg", &load_csg);

  py::class_<CsgTree>(m, "CsgTree")
      .def(py::init<>())
      .def("__repr__", [](const CsgTree& a) { return "<csg.CsgTree>"; });
}
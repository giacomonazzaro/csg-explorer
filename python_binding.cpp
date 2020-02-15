#include <pybind11/pybind11.h>

#include "../csg.h"
#include "../parser.h"

// a function that has nothing to do with the class
// the point is to show how one can return a copy "Eigen::VectorXd"
float eval(const CsgTree& csg, float x, float y, float z) {
  return eval_csg(csg, {x, y, z});
}

// ----------------
// Python interface
// ----------------

namespace py = pybind11;

PYBIND11_MODULE(csg, m) {
  m.doc() = "pybind11 csg plugin";

  m.def("eval", &eval);
  m.def("load_csg", &load_csg);

  py::class_<CsgTree>(m, "CsgTree")
      .def(py::init<>())
      .def("__repr__", [](const CsgTree& a) { return "<csg.CsgTree>"; });
}
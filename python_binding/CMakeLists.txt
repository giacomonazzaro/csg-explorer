cmake_minimum_required(VERSION 2.8.12)
project(pycsg)

add_subdirectory(pybind11)
pybind11_add_module(pycsg python_binding.cpp)
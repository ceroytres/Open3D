// ----------------------------------------------------------------------------
// -                        Open3D: www.open3d.org                            -
// ----------------------------------------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2018 www.open3d.org
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
// ----------------------------------------------------------------------------

#include <vector>

#include "open3d/core/Tensor.h"
#include "open3d/core/TensorKey.h"
#include "open3d/utility/Optional.h"
#include "pybind/core/core.h"
#include "pybind/core/tensor_converter.h"
#include "pybind/docstring.h"
#include "pybind/open3d_pybind.h"
#include "pybind/pybind_utils.h"

namespace open3d {
namespace core {

static TensorKey ToTensorKey(int key) { return TensorKey::Index(key); }

static TensorKey ToTensorKey(const py::slice& key) {
    Py_ssize_t start;
    Py_ssize_t stop;
    Py_ssize_t step;
    PySlice_Unpack(key.ptr(), &start, &stop, &step);
    PySliceObject* slice_key = reinterpret_cast<PySliceObject*>(key.ptr());
    return TensorKey::Slice(static_cast<int64_t>(start),
                            static_cast<int64_t>(stop),
                            static_cast<int64_t>(step),
                            py::detail::PyNone_Check(slice_key->start),
                            py::detail::PyNone_Check(slice_key->stop),
                            py::detail::PyNone_Check(slice_key->step));
}

static TensorKey ToTensorKey(const py::list& key) {
    Tensor key_tensor = PyTupleToTensor(key);
    if (key_tensor.GetDtype() != Dtype::Bool) {
        key_tensor = key_tensor.To(Dtype::Int64, /*copy=*/false);
    }
    return TensorKey::IndexTensor(key_tensor);
}

static TensorKey ToTensorKey(const py::tuple& key) {
    Tensor key_tensor = PyTupleToTensor(key);
    if (key_tensor.GetDtype() != Dtype::Bool) {
        key_tensor = key_tensor.To(Dtype::Int64, /*copy=*/false);
    }
    return TensorKey::IndexTensor(key_tensor);
}

static TensorKey ToTensorKey(const py::array& key) {
    Tensor key_tensor = PyArrayToTensor(key, /*inplace=*/false);
    if (key_tensor.GetDtype() != Dtype::Bool) {
        key_tensor = key_tensor.To(Dtype::Int64);
    }
    return TensorKey::IndexTensor(key_tensor);
}

static TensorKey ToTensorKey(const Tensor& key_tensor) {
    if (key_tensor.GetDtype() != Dtype::Bool) {
        return TensorKey::IndexTensor(
                key_tensor.To(Dtype::Int64, /*copy=*/false));
    } else {
        return TensorKey::IndexTensor(key_tensor);
    }
}

/// Convert supported types to TensorKey. Infer types via type name and dynamic
/// casting. Supported types:
/// 1) int
/// 2) slice
/// 3) list
/// 4) tuple
/// 5) numpy.ndarray
/// 6) Tensor
static TensorKey PyHandleToTensorKey(const py::handle& item) {
    // Infer types from type name and dynamic casting.
    // See: https://github.com/pybind/pybind11/issues/84.
    std::string class_name(item.get_type().str());
    if (class_name == "<class 'int'>") {
        return ToTensorKey(static_cast<int64_t>(item.cast<py::int_>()));
    } else if (class_name == "<class 'slice'>") {
        return ToTensorKey(item.cast<py::slice>());
    } else if (class_name == "<class 'list'>") {
        return ToTensorKey(item.cast<py::list>());
    } else if (class_name == "<class 'tuple'>") {
        return ToTensorKey(item.cast<py::tuple>());
    } else if (class_name == "<class 'numpy.ndarray'>") {
        return ToTensorKey(item.cast<py::array>());
    } else if (class_name.find("open3d") != std::string::npos &&
               class_name.find("Tensor") != std::string::npos) {
        try {
            Tensor* tensor = item.cast<Tensor*>();
            return ToTensorKey(*tensor);
        } catch (...) {
            utility::LogError("Cannot cast index to Tensor.");
        }
    } else {
        utility::LogError("PyHandleToTensorKey has invlaid key type {}.",
                          class_name);
    }
}

void pybind_core_tensor_accessor(py::class_<Tensor>& tensor) {
    utility::LogInfo("pybind_core_extra");

    tensor.def("__getitem__", [](const Tensor& tensor, int key) {
        return tensor.GetItem(ToTensorKey(key));
    });

    tensor.def("__getitem__", [](const Tensor& tensor, const py::slice& key) {
        return tensor.GetItem(ToTensorKey(key));
    });

    tensor.def("__getitem__", [](const Tensor& tensor, const py::array& key) {
        return tensor.GetItem(ToTensorKey(key));
    });

    tensor.def("__getitem__", [](const Tensor& tensor, const Tensor& key) {
        utility::LogInfo("getitem tensor");
        return tensor.GetItem(ToTensorKey(key));
    });

    // List is interpreted as one TensorKey object, which calls
    // Tensor::GetItem(const TensorKey&).
    // E.g. a[[3, 4, 5]] is a list. It indices the first dimension of a.
    // E.g. a[(3, 4, 5)] does very different things. It indices the first three
    //      dimensions of a.
    tensor.def("__getitem__", [](const Tensor& tensor, const py::list& key) {
        return tensor.GetItem(ToTensorKey(key));
    });

    // Tuple is interpreted as a vector TensorKey objects, which calls
    // Tensor::GetItem(const std::vector<TensorKey>&).
    // E.g. a[1:2, [3, 4, 5], 3:10] results in a tuple of size 3.
    tensor.def("__getitem__", [](const Tensor& tensor, const py::tuple& key) {
        std::vector<TensorKey> tks;
        for (const py::handle& item : key) {
            tks.push_back(PyHandleToTensorKey(item));
        }
        return tensor.GetItem(tks);
    });

    tensor.def("_getitem", [](const Tensor& tensor, const TensorKey& tk) {
        return tensor.GetItem(tk);
    });

    tensor.def("_getitem_vector",
               [](const Tensor& tensor, const std::vector<TensorKey>& tks) {
                   return tensor.GetItem(tks);
               });

    tensor.def("_setitem",
               [](Tensor& tensor, const TensorKey& tk, const Tensor& value) {
                   return tensor.SetItem(tk, value);
               });

    tensor.def("_setitem_vector",
               [](Tensor& tensor, const std::vector<TensorKey>& tks,
                  const Tensor& value) { return tensor.SetItem(tks, value); });
}

}  // namespace core
}  // namespace open3d

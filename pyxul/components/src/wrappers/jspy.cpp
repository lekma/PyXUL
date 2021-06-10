/*
# Python for XUL
# copyright © 2021 Malek Hadj-Ali
#
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 3
# as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#include "wrappers/api.h"
#include "wrappers/internals.h"


namespace pyxul::wrappers::jspy {


namespace { // anonymous


static JS::Value
WrapDouble(double value)
{
    if (value == -1.0 && PyErr_Occurred()) {
        return JS::UndefinedValue();
    }
    return JS::DoubleValue(value);
}


static JS::Value
WrapLong(PyObject *aValue)
{
    int overflow;
    long value = PyLong_AsLongAndOverflow(aValue, &overflow);

    if (value == -1) {
        if (overflow) {
            return WrapDouble(PyLong_AsDouble(aValue));
        }
        if (PyErr_Occurred()) {
            return JS::UndefinedValue();
        }
    }
    return JS::NumberValue(value);
}


static JS::Value
WrapBytes(JSContext *aCx, const char *str, Py_ssize_t size, bool uc)
{
    JSString *aJSString = nullptr;

    if (uc) {
        aJSString = JS_NewUCStringCopyN(
            aCx, (const char16_t *)(str + 2), ((size / 2) - 1)
        );
    }
    else {
        aJSString = JS_NewStringCopyN(aCx, str, size);
    }
    return aJSString ? JS::StringValue(aJSString) : JS::UndefinedValue();
}


static JS::Value
WrapValue(JSContext *aCx, PyObject *aValue)
{
    JSObject *aJSObject = nullptr;

    if (PyLong_Check(aValue)) {
        return WrapLong(aValue);
    }
    if (PyFloat_Check(aValue)) {
        return WrapDouble(PyFloat_AsDouble(aValue));
    }
    if (PyBytes_Check(aValue)) {
        return WrapBytes(
            aCx, PyBytes_AS_STRING(aValue), PyBytes_GET_SIZE(aValue), false
        );
    }
    if (PyByteArray_Check(aValue)) {
        return WrapBytes(
            aCx, PyByteArray_AS_STRING(aValue), PyByteArray_GET_SIZE(aValue), false
        );
    }
    if (PyUnicode_Check(aValue)) {
        return WrapUnicode(aCx, aValue);
    }
    if ((aJSObject = WrapObject(aCx, aValue))) {
        return JS::ObjectValue(*aJSObject);
    }
    return JS::UndefinedValue();
}


} // namespace anonymous


JS::Value
WrapUnicode(JSContext *aCx, PyObject *aValue)
{
    JS::Value result = JS::UndefinedValue();
    PyObject *aBytes = nullptr;

    if ((aBytes = PyUnicode_AsUTF16String(aValue))) { // +1
        result = WrapBytes(
            aCx, PyBytes_AS_STRING(aBytes), PyBytes_GET_SIZE(aBytes), true
        );
        Py_DECREF(aBytes); // -1
    }
    return result;
}


bool
WrapArgs(JSContext *aCx, PyObject *args, JS::AutoValueVector &out)
{
    PyObject *seq = nullptr;
    Py_ssize_t size = 0, i;
    JS::Value arg = JS::UndefinedValue();

    if (!out.empty()) {
        PyErr_SetString(PyExc_SystemError, "expected empty arg vector");
        return false;
    }
    if (!(seq = PySequence_Fast(args, "expected a sequence"))) { // +1
        return false;
    }
    if ((size = PySequence_Fast_GET_SIZE(seq)) && !out.reserve(size)) {
        PyErr_NoMemory();
        Py_DECREF(seq); // -1
        return false;
    }
    for (i = 0; i < size; i++) {
        arg = Wrap(aCx, PySequence_Fast_GET_ITEM(seq, i)); // borrowed
        if (arg.isUndefined()) {
            out.clear();
            Py_DECREF(seq); // -1
            return false;
        }
        out.infallibleAppend(arg);
    }
    Py_DECREF(seq); // -1
    return true;
}


bool
WrapArgs(JSContext *aCx, const JS::HandleObject &args, JS::AutoValueVector &out)
{
    uint32_t size = 0, i;
    JS::RootedValue arg(aCx);

    if (!out.empty()) {
        PyErr_SetString(PyExc_SystemError, "expected empty arg vector");
        return false;
    }
    if (!JS_GetArrayLength(aCx, args, &size)) {
        return false;
    }
    if (size && !out.reserve(size)) {
        PyErr_NoMemory();
        return false;
    }
    for (i = 0; i < size; i++) {
        if (!JS_GetElement(aCx, args, i, &arg)) {
            out.clear();
            return false;
        }
        out.infallibleAppend(arg);
    }
    return true;
}


PyObject *
Unwrap(const JS::HandleObject &aJSObject)
{
    return Object::Unwrap(aJSObject);
}


/* -------------------------------------------------------------------------- */

bool
Check(JS::MutableHandleObject aJSObject)
{
    aJSObject.set(xpc::Unwrap(aJSObject));
    return Type::Check(aJSObject);
}


JSObject *
WrapObject(JSContext *aCx, PyObject *aObject)
{
    JS::RootedObject aResult(aCx);

    if (!(aResult = pyjs::Unwrap(aObject))) {
        aResult = Object::New(aCx, aObject);
    }
    return aResult;
}


JS::Value
Wrap(JSContext *aCx, PyObject *aValue)
{
    JS::RootedValue value(aCx, JS::UndefinedValue());

    if (aValue == Py_None) {
        value.setNull();
    }
    else if (aValue == Py_False) {
        value.setBoolean(false);
    }
    else if (aValue == Py_True) {
        value.setBoolean(true);
    }
    else {
        value.set(WrapValue(aCx, aValue));
    }
    if (!value.isUndefined() && !JS_WrapValue(aCx, &value)) {
        value.setUndefined();
    }
    if (value.isUndefined() && !PyErr_Occurred()) {
        PyErr_Format(PyExc_TypeError, "failed to wrap %s", aValue);
    }
    return value;
}


/* Initialize/Finalize ------------------------------------------------------ */

bool
Initialize(void)
{
    AutoJSContext aCx;
    AutoReporter ar(aCx);

    // init base prototype
    PyObject *aTypeBase = (PyObject *)&PyType_Type;
    Type::ProtoBase.init(aCx, Type::New(aCx, aTypeBase));
    PY_ENSURE_TRUE(
        (Type::ProtoBase && Type::ProtoBase.initialized()), false,
        errors::JSError, "Failed to create initial prototype"
    );
    Object::Objects.put(aTypeBase, Type::ProtoBase);

    return true;
}


void
Finalize(void)
{
    Type::ProtoBase.reset();

    Object::Objects.finalize();
}


} // namespace pyxul::wrappers::jspy


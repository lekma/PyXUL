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


namespace pyxul::wrappers::pyjs {


namespace { // anonymous


} // namespace anonymous


PyObject *
WrapString(JSContext *aCx, const JS::HandleString &aJSString)
{
    size_t size = size_t(-1);
    char *str = nullptr;
    PyObject *aResult = nullptr;

    if ((str = JS_EncodeStringToUTF8(aCx, aJSString))) {
        if ((size = strlen(str)) < PY_SSIZE_T_MAX) {
            aResult = PyUnicode_DecodeUTF8Stateful(str, size, nullptr, nullptr);
        }
        else {
            PyErr_SetString(
                PyExc_OverflowError, "JS::String too long for Python"
            );
        }
        JS_free(aCx, str);
    }
    return aResult;
}


PyObject *
WrapSymbol(JSContext *aCx, const JS::HandleSymbol &aJSSymbol)
{
    JS::RootedString aJSString(aCx, JS::GetSymbolDescription(aJSSymbol));
    PY_ENSURE_TRUE(
        aJSString, nullptr, errors::JSError, "JS::Symbol without description"
    );
    return WrapString(aCx, aJSString);
}


// WrapId
PyObject *
WrapId(JSContext *aCx, jsid aId)
{
    if (JSID_IS_STRING(aId)) {
        JS::RootedString aJSString(aCx, JSID_TO_STRING(aId));
        return WrapString(aCx, aJSString);
    }
    if (JSID_IS_SYMBOL(aId)) {
        JS::RootedSymbol aJSSymbol(aCx, JSID_TO_SYMBOL(aId));
        return WrapSymbol(aCx, aJSSymbol);
    }
    PyErr_SetString(PyExc_TypeError, "property name must be string or symbol");
    return nullptr;
}


PyObject *
WrapArgs(JSContext *aCx, JS::CallArgs &args)
{
    unsigned size = args.length(), i;
    PyObject *result = nullptr, *item = nullptr;

    if ((result = PyTuple_New(size))) {
        for (i = 0; i < size; i++) {
            if (!(item = Wrap(aCx, args[i]))) { // +1
                Py_CLEAR(result);
                break;
            }
            PyTuple_SET_ITEM(result, i, item); // -1
        }
    }
    return result;
}


JSObject *
Unwrap(PyObject *aPyObject)
{
    return Object::Unwrap(aPyObject);
}


/* -------------------------------------------------------------------------- */

bool
Check(PyObject *aPyObject)
{
    return PyObject_TypeCheck(aPyObject, &Object::Type);
}


PyObject *
WrapObject(
    JSContext *aCx, JS::MutableHandleObject aJSObject,
    const JS::HandleObject &aThis
)
{
    PyObject *aResult = nullptr;

    aJSObject.set(xpc::Unwrap(aJSObject));
    if (aJSObject && !(aResult = jspy::Unwrap(aJSObject))) {
        aResult = Object::New(aCx, aJSObject, aThis);
    }
    return aResult;
}


PyObject *
Wrap(JSContext *aCx, const JS::HandleValue &aJSValue)
{
    if (aJSValue.isPrimitive()) {
        PY_ENSURE_TRUE(
            !aJSValue.isUndefined(), nullptr,
            PyExc_TypeError, "cannot wrap JS::UndefinedValue"
        );
        if (aJSValue.isNull()) {
            Py_RETURN_NONE;
        }
        if (aJSValue.isBoolean()) {
            if (aJSValue.isTrue()) {
                Py_RETURN_TRUE;
            }
            Py_RETURN_FALSE;
        }
        if (aJSValue.isInt32()) {
            return PyLong_FromLong(aJSValue.toInt32());
        }
        if (aJSValue.isDouble()) {
            return PyFloat_FromDouble(aJSValue.toDouble());
        }
        if (aJSValue.isString()) {
            JS::RootedString aJSString(aCx, aJSValue.toString());
            PY_ENSURE_TRUE(
                aJSString, nullptr,
                errors::JSError, "JS::StringValue.toString() returned null"
            );
            return WrapString(aCx, aJSString);
        }
        if (aJSValue.isSymbol()) {
            JS::RootedSymbol aJSSymbol(aCx, aJSValue.toSymbol());
            PY_ENSURE_TRUE(
                aJSSymbol, nullptr,
                errors::JSError, "JS::SymbolValue.toSymbol() returned null"
            );
            return WrapSymbol(aCx, aJSSymbol);
        }
        PyErr_SetString(PyExc_TypeError, "unknow JS::Value primitive type");
        return nullptr;
    }
    JS::RootedObject aJSObject(aCx, &aJSValue.toObject());
    PY_ENSURE_TRUE(
        aJSObject, nullptr,
        errors::JSError, "JS::ObjectValue.toObject() returned null"
    );
    return WrapObject(aCx, &aJSObject);
}


/* Initialize/Finalize ------------------------------------------------------ */

bool
Initialize(void)
{
    if (
        PyType_Ready(&Object::Type) ||
        _PyType_ReadyWithBase(&Object::Iterator::Type, &Object::Type) ||
        _PyType_ReadyWithBase(&Object::Array::Type, &Object::Type) ||
        _PyType_ReadyWithBase(&Object::Map::Type, &Object::Type) ||
        _PyType_ReadyWithBase(&Object::Set::Type, &Object::Type) ||
        _PyType_ReadyWithBase(&Object::Callable::Type, &Object::Type)
    ) {
        return false;
    }
    return true;
}


} // namespace pyxul::wrappers::pyjs


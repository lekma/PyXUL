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


static std::allocator<JS::PersistentRootedObject> alloc;


} // namespace anonymous


/* --------------------------------------------------------------------------
   pyxul::wrappers::pyjs::Object
   -------------------------------------------------------------------------- */

bool
Object::__iter__(JSContext* aCx, Object *self, JS::MutableHandleValue aResult)
{
    // get the @@iterator method (sigh...)
    JS::RootedId aId(
        aCx, SYMBOL_TO_JSID(JS::GetWellKnownSymbol(aCx, JS::SymbolCode::iterator))
    );
    JS::RootedValue aCallee(aCx);
    if (!JS_GetPropertyById(aCx, self->mJSObject, aId, &aCallee)) {
        return false;
    }
    PY_ENSURE_TRUE(
        (aCallee.isObject() && JS::IsCallable(&aCallee.toObject())), false,
        PyExc_TypeError, "%S is not iterable", self
    );
    return JS::Call(
        aCx, self->mJSObject, aCallee, JS::HandleValueArray::empty(), aResult
    );
}


void
Object::__finalize__(Object *self)
{
    self->mThis.reset();
    self->mJSObject.reset();
}


JSObject *
Object::__unwrap__(Object *self)
{
    return self->mJSObject;
}


PyObject *
Object::__repr__(JSContext *aCx, Object *self)
{
    JS::RootedValue aJSValue(aCx, JS::ObjectValue(*self->mJSObject));
    JS::RootedString aJSString(aCx, JS_ValueToSource(aCx, aJSValue));
    return aJSString ? WrapString(aCx, aJSString) : nullptr;
}


PyObject *
Object::__str__(JSContext *aCx, Object *self)
{
    JS::RootedValue aJSValue(aCx, JS::ObjectValue(*self->mJSObject));
    JS::RootedString aJSString(aCx, JS::ToString(aCx, aJSValue));
    return aJSString ? WrapString(aCx, aJSString) : nullptr;
    /*return PyUnicode_FromFormat(
        "[object %s]", js::ObjectClassName(aCx, self->mJSObject)
    );*/
}


PyObject *
Object::__getattr__(JSContext *aCx, Object *self, const char *aName)
{
    JS::RootedValue aResult(aCx, JS::UndefinedValue());
    if (!JS_GetProperty(aCx, self->mJSObject, aName, &aResult)) {
        return nullptr;
    }
    PY_ENSURE_TRUE(
        !aResult.isUndefined(), nullptr,
        PyExc_AttributeError, "%S has no attribute '%s'", self, aName
    );
    if (aResult.isObject()) {
        JS::RootedObject aJSObject(aCx, &aResult.toObject());
        PY_ENSURE_TRUE(
            aJSObject, nullptr, errors::JSError, "JS_GetProperty() returned null"
        );
        if (JS::IsCallable(aJSObject)) {
            return WrapObject(aCx, &aJSObject, self->mJSObject);
        }
        return WrapObject(aCx, &aJSObject);
    }
    return Wrap(aCx, aResult);
}


int
Object::__setattr__(
    JSContext *aCx, Object *self, const char *aName, PyObject *aValue
)
{
    JS::RootedValue aJSValue(aCx, jspy::Wrap(aCx, aValue));
    if (
        aJSValue.isUndefined() ||
        !JS_SetProperty(aCx, self->mJSObject, aName, aJSValue)
    ) {
        return -1;
    }
    return 0;
}


int
Object::__delattr__(JSContext *aCx, Object *self, const char *aName)
{
    JS::ObjectOpResult deleted;
    bool ok = false, found = false;

    ok = JS_DeleteProperty(aCx, self->mJSObject, aName, deleted);
    if (ok && deleted) {
        ok = JS_HasProperty(aCx, self->mJSObject, aName, &found);
        if (ok && found) {
            PyErr_Format(PyExc_AttributeError, "readonly attribute '%s'", aName);
            deleted.failReadOnly();
        }
    }
    return (ok && deleted) ? 0 : -1;
}


/* -------------------------------------------------------------------------- */

PyObject *
Object::__compare__(Object *self, PyObject *aOther, int op)
{
    bool equals = false;

    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::__compare__");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    JS::RootedObject aOtherObj(aCx, __unwrap__((Object *)aOther));
    if (!aOtherObj) {
        return nullptr;
    }
    JS::RootedValue aSelfVal(aCx, JS::ObjectValue(*self->mJSObject));
    JS::RootedValue aOtherVal(aCx, JS::ObjectValue(*aOtherObj));
    if (!JS_StrictlyEqual(aCx, aSelfVal, aOtherVal, &equals)) {
        return nullptr;
    }
    if (equals) {
        switch (op) {
            case Py_EQ:
            case Py_LE:
            case Py_GE:
                Py_RETURN_TRUE;
            case Py_NE:
            case Py_LT:
            case Py_GT:
                Py_RETURN_FALSE;
            default:
                PyErr_BadArgument();
                return nullptr;
        }
    }
    switch (op) {
        case Py_EQ:
            Py_RETURN_FALSE;
        case Py_NE:
            Py_RETURN_TRUE;
        case Py_LE:
        case Py_GE:
        case Py_LT:
        case Py_GT:
            PyErr_SetString(PyExc_TypeError, "unorderable types");
            return nullptr;
        default:
            PyErr_BadArgument();
            return nullptr;
    }
}


/* -------------------------------------------------------------------------- */

template<typename T>
PyObject *
Object::Alloc(
    JSContext *aCx, const JS::HandleObject &aJSObject,
    const JS::HandleObject &aThis
)
{
    Object *self = nullptr;

    if ((self = (Object *)T::Type.tp_alloc(&T::Type, 0))) {
        alloc.construct(&self->mJSObject, aCx, aJSObject);
        alloc.construct(&self->mThis, aCx, aThis);
    }
    return self;
}


// Object::Type.tp_dealloc
void
Object::Dealloc(Object *self)
{
    if (PyObject_CallFinalizerFromDealloc(self)) {
        return;
    }
    alloc.destroy(&self->mThis);
    alloc.destroy(&self->mJSObject);
    Py_TYPE(self)->tp_free(self);
}


// Object::Type.tp_repr
PyObject *
Object::Repr(Object *self)
{
    PyObject *result = nullptr, *aRepr = nullptr;

    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Repr");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    if ((aRepr = __str__(aCx, self))) {
        result = PyUnicode_FromFormat(
            "<%s object at %p; wrapping: %U>",
            Py_TYPE(self)->tp_name, self, aRepr
        );
        Py_DECREF(aRepr);
    }
    return result;
}


// Object::Type.tp_hash
Py_hash_t
Object::Hash(Object *self)
{
    return _Py_HashPointer(self->mJSObject);
}


// Object::Type.tp_str
PyObject *
Object::Str(Object *self)
{
    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Str");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    return __str__(aCx, self);
}


// Object::Type.tp_getattro
PyObject *
Object::GetAttrO(Object *self, PyObject *aName)
{
    const char *name = nullptr;
    PyObject *result = nullptr;

    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::GetAttrO");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    /*if ((name = PyUnicode_AsUTF8(aName))) {
        return __getattr__(aCx, self, name);
    }
    return nullptr;*/

    if ((name = PyUnicode_AsUTF8(aName))) {
        if (
            !(result = __getattr__(aCx, self, name)) &&
            PyErr_ExceptionMatches(PyExc_AttributeError)
        ) {
            PyErr_Clear();
            result = PyObject_GenericGetAttr(self, aName);
        }
    }
    return result;
}


// Object::Type.tp_setattro
int
Object::SetAttrO(Object *self, PyObject *aName, PyObject *aValue)
{
    const char *name = nullptr;

    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::SetAttrO");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    if ((name = PyUnicode_AsUTF8(aName))) {
        if (aValue) {
            return __setattr__(aCx, self, name, aValue);
        }
        return __delattr__(aCx, self, name);
    }
    return -1;
}


// Object::Type.tp_richcompare
PyObject *
Object::RichCompare(Object *self, PyObject *aOther, int op)
{
    if (Check(aOther)) {
        return __compare__(self, aOther, op);
    }
    Py_RETURN_NOTIMPLEMENTED;
}


// Object::Type.tp_iter
template<typename T>
PyObject *
Object::Iter(Object *self)
{
    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Iter");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    JS::RootedValue aResult(aCx);
    if (!T::__iter__(aCx, self, &aResult)) {
        return nullptr;
    }
    PY_ENSURE_TRUE(
        aResult.isObject(), nullptr, PyExc_TypeError,
        "%S[@@iterator]() returned a non-object value", self
    );
    JS::RootedObject aIterator(aCx, &aResult.toObject());
    return Alloc<Object::Iterator>(aCx, aIterator, nullptr);
}


// Object::Dir
PyObject *
Object::Dir(Object *self)
{
    PyObject *aNames = nullptr, *aName = nullptr;
    size_t aLength, i;

    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Dir");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    JS::Rooted<JS::IdVector> aIds(aCx, JS::IdVector(aCx));
    if (JS_Enumerate(aCx, self->mJSObject, &aIds)) {
        aLength = aIds.length();
        if ((aNames = PyList_New(aLength))) {
            for (i = 0; i < aLength; i++) {
                if ((aName = WrapId(aCx, aIds[i]))) { // +1
                    PyList_SET_ITEM(aNames, i, aName); // -1
                }
                else {
                    Py_CLEAR(aNames);
                    break;
                }
            }
        }
    }
    return aNames;
}


// Object::Type.tp_methods
PyMethodDef Object::Methods[] = {
    {"__dir__", (PyCFunction)Object::Dir, METH_NOARGS, nullptr},
    {nullptr} /* Sentinel */
};


// Object::Type.tp_finalize
void
Object::Finalize(Object *self)
{
    __finalize__(self);
}


// Object::Type
PyTypeObject Object::Type = {
    PyVarObject_HEAD_INIT(nullptr, 0)
    .tp_name = "pyxul::wrappers::pyjs::Object",
    .tp_basicsize = sizeof(Object),
    .tp_dealloc = (destructor)Object::Dealloc,
    .tp_repr = (reprfunc)Object::Repr,
    .tp_hash = (hashfunc)Object::Hash,
    .tp_str = (reprfunc)Object::Str,
    .tp_getattro = (getattrofunc)Object::GetAttrO,
    .tp_setattro = (setattrofunc)Object::SetAttrO,
    .tp_flags = (Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_FINALIZE),
    .tp_richcompare = (richcmpfunc)Object::RichCompare,
    .tp_iter = (getiterfunc)Object::Iter<Object>,
    .tp_methods = Object::Methods,
    .tp_finalize = (destructor)Object::Finalize,
};


/* -------------------------------------------------------------------------- */

// Object::New
PyObject *
Object::New(
    JSContext *aCx, const JS::HandleObject &aJSObject,
    const JS::HandleObject &aThis
)
{
    bool check = false;

    JSAutoCompartment ac(aCx, aJSObject);

    if (!JS_IsArrayObject(aCx, aJSObject, &check)) {
        return nullptr;
    }
    else if (check) {
        return Alloc<Object::Array>(aCx, aJSObject, aThis);
    }
    if (!JS::IsMapObject(aCx, aJSObject, &check)) {
        return nullptr;
    }
    else if (check) {
        return Alloc<Object::Map>(aCx, aJSObject, aThis);
    }
    if (!JS::IsSetObject(aCx, aJSObject, &check)) {
        return nullptr;
    }
    else if (check) {
        return Alloc<Object::Set>(aCx, aJSObject, aThis);
    }
    if (JS::IsCallable(aJSObject)) {
        return Alloc<Object::Callable>(aCx, aJSObject, aThis);
    }
    return Alloc<Object>(aCx, aJSObject, aThis);
}


// Object::Unwrap
JSObject *
Object::Unwrap(PyObject *aPyObject)
{
    if (Check(aPyObject)) {
        return Object::__unwrap__((Object *)aPyObject);
    }
    return nullptr;
}


/* --------------------------------------------------------------------------
   pyxul::wrappers::pyjs::Object::Iterator
   -------------------------------------------------------------------------- */

PyObject *
Object::Iterator::__next__(JSContext *aCx, Object *self)
{
    JS::RootedValue aResult(aCx);
    if (
        !JS::Call(
            aCx, self->mJSObject, "next", JS::HandleValueArray::empty(), &aResult
        )
    ) {
        return nullptr;
    }
    PY_ENSURE_TRUE(
        aResult.isObject(), nullptr, PyExc_TypeError,
        "iterator.next() returned a non-object value", self
    );
    JS::RootedObject resultObj(aCx, &aResult.toObject());
    if (!JS_GetProperty(aCx, resultObj, "done", &aResult)) {
        return nullptr;
    }
    PY_ENSURE_TRUE(
        aResult.isBoolean(), nullptr, PyExc_TypeError,
        "iterator.next() returned a non-boolean value for result.done"
    );
    if (aResult.isTrue()) { // exhausted
        return nullptr;
    }
    if (!JS_GetProperty(aCx, resultObj, "value", &aResult)) {
        return nullptr;
    }
    return Wrap(aCx, aResult);
}


/* -------------------------------------------------------------------------- */

// Object::Iterator::Type.tp_iternext
PyObject *
Object::Iterator::Next(Object *self)
{
    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Iterator::Next");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    return __next__(aCx, self);
}


// Object::Iterator::Type
PyTypeObject Object::Iterator::Type = {
    PyVarObject_HEAD_INIT(nullptr, 0)
    .tp_name = "pyxul::wrappers::pyjs::Object::Iterator",
    .tp_basicsize = sizeof(Object),
    .tp_flags = (Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_FINALIZE),
    .tp_iter = PyObject_SelfIter,
    .tp_iternext = (iternextfunc)Object::Iterator::Next,
};


/* --------------------------------------------------------------------------
   pyxul::wrappers::pyjs::Object::Array
   -------------------------------------------------------------------------- */

bool
Object::Array::__check__(PyObject *aOther)
{
    return (
        PySequence_Check(aOther) ||
        PyObject_TypeCheck(aOther, &Object::Array::Type)
    );
}


PyObject *
Object::Array::__getElement__(JSContext *aCx, Object *self, uint32_t aIdx)
{
    JS::RootedValue aResult(aCx, JS::UndefinedValue());
    if (!JS_GetElement(aCx, self->mJSObject, aIdx, &aResult)) {
        return nullptr;
    }
    return Wrap(aCx, aResult);
}


int
Object::Array::__setElement__(
    JSContext *aCx, Object *self, uint32_t aIdx, PyObject *aValue
)
{
    JS::RootedValue aJSValue(aCx, jspy::Wrap(aCx, aValue));
    if (
        aJSValue.isUndefined() ||
        !JS_SetElement(aCx, self->mJSObject, aIdx, aJSValue)
    ) {
        return -1;
    }
    return 0;
}


PyObject *
Object::Array::__getElements__(
    JSContext *aCx, Object *self, uint32_t aStart, uint32_t aCount
)
{
    JS::AutoValueArray<2> aJSArgs(aCx);
    aJSArgs[0].setNumber(aStart);
    aJSArgs[1].setNumber(aCount);
    JS::RootedValue aResult(aCx, JS::UndefinedValue());
    if (!JS::Call(aCx, self->mJSObject, "slice", aJSArgs, &aResult)) {
        return nullptr;
    }
    PY_ENSURE_TRUE(
        aResult.isObject(), nullptr, PyExc_TypeError,
        "Array.slice() returned a non-object value"
    );
    return Wrap(aCx, aResult);
}


int
Object::Array::__setElements__(
    JSContext *aCx, Object *self, uint32_t aStart, uint32_t aCount,
    PyObject *aValue
)
{
    PyObject *aArgs = nullptr, *aTmp = nullptr;
    int res = -1;

    if ((aArgs = Py_BuildValue("[kk]", aStart, aCount))) { // +1
        if ((aTmp = _PyList_Extend((PyListObject *)aArgs, aValue))) { // +1
            Py_DECREF(aTmp); // -1
            JS::AutoValueVector aJSArgs(aCx);
            if (jspy::WrapArgs(aCx, aArgs, aJSArgs)) {
                JS::RootedValue aIgnored(aCx, JS::UndefinedValue()); // ignored
                if (JS::Call(aCx, self->mJSObject, "splice", aJSArgs, &aIgnored)) {
                    res = 0;
                }
            }
        }
        Py_DECREF(aArgs); // -1
    }
    return res;
}


int
Object::Array::__delElements__(
    JSContext *aCx, Object *self, uint32_t aStart, uint32_t aCount
)
{
    JS::AutoValueArray<2> aJSArgs(aCx);
    aJSArgs[0].setNumber(aStart);
    aJSArgs[1].setNumber(aCount);
    JS::RootedValue aIgnored(aCx, JS::UndefinedValue()); // ignored
    return JS::Call(aCx, self->mJSObject, "splice", aJSArgs, &aIgnored) ? 0 : -1;
}


/* -------------------------------------------------------------------------- */

Py_ssize_t
Object::Array::__len__(JSContext *aCx, Object *self)
{
    uint32_t aLen;

    if (!JS_GetArrayLength(aCx, self->mJSObject, &aLen)) {
        return -1;
    }
    return aLen;
}


PyObject *
Object::Array::__getitem__(JSContext *aCx, Object *self, Py_ssize_t aIdx)
{
    bool found = false;

    PY_ENSURE_TRUE(
        (aIdx <= UINT32_MAX), nullptr, PyExc_OverflowError,
        "index too big for JavaScript"
    );
    if (!JS_HasElement(aCx, self->mJSObject, aIdx, &found)) {
        return nullptr;
    }
    PY_ENSURE_TRUE(found, nullptr, PyExc_IndexError, "index out of range");
    return __getElement__(aCx, self, aIdx);
}


int
Object::Array::__setitem__(
    JSContext *aCx, Object *self, Py_ssize_t aIdx, PyObject *aValue
)
{
    bool found = false;

    PY_ENSURE_TRUE(
        (aIdx <= UINT32_MAX), -1, PyExc_OverflowError,
        "assignment index too big for JavaScript"
    );
    if (!JS_HasElement(aCx, self->mJSObject, aIdx, &found)) {
        return -1;
    }
    PY_ENSURE_TRUE(found, -1, PyExc_IndexError, "assignment index out of range");
    if (aValue) {
        return __setElement__(aCx, self, aIdx, aValue);
    }
    return __delElements__(aCx, self, aIdx, 1);
}


PyObject *
Object::Array::__getslice__(
    JSContext *aCx, Object *self, PyObject *aSlice, Py_ssize_t aLen
)
{
    Py_ssize_t start, stop, slicelength;

    if (_PySlice_GetIndices(aSlice, aLen, &start, &stop, &slicelength)) {
        return nullptr;
    }
    PY_ENSURE_TRUE(
        ((start + stop) <= UINT32_MAX), nullptr,
        PyExc_IndexError, "slice out of range"
    );
    return __getElements__(aCx, self, start, stop);
}


int
Object::Array::__setslice__(
    JSContext *aCx, Object *self, PyObject *aSlice, Py_ssize_t aLen,
    PyObject *aValue
)
{
    Py_ssize_t start, stop, slicelength;

    if (_PySlice_GetIndices(aSlice, aLen, &start, &stop, &slicelength)) {
        return -1;
    }
    PY_ENSURE_TRUE(
        ((start + slicelength) <= UINT32_MAX), -1,
        PyExc_IndexError, "assignment slice out of range"
    );
    if (aValue) {
        return __setElements__(aCx, self, start, slicelength, aValue);
    }
    return __delElements__(aCx, self, start, slicelength);
}


/* -------------------------------------------------------------------------- */

PyObject *
Object::Array::__compare__(Object *self, PyObject *aOther, int op)
{
    Py_ssize_t aSelfLen, aOtherLen, i;
    PyObject *aSelfItem = nullptr, *aOtherItem = nullptr, *result = nullptr;
    int cmp;

    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Array::__compare__");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    if (
        ((aSelfLen = __len__(aCx, self)) < 0) ||
        ((aOtherLen = PySequence_Size(aOther)) < 0)
    ) {
        return nullptr;
    }
    if (aSelfLen != aOtherLen) {
        if (op == Py_EQ) {
            Py_RETURN_FALSE;
        }
        if (op == Py_NE) {
            Py_RETURN_TRUE;
        }
    }
    for (i = 0; i < aSelfLen && i < aOtherLen; i++) {
        if (
            !(aSelfItem = __getElement__(aCx, self, i)) || // +1
            !(aOtherItem = PySequence_GetItem(aOther, i)) || // +1
            ((cmp = PyObject_RichCompareBool(aSelfItem, aOtherItem, Py_EQ)) < 0)
        ) {
            Py_XDECREF(aOtherItem); // -1
            Py_XDECREF(aSelfItem); // -1
            return nullptr;
        }
        if (!cmp) { // We have an item that differs
            switch (op) {
                case Py_EQ:
                case Py_NE:
                    // shortcuts for EQ/NE
                    result = PyBool_FromLong(op == Py_NE);
                    break;
                default:
                    // Compare the items again using the proper operator
                    result = PyObject_RichCompare(aSelfItem, aOtherItem, op);
                    break;
            }
            Py_DECREF(aOtherItem); // -1
            Py_DECREF(aSelfItem); // -1
            return result;
        }
        Py_DECREF(aOtherItem); // -1
        Py_DECREF(aSelfItem); // -1
    }
    switch (op) {
        case Py_EQ:
            cmp = aSelfLen == aOtherLen;
            break;
        case Py_NE:
            cmp = aSelfLen != aOtherLen;
            break;
        case Py_LE:
            cmp = aSelfLen <= aOtherLen;
            break;
        case Py_GE:
            cmp = aSelfLen >= aOtherLen;
            break;
        case Py_LT:
            cmp = aSelfLen < aOtherLen;
            break;
        case Py_GT:
            cmp = aSelfLen > aOtherLen;
            break;
        default:
            PyErr_BadArgument();
            return nullptr;
    }
    return PyBool_FromLong(cmp);
}


/* -------------------------------------------------------------------------- */

// Object::Array::AsSequence.sq_length
Py_ssize_t
Object::Array::Length(Object *self)
{
    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Array::Length");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    return __len__(aCx, self);
}


// Object::Array::AsSequence.sq_concat
PyObject *
Object::Array::Concat(Object *self, PyObject *aOther)
{
    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Array::Concat");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    JS::AutoValueVector aJSArgs(aCx);
    if (!jspy::WrapArgs(aCx, aOther, aJSArgs)) {
        return nullptr;
    }
    JS::RootedValue aResult(aCx, JS::UndefinedValue());
    if (!JS::Call(aCx, self->mJSObject, "concat", aJSArgs, &aResult)) {
        return nullptr;
    }
    return Wrap(aCx, aResult);
}


// Object::Array::AsSequence.sq_repeat
PyObject *
Object::Array::Repeat(Object *self, Py_ssize_t aCount)
{
    Py_ssize_t aLen, i;

    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Array::Repeat");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    if (aCount < 0) {
        aCount = 0;
    }
    if (aCount) {
        if ((aLen = __len__(aCx, self)) < 0) {
            return nullptr;
        }
        PY_ENSURE_TRUE(
            ((aCount * aLen) <= UINT32_MAX), nullptr, PyExc_OverflowError,
            "the resulting length would be too big for JavaScript"
        );
    }
    JS::RootedObject aResult(aCx, JS_NewArrayObject(aCx, 0));
    if (!aResult) {
        PyErr_NoMemory();
        return nullptr;
    }
    if (aCount) {
        JS::AutoValueVector aJSArgs(aCx);
        if (!jspy::WrapArgs(aCx, self->mJSObject, aJSArgs)) {
            return nullptr;
        }
        JS::RootedValue aIgnored(aCx, JS::UndefinedValue()); // ignored
        for (i = 0; i < aCount; i++) {
            if (!JS::Call(aCx, aResult, "push", aJSArgs, &aIgnored)) {
                return nullptr;
            }
        }
    }
    return WrapObject(aCx, &aResult);
}


// Object::Array::AsSequence.sq_item
PyObject *
Object::Array::GetItem(Object *self, Py_ssize_t aIdx)
{
    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Array::GetItem");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    return __getitem__(aCx, self, aIdx);
}


// Object::Array::AsSequence.sq_ass_item
int
Object::Array::SetItem(Object *self, Py_ssize_t aIdx, PyObject *aValue)
{
    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Array::SetItem");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    return __setitem__(aCx, self, aIdx, aValue);
}


// Object::Array::AsSequence.sq_contains
int
Object::Array::Contains(Object *self, PyObject *aValue)
{
    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Array::Contains");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    JS::RootedValue aResult(aCx, JS::UndefinedValue());
    JS::RootedValue aJSValue(aCx, jspy::Wrap(aCx, aValue));
    if (
        aJSValue.isUndefined() ||
        !JS::Call(
            aCx, self->mJSObject, "includes", JS::HandleValueArray(aJSValue),
            &aResult
        )
    ) {
        return -1;
    }
    PY_ENSURE_TRUE(
        aResult.isBoolean(), -1, PyExc_TypeError,
        "Array.includes() returned a non-boolean value"
    );
    return aResult.isTrue();
}


// Object::Array::AsSequence.sq_inplace_concat
PyObject *
Object::Array::InPlaceConcat(Object *self, PyObject *aOther)
{
    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Array::InPlaceConcat");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    JS::AutoValueVector aJSArgs(aCx);
    if (!jspy::WrapArgs(aCx, aOther, aJSArgs)) {
        return nullptr;
    }
    JS::RootedValue aIgnored(aCx, JS::UndefinedValue()); // ignored
    if (!JS::Call(aCx, self->mJSObject, "push", aJSArgs, &aIgnored)) {
        return nullptr;
    }
    Py_INCREF(self);
    return self;
}


// Object::Array::AsSequence.sq_inplace_repeat
PyObject *
Object::Array::InPlaceRepeat(Object *self, Py_ssize_t aCount)
{
    Py_ssize_t aLen, i;

    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Array::InPlaceRepeat");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    if (aCount < 0) {
        aCount = 0;
    }
    if ((aLen = __len__(aCx, self)) < 0) {
        return nullptr;
    }
    if (aCount > 1) {
        PY_ENSURE_TRUE(
            ((aCount * aLen) <= UINT32_MAX), nullptr, PyExc_OverflowError,
            "the resulting length would be too big for JavaScript"
        );
        JS::AutoValueVector aJSArgs(aCx);
        if (!jspy::WrapArgs(aCx, self->mJSObject, aJSArgs)) {
            return nullptr;
        }
        JS::RootedValue aIgnored(aCx, JS::UndefinedValue()); // ignored
        for (i = 1; i < aCount; i++) {
            if (!JS::Call(aCx, self->mJSObject, "push", aJSArgs, &aIgnored)) {
                return nullptr;
            }
        }
    }
    else if (aCount == 0 && __delElements__(aCx, self, 0, aLen)) {
        return nullptr;
    }
    Py_INCREF(self);
    return self;
}


// Object::Array::Type.tp_as_sequence
PySequenceMethods Object::Array::AsSequence = {
    .sq_length = (lenfunc)Object::Array::Length,
    .sq_concat = (binaryfunc)Object::Array::Concat,
    .sq_repeat = (ssizeargfunc)Object::Array::Repeat,
    .sq_item = (ssizeargfunc)Object::Array::GetItem,
    .sq_ass_item = (ssizeobjargproc)Object::Array::SetItem,
    .sq_contains = (objobjproc)Object::Array::Contains,
    .sq_inplace_concat = (binaryfunc)Object::Array::InPlaceConcat,
    .sq_inplace_repeat = (ssizeargfunc)Object::Array::InPlaceRepeat,
};


// Object::Array::AsMapping.mp_subscript
PyObject *
Object::Array::GetSlice(Object *self, PyObject *aSlice)
{
    Py_ssize_t aLen, aIdx;

    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Array::GetSlice");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    if ((aLen = __len__(aCx, self)) < 0) {
        return nullptr;
    }
    if (PyIndex_Check(aSlice)) {
        if (_PyIndex_AsSsize_t(aSlice, aLen, &aIdx)) {
            return nullptr;
        }
        return __getitem__(aCx, self, aIdx);
    }
    else if (PySlice_Check(aSlice)) {
        return __getslice__(aCx, self, aSlice, aLen);
    }
    PyErr_SetString(PyExc_TypeError, "indices must be integers");
    return nullptr;
}


// Object::Array::AsMapping.mp_ass_subscript
int
Object::Array::SetSlice(Object *self, PyObject *aSlice, PyObject *aValue)
{
    Py_ssize_t aLen, aIdx;

    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Array::SetSlice");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    if ((aLen = __len__(aCx, self)) < 0) {
        return -1;
    }
    if (PyIndex_Check(aSlice)) {
        if (_PyIndex_AsSsize_t(aSlice, aLen, &aIdx)) {
            return -1;
        }
        return __setitem__(aCx, self, aIdx, aValue);
    }
    else if (PySlice_Check(aSlice)) {
        return __setslice__(aCx, self, aSlice, aLen, aValue);
    }
    PyErr_SetString(PyExc_TypeError, "indices must be integers");
    return -1;
}


// Object::Array::Type.tp_as_mapping
PyMappingMethods Object::Array::AsMapping = {
    .mp_subscript = (binaryfunc)Object::Array::GetSlice,
    .mp_ass_subscript = (objobjargproc)Object::Array::SetSlice,
};


// Object::Array::Type.tp_richcompare
PyObject *
Object::Array::RichCompare(Object *self, PyObject *aOther, int op)
{
    if (__check__(aOther)) {
        return __compare__(self, aOther, op);
    }
    return Object::RichCompare(self, aOther, op);
}


// Object::Array::Type
PyTypeObject Object::Array::Type = {
    PyVarObject_HEAD_INIT(nullptr, 0)
    .tp_name = "pyxul::wrappers::pyjs::Object::Array",
    .tp_basicsize = sizeof(Object),
    .tp_as_sequence = &Object::Array::AsSequence,
    .tp_as_mapping = &Object::Array::AsMapping,
    .tp_flags = (Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_FINALIZE),
    .tp_richcompare = (richcmpfunc)Object::Array::RichCompare,
};


/* --------------------------------------------------------------------------
   pyxul::wrappers::pyjs::Object::Map
   -------------------------------------------------------------------------- */

bool
Object::Map::__check__(PyObject *aOther)
{
    return (
        PyMapping_Check(aOther) ||
        PyObject_TypeCheck(aOther, &Object::Map::Type)
    );
}


/* -------------------------------------------------------------------------- */

bool
Object::Map::__iter__(JSContext* aCx, Object *self, JS::MutableHandleValue aResult)
{
    return JS::MapKeys(aCx, self->mJSObject, aResult);
}


PyObject *
Object::Map::__getitem__(JSContext *aCx, Object *self, PyObject *aKey)
{
    bool found = false;

    JS::RootedValue aJSKey(aCx, jspy::Wrap(aCx, aKey));
    if (
        aJSKey.isUndefined() ||
        !JS::MapHas(aCx, self->mJSObject, aJSKey, &found)
    ) {
        return nullptr;
    }
    PY_ENSURE_TRUE(found, nullptr, PyExc_KeyError, "%R", aKey);
    JS::RootedValue aResult(aCx, JS::UndefinedValue());
    if (!JS::MapGet(aCx, self->mJSObject, aJSKey, &aResult)) {
        return nullptr;
    }
    return Wrap(aCx, aResult);
}


int
Object::Map::__setitem__(
    JSContext *aCx, Object *self, PyObject *aKey, PyObject *aValue
)
{
    JS::RootedValue aJSKey(aCx, jspy::Wrap(aCx, aKey));
    JS::RootedValue aJSValue(aCx, jspy::Wrap(aCx, aValue));
    if (
        aJSKey.isUndefined() || aJSValue.isUndefined() ||
        !JS::MapSet(aCx, self->mJSObject, aJSKey, aJSValue)
    ) {
        return -1;
    }
    return 0;
}


int
Object::Map::__delitem__(JSContext *aCx, Object *self, PyObject *aKey)
{
    bool found = false;

    JS::RootedValue aJSKey(aCx, jspy::Wrap(aCx, aKey));
    if (
        aJSKey.isUndefined() ||
        !JS::MapDelete(aCx, self->mJSObject, aJSKey, &found)
    ) {
        return -1;
    }
    PY_ENSURE_TRUE(found, -1, PyExc_KeyError, "%R", aKey);
    return 0;
}


/* -------------------------------------------------------------------------- */

int
Object::Map::__eq__(JSContext *aCx, Object *self, PyObject *aOther)
{
    Py_ssize_t size = -1;
    if ((size = PyMapping_Size(aOther)) < 0) {
        return -1;
    }
    if (size != JS::MapSize(aCx, self->mJSObject)) {
        return 0;
    }
    JS::RootedValue aIterable(aCx, JS::ObjectValue(*self->mJSObject));
    JS::ForOfIterator aIterator(aCx);
    if (!aIterator.init(aIterable)) {
        return -1;
    }
    PyObject *aSelfEntry = nullptr, *aSelfItem = nullptr;
    PyObject *aSelfKey = nullptr, *aOtherValue = nullptr;
    JS::RootedValue aEntry(aCx);
    bool done = false;
    int eq;
    while (true) {
        if (!aIterator.next(&aEntry, &done)) {
            return -1;
        }
        if (done) {
            break;
        }
        if (!(aSelfEntry = Wrap(aCx, aEntry))) { // +1
            return -1;
        }
        if (!(aSelfItem = _PySequence_Fast(aSelfEntry, "expected a sequence", 2))) { // +1
            Py_DECREF(aSelfEntry); // -1
            return -1;
        }
        aSelfKey = PySequence_Fast_GET_ITEM(aSelfItem, 0); // borrowed key
        if (!PyMapping_HasKey(aOther, aSelfKey)) {
            Py_DECREF(aSelfItem); // -1
            Py_DECREF(aSelfEntry); // -1
            return 0;
        }
        if (!(aOtherValue = PyObject_GetItem(aOther, aSelfKey))) { // +1
            Py_DECREF(aSelfItem); // -1
            Py_DECREF(aSelfEntry); // -1
            return -1;
        }
        eq = PyObject_RichCompareBool(
            PySequence_Fast_GET_ITEM(aSelfItem, 1), // borrowed value
            aOtherValue, Py_EQ
        );
        Py_DECREF(aOtherValue); // -1
        Py_DECREF(aSelfItem); // -1
        Py_DECREF(aSelfEntry); // -1
        if (eq <= 0) {
            return eq;
        }
    }
    return 1;
}


PyObject *
Object::Map::__compare__(Object *self, PyObject *aOther, int op)
{
    int eq = -1;

    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Map::__compare__");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    if ((eq = __eq__(aCx, self, aOther)) < 0) {
        return nullptr;
    }
    switch (op) {
        case Py_EQ:
        case Py_NE:
            return PyBool_FromLong(eq == (op == Py_EQ));
        case Py_LE:
        case Py_GE:
            if (eq) {
                Py_RETURN_TRUE;
            }
            MOZ_FALLTHROUGH; // IMPORTANT: fall through
        case Py_LT:
        case Py_GT:
            PyErr_SetString(PyExc_TypeError, "unorderable types");
            return nullptr;
        default:
            PyErr_BadArgument();
            return nullptr;
    }
}


/* -------------------------------------------------------------------------- */

// Object::Map::AsSequence.sq_contains
int
Object::Map::Contains(Object *self, PyObject *aKey)
{
    bool found = false;

    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Map::Contains");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    JS::RootedValue aJSKey(aCx, jspy::Wrap(aCx, aKey));
    if (
        aJSKey.isUndefined() ||
        !JS::MapHas(aCx, self->mJSObject, aJSKey, &found)
    ) {
        return -1;
    }
    return found;
}


// Object::Map::Type.tp_as_sequence
PySequenceMethods Object::Map::AsSequence = {
    .sq_contains = (objobjproc)Object::Map::Contains,
};


// Object::Map::AsMapping.mp_length
Py_ssize_t
Object::Map::Length(Object *self)
{
    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Map::Length");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    return JS::MapSize(aCx, self->mJSObject);
}


// Object::Map::AsMapping.mp_subscript
PyObject *
Object::Map::GetItem(Object *self, PyObject *aKey)
{
    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Map::GetItem");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    return __getitem__(aCx, self, aKey);
}


// Object::Map::AsMapping.mp_ass_subscript
int
Object::Map::SetItem(Object *self, PyObject *aKey, PyObject *aValue)
{
    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Map::SetItem");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    if (aValue) {
        return __setitem__(aCx, self, aKey, aValue);
    }
    return __delitem__(aCx, self, aKey);
}


// Object::Map::Type.tp_as_mapping
PyMappingMethods Object::Map::AsMapping = {
    .mp_length = (lenfunc)Object::Map::Length,
    .mp_subscript = (binaryfunc)Object::Map::GetItem,
    .mp_ass_subscript = (objobjargproc)Object::Map::SetItem,
};


// Object::Map::Type.tp_richcompare
PyObject *
Object::Map::RichCompare(Object *self, PyObject *aOther, int op)
{
    if (__check__(aOther)) {
        return __compare__(self, aOther, op);
    }
    return Object::RichCompare(self, aOther, op);
}


// Object::Map::Type
PyTypeObject Object::Map::Type = {
    PyVarObject_HEAD_INIT(nullptr, 0)
    .tp_name = "pyxul::wrappers::pyjs::Object::Map",
    .tp_basicsize = sizeof(Object),
    .tp_as_sequence = &Object::Map::AsSequence,
    .tp_as_mapping = &Object::Map::AsMapping,
    .tp_flags = (Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_FINALIZE),
    .tp_richcompare = (richcmpfunc)Object::Map::RichCompare,
    .tp_iter = (getiterfunc)Object::Iter<Object::Map>,
};


/* --------------------------------------------------------------------------
   pyxul::wrappers::pyjs::Object::Set
   -------------------------------------------------------------------------- */

bool
Object::Set::__check__(PyObject *aOther)
{
    return (
        PyAnySet_Check(aOther) ||
        PyObject_TypeCheck(aOther, &Object::Set::Type)
    );
}


JSObject *
Object::Set::__copy__(JSContext *aCx, Object *self)
{
    JS::RootedObject aResult(aCx, JS::NewSetObject(aCx));
    if (!aResult) {
        return nullptr;
    }
    JS::RootedValue aIterable(aCx, JS::ObjectValue(*self->mJSObject));
    JS::ForOfIterator aIterator(aCx);
    if (!aIterator.init(aIterable)) {
        return nullptr;
    }
    JS::RootedValue aJSValue(aCx);
    bool done = false;
    while (true) {
        if (!aIterator.next(&aJSValue, &done)) {
            return nullptr;
        }
        if (done) {
            break;
        }
        if (!JS::SetAdd(aCx, aResult, aJSValue)) {
            return nullptr;
        }
    }
    return aResult;
}


/* -------------------------------------------------------------------------- */

bool
Object::Set::__iter__(JSContext* aCx, Object *self, JS::MutableHandleValue aResult)
{
    return JS::SetValues(aCx, self->mJSObject, aResult);
}


int
Object::Set::__sub__(
    JSContext *aCx, PyObject *aOther, JS::MutableHandleObject aResult
)
{
    PyObject *aIter = nullptr, *aValue = nullptr;
    bool ignored;

    PY_ENSURE_TRUE(
        __check__(aOther), -1, PyExc_TypeError,
        "unsupported operand type(s) for -: 'Set' and '%s'",
        Py_TYPE(aOther)->tp_name
    );
    if (!(aIter = PyObject_GetIter(aOther))) { // +1
        return -1;
    }
    JS::RootedValue aJSValue(aCx, JS::UndefinedValue());
    while ((aValue = PyIter_Next(aIter))) { // +1
        aJSValue.set(jspy::Wrap(aCx, aValue));
        if (
            aJSValue.isUndefined() ||
            !JS::SetDelete(aCx, aResult, aJSValue, &ignored)
        ) {
            Py_DECREF(aValue); // -1
            Py_DECREF(aIter); // -1
            return -1;
        }
        Py_DECREF(aValue); // -1
    }
    Py_DECREF(aIter); // -1
    if (PyErr_Occurred()) {
        return -1;
    }
    return 0;
}


int
Object::Set::__and__(
    JSContext *aCx, PyObject *aOther, JS::MutableHandleObject aResult
)
{
    PyObject *aIter = nullptr, *aValue = nullptr;
    bool found;

    PY_ENSURE_TRUE(
        __check__(aOther), -1, PyExc_TypeError,
        "unsupported operand type(s) for &: 'Set' and '%s'",
        Py_TYPE(aOther)->tp_name
    );
    JS::RootedObject result(aCx, JS::NewSetObject(aCx));
    if (!result) {
        return -1;
    }
    if (!(aIter = PyObject_GetIter(aOther))) { // +1
        return -1;
    }
    JS::RootedValue aJSValue(aCx, JS::UndefinedValue());
    while ((aValue = PyIter_Next(aIter))) { // +1
        found = false;
        aJSValue.set(jspy::Wrap(aCx, aValue));
        if (
            aJSValue.isUndefined() ||
            !JS::SetHas(aCx, aResult, aJSValue, &found)
        ) {
            Py_DECREF(aValue); // -1
            Py_DECREF(aIter); // -1
            return -1;
        }
        Py_DECREF(aValue); // -1
        if (found && !JS::SetAdd(aCx, result, aJSValue)) {
            Py_DECREF(aIter); // -1
            return -1;
        }
    }
    Py_DECREF(aIter); // -1
    if (PyErr_Occurred()) {
        return -1;
    }
    aResult.set(result);
    return 0;
}


int
Object::Set::__xor__(
    JSContext *aCx, PyObject *aOther, JS::MutableHandleObject aResult
)
{
    PyObject *aIter = nullptr, *aValue = nullptr;
    bool found;

    PY_ENSURE_TRUE(
        __check__(aOther), -1, PyExc_TypeError,
        "unsupported operand type(s) for ^: 'Set' and '%s'",
        Py_TYPE(aOther)->tp_name
    );
    if (!(aIter = PyObject_GetIter(aOther))) { // +1
        return -1;
    }
    JS::RootedValue aJSValue(aCx, JS::UndefinedValue());
    while ((aValue = PyIter_Next(aIter))) { // +1
        found = true;
        aJSValue.set(jspy::Wrap(aCx, aValue));
        if (
            aJSValue.isUndefined() ||
            !JS::SetDelete(aCx, aResult, aJSValue, &found)
        ) {
            Py_DECREF(aValue); // -1
            Py_DECREF(aIter); // -1
            return -1;
        }
        Py_DECREF(aValue); // -1
        if (!found && !JS::SetAdd(aCx, aResult, aJSValue)) {
            Py_DECREF(aIter); // -1
            return -1;
        }
    }
    Py_DECREF(aIter); // -1
    if (PyErr_Occurred()) {
        return -1;
    }
    return 0;
}


int
Object::Set::__or__(
    JSContext *aCx, PyObject *aOther, JS::MutableHandleObject aResult
)
{
    PyObject *aIter = nullptr, *aValue = nullptr;

    PY_ENSURE_TRUE(
        __check__(aOther), -1, PyExc_TypeError,
        "unsupported operand type(s) for |: 'Set' and '%s'",
        Py_TYPE(aOther)->tp_name
    );
    if (!(aIter = PyObject_GetIter(aOther))) { // +1
        return -1;
    }
    JS::RootedValue aJSValue(aCx, JS::UndefinedValue());
    while ((aValue = PyIter_Next(aIter))) { // +1
        aJSValue.set(jspy::Wrap(aCx, aValue));
        if (
            aJSValue.isUndefined() ||
            !JS::SetAdd(aCx, aResult, aJSValue)
        ) {
            Py_DECREF(aValue); // -1
            Py_DECREF(aIter); // -1
            return -1;
        }
        Py_DECREF(aValue); // -1
    }
    Py_DECREF(aIter); // -1
    if (PyErr_Occurred()) {
        return -1;
    }
    return 0;
}


/* -------------------------------------------------------------------------- */

PyObject *
Object::Set::__le__(JSContext *aCx, Object *self, PyObject *aOther)
{
    Py_ssize_t size = -1;
    PyObject *aValue = nullptr;
    int found;

    if ((size = PySequence_Size(aOther)) < 0) {
        return nullptr;
    }
    if (size < JS::SetSize(aCx, self->mJSObject)) {
        Py_RETURN_FALSE;
    }
    JS::RootedValue aIterable(aCx, JS::ObjectValue(*self->mJSObject));
    JS::ForOfIterator aIterator(aCx);
    if (!aIterator.init(aIterable)) {
        return nullptr;
    }
    JS::RootedValue aJSValue(aCx);
    bool done = false;
    while (true) {
        if (!aIterator.next(&aJSValue, &done)) {
            return nullptr;
        }
        if (done) {
            break;
        }
        if (!(aValue = Wrap(aCx, aJSValue))) { // +1
            return nullptr;
        }
        if ((found = PySequence_Contains(aOther, aValue)) < 0) {
            Py_DECREF(aValue); // -1
            return nullptr;
        }
        Py_DECREF(aValue); // -1
        if (!found) {
            Py_RETURN_FALSE;
        }
    }
    Py_RETURN_TRUE;
}


PyObject *
Object::Set::__ge__(JSContext *aCx, Object *self, PyObject *aOther)
{
    Py_ssize_t size = -1;
    PyObject *aIter = nullptr, *aValue = nullptr;
    bool found;

    if ((size = PySequence_Size(aOther)) < 0) {
        return nullptr;
    }
    if (size > JS::SetSize(aCx, self->mJSObject)) {
        Py_RETURN_FALSE;
    }
    if (!(aIter = PyObject_GetIter(aOther))) { // +1
        return nullptr;
    }
    JS::RootedValue aJSValue(aCx, JS::UndefinedValue());
    while ((aValue = PyIter_Next(aIter))) { // +1
        found = true;
        aJSValue.set(jspy::Wrap(aCx, aValue));
        if (
            aJSValue.isUndefined() ||
            !JS::SetHas(aCx, self->mJSObject, aJSValue, &found)
        ) {
            Py_DECREF(aValue); // -1
            Py_DECREF(aIter); // -1
            return nullptr;
        }
        Py_DECREF(aValue); // -1
        if (!found) {
            Py_DECREF(aIter); // -1
            Py_RETURN_FALSE;
        }
    }
    Py_DECREF(aIter); // -1
    if (PyErr_Occurred()) {
        return nullptr;
    }
    Py_RETURN_TRUE;
}


PyObject *
Object::Set::__compare__(Object *self, PyObject *aOther, int op)
{
    Py_ssize_t size = -1;
    int eq;

    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Set::__compare__");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    if ((size = PySequence_Size(aOther)) < 0) {
        return nullptr;
    }

    switch (op) {
        case Py_EQ:
            if (size != JS::SetSize(aCx, self->mJSObject)) {
                Py_RETURN_FALSE;
            }
            return __le__(aCx, self, aOther);
        case Py_NE:
            if ((eq = PyObject_RichCompareBool(self, aOther, Py_EQ)) < 0) {
                return nullptr;
            }
            return PyBool_FromLong(!eq);
        case Py_LE:
            return __le__(aCx, self, aOther);
        case Py_GE:
            return __ge__(aCx, self, aOther);
        case Py_LT:
            if (size <= JS::SetSize(aCx, self->mJSObject)) {
                Py_RETURN_FALSE;
            }
            return __le__(aCx, self, aOther);
        case Py_GT:
            if (size >= JS::SetSize(aCx, self->mJSObject)) {
                Py_RETURN_FALSE;
            }
            return __ge__(aCx, self, aOther);
        default:
            PyErr_BadArgument();
            return nullptr;
    }
}


/* -------------------------------------------------------------------------- */

// Object::Set::AsNumber.nb_subtract
PyObject *
Object::Set::Subtract(Object *self, PyObject *aOther)
{
    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Set::Subtract");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    JS::RootedObject aResult(aCx, __copy__(aCx, self));
    if (!aResult || __sub__(aCx, aOther, &aResult)) {
        return nullptr;
    }
    return WrapObject(aCx, &aResult);
}


// Object::Set::AsNumber.nb_and
PyObject *
Object::Set::And(Object *self, PyObject *aOther)
{
    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Set::And");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    JS::RootedObject aResult(aCx, __copy__(aCx, self));
    if (!aResult || __and__(aCx, aOther, &aResult)) {
        return nullptr;
    }
    return WrapObject(aCx, &aResult);
}


// Object::Set::AsNumber.nb_xor
PyObject *
Object::Set::Xor(Object *self, PyObject *aOther)
{
    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Set::Xor");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    JS::RootedObject aResult(aCx, __copy__(aCx, self));
    if (!aResult || __xor__(aCx, aOther, &aResult)) {
        return nullptr;
    }
    return WrapObject(aCx, &aResult);
}


// Object::Set::AsNumber.nb_or
PyObject *
Object::Set::Or(Object *self, PyObject *aOther)
{
    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Set::Or");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    JS::RootedObject aResult(aCx, __copy__(aCx, self));
    if (!aResult || __or__(aCx, aOther, &aResult)) {
        return nullptr;
    }
    return WrapObject(aCx, &aResult);
}


// Object::Set::Type.tp_as_number
PyNumberMethods Object::Set::AsNumber = {
    .nb_subtract = (binaryfunc)Object::Set::Subtract,
    .nb_and = (binaryfunc)Object::Set::And,
    .nb_xor = (binaryfunc)Object::Set::Xor,
    .nb_or = (binaryfunc)Object::Set::Or,
};


// Object::Set::AsSequence.sq_length
Py_ssize_t
Object::Set::Length(Object *self)
{
    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Set::Length");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    return JS::SetSize(aCx, self->mJSObject);
}


// Object::Set::AsSequence.sq_contains
int
Object::Set::Contains(Object *self, PyObject *aValue)
{
    bool found = false;

    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Set::Contains");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    JS::RootedValue aJSValue(aCx, jspy::Wrap(aCx, aValue));
    if (
        aJSValue.isUndefined() ||
        !JS::SetHas(aCx, self->mJSObject, aJSValue, &found)
    ) {
        return -1;
    }
    return found;
}


// Object::Set::Type.tp_as_sequence
PySequenceMethods Object::Set::AsSequence = {
    .sq_length = (lenfunc)Object::Set::Length,
    .sq_contains = (objobjproc)Object::Set::Contains,
};


// Object::Set::Type.tp_richcompare
PyObject *
Object::Set::RichCompare(Object *self, PyObject *aOther, int op)
{
    if (__check__(aOther)) {
        return __compare__(self, aOther, op);
    }
    return Object::RichCompare(self, aOther, op);
}


// Object::Set::IsDisjoint
PyObject *
Object::Set::IsDisjoint(Object *self, PyObject *aOther)
{
    PyObject *aIter = nullptr, *aValue = nullptr;
    bool found;

    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Set::IsDisjoint");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    if (!(aIter = PyObject_GetIter(aOther))) { // +1
        return nullptr;
    }
    JS::RootedValue aJSValue(aCx, JS::UndefinedValue());
    while ((aValue = PyIter_Next(aIter))) { // +1
        found = false;
        aJSValue.set(jspy::Wrap(aCx, aValue));
        if (
            aJSValue.isUndefined() ||
            !JS::SetHas(aCx, self->mJSObject, aJSValue, &found)
        ) {
            Py_DECREF(aValue); // -1
            Py_DECREF(aIter); // -1
            return nullptr;
        }
        Py_DECREF(aValue); // -1
        if (found) {
            Py_DECREF(aIter); // -1
            Py_RETURN_FALSE;
        }
    }
    Py_DECREF(aIter); // -1
    if (PyErr_Occurred()) {
        return nullptr;
    }
    Py_RETURN_TRUE;
}


// Object::Set::IsSubset
PyObject *
Object::Set::IsSubset(Object *self, PyObject *aOther)
{
    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Set::IsSubset");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    return __le__(aCx, self, aOther);
}


// Object::Set::IsSuperset
PyObject *
Object::Set::IsSuperset(Object *self, PyObject *aOther)
{
    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Set::IsSuperset");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    return __ge__(aCx, self, aOther);
}


// Object::Set::Difference
PyObject *
Object::Set::Difference(Object *self, PyObject *args)
{
    Py_ssize_t size = PyTuple_GET_SIZE(args), i;
    PyObject *aOther = nullptr;
    int res = -1;

    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Set::Difference");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    JS::RootedObject aResult(aCx, __copy__(aCx, self));
    if (!aResult) {
        return nullptr;
    }
    for (i = 0; i < size; i++) {
        aOther = PySet_New(PyTuple_GET_ITEM(args, i)); // +1
        res = (aOther) ? __sub__(aCx, aOther, &aResult) : -1;
        Py_XDECREF(aOther); // -1
        if (res) {
            return nullptr;
        }
    }
    return WrapObject(aCx, &aResult);
}


// Object::Set::Intersection
PyObject *
Object::Set::Intersection(Object *self, PyObject *args)
{
    Py_ssize_t size = PyTuple_GET_SIZE(args), i;
    PyObject *aOther = nullptr;
    int res = -1;

    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Set::Intersection");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    JS::RootedObject aResult(aCx, __copy__(aCx, self));
    if (!aResult) {
        return nullptr;
    }
    for (i = 0; i < size; i++) {
        aOther = PySet_New(PyTuple_GET_ITEM(args, i)); // +1
        res = (aOther) ? __and__(aCx, aOther, &aResult) : -1;
        Py_XDECREF(aOther); // -1
        if (res) {
            return nullptr;
        }
    }
    return WrapObject(aCx, &aResult);
}


// Object::Set::SymmetricDifference
PyObject *
Object::Set::SymmetricDifference(Object *self, PyObject *args)
{
    Py_ssize_t size = PyTuple_GET_SIZE(args), i;
    PyObject *aOther = nullptr;
    int res = -1;

    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Set::SymmetricDifference");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    JS::RootedObject aResult(aCx, __copy__(aCx, self));
    if (!aResult) {
        return nullptr;
    }
    for (i = 0; i < size; i++) {
        aOther = PySet_New(PyTuple_GET_ITEM(args, i)); // +1
        res = (aOther) ? __xor__(aCx, aOther, &aResult) : -1;
        Py_XDECREF(aOther); // -1
        if (res) {
            return nullptr;
        }
    }
    return WrapObject(aCx, &aResult);
}


// Object::Set::Union
PyObject *
Object::Set::Union(Object *self, PyObject *args)
{
    Py_ssize_t size = PyTuple_GET_SIZE(args), i;
    PyObject *aOther = nullptr;
    int res = -1;

    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Set::Union");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    JS::RootedObject aResult(aCx, __copy__(aCx, self));
    if (!aResult) {
        return nullptr;
    }
    for (i = 0; i < size; i++) {
        aOther = PySet_New(PyTuple_GET_ITEM(args, i)); // +1
        res = (aOther) ? __or__(aCx, aOther, &aResult) : -1;
        Py_XDECREF(aOther); // -1
        if (res) {
            return nullptr;
        }
    }
    return WrapObject(aCx, &aResult);
}


// Object::Set::Type.tp_methods
PyMethodDef Object::Set::Methods[] = {
    {"isdisjoint", (PyCFunction)Object::Set::IsDisjoint, METH_O, nullptr},
    {"issubset", (PyCFunction)Object::Set::IsSubset, METH_O, nullptr},
    {"issuperset", (PyCFunction)Object::Set::IsSuperset, METH_O, nullptr},
    {"difference", (PyCFunction)Object::Set::Difference, METH_VARARGS, nullptr},
    {"intersection", (PyCFunction)Object::Set::Intersection, METH_VARARGS, nullptr},
    {"symmetric_difference", (PyCFunction)Object::Set::SymmetricDifference, METH_VARARGS, nullptr},
    {"union", (PyCFunction)Object::Set::Union, METH_VARARGS, nullptr},
    {nullptr} /* Sentinel */
};


// Object::Set::Type
PyTypeObject Object::Set::Type = {
    PyVarObject_HEAD_INIT(nullptr, 0)
    .tp_name = "pyxul::wrappers::pyjs::Object::Set",
    .tp_basicsize = sizeof(Object),
    .tp_as_number = &Object::Set::AsNumber,
    .tp_as_sequence = &Object::Set::AsSequence,
    .tp_flags = (Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_FINALIZE),
    .tp_richcompare = (richcmpfunc)Object::Set::RichCompare,
    .tp_iter = (getiterfunc)Object::Iter<Object::Set>,
    .tp_methods = Object::Set::Methods,
};


/* --------------------------------------------------------------------------
   pyxul::wrappers::pyjs::Object::Callable
   -------------------------------------------------------------------------- */

PyObject *
Object::Callable::__construct__(
    JSContext *aCx, JS::HandleValue aFunction, const JS::HandleValueArray &aArgs
)
{
    JS::RootedObject aResult(aCx);

    if (!JS::Construct(aCx, aFunction, aArgs, &aResult)) {
        return nullptr;
    }
    PY_ENSURE_TRUE(
        aResult, nullptr, errors::JSError, "JS::Construct() returned null"
    );
    return WrapObject(aCx, &aResult);
}


PyObject *
Object::Callable::__call__(
    JSContext *aCx, JS::HandleObject aThis, JS::HandleValue aFunction,
    const JS::HandleValueArray &aArgs
)
{
    JS::RootedValue aResult(aCx, JS::UndefinedValue());

    if (!JS::Call(aCx, aThis, aFunction, aArgs, &aResult)) {
        return nullptr;
    }
    if (aResult.isUndefined()) {
        Py_RETURN_NONE;
    }
    return Wrap(aCx, aResult);
}


/* -------------------------------------------------------------------------- */

// Object::Callable::Type.tp_call
PyObject *
Object::Callable::Call(Object *self, PyObject *aArgs, PyObject *aKwargs)
{
    PY_ENSURE_TRUE(
        !aKwargs, nullptr, PyExc_TypeError, "JavaScript does not support kwargs"
    );

    dom::AutoEntryScript aes(self->mJSObject, "pyjs::Object::Callable::Call");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    JS::RootedValue aFunction(aCx, JS::ObjectValue(*self->mJSObject));
    JS::AutoValueVector aJSArgs(aCx);
    if (!jspy::WrapArgs(aCx, aArgs, aJSArgs)) {
        return nullptr;
    }
    if (
        (JS::IsConstructor(self->mJSObject) && self->mThis) ||
        JS::IsClassConstructor(self->mJSObject)
    ) {
        return __construct__(aCx, aFunction, aJSArgs);
    }
    return __call__(aCx, self->mThis, aFunction, aJSArgs);
}


// Object::Callable::Type
PyTypeObject Object::Callable::Type = {
    PyVarObject_HEAD_INIT(nullptr, 0)
    .tp_name = "pyxul::wrappers::pyjs::Object::Callable",
    .tp_basicsize = sizeof(Object),
    .tp_call = (ternaryfunc)Object::Callable::Call,
    .tp_flags = (Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_FINALIZE),
};


} // namespace pyxul::wrappers::pyjs


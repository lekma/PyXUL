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


#define JSPY_PROP_FLAGS \
    (JSPROP_READONLY | JSPROP_PERMANENT)


#define _JSPY_FN_CHECK_ARGS(name, nargs, argc, op, err) \
    do { \
        if (argc op nargs) { \
            PyErr_Format( \
                PyExc_TypeError, "%s() takes %s %u argument%s (%u given)", \
                name, err, nargs, nargs != 1 ? "s" : "", argc \
            ); \
            return xpc::ReportError(); \
        } \
    } while (0)

#define JSPY_FN_CHECK_NUM_ARGS(name, nargs, argc) \
    _JSPY_FN_CHECK_ARGS(name, nargs, argc, !=, "exactly")

#define JSPY_FN_CHECK_MIN_ARGS(name, nargs, argc) \
    _JSPY_FN_CHECK_ARGS(name, nargs, argc, <, "at least")

#define JSPY_FN_CHECK_MAX_ARGS(name, nargs, argc) \
    _JSPY_FN_CHECK_ARGS(name, nargs, argc, >, "at most")

#define JSPY_FN_CHECK_VAR_ARGS(name, minargs, maxargs, argc) \
    do { \
        JSPY_FN_CHECK_MIN_ARGS(name, minargs, argc); \
        JSPY_FN_CHECK_MAX_ARGS(name, maxargs, argc); \
    } while (0)


} // namespace anonymous


/* --------------------------------------------------------------------------
   pyxul::wrappers::jspy::Type
   -------------------------------------------------------------------------- */

PyObject *
Type::__iter__(PyObject *aObject, int type)
{
    PyObject *result = nullptr, *dict = nullptr;

    if (type) {
        if ((dict = _PyObject_Dir(aObject))) { // +1
            switch (type) {
                case IterType::Keys:
                    result = PyDict_Keys(dict); // +1
                    break;
                case IterType::Values:
                    result = PyDict_Values(dict); // +1
                    break;
                default:
                    PyErr_BadArgument();
                    break;
            }
            Py_DECREF(dict); // -1
        }
    }
    else {
        Py_INCREF(aObject); // +1
        return aObject;
    }
    return result;
}


bool
Type::__iterator__(
    JSContext *aCx, JS::CallArgs &args, PyObject *aObject, bool legacy
)
{
    bool result = false;
    PyObject *aIter = nullptr;

    if ((aIter = PyObject_GetIter(aObject))) { // +1
        JS::RootedObject aJSIter(aCx, Object::__new__(aCx, aIter)); // no caching
        if (aJSIter) {
            JS::RootedObject aProto(aCx);
            JS::RootedValue aNext(aCx);
            result = (
                JS_GetPrototype(aCx, aJSIter, &aProto) &&
                JS_GetProperty(
                    aCx, aProto, (legacy) ? "__legacy__" : "__next__", &aNext
                ) &&
                JS_SetProperty(aCx, aProto, "next", aNext)
            );
            if (result) {
                args.rval().setObject(*aJSIter);
            }
        }
        Py_DECREF(aIter); // -1
    }
    return result;
}


bool
Type::__map__(JSContext *aCx, JS::CallArgs &args, PyObject *aObject)
{
    PyObject *items = nullptr, *item = nullptr;
    Py_ssize_t size = 0, i = 0;

    if ((items = PySequence_Fast(aObject, "expected a sequence"))) { // +1
        if ((size = PySequence_Fast_GET_SIZE(items)) > 0) {
            // |function| is args[0]
            JS::RootedValue aFun(aCx, args[0]);
            // |this| is args[1] if provided
            JS::RootedValue aThis(
                aCx, (args.length() == 2) ? args[1] : JS::NullValue()
            );
            // |rval| is ignored
            JS::RootedValue aRv(aCx);
            // |args| is [value, key, object being traversed]
            JS::AutoValueArray<3> aArgs(aCx);
            aArgs[2].set(args.thisv()); // object being traversed
            for (; i < size; i++) {
                item = PySequence_Fast_GET_ITEM(items, i); // borrowed
                if (!(item = _PySequence_Fast(item, "expected a sequence", 2))) { // +1
                    break;
                }
                aArgs[0].set(Wrap(aCx, PySequence_Fast_GET_ITEM(item, 1))); // borrowed value
                aArgs[1].set(Wrap(aCx, PySequence_Fast_GET_ITEM(item, 0))); // borrowed key
                Py_DECREF(item); // -1
                if (
                    aArgs[0].isUndefined() || aArgs[1].isUndefined() ||
                    !JS::Call(aCx, aThis, aFun, aArgs, &aRv)
                ) {
                    break;
                }
            }
        }
        Py_DECREF(items); // -1
        return (i == size);
    }
    return false;
}


/* -------------------------------------------------------------------------- */

template<typename T>
JSObject *
Type::Alloc(
    JSContext *aCx, PyObject *aType, const JS::HandleObject &aProto, bool isBase
)
{
    AutoReporter ar(aCx);

    JS::RootedObject self(aCx, Object::Alloc<Type>(aCx, aProto, aType));
    if (self && isBase) {
        if (
            !JS_DefineFunctions(aCx, self, T::Functions) ||
            !JS_DefineProperties(aCx, self, T::Properties)
        ) {
            self.set(nullptr);
        }
    }
    return self;
}


bool
Type::GetProperty(
    JSContext* aCx, JS::HandleObject self, JS::HandleId aId,
    JS::MutableHandleValue aResult
)
{
    AutoGILState ags; // XXX: important

    AutoResult result = false;
    AutoReporter ar(aCx);

    result = __getattr__(aCx, self, aId, aResult);
    return bool(result);

    /*AutoResult result = false;
    AutoReporter ar(aCx);
    PyObject *err_type = nullptr, *err_inst = nullptr, *err_trbk = nullptr;

    if (
        !(result = __getattr__(aCx, self, aId, aResult)) &&
        PyErr_ExceptionMatches(PyExc_AttributeError)
    ) {
        PyErr_Fetch(&err_type, &err_inst, &err_trbk);
        JS::Rooted<JS::PropertyDescriptor> desc(aCx);
        if ((result = JS_GetPropertyDescriptorById(aCx, self, aId, &desc))) {
            JS::RootedObject aObject(aCx, desc.object());
            if (!aObject) {
                PyErr_Restore(err_type, err_inst, err_trbk);
                result = false;
            }
            else {
                Py_XDECREF(err_trbk);
                Py_XDECREF(err_inst);
                Py_XDECREF(err_type);
                result = JS_GetPropertyById(aCx, aObject, aId, aResult);
            }
        }
        else {
            PyErr_Restore(err_type, err_inst, err_trbk);
            //Py_XDECREF(err_trbk);
            //Py_XDECREF(err_inst);
            //Py_XDECREF(err_type);
        }
    }
    return bool(result);*/
}


bool
Type::SetProperty(
    JSContext* aCx, JS::HandleObject self, JS::HandleId aId,
    JS::MutableHandleValue aValue, JS::ObjectOpResult &strict
)
{
    AutoGILState ags; // XXX: important

    AutoResult result = false;
    AutoReporter ar(aCx);

    if ((result = __setattr__(aCx, self, aId, aValue))) {
        strict.succeed();
    }
    return bool(result);
}


bool
Type::DelProperty(
    JSContext* aCx, JS::HandleObject self, JS::HandleId aId,
    JS::ObjectOpResult &strict
)
{
    AutoGILState ags; // XXX: important

    AutoResult result = false;
    AutoReporter ar(aCx);

    if ((result = __delattr__(aCx, self, aId))) {
        strict.succeed();
    }
    return bool(result);
}


bool
Type::HasInstance(
    JSContext *aCx, JS::HandleObject self, JS::MutableHandleValue vp,
    bool *isInstancep
)
{
    AutoGILState ags; // XXX: important

    AutoResult result = false;
    AutoReporter ar(aCx);
    PyObject *aPyObject = nullptr;
    int isInstance = 0;

    if (vp.isObject()) {
        JS::RootedObject aJSObject(aCx, &vp.toObject());
        if (aJSObject) {
            if (jspy::Check(&aJSObject)) {
                if ((aPyObject = __unwrap__(aJSObject))) { // +1
                    isInstance = PyObject_IsInstance(aPyObject, __unwrap__(self));
                    if (isInstance >= 0) {
                        *isInstancep = isInstance;
                        result = true;
                    }
                    Py_DECREF(aPyObject); // -1
                }
            }
            else {
                result = true;
            }
        }
    }
    else {
        result = true;
    }
    return bool(result);
}


// Type::ToString
bool
Type::ToString(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    AutoGILState ags; // XXX: important

    JSPY_FN_CHECK_NUM_ARGS("toString", 0, argc);
    AutoResult result = false;
    AutoReporter ar(aCx);
    PyObject *aResult = nullptr;

    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject self(aCx, &args.thisv().toObject());
    if (self && (aResult = PyObject_Str(__unwrap__(self)))) { // +1
        args.rval().set(Wrap(aCx, aResult));
        Py_DECREF(aResult); // -1
        result = !args.rval().isUndefined();
    }
    return bool(result);
}


// Type::ToSource
bool
Type::ToSource(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    AutoGILState ags; // XXX: important

    JSPY_FN_CHECK_NUM_ARGS("toSource", 0, argc);
    AutoResult result = false;
    AutoReporter ar(aCx);
    PyObject *aResult = nullptr;

    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject self(aCx, &args.thisv().toObject());
    if (self && (aResult = PyObject_Repr(__unwrap__(self)))) { // +1
        args.rval().set(Wrap(aCx, aResult));
        Py_DECREF(aResult); // -1
        result = !args.rval().isUndefined();
    }
    return bool(result);
}


// Type::ToPrimitive
bool
Type::ToPrimitive(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    AutoGILState ags; // XXX: important

    JSPY_FN_CHECK_NUM_ARGS("[@@toPrimitive]", 1, argc);
    AutoReporter ar(aCx);
    JSType hint;

    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    if (JS::GetFirstArgumentAsTypeHint(aCx, args, &hint)) {
        JS::RootedObject self(aCx, &args.thisv().toObject());
        if (self) {
            args.rval().set(args.thisv());
            return JS::OrdinaryToPrimitive(aCx, self, hint, args.rval());
        }
    }
    return false;
}


// Type::ValueOf
bool
Type::ValueOf(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    AutoGILState ags; // XXX: important

    JSPY_FN_CHECK_NUM_ARGS("valueOf", 0, argc);
    AutoReporter ar(aCx);

    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject self(aCx, &args.thisv().toObject());
    if (self) {
        args.rval().setObject(*self);
        return true;
    }
    return false;
}


// Type::GetIterator
template<typename T>
bool
Type::GetIterator(
    JSContext *aCx, unsigned argc, JS::Value *vp, const char *name, int type
)
{
    AutoGILState ags; // XXX: important

    JSPY_FN_CHECK_NUM_ARGS(name, 0, argc);
    AutoResult result = false;
    AutoReporter ar(aCx);
    PyObject *aObject = nullptr;

    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject self(aCx, &args.thisv().toObject());
    if (self && (aObject = T::__iter__(__unwrap__(self), type))) { // +1
        result = __iterator__(aCx, args, aObject, false);
        Py_DECREF(aObject); // -1
    }
    return bool(result);
}

template<typename T, int type>
bool
Type::GetIterator(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    return GetIterator<T>(aCx, argc, vp, "[@@iterator]", type);
}

template<typename T>
bool
Type::GetKeys(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    return GetIterator<T>(aCx, argc, vp, "keys", IterType::Keys);
}

template<typename T>
bool
Type::GetValues(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    return GetIterator<T>(aCx, argc, vp, "values", IterType::Values);
}

template<typename T>
bool
Type::GetEntries(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    return GetIterator<T>(aCx, argc, vp, "entries", IterType::Entries);
}


// Type::GetLegacyIterator
template<typename T>
bool
Type::GetLegacyIterator(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    AutoGILState ags; // XXX: important

    // args[0].isTrue(): keys, for (variable in object)
    // args[0].isFalse(): values, for each (variable in object)

    JSPY_FN_CHECK_NUM_ARGS("__iterator__", 1, argc);
    AutoResult result = false;
    AutoReporter ar(aCx);
    PyObject *aObject = nullptr;
    int type;

    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    type = args[0].isTrue() ? IterType::Keys : IterType::Values;
    JS::RootedObject self(aCx, &args.thisv().toObject());
    if (self && (aObject = T::__iter__(__unwrap__(self), type))) { // +1
        result = __iterator__(aCx, args, aObject, true);
        Py_DECREF(aObject); // -1
    }
    return bool(result);
}


// Type::ForEach
template<typename T>
bool
Type::ForEach(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    AutoGILState ags; // XXX: important

    JSPY_FN_CHECK_VAR_ARGS("forEach", 1, 2, argc);
    AutoResult result = false;
    AutoReporter ar(aCx);
    PyObject *aObject = nullptr;

    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    args.rval().setUndefined();
    JS::RootedObject self(aCx, &args.thisv().toObject());
    if (self && (aObject = T::__iter__(__unwrap__(self), IterType::Entries))) { // +1
        result = __map__(aCx, args, aObject);
        Py_DECREF(aObject); // -1
    }
    return bool(result);
}


// Type::ClassOps
const js::ClassOps Type::ClassOps = {
    JSPY_TYPE_CLASS_OPS
};


// Type::ObjectOps
const js::ObjectOps Type::ObjectOps = {
    JSPY_BASE_OBJECT_OPS
};


/* public ------------------------------------------------------------------- */

// Type::Class
const js::Class Type::Class = {
    JSPY_CLASS("pyxul::wrappers::jspy::Type")
};


// Type::Functions
const JSFunctionSpec Type::Functions[] = {
    JS_FN("toString", Type::ToString, 0, JSPY_PROP_FLAGS),
    JS_FN("toSource", Type::ToSource, 0, JSPY_PROP_FLAGS),
    JS_SYM_FN(toPrimitive, Type::ToPrimitive, 1, JSPY_PROP_FLAGS),
    JS_FN("valueOf", Type::ValueOf, 0, JSPY_PROP_FLAGS),
    JS_SYM_FN(iterator, (Type::GetIterator<Type, 0>), 0, JSPY_PROP_FLAGS),
    JS_FN("__iterator__", Type::GetLegacyIterator<Type>, 1, JSPY_PROP_FLAGS),
    JS_FS_END
};


// Type::Properties
const JSPropertySpec Type::Properties[] = {
    JS_PS_END
};


// Type::ProtoBase
JS::PersistentRootedObject Type::ProtoBase;


JSObject *
Type::New(JSContext* aCx, PyObject *aType, const JS::HandleObject &aProto)
{
    PyTypeObject *type = (PyTypeObject *)aType;

    if (type == &PyType_Type) {
        return ProtoBase ? ProtoBase : Alloc<Type>(aCx, aType, nullptr, true);
    }
    if (_PyType_IsDict(type)) {
        return Alloc<Type::Mapping>(aCx, aType, aProto, true);
    }
    if (_PyType_IsTuple(type) || _PyType_IsList(type)) {
        return Alloc<Type::Sequence>(aCx, aType, aProto, true);
    }
    if (_PyType_IsAnySet(type)) {
        return Alloc<Type::Set>(aCx, aType, aProto, true);
    }
    if (_PyType_IsIterator(type)) {
        return Alloc<Type::Iterator>(aCx, aType, aProto, true);
    }
    if (_PyType_IsMapping(type)) {
        return Alloc<Type::Mapping>(aCx, aType, aProto, true);
    }
    if (_PyType_IsSequence(type)) {
        return Alloc<Type::Sequence>(aCx, aType, aProto, true);
    }
    // just a Type
    return Alloc<Type>(aCx, aType, aProto, false);
}


JSObject *
Type::GetProto(JSContext* aCx, PyTypeObject *aType)
{
    if (aType == &PyType_Type) {
        return ProtoBase;
    }
    return WrapObject(aCx, (PyObject *)aType);
}


bool
Type::Check(const JS::HandleObject &aJSObject)
{
    dom::AutoEntryScript aes(aJSObject, "jspy::Type::Check");
    JSContext *aCx = aes.cx();
    AutoReporter ar(aCx);

    JS::RootedObject aBase(aCx);
    JS::RootedObject aProto(aCx, aJSObject);
    while (aProto) {
        if (aProto == ProtoBase) {
            return true;
        }
        aBase = aProto;
        if (!JS_GetPrototype(aCx, aBase, &aProto)) {
            return false;
        }
        if (aProto) {
            aProto = xpc::Unwrap(aProto);
        }
    }
    return false;
}


/* --------------------------------------------------------------------------
   pyxul::wrappers::jspy::Type::Iterator
   -------------------------------------------------------------------------- */

bool
Type::Iterator::__wrap__(JSContext *aCx, JS::CallArgs &args, PyObject *aObject)
{
    JS::RootedValue value(aCx, JS::UndefinedValue());
    JS::RootedValue done(aCx, JS::TrueValue());
    if (aObject) {
        value.set(Wrap(aCx, aObject));
        if (value.isUndefined()) {
            return false;
        }
        done.setBoolean(false);
    }
    JS::RootedObject aResult(aCx, JS_NewPlainObject(aCx));
    if (
        aResult &&
        JS_SetProperty(aCx, aResult, "value", value) &&
        JS_SetProperty(aCx, aResult, "done", done)
    ) {
        args.rval().setObject(*aResult);
        return true;
    }
    return false;
}


// Type::Iterator::GetIterator
bool
Type::Iterator::GetIterator(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    AutoGILState ags; // XXX: important

    JSPY_FN_CHECK_NUM_ARGS("[@@iterator]", 0, argc);
    AutoReporter ar(aCx);
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    args.rval().set(args.thisv());
    return true;
}


// Type::Iterator::__next__
bool
Type::Iterator::__next__(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    AutoGILState ags; // XXX: important

    JSPY_FN_CHECK_NUM_ARGS("next", 0, argc);
    AutoResult result = false;
    AutoReporter ar(aCx);
    PyObject *aObject = nullptr;

    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject self(aCx, &args.thisv().toObject());
    if (self) {
        aObject = PyIter_Next(__unwrap__(self)); // +1
        if (!PyErr_Occurred()) {
            result = __wrap__(aCx, args, aObject);
        }
        Py_XDECREF(aObject); // -1
    }
    return bool(result);
}


// Type::Iterator::__legacy__
bool
Type::Iterator::__legacy__(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    AutoGILState ags; // XXX: important

    JSPY_FN_CHECK_NUM_ARGS("next", 0, argc);
    AutoReporter ar(aCx);
    PyObject *aResult = nullptr;

    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject self(aCx, &args.thisv().toObject());
    if (self) {
        if ((aResult = PyIter_Next(__unwrap__(self)))) { // +1
            args.rval().set(Wrap(aCx, aResult));
            Py_DECREF(aResult); // -1
            return (args.rval().isUndefined()) ? xpc::ReportError() : true;
        }
        else if (!PyErr_Occurred()) {
            return JS_ThrowStopIteration(aCx);
        }
    }
    return xpc::ReportError();
}


// Type::Iterator::Next
bool
Type::Iterator::Next(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    AutoGILState ags; // XXX: important

    PyErr_SetNone(PyExc_NotImplementedError);
    return xpc::ReportError();
}


/* public ------------------------------------------------------------------- */

// Type::Iterator::Functions
const JSFunctionSpec Type::Iterator::Functions[] = {
    JS_SYM_FN(iterator, Type::Iterator::GetIterator, 0, JSPY_PROP_FLAGS),
    JS_FN("__next__", Type::Iterator::__next__, 0, JSPY_PROP_FLAGS),
    JS_FN("__legacy__", Type::Iterator::__legacy__, 0, JSPY_PROP_FLAGS),
    JS_FN("next", Type::Iterator::Next, 0, 0),
    JS_FS_END
};


// Type::Iterator::Properties
const JSPropertySpec Type::Iterator::Properties[] = {
    JS_PS_END
};


/* --------------------------------------------------------------------------
   pyxul::wrappers::jspy::Type::Mapping
   -------------------------------------------------------------------------- */

PyObject *
Type::Mapping::__iter__(PyObject *aObject, int type)
{
    switch (type) {
        case IterType::Keys:
            return PyMapping_Keys(aObject); // +1
        case IterType::Values:
            return PyMapping_Values(aObject); // +1
        case IterType::Entries:
            return PyMapping_Items(aObject); // +1
        default:
            PyErr_BadArgument();
            return nullptr;
    }
}


/* -------------------------------------------------------------------------- */

// Type::Mapping::Get
bool
Type::Mapping::Get(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    AutoGILState ags; // XXX: important

    JSPY_FN_CHECK_NUM_ARGS("get", 1, argc);
    AutoResult result = false;
    AutoReporter ar(aCx);
    PyObject *aKey = nullptr, *aResult = nullptr;

    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject self(aCx, &args.thisv().toObject());
    if (self && (aKey = pyjs::Wrap(aCx, args[0]))) { // +1
        if ((aResult = PyObject_GetItem(__unwrap__(self), aKey))) { // +1
            args.rval().set(Wrap(aCx, aResult));
            Py_DECREF(aResult); // -1
            result = !args.rval().isUndefined();
        }
        else {
            args.rval().setUndefined();
        }
        Py_DECREF(aKey); // -1
    }
    return bool(result);
}


// Type::Mapping::Set
bool
Type::Mapping::Set(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    AutoGILState ags; // XXX: important

    JSPY_FN_CHECK_NUM_ARGS("set", 2, argc);
    AutoResult result = false;
    AutoReporter ar(aCx);
    PyObject *aKey = nullptr, *aValue = nullptr;

    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject self(aCx, &args.thisv().toObject());
    if (self) {
        if ((aKey = pyjs::Wrap(aCx, args[0]))) { // +1
            if ((aValue = pyjs::Wrap(aCx, args[1]))) { // +1
                if (PyObject_SetItem(__unwrap__(self), aKey, aValue)) {
                    args.rval().setUndefined();
                }
                else {
                    args.rval().set(args.thisv());
                    result = true;
                }
                Py_DECREF(aValue); // -1
            }
            Py_DECREF(aKey); // -1
        }
    }
    return bool(result);
}


// Type::Mapping::Delete
bool
Type::Mapping::Delete(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    AutoGILState ags; // XXX: important

    JSPY_FN_CHECK_NUM_ARGS("delete", 1, argc);
    AutoResult result = false;
    AutoReporter ar(aCx);
    PyObject *aKey = nullptr, *aObject = nullptr;

    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject self(aCx, &args.thisv().toObject());
    if (self && (aKey = pyjs::Wrap(aCx, args[0]))) { // +1
        aObject = __unwrap__(self); // borrowed
        if (PyMapping_HasKey(aObject, aKey)) {
            if (PyObject_DelItem(aObject, aKey)) {
                args.rval().setUndefined();
            }
            else {
                args.rval().setBoolean(true);
                result = true;
            }
        }
        else {
            args.rval().setBoolean(false);
            result = true;
        }
        Py_DECREF(aKey); // -1
    }
    return bool(result);
}


// Type::Mapping::Has
bool
Type::Mapping::Has(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    AutoGILState ags; // XXX: important

    JSPY_FN_CHECK_NUM_ARGS("has", 1, argc);
    AutoResult result = false;
    AutoReporter ar(aCx);
    PyObject *aKey = nullptr;

    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject self(aCx, &args.thisv().toObject());
    if (self && (aKey = pyjs::Wrap(aCx, args[0]))) { // +1
        args.rval().setBoolean(PyMapping_HasKey(__unwrap__(self), aKey));
        result = true;
        Py_DECREF(aKey); // -1
    }
    return bool(result);
}


// Type::Mapping::Clear
bool
Type::Mapping::Clear(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    AutoGILState ags; // XXX: important

    JSPY_FN_CHECK_NUM_ARGS("clear", 0, argc);
    AutoResult result = false;
    AutoReporter ar(aCx);

    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject self(aCx, &args.thisv().toObject());
    if (self) {
        result = !_PyMapping_Clear(__unwrap__(self));
        args.rval().setUndefined();
    }
    return bool(result);
}


// Type::Mapping::Size
bool
Type::Mapping::Size(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    AutoGILState ags; // XXX: important

    AutoReporter ar(aCx);
    Py_ssize_t pysize;
    uint32_t size;

    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject self(aCx, &args.thisv().toObject());
    if (
        !self || ((pysize = PyMapping_Size(__unwrap__(self))) < 0) ||
        (
            ((size = _PySsize_tAsUint32_t(pysize)) == ((uint32_t)-1)) &&
            PyErr_Occurred()
        )
    ) {
        return false;
    }
    args.rval().setNumber(size);
    return true;
}


/* public ------------------------------------------------------------------- */

// Type::Mapping::Functions
const JSFunctionSpec Type::Mapping::Functions[] = {
    JS_SYM_FN(iterator, (Type::GetIterator<Type::Mapping, IterType::Entries>), 0, JSPY_PROP_FLAGS),
    JS_FN("keys", Type::GetKeys<Type::Mapping>, 0, JSPY_PROP_FLAGS),
    JS_FN("values", Type::GetValues<Type::Mapping>, 0, JSPY_PROP_FLAGS),
    JS_FN("entries", Type::GetEntries<Type::Mapping>, 0, JSPY_PROP_FLAGS),
    JS_FN("__iterator__", Type::GetLegacyIterator<Type::Mapping>, 1, JSPY_PROP_FLAGS),
    JS_FN("forEach", Type::ForEach<Type::Mapping>, 2, JSPY_PROP_FLAGS),
    JS_FN("get", Type::Mapping::Get, 1, JSPY_PROP_FLAGS),
    JS_FN("set", Type::Mapping::Set, 2, JSPY_PROP_FLAGS),
    JS_FN("delete", Type::Mapping::Delete, 1, JSPY_PROP_FLAGS),
    JS_FN("has", Type::Mapping::Has, 1, JSPY_PROP_FLAGS),
    JS_FN("clear", Type::Mapping::Clear, 0, JSPY_PROP_FLAGS),
    JS_FS_END
};


// Type::Mapping::Properties
const JSPropertySpec Type::Mapping::Properties[] = {
    JS_PSG("size", Type::Mapping::Size, JSPROP_PERMANENT),
    JS_PS_END
};


/* --------------------------------------------------------------------------
   pyxul::wrappers::jspy::Type::Sequence
   -------------------------------------------------------------------------- */

PyObject *
Type::Sequence::__iter__(PyObject *aObject, int type)
{
    static PyObject *Range = (PyObject *)&PyRange_Type;
    static PyObject *Enum = (PyObject *)&PyEnum_Type;
    Py_ssize_t len;

    if ((len = PySequence_Length(aObject)) >= 0) {
        switch (type) {
            case IterType::Keys:
                return PyObject_CallFunction(Range, "n", len); // +1
            case IterType::Values:
                return PySequence_Fast(aObject, "not a sequence!?"); // +1
            case IterType::Entries:
                return PyObject_CallFunctionObjArgs(Enum, aObject, nullptr); // +1
            default:
                PyErr_BadArgument();
                return nullptr;
        }
    }
    return nullptr;
}


/* -------------------------------------------------------------------------- */

// Type::Sequence::Length
bool
Type::Sequence::Length(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    AutoGILState ags; // XXX: important

    AutoReporter ar(aCx);
    Py_ssize_t pylength;
    uint32_t length;

    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject self(aCx, &args.thisv().toObject());
    if (
        !self || ((pylength = PySequence_Length(__unwrap__(self))) < 0) ||
        (
            ((length = _PySsize_tAsUint32_t(pylength)) == ((uint32_t)-1)) &&
            PyErr_Occurred()
        )
    ) {
        return false;
    }
    args.rval().setNumber(length);
    return true;
}


/* public ------------------------------------------------------------------- */

// Type::Sequence::Functions
const JSFunctionSpec Type::Sequence::Functions[] = {
    JS_SYM_FN(iterator, (Type::GetIterator<Type::Sequence, IterType::Values>), 0, JSPY_PROP_FLAGS),
    JS_FN("keys", Type::GetKeys<Type::Sequence>, 0, JSPY_PROP_FLAGS),
    JS_FN("values", Type::GetValues<Type::Sequence>, 0, JSPY_PROP_FLAGS),
    JS_FN("entries", Type::GetEntries<Type::Sequence>, 0, JSPY_PROP_FLAGS),
    JS_FN("__iterator__", Type::GetLegacyIterator<Type::Sequence>, 1, JSPY_PROP_FLAGS),
    JS_FN("forEach", Type::ForEach<Type::Sequence>, 2, JSPY_PROP_FLAGS),
    JS_FS_END
};


// Type::Sequence::Properties
const JSPropertySpec Type::Sequence::Properties[] = {
    JS_PSG("length", Type::Sequence::Length, JSPROP_PERMANENT),
    JS_PS_END
};


/* --------------------------------------------------------------------------
   pyxul::wrappers::jspy::Type::Set
   -------------------------------------------------------------------------- */

PyObject *
Type::Set::__iter__(PyObject *aObject, int type)
{
    static PyObject *Zip = (PyObject *)&PyZip_Type;

    switch (type) {
        case IterType::Keys:
        case IterType::Values:
            return PySequence_Fast(aObject, "not a set!?"); // +1
        case IterType::Entries:
            return PyObject_CallFunctionObjArgs(Zip, aObject, aObject, nullptr); // +1
        default:
            PyErr_BadArgument();
            return nullptr;
    }
}


/* -------------------------------------------------------------------------- */

// Type::Set::Add
bool
Type::Set::Add(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    AutoGILState ags; // XXX: important

    JSPY_FN_CHECK_NUM_ARGS("add", 1, argc);
    AutoResult result = false;
    AutoReporter ar(aCx);
    PyObject *aKey = nullptr;

    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject self(aCx, &args.thisv().toObject());
    if (self && (aKey = pyjs::Wrap(aCx, args[0]))) { // +1
        if (PySet_Add(__unwrap__(self), aKey)) {
            args.rval().setUndefined();
        }
        else {
            args.rval().set(args.thisv());
            result = true;
        }
        Py_DECREF(aKey); // -1
    }
    return bool(result);
}


// Type::Set::Delete
bool
Type::Set::Delete(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    AutoGILState ags; // XXX: important

    JSPY_FN_CHECK_NUM_ARGS("delete", 1, argc);
    AutoResult result = false;
    AutoReporter ar(aCx);
    PyObject *aKey = nullptr;
    int found = -1;

    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject self(aCx, &args.thisv().toObject());
    if (self && (aKey = pyjs::Wrap(aCx, args[0]))) { // +1
        if ((found = PySet_Discard(__unwrap__(self), aKey)) < 0) {
            args.rval().setUndefined();
        }
        else {
            args.rval().setBoolean(found);
            result = true;
        }
        Py_DECREF(aKey); // -1
    }
    return bool(result);
}


// Type::Set::Has
bool
Type::Set::Has(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    AutoGILState ags; // XXX: important

    JSPY_FN_CHECK_NUM_ARGS("has", 1, argc);
    AutoResult result = false;
    AutoReporter ar(aCx);
    PyObject *aKey = nullptr;
    int found = -1;

    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject self(aCx, &args.thisv().toObject());
    if (self && (aKey = pyjs::Wrap(aCx, args[0]))) { // +1
        if ((found = PySet_Contains(__unwrap__(self), aKey)) < 0) {
            args.rval().setUndefined();
        }
        else {
            args.rval().setBoolean(found);
            result = true;
        }
        Py_DECREF(aKey); // -1
    }
    return bool(result);
}


// Type::Set::Clear
bool
Type::Set::Clear(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    AutoGILState ags; // XXX: important

    JSPY_FN_CHECK_NUM_ARGS("clear", 0, argc);
    AutoResult result = false;
    AutoReporter ar(aCx);

    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject self(aCx, &args.thisv().toObject());
    if (self) {
        result = !PySet_Clear(__unwrap__(self));
        args.rval().setUndefined();
    }
    return bool(result);
}


// Type::Set::Size
bool
Type::Set::Size(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    AutoGILState ags; // XXX: important

    AutoReporter ar(aCx);
    Py_ssize_t pysize;
    uint32_t size;

    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject self(aCx, &args.thisv().toObject());
    if (
        !self || ((pysize = PySet_Size(__unwrap__(self))) < 0) ||
        (
            ((size = _PySsize_tAsUint32_t(pysize)) == ((uint32_t)-1)) &&
            PyErr_Occurred()
        )
    ) {
        return false;
    }
    args.rval().setNumber(size);
    return true;
}


/* public ------------------------------------------------------------------- */

// Type::Set::Functions
const JSFunctionSpec Type::Set::Functions[] = {
    JS_SYM_FN(iterator, (Type::GetIterator<Type::Set, IterType::Values>), 0, JSPY_PROP_FLAGS),
    JS_FN("keys", Type::GetKeys<Type::Set>, 0, JSPY_PROP_FLAGS),
    JS_FN("values", Type::GetValues<Type::Set>, 0, JSPY_PROP_FLAGS),
    JS_FN("entries", Type::GetEntries<Type::Set>, 0, JSPY_PROP_FLAGS),
    JS_FN("__iterator__", Type::GetLegacyIterator<Type::Set>, 1, JSPY_PROP_FLAGS),
    JS_FN("forEach", Type::ForEach<Type::Set>, 2, JSPY_PROP_FLAGS),
    JS_FN("add", Type::Set::Add, 1, JSPY_PROP_FLAGS),
    JS_FN("delete", Type::Set::Delete, 1, JSPY_PROP_FLAGS),
    JS_FN("has", Type::Set::Has, 1, JSPY_PROP_FLAGS),
    JS_FN("clear", Type::Set::Clear, 0, JSPY_PROP_FLAGS),
    JS_FS_END
};


// Type::Set::Properties
const JSPropertySpec Type::Set::Properties[] = {
    JS_PSG("size", Type::Set::Size, JSPROP_PERMANENT),
    JS_PS_END
};


} // namespace pyxul::wrappers::jspy


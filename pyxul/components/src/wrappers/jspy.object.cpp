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


// nameToId
static bool
__name2id__(JSContext *aCx, PyObject *aName, JS::MutableHandleId aId)
{
    JS::RootedValue aJSName(aCx, WrapUnicode(aCx, aName));
    return aJSName.isUndefined() ? false : JS_ValueToId(aCx, aJSName, aId);
}


// mergeNamesToIds
static bool
__merge__(JSContext *aCx, JS::AutoIdVector &aIds, PyObject *aNames)
{
    JS::RootedId aId(aCx);
    Py_ssize_t size = PyList_GET_SIZE(aNames), i;

    for (i = 0; i < size; i++) {
        if (
            !__name2id__(aCx, PyList_GET_ITEM(aNames, i), &aId) ||
            !aIds.append(aId)
        ) {
            return false;
        }
    }
    return true;
}


} // namespace anonymous


/* ObjectCache -------------------------------------------------------------- */

void
ObjectCache::finalizeObject(PyObject *aKey, JSObject *aData)
{

    if (aKey && aData) {
        Object::__finalize__(aData); // XXX: this is questionnable but kinda necessary
    }
}


/* --------------------------------------------------------------------------
   pyxul::wrappers::jspy::Object
   -------------------------------------------------------------------------- */

JSObject *
Object::__new__(JSContext* aCx, PyObject *aObject)
{
    JS::RootedObject aProto(aCx, Type::GetProto(aCx, Py_TYPE(aObject)));
    if (!aProto) {
        return nullptr;
    }
    JSAutoCompartment ac(aCx, aProto);
    if (PyType_Check(aObject)) {
        return Type::New(aCx, aObject, aProto);
    }
    if (
        PyTuple_Check(aObject) || PyList_Check(aObject) ||
        (!PyMapping_Check(aObject) && PySequence_Check(aObject))
    ) {
        return Alloc<Object::Sequence>(aCx, aProto, aObject);
    }
    if (PyCallable_Check(aObject)) {
        return Alloc<Object::Callable>(aCx, aProto, aObject);
    }
    return Alloc<Object>(aCx, aProto, aObject);
}


void
Object::__finalize__(JSObject *self)
{
    AutoGILState ags; // XXX: important

    PyObject *aObject = nullptr;

    if ((aObject = (PyObject *)JS_GetPrivate(self))) {
        Py_DECREF(aObject);
        JS_SetPrivate(self, nullptr);
    }
}


PyObject *
Object::__unwrap__(JS::HandleObject self)
{
    return (PyObject *)JS_GetPrivate(self); // borrowed
}


bool
Object::__getattr__(
    JSContext *aCx, JS::HandleObject self, JS::HandleId aId,
    JS::MutableHandleValue aResult
)
{
    bool result = false;
    PyObject *aName = nullptr, *aPyResult = nullptr;

    if ((aName = pyjs::WrapId(aCx, aId))) { // +1
        if ((aPyResult = PyObject_GetAttr(__unwrap__(self), aName))) { // +1
            aResult.set(Wrap(aCx, aPyResult));
            Py_DECREF(aPyResult); // -1
            result = !aResult.isUndefined();
        }
        Py_DECREF(aName); // -1
    }
    return result;
}


bool
Object::__setattr__(
    JSContext* aCx, JS::HandleObject self, JS::HandleId aId,
    JS::HandleValue aValue
)
{
    bool result = false;
    PyObject *aName = nullptr, *aPyValue = nullptr;

    if ((aName = pyjs::WrapId(aCx, aId))) { // +1
        if ((aPyValue = pyjs::Wrap(aCx, aValue))) { // +1
            result = !PyObject_SetAttr(__unwrap__(self), aName, aPyValue);
            Py_DECREF(aPyValue); // -1
        }
        Py_DECREF(aName); // -1
    }
    return result;
}


bool
Object::__delattr__(JSContext *aCx, JS::HandleObject self, JS::HandleId aId)
{
    bool result = false;
    PyObject *aName = nullptr;

    if ((aName = pyjs::WrapId(aCx, aId))) { // +1
        result = !PyObject_DelAttr(__unwrap__(self), aName);
        Py_DECREF(aName); // -1
    }
    return result;
}


/* -------------------------------------------------------------------------- */

template<typename T>
JSObject *
Object::Alloc(
    JSContext *aCx, const JS::HandleObject &aProto, PyObject *aPyObject
)
{
    AutoReporter ar(aCx);

    const JSClass *aJSClass = js::Jsvalify(&T::Class);
    JSObject *self = nullptr;

    /*if (aProto) {
        self = JS_NewObjectWithGivenProto(aCx, aJSClass, aProto);
    }
    else {
        self = JS_NewObject(aCx, aJSClass);
    }
    if (self) {*/
    if ((self = JS_NewObjectWithGivenProto(aCx, aJSClass, aProto))) {
        Py_INCREF(aPyObject);
        JS_SetPrivate(self, aPyObject);
        JS::ExposeObjectToActiveJS(self);
    }
    return self;
}

template
JSObject *
Object::Alloc<Type>(
    JSContext *aCx, const JS::HandleObject &aProto, PyObject *aPyObject
);


bool
Object::Resolve(
    JSContext* aCx, JS::HandleObject self, JS::HandleId aId, bool *resolvedp
)
{
    AutoGILState ags; // XXX: important

    AutoReporter ar(aCx);
    PyObject *aName = nullptr;
    bool result = false;

    if ((aName = pyjs::WrapId(aCx, aId))) { // +1
        result = true;
        if (PyObject_HasAttr(__unwrap__(self), aName)) {
            *resolvedp = true;
            /*if (
                !JS_DefinePropertyById(
                    aCx, self, aId, JS::UndefinedHandleValue,
                    JSPROP_ENUMERATE | JSPROP_SHARED | JSPROP_RESOLVING
                )
            ) {
                result = false;
            }*/
        }
        Py_DECREF(aName); // -1
    }
    return result;
}


void
Object::Finalize(js::FreeOp *fop, JSObject *self)
{
    PyObject *aObject = nullptr;

    if ((aObject = (PyObject *)JS_GetPrivate(self))) {
        Objects.remove(aObject);
        __finalize__(self);
    }
}


bool
Object::Call(JSContext *aCx, unsigned argc, JS::Value *vp)
{
    AutoGILState ags; // XXX: important

    AutoResult result = false;
    AutoReporter ar(aCx);
    PyObject *aArgs = nullptr, *aResult = nullptr;

    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    if ((aArgs = pyjs::WrapArgs(aCx, args))) { // +1
        JS::RootedObject self(aCx, &args.callee());
        if (
            self &&
            (aResult = PyObject_CallObject(__unwrap__(self), aArgs)) // +1
        ) {
            args.rval().set(Wrap(aCx, aResult));
            Py_DECREF(aResult); // -1
            result = !args.rval().isUndefined();
        }
        Py_DECREF(aArgs); // -1
    }
    return bool(result);
}


void
Object::Moved(JSObject *self, const JSObject *old)
{
    AutoGILState ags; // XXX: important???

    JS::AutoSuppressGCAnalysis nogc;

    PyObject *aObject = nullptr;

    if ((aObject = (PyObject *)JS_GetPrivate(self))) {
        Objects.update(aObject, self);
    }
}


bool
Object::GetPropertyOp(
    JSContext* aCx, JS::HandleObject self, JS::HandleValue rec, JS::HandleId aId,
    JS::MutableHandleValue aResult
)
{
    AutoGILState ags; // XXX: important

    AutoResult result = false;
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
                result = JS_ForwardGetPropertyTo(
                    aCx, aObject, aId, rec, aResult
                );
            }
        }
        else {
            PyErr_Restore(err_type, err_inst, err_trbk);
            //Py_XDECREF(err_trbk);
            //Py_XDECREF(err_inst);
            //Py_XDECREF(err_type);
        }
    }
    return bool(result);
}


bool
Object::SetPropertyOp(
    JSContext* aCx, JS::HandleObject self, JS::HandleId aId,
    JS::HandleValue aValue, JS::HandleValue rec, JS::ObjectOpResult &strict
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
Object::DeletePropertyOp(
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
Object::Enumerate(
    JSContext *aCx, JS::HandleObject self, JS::AutoIdVector &aIds, bool unused
)
{
    AutoGILState ags; // XXX: important

    AutoResult result = false;
    AutoReporter ar(aCx);
    PyObject *aVars = nullptr, *aNames = nullptr;

    if ((aVars = _PyObject_Vars(__unwrap__(self)))) { // +1
        if ((aNames = PyDict_Keys(aVars))) { // +1
            result = __merge__(aCx, aIds, aNames);
            Py_DECREF(aNames); // -1
        }
        Py_DECREF(aVars); // -1
    }
    return bool(result);
}


// Object::ClassOps
const js::ClassOps Object::ClassOps = {
    JSPY_BASE_CLASS_OPS
};


// Object::ClassExtension
const js::ClassExtension Object::ClassExtension = {
    .objectMovedOp = Moved,
};


// Object::ObjectOps
const js::ObjectOps Object::ObjectOps = {
    JSPY_OBJECT_OBJECT_OPS
};


/* public ------------------------------------------------------------------- */

// Object::Class
const js::Class Object::Class = {
    JSPY_CLASS("pyxul::wrappers::jspy::Object")
};


// Object::Objects
ObjectCache Object::Objects;


JSObject *
Object::New(JSContext* aCx, PyObject *aObject)
{
    JSObject *aResult = nullptr;

    if ((aResult = Objects.get(aObject))) {
        JS::ExposeObjectToActiveJS(aResult);
    }
    else if ((aResult = __new__(aCx, aObject))) {
        Objects.put(aObject, aResult);
    }
    return aResult;
}


PyObject *
Object::Unwrap(const JS::HandleObject &aJSObject)
{
    PyObject *aResult = nullptr;

    if (
        Type::Check(aJSObject) &&
        (aResult = __unwrap__(aJSObject)) // borrowed
    ) {
        Py_INCREF(aResult);
    }
    return aResult;
}


/* --------------------------------------------------------------------------
   pyxul::wrappers::jspy::Object::Sequence
   -------------------------------------------------------------------------- */

bool
Object::Sequence::__getitem__(
    JSContext* aCx, JS::HandleObject self, JS::HandleId aId,
    JS::MutableHandleValue aResult
)
{
    bool result = false;
    PyObject *aPyResult = nullptr;

    if ((aPyResult = PySequence_GetItem(__unwrap__(self), JSID_TO_INT(aId)))) { // +1
        aResult.set(Wrap(aCx, aPyResult));
        Py_DECREF(aPyResult); // -1
        result = !aResult.isUndefined();
    }
    return result;
}


bool
Object::Sequence::__setitem__(
    JSContext* aCx, JS::HandleObject self, JS::HandleId aId,
    JS::HandleValue aValue
)
{
    bool result = false;
    PyObject *aPyValue = nullptr;

    if ((aPyValue = pyjs::Wrap(aCx, aValue))) { // +1
        result = !PySequence_SetItem(
            __unwrap__(self), JSID_TO_INT(aId), aPyValue
        );
        Py_DECREF(aPyValue); // -1
    }
    return result;
}


bool
Object::Sequence::__delitem__(
    JSContext* aCx, JS::HandleObject self, JS::HandleId aId
)
{
    bool result = false;

    result = !PySequence_DelItem(__unwrap__(self), JSID_TO_INT(aId));
    return result;
}


/* -------------------------------------------------------------------------- */

bool
Object::Sequence::GetPropertyOp(
    JSContext* aCx, JS::HandleObject self, JS::HandleValue rec, JS::HandleId aId,
    JS::MutableHandleValue aResult
)
{
    if (JSID_IS_INT(aId)) {
        AutoGILState ags; // XXX: important
        AutoResult result = false;
        AutoReporter ar(aCx);
        result = __getitem__(aCx, self, aId, aResult);
        return bool(result);
    }
    return Object::GetPropertyOp(aCx, self, rec, aId, aResult);
}


bool
Object::Sequence::SetPropertyOp(
    JSContext* aCx, JS::HandleObject self, JS::HandleId aId,
    JS::HandleValue aValue, JS::HandleValue rec, JS::ObjectOpResult &strict
)
{
    if (JSID_IS_INT(aId)) {
        AutoGILState ags; // XXX: important
        AutoResult result = false;
        AutoReporter ar(aCx);
        if ((result = __setitem__(aCx, self, aId, aValue))) {
            strict.succeed();
        }
        return bool(result);
    }
    return Object::SetPropertyOp(aCx, self, aId, aValue, rec, strict);
}


bool
Object::Sequence::DeletePropertyOp(
    JSContext* aCx, JS::HandleObject self, JS::HandleId aId,
    JS::ObjectOpResult &strict
)
{
    if (JSID_IS_INT(aId)) {
        AutoGILState ags; // XXX: important
        AutoResult result = false;
        AutoReporter ar(aCx);
        if ((result = __delitem__(aCx, self, aId))) {
            strict.succeed();
        }
        return bool(result);
    }
    return Object::DeletePropertyOp(aCx, self, aId, strict);
}


// Object::Sequence::ObjectOps
const js::ObjectOps Object::Sequence::ObjectOps = {
    JSPY_OBJECT_OBJECT_OPS
};


/* public ------------------------------------------------------------------- */

// Object::Sequence::Class
const js::Class Object::Sequence::Class = {
    JSPY_CLASS("pyxul::wrappers::jspy::Object::Sequence")
};


/* --------------------------------------------------------------------------
   pyxul::wrappers::jspy::Object::Callable
   -------------------------------------------------------------------------- */

// Object::Callable::ClassOps
const js::ClassOps Object::Callable::ClassOps = {
    JSPY_CALLABLE_CLASS_OPS
};


/* public ------------------------------------------------------------------- */

// Object::Callable::Class
const js::Class Object::Callable::Class = {
    JSPY_CLASS("pyxul::wrappers::jspy::Object::Callable")
};


} // namespace pyxul::wrappers::jspy


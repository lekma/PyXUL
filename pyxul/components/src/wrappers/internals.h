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


#ifndef __pyxul_wrappers_internals_h__
#define __pyxul_wrappers_internals_h__


#include "pyxul/jsapi.h"
#include "pyxul/python.h"

#include "wrappers/cache.h"

#include "errors.h"
#include "python.h"
#include "xpc.h"


namespace pyxul::wrappers {


    class MOZ_STACK_CLASS AutoResult final {
        public:
            AutoResult(bool aResult) {
                mResult = aResult;
            }

            AutoResult &operator=(const bool aResult) {
                mResult = aResult;
                return *this;
            }

            explicit operator bool() const {
                return mResult;
            }

            ~AutoResult() {
                if (!mResult) {
                    xpc::ReportError();
                }
            }

        private:
            bool mResult;
    };


    namespace pyjs {


        // wrappers::pyjs::Object
        class Object : public PyObject {
            public:
                static PyTypeObject Type;

                static PyMethodDef Methods[];

                class Iterator;
                class Array;
                class Map;
                class Set;
                class Callable;

                static PyObject *New(
                    JSContext *aCx, const JS::HandleObject &aJSObject,
                    const JS::HandleObject &aThis
                );
                static JSObject *Unwrap(PyObject *aPyObject);

                static bool __iter__(
                    JSContext* aCx, Object *self, JS::MutableHandleValue aResult
                );

            protected:
                static void __finalize__(Object *self);
                static JSObject *__unwrap__(Object *self);

                static PyObject *__repr__(JSContext *aCx, Object *self);
                static PyObject *__str__(JSContext *aCx, Object *self);
                static PyObject *__getattr__(
                    JSContext *aCx, Object *self, const char *aName
                );
                static int __setattr__(
                    JSContext *aCx, Object *self, const char *aName,
                    PyObject *aValue
                );
                static int __delattr__(
                    JSContext *aCx, Object *self, const char *aName
                );

                static PyObject *__compare__(
                    Object *self, PyObject *aOther, int op
                );

                template<typename T>
                static PyObject *Alloc(
                    JSContext *aCx, const JS::HandleObject &aJSObject,
                    const JS::HandleObject &aThis
                );

                static void Dealloc(Object *self);
                static PyObject *Repr(Object *self);
                static Py_hash_t Hash(Object *self);
                static PyObject *Str(Object *self);
                static PyObject *GetAttrO(Object *self, PyObject *aName);
                static int SetAttrO(
                    Object *self, PyObject *aName, PyObject *aValue
                );
                static PyObject *RichCompare(
                    Object *self, PyObject *aOther, int op
                );
                template<typename T>
                static PyObject *Iter(Object *self);
                static void Finalize(Object *self);

                static PyObject *Dir(Object *self);

            private:
                JS::PersistentRootedObject mJSObject;
                JS::PersistentRootedObject mThis; //for callables

                Object();
                ~Object();
        };


        // wrappers::pyjs::Object::Iterator
        class Object::Iterator final : public Object {
            public:
                static PyTypeObject Type;

            protected:
                static PyObject *__next__(JSContext *aCx, Object *self);

                static PyObject *Next(Object *self);
        };


        // wrappers::pyjs::Object::Array
        class Object::Array final : public Object {
            public:
                static PyTypeObject Type;

                static PySequenceMethods AsSequence;
                static PyMappingMethods AsMapping;

            protected:
                static Py_ssize_t __len__(JSContext *aCx, Object *self);
                static PyObject *__getitem__(
                    JSContext *aCx, Object *self, Py_ssize_t aIdx
                );
                static int __setitem__(
                    JSContext *aCx, Object *self, Py_ssize_t aIdx,
                    PyObject *aValue
                );
                static PyObject *__getslice__(
                    JSContext *aCx, Object *self, PyObject *aSlice,
                    Py_ssize_t aLen
                );
                static int __setslice__(
                    JSContext *aCx, Object *self, PyObject *aSlice,
                    Py_ssize_t aLen, PyObject *aValue
                );

                static PyObject *__compare__(
                    Object *self, PyObject *aOther, int op
                );

                static Py_ssize_t Length(Object *self);
                static PyObject *Concat(Object *self, PyObject *aOther);
                static PyObject *Repeat(Object *self, Py_ssize_t aCount);
                static PyObject *GetItem(Object *self, Py_ssize_t aIdx);
                static int SetItem(
                    Object *self, Py_ssize_t aIdx, PyObject *aValue
                );
                static int Contains(Object *self, PyObject *aValue);
                static PyObject *InPlaceConcat(Object *self, PyObject *aOther);
                static PyObject *InPlaceRepeat(Object *self, Py_ssize_t aCount);

                static PyObject *GetSlice(Object *self, PyObject *aSlice);
                static int SetSlice(
                    Object *self, PyObject *aSlice, PyObject *aValue
                );

                static PyObject *RichCompare(
                    Object *self, PyObject *aOther, int op
                );
            private:
                static bool __check__(PyObject *aOther);
                static PyObject *__getElement__(
                    JSContext *aCx, Object *self, uint32_t aIdx
                );
                static int __setElement__(
                    JSContext *aCx, Object *self, uint32_t aIdx, PyObject *aValue
                );
                static PyObject *__getElements__(
                    JSContext *aCx, Object *self, uint32_t aStart, uint32_t aCount
                );
                static int __setElements__(
                    JSContext *aCx, Object *self, uint32_t aStart, uint32_t aCount,
                    PyObject *aValue
                );
                static int __delElements__(
                    JSContext *aCx, Object *self, uint32_t aStart, uint32_t aCount
                );
        };


        // wrappers::pyjs::Object::Map
        class Object::Map final : public Object {
            public:
                static PyTypeObject Type;

                static PySequenceMethods AsSequence;
                static PyMappingMethods AsMapping;

                static bool __iter__(
                    JSContext* aCx, Object *self, JS::MutableHandleValue aResult
                );

            protected:
                static PyObject *__getitem__(
                    JSContext *aCx, Object *self, PyObject *aKey
                );
                static int __setitem__(
                    JSContext *aCx, Object *self, PyObject *aKey,
                    PyObject *aValue
                );
                static int __delitem__(
                    JSContext *aCx, Object *self, PyObject *aKey
                );

                static int __eq__(
                    JSContext *aCx, Object *self, PyObject *aOther
                );
                static PyObject *__compare__(
                    Object *self, PyObject *aOther, int op
                );

                static int Contains(Object *self, PyObject *aKey);

                static Py_ssize_t Length(Object *self);
                static PyObject *GetItem(Object *self, PyObject *aKey);
                static int SetItem(
                    Object *self, PyObject *aKey, PyObject *aValue
                );

                static PyObject *RichCompare(
                    Object *self, PyObject *aOther, int op
                );
            private:
                static bool __check__(PyObject *aOther);
        };


        // wrappers::pyjs::Object::Set
        class Object::Set final : public Object {
            public:
                static PyTypeObject Type;

                static PyNumberMethods AsNumber;
                static PySequenceMethods AsSequence;

                static PyMethodDef Methods[];

                static bool __iter__(
                    JSContext* aCx, Object *self, JS::MutableHandleValue aResult
                );

            protected:
                static int __sub__(
                    JSContext *aCx, PyObject *aOther,
                    JS::MutableHandleObject aResult
                );
                static int __and__(
                    JSContext *aCx, PyObject *aOther,
                    JS::MutableHandleObject aResult
                );
                static int __xor__(
                    JSContext *aCx, PyObject *aOther,
                    JS::MutableHandleObject aResult
                );
                static int __or__(
                    JSContext *aCx, PyObject *aOther,
                    JS::MutableHandleObject aResult
                );

                static PyObject *__le__(
                    JSContext *aCx, Object *self, PyObject *aOther
                );
                static PyObject *__ge__(
                    JSContext *aCx, Object *self, PyObject *aOther
                );
                static PyObject *__compare__(
                    Object *self, PyObject *aOther, int op
                );

                static PyObject *Subtract(Object *self, PyObject *aOther);
                static PyObject *And(Object *self, PyObject *aOther);
                static PyObject *Xor(Object *self, PyObject *aOther);
                static PyObject *Or(Object *self, PyObject *aOther);

                static Py_ssize_t Length(Object *self);
                static int Contains(Object *self, PyObject *aValue);

                static PyObject *RichCompare(
                    Object *self, PyObject *aOther, int op
                );

                static PyObject *IsDisjoint(Object *self, PyObject *aOther);
                static PyObject *IsSubset(Object *self, PyObject *aOther);
                static PyObject *IsSuperset(Object *self, PyObject *aOther);
                static PyObject *Difference(Object *self, PyObject *args);
                static PyObject *Intersection(Object *self, PyObject *args);
                static PyObject *SymmetricDifference(Object *self, PyObject *args);
                static PyObject *Union(Object *self, PyObject *args);
            private:
                static bool __check__(PyObject *aOther);
                static JSObject *__copy__(JSContext *aCx, Object *self);
        };


        // wrappers::pyjs::Object::Callable
        class Object::Callable final : public Object {
            public:
                static PyTypeObject Type;

            protected:
                static PyObject *__call__(
                    JSContext *aCx, Object *self, PyObject *aArgs
                );

                static PyObject *Call(
                    Object *self, PyObject *aArgs, PyObject *aKwargs
                );
        };


        PyObject *WrapString(JSContext *aCx, const JS::HandleString &aJSString);
        PyObject *WrapSymbol(JSContext *aCx, const JS::HandleSymbol &aJSSymbol);
        PyObject *WrapId(JSContext *aCx, jsid aId);
        PyObject *WrapArgs(JSContext *aCx, JS::CallArgs &args);
        JSObject *Unwrap(PyObject *aPyObject);


    } // namespace pyjs


    namespace jspy {


        class ObjectCache final : public Cache<PyObject, JSObject> {
            public:
                void finalizeObject(PyObject *aKey, JSObject *aData) override;
        };


        // wrappers::jspy::Object
        class Object {
            friend class ObjectCache;

            public:
                static const js::Class Class;

                static ObjectCache Objects;

                class Sequence;
                class Callable;

                static JSObject *New(JSContext* aCx, PyObject *aObject);
                static PyObject *Unwrap(const JS::HandleObject &aJSObject);

            protected:
                static const js::ClassOps ClassOps;
                static const js::ClassExtension ClassExtension;
                static const js::ObjectOps ObjectOps;

                static JSObject *__new__(JSContext* aCx, PyObject *aObject);
                static void __finalize__(JSObject *self);
                static PyObject *__unwrap__(JS::HandleObject self);

                static bool __getattr__(
                    JSContext *aCx, JS::HandleObject self, JS::HandleId aId,
                    JS::MutableHandleValue aResult
                );
                static bool __setattr__(
                    JSContext* aCx, JS::HandleObject self, JS::HandleId aId,
                    JS::HandleValue aValue
                );
                static bool __delattr__(
                    JSContext *aCx, JS::HandleObject self, JS::HandleId aId
                );

                template<typename T>
                static JSObject *Alloc(
                    JSContext *aCx, const JS::HandleObject &aProto,
                    PyObject *aPyObject
                );

                static bool Resolve(
                    JSContext* aCx, JS::HandleObject self, JS::HandleId aId,
                    bool *resolvedp
                );
                static void Finalize(js::FreeOp *fop, JSObject *self);
                static bool Call(JSContext *aCx, unsigned argc, JS::Value *vp);
                static void Moved(JSObject *self, const JSObject *old);

                static bool GetPropertyOp(
                    JSContext* aCx, JS::HandleObject self, JS::HandleValue rec,
                    JS::HandleId aId, JS::MutableHandleValue aResult
                );
                static bool SetPropertyOp(
                    JSContext* aCx, JS::HandleObject self, JS::HandleId aId,
                    JS::HandleValue aValue, JS::HandleValue rec,
                    JS::ObjectOpResult &strict
                );
                static bool DeletePropertyOp(
                    JSContext* aCx, JS::HandleObject self, JS::HandleId aId,
                    JS::ObjectOpResult &strict
                );

                static bool Enumerate(
                    JSContext *aCx, JS::HandleObject self, JS::AutoIdVector &aIds,
                    bool unused
                );

            private:
                Object();
                ~Object();
        };


        // wrappers::jspy::Object::Sequence
        class Object::Sequence final : public Object {
            public:
                static const js::Class Class;

            protected:
                static const js::ObjectOps ObjectOps;

                static bool __getitem__(
                    JSContext* aCx, JS::HandleObject self, JS::HandleId aId,
                    JS::MutableHandleValue aResult
                );
                static bool __setitem__(
                    JSContext* aCx, JS::HandleObject self, JS::HandleId aId,
                    JS::HandleValue aValue
                );
                static bool __delitem__(
                    JSContext* aCx, JS::HandleObject self, JS::HandleId aId
                );

                static bool GetPropertyOp(
                    JSContext* aCx, JS::HandleObject self, JS::HandleValue rec,
                    JS::HandleId aId, JS::MutableHandleValue aResult
                );
                static bool SetPropertyOp(
                    JSContext* aCx, JS::HandleObject self, JS::HandleId aId,
                    JS::HandleValue aValue, JS::HandleValue rec,
                    JS::ObjectOpResult &strict
                );
                static bool DeletePropertyOp(
                    JSContext* aCx, JS::HandleObject self, JS::HandleId aId,
                    JS::ObjectOpResult &strict
                );
        };


        // wrappers::jspy::Object::Callable
        class Object::Callable final : public Object {
            public:
                static const js::Class Class;

            protected:
                static const js::ClassOps ClassOps;
        };


        // wrappers::jspy::Type
        class Type : public Object {
            public:
                static const js::Class Class;

                static const JSFunctionSpec Functions[];
                static const JSPropertySpec Properties[];

                static JS::PersistentRootedObject ProtoBase;

                class Iterator;
                class Mapping;
                class Sequence;
                class Set;

                static JSObject *New(
                    JSContext* aCx, PyObject *aType,
                    const JS::HandleObject &aProto = nullptr);
                static JSObject *GetProto(JSContext* aCx, PyTypeObject *aType);
                static bool Check(const JS::HandleObject &aJSObject);

                enum IterType : int {
                    Keys = 2,
                    Values = 4,
                    Entries = 8
                };
                static PyObject *__iter__(PyObject *aObject, int type);

            protected:
                static const js::ClassOps ClassOps;
                static const js::ObjectOps ObjectOps;

                static bool __iterator__(
                    JSContext *aCx, JS::CallArgs &args, PyObject *aObject,
                    bool legacy
                );
                static bool __map__(
                    JSContext *aCx, JS::CallArgs &args, PyObject *aObject
                );

                template<typename T>
                static JSObject *Alloc(
                    JSContext *aCx, PyObject *aType,
                    const JS::HandleObject &aProto, bool isBase
                );

                static bool GetProperty(
                    JSContext* aCx, JS::HandleObject self, JS::HandleId aId,
                    JS::MutableHandleValue aResult
                );
                static bool SetProperty(
                    JSContext* aCx, JS::HandleObject self, JS::HandleId aId,
                    JS::MutableHandleValue aValue, JS::ObjectOpResult &strict
                );
                static bool DelProperty(
                    JSContext* aCx, JS::HandleObject self, JS::HandleId aId,
                    JS::ObjectOpResult &strict
                );

                static bool HasInstance(
                    JSContext *aCx, JS::HandleObject self,
                    JS::MutableHandleValue vp, bool *isInstancep
                );

                static bool ToString(JSContext *aCx, unsigned argc, JS::Value *vp);
                static bool ToSource(JSContext *aCx, unsigned argc, JS::Value *vp);
                static bool ToPrimitive(JSContext *aCx, unsigned argc, JS::Value *vp);
                static bool ValueOf(JSContext *aCx, unsigned argc, JS::Value *vp);

                template<typename T>
                static bool GetIterator(
                    JSContext *aCx, unsigned argc, JS::Value *vp,
                    const char *name, int type
                );
                template<typename T, int type>
                static bool GetIterator(JSContext *aCx, unsigned argc, JS::Value *vp);
                template<typename T>
                static bool GetKeys(JSContext *aCx, unsigned argc, JS::Value *vp);
                template<typename T>
                static bool GetValues(JSContext *aCx, unsigned argc, JS::Value *vp);
                template<typename T>
                static bool GetEntries(JSContext *aCx, unsigned argc, JS::Value *vp);

                template<typename T>
                static bool GetLegacyIterator(JSContext *aCx, unsigned argc, JS::Value *vp);
                template<typename T>
                static bool ForEach(JSContext *aCx, unsigned argc, JS::Value *vp);
        };


        // wrappers::jspy::Type::Iterator
        class Type::Iterator final : public Type {
            public:
                static const JSFunctionSpec Functions[];
                static const JSPropertySpec Properties[];

            protected:
                static bool __wrap__(
                    JSContext *aCx, JS::CallArgs &args, PyObject *aObject
                );

                static bool GetIterator(JSContext *aCx, unsigned argc, JS::Value *vp);
                static bool Next(JSContext *aCx, unsigned argc, JS::Value *vp);
                static bool __next__(JSContext *aCx, unsigned argc, JS::Value *vp);
                static bool __legacy__(JSContext *aCx, unsigned argc, JS::Value *vp);
        };


        // wrappers::jspy::Type::Mapping
        class Type::Mapping final : public Type {
            public:
                static const JSFunctionSpec Functions[];
                static const JSPropertySpec Properties[];

                static PyObject *__iter__(PyObject *aObject, int type);

            protected:
                static bool Get(JSContext *aCx, unsigned argc, JS::Value *vp);
                static bool Set(JSContext *aCx, unsigned argc, JS::Value *vp);
                static bool Delete(JSContext *aCx, unsigned argc, JS::Value *vp);
                static bool Has(JSContext *aCx, unsigned argc, JS::Value *vp);
                static bool Clear(JSContext *aCx, unsigned argc, JS::Value *vp);

                static bool Size(JSContext *aCx, unsigned argc, JS::Value *vp);
        };


        // wrappers::jspy::Type::Sequence
        class Type::Sequence final : public Type {
            public:
                static const JSFunctionSpec Functions[];
                static const JSPropertySpec Properties[];

                static PyObject *__iter__(PyObject *aObject, int type);

            protected:
                static bool Length(JSContext *aCx, unsigned argc, JS::Value *vp);
        };


        // wrappers::jspy::Type::Set
        class Type::Set final : public Type {
            public:
                static const JSFunctionSpec Functions[];
                static const JSPropertySpec Properties[];

                static PyObject *__iter__(PyObject *aObject, int type);

            protected:
                static bool Add(JSContext *aCx, unsigned argc, JS::Value *vp);
                static bool Delete(JSContext *aCx, unsigned argc, JS::Value *vp);
                static bool Has(JSContext *aCx, unsigned argc, JS::Value *vp);
                static bool Clear(JSContext *aCx, unsigned argc, JS::Value *vp);

                static bool Size(JSContext *aCx, unsigned argc, JS::Value *vp);
        };


        JS::Value WrapUnicode(JSContext* aCx, PyObject *aValue);
        bool WrapArgs(JSContext *aCx, PyObject *args, JS::AutoValueVector &out);
        bool WrapArgs(
            JSContext *aCx, const JS::HandleObject &args,
            JS::AutoValueVector &out
        );
        PyObject *Unwrap(const JS::HandleObject &aJSObject);


    } // namespace jspy


} // namespace pyxul::wrappers


// ClassOps
#define JSPY_BASE_CLASS_OPS \
    .resolve = Resolve, \
    .finalize = Finalize,

#define JSPY_CALLABLE_CLASS_OPS \
    JSPY_BASE_CLASS_OPS \
    .call = Call,

#define JSPY_TYPE_CLASS_OPS \
    .delProperty = DelProperty, \
    .getProperty = GetProperty, \
    .setProperty = SetProperty, \
    JSPY_CALLABLE_CLASS_OPS \
    .hasInstance = HasInstance, \
    .construct = Call,


// ObjectOps
#define JSPY_BASE_OBJECT_OPS \
    .enumerate = Enumerate,

#define JSPY_OBJECT_OBJECT_OPS  \
    .getProperty = GetPropertyOp, \
    .setProperty = SetPropertyOp, \
    .deleteProperty = DeletePropertyOp,\
    JSPY_BASE_OBJECT_OPS


// Class
#define JSPY_CLASS(n) \
    .name = n, \
    .flags = (JSCLASS_HAS_PRIVATE |JSCLASS_FOREGROUND_FINALIZE), \
    .cOps = &ClassOps, \
    .ext = &ClassExtension, \
    .oOps = &ObjectOps,


#endif // __pyxul_wrappers_internals_h__


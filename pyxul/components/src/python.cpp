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


#include "python.h"

#include "errors.h"
#include "xpc.h"


using namespace pyxul;


/* --------------------------------------------------------------------------
   hooks
   -------------------------------------------------------------------------- */

/* excepthook */
PyDoc_STRVAR(__excepthook___doc, "excepthook(type, value, traceback)");

static PyObject *
__excepthook__(PyObject *module, PyObject *args)
{
    PyObject *type, *value, *traceback;

    if (
        !PyArg_ParseTuple(args, "OOO:excepthook", &type, &value, &traceback) ||
        !xpc::SetException(type, value, traceback)
    ) {
        return nullptr;
    }
    Py_RETURN_NONE;
}


/* showwarning */
PyDoc_STRVAR(
    __showwarning___doc,
    "showwarning(message, category, filename, lineno, file=None, line=None)"
);

static PyObject *
__showwarning__(PyObject *module, PyObject *args, PyObject *kwargs)
{
    PyObject *message, *category, *filename, *lineno;
    PyObject *file = Py_None, *line = Py_None;

    static const char *kwlist[] = {
        "message", "category", "filename", "lineno", "file", "line", nullptr
    };

    if (
        !PyArg_ParseTupleAndKeywords(
            args, kwargs, "OOOO|OO:showwarning", const_cast<char **>(kwlist),
            &message, &category, &filename, &lineno, &file, &line
        ) ||
        !xpc::ReportWarning(message, category, filename, lineno, line)
    ) {
        return nullptr;
    }
    Py_RETURN_NONE;
}


/* --------------------------------------------------------------------------
   pyxul module
   -------------------------------------------------------------------------- */

/* pyxul_def.m_methods */
static PyMethodDef pyxul_m_methods[] = {
    {
        "__excepthook__", (PyCFunction)__excepthook__,
        METH_VARARGS, __excepthook___doc
    },
    {
        "__showwarning__", (PyCFunction)__showwarning__,
        METH_VARARGS | METH_KEYWORDS, __showwarning___doc
    },
    {nullptr} /* Sentinel */
};


/* pyxul_def */
static PyModuleDef pyxul_def = {
    PyModuleDef_HEAD_INIT,
    .m_name = "pyxul",
    .m_doc = "pyxul module",
    .m_size = 0,
    .m_methods = pyxul_m_methods,
};


static int
pyxul_init(PyObject *module)
{
    if (
        _PyModule_AddObject(module, "Error", errors::Error) ||
        _PyModule_AddObject(module, "XPCOMError", errors::XPCOMError) ||
        _PyModule_AddObject(module, "JSError", errors::JSError) ||
        _PyModule_AddObject(module, "JSWarning", errors::JSWarning)
    ) {
        return -1;
    }
    return 0;
}


/* module initialization */
PyMODINIT_FUNC
PyInit_pyxul(void)
{
    PyObject *module = nullptr;

    if ((module = PyState_FindModule(&pyxul_def))) {
        Py_INCREF(module);
    }
    else if ((module = PyModule_Create(&pyxul_def)) && pyxul_init(module)) {
        Py_CLEAR(module);
    }
    return module;
}


/* --------------------------------------------------------------------------
   _xpcom module
   -------------------------------------------------------------------------- */

static PyObject *
_xpcom_throwResult(PyObject *module, PyObject *arg)
{
    PyObject *pylong = nullptr;
    unsigned long rv = ((unsigned long)-1);

    if (!(pylong = PyNumber_Long(arg))) { // +1
        return nullptr;
    }
    rv = PyLong_AsUnsignedLong(pylong);
    Py_DECREF(pylong); // -1
    if ((rv == ((unsigned long)-1)) && PyErr_Occurred()) {
        return nullptr;
    }
    if (rv > UINT32_MAX) {
        PyErr_SetString(PyExc_OverflowError, "nsresult out of range");
        return nullptr;
    }
    xpc::ThrowResult((nsresult)rv);
    return nullptr;
}


static PyObject *
_xpcom_importModule(PyObject *module, PyObject *args)
{
    const char *uri = nullptr;
    PyObject *target = Py_None;
    bool result = false;

    if (!PyArg_ParseTuple(args, "s|O!:importModule", &uri, &PyModule_Type, &target)) {
        return nullptr;
    }
    if (target == Py_None) {
        if (!(target = PyImport_ImportModule("builtins"))) {
            return nullptr;
        }
    }
    else {
        Py_INCREF(target);
    }
    result = xpc::ImportModule(uri, target);
    Py_DECREF(target);
    if (!result) {
        return nullptr;
    }
    Py_RETURN_NONE;
}


/* _xpcom_def.m_methods */
static PyMethodDef _xpcom_m_methods[] = {
    {"throwResult", (PyCFunction)_xpcom_throwResult, METH_O, nullptr},
    {"importModule", (PyCFunction)_xpcom_importModule, METH_VARARGS, nullptr},
    {nullptr} /* Sentinel */
};


/* _xpcom_def */
static PyModuleDef _xpcom_def = {
    PyModuleDef_HEAD_INIT,
    .m_name = "_xpcom",
    .m_doc = "_xpcom module",
    .m_size = 0,
    .m_methods = _xpcom_m_methods,
};


static int
_xpcom_init(PyObject *module)
{
    _Py_IDENTIFIER(Components);
    PyObject *global = nullptr, *components = nullptr;
    int res = -1;

    if ((global = xpc::GetGlobalJSObject())) { // +1
        if ((components = _PyObject_GetAttrId(global, &PyId_Components))) { // +1
            if (!_PyModule_AddObject(module, PyId_Components.string, components)) {
                res = 0;
            }
            Py_DECREF(components); // -1
        }
        Py_DECREF(global); // -1
    }
    return res;
}


/* module initialization */
PyMODINIT_FUNC
PyInit__xpcom(void)
{
    PyObject *module = nullptr;

    if ((module = PyState_FindModule(&_xpcom_def))) {
        Py_INCREF(module);
    }
    else if ((module = PyModule_Create(&_xpcom_def)) && _xpcom_init(module)) {
        Py_CLEAR(module);
    }
    return module;
}


/* --------------------------------------------------------------------------
   utils
   -------------------------------------------------------------------------- */

int
_PyType_ReadyWithBase(PyTypeObject *type, PyTypeObject *base)
{
    type->tp_base = base;
    return PyType_Ready(type);
}


int
_PyModule_AddObject(PyObject *module, const char *name, PyObject *object)
{
    Py_INCREF(object);
    if (PyModule_AddObject(module, name, object)) {
        Py_DECREF(object);
        return -1;
    }
    return 0;
}


void
_Py_Collect(void)
{
    while (PyGC_Collect() > 0);
}


PyObject *
_PyObject_Vars(PyObject *self)
{
    _Py_IDENTIFIER(__dict__);
    _Py_IDENTIFIER(copy);
    PyObject *dict = nullptr, *result = nullptr;

    if (_PyObject_LookupAttrId(self, &PyId___dict__, &dict) == 0) {
        dict = PyDict_New();
    }
    if (dict) {
        result = _PyObject_CallMethodId(dict, &PyId_copy, nullptr);
        Py_DECREF(dict);
    }
    return result;
}

static int
merge_type_dict(PyObject *result, PyObject *type, int update)
{
    _Py_IDENTIFIER(__bases__);
    PyObject *dict = nullptr, *bases = nullptr, *base = nullptr;
    int status = -1;
    Py_ssize_t i, n;

    if (!(dict = _PyObject_Vars(type))) {
        return -1;
    }
    status = PyDict_Merge(result, dict, update);
    Py_DECREF(dict);
    if (status) {
        return -1;
    }
    if (_PyObject_LookupAttrId(type, &PyId___bases__, &bases) < 0) {
        return -1;
    }
    if (bases) {
        if ((n = PySequence_Size(bases)) < 0) {
            Py_DECREF(bases);
            return -1;
        }
        else {
            for (i = 0; i < n; i++) {
                if (!(base = PySequence_GetItem(bases, i))) {
                    Py_DECREF(bases);
                    return -1;
                }
                status = merge_type_dict(result, base, 1);
                Py_DECREF(base);
                if (status) {
                    Py_DECREF(bases);
                    return -1;
                }
            }
        }
        Py_DECREF(bases);
    }
    return 0;
}

PyObject *
_PyObject_Dir(PyObject *self)
{
    PyObject *result = nullptr, *type = nullptr;

    if (PyType_Check(self)) {
        Py_INCREF(self);
        type = self;
        result = PyDict_New();
    }
    else if ((type = PyObject_Type(self))) {
        result = _PyObject_Vars(self);
    }
    if (result && merge_type_dict(result, type, 0)) {
        Py_CLEAR(result);
    }
    Py_XDECREF(type);
    return result;
}


PyObject *
_PySequence_Fast(PyObject *obj, const char *message, Py_ssize_t len)
{
    PyObject *result = nullptr;

    if (
        (result = PySequence_Fast(obj, message)) &&
        (PySequence_Fast_GET_SIZE(result) != len)
    ) {
        PyErr_Format(PyExc_ValueError, "expected a sequence of len %zd", len);
        Py_CLEAR(result);
    }
    return result;
}


int
_PyMapping_Clear(PyObject *self)
{
    _Py_IDENTIFIER(clear);
    PyObject *cleared = nullptr, *keys = nullptr;
    Py_ssize_t i = 0 , size = 0;

    if ((cleared = _PyObject_CallMethodId(self, &PyId_clear, nullptr))) {
        Py_DECREF(cleared);
        return 0;
    }
    if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
        PyErr_Clear();
        if ((keys = PyMapping_Keys(self))) { // +1
            if ((size = PySequence_Fast_GET_SIZE(keys)) > 0) {
                for (; i < size; i++) {
                    if (
                        PyMapping_DelItem(
                            self, PySequence_Fast_GET_ITEM(keys, i) // borrowed
                        )
                    ) {
                        break;
                    }
                }
            }
            Py_DECREF(keys); // -1
            return (i == size) ? 0 : -1;
        }
    }
    return -1;
}


int
_PySlice_GetIndices(
    PyObject *slice, Py_ssize_t length,
    Py_ssize_t *start, Py_ssize_t *stop, Py_ssize_t *slicelength
)
{
    Py_ssize_t step;

    if (PySlice_Unpack(slice, start, stop, &step)) {
        return -1;
    }
    if (step != 1) {
        PyErr_SetString(PyExc_ValueError, "slice step can only be 1");
        return -1;
    }
    *slicelength = PySlice_AdjustIndices(length, start, stop, step);
    return 0;
}


int
_PyIndex_AsSsize_t(PyObject *index, Py_ssize_t length, Py_ssize_t *result)
{
    Py_ssize_t idx = -1;

    if (
        ((idx = PyNumber_AsSsize_t(index, PyExc_IndexError)) == -1) &&
        PyErr_Occurred()
    ) {
        return -1;
    }
    if (idx < 0) {
        idx += length;
    }
    *result = idx;
    return 0;
}


uint32_t
_PySsize_tAsUint32_t(Py_ssize_t value)
{
    PyObject *pylong = nullptr;
    uint32_t result = ((uint32_t)-1);

    if ((value < 0) || (value > UINT32_MAX)) {
        PyErr_SetString(
            PyExc_OverflowError, "Python size out of range for JavaScript"
        );
    }
    else if ((pylong = PyLong_FromSsize_t(value))) {
        result = PyLong_AsUnsignedLong(pylong);
        Py_DECREF(pylong);
    }
    return result;
}


/* --------------------------------------------------------------------------
   error helpers
   -------------------------------------------------------------------------- */

// taken from Python/traceback.c
#define PyTraceBack_LIMIT 512

long
_PyTraceBack_GetLimit(void)
{
    _Py_IDENTIFIER(tracebacklimit);
    long limit = PyTraceBack_LIMIT;
    PyObject *pylimit = nullptr;

    if ((pylimit = _PySys_GetObjectId(&PyId_tracebacklimit))) { // borrowed
        PyObject *exc_type, *exc_value, *exc_tb;
        PyErr_Fetch(&exc_type, &exc_value, &exc_tb);
        limit = PyLong_AsLong(pylimit);
        if (limit == -1 && PyErr_Occurred()) {
            if (PyErr_ExceptionMatches(PyExc_OverflowError)) {
                limit = PyTraceBack_LIMIT;
                PyErr_Clear();
            }
            else {
                Py_XDECREF(exc_type);
                Py_XDECREF(exc_value);
                Py_XDECREF(exc_tb);
                return -1;
            }
        }
        else if (limit <= 0) {
            limit = 0;
        }
        PyErr_Restore(exc_type, exc_value, exc_tb);
    }
    return limit;
}


PyObject *
_Py_GetSourceLine(PyObject *filename, PyObject *lineno, PyObject *globals)
{
    _Py_IDENTIFIER(checkcache);
    _Py_IDENTIFIER(getline);
    PyObject *linecache = nullptr, *checkcache = nullptr, *getline = nullptr;
    PyObject *unused = nullptr, *result = nullptr;

    if ((linecache = PyImport_ImportModule("linecache")) &&
        (checkcache = _PyObject_GetAttrId(linecache, &PyId_checkcache)) &&
        (getline = _PyObject_GetAttrId(linecache, &PyId_getline)) &&
        (unused = PyObject_CallFunctionObjArgs(checkcache, filename, nullptr))) {
        result = PyObject_CallFunctionObjArgs(
            getline, filename, lineno, globals, nullptr
        );
    }
    Py_XDECREF(unused);
    Py_XDECREF(getline);
    Py_XDECREF(checkcache);
    Py_XDECREF(linecache);
    return result;
}


PyObject *
_PyFrame_GetSourceLine(PyFrameObject *frame, int lineno)
{
    PyObject *_lineno = nullptr, *result = nullptr;

    if ((_lineno = PyLong_FromLong(lineno))) {
        result = _Py_GetSourceLine(
            frame->f_code->co_filename, _lineno, frame->f_globals
        );
    }
    Py_XDECREF(_lineno);
    return result;
}


/* --------------------------------------------------------------------------
   execute/import
   -------------------------------------------------------------------------- */

_Py_IDENTIFIER(__file__);
_Py_IDENTIFIER(__cached__);


/* compile ------------------------------------------------------------------ */

static struct _mod *
_PyParser_ASTFromFile(
    FILE *fp, PyObject *path, PyCompilerFlags *flags, PyArena *arena
)
{
    return PyParser_ASTFromFileObject(
        fp, path, nullptr, Py_file_input, nullptr, nullptr,
        flags, nullptr, arena
    );
}


static PyObject *
_PyAST_Compile(
    struct _mod *mod, PyObject *path, PyCompilerFlags *flags, PyArena *arena
)
{
    return (PyObject *)PyAST_CompileObject(mod, path, flags, -1, arena);
}


static PyObject *
_PyCompile_File(FILE *fp, PyObject *path)
{
    PyCompilerFlags flags = _PyCompilerFlags_INIT;
    //flags.cf_flags = PyCF_SOURCE_IS_UTF8;

    PyArena *arena = nullptr;
    struct _mod *mod = nullptr;
    PyObject *code = nullptr;

    if ((arena = PyArena_New())) {
        if ((mod = _PyParser_ASTFromFile(fp, path, &flags, arena))) {
            code = _PyAST_Compile(mod, path, &flags, arena);
        }
        PyArena_Free(arena);
    }
    return code;
}


/* execute ------------------------------------------------------------------ */

static int
_set_file(PyObject *globals, PyObject *path)
{
    if (!_PyDict_GetItemId(globals, &PyId___file__)) { // borrowed
        if (
            _PyDict_SetItemId(globals, &PyId___file__, path) ||
            _PyDict_SetItemId(globals, &PyId___cached__, Py_None)
        ) {
            return -1;
        }
        return 1;
    }
    return 0;
}


static int
_set_loader(PyObject *globals, PyObject *path, const char *loader_name)
{
    _Py_IDENTIFIER(_bootstrap_external);
    _Py_IDENTIFIER(__loader__);
    PyObject *importlib = nullptr,  *bootstrap = nullptr;
    PyObject *loader_type = nullptr, *loader = nullptr;
    int res = -1;

    if ((importlib = PyImport_AddModule("_frozen_importlib"))) { // borrowed
        bootstrap = _PyObject_GetAttrId(importlib, &PyId__bootstrap_external);
        if (bootstrap) { // +1
            loader_type = PyObject_GetAttrString(bootstrap, loader_name);
            if (loader_type) { // +1
                loader = PyObject_CallFunction(
                    loader_type, "sO", "__main__", path
                );
                if (loader) { // +1
                    res = _PyDict_SetItemId(globals, &PyId___loader__, loader);
                    Py_DECREF(loader); // - 1
                }
                Py_DECREF(loader_type); // - 1
            }
            Py_DECREF(bootstrap); // - 1
        }
    }
    return res;
}


static int
_set_builtins(PyObject *globals)
{
    _Py_IDENTIFIER(__builtins__);
    PyObject *builtins = nullptr;

    if (!_PyDict_GetItemId(globals, &PyId___builtins__)) { // borrowed
        if ((builtins = PyEval_GetBuiltins())) { // borrowed
            return _PyDict_SetItemId(globals, &PyId___builtins__, builtins);
        }
        PyErr_SetString(PyExc_SystemError, "no __builtins__!");
        return -1;
    }
    return 0;
}


static void
_flush_output(struct _Py_Identifier *output)
{
    _Py_IDENTIFIER(flush);
    PyObject *f = nullptr, *r = nullptr;

    if ((f = _PySys_GetObjectId(output))) { // borrowed
        if ((r = _PyObject_CallMethodId(f, &PyId_flush, nullptr))) {
            Py_DECREF(r);
        }
        else {
            PyErr_Clear();
        }
    }
}


static void
_flush_io(void)
{
    _Py_IDENTIFIER(stderr);
    _Py_IDENTIFIER(stdout);
    PyObject *type = nullptr, *value = nullptr, *traceback = nullptr;

    PyErr_Fetch(&type, &value, &traceback);
    _flush_output(&PyId_stderr);
    _flush_output(&PyId_stdout);
    PyErr_Restore(type, value, traceback);
}


static int
_execute_file(FILE *fp, PyObject *path, PyObject *globals)
{
    PyObject *code = nullptr, *result = nullptr;
    int res = -1;

    if (!_set_builtins(globals) && (code = _PyCompile_File(fp, path))) {
        res = ((result = PyEval_EvalCode(code, globals, globals))) ? 0 : -1;
        _flush_io();
        Py_XDECREF(result);
        Py_DECREF(code);
    }
    return res;
}


static void
_clear_file(PyObject *globals)
{
    PyObject *type = nullptr, *value = nullptr, *traceback = nullptr;

    PyErr_Fetch(&type, &value, &traceback);
    if (
        _PyDict_DelItemId(globals, &PyId___file__) ||
        _PyDict_DelItemId(globals, &PyId___cached__)
    ) {
        PyErr_Clear();
    }
    PyErr_Restore(type, value, traceback);
}


int
_PyExecute_File(FILE *fp, PyObject *path, PyObject *globals)
{
    int file_set = 0, res = -1;

    if ((file_set = _set_file(globals, path)) >= 0) {
        if (_set_loader(globals, path, "SourceFileLoader")) {
            PyErr_SetString(
                PyExc_RuntimeError, "failed to set __main__.__loader__"
            );
        }
        else {
            res = _execute_file(fp, path, globals);
        }
        if (file_set) {
            _clear_file(globals);
        }
    }
    return res;
}


/* import ------------------------------------------------------------------- */

PyObject *
_PyImport_File(FILE *fp, PyObject *path, const char *name)
{
    PyObject *code = nullptr, *module = nullptr, *modules = nullptr;

    _PyImport_AcquireLock();

    if ((code = _PyCompile_File(fp, path))) {
        if (
            (module = PyImport_ExecCodeModule(name, code)) &&
            (
                !(modules = PyImport_GetModuleDict()) || // borrowed
                PyDict_DelItemString(modules, name)
            )
        ) {
            Py_CLEAR(module);
            PyErr_SetString(
                PyExc_SystemError, "failed to get/update sys.modules"
            );
        }
        Py_DECREF(code);
    }

    if (_PyImport_ReleaseLock() < 0) {
        PyErr_SetString(PyExc_RuntimeError, "not holding the import lock");
        Py_CLEAR(module);
    }
    return module;
}


/* --------------------------------------------------------------------------
   setup
   -------------------------------------------------------------------------- */

static int
checkName(const char *name)
{
    static const char *emptyName = "";

    return (name && strcmp(name, emptyName));
}

static int
_Py_CopyFunc(
    const char *from, const char *fromName, const char *to, const char *toName
)
{
    PyObject *src = nullptr, *dst = nullptr, *fun = nullptr;
    int res = -1;

    if (checkName(from) && checkName(fromName)) {
        if (
            (src = PyImport_ImportModule(from)) &&
            (dst = checkName(to) ? PyImport_ImportModule(to) : (Py_XINCREF(src), src))
        ) {
            if (
                (src != dst) ||
                (checkName(toName) && strcmp(fromName, toName))
            ) {
                if ((fun = PyObject_GetAttrString(src, fromName))) {
                    res = PyObject_SetAttrString(
                        dst, checkName(toName) ? toName : fromName, fun
                    );
                }
            }
            else {
                PyErr_SetString(
                    PyExc_ValueError, "missing argument while copying functions"
                );
            }
        }
        Py_XDECREF(fun);
        Py_XDECREF(dst);
        Py_XDECREF(src);
    }
    else {
        PyErr_SetString(
            PyExc_ValueError, "missing argument while copying functions"
        );
    }
    return res;
}


static int
_Py_SetupHooks(void)
{
    const char *warnings = "warnings";
    const char *showwarning = "showwarning";
    const char *__showwarning__ = "__showwarning__";

    if (
        _Py_CopyFunc(warnings, showwarning, nullptr, __showwarning__) ||
        _Py_CopyFunc("pyxul", __showwarning__, warnings, showwarning)
    ) {
        return -1;
    }
    return 0;
}


static int
_Py_SetupSysPath(const char *dir)
{
    PyObject *syspath = nullptr, *path = nullptr;

    if (!(syspath = PySys_GetObject("path"))) { // borrowed
        PyErr_SetString(PyExc_SystemError, "Failed to get sys.path");
        return -1;
    }
    if (
        !(path = PyUnicode_DecodeFSDefault(dir)) ||
        PyList_Insert(syspath, 0, path)
    ) {
        Py_XDECREF(path);
        return -1;
    }
    Py_DECREF(path);
    return 0;
}


int
_Py_Setup(const char *dist, const char *site)
{
    if (_Py_SetupSysPath(dist) || _Py_SetupSysPath(site)) {
        return -1;
    }
    return _Py_SetupHooks();
}


/* --------------------------------------------------------------------------
   initialization/finalization
   -------------------------------------------------------------------------- */

static int
_Py_InitExceptHook(void)
{
    AutoGILState ags; // XXX: important

    static PyMethodDef sys_functions[] = {
        {
            "excepthook", (PyCFunction)__excepthook__,
            METH_VARARGS, __excepthook___doc
        },
        {nullptr}
    };

    PyObject *sys = nullptr;
    int res = -1;

    if ((sys = PyImport_ImportModule("sys"))) {
        res = PyModule_AddFunctions(sys, sys_functions);
        Py_DECREF(sys);
    }
    return res;
}

int
_Py_Initialize(void)
{
    static struct _inittab modules[] = {
        {"pyxul", &PyInit_pyxul},
        {"_xpcom", &PyInit__xpcom},
        {nullptr}
    };

    if (!PyImport_ExtendInittab(modules)) {
        Py_InitializeEx(0);
    }
    return Py_IsInitialized() ? (_Py_InitExceptHook() ? 0 : 1) : 0;
}


void
_Py_Finalize(void)
{
    if (Py_FinalizeEx()) {
        fprintf(
            stderr,
            "error: flushing buffered data failed while finalizing Python.\n"
        );
    }
}


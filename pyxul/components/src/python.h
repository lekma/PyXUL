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


#ifndef __pyxul_python_h__
#define __pyxul_python_h__


#include "pyxul/python.h"


namespace pyxul {


    class MOZ_STACK_CLASS AutoGILState final {
        public:
            AutoGILState() {
                mState = PyGILState_Ensure();
            }

            ~AutoGILState() {
                PyGILState_Release(mState);
            }

        private:
            PyGILState_STATE mState;
    };


} // namespace pyxul


/* typecheck ---------------------------------------------------------------- */

#define _PyType_TypeCheck(a, b) \
    ((a) == (b) || PyType_IsSubtype((a), (b)))

#define _PyType_IsDict(tp) _PyType_TypeCheck((tp), &PyDict_Type)

#define _PyType_IsTuple(tp) _PyType_TypeCheck((tp), &PyTuple_Type)

#define _PyType_IsList(tp) _PyType_TypeCheck((tp), &PyList_Type)

#define _PyType_IsAnySet(tp) \
    ( \
        _PyType_TypeCheck((tp), &PySet_Type) || \
        _PyType_TypeCheck((tp), &PyFrozenSet_Type) \
    )


#define _PyType_IsIterator(tp) \
    (tp->tp_iternext && tp->tp_iternext != &_PyObject_NextNotImplemented)

#define _PyType_IsMapping(tp) \
    (tp->tp_as_mapping && tp->tp_as_mapping->mp_subscript)

#define _PyType_IsSequence(tp) \
    (tp->tp_as_sequence && tp->tp_as_sequence->sq_item)


/* utils -------------------------------------------------------------------- */

int _PyType_ReadyWithBase(PyTypeObject *type, PyTypeObject *base);

int _PyModule_AddObject(PyObject *module, const char *name, PyObject *object);

void _Py_Collect(void);

PyObject *_PyObject_Vars(PyObject *self);

PyObject *_PyObject_Dir(PyObject *self);

PyObject *_PySequence_Fast(PyObject *obj, const char *message, Py_ssize_t len);

int _PyMapping_Clear(PyObject *self);

int _PySlice_GetIndices(
    PyObject *slice, Py_ssize_t length,
    Py_ssize_t *start, Py_ssize_t *stop, Py_ssize_t *slicelength
);

int _PyIndex_AsSsize_t(PyObject *index, Py_ssize_t length, Py_ssize_t *result);

uint32_t _PySsize_tAsUint32_t(Py_ssize_t value);


/* error helpers ------------------------------------------------------------ */

long _PyTraceBack_GetLimit(void);

PyObject *_Py_GetSourceLine(
    PyObject *filename, PyObject *lineno, PyObject *globals = nullptr
);

PyObject *_PyFrame_GetSourceLine(PyFrameObject *frame, int lineno);


/* execute/import ----------------------------------------------------------- */

int _PyExecute_File(FILE *fp, PyObject *path, PyObject *globals);

PyObject *_PyImport_File(FILE *fp, PyObject *path, const char *name);


/* setup -------------------------------------------------------------------- */

int _Py_Setup(const char *dist, const char *site);


/* initialization/finalization ---------------------------------------------- */

int _Py_Initialize(void);

void _Py_Finalize(void);


#endif // __pyxul_python_h__


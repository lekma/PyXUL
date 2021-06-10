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


#ifndef __pyxul_include_python_h__
#define __pyxul_include_python_h__


#pragma GCC visibility push(default)
#define PY_SSIZE_T_CLEAN
#include "Python.h"
#include "traceback.h"
#include "frameobject.h"
#pragma GCC visibility pop


#define PY_ENSURE_SUCCESS(res, ret, ...) \
    do { \
        nsresult _rv = res; \
        if (NS_FAILED(_rv)) { \
            PyErr_Format(__VA_ARGS__); \
            return ret; \
        } \
    } while (0)


#define PY_ENSURE_TRUE(x, ret, ...) \
    do { \
        if (MOZ_UNLIKELY(!(x))) { \
            PyErr_Format(__VA_ARGS__); \
            return ret; \
        } \
    } while (0)


#endif // __pyxul_include_python_h__


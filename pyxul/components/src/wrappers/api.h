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


#ifndef __pyxul_wrappers_api_h__
#define __pyxul_wrappers_api_h__


#include "pyxul/jsapi.h"
#include "pyxul/python.h"


namespace pyxul::wrappers {


    namespace pyjs {


        bool Check(PyObject *aPyObject);
        PyObject *WrapObject(
            JSContext *aCx, JS::MutableHandleObject aJSObject,
            const JS::HandleObject &aThis = nullptr
        );
        PyObject *Wrap(JSContext *aCx, const JS::HandleValue &aJSValue);


        bool Initialize();


    } // namespace pyjs


    namespace jspy {


        bool Check(JS::MutableHandleObject aJSObject);
        JSObject *WrapObject(JSContext *aCx, PyObject *aObject);
        JS::Value Wrap(JSContext *aCx, PyObject *aPyValue);


        bool Initialize();
        void Finalize();


    } // namespace jspy


} // namespace pyxul::wrappers


#endif // __pyxul_wrappers_api_h__


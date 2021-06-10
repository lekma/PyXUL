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


#ifndef __pyxul_errors_h__
#define __pyxul_errors_h__


#include "pyxul/python.h"
#include "pyxul/xpcom_base.h"

#include "nsIException.h"
#include "nsIScriptError.h"


namespace pyxul::errors {


    already_AddRefed<nsIException> Exception(
        JSContext *aCx, PyObject *aValue, nsIStackFrame *aLocation,
        nsIException *aMaybeCause
    );

    already_AddRefed<nsIScriptError> Warning(
        PyObject *aMessage, PyObject *aCategory, PyObject *aFilename,
        PyObject *aLineno, PyObject *aLine
    );


    extern PyObject *Error;
    extern PyObject *XPCOMError;
    extern PyObject *JSError;
    extern PyObject *JSWarning;


    bool Initialize();
    void Finalize();


} // namespace pyxul::errors


#endif // __pyxul_errors_h__


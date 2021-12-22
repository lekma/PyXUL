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


#ifndef __pyxul_xpc_h__
#define __pyxul_xpc_h__


#include "pyxul/jsapi.h"
#include "pyxul/python.h"
#include "pyxul/xpcom.h"

#include "nsIDOMWindow.h"
#include "nsIURI.h"

#include "mozilla/FileLocation.h"


namespace pyxul {


    class MOZ_STACK_CLASS AutoReporter final {
        public:
            AutoReporter(JSContext *aCx);
            ~AutoReporter();

        private:
            JSContext *mCx;
            JS::WarningReporter mWarningReporter;

            bool ReportError(JS::HandleObject aError);

            static void ReportWarning(JSContext *aCx, JSErrorReport *aReport);
    };


    namespace xpc {

        JSObject *Unwrap(JSObject *aObject);
        bool WrapJS(
            JSContext *aCx, JSObject *aObject, const nsIID &aIID, void **aResult
        );

        PyObject *GetJSGlobalObject();

        bool ImportModule(const char *aUri, PyObject *aTarget);

        bool ReportError();
        bool SetException(
            PyObject *aType, PyObject *aValue, PyObject *aTraceback
        );
        bool ReportWarning(
            PyObject *aMessage, PyObject *aCategory, PyObject *aFilename,
            PyObject *aLineno, PyObject *aLine
        );
        void ThrowResult(nsresult aRv);


        nsresult Execute(
            nsIDOMWindow *aDOMWindow,
            const nsACString &aSrc, const char *aOriginCharset, nsIURI *aBaseURI
        );
        void Cleanup(nsIDOMWindow *aDOMWindow);
        const mozilla::Module *LoadModule(mozilla::FileLocation &aFileLocation);


        bool Initialize();
        void Finalize();


    } // namespace xpc


} // namespace pyxul


#endif // __pyxul_xpc_h__


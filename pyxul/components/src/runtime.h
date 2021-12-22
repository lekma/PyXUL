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


#ifndef __pyxul_runtime_h__
#define __pyxul_runtime_h__


#include "pyxul/jsapi.h"
#include "pyxul/xpcom.h"

#include "mozilla/ModuleLoader.h"
#include "nsIObserver.h"
#include "pyIRuntime.h"

#include "prlink.h"


/* 741fc250-fb96-49b5-b9c8-58377de3b43c */
#define PY_RUNTIME_CID \
    { \
        0x741fc250, 0xfb96, 0x49b5, \
        { \
            0xb9, 0xc8, 0x58, 0x37, 0x7d, 0xe3, 0xb4, 0x3c \
        } \
    }

#define PY_RUNTIME_CONTRACTID "@python.org/runtime;1"


namespace pyxul {


    class pyRuntime final : public pyIRuntime,
                            public nsIObserver,
                            public mozilla::ModuleLoader {
        public:
            NS_DECL_ISUPPORTS
            NS_DECL_PYIRUNTIME
            NS_DECL_NSIOBSERVER

            // mozilla::ModuleLoader
            const mozilla::Module *LoadModule(
                mozilla::FileLocation &aFileLocation
            );


            static JSObject *GetJSGlobal(JSContext *aCx);

            static already_AddRefed<pyRuntime> GetRuntime();

        private:
            pyRuntime();
            ~pyRuntime();


            bool Initialize();
            void Finalize();


            PRLibrary *mPythonLibrary;
            JS::PersistentRootedObject mJSGlobal;
            nsTArray<uint64_t> mWindowIDs;

            static mozilla::StaticRefPtr<pyRuntime> sRuntime;
    };


} // namespace pyxul


#endif // __pyxul_runtime_h__


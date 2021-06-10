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


#ifndef __pyxul_modules_h__
#define __pyxul_modules_h__


#include "pyxul/python.h"
#include "pyxul/xpcom.h"

#include "xpcIJSGetFactory.h"

#include "mozilla/Module.h"

#include "nsDataHashtable.h"


namespace pyxul::modules {


    class Module final : public mozilla::Module {
        public:
            typedef nsDataHashtable<nsCStringHashKey, Module *> ModuleCache;
            static ModuleCache Modules;

            Module();
            ~Module();

            bool Init(const char *aPath, nsIFile *aFile);
            void Clear();

            static already_AddRefed<nsIFactory> GetFactory(
                const mozilla::Module &module,
                const mozilla::Module::CIDEntry &entry
            );

            static const Module *Load(
                const nsCString &aPath, nsIFile *aFile
            );

        private:
            PyObject *mModule;
            nsCOMPtr<xpcIJSGetFactory> mGetFactory;
    };


    void Finalize();


} // namespace pyxul::modules


#endif // __pyxul_modules_h__


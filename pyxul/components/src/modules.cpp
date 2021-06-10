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


#include "modules.h"

#include "errors.h"
#include "python.h"
#include "xpc.h"

#include "wrappers/api.h"

#include "nsNetUtil.h"


using namespace pyxul::wrappers;


namespace pyxul::modules {


namespace { // anonymous


static bool
__wrap__(PyObject *aObject, const nsIID &aIID, void **aResult)
{
    bool result = false;

    AutoJSContext aCx;
    AutoReporter ar(aCx);

    JS::RootedObject aJSObject(aCx, jspy::WrapObject(aCx, aObject));
    if (
        aJSObject &&
        !(result = xpc::WrapJS(aCx, aJSObject, aIID, aResult)) &&
        !PyErr_Occurred()
    ) {
        PyErr_Format(errors::XPCOMError, "Failed to wrap object: %S", aObject);
    }
    return result;
}


static bool
__basename__(nsIFile *aFile, nsACString &aName)
{
    nsresult rv = NS_ERROR_FAILURE;
    nsCOMPtr<nsIURI> aURI;
    nsCOMPtr<nsIURL> aURL;

    rv = NS_NewFileURI(getter_AddRefs(aURI), aFile);
    PY_ENSURE_SUCCESS(
        rv, false, errors::XPCOMError, "Failed to create URL from file"
    );
    aURL = do_QueryInterface(aURI, &rv);
    PY_ENSURE_SUCCESS(
        rv, false, errors::XPCOMError, "Failed to QueryInterface to nsIURL"
    );
    rv = aURL->GetFileBaseName(aName);
    PY_ENSURE_SUCCESS(
        rv, false, errors::XPCOMError, "Failed to get base name from url"
    );
    return true;
}


} // namespace anonymous


/* --------------------------------------------------------------------------
   Module implementation
   -------------------------------------------------------------------------- */

// Module::Modules
Module::ModuleCache Module::Modules;


Module::Module(): mozilla::Module()
{
    mVersion = mozilla::Module::kVersion;
    mCIDs = nullptr;
    mContractIDs = nullptr;
    mCategoryEntries = nullptr;
    getFactoryProc = GetFactory;
    loadProc = nullptr;
    unloadProc = nullptr;
    selector = Module::MAIN_PROCESS_ONLY;

    mModule = nullptr;
    mGetFactory = nullptr;
}


Module::~Module()
{
    Clear();
}


bool
Module::Init(const char *aPath, nsIFile *aFile)
{
    _Py_IDENTIFIER(NSGetFactory);
    nsCString aName;
    FILE *fp;
    PyObject *path = nullptr, *aGetFactory = nullptr;
    bool result = false;

    if (__basename__(aFile, aName)) {
        if (NS_FAILED(aFile->OpenANSIFileDesc("r", &fp))) {
            PyErr_SetFromErrnoWithFilename(PyExc_OSError, aPath);
        }
        else if (!(path = PyUnicode_DecodeFSDefault(aPath))) { // +1
            fclose(fp);
        }
        else {
            if ((mModule = _PyImport_File(fp, path, aName.get()))) {
                if ((aGetFactory = _PyObject_GetAttrId(mModule, &PyId_NSGetFactory))) { // +1
                    if (PyCallable_Check(aGetFactory)) {
                        result = __wrap__(
                            aGetFactory, NS_GET_IID(xpcIJSGetFactory),
                            getter_AddRefs(mGetFactory)
                        );
                    }
                    else {
                        PyErr_SetString(
                            PyExc_TypeError, "NSGetFactory must be callable"
                        );
                    }
                    Py_DECREF(aGetFactory); // -1
                }
            }
            Py_DECREF(path); // -1
            fclose(fp);
        }
    }
    return result;
}


void
Module::Clear()
{
    mGetFactory = nullptr;
    Py_CLEAR(mModule);
}


already_AddRefed<nsIFactory>
Module::GetFactory(
    const mozilla::Module &module, const mozilla::Module::CIDEntry &entry
)
{
    AutoGILState ags; // XXX: important

    const Module &self = static_cast<const Module &>(module);
    nsCOMPtr<nsIFactory> aFactory;

    if (NS_FAILED(self.mGetFactory->Get(*entry.cid, getter_AddRefs(aFactory)))) {
        return nullptr;
    }
    return aFactory.forget();
}


const Module *
Module::Load(const nsCString &aPath, nsIFile *aFile)
{
    Module *aModule = nullptr;

    if (!Modules.Get(aPath, &aModule)) {
        if (!(aModule = new (std::nothrow) Module())) {
            PyErr_NoMemory();
        }
        else if (!aModule->Init(aPath.get(), aFile)) {
            delete aModule;
            aModule = nullptr;
        }
        else {
            Modules.Put(aPath, aModule);
        }
    }
    return aModule;
}


/* Initialize/Finalize ------------------------------------------------------ */

void
Finalize()
{
    // Modules are intentionally leaked, but still cleared.
    for (auto iter = Module::Modules.Iter(); !iter.Done(); iter.Next()) {
        iter.Data()->Clear();
        iter.Remove();
    }
}


} // namespace pyxul::modules

